/**
 * @file v4l2_server_main.cpp
 * @brief The main entry point for the live video streaming application.
 * @details This is the primary program you run to actually use your camera.
 * When executed, it automatically finds the camera on the USB bus, launches the
 * video decoding engine, spins up the network broadcaster, and starts serving
 * the live video feed to any web browser that connects to the Raspberry Pi's IP address.
 *
 * By default, it broadcasts on port 8080, but you can override this by passing
 * a different port number when you launch the program from the terminal.
 */

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
#include "index_html.hpp"
#include "mjpeg_server.hpp"
#include "v4l2_device.hpp"
#include "v4l2_video_source.hpp"
#include "video_frame_buffer.hpp"

namespace {
constexpr int DEFAULT_PORT = 8080;
constexpr std::string DEFAULT_DEVICE_PATH = "/dev/video0";

std::atomic<bool> running{true};
int64_t currentClaimSequence_{-1};
}  // namespace

void signalHandler(int) {
    running.store(false, std::memory_order_release);
}

int main(int argc, const char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        int port = DEFAULT_PORT;
        std::string devicePath = DEFAULT_DEVICE_PATH;
        CameraResolution targetResolution = SupportedResolutions::VGA_480P;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "-h" || arg == "--help") {
                std::print(
                    "Usage: {} [options]\n\n"
                    "Options:\n"
                    "  -h, --help    Show this help message and exit\n"
                    "  --dev <path>  Specify the video device path (default: {})\n"
                    "  --port <num>  Specify the server port number (default: {})\n"
                    "  --res <val>   Specify the resolution: 720p, 480p, 240p (default: 480p)\n",
                    argv[0], DEFAULT_DEVICE_PATH, DEFAULT_PORT);
                return 0;
            } else if (arg == "--dev" && i + 1 < argc) {
                devicePath = argv[++i];
            } else if (arg == "--port" && i + 1 < argc) {
                unsigned long val = std::stoul(argv[++i]);
                if (val > 65535) {
                    throw std::out_of_range(std::format("Invalid port: {}", val));
                }
                port = static_cast<int>(val);
            } else if ((arg == "--res" || arg == "--resolution") && i + 1 < argc) {
                std::string resStr = argv[++i];
                if (resStr == "720p" || resStr == "1280x720") {
                    targetResolution = SupportedResolutions::HD_720P;
                } else if (resStr == "480p" || resStr == "640x480") {
                    targetResolution = SupportedResolutions::VGA_480P;
                } else if (resStr == "240p" || resStr == "320x240") {
                    targetResolution = SupportedResolutions::LOW_240P;
                } else {
                    throw std::invalid_argument(std::format(
                        "Invalid resolution: {}. Supported: 720p, 480p, 240p.", resStr));
                }
            } else {
                throw std::invalid_argument(
                    std::format("Unknown argument: {}\nUse --help for usage details.", arg));
            }
        }

        VideoFrameBuffer ringBuffer;
        ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

        V4l2VideoSource::FrameHandler handler = [&ringBuffer](std::span<const uint8_t> payload) -> bool {
            if (!running.load(std::memory_order_relaxed)) {
                return false;
            }
            if (!payload.empty()) {
                currentClaimSequence_ = ringBuffer.claim();
                VideoFrameFragment& slot = ringBuffer.getBySequence(currentClaimSequence_);
                slot.clear();
                slot.insertContent(payload);
                ringBuffer.publish(currentClaimSequence_);
            }
            return true;
        };

        V4l2Device v4l2_device(devicePath, targetResolution);

        V4l2VideoSource videoSource(handler, &running);

        CameraResolution activeRes = v4l2_device.get_resolution();
        std::cout << std::format("[Server Core] Camera hardware initialized at {}x{}\n",
                                 activeRes.width, activeRes.height);

        MjpegServer server(port, running, Resources::index_html, ringBuffer);

        std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";

        videoSource.start(v4l2_device);
        server.start();

        std::cout << "[Server Core] System fully operational. Awaiting network events.\n";

        while (running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "[Server Core] Shutdown signal received. Stopping worker lanes...\n";

        videoSource.stop();

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception in application core: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "[System Termination] All resources returned. Server exited cleanly.\n";
    return EXIT_SUCCESS;
}
