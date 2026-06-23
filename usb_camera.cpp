#include "usb_camera.hpp"

#include <libusb.h>
#include <sys/types.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "constants.hpp"
#include "usb_device_info.hpp"

UsbCamera::UsbCamera(const UsbDeviceInfo& target, uint8_t formatIndex) {
    if (libusb_init(&context_) < 0) {
        throw std::runtime_error("libusb_init failed");
    }

    deviceHandle_ = open(context_, target);
    if (!deviceHandle_) {
        throw std::runtime_error("Specified Borescope hardware device not found on USB bus");
    }

    for (int iface : {UsbProtocol::IAP_CONTROL_INTERFACE, UsbProtocol::VIDEO_STREAM_INTERFACE}) {
        if (libusb_kernel_driver_active(deviceHandle_, iface) == 1) {
            libusb_detach_kernel_driver(deviceHandle_, iface);
        }
    }

    if (libusb_claim_interface(deviceHandle_, UsbProtocol::IAP_CONTROL_INTERFACE) < 0 ||
        libusb_claim_interface(deviceHandle_, UsbProtocol::VIDEO_STREAM_INTERFACE) < 0) {
        throw std::runtime_error("Failed to claim USB hardware interfaces");
    }

    // Loop 30 times with a rapid 100ms timeout to clear out pending heartbeat data
    // from the iAP interface before we attempt to stream.
    int heartbeatSinkBytes = 0;
    unsigned char iApHeartbeatSink[512];
    for (int i = 0; i < 30; ++i) {
        libusb_bulk_transfer(deviceHandle_, LIBUSB_ENDPOINT_IN | UsbProtocol::IAP_ENDPOINT,
                             iApHeartbeatSink, sizeof(iApHeartbeatSink), &heartbeatSinkBytes,
                             UsbProtocol::HEARTBEAT_SINK_USB_TIMEOUT);
    }

    if (libusb_set_interface_alt_setting(deviceHandle_, UsbProtocol::VIDEO_STREAM_INTERFACE,
                                         UsbProtocol::ALT_SETTING_VIDEO_ENABLE) < 0) {
        throw std::runtime_error("libusb_set_interface_alt_setting failed");
    }

    libusb_clear_halt(deviceHandle_, LIBUSB_ENDPOINT_IN | UsbProtocol::VIDEO_ENDPOINT);

    uint8_t payload[26] = {0};
    payload[2] = 0x02;          // MJPEG Format Index
    payload[3] = formatIndex;   // Resolution Index (1: 480p, 2: 240p, 3: 720p)
    uint32_t interval = 10000000 / 30; // 30 FPS tick units
    payload[4] = interval & 0xFF;
    payload[5] = (interval >> 8) & 0xFF;
    payload[6] = (interval >> 16) & 0xFF;
    payload[7] = (interval >> 24) & 0xFF;

    int probeRet = libusb_control_transfer(
        deviceHandle_, 0x21, 0x01, 0x0100, UsbProtocol::VIDEO_STREAM_INTERFACE,
        payload, sizeof(payload), UsbConfig::USB_TIMEOUT);
    if (probeRet < 0) {
        throw std::runtime_error("Hardware resolution PROBE negotiation failed.");
    }

    int commitRet = libusb_control_transfer(
        deviceHandle_, 0x21, 0x01, 0x0200, UsbProtocol::VIDEO_STREAM_INTERFACE,
        payload, sizeof(payload), UsbConfig::USB_TIMEOUT);
    if (commitRet < 0) {
        throw std::runtime_error("Hardware resolution COMMIT negotiation failed.");
    }

    int numBytes = 0;
    write(UsbProtocol::IAP_ENDPOINT, UsbProtocol::IAP_AUTH_HANDSHAKE,
          sizeof(UsbProtocol::IAP_AUTH_HANDSHAKE), numBytes);

    write(UsbProtocol::VIDEO_ENDPOINT, UsbProtocol::START_VIDEO_COMMAND,
          sizeof(UsbProtocol::START_VIDEO_COMMAND), numBytes);
}

UsbCamera::~UsbCamera() {
    if (deviceHandle_) {
        libusb_release_interface(deviceHandle_, UsbProtocol::IAP_CONTROL_INTERFACE);
        libusb_release_interface(deviceHandle_, UsbProtocol::VIDEO_STREAM_INTERFACE);

        libusb_attach_kernel_driver(deviceHandle_, UsbProtocol::IAP_CONTROL_INTERFACE);
        libusb_attach_kernel_driver(deviceHandle_, UsbProtocol::VIDEO_STREAM_INTERFACE);

        libusb_close(deviceHandle_);
    }
    if (context_) {
        libusb_exit(context_);
    }
}

libusb_device_handle* UsbCamera::open(libusb_context* context, const UsbDeviceInfo& target) {
    libusb_device** devices = nullptr;
    ssize_t count = libusb_get_device_list(context, &devices);
    if (count < 0) return nullptr;

    libusb_device_handle* handle = nullptr;

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* device = devices[i];
        if (libusb_get_bus_number(device) == target.bus &&
            libusb_get_device_address(device) == target.address) {
            if (libusb_open(device, &handle) == 0) {
                break;
            }
        }
    }

    libusb_free_device_list(devices, 1);
    return handle;
}

std::vector<UsbDeviceInfo> UsbCamera::listCameras() {
    std::vector<UsbDeviceInfo> cameras;
    libusb_context* ctx = nullptr;

    if (libusb_init(&ctx) < 0) {
        return cameras;
    }

    libusb_device** devices = nullptr;
    ssize_t count = libusb_get_device_list(ctx, &devices);
    if (count < 0) {
        libusb_exit(ctx);
        return cameras;
    }

    for (ssize_t i = 0; i < count; ++i) {
        libusb_device* device = devices[i];
        struct libusb_device_descriptor desc{};

        if (libusb_get_device_descriptor(device, &desc) < 0) continue;

        bool isSupported =
            std::ranges::any_of(UsbProtocol::VENDOR_PRODUCT_ID_LIST, [&desc](const auto& vp) {
                return desc.idVendor == vp.first && desc.idProduct == vp.second;
            });

        if (isSupported) {
            UsbDeviceInfo info{.bus = libusb_get_bus_number(device),
                               .address = libusb_get_device_address(device),
                               .vendorId = desc.idVendor,
                               .productId = desc.idProduct,
                               .manufacturer = "Unknown",
                               .product = "Unknown",
                               .serialNumber = "Unknown",
                               .isSuperCamera = true};

            libusb_device_handle* handle = nullptr;
            if (libusb_open(device, &handle) == 0) {
                unsigned char strBuf[256];

                if (desc.iManufacturer &&
                    libusb_get_string_descriptor_ascii(handle, desc.iManufacturer, strBuf,
                                                       sizeof(strBuf)) > 0)
                    info.manufacturer = reinterpret_cast<char*>(strBuf);

                if (desc.iProduct && libusb_get_string_descriptor_ascii(handle, desc.iProduct,
                                                                        strBuf, sizeof(strBuf)) > 0)
                    info.product = reinterpret_cast<char*>(strBuf);

                if (desc.iSerialNumber &&
                    libusb_get_string_descriptor_ascii(handle, desc.iSerialNumber, strBuf,
                                                       sizeof(strBuf)) > 0)
                    info.serialNumber = reinterpret_cast<char*>(strBuf);

                libusb_close(handle);
            }
            cameras.push_back(info);
        }
    }

    libusb_free_device_list(devices, 1);
    libusb_exit(ctx);
    return cameras;
}

[[nodiscard]] libusb_device_handle* UsbCamera::getRawHandle() const {
    return deviceHandle_;
}

[[nodiscard]] libusb_context* UsbCamera::getContext() const {
    return context_;
}

int UsbCamera::read(std::vector<uint8_t>& buffer) {
    int numBytes = 0;
    return read(UsbProtocol::VIDEO_ENDPOINT, buffer, Units::FOUR_KILOBYTES, numBytes);
}

int UsbCamera::read(uint8_t* buffer, size_t maxSize, int& numBytes) {
    return read(UsbProtocol::VIDEO_ENDPOINT, buffer, maxSize, numBytes);
}

int UsbCamera::read(unsigned char endpoint, uint8_t* buffer, size_t maxSize, int& numBytes) {
    return libusb_bulk_transfer(deviceHandle_, LIBUSB_ENDPOINT_IN | endpoint, buffer, maxSize,
                                &numBytes, UsbConfig::USB_TIMEOUT);
}

int UsbCamera::read(unsigned char endpoint, std::vector<uint8_t>& buffer, size_t maxSize,
                    int& numBytes) {
    buffer.resize(maxSize);

    int error = libusb_bulk_transfer(deviceHandle_, LIBUSB_ENDPOINT_IN | endpoint, buffer.data(),
                                     maxSize, &numBytes, UsbConfig::USB_TIMEOUT);

    if (error != 0) {
        return error;
    }

    buffer.resize(numBytes);
    return 0;
}

int UsbCamera::write(unsigned char endpoint, const uint8_t* buffer, size_t length, int& numBytes) {
    return libusb_bulk_transfer(deviceHandle_, LIBUSB_ENDPOINT_OUT | endpoint,
                                const_cast<unsigned char*>(buffer), length, &numBytes,
                                UsbConfig::USB_TIMEOUT);
}
