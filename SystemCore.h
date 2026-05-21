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
    double cpuPercent;       // CPU% (реальний, за інтервал)
    double memPercent;       // MEM%
    ULONGLONG cpuTime;       // TIME+ (in milliseconds)
    wchar_t state;           // S (R/S/Z/T)
    ULONGLONG ioReadBytes;   // Cumulative read
    ULONGLONG ioWriteBytes;  // Cumulative write
    double ioReadRate;       // Bytes/s read (rate)
    double ioWriteRate;      // Bytes/s write (rate)
};

// Зберігає попередній стан процесу для обчислення rate
struct PrevProcessState {
    ULONGLONG cpuKernel;
    ULONGLONG cpuUser;
    ULONGLONG ioRead;
    ULONGLONG ioWrite;
    ULONGLONG timestamp; // GetTickCount64
};

class ProcessMonitor {
private:
    std::unordered_map<DWORD, PrevProcessState> prevStates;
public:
    void UpdateRates(std::vector<ProcessInfo>& processes);
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
};