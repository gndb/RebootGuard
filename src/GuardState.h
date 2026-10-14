#pragma once

struct GuardState {
    bool enabled = false;
    unsigned long error = 0;
    unsigned long cancelled = 0;

    void SetEnabled(bool value) { enabled = value; error = 0; }
    bool BlocksSession() const { return enabled; }
    void Tick(unsigned long (*cancelPending)()) {
        if (!enabled) return;
        unsigned long outcome = cancelPending();
        if (outcome == 0) ++cancelled;
        error = (outcome == 1116) ? 0 : outcome;
    }
};
