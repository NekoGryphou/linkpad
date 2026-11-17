#include "chord.hpp"

#include <linux/input-event-codes.h>

#include <cstdio>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static const ChordRule viewMenu{{BTN_SELECT, BTN_START}, 1000};
static const ChordRule viewMenuA{{BTN_SELECT, BTN_START, BTN_SOUTH}, 1000};
static const ChordRule viewX{{BTN_SELECT, BTN_NORTH}, 500};

static Chord make(const std::vector<ChordRule> &rules) {
    Chord chord;
    chord.configure(rules, {});
    return chord;
}

static int test_basic_fire() {
    Chord c = make({viewMenu});
    CHECK(!c.update(BTN_SELECT, true, true, 100));
    CHECK(c.update(BTN_START, true, true, 200));
    CHECK(c.due(true, 1199) == -1);
    CHECK(c.due(true, 1200) == 0);
    CHECK(c.due(true, 5000) == -1);
    return 0;
}

static int test_custom_hold() {
    Chord c = make({ChordRule{{BTN_SELECT, BTN_START}, 250}});
    c.update(BTN_SELECT, true, true, 0);
    c.update(BTN_START, true, true, 10);
    CHECK(c.due(true, 259) == -1);
    CHECK(c.due(true, 260) == 0);
    return 0;
}

static int test_overlap_no_fallback() {
    Chord c = make({viewMenu, viewMenuA});
    c.update(BTN_SELECT, true, true, 0);
    CHECK(c.update(BTN_START, true, true, 10));
    CHECK(c.update(BTN_SOUTH, true, true, 500));
    CHECK(c.due(true, 1010) == -1);   // smaller combo replaced
    CHECK(c.due(true, 1500) == 1);    // larger combo, timed from its own completion
    c.update(BTN_SOUTH, false, true, 1600);
    CHECK(c.due(true, 5000) == -1);

    Chord d = make({viewMenu, viewMenuA});
    d.update(BTN_SELECT, true, true, 0);
    d.update(BTN_START, true, true, 10);
    d.update(BTN_SOUTH, true, true, 500);
    d.update(BTN_SOUTH, false, true, 600);   // partial release before the deadline
    CHECK(d.due(true, 5000) == -1);           // no fallback to view+menu
    d.update(BTN_SELECT, false, true, 5100);
    d.update(BTN_START, false, true, 5100);
    d.update(BTN_SELECT, true, true, 6000);
    d.update(BTN_START, true, true, 6000);
    CHECK(d.due(true, 7000) == 0);            // full release re-arms
    return 0;
}

static int test_two_rules_independent() {
    Chord c = make({viewMenu, viewX});
    c.update(BTN_SELECT, true, true, 0);
    CHECK(c.update(BTN_NORTH, true, true, 0));
    CHECK(c.due(true, 499) == -1);
    CHECK(c.due(true, 500) == 1);
    return 0;
}

static int test_release_rearm() {
    Chord c = make({viewMenu});
    c.update(BTN_SELECT, true, true, 0);
    c.update(BTN_START, true, true, 0);
    CHECK(c.due(true, 1000) == 0);
    c.update(BTN_SELECT, false, true, 1100);
    CHECK(!c.update(BTN_SELECT, true, true, 1200));
    CHECK(c.due(true, 5000) == -1);
    c.update(BTN_SELECT, false, true, 5100);
    c.update(BTN_START, false, true, 5100);
    c.update(BTN_SELECT, true, true, 5200);
    CHECK(c.update(BTN_START, true, true, 5200));
    CHECK(c.due(true, 6200) == 0);
    return 0;
}

static int test_release_before_deadline() {
    Chord c = make({viewMenu});
    c.update(BTN_SELECT, true, true, 0);
    c.update(BTN_START, true, true, 0);
    c.update(BTN_START, false, true, 500);
    CHECK(c.due(true, 1000) == -1);
    CHECK(!c.update(BTN_START, true, true, 600));  // re-press without full release
    CHECK(c.due(true, 2000) == -1);
    return 0;
}

static int test_home_guard() {
    Chord c = make({viewMenu});
    c.update(BTN_SELECT, true, false, 0);           // first press off home
    CHECK(!c.update(BTN_START, true, true, 10));
    CHECK(c.due(true, 5000) == -1);
    c.update(BTN_SELECT, false, true, 5100);
    c.update(BTN_START, false, true, 5100);
    c.update(BTN_SELECT, true, true, 6000);
    CHECK(c.update(BTN_START, true, true, 6000));
    CHECK(c.due(false, 7000) == -1);                // home lost at the deadline
    CHECK(c.due(true, 7001) == -1);
    return 0;
}

static int test_reconfigure_while_held() {
    Chord c;
    c.configure({viewMenu}, {BTN_SELECT, BTN_START});
    CHECK(c.due(true, 10000) == -1);
    CHECK(!c.update(BTN_SOUTH, true, true, 10000));
    c.update(BTN_SOUTH, false, true, 10100);
    c.update(BTN_SELECT, false, true, 10100);
    c.update(BTN_START, false, true, 10100);
    c.update(BTN_SELECT, true, true, 11000);
    CHECK(c.update(BTN_START, true, true, 11000));
    CHECK(c.due(true, 12000) == 0);
    return 0;
}

static int test_resync_blocks() {
    Chord c = make({viewMenu});
    c.update(BTN_SELECT, true, true, 0);
    c.update(BTN_START, true, true, 0);
    c.configure({viewMenu}, {BTN_SELECT, BTN_START});  // SYN_DROPPED resync with keys held
    CHECK(c.due(true, 2000) == -1);
    c.configure({viewMenu}, {});                         // resync after everything was released
    c.update(BTN_SELECT, true, true, 3000);
    CHECK(c.update(BTN_START, true, true, 3000));
    CHECK(c.due(true, 4000) == 0);
    return 0;
}

static int test_extra_button_no_match() {
    Chord c = make({viewMenu});
    c.update(BTN_SELECT, true, true, 0);
    CHECK(c.update(BTN_START, true, true, 0));
    CHECK(!c.update(BTN_EAST, true, true, 100));
    CHECK(c.due(true, 2000) == -1);
    Chord d = make({viewMenu});
    d.update(BTN_EAST, true, true, 0);
    d.update(BTN_SELECT, true, true, 0);
    CHECK(!d.update(BTN_START, true, true, 0));
    CHECK(d.due(true, 2000) == -1);
    return 0;
}

static int test_ignores_unrelated_codes() {
    Chord c = make({viewMenu});
    c.update(BTN_SELECT, true, true, 0);
    CHECK(!c.update(KEY_VOLUMEUP, true, true, 0));     // not a controller button
    CHECK(c.update(BTN_START, true, true, 0));
    CHECK(c.due(true, 1000) == 0);
    return 0;
}

static int test_active() {
    Chord c = make({viewMenu});
    CHECK(!c.active());
    c.update(BTN_SOUTH, true, true, 0);
    CHECK(!c.active());                                 // plain button press is not ours
    c.update(BTN_SOUTH, false, true, 10);
    c.update(BTN_SELECT, true, true, 100);
    CHECK(!c.active());                                 // incomplete combination
    c.update(BTN_START, true, true, 100);
    CHECK(c.active());
    CHECK(c.due(true, 1100) == 0);
    CHECK(c.active());                                  // fired, still held
    c.update(BTN_SELECT, false, true, 1200);
    CHECK(c.active());                                  // until full release
    c.update(BTN_START, false, true, 1200);
    CHECK(!c.active());
    Chord held;
    held.configure({viewMenu}, {BTN_SELECT, BTN_START});
    CHECK(!held.active());                              // held before configuration: not ours
    return 0;
}

static int test_device_isolation() {
    Chord one = make({viewMenu}), two = make({viewMenu});
    one.update(BTN_SELECT, true, true, 0);
    two.update(BTN_START, true, true, 0);
    CHECK(one.due(true, 3000) == -1);
    CHECK(two.due(true, 3000) == -1);
    return 0;
}

int main() {
    if (test_basic_fire() || test_custom_hold() || test_overlap_no_fallback() || test_two_rules_independent() ||
        test_release_rearm() || test_release_before_deadline() || test_home_guard() ||
        test_reconfigure_while_held() || test_resync_blocks() || test_extra_button_no_match() ||
        test_ignores_unrelated_codes() || test_active() || test_device_isolation())
        return 1;
    std::puts("PASS chord combinations, overlap, release, home guard, resync and isolation");
}
