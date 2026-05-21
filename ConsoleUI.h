// ConsoleUI.h
#pragma once
#include "Config.h"
#include "SystemCore.h"
#include <vector>

// Буфер кадру для безмерехтливого виводу
struct ScreenCell {
    wchar_t ch = L' ';
    WORD attr = 7; // WHITE
};

class FrameBuffer {
public:
    void Init(int width, int height);
    void Clear();
    void SetCursor(int x, int y);
    void SetColor(WORD attr);
    void Print(const std::wstring& text);
    void PrintChar(wchar_t ch);
    void Flush(HANDLE hConsole);
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }
    void NewLine();
    void PadToEnd(); // заповнити пробілами до кінця рядка

private:
    int m_width = 120;
    int m_height = 40;
    int m_curX = 0;
    int m_curY = 0;
    WORD m_attr = 7;
    std::vector<CHAR_INFO> m_buffer;
};

class ConsoleUI {
public:
    static void InitConsole();
    static void ResetCursor();
    static void SetCursorVisibility(bool visible);
    static void RenderMonitor(AppConfig& config, CpuMonitor& cpuMon, ProcessMonitor& procMon);
    static void RenderHelp(Language lang);
    static void HandleKillDialog(AppConfig& config, CpuMonitor& cpuMon);
};
