// SPDX-License-Identifier: GPL-2.0+ OR MIT

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

#include "useeplus_protocol.h"

struct up_decode_context {
	size_t index;
	unsigned long flags;

	u8 *vaddr;

	struct up_buffer *active_buf;
	size_t active_pl_len;

	size_t decode_buf_len;
	u8 *decode_buf;
};

struct up_decode_state {
	size_t usb_frm_len;
	u8 frame_id;
	u8 dev_num;
	u8 flags;
};

static bool up_check_ghost_hdr(u8 *buf, size_t len, size_t buf_off,
			       size_t *u_hdr_off)
{
	struct up_usb_frm_hdr *u_hdr;
	size_t ghost_lim;
	size_t o;

	if (UP_USB_FRM_HDR_LEN + buf_off > len)
		return false;

	ghost_lim = len - buf_off - UP_USB_FRM_HDR_LEN;
	if (ghost_lim > MAX_GHOST_HDR_OFF)
		ghost_lim = MAX_GHOST_HDR_OFF;

	for (o = UP_USB_FRM_HDR_LEN; o <= ghost_lim; o++) {
		u_hdr = up_get_usb_frm_hdr(buf, buf_off + o);

		if (up_is_valid_usb_frm_hdr(u_hdr)) {
			*u_hdr_off = o;
			return true;
		}
	}

	return false;
}

static enum up_decode_status up_decode(u8 *buf, size_t len, size_t *cur_pos,
				       struct up_decode_state *state)
{
	struct up_video_frm_frag_hdr *v_hdr;
	struct up_usb_frm_hdr *u_hdr;
	u16 u_frm_pl_len;
	size_t u_hdr_off;
	size_t v_hdr_off;
	size_t buf_off;

	u_hdr_off = 0;
	buf_off = *cur_pos;

	if (UP_USB_FRM_HDR_LEN + buf_off > len)
		return UP_DECODE_NEED_DATA;

	u_hdr = up_get_usb_frm_hdr(buf, buf_off);

	if (!up_is_valid_usb_frm_hdr(u_hdr)) {
		(*cur_pos)++;
		return UP_INVALID_USB_FRM_HDR;
	}

	if (up_check_ghost_hdr(buf, len, buf_off, &u_hdr_off)) {
		/*
		 * Hardware packs 4 944 byte packets into 4K pages, leaving the
		 * remaining 320 bytes uninitialized. This uninitialized data
		 * contains remnants of other packets, which we need to filter out.
		 *
		 * We found another valid packet within a short distance from the
		 * previous one. Treat the short packet as a ghost, and skip it.
		 */
		*cur_pos += u_hdr_off;
		return UP_IS_GHOST_HDR;
	}

	/*
	 * A valid payload cannot be larger than the packet size (944 bytes),
	 * less the packet header size (5 bytes) and payload header size (7 bytes).
	 * A payload is at most 932 bytes. For our purposes, using an upper bound of
	 * 1024 is quick sanity check against that we aren't looking at uninitialized
	 * data.
	 */
	u_frm_pl_len = up_get_usb_frm_pl_len(u_hdr);
	if (u_frm_pl_len > UP_MAX_VIDEO_FRM_FRAG_LEN) {
		(*cur_pos)++;
		return UP_INVALID_USB_FRM_HDR;
	}

	state->usb_frm_len = UP_USB_FRM_HDR_LEN + u_frm_pl_len;

	if ((state->usb_frm_len + buf_off) > len)
		return UP_DECODE_NEED_DATA;

	if (u_frm_pl_len < UP_VIDEO_FRM_FRAG_HDR_LEN) {
		*cur_pos += state->usb_frm_len;
		return UP_DECODE_SKIP;
	}

	v_hdr_off = UP_USB_FRM_HDR_LEN + buf_off;
	v_hdr = up_get_video_frm_frag_hdr(buf, v_hdr_off);

	state->frame_id = v_hdr->frame_id;
	state->dev_num = v_hdr->device_number;
	state->flags = v_hdr->flags;

	return UP_DECODE_OK;
}

size_t up_decode_bulk(struct up_decoder *dec, u8 *buf, size_t len)
{
	void (*o_vfs)(void *context, u8 frame_id, u8 dev_num);
	void (*o_vff)(void *context, u8 *data, size_t len);
	struct up_video_frm_frag_hdr *v_hdr;
	void (*o_vfic)(void *context);
	void (*o_vfc)(void *context);
	struct up_decode_state state;
	size_t video_data_start;
	size_t video_data_len;
	u8 *video_data_ptr;
	size_t img_size;
	size_t soi_lim;
	size_t cur_pos;
	size_t v_off;
	u8 frame_id;
	bool found;
	u8 dev_num;
	void *ctx;
	size_t i;

	cur_pos = 0;
	found = false;

	o_vfs = dec->cb.on_video_frame_start;
	o_vff = dec->cb.on_video_frame_fragment;
	o_vfc = dec->cb.on_video_frame_complete;
	o_vfic = dec->cb.on_video_frame_incomplete;

	if (len == 0 || !buf)
		return 0;

	while ((len - cur_pos) >= VIDEO_DATA_OFFSET) {
		switch (up_decode(buf, len, &cur_pos, &state)) {
		case UP_DECODE_NEED_DATA:
			return cur_pos;
		case UP_DECODE_SKIP:
			continue;
		case UP_INVALID_USB_FRM_HDR:
			continue;
		case UP_INVALID_VIDEO_FRM_FRAG_HDR:
			continue;
		case UP_IS_GHOST_HDR:
			continue;
		case UP_DECODE_OK:
			break;
		}

		if (state.dev_num > MAX_DEV_NUM)
			goto advance;

		ctx = dec->context;
		frame_id = state.frame_id;
		dev_num = state.dev_num;

		if (dec->building_frame && dec->frame_id != frame_id) {
			if (!dec->eof_reached && o_vfic)
				o_vfic(dec->context);
		}

		if (!dec->building_frame || dec->frame_id != state.frame_id) {
			if (o_vfs)
				o_vfs(ctx, frame_id, dev_num);

			dec->frame_id = state.frame_id;
			dec->building_frame = true;
			dec->found_soi = false;
			dec->eof_reached = false;
		}

		if (dec->eof_reached)
			goto advance;

		v_off = UP_USB_FRM_HDR_LEN + cur_pos;
		v_hdr = up_get_video_frm_frag_hdr(buf, v_off);

		if (!up_is_valid_video_frm_frag_hdr(v_hdr))
			goto advance;

		video_data_start = VIDEO_DATA_OFFSET + cur_pos;
		video_data_len = state.usb_frm_len - VIDEO_DATA_OFFSET;
		video_data_ptr = buf + video_data_start;

		if (!dec->found_soi) {
			found = false;
			soi_lim = video_data_len;
			if (soi_lim > JPEG_SOI_MAX_POS)
				soi_lim = JPEG_SOI_MAX_POS;

			if (video_data_len >= 2) {
				for (i = 0; i < soi_lim - 1; i++) {
					if (up_is_jpg_soi(video_data_ptr, i)) {
						video_data_ptr += i;
						video_data_len -= i;
						dec->found_soi = true;
						found = true;
						break;
					}
				}
			}

			if (!found)
				goto advance;
		}

		img_size = video_data_len;
		if (video_data_len >= 2) {
			for (i = 0; i < video_data_len - 1; i++) {
				if (up_is_jpg_eoi(video_data_ptr, i)) {
					img_size = i + 2;
					dec->eof_reached = true;
					break;
				}
			}
		}

		if (o_vff)
			o_vff(dec->context, video_data_ptr, img_size);

		;
		if (dec->eof_reached && o_vfc)
			o_vfc(dec->context);

advance:
		cur_pos += state.usb_frm_len;
	}

	return cur_pos;
}
