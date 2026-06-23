#pragma once
#include <libusb.h>

#include <span>

#include "usb_context.hpp"

/**
 * @brief A safely managed snapshot of every device currently plugged into the computer.
 * @details Asking the operating system for a list of USB devices requires reserving a chunk
 * of memory. If you forget to free that memory, the system eventually crashes.
 *
 * This class acts as an automatic garbage collector for that list. It grabs the "census" of
 * plugged-in devices, lets you read it safely, and guarantees the memory is cleanly deleted
 * as soon as you are done looking at it.
 */
class UsbDeviceList {
   public:
    /**
     * @brief Take a snapshot of all currently connected USB devices.
     * @param context The active USB permit required to ask the OS for this list.
     */
    explicit UsbDeviceList(UsbContext& context)
        : count_(libusb_get_device_list(context.get(), &devices_)) {}

    /**
     * @brief Automatically throw away the snapshot and free the memory.
     */
    ~UsbDeviceList() {
        if (devices_) {
            libusb_free_device_list(devices_, 1);
        }
    }

    /**
     * @brief Prevent copying so we don't accidentally free the same list of devices twice.
     */
    UsbDeviceList(const UsbDeviceList&) = delete;

    /**
     * @brief Prevent assignment to enforce safe memory cleanup.
     */
    UsbDeviceList& operator=(const UsbDeviceList&) = delete;

    /**
     * @brief Look at the census of devices.
     * @return A modern C++ span (a safe viewing window) over the raw list of devices.
     */
    std::span<libusb_device*> get() const {
        if (count_ <= 0 || !devices_) return {};
        return {devices_, static_cast<size_t>(count_)};
    }

   private:
    /**
     * @brief The raw, dangerous C-array of hardware devices provided by the OS.
     */
    libusb_device** devices_{nullptr};

    /**
     * @brief Exactly how many devices were found plugged in.
     */
    ssize_t count_{0};
};
