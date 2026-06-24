#pragma once
#include <libusb.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <thread>
#include <vector>

#include "camera_resolution.hpp"
#include "constants.hpp"

class UsbCamera;
struct UsbDeviceInfo;

/**
 * @brief A zero-cost wrapper that manages the libusb asynchronous event loop.
 * @details Isolates the raw libusb C-API from the application logic, handling DMA memory
 * allocation, bulk transfer submissions, and safe teardown mechanics.
 */
class LibusbVideoSource {
   public:
    /**
     * @brief Signature for the callback fired when a USB payload arrives.
     * @return true to automatically re-submit the transfer request, false to halt the loop.
     */
    using TransferHandler = std::function<bool(UsbTransferStatus, std::span<const uint8_t>)>;

    /**
     * @brief Constructs the libusb video source.
     * @param transferHandler The callback to execute when a chunk of data is successfully read.
     * @param running Pointer to the global atomic shutdown flag.
     */
    explicit LibusbVideoSource(TransferHandler transferHandler, std::atomic<bool>* running);

    ~LibusbVideoSource();

    /**
     * @brief Spawns a background worker thread and begins polling the target hardware.
     * @param target The verified USB device descriptor to connect to.
     * @param formatIndex
     */
    void start(const UsbDeviceInfo& target,
               CameraResolution resolution = SupportedResolutions::VGA_480P);

    /**
     * @brief Safely cancels all active USB transfers and joins the worker thread.
     */
    void stop();

   private:
    TransferHandler transferHandler_;
    std::atomic<bool>* running_;
    std::atomic<int> activeTransfers_{0};
    std::unique_ptr<UsbCamera> camera_;
    std::unique_ptr<unsigned char[]> fallbackMemory_;
    std::jthread workerThread_;
    std::vector<libusb_transfer*> transferPool_;
    std::vector<uint8_t> transferMemory_;

    void loop(const UsbDeviceInfo& target, CameraResolution resolution);

    static void LIBUSB_CALL transferCallback(struct libusb_transfer* transfer);
};
