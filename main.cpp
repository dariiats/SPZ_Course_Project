// main.cpp
#include "ConsoleUI.h"

int main() {
    ConsoleUI::InitConsole();
    SystemManager::EnableDebugPrivilege(); // Активація SeDebugPrivilege

    CpuMonitor cpuMon;
    AppConfig config;

    cpuMon.GetCpuUsage();
    Sleep(100);

    // main.cpp (всередині циклу while)
    while (true) {
        // Обробка подій миші
        if (ConsoleUI::ProcessMouseInput(config, cpuMon)) {
            if (!config.showHelp) {
                ConsoleUI::RenderMonitor(config, cpuMon);
            }
            continue;
        }

        // [F1] або [H] - Довідка
        if ((GetAsyncKeyState(VK_F1) & 0x8000) || (GetAsyncKeyState('H') & 0x8000)) {
            config.showHelp = !config.showHelp;
            system("cls");
            Sleep(250);
        }
        // [F2] або [L] - Мова
        if ((GetAsyncKeyState(VK_F2) & 0x8000) || (GetAsyncKeyState('L') & 0x8000)) {
            config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
            Sleep(250);
        }
        // [F6] або [I] - Інтервал
        if ((GetAsyncKeyState(VK_F6) & 0x8000) || (GetAsyncKeyState('I') & 0x8000)) {
            if (config.refreshInterval == 1000) config.refreshInterval = 3000;
            else if (config.refreshInterval == 3000) config.refreshInterval = 5000;
            else config.refreshInterval = 1000;
            Sleep(250);
        }
        // [Tab] або [F3] - Перемикання вкладок
        if ((GetAsyncKeyState(VK_TAB) & 0x8000) || (GetAsyncKeyState(VK_F3) & 0x8000)) {
            config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
            config.scrollOffset = 0;
            config.selectedRow = 0;
            system("cls");
            Sleep(250);
        }

        // Навігація стрілками (htop-style: по одному рядку)
        if (!config.showHelp) {
            size_t totalProcesses = SystemManager::GetProcesses().size();

            // Стрілка вниз
            if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
                if (config.selectedRow < (int)totalProcesses - 1) {
                    config.selectedRow++;
                    // Автоскрол якщо виділення вийшло за видиму область
                    if (config.selectedRow >= config.scrollOffset + config.visibleRows) {
                        config.scrollOffset = config.selectedRow - config.visibleRows + 1;
                    }
                }
                ConsoleUI::RenderMonitor(config, cpuMon);
                Sleep(80);
            }
            // Стрілка вгору
            if (GetAsyncKeyState(VK_UP) & 0x8000) {
                if (config.selectedRow > 0) {
                    config.selectedRow--;
                    // Автоскрол вгору
                    if (config.selectedRow < config.scrollOffset) {
                        config.scrollOffset = config.selectedRow;
                    }
                }
                ConsoleUI::RenderMonitor(config, cpuMon);
                Sleep(80);
            }
            // Page Down
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
                config.selectedRow += config.visibleRows;
                if (config.selectedRow >= (int)totalProcesses) config.selectedRow = (int)totalProcesses - 1;
                if (config.selectedRow >= config.scrollOffset + config.visibleRows) {
                    config.scrollOffset = config.selectedRow - config.visibleRows + 1;
                }
                ConsoleUI::RenderMonitor(config, cpuMon);
                Sleep(150);
            }
            // Page Up
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
                config.selectedRow -= config.visibleRows;
                if (config.selectedRow < 0) config.selectedRow = 0;
                if (config.selectedRow < config.scrollOffset) {
                    config.scrollOffset = config.selectedRow;
                }
                ConsoleUI::RenderMonitor(config, cpuMon);
                Sleep(150);
            }
            // Home
            if (GetAsyncKeyState(VK_HOME) & 0x8000) {
                config.selectedRow = 0;
                config.scrollOffset = 0;
                ConsoleUI::RenderMonitor(config, cpuMon);
                Sleep(150);
            }
            // End
            if (GetAsyncKeyState(VK_END) & 0x8000) {
                config.selectedRow = (int)totalProcesses - 1;
                config.scrollOffset = (int)totalProcesses - config.visibleRows;
                if (config.scrollOffset < 0) config.scrollOffset = 0;
                ConsoleUI::RenderMonitor(config, cpuMon);
                Sleep(150);
            }
        }

        // Рендеринг екранів
        if (config.showHelp) {
            ConsoleUI::RenderHelp(config.lang);
            Sleep(100);
            continue;
        }

        ConsoleUI::RenderMonitor(config, cpuMon);

        // [F9] або [K] - Завершити процес за PID
        if ((GetAsyncKeyState(VK_F9) & 0x8000) || (GetAsyncKeyState('K') & 0x8000)) {
            ConsoleUI::HandleKillDialog(config, cpuMon);
        }

        Sleep(config.refreshInterval);
    }
    return 0;
}