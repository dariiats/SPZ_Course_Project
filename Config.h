// Config.h
#pragma once
#include <string>

enum class Language {
    Ukrainian,
    English
};

enum class TabView {
    Main,
    IO
};

enum class SortColumn {
    Pid,
    User,
    Priority,
    Nice,
    Virt,
    Res,
    Shr,
    State,
    CpuPercent,
    MemPercent,
    Time,
    Command
};

enum class IoSortColumn {
    Pid,
    User,
    IoPriority,
    DiskRW,
    DiskRead,
    DiskWrite,
    Command
};

struct AppConfig {
    Language lang = Language::Ukrainian;
    int refreshInterval = 1000;
    bool showHelp = false;
    bool showSortMenu = false;
    bool showSearch = false;       // F3 — Search (перехід до збігу)
    bool showFilter = false;       // F4 — Filter (фільтрація списку)
    bool searchNeedsJump = false;  // Прапорець: потрібно перемістити курсор до збігу
    std::wstring searchQuery;      // Спільний текст для Search і Filter
    int sortMenuIndex = 5;
    int pageOffset = 0;
    int selectedRow = 0;
    int savedPageOffset = 0;       // Збережена позиція перед Search
    int savedSelectedRow = 0;      // Збережений виділений рядок перед Search
    TabView activeTab = TabView::Main;
    SortColumn sortColumn = SortColumn::Res;
    IoSortColumn ioSortColumn = IoSortColumn::DiskRW;
    bool sortAscending = false; // false = descending (default like htop)
};

class LocalizationManager {
public:
    static std::wstring GetText(const std::string& key, Language lang) {
        if (lang == Language::Ukrainian) {
            if (key == "title") return L"=== СИСТЕМНИЙ МОНІТОР ===";
            if (key == "cpu") return L"Завантаження CPU: ";
            if (key == "ram") return L"Використання RAM: ";
            if (key == "headers") return L"PID     USER     PRI NI   VIRT    RES    SHR S CPU%  MEM%    TIME+ КОМАНДА";
            if (key == "footer") return L"[H] Довідка | [L] Мова (UA/EN) | [I] Інтервал | [K] Завершити PID";
            if (key == "access_denied") return L"Відмовлено в доступі!";
            if (key == "not_found") return L"Процес не знайдено!";
            if (key == "success") return L"Успішно завершено!";
        }
        else {
            if (key == "title") return L"=== SYSTEM MONITOR ===";
            if (key == "cpu") return L"CPU Usage: ";
            if (key == "ram") return L"RAM Usage: ";
            if (key == "headers") return L"PID     USER     PRI NI   VIRT    RES    SHR S CPU%  MEM%    TIME+ COMMAND";
            if (key == "footer") return L"[H] Help | [L] Language (UA/EN) | [I] Interval | [K] Kill PID";
            if (key == "access_denied") return L"Access Denied!";
            if (key == "not_found") return L"Process Not Found!";
            if (key == "success") return L"Successfully Killed!";
        }
        return L"";
    }
};