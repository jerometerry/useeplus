/* SPDX-License-Identifier: MIT OR GPL-2.0-only */

#ifndef USEEPLUS_PROTOCOL_MACOS_C_H
#define USEEPLUS_PROTOCOL_MACOS_C_H

// NOLINTBEGIN(bugprone-reserved-identifier,cppcoreguidelines-use-enum-class,modernize-use-using,cppcoreguidelines-macro-usage)

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <libkern/OSByteOrder.h>

#ifndef __packed
#define __packed __attribute__((packed))
#endif

typedef uint8_t	 u8;
typedef uint16_t u16;
typedef uint32_t u32;

#define le16_to_cpu(x) OSSwapLittleToHostInt16(x)

struct up_decoder_callbacks {
	void (*on_video_frame_start)(void *context, u8 frame_id, u8 dev_num);
	void (*on_video_frame_fragment)(void *context, u8 *data, size_t len);
	void (*on_video_frame_complete)(void *context);
	void (*on_video_frame_incomplete)(void *context);
};

struct up_decoder {
	struct up_decoder_callbacks cb;
	void *context;

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

// NOLINTEND(bugprone-reserved-identifier,cppcoreguidelines-use-enum-class,modernize-use-using,cppcoreguidelines-macro-usage)

#endif /* USEEPLUS_PROTOCOL_MACOS_C_H */
