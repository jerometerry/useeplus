#pragma once
#include "array.hpp"
#include "disruptor.hpp"

inline constexpr int64_t ARRAY_BUFFER_CAPACITY = 128;

using ArrayBuffer =
    disruptor::Disruptor<Array, ARRAY_BUFFER_CAPACITY, disruptor::BlockingWaitStrategy>;
