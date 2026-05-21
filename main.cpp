// main.cpp
#include "ConsoleUI.h"

int main() {
    ConsoleUI::InitConsole();
    SystemManager::EnableDebugPrivilege();

    CpuMonitor cpuMon;
    AppConfig config;

    cpuMon.GetCpuUsage();
    Sleep(100);

    ULONGLONG lastRenderTime = 0;
    bool forceRedraw = true;

    while (true) {
        ULONGLONG now = GetTickCount64();
        bool inputHandled = false;

        // Обробка подій миші (миттєво)
        if (ConsoleUI::ProcessMouseInput(config, cpuMon)) {
            forceRedraw = true;
            inputHandled = true;
        }

        // Клавіатура — перевіряємо без затримки
        if ((GetAsyncKeyState(VK_F1) & 0x8000) || (GetAsyncKeyState('H') & 0x8000)) {
            config.showHelp = !config.showHelp;
            system("cls");
            forceRedraw = true;
            inputHandled = true;
            Sleep(150); // debounce тільки для toggle
        }
        if ((GetAsyncKeyState(VK_F2) & 0x8000) || (GetAsyncKeyState('L') & 0x8000)) {
            config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
            forceRedraw = true;
            inputHandled = true;
            Sleep(150);
        }
        if ((GetAsyncKeyState(VK_F6) & 0x8000) || (GetAsyncKeyState('I') & 0x8000)) {
            if (config.refreshInterval == 1000) config.refreshInterval = 3000;
            else if (config.refreshInterval == 3000) config.refreshInterval = 5000;
            else config.refreshInterval = 1000;
            forceRedraw = true;
            inputHandled = true;
            Sleep(150);
        }
        if ((GetAsyncKeyState(VK_TAB) & 0x8000) || (GetAsyncKeyState(VK_F3) & 0x8000)) {
            config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
            config.scrollOffset = 0;
            config.selectedRow = 0;
            system("cls");
            forceRedraw = true;
            inputHandled = true;
            Sleep(150);
        }
        if ((GetAsyncKeyState(VK_F9) & 0x8000) || (GetAsyncKeyState('K') & 0x8000)) {
            ConsoleUI::HandleKillDialog(config, cpuMon);
            forceRedraw = true;
            inputHandled = true;
        }
        // [Tab] або [F3] - Перемикання вкладок
        if ((GetAsyncKeyState(VK_TAB) & 0x8000) || (GetAsyncKeyState(VK_F3) & 0x8000)) {
            config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
            config.scrollOffset = 0;
            config.selectedRow = 0;
            system("cls");
            Sleep(250);
        }

        // Навігація
        if (!config.showHelp) {
            size_t totalProcesses = SystemManager::GetProcesses().size();

            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                if (config.selectedRow < (int)totalProcesses - 1) {
                    config.selectedRow++;
                    if (config.selectedRow >= config.scrollOffset + config.visibleRows)
                        config.scrollOffset = config.selectedRow - config.visibleRows + 1;
                }
                forceRedraw = true;
                inputHandled = true;
                Sleep(50);
            }
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                if (config.selectedRow > 0) {
                    config.selectedRow--;
                    if (config.selectedRow < config.scrollOffset)
                        config.scrollOffset = config.selectedRow;
                }
                forceRedraw = true;
                inputHandled = true;
                Sleep(50);
            }
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
                config.selectedRow += config.visibleRows;
                if (config.selectedRow >= (int)totalProcesses) config.selectedRow = (int)totalProcesses - 1;
                if (config.selectedRow >= config.scrollOffset + config.visibleRows)
                    config.scrollOffset = config.selectedRow - config.visibleRows + 1;
                forceRedraw = true;
                inputHandled = true;
                Sleep(100);
            }
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
                config.selectedRow -= config.visibleRows;
                if (config.selectedRow < 0) config.selectedRow = 0;
                if (config.selectedRow < config.scrollOffset)
                    config.scrollOffset = config.selectedRow;
                forceRedraw = true;
                inputHandled = true;
                Sleep(100);
            }
            if (GetAsyncKeyState(VK_HOME) & 0x8000) {
                config.selectedRow = 0;
                config.scrollOffset = 0;
                forceRedraw = true;
                inputHandled = true;
                Sleep(100);
            }
            if (GetAsyncKeyState(VK_END) & 0x8000) {
                config.selectedRow = (int)totalProcesses - 1;
                config.scrollOffset = (int)totalProcesses - config.visibleRows;
                if (config.scrollOffset < 0) config.scrollOffset = 0;
                forceRedraw = true;
                inputHandled = true;
                Sleep(100);
            }
        }

        // Рендеринг: або по таймеру (refreshInterval), або при вводі
        bool timeToRefresh = (now - lastRenderTime) >= (ULONGLONG)config.refreshInterval;

        if (forceRedraw || timeToRefresh) {
            if (config.showHelp) {
                ConsoleUI::RenderHelp(config.lang);
            } else {
                ConsoleUI::RenderMonitor(config, cpuMon);
            }
            lastRenderTime = now;
            forceRedraw = false;
        }

        // Короткий sleep щоб не жерти 100% CPU, але реагувати швидко
        Sleep(30);
    }
    return 0;
}
