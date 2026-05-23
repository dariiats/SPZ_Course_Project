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
    int priority;            // PRI (Windows base priority)
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

// Per-core CPU моніторинг через NtQuerySystemInformation
class PerCoreCpuMonitor {
public:
    PerCoreCpuMonitor();
    void Update();                          // Оновити дані (викликати з інтервалом)
    int GetCoreCount() const;
    double GetCoreUsage(int coreIdx) const; // Повертає % для конкретного ядра
    const std::vector<double>& GetAllCoreUsages() const;

private:
    struct CoreTimes {
        ULONGLONG idle = 0;
        ULONGLONG kernel = 0;
        ULONGLONG user = 0;
    };
    int coreCount_ = 0;
    std::vector<CoreTimes> prevTimes_;
    std::vector<double> coreUsages_;
};

class SystemManager {
public:
    static bool EnableDebugPrivilege();
    static std::vector<ProcessInfo> GetProcesses();
    static DWORD KillProcess(DWORD pid);
    static DWORD ChangeProcessPriority(DWORD pid, bool increase);

private:
    // Зберігаємо попередні CPU-часи для обчислення per-process CPU%
    struct PrevCpuData {
        ULONGLONG kernelTime = 0;
        ULONGLONG userTime = 0;
        ULONGLONG timestamp = 0; // system time snapshot
    };
    static std::unordered_map<DWORD, PrevCpuData> prevCpuMap_;
    static ULONGLONG prevSystemTime_;

    // Зберігаємо попередні IO-лічильники для обчислення rate
    struct PrevIoData {
        ULONGLONG readBytes = 0;
        ULONGLONG writeBytes = 0;
    };
    static std::unordered_map<DWORD, PrevIoData> prevIoMap_;
};