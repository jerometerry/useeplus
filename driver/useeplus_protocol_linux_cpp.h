/* SPDX-License-Identifier: MIT OR GPL-2.0-only */

#ifndef USEEPLUS_PROTOCOL_LINUX_CPP_H
#define USEEPLUS_PROTOCOL_LINUX_CPP_H

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

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;

inline uint16_t le16_to_cpu(uint16_t x)
{
	return le16toh(x);
}

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

extern "C" {

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

}

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

constexpr int UP_MAX_VIDEO_FRM_FRAG_LEN = 1024;
constexpr int JPEG_SOI_MAX_POS = 256;
constexpr int MAX_GHOST_HDR_OFF = 160;

constexpr size_t UP_USB_FRM_HDR_LEN = sizeof(struct up_usb_frm_hdr);
constexpr size_t UP_VIDEO_FRM_FRAG_HDR_LEN =
	sizeof(struct up_video_frm_frag_hdr);
constexpr size_t VIDEO_DATA_OFFSET =
	UP_USB_FRM_HDR_LEN + UP_VIDEO_FRM_FRAG_HDR_LEN;

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

// NOLINTEND(bugprone-reserved-identifier,cppcoreguidelines-use-enum-class,modernize-use-using,cppcoreguidelines-macro-usage)

#endif /* USEEPLUS_PROTOCOL_LINUX_CPP_H */
