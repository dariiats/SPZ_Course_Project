// main.cpp
#include "ConsoleUI.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <algorithm>
#include <conio.h>
#include <tlhelp32.h>
#include <unordered_map>

// === Глобальний стан для синхронiзацiї мiж потоками ===
std::mutex g_configMutex;       // Захист AppConfig
std::mutex g_dataMutex;         // Захист кешованих даних процесiв
std::atomic<bool> g_needsCls{ false };
std::atomic<bool> g_killRequested{ false };
std::atomic<bool> g_running{ true };
std::atomic<bool> g_inputPaused{ false };

// Кешованi данi процесiв (заповнюються Data Thread, читаються Render Thread)
std::vector<ProcessInfo> g_cachedProcesses;
int g_cachedThreadCount = 0;
std::vector<double> g_cachedCoreUsages;
std::unordered_map<DWORD, std::vector<ThreadInfo>> g_cachedThreads; // PID -> threads

// ============================================================
// ПОТiК 1: Input — обробка клавiатури (polling ~30ms)
// ============================================================
void InputThread(AppConfig& config) {
    while (g_running) {
        // Пауза пiд час Kill-дiалогу
        if (g_inputPaused) {
            Sleep(50);
            continue;
        }
        // [F1 / H] - Довiдка
        if ((GetAsyncKeyState(VK_F1) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState('H') & 0x8000))) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.showHelp = !config.showHelp;
            g_needsCls = true;
            Sleep(250);
        }
        // [L] - Мова (тiльки коли не в режимi вводу)
        if (!config.showSearch && !config.showFilter && (GetAsyncKeyState('L') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
            Sleep(250);
        }
        // [I] - iнтервал (тiльки коли не в режимi вводу)
        if (!config.showSearch && !config.showFilter && (GetAsyncKeyState('I') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            if (config.refreshInterval == 1000) config.refreshInterval = 3000;
            else if (config.refreshInterval == 3000) config.refreshInterval = 5000;
            else config.refreshInterval = 1000;
            Sleep(250);
        }
        // [Tab] - Перемикання вкладок
        if (GetAsyncKeyState(VK_TAB) & 0x8000) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
            config.pageOffset = 0;
            config.selectedRow = 0;
            config.selectedPid = 0;
            config.pinnedPid = 0;
            g_needsCls = true;
            Sleep(250);
        }

        // [F3 / /] - Search (перехiд до збiгу без фiльтрацiї)
        if ((GetAsyncKeyState(VK_F3) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState(VK_OEM_2) & 0x8000))) {
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (!config.showSearch) {
                    config.showSearch = true;
                    config.showFilter = false;
                    config.searchQuery.clear();
                    config.searchMatchIndex = 0;
                    config.savedPageOffset = config.pageOffset;
                    config.savedSelectedRow = config.selectedRow;
                } else {
                    // Повторне F3 — наступний збiг
                    config.searchMatchIndex++;
                }
            }
            Sleep(250);
            while (_kbhit()) _getch();
            continue;
        }

        // [F4 / \] - Filter (фiльтрацiя списку)
        if ((GetAsyncKeyState(VK_F4) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState(VK_OEM_5) & 0x8000))) {
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (!config.showFilter) {
                    config.showFilter = true;
                    config.showSearch = false;
                    config.searchQuery.clear();
                    config.pageOffset = 0;
                    config.selectedRow = 0;
                }
            }
            Sleep(250);
            while (_kbhit()) _getch();
            continue;
        }

        // Режим вводу тексту для Search або Filter
        if (config.showSearch || config.showFilter) {
            if (_kbhit()) {
                int ch = _getch();
                if (ch == 0 || ch == 0xE0) {
                    int ext = _getch();
                    // F3 (0x3D) — наступний збiг (в search)
                    if (ext == 0x3D && config.showSearch) {
                        std::lock_guard<std::mutex> lock(g_configMutex);
                        config.searchMatchIndex++;
                    }
                } else if (ch == 27) {
                    // Esc — скасувати
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (config.showSearch) {
                        config.showSearch = false;
                        config.searchQuery.clear();
                        config.searchMatchIndex = 0;
                        config.pageOffset = config.savedPageOffset;
                        config.selectedRow = config.savedSelectedRow;
                        config.selectedPid = 0;
                        config.pinnedPid = 0;
                    } else {
                        config.showFilter = false;
                        config.searchQuery.clear();
                        config.pageOffset = 0;
                        config.selectedRow = 0;
                        config.selectedPid = 0;
                        config.pinnedPid = 0;
                    }
                } else if (ch == '\r' || ch == '\n') {
                    // Enter — пiдтвердити
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (config.showSearch) {
                        config.showSearch = false;
                        config.pinnedPid = config.selectedPid;
                        config.searchMatchIndex = 0;
                    } else {
                        config.showFilter = false;
                        config.pinnedPid = config.selectedPid;
                    }
                    config.searchQuery.clear();
                } else if (ch == '\b') {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (!config.searchQuery.empty()) {
                        config.searchQuery.pop_back();
                        if (config.showSearch) config.searchMatchIndex = 0;
                        if (config.showFilter) { config.pageOffset = 0; config.selectedRow = 0; }
                    }
                } else if (ch >= 32 && ch < 127) {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    config.searchQuery += static_cast<wchar_t>(towlower(ch));
                    if (config.showSearch) config.searchMatchIndex = 0;
                    if (config.showFilter) { config.pageOffset = 0; config.selectedRow = 0; }
                }
            }
            Sleep(30);
            continue;
        }

        // [F5 / T] - Tree view (дерево процесiв)
        if ((GetAsyncKeyState(VK_F5) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState('T') & 0x8000))) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.treeView = !config.treeView;
            config.pageOffset = 0;
            config.selectedRow = 0;
            config.selectedPid = 0;
            Sleep(250);
        }

        // [P] - Показати/сховати потоки процесiв (як htop H)
        if (!config.showSearch && !config.showFilter && (GetAsyncKeyState('P') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.showThreads = !config.showThreads;
            Sleep(250);
        }

        // [F2 / S] - Меню сортування (Setup в htop)
        if ((GetAsyncKeyState(VK_F2) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState('S') & 0x8000))) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.showSortMenu = !config.showSortMenu;
            if (config.showSortMenu) {
                int maxIdx = (config.activeTab == TabView::IO) ? 5 : 10;
                if (config.sortMenuIndex > maxIdx) config.sortMenuIndex = 0;
                g_needsCls = true;
            }
            Sleep(250);
        }

        // Навiгацiя в меню сортування
        if (config.showSortMenu) {
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (config.sortMenuIndex > 0) config.sortMenuIndex--;
                Sleep(150);
            }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                int maxIdx = (config.activeTab == TabView::IO) ? 5 : 10;
                if (config.sortMenuIndex < maxIdx) config.sortMenuIndex++;
                Sleep(150);
            }
            if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (config.activeTab == TabView::IO) {
                    config.ioSortColumn = static_cast<IoSortColumn>(config.sortMenuIndex);
                } else {
                    config.sortColumn = static_cast<SortColumn>(config.sortMenuIndex);
                }
                config.showSortMenu = false;
                config.pageOffset = 0;
                config.selectedRow = 0;
                config.selectedPid = 0;
                config.pinnedPid = 0;
                g_needsCls = true;
                Sleep(250);
            }
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.showSortMenu = false;
                g_needsCls = true;
                Sleep(250);
            }
            Sleep(30);
            continue;
        }

        // [F6 / >] - iнвертувати сортування (SortBy в htop)
        if ((GetAsyncKeyState(VK_F6) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000))) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.sortAscending = !config.sortAscending;
            config.pageOffset = 0;
            config.selectedRow = 0;
            config.selectedPid = 0;
            config.pinnedPid = 0;
            Sleep(250);
        }

        // [F7 / ]] - Pri+ (пiдвищити прiоритет видiленого процесу)
        if ((GetAsyncKeyState(VK_F7) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState(VK_OEM_6) & 0x8000))) {
            DWORD targetPid = 0;
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                targetPid = config.selectedPid;
            }
            if (targetPid != 0) {
                SystemManager::ChangeProcessPriority(targetPid, true);
            }
            Sleep(250);
        }

        // [F8 / [] - Pri- (знизити прiоритет видiленого процесу)
        if ((GetAsyncKeyState(VK_F8) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState(VK_OEM_4) & 0x8000))) {
            DWORD targetPid = 0;
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                targetPid = config.selectedPid;
            }
            if (targetPid != 0) {
                SystemManager::ChangeProcessPriority(targetPid, false);
            }
            Sleep(250);
        }

        // [F10 / Q] - Quit (вихiд)
        if ((GetAsyncKeyState(VK_F10) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState('Q') & 0x8000))) {
            g_running = false;
            break;
        }

        // [Space] - Закрiпити/вiдкрiпити процес пiд курсором
        if (!config.showSearch && !config.showFilter && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            if (config.pinnedPid == config.selectedPid) {
                config.pinnedPid = 0; // вiдкрiпити
            } else {
                config.pinnedPid = config.selectedPid; // закрiпити
            }
            Sleep(250);
        }

        // Стрiлки — видiлення та гортання
        if (!config.showHelp && !config.showSortMenu) {
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.selectedPid = 0;
                if (config.selectedRow > 0) {
                    config.selectedRow--;
                } else if (config.pageOffset > 0) {
                    config.pageOffset--;
                }
                Sleep(120);
            }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.selectedPid = 0;
                config.selectedRow++;
                Sleep(120);
            }
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.selectedPid = 0;
                config.pageOffset += config.visibleRows;
                config.selectedRow = 0;
                Sleep(150);
            }
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.selectedPid = 0;
                if (config.pageOffset >= config.visibleRows) {
                    config.pageOffset -= config.visibleRows;
                    config.selectedRow = 0;
                }
                Sleep(150);
            }
        }

        // [Esc] - Скинути пiн i повернутись на початок списку
        if (!config.showSearch && !config.showFilter && !config.showSortMenu && (GetAsyncKeyState(VK_ESCAPE) & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.pinnedPid = 0;
            config.selectedPid = 0;
            config.pageOffset = 0;
            config.selectedRow = 0;
            Sleep(250);
        }

        // [F9 / K] - Kill
        if ((GetAsyncKeyState(VK_F9) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState('K') & 0x8000))) {
            g_killRequested = true;
            Sleep(250);
        }

        Sleep(30);
    }
}

// ============================================================
// ПОТiК 2: Data Collector — збiр даних процесiв у фонi
// ============================================================
void DataThread(AppConfig& config) {
    PerCoreCpuMonitor coreMon;

    while (g_running) {
        // Збiр даних (найважча операцiя)
        std::vector<ProcessInfo> freshProcesses = SystemManager::GetProcesses();

        // Пiдрахунок потокiв
        int threadCount = 0;
        HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hThreadSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(THREADENTRY32) };
            if (Thread32First(hThreadSnap, &te)) {
                do { threadCount++; } while (Thread32Next(hThreadSnap, &te));
            }
            CloseHandle(hThreadSnap);
        }

        // Оновлення per-core CPU
        coreMon.Update();

        // Збiр потокiв (тiльки якщо режим увiмкнено)
        bool needThreads;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            needThreads = config.showThreads;
        }
        std::unordered_map<DWORD, std::vector<ThreadInfo>> freshThreads;
        if (needThreads) {
            freshThreads = SystemManager::GetAllThreads();
        }

        // Оновлення кешу пiд mutex
        {
            std::lock_guard<std::mutex> lock(g_dataMutex);
            g_cachedProcesses = std::move(freshProcesses);
            g_cachedThreadCount = threadCount;
            g_cachedCoreUsages = coreMon.GetAllCoreUsages();
            g_cachedThreads = std::move(freshThreads);
        }

        // Спимо вiдповiдно до iнтервалу оновлення
        int interval;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            interval = config.refreshInterval;
        }
        Sleep(interval);
    }
}

// ============================================================
// ПОТiК 3: Render — вiдображення UI
// ============================================================
void RenderThread(AppConfig& config, CpuMonitor& cpuMon) {
    int prevWidth = 0;

    while (g_running) {
        // Детекцiя змiни розмiру вiкна
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int curWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (prevWidth != 0 && curWidth != prevWidth) {
            std::wcout << L"\x1b[2J\x1b[H"; // Повне очищення при resize
        }
        prevWidth = curWidth;

        // Очищення екрану якщо потрiбно
        if (g_needsCls.exchange(false)) {
            std::wcout << L"\x1b[2J\x1b[H";
        }

        // Kill-дiалог (потребує stdin)
        if (g_killRequested.exchange(false)) {
            g_inputPaused = true;
            // Очищуємо stdin вiд залишкiв
            while (_kbhit()) _getch();
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                ConsoleUI::HandleKillDialog(config, cpuMon);
            }
            g_inputPaused = false;
            continue;
        }

        int interval;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);

            if (config.showSortMenu) {
                ConsoleUI::RenderSortMenu(config);
            } else if (config.showHelp) {
                ConsoleUI::RenderHelp(config.lang);
            } else {
                ConsoleUI::RenderMonitor(config, cpuMon);
            }
            interval = config.refreshInterval;
        }
        // Mutex звiльнений — InputThread може працювати пiд час sleep
        Sleep(interval / 4); // Рендер ~250мс — достатньо плавно
    }
}

// ============================================================
// MAIN — запуск потокiв
// ============================================================
int main() {
    ConsoleUI::InitConsole();
    SystemManager::EnableDebugPrivilege();

    CpuMonitor cpuMon;
    AppConfig config;

    cpuMon.GetCpuUsage();
    Sleep(100);

    // Перший збiр даних перед запуском потокiв
    {
        g_cachedProcesses = SystemManager::GetProcesses();
        HANDLE hThreadSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hThreadSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(THREADENTRY32) };
            if (Thread32First(hThreadSnap, &te)) {
                do { g_cachedThreadCount++; } while (Thread32Next(hThreadSnap, &te));
            }
            CloseHandle(hThreadSnap);
        }
        // iнiцiалiзацiя per-core (нулi на першому кадрi)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_cachedCoreUsages.resize(si.dwNumberOfProcessors, 0.0);
    }

    // Запуск 3 потокiв
    std::thread input(InputThread, std::ref(config));
    std::thread data(DataThread, std::ref(config));
    std::thread render(RenderThread, std::ref(config), std::ref(cpuMon));

    input.join();
    data.join();
    render.join();

    // Вiдновлення консолi при виходi
    std::wcout << L"\x1b[?1049l"; // Повернення з альтернативного буфера
    std::wcout << L"\x1b[?25h";   // Показати курсор

    return 0;
}
