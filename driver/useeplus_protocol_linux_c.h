/* SPDX-License-Identifier: MIT OR GPL-2.0-only */

#ifndef USEEPLUS_PROTOCOL_LINUX_C_H
#define USEEPLUS_PROTOCOL_LINUX_C_H

// NOLINTBEGIN(bugprone-reserved-identifier,cppcoreguidelines-use-enum-class,modernize-use-using,cppcoreguidelines-macro-usage)

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <endian.h>

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#ifndef __packed
#define __packed __attribute__((packed))
#endif

typedef uint8_t	 u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define le16_to_cpu(x) le16toh(x)

/*
 * Useeplus Protocol Structure (OSI Aligned)
 *
 * The hardware operates using a dual-layer encapsulation strategy.
 * A complete Presentation-Layer (Layer 6) MJPEG image is known as a "Video Frame".
 * Because Video Frames are large, they are chunked into smaller "Video Frame Fragments".
 * Each fragment is then encapsulated in a Link-Layer (Layer 2) "USB Frame" for transport.
 *
 * | Byte Offset | Field Name            | Size | OSI Layer | Description                         |
 * |-------------|-----------------------|------|-----------|-------------------------------------|
 * | 0x00        | Start Frame Delimiter | 2    | L2 (USB)  | 0xBBAA (Little-Endian signature)    |
 * | 0x02        | Device ID             | 1    | L2 (USB)  | 0x0B = Video, 0x07 = Gravity Sensor |
 * | 0x03        | Payload Length        | 2    | L2 (USB)  | Total bytes following the USB Header|
 * |-------------|-----------------------|------|-----------|-------------------------------------|
 * | 0x05        | Frame ID              | 1    | L6 (Video)| Rolls on new Video Frame start      |
 * | 0x06        | Device Number         | 1    | L6 (Video)| Secondary internal lens index       |
 * | 0x07        | Flags                 | 1    | L6 (Video)| Bit 0: Gravity, Bit 1: Button       |
 * | 0x08        | IMU Matrix            | 4    | L6 (Video)| 32-bit accelerometer telemetry      |
 * |-------------|-----------------------|------|-----------|-------------------------------------|
 * | 0x0C (12)   | Video Frame Fragment  | Var  | L6 (Video)| MJPEG fragment                      |
 *
 * Video Frame Assembly Rules
 *
 * A single complete Video Frame is reassembled by concatenating the Video Frame Fragments
 * from dozens of smaller USB Frames sharing the same Frame ID.
 *
 * - Start of Video Frame: The fragment payload of the first USB Frame for a given Frame ID
 * will begin with the JPEG SOI Marker (FF D8), usually followed by the APP0/JFIF headers.
 *
 * - Continuation: Subsequent USB Frames for the same Frame ID will contain raw
 * JPEG stream data starting immediately at the Video Data Offset (Byte 0x0C).
 *
 * - End of Video Frame: The final USB Frame for a given Frame ID will contain the JPEG
 * EOI Marker (FF D9) somewhere within its fragment block. Uninitialized padding bytes
 * may exist between the EOI marker and the declared Payload Length.
 *
 * Memory Alignment and Uninitialized Memory
 *
 * 4KB Page Alignment
 *
 * The hardware's internal DMA (Direct Memory Access) buffers are aligned into
 * 4-Kilobyte (4096 bytes) pages. A standard Useeplus Video USB Frame is exactly
 * 944 bytes long (12 bytes of Protocol Overhead + 932 bytes of Video Frame Fragment).
 *
 * The hardware aggressively packs exactly four full USB Frames into a single 4KB
 * page: 4 frames * 944 bytes = 3776 bytes.
 *
 * This packing leaves exactly 320 bytes of unused space at the tail end of
 * every 4KB page (4096 - 3776 = 320).
 *
 * Uninitialized Memory
 *
 * The hardware does not zero out or initialize these 320 bytes before
 * transmitting the USB buffer. The data in the unused memory is arbitrary,
 * often containing valid Start Frame Delimiters from previous or newer USB Frames.
 * There are no checksums built into the protocol for error detection, which poses a
 * challenge when decoding the video stream.
 *
 * 1. Signature Check
 *
 * Every USB Frame evaluation begins by ensuring the current pointer sits exactly
 * on the 0xBBAA Start Frame Delimiter and a valid Device ID (0x0B or 0x07). If this
 * signature fails, the parser enters Seek Mode (see Step 4).
 *
 * 2. Ghost Header Look-Ahead
 *
 * Before the decoder ever reads or trusts the length field of a newly
 * discovered signature, it performs a bounded look-ahead. It scans the next
 * 160 bytes of memory.
 *
 * - If another perfect 0xBBAA signature is found within a short distance, it
 * proves the hardware stuttered or the current header is a ghost remnant.
 * - The decoder treats the current header as a ghost, advances the pointer to
 * the newly discovered real header, skipping the "garbage data".
 *
 * 3. Length Validation
 *
 * If no ghost header is found, the decoder reads the length and sanity-checks it
 * against an upper bound of UP_MAX_VIDEO_FRM_FRAG_LEN (1024 bytes).
 *
 * - If the length exceeds 1024, it means the decoder is looking at garbage data
 * that happens to start with 0xBBAA. The decoder rejects the USB Frame and
 * enters Seek Mode.
 *
 * 4. Seek Mode
 *
 * Whenever the signature fails, or a massive garbage length is detected, the
 * decoder returns an INVALID_PKT state.
 *
 * - Continue the decoding loop, moving ahead by 1 byte. The decoder will
 * continue incrementally until a valid 0xBBAA hardware signature is found, or
 * until it exhausts the data arriving from the FIFO work queue.
 */

struct up_decoder_callbacks {
	void (*on_video_frame_start)(void *context, u8 frame_id, u8 dev_num);
	void (*on_video_frame_fragment)(void *context, u8 *data, size_t len);
	void (*on_video_frame_complete)(void *context);
	void (*on_video_frame_incomplete)(void *context);
};

struct up_decoder {
	struct up_decoder_callbacks cb;
	void			   *context;

	bool building_frame;
	bool eof_reached;
	bool found_soi;
	int  frame_id;
};

enum up_usb_topology {
	UP_IAP_INTERFACE = 0,
	UP_VIDEO_INTERFACE = 1,
	UP_ALT_VIDEO_ENABLE = 1,
	UP_VIDEO_ENDPOINT = 0x01,
	UP_IAP_ENDPOINT = 0x02,
};

enum up_hw_signatures {
	UP_PKT_DEL = 0xBBAA,
	VIDEO_CAMERA_ID = 0x0B,
	GRAVITY_SENSOR_ID = 0x07,
	MAX_DEV_NUM = 1,
};

enum up_resolution_index {
	UP_RES_480P = 1,
	UP_RES_240P = 2,
	UP_RES_720P = 3,
};

enum up_jpeg_marker {
	JPEG_DEL = 0xFF,
	JPEG_SOI = 0xD8,
	JPEG_EOI = 0xD9,
};

enum up_decode_status {
	UP_DECODE_OK,
	UP_INVALID_USB_FRM_HDR,
	UP_INVALID_VIDEO_FRM_FRAG_HDR,
	UP_IS_GHOST_HDR,
	UP_DECODE_SKIP,
	UP_DECODE_NEED_DATA,
};

size_t up_decode_bulk(struct up_decoder *dec, u8 *buffer, size_t len);

struct up_usb_frm_hdr {
	u16 le_delimiter;
	u8  device_id;
	u16 le_length;
} __packed;

struct up_video_frm_frag_hdr {
	u8  frame_id;
	u8  device_number;
	u8  flags;
	u32 le_gravity_sensor;
} __packed;

#define UP_MAX_VIDEO_FRM_FRAG_LEN 1024
#define JPEG_SOI_MAX_POS 256
#define MAX_GHOST_HDR_OFF 160

#define UP_USB_FRM_HDR_LEN (sizeof(struct up_usb_frm_hdr))
#define UP_VIDEO_FRM_FRAG_HDR_LEN (sizeof(struct up_video_frm_frag_hdr))
#define VIDEO_DATA_OFFSET (UP_USB_FRM_HDR_LEN + UP_VIDEO_FRM_FRAG_HDR_LEN)

struct up_decode_context {
	size_t	      index;
	unsigned long flags;

	u8 *vaddr;

	struct up_buffer *active_buf;
	size_t		  active_pl_len;

	size_t decode_buf_len;
	u8    *decode_buf;
};

struct up_decode_state {
	size_t usb_frm_len;
	u8     frame_id;
	u8     dev_num;
	u8     flags;
};

size_t up_decode_bulk(struct up_decoder *dec, u8 *buf, size_t len);

static inline bool up_is_valid_dev_id(u8 dev_id)
{
	return (dev_id == VIDEO_CAMERA_ID || dev_id == GRAVITY_SENSOR_ID);
}

static inline bool up_is_valid_usb_frm_del(u16 delimiter)
{
	return (delimiter == UP_PKT_DEL);
}

static inline u16 up_get_usb_frm_del(const struct up_usb_frm_hdr *hdr)
{
	return le16_to_cpu(hdr->le_delimiter);
}

static inline u16 up_get_usb_frm_pl_len(const struct up_usb_frm_hdr *hdr)
{
	return le16_to_cpu(hdr->le_length);
}

static inline bool up_check_usb_frm_hdr(u16 del, u8 dev_id)
{
	return (up_is_valid_usb_frm_del(del) && up_is_valid_dev_id(dev_id));
}

static inline struct up_usb_frm_hdr *up_get_usb_frm_hdr(u8 *buf, size_t index)
{
	return (struct up_usb_frm_hdr *)(buf + index);
}

static inline struct up_video_frm_frag_hdr *
up_get_video_frm_frag_hdr(u8 *buf, size_t index)
{
	return (struct up_video_frm_frag_hdr *)(buf + index);
}

static inline bool up_is_valid_usb_frm_hdr(struct up_usb_frm_hdr *hdr)
{
	u16 del = up_get_usb_frm_del(hdr);
	u8  dev_id = hdr->device_id;

	return up_check_usb_frm_hdr(del, dev_id);
}

static inline bool up_is_jpg_soi(const u8 *ptr, size_t i)
{
	return (ptr[i] == JPEG_DEL && ptr[i + 1] == JPEG_SOI);
}

static inline bool up_is_jpg_eoi(const u8 *ptr, size_t i)
{
	return (ptr[i] == JPEG_DEL && ptr[i + 1] == JPEG_EOI);
}

static inline bool up_has_gravity_sensor(u8 flags)
{
	return (flags & 0x01) != 0;
}

static inline bool up_is_button_pressed(u8 flags)
{
	return (flags & 0x02) != 0;
}

static inline u8 up_get_other_flags(u8 flags)
{
	return ((flags >> 2) & 0x3F);
}

static inline bool up_has_other_flags(u8 flags)
{
	return up_get_other_flags(flags) != 0;
}

static inline void up_set_has_gravity_sensor(struct up_video_frm_frag_hdr *hdr,
					     bool has_gs)
{
	uint8_t val = hdr->flags;

	if (has_gs)
		val |= 0x01;
	else
		val &= ~0x01;

	hdr->flags = val;
}

static inline void up_set_button_pressed(struct up_video_frm_frag_hdr *hdr,
					 bool			       pressed)
{
	uint8_t val = hdr->flags;

	if (pressed)
		val |= 0x02;
	else
		val &= ~0x02;

	hdr->flags = val;
}

static inline void up_set_other_flags(struct up_video_frm_frag_hdr *hdr,
				      uint8_t			    other)
{
	uint8_t val = hdr->flags;

	val &= 0x03;
	val |= ((other & 0x3F) << 2);
	hdr->flags = val;
}

static inline bool
up_is_valid_video_frm_frag_hdr(const struct up_video_frm_frag_hdr *hdr)
{
	if (!hdr)
		return false;
	if (hdr->device_number > MAX_DEV_NUM)
		return false;
	if (up_has_gravity_sensor(hdr->flags))
		return false;
	if (up_has_other_flags(hdr->flags))
		return false;
	return true;
}

// NOLINTEND(bugprone-reserved-identifier,cppcoreguidelines-use-enum-class,modernize-use-using,cppcoreguidelines-macro-usage)

#endif /* USEEPLUS_PROTOCOL_LINUX_C_H */
