// SystemCore.cpp
#include "SystemCore.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <sddl.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

// Статичнi члени SystemManager
std::unordered_map<DWORD, SystemManager::PrevCpuData> SystemManager::prevCpuMap_;
ULONGLONG SystemManager::prevSystemTime_ = 0;
std::unordered_map<DWORD, SystemManager::PrevIoData> SystemManager::prevIoMap_;

// Per-thread CPU tracking
static std::unordered_map<DWORD, SystemManager::PrevCpuData> g_prevThreadCpuMap;

static std::wstring GetProcessUserName(HANDLE hProcess) {
    HANDLE hToken = NULL;
    if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) return L"-";

    DWORD dwSize = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &dwSize);
    if (dwSize == 0) { CloseHandle(hToken); return L"-"; }

    std::vector<BYTE> buffer(dwSize);
    PTOKEN_USER pTokenUser = reinterpret_cast<PTOKEN_USER>(buffer.data());
    if (!GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
        CloseHandle(hToken);
        return L"-";
    }

    wchar_t userName[256] = {}, domainName[256] = {};
    DWORD userLen = 256, domainLen = 256;
    SID_NAME_USE sidType;
    if (!LookupAccountSidW(NULL, pTokenUser->User.Sid, userName, &userLen, domainName, &domainLen, &sidType)) {
        CloseHandle(hToken);
        return L"-";
    }
    CloseHandle(hToken);
    return std::wstring(userName);
}

static wchar_t GetProcessState(HANDLE hProcess) {
    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProcess, &exitCode)) {
        if (exitCode == STILL_ACTIVE) return L'R';
    }
    return L'S';
}

ULONGLONG CpuMonitor::FileTimeToQuadWord(const FILETIME& ft) {
    return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

CpuMonitor::CpuMonitor() { Reset(); }

void CpuMonitor::Reset() {
    FILETIME idle, kernel, user;
    if (GetSystemTimes(&idle, &kernel, &user)) {
        prevIdleTime = FileTimeToQuadWord(idle);
        prevKernelTime = FileTimeToQuadWord(kernel);
        prevUserTime = FileTimeToQuadWord(user);
    }
}

double CpuMonitor::GetCpuUsage() {
    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return 0.0;
    ULONGLONG curIdle = FileTimeToQuadWord(idle), curKernel = FileTimeToQuadWord(kernel), curUser = FileTimeToQuadWord(user);
    ULONGLONG diffIdle = curIdle - prevIdleTime, diffKernel = curKernel - prevKernelTime, diffUser = curUser - prevUserTime;
    ULONGLONG diffTotal = diffKernel + diffUser;
    prevIdleTime = curIdle; prevKernelTime = curKernel; prevUserTime = curUser;
    if (diffTotal == 0) return 0.0;
    double cpuPercent = (1.0 - (static_cast<double>(diffIdle) / diffTotal)) * 100.0;
    return (cpuPercent < 0.0) ? 0.0 : (cpuPercent > 100.0) ? 100.0 : cpuPercent;
}

// === PerCoreCpuMonitor: реальне per-core навантаження ===
// Використовує NtQuerySystemInformation з SystemProcessorPerformanceInformation

typedef struct _SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION {
    LARGE_INTEGER IdleTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER Reserved1[2];
    ULONG Reserved2;
} SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION;

// SystemProcessorPerformanceInformation = 8
typedef NTSTATUS(WINAPI* NtQuerySystemInformationFn)(
    ULONG SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength
);

static NtQuerySystemInformationFn g_NtQuerySystemInformation = nullptr;

static bool InitNtQuery() {
    if (g_NtQuerySystemInformation) return true;
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return false;
    g_NtQuerySystemInformation = (NtQuerySystemInformationFn)GetProcAddress(hNtdll, "NtQuerySystemInformation");
    return g_NtQuerySystemInformation != nullptr;
}

PerCoreCpuMonitor::PerCoreCpuMonitor() {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    coreCount_ = si.dwNumberOfProcessors;
    prevTimes_.resize(coreCount_);
    coreUsages_.resize(coreCount_, 0.0);
    Update(); // Перший знiмок
}

void PerCoreCpuMonitor::Update() {
    if (!InitNtQuery()) return;

    std::vector<SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION> info(coreCount_);
    ULONG returnLength = 0;
    NTSTATUS status = g_NtQuerySystemInformation(
        8, // SystemProcessorPerformanceInformation
        info.data(),
        static_cast<ULONG>(sizeof(SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION) * coreCount_),
        &returnLength
    );

    if (status != 0) return; // STATUS_SUCCESS = 0

    for (int i = 0; i < coreCount_; ++i) {
        ULONGLONG idle = info[i].IdleTime.QuadPart;
        ULONGLONG kernel = info[i].KernelTime.QuadPart;
        ULONGLONG user = info[i].UserTime.QuadPart;

        ULONGLONG diffIdle = idle - prevTimes_[i].idle;
        ULONGLONG diffKernel = kernel - prevTimes_[i].kernel;
        ULONGLONG diffUser = user - prevTimes_[i].user;
        ULONGLONG diffTotal = diffKernel + diffUser;

        if (diffTotal > 0) {
            double usage = (1.0 - static_cast<double>(diffIdle) / static_cast<double>(diffTotal)) * 100.0;
            if (usage < 0.0) usage = 0.0;
            if (usage > 100.0) usage = 100.0;
            coreUsages_[i] = usage;
        } else {
            coreUsages_[i] = 0.0;
        }

        prevTimes_[i].idle = idle;
        prevTimes_[i].kernel = kernel;
        prevTimes_[i].user = user;
    }
}

int PerCoreCpuMonitor::GetCoreCount() const {
    return coreCount_;
}

double PerCoreCpuMonitor::GetCoreUsage(int coreIdx) const {
    if (coreIdx < 0 || coreIdx >= coreCount_) return 0.0;
    return coreUsages_[coreIdx];
}

const std::vector<double>& PerCoreCpuMonitor::GetAllCoreUsages() const {
    return coreUsages_;
}

bool SystemManager::EnableDebugPrivilege() {
    HANDLE hToken;
    LUID luid;
    TOKEN_PRIVILEGES tkp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) return false;
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        CloseHandle(hToken);
        return false;
    }
    tkp.PrivilegeCount = 1;
    tkp.Privileges[0].Luid = luid;
    tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool result = AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(TOKEN_PRIVILEGES), NULL, NULL);
    CloseHandle(hToken);
    return result && (GetLastError() == ERROR_SUCCESS);
}

std::vector<ProcessInfo> SystemManager::GetProcesses() {
    std::vector<ProcessInfo> processList;
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) return processList;

    MEMORYSTATUSEX memStatus = { sizeof(MEMORYSTATUSEX) };
    GlobalMemoryStatusEx(&memStatus);
    double totalPhysMB = memStatus.ullTotalPhys / (1024.0 * 1024.0);

    // Поточний системний час для обчислення дельти CPU
    FILETIME sysIdleTime, sysKernelTime, sysUserTime;
    GetSystemTimes(&sysIdleTime, &sysKernelTime, &sysUserTime);
    ULONGLONG curSystemTime =
        ((static_cast<ULONGLONG>(sysKernelTime.dwHighDateTime) << 32) | sysKernelTime.dwLowDateTime) +
        ((static_cast<ULONGLONG>(sysUserTime.dwHighDateTime) << 32) | sysUserTime.dwLowDateTime);

    ULONGLONG systemTimeDelta = curSystemTime - prevSystemTime_;

    // Wall clock delta для IO rate (в мiлiсекундах)
    static ULONGLONG prevWallClock = 0;
    ULONGLONG curWallClock = GetTickCount64();
    double wallDeltaSec = (prevWallClock > 0) ? (curWallClock - prevWallClock) / 1000.0 : 0.0;
    prevWallClock = curWallClock;

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int numCores = si.dwNumberOfProcessors;

    std::unordered_map<DWORD, PrevCpuData> newCpuMap;
    std::unordered_map<DWORD, PrevIoData> newIoMap;

    PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            ProcessInfo info = {};
            info.pid = pe.th32ProcessID;
            info.parentPid = pe.th32ParentProcessID;
            info.name = pe.szExeFile;
            info.priority = 0;
            info.virtualMemory = 0;
            info.memoryUsage = 0;
            info.sharedMemory = 0;
            info.cpuPercent = 0.0;
            info.memPercent = 0.0;
            info.cpuTime = 0;
            info.state = L'S';
            info.userName = L"-";
            info.ioReadBytes = 0;
            info.ioWriteBytes = 0;
            info.ioDiskRead = 0;
            info.ioDiskWrite = 0;
            info.threadCount = 0;
            info.isThread = false;
            info.tid = 0;

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (hProcess != NULL) {
                // Memory info
                PROCESS_MEMORY_COUNTERS_EX pmc = { sizeof(PROCESS_MEMORY_COUNTERS_EX) };
                if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                    info.memoryUsage = pmc.WorkingSetSize;
                    info.virtualMemory = pmc.PrivateUsage;
                    info.sharedMemory = (pmc.WorkingSetSize > pmc.PrivateUsage)
                        ? (pmc.WorkingSetSize - pmc.PrivateUsage) : 0;
                }

                // Priority — реальний Windows base priority
                DWORD priClass = GetPriorityClass(hProcess);
                switch (priClass) {
                    case REALTIME_PRIORITY_CLASS:       info.priority = 24; break;
                    case HIGH_PRIORITY_CLASS:           info.priority = 13; break;
                    case ABOVE_NORMAL_PRIORITY_CLASS:   info.priority = 10; break;
                    case NORMAL_PRIORITY_CLASS:         info.priority = 8;  break;
                    case BELOW_NORMAL_PRIORITY_CLASS:   info.priority = 6;  break;
                    case IDLE_PRIORITY_CLASS:           info.priority = 4;  break;
                    default:                           info.priority = 8;  break;
                }

                // CPU time + per-process CPU%
                FILETIME createTime, exitTime, kernelTime, userTime;
                if (GetProcessTimes(hProcess, &createTime, &exitTime, &kernelTime, &userTime)) {
                    ULONGLONG kt = (static_cast<ULONGLONG>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
                    ULONGLONG ut = (static_cast<ULONGLONG>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
                    info.cpuTime = (kt + ut) / 10000; // 100ns -> ms

                    // Обчислення CPU% через дельту
                    if (prevSystemTime_ > 0 && systemTimeDelta > 0) {
                        auto it = prevCpuMap_.find(pe.th32ProcessID);
                        if (it != prevCpuMap_.end()) {
                            ULONGLONG procDelta = (kt + ut) - (it->second.kernelTime + it->second.userTime);
                            // CPU% нормалiзований до 0-100% (systemTimeDelta вже сумарний по всiх ядрах)
                            info.cpuPercent = (static_cast<double>(procDelta) / static_cast<double>(systemTimeDelta)) * 100.0;
                            if (info.cpuPercent < 0.0) info.cpuPercent = 0.0;
                            if (info.cpuPercent > 100.0) info.cpuPercent = 100.0;
                        }
                    }

                    // Зберiгаємо поточнi значення для наступного виклику
                    newCpuMap[pe.th32ProcessID] = { kt, ut, curSystemTime };
                }

                // MEM%
                double resMB = info.memoryUsage / (1024.0 * 1024.0);
                info.memPercent = (totalPhysMB > 0) ? (resMB / totalPhysMB) * 100.0 : 0.0;

                // State
                info.state = GetProcessState(hProcess);

                // User name
                info.userName = GetProcessUserName(hProcess);

                // I/O — обчислення rate через дельту
                IO_COUNTERS ioCounters;
                if (GetProcessIoCounters(hProcess, &ioCounters)) {
                    info.ioReadBytes = ioCounters.ReadTransferCount;
                    info.ioWriteBytes = ioCounters.WriteTransferCount;

                    // Rate = (current - previous) / wallClockDelta
                    auto ioIt = prevIoMap_.find(pe.th32ProcessID);
                    if (ioIt != prevIoMap_.end() && wallDeltaSec > 0.0) {
                        ULONGLONG readDelta = ioCounters.ReadTransferCount - ioIt->second.readBytes;
                        ULONGLONG writeDelta = ioCounters.WriteTransferCount - ioIt->second.writeBytes;
                        info.ioDiskRead = static_cast<ULONGLONG>(readDelta / wallDeltaSec);
                        info.ioDiskWrite = static_cast<ULONGLONG>(writeDelta / wallDeltaSec);
                    } else {
                        info.ioDiskRead = 0;
                        info.ioDiskWrite = 0;
                    }
                    // Зберiгаємо поточнi IO для наступного виклику
                    newIoMap[pe.th32ProcessID] = { ioCounters.ReadTransferCount, ioCounters.WriteTransferCount };
                }

                CloseHandle(hProcess);
            }
            processList.push_back(info);
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);

    // Пiдрахунок потокiв для кожного процесу
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te = { sizeof(THREADENTRY32) };
        if (Thread32First(hThreadSnap, &te)) {
            std::unordered_map<DWORD, int> threadCounts;
            do {
                threadCounts[te.th32OwnerProcessID]++;
            } while (Thread32Next(hThreadSnap, &te));
            for (auto& proc : processList) {
                auto it = threadCounts.find(proc.pid);
                if (it != threadCounts.end()) proc.threadCount = it->second;
            }
        }
        CloseHandle(hThreadSnap);
    }

    // Оновлюємо збереженi данi для наступної iтерацiї
    prevCpuMap_ = std::move(newCpuMap);
    prevIoMap_ = std::move(newIoMap);
    prevSystemTime_ = curSystemTime;

    return processList;
}

DWORD SystemManager::KillProcess(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess == NULL) return GetLastError();

    // Перевiряємо чи процес ще живий (не завершився мiж вибором i kill)
    DWORD exitCode = 0;
    if (GetExitCodeProcess(hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
        CloseHandle(hProcess);
        return ERROR_PROCESS_ABORTED; // Процес вже завершений
    }

    DWORD result = 0;
    if (!TerminateProcess(hProcess, 0)) result = GetLastError();
    CloseHandle(hProcess);
    return result;
}

// М'яке завершення — надсилає WM_CLOSE всiм вiкнам процесу
static BOOL CALLBACK EnumWindowsCloseProc(HWND hwnd, LPARAM lParam) {
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == (DWORD)lParam) {
        PostMessage(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

DWORD SystemManager::CloseProcess(DWORD pid) {
    EnumWindows(EnumWindowsCloseProc, (LPARAM)pid);
    return 0;
}

DWORD SystemManager::ChangeProcessPriority(DWORD pid, bool increase) {
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == NULL) return GetLastError();

    // Отримуємо поточний клас прiоритету
    DWORD currentClass = GetPriorityClass(hProcess);
    if (currentClass == 0) {
        DWORD err = GetLastError();
        CloseHandle(hProcess);
        return err;
    }

    // Порядок класiв прiоритету (вiд низького до високого)
    static const DWORD priorityLevels[] = {
        IDLE_PRIORITY_CLASS,
        BELOW_NORMAL_PRIORITY_CLASS,
        NORMAL_PRIORITY_CLASS,
        ABOVE_NORMAL_PRIORITY_CLASS,
        HIGH_PRIORITY_CLASS,
        REALTIME_PRIORITY_CLASS
    };
    static const int numLevels = 6;

    int currentIdx = 2; // default: NORMAL
    for (int i = 0; i < numLevels; ++i) {
        if (priorityLevels[i] == currentClass) { currentIdx = i; break; }
    }

    int newIdx = increase ? (currentIdx + 1) : (currentIdx - 1);
    if (newIdx < 0) newIdx = 0;
    if (newIdx >= numLevels) newIdx = numLevels - 1;

    DWORD result = 0;
    if (!SetPriorityClass(hProcess, priorityLevels[newIdx])) {
        result = GetLastError();
    }
    CloseHandle(hProcess);
    return result;
}

std::vector<ThreadInfo> SystemManager::GetThreadsForProcess(DWORD pid) {
    // Не використовується напряму — див. GetAllThreads()
    return {};
}

std::unordered_map<DWORD, std::vector<ThreadInfo>> SystemManager::GetAllThreads() {
    std::unordered_map<DWORD, std::vector<ThreadInfo>> result;

    FILETIME sysIdleTime, sysKernelTime, sysUserTime;
    GetSystemTimes(&sysIdleTime, &sysKernelTime, &sysUserTime);
    ULONGLONG curSystemTime =
        ((static_cast<ULONGLONG>(sysKernelTime.dwHighDateTime) << 32) | sysKernelTime.dwLowDateTime) +
        ((static_cast<ULONGLONG>(sysUserTime.dwHighDateTime) << 32) | sysUserTime.dwLowDateTime);

    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return result;

    THREADENTRY32 te = { sizeof(THREADENTRY32) };
    if (Thread32First(hSnap, &te)) {
        do {
            ThreadInfo ti = {};
            ti.tid = te.th32ThreadID;
            ti.ownerPid = te.th32OwnerProcessID;
            ti.priority = te.tpBasePri;
            ti.cpuPercent = 0.0;
            ti.cpuTime = 0;
            ti.state = L'S';

            HANDLE hThread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
            if (hThread != NULL) {
                FILETIME createTime, exitTime, kernelTime, userTime;
                if (GetThreadTimes(hThread, &createTime, &exitTime, &kernelTime, &userTime)) {
                    ULONGLONG kt = (static_cast<ULONGLONG>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
                    ULONGLONG ut = (static_cast<ULONGLONG>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
                    ti.cpuTime = (kt + ut) / 10000;

                    if (prevSystemTime_ > 0) {
                        ULONGLONG sysDelta = curSystemTime - prevSystemTime_;
                        auto it = g_prevThreadCpuMap.find(te.th32ThreadID);
                        if (it != g_prevThreadCpuMap.end() && sysDelta > 0) {
                            ULONGLONG threadDelta = (kt + ut) - (it->second.kernelTime + it->second.userTime);
                            ti.cpuPercent = (static_cast<double>(threadDelta) / static_cast<double>(sysDelta)) * 100.0;
                            if (ti.cpuPercent < 0.0) ti.cpuPercent = 0.0;
                            if (ti.cpuPercent > 100.0) ti.cpuPercent = 100.0;
                        }
                    }
                    g_prevThreadCpuMap[te.th32ThreadID] = { kt, ut, curSystemTime };

                    if (ti.cpuPercent > 0.0) ti.state = L'R';
                }
                CloseHandle(hThread);
            }

            result[te.th32OwnerProcessID].push_back(ti);
        } while (Thread32Next(hSnap, &te));
    }
    CloseHandle(hSnap);
    return result;
}