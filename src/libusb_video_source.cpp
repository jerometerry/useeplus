#include "libusb_video_source.hpp"

#include <libusb.h>
#include <sys/time.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "constants.hpp"
#include "usb_camera.hpp"
#include "usb_device_info.hpp"

LibusbVideoSource::LibusbVideoSource(TransferHandler transferHandler, std::atomic<bool>* running)
    : transferHandler_(std::move(transferHandler)), running_(running) {}

LibusbVideoSource::~LibusbVideoSource() {
    stop();
}

void LibusbVideoSource::start(const UsbDeviceInfo& target, CameraResolution resolution) {
    workerThread_ = std::jthread(&LibusbVideoSource::loop, this, target, resolution);
}

void LibusbVideoSource::stop() {
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void LibusbVideoSource::loop(const UsbDeviceInfo& target, CameraResolution resolution) {
    camera_ = std::make_unique<UsbCamera>(target, resolution);

    uint8_t* rawDmaBuffer = reinterpret_cast<uint8_t*>(
        libusb_dev_mem_alloc(camera_->getRawHandle(), UsbConfig::DMA_BUFFER_SIZE));
    bool isDmaAllocated = (rawDmaBuffer != nullptr);

    if (!isDmaAllocated) {
        std::println(std::cout, "[DRIVER INFO] DMA allocation failed. Falling back to fixed heap.");
        fallbackMemory_ = std::make_unique<uint8_t[]>(UsbConfig::DMA_BUFFER_SIZE);
        rawDmaBuffer = fallbackMemory_.get();
    }

    auto dmaGuard = std::unique_ptr<uint8_t, std::function<void(uint8_t*)>>{
        rawDmaBuffer, [this, isDmaAllocated](uint8_t* ptr) {
            if (isDmaAllocated && ptr && camera_) {
                libusb_dev_mem_free(camera_->getRawHandle(), ptr, UsbConfig::DMA_BUFFER_SIZE);
            } else {
                fallbackMemory_.reset();
            }
        }};

    auto transferPoolGuard = std::unique_ptr<std::vector<libusb_transfer*>,
                                             std::function<void(std::vector<libusb_transfer*>*)>>{
            // Halt the hardware FIRST while the OS is still actively draining the USB wire.
            // This prevents the camera's internal memory from overflowing and stalling.
            if (camera_) {
                camera_->haltHardware();
            }

            &transferPool_, [this](std::vector<libusb_transfer*>* pool) {
            for (auto* transfer : *pool) {
                libusb_cancel_transfer(transfer);
            }

            struct timeval shutdownTimeValue = {0, UsbConfig::SHUTDOWN_WAIT_TIMEOUT};
            while (activeTransfers_.load(std::memory_order_acquire) > 0 && camera_) {
                libusb_handle_events_timeout(camera_->getContext(), &shutdownTimeValue);
            }

            for (auto* transfer : *pool) {
                libusb_free_transfer(transfer);
            }
            pool->clear();
        }};

    try {
        transferPool_.reserve(UsbConfig::BULK_TRANSFER_COUNT);

        for (size_t i = 0; i < UsbConfig::BULK_TRANSFER_COUNT; ++i) {
            libusb_transfer* transfer = libusb_alloc_transfer(0);
            if (!transfer) [[unlikely]] {
                throw std::runtime_error("Failed to allocate libusb transfer structure.");
            }

            libusb_fill_bulk_transfer(transfer, camera_->getRawHandle(),
                                      LIBUSB_ENDPOINT_IN | UsbProtocol::VIDEO_STREAM_INTERFACE,
                                      dmaGuard.get() + (i * UsbConfig::BULK_TRANSFER_SIZE),
                                      UsbConfig::BULK_TRANSFER_SIZE, transferCallback, this,
                                      UsbConfig::USB_TIMEOUT);

            int submitResult = libusb_submit_transfer(transfer);
            if (submitResult == LIBUSB_SUCCESS) {
                activeTransfers_.fetch_add(1, std::memory_order_relaxed);
                transferPool_.push_back(transfer);
            } else {
                libusb_free_transfer(transfer);
                throw std::runtime_error(
                    std::format("Failed to submit initial transfer: {}", submitResult));
            }
        }

        struct timeval activeTimeValue = {0, Units::ONE_HUNDRED_MILLISECONDS};
        while (running_->load(std::memory_order_relaxed)) {
            int error = libusb_handle_events_timeout(camera_->getContext(), &activeTimeValue);
            if (error != LIBUSB_SUCCESS && error != LIBUSB_ERROR_INTERRUPTED) {
                std::println(std::cerr, "libusb_handle_events failed. Error: {}", error);
                break;
            }
        }

        for (int i = 0; i < 5; ++i) {
            struct timeval finalFlush = {0, 1000};
            libusb_handle_events_timeout(camera_->getContext(), &finalFlush);
        }

    } catch (const std::exception& e) {
        std::println(std::cerr, "[DRIVER ERROR] Exception caught: {}", e.what());
        if (running_) {
            running_->store(false, std::memory_order_release);
        }
    }
}

void LIBUSB_CALL LibusbVideoSource::transferCallback(struct libusb_transfer* transfer) {
    auto* source = static_cast<LibusbVideoSource*>(transfer->user_data);
    if (!source) {
        [[unlikely]] return;
    }

    const size_t remainingTransfers =
        source->activeTransfers_.fetch_sub(1, std::memory_order_acq_rel) - 1;

    if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
        if (remainingTransfers == 0) {
            source->running_->store(false, std::memory_order_release);
        }
        return;
    }

    UsbTransferStatus status = UsbTransferStatus::Error;
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        status = UsbTransferStatus::Completed;
    } else if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
        status = UsbTransferStatus::Disconnected;
    }

    std::span<const uint8_t> payload;
    if (status == UsbTransferStatus::Completed && transfer->actual_length > 0) {
        payload = std::span<const uint8_t>(transfer->buffer, transfer->actual_length);
    }

    bool shouldResubmit = source->transferHandler_(status, payload);
    if (!shouldResubmit) {
        source->running_->store(false, std::memory_order_release);
    } else if (source->running_->load(std::memory_order_relaxed)) {
        source->activeTransfers_.fetch_add(1, std::memory_order_relaxed);
        if (libusb_submit_transfer(transfer) != LIBUSB_SUCCESS) {
            source->activeTransfers_.fetch_sub(1, std::memory_order_release);
        }
    }
}
