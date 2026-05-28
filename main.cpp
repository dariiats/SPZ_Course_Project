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

// Семафор для трiгеру рендеру — Input та Data сигналять, Render чекає
HANDLE g_renderSemaphore = NULL;

// Кешованi данi процесiв (заповнюються Data Thread, читаються Render Thread)
std::vector<ProcessInfo> g_cachedProcesses;
int g_cachedThreadCount = 0;
std::vector<double> g_cachedCoreUsages;
std::unordered_map<DWORD, std::vector<ThreadInfo>> g_cachedThreads; // PID -> threads

// Сигнал рендеру: "є що малювати"
inline void SignalRender() {
    ReleaseSemaphore(g_renderSemaphore, 1, NULL);
}

// === Коди extended-клавiш (_getch повертає 0 або 0xE0, потiм другий байт) ===
enum ExtKey : int {
    EXT_UP    = 0x48,
    EXT_DOWN  = 0x50,
    EXT_LEFT  = 0x4B,
    EXT_RIGHT = 0x4D,
    EXT_F1    = 0x3B,
    EXT_F2    = 0x3C,
    EXT_F3    = 0x3D,
    EXT_F4    = 0x3E,
    EXT_F5    = 0x3F,
    EXT_F6    = 0x40,
    EXT_F7    = 0x41,
    EXT_F8    = 0x42,
    EXT_F9    = 0x43,
    EXT_F10   = 0x44,
};

// === Стани input state machine ===
enum class InputState {
    Normal,     // Основний режим — навiгацiя, хоткеї
    SortMenu,   // Меню сортування
    TextInput,  // Введення тексту (Search / Filter)
};

// ============================================================
// ПОТiК 1: Input — блокуючий _getch() + state machine
// ============================================================
void InputThread(AppConfig& config) {
    while (g_running) {
        // Пауза пiд час Kill-дiалогу
        if (g_inputPaused) {
            Sleep(50);
            continue;
        }

        // Блокуючий виклик — чекаємо натискання клавiшi
        int ch = _getch();

        // Визначаємо поточний стан
        InputState state;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            if (config.showSearch || config.showFilter)
                state = InputState::TextInput;
            else if (config.showSortMenu)
                state = InputState::SortMenu;
            else
                state = InputState::Normal;
        }

        // Extended key (стрiлки, F-клавiшi)
        if (ch == 0 || ch == 0xE0) {
            int ext = _getch();

            switch (state) {
            case InputState::TextInput:
                if (ext == EXT_F3) {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (config.showSearch) config.searchMatchIndex++;
                }
                break;

            case InputState::SortMenu:
                switch (ext) {
                case EXT_UP: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (config.sortMenuIndex > 0) config.sortMenuIndex--;
                } break;
                case EXT_DOWN: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    int maxIdx = (config.activeTab == TabView::IO) ? 5 : 10;
                    if (config.sortMenuIndex < maxIdx) config.sortMenuIndex++;
                } break;
                case EXT_F2: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    config.showSortMenu = false;
                    g_needsCls = true;
                } break;
                case EXT_F10: {
                    g_running = false;
                } break;
                default: break;
                }
                break;

            case InputState::Normal:
                switch (ext) {
                case EXT_UP: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (!config.showHelp) {
                        config.selectedPid = 0;
                        if (config.selectedRow > 0) config.selectedRow--;
                        else if (config.pageOffset > 0) config.pageOffset--;
                    }
                } break;
                case EXT_DOWN: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (!config.showHelp) {
                        config.selectedPid = 0;
                        config.selectedRow++;
                    }
                } break;
                case EXT_RIGHT: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (!config.showHelp) {
                        config.selectedPid = 0;
                        config.pageOffset += config.visibleRows;
                        config.selectedRow = 0;
                    }
                } break;
                case EXT_LEFT: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (!config.showHelp) {
                        config.selectedPid = 0;
                        if (config.pageOffset >= config.visibleRows) {
                            config.pageOffset -= config.visibleRows;
                            config.selectedRow = 0;
                        }
                    }
                } break;
                case EXT_F1: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    config.showHelp = !config.showHelp;
                    g_needsCls = true;
                } break;
                case EXT_F2: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    config.showSortMenu = true;
                    int maxIdx = (config.activeTab == TabView::IO) ? 5 : 10;
                    if (config.sortMenuIndex > maxIdx) config.sortMenuIndex = 0;
                    g_needsCls = true;
                } break;
                case EXT_F3: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (!config.showSearch) {
                        config.showSearch = true;
                        config.showFilter = false;
                        config.searchQuery.clear();
                        config.searchMatchIndex = 0;
                        config.savedPageOffset = config.pageOffset;
                        config.savedSelectedRow = config.selectedRow;
                    } else {
                        config.searchMatchIndex++;
                    }
                } break;
                case EXT_F4: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    if (!config.showFilter) {
                        config.showFilter = true;
                        config.showSearch = false;
                        config.searchQuery.clear();
                        config.pageOffset = 0;
                        config.selectedRow = 0;
                    }
                } break;
                case EXT_F5: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    config.treeView = !config.treeView;
                    config.pageOffset = 0;
                    config.selectedRow = 0;
                    config.selectedPid = 0;
                } break;
                case EXT_F6: {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    config.sortAscending = !config.sortAscending;
                    config.pageOffset = 0;
                    config.selectedRow = 0;
                    config.selectedPid = 0;
                    config.pinnedPid = 0;
                } break;
                case EXT_F7: {
                    DWORD targetPid = 0;
                    { std::lock_guard<std::mutex> lock(g_configMutex); targetPid = config.selectedPid; }
                    if (targetPid != 0) SystemManager::ChangeProcessPriority(targetPid, true);
                } break;
                case EXT_F8: {
                    DWORD targetPid = 0;
                    { std::lock_guard<std::mutex> lock(g_configMutex); targetPid = config.selectedPid; }
                    if (targetPid != 0) SystemManager::ChangeProcessPriority(targetPid, false);
                } break;
                case EXT_F9: {
                    g_killRequested = true;
                } break;
                case EXT_F10: {
                    g_running = false;
                } break;
                default: break;
                }
                break;
            }

            SignalRender();
            continue;
        }

        // Звичайний символ (не extended)
        switch (state) {
        case InputState::TextInput:
            if (ch == 27) {
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
            break;

        case InputState::SortMenu:
            if (ch == '\r' || ch == '\n') {
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
            } else if (ch == 27) {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.showSortMenu = false;
                g_needsCls = true;
            } else if (ch == 'q' || ch == 'Q') {
                g_running = false;
            }
            break;

        case InputState::Normal:
            switch (ch) {
            case 27: {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.pinnedPid = 0;
                config.selectedPid = 0;
                config.pageOffset = 0;
                config.selectedRow = 0;
            } break;
            case '\t': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.activeTab = (config.activeTab == TabView::Main) ? TabView::IO : TabView::Main;
                config.pageOffset = 0;
                config.selectedRow = 0;
                config.selectedPid = 0;
                config.pinnedPid = 0;
                g_needsCls = true;
            } break;
            case ' ': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (config.pinnedPid == config.selectedPid)
                    config.pinnedPid = 0;
                else
                    config.pinnedPid = config.selectedPid;
            } break;
            case 'h': case 'H': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.showHelp = !config.showHelp;
                g_needsCls = true;
            } break;
            case 'l': case 'L': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.lang = (config.lang == Language::Ukrainian) ? Language::English : Language::Ukrainian;
            } break;
            case 'i': case 'I': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                if (config.refreshInterval == 1000) config.refreshInterval = 3000;
                else if (config.refreshInterval == 3000) config.refreshInterval = 5000;
                else config.refreshInterval = 1000;
            } break;
            case 's': case 'S': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.showSortMenu = true;
                int maxIdx = (config.activeTab == TabView::IO) ? 5 : 10;
                if (config.sortMenuIndex > maxIdx) config.sortMenuIndex = 0;
                g_needsCls = true;
            } break;
            case '/': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.showSearch = true;
                config.showFilter = false;
                config.searchQuery.clear();
                config.searchMatchIndex = 0;
                config.savedPageOffset = config.pageOffset;
                config.savedSelectedRow = config.selectedRow;
            } break;
            case '\\': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.showFilter = true;
                config.showSearch = false;
                config.searchQuery.clear();
                config.pageOffset = 0;
                config.selectedRow = 0;
            } break;
            case 't': case 'T': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.treeView = !config.treeView;
                config.pageOffset = 0;
                config.selectedRow = 0;
                config.selectedPid = 0;
            } break;
            case 'p': case 'P': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.showThreads = !config.showThreads;
            } break;
            case '>': {
                std::lock_guard<std::mutex> lock(g_configMutex);
                config.sortAscending = !config.sortAscending;
                config.pageOffset = 0;
                config.selectedRow = 0;
                config.selectedPid = 0;
                config.pinnedPid = 0;
            } break;
            case ']': {
                DWORD targetPid = 0;
                { std::lock_guard<std::mutex> lock(g_configMutex); targetPid = config.selectedPid; }
                if (targetPid != 0) SystemManager::ChangeProcessPriority(targetPid, true);
            } break;
            case '[': {
                DWORD targetPid = 0;
                { std::lock_guard<std::mutex> lock(g_configMutex); targetPid = config.selectedPid; }
                if (targetPid != 0) SystemManager::ChangeProcessPriority(targetPid, false);
            } break;
            case 'k': case 'K': {
                g_killRequested = true;
            } break;
            case 'q': case 'Q': {
                g_running = false;
            } break;
            default: break;
            }
            break;
        }

        SignalRender();
        if (!g_running) break;
    }

    // При виходi — розблокувати Render щоб вiн теж завершився
    SignalRender();
}

// ============================================================
// ПОТiК 2: Data Collector — збiр даних з перiодичнiстю, трiгерить рендер
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

        // Данi оновленi — трiгеримо рендер
        SignalRender();

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
// ПОТiК 3: Render — чекає на семафорi, малює коли є сигнал
// ============================================================
void RenderThread(AppConfig& config, CpuMonitor& cpuMon) {
    int prevWidth = 0;

    while (g_running) {
        // Блокуємось на семафорi — чекаємо сигнал вiд Input або Data
        WaitForSingleObject(g_renderSemaphore, INFINITE);

        if (!g_running) break;

        // Зливаємо зайвi сигнали (якщо накопичились) — рендеримо один раз
        while (WaitForSingleObject(g_renderSemaphore, 0) == WAIT_OBJECT_0) {}

        // Debounce: якщо юзер друкує швидко (TextInput mode),
        // чекаємо 15мс щоб зiбрати пачку символiв перед рендером.
        // Це уникає перемальовування на КОЖЕН символ.
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            if (config.showSearch || config.showFilter) {
                // Вiдпускаємо mutex i чекаємо трохи
            }
        }
        // Перевiряємо чи прийшли ще сигнали за 15мс
        if (WaitForSingleObject(g_renderSemaphore, 15) == WAIT_OBJECT_0) {
            // Прийшов ще сигнал — зливаємо всi що накопичились
            while (WaitForSingleObject(g_renderSemaphore, 0) == WAIT_OBJECT_0) {}
        }

        if (!g_running) break;

        // Детекцiя змiни розмiру вiкна
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int curWidth = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        if (prevWidth != 0 && curWidth != prevWidth) {
            std::wcout << L"\x1b[2J\x1b[H";
        }
        prevWidth = curWidth;

        // Очищення екрану якщо потрiбно
        if (g_needsCls.exchange(false)) {
            std::wcout << L"\x1b[2J\x1b[H";
        }

        // Kill-дiалог (потребує stdin)
        if (g_killRequested.exchange(false)) {
            g_inputPaused = true;
            while (_kbhit()) _getch();
            {
                std::lock_guard<std::mutex> lock(g_configMutex);
                ConsoleUI::HandleKillDialog(config, cpuMon);
            }
            g_inputPaused = false;
            continue;
        }

        // Копiюємо config пiд mutex — малюємо вже без нього
        AppConfig configSnapshot;
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            configSnapshot = config;
        }

        // Рендер без утримання mutex — Input вiльний працювати
        if (configSnapshot.showSortMenu) {
            ConsoleUI::RenderSortMenu(configSnapshot);
        } else if (configSnapshot.showHelp) {
            ConsoleUI::RenderHelp(configSnapshot.lang);
        } else {
            ConsoleUI::RenderMonitor(configSnapshot, cpuMon);
        }

        // Повертаємо змiни якi рендер мiг внести (visibleRows, selectedPid)
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            config.visibleRows = configSnapshot.visibleRows;
            config.selectedPid = configSnapshot.selectedPid;
        }
    }
}

// ============================================================
// MAIN — запуск потокiв
// ============================================================
int main() {
    ConsoleUI::InitConsole();
    SystemManager::EnableDebugPrivilege();

    // Створюємо семафор (initial=1 щоб перший рендер вiдбувся одразу, max=32 щоб не переповнювався)
    g_renderSemaphore = CreateSemaphoreW(NULL, 1, 32, NULL);

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
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        g_cachedCoreUsages.resize(si.dwNumberOfProcessors, 0.0);
    }

    // Запуск 3 потокiв
    std::thread input(InputThread, std::ref(config));
    std::thread data(DataThread, std::ref(config));
    std::thread render(RenderThread, std::ref(config), std::ref(cpuMon));

    input.join();
    g_running = false;  // Гарантуємо що iншi потоки теж зупиняться
    SignalRender();      // Розблокувати Render якщо вiн чекає
    data.join();
    render.join();

    // Прибираємо
    CloseHandle(g_renderSemaphore);

    // Вiдновлення консолi при виходi
    std::wcout << L"\x1b[?1049l";
    std::wcout << L"\x1b[?25h";

    return 0;
}
