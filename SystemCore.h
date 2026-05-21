// SystemCore.h
#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <unordered_map>

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
    ULONGLONG ioReadBytes;
    ULONGLONG ioWriteBytes;
    ULONGLONG ioDiskRead;    // Disk read rate (bytes)
    ULONGLONG ioDiskWrite;   // Disk write rate (bytes)
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
    static bool EnableDebugPrivilege();
    static std::vector<ProcessInfo> GetProcesses();
    static DWORD KillProcess(DWORD pid);

private:
    // Зберігаємо попередні CPU-часи для обчислення per-process CPU%
    struct PrevCpuData {
        ULONGLONG kernelTime = 0;
        ULONGLONG userTime = 0;
        ULONGLONG timestamp = 0; // system time snapshot
    };
    static std::unordered_map<DWORD, PrevCpuData> prevCpuMap_;
    static ULONGLONG prevSystemTime_;
};