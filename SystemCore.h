// SystemCore.h
#pragma once
#include <windows.h>
#include <vector>
#include <string>

struct ProcessInfo {
    DWORD pid;
    std::wstring name;
    SIZE_T memoryUsage;
};

class CpuMonitor {
private:
    ULONGLONG prevIdleTime = 0;
    ULONGLONG prevKernelTime = 0;
    ULONGLONG prevUserTime = 0;
    ULONGLONG FileTimeToQuadWord(const FILETIME& ft);
public:
    CpuMonitor();
    void Reset();
    double GetCpuUsage();
};

class SystemManager {
public:
    static bool EnableDebugPrivilege(); // Увімкнення прав для перегляду Chrome/системних процесів
    static std::vector<ProcessInfo> GetProcesses();
    static DWORD KillProcess(DWORD pid);
};