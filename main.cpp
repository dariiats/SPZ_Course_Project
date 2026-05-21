// main.cpp
#include "ConsoleUI.h"

int main() {
    ConsoleUI::InitConsole();
    SystemManager::EnableDebugPrivilege();

    CpuMonitor cpuMon;
    AppConfig config;
    ProcessMonitor procMon;

    cpuMon.GetCpuUsage();
    Sleep(100);

    ULONGLONG lastRender = 0;
    bool needsRender = true;

    while (true) {
        bool keyHandled = false;

        // [F1] / [H] - Довідка
        if ((GetAsyncKeyState(VK_F1) & 0x8000) || (GetAsyncKeyState('H') & 0x8000)) {
            config.showHelp = !config.showHelp;
            needsRender = true;
            keyHandled = true;
        }
        // [F2] / [L] - Мова
        else if ((GetAsyncKeyState(VK_F2) & 0x8000) || (GetAsyncKeyState('L') & 0x8000)) {
            config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
            needsRender = true;
            keyHandled = true;
        }
        // [F6] / [I] - Інтервал
        else if ((GetAsyncKeyState(VK_F6) & 0x8000) || (GetAsyncKeyState('I') & 0x8000)) {
            if (config.refreshInterval == 1000) config.refreshInterval = 3000;
            else if (config.refreshInterval == 3000) config.refreshInterval = 5000;
            else config.refreshInterval = 1000;
            needsRender = true;
            keyHandled = true;
        }
        // [Tab] - перемикання вкладок
        else if (GetAsyncKeyState(VK_TAB) & 0x8000) {
            config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
            config.pageOffset = 0;
            needsRender = true;
            keyHandled = true;
        }
        // Гортання сторінок
        else if (!config.showHelp && (GetAsyncKeyState(VK_RIGHT) & 0x8000)) {
            size_t totalProcesses = SystemManager::GetProcesses().size();
            if (config.pageOffset + 15 < (int)totalProcesses) {
                config.pageOffset += 15;
                needsRender = true;
            }
            keyHandled = true;
        }
        else if (!config.showHelp && (GetAsyncKeyState(VK_LEFT) & 0x8000)) {
            if (config.pageOffset - 15 >= 0) {
                config.pageOffset -= 15;
                needsRender = true;
            }
            keyHandled = true;
        }
        // [F9] / [K] - Завершити процес
        else if ((GetAsyncKeyState(VK_F9) & 0x8000) || (GetAsyncKeyState('K') & 0x8000)) {
            ConsoleUI::HandleKillDialog(config, cpuMon);
            needsRender = true;
            keyHandled = true;
        }

        // Періодичний рендер за інтервалом
        ULONGLONG now = GetTickCount64();
        if (needsRender || (now - lastRender >= (ULONGLONG)config.refreshInterval)) {
            if (config.showHelp) {
                ConsoleUI::RenderHelp(config.lang);
            } else {
                ConsoleUI::RenderMonitor(config, cpuMon, procMon);
            }
            lastRender = now;
            needsRender = false;
        }

        // Затримка після клавіші, щоб не "тарахкотіло"
        if (keyHandled) Sleep(180);
        else Sleep(30); // короткий sleep для опитування клавіш
    }
    return 0;
}
