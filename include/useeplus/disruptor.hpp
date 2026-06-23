#pragma once
#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstdint>
#include <new>
#include <optional>
#include <thread>

namespace disruptor {

inline static void yieldCurrentThread() {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
    asm volatile("yield" ::: "memory");
#else
    std::this_thread::yield();
#endif
}

#if defined(__cpp_lib_hardware_interference_size) && !defined(__arm__) && !defined(__aarch64__)
inline constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
inline constexpr size_t CACHE_LINE_SIZE = 64;
#endif

struct alignas(CACHE_LINE_SIZE) Sequence {
    std::atomic<int64_t> value{-1};

    [[nodiscard]] int64_t get() const noexcept {
        return value.load(std::memory_order_acquire);
    }

    void set(int64_t newValue) noexcept {
        value.store(newValue, std::memory_order_release);
    }
};

template <typename T>
concept IsWaitStrategy = requires(T t, Sequence& sequence, int64_t target) {
    { t.waitFor(sequence, target) } -> std::same_as<int64_t>;
    { t.signalAllWhenBlocking(sequence) } -> std::same_as<void>;
};

class YieldingWaitStrategy {
   public:
    [[nodiscard]] int64_t waitFor(const Sequence& sequence, int64_t target) const noexcept {
        int64_t current{0};
        uint32_t counter = 2000;

        while ((current = sequence.get()) < target) {
            if (counter == 0) {
                yieldCurrentThread();
            } else {
                --counter;
            }
        }
        return current;
    }

    void signalAllWhenBlocking(Sequence& /*sequence*/) const noexcept {}
};

class BlockingWaitStrategy {
   public:
    [[nodiscard]] int64_t waitFor(const Sequence& sequence, int64_t target) const noexcept {
        int64_t current = sequence.get();
        while (current < target) {
            sequence.value.wait(current, std::memory_order_acquire);
            current = sequence.get();
        }
        return current;
    }

    void signalAllWhenBlocking(Sequence& sequence) const noexcept {
        sequence.value.notify_all();
    }
};

template <typename T, size_t Capacity>
    requires(std::has_single_bit(Capacity))
class alignas(CACHE_LINE_SIZE) RingBuffer {
   private:
    static constexpr size_t mask = Capacity - 1;
    std::array<T, Capacity> data{};

   public:
    [[nodiscard]] T& operator[](int64_t sequence) noexcept {
        return data[sequence & mask];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    }

    [[nodiscard]] static constexpr size_t capacity() noexcept {
        return Capacity;
    }
};

template <typename T, size_t Capacity, IsWaitStrategy StrategyType = YieldingWaitStrategy>
class Disruptor {
   private:
    alignas(CACHE_LINE_SIZE) RingBuffer<T, Capacity> buffer;
    alignas(CACHE_LINE_SIZE) Sequence publishedSequence;
    alignas(CACHE_LINE_SIZE) Sequence consumerSequence;

    struct alignas(CACHE_LINE_SIZE) ProducerState {
        int64_t sequence{-1};
        int64_t cachedConsumerSequence{-1};
    };
    ProducerState producer;

    [[no_unique_address]] StrategyType waitStrategy;

   public:
    void preAllocate(const int64_t slotSize) {
        for (size_t i = 0; i < Capacity; i++) {
            T& slot = getBySequence(i);
            slot.preAllocate(slotSize);
        }
    }

    [[nodiscard]] int64_t claim() noexcept {
        int64_t nextSequence = producer.sequence + 1;
        int64_t wrapPoint = nextSequence - buffer.capacity();

        if (producer.cachedConsumerSequence < wrapPoint) {
            while ((producer.cachedConsumerSequence = consumerSequence.get()) < wrapPoint) {
                yieldCurrentThread();
            }
        }

        producer.sequence = nextSequence;
        return nextSequence;
    }

    [[nodiscard]] std::optional<int64_t> tryClaim() noexcept {
        int64_t nextSequence = producer.sequence + 1;
        int64_t wrapPoint = nextSequence - buffer.capacity();

        if (producer.cachedConsumerSequence < wrapPoint) {
            producer.cachedConsumerSequence = consumerSequence.get();

            if (producer.cachedConsumerSequence < wrapPoint) {
                return std::nullopt;
            }
        }

        producer.sequence = nextSequence;
        return nextSequence;
    }

    [[nodiscard]] T& getBySequence(int64_t sequence) noexcept {
        return buffer[sequence];
    }

    void publish(int64_t sequence) noexcept {
        publishedSequence.set(sequence);
        waitStrategy.signalAllWhenBlocking(publishedSequence);
    }

    [[nodiscard]] int64_t waitFor(int64_t nextSequence) noexcept {
        return waitStrategy.waitFor(publishedSequence, nextSequence);
    }

    void markConsumed(int64_t sequence) noexcept {
        consumerSequence.set(sequence);
    }

    [[nodiscard]] int64_t getHighestPublished() const noexcept {
        return publishedSequence.get();
    }
};
}  // namespace disruptor
