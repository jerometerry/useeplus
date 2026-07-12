## Learning The Fundamentals

I had fun building a V4L2 Linux Kernel Driver, using AI to generate the V4L2 / VB2 code. Now that
it's working reliably I want to go back and understand the fundamentals of V4L2 and VB2, and review
the design of the useeplus V4L2 Driver.

## Where to Start

The first thing I wanted to do is collapse the driver into a single .c file that I can compile
on my Raspberry Pi. I've copied useeplus_core.c from the ./driver folder, added all the includes
manually, and reorganized the file for readability.

### Bottom Up - Module Definition

The organization follows the bottom up Linux Kernel Driver style. At the bottom of useeplus_core.c
is the module definition

```bash
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
```

Working back to the top of the file, methods are ordered such that forward declarations are avoided.
This results in the file ending up in reverse order that what you would typically write.

### Includes

I organized the includes alphabetically so we can clearly identify the drivers kernel dependencies.

```bash
// SPDX-License-Identifier: MIT OR GPL-2.0-only

#include <asm/byteorder.h>

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kfifo.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/unaligned.h>
#include <linux/usb.h>
#include <linux/vmalloc.h>
#include <linux/workqueue.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-ioctl.h>

#include <media/videobuf2-v4l2.h>
#include <media/videobuf2-vmalloc.h>
```

### Defines

I moved all the defines to below the includes

```bash
#define UP_MAX_VIDEO_FRM_FRAG_LEN 1024
#define JPEG_SOI_MAX_POS 256
#define MAX_GHOST_HDR_OFF 160

#define UP_USB_FRM_HDR_LEN (sizeof(struct up_usb_frm_hdr))
#define UP_VIDEO_FRM_FRAG_HDR_LEN (sizeof(struct up_video_frm_frag_hdr))
#define VIDEO_DATA_OFFSET (UP_USB_FRM_HDR_LEN + UP_VIDEO_FRM_FRAG_HDR_LEN)

#define USB_DRIVER_NAME "useeplus"
#define CAP_DRIVER "useeplus"
#define CAP_CARD "useeplus protocol cameras"
#define V4L2_INPUT_NAME "Camera Lens Channel 0"
#define VIDEO_QUEUE_NAME "useeplus-queue"
#define VIDEO_DEVICE_NAME "useeplus-video"

#define NUM_URBS 4
#define URB_SIZE (16 * 1024)
#define MAX_FRAME_SIZE (256 * 1024)
#define MAX_WORKSPACE_SIZE (512 * 1024)
#define FIFO_Q_SIZE (256 * 1024)

#define UP_DEF_WIDTH 640
#define UP_DEF_HEIGHT 480

#define DIAG_DATA_FORMAT \
	"URBs:%lu Err:%lu Pkt:%lu Frm:%lu Deliv:%lu D-SOI:%lu D-EOI:%lu D-Q:%lu Ghost:%lu\n"
```

### Enums

Following the Defines, I grouped all the enums.

```bash
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

enum up_config {
	HB_BUF_SIZE = 512,
	HB_SINK_COUNT = 30,
	HB_SINK_TO = 100,
	DIAG_LOG_ITERATIONS = 300,
	USB_TO = 1000,
	USB_CTRL_SET_TO = 5000,
};

enum up_stream_state {
	STREAM_HW_ACTIVE = 0,
	STREAM_CLIENT_READY = 1,
};
```

### Structs

After the Enums, I grouped all the Structs.

#### Useeplus Protocol

The structs `up_usb_frm_hdr` and `up_video_frm_frag_hdr` belong to the Useeplus protocol, defining
the USB Frame Header and Video Frame Header, respectively.

```bash
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
```

#### Useeplus Decoder

These structs are used by the Useeplus decoder logic contained in the `up_decode_bulk` function.

```bash
struct up_decoder_callbacks {
	void (*on_video_frame_start)(void *context, u8 frame_id, u8 dev_num);
	void (*on_video_frame_fragment)(void *context, u8 *data, size_t len);
	void (*on_video_frame_complete)(void *context);
	void (*on_video_frame_incomplete)(void *context);
};

struct up_decode_state {
	size_t usb_frm_len;
	u8 frame_id;
	u8 dev_num;
	u8 flags;
};

struct up_decoder {
	struct up_decoder_callbacks cb;
	void *context;

	bool building_frame;
	bool eof_reached;
	bool found_soi;
	int  frame_id;
};
```

#### Useeplus Driver Data

The struct `up_drv_data` holds the data structures allocated by the driver, initialized in the
`up_probe` function and registered using `video_set_drvdata()`.

```bash
struct up_buffer {
	struct vb2_v4l2_buffer vb2_buffer;
	struct list_head       list;
};

struct up_drv_data {
	struct {
		struct urb	     *urbs[NUM_URBS];
		u8		     *urb_buffers[NUM_URBS];
		struct usb_device    *udev;
		struct usb_interface *itf;
		u8		      video_out_ep;
		u8		      video_in_ep;
		u8		      iap_out_ep;
		u8		      iap_in_ep;
		dma_addr_t	      urb_dma_addrs[NUM_URBS];
	} usb;

	struct {
		struct video_device      video_dev;
		struct v4l2_device       v4l2_dev;
		struct vb2_queue         queue;
		// Mutex protecting the video_queue
		struct mutex             lock;
		u32	                 height;
		u32	                 width;
		enum up_resolution_index current_hw_index;
	} v4l2;

	struct {
		unsigned long streaming;
		// Spinlock protecting access to ready_queue
		spinlock_t	 ready_lock;
		struct list_head ready_queue;
		u64		 sequence;
	} pipeline;

	struct {
		struct workqueue_struct *wq;
		struct work_struct	 work;
		DECLARE_KFIFO_PTR(fifo, u8);

		u8    *workspace_buf;
		size_t workspace_len;

		struct up_buffer *active_buf;
		size_t		  active_pl_len;

		bool building_frame;
		bool eof_reached;
		bool found_soi;
		int  frame_id;
	} decoder;

	struct {
		unsigned long urbs_processed;
		unsigned long usb_errors;
		unsigned long packets_found;
		unsigned long frames_found;
		unsigned long frames_dropped_soi;
		unsigned long frames_dropped_eoi;
		unsigned long frames_dropped_queue;
		unsigned long frames_delivered;
		unsigned long ghost_headers;
	} dbg;
};
```

## Installing Useeplus Driver

To have confidence that I didn't break the driver as part of this learning exercise, I want to be
able to compile, install and load the Useeplus driver. To do that, I need to explain where I placed
the driver in the Linux Kernel tree.

### On-Tree

I used my Raspberry Pi 5 for testing the Useeplus driver. My Raspberry Pi 5 is running the
Raspberry Pi variant of Linux, so I used the Raspberry Pi Linux repo.

```bash
git clone --depth 1 https://github.com/raspberrypi/linux.git
```

I placed the Useeplus driver in drivers/media/usb/useeplus

Here's my [fork of rpi-linux](https://github.com/jerometerry/rpi-linux/tree/useeplus_v4l2) where I
added the Useeplus driver.

Pull in the latest upstream commits

```bash
git fetch upstream
git merge upstream/rpi-6.18.y
```

Clean previous build artifacts

```bash
make mrproper
```

### KConfig and Makefile

To build the driver with the Linux Kbuild system, I added a Kconfig and Makefile.

Kconfig is used to add Useeplus to the Linux Kernel config file before compiling to enable / disable
kernel features. For testing, I ran make menuconfig and enabled Useeplus, since it defaults to not
loading by default. The Makefile is self explanatory.

Here's the contents of the drivers/media/usb/useeplus folder with the useeplus_core.c file
containing the single file variant driver useeplus_core.c.

```bash
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux/drivers/media/usb/useeplus $ tree
.
├── Kconfig
├── Makefile
└── useeplus_core.c

1 directory, 3 files
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux/drivers/media/usb/useeplus $ cat Kconfig
# SPDX-License-Identifier: MIT OR GPL-2.0-only
config USB_USEEPLUS
	tristate "Useeplus Protocol USB Camera support"
	depends on VIDEO_DEV && USB
	select VIDEOBUF2_VMALLOC
	help
	  Choose Y or M here to support non-UVC USB cameras utilizing
	  the Useeplus proprietary protocol.

	  To compile this driver as a module, choose M here: the module will be called useeplus
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux/drivers/media/usb/useeplus $ cat Makefile
# SPDX-License-Identifier: MIT OR GPL-2.0-only
obj-$(CONFIG_USB_USEEPLUS) += useeplus.o
useeplus-y := useeplus_core.o
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux/drivers/media/usb/useeplus $


```

### Registering Useeplus Driver with usb/Kconfig

I added the useeplus Kconfig under `MEDIA_CAMERA_SUPPORT` just above uvc. I figure that since the
Useeplus cameras are nearly uvc, keeping it close to uvc was appropriate.

```bash
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux/drivers/media/usb $ cat Kconfig
# SPDX-License-Identifier: GPL-2.0-only

if USB && MEDIA_SUPPORT

menuconfig MEDIA_USB_SUPPORT
	bool "Media USB Adapters"
	help
	  Enable media drivers for USB bus.
	  If you have such devices, say Y.

if MEDIA_USB_SUPPORT

if MEDIA_CAMERA_SUPPORT
	comment "Webcam devices"

source "drivers/media/usb/gspca/Kconfig"
source "drivers/media/usb/pwc/Kconfig"
source "drivers/media/usb/s2255/Kconfig"
source "drivers/media/usb/usbtv/Kconfig"

# Reference to useeplus/Kconfig
source "drivers/media/usb/useeplus/Kconfig"

source "drivers/media/usb/uvc/Kconfig"

endif

if MEDIA_ANALOG_TV_SUPPORT
	comment "Analog TV USB devices"

source "drivers/media/usb/go7007/Kconfig"
source "drivers/media/usb/hdpvr/Kconfig"
source "drivers/media/usb/pvrusb2/Kconfig"
source "drivers/media/usb/stk1160/Kconfig"

endif

if (MEDIA_ANALOG_TV_SUPPORT || MEDIA_DIGITAL_TV_SUPPORT)
	comment "Analog/digital TV USB devices"

source "drivers/media/usb/au0828/Kconfig"
source "drivers/media/usb/cx231xx/Kconfig"

endif

if I2C && MEDIA_DIGITAL_TV_SUPPORT
	comment "Digital TV USB devices"

source "drivers/media/usb/as102/Kconfig"
source "drivers/media/usb/b2c2/Kconfig"
source "drivers/media/usb/dvb-usb-v2/Kconfig"
source "drivers/media/usb/dvb-usb/Kconfig"
source "drivers/media/usb/siano/Kconfig"
source "drivers/media/usb/ttusb-budget/Kconfig"
source "drivers/media/usb/ttusb-dec/Kconfig"

endif

if (MEDIA_CAMERA_SUPPORT || MEDIA_ANALOG_TV_SUPPORT || MEDIA_DIGITAL_TV_SUPPORT)
	comment "Webcam, TV (analog/digital) USB devices"

source "drivers/media/usb/em28xx/Kconfig"

endif

if MEDIA_SDR_SUPPORT
	comment "Software defined radio USB devices"

source "drivers/media/usb/airspy/Kconfig"
source "drivers/media/usb/hackrf/Kconfig"
source "drivers/media/usb/msi2500/Kconfig"

endif

endif #MEDIA_USB_SUPPORT
endif #USB
```

### Registering Useeplus Driver with usb/Makefile

To get the Useeplus driver to be compiled during the Linux Kernel build, I added Useeplus to
`drivers/media/usb/Makefile`, again just above `uvc`.

```bash
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux/drivers/media/usb $ cat Makefile
# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the USB media device drivers
#

# DVB USB-only drivers. Please keep it alphabetically sorted by directory name
# (e. g. LC_ALL=C sort Makefile)
obj-y += b2c2/
obj-y += dvb-usb/
obj-y += dvb-usb-v2/
obj-y += s2255/
obj-y += siano/
obj-y += ttusb-budget/
obj-y += ttusb-dec/

# Please keep it alphabetically sorted by Kconfig name
# (e. g. LC_ALL=C sort Makefile)
obj-$(CONFIG_DVB_AS102) += as102/
obj-$(CONFIG_USB_AIRSPY) += airspy/
obj-$(CONFIG_USB_GSPCA) += gspca/
obj-$(CONFIG_USB_HACKRF) += hackrf/
obj-$(CONFIG_USB_MSI2500) += msi2500/
obj-$(CONFIG_USB_PWC) += pwc/

# Referencing Useeplus
obj-$(CONFIG_USB_USEEPLUS) += useeplus/

obj-$(CONFIG_USB_VIDEO_CLASS) += uvc/
obj-$(CONFIG_VIDEO_AU0828) += au0828/
obj-$(CONFIG_VIDEO_CX231XX) += cx231xx/
obj-$(CONFIG_VIDEO_EM28XX) += em28xx/
obj-$(CONFIG_VIDEO_GO7007) += go7007/
obj-$(CONFIG_VIDEO_HDPVR) += hdpvr/
obj-$(CONFIG_VIDEO_PVRUSB2) += pvrusb2/
obj-$(CONFIG_VIDEO_STK1160) += stk1160/
obj-$(CONFIG_VIDEO_USBTV) += usbtv/
```

### Enabling Useeplus Driver in Config

Before compiling the Linux Kernel, I ran make menuconfig to ensure Useeplus driver was enabled.

Since I'm on a Raspberry Pi 5, I used `make bcm2712_defconfig` instead of `make defconfig`

```bash
KERNEL=kernel_custom
make bcm2712_defconfig
make menuconfig
```

That launches the menu config editor. To enable the Useeplus driver, here's how to navigate the
config editor to get to Useeplus

1. Device Drivers --->
2. Multimedia support --->
3. Media drivers --->
4. Media USB Adapters --->
5. Useeplus Protocol USB Camera support

Toggle the option for "Useeplus Protocol USB Camera support" to "M". Exit menu config, saving the
changes.

To confirm this was saved, grep .config file for `CONFIG_USB_USEEPLUS`, and ensure it's set to "m".

```bash
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux $ cat .config | grep USEEPLUS
CONFIG_USB_USEEPLUS=m
```

### Compiling

With Useeplus driver enabled in the config, compile the Linux Kernel using `make -j$(nproc)`. This
runs make (in the linux tree root), splitting the work in parallel jobs across all cores. The first
build will take awhile.

```bash
make -j$(nproc) Image.gz modules dtbs
sudo make modules_install

export KERNEL_RELEASE=$(make -s kernelrelease)
sudo update-initramfs -c -k $KERNEL_RELEASE
sudo cp /boot/initrd.img-$KERNEL_RELEASE /boot/firmware/initramfs-custom.img

sudo cp /boot/firmware/$KERNEL.img /boot/firmware/$KERNEL-backup.img
sudo cp arch/arm64/boot/Image.gz /boot/firmware/$KERNEL.img
sudo cp arch/arm64/boot/dts/broadcom/*.dtb /boot/firmware/
sudo cp arch/arm64/boot/dts/overlays/*.dtb* /boot/firmware/overlays/
sudo cp arch/arm64/boot/dts/overlays/README /boot/firmware/overlays/

sudo reboot
```

### Loading Useeplus

After installing the modules, those modules aren't loaded automatically. To confirm, run lsmod

```bash
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux $ lsmod | grep useeplus
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux $
```

After installing new modules, rebuild the module dependencies.

```bash
sudo depmod -a
```

Test if the useeplus driver is ready to be loaded by running modinfo.

```bash
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux $ modinfo -F depends useeplus
videobuf2-v4l2,videodev,videobuf2-common,videobuf2-vmalloc
```

Load the useeplus driver by running modprobe, and confirm via lsmod

```bash
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux $ sudo modprobe useeplus
jterry@authentic-nerd:~/github/useeplus-rpi/rpi-linux $ lsmod | grep useeplus
useeplus               65536  0
videobuf2_vmalloc      65536  1 useeplus
videobuf2_v4l2         49152  4 useeplus,pisp_be,rpi_hevc_dec,v4l2_mem2mem
videodev              360448  5 useeplus,pisp_be,rpi_hevc_dec,videobuf2_v4l2,v4l2_mem2mem
videobuf2_common       98304  8 useeplus,videobuf2_vmalloc,pisp_be,rpi_hevc_dec,videobuf2_dma_contig,videobuf2_v4l2,v4l2_mem2mem,videobuf2_memops
```

## Useeplus Driver Skeleton

```c
// Useeplus Decoder Helpers
static inline bool up_is_valid_dev_id(u8 dev_id);

static inline bool up_is_valid_usb_frm_del(u16 delimiter);

static inline u16 up_get_usb_frm_del(const struct up_usb_frm_hdr \*hdr);

static inline u16 up_get_usb_frm_pl_len(const struct up_usb_frm_hdr \*hdr);

static inline bool up_check_usb_frm_hdr(u16 del, u8 dev_id);

static inline struct up_usb_frm_hdr *up_get_usb_frm_hdr(u8 *buf, size_t index);

static inline struct up_video_frm_frag_hdr * up_get_video_frm_frag_hdr(u8 *buf, size_t index);

static inline bool up_is_valid_usb_frm_hdr(struct up_usb_frm_hdr \*hdr);

static inline bool up_is_jpg_soi(const u8 \*ptr, size_t i);

static inline bool up_is_jpg_eoi(const u8 \*ptr, size_t i);

static inline bool up_has_gravity_sensor(u8 flags);

static inline bool up_is_button_pressed(u8 flags);

static inline u8 up_get_other_flags(u8 flags);

static inline bool up_has_other_flags(u8 flags);

static inline void up_set_has_gravity_sensor(struct up_video_frm_frag_hdr \*hdr, bool has_gs);

static inline void up_set_button_pressed(struct up_video_frm_frag_hdr \*hdr, bool pressed);

static inline void up_set_other_flags(struct up_video_frm_frag_hdr \*hdr, uint8_t other);

static inline bool up_is_valid_video_frm_frag_hdr(const struct up_video_frm_frag_hdr \*hdr);

// Useeplus Decoder Logic
static bool up_check_ghost_hdr(u8 *buf, size_t len, size_t buf_off, size_t *u_hdr_off);

static enum up_decode_status up_decode(u8 *buf, size_t len, size_t *cur_pos, struct up_decode_state \*state);

static size_t up_decode_bulk(struct up_decoder *dec, u8 *buf, size_t len);

// Useeeplus Driver

// Methods needed to initialize v4l2_ioctl_ops.
static int up_g_parm(struct file *file, void *priv, struct v4l2_streamparm \*sp);

static int up_s_parm(struct file *file, void *priv, struct v4l2_streamparm \*sp);

static int up_s_input(struct file *file, void *priv, unsigned int i);

static int up_g_input(struct file *file, void *priv, unsigned int \*i);

static int up_enum_input(struct file *file, void *priv, struct v4l2_input \*inp);

static const struct v4l2_frmsize_discrete up_sizes[] = {
	{ 640, 480 },
	{ 320, 240 },
	{ 1280, 720 },
};

static int up_enum_frameintervals(struct file *file, void *priv, struct v4l2_frmivalenum \*fival);

static int up_enum_framesizes(struct file *file, void *priv, struct v4l2_frmsizeenum \*fsize);

static int up_enum_fmt_vid_cap(struct file *file, void *priv, struct v4l2_fmtdesc \*f);

static void up_enforce_format(struct up_drv_data *drv_data, struct v4l2_format *f);

static int up_try_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format \*f);

static int up_set_hardware_resolution(struct up_drv_data \*drv_data, u8 frame_index, u32 target_fps);

static int up_s_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format \*f);

static int up_g_fmt_vid_cap(struct file *file, void *priv, struct v4l2_format \*f);

static int up_vidioc_querycap(struct file *file, void *priv, struct v4l2_capability \*cap);

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

// Methods needed to initialize vb2_ops
static int up_write_msg(struct up_drv_data *data, u8 ep_addr, const u8 *tokens, size_t len);

static const u8 iap_auth_handshake[] = { 0xFF, 0x55, 0xFF, 0x55, 0xEE, 0x10 };

static int up_iap_auth(struct up_drv_data \*drv_data);

static const u8 start_video_command[] = { 0xBB, 0xAA, 0x05, 0x00, 0x00 };

static int up_start_video(struct up_drv_data \*drv_data);

static const u8 stop_video_command[] = { 0xBB, 0xAA, 0x06, 0x00, 0x00 };

static int up_stop_video(struct up_drv_data \*drv_data);

static void up_stop_streaming(struct vb2_queue \*vq);

static int up_start_streaming(struct vb2_queue \*vq, unsigned int count);

static void up_buf_queue(struct vb2_buffer \*vb);

static int up_buf_prepare(struct vb2_buffer \*vb);

static int up_queue_setup(struct vb2_queue *vq, unsigned int *nbuffers, unsigned int *nplanes,
			  unsigned int sizes[], struct device *alloc_devs[]);

static const struct vb2_ops up_vb2_ops = {
	.stop_streaming = up_stop_streaming,
	.start_streaming = up_start_streaming,
	.buf_queue = up_buf_queue,
	.buf_prepare = up_buf_prepare,
	.queue_setup = up_queue_setup,
	.wait_prepare = vb2_ops_wait_prepare,
	.wait_finish = vb2_ops_wait_finish,
};

// Methods needed to initialize v4l2_file_operations
static int up_v4l2_release(struct file \*file);

static int up_v4l2_open(struct file \*file);

static const struct v4l2_file_operations up_v4l2_fops = {
	.release = up_v4l2_release,
	.open = up_v4l2_open,
	.unlocked_ioctl = video_ioctl2,
	.read = vb2_fop_read,
	.poll = vb2_fop_poll,
	.mmap = vb2_fop_mmap,
	.owner = THIS_MODULE,
};

// Methods needed to initialize usb_driver
static void up_free_urb(struct up_drv_data \*drv_data, int urb_index);

static void up_free_urbs(struct up_drv_data \*drv_data);

static void up_disconnect(struct usb_interface \*itf);

static int up_reset_resume(struct usb_interface \*intf);

static int up_resume(struct usb_interface \*intf);

static int up_suspend(struct usb_interface \*intf, pm_message_t message);

static void up_device_release(struct v4l2_device \*v4l2_dev);

static void up_on_frame_incomplete(void \*context);

static void up_on_frame_complete(void \*context);

static void up_on_frame_start(void \*context, u8 frame_id, u8 dev_num);

static void up_on_video_payload(void *context, u8 *data, size_t len);

static void up_work_handler(struct work_struct \*work);

static void up_read_bulk_callback(struct urb \*urb);

static int up_alloc_urbs(struct up_drv_data \*drv_data);

static int up_probe(struct usb_interface *itf, const struct usb_device_id *id);

static const struct usb_device_id up_table[] = {
	{ USB_DEVICE(0x0329, 0x2022) },
	{ USB_DEVICE(0x2ce3, 0x3828) },
	{}
};

static struct usb_driver up_driver = {
	.disconnect = up_disconnect,
	.reset_resume = up_reset_resume,
	.resume = up_resume,
	.suspend = up_suspend,
	.probe = up_probe,
	.id_table = up_table,
	.name = USB_DRIVER_NAME,
};

module_usb_driver(up_driver);

// Confiure module info
MODULE_LICENSE("Dual MIT/GPL");
MODULE_AUTHOR("Jerome Terry");
MODULE_DESCRIPTION("V4L2 driver for Useeplus protocol cameras");
MODULE_VERSION("0.1.0");
MODULE_DEVICE_TABLE(usb, up_table);
```
