#pragma once
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "constants.hpp"
#include "mjpeg_server.hpp"
#include "video_frame_buffer.hpp"

struct CameraResolution {
    uint32_t width;
    uint32_t height;

    bool operator==(const CameraResolution&) const = default;
};

namespace SupportedResolutions {
constexpr CameraResolution HD_720P{1280, 720};
constexpr CameraResolution VGA_480P{640, 480};
constexpr CameraResolution LOW_240P{320, 240};
}  // namespace SupportedResolutions

class V4l2Camera {
   public:
    using FrameHandler = std::function<bool(std::span<const uint8_t>)>;

   private:
    struct MmapBuffer {
        void* start;
        size_t length;
    };

    int fd_;
    std::vector<MmapBuffer> buffers_;
    FrameHandler frame_handler_;
    const std::atomic<bool>& running_;
    CameraResolution active_resolution_;

   public:
    V4l2Camera(const std::string& device_path, CameraResolution target_resolution,
               FrameHandler frame_handler, const std::atomic<bool>& running)
        : fd_(open(device_path.c_str(), O_RDWR | O_NONBLOCK, 0)),
          frame_handler_(std::move(frame_handler)),
          running_(running),
          active_resolution_(target_resolution) {
        if (fd_ < 0) {
            throw std::runtime_error("Cannot open " + device_path);
        }

        // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
        struct v4l2_format fmt = {};
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        fmt.fmt.pix.width = target_resolution.width;
        fmt.fmt.pix.height = target_resolution.height;

        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
            throw std::runtime_error("Failed to set video format");
        }

        if (ioctl(fd_, VIDIOC_G_FMT, &fmt) < 0) {
            throw std::runtime_error("Failed to verify video format");
        }

        if (fmt.fmt.pix.width != target_resolution.width ||
            fmt.fmt.pix.height != target_resolution.height) {
            std::cerr << "Warning: Requested " << target_resolution.width << "x"
                      << target_resolution.height << ", but driver fell back to "
                      << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << "\n";

            active_resolution_.width = fmt.fmt.pix.width;
            active_resolution_.height = fmt.fmt.pix.height;
        }

        struct v4l2_requestbuffers req = {};
        req.count = 4;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;
        if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
            throw std::runtime_error("Failed to request buffers");
        }

        buffers_.resize(req.count);
        for (unsigned int i = 0; i < req.count; ++i) {
            struct v4l2_buffer buf = {};
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            buf.index = i;
            ioctl(fd_, VIDIOC_QUERYBUF, &buf);

            buffers_[i].length = buf.length;
            buffers_[i].start =
                mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);

            ioctl(fd_, VIDIOC_QBUF, &buf);
        }
        // NOLINTEND(cppcoreguidelines-pro-type-union-access)

        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
            throw std::runtime_error("Failed to start streaming");
        }
    }

    ~V4l2Camera() {
        if (fd_ >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(fd_, VIDIOC_STREAMOFF, &type);

            for (auto& buf : buffers_) {
                munmap(buf.start, buf.length);
            }
            close(fd_);
        }
    }

    CameraResolution get_resolution() const {
        return active_resolution_;
    }

    void poll_frames() {
        while (running_.load(std::memory_order_relaxed)) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd_, &fds);
            struct timeval tv = {1, 0};

            int r = select(fd_ + 1, &fds, nullptr, nullptr, &tv);

            if (r > 0) {
                struct v4l2_buffer buf = {};
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;

                if (ioctl(fd_, VIDIOC_DQBUF, &buf) == 0) {
                    if (!(buf.flags & V4L2_BUF_FLAG_ERROR)) {
                        uint8_t* payload_start = static_cast<uint8_t*>(buffers_[buf.index].start);
                        std::span<const uint8_t> payload(payload_start, buf.bytesused);

                        if (frame_handler_) {
                            frame_handler_(payload);
                        }
                    }
                    ioctl(fd_, VIDIOC_QBUF, &buf);
                }
            }
        }
    }
};
