#include "ConsoleUI.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#include <cmath>

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

    // Увімкнення підтримки миші
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hInput, &mode);
    mode |= ENABLE_MOUSE_INPUT;
    mode &= ~ENABLE_QUICK_EDIT_MODE;
    mode |= ENABLE_EXTENDED_FLAGS;
    SetConsoleMode(hInput, mode);

    // Вимкнення буферизації wcout для миттєвого виводу
    std::wcout.sync_with_stdio(false);
    std::wcout.flush();
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
    // Форматуємо в буфер щоб уникнути sticky formatting
    wchar_t pctBuf[8];
    swprintf(pctBuf, 8, L"%5.1f%%", percentage);
    std::wcout << pctBuf;
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
        << L"  [F3 / Tab] - Switch tab (Main / I/O)\n"
        << L"  [F6 / I] - Change refresh interval\n"
        << L"  [F9 / K] - Kill process via PID\n"
        << L"  [<- / ->] - Page scroll\n\n Press [H] to return...";
    for (int i = 0; i < 20; i++) std::wcout << std::setw(w) << L" " << std::endl;
}

void ConsoleUI::RenderMonitor(AppConfig& config, CpuMonitor& cpuMon) {
    std::wcout.flush(); // Flush перед перемальовуванням
    ResetCursor();
    int termWidth = GetConsoleWidth();
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
        // Очищення залишку рядка щоб не було артефактів при перемальовуванні
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_SCREEN_BUFFER_INFO csbInfo;
        GetConsoleScreenBufferInfo(hOut, &csbInfo);
        DWORD written;
        int remaining = csbInfo.dwSize.X - csbInfo.dwCursorPosition.X;
        if (remaining > 0) {
            FillConsoleOutputCharacterW(hOut, L' ', remaining, csbInfo.dwCursorPosition, &written);
            FillConsoleOutputAttribute(hOut, WHITE, remaining, csbInfo.dwCursorPosition, &written);
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
    int days = uptimeMs / (1000 * 60 * 60 * 24);
    int hours = (uptimeMs / (1000 * 60 * 60)) % 24;
    int mins = (uptimeMs / (1000 * 60)) % 60;
    int secs = (uptimeMs / 1000) % 60;

    // РЯДКИ СТАТИСТИКИ
    DrawWideBar(L"Mem", usedMemG, totalMemG, L"G", FG_BRIGHT_GREEN);
    SetColor(FG_BRIGHT_CYAN); std::wcout << L"  Tasks: "; SetColor(WHITE);
    std::wcout << processes.size() << L", 128 thr; 1 running" << std::endl;

    DrawWideBar(L"Swp", usedPageG, totalPageG, L"G", FG_BRIGHT_RED);
    SetColor(FG_BRIGHT_CYAN); std::wcout << L"  Load avg: "; SetColor(WHITE);
    std::wcout << std::fixed << std::setprecision(2) << (overallCpu / 100.0) + 0.15 << L" "
        << (overallCpu / 100.0) + 0.08 << L" " << (overallCpu / 100.0) + 0.02 << std::endl;

    SetColor(FG_BRIGHT_CYAN); std::wcout << std::setw(51) << L" " << L"  Uptime: "; SetColor(WHITE);
    if (days > 0) std::wcout << days << L" days, ";
    std::wcout << std::setfill(L'0') << std::setw(2) << hours << L":"
        << std::setw(2) << mins << L":" << std::setw(2) << secs << std::setfill(L' ') << std::endl;

    // === ВКЛАДКИ (TABS) ===
    SetColor(DARKGRAY);
    std::wcout << separator << std::endl;

    // Малюємо вкладки
    if (config.activeTab == TabView::Main) {
        SetColor(BG_GREEN_FG_BLACK);
    } else {
        SetColor((DARKGRAY << 4) | BLACK);
    }
    std::wcout << L" Main ";

    if (config.activeTab == TabView::IO) {
        SetColor((BLUE << 4) | BLACK);
    } else {
        SetColor((DARKGRAY << 4) | BLACK);
    }
    std::wcout << L" I/O ";

    SetColor(WHITE);
    std::wcout << std::setw(termWidth - 12) << L" " << std::endl;

    // === ШАПКА ТАБЛИЦІ залежно від вкладки ===
    SetColor(BG_GREEN_FG_BLACK);

    if (config.activeTab == TabView::Main) {
        // htop-style columns: PID(7) USER(9) PRI(4) NI(4) VIRT(7) RES(7) SHR(7) S(2) CPU%(6) MEM%(6) TIME+(10) COMMAND(rest)
        int fixedColsWidth = 7 + 9 + 4 + 4 + 7 + 7 + 7 + 2 + 6 + 6 + 10; // = 69
        int cmdColW = termWidth - fixedColsWidth;
        if (cmdColW < 15) cmdColW = 15;

        std::wcout << std::left
            << std::setw(7) << L"  PID"
            << std::setw(9) << L"USER"
            << std::setw(4) << L"PRI"
            << std::setw(4) << L"NI"
            << std::setw(7) << L"VIRT"
            << std::setw(7) << L"RES"
            << std::setw(7) << L"SHR"
            << std::setw(2) << L"S"
            << std::setw(6) << L"CPU%"
            << std::setw(6) << L"MEM%"
            << std::setw(10) << L"TIME+"
            << std::setw(cmdColW) << (config.lang == Language::Ukrainian ? L"КОМАНДА" : L"COMMAND")
            << std::endl;
    } else {
        // I/O columns: PID(7) USER(9) DISK_READ(12) DISK_WRITE(12) READ_OPS(10) WRITE_OPS(10) COMMAND(rest)
        int fixedIOWidth = 7 + 9 + 12 + 12 + 10 + 10; // = 60
        int cmdColW = termWidth - fixedIOWidth;
        if (cmdColW < 15) cmdColW = 15;

        std::wcout << std::left
            << std::setw(7) << L"  PID"
            << std::setw(9) << L"USER"
            << std::setw(12) << L"DISK READ"
            << std::setw(12) << L"DISK WRITE"
            << std::setw(10) << L"READ OPS"
            << std::setw(10) << L"WRITE OPS"
            << std::setw(cmdColW) << (config.lang == Language::Ukrainian ? L"КОМАНДА" : L"COMMAND")
            << std::endl;
    }
    SetColor(WHITE);

    std::sort(processes.begin(), processes.end(), [&config](const ProcessInfo& a, const ProcessInfo& b) {
        if (config.activeTab == TabView::IO)
            return (a.ioReadBytes + a.ioWriteBytes) > (b.ioReadBytes + b.ioWriteBytes);
        return a.memoryUsage > b.memoryUsage;
    });

    // Динамічна кількість видимих рядків на основі висоти консолі
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int consoleHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    // Рядки зайняті: ядра + mem/swp/uptime(3) + separator(1) + tabs(1) + header(1) + separator(1) + footer(1) = overhead
    SYSTEM_INFO si; GetSystemInfo(&si);
    int coreRows = ((int)si.dwNumberOfProcessors + (si.dwNumberOfProcessors > 8 ? 3 : 1)) / (si.dwNumberOfProcessors > 8 ? 4 : 2);
    int overhead = coreRows + 3 + 1 + 1 + 1 + 1 + 1; // cores + stats + sep + tab + header + sep + footer
    config.visibleRows = consoleHeight - overhead - 1;
    if (config.visibleRows < 5) config.visibleRows = 5;

    // Коригуємо scrollOffset
    int totalProc = (int)processes.size();
    if (config.scrollOffset > totalProc - config.visibleRows)
        config.scrollOffset = totalProc - config.visibleRows;
    if (config.scrollOffset < 0) config.scrollOffset = 0;
    if (config.selectedRow >= totalProc) config.selectedRow = totalProc - 1;
    if (config.selectedRow < 0) config.selectedRow = 0;

    // Helper lambdas
    auto formatMem = [](SIZE_T bytes) -> std::wstring {
        double kb = bytes / 1024.0;
        if (kb >= 1024.0) {
            double mb = kb / 1024.0;
            wchar_t buf[16];
            swprintf(buf, 16, L"%.0fM", mb);
            return buf;
        }
        wchar_t buf[16];
        swprintf(buf, 16, L"%.0fK", kb);
        return buf;
    };

    auto formatTime = [](ULONGLONG ms) -> std::wstring {
        int totalSec = (int)(ms / 1000);
        int hours = totalSec / 3600;
        int mins = (totalSec % 3600) / 60;
        int secs = totalSec % 60;
        int hundredths = (int)((ms % 1000) / 10);
        wchar_t buf[20];
        if (hours > 0)
            swprintf(buf, 20, L"%d:%02d:%02d", hours, mins, secs);
        else
            swprintf(buf, 20, L"%d:%02d.%02d", mins, secs, hundredths);
        return buf;
    };

    auto formatIOBytes = [](ULONGLONG bytes) -> std::wstring {
        wchar_t buf[16];
        if (bytes >= 1024ULL * 1024 * 1024)
            swprintf(buf, 16, L"%.1fG", bytes / (1024.0 * 1024.0 * 1024.0));
        else if (bytes >= 1024ULL * 1024)
            swprintf(buf, 16, L"%.1fM", bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024ULL)
            swprintf(buf, 16, L"%.1fK", bytes / 1024.0);
        else
            swprintf(buf, 16, L"%lluB", bytes);
        return buf;
    };

    int fixedColsWidth, cmdColW;
    if (config.activeTab == TabView::Main) {
        fixedColsWidth = 7 + 9 + 4 + 4 + 7 + 7 + 7 + 2 + 6 + 6 + 10;
        cmdColW = termWidth - fixedColsWidth;
    } else {
        fixedColsWidth = 7 + 9 + 12 + 12 + 10 + 10;
        cmdColW = termWidth - fixedColsWidth;
    }
    if (cmdColW < 15) cmdColW = 15;

    int printedCount = 0;
    int endIdx = (std::min)(config.scrollOffset + config.visibleRows, totalProc);
    for (int i = config.scrollOffset; i < endIdx; ++i) {
        const auto& proc = processes[i];

        std::wstring name = proc.name;
        if ((int)name.length() > cmdColW - 1) name = name.substr(0, cmdColW - 2) + L"~";

        std::wstring user = proc.userName;
        if (user.length() > 8) user = user.substr(0, 8);

        bool isSelected = (i == config.selectedRow);

        if (isSelected) {
            SetColor(BG_CYAN_FG_BLACK);
        } else {
            SetColor(FG_BRIGHT_CYAN);
        }

        // PID
        std::wcout << std::right << std::setw(6) << proc.pid << L" ";

        // USER
        if (!isSelected) SetColor(WHITE);
        std::wcout << std::left << std::setw(9) << user;

        if (config.activeTab == TabView::Main) {
            // PRI
            std::wcout << std::right << std::setw(3) << proc.priority << L" ";
            // NI
            std::wcout << std::right << std::setw(3) << proc.niceness << L" ";
            // VIRT
            std::wcout << std::right << std::setw(6) << formatMem(proc.virtualMemory) << L" ";
            // RES
            if (!isSelected) SetColor(FG_BRIGHT_GREEN);
            std::wcout << std::right << std::setw(6) << formatMem(proc.memoryUsage) << L" ";
            // SHR
            if (!isSelected) SetColor(WHITE);
            std::wcout << std::right << std::setw(6) << formatMem(proc.sharedMemory) << L" ";
            // S
            std::wcout << proc.state << L" ";
            // CPU%
            if (!isSelected && proc.cpuPercent > 5.0) SetColor(FG_BRIGHT_RED);
            else if (!isSelected) SetColor(FG_BRIGHT_GREEN);
            std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << proc.cpuPercent;
            // MEM%
            if (!isSelected) SetColor(WHITE);
            std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << proc.memPercent << L" ";
            // TIME+
            std::wcout << std::right << std::setw(9) << formatTime(proc.cpuTime) << L" ";
        } else {
            // I/O tab
            if (!isSelected) SetColor(FG_BRIGHT_GREEN);
            std::wcout << std::right << std::setw(11) << formatIOBytes(proc.ioReadBytes) << L" ";
            if (!isSelected) SetColor(FG_BRIGHT_RED);
            std::wcout << std::right << std::setw(11) << formatIOBytes(proc.ioWriteBytes) << L" ";
            if (!isSelected) SetColor(WHITE);
            std::wcout << std::right << std::setw(9) << proc.ioReadOps << L" ";
            std::wcout << std::right << std::setw(9) << proc.ioWriteOps << L" ";
        }

        // COMMAND
        if (!isSelected) SetColor(WHITE);
        std::wcout << std::left << std::setw(cmdColW) << name;

        std::wcout << std::endl;
        if (isSelected) SetColor(WHITE);
        printedCount++;
    }

    // Заповнюємо порожні рядки якщо процесів менше ніж видимих рядків
    SetColor(WHITE);
    while (printedCount < config.visibleRows) { std::wcout << std::setw(termWidth) << L" " << std::endl; printedCount++; }

    SetColor(DARKGRAY);
    std::wcout << separator << std::endl;

    // НИЖНЯ ПАНЕЛЬ: F-КЛАВІШІ
    SetColor(BLACK);
    std::wstring f1 = (config.lang == Language::Ukrainian ? L"Довідка " : L"Help    ");
    std::wstring f2 = (config.lang == Language::Ukrainian ? L"Мова    " : L"Lang    ");
    std::wstring f3 = (config.lang == Language::Ukrainian ? L"Вкладка " : L"Tab     ");
    std::wstring f6 = (config.lang == Language::Ukrainian ? L"Інтервал" : L"Interval");
    std::wstring f9 = (config.lang == Language::Ukrainian ? L"Заверш  " : L"Kill    ");

    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F1 "; SetColor((CYAN << 4) | BLACK); std::wcout << f1;
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F2 "; SetColor((CYAN << 4) | BLACK); std::wcout << f2;
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F3 "; SetColor((CYAN << 4) | BLACK); std::wcout << f3;
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F6 "; SetColor((CYAN << 4) | BLACK); std::wcout << f6;
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" F9 "; SetColor((CYAN << 4) | BLACK); std::wcout << f9;
    SetColor((DARKGRAY << 4) | WHITE); std::wcout << L" <- -> "; SetColor((CYAN << 4) | BLACK); std::wcout << (config.lang == Language::Ukrainian ? L"Гортання " : L"Scroll   ");

    SetColor(WHITE);
    // Заливаємо залишок рядка пробілами
    std::wcout << std::setw(termWidth - 65) << L" " << std::endl;
    std::wcout.flush(); // Атомарний вивід всього кадру
}

// Layout helpers: обчислюють позиції рядків на екрані
int ConsoleUI::GetTabRowY() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;
    int numCols = (numCores > 8) ? 4 : 2;
    int numRows = (numCores + numCols - 1) / numCols;
    // core rows + Mem row + Swp row + Uptime row + separator row = numRows + 4
    return numRows + 3 + 1; // +1 for separator
}

int ConsoleUI::GetHeaderRowY() {
    return GetTabRowY() + 2; // tab row + header row
}

int ConsoleUI::GetFooterRowY() {
    // Динамічно: header + visibleRows + separator
    // Але тут ми не маємо config, тому використаємо консольну висоту
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int consoleHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return consoleHeight - 2; // Передостанній рядок (footer)
}

bool ConsoleUI::ProcessMouseInput(AppConfig& config, CpuMonitor& cpuMon) {
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD numEvents = 0;
    GetNumberOfConsoleInputEvents(hInput, &numEvents);
    if (numEvents == 0) return false;

    // Читаємо ВСІ події з буфера щоб він не переповнювався
    INPUT_RECORD inputRecord[128];
    DWORD eventsRead = 0;
    DWORD toRead = (numEvents > 128) ? 128 : numEvents;
    ReadConsoleInput(hInput, inputRecord, toRead, &eventsRead);

    bool needRedraw = false;

    for (DWORD i = 0; i < eventsRead; ++i) {
        if (inputRecord[i].EventType != MOUSE_EVENT) continue;

        MOUSE_EVENT_RECORD& mouse = inputRecord[i].Event.MouseEvent;
        SHORT mx = mouse.dwMousePosition.X;
        SHORT my = mouse.dwMousePosition.Y;

        // Клік лівою кнопкою
        if (mouse.dwEventFlags == 0 && (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
            int tabRow = GetTabRowY();
            int headerRow = GetHeaderRowY();
            int footerRow = GetFooterRowY();

            // Клік на вкладки
            if (my == tabRow) {
                if (mx >= 0 && mx < 6) {
                    if (config.activeTab != TabView::Main) {
                        config.activeTab = TabView::Main;
                        config.scrollOffset = 0;
                        config.selectedRow = 0;
                        needRedraw = true;
                    }
                } else if (mx >= 6 && mx < 12) {
                    if (config.activeTab != TabView::IO) {
                        config.activeTab = TabView::IO;
                        config.scrollOffset = 0;
                        config.selectedRow = 0;
                        needRedraw = true;
                    }
                }
            }

            // Клік на рядок процесу
            int firstProcessRow = headerRow + 1;
            if (my >= firstProcessRow && my < firstProcessRow + config.visibleRows) {
                int clickedVisibleRow = my - firstProcessRow;
                int globalIdx = config.scrollOffset + clickedVisibleRow;
                config.selectedRow = globalIdx;
                needRedraw = true;
            }

            // Клік на нижню панель F-кнопок
            if (my >= footerRow) {
                if (mx < 12) {
                    config.showHelp = !config.showHelp;
                    system("cls");
                    needRedraw = true;
                } else if (mx < 24) {
                    config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
                    needRedraw = true;
                } else if (mx < 36) {
                    config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
                    config.scrollOffset = 0;
                    config.selectedRow = 0;
                    needRedraw = true;
                } else if (mx < 48) {
                    if (config.refreshInterval == 1000) config.refreshInterval = 3000;
                    else if (config.refreshInterval == 3000) config.refreshInterval = 5000;
                    else config.refreshInterval = 1000;
                    needRedraw = true;
                } else if (mx < 60) {
                    ConsoleUI::HandleKillDialog(config, cpuMon);
                    needRedraw = true;
                }
            }
        }

        // Скрол колесиком миші (по 3 рядки)
        if (mouse.dwEventFlags == MOUSE_WHEELED) {
            int scrollDir = (SHORT)HIWORD(mouse.dwButtonState);
            if (scrollDir > 0) {
                config.scrollOffset -= 3;
                if (config.scrollOffset < 0) config.scrollOffset = 0;
                if (config.selectedRow >= config.scrollOffset + config.visibleRows)
                    config.selectedRow = config.scrollOffset + config.visibleRows - 1;
                needRedraw = true;
            } else {
                size_t totalProcesses = SystemManager::GetProcesses().size();
                config.scrollOffset += 3;
                if (config.scrollOffset > (int)totalProcesses - config.visibleRows)
                    config.scrollOffset = (int)totalProcesses - config.visibleRows;
                if (config.scrollOffset < 0) config.scrollOffset = 0;
                if (config.selectedRow < config.scrollOffset)
                    config.selectedRow = config.scrollOffset;
                needRedraw = true;
            }
        }
    }

    return needRedraw;
}

// Layout helpers: обчислюють позиції рядків на екрані
int ConsoleUI::GetTabRowY() {
    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;
    int numCols = (numCores > 8) ? 4 : 2;
    int numRows = (numCores + numCols - 1) / numCols;
    // core rows + Mem row + Swp row + Uptime row + separator row = numRows + 4
    return numRows + 3 + 1; // +1 for separator
}

int ConsoleUI::GetHeaderRowY() {
    return GetTabRowY() + 2; // tab row + header row
}

int ConsoleUI::GetFooterRowY() {
    // Динамічно: header + visibleRows + separator
    // Але тут ми не маємо config, тому використаємо консольну висоту
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int consoleHeight = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return consoleHeight - 2; // Передостанній рядок (footer)
}

bool ConsoleUI::ProcessMouseInput(AppConfig& config, CpuMonitor& cpuMon) {
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD numEvents = 0;
    GetNumberOfConsoleInputEvents(hInput, &numEvents);
    if (numEvents == 0) return false;

    INPUT_RECORD inputRecord[32];
    DWORD eventsRead = 0;
    ReadConsoleInput(hInput, inputRecord, 32, &eventsRead);

    bool needRedraw = false;

    for (DWORD i = 0; i < eventsRead; ++i) {
        if (inputRecord[i].EventType != MOUSE_EVENT) continue;

        MOUSE_EVENT_RECORD& mouse = inputRecord[i].Event.MouseEvent;
        SHORT mx = mouse.dwMousePosition.X;
        SHORT my = mouse.dwMousePosition.Y;

        // Клік лівою кнопкою
        if (mouse.dwEventFlags == 0 && (mouse.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED)) {
            int tabRow = GetTabRowY();
            int headerRow = GetHeaderRowY();
            int footerRow = GetFooterRowY();

            // Клік на вкладки
            if (my == tabRow) {
                if (mx >= 0 && mx < 6) {
                    if (config.activeTab != TabView::Main) {
                        config.activeTab = TabView::Main;
                        config.scrollOffset = 0;
                        config.selectedRow = 0;
                        needRedraw = true;
                    }
                } else if (mx >= 6 && mx < 12) {
                    if (config.activeTab != TabView::IO) {
                        config.activeTab = TabView::IO;
                        config.scrollOffset = 0;
                        config.selectedRow = 0;
                        needRedraw = true;
                    }
                }
            }

            // Клік на рядок процесу
            int firstProcessRow = headerRow + 1;
            if (my >= firstProcessRow && my < firstProcessRow + config.visibleRows) {
                int clickedVisibleRow = my - firstProcessRow;
                int globalIdx = config.scrollOffset + clickedVisibleRow;
                config.selectedRow = globalIdx;
                needRedraw = true;
            }

            // Клік на нижню панель F-кнопок
            if (my == footerRow + 1) {
                if (mx < 12) {
                    config.showHelp = !config.showHelp;
                    system("cls");
                    needRedraw = true;
                } else if (mx < 24) {
                    config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
                    needRedraw = true;
                } else if (mx < 36) {
                    config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
                    config.scrollOffset = 0;
                    config.selectedRow = 0;
                    needRedraw = true;
                } else if (mx < 48) {
                    if (config.refreshInterval == 1000) config.refreshInterval = 3000;
                    else if (config.refreshInterval == 3000) config.refreshInterval = 5000;
                    else config.refreshInterval = 1000;
                    needRedraw = true;
                } else if (mx < 60) {
                    ConsoleUI::HandleKillDialog(config, cpuMon);
                    needRedraw = true;
                }
            }
        }

        // Скрол колесиком миші (по 3 рядки, як у htop)
        if (mouse.dwEventFlags == MOUSE_WHEELED) {
            int scrollDir = (SHORT)HIWORD(mouse.dwButtonState);
            if (scrollDir > 0) {
                // Скрол вгору
                config.scrollOffset -= 3;
                if (config.scrollOffset < 0) config.scrollOffset = 0;
                // Підтягуємо виділення якщо воно вийшло за видиму область
                if (config.selectedRow >= config.scrollOffset + config.visibleRows) {
                    config.selectedRow = config.scrollOffset + config.visibleRows - 1;
                }
                needRedraw = true;
            } else {
                // Скрол вниз
                size_t totalProcesses = SystemManager::GetProcesses().size();
                config.scrollOffset += 3;
                if (config.scrollOffset > (int)totalProcesses - config.visibleRows) {
                    config.scrollOffset = (int)totalProcesses - config.visibleRows;
                }
                if (config.scrollOffset < 0) config.scrollOffset = 0;
                // Підтягуємо виділення
                if (config.selectedRow < config.scrollOffset) {
                    config.selectedRow = config.scrollOffset;
                }
                needRedraw = true;
            }
        }
    }

    return needRedraw;
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