#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <cstdio>

static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s (error %lu)\n", __LINE__, #expr, GetLastError()); ++failures; } } while (0)

static HANDLE testProcess = nullptr;
static DWORD testProcessId = 0;

struct OwnedTestChild {
    HANDLE job = nullptr;
    PROCESS_INFORMATION process = {};

    void Close() {
        // These handles come only from this harness's CreateProcess call.
        // Never find or reopen another process for cleanup.
        if (process.hProcess && WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0) {
            BOOL terminated = TerminateProcess(process.hProcess, 2);
            CHECK(terminated || WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0);
        }
        if (job) { CloseHandle(job); job = nullptr; }
        if (process.hThread) { CloseHandle(process.hThread); process.hThread = nullptr; }
        if (process.hProcess) {
            CHECK(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
            CloseHandle(process.hProcess);
            process.hProcess = nullptr;
        }
        testProcess = nullptr;
        testProcessId = 0;
    }

    ~OwnedTestChild() { Close(); }
};

static bool IsOwnedWindow(HWND window) {
    DWORD pid = 0;
    return testProcess && WaitForSingleObject(testProcess, 0) == WAIT_TIMEOUT &&
        GetWindowThreadProcessId(window, &pid) != 0 && pid == testProcessId;
}

static LRESULT SendLocal(HWND window, UINT message, WPARAM wParam = 0, LPARAM lParam = 0) {
    DWORD_PTR result = 99;
    bool owned = IsOwnedWindow(window);
    CHECK(owned);
    if (!owned) return static_cast<LRESULT>(result);
    CHECK(SendMessageTimeoutW(window, message, wParam, lParam, SMTO_ABORTIFHUNG, 3000, &result) != 0);
    return static_cast<LRESULT>(result);
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    std::puts("Native window/tray test: real shutdown-block reason registration; cancellation is stubbed. "
        "No system shutdown, restart, or logoff is initiated.");
    // Test only our newly spawned instance. Never message another guard or HWND_BROADCAST.
    if (FindWindowW(L"RebootGuard.Minimal.2026.Test", nullptr)) {
        std::puts("Refusing to test: a guard is already running.");
        return 2;
    }
    OwnedTestChild child;
    child.job = CreateJobObjectW(nullptr, nullptr);
    if (!child.job) { CHECK(child.job != nullptr); return 2; }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    BOOL configured = SetInformationJobObject(child.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    CHECK(configured);
    if (!configured) return 2;
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION& process = child.process;
    // Attach while suspended so an interrupted harness cannot leave a running test guard.
    BOOL created = CreateProcessW(argv[1], nullptr, nullptr, nullptr, FALSE, CREATE_SUSPENDED,
        nullptr, nullptr, &startup, &process);
    CHECK(created);
    if (!created) return 2;
    BOOL assigned = AssignProcessToJobObject(child.job, process.hProcess);
    CHECK(assigned);
    if (!assigned) return 2;
    testProcess = process.hProcess;
    testProcessId = process.dwProcessId;
    DWORD resumed = ResumeThread(process.hThread);
    CHECK(resumed != static_cast<DWORD>(-1));
    if (resumed == static_cast<DWORD>(-1)) return 2;
    CloseHandle(process.hThread);
    process.hThread = nullptr;
    HWND window = nullptr;
    bool ready = false;
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0) break;
        window = FindWindowW(L"RebootGuard.Minimal.2026.Test", nullptr);
        wchar_t title[128] = {};
        if (IsOwnedWindow(window)) GetWindowTextW(window, title, ARRAYSIZE(title));
        if (wcsstr(title, L"已开启")) { ready = true; break; }
        Sleep(50);
    }
    if (!ready || !IsOwnedWindow(window)) {
        std::puts("Could not identify a ready test-owned window; no messages sent. Cleaning up only the spawned child.");
        return 2;
    }
    CHECK(GetAncestor(window, GA_ROOT) == window);
    CHECK(!IsWindowVisible(window));
    wchar_t reason[256] = {};
    DWORD reasonSize = ARRAYSIZE(reason);
    CHECK(ShutdownBlockReasonQuery(window, reason, &reasonSize));
    CHECK(wcsstr(reason, L"后台计算") != nullptr);
    // Synthetic messages sent directly to this test HWND do not request a system shutdown.
    CHECK(SendLocal(window, WM_QUERYENDSESSION) == FALSE);
    CHECK(SendLocal(window, WM_QUERYENDSESSION, 0, ENDSESSION_LOGOFF) == FALSE);
    SendLocal(window, WM_ENDSESSION, FALSE);
    CHECK(SendLocal(window, WM_QUERYENDSESSION) == FALSE);

    SendLocal(window, WM_COMMAND, 1002); // Release, no reboot action.
    CHECK(SendLocal(window, WM_QUERYENDSESSION) == TRUE);
    reasonSize = ARRAYSIZE(reason);
    CHECK(!ShutdownBlockReasonQuery(window, reason, &reasonSize));
    SendLocal(window, WM_TIMER, 1); // Simulate an already queued timer after release.
    CHECK(SendLocal(window, WM_QUERYENDSESSION) == TRUE);
    SendLocal(window, WM_COMMAND, 1001);
    CHECK(SendLocal(window, WM_QUERYENDSESSION) == FALSE);

    NOTIFYICONDATAW icon = {};
    icon.cbSize = sizeof(icon);
    icon.hWnd = window;
    icon.uID = 1;
    if (!IsOwnedWindow(window)) { CHECK(IsOwnedWindow(window)); return 2; }
    CHECK(Shell_NotifyIconW(NIM_DELETE, &icon));
    SendLocal(window, RegisterWindowMessageW(L"TaskbarCreated"));
    NOTIFYICONIDENTIFIER identifier = {};
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = window;
    identifier.uID = 1;
    RECT rect;
    CHECK(SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect)));
    CHECK(SendLocal(window, WM_QUERYENDSESSION) == FALSE);

    // Explorer also sends TaskbarCreated on DPI changes while the icon still exists.
    SendLocal(window, RegisterWindowMessageW(L"TaskbarCreated"));
    SendLocal(window, WM_TIMER, 2); // A stale tray retry after successful recovery.
    CHECK(SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect)));

    SendLocal(window, WM_COMMAND, 1003); // Only terminates our test application.
    CHECK(FAILED(Shell_NotifyIconGetRect(&identifier, &rect))); // No orphaned icon.
    CHECK(WaitForSingleObject(process.hProcess, 5000) == WAIT_OBJECT_0);
    DWORD exitCode = 99;
    CHECK(GetExitCodeProcess(process.hProcess, &exitCode));
    CHECK(exitCode == 0);
    CHECK(!IsWindow(window));
    child.Close();
    std::printf("Native window/tray lifecycle (real block reason, stubbed cancellation): %s. "
        "No system shutdown, restart, or logoff initiated.\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
