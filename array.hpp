#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#include "disruptor.hpp"

struct alignas(disruptor::CACHE_LINE_SIZE) Array {
    std::vector<uint8_t> storage;
    size_t activeSize_{0};

    void preAllocate(size_t frame_reserve_capacity) {
        storage.resize(frame_reserve_capacity);
        activeSize_ = 0;
    }

    void clear() noexcept {
        activeSize_ = 0;
    }

    bool empty() const noexcept {
        return activeSize_ == 0;
    }

    void insert(std::span<const uint8_t> content) {
        size_t write_offset = activeSize_;

        if (write_offset + content.size() > storage.size()) {
            storage.resize(write_offset + content.size());
        }

        std::memcpy(storage.data() + write_offset, content.data(), content.size());
        activeSize_ += content.size();
    }

    size_t size() const noexcept {
        return activeSize_;
    }

    uint8_t front() const {
        if (empty()) {
            throw std::out_of_range("Buffer is empty");
        }
        return storage.front();
    }

    std::vector<uint8_t>& data() noexcept {
        return storage;
    }
};
