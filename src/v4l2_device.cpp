#include "v4l2_device.hpp"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>
#include <iostream>

V4l2Device::V4l2Device(const std::string& device_path, CameraResolution target_resolution)
    : active_resolution_(target_resolution) {

    fd_ = open(device_path.c_str(), O_RDWR | O_NONBLOCK, 0);
    if (fd_ < 0) throw std::runtime_error("Cannot open " + device_path);

    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.width = target_resolution.width;
    fmt.fmt.pix.height = target_resolution.height;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) throw std::runtime_error("Failed to set video format");
    if (ioctl(fd_, VIDIOC_G_FMT, &fmt) < 0) throw std::runtime_error("Failed to verify format");

    if (fmt.fmt.pix.width != target_resolution.width || fmt.fmt.pix.height != target_resolution.height) {
        std::cerr << "Warning: Driver fell back to " << fmt.fmt.pix.width << "x" << fmt.fmt.pix.height << "\n";
        active_resolution_.width = fmt.fmt.pix.width;
        active_resolution_.height = fmt.fmt.pix.height;
    }

    struct v4l2_requestbuffers req = {};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) throw std::runtime_error("Failed to request buffers");

    buffers_.resize(req.count);
    for (unsigned int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ioctl(fd_, VIDIOC_QUERYBUF, &buf);

        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, buf.m.offset);
        ioctl(fd_, VIDIOC_QBUF, &buf);
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) throw std::runtime_error("Failed to start streaming");
}

V4l2Device::~V4l2Device() {
    if (fd_ >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(fd_, VIDIOC_STREAMOFF, &type);
        for (auto& buf : buffers_) {
            munmap(buf.start, buf.length);
        }
        close(fd_);
    }
}

CameraResolution V4l2Device::get_resolution() const { return active_resolution_; }
int V4l2Device::get_fd() const { return fd_; }

bool V4l2Device::dequeue_frame(Frame& out_frame) {
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_DQBUF, &buf) == 0) {
        if (!(buf.flags & V4L2_BUF_FLAG_ERROR)) {
            uint8_t* payload_start = static_cast<uint8_t*>(buffers_[buf.index].start);
            out_frame.payload = std::span<const uint8_t>(payload_start, buf.bytesused);
            out_frame.buffer_index = buf.index;
            return true;
        }
        // If error flag was set, immediately requeue it and pretend we didn't read it
        enqueue_buffer(buf.index);
    }
    return false;
}

void V4l2Device::enqueue_buffer(uint32_t buffer_index) {
    struct v4l2_buffer buf = {};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = buffer_index;
    ioctl(fd_, VIDIOC_QBUF, &buf);
}
