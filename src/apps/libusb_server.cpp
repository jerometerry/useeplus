/**
 * @file libusb_server_main.cpp
 * @brief The main entry point for the live video streaming application.
 * @details This is the primary program you run to actually use your camera.
 * When executed, it automatically finds the camera on the USB bus, launches the
 * video decoding engine, spins up the network broadcaster, and starts serving
 * the live video feed to any web browser that connects to the Raspberry Pi's IP address.
 *
 * By default, it broadcasts on port 8080, but you can override this by passing
 * a different port number when you launch the program from the terminal.
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "constants.hpp"
#include "index_html.hpp"
#include "libusb_video_source.hpp"
#include "mjpeg_server.hpp"
#include "usb_camera.hpp"
#include "usb_device_info.hpp"
#include "useeplus_video_stream.hpp"
#include "video_frame_buffer.hpp"

namespace {
constexpr int DEFAULT_PORT = 8080;
std::atomic<bool> running{true};
}  // namespace

void signalHandler(int) {
    running.store(false, std::memory_order_release);
}

int main(int argc, const char* argv[]) {
    int port = DEFAULT_PORT;
    CameraResolution resolution = SupportedResolutions::VGA_480P;
    std::string resLabel = "480p";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            std::cout
                << "Usage: " << argv[0] << " [options]\n\n"
                << "Options:\n"
                << "  -h, --help    Show this help message and exit\n"
                << "  --port <num>  Specify the server port number (default: " << DEFAULT_PORT
                << ")\n"
                << "  --res <val>   Specify the resolution: 720p, 480p, 240p (default: 480p)\n";
            return EXIT_SUCCESS;
        } else if (arg == "--port" && i + 1 < argc) {
            unsigned long val = std::stoul(argv[++i]);
            if (val > 65535) throw std::out_of_range("Invalid port");
            port = static_cast<int>(val);
        } else if ((arg == "--res" || arg == "--resolution") && i + 1 < argc) {
            std::string resStr = argv[++i];
            if (resStr == "720p" || resStr == "1280x720") {
                resolution = SupportedResolutions::HD_720P;
                resLabel = "720p";
            } else if (resStr == "480p" || resStr == "640x480") {
                resolution = SupportedResolutions::VGA_480P;
                resLabel = "480p";
            } else if (resStr == "240p" || resStr == "320x240") {
                resolution = SupportedResolutions::LOW_240P;
                resLabel = "240p";
            } else {
                std::cerr << "Invalid resolution. Supported: 720p, 480p, 240p.\n";
                return EXIT_FAILURE;
            }
        }
    }

    std::cout << "==================================================================\n";
    std::cout << "  Pi-Borescope Streamer Started (User-Space libusb)\n";
    std::cout << "  -> Status:     Running on port " << port << "\n";
    std::cout << "  -> Resolution: " << resLabel << " \n";
    std::cout << "==================================================================\n";

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    std::signal(SIGPIPE, SIG_IGN);

    try {
        std::vector<UsbDeviceInfo> cameras = UsbCamera::listCameras();
        if (cameras.empty()) {
            std::cerr << "[Fatal] No Useeplus supercamera devices found on the USB bus.\n";
            return EXIT_FAILURE;
        }

        UsbDeviceInfo camera = cameras.front();

        if (cameras.size() > 1) {
            std::cout << "\nMultiple cameras detected:\n";
            for (size_t i = 0; i < cameras.size(); ++i) {
                std::cout << "  [" << i << "] Bus " << static_cast<int>(cameras[i].bus)
                          << " Address " << static_cast<int>(cameras[i].address) << " - "
                          << cameras[i].manufacturer << " " << cameras[i].product << " (Serial: "
                          << (cameras[i].serialNumber.empty() ? "N/A" : cameras[i].serialNumber)
                          << ")\n";
            }

            size_t choice = 0;
            while (true) {
                std::cout << "\nSelect camera to stream [0-" << (cameras.size() - 1) << "]: ";
                if (std::cin >> choice && choice < cameras.size()) {
                    camera = cameras[choice];
                    break;
                }
                std::cout << "Invalid selection. Please try again.\n";
                std::cin.clear();
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }

        std::cout << "\n[Info] Binding stream to camera on Bus " << static_cast<int>(camera.bus)
                  << " Address " << static_cast<int>(camera.address) << "...\n";

        VideoFrameBuffer ringBuffer;
        ringBuffer.preAllocate(Units::ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES);

        UseeplusVideoStream stream(ringBuffer);

        auto transfer = [&stream](UsbTransferStatus status,
                                  std::span<const uint8_t> payload) -> bool {
            if (!running.load(std::memory_order_relaxed)) {
                return false;
            }
            if (status == UsbTransferStatus::Completed) {
                if (!payload.empty()) {
                    stream.send(payload);
                }
                return true;
            }
            return status != UsbTransferStatus::Disconnected;
        };
        LibusbVideoSource source(transfer, &running);

        MjpegServer server(port, running, Resources::index_html, ringBuffer);

        std::cout << "[Server Core] Starting asynchronous capture and network worker engines...\n";

        source.start(camera, resolution);
        server.start();

        std::cout << "[Server Core] System fully operational. Awaiting network events.\n";

        while (running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "[Server Core] Shutdown signal received. Stopping worker lanes...\n";

        source.stop();

    } catch (const std::exception& e) {
        std::cerr << "[Fatal] Unhandled exception in application core: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    std::cout << "[System Termination] All resources returned. Server exited cleanly.\n";
    return EXIT_SUCCESS;
}
