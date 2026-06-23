#pragma once
#include <atomic>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

#include "useeplus_protocol.h"

namespace UsbProtocol {
/**
 * @brief The primary control interface of the USB device.
 */
inline constexpr int IAP_CONTROL_INTERFACE = 0;

/**
 * @brief The secondary data interface of the USB device.
 */
inline constexpr int VIDEO_STREAM_INTERFACE = 1;

/**
 * @brief The alternate setting required to activate the video stream on Interface B.
 */
inline constexpr int ALT_SETTING_VIDEO_ENABLE = 1;

/**
 * @brief The primary USB channel where the heavy video data flows in.
 */
inline constexpr unsigned char VIDEO_ENDPOINT = 1;

/**
 * @brief A secondary USB channel used for camera state or hardware button presses.
 */
inline constexpr unsigned char IAP_ENDPOINT = 2;

/**
 * @brief How long (in ms) we will wait for the USB hardware to respond before assuming it
 * disconnected.
 */
inline constexpr unsigned int HEARTBEAT_SINK_USB_TIMEOUT = 100;

inline constexpr uint8_t IAP_AUTH_HANDSHAKE[] = {0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10};

inline constexpr uint8_t START_VIDEO_COMMAND[] = {0xBB, 0xAA, 5, 0, 0};

/**
 * @brief The universal mathematical signature (Start of Image) that begins every valid JPEG file.
 */
inline constexpr uint8_t JPEG_SOI_MARKERS[] = {0xFF, 0xD8};

/**
 * @brief We will stop searching for the JPEG start signature if we don't find it within the first
 * 32 bytes of a payload.
 */
inline constexpr size_t JPEG_SOI_MARKERS_MAX_POSITION = 256;

inline const size_t MAX_SCAN_LIMIT = 300;

/**
 * @brief The internal hardware lens ID for endoscopes equipped with a physical orientation sensor.
 */
inline constexpr uint8_t GRAVITY_SENSOR_CAMERA_ID = 0x07;

/**
 * @brief The internal hardware lens ID for standard, non-gravity video endoscopes.
 */
inline constexpr uint8_t VIDEO_CAMERA_ID = 0x0B;

/**
 * @brief A registry of all known internal camera lenses this software knows how to decode.
 */
inline constexpr uint8_t VALID_CAMERA_IDS[] = {GRAVITY_SENSOR_CAMERA_ID, VIDEO_CAMERA_ID};

/**
 * @brief The secret 0xBBAA "shipping label" code the hardware uses to tag a valid video chunk on
 * the wire.
 */
inline constexpr uint16_t USB_FRAME_HEADER = 0xBBAA;

inline constexpr uint8_t USB_FRAME_HEADER_A = 0xAA;

inline constexpr uint8_t USB_FRAME_HEADER_B = 0xBB;

inline constexpr uint8_t BOUNDARY_MARKER = 0xFF;

inline constexpr uint8_t START_MARKER = 0xD8;

inline constexpr uint8_t END_MARKER = 0xD9;

/**
 * @brief The official hardware whitelist. The software will only connect to cameras matching these
 * Manufacturer and Model IDs.
 */
inline constexpr std::pair<uint16_t, uint16_t> VENDOR_PRODUCT_ID_LIST[] = {{0x2ce3, 0x3828},
                                                                           {0x0329, 0x2022}};
}  // namespace UsbProtocol

namespace Units {
inline constexpr size_t ONE_KILOBYTE = 1024;
inline constexpr size_t TWO_KILOBYTEs = 2 * ONE_KILOBYTE;
inline constexpr size_t FOUR_KILOBYTES = 4 * ONE_KILOBYTE;
inline constexpr size_t EIGHT_KILOBYTES = 8 * ONE_KILOBYTE;
inline constexpr size_t SIXTEEN_KILOBYTES = 16 * ONE_KILOBYTE;
inline constexpr size_t THIRTY_TWO_KILOBYTES = 32 * ONE_KILOBYTE;
inline constexpr size_t SIXTY_FOUR_KILOBYTES = 64 * ONE_KILOBYTE;
inline constexpr size_t ONE_HUNDRED_TWENTY_EIGHT_KILOBYTES = 128 * ONE_KILOBYTE;
inline constexpr size_t TWO_HUNDRED_FIFTY_SIX_KILOBYTES = 256 * ONE_KILOBYTE;
inline constexpr size_t ONE_MEGABYTE = ONE_KILOBYTE * ONE_KILOBYTE;
inline constexpr size_t TWO_MEGABYTES = 2 * ONE_MEGABYTE;

inline constexpr int TEN_MILLISECONDS = 10000;
inline constexpr int ONE_HUNDRED_MILLISECONDS = 100000;
}  // namespace Units

namespace BufferPoolConfig {
/**
 * @brief The number of Buffer objects pre-allocated when the pool is created.
 */
inline constexpr int INITIAL_POOL_SIZE = 4;

/**
 * @brief The hard limit on how many Buffers can exist. If exceeded, new Buffers fall back to heap
 * allocation.
 */
inline constexpr int MAX_POOL_SIZE = 8;

/**
 * @brief The number of bytes reserved at the front of every Buffer specifically for injecting HTTP
 * chunk headers.
 */
static constexpr size_t BUFFER_PADDING = 128;
}  // namespace BufferPoolConfig

namespace UsbConfig {
/**
 * @brief How long (in ms) we will wait for the USB hardware to respond before assuming it
 * disconnected.
 */
inline constexpr unsigned int USB_TIMEOUT = 1000;

/**
 * @brief The grace period (in ms) allowed for pending asynchronous USB transfers to cleanly cancel
 * before the driver thread forcibly exits during an application shutdown.
 */
inline constexpr unsigned int SHUTDOWN_WAIT_TIMEOUT = 50;

/**
 * @brief The number of asynchronous USB transfer requests kept simultaneously in-flight.
 * @details Maintaining multiple active requests ensures the hardware host controller always has
 * a buffer ready to fill. If this drops to 1, the USB bus will idle between transfers, causing
 * micro-stutters and dropped video frames.
 */
inline constexpr size_t BULK_TRANSFER_COUNT = 4;

/**
 * @brief The payload size of a single USB bulk transfer request.
 * @details 16 KB is a highly optimized chunk size for USB 2.0 High-Speed bulk endpoints.
 * It is large enough to maintain high throughput bandwidth, but small enough to guarantee
 * minimal latency when passing chunks to the UseeplusVideoStream decoder.
 */
inline constexpr size_t BULK_TRANSFER_SIZE = Units::SIXTEEN_KILOBYTES;

/**
 * @brief The total pre-allocated memory footprint locked by the USB ingestion driver.
 * @details By strictly defining this upfront (64 KB total), libusb can perform Direct Memory
 * Access (DMA) routing straight into this user-space buffer array. This guarantees that
 * raw hardware ingestion requires zero heap allocations on the hot path.
 */
inline constexpr size_t DMA_BUFFER_SIZE = BULK_TRANSFER_COUNT * BULK_TRANSFER_SIZE;
}  // namespace UsbConfig

namespace WebServerConfig {
/**
 * @brief The absolute maximum number of web browsers allowed to watch the live feed simultaneously.
 */
inline constexpr size_t MAX_CLIENTS = 16;

/**
 * @brief The size of the fast temporary memory stack used to build network text headers.
 */
inline constexpr size_t HEADER_BUFFER_SIZE = 128;

/**
 * @brief The delay (in ms) before the timer initially fires. 0 means execute immediately on loop
 * start.
 */
inline constexpr int TIMER_FALLTHROUGH = 0;

/**
 * @brief The polling interval (in ms) for the uWebSockets event loop to check the frame queue.
 * @details 15ms safely over-samples a 30 FPS (33ms) camera feed to prevent missed frames.
 */
inline constexpr int TIMER_INTERVAL_MS = 15;

/**
 * @brief The threshold of queued bytes allowed in the OS network socket before the server assumes
 * the viewer is lagging.
 *
 * @details Set to 0 to strictly enforce a "drop-oldest" backpressure policy. This guarantees
 * real-time latency by immediately dropping frames for slow connections rather than queuing them in
 * the heap.
 */
inline const size_t MAX_OUTGOING_CLIENT_BUFFER_SIZE = 0;
}  // namespace WebServerConfig

namespace Arguments {
enum class ParseResult : std::uint8_t { Success, HelpRequested, Error };
}

enum class UsbTransferStatus : std::uint8_t { Completed, Disconnected, Error };

inline constexpr size_t USB_PACKET_HEADER_SIZE = UP_USB_FRM_HDR_LEN;
inline constexpr size_t USB_PAYLOAD_HEADER_SIZE = UP_VIDEO_FRM_FRAG_HDR_LEN;
inline constexpr size_t TOTAL_USB_HEADER_SIZE = VIDEO_DATA_OFFSET;
