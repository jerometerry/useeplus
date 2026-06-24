#pragma once
#include <atomic>
#include <functional>
#include <span>
#include <thread>

#include "v4l2_device.hpp"

class V4l2VideoSource {
   public:
    using FrameHandler = std::function<bool(std::span<const uint8_t>)>;

    V4l2VideoSource(FrameHandler handler, std::atomic<bool>* running)
        : handler_(std::move(handler)), running_(running) {}

    ~V4l2VideoSource() {
        stop();
    }

    void start(V4l2Device& device) {
        workerThread_ = std::jthread(&V4l2VideoSource::loop, this, &device);
    }

    void stop() {
        if (workerThread_.joinable()) workerThread_.join();
    }

   private:
    void loop(V4l2Device* device);

    FrameHandler handler_;
    std::atomic<bool>* running_;
    std::jthread workerThread_;
};
