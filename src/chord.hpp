#pragma once
// Per-controller button-combination matcher. Pure logic: times are milliseconds
// from a monotonic clock supplied by the caller.
#include <linux/input-event-codes.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

struct ChordRule {
    std::set<int> buttons;
    int holdMs = 1000;
};

// Buttons that count towards the exact pressed set. Other key codes are ignored.
inline bool isControllerButton(int code) {
    switch (code) {
    case BTN_SOUTH: case BTN_EAST: case BTN_NORTH: case BTN_WEST:
    case BTN_SELECT: case BTN_START: case BTN_MODE:
    case BTN_TL: case BTN_TR: case BTN_THUMBL: case BTN_THUMBR:
        return true;
    default:
        return false;
    }
}

struct Chord {
    std::vector<ChordRule> rules;
    std::set<int> pressed;
    bool pending = false;  // a rule matches and waits for its deadline
    bool latched = false;  // nothing may fire until every button is released
    bool engaged = false;  // a rule matched since the last full release
    int selected = -1;
    std::int64_t deadline = 0;

    // Buttons already held when (re)configured can never start a shortcut.
    void configure(const std::vector<ChordRule> &configured, const std::set<int> &held) {
        rules = configured;
        pressed.clear();
        for (int code : held) {
            if (isControllerButton(code)) pressed.insert(code);
        }
        pending = false;
        selected = -1;
        engaged = false;
        latched = !pressed.empty();
    }

    void block() {
        pending = false;
        selected = -1;
        latched = !pressed.empty();
    }

    // Returns true when a rule newly matches; its deadline is then set.
    bool update(int code, bool down, bool home, std::int64_t now) {
        if (!isControllerButton(code)) return false;
        if (down) {
            if (!pressed.insert(code).second) return false;
        } else {
            if (!pressed.erase(code)) return false;
            block();
        }
        if (!home) block();
        if (pressed.empty()) {
            latched = false;
            engaged = false;
            return false;
        }
        if (latched) return false;
        selected = -1;
        pending = false;
        for (std::size_t i = 0; i < rules.size(); ++i) {
            if (rules[i].buttons == pressed) {
                selected = static_cast<int>(i);
                break;
            }
        }
        if (selected < 0) return false;
        pending = true;
        engaged = true;
        deadline = now + rules[selected].holdMs;
        return true;
    }

    // Index of the rule to fire now, or -1. Firing latches until full release.
    int due(bool home, std::int64_t now) {
        if (!home) block();
        if (!pending || latched || now < deadline) return -1;
        const int match = selected;
        block();
        return match;
    }

    // True from a full match until every button is released: native UI
    // input belongs to the shortcut during that time.
    bool active() const { return engaged && !pressed.empty(); }

    // True while the held buttons include View, Menu or Guide and could still
    // become a configured combination. Other buttons alone never qualify, so
    // A, B, X, Y, bumpers and sticks keep their normal function.
    bool candidate() const {
        if (latched || pressed.empty()) return false;
        if (!pressed.count(BTN_SELECT) && !pressed.count(BTN_START) && !pressed.count(BTN_MODE)) return false;
        for (const ChordRule &rule : rules) {
            if (std::includes(rule.buttons.begin(), rule.buttons.end(), pressed.begin(), pressed.end())) return true;
        }
        return false;
    }
};
