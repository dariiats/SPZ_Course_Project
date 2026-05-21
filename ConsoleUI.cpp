#include "ConsoleUI.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>
#include <cmath>
#include <sstream>

enum ConsoleColors {
    BLACK = 0, BLUE = 1, GREEN = 2, CYAN = 3, RED = 4, MAGENTA = 5, BROWN = 6, LIGHTGRAY = 7,
    DARKGRAY = 8, LIGHTBLUE = 9, LIGHTGREEN = 10, LIGHTCYAN = 11, LIGHTRED = 12, WHITE = 15,
    BG_GREEN_FG_BLACK = (GREEN << 4) | BLACK,
    BG_CYAN_FG_BLACK = (CYAN << 4) | BLACK,
    FG_BRIGHT_GREEN = LIGHTGREEN,
    FG_BRIGHT_CYAN = LIGHTCYAN,
    FG_BRIGHT_RED = LIGHTRED
};

// === FrameBuffer implementation ===

void FrameBuffer::Init(int width, int height) {
    m_width = width;
    m_height = height;
    m_buffer.resize(width * height);
    Clear();
}

void FrameBuffer::Clear() {
    for (auto& ci : m_buffer) {
        ci.Char.UnicodeChar = L' ';
        ci.Attributes = 7;
    }
    m_curX = 0;
    m_curY = 0;
    m_attr = 7;
}

void FrameBuffer::SetCursor(int x, int y) {
    m_curX = x;
    m_curY = y;
}

void FrameBuffer::SetColor(WORD attr) {
    m_attr = attr;
}

void FrameBuffer::Print(const std::wstring& text) {
    for (wchar_t ch : text) {
        if (ch == L'\n') {
            // Заповнити залишок рядка пробілами
            while (m_curX < m_width) {
                if (m_curY < m_height) {
                    int idx = m_curY * m_width + m_curX;
                    m_buffer[idx].Char.UnicodeChar = L' ';
                    m_buffer[idx].Attributes = m_attr;
                }
                m_curX++;
            }
            m_curX = 0;
            m_curY++;
        } else {
            if (m_curY < m_height && m_curX < m_width) {
                int idx = m_curY * m_width + m_curX;
                m_buffer[idx].Char.UnicodeChar = ch;
                m_buffer[idx].Attributes = m_attr;
            }
            m_curX++;
        }
    }
}

void FrameBuffer::PrintChar(wchar_t ch) {
    if (m_curY < m_height && m_curX < m_width) {
        int idx = m_curY * m_width + m_curX;
        m_buffer[idx].Char.UnicodeChar = ch;
        m_buffer[idx].Attributes = m_attr;
    }
    m_curX++;
}

void FrameBuffer::NewLine() {
    // Заповнити залишок рядка пробілами
    while (m_curX < m_width) {
        if (m_curY < m_height) {
            int idx = m_curY * m_width + m_curX;
            m_buffer[idx].Char.UnicodeChar = L' ';
            m_buffer[idx].Attributes = m_attr;
        }
        m_curX++;
    }
    m_curX = 0;
    m_curY++;
}

void FrameBuffer::PadToEnd() {
    while (m_curX < m_width) {
        if (m_curY < m_height) {
            int idx = m_curY * m_width + m_curX;
            m_buffer[idx].Char.UnicodeChar = L' ';
            m_buffer[idx].Attributes = m_attr;
        }
        m_curX++;
    }
}

void FrameBuffer::Flush(HANDLE hConsole) {
    COORD bufSize = { (SHORT)m_width, (SHORT)m_height };
    COORD bufCoord = { 0, 0 };
    SMALL_RECT writeRegion = { 0, 0, (SHORT)(m_width - 1), (SHORT)(m_height - 1) };
    WriteConsoleOutputW(hConsole, m_buffer.data(), bufSize, bufCoord, &writeRegion);
}

// === Глобальний буфер кадру ===
static FrameBuffer g_frame;
static bool g_frameInited = false;

static int GetConsoleWidth() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    int width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    return (width > 60) ? width : 60;
}

static int GetConsoleHeight() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    return csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

static void EnsureFrame() {
    int w = GetConsoleWidth();
    int h = GetConsoleHeight();
    if (!g_frameInited || g_frame.GetWidth() != w || g_frame.GetHeight() != h) {
        g_frame.Init(w, h);
        g_frameInited = true;
    }
}

// Допоміжна: форматований рядок фіксованої ширини (вирівнювання вправо)
static std::wstring RightAlign(const std::wstring& s, int width) {
    if ((int)s.length() >= width) return s.substr(0, width);
    return std::wstring(width - s.length(), L' ') + s;
}

static std::wstring LeftAlign(const std::wstring& s, int width) {
    if ((int)s.length() >= width) return s.substr(0, width);
    return s + std::wstring(width - s.length(), L' ');
}

// === ConsoleUI ===

void ConsoleUI::InitConsole() {
    std::setlocale(LC_ALL, "");
    SetCursorVisibility(false);
    // Встановлюємо розмір буфера консолі = розміру вікна (без скролу)
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole, &csbi);
    int w = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int h = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    COORD bufSize = { (SHORT)w, (SHORT)h };
    SetConsoleScreenBufferSize(hConsole, bufSize);
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

// Малює бар ядра у FrameBuffer
static void DrawCoreBarFB(FrameBuffer& fb, int coreId, double percentage) {
    fb.SetColor(FG_BRIGHT_CYAN);
    std::wstring idStr = (coreId < 10) ? L" " + std::to_wstring(coreId) : std::to_wstring(coreId);
    fb.Print(idStr);
    fb.SetColor(WHITE);
    fb.PrintChar(L'[');

    int totalBars = 15;
    int filledBars = static_cast<int>((percentage / 100.0) * totalBars);
    WORD barColor = (percentage > 80.0) ? FG_BRIGHT_RED : FG_BRIGHT_GREEN;

    fb.SetColor(barColor);
    for (int i = 0; i < totalBars; ++i) {
        fb.PrintChar(i < filledBars ? L'|' : L' ');
    }

    fb.SetColor(WHITE);
    fb.PrintChar(L' ');

    wchar_t buf[8];
    swprintf(buf, 8, L"%5.1f", percentage);
    fb.Print(std::wstring(buf));
    fb.Print(L"%]  ");
}

// Малює широкий бар (Mem/Swp)
static void DrawWideBarFB(FrameBuffer& fb, const std::wstring& label, double used, double total, WORD barColor) {
    fb.SetColor(FG_BRIGHT_CYAN);
    fb.Print(LeftAlign(label, 4));
    fb.SetColor(WHITE);
    fb.PrintChar(L'[');

    int totalBars = 35;
    double percentage = (total > 0) ? (used / total) * 100.0 : 0.0;
    int filledBars = static_cast<int>((percentage / 100.0) * totalBars);

    fb.SetColor(barColor);
    for (int i = 0; i < totalBars; ++i) {
        fb.PrintChar(i < filledBars ? L'|' : L' ');
    }

    fb.SetColor(WHITE);
    wchar_t buf[32];
    swprintf(buf, 32, L"%5.2fG/%5.2fG]", used, total);
    fb.Print(std::wstring(buf));
}

void ConsoleUI::RenderHelp(Language lang) {
    EnsureFrame();
    g_frame.Clear();

    int w = g_frame.GetWidth();
    g_frame.SetColor(FG_BRIGHT_CYAN);
    g_frame.Print(L"=== ");
    g_frame.Print(lang == Language::Ukrainian ? L"ДОВІДКА" : L"HELP SYSTEM");
    g_frame.Print(L" ===");
    g_frame.NewLine();

    g_frame.SetColor(WHITE);
    g_frame.Print(L"  [F1 / H] - Close/open this help window"); g_frame.NewLine();
    g_frame.Print(L"  [F2 / L] - Toggle language (UA / EN)"); g_frame.NewLine();
    g_frame.Print(L"  [F6 / I] - Change refresh interval"); g_frame.NewLine();
    g_frame.Print(L"  [F9 / K] - Kill process via PID"); g_frame.NewLine();
    g_frame.Print(L"  [<- / ->] - Page scroll"); g_frame.NewLine();
    g_frame.NewLine();
    g_frame.Print(L" Press [H] to return..."); g_frame.NewLine();

    g_frame.Flush(GetStdHandle(STD_OUTPUT_HANDLE));
}

void ConsoleUI::RenderMonitor(AppConfig& config, CpuMonitor& cpuMon, ProcessMonitor& procMon) {
    // === Крок 1: збираємо ВСІ дані до малювання ===
    EnsureFrame();
    int termWidth = g_frame.GetWidth();

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int numCores = sysInfo.dwNumberOfProcessors;
    double overallCpu = cpuMon.GetCpuUsage();

    int numCols = (numCores > 8) ? 4 : 2;
    int numRows = (numCores + numCols - 1) / numCols;

    MEMORYSTATUSEX memInfo = { sizeof(MEMORYSTATUSEX) };
    GlobalMemoryStatusEx(&memInfo);
    double totalMemG = memInfo.ullTotalPhys / (1024.0 * 1024.0 * 1024.0);
    double usedMemG = (memInfo.ullTotalPhys - memInfo.ullAvailPhys) / (1024.0 * 1024.0 * 1024.0);
    double totalPageG = memInfo.ullTotalPageFile / (1024.0 * 1024.0 * 1024.0);
    double usedPageG = (memInfo.ullTotalPageFile - memInfo.ullAvailPageFile) / (1024.0 * 1024.0 * 1024.0);

    std::vector<ProcessInfo> processes = SystemManager::GetProcesses();
    procMon.UpdateRates(processes);

    ULONGLONG uptimeMs = GetTickCount64();
    int days = (int)(uptimeMs / (1000ULL * 60 * 60 * 24));
    int hours = (int)((uptimeMs / (1000ULL * 60 * 60)) % 24);
    int mins = (int)((uptimeMs / (1000ULL * 60)) % 60);
    int secs = (int)((uptimeMs / 1000ULL) % 60);

    std::sort(processes.begin(), processes.end(), [&config](const ProcessInfo& a, const ProcessInfo& b) {
        if (config.activeTab == TabView::IO)
            return (a.ioReadBytes + a.ioWriteBytes) > (b.ioReadBytes + b.ioWriteBytes);
        return a.memoryUsage > b.memoryUsage;
    });

    if (config.pageOffset >= (int)processes.size()) config.pageOffset = 0;

    // === Крок 2: малюємо в off-screen буфер ===
    g_frame.Clear();
    g_frame.SetCursor(0, 0);

    // ВЕРХНЯ ПАНЕЛЬ: СІТКА ЯДЕР
    for (int r = 0; r < numRows; ++r) {
        for (int c = 0; c < numCols; ++c) {
            int coreIdx = c * numRows + r;
            if (coreIdx < numCores) {
                double simulatedCoreLoad = overallCpu + (coreIdx % 3) * 2.5 - (coreIdx % 2) * 1.5;
                if (simulatedCoreLoad < 0) simulatedCoreLoad = 0;
                if (simulatedCoreLoad > 100) simulatedCoreLoad = 100;
                DrawCoreBarFB(g_frame, coreIdx, simulatedCoreLoad);
            } else {
                g_frame.SetColor(WHITE);
                g_frame.Print(std::wstring(28, L' '));
            }
        }
        g_frame.NewLine();
    }

    // РЯДОК Mem + Tasks
    DrawWideBarFB(g_frame, L"Mem", usedMemG, totalMemG, FG_BRIGHT_GREEN);
    g_frame.SetColor(FG_BRIGHT_CYAN);
    g_frame.Print(L"  Tasks: ");
    g_frame.SetColor(WHITE);
    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%zu, 128 thr; 1 running", processes.size());
        g_frame.Print(buf);
    }
    g_frame.NewLine();

    // РЯДОК Swp + Load avg
    DrawWideBarFB(g_frame, L"Swp", usedPageG, totalPageG, FG_BRIGHT_RED);
    g_frame.SetColor(FG_BRIGHT_CYAN);
    g_frame.Print(L"  Load avg: ");
    g_frame.SetColor(WHITE);
    {
        wchar_t buf[64];
        swprintf(buf, 64, L"%.2f %.2f %.2f",
            (overallCpu / 100.0) + 0.15,
            (overallCpu / 100.0) + 0.08,
            (overallCpu / 100.0) + 0.02);
        g_frame.Print(buf);
    }
    g_frame.NewLine();

    // РЯДОК Uptime
    g_frame.SetColor(WHITE);
    g_frame.Print(std::wstring(51, L' '));
    g_frame.SetColor(FG_BRIGHT_CYAN);
    g_frame.Print(L"  Uptime: ");
    g_frame.SetColor(WHITE);
    {
        wchar_t buf[64];
        if (days > 0) swprintf(buf, 64, L"%d days, %02d:%02d:%02d", days, hours, mins, secs);
        else swprintf(buf, 64, L"%02d:%02d:%02d", hours, mins, secs);
        g_frame.Print(buf);
    }
    g_frame.NewLine();

    // === Розділювач ===
    g_frame.SetColor(DARKGRAY);
    g_frame.Print(std::wstring(termWidth, L'-'));
    g_frame.NewLine();

    // === Вкладки ===
    g_frame.SetColor(config.activeTab == TabView::Main ? BG_GREEN_FG_BLACK : ((DARKGRAY << 4) | BLACK));
    g_frame.Print(L" Main ");
    g_frame.SetColor(config.activeTab == TabView::IO ? BG_GREEN_FG_BLACK : ((DARKGRAY << 4) | BLACK));
    g_frame.Print(L" I/O ");
    g_frame.SetColor(WHITE);
    g_frame.NewLine();

    // === Шапка таблиці ===
    g_frame.SetColor(BG_GREEN_FG_BLACK);
    int cmdColW;
    if (config.activeTab == TabView::Main) {
        int fixedColsWidth = 7 + 9 + 4 + 4 + 7 + 7 + 7 + 2 + 6 + 6 + 10;
        cmdColW = termWidth - fixedColsWidth;
        if (cmdColW < 15) cmdColW = 15;
        g_frame.Print(LeftAlign(L"  PID", 7));
        g_frame.Print(LeftAlign(L"USER", 9));
        g_frame.Print(LeftAlign(L"PRI", 4));
        g_frame.Print(LeftAlign(L"NI", 4));
        g_frame.Print(LeftAlign(L"VIRT", 7));
        g_frame.Print(LeftAlign(L"RES", 7));
        g_frame.Print(LeftAlign(L"SHR", 7));
        g_frame.Print(LeftAlign(L"S", 2));
        g_frame.Print(LeftAlign(L"CPU%", 6));
        g_frame.Print(LeftAlign(L"MEM%", 6));
        g_frame.Print(LeftAlign(L"TIME+", 10));
        g_frame.Print(LeftAlign(config.lang == Language::Ukrainian ? L"КОМАНДА" : L"COMMAND", cmdColW));
    } else {
        int fixedIOWidth = 7 + 9 + 4 + 9 + 11 + 12 + 6 + 6;
        cmdColW = termWidth - fixedIOWidth;
        if (cmdColW < 15) cmdColW = 15;
        g_frame.Print(LeftAlign(L"  PID", 7));
        g_frame.Print(LeftAlign(L"USER", 9));
        g_frame.Print(LeftAlign(L"IO", 4));
        g_frame.Print(LeftAlign(L"DISK R/W", 9));
        g_frame.Print(LeftAlign(L"DISK READ", 11));
        g_frame.Print(LeftAlign(L"DISK WRITE", 12));
        g_frame.Print(LeftAlign(L"SWPD%", 6));
        g_frame.Print(LeftAlign(L"IOD%", 6));
        g_frame.Print(LeftAlign(L"Command", cmdColW));
    }
    g_frame.PadToEnd();
    g_frame.NewLine();

    // === Рядки процесів ===
    auto formatMem = [](SIZE_T bytes) -> std::wstring {
        double kb = bytes / 1024.0;
        wchar_t buf[16];
        if (kb >= 1024.0) swprintf(buf, 16, L"%.0fM", kb / 1024.0);
        else swprintf(buf, 16, L"%.0fK", kb);
        return buf;
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

    auto formatRate = [](double bytesPerSec) -> std::wstring {
        wchar_t buf[16];
        if (bytesPerSec >= 1024.0 * 1024.0 * 1024.0) swprintf(buf, 16, L"%.1fG/s", bytesPerSec / (1024.0 * 1024.0 * 1024.0));
        else if (bytesPerSec >= 1024.0 * 1024.0) swprintf(buf, 16, L"%.1fM/s", bytesPerSec / (1024.0 * 1024.0));
        else if (bytesPerSec >= 1024.0) swprintf(buf, 16, L"%.2fK/s", bytesPerSec / 1024.0);
        else if (bytesPerSec > 0.01) swprintf(buf, 16, L"%.0fB/s", bytesPerSec);
        else swprintf(buf, 16, L"0B/s");
        return buf;
    };

    int printedCount = 0;
    int endIdx = (std::min)(config.pageOffset + 15, (int)processes.size());
    for (int i = config.pageOffset; i < endIdx; ++i) {
        const auto& proc = processes[i];

        std::wstring name = proc.name;
        if ((int)name.length() > cmdColW - 1) name = name.substr(0, cmdColW - 2) + L"~";

        std::wstring user = proc.userName;
        if (user.length() > 8) user = user.substr(0, 8);

        bool isFirst = (printedCount == 0);
        WORD baseColor = isFirst ? BG_CYAN_FG_BLACK : WHITE;

        // PID
        g_frame.SetColor(isFirst ? BG_CYAN_FG_BLACK : FG_BRIGHT_CYAN);
        g_frame.Print(RightAlign(std::to_wstring(proc.pid), 6));
        g_frame.PrintChar(L' ');

        // USER
        g_frame.SetColor(baseColor);
        g_frame.Print(LeftAlign(user, 9));

        if (config.activeTab == TabView::Main) {
            g_frame.Print(RightAlign(std::to_wstring(proc.priority), 3));
            g_frame.PrintChar(L' ');
            g_frame.Print(RightAlign(std::to_wstring(proc.niceness), 3));
            g_frame.PrintChar(L' ');
            g_frame.Print(RightAlign(formatMem(proc.virtualMemory), 6));
            g_frame.PrintChar(L' ');

            g_frame.SetColor(isFirst ? BG_CYAN_FG_BLACK : FG_BRIGHT_GREEN);
            g_frame.Print(RightAlign(formatMem(proc.memoryUsage), 6));
            g_frame.PrintChar(L' ');

            g_frame.SetColor(baseColor);
            g_frame.Print(RightAlign(formatMem(proc.sharedMemory), 6));
            g_frame.PrintChar(L' ');
            g_frame.PrintChar(proc.state);
            g_frame.PrintChar(L' ');

            // CPU%
            WORD cpuColor = baseColor;
            if (!isFirst) cpuColor = (proc.cpuPercent > 5.0) ? FG_BRIGHT_RED : FG_BRIGHT_GREEN;
            g_frame.SetColor(cpuColor);
            wchar_t cpuBuf[8]; swprintf(cpuBuf, 8, L"%5.1f", proc.cpuPercent);
            g_frame.Print(std::wstring(cpuBuf));

            g_frame.SetColor(baseColor);
            wchar_t memBuf[8]; swprintf(memBuf, 8, L"%5.1f", proc.memPercent);
            g_frame.Print(std::wstring(memBuf));
            g_frame.PrintChar(L' ');

            g_frame.Print(RightAlign(formatTime(proc.cpuTime), 9));
            g_frame.PrintChar(L' ');
        } else {
            // I/O
            g_frame.SetColor(baseColor);
            g_frame.Print(LeftAlign(L"B4", 3));
            g_frame.PrintChar(L' ');

            g_frame.Print(RightAlign(formatRate(proc.ioReadRate + proc.ioWriteRate), 8));
            g_frame.PrintChar(L' ');

            g_frame.SetColor(isFirst ? BG_CYAN_FG_BLACK : FG_BRIGHT_GREEN);
            g_frame.Print(RightAlign(formatRate(proc.ioReadRate), 10));
            g_frame.PrintChar(L' ');

            g_frame.SetColor(isFirst ? BG_CYAN_FG_BLACK : FG_BRIGHT_RED);
            g_frame.Print(RightAlign(formatRate(proc.ioWriteRate), 11));
            g_frame.PrintChar(L' ');

            g_frame.SetColor(baseColor);
            g_frame.Print(RightAlign(L"N/A", 5));
            g_frame.PrintChar(L' ');
            g_frame.Print(RightAlign(L"N/A", 5));
            g_frame.PrintChar(L' ');
        }

        g_frame.SetColor(baseColor);
        g_frame.Print(LeftAlign(name, cmdColW));
        g_frame.PadToEnd();
        g_frame.NewLine();
        printedCount++;
    }

    // Заповнюємо до 15 рядків
    g_frame.SetColor(WHITE);
    while (printedCount < 15) {
        g_frame.NewLine();
        printedCount++;
    }

    // Розділювач
    g_frame.SetColor(DARKGRAY);
    g_frame.Print(std::wstring(termWidth, L'-'));
    g_frame.NewLine();

    // НИЖНЯ ПАНЕЛЬ: F-клавіші
    auto fkey = [&](const wchar_t* key, const wchar_t* label) {
        g_frame.SetColor((DARKGRAY << 4) | WHITE);
        g_frame.Print(key);
        g_frame.SetColor((CYAN << 4) | BLACK);
        g_frame.Print(label);
    };

    bool ua = (config.lang == Language::Ukrainian);
    fkey(L" F1 ", ua ? L"Довідка " : L"Help    ");
    fkey(L" F2 ", ua ? L"Мова    " : L"Lang    ");
    fkey(L" Tab", ua ? L"Вкладка " : L"Tab     ");
    fkey(L" F6 ", ua ? L"Інтервал" : L"Interval");
    fkey(L" F9 ", ua ? L"Заверш  " : L"Kill    ");
    fkey(L" <->", ua ? L"Гортання" : L"Scroll  ");

    g_frame.SetColor(WHITE);
    g_frame.PadToEnd();

    // === Крок 3: один атомарний вивід ===
    g_frame.Flush(GetStdHandle(STD_OUTPUT_HANDLE));
}

void ConsoleUI::HandleKillDialog(AppConfig& config, CpuMonitor& cpuMon) {
    // Для діалогу — тимчасово використовуємо звичайний вивід
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetCursorVisibility(true);

    // Очищаємо нижню частину для введення
    EnsureFrame();
    int h = g_frame.GetHeight();
    int w = g_frame.GetWidth();

    COORD pos = { 0, (SHORT)(h - 2) };
    SetConsoleCursorPosition(hConsole, pos);

    // Заповнюємо два рядки пробілами
    DWORD written;
    FillConsoleOutputCharacterW(hConsole, L' ', w * 2, pos, &written);
    FillConsoleOutputAttribute(hConsole, 7, w * 2, pos, &written);

    SetConsoleTextAttribute(hConsole, FG_BRIGHT_RED);
    std::wcout << (config.lang == Language::Ukrainian ? L"[KILL] Введіть PID процесу: " : L"[KILL] Enter Target PID: ");
    SetConsoleTextAttribute(hConsole, WHITE);
    std::wcout.flush();

    DWORD pidToKill = 0;
    std::cin >> pidToKill;

    if (std::cin.fail()) {
        std::cin.clear();
        (std::cin.ignore)((std::numeric_limits<std::streamsize>::max)(), '\n');
        SetConsoleTextAttribute(hConsole, FG_BRIGHT_RED);
        std::wcout << (config.lang == Language::Ukrainian ? L"[Помилка] Некоректний формат!" : L"[Error] Invalid PID format!");
        std::wcout.flush();
        Sleep(1200);
        SetCursorVisibility(false);
        cpuMon.Reset();
        return;
    }

    DWORD result = SystemManager::KillProcess(pidToKill);
    SetConsoleCursorPosition(hConsole, { 0, (SHORT)(h - 1) });
    if (result == 0) {
        SetConsoleTextAttribute(hConsole, FG_BRIGHT_GREEN);
        std::wcout << LocalizationManager::GetText("success", config.lang);
    } else if (result == ERROR_ACCESS_DENIED) {
        SetConsoleTextAttribute(hConsole, FG_BRIGHT_RED);
        std::wcout << LocalizationManager::GetText("access_denied", config.lang);
    } else {
        SetConsoleTextAttribute(hConsole, FG_BRIGHT_RED);
        std::wcout << LocalizationManager::GetText("not_found", config.lang);
    }
    std::wcout.flush();

    Sleep(1200);
    SetConsoleTextAttribute(hConsole, WHITE);
    SetCursorVisibility(false);
    cpuMon.Reset();
}
