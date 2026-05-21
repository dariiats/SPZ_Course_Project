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
        // [Tab] - Перемикання вкладок Main/IO
        if (GetAsyncKeyState(VK_TAB) & 0x8000) {
            config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
            config.pageOffset = 0;
            system("cls");
            Sleep(250);
        }

        // Гортання сторінок стрілками
        if (!config.showHelp) {
            size_t totalProcesses = SystemManager::GetProcesses().size();
            if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
                if (config.pageOffset + 15 < (int)totalProcesses) {
                    config.pageOffset += 15;
                    ConsoleUI::RenderMonitor(config, cpuMon);
                }
                Sleep(150);
            }
            if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
                if (config.pageOffset - 15 >= 0) {
                    config.pageOffset -= 15;
                    ConsoleUI::RenderMonitor(config, cpuMon);
                }
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