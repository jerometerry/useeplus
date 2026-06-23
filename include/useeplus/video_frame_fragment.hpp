#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

#include "disruptor.hpp"

struct alignas(disruptor::CACHE_LINE_SIZE) VideoFrameFragment {
    static constexpr size_t PADDING_SIZE = 128;

    std::vector<uint8_t> storage;
    size_t activeSize{0};

    VideoFrameFragment() {
        storage.resize(paddingSize());
    }

    void preAllocate(size_t frame_reserve_capacity) {
        storage.resize(PADDING_SIZE + frame_reserve_capacity);
        activeSize = 0;
    }

    void clear() noexcept {
        activeSize = 0;
    }

    bool empty() const noexcept {
        return activeSize == 0;
    }

    void insertContent(std::span<const uint8_t> content) {
        size_t write_offset = paddingSize() + activeSize;

        if (write_offset + content.size() > storage.size()) {
            storage.resize(write_offset + content.size());
        }

        std::memcpy(storage.data() + write_offset, content.data(), content.size());
        activeSize += content.size();
    }

    static size_t paddingSize() noexcept {
        return PADDING_SIZE;
    }

    size_t contentSize() const noexcept {
        return activeSize;
    }

    size_t totalSize() const noexcept {
        return paddingSize() + activeSize;
    }

    uint8_t front() const {
        if (empty()) throw std::out_of_range("Buffer is empty");
        return storage[paddingSize()];
    }

    std::span<const uint8_t> getContentSlice() const noexcept {
        return {storage.data() + paddingSize(), activeSize};
    }

    std::span<uint8_t> getMutableContentSlice() noexcept {
        return {storage.data() + paddingSize(), activeSize};
    }

    std::vector<uint8_t>& data() noexcept {
        return storage;
    }
};
