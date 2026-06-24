#pragma once
#include <span>
#include <string>
#include <vector>
#include <cstdint>

#include "camera_resolution.hpp"

class V4l2Device {
public:
    V4l2Device(const std::string& device_path, CameraResolution target_resolution);
    ~V4l2Device();

    CameraResolution get_resolution() const;
    int get_fd() const;

    struct Frame {
        std::span<const uint8_t> payload;
        uint32_t buffer_index;
    };

    bool dequeue_frame(Frame& out_frame);

    void enqueue_buffer(uint32_t buffer_index);

private:
    struct MmapBuffer {
        void* start;
        size_t length;
    };

    int fd_;
    std::vector<MmapBuffer> buffers_;
    CameraResolution active_resolution_;
};