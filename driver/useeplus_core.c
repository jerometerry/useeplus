// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include "useeplus_core.h"
#include "useeplus_protocol.h"
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/unaligned.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>
#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>

static int up_g_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(&sp->parm.capture, 0, sizeof(sp->parm.capture));

	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.capture.timeperframe.numerator = 1;
	sp->parm.capture.timeperframe.denominator = 30;

	sp->parm.capture.readbuffers = 1;

	return 0;
}

static int up_s_parm(struct file *file, void *priv, struct v4l2_streamparm *sp)
{
	if (sp->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	memset(&sp->parm.capture, 0, sizeof(sp->parm.capture));

	sp->parm.capture.capability = V4L2_CAP_TIMEPERFRAME;
	sp->parm.capture.timeperframe.numerator = 1;
	sp->parm.capture.timeperframe.denominator = 30;

	sp->parm.capture.readbuffers = 1;

	return 0;
}

static int up_s_input(struct file *file, void *priv, unsigned int i)
{
	return i == 0 ? 0 : -EINVAL;
}

static int up_g_input(struct file *file, void *priv, unsigned int *i)
{
	*i = 0;
	return 0;
}

static int up_enum_input(struct file *file, void *priv, struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;

	inp->type = V4L2_INPUT_TYPE_CAMERA;
	strscpy(inp->name, V4L2_INPUT_NAME, sizeof(inp->name));

	return 0;
}

static const struct v4l2_frmsize_discrete up_sizes[] = {
	{ 640, 480 },
	{ 320, 240 },
	{ 1280, 720 },
};

static int up_enum_frameintervals(struct file *file, void *priv,
				  struct v4l2_frmivalenum *fival)
{
	bool size_supported = false;
	unsigned int i;

	if (fival->index > 0)
		return -EINVAL;

	if (fival->pixel_format != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(up_sizes); i++) {
		if (fival->width == up_sizes[i].width &&
		    fival->height == up_sizes[i].height) {
			size_supported = true;
			break;
		}
	}

	if (!size_supported)
		return -EINVAL;

	fival->type = V4L2_FRMIVAL_TYPE_DISCRETE;
	fival->discrete.numerator = 1;
	fival->discrete.denominator = 30;

	return 0;
}

static int up_enum_framesizes(struct file *file, void *priv,
			      struct v4l2_frmsizeenum *fsize)
{
	if (fsize->pixel_format != V4L2_PIX_FMT_MJPEG)
		return -EINVAL;

	if (fsize->index >= ARRAY_SIZE(up_sizes))
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_DISCRETE;
	fsize->discrete = up_sizes[fsize->index];

	return 0;
}

static int up_enum_fmt_vid_cap(struct file *file, void *priv,
			       struct v4l2_fmtdesc *f)
{
	if (f->index > 0)
		return -EINVAL;

	f->pixelformat = V4L2_PIX_FMT_MJPEG;

	return 0;
}

static void up_enforce_format(struct up_drv_data *drv_data,
			      struct v4l2_format *f)
{
	f->fmt.pix.width = drv_data->v4l2.width;
	f->fmt.pix.height = drv_data->v4l2.height;
	f->fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.bytesperline = 0;
	f->fmt.pix.sizeimage = MAX_FRAME_SIZE;
	f->fmt.pix.colorspace = V4L2_COLORSPACE_SRGB;
}

static int up_try_fmt_vid_cap(struct file *file, void *priv,
			      struct v4l2_format *f)
{
	struct up_drv_data *drv_data = video_drvdata(file);

	up_enforce_format(drv_data, f);

	return 0;
}

static int up_set_hardware_resolution(struct up_drv_data *drv_data,
				      u8 frame_index, u32 target_fps)
{
	struct usb_device *u_dev = drv_data->usb.udev;
	int pipe_out = usb_sndctrlpipe(u_dev, 0);
	u32 frame_interval;
	int retval;
	u8 *buf;

	buf = kzalloc(26, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Calculate UVC frame interval units: 10,000,000 / FPS */
	if (target_fps == 0)
		target_fps = 30; /* Fallback baseline protection */
	frame_interval = 10000000 / target_fps;

	buf[0] = 0x01; /* bmHint Low Byte (flags frame interval selection) */
	buf[1] = 0x00;
	buf[2] = 0x02; /* bFormatIndex (Fixed to 2 for MJPEG container) */
	buf[3] = frame_index;

	buf[4] = (frame_interval & 0xFF);
	buf[5] = ((frame_interval >> 8) & 0xFF);
	buf[6] = ((frame_interval >> 16) & 0xFF);
	buf[7] = ((frame_interval >> 24) & 0xFF);

	dev_info(&u_dev->dev,
		 "Negotiating camera pipeline via Interface %d (Mode %d)...\n",
		 UP_VIDEO_INTERFACE, frame_index);

	retval = usb_control_msg(u_dev, pipe_out, 0x01, 0x21, 0x0100,
				 UP_VIDEO_INTERFACE, buf, 26, USB_CTRL_SET_TO);
	if (retval < 0) {
		dev_err(&u_dev->dev, "Hardware stream probe stalled: %d\n",
			retval);
		goto out;
	}

	retval = usb_control_msg(u_dev, pipe_out, 0x01, 0x21, 0x0200,
				 UP_VIDEO_INTERFACE, buf, 26, USB_CTRL_SET_TO);
	if (retval < 0) {
		dev_err(&u_dev->dev,
			"Hardware stream commit lock stalled: %d\n", retval);
		goto out;
	}

	retval = 0;

out:
	kfree(buf);
	return retval;
}

static int up_s_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct up_drv_data *drv_data = video_drvdata(file);
	u8 target_hardware_index;

	if (f->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (vb2_is_busy(&drv_data->v4l2.queue))
		return -EBUSY;

	if (f->fmt.pix.width >= 1280 || f->fmt.pix.height >= 720) {
		target_hardware_index = UP_RES_720P;
		drv_data->v4l2.width = 1280;
		drv_data->v4l2.height = 720;
	} else if (f->fmt.pix.width <= 320 || f->fmt.pix.height <= 240) {
		target_hardware_index = UP_RES_240P;
		drv_data->v4l2.width = 320;
		drv_data->v4l2.height = 240;
	} else {
		target_hardware_index = UP_RES_480P;
		drv_data->v4l2.width = 640;
		drv_data->v4l2.height = 480;
	}

	up_enforce_format(drv_data, f);

	drv_data->v4l2.current_hw_index = target_hardware_index;

	dev_info(&drv_data->usb.udev->dev,
		 "Applying resolution payload index %d (%dx%d) to camera...\n",
		 target_hardware_index, drv_data->v4l2.width,
		 drv_data->v4l2.height);

	return up_set_hardware_resolution(drv_data, target_hardware_index, 30);
}

static int up_g_fmt_vid_cap(struct file *file, void *priv,
			    struct v4l2_format *f)
{
	struct up_drv_data *drv_data = video_drvdata(file);

	up_enforce_format(drv_data, f);

	return 0;
}

static int up_vidioc_querycap(struct file *file, void *priv,
			      struct v4l2_capability *cap)
{
	struct up_drv_data *drv_data = video_drvdata(file);

	strscpy(cap->driver, CAP_DRIVER, sizeof(cap->driver));
	strscpy(cap->card, CAP_CARD, sizeof(cap->card));
	usb_make_path(drv_data->usb.udev, cap->bus_info, sizeof(cap->bus_info));

	cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			    V4L2_CAP_READWRITE | V4L2_CAP_DEVICE_CAPS;

	cap->device_caps = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING |
			   V4L2_CAP_READWRITE;

	return 0;
}

static const struct v4l2_ioctl_ops up_v4l2_ioctl_ops = {
	.vidioc_s_parm = up_s_parm,
	.vidioc_g_parm = up_g_parm,
	.vidioc_s_input = up_s_input,
	.vidioc_g_input = up_g_input,
	.vidioc_enum_input = up_enum_input,
	.vidioc_enum_frameintervals = up_enum_frameintervals,
	.vidioc_enum_framesizes = up_enum_framesizes,
	.vidioc_enum_fmt_vid_cap = up_enum_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap = up_try_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap = up_s_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap = up_g_fmt_vid_cap,
	.vidioc_querycap = up_vidioc_querycap,
	.vidioc_streamoff = vb2_ioctl_streamoff,
	.vidioc_streamon = vb2_ioctl_streamon,
	.vidioc_querybuf = vb2_ioctl_querybuf,
	.vidioc_qbuf = vb2_ioctl_qbuf,
	.vidioc_reqbufs = vb2_ioctl_reqbufs,
	.vidioc_dqbuf = vb2_ioctl_dqbuf,
	.vidioc_create_bufs = vb2_ioctl_create_bufs,
};

static void up_stop_streaming(struct vb2_queue *vq)
{
	struct vb2_v4l2_buffer *v4l2_buf;
	struct up_drv_data *drv_data;
	struct up_buffer *active_buf;
	struct vb2_buffer *vb2_buf;
	struct up_buffer *buf;
	unsigned long flags;
	int i;

	drv_data = vb2_get_drv_priv(vq);

	/*
	 * Signal the callback and hardware state to STOP processing immediately.
	 */
	clear_bit(STREAM_CLIENT_READY, &drv_data->pipeline.streaming);
	clear_bit(STREAM_HW_ACTIVE, &drv_data->pipeline.streaming);

	/*
	 * Ensure all CPU cores see the bit changes before we start Freeing URBs.
	 */
	smp_mb__after_atomic();

	/*
	 * usb_kill_urb blocks until any active callback finishes executing.
	 */
	for (i = 0; i < NUM_URBS; i++) {
		if (drv_data->usb.urbs[i])
			usb_kill_urb(drv_data->usb.urbs[i]);
	}

	/* Stop the decoder worker completely */
	cancel_work_sync(&drv_data->decoder.work);

	/*
	 * Flush any stale byte data left in the FIFO
	 * to prevent corrupting the stream when user space restarts it.
	 */
	kfifo_reset(&drv_data->decoder.fifo);

	/* Reset decoder parsing state states */
	drv_data->decoder.building_frame = false;
	drv_data->decoder.found_soi = false;
	drv_data->decoder.eof_reached = false;

	/* Clean up the active working buffer */
	active_buf = drv_data->decoder.active_buf;
	if (active_buf) {
		v4l2_buf = &active_buf->vb2_buffer;
		vb2_buf = &v4l2_buf->vb2_buf;

		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
		drv_data->decoder.active_buf = NULL;
		drv_data->decoder.active_pl_len = 0;
	}

	/*
	 * Safely drain any buffers that were left over in the queue.
	 */
	spin_lock_irqsave(&drv_data->pipeline.ready_lock, flags);
	while (!list_empty(&drv_data->pipeline.ready_queue)) {
		buf = list_first_entry(&drv_data->pipeline.ready_queue,
				       struct up_buffer, list);
		list_del(&buf->list);
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_ERROR);
	}
	spin_unlock_irqrestore(&drv_data->pipeline.ready_lock, flags);
}

static int up_write_msg(struct up_drv_data *data, u8 ep_addr, const u8 *tokens,
			size_t len)
{
	struct usb_device *u_dev;
	int sent_bytes;
	int out_pipe;
	int retval;
	u8 *buf;

	u_dev = data->usb.udev;
	buf = kmemdup(tokens, len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	out_pipe = usb_sndbulkpipe(u_dev, ep_addr);
	retval = usb_bulk_msg(u_dev, out_pipe, buf, len, &sent_bytes, USB_TO);

	kfree(buf);
	return retval;
}

static const u8 iap_auth_handshake[] = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };

static int up_iap_auth(struct up_drv_data *drv_data)
{
	size_t size = sizeof(iap_auth_handshake);
	int ep = drv_data->usb.iap_out_ep;

	return up_write_msg(drv_data, ep, iap_auth_handshake, size);
}

static const u8 start_video_command[] = { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

static int up_start_video(struct up_drv_data *drv_data)
{
	size_t size = sizeof(start_video_command);
	int    ep = drv_data->usb.video_out_ep;

	return up_write_msg(drv_data, ep, start_video_command, size);
}

static int up_start_streaming(struct vb2_queue *vq, unsigned int count)
{
	struct up_drv_data *drv_data;
	struct usb_interface *itf;
	struct up_buffer *buf;
	unsigned long flags;
	int urb_sub;
	int retval;
	int i;

	drv_data = vb2_get_drv_priv(vq);
	itf = drv_data->usb.itf;
	if (test_and_set_bit(STREAM_HW_ACTIVE, &drv_data->pipeline.streaming))
		return 0;

	spin_lock_irqsave(&drv_data->pipeline.ready_lock, flags);
	drv_data->pipeline.sequence = 0;
	drv_data->decoder.active_buf = NULL;
	drv_data->decoder.active_pl_len = 0;
	drv_data->decoder.frame_id = -1;
	drv_data->decoder.building_frame = false;
	drv_data->decoder.workspace_len = 0;
	kfifo_reset(&drv_data->decoder.fifo);
	spin_unlock_irqrestore(&drv_data->pipeline.ready_lock, flags);

	u8 hw_idx = drv_data->v4l2.current_hw_index ?
			    drv_data->v4l2.current_hw_index :
			    1;
	retval = up_set_hardware_resolution(drv_data, hw_idx, 30);
	if (retval) {
		dev_err(&itf->dev, "up_set_hardware_resolution failed: %d\n",
			retval);
		goto error_start;
	}

	retval = up_iap_auth(drv_data);
	if (retval) {
		dev_err(&itf->dev, "up_iap_auth failed: %d\n", retval);
		goto error_start;
	}

	retval = up_start_video(drv_data);
	if (retval) {
		dev_err(&itf->dev, "up_start_video failed: %d\n", retval);
		goto error_start;
	}

	/*
	 * Allow URB callback paths to start passing payloads to buffers
	 * We do this before submitting URBs so that the read callbacks can
	 * start processing data before we finish initializing all URBs
	 */
	set_bit(STREAM_CLIENT_READY, &drv_data->pipeline.streaming);

	/*
	 * Ensure the bit is visible to all CPU cores before submitting URBs
	 * Required after Non-Value-Returning set_bit operation.
	 */
	smp_mb__after_atomic();

	/*
	 * Submit the URBs
	 */
	for (urb_sub = 0; urb_sub < NUM_URBS; urb_sub++) {
		retval =
			usb_submit_urb(drv_data->usb.urbs[urb_sub], GFP_KERNEL);
		if (retval) {
			dev_err(&drv_data->usb.itf->dev,
				"Failed to submit URBs: %d\n", retval);
			goto error_start;
		}
	}

	return 0;

error_start:
	/*
	 * Clear the client-ready bit immediately to block incoming URB data paths
	 */
	clear_bit(STREAM_CLIENT_READY, &drv_data->pipeline.streaming);

	/*
	 * Free any URBs that were successfully submitted before the failure
	 */
	for (i = 0; i < urb_sub; i++)
		usb_kill_urb(drv_data->usb.urbs[i]);

	/*
	 * Drain the queue and return buffers to userspace per V4L2 spec
	 */
	spin_lock_irqsave(&drv_data->pipeline.ready_lock, flags);
	while (!list_empty(&drv_data->pipeline.ready_queue)) {
		buf = list_first_entry(&drv_data->pipeline.ready_queue,
				       struct up_buffer, list);
		list_del(&buf->list);
		/*
		 * Buffers correctly marked as queued for V4L2 cleanup on start error
		 */
		vb2_buffer_done(&buf->vb2_buffer.vb2_buf, VB2_BUF_STATE_QUEUED);
	}
	spin_unlock_irqrestore(&drv_data->pipeline.ready_lock, flags);

	/*
	 * Clear the HW guard last so a future start_streaming invocation can re-attempt
	 */
	clear_bit(STREAM_HW_ACTIVE, &drv_data->pipeline.streaming);

	return retval;
}

static void up_buf_queue(struct vb2_buffer *vb)
{
	struct up_drv_data *drv_data = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *v4l2_buf = to_vb2_v4l2_buffer(vb);
	struct up_buffer *buf;
	unsigned long flags;

	buf = container_of(v4l2_buf, struct up_buffer, vb2_buffer);

	spin_lock_irqsave(&drv_data->pipeline.ready_lock, flags);
	list_add_tail(&buf->list, &drv_data->pipeline.ready_queue);
	spin_unlock_irqrestore(&drv_data->pipeline.ready_lock, flags);
}

static int up_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;

	if (vb2_plane_size(vb, 0) < MAX_FRAME_SIZE)
		return -EINVAL;

	vb2_set_plane_payload(vb, 0, MAX_FRAME_SIZE);
	return 0;
}

static int up_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers,
			  unsigned int *nplanes, unsigned int sizes[],
			  struct device *alloc_devs[])
{
	unsigned int allocated_buffers = vb2_get_num_buffers(vq);

	if (allocated_buffers + *nbuffers < 2)
		*nbuffers = 2 - allocated_buffers;

	if (*nplanes)
		return sizes[0] < MAX_FRAME_SIZE ? -EINVAL : 0;

	*nplanes = 1;
	sizes[0] = MAX_FRAME_SIZE;
	return 0;
}

static const struct vb2_ops up_vb2_ops = {
	.stop_streaming = up_stop_streaming,
	.start_streaming = up_start_streaming,
	.buf_queue = up_buf_queue,
	.buf_prepare = up_buf_prepare,
	.queue_setup = up_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

static int up_v4l2_release(struct file *file)
{
	return _vb2_fop_release(file, NULL);
}

static int up_v4l2_open(struct file *file)
{
	return v4l2_fh_open(file);
}

static const struct v4l2_file_operations up_v4l2_fops = {
	.release = up_v4l2_release,
	.open = up_v4l2_open,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.owner = THIS_MODULE,
};

static void up_free_urb(struct up_drv_data *drv_data, int urb_index)
{
	struct usb_device *u_dev = drv_data->usb.udev;
	dma_addr_t dma_addr;
	u8 *urb_buf;

	if (!drv_data->usb.urbs[urb_index])
		return;

	urb_buf = drv_data->usb.urb_buffers[urb_index];
	dma_addr = drv_data->usb.urb_dma_addrs[urb_index];
	if (urb_buf) {
		usb_free_coherent(u_dev, URB_SIZE, urb_buf, dma_addr);
		drv_data->usb.urb_buffers[urb_index] = NULL;
	}

	usb_free_urb(drv_data->usb.urbs[urb_index]);
	drv_data->usb.urbs[urb_index] = NULL;
}

static void up_free_urbs(struct up_drv_data *drv_data)
{
	int i;

	clear_bit(STREAM_CLIENT_READY, &drv_data->pipeline.streaming);
	clear_bit(STREAM_HW_ACTIVE, &drv_data->pipeline.streaming);

	/*
	 * Ensure every callback is stopped and no new ones can be submitted.
	 */
	for (i = 0; i < NUM_URBS; i++)
		usb_kill_urb(drv_data->usb.urbs[i]);

	/*
	 * Release URB resources
	 */
	for (i = 0; i < NUM_URBS; i++)
		up_free_urb(drv_data, i);
}

static void up_disconnect(struct usb_interface *itf)
{
	struct usb_interface *iap_intf;
	struct up_drv_data *drv_data;
	struct usb_driver *driver;
	int itf_num;

	driver = to_usb_driver(itf->dev.driver);
	drv_data = usb_get_intfdata(itf);
	itf_num = itf->cur_altsetting->desc.bInterfaceNumber;
	usb_set_intfdata(itf, NULL);

	/*
	 * Ignore the iAP interface disconnect.
	 * The Video Interface disconnect handles the full device teardown.
	 */
	if (itf_num == UP_IAP_INTERFACE)
		return;

	if (!drv_data)
		return;

	/*
	 * Explicitly release the iAP interface claimed in probe
	 */
	iap_intf = usb_ifnum_to_if(drv_data->usb.udev, UP_IAP_INTERFACE);
	if (iap_intf) {
		usb_set_intfdata(iap_intf, NULL);
		usb_driver_release_interface(driver, iap_intf);
	}

	up_free_urbs(drv_data);

	cancel_work_sync(&drv_data->decoder.work);
	if (drv_data->decoder.wq) {
		destroy_workqueue(drv_data->decoder.wq);
		drv_data->decoder.wq = NULL;
	}

	if (video_is_registered(&drv_data->v4l2.video_dev))
		video_unregister_device(&drv_data->v4l2.video_dev);

	v4l2_device_disconnect(&drv_data->v4l2.v4l2_dev);
	v4l2_device_put(&drv_data->v4l2.v4l2_dev);

	dev_info(&itf->dev, "Device disconnected.\n");
}

static int up_reset_resume(struct usb_interface *intf)
{
	return 0;
}

static int up_resume(struct usb_interface *intf)
{
	return 0;
}

static int up_suspend(struct usb_interface *intf, pm_message_t message)
{
	return 0;
}

static void up_device_release(struct v4l2_device *v4l2_dev)
{
	struct up_drv_data *drv_data;

	drv_data = container_of(v4l2_dev, struct up_drv_data, v4l2.v4l2_dev);

	kfifo_free(&drv_data->decoder.fifo);
	kfree(drv_data->decoder.workspace_buf);
	kfree(drv_data);
}

static void up_on_frame_incomplete(void *context)
{
	struct up_drv_data *drv_data = (struct up_drv_data *)context;
	struct vb2_v4l2_buffer *v4l2_buf;
	struct up_buffer *active_buf;
	struct vb2_buffer *vb2_buf;

	if (!drv_data->decoder.active_buf)
		return;

	active_buf = drv_data->decoder.active_buf;
	v4l2_buf = &active_buf->vb2_buffer;
	vb2_buf = &v4l2_buf->vb2_buf;

	/*
	 * The hardware dropped the End of Image (EOI) marker and rolled over.
	 * Return the buffer to the V4L2 subsystem with an error state to
	 * prevent kernel memory starvation and notify userspace of the tear.
	 */
	vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);

	/*
	 * Clear the active pointer so the upcoming on_video_frame_start
	 * knows it needs to pull a fresh buffer from the ready_queue.
	 */
	drv_data->decoder.active_buf = NULL;
	drv_data->decoder.active_pl_len = 0;
}

static void up_on_frame_complete(void *context)
{
	struct up_drv_data *drv_data = (struct up_drv_data *)context;
	struct vb2_v4l2_buffer *v4l2_buf;
	struct up_buffer *active_buf;
	struct vb2_buffer *vb2_buf;
	size_t vff_len;

	if (!drv_data->decoder.active_buf)
		return;

	active_buf = drv_data->decoder.active_buf;
	v4l2_buf = &active_buf->vb2_buffer;
	vb2_buf = &v4l2_buf->vb2_buf;

	vff_len = drv_data->decoder.active_pl_len;
	if (vff_len < 2) {
		drv_data->dbg.frames_dropped_eoi++;
		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
	} else {
		vb2_set_plane_payload(vb2_buf, 0, vff_len);

		vb2_buf->timestamp = ktime_get_ns();
		v4l2_buf->sequence = drv_data->pipeline.sequence++;

		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_DONE);

		drv_data->dbg.frames_delivered++;
	}

	drv_data->decoder.active_buf = NULL;
	drv_data->decoder.active_pl_len = 0;
}

static void up_on_frame_start(void *context, u8 frame_id, u8 dev_num)
{
	struct up_drv_data *drv_data = (struct up_drv_data *)context;
	struct vb2_v4l2_buffer *v4l2_buf;
	struct up_buffer *active_buf;
	struct vb2_buffer *vb2_buf;
	struct list_head *rdy_q;
	unsigned long flags;

	active_buf = drv_data->decoder.active_buf;
	if (active_buf) {
		v4l2_buf = &active_buf->vb2_buffer;
		vb2_buf = &v4l2_buf->vb2_buf;
		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);
		drv_data->decoder.active_buf = NULL;
	}

	drv_data->decoder.active_pl_len = 0;

	rdy_q = &drv_data->pipeline.ready_queue;

	spin_lock_irqsave(&drv_data->pipeline.ready_lock, flags);
	if (!list_empty(rdy_q)) {
		active_buf = list_first_entry(rdy_q, struct up_buffer, list);
		list_del(&active_buf->list);

		drv_data->decoder.active_buf = active_buf;
	} else {
		drv_data->decoder.active_buf = NULL;
	}
	spin_unlock_irqrestore(&drv_data->pipeline.ready_lock, flags);
}

static void up_on_video_payload(void *context, u8 *data, size_t len)
{
	struct up_drv_data *drv_data = (struct up_drv_data *)context;
	struct vb2_v4l2_buffer *v4l2_buf;
	struct up_buffer *active_buf;
	struct vb2_buffer *vb2_buf;
	struct device *dev;
	size_t vff_len;
	u8 *vaddr;

	if (!drv_data->decoder.active_buf)
		return;

	active_buf = drv_data->decoder.active_buf;
	v4l2_buf = &active_buf->vb2_buffer;
	vb2_buf = &v4l2_buf->vb2_buf;

	vff_len = drv_data->decoder.active_pl_len;
	if (vff_len + len > MAX_FRAME_SIZE) {
		dev = &drv_data->usb.itf->dev;
		dev_err_ratelimited(dev, "useeplus: Overflow Prevention.\n");

		vb2_buffer_done(vb2_buf, VB2_BUF_STATE_ERROR);

		drv_data->decoder.active_buf = NULL;
		drv_data->decoder.active_pl_len = 0;
		drv_data->dbg.frames_dropped_soi++;

		return;
	}

	vaddr = vb2_plane_vaddr(vb2_buf, 0);

	if (vaddr) {
		memcpy(vaddr + vff_len, data, len);
		drv_data->decoder.active_pl_len += len;
	}
}

static void up_work_handler(struct work_struct *work)
{
	struct up_decoder decoder = { 0 };
	struct up_drv_data *drv_data;
	unsigned int len;
	size_t remaining;
	size_t consumed;
	size_t buf_len;
	u8 *dec_buf;
	u8 *buf;

	drv_data = container_of(work, struct up_drv_data, decoder.work);

	dec_buf = drv_data->decoder.workspace_buf;
	buf = dec_buf + drv_data->decoder.workspace_len;
	buf_len = MAX_WORKSPACE_SIZE - drv_data->decoder.workspace_len;
	len = kfifo_out(&drv_data->decoder.fifo, buf, buf_len);

	drv_data->decoder.workspace_len += len;

	if (drv_data->decoder.workspace_len > 0) {
		decoder.context = drv_data;
		decoder.building_frame = drv_data->decoder.building_frame;
		decoder.frame_id = drv_data->decoder.frame_id;
		decoder.found_soi = drv_data->decoder.found_soi;
		decoder.eof_reached = drv_data->decoder.eof_reached;

		decoder.cb.on_video_frame_start = up_on_frame_start;
		decoder.cb.on_video_frame_fragment = up_on_video_payload;
		decoder.cb.on_video_frame_complete = up_on_frame_complete;
		decoder.cb.on_video_frame_incomplete = up_on_frame_incomplete;

		buf_len = drv_data->decoder.workspace_len;
		consumed = up_decode_bulk(&decoder, dec_buf, buf_len);

		drv_data->decoder.building_frame = decoder.building_frame;
		drv_data->decoder.frame_id = decoder.frame_id;
		drv_data->decoder.found_soi = decoder.found_soi;
		drv_data->decoder.eof_reached = decoder.eof_reached;

		buf_len = drv_data->decoder.workspace_len;
		if (consumed < buf_len) {
			remaining = buf_len - consumed;
			memmove(dec_buf, dec_buf + consumed, remaining);
			drv_data->decoder.workspace_len = remaining;
		} else {
			drv_data->decoder.workspace_len = 0;
		}
	}
}

static void up_read_bulk_callback(struct urb *urb)
{
	struct up_drv_data *drv_data;
	int retval;

	drv_data = urb->context;
	if (!test_bit(STREAM_CLIENT_READY, &drv_data->pipeline.streaming))
		return;

	if (urb->status) {
		switch (urb->status) {
		case -ENOENT:
		case -ECONNRESET:
		case -ESHUTDOWN:
		case -ENODEV:
			dev_dbg(&urb->dev->dev, "URB stopped cleanly: %d\n",
				urb->status);
			return;
		case -EPROTO:
			drv_data->dbg.usb_errors++;
			goto resubmit;
		case -EILSEQ:
		case -ECOMM:
			dev_dbg(&urb->dev->dev,
				"Transient CRC/timeout error: %d\n",
				urb->status);
			goto resubmit;
		case -EPIPE:
			dev_err(&urb->dev->dev, "Endpoint stalled.\n");
			return;
		default:
			dev_err(&urb->dev->dev, "Uncaught URB error: %d\n",
				urb->status);
			return;
		}
	}

	drv_data->dbg.urbs_processed++;
	if (drv_data->dbg.urbs_processed % DIAG_LOG_ITERATIONS == 0) {
		dev_dbg(&drv_data->usb.itf->dev, DIAG_DATA_FORMAT,
			drv_data->dbg.urbs_processed, drv_data->dbg.usb_errors,
			drv_data->dbg.packets_found, drv_data->dbg.frames_found,
			drv_data->dbg.frames_delivered,
			drv_data->dbg.frames_dropped_soi,
			drv_data->dbg.frames_dropped_eoi,
			drv_data->dbg.frames_dropped_queue,
			drv_data->dbg.ghost_headers);
	}

	if (kfifo_avail(&drv_data->decoder.fifo) >= urb->actual_length) {
		kfifo_in(&drv_data->decoder.fifo, urb->transfer_buffer,
			 urb->actual_length);
	} else {
		dev_warn(&urb->dev->dev,
			 "kfifo overflow, dropping URB payload\n");
	}

	queue_work(drv_data->decoder.wq, &drv_data->decoder.work);

resubmit:
	if (test_bit(STREAM_CLIENT_READY, &drv_data->pipeline.streaming)) {
		retval = usb_submit_urb(urb, GFP_ATOMIC);
		if (retval && retval != -ENODEV && retval != -ESHUTDOWN &&
		    retval != -ENOENT)
			dev_err(&drv_data->usb.itf->dev,
				"usb_submit_urb failed: %d\n", retval);
	}
}

static int up_alloc_urbs(struct up_drv_data *drv_data)
{
	struct usb_device *usb_dev;
	struct usb_interface *itf;
	usb_complete_t u_comp;
	dma_addr_t *dma;
	struct urb *urb;
	int vid_in_pipe;
	u8 *urb_ptr;
	u8 *urb_buf;
	int i;

	usb_dev =  = drv_data->usb.udev;
	vid_in_pipe = usb_rcvbulkpipe(usb_dev, drv_data->usb.video_in_ep);
	u_comp = up_read_bulk_callback;
	itf =  = drv_data->usb.itf;

	for (i = 0; i < NUM_URBS; i++) {
		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			dev_err(&itf->dev, "usb_alloc_urb failed\n");
			return -ENOMEM;
		}

		drv_data->usb.urbs[i] = urb;

		dma = &drv_data->usb.urb_dma_addrs[i];
		urb_ptr = usb_alloc_coherent(usb_dev, URB_SIZE, GFP_KERNEL, dma);
		if (!urb_ptr) {
			dev_err(&itf->dev, "usb_alloc_coherent failed\n");
			return -ENOMEM;
		}

		drv_data->usb.urb_buffers[i] = urb_ptr;

		urb_buf = drv_data->usb.urb_buffers[i];
		usb_fill_bulk_urb(urb, usb_dev, vid_in_pipe, urb_buf, URB_SIZE,
				  u_comp, drv_data);

		urb->transfer_dma = *dma;
		urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	}

	return 0;
}

static int up_probe(struct usb_interface *itf, const struct usb_device_id *id)
{
	struct usb_endpoint_descriptor *ep_desc;
	struct usb_host_interface *video_alt;
	struct usb_interface *iap_intf;
	struct up_drv_data *drv_data;
	struct usb_device *usb_dev;
	struct usb_driver *driver;
	struct vb2_queue *q;
	int vid_in_pipe;
	int iap_in_pipe;
	int hb_bytes;
	u8 *hb_sink;
	int itf_num;
	int retval;
	int i;
	u8 ep;

	driver = to_usb_driver(itf->dev.driver);
	usb_dev = interface_to_usbdev(itf);
	itf_num = itf->cur_altsetting->desc.bInterfaceNumber;
	drv_data = NULL;

	if (itf_num != UP_VIDEO_INTERFACE)
		return -ENODEV;

	dev_info(&itf->dev, "Useeplus borescope identified\n");

	drv_data = kzalloc(sizeof(*drv_data), GFP_KERNEL);
	if (!drv_data)
		return -ENOMEM;

	drv_data->usb.udev = usb_dev;
	drv_data->usb.itf = itf;
	drv_data->pipeline.sequence = 0;
	drv_data->decoder.building_frame = false;
	drv_data->decoder.active_pl_len = 0;
	drv_data->decoder.workspace_len = 0;
	drv_data->v4l2.width = UP_DEF_WIDTH;
	drv_data->v4l2.height = UP_DEF_HEIGHT;

	mutex_init(&drv_data->v4l2.lock);
	spin_lock_init(&drv_data->pipeline.ready_lock);
	INIT_LIST_HEAD(&drv_data->pipeline.ready_queue);

	iap_intf = usb_ifnum_to_if(usb_dev, UP_IAP_INTERFACE);
	if (!iap_intf) {
		dev_err(&itf->dev, "Could not find iAP interface\n");
		retval = -ENODEV;
		goto error_free_dev;
	}

	retval = usb_driver_claim_interface(driver, iap_intf, drv_data);
	if (retval) {
		dev_err(&itf->dev, "Could not claim iAP interface\n");
		goto error_free_dev;
	}

	drv_data->decoder.workspace_buf =
		kzalloc(MAX_WORKSPACE_SIZE, GFP_KERNEL);
	if (!drv_data->decoder.workspace_buf) {
		retval = -ENOMEM;
		goto error_release_iap;
	}

	INIT_WORK(&drv_data->decoder.work, up_work_handler);

	drv_data->decoder.wq =
		alloc_ordered_workqueue("useeplus_wq", WQ_MEM_RECLAIM);
	if (!drv_data->decoder.wq) {
		dev_err(&itf->dev, "Could not allocate workqueue\n");
		retval = -ENOMEM;
		goto error_release_iap;
	}

	if (kfifo_alloc(&drv_data->decoder.fifo, FIFO_Q_SIZE, GFP_KERNEL)) {
		dev_err(&itf->dev, "Could not allocate FIFO queue\n");
		retval = -ENOMEM;
		goto error_release_iap;
	}

	video_alt = usb_altnum_to_altsetting(itf, UP_ALT_VIDEO_ENABLE);
	if (!video_alt) {
		dev_err(&itf->dev, "Could not find Video Altsetting\n");
		retval = -ENODEV;
		goto error_release_iap;
	}

	for (i = 0; i < video_alt->desc.bNumEndpoints; i++) {
		ep_desc = &video_alt->endpoint[i].desc;
		ep = ep_desc->bEndpointAddress;

		if (usb_endpoint_num(ep_desc) == UP_VIDEO_ENDPOINT) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->usb.video_in_ep = ep;
			else
				drv_data->usb.video_out_ep = ep;
		}
	}

	for (i = 0; i < iap_intf->cur_altsetting->desc.bNumEndpoints; i++) {
		ep_desc = &iap_intf->cur_altsetting->endpoint[i].desc;
		ep = ep_desc->bEndpointAddress;

		if (usb_endpoint_num(ep_desc) == UP_IAP_ENDPOINT) {
			if (usb_endpoint_dir_in(ep_desc))
				drv_data->usb.iap_in_ep = ep;
			else
				drv_data->usb.iap_out_ep = ep;
		}
	}

	if (!drv_data->usb.video_in_ep || !drv_data->usb.video_out_ep ||
	    !drv_data->usb.iap_in_ep || !drv_data->usb.iap_out_ep) {
		dev_err(&itf->dev, "Could not map all endpoints\n");
		retval = -ENODEV;
		goto error_release_iap;
	}

	vid_in_pipe = usb_rcvbulkpipe(usb_dev, drv_data->usb.video_in_ep);
	iap_in_pipe = usb_rcvbulkpipe(usb_dev, drv_data->usb.iap_in_ep);

	drv_data->v4l2.v4l2_dev.release = up_device_release;
	retval = v4l2_device_register(&itf->dev, &drv_data->v4l2.v4l2_dev);
	if (retval) {
		dev_err(&itf->dev,
			"v4l2_device_register failed with error %d\n", retval);
		goto error_release_iap;
	}

	q = &drv_data->v4l2.queue;
	q->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	q->io_modes = VB2_MMAP | VB2_USERPTR | VB2_READ;
	q->min_reqbufs_allocation = 2;
	q->drv_priv = drv_data;
	q->buf_struct_size = sizeof(struct up_buffer);
	q->ops = &up_vb2_ops;
	q->mem_ops = &vb2_vmalloc_memops;
	q->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC;
	q->min_queued_buffers = 2;
	q->lock = &drv_data->v4l2.lock;
	q->dev = &itf->dev;
	strscpy(q->name, VIDEO_QUEUE_NAME, sizeof(q->name));

	retval = vb2_queue_init(q);
	if (retval) {
		dev_err(&itf->dev, "vb2_queue_init failed\n");
		goto error_unreg_v4l2;
	}

	strscpy(drv_data->v4l2.video_dev.name, VIDEO_DEVICE_NAME,
		sizeof(drv_data->v4l2.video_dev.name));
	drv_data->v4l2.video_dev.v4l2_dev = &drv_data->v4l2.v4l2_dev;
	drv_data->v4l2.video_dev.fops = &up_v4l2_fops;
	drv_data->v4l2.video_dev.ioctl_ops = &up_v4l2_ioctl_ops;
	drv_data->v4l2.video_dev.release = video_device_release_empty;
	drv_data->v4l2.video_dev.lock = &drv_data->v4l2.lock;
	drv_data->v4l2.video_dev.queue = q;
	drv_data->v4l2.video_dev.device_caps = V4L2_CAP_VIDEO_CAPTURE |
					       V4L2_CAP_STREAMING |
					       V4L2_CAP_READWRITE;

	video_set_drvdata(&drv_data->v4l2.video_dev, drv_data);

	hb_sink = kmalloc(HB_BUF_SIZE, GFP_KERNEL);
	if (!hb_sink) {
		retval = -ENOMEM;
		goto error_unreg_v4l2;
	}

	/*
	 * Prune the incoming iAP heartbeats
	 */
	for (i = 0; i < HB_SINK_COUNT; i++) {
		usb_bulk_msg(usb_dev, iap_in_pipe, hb_sink, HB_BUF_SIZE,
			     &hb_bytes, HB_SINK_TO);
	}

	kfree(hb_sink);

	retval = usb_set_interface(usb_dev, UP_VIDEO_INTERFACE,
				   UP_ALT_VIDEO_ENABLE);
	if (retval) {
		dev_err(&itf->dev, "usb_set_interface failed with error %d\n",
			retval);
		goto error_unreg_v4l2;
	}

	retval = usb_clear_halt(usb_dev, vid_in_pipe);
	if (retval)
		dev_info(&itf->dev, "usb_clear_halt failed with error %d\n",
			 retval);

	retval = up_alloc_urbs(drv_data);
	if (retval)
		goto error_urbs;

	usb_set_intfdata(itf, drv_data);

	retval = video_register_device(&drv_data->v4l2.video_dev,
				       VFL_TYPE_VIDEO, -1);
	if (retval) {
		dev_err(&itf->dev,
			"video_register_device failed with error %d\n", retval);
		goto error_urbs;
	}

	dev_info(&itf->dev, "Device connected.\n");
	return 0;

error_urbs:
	dev_dbg(&itf->dev, "Rolling back URBs\n");
	up_free_urbs(drv_data);

error_unreg_v4l2:
	dev_dbg(&itf->dev, "Unregistering device and releasing queue\n");
	vb2_queue_release(&drv_data->v4l2.queue);
	v4l2_device_unregister(&drv_data->v4l2.v4l2_dev);

error_release_iap:
	usb_driver_release_interface(driver, iap_intf);
	kfifo_free(&drv_data->decoder.fifo);
	if (drv_data->decoder.wq)
		destroy_workqueue(drv_data->decoder.wq);
	kfree(drv_data->decoder.workspace_buf);

error_free_dev:
	kfree(drv_data);
	return retval;
}

static const struct usb_device_id up_table[] = { { USB_DEVICE(0x0329, 0x2022) },
						 { USB_DEVICE(0x2ce3, 0x3828) },
						 {} };

static struct usb_driver up_driver = {
	.disconnect = up_disconnect,
	.reset_resume = up_reset_resume,
	.resume = up_resume,
	.suspend = up_suspend,
	.probe = up_probe,
	.id_table = up_table,
	.name = USB_DRIVER_NAME,
};

static void __exit up_exit(void)
{
	pr_debug("useeplus_v4l2: Module exited.\n");
	usb_deregister(&up_driver);
}

static int __init up_init(void)
{
	pr_debug("useeplus_v4l2: Module initialized.\n");
	return usb_register(&up_driver);
}

MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("V4L2 driver for Useeplus protocol cameras");
MODULE_VERSION("0.1.0");
MODULE_DEVICE_TABLE(usb, up_table);

module_exit(up_exit);
module_init(up_init);
