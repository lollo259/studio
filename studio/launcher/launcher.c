/*
 * Studio launcher: Score.exe / Tape.exe at the top of the Studio folder start the real
 * app inside its own folder, passing on any files (so "Open with" works too).
 * Built twice with a different TARGET.
 * SPDX-License-Identifier: GPL-3.0-only
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>

#ifndef TARGET
#error define TARGET, e.g. L"Score\\bin\\LaurenceWhoScore5.exe"
#endif

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR args, int show)
{
    (void)inst; (void)prev; (void)show;
    wchar_t dir[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, dir, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        return 1;
    }
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash) {
        *slash = 0;
    }

    wchar_t exe[MAX_PATH * 2];
    _snwprintf(exe, MAX_PATH * 2, L"%s\\%s", dir, TARGET);
    exe[MAX_PATH * 2 - 1] = 0;

    wchar_t workdir[MAX_PATH * 2];
    wcsncpy(workdir, exe, MAX_PATH * 2);
    wchar_t* ws = wcsrchr(workdir, L'\\');
    if (ws) {
        *ws = 0;
    }

    static wchar_t cmd[32768];
    _snwprintf(cmd, 32768, L"\"%s\" %s", exe, args ? args : L"");
    cmd[32767] = 0;

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessW(exe, cmd, NULL, NULL, FALSE, 0, NULL, workdir, &si, &pi)) {
        wchar_t msg[MAX_PATH * 3];
        _snwprintf(msg, MAX_PATH * 3, L"Couldn't start\n%s\n\nKeep this file inside the Studio folder, next to the Score and Tape folders.", exe);
        msg[MAX_PATH * 3 - 1] = 0;
        MessageBoxW(NULL, msg, L"Studio", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
