#include "ConsoleUI.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#include <cmath>
#include <mutex>

// === Virtual Terminal Sequences ===
// Foreground colors
#define VT_RESET        L"\x1b[0m"
#define VT_FG_BLACK     L"\x1b[30m"
#define VT_FG_RED       L"\x1b[31m"
#define VT_FG_GREEN     L"\x1b[32m"
#define VT_FG_YELLOW    L"\x1b[33m"
#define VT_FG_BLUE      L"\x1b[34m"
#define VT_FG_MAGENTA   L"\x1b[35m"
#define VT_FG_CYAN      L"\x1b[36m"
#define VT_FG_WHITE     L"\x1b[37m"
#define VT_FG_BRIGHT_RED    L"\x1b[91m"
#define VT_FG_BRIGHT_GREEN  L"\x1b[92m"
#define VT_FG_BRIGHT_CYAN   L"\x1b[96m"
#define VT_FG_BRIGHT_WHITE  L"\x1b[97m"
#define VT_FG_DARKGRAY      L"\x1b[90m"

// Background colors
#define VT_BG_BLACK     L"\x1b[40m"
#define VT_BG_GREEN     L"\x1b[42m"
#define VT_BG_CYAN      L"\x1b[46m"
#define VT_BG_DARKGRAY  L"\x1b[100m"

// Cursor control
#define VT_CURSOR_HOME  L"\x1b[H"
#define VT_CURSOR_HIDE  L"\x1b[?25l"
#define VT_CURSOR_SHOW  L"\x1b[?25h"
#define VT_CLEAR_SCREEN L"\x1b[2J"
#define VT_CLEAR_LINE   L"\x1b[K"

// Отримання поточної ширини консолі
int GetConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return (width > 60) ? width : 60;
}

void ConsoleUI::InitConsole() {
    std::setlocale(LC_ALL, "");

    // Увімкнення Virtual Terminal Processing
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);

    std::wcout << VT_CURSOR_HIDE;
}

void ConsoleUI::ResetCursor() {
    std::wcout << VT_CURSOR_HOME;
}

void ConsoleUI::SetCursorVisibility(bool visible) {
    std::wcout << (visible ? VT_CURSOR_SHOW : VT_CURSOR_HIDE);
}

static void DrawCoreBar(int coreId, double percentage) {
    std::wcout << VT_FG_BRIGHT_CYAN << std::right << std::setw(2) << coreId;
    std::wcout << VT_RESET << L"[";

    int totalBars = 15;
    int filledBars = static_cast<int>((percentage / 100.0) * totalBars);
    const wchar_t* barColor = (percentage > 80.0) ? VT_FG_BRIGHT_RED : VT_FG_BRIGHT_GREEN;

    std::wcout << barColor;
    for (int i = 0; i < totalBars; ++i) {
        if (i < filledBars) std::wcout << L"|";
        else std::wcout << L" ";
    }

    std::wcout << VT_RESET << L" ";
    std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << percentage << L"%";
    std::wcout << L"]  ";
}

static void DrawWideBar(std::wstring label, double used, double total, const wchar_t* barColor) {
    std::wcout << VT_FG_BRIGHT_CYAN << std::left << std::setw(4) << label;
    std::wcout << VT_RESET << L"[";

    int totalBars = 35;
    double percentage = (used / total) * 100.0;
    int filledBars = static_cast<int>((percentage / 100.0) * totalBars);

    std::wcout << barColor;
    for (int i = 0; i < totalBars; ++i) {
        if (i < filledBars) std::wcout << L"|";
        else std::wcout << L" ";
    }

    std::wcout << VT_RESET << std::right << std::fixed << std::setprecision(2)
        << std::setw(5) << used << L"G/" << std::setprecision(2) << total << L"G]";
}

void ConsoleUI::RenderHelp(Language lang) {
    int w = GetConsoleWidth();
    ResetCursor();
    std::wcout << VT_FG_BRIGHT_CYAN
        << L"=== " << (lang == Language::Ukrainian ? L"ДОВІДКА" : L"HELP SYSTEM") << L" ==="
        << std::setw(w - 15) << L" " << std::endl;
    std::wcout << VT_RESET
        << L"  [F1 / H] - Close/open this help window\n"
        << L"  [F2 / L] - Toggle language (UA / EN)\n"
        << L"  [F3 / S] - Open sort menu (arrows + Enter/Esc)\n"
        << L"  [F4 / R] - Toggle sort direction (Asc/Desc)\n"
        << L"  [F6 / I] - Change refresh interval\n"
        << L"  [F9 / K] - Kill process via PID\n"
        << L"  [Tab]    - Switch tab (Main / IO)\n"
        << L"  [Up/Down]  - Select process\n"
        << L"  [<- / ->]  - Page scroll\n\n Press [H] to return...";
    for (int i = 0; i < 18; i++) std::wcout << std::setw(w) << L" " << std::endl;
}

void ConsoleUI::RenderMonitor(AppConfig& config, CpuMonitor& cpuMon) {
    ResetCursor();
    int termWidth = GetConsoleWidth();
    std::wstring separator(termWidth, L'-');

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;
    double overallCpu = cpuMon.GetCpuUsage();

    int numCols = (numCores > 8) ? 4 : 2;
    int numRows = (numCores + numCols - 1) / numCols;

    // ВЕРХНЯ ПАНЕЛЬ: СІТКА ЯДЕР
    for (int r = 0; r < numRows; ++r) {
        for (int c = 0; c < numCols; ++c) {
            int coreIdx = c * numRows + r;
            if (coreIdx < numCores) {
                double simulatedCoreLoad = overallCpu + (coreIdx % 3) * 2.5 - (coreIdx % 2) * 1.5;
                if (simulatedCoreLoad < 0) simulatedCoreLoad = 0;
                if (simulatedCoreLoad > 100) simulatedCoreLoad = 100;
                DrawCoreBar(coreIdx, simulatedCoreLoad);
            } else {
                std::wcout << std::setw(28) << L" ";
            }
        }
        std::wcout << VT_CLEAR_LINE << std::endl;
    }

    MEMORYSTATUSEX memInfo = { sizeof(MEMORYSTATUSEX) };
    GlobalMemoryStatusEx(&memInfo);
    double totalMemG = memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    double usedMemG = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    double totalPageG = memInfo.ullTotalPageFile / (1024.0 * 1024.0 * 1024.0);
    double usedPageG = (memInfo.ullTotalPageFile - memInfo.ullAvailPageFile) / (1024.0 * 1024.0 * 1024.0);

    std::vector<ProcessInfo> processes;
    int totalThreads = 0;
    {
        extern std::mutex g_dataMutex;
        extern std::vector<ProcessInfo> g_cachedProcesses;
        extern int g_cachedThreadCount;
        std::lock_guard<std::mutex> lock(g_dataMutex);
        processes = g_cachedProcesses;
        totalThreads = g_cachedThreadCount;
    }

    ULONGLONG uptimeMs = GetTickCount64();
    int days = static_cast<int>(uptimeMs / (1000ULL * 60 * 60 * 24));
    int hours = static_cast<int>((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int mins = static_cast<int>((uptimeMs / (1000ULL * 60)) % 60);
    int secs = static_cast<int>((uptimeMs / 1000ULL) % 60);

    // РЯДКИ СТАТИСТИКИ
    DrawWideBar(L"Mem", usedMemG, totalMemG, VT_FG_BRIGHT_GREEN);
    std::wcout << VT_FG_BRIGHT_CYAN << L"  Tasks: " << VT_RESET;
    int runningCount = 0;
    for (const auto& p : processes) {
        if (p.state == L'R') runningCount++;
    }
    std::wcout << processes.size() << L", " << totalThreads << L" thr; "
        << runningCount << L" running" << VT_CLEAR_LINE << std::endl;

    DrawWideBar(L"Swp", usedPageG, totalPageG, VT_FG_BRIGHT_RED);
    std::wcout << VT_FG_BRIGHT_CYAN << L"  Load avg: " << VT_RESET;
    std::wcout << std::fixed << std::setprecision(2) << (overallCpu / 100.0) + 0.15 << L" "
        << (overallCpu / 100.0) + 0.08 << L" " << (overallCpu / 100.0) + 0.02
        << VT_CLEAR_LINE << std::endl;

    // Uptime вирівняний під Tasks/Load avg (позиція після DrawWideBar)
    // DrawWideBar виводить ~52 символи, тому відступ = 52
    std::wcout << std::setw(52) << L" " << VT_FG_BRIGHT_CYAN << L"Uptime: " << VT_RESET;
    if (days > 0) std::wcout << days << L" days, ";
    std::wcout << std::setfill(L'0') << std::setw(2) << hours << L":"
        << std::setw(2) << mins << L":" << std::setw(2) << secs
        << std::setfill(L' ') << VT_CLEAR_LINE << std::endl;

    // === ВКЛАДКИ ===
    std::wcout << VT_FG_DARKGRAY << separator << std::endl;

    if (config.activeTab == TabView::Main)
        std::wcout << VT_BG_GREEN << VT_FG_BLACK;
    else
        std::wcout << VT_BG_DARKGRAY << VT_FG_BLACK;
    std::wcout << L" Main ";

    if (config.activeTab == TabView::IO)
        std::wcout << VT_BG_GREEN << VT_FG_BLACK;
    else
        std::wcout << VT_BG_DARKGRAY << VT_FG_BLACK;
    std::wcout << L" I/O ";
    std::wcout << VT_RESET << std::setw(termWidth - 12) << L" " << std::endl;

    // === ШАПКА ТАБЛИЦІ ===
    std::wcout << VT_BG_GREEN << VT_FG_BLACK;

    int cmdColW;
    if (config.activeTab == TabView::Main) {
        int fixedColsWidth = 7 + 9 + 4 + 4 + 7 + 7 + 7 + 2 + 6 + 6 + 10;
        cmdColW = termWidth - fixedColsWidth;
        if (cmdColW < 15) cmdColW = 15;

        // Маркер сортування
        std::wstring mPid = L"  PID", mUser = L"USER", mPri = L"PRI", mNi = L"NI",
                     mVirt = L"VIRT", mRes = L"RES", mShr = L"SHR", mState = L"S",
                     mCpu = L"CPU%", mMem = L"MEM%", mTime = L"TIME+",
                     mCmd = (config.lang == Language::Ukrainian ? L"КОМАНДА" : L"COMMAND");
        const wchar_t arrow = config.sortAscending ? L'\x25B2' : L'\x25BC';
        switch (config.sortColumn) {
            case SortColumn::Pid:        mPid = std::wstring(L" PID") + arrow; break;
            case SortColumn::User:       mUser = std::wstring(L"USER") + arrow; break;
            case SortColumn::Priority:   mPri = std::wstring(L"PRI") + arrow; break;
            case SortColumn::Nice:       mNi = std::wstring(L"NI") + arrow; break;
            case SortColumn::Virt:       mVirt = std::wstring(L"VIRT") + arrow; break;
            case SortColumn::Res:        mRes = std::wstring(L"RES") + arrow; break;
            case SortColumn::Shr:        mShr = std::wstring(L"SHR") + arrow; break;
            case SortColumn::State:      mState = std::wstring(L"S") + arrow; break;
            case SortColumn::CpuPercent: mCpu = std::wstring(L"CPU") + arrow; break;
            case SortColumn::MemPercent: mMem = std::wstring(L"MEM") + arrow; break;
            case SortColumn::Time:       mTime = std::wstring(L"TIME") + arrow; break;
            case SortColumn::Command:    mCmd = arrow + (config.lang == Language::Ukrainian ? std::wstring(L"КОМАНДА") : std::wstring(L"COMMAND")); break;
        }

        std::wcout << std::left
            << std::setw(7) << mPid
            << std::setw(9) << mUser
            << std::setw(4) << mPri
            << std::setw(4) << mNi
            << std::setw(7) << mVirt
            << std::setw(7) << mRes
            << std::setw(7) << mShr
            << std::setw(2) << mState
            << std::setw(6) << mCpu
            << std::setw(6) << mMem
            << std::setw(10) << mTime
            << std::setw(cmdColW) << mCmd
            << VT_CLEAR_LINE << std::endl;
    } else {
        int fixedIOWidth = 7 + 9 + 4 + 9 + 11 + 12 + 6 + 6;
        cmdColW = termWidth - fixedIOWidth;
        if (cmdColW < 15) cmdColW = 15;
        std::wcout << std::left
            << std::setw(7) << L"  PID"
            << std::setw(9) << L"USER"
            << std::setw(4) << L"IO"
            << std::setw(9) << L"DISK R/W"
            << std::setw(11) << L"DISK READ"
            << std::setw(12) << L"DISK WRITE"
            << std::setw(6) << L"SWPD%"
            << std::setw(6) << L"IOD%"
            << std::setw(cmdColW) << L"Command"
            << VT_CLEAR_LINE << std::endl;
    }
    std::wcout << VT_RESET;

    // === СОРТУВАННЯ ===
    std::sort(processes.begin(), processes.end(), [&config](const ProcessInfo& a, const ProcessInfo& b) {
        if (config.activeTab == TabView::IO)
            return (a.ioReadBytes + a.ioWriteBytes) > (b.ioReadBytes + b.ioWriteBytes);

        bool result;
        switch (config.sortColumn) {
            case SortColumn::Pid:        result = a.pid > b.pid; break;
            case SortColumn::User:       result = a.userName < b.userName; break;
            case SortColumn::Priority:   result = a.priority < b.priority; break;
            case SortColumn::Nice:       result = a.niceness < b.niceness; break;
            case SortColumn::Virt:       result = a.virtualMemory > b.virtualMemory; break;
            case SortColumn::Res:        result = a.memoryUsage > b.memoryUsage; break;
            case SortColumn::Shr:        result = a.sharedMemory > b.sharedMemory; break;
            case SortColumn::State:      result = a.state < b.state; break;
            case SortColumn::CpuPercent: result = a.cpuPercent > b.cpuPercent; break;
            case SortColumn::MemPercent: result = a.memPercent > b.memPercent; break;
            case SortColumn::Time:       result = a.cpuTime > b.cpuTime; break;
            case SortColumn::Command:    result = a.name < b.name; break;
            default:                     result = a.memoryUsage > b.memoryUsage; break;
        }
        return config.sortAscending ? !result : result;
    });

    if (config.pageOffset >= (int)processes.size()) config.pageOffset = 0;
    if (config.selectedRow >= (std::min)(15, (int)processes.size() - config.pageOffset))
        config.selectedRow = (std::min)(15, (int)processes.size() - config.pageOffset) - 1;
    if (config.selectedRow < 0) config.selectedRow = 0;

    // === РЯДКИ ПРОЦЕСІВ ===
    int printedCount = 0;
    for (int i = config.pageOffset; i < (std::min)(config.pageOffset + 15, (int)processes.size()); ++i) {
        const auto& proc = processes[i];

        auto formatMem = [](SIZE_T bytes) -> std::wstring {
            double kb = bytes / 1024.0;
            if (kb >= 1024.0) {
                wchar_t buf[16]; swprintf(buf, 16, L"%.0fM", kb / 1024.0); return buf;
            }
            wchar_t buf[16]; swprintf(buf, 16, L"%.0fK", kb); return buf;
        };

        auto formatTime = [](ULONGLONG ms) -> std::wstring {
            int totalSec = (int)(ms / 1000);
            int h = totalSec / 3600, m = (totalSec % 3600) / 60, s = totalSec % 60;
            int cs = (int)((ms % 1000) / 10);
            wchar_t buf[20];
            if (h > 0) swprintf(buf, 20, L"%d:%02d:%02d", h, m, s);
            else swprintf(buf, 20, L"%d:%02d.%02d", m, s, cs);
            return buf;
        };

        std::wstring name = proc.name;
        if ((int)name.length() > cmdColW - 1) name = name.substr(0, cmdColW - 2) + L"~";

        std::wstring user = proc.userName;
        if (user.length() > 8) user = user.substr(0, 8);

        bool isSelected = (printedCount == config.selectedRow);

        if (isSelected) {
            std::wcout << VT_BG_CYAN << VT_FG_BLACK;
        } else {
            std::wcout << VT_FG_BRIGHT_CYAN;
        }

        std::wcout << std::right << std::setw(6) << proc.pid << L" ";
        if (!isSelected) std::wcout << VT_RESET;
        std::wcout << std::left << std::setw(9) << user;

        if (config.activeTab == TabView::Main) {
            std::wcout << std::right << std::setw(3) << proc.priority << L" ";
            std::wcout << std::right << std::setw(3) << proc.niceness << L" ";
            std::wcout << std::right << std::setw(6) << formatMem(proc.virtualMemory) << L" ";
            if (!isSelected) std::wcout << VT_FG_BRIGHT_GREEN;
            std::wcout << std::right << std::setw(6) << formatMem(proc.memoryUsage) << L" ";
            if (!isSelected) std::wcout << VT_RESET;
            std::wcout << std::right << std::setw(6) << formatMem(proc.sharedMemory) << L" ";
            std::wcout << proc.state << L" ";
            if (!isSelected) {
                if (proc.cpuPercent > 5.0) std::wcout << VT_FG_BRIGHT_RED;
                else std::wcout << VT_FG_BRIGHT_GREEN;
            }
            std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << proc.cpuPercent;
            if (!isSelected) std::wcout << VT_RESET;
            std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << proc.memPercent << L" ";
            std::wcout << std::right << std::setw(9) << formatTime(proc.cpuTime) << L" ";
        } else {
            auto formatIO = [](ULONGLONG bytes) -> std::wstring {
                wchar_t buf[16];
                if (bytes >= 1024ULL * 1024 * 1024) swprintf(buf, 16, L"%.1fG", bytes / (1024.0 * 1024.0 * 1024.0));
                else if (bytes >= 1024ULL * 1024) swprintf(buf, 16, L"%.1fM", bytes / (1024.0 * 1024.0));
                else if (bytes >= 1024ULL) swprintf(buf, 16, L"%.2fK", bytes / 1024.0);
                else swprintf(buf, 16, L"%.2fB", (double)bytes);
                return buf;
            };

            std::wcout << std::left << std::setw(3) << L"B4" << L" ";
            if (!isSelected) std::wcout << VT_RESET;
            std::wstring rw = formatIO(proc.ioDiskRead) + L"/s";
            std::wcout << std::right << std::setw(8) << rw << L" ";
            if (!isSelected) std::wcout << VT_FG_BRIGHT_GREEN;
            std::wstring dr = formatIO(proc.ioReadBytes) + L"/s";
            std::wcout << std::right << std::setw(10) << dr << L" ";
            if (!isSelected) std::wcout << VT_FG_BRIGHT_RED;
            std::wstring dw = formatIO(proc.ioWriteBytes) + L"/s";
            std::wcout << std::right << std::setw(11) << dw << L" ";
            if (!isSelected) std::wcout << VT_RESET;
            std::wcout << std::right << std::setw(5) << L"N/A" << L" ";
            std::wcout << std::right << std::setw(5) << L"N/A" << L" ";
        }

        std::wcout << std::left << std::setw(cmdColW) << name;
        std::wcout << VT_RESET << VT_CLEAR_LINE << std::endl;
        printedCount++;
    }

    // Заповнення порожніх рядків
    while (printedCount < 15) {
        std::wcout << VT_CLEAR_LINE << std::setw(termWidth) << L" " << std::endl;
        printedCount++;
    }

    // === НИЖНЯ ПАНЕЛЬ ===
    std::wcout << VT_FG_DARKGRAY << separator << std::endl;

    std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F1 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Довідка " : L"Help    ");
    std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F2 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Мова    " : L"Lang    ");
    std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F3 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Сорт    " : L"Sort    ");
    std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F4 " << VT_BG_CYAN << VT_FG_BLACK << (config.sortAscending ? L"Asc " : L"Desc");
    std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" Tab" << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Вкладка " : L"Tab     ");
    std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F6 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Інтервал" : L"Interval");
    std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F9 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Заверш  " : L"Kill    ");
    std::wcout << VT_RESET << VT_CLEAR_LINE << std::endl;
}

void ConsoleUI::RenderSortMenu(AppConfig& config) {
    ResetCursor();
    int termWidth = GetConsoleWidth();

    const std::wstring items[] = {
        L"PID", L"USER", L"PRIORITY", L"NICE", L"M_VIRT", L"M_RESIDENT",
        L"M_SHARE", L"STATE", L"PERCENT_CPU", L"PERCENT_MEM", L"TIME", L"Command"
    };
    const int itemCount = 12;

    // Заголовок
    std::wcout << VT_BG_GREEN << VT_FG_BLACK << L" Sort by"
        << std::setw(termWidth - 8) << L" " << VT_RESET << std::endl;

    for (int i = 0; i < itemCount; ++i) {
        if (i == config.sortMenuIndex) {
            std::wcout << VT_BG_CYAN << VT_FG_BLACK;
        } else {
            std::wcout << VT_RESET;
        }
        std::wcout << L"  " << std::left << std::setw(termWidth - 2) << items[i]
            << VT_RESET << std::endl;
    }

    // Заповнення
    std::wcout << VT_RESET;
    for (int i = itemCount + 1; i < 25; ++i) {
        std::wcout << VT_CLEAR_LINE << std::setw(termWidth) << L" " << std::endl;
    }

    // Нижня панель
    std::wcout << VT_FG_DARKGRAY << std::wstring(termWidth, L'-') << std::endl;
    std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L"Enter"
        << VT_BG_CYAN << VT_FG_BLACK << L"Sort  "
        << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" Esc "
        << VT_BG_CYAN << VT_FG_BLACK << L"Cancel"
        << VT_RESET << VT_CLEAR_LINE << std::endl;
}

void ConsoleUI::HandleKillDialog(AppConfig& config, CpuMonitor& cpuMon) {
    std::wcout << VT_CURSOR_SHOW;
    DWORD pidToKill = 0;

    std::wcout << VT_FG_BRIGHT_RED
        << (config.lang == Language::Ukrainian ? L"\n[KILL] Введіть PID процесу: " : L"\n[KILL] Enter Target PID: ")
        << VT_RESET;
    std::cin >> pidToKill;

    if (std::cin.fail()) {
        std::cin.clear();
        (std::cin.ignore)((std::numeric_limits<std::streamsize>::max)(), '\n');
        std::wcout << VT_FG_BRIGHT_RED
            << (config.lang == Language::Ukrainian ? L"[Помилка] Некоректний формат!" : L"[Error] Invalid PID format!")
            << VT_RESET;
        Sleep(1200);
        std::wcout << VT_CURSOR_HIDE << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
        cpuMon.Reset();
        return;
    }

    DWORD result = SystemManager::KillProcess(pidToKill);
    if (result == 0) {
        std::wcout << VT_FG_BRIGHT_GREEN << LocalizationManager::GetText("success", config.lang);
    } else if (result == ERROR_ACCESS_DENIED) {
        std::wcout << VT_FG_BRIGHT_RED << LocalizationManager::GetText("access_denied", config.lang);
    } else {
        std::wcout << VT_FG_BRIGHT_RED << LocalizationManager::GetText("not_found", config.lang);
    }

    Sleep(1200);
    std::wcout << VT_RESET << VT_CURSOR_HIDE << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
    cpuMon.Reset();
}
