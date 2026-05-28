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
#include <conio.h>
#include <sstream>

// === Virtual Terminal Sequences ===
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

#define VT_BG_BLACK     L"\x1b[40m"
#define VT_BG_GREEN     L"\x1b[42m"
#define VT_BG_YELLOW    L"\x1b[43m"
#define VT_BG_CYAN      L"\x1b[46m"
#define VT_BG_DARKGRAY  L"\x1b[100m"

#define VT_CURSOR_HOME  L"\x1b[H"
#define VT_CURSOR_HIDE  L"\x1b[?25l"
#define VT_CURSOR_SHOW  L"\x1b[?25h"
#define VT_CLEAR_SCREEN L"\x1b[2J"
#define VT_CLEAR_LINE   L"\x1b[K"
#define VT_CLEAR_BELOW  L"\x1b[J"

// === Буферизований вивiд: один WriteConsoleW на весь кадр ===
static std::wstring g_outBuf;

static void BufReserve(size_t hint = 16384) {
    g_outBuf.clear();
    g_outBuf.reserve(hint);
}

static void BufFlush() {
    if (g_outBuf.empty()) return;
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD written = 0;
    const wchar_t* ptr = g_outBuf.c_str();
    DWORD remaining = static_cast<DWORD>(g_outBuf.size());
    while (remaining > 0) {
        DWORD chunk = (remaining > 32000) ? 32000 : remaining;
        if (!WriteConsoleW(hOut, ptr, chunk, &written, NULL) || written == 0) break;
        ptr += written;
        remaining -= written;
    }
    g_outBuf.clear();
}

// Append helpers
static inline void B(const wchar_t* s) { g_outBuf += s; }
static inline void B(const std::wstring& s) { g_outBuf += s; }
static inline void B(wchar_t c) { g_outBuf += c; }

static void BInt(int val, int width = 0) {
    wchar_t buf[16];
    swprintf(buf, 16, L"%*d", width, val);
    g_outBuf += buf;
}

static void BDouble(double val, int width, int prec) {
    wchar_t buf[32];
    swprintf(buf, 32, L"%*.*f", width, prec, val);
    g_outBuf += buf;
}

static void BPad(const std::wstring& s, int width, bool leftAlign = true) {
    if ((int)s.size() >= width) {
        g_outBuf += s.substr(0, width);
    } else {
        if (leftAlign) {
            g_outBuf += s;
            g_outBuf.append(width - s.size(), L' ');
        } else {
            g_outBuf.append(width - s.size(), L' ');
            g_outBuf += s;
        }
    }
}

static void BNewline() { g_outBuf += L'\n'; }

// Отримання поточної ширини консолi
static int GetConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Right - csbi.srWindow.Left + 1;
}

static int GetConsoleHeight() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

void ConsoleUI::InitConsole() {
    std::setlocale(LC_ALL, "");

    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, dwMode);

    std::wcout << L"\x1b[?1049h";
    std::wcout << VT_CURSOR_HIDE;
    std::wcout << VT_CLEAR_SCREEN << VT_CURSOR_HOME;

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
    wchar_t idBuf[4];
    swprintf(idBuf, 4, L"%2d", coreId);
    B(VT_FG_BRIGHT_CYAN); B(idBuf);
    B(VT_RESET); B(L'[');

    int totalBars = 15;
    int filledBars = static_cast<int>((percentage / 100.0) * totalBars);
    B((percentage > 80.0) ? VT_FG_BRIGHT_RED : VT_FG_BRIGHT_GREEN);
    for (int i = 0; i < totalBars; ++i) {
        B((i < filledBars) ? L'|' : L' ');
    }
    B(VT_RESET); B(L' ');
    BDouble(percentage, 5, 1); B(L"%]  ");
}

static void DrawWideBar(const wchar_t* label, double used, double total, const wchar_t* barColor) {
    B(VT_FG_BRIGHT_CYAN);
    BPad(label, 4, true);
    B(VT_RESET); B(L'[');

    int totalBars = 35;
    double percentage = (total > 0.0) ? (used / total) * 100.0 : 0.0;
    int filledBars = static_cast<int>((percentage / 100.0) * totalBars);

    B(barColor);
    for (int i = 0; i < totalBars; ++i) {
        B((i < filledBars) ? L'|' : L' ');
    }
    B(VT_RESET);
    BDouble(used, 5, 2); B(L"G/");
    BDouble(total, 0, 2); B(L"G]");
}

void ConsoleUI::RenderHelp(Language lang) {
    BufReserve(4096);
    B(VT_CURSOR_HOME);
    int w = GetConsoleWidth();
    B(VT_FG_BRIGHT_CYAN);
    B(L"=== "); B(lang == Language::Ukrainian ? L"ДОВiДКА" : L"HELP SYSTEM"); B(L" ===");
    g_outBuf.append(w > 20 ? w - 15 : 5, L' '); BNewline();
    if (lang == Language::Ukrainian) {
        B(VT_RESET); B(VT_FG_BRIGHT_CYAN); B(L" Клавiшi (F / альтернатива):"); B(VT_RESET); BNewline();
        B(L"  [F1 / H]    Вiдкрити/закрити довiдку\n");
        B(L"  [F2 / S]    Меню сортування (вибiр колонки)\n");
        B(L"  [F3 / /]    Пошук (перехiд до збiгу по iменi)\n");
        B(L"              Повторне F3 — наступний збiг\n");
        B(L"  [F4 / \\]    Фiльтр (залишає лише збiги)\n");
        B(L"  [F5 / T]    Дерево процесiв (вкл/викл)\n");
        B(L"  [P]         Показати потоки (вкл/викл)\n");
        B(L"  [F6 / >]    Змiнити напрямок сортування\n");
        B(L"  [F7 / ]]    Pri+ (пiдвищити прiоритет)\n");
        B(L"  [F8 / []    Pri- (знизити прiоритет)\n");
        B(L"  [F9 / K]    Kill (меню завершення процесу)\n");
        B(L"  [F10 / Q]   Вихiд з програми\n");
        B(L"  [Tab]       Перемикання вкладок (Main / IO)\n");
        B(L"  [Space]     Закрiпити/вiдкрiпити процес\n");
        B(L"  [L]         Змiнити мову (UA / EN)\n");
        B(L"  [I]         Змiнити iнтервал (1с/3с/5с)\n");
        B(L"  [Вгору/Вниз] Навiгацiя по списку\n");
        B(L"  [<- / ->]   Гортання сторiнок\n");
        B(L"  [Enter]     Пiдтвердити вибiр\n");
        B(L"  [Esc]       Скасувати / скинути закрiплення\n\n");
        B(VT_FG_BRIGHT_CYAN); B(L" Закрiплення (жовтий):"); B(VT_RESET); BNewline();
        B(L"  Space або Enter в пошуку/фiльтрi закрiплює\n");
        B(L"  процес. Курсор тримається на ньому при\n");
        B(L"  оновленнi списку. Скинути: Esc.\n\n");
        B(VT_FG_BRIGHT_CYAN); B(L" Kill (F9/K):"); B(VT_RESET); BNewline();
        B(L"  Завершує процес пiд курсором. Меню:\n");
        B(L"  TERMINATE — жорстке завершення\n");
        B(L"  WM_CLOSE  — м'яке (закриття вiкон)\n\n");
        B(VT_FG_DARKGRAY); B(L" Натиснiть [F1] щоб повернутись..."); B(VT_RESET);
    } else {
        B(VT_RESET); B(VT_FG_BRIGHT_CYAN); B(L" Keys (F / alternative):"); B(VT_RESET); BNewline();
        B(L"  [F1 / H]    Open/close this help\n");
        B(L"  [F2 / S]    Sort menu (choose column)\n");
        B(L"  [F3 / /]    Search (jump to match by name)\n");
        B(L"              Press F3 again — next match\n");
        B(L"  [F4 / \\]    Filter (show only matches)\n");
        B(L"  [F5 / T]    Tree view (toggle)\n");
        B(L"  [P]         Show threads (toggle)\n");
        B(L"  [F6 / >]    Toggle sort direction\n");
        B(L"  [F7 / ]]    Pri+ (raise process priority)\n");
        B(L"  [F8 / []    Pri- (lower process priority)\n");
        B(L"  [F9 / K]    Kill (process termination menu)\n");
        B(L"  [F10 / Q]   Quit\n");
        B(L"  [Tab]       Switch tab (Main / IO)\n");
        B(L"  [Space]     Pin/unpin process\n");
        B(L"  [L]         Toggle language (UA / EN)\n");
        B(L"  [I]         Change interval (1s/3s/5s)\n");
        B(L"  [Up/Down]   Navigate process list\n");
        B(L"  [<- / ->]   Page scroll\n");
        B(L"  [Enter]     Confirm selection\n");
        B(L"  [Esc]       Cancel / unpin process\n\n");
        B(VT_FG_BRIGHT_CYAN); B(L" Pinning (yellow):"); B(VT_RESET); BNewline();
        B(L"  Space or Enter in search/filter pins a\n");
        B(L"  process. Cursor stays on it during list\n");
        B(L"  refresh. Reset: Esc.\n\n");
        B(VT_FG_BRIGHT_CYAN); B(L" Kill (F9/K):"); B(VT_RESET); BNewline();
        B(L"  Terminates process under cursor. Menu:\n");
        B(L"  TERMINATE — force kill (immediate)\n");
        B(L"  WM_CLOSE  — graceful (close windows)\n\n");
        B(VT_FG_DARKGRAY); B(L" Press [F1] to return..."); B(VT_RESET);
    }
    for (int i = 0; i < 5; i++) { B(VT_CLEAR_LINE); BNewline(); }
    B(VT_CLEAR_BELOW);
    BufFlush();
}

void ConsoleUI::RenderMonitor(AppConfig& config, CpuMonitor& cpuMon) {
    BufReserve(32768);
    B(VT_CURSOR_HOME);

    int termWidth = GetConsoleWidth();
    int termHeight = GetConsoleHeight();

    const int MIN_WIDTH = 80;
    const int MIN_HEIGHT = 24;

    if (termWidth < MIN_WIDTH || termHeight < MIN_HEIGHT) {
        B(VT_CLEAR_SCREEN); B(VT_CURSOR_HOME);
        B(VT_FG_BRIGHT_RED);
        if (config.lang == Language::Ukrainian) {
            B(L"  Вiкно замале!"); B(VT_CLEAR_LINE); BNewline();
            B(L"  Мiнiмум: "); BInt(MIN_WIDTH); B(L"x"); BInt(MIN_HEIGHT); B(VT_CLEAR_LINE); BNewline();
            B(L"  Зараз:   "); BInt(termWidth); B(L"x"); BInt(termHeight); B(VT_CLEAR_LINE); BNewline();
        } else {
            B(L"  Window too small!"); B(VT_CLEAR_LINE); BNewline();
            B(L"  Minimum: "); BInt(MIN_WIDTH); B(L"x"); BInt(MIN_HEIGHT); B(VT_CLEAR_LINE); BNewline();
            B(L"  Current: "); BInt(termWidth); B(L"x"); BInt(termHeight); B(VT_CLEAR_LINE); BNewline();
        }
        B(VT_RESET); B(VT_CLEAR_BELOW);
        BufFlush();
        return;
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;
    double overallCpu = cpuMon.GetCpuUsage();
    (void)overallCpu;

    std::vector<double> coreUsages;
    {
        extern std::mutex g_dataMutex;
        extern std::vector<double> g_cachedCoreUsages;
        std::lock_guard<std::mutex> lock(g_dataMutex);
        coreUsages = g_cachedCoreUsages;
    }

    int numCols = (numCores > 8) ? 4 : 2;
    int numRows = (numCores + numCols - 1) / numCols;

    int visibleRows = termHeight - numRows - 7;
    if (visibleRows < 5) visibleRows = 5;
    config.visibleRows = visibleRows;

    // ВЕРХНЯ ПАНЕЛЬ: ЯДРА
    for (int r = 0; r < numRows; ++r) {
        for (int c = 0; c < numCols; ++c) {
            int coreIdx = c * numRows + r;
            if (coreIdx < numCores) {
                double coreLoad = (coreIdx < (int)coreUsages.size()) ? coreUsages[coreIdx] : 0.0;
                DrawCoreBar(coreIdx, coreLoad);
            } else {
                g_outBuf.append(28, L' ');
            }
        }
        B(VT_CLEAR_LINE); BNewline();
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

    // Фiльтрацiя (F4)
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

    bool filterHasResults = true;
    if (config.showFilter && !config.searchQuery.empty() && processes.empty()) {
        filterHasResults = false;
    }

    ULONGLONG uptimeMs = GetTickCount64();
    int days = static_cast<int>(uptimeMs / (1000ULL * 60 * 60 * 24));
    int hours = static_cast<int>((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int mins = static_cast<int>((uptimeMs / (1000ULL * 60)) % 60);
    int secs = static_cast<int>((uptimeMs / 1000ULL) % 60);

    bool ua = (config.lang == Language::Ukrainian);

    // Mem bar + Tasks
    DrawWideBar(L"Mem", usedMemG, totalMemG, VT_FG_BRIGHT_GREEN);
    B(VT_FG_BRIGHT_CYAN); B(ua ? L"  Задачi: " : L"  Tasks: "); B(VT_RESET);
    int runningCount = 0;
    for (const auto& p : processes) { if (p.cpuPercent > 0.0) runningCount++; }
    BInt((int)processes.size()); B(L", "); BInt(totalThreads);
    B(ua ? L" пот; " : L" thr; ");
    BInt(runningCount); B(ua ? L" активн" : L" running");
    B(VT_CLEAR_LINE); BNewline();

    // Swap bar + Uptime
    DrawWideBar(L"Swp", usedPageG, totalPageG, VT_FG_BRIGHT_RED);
    B(VT_FG_BRIGHT_CYAN); B(ua ? L"  Час роботи: " : L"  Uptime: "); B(VT_RESET);
    if (days > 0) { BInt(days); B(ua ? L" дн, " : L" days, "); }
    wchar_t timeBuf[16];
    swprintf(timeBuf, 16, L"%02d:%02d:%02d", hours, mins, secs);
    B(timeBuf);
    B(VT_CLEAR_LINE); BNewline();

    // Tabs
    std::wstring separator(termWidth, L'-');
    B(VT_FG_DARKGRAY); B(separator); BNewline();

    B((config.activeTab == TabView::Main) ? VT_BG_GREEN : VT_BG_DARKGRAY);
    B(VT_FG_BLACK); B(L" Main ");
    B((config.activeTab == TabView::IO) ? VT_BG_GREEN : VT_BG_DARKGRAY);
    B(VT_FG_BLACK); B(L" I/O ");
    B(VT_RESET); g_outBuf.append(termWidth > 12 ? termWidth - 12 : 0, L' '); BNewline();

    // Header
    B(VT_BG_GREEN); B(VT_FG_BLACK);
    int cmdColW;
    if (config.activeTab == TabView::Main) {
        int fixedColsWidth = 7 + 9 + 4 + 7 + 7 + 7 + 2 + 6 + 6 + 10;
        cmdColW = termWidth - fixedColsWidth;
        if (cmdColW < 15) cmdColW = 15;

        std::wstring mPid = L"  PID", mUser = L"USER", mPri = L"PRI",
                     mVirt = L"VIRT", mRes = L"RES", mShr = L"SHR", mState = L"S",
                     mCpu = L"CPU%", mMem = L"MEM%", mTime = L"TIME+",
                     mCmd = (ua ? L"КОМАНДА" : L"COMMAND");
        const wchar_t arrow = config.sortAscending ? L'^' : L'v';
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
            case SortColumn::Command:    mCmd = arrow + (ua ? std::wstring(L"КОМАНДА") : std::wstring(L"COMMAND")); break;
            default: break;
        }
        BPad(mPid, 7); BPad(mUser, 9); BPad(mPri, 4);
        BPad(mVirt, 7); BPad(mRes, 7); BPad(mShr, 7);
        BPad(mState, 2); BPad(mCpu, 6); BPad(mMem, 6);
        BPad(mTime, 10); BPad(mCmd, cmdColW);
    } else {
        int fixedIOWidth = 7 + 9 + 11 + 11 + 11;
        cmdColW = termWidth - fixedIOWidth;
        if (cmdColW < 15) cmdColW = 15;

        std::wstring ioPid = L"  PID", ioUser = L"USER", ioRW = L"DISK R/W",
                     ioRead = L"DISK READ", ioWrite = L"DISK WRITE",
                     ioCmd = (ua ? L"КОМАНДА" : L"Command");
        const wchar_t ioArrow = config.sortAscending ? L'^' : L'v';
        switch (config.ioSortColumn) {
            case IoSortColumn::Pid:       ioPid = std::wstring(L" PID") + ioArrow; break;
            case IoSortColumn::User:      ioUser = std::wstring(L"USER") + ioArrow; break;
            case IoSortColumn::DiskRW:    ioRW = std::wstring(L"DISK") + ioArrow + L"R/W"; break;
            case IoSortColumn::DiskRead:  ioRead = std::wstring(L"DISK") + ioArrow + L"READ"; break;
            case IoSortColumn::DiskWrite: ioWrite = std::wstring(L"DISK") + ioArrow + L"WRITE"; break;
            case IoSortColumn::Command:   ioCmd = ioArrow + (ua ? std::wstring(L"КОМАНДА") : std::wstring(L"Command")); break;
            default: break;
        }
        BPad(ioPid, 7); BPad(ioUser, 9); BPad(ioRW, 11);
        BPad(ioRead, 11); BPad(ioWrite, 11); BPad(ioCmd, cmdColW);
    }
    B(VT_CLEAR_LINE); BNewline();
    B(VT_RESET);

    // Threads injection (non-tree mode)
    if (config.showThreads && config.activeTab == TabView::Main && !config.treeView) {
        extern std::unordered_map<DWORD, std::vector<ThreadInfo>> g_cachedThreads;
        std::unordered_map<DWORD, std::vector<ThreadInfo>> threadsCopy;
        {
            extern std::mutex g_dataMutex;
            std::lock_guard<std::mutex> lock(g_dataMutex);
            threadsCopy = g_cachedThreads;
        }
        for (const auto& pair : threadsCopy) {
            std::wstring procName = L"";
            std::wstring procUser = L"-";
            for (const auto& p : processes) {
                if (p.pid == pair.first && !p.isThread) {
                    procName = p.name; procUser = p.userName; break;
                }
            }
            for (const auto& t : pair.second) {
                ProcessInfo tRow = {};
                tRow.pid = pair.first; tRow.tid = t.tid; tRow.parentPid = pair.first;
                tRow.name = procName; tRow.userName = procUser;
                tRow.priority = t.priority; tRow.cpuPercent = t.cpuPercent;
                tRow.cpuTime = t.cpuTime; tRow.state = t.state;
                tRow.isThread = true;
                processes.push_back(std::move(tRow));
            }
        }
    }

    // Sorting
    std::sort(processes.begin(), processes.end(), [&config](const ProcessInfo& a, const ProcessInfo& b) {
        int cmp = 0;
        if (config.activeTab == TabView::IO) {
            switch (config.ioSortColumn) {
                case IoSortColumn::Pid: cmp = (a.pid > b.pid) ? 1 : (a.pid < b.pid) ? -1 : 0; break;
                case IoSortColumn::User: cmp = (a.userName < b.userName) ? 1 : (a.userName > b.userName) ? -1 : 0; break;
                case IoSortColumn::DiskRW: { auto av = a.ioDiskRead+a.ioDiskWrite, bv = b.ioDiskRead+b.ioDiskWrite; cmp = (av>bv)?1:(av<bv)?-1:0; } break;
                case IoSortColumn::DiskRead: cmp = (a.ioDiskRead > b.ioDiskRead) ? 1 : (a.ioDiskRead < b.ioDiskRead) ? -1 : 0; break;
                case IoSortColumn::DiskWrite: cmp = (a.ioDiskWrite > b.ioDiskWrite) ? 1 : (a.ioDiskWrite < b.ioDiskWrite) ? -1 : 0; break;
                case IoSortColumn::Command: cmp = (a.name < b.name) ? 1 : (a.name > b.name) ? -1 : 0; break;
                default: { auto av = a.ioDiskRead+a.ioDiskWrite, bv = b.ioDiskRead+b.ioDiskWrite; cmp = (av>bv)?1:(av<bv)?-1:0; } break;
            }
        } else {
            switch (config.sortColumn) {
                case SortColumn::Pid: cmp = (a.pid > b.pid) ? 1 : (a.pid < b.pid) ? -1 : 0; break;
                case SortColumn::User: cmp = (a.userName < b.userName) ? 1 : (a.userName > b.userName) ? -1 : 0; break;
                case SortColumn::Priority: cmp = (a.priority > b.priority) ? 1 : (a.priority < b.priority) ? -1 : 0; break;
                case SortColumn::Virt: cmp = (a.virtualMemory > b.virtualMemory) ? 1 : (a.virtualMemory < b.virtualMemory) ? -1 : 0; break;
                case SortColumn::Res: cmp = (a.memoryUsage > b.memoryUsage) ? 1 : (a.memoryUsage < b.memoryUsage) ? -1 : 0; break;
                case SortColumn::Shr: cmp = (a.sharedMemory > b.sharedMemory) ? 1 : (a.sharedMemory < b.sharedMemory) ? -1 : 0; break;
                case SortColumn::State: cmp = (a.state < b.state) ? 1 : (a.state > b.state) ? -1 : 0; break;
                case SortColumn::CpuPercent: cmp = (a.cpuPercent > b.cpuPercent) ? 1 : (a.cpuPercent < b.cpuPercent) ? -1 : 0; break;
                case SortColumn::MemPercent: cmp = (a.memPercent > b.memPercent) ? 1 : (a.memPercent < b.memPercent) ? -1 : 0; break;
                case SortColumn::Time: cmp = (a.cpuTime > b.cpuTime) ? 1 : (a.cpuTime < b.cpuTime) ? -1 : 0; break;
                case SortColumn::Command: cmp = (a.name < b.name) ? 1 : (a.name > b.name) ? -1 : 0; break;
                default: cmp = (a.memoryUsage > b.memoryUsage) ? 1 : (a.memoryUsage < b.memoryUsage) ? -1 : 0; break;
            }
        }
        if (cmp == 0) return a.pid < b.pid;
        return config.sortAscending ? (cmp < 0) : (cmp > 0);
    });

    // Tree view
    if (config.treeView) {
        std::unordered_map<DWORD, std::vector<ThreadInfo>> threadsCopyTree;
        if (config.showThreads && config.activeTab == TabView::Main) {
            extern std::unordered_map<DWORD, std::vector<ThreadInfo>> g_cachedThreads;
            extern std::mutex g_dataMutex;
            std::lock_guard<std::mutex> lock(g_dataMutex);
            threadsCopyTree = g_cachedThreads;
        }
        processes.erase(std::remove_if(processes.begin(), processes.end(),
            [](const ProcessInfo& p) { return p.isThread; }), processes.end());

        std::unordered_map<DWORD, std::vector<int>> childrenMap;
        std::unordered_map<DWORD, int> pidIndex;
        for (int i = 0; i < (int)processes.size(); ++i) {
            pidIndex[processes[i].pid] = i;
            childrenMap[processes[i].parentPid].push_back(i);
        }
        std::vector<int> roots;
        for (int i = 0; i < (int)processes.size(); ++i) {
            if (pidIndex.find(processes[i].parentPid) == pidIndex.end()) roots.push_back(i);
        }
        struct TreeEntry { int idx; int depth; };
        std::vector<TreeEntry> treeOrder;
        std::function<void(int, int)> buildTree = [&](int idx, int depth) {
            treeOrder.push_back({ idx, depth });
            auto it = childrenMap.find(processes[idx].pid);
            if (it != childrenMap.end()) {
                for (int childIdx : it->second) buildTree(childIdx, depth + 1);
            }
        };
        for (int root : roots) buildTree(root, 0);

        std::vector<ProcessInfo> treeProcesses;
        treeProcesses.reserve(treeOrder.size());
        for (const auto& entry : treeOrder) {
            ProcessInfo p = processes[entry.idx];
            if (entry.depth > 0) {
                std::wstring prefix;
                for (int d = 0; d < entry.depth - 1; ++d) prefix += L"  ";
                prefix += L"|-";
                p.name = prefix + p.name;
            }
            treeProcesses.push_back(p);
            if (config.showThreads && config.activeTab == TabView::Main) {
                auto tIt = threadsCopyTree.find(p.pid);
                if (tIt != threadsCopyTree.end()) {
                    for (const auto& t : tIt->second) {
                        ProcessInfo tRow = {};
                        tRow.pid = p.pid; tRow.tid = t.tid; tRow.parentPid = p.pid;
                        std::wstring tPrefix;
                        for (int d = 0; d < entry.depth; ++d) tPrefix += L"  ";
                        tPrefix += L"  |-";
                        tRow.name = tPrefix + p.name; tRow.userName = p.userName;
                        tRow.priority = t.priority; tRow.cpuPercent = t.cpuPercent;
                        tRow.cpuTime = t.cpuTime; tRow.state = t.state;
                        tRow.isThread = true;
                        treeProcesses.push_back(std::move(tRow));
                    }
                }
            }
        }
        processes = std::move(treeProcesses);
    }

    // Search (F3)
    bool searchFound = true;
    if (config.showSearch && !config.searchQuery.empty()) {
        std::wstring query = config.searchQuery;
        bool found = false; int matchCount = 0;
        for (int i = 0; i < (int)processes.size(); ++i) {
            std::wstring nameLower = processes[i].name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
            if (nameLower.find(query) == 0) {
                if (matchCount == config.searchMatchIndex) {
                    config.pageOffset = (i / visibleRows) * visibleRows;
                    config.selectedRow = i - config.pageOffset;
                    found = true; break;
                }
                matchCount++;
            }
        }
        if (!found && matchCount > 0) {
            config.searchMatchIndex = 0;
            for (int i = 0; i < (int)processes.size(); ++i) {
                std::wstring nameLower = processes[i].name;
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::towlower);
                if (nameLower.find(query) == 0) {
                    config.pageOffset = (i / visibleRows) * visibleRows;
                    config.selectedRow = i - config.pageOffset;
                    found = true; break;
                }
            }
        }
        searchFound = found;
    } else if (config.pinnedPid != 0) {
        for (int i = 0; i < (int)processes.size(); ++i) {
            if (processes[i].pid == config.pinnedPid) {
                config.pageOffset = (i / visibleRows) * visibleRows;
                config.selectedRow = i - config.pageOffset;
                break;
            }
        }
    }

    if (config.pageOffset >= (int)processes.size()) {
        config.pageOffset = (std::max)(0, (int)processes.size() - visibleRows);
    }
    if (config.selectedRow >= (std::min)(visibleRows, (int)processes.size() - config.pageOffset))
        config.selectedRow = (std::min)(visibleRows, (int)processes.size() - config.pageOffset) - 1;
    if (config.selectedRow < 0) config.selectedRow = 0;

    // === PROCESS ROWS ===
    auto formatMem = [](SIZE_T bytes) -> std::wstring {
        double kb = bytes / 1024.0;
        wchar_t buf[16];
        if (kb >= 1024.0) { swprintf(buf, 16, L"%.0fM", kb / 1024.0); return buf; }
        swprintf(buf, 16, L"%.0fK", kb); return buf;
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
    auto formatIO = [](ULONGLONG bytes) -> std::wstring {
        wchar_t buf[16];
        if (bytes >= 1024ULL * 1024 * 1024) swprintf(buf, 16, L"%.1fG", bytes / (1024.0 * 1024.0 * 1024.0));
        else if (bytes >= 1024ULL * 1024) swprintf(buf, 16, L"%.1fM", bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024ULL) swprintf(buf, 16, L"%.2fK", bytes / 1024.0);
        else swprintf(buf, 16, L"%.2fB", (double)bytes);
        return buf;
    };

    int printedCount = 0;
    for (int i = config.pageOffset; i < (std::min)(config.pageOffset + visibleRows, (int)processes.size()); ++i) {
        const auto& proc = processes[i];

        std::wstring name = proc.name;
        if ((int)name.length() > cmdColW - 1) name = name.substr(0, cmdColW - 2) + L"~";

        std::wstring user = proc.userName;
        if (user.length() > 8) user = user.substr(0, 8);

        bool isSelected = (printedCount == config.selectedRow);
        bool isPinned = (config.pinnedPid != 0 && proc.pid == config.pinnedPid);

        if (isSelected) {
            config.selectedPid = proc.pid;
            if (isPinned) { B(VT_BG_YELLOW); B(VT_FG_BLACK); }
            else { B(VT_BG_CYAN); B(VT_FG_BLACK); }
        } else if (isPinned) {
            B(VT_FG_YELLOW);
        } else if (proc.isThread) {
            B(VT_FG_GREEN);
        } else {
            B(VT_FG_BRIGHT_CYAN);
        }

        // PID/TID
        if (proc.isThread) { BInt(proc.tid, 6); }
        else { BInt(proc.pid, 6); }
        B(L' ');

        if (!isSelected && !isPinned) B(VT_RESET);
        if (proc.isThread && !isSelected && !isPinned) B(VT_FG_GREEN);
        BPad(user, 9);

        if (config.activeTab == TabView::Main) {
            BInt(proc.priority, 3); B(L' ');
            BPad(formatMem(proc.virtualMemory), 6, false); B(L' ');
            if (!isSelected && !isPinned) B(VT_FG_BRIGHT_GREEN);
            BPad(formatMem(proc.memoryUsage), 6, false); B(L' ');
            if (!isSelected && !isPinned) B(VT_RESET);
            BPad(formatMem(proc.sharedMemory), 6, false); B(L' ');
            B(proc.state); B(L' ');
            if (!isSelected && !isPinned) {
                B((proc.cpuPercent > 5.0) ? VT_FG_BRIGHT_RED : VT_FG_BRIGHT_GREEN);
            }
            BDouble(proc.cpuPercent, 5, 1);
            if (!isSelected && !isPinned) B(VT_RESET);
            BDouble(proc.memPercent, 5, 1); B(L' ');
            BPad(formatTime(proc.cpuTime), 9, false); B(L' ');
        } else {
            if (!isSelected && !isPinned) B(VT_RESET);
            std::wstring rw = formatIO(proc.ioDiskRead + proc.ioDiskWrite) + L"/s";
            BPad(rw, 10, false); B(L' ');
            if (!isSelected && !isPinned) B(VT_FG_BRIGHT_GREEN);
            std::wstring dr = formatIO(proc.ioDiskRead) + L"/s";
            BPad(dr, 10, false); B(L' ');
            if (!isSelected && !isPinned) B(VT_FG_BRIGHT_RED);
            std::wstring dw = formatIO(proc.ioDiskWrite) + L"/s";
            BPad(dw, 10, false); B(L' ');
            if (!isSelected && !isPinned) B(VT_RESET);
        }

        BPad(name, cmdColW);
        B(VT_RESET); B(VT_CLEAR_LINE); BNewline();
        printedCount++;
    }

    // Empty rows
    while (printedCount < visibleRows) {
        B(VT_CLEAR_LINE); g_outBuf.append(termWidth, L' '); BNewline();
        printedCount++;
    }

    // Footer
    B(VT_FG_DARKGRAY); B(separator); BNewline();

    if (config.showSearch || config.showFilter) {
        bool hasResults = config.showSearch ? searchFound : filterHasResults;
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE);
        B(config.showSearch ? L" F3 " : L" F4 ");
        B(VT_BG_CYAN); B(VT_FG_BLACK);
        B(config.showSearch ? (ua ? L"Пошук: " : L"Search: ") : (ua ? L"Фiльтр: " : L"Filter: "));
        B(VT_RESET); B(hasResults ? VT_FG_BRIGHT_GREEN : VT_FG_BRIGHT_RED);
        B(config.searchQuery); B(L"_");
        B(VT_RESET); B(VT_CLEAR_LINE);
    } else {
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F1 "); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Довiдка" : L"Help  ");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F2 "); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Сорт  " : L"SortBy");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F3 "); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Пошук " : L"Search");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F4 "); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Фiльтр" : L"Filter");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F5 "); B(VT_BG_CYAN); B(VT_FG_BLACK);
        B(config.treeView ? (ua ? L"Список" : L"List  ") : (ua ? L"Дерево" : L"Tree  "));
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F6 "); B(VT_BG_CYAN); B(VT_FG_BLACK);
        B(config.sortAscending ? L"^" : L"v");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F7 "); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Прiор+" : L"Pri+  ");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F8 "); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Прiор-" : L"Pri-  ");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" F9 "); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Заверш" : L"Kill  ");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L"F10 "); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Вихiд " : L"Quit  ");
        B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" Tab"); B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Вкладка" : L"Tab   ");
        B(VT_RESET); B(VT_FG_DARKGRAY); B(L" [L]");
        B(VT_FG_BRIGHT_CYAN); B(ua ? L"UA" : L"EN");
        B(VT_FG_DARKGRAY); B(L" [I]");
        B(VT_FG_BRIGHT_CYAN); BInt(config.refreshInterval / 1000); B(ua ? L"с" : L"s");
        B(VT_RESET); B(VT_CLEAR_LINE);
    }
    B(VT_CLEAR_BELOW);

    BufFlush();
}

void ConsoleUI::RenderSortMenu(AppConfig& config) {
    BufReserve(4096);
    B(VT_CURSOR_HOME);
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
    if (config.activeTab == TabView::IO) { items = ioItems; itemCount = 6; }
    else { items = mainItems; itemCount = 11; }

    bool ua = (config.lang == Language::Ukrainian);
    B(VT_BG_GREEN); B(VT_FG_BLACK);
    std::wstring title = ua ? L" Сортувати за" : L" Sort by";
    B(title); g_outBuf.append(termWidth > (int)title.size() ? termWidth - (int)title.size() : 0, L' ');
    B(VT_RESET); BNewline();

    for (int i = 0; i < itemCount; ++i) {
        if (i == config.sortMenuIndex) { B(VT_BG_CYAN); B(VT_FG_BLACK); }
        else { B(VT_RESET); }
        B(L"  ");
        BPad(items[i], termWidth - 2);
        B(VT_RESET); BNewline();
    }

    B(VT_RESET);
    for (int i = itemCount + 1; i < 25; ++i) {
        B(VT_CLEAR_LINE); g_outBuf.append(termWidth, L' '); BNewline();
    }

    std::wstring sep(termWidth, L'-');
    B(VT_FG_DARKGRAY); B(sep); BNewline();
    B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L"Enter");
    B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Обрати" : L"Sort  ");
    B(VT_BG_DARKGRAY); B(VT_FG_BRIGHT_WHITE); B(L" Esc ");
    B(VT_BG_CYAN); B(VT_FG_BLACK); B(ua ? L"Назад " : L"Cancel");
    B(VT_RESET); B(VT_CLEAR_LINE); B(VT_CLEAR_BELOW);

    BufFlush();
}

void ConsoleUI::HandleKillDialog(AppConfig& config, CpuMonitor& cpuMon) {
    // Kill dialog uses direct wcout since it's interactive and rare
    bool ua = (config.lang == Language::Ukrainian);
    DWORD pidToKill = config.selectedPid;

    if (pidToKill == 0) {
        std::wcout << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
        std::wcout << VT_FG_BRIGHT_RED
            << (ua ? L"\n  Процес не видiлено!" : L"\n  No process selected!")
            << VT_RESET;
        Sleep(1200);
        std::wcout << VT_CURSOR_HIDE << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
        cpuMon.Reset();
        return;
    }

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
        std::wcout << (ua ? L"  Оберiть сигнал:\n\n" : L"  Choose signal:\n\n");

        for (int i = 0; i < NUM_SIGNALS; ++i) {
            if (i == selected)
                std::wcout << L"  " << VT_BG_CYAN << VT_FG_BLACK << L" " << signals[i] << L" " << VT_RESET << L"\n";
            else
                std::wcout << L"   " << signals[i] << L"\n";
        }
        std::wcout << L"\n" << VT_FG_DARKGRAY
            << (ua ? L"  [Enter] Пiдтвердити  [Esc] Скасувати" : L"  [Enter] Confirm  [Esc] Cancel")
            << VT_RESET;

        int ch = _getch();
        if (ch == 27) {
            std::wcout << VT_CURSOR_HIDE << VT_CLEAR_SCREEN << VT_CURSOR_HOME;
            cpuMon.Reset(); return;
        }
        if (ch == 0 || ch == 0xE0) {
            int ext = _getch();
            if (ext == 72 && selected > 0) selected--;
            else if (ext == 80 && selected < NUM_SIGNALS - 1) selected++;
            continue;
        }
        if (ch == '\r' || ch == '\n') break;
    }

    DWORD result = (selected == 0) ? SystemManager::KillProcess(pidToKill) : SystemManager::CloseProcess(pidToKill);

    std::wcout << L"\x1b[2J\x1b[H";
    if (result == 0) {
        std::wcout << VT_FG_BRIGHT_GREEN
            << (ua ? L"\n  Успiшно завершено! PID: " : L"\n  Successfully killed! PID: ")
            << pidToKill << VT_RESET;
    } else if (result == ERROR_ACCESS_DENIED) {
        std::wcout << VT_FG_BRIGHT_RED
            << (ua ? L"\n  Вiдмовлено в доступi! PID: " : L"\n  Access denied! PID: ")
            << pidToKill << VT_RESET;
    } else if (result == ERROR_INVALID_PARAMETER || result == ERROR_PROCESS_ABORTED) {
        std::wcout << VT_FG_BRIGHT_RED
            << (ua ? L"\n  Процес не знайдено або вже завершено! PID: " : L"\n  Process not found or already terminated! PID: ")
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
