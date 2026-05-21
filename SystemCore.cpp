// SystemCore.cpp
#include "SystemCore.h"
#include <tlhelp32.h>
#include <psapi.h>

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

    PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(hSnapshot, &pe)) {
        do {
            ProcessInfo info = { pe.th32ProcessID, pe.szExeFile, 0 };
            HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (hProcess != NULL) {
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(hProcess, &pmc, sizeof(pmc))) {
                    info.memoryUsage = pmc.WorkingSetSize;
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