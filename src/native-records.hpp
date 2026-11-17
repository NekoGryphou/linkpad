#pragma once
// QControllerEvent::setButton calls reported by bootstrap.so, kept until the
// event reaches the application event filter. Events can be built on another
// thread and delivered later, so records are matched by event address.
// record() runs inside Steam Link behind C code and must never throw or
// allocate: a fixed ring buffer overwrites the oldest record when full.
#include <array>
#include <atomic>
#include <cstddef>
#include <vector>

struct NativeButton {
    const void *event = nullptr;
    int button = 0;
    bool down = false;
};

class NativeRecords {
public:
    static constexpr std::size_t capacity = 32;

    void record(const void *event, int button, bool down) noexcept {
        Lock lock(busy);
        ring[next % capacity] = NativeButton{event, button, down};
        ++next;
    }

    // Matching records, oldest first; they are removed.
    std::vector<NativeButton> take(const void *event) {
        std::vector<NativeButton> taken;
        if (!event) return taken;
        taken.reserve(capacity);  // allocate before locking; push_back below cannot throw
        Lock lock(busy);
        for (std::size_t i = 0; i < capacity; ++i) {
            NativeButton &slot = ring[(next + i) % capacity];
            if (slot.event != event) continue;
            taken.push_back(slot);
            slot = NativeButton();
        }
        return taken;
    }

private:
    struct Lock {
        std::atomic_flag &flag;
        explicit Lock(std::atomic_flag &f) noexcept : flag(f) {
            while (flag.test_and_set(std::memory_order_acquire)) {
            }
        }
        ~Lock() { flag.clear(std::memory_order_release); }
    };

    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    std::array<NativeButton, capacity> ring{};
    std::size_t next = 0;
};
