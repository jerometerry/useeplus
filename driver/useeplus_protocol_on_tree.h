/* SPDX-License-Identifier: MIT OR GPL-2.0-only */
#ifndef USEEPLUS_PROTOCOL_H
#define USEEPLUS_PROTOCOL_H

#include <linux/types.h>
#include <asm/byteorder.h>

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

size_t up_decode_bulk(struct up_decoder *dec, u8 *buf, size_t len);

struct up_usb_frm_hdr {
	__le16 le_delimiter;
	u8 device_id;
	__le16 le_length;
} __packed;

struct up_video_frm_frag_hdr {
	u8 frame_id;
	u8 device_number;
	u8 flags;
	__le32 le_gravity_sensor;
} __packed;

#define UP_MAX_VIDEO_FRM_FRAG_LEN 1024
#define JPEG_SOI_MAX_POS 256
#define MAX_GHOST_HDR_OFF 160

#define UP_USB_FRM_HDR_LEN (sizeof(struct up_usb_frm_hdr))
#define UP_VIDEO_FRM_FRAG_HDR_LEN (sizeof(struct up_video_frm_frag_hdr))
#define VIDEO_DATA_OFFSET (UP_USB_FRM_HDR_LEN + UP_VIDEO_FRM_FRAG_HDR_LEN)

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
	u8 dev_id = hdr->device_id;

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
					 bool pressed)
{
	uint8_t val = hdr->flags;

	if (pressed)
		val |= 0x02;
	else
		val &= ~0x02;

	hdr->flags = val;
}

static inline void up_set_other_flags(struct up_video_frm_frag_hdr *hdr,
				      uint8_t other)
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

#endif /* USEEPLUS_PROTOCOL_H */
