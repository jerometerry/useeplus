#include "usb_device_finder.hpp"

#include <libusb.h>

#include <algorithm>
#include <cstddef>
#include <format>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "constants.hpp"
#include "usb_context.hpp"
#include "usb_device_info.hpp"
#include "usb_device_list.hpp"

std::vector<UsbDeviceInfo> UsbDeviceFinder::all() {
    return find(false);
}

std::vector<UsbDeviceInfo> UsbDeviceFinder::superCameras() {
    return find(true);
}

std::vector<UsbDeviceInfo> UsbDeviceFinder::find(bool onlySuperCameras) {
    std::vector<UsbDeviceInfo> foundDevices;
    UsbContext context;
    UsbDeviceList attachedDevices(context);

    for (libusb_device* device : attachedDevices.get()) {
        struct libusb_device_descriptor desc{};

        if (libusb_get_device_descriptor(device, &desc) < 0) {
            continue;
        }

        bool isSuperCamera =
            std::ranges::any_of(UsbProtocol::VENDOR_PRODUCT_ID_LIST, [&desc](const auto& vp) {
                return desc.idVendor == vp.first && desc.idProduct == vp.second;
            });

        if (onlySuperCameras && !isSuperCamera) {
            continue;
        }

        UsbDeviceInfo info{.bus = libusb_get_bus_number(device),
                           .address = libusb_get_device_address(device),
                           .vendorId = desc.idVendor,
                           .productId = desc.idProduct,
                           .manufacturer = "Unknown",
                           .product = "Unknown",
                           .serialNumber = "Unknown",
                           .isSuperCamera = isSuperCamera};

        libusb_device_handle* handle = nullptr;
        if (libusb_open(device, &handle) == 0) {
            unsigned char strBuf[256];

            if (desc.iManufacturer && libusb_get_string_descriptor_ascii(
                                          handle, desc.iManufacturer, strBuf, sizeof(strBuf)) > 0) {
                info.manufacturer = reinterpret_cast<char*>(strBuf);
            }
            if (desc.iProduct && libusb_get_string_descriptor_ascii(handle, desc.iProduct, strBuf,
                                                                    sizeof(strBuf)) > 0) {
                info.product = reinterpret_cast<char*>(strBuf);
            }
            if (desc.iSerialNumber && libusb_get_string_descriptor_ascii(
                                          handle, desc.iSerialNumber, strBuf, sizeof(strBuf)) > 0) {
                info.serialNumber = reinterpret_cast<char*>(strBuf);
            }

            libusb_close(handle);
        }
        foundDevices.push_back(info);
    }

    return foundDevices;
}

libusb_device_handle* UsbDeviceFinder::open(UsbContext& context, const UsbDeviceInfo& target) {
    UsbDeviceList attachedDevices(context);

    for (libusb_device* device : attachedDevices.get()) {
        if (libusb_get_bus_number(device) == target.bus &&
            libusb_get_device_address(device) == target.address) {
            libusb_device_handle* handle = nullptr;
            if (libusb_open(device, &handle) == 0) {
                return handle;
            }
        }
    }
    return nullptr;
}

std::string UsbDeviceFinder::toJson(const std::vector<UsbDeviceInfo>& devices) {
    std::string json = "[";
    for (size_t i = 0; i < devices.size(); ++i) {
        const auto& dev = devices[i];

        json += std::format(
            R"({{"bus":{},"address":{},"vendorId":{},"productId":{},"manufacturer":"{}","product":"{}","serialNumber":"{}","isSuperCamera":{}}})",
            dev.bus, dev.address, dev.vendorId, dev.productId, dev.manufacturer, dev.product,
            dev.serialNumber, dev.isSuperCamera);

        if (i < devices.size() - 1) {
            json += ",";
        }
    }

    json += "]";
    return json;
}
