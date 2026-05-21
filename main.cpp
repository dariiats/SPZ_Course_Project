// main.cpp
#include "ConsoleUI.h"
#include <thread>
#include <mutex>
#include <atomic>

std::mutex g_mutex;
std::atomic<bool> g_needsCls{ false };
std::atomic<bool> g_killRequested{ false };

// Потік вводу — обробка клавіш
void InputThread(AppConfig& config, CpuMonitor& cpuMon) {
    while (true) {
        // [F1] або [H] - Довідка
        if ((GetAsyncKeyState(VK_F1) & 0x8000) || (GetAsyncKeyState('H') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            config.showHelp = !config.showHelp;
            g_needsCls = true;
            Sleep(250);
        }
        // [F2] або [L] - Мова
        if ((GetAsyncKeyState(VK_F2) & 0x8000) || (GetAsyncKeyState('L') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
            Sleep(250);
        }
        // [F6] або [I] - Інтервал
        if ((GetAsyncKeyState(VK_F6) & 0x8000) || (GetAsyncKeyState('I') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (config.refreshInterval == 1000) config.refreshInterval = 3000;
            else if (config.refreshInterval == 3000) config.refreshInterval = 5000;
            else config.refreshInterval = 1000;
            Sleep(250);
        }
        // [Tab] - Перемикання вкладок Main/IO
        if (GetAsyncKeyState(VK_TAB) & 0x8000) {
            std::lock_guard<std::mutex> lock(g_mutex);
            config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
            config.pageOffset = 0;
            config.selectedRow = 0;
            g_needsCls = true;
            Sleep(250);
        }

        // [F3] або [S] - Відкрити/закрити меню сортування
        if ((GetAsyncKeyState(VK_F3) & 0x8000) || (GetAsyncKeyState('S') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            config.showSortMenu = !config.showSortMenu;
            if (config.showSortMenu) g_needsCls = true;
            Sleep(250);
        }

        // Навігація в меню сортування
        if (config.showSortMenu) {
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (config.sortMenuIndex > 0) config.sortMenuIndex--;
                Sleep(150);
            }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (config.sortMenuIndex < 11) config.sortMenuIndex++;
                Sleep(150);
            }
            if (GetAsyncKeyState(VK_RETURN) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_mutex);
                config.sortColumn = static_cast<SortColumn>(config.sortMenuIndex);
                config.showSortMenu = false;
                config.pageOffset = 0;
                config.selectedRow = 0;
                g_needsCls = true;
                Sleep(250);
            }
            if (GetAsyncKeyState(VK_ESCAPE) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_mutex);
                config.showSortMenu = false;
                g_needsCls = true;
                Sleep(250);
            }
            Sleep(30);
            continue;
        }

        // [F4] або [R] - Інвертувати напрямок сортування
        if ((GetAsyncKeyState(VK_F4) & 0x8000) || (GetAsyncKeyState('R') & 0x8000)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            config.sortAscending = !config.sortAscending;
            config.pageOffset = 0;
            config.selectedRow = 0;
            Sleep(250);
        }

        // Стрілки — виділення та гортання
        if (!config.showHelp && !config.showSortMenu) {
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (config.selectedRow > 0) {
                    config.selectedRow--;
                } else if (config.pageOffset > 0) {
                    config.pageOffset--;
                }
                Sleep(120);
            }
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_mutex);
                config.selectedRow++;
                Sleep(120);
            }
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_mutex);
                config.pageOffset += 15;
                config.selectedRow = 0;
                Sleep(150);
            }
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (config.pageOffset >= 15) {
                    config.pageOffset -= 15;
                    config.selectedRow = 0;
                }
                Sleep(150);
            }
        }

        // [F9] або [K] - Завершити процес за PID
        if ((GetAsyncKeyState(VK_F9) & 0x8000) || (GetAsyncKeyState('K') & 0x8000)) {
            g_killRequested = true;
            Sleep(250);
        }

        Sleep(30); // Polling interval для input
    }
}

// Потік виводу — рендеринг UI
void RenderThread(AppConfig& config, CpuMonitor& cpuMon) {
    while (true) {
        // Обробка cls
        if (g_needsCls.exchange(false)) {
            system("cls");
        }

        // Обробка kill-діалогу (потребує stdin, тому в render thread)
        if (g_killRequested.exchange(false)) {
            std::lock_guard<std::mutex> lock(g_mutex);
            ConsoleUI::HandleKillDialog(config, cpuMon);
            continue;
        }

        std::lock_guard<std::mutex> lock(g_mutex);

        if (config.showSortMenu) {
            ConsoleUI::RenderSortMenu(config);
            Sleep(50);
            continue;
        }

        if (config.showHelp) {
            ConsoleUI::RenderHelp(config.lang);
            Sleep(100);
            continue;
        }

        ConsoleUI::RenderMonitor(config, cpuMon);
        int interval = config.refreshInterval;
        // Розблоковуємо mutex перед sleep
        // (lock_guard вже звільнить при виході з блоку)
        Sleep(interval);
    }
}

int main() {
    ConsoleUI::InitConsole();
    SystemManager::EnableDebugPrivilege();

    CpuMonitor cpuMon;
    AppConfig config;

    cpuMon.GetCpuUsage();
    Sleep(100);

    // Запуск потоків
    std::thread inputThread(InputThread, std::ref(config), std::ref(cpuMon));
    std::thread renderThread(RenderThread, std::ref(config), std::ref(cpuMon));

    inputThread.join();
    renderThread.join();

    return 0;
}
