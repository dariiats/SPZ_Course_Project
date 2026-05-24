#include "ConsoleUI.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#include <cmath>
#include <mutex>
#include <functional>
#include <unordered_map>
#include <io.h>
#include <fcntl.h>
#include <conio.h>

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
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

// Отримання поточної висоти консолі
int GetConsoleHeight() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    return height;
}

void ConsoleUI::InitConsole() {
    // Встановлюємо UTF-8/Unicode для коректного відображення символів
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    _setmode(_fileno(stdout), _O_U16TEXT);
    std::setlocale(LC_ALL, ".UTF-8");

    // Увімкнення Virtual Terminal Processing
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);

    // Альтернативний screen buffer (як htop)
    std::wcout << L"\x1b[?1049h";
    std::wcout << VT_CURSOR_HIDE;
    std::wcout << VT_CLEAR_SCREEN << VT_CURSOR_HOME;

    // Вимикаємо обробку вводу консоллю щоб _kbhit/_getch працювали коректно
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD inMode = 0;
    GetConsoleMode(hIn, &inMode);
    inMode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    SetConsoleMode(hIn, inMode);
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
    if (lang == Language::Ukrainian) {
        std::wcout << VT_RESET
            << VT_FG_BRIGHT_CYAN << L" Клавіші (F / альтернатива):" << VT_RESET << L"\n"
            << L"  [F1 / H]    Відкрити/закрити довідку\n"
            << L"  [F2 / S]    Меню сортування (вибір колонки)\n"
            << L"  [F3 / /]    Пошук (перехід до збігу по імені)\n"
            << L"              Повторне F3 — наступний збіг\n"
            << L"  [F4 / \\]    Фільтр (залишає лише збіги)\n"
            << L"  [F5 / T]    Дерево процесів (вкл/викл)\n"
            << L"  [F6 / >]    Змінити напрямок сортування\n"
            << L"  [F7 / ]]    Pri+ (підвищити пріоритет)\n"
            << L"  [F8 / []    Pri- (знизити пріоритет)\n"
            << L"  [F9 / K]    Kill (меню завершення процесу)\n"
            << L"  [F10 / Q]   Вихід з програми\n"
            << L"  [Tab]       Перемикання вкладок (Main / IO)\n"
            << L"  [Space]     Закріпити/відкріпити процес\n"
            << L"  [L]         Змінити мову (UA / EN)\n"
            << L"  [I]         Змінити інтервал (1с/3с/5с)\n"
            << L"  [Вгору/Вниз] Навігація по списку\n"
            << L"  [<- / ->]   Гортання сторінок\n"
            << L"  [Enter]     Підтвердити вибір\n"
            << L"  [Esc]       Скасувати / скинути закріплення\n\n"
            << VT_FG_BRIGHT_CYAN << L" Закріплення (жовтий):" << VT_RESET << L"\n"
            << L"  Space або Enter в пошуку/фільтрі закріплює\n"
            << L"  процес. Курсор тримається на ньому при\n"
            << L"  оновленні списку. Скинути: Esc.\n\n"
            << VT_FG_BRIGHT_CYAN << L" Kill (F9/K):" << VT_RESET << L"\n"
            << L"  Завершує процес під курсором. Меню:\n"
            << L"  TERMINATE — жорстке завершення\n"
            << L"  WM_CLOSE  — м'яке (закриття вікон)\n\n"
            << VT_FG_DARKGRAY << L" Натисніть [F1] щоб повернутись..." << VT_RESET;
    } else {
        std::wcout << VT_RESET
            << VT_FG_BRIGHT_CYAN << L" Keys (F / alternative):" << VT_RESET << L"\n"
            << L"  [F1 / H]    Open/close this help\n"
            << L"  [F2 / S]    Sort menu (choose column)\n"
            << L"  [F3 / /]    Search (jump to match by name)\n"
            << L"              Press F3 again — next match\n"
            << L"  [F4 / \\]    Filter (show only matches)\n"
            << L"  [F5 / T]    Tree view (toggle)\n"
            << L"  [F6 / >]    Toggle sort direction\n"
            << L"  [F7 / ]]    Pri+ (raise process priority)\n"
            << L"  [F8 / []    Pri- (lower process priority)\n"
            << L"  [F9 / K]    Kill (process termination menu)\n"
            << L"  [F10 / Q]   Quit\n"
            << L"  [Tab]       Switch tab (Main / IO)\n"
            << L"  [Space]     Pin/unpin process\n"
            << L"  [L]         Toggle language (UA / EN)\n"
            << L"  [I]         Change interval (1s/3s/5s)\n"
            << L"  [Up/Down]   Navigate process list\n"
            << L"  [<- / ->]   Page scroll\n"
            << L"  [Enter]     Confirm selection\n"
            << L"  [Esc]       Cancel / unpin process\n\n"
            << VT_FG_BRIGHT_CYAN << L" Pinning (yellow):" << VT_RESET << L"\n"
            << L"  Space or Enter in search/filter pins a\n"
            << L"  process. Cursor stays on it during list\n"
            << L"  refresh. Reset: Esc.\n\n"
            << VT_FG_BRIGHT_CYAN << L" Kill (F9/K):" << VT_RESET << L"\n"
            << L"  Terminates process under cursor. Menu:\n"
            << L"  TERMINATE — force kill (immediate)\n"
            << L"  WM_CLOSE  — graceful (close windows)\n\n"
            << VT_FG_DARKGRAY << L" Press [F1] to return..." << VT_RESET;
    }
    for (int i = 0; i < 5; i++) std::wcout << VT_CLEAR_LINE << L"\n";
    std::wcout << L"\x1b[J";
}

void ConsoleUI::RenderMonitor(AppConfig& config, CpuMonitor& cpuMon) {
    // Переміщуємо курсор на початок і забороняємо scroll
    std::wcout << VT_CURSOR_HOME;
    int termWidth = GetConsoleWidth();
    int termHeight = GetConsoleHeight();

    // Мінімальний розмір вікна для коректного відображення
    const int MIN_WIDTH = 80;
    const int MIN_HEIGHT = 24;

    if (termWidth < MIN_WIDTH || termHeight < MIN_HEIGHT) {
        std::wcout << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
        std::wcout << VT_FG_BRIGHT_RED;
        if (config.lang == Language::Ukrainian) {
            std::wcout << L"  Вікно замале!" << VT_CLEAR_LINE << std::endl;
            std::wcout << L"  Мінімум: " << MIN_WIDTH << L"x" << MIN_HEIGHT << VT_CLEAR_LINE << std::endl;
            std::wcout << L"  Зараз:   " << termWidth << L"x" << termHeight << VT_CLEAR_LINE << std::endl;
        } else {
            std::wcout << L"  Window too small!" << VT_CLEAR_LINE << std::endl;
            std::wcout << L"  Minimum: " << MIN_WIDTH << L"x" << MIN_HEIGHT << VT_CLEAR_LINE << std::endl;
            std::wcout << L"  Current: " << termWidth << L"x" << termHeight << VT_CLEAR_LINE << std::endl;
        }
        std::wcout << VT_RESET << L"\x1b[J";
        return;
    }

    std::wstring separator(termWidth, L'-');

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;
    double overallCpu = cpuMon.GetCpuUsage();

    // Отримуємо реальні per-core дані з кешу
    std::vector<double> coreUsages;
    {
        extern std::mutex g_dataMutex;
        extern std::vector<double> g_cachedCoreUsages;
        std::lock_guard<std::mutex> lock(g_dataMutex);
        coreUsages = g_cachedCoreUsages;
    }

    int numCols = (numCores > 8) ? 4 : 2;
    int numRows = (numCores + numCols - 1) / numCols;

    // Кількість видимих рядків процесів (динамічно від висоти вікна)
    int visibleRows = termHeight - numRows - 7;
    if (visibleRows < 5) visibleRows = 5;
    config.visibleRows = visibleRows;

    // ВЕРХНЯ ПАНЕЛЬ: СІТКА ЯДЕР (реальні дані)
    for (int r = 0; r < numRows; ++r) {
        for (int c = 0; c < numCols; ++c) {
            int coreIdx = c * numRows + r;
            if (coreIdx < numCores) {
                double coreLoad = (coreIdx < (int)coreUsages.size()) ? coreUsages[coreIdx] : 0.0;
                DrawCoreBar(coreIdx, coreLoad);
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

    // Фільтрація за пошуковим запитом (F4 Filter)
    if (config.showFilter && !config.searchQuery.empty()) {
        std::wstring query = config.searchQuery;
        std::vector<ProcessInfo> filtered;
        for (const auto& p : processes) {
            std::wstring nameLower = p.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
            if (nameLower.find(query) == 0) {
                filtered.push_back(p);
            }
        }
        processes = std::move(filtered);
    }

    // Для Filter — перевіряємо чи є результати
    bool filterHasResults = true;
    if (config.showFilter && !config.searchQuery.empty() && processes.empty()) {
        filterHasResults = false;
    }

    ULONGLONG uptimeMs = GetTickCount64();
    int days = static_cast<int>(uptimeMs / (1000ULL * 60 * 60 * 24));
    int hours = static_cast<int>((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int mins = static_cast<int>((uptimeMs / (1000ULL * 60)) % 60);
    int secs = static_cast<int>((uptimeMs / 1000ULL) % 60);

    // РЯДКИ СТАТИСТИКИ
    DrawWideBar(L"Mem", usedMemG, totalMemG, VT_FG_BRIGHT_GREEN);
    bool ua = (config.lang == Language::Ukrainian);
    std::wcout << VT_FG_BRIGHT_CYAN << (ua ? L"  Задачі: " : L"  Tasks: ") << VT_RESET;
    int runningCount = 0;
    for (const auto& p : processes) {
        if (p.cpuPercent > 0.0) runningCount++;
    }
    std::wcout << processes.size() << L", " << totalThreads << (ua ? L" пот; " : L" thr; ")
        << runningCount << (ua ? L" активн" : L" running") << VT_CLEAR_LINE << std::endl;

    DrawWideBar(L"Swp", usedPageG, totalPageG, VT_FG_BRIGHT_RED);
    std::wcout << VT_FG_BRIGHT_CYAN << (ua ? L"  Час роботи: " : L"  Uptime: ") << VT_RESET;
    if (days > 0) std::wcout << days << (ua ? L" дн, " : L" days, ");
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
        int fixedColsWidth = 7 + 9 + 4 + 7 + 7 + 7 + 2 + 6 + 6 + 10;
        cmdColW = termWidth - fixedColsWidth;
        if (cmdColW < 15) cmdColW = 15;

        // Маркер сортування
        std::wstring mPid = L"  PID", mUser = L"USER", mPri = L"PRI",
                     mVirt = L"VIRT", mRes = L"RES", mShr = L"SHR", mState = L"S",
                     mCpu = L"CPU%", mMem = L"MEM%", mTime = L"TIME+",
                     mCmd = (config.lang == Language::Ukrainian ? L"КОМАНДА" : L"COMMAND");
        const wchar_t arrow = config.sortAscending ? L'\x25B2' : L'\x25BC';
        switch (config.sortColumn) {
            case SortColumn::Pid:        mPid = std::wstring(L" PID") + arrow; break;
            case SortColumn::User:       mUser = std::wstring(L"USER") + arrow; break;
            case SortColumn::Priority:   mPri = std::wstring(L"PRI") + arrow; break;
            case SortColumn::Virt:       mVirt = std::wstring(L"VIRT") + arrow; break;
            case SortColumn::Res:        mRes = std::wstring(L"RES") + arrow; break;
            case SortColumn::Shr:        mShr = std::wstring(L"SHR") + arrow; break;
            case SortColumn::State:      mState = std::wstring(L"S") + arrow; break;
            case SortColumn::CpuPercent: mCpu = std::wstring(L"CPU") + arrow; break;
            case SortColumn::MemPercent: mMem = std::wstring(L"MEM") + arrow; break;
            case SortColumn::Time:       mTime = std::wstring(L"TIME") + arrow; break;
            case SortColumn::Command:    mCmd = arrow + (config.lang == Language::Ukrainian ? std::wstring(L"КОМАНДА") : std::wstring(L"COMMAND")); break;
            default: break;
        }

        std::wcout << std::left
            << std::setw(7) << mPid
            << std::setw(9) << mUser
            << std::setw(4) << mPri
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
        int fixedIOWidth = 7 + 9 + 11 + 11 + 11;
        cmdColW = termWidth - fixedIOWidth;
        if (cmdColW < 15) cmdColW = 15;

        // Маркер сортування IO
        std::wstring ioPid = L"  PID", ioUser = L"USER", ioRW = L"DISK R/W",
                     ioRead = L"DISK READ", ioWrite = L"DISK WRITE",
                     ioCmd = (config.lang == Language::Ukrainian ? L"КОМАНДА" : L"Command");
        const wchar_t ioArrow = config.sortAscending ? L'\x25B2' : L'\x25BC';
        switch (config.ioSortColumn) {
            case IoSortColumn::Pid:       ioPid = std::wstring(L" PID") + ioArrow; break;
            case IoSortColumn::User:      ioUser = std::wstring(L"USER") + ioArrow; break;
            case IoSortColumn::DiskRW:    ioRW = std::wstring(L"DISK") + ioArrow + L"R/W"; break;
            case IoSortColumn::DiskRead:  ioRead = std::wstring(L"DISK") + ioArrow + L"READ"; break;
            case IoSortColumn::DiskWrite: ioWrite = std::wstring(L"DISK") + ioArrow + L"WRITE"; break;
            case IoSortColumn::Command:   ioCmd = ioArrow + (config.lang == Language::Ukrainian ? std::wstring(L"КОМАНДА") : std::wstring(L"Command")); break;
            default: break;
        }

        std::wcout << std::left
            << std::setw(7) << ioPid
            << std::setw(9) << ioUser
            << std::setw(11) << ioRW
            << std::setw(11) << ioRead
            << std::setw(11) << ioWrite
            << std::setw(cmdColW) << ioCmd
            << VT_CLEAR_LINE << std::endl;
    }
    std::wcout << VT_RESET;

    // === СОРТУВАННЯ ===
    std::sort(processes.begin(), processes.end(), [&config](const ProcessInfo& a, const ProcessInfo& b) {
        int cmp = 0; // -1: a<b, 0: equal, 1: a>b
        if (config.activeTab == TabView::IO) {
            switch (config.ioSortColumn) {
                case IoSortColumn::Pid:
                    cmp = (a.pid > b.pid) ? 1 : (a.pid < b.pid) ? -1 : 0; break;
                case IoSortColumn::User:
                    cmp = (a.userName < b.userName) ? 1 : (a.userName > b.userName) ? -1 : 0; break;
                case IoSortColumn::DiskRW:
                    cmp = ((a.ioDiskRead + a.ioDiskWrite) > (b.ioDiskRead + b.ioDiskWrite)) ? 1 :
                          ((a.ioDiskRead + a.ioDiskWrite) < (b.ioDiskRead + b.ioDiskWrite)) ? -1 : 0; break;
                case IoSortColumn::DiskRead:
                    cmp = (a.ioDiskRead > b.ioDiskRead) ? 1 : (a.ioDiskRead < b.ioDiskRead) ? -1 : 0; break;
                case IoSortColumn::DiskWrite:
                    cmp = (a.ioDiskWrite > b.ioDiskWrite) ? 1 : (a.ioDiskWrite < b.ioDiskWrite) ? -1 : 0; break;
                case IoSortColumn::Command:
                    cmp = (a.name < b.name) ? 1 : (a.name > b.name) ? -1 : 0; break;
                default:
                    cmp = ((a.ioDiskRead + a.ioDiskWrite) > (b.ioDiskRead + b.ioDiskWrite)) ? 1 :
                          ((a.ioDiskRead + a.ioDiskWrite) < (b.ioDiskRead + b.ioDiskWrite)) ? -1 : 0; break;
            }
        } else {
            switch (config.sortColumn) {
                case SortColumn::Pid:
                    cmp = (a.pid > b.pid) ? 1 : (a.pid < b.pid) ? -1 : 0; break;
                case SortColumn::User:
                    cmp = (a.userName < b.userName) ? 1 : (a.userName > b.userName) ? -1 : 0; break;
                case SortColumn::Priority:
                    cmp = (a.priority > b.priority) ? 1 : (a.priority < b.priority) ? -1 : 0; break;
                case SortColumn::Virt:
                    cmp = (a.virtualMemory > b.virtualMemory) ? 1 : (a.virtualMemory < b.virtualMemory) ? -1 : 0; break;
                case SortColumn::Res:
                    cmp = (a.memoryUsage > b.memoryUsage) ? 1 : (a.memoryUsage < b.memoryUsage) ? -1 : 0; break;
                case SortColumn::Shr:
                    cmp = (a.sharedMemory > b.sharedMemory) ? 1 : (a.sharedMemory < b.sharedMemory) ? -1 : 0; break;
                case SortColumn::State:
                    cmp = (a.state < b.state) ? 1 : (a.state > b.state) ? -1 : 0; break;
                case SortColumn::CpuPercent:
                    cmp = (a.cpuPercent > b.cpuPercent) ? 1 : (a.cpuPercent < b.cpuPercent) ? -1 : 0; break;
                case SortColumn::MemPercent:
                    cmp = (a.memPercent > b.memPercent) ? 1 : (a.memPercent < b.memPercent) ? -1 : 0; break;
                case SortColumn::Time:
                    cmp = (a.cpuTime > b.cpuTime) ? 1 : (a.cpuTime < b.cpuTime) ? -1 : 0; break;
                case SortColumn::Command:
                    cmp = (a.name < b.name) ? 1 : (a.name > b.name) ? -1 : 0; break;
                default:
                    cmp = (a.memoryUsage > b.memoryUsage) ? 1 : (a.memoryUsage < b.memoryUsage) ? -1 : 0; break;
            }
        }
        if (cmp == 0) {
            // Вторинне сортування по PID для стабільності
            return a.pid < b.pid;
        }
        return config.sortAscending ? (cmp < 0) : (cmp > 0);
    });

    // Tree view — перебудова списку в деревоподібному порядку
    if (config.treeView) {
        std::unordered_map<DWORD, std::vector<int>> childrenMap;
        std::unordered_map<DWORD, int> pidIndex;
        for (int i = 0; i < (int)processes.size(); ++i) {
            pidIndex[processes[i].pid] = i;
            childrenMap[processes[i].parentPid].push_back(i);
        }

        // Знаходимо кореневі процеси (батько не в списку)
        std::vector<int> roots;
        for (int i = 0; i < (int)processes.size(); ++i) {
            if (pidIndex.find(processes[i].parentPid) == pidIndex.end()) {
                roots.push_back(i);
            }
        }

        // DFS для побудови плоского дерева з відступами
        struct TreeEntry { int idx; int depth; };
        std::vector<TreeEntry> treeOrder;
        std::function<void(int, int)> buildTree = [&](int idx, int depth) {
            treeOrder.push_back({ idx, depth });
            auto it = childrenMap.find(processes[idx].pid);
            if (it != childrenMap.end()) {
                for (int childIdx : it->second) {
                    buildTree(childIdx, depth + 1);
                }
            }
        };
        for (int root : roots) {
            buildTree(root, 0);
        }

        // Перебудовуємо список з відступами в імені
        std::vector<ProcessInfo> treeProcesses;
        treeProcesses.reserve(treeOrder.size());
        for (const auto& entry : treeOrder) {
            ProcessInfo p = processes[entry.idx];
            if (entry.depth > 0) {
                std::wstring prefix;
                for (int d = 0; d < entry.depth - 1; ++d) prefix += L"  ";
                prefix += L"├─";
                p.name = prefix + p.name;
            }
            treeProcesses.push_back(std::move(p));
        }
        processes = std::move(treeProcesses);
    }

    // Search (F3) — завжди тримаємо курсор на N-му збігу (після сортування)
    bool searchFound = true;

    if (config.showSearch && !config.searchQuery.empty()) {
        std::wstring query = config.searchQuery;
        bool found = false;
        int matchCount = 0;

        for (int i = 0; i < (int)processes.size(); ++i) {
            std::wstring nameLower = processes[i].name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
            if (nameLower.find(query) == 0) {
                if (matchCount == config.searchMatchIndex) {
                    config.pageOffset = (i / visibleRows) * visibleRows;
                    config.selectedRow = i - config.pageOffset;
                    found = true;
                    break;
                }
                matchCount++;
            }
        }
        // Wrap around — якщо matchIndex вийшов за межі, скидаємо на перший збіг
        if (!found && matchCount > 0) {
            config.searchMatchIndex = 0;
            for (int i = 0; i < (int)processes.size(); ++i) {
                std::wstring nameLower = processes[i].name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
                if (nameLower.find(query) == 0) {
                    config.pageOffset = (i / visibleRows) * visibleRows;
                    config.selectedRow = i - config.pageOffset;
                    found = true;
                    break;
                }
            }
        }
        searchFound = found;
    } else if (config.showSearch) {
        searchFound = true;
    } else if (config.pinnedPid != 0) {
        // Тримаємо курсор на закріпленому процесі після оновлення даних
        for (int i = 0; i < (int)processes.size(); ++i) {
            if (processes[i].pid == config.pinnedPid) {
                config.pageOffset = (i / visibleRows) * visibleRows;
                config.selectedRow = i - config.pageOffset;
                break;
            }
        }
    }

    if (config.pageOffset >= (int)processes.size()) config.pageOffset = 0;
    if (config.selectedRow >= (std::min)(visibleRows, (int)processes.size() - config.pageOffset))
        config.selectedRow = (std::min)(visibleRows, (int)processes.size() - config.pageOffset) - 1;
    if (config.selectedRow < 0) config.selectedRow = 0;

    // === РЯДКИ ПРОЦЕСІВ ===
    int printedCount = 0;
    for (int i = config.pageOffset; i < (std::min)(config.pageOffset + visibleRows, (int)processes.size()); ++i) {
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
        bool isPinned = (config.pinnedPid != 0 && proc.pid == config.pinnedPid);

        if (isSelected) {
            config.selectedPid = proc.pid;
            if (isPinned) {
                std::wcout << L"\x1b[43m" << VT_FG_BLACK; // жовтий фон
            } else {
                std::wcout << VT_BG_CYAN << VT_FG_BLACK;
            }
        } else if (isPinned) {
            std::wcout << VT_FG_YELLOW;
        } else {
            std::wcout << VT_FG_BRIGHT_CYAN;
        }

        std::wcout << std::right << std::setw(6) << proc.pid << L" ";
        if (!isSelected && !isPinned) std::wcout << VT_RESET;
        std::wcout << std::left << std::setw(9) << user;

        if (config.activeTab == TabView::Main) {
            std::wcout << std::right << std::setw(3) << proc.priority << L" ";
            std::wcout << std::right << std::setw(6) << formatMem(proc.virtualMemory) << L" ";
            if (!isSelected && !isPinned) std::wcout << VT_FG_BRIGHT_GREEN;
            std::wcout << std::right << std::setw(6) << formatMem(proc.memoryUsage) << L" ";
            if (!isSelected && !isPinned) std::wcout << VT_RESET;
            std::wcout << std::right << std::setw(6) << formatMem(proc.sharedMemory) << L" ";
            std::wcout << proc.state << L" ";
            if (!isSelected && !isPinned) {
                if (proc.cpuPercent > 5.0) std::wcout << VT_FG_BRIGHT_RED;
                else std::wcout << VT_FG_BRIGHT_GREEN;
            }
            std::wcout << std::right << std::fixed << std::setprecision(1) << std::setw(5) << proc.cpuPercent;
            if (!isSelected && !isPinned) std::wcout << VT_RESET;
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

            if (!isSelected && !isPinned) std::wcout << VT_RESET;
            // DISK R/W = read + write rate
            std::wstring rw = formatIO(proc.ioDiskRead + proc.ioDiskWrite) + L"/s";
            std::wcout << std::right << std::setw(10) << rw << L" ";
            // DISK READ rate
            if (!isSelected && !isPinned) std::wcout << VT_FG_BRIGHT_GREEN;
            std::wstring dr = formatIO(proc.ioDiskRead) + L"/s";
            std::wcout << std::right << std::setw(10) << dr << L" ";
            // DISK WRITE rate
            if (!isSelected && !isPinned) std::wcout << VT_FG_BRIGHT_RED;
            std::wstring dw = formatIO(proc.ioDiskWrite) + L"/s";
            std::wcout << std::right << std::setw(10) << dw << L" ";
            if (!isSelected && !isPinned) std::wcout << VT_RESET;
        }

        std::wcout << std::left << std::setw(cmdColW) << name;
        std::wcout << VT_RESET << VT_CLEAR_LINE << std::endl;
        printedCount++;
    }

    // Заповнення порожніх рядків
    while (printedCount < visibleRows) {
        std::wcout << VT_CLEAR_LINE << std::setw(termWidth) << L" " << std::endl;
        printedCount++;
    }

    // === НИЖНЯ ПАНЕЛЬ ===
    std::wcout << VT_FG_DARKGRAY << separator << std::endl;

    // Нижня панель
    if (config.showSearch || config.showFilter) {
        bool hasResults = config.showSearch ? searchFound : filterHasResults;
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE
            << (config.showSearch ? L" F3 " : L" F4 ")
            << VT_BG_CYAN << VT_FG_BLACK
            << (config.showSearch ? (config.lang == Language::Ukrainian ? L"Пошук: " : L"Search: ")
                                  : (config.lang == Language::Ukrainian ? L"Фільтр: " : L"Filter: "))
            << VT_RESET << (hasResults ? VT_FG_BRIGHT_GREEN : VT_FG_BRIGHT_RED)
            << config.searchQuery << L"_"
            << VT_RESET << VT_CLEAR_LINE;
    } else {
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F1 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Довідка" : L"Help  ");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F2 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Сорт  " : L"SortBy");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F3 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Пошук " : L"Search");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F4 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Фільтр" : L"Filter");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F5 " << VT_BG_CYAN << VT_FG_BLACK << (config.treeView ? (config.lang == Language::Ukrainian ? L"Список" : L"List  ") : (config.lang == Language::Ukrainian ? L"Дерево" : L"Tree  "));
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F6 " << VT_BG_CYAN << VT_FG_BLACK << (config.sortAscending ? L"\x25B2" : L"\x25BC");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F7 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Пріор+" : L"Pri+  ");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F8 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Пріор-" : L"Pri-  ");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" F9 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Заверш" : L"Kill  ");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L"F10 " << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Вихід " : L"Quit  ");
        std::wcout << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" Tab" << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Вкладка" : L"Tab   ");
        // Інфо справа: мова та інтервал
        std::wcout << VT_RESET << VT_FG_DARKGRAY << L" [L]"
            << VT_FG_BRIGHT_CYAN << (config.lang == Language::Ukrainian ? L"UA" : L"EN")
            << VT_FG_DARKGRAY << L" [I]"
            << VT_FG_BRIGHT_CYAN << (config.refreshInterval / 1000) << (config.lang == Language::Ukrainian ? L"с" : L"s");
        std::wcout << VT_RESET << VT_CLEAR_LINE;
    }
    std::wcout << L"\x1b[J";
}

void ConsoleUI::RenderSortMenu(AppConfig& config) {
    ResetCursor();
    int termWidth = GetConsoleWidth();

    const std::wstring mainItems[] = {
        L"PID", L"USER", L"PRIORITY", L"M_VIRT", L"M_RESIDENT",
        L"M_SHARE", L"STATE", L"PERCENT_CPU", L"PERCENT_MEM", L"TIME", L"Command"
    };
    const std::wstring ioItems[] = {
        L"PID", L"USER", L"DISK_R/W", L"DISK_READ", L"DISK_WRITE", L"Command"
    };

    const std::wstring* items;
    int itemCount;
    if (config.activeTab == TabView::IO) {
        items = ioItems;
        itemCount = 6;
    } else {
        items = mainItems;
        itemCount = 11;
    }

    // Заголовок
    std::wcout << VT_BG_GREEN << VT_FG_BLACK
        << (config.lang == Language::Ukrainian ? L" Сортувати за" : L" Sort by")
        << std::setw(termWidth - 13) << L" " << VT_RESET << std::endl;

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
        << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Обрати" : L"Sort  ")
        << VT_BG_DARKGRAY << VT_FG_BRIGHT_WHITE << L" Esc "
        << VT_BG_CYAN << VT_FG_BLACK << (config.lang == Language::Ukrainian ? L"Назад " : L"Cancel")
        << VT_RESET << VT_CLEAR_LINE << L"\x1b[J";
}

void ConsoleUI::HandleKillDialog(AppConfig& config, CpuMonitor& cpuMon) {
    bool ua = (config.lang == Language::Ukrainian);
    DWORD pidToKill = config.selectedPid;

    if (pidToKill == 0) {
        std::wcout << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
        std::wcout << VT_FG_BRIGHT_RED
            << (ua ? L"\n  Процес не виділено!" : L"\n  No process selected!")
            << VT_RESET;
        Sleep(1200);
        std::wcout << VT_CURSOR_HIDE << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
        cpuMon.Reset();
        return;
    }

    // Меню сигналів (Windows-аналоги)
    const int NUM_SIGNALS = 2;
    std::wstring signals[NUM_SIGNALS] = {
        ua ? L"TERMINATE (жорстке завершення)" : L"TERMINATE (force kill)",
        ua ? L"WM_CLOSE  (м'яке завершення)"  : L"WM_CLOSE  (graceful close)"
    };
    int selected = 0;

    while (true) {
        std::wcout << L"\x1b[2J\x1b[H";
        std::wcout << VT_FG_BRIGHT_RED
            << (ua ? L"  Завершити процес PID " : L"  Kill process PID ")
            << pidToKill << VT_RESET << L"\n\n";

        std::wcout << (ua ? L"  Оберіть сигнал:\n\n" : L"  Choose signal:\n\n");

        for (int i = 0; i < NUM_SIGNALS; ++i) {
            if (i == selected) {
                std::wcout << L"  " << VT_BG_CYAN << VT_FG_BLACK << L" " << signals[i] << L" " << VT_RESET << L"\n";
            } else {
                std::wcout << L"   " << signals[i] << L"\n";
            }
        }

        std::wcout << L"\n" << VT_FG_DARKGRAY
            << (ua ? L"  [Enter] Підтвердити  [Esc] Скасувати" : L"  [Enter] Confirm  [Esc] Cancel")
            << VT_RESET;

        int ch = _getch();
        if (ch == 27) { // Esc
            std::wcout << VT_CURSOR_HIDE << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
            cpuMon.Reset();
            return;
        }
        if (ch == 0 || ch == 0xE0) {
            int ext = _getch();
            if (ext == 72) { // Up
                if (selected > 0) selected--;
            } else if (ext == 80) { // Down
                if (selected < NUM_SIGNALS - 1) selected++;
            }
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            break;
        }
    }

    // Виконання
    DWORD result = 0;
    if (selected == 0) {
        // TERMINATE — жорстке завершення
        result = SystemManager::KillProcess(pidToKill);
    } else {
        // WM_CLOSE — м'яке завершення (закрити всі вікна процесу)
        result = SystemManager::CloseProcess(pidToKill);
    }

    std::wcout << L"\x1b[2J\x1b[H";
    if (result == 0) {
        std::wcout << VT_FG_BRIGHT_GREEN
            << (ua ? L"\n  Успішно завершено! PID: " : L"\n  Successfully killed! PID: ")
            << pidToKill << VT_RESET;
    } else if (result == ERROR_ACCESS_DENIED) {
        std::wcout << VT_FG_BRIGHT_RED
            << (ua ? L"\n  Відмовлено в доступі! PID: " : L"\n  Access denied! PID: ")
            << pidToKill << VT_RESET;
    } else {
        std::wcout << VT_FG_BRIGHT_RED
            << (ua ? L"\n  Помилка завершення! PID: " : L"\n  Failed to kill! PID: ")
            << pidToKill << VT_RESET;
    }

    Sleep(1500);
    std::wcout << VT_CURSOR_HIDE << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
    cpuMon.Reset();
}
