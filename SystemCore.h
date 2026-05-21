// SystemCore.h
#pragma once
#ifndef SYSTEMCORE_H
#define SYSTEMCORE_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <vector>
#include <string>

struct ProcessInfo {
    DWORD pid;
    std::wstring name;
    std::wstring userName;
    SIZE_T memoryUsage;      // RES (Working Set)
    SIZE_T virtualMemory;    // VIRT
    SIZE_T sharedMemory;     // SHR
    int priority;            // PRI
    int niceness;            // NI
    double cpuPercent;       // CPU%
    double memPercent;       // MEM%
    ULONGLONG cpuTime;       // TIME+ (in milliseconds)
    wchar_t state;           // S (R/S/Z/T)
    // I/O fields
    ULONGLONG ioReadBytes;   // Disk Read
    ULONGLONG ioWriteBytes;  // Disk Write
    ULONGLONG ioReadOps;     // Read operations count
    ULONGLONG ioWriteOps;    // Write operations count
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

#endif // SYSTEMCORE_H