// ConsoleUI.h
#pragma once
#ifndef CONSOLEUI_H
#define CONSOLEUI_H

#include "Config.h"
#include "SystemCore.h"

class ConsoleUI {
public:
    static void InitConsole();
    static void ResetCursor();
    static void SetCursorVisibility(bool visible);
    static void RenderMonitor(AppConfig& config, CpuMonitor& cpuMon);
    static void RenderHelp(Language lang);
    static void HandleKillDialog(AppConfig& config, CpuMonitor& cpuMon);
    static bool ProcessMouseInput(AppConfig& config, CpuMonitor& cpuMon);
    static int GetTabRowY();
    static int GetHeaderRowY();
    static int GetFooterRowY();
};

#endif // CONSOLEUI_H