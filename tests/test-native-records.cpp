#include "native-records.hpp"

#include <cstdio>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 1; } } while (0)

static int test_take_matching_in_order() {
    NativeRecords records;
    int a = 0, b = 0;
    records.record(&a, 2, true);
    records.record(&b, 7, true);
    records.record(&a, 7, false);
    const auto taken = records.take(&a);
    CHECK(taken.size() == 2);
    CHECK(taken[0].button == 2 && taken[0].down);
    CHECK(taken[1].button == 7 && !taken[1].down);
    CHECK(records.take(&a).empty());
    CHECK(records.take(&b).size() == 1);
    CHECK(records.take(nullptr).empty());
    return 0;
}

static int test_oldest_evicted() {
    NativeRecords records;
    int events[NativeRecords::capacity + 1] = {};
    for (int &event : events) records.record(&event, 0, true);
    CHECK(records.take(&events[0]).empty());
    for (std::size_t i = 1; i < NativeRecords::capacity + 1; ++i) CHECK(records.take(&events[i]).size() == 1);
    return 0;
}

static int test_record_cannot_throw() {
    NativeRecords records;
    static_assert(noexcept(records.record(nullptr, 0, false)), "record() is called from Steam Link through C code");
    return 0;
}

static int test_concurrent_use() {
    NativeRecords records;
    int a = 0, b = 0;
    auto writer = [&](int *event) {
        for (int i = 0; i < 20000; ++i) records.record(event, i, i % 2);
    };
    std::thread one(writer, &a), two(writer, &b);
    std::size_t taken = 0;
    for (int i = 0; i < 20000; ++i) taken += records.take(i % 2 ? &a : &b).size();
    one.join();
    two.join();
    taken += records.take(&a).size() + records.take(&b).size();
    CHECK(taken <= 40000);
    CHECK(records.take(&a).empty() && records.take(&b).empty());
    return 0;
}

int main() {
    if (test_take_matching_in_order() || test_oldest_evicted() || test_record_cannot_throw() || test_concurrent_use())
        return 1;
    std::puts("PASS native button records: matching, eviction, no-throw recording and concurrency");
}
