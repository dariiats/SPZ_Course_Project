// SystemCore.cpp
#include "SystemCore.h"
#include <tlhelp32.h>
#include <psapi.h>
#include <sddl.h>
#include <string>

#pragma comment(lib, "advapi32.lib")

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
            info.ioReadRate = 0.0;
            info.ioWriteRate = 0.0;

            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (hProcess != NULL) {
                // Memory info
                PROCESS_MEMORY_COUNTERS_EX pmc = { sizeof(PROCESS_MEMORY_COUNTERS_EX) };
                if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                    info.memoryUsage = pmc.WorkingSetSize;           // RES
                    info.virtualMemory = pmc.PrivateUsage;           // VIRT (private bytes)
                    // SHR approximation: WorkingSet - Private
                    info.sharedMemory = (pmc.WorkingSetSize > pmc.PrivateUsage)
                        ? (pmc.WorkingSetSize - pmc.PrivateUsage) : 0;
                }

                // Priority
                info.priority = GetPriorityClass(hProcess);
                // Map priority class to numeric value like htop
                switch (info.priority) {
                    case REALTIME_PRIORITY_CLASS: info.priority = -20; info.niceness = -20; break;
                    case HIGH_PRIORITY_CLASS: info.priority = -10; info.niceness = -10; break;
                    case ABOVE_NORMAL_PRIORITY_CLASS: info.priority = -5; info.niceness = -5; break;
                    case NORMAL_PRIORITY_CLASS: info.priority = 20; info.niceness = 0; break;
                    case BELOW_NORMAL_PRIORITY_CLASS: info.priority = 30; info.niceness = 10; break;
                    case IDLE_PRIORITY_CLASS: info.priority = 39; info.niceness = 19; break;
                    default: info.priority = 20; info.niceness = 0; break;
                }

                // CPU time
                FILETIME createTime, exitTime, kernelTime, userTime;
                if (GetProcessTimes(hProcess, &createTime, &exitTime, &kernelTime, &userTime)) {
                    ULONGLONG kt = (static_cast<ULONGLONG>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
                    ULONGLONG ut = (static_cast<ULONGLONG>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
                    info.cpuTime = (kt + ut) / 10000; // Convert 100ns units to ms
                }

                // MEM%
                double resMB = info.memoryUsage / (1024.0 * 1024.0);
                info.memPercent = (totalPhysMB > 0) ? (resMB / totalPhysMB) * 100.0 : 0.0;

                // State
                info.state = GetProcessState(hProcess);

                // User name
                info.userName = GetProcessUserName(hProcess);

                // I/O
                IO_COUNTERS ioCounters;
                if (GetProcessIoCounters(hProcess, &ioCounters)) {
                    info.ioReadBytes = ioCounters.ReadTransferCount;
                    info.ioWriteBytes = ioCounters.WriteTransferCount;
                }

                CloseHandle(hProcess);
            }
            processList.push_back(info);
        } while (Process32NextW(hSnapshot, &pe));
    }
    CloseHandle(hSnapshot);
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

void ProcessMonitor::UpdateRates(std::vector<ProcessInfo>& processes) {
    ULONGLONG now = GetTickCount64();
    int numCpus = 1;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    numCpus = (int)si.dwNumberOfProcessors;

    for (auto& proc : processes) {
        // Отримуємо поточний CPU time для цього процесу
        ULONGLONG curKernel = 0, curUser = 0;
        HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, proc.pid);
        if (hProc) {
            FILETIME createTime, exitTime, kernelTime, userTime;
            if (GetProcessTimes(hProc, &createTime, &exitTime, &kernelTime, &userTime)) {
                curKernel = (static_cast<ULONGLONG>(kernelTime.dwHighDateTime) << 32) | kernelTime.dwLowDateTime;
                curUser = (static_cast<ULONGLONG>(userTime.dwHighDateTime) << 32) | userTime.dwLowDateTime;
            }
            CloseHandle(hProc);
        }

        auto it = prevStates.find(proc.pid);
        if (it != prevStates.end()) {
            PrevProcessState& prev = it->second;
            ULONGLONG elapsed = now - prev.timestamp;
            if (elapsed > 0) {
                double elapsedSec = elapsed / 1000.0;

                // CPU% = (delta CPU time / elapsed / numCPUs) * 100
                ULONGLONG deltaCpu = (curKernel + curUser) - (prev.cpuKernel + prev.cpuUser);
                // deltaCpu is in 100ns units, elapsed is in ms
                double cpuSec = deltaCpu / 10000000.0; // to seconds
                proc.cpuPercent = (cpuSec / elapsedSec) / numCpus * 100.0;
                if (proc.cpuPercent < 0) proc.cpuPercent = 0;
                if (proc.cpuPercent > 100) proc.cpuPercent = 100;

                // I/O rate
                ULONGLONG deltaRead = proc.ioReadBytes - prev.ioRead;
                ULONGLONG deltaWrite = proc.ioWriteBytes - prev.ioWrite;
                proc.ioReadRate = deltaRead / elapsedSec;
                proc.ioWriteRate = deltaWrite / elapsedSec;
            }
        } else {
            proc.cpuPercent = 0.0;
            proc.ioReadRate = 0.0;
            proc.ioWriteRate = 0.0;
        }

        // Зберігаємо поточний стан
        prevStates[proc.pid] = { curKernel, curUser, proc.ioReadBytes, proc.ioWriteBytes, now };
    }

    // Видаляємо процеси які зникли
    std::unordered_map<DWORD, PrevProcessState> newStates;
    for (auto& proc : processes) {
        auto it = prevStates.find(proc.pid);
        if (it != prevStates.end()) newStates[proc.pid] = it->second;
    }
    prevStates = newStates;
}
