// main.cpp
#include "ConsoleUI.h"
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
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
        // [F1] - Довідка
        if (GetAsyncKeyState(VK_F1) & 0x8000) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.showHelp = !config.showHelp;
            g_needsCls = true;
            Sleep(250);
        }
        // [F2] - Мова
        if (GetAsyncKeyState(VK_F2) & 0x8000) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
            Sleep(250);
        }
        // [F6] - Інтервал
        if (GetAsyncKeyState(VK_F6) & 0x8000) {
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
            g_needsCls = true;
            Sleep(250);
        }

        // [F3] - Меню сортування
        if (GetAsyncKeyState(VK_F3) & 0x8000) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.showSortMenu = !config.showSortMenu;
            if (config.showSortMenu) {
                // Скидаємо індекс якщо він за межами для поточної вкладки
                int maxIdx = (config.activeTab == TabView::IO) ? 6 : 11;
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
                int maxIdx = (config.activeTab == TabView::IO) ? 6 : 11;
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

        // [F4] - Інвертувати сортування
        if (GetAsyncKeyState(VK_F4) & 0x8000) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.sortAscending = !config.sortAscending;
            config.pageOffset = 0;
            config.selectedRow = 0;
            Sleep(250);
        }

        // [F5] - Пошук
        if (GetAsyncKeyState(VK_F5) & 0x8000) {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.showSearch = !config.showSearch;
            if (!config.showSearch) {
                config.searchQuery.clear();
                config.pageOffset = 0;
                config.selectedRow = 0;
            }
            Sleep(250);
        }

        // Введення тексту пошуку
        if (config.showSearch && !config.showSortMenu && !config.showHelp) {
            for (int key = 'A'; key <= 'Z'; ++key) {
                if (GetAsyncKeyState(key) & 0x8000) {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    config.searchQuery += static_cast<wchar_t>(key + 32); // lowercase
                    config.pageOffset = 0;
                    config.selectedRow = 0;
                    Sleep(150);
                }
            }
            for (int key = '0'; key <= '9'; ++key) {
                if (GetAsyncKeyState(key) & 0x8000) {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    config.searchQuery += static_cast<wchar_t>(key);
                    config.pageOffset = 0;
                    config.selectedRow = 0;
                    Sleep(150);
                }
            }
            // Backspace
            if (GetAsyncKeyState(VK_BACK) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (!config.searchQuery.empty()) {
                    config.searchQuery.pop_back();
                    config.pageOffset = 0;
                    config.selectedRow = 0;
                }
                Sleep(150);
            }
            // Esc — закрити пошук
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.showSearch = false;
                config.searchQuery.clear();
                config.pageOffset = 0;
                config.selectedRow = 0;
                Sleep(250);
            }
        }

        // Стрілки — виділення та гортання
        if (!config.showHelp && !config.showSortMenu) {
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (config.selectedRow > 0) {
                    config.selectedRow--;
                } else if (config.pageOffset > 0) {
                    config.pageOffset--;
                }
                Sleep(120);
            }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.selectedRow++;
                Sleep(120);
            }
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.pageOffset += 15;
                config.selectedRow = 0;
                Sleep(150);
            }
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (config.pageOffset >= 15) {
                    config.pageOffset -= 15;
                    config.selectedRow = 0;
                }
                Sleep(150);
            }
        }

        // [F9] - Kill
        if (GetAsyncKeyState(VK_F9) & 0x8000) {
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

    return 0;
}
