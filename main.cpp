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

// === Глобальний стан для синхронізації між потоками ===
std::mutex g_configMutex;       // Захист AppConfig
std::mutex g_dataMutex;         // Захист кешованих даних процесів
std::atomic<bool> g_needsCls{ false };
std::atomic<bool> g_killRequested{ false };
std::atomic<bool> g_running{ true };
std::atomic<bool> g_inputPaused{ false };

// Кешовані дані процесів (заповнюються Data Thread, читаються Render Thread)
std::vector<ProcessInfo> g_cachedProcesses;
int g_cachedThreadCount = 0;
std::vector<double> g_cachedCoreUsages;

// ============================================================
// ПОТІК 1: Input — обробка клавіатури (polling ~30ms)
// ============================================================
void InputThread(AppConfig& config) {
    while (g_running) {
        // Пауза під час Kill-діалогу
        if (g_inputPaused) {
            Sleep(50);
            continue;
        }

        // Обробляємо клавіші тільки коли консоль у фокусі
        HWND foreground = GetForegroundWindow();
        HWND console = GetConsoleWindow();
        if (foreground != console) {
            Sleep(100);
            continue;
        }
        // [F1 / H] - Довідка
        if ((GetAsyncKeyState(VK_F1) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState('H') & 0x8000))) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.showHelp = !config.showHelp;
            g_needsCls = true;
            Sleep(250);
        }
        // [L] - Мова (тільки коли не в режимі вводу)
        if (!config.showSearch && !config.showFilter && (GetAsyncKeyState('L') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
            Sleep(250);
        }
        // [I] - Інтервал (тільки коли не в режимі вводу)
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

        // [F3 / /] - Search (перехід до збігу без фільтрації)
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
                    // Повторне F3 — наступний збіг
                    config.searchMatchIndex++;
                }
            }
            Sleep(250);
            while (_kbhit()) _getch();
            continue;
        }

        // [F4 / \] - Filter (фільтрація списку)
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
                    // F3 (0x3D) — наступний збіг (в search)
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
                    // Enter — підтвердити
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

        // [F5 / T] - Tree view (дерево процесів)
        if ((GetAsyncKeyState(VK_F5) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState('T') & 0x8000))) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.treeView = !config.treeView;
            config.pageOffset = 0;
            config.selectedRow = 0;
            config.selectedPid = 0;
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

        // Навігація в меню сортування
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

        // [F6 / >] - Інвертувати сортування (SortBy в htop)
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

        // [F7 / ]] - Pri+ (підвищити пріоритет виділеного процесу)
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

        // [F8 / [] - Pri- (знизити пріоритет виділеного процесу)
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

        // [F10 / Q] - Quit (вихід)
        if ((GetAsyncKeyState(VK_F10) & 0x8000) ||
            (!config.showSearch && !config.showFilter && (GetAsyncKeyState('Q') & 0x8000))) {
            g_running = false;
            break;
        }

        // [Space] - Закріпити/відкріпити процес під курсором
        if (!config.showSearch && !config.showFilter && (GetAsyncKeyState(VK_SPACE) & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            if (config.pinnedPid == config.selectedPid) {
                config.pinnedPid = 0; // відкріпити
            } else {
                config.pinnedPid = config.selectedPid; // закріпити
            }
            Sleep(250);
        }

        // Стрілки — виділення та гортання
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

        // [Esc] - Скинути пін і повернутись на початок списку
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
// ПОТІК 2: Data Collector — збір даних процесів у фоні
// ============================================================
void DataThread(AppConfig& config) {
    PerCoreCpuMonitor coreMon;

    while (g_running) {
        // Збір даних (найважча операція)
        std::vector<ProcessInfo> freshProcesses = SystemManager::GetProcesses();

        // Підрахунок потоків
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

        // Оновлення кешу під mutex
        {
            std::lock_guard<std::mutex> lock(g_dataMutex);
            g_cachedProcesses = std::move(freshProcesses);
            g_cachedThreadCount = threadCount;
            g_cachedCoreUsages = coreMon.GetAllCoreUsages();
        }

        // Спимо відповідно до інтервалу оновлення
        int interval;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            interval = config.refreshInterval;
        }
        Sleep(interval);
    }
}

// ============================================================
// ПОТІК 3: Render — відображення UI
// ============================================================
void RenderThread(AppConfig& config, CpuMonitor& cpuMon) {
    int prevWidth = 0;

    while (g_running) {
        // Детекція зміни розміру вікна
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int curWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (prevWidth != 0 && curWidth != prevWidth) {
            std::wcout << L"\x1b[2J\x1b[H"; // Повне очищення при resize
        }
        prevWidth = curWidth;

        // Очищення екрану якщо потрібно
        if (g_needsCls.exchange(false)) {
            std::wcout << L"\x1b[2J\x1b[H";
        }

        // Kill-діалог (потребує stdin)
        if (g_killRequested.exchange(false)) {
            g_inputPaused = true;
            // Очищуємо stdin від залишків
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
        // Mutex звільнений — InputThread може працювати під час sleep
        Sleep(interval / 4); // Рендер ~250мс — достатньо плавно
    }
}

// ============================================================
// MAIN — запуск потоків
// ============================================================
int main() {
    ConsoleUI::InitConsole();
    SystemManager::EnableDebugPrivilege();

    CpuMonitor cpuMon;
    AppConfig config;

    cpuMon.GetCpuUsage();
    Sleep(100);

    // Перший збір даних перед запуском потоків
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
        // Ініціалізація per-core (нулі на першому кадрі)
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_cachedCoreUsages.resize(si.dwNumberOfProcessors, 0.0);
    }

    // Запуск 3 потоків
    std::thread input(InputThread, std::ref(config));
    std::thread data(DataThread, std::ref(config));
    std::thread render(RenderThread, std::ref(config), std::ref(cpuMon));

    input.join();
    data.join();
    render.join();

    // Відновлення консолі при виході
    std::wcout << L"\x1b[?1049l"; // Повернення з альтернативного буфера
    std::wcout << L"\x1b[?25h";   // Показати курсор

    return 0;
}
