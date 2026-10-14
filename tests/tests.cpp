#include <cstdio>
#include "GuardState.h"

static unsigned calls = 0;
static unsigned long result = 1116;
static unsigned long CancelPending() { ++calls; return result; }
static int failures = 0;
#define CHECK(expr) do { if (!(expr)) { std::printf("FAIL line %d: %s\n", __LINE__, #expr); ++failures; } } while (0)

int main() {
    GuardState guard;
    CHECK(!guard.BlocksSession()); // Enable only after native registration succeeds.
    guard.SetEnabled(true);
    CHECK(guard.BlocksSession());
    guard.Tick(CancelPending);
    CHECK(calls == 1);
    CHECK(guard.error == 0); // No shutdown in progress is normal, not a failure.
    CHECK(guard.cancelled == 0);

    result = 0;
    guard.Tick(CancelPending);
    CHECK(guard.cancelled == 1);
    CHECK(guard.error == 0);

    result = 5; // Permission failure must not be displayed as healthy cancellation.
    guard.Tick(CancelPending);
    CHECK(guard.error == 5);
    CHECK(guard.cancelled == 1);
    result = 1116;
    guard.Tick(CancelPending);
    CHECK(guard.error == 0);

    guard.SetEnabled(false);
    CHECK(!guard.BlocksSession());
    unsigned before = calls;
    result = 0;
    guard.Tick(CancelPending); // A queued timer after release must do nothing.
    CHECK(calls == before);
    CHECK(guard.cancelled == 1);
    CHECK(guard.error == 0);

    guard.SetEnabled(true);
    CHECK(guard.BlocksSession());
    guard.Tick(CancelPending);
    CHECK(calls == before + 1);
    CHECK(guard.cancelled == 2);
    std::printf("Guard lifecycle checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
