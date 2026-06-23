#pragma once
#include <libusb.h>

/**
 * @brief The official permit required to bypass the operating system and talk to USB ports.
 * @details Under the hood, this uses a C++ concept called RAII to safely manage the `libusb`
 * environment. To a hobbyist, this just means it automatically asks the Raspberry Pi for
 * permission to read the USB ports when created, and absolutely guarantees that permission
 * is safely handed back when destroyed, preventing silent memory leaks.
 */
class UsbContext {
   public:
    /**
     * @brief Ask the operating system for permission to inspect the USB ports.
     * @throws std::runtime_error If the OS refuses to grant USB access.
     */
    UsbContext();

    /**
     * @brief Safely surrender our USB access permit back to the operating system.
     */
    ~UsbContext();

    /**
     * @brief Prevent copying to ensure we don't accidentally create ghost permits.
     */
    UsbContext(const UsbContext&) = delete;

    /**
     * @brief Prevent assignment to enforce strict, unique ownership of the USB environment.
     */
    UsbContext& operator=(const UsbContext&) = delete;

    /**
     * @brief Get the raw permit needed by internal `libusb` functions.
     * @return A pointer to the active environment context.
     */
    libusb_context* get();

   private:
    /**
     * @brief The raw C-pointer to the underlying libusb environment.
     */
    libusb_context* context_{nullptr};
};
