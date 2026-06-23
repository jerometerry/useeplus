#pragma once
#include <libusb.h>

#include <string>
#include <vector>

#include "usb_device_info.hpp"

class UsbContext;

/**
 * @brief The scouting utility that scans the physical USB ports looking for our camera.
 * @details Rather than blindly guessing where the camera is plugged in, this namespace acts
 * as a radar. It pings every single USB port on the machine, checks the ID badge of whatever
 * is plugged in against our list of known endoscopes, and hands back the exact coordinates
 * needed to connect.
 */
namespace UsbDeviceFinder {
/**
 * @brief Scan the motherboard and return the ID badges for absolutely everything plugged in.
 * @details Useful for debugging if you want to see your keyboard, mouse, and camera all at once.
 * @return A list of ID badges for every attached USB device.
 */
std::vector<UsbDeviceInfo> all();

/**
 * @brief Scan the motherboard and return ONLY compatible endoscope cameras.
 * @return A list of ID badges specifically for recognized camera hardware.
 */
std::vector<UsbDeviceInfo> superCameras();

/**
 * @brief The core scanning engine that interrogates the USB ports.
 * @param onlySuperCameras If true, filters out keyboards/mice and only returns valid cameras.
 * @return The filtered list of discovered ID badges.
 */
std::vector<UsbDeviceInfo> find(bool onlySuperCameras);

/**
 * @brief Claim a specific piece of hardware and open a direct communication channel to it.
 * @param context The active USB permit required to talk to the OS.
 * @param target The specific ID badge (Bus and Address) of the camera we want to wake up.
 * @return A raw handle to the opened device, or a null pointer if it was unplugged before we could
 * open it.
 */
libusb_device_handle* open(UsbContext& context, const UsbDeviceInfo& target);

/**
 * @brief Scans the USB bus and builds a JSON array of all connected Useeplus cameras.
 * @return A formatted JSON string ready to be sent over the network.
 */
std::string toJson(const std::vector<UsbDeviceInfo>& devices);
};  // namespace UsbDeviceFinder
