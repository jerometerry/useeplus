#pragma once
#include <atomic>
#include <cstdint>
#include <string_view>
#include <thread>
#include <vector>

#include "video_frame_buffer.hpp"

// IWYU pragma: begin_exports
struct us_listen_socket_t;
struct us_timer_t;
// IWYU pragma: end_exports

namespace uWS {
template <bool SSL>
struct HttpResponse;
}

/**
 * @brief The server that streams the USB camera video to your web browser or video player.
 * @details MjpegServer bridges the physical USB camera and your network using the highly
 * optimized uWebSockets epoll engine. It takes the raw video pictures coming from the
 * hardware and packages them into an MJPEG stream that any standard web browser can display.
 */
class MjpegServer {
   public:
    /**
     * @brief Initializes the web server configuration.
     * @param port The HTTP port to bind to (e.g., 8080).
     * @param running A reference to the global shutdown flag to monitor for graceful exit.
     * @param frameSource The provider (usually MjpegFrameQueue) that supplies the video frames.
     */
    explicit MjpegServer(int port, const std::atomic<bool>& running, std::string_view index_html,
                         VideoFrameBuffer& disruptor);
    ~MjpegServer();

    MjpegServer(const MjpegServer&) = delete;
    MjpegServer& operator=(const MjpegServer&) = delete;

    /**
     * @brief Blocks the calling thread and begins the uWebSockets epoll event loop.
     */
    void start();

   private:
    struct ViewerState {
        uWS::HttpResponse<false>* res{};
        uint32_t lastSentFrameId{0};
        bool isClosed{false};

        bool isLagging{false};
        uint32_t lagStartFrameId{0};
    };

    struct CorkState {
        uWS::HttpResponse<false>* res{};
        std::string_view payload{};
        bool ok{false};
    };

    const int port_;
    const std::atomic<bool>& running_;
    const std::string_view index_html_;
    VideoFrameBuffer* disruptor_;
    int64_t nextReadSequence_{0};

    std::thread networkThread_;
    us_listen_socket_t* listenSocket_{nullptr};

    std::vector<ViewerState> activeViewers_;

    static void onTimer(us_timer_t* t);
};
