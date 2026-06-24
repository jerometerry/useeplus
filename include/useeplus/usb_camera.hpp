#pragma once
#include <libusb.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "camera_resolution.hpp"
#include "usb_device_info.hpp"

/**
 * @brief The direct hardware connection to the physical USB endoscope.
 * @details UsbCamera acts as the physical translator between our software and the proprietary
 * camera plugged into the USB port. Because this camera doesn't act like a standard webcam,
 * standard video software can't read it. This class uses `libusb` to bypass the operating system
 * and talk to the hardware directly.
 *
 * It handles all the messy USB setup: taking control of the device from the Raspberry Pi's OS,
 * sending the secret "handshake" codes to wake the camera up, and reading the raw video bytes
 * pouring over the physical cable.
 */
class UsbCamera {
   public:
    /**
     * @brief Connect to the camera and turn on the video feed.
     * @details Finds the specific camera on the USB bus, asks the operating system to step aside,
     * claims the necessary USB communication channels, and sends the exact sequence of bytes
     * required to wake the camera up and command it to start streaming.
     * @param target The specific camera hardware to connect to (usually found via listAvailable).
     * @param resolution
     * @throws std::runtime_error If the camera isn't plugged in, or if the OS refuses to let go of
     * it.
     */
    UsbCamera(const UsbDeviceInfo& target,
              CameraResolution resolution = SupportedResolutions::VGA_480P);

    /**
     * @brief Safely power down the connection and give the camera back to the OS.
     * @details Tells the camera to stop sending data, releases our exclusive lock on the USB
     * ports, and re-attaches the default operating system drivers so the Raspberry Pi knows
     * the device is still there.
     */
    ~UsbCamera();

    /**
     * @brief Prevent copying to ensure we don't accidentally cross wires.
     * @details You cannot have two active connections to the exact same physical camera.
     */
    UsbCamera(const UsbCamera&) = delete;

    /**
     * @brief Prevent assignment copying to enforce unique ownership of the USB connection.
     */
    UsbCamera& operator=(const UsbCamera&) = delete;

    /**
     * @brief Prevent moving to keep the underlying libusb pointers stable.
     */
    UsbCamera(UsbCamera&&) = delete;

    /**
     * @brief Prevent move assignment to keep the underlying libusb pointers stable.
     */
    UsbCamera& operator=(UsbCamera&&) = delete;

    /**
     * @brief Find the physical camera hardware on the computer's USB ports.
     * @param context The active libusb environment.
     * @param target The Vendor ID and Product ID we are looking for.
     * @return A raw handle to the USB device if found, or a null pointer if it's not plugged in.
     */
    static libusb_device_handle* open(libusb_context* context, const UsbDeviceInfo& target);

    /**
     * @brief List all available USB cameras
     * @return A vector of available USB camera devices
     */
    static std::vector<UsbDeviceInfo> listCameras();

    /**
     * @brief Scan the computer for any plugged-in borescopes.
     * @details Useful for detecting if the camera is actually plugged in and recognized
     * by the hardware before the program tries to start the video stream.
     * @param context The active libusb environment.
     * @return A list of all connected devices that match the borescope's hardware ID.
     */
    static std::vector<UsbDeviceInfo> listAvailable(libusb_context* context);

    /**
     * @brief Get the Raw Handle object
     *
     * @return libusb_device_handle*
     */
    [[nodiscard]] libusb_device_handle* getRawHandle() const;

    /**
     * @brief Get the Context object
     *
     * @return libusb_context*
     */
    [[nodiscard]] libusb_context* getContext() const;

    /**
     * @brief Pull the latest chunk of raw data from the main USB cable into a dynamic buffer.
     * @details Listens on the camera's main data channel (Endpoint 1) and waits to receive
     * up to 4 Kilobytes of video data. The buffer will automatically resize to fit exactly
     * what was received.
     * @param buffer The dynamic list of bytes where the camera data will be stored.
     * @return 0 on success, or a negative libusb error code if the wire disconnected.
     */
    int read(std::vector<uint8_t>& buffer);

    /**
     * @brief Pull the latest chunk of raw data from the main USB cable into a fixed memory block.
     * @details A faster read method that places the video data directly into a pre-allocated
     * array without resizing anything.
     * @param buffer The raw memory address where the data should be written.
     * @param maxSize The absolute maximum number of bytes that can fit in the buffer safely.
     * @param numBytes Tracks exactly how many bytes the camera actually sent us.
     * @return 0 on success, or a negative libusb error code if something went wrong.
     */
    int read(uint8_t* buffer, size_t maxSize, int& numBytes);

    /**
     * @brief Read data from a specific USB channel on the camera.
     * @details An advanced function used to listen to different parts of the camera.
     * For example, the video feed might come down one endpoint, while the physical snapshot
     * button on the camera's handle might send its clicks down a different endpoint.
     * @param endpoint The specific USB channel to listen to (e.g., 1 or 2).
     * @param buffer The raw memory address to store the incoming data.
     * @param maxSize The maximum number of bytes to accept.
     * @param numBytes Tracks exactly how many bytes were received.
     * @return 0 on success, or a negative libusb error code.
     */
    int read(unsigned char endpoint, uint8_t* buffer, size_t maxSize, int& numBytes);

    /**
     * @brief Pull data from a specific USB channel into a dynamic buffer.
     * @param endpoint The specific USB channel to listen to.
     * @param buffer The dynamic list of bytes where the data will be stored.
     * @param maxSize The maximum number of bytes we are willing to accept.
     * @param numBytes Tracks exactly how many bytes were received.
     * @return 0 on success, or a negative libusb error code.
     */
    int read(unsigned char endpoint, std::vector<uint8_t>& buffer, size_t maxSize, int& numBytes);

    /**
     * @brief Send commands down the USB cable to the camera.
     * @details Used exclusively during startup to send the "secret handshake" tokens
     * that command the camera hardware to wake up and start streaming its video feed.
     * @param endpoint The specific USB channel to send the command to.
     * @param buffer The sequence of bytes to send.
     * @param length The total number of bytes we are sending.
     * @param numBytes Tracks exactly how many bytes successfully made it across the wire.
     * @return 0 on success, or a negative libusb error code if the write failed.
     */
    int write(unsigned char endpoint, const uint8_t* buffer, size_t length, int& numBytes);

   private:
    /**
     * @brief The global libusb environment our connection lives inside.
     */
    libusb_context* context_ = nullptr;

    /**
     * @brief The direct handle to the physical hardware device.
     */
    libusb_device_handle* deviceHandle_ = nullptr;
};
