#pragma once
#include <cstdint>

struct Chord {
    bool view = false, menu = false, blocked = false, pending = false;
    std::int64_t deadline = 0;
    bool update(int code, bool down, bool home, std::int64_t now) {
        if (code != 314 && code != 315) return false;
        bool &key = code == 314 ? view : menu;
        if (key == down) return false;
        key = down;
        if (!down || !home) block();
        if (!view && !menu) { blocked = false; pending = false; }
        if (blocked || !view || !menu || pending) return false;
        deadline = now + 1000; pending = true; return true;
    }
    bool due(bool home, std::int64_t now) {
        if (!home) block();
        if (!pending || blocked || now < deadline) return false;
        block(); return true;
    }
    void reset(bool v = false, bool m = false) {
        view = v; menu = m; blocked = v || m; pending = false;
    }
    void block() { pending = false; blocked = view || menu; }
};
