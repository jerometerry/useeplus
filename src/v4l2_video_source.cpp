#include "v4l2_video_source.hpp"

#include <sys/select.h>

#include <iostream>

void V4l2VideoSource::loop(V4l2Device* device) {
    while (running_->load(std::memory_order_relaxed)) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(device->get_fd(), &fds);

        struct timeval tv = {1, 0};

        int r = select(device->get_fd() + 1, &fds, nullptr, nullptr, &tv);

        if (r > 0) {
            V4l2Device::Frame frame;
            if (device->dequeue_frame(frame)) {
                if (handler_) {
                    handler_(frame.payload);
                }

                device->enqueue_buffer(frame.buffer_index);
            }
        } else if (r < 0) {
            std::cerr << "[V4L2 Error] select() polling failed.\n";
            break;
        }
    }
}