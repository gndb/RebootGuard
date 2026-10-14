#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include "GuardState.h"
#include "resources.h"

#ifdef GUARD_TEST_INSTANCE
static const wchar_t ClassName[] = L"RebootGuard.Minimal.2026.Test";
static const wchar_t MutexName[] = L"Local\\RebootGuard.Minimal.2026.Test";
#else
static const wchar_t ClassName[] = L"RebootGuard.Minimal.2026";
static const wchar_t MutexName[] = L"Local\\RebootGuard.Minimal.2026";
#endif
static const wchar_t AppName[] = L"重启保护";
static const UINT TrayMessage = WM_APP + 1;
static const UINT ShowMenuMessage = WM_APP + 2;
static const UINT EnableCommand = 1001, ReleaseCommand = 1002, ExitCommand = 1003;
static const UINT_PTR PollTimer = 1, TrayRetryTimer = 2;
static GuardState guard;
static NOTIFYICONDATAW tray = {};
static UINT taskbarCreated;
static bool trayPresent = false;
static bool blockReasonRegistered = false;
static HICON activeIcon, releasedIcon, errorIcon;

static void ErrorBox(HWND window, const wchar_t* action, DWORD error) {
    wchar_t message[256];
    wsprintfW(message, L"%s\nWindows 错误码：%lu\n未获得完整保护。", action, error);
    MessageBoxW(window, message, AppName, MB_OK | MB_ICONERROR);
}

static DWORD EnableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return GetLastError();
    TOKEN_PRIVILEGES privileges = {};
    privileges.PrivilegeCount = 1;
    if (!LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) {
        DWORD error = GetLastError();
        CloseHandle(token);
        return error;
    }
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    SetLastError(ERROR_SUCCESS);
    BOOL adjusted = AdjustTokenPrivileges(token, FALSE, &privileges, 0, nullptr, nullptr);
    DWORD error = GetLastError();
    CloseHandle(token);
    return adjusted ? error : (error ? error : ERROR_ACCESS_DENIED);
}

static unsigned long CancelPending() {
#ifdef GUARD_TEST_INSTANCE
    // Native UI tests register a real block reason, but never cancel a system shutdown.
    return ERROR_NO_SHUTDOWN_IN_PROGRESS;
#else
    if (AbortSystemShutdownW(nullptr)) return ERROR_SUCCESS;
    return GetLastError();
#endif
}

static void UpdateAppearance(HWND window) {
    const wchar_t* title = !guard.enabled ? L"重启保护：已解除" :
        (guard.error ? L"重启保护：倒计时取消异常" : L"重启保护：已开启（尽力拦截）");
    SetWindowTextW(window, title);
    lstrcpynW(tray.szTip, title, ARRAYSIZE(tray.szTip));
    tray.hIcon = !guard.enabled ? releasedIcon : (guard.error ? errorIcon : activeIcon);
    tray.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    if (trayPresent && !Shell_NotifyIconW(NIM_MODIFY, &tray)) {
        trayPresent = false;
        SetTimer(window, TrayRetryTimer, 2000, nullptr);
    }
}

static void AddTray(HWND window) {
    UpdateAppearance(window);
    // TaskbarCreated can also mean a DPI change, with our icon still present.
    trayPresent = Shell_NotifyIconW(NIM_ADD, &tray) != FALSE ||
        Shell_NotifyIconW(NIM_MODIFY, &tray) != FALSE;
    if (trayPresent) KillTimer(window, TrayRetryTimer);
    else SetTimer(window, TrayRetryTimer, 2000, nullptr);
}

static void Poll(HWND window) {
    DWORD previousError = guard.error;
    guard.Tick(CancelPending);
    if (guard.error != previousError) UpdateAppearance(window);
}

static bool SetProtection(HWND window, bool enabled) {
    if (enabled == guard.enabled) return true;
    if (enabled) {
        if (!ShutdownBlockReasonCreate(window, L"后台计算正在运行。请先在“重启保护”托盘菜单解除保护。")) {
            ErrorBox(window, L"无法注册关机拦截理由。", GetLastError());
            return false;
        }
        blockReasonRegistered = true;
        if (!SetTimer(window, PollTimer, 1000, nullptr)) {
            DWORD error = GetLastError();
            ShutdownBlockReasonDestroy(window);
            blockReasonRegistered = false;
            ErrorBox(window, L"无法启动倒计时检查。", error);
            return false;
        }
        guard.SetEnabled(true);
        Poll(window);
    } else {
        // KillTimer does not discard already queued WM_TIMER messages.
        guard.SetEnabled(false);
        KillTimer(window, PollTimer);
        if (blockReasonRegistered) ShutdownBlockReasonDestroy(window);
        blockReasonRegistered = false;
    }
    UpdateAppearance(window);
    return true;
}

static void ShowMenu(HWND window) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    wchar_t status[128];
    if (guard.error) wsprintfW(status, L"倒计时取消异常：错误 %lu", guard.error);
    else if (guard.enabled) wsprintfW(status, L"保护已开启 · 已取消 %lu 次倒计时", guard.cancelled);
    else lstrcpyW(status, L"保护已解除，可以手动重启");
    AppendMenuW(menu, MF_STRING | MF_DISABLED, 0, status);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (guard.enabled ? MF_CHECKED : 0), EnableCommand, L"开启保护");
    AppendMenuW(menu, MF_STRING | (!guard.enabled ? MF_CHECKED : 0), ReleaseCommand, L"解除保护（不重启）");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ExitCommand, L"退出并解除保护");
    POINT point;
    GetCursorPos(&point);
    SetForegroundWindow(window);
    UINT choice = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
        point.x, point.y, 0, window, nullptr));
    PostMessageW(window, WM_NULL, 0, 0);
    DestroyMenu(menu);
    if (choice) SendMessageW(window, WM_COMMAND, choice, 0);
}

static void Cleanup(HWND window) {
    SetProtection(window, false);
    KillTimer(window, TrayRetryTimer);
    if (trayPresent) Shell_NotifyIconW(NIM_DELETE, &tray);
    trayPresent = false;
}

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (taskbarCreated && message == taskbarCreated) {
        trayPresent = false;
        AddTray(window);
        return 0;
    }
    switch (message) {
    case WM_QUERYENDSESSION:
        // Includes logoff. Forced/critical shutdown can override this veto.
        return guard.BlocksSession() ? FALSE : TRUE;
    case WM_ENDSESSION:
        if (wParam) { Cleanup(window); PostQuitMessage(0); }
        return 0;
    case WM_TIMER:
        if (wParam == PollTimer) Poll(window);
        else if (wParam == TrayRetryTimer && !trayPresent) AddTray(window);
        return 0;
    case TrayMessage:
        if (lParam == WM_RBUTTONUP || lParam == WM_LBUTTONUP) ShowMenu(window);
        return 0;
    case ShowMenuMessage:
        ShowMenu(window);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wParam) == EnableCommand) SetProtection(window, true);
        else if (LOWORD(wParam) == ReleaseCommand) SetProtection(window, false);
        else if (LOWORD(wParam) == ExitCommand) DestroyWindow(window);
        return 0;
    case WM_CLOSE:
        // The hidden window is not a user close target; use the explicit tray action.
        return 0;
    case WM_DESTROY:
        Cleanup(window);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, FALSE, MutexName);
    if (!mutex) { ErrorBox(nullptr, L"无法创建程序实例。", GetLastError()); return 1; }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = nullptr;
        for (int attempt = 0; attempt < 20 && !existing; ++attempt) {
            existing = FindWindowW(ClassName, nullptr);
            if (!existing) Sleep(50);
        }
        if (!existing || !PostMessageW(existing, ShowMenuMessage, 0, 0))
            ErrorBox(nullptr, L"无法打开已运行程序的菜单，请查看托盘图标。", existing ? GetLastError() : ERROR_TIMEOUT);
        CloseHandle(mutex);
        return 0;
    }
    DWORD error = EnableShutdownPrivilege();
    if (error) {
        ErrorBox(nullptr, L"无法启用取消关机权限。请尝试以管理员身份运行。", error);
        CloseHandle(mutex);
        return 1;
    }
    if (!SetProcessShutdownParameters(0x3FF, 0)) {
        ErrorBox(nullptr, L"无法设置提前接收关机通知。", GetLastError());
        CloseHandle(mutex);
        return 1;
    }
    activeIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_GUARD_ACTIVE));
    releasedIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_GUARD_RELEASED));
    errorIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_GUARD_ERROR));
    if (!activeIcon || !releasedIcon || !errorIcon) {
        ErrorBox(nullptr, L"无法加载程序图标。", GetLastError());
        CloseHandle(mutex);
        return 1;
    }
    WNDCLASSW windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = activeIcon;
    windowClass.lpszClassName = ClassName;
    if (!RegisterClassW(&windowClass)) { CloseHandle(mutex); return 1; }
    // A hidden top-level window receives session broadcasts; HWND_MESSAGE does not.
    HWND window = CreateWindowExW(0, ClassName, AppName, WS_OVERLAPPED,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr);
    if (!window) { CloseHandle(mutex); return 1; }
    taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    // Preserve Explorer recovery and the desktop relaunch menu if run elevated.
    // Do not admit WM_COMMAND: lower-integrity callers can only display the menu.
    if (!taskbarCreated ||
        !ChangeWindowMessageFilterEx(window, taskbarCreated, MSGFLT_ALLOW, nullptr) ||
        !ChangeWindowMessageFilterEx(window, ShowMenuMessage, MSGFLT_ALLOW, nullptr)) {
        ErrorBox(window, L"无法配置托盘恢复和菜单入口。", GetLastError());
        DestroyWindow(window);
        CloseHandle(mutex);
        return 1;
    }
    tray.cbSize = sizeof(tray);
    tray.hWnd = window;
    tray.uID = 1;
    tray.uCallbackMessage = TrayMessage;
    AddTray(window);
    if (!SetProtection(window, true)) {
        DestroyWindow(window);
        CloseHandle(mutex);
        return 1;
    }
    MSG message;
    BOOL received;
    while ((received = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (IsWindow(window)) { Cleanup(window); DestroyWindow(window); }
    CloseHandle(mutex);
    return received == -1 ? 1 : 0;
}
