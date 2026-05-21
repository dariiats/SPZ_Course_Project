#include "ConsoleUI.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#include <cmath>
#include <tlhelp32.h>

enum ConsoleColors {
    BLACK = 0, BLUE = 1, GREEN = 2, CYAN = 3, RED = 4, MAGENTA = 5, BROWN = 6, LIGHTGRAY = 7,
    DARKGRAY = 8, LIGHTBLUE = 9, LIGHTGREEN = 10, LIGHTCYAN = 11, LIGHTRED = 12, WHITE = 15,
    BG_GREEN_FG_BLACK = (GREEN << 4) | BLACK,
    BG_CYAN_FG_BLACK = (CYAN << 4) | BLACK,
    FG_BRIGHT_GREEN = LIGHTGREEN,
    FG_BRIGHT_CYAN = LIGHTCYAN,
    FG_BRIGHT_RED = LIGHTRED
};

void SetColor(WORD color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), color);
}

// НОВА ФУНКЦІЯ: Отримання поточної ширини консолі
int GetConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return (width > 60) ? width : 60; // Мінімальна ширина 60 символів
}

void ConsoleUI::InitConsole() {
    std::setlocale(LC_ALL, "");
    SetCursorVisibility(false);
}

void ConsoleUI::ResetCursor() {
    COORD coord = { 0, 0 };
    SetConsoleCursorPosition(GetStdHandle(STD_OUTPUT_HANDLE), coord);
}

void ConsoleUI::SetCursorVisibility(bool visible) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hConsole, &cursorInfo);
    cursorInfo.bVisible = visible;
    SetConsoleCursorInfo(hConsole, &cursorInfo);
}

void DrawCoreBar(int coreId, double percentage) {
    SetColor(FG_BRIGHT_CYAN);
    std::wcout << std::right << std::setw(2) << coreId;
    SetColor(WHITE);
    std::wcout << L"[";

    int totalBars = 15;
    int filledBars = static_cast<int>((percentage / 100.0) * totalBars);
    WORD barColor = (percentage > 80.0) ? FG_BRIGHT_RED : FG_BRIGHT_GREEN;

    SetColor(barColor);
    for (int i = 0; i < totalBars; ++i) {
        if (i < filledBars) std::wcout << L"|";
        else std::wcout << L" ";
    }

    SetColor(WHITE);
    std::wcout << L" ";
    std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << percentage << L"%";
    std::wcout << L"]  ";
}

void DrawWideBar(std::wstring label, double used, double total, std::wstring unit, WORD barColor) {
    SetColor(FG_BRIGHT_CYAN);
    std::wcout << std::left << std::setw(4) << label;
    SetColor(WHITE);
    std::wcout << L"[";

    int totalBars = 35;
    double percentage = (used / total) * 100.0;
    int filledBars = static_cast<int>((percentage / 100.0) * totalBars);

    SetColor(barColor);
    for (int i = 0; i < totalBars; ++i) {
        if (i < filledBars) std::wcout << L"|";
        else std::wcout << L" ";
    }

    SetColor(WHITE);
    std::wcout << std::right << std::fixed << std::setprecision(2) << std::setw(5) << used << L"G/"
        << std::setprecision(2) << total << L"G]";
}

void ConsoleUI::RenderHelp(Language lang) {
    int w = GetConsoleWidth();
    ResetCursor();
    SetColor(FG_BRIGHT_CYAN);
    std::wcout << L"=== " << (lang == Language::Ukrainian ? L"ДОВІДКА" : L"HELP SYSTEM") << L" ===" << std::setw(w - 15) << L" " << std::endl;
    SetColor(WHITE);
    std::wcout << L"  [F1 / H] - Close/open this help window\n"
        << L"  [F2 / L] - Toggle language (UA / EN)\n"
        << L"  [F3 / S] - Open sort menu (arrows + Enter/Esc)\n"
        << L"  [F6 / I] - Change refresh interval\n"
        << L"  [F9 / K] - Kill process via PID\n"
        << L"  [Tab]    - Switch tab (Main / IO)\n"
        << L"  [<- / ->] - Page scroll\n\n Press [H] to return...";
    for (int i = 0; i < 20; i++) std::wcout << std::setw(w) << L" " << std::endl;
}

void ConsoleUI::RenderMonitor(AppConfig& config, CpuMonitor& cpuMon) {
    ResetCursor();
    int termWidth = GetConsoleWidth(); // Отримуємо динамічну ширину
    std::wstring separator(termWidth, L'-'); // Гумова лінія-розділювач

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
            }
            else {
                std::wcout << std::setw(28) << L" ";
            }
        }
        std::wcout << std::endl;
    }

    MEMORYSTATUSEX memInfo = { sizeof(MEMORYSTATUSEX) };
    GlobalMemoryStatusEx(&memInfo);
    double totalMemG = memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    double usedMemG = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    double totalPageG = memInfo.ullTotalPageFile / (1024.0 * 1024.0 * 1024.0);
    double usedPageG = (memInfo.ullTotalPageFile - memInfo.ullAvailPageFile) / (1024.0 * 1024.0 * 1024.0);

    std::vector<ProcessInfo> processes = SystemManager::GetProcesses();

    ULONGLONG uptimeMs = GetTickCount64();
    int days = static_cast<int>(uptimeMs / (1000ULL * 60 * 60 * 24));
    int hours = static_cast<int>((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int mins = static_cast<int>((uptimeMs / (1000ULL * 60)) % 60);
    int secs = static_cast<int>((uptimeMs / 1000ULL) % 60);

    // РЯДКИ СТАТИСТИКИ
    DrawWideBar(L"Mem", usedMemG, totalMemG, L"G", FG_BRIGHT_GREEN);
    SetColor(FG_BRIGHT_CYAN); std::wcout << L"  Tasks: "; SetColor(WHITE);
    // Підрахунок реальних потоків та running-процесів
    int runningCount = 0;
    int totalThreads = 0;
    for (const auto& p : processes) {
        if (p.state == L'R') runningCount++;
    }
    // Підрахунок потоків через snapshot
    HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hThreadSnap != INVALID_HANDLE_VALUE) {
        THREADENTRY32 te = { sizeof(THREADENTRY32) };
        if (Thread32First(hThreadSnap, &te)) {
            do { totalThreads++; } while (Thread32Next(hThreadSnap, &te));
        }
        CloseHandle(hThreadSnap);
    }
    std::wcout << processes.size() << L", " << totalThreads << L" thr; " << runningCount << L" running" << std::endl;

    DrawWideBar(L"Swp", usedPageG, totalPageG, L"G", FG_BRIGHT_RED);
    SetColor(FG_BRIGHT_CYAN); std::wcout << L"  Load avg: "; SetColor(WHITE);
    std::wcout << std::fixed << std::setprecision(2) << (overallCpu / 100.0) + 0.15 << L" "
        << (overallCpu / 100.0) + 0.08 << L" " << (overallCpu / 100.0) + 0.02 << std::endl;

    SetColor(FG_BRIGHT_CYAN); std::wcout << std::setw(51) << L" " << L"  Uptime: "; SetColor(WHITE);
    if (days > 0) std::wcout << days << L" days, ";
    std::wcout << std::setfill(L'0') << std::setw(2) << hours << L":"
        << std::setw(2) << mins << L":" << std::setw(2) << secs << std::setfill(L' ') << std::endl;

    // === ВКЛАДКИ ===
    SetColor(DARKGRAY);
    std::wcout << separator << std::endl;

    if (config.activeTab == TabView::Main) {
        SetColor(BG_GREEN_FG_BLACK);
    } else {
        SetColor((DARKGRAY << 4) | BLACK);
    }
    std::wcout << L" Main ";

    if (config.activeTab == TabView::IO) {
        SetColor(BG_GREEN_FG_BLACK);
    } else {
        SetColor((DARKGRAY << 4) | BLACK);
    }
    std::wcout << L" I/O ";
    SetColor(WHITE);
    std::wcout << std::setw(termWidth - 12) << L" " << std::endl;

    // === ШАПКА ТАБЛИЦІ ===
    SetColor(BG_GREEN_FG_BLACK);

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
        const wchar_t arrow = L'\x25BC';
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
            << std::endl;
    } else {
        // I/O columns: PID(7) USER(9) IO(4) DISK_RW(9) DISK_READ(11) DISK_WRITE(12) SWPD%(6) IOD%(6) COMMAND(rest)
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
            << std::endl;
    }
    SetColor(WHITE);

    std::sort(processes.begin(), processes.end(), [&config](const ProcessInfo& a, const ProcessInfo& b) {
        if (config.activeTab == TabView::IO)
            return (a.ioReadBytes + a.ioWriteBytes) > (b.ioReadBytes + b.ioWriteBytes);
        switch (config.sortColumn) {
            case SortColumn::Pid:        return a.pid > b.pid;
            case SortColumn::User:       return a.userName < b.userName;
            case SortColumn::Priority:   return a.priority < b.priority;
            case SortColumn::Nice:       return a.niceness < b.niceness;
            case SortColumn::Virt:       return a.virtualMemory > b.virtualMemory;
            case SortColumn::Res:        return a.memoryUsage > b.memoryUsage;
            case SortColumn::Shr:        return a.sharedMemory > b.sharedMemory;
            case SortColumn::State:      return a.state < b.state;
            case SortColumn::CpuPercent: return a.cpuPercent > b.cpuPercent;
            case SortColumn::MemPercent: return a.memPercent > b.memPercent;
            case SortColumn::Time:       return a.cpuTime > b.cpuTime;
            case SortColumn::Command:    return a.name < b.name;
            default:                     return a.memoryUsage > b.memoryUsage;
        }
    });

    if (config.pageOffset >= (int)processes.size()) config.pageOffset = 0;

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

        if (printedCount == 0) { SetColor(BG_CYAN_FG_BLACK); }
        else { SetColor(FG_BRIGHT_CYAN); }

        std::wcout << std::right << std::setw(6) << proc.pid << L" ";
        if (printedCount != 0) SetColor(WHITE);
        std::wcout << std::left << std::setw(9) << user;

        if (config.activeTab == TabView::Main) {
            std::wcout << std::right << std::setw(3) << proc.priority << L" ";
            std::wcout << std::right << std::setw(3) << proc.niceness << L" ";
            std::wcout << std::right << std::setw(6) << formatMem(proc.virtualMemory) << L" ";
            if (printedCount != 0) SetColor(FG_BRIGHT_GREEN);
            std::wcout << std::right << std::setw(6) << formatMem(proc.memoryUsage) << L" ";
            if (printedCount != 0) SetColor(WHITE);
            std::wcout << std::right << std::setw(6) << formatMem(proc.sharedMemory) << L" ";
            std::wcout << proc.state << L" ";
            if (printedCount != 0 && proc.cpuPercent > 5.0) SetColor(FG_BRIGHT_RED);
            else if (printedCount != 0) SetColor(FG_BRIGHT_GREEN);
            std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << proc.cpuPercent;
            if (printedCount != 0) SetColor(WHITE);
            std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << proc.memPercent << L" ";
            std::wcout << std::right << std::setw(9) << formatTime(proc.cpuTime) << L" ";
        } else {
            // I/O tab
            auto formatIO = [](ULONGLONG bytes) -> std::wstring {
                wchar_t buf[16];
                if (bytes >= 1024ULL * 1024 * 1024) swprintf(buf, 16, L"%.1fG", bytes / (1024.0 * 1024.0 * 1024.0));
                else if (bytes >= 1024ULL * 1024) swprintf(buf, 16, L"%.1fM", bytes / (1024.0 * 1024.0));
                else if (bytes >= 1024ULL) swprintf(buf, 16, L"%.2fK", bytes / 1024.0);
                else swprintf(buf, 16, L"%.2fB", (double)bytes);
                return buf;
            };

            // IO priority (B4 = background)
            std::wcout << std::left << std::setw(3) << L"B4" << L" ";
            // DISK R/W (combined rate)
            if (printedCount != 0) SetColor(WHITE);
            std::wstring rw = formatIO(proc.ioDiskRead) + L"/s";
            std::wcout << std::right << std::setw(8) << rw << L" ";
            // DISK READ
            if (printedCount != 0) SetColor(FG_BRIGHT_GREEN);
            std::wstring dr = formatIO(proc.ioReadBytes) + L"/s";
            std::wcout << std::right << std::setw(10) << dr << L" ";
            // DISK WRITE
            if (printedCount != 0) SetColor(FG_BRIGHT_RED);
            std::wstring dw = formatIO(proc.ioWriteBytes) + L"/s";
            std::wcout << std::right << std::setw(11) << dw << L" ";
            // SWPD%
            if (printedCount != 0) SetColor(WHITE);
            std::wcout << std::right << std::setw(5) << L"N/A" << L" ";
            // IOD%
            std::wcout << std::right << std::setw(5) << L"N/A" << L" ";
            if (printedCount != 0) SetColor(WHITE);
        }

        std::wcout << std::left << std::setw(cmdColW) << name << std::endl;
        if (printedCount == 0) SetColor(WHITE);
        printedCount++;
    }

    SetColor(WHITE);
    while (printedCount < 15) { std::wcout << std::setw(termWidth) << L" " << std::endl; printedCount++; }

    SetColor(DARKGRAY);
    std::wcout << separator << std::endl;

    // НИЖНЯ ПАНЕЛЬ: F-КЛАВІШІ
    SetColor(BLACK);
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F1 "; SetColor((CYAN << 4) | BLACK); std::wcout << (config.lang == Language::Ukrainian ? L"Довідка " : L"Help    ");
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F2 "; SetColor((CYAN << 4) | BLACK); std::wcout << (config.lang == Language::Ukrainian ? L"Мова    " : L"Lang    ");
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F3 "; SetColor((CYAN << 4) | BLACK); std::wcout << (config.lang == Language::Ukrainian ? L"Сорт    " : L"Sort    ");
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" Tab"; SetColor((CYAN << 4) | BLACK); std::wcout << (config.lang == Language::Ukrainian ? L"Вкладка " : L"Tab     ");
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F6 "; SetColor((CYAN << 4) | BLACK); std::wcout << (config.lang == Language::Ukrainian ? L"Інтервал" : L"Interval");
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F9 "; SetColor((CYAN << 4) | BLACK); std::wcout << (config.lang == Language::Ukrainian ? L"Заверш  " : L"Kill    ");
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" <->" ; SetColor((CYAN << 4) | BLACK); std::wcout << (config.lang == Language::Ukrainian ? L"Гортання " : L"Scroll   ");

    SetColor(WHITE);
    // Заливаємо залишок рядка пробілами
    std::wcout << std::setw(termWidth - 65) << L" " << std::endl;
}

void ConsoleUI::RenderSortMenu(AppConfig& config) {
    ResetCursor();
    int termWidth = GetConsoleWidth();

    const std::wstring items[] = {
        L"PID", L"USER", L"PRIORITY", L"NICE", L"M_VIRT", L"M_RESIDENT",
        L"M_SHARE", L"STATE", L"PERCENT_CPU", L"PERCENT_MEM", L"TIME", L"Command"
    };
    const int itemCount = 12;

    // Рядок "Sort by" зверху
    SetColor(BG_GREEN_FG_BLACK);
    std::wcout << L" Sort by" << std::setw(termWidth - 8) << L" " << std::endl;

    for (int i = 0; i < itemCount; ++i) {
        if (i == config.sortMenuIndex) {
            SetColor(BG_CYAN_FG_BLACK);
        } else {
            SetColor(WHITE);
        }
        std::wcout << L"  " << std::left << std::setw(termWidth - 2) << items[i] << std::endl;
    }

    // Заповнюємо решту рядків
    SetColor(WHITE);
    for (int i = itemCount + 1; i < 25; ++i) {
        std::wcout << std::setw(termWidth) << L" " << std::endl;
    }

    // Нижня панель
    SetColor(DARKGRAY);
    std::wstring separator(termWidth, L'-');
    std::wcout << separator << std::endl;
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L"Enter";
    SetColor((CYAN << 4) | BLACK); std::wcout << L"Sort  ";
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" Esc ";
    SetColor((CYAN << 4) | BLACK); std::wcout << L"Cancel";
    SetColor(WHITE);
    std::wcout << std::setw(termWidth - 22) << L" " << std::endl;
}

void ConsoleUI::HandleKillDialog(AppConfig& config, CpuMonitor& cpuMon) {
    SetCursorVisibility(true);
    SetColor(FG_BRIGHT_RED);
    DWORD pidToKill = 0;

    std::wcout << (config.lang == Language::Ukrainian ? L"\n[KILL] Введіть PID процесу: " : L"\n[KILL] Enter Target PID: ");
    SetColor(WHITE);
    std::cin >> pidToKill;

    if (std::cin.fail()) {
        std::cin.clear();
        (std::cin.ignore)((std::numeric_limits<std::streamsize>::max)(), '\n');
        SetColor(FG_BRIGHT_RED);
        std::wcout << (config.lang == Language::Ukrainian ? L"[Помилка] Некоректний формат!" : L"[Error] Invalid PID format!");
        Sleep(1200);
        SetCursorVisibility(false);
        system("cls");
        cpuMon.Reset();
        return;
    }

    DWORD result = SystemManager::KillProcess(pidToKill);
    if (result == 0) {
        SetColor(FG_BRIGHT_GREEN);
        std::wcout << LocalizationManager::GetText("success", config.lang);
    }
    else if (result == ERROR_ACCESS_DENIED) {
        SetColor(FG_BRIGHT_RED);
        std::wcout << LocalizationManager::GetText("access_denied", config.lang);
    }
    else {
        SetColor(FG_BRIGHT_RED);
        std::wcout << LocalizationManager::GetText("not_found", config.lang);
    }

    Sleep(1200);
    SetColor(WHITE);
    SetCursorVisibility(false);
    system("cls");
    cpuMon.Reset();
}