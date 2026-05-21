// ConsoleUI.h
#pragma once
#include "Config.h"
#include "SystemCore.h"

class ConsoleUI {
public:
    static void InitConsole();
    static void ResetCursor();
    static void SetCursorVisibility(bool visible);
    static void RenderMonitor(AppConfig& config, CpuMonitor& cpuMon);
    static void RenderHelp(Language lang);
    static void RenderSortMenu(AppConfig& config);
    static void HandleKillDialog(AppConfig& config, CpuMonitor& cpuMon);
};