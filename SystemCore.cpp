// SystemCore.cpp
#include "SystemCore.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <sddl.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

// Статичні члени SystemManager
std::unordered_map<DWORD, SystemManager::PrevCpuData> SystemManager::prevCpuMap_;
ULONGLONG SystemManager::prevSystemTime_ = 0;
std::unordered_map<DWORD, SystemManager::PrevIoData> SystemManager::prevIoMap_;

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
    Update(); // Перший знімок
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
            info.name = pe.szExeFile;
            info.priority = 0;
            info.niceness = 0;
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

                // Priority
                info.priority = GetPriorityClass(hProcess);
                switch (info.priority) {
                    case REALTIME_PRIORITY_CLASS: info.priority = -20; info.niceness = -20; break;
                    case HIGH_PRIORITY_CLASS: info.priority = -10; info.niceness = -10; break;
                    case ABOVE_NORMAL_PRIORITY_CLASS: info.priority = -5; info.niceness = -5; break;
                    case NORMAL_PRIORITY_CLASS: info.priority = 20; info.niceness = 0; break;
                    case BELOW_NORMAL_PRIORITY_CLASS: info.priority = 30; info.niceness = 10; break;
                    case IDLE_PRIORITY_CLASS: info.priority = 39; info.niceness = 19; break;
                    default: info.priority = 20; info.niceness = 0; break;
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
                            // CPU% нормалізований до 0-100% (systemTimeDelta вже сумарний по всіх ядрах)
                            info.cpuPercent = (static_cast<double>(procDelta) / static_cast<double>(systemTimeDelta)) * 100.0;
                            if (info.cpuPercent < 0.0) info.cpuPercent = 0.0;
                            if (info.cpuPercent > 100.0) info.cpuPercent = 100.0;
                        }
                    }

                    // Зберігаємо поточні значення для наступного виклику
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

                    // Rate = (current - previous) / deltaTime
                    auto ioIt = prevIoMap_.find(pe.th32ProcessID);
                    if (ioIt != prevIoMap_.end() && prevSystemTime_ > 0 && systemTimeDelta > 0) {
                        // systemTimeDelta в 100ns units, конвертуємо в секунди
                        double deltaSec = static_cast<double>(systemTimeDelta) / 10000000.0;
                        if (deltaSec > 0.0) {
                            ULONGLONG readDelta = ioCounters.ReadTransferCount - ioIt->second.readBytes;
                            ULONGLONG writeDelta = ioCounters.WriteTransferCount - ioIt->second.writeBytes;
                            info.ioDiskRead = static_cast<ULONGLONG>(readDelta / deltaSec);
                            info.ioDiskWrite = static_cast<ULONGLONG>(writeDelta / deltaSec);
                        }
                    } else {
                        info.ioDiskRead = 0;
                        info.ioDiskWrite = 0;
                    }
                    // Зберігаємо поточні IO для наступного виклику
                    newIoMap[pe.th32ProcessID] = { ioCounters.ReadTransferCount, ioCounters.WriteTransferCount };
                }

                CloseHandle(hProcess);
            }
            processList.push_back(info);
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);

    // Оновлюємо збережені дані для наступної ітерації
    prevCpuMap_ = std::move(newCpuMap);
    prevIoMap_ = std::move(newIoMap);
    prevSystemTime_ = curSystemTime;

    return processList;
}

DWORD SystemManager::KillProcess(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess == NULL) return GetLastError();
    DWORD result = 0;
    if (!TerminateProcess(hProcess, 0)) result = GetLastError();
    CloseHandle(hProcess);
    return result;
}

DWORD SystemManager::ChangeProcessPriority(DWORD pid, bool increase) {
    HANDLE hProcess = OpenProcess(PROCESS_SET_INFORMATION | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == NULL) return GetLastError();

    // Отримуємо поточний клас пріоритету
    DWORD currentClass = GetPriorityClass(hProcess);
    if (currentClass == 0) {
        DWORD err = GetLastError();
        CloseHandle(hProcess);
        return err;
    }

    // Порядок класів пріоритету (від низького до високого)
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