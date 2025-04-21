#include <linux/fb.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/serial_core.h>
#include <linux/ioport.h>
#include <linux/wait.h>
#include <linux/reset.h>
#include <linux/gpio/consumer.h>

#include "hw_reg.h"
#include "dcfb.h"
#include <dt-bindings/clock/nuvoton,ma35d1-clk.h>

#include <linux/clk.h>
#include <linux/irq.h>
#include <asm/irq.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <video/of_display_timing.h>
#include <video/of_videomode.h>
#include <video/videomode.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>


#define DISP_VERSION "1.18"

enum {
	PIX_FMT_X4R4G4B4	= 0x00,
	PIX_FMT_A4R4G4B4	= 0x01,
	PIX_FMT_X1R5G5B5	= 0x02,
	PIX_FMT_A1R5G5B5	= 0x03,
	PIX_FMT_R5G6B5		= 0x04,
	PIX_FMT_X8R8G8B8	= 0x05,
	PIX_FMT_A8R8G8B8	= 0x06,
	PIX_FMT_YUY2		= 0x07,
	PIX_FMT_UYVY		= 0x08,
	PIX_FMT_INDEX8		= 0x09,
	PIX_FMT_MONOCHROME	= 0x0A,
	PIX_FMT_YV12		= 0x0F,
	PIX_FMT_A8		= 0x10,
	PIX_FMT_NV12		= 0x11,
	PIX_FMT_NV16		= 0x12,
	PIX_FMT_RG16		= 0x13,
	PIX_FMT_R8		= 0x14,
	PIX_FMT_NV12_10BIT	= 0x15,
	PIX_FMT_A2R10G10B10	= 0x16,
	PIX_FMT_NV16_10BIT	= 0x17,
};

#define PIX_FMT_NONE   0

#define CURSOR_FMT_DISALBE   0
#define CURSOR_FMT_MASKED    1
#define CURSOR_FMT_A8R8G8B8  2
#define CURSOR_SIZE 32

#define REG_CLK_CLKDIV0 0x2C

//#undef DEBUG

//#define DEBUG_MSG

#if 0
/*module param*/
static uint irq = 0;
module_param(irq, uint, 0644);
static ulong reg_base = 0;
module_param(reg_base, ulong, 0644);
static uint reg_size = 64 << 10;
module_param(reg_size, uint, 0644);

#else
static ulong reg_base = 0;
static uint reg_size;
#endif

static void __iomem *reg_virtual;
static dma_addr_t framebuffer_phys = 0;
static dma_addr_t overlay_phys = 0;
static wait_queue_head_t vsync_wait;
static struct fb_info *overlay_info = NULL;
static u32 dst_global_alpha_mode = 0;
static u32 dst_global_alpha_value = 0;
static void *cursor_data = NULL;
static u32 gu32_FB_SIZE = 0;
u32 gu32_OverlayEn, gu32_OverlayWidth, gu32_OverlayHeight, gu32_bufferCnt;
u32 gu32_ColorkeyEn;
u32 gu32_fb_format, gu32_sync_active, gu32_pix_swizzle, gu32_dpiConfig;
bool ResetOffsetFlag;
static u8 gu8_fb0_open_cnt, gu8_fb1_open_cnt;
ulong gulpixclkHz;
const uint32_t DivideFactor[] = {2, 4, 6, 8, 10, 12, 14, 16};
//static int  _underflow_cnt = 0;

/* ToDo */
typedef struct _dc_last_overlay_scale_info {
	unsigned int lastverfiltertap;
	unsigned int lasthorfiltertap;
	unsigned int lastheight;
	unsigned int lastwidth;
	unsigned int lastrectx;
	unsigned int lastrecty;
}
dc_last_overlay_scale_info;

struct ultrafb_info {
	struct device	*dev;
	struct clk	*dcuclk;
	struct clk	*pixclock;
	u8 clk_div;
	struct fb_info	*info;
	void __iomem	*reg_virtual;
	struct reset_control	*reset;
	//struct gpio_desc *bl_gpio;
	uint irq;
	spinlock_t lock;

	dma_addr_t fb_start_dma;
	dc_tile_mode tile_mode;

	bool enabled;
	u32 format;
	u32 pseudo_palette[16];

	/* cursor info. */
	u32 cursor_format;
	int cursor_x;
	int cursor_y;
	int cursor_hot_x;
	int cursor_hot_y;
	dma_addr_t cursor_start_dma;
	void *cursor_virtual;

	/* frame buffer info. */
	dma_addr_t fb_current_dma;
	dc_frame_info framebuffersize;

	/* overlay info. */
	dma_addr_t overlay_current_dma;
	dc_overlay_rect overlayrect;
	dc_frame_info overlaysize;

	/* alpha blending info. */
	dc_global_alpha blending;
	dc_alpha_blending_mode mode;

	/* scale info. */
	dc_filter_tap filtertap;
	dc_sync_table synctable;

	/* gamma info. */
	dc_gamma_table gamma_table;

	/* dither info. */
	bool dither_enable;

	/* rotation info. */
	dc_rot_angle rotangle;

	/* colorkey info. */
	dc_color_key colorkey;

	volatile u32 vblank_count;

};

struct pix_fmt_info {
	int pix_fmt;
	int8_t bits_per_pixel;
	int8_t transp_offset;
	int8_t transp_length;
	int8_t red_offset;
	int8_t red_length;
	int8_t green_offset;
	int8_t green_length;
	int8_t blue_offset;
	int8_t blue_length;
};

static struct pix_fmt_info pix_fmt_xlate[] = {
	{PIX_FMT_X4R4G4B4, 16,  12, 4,  8,  4,  4, 4,  0, 4},
	{PIX_FMT_A4R4G4B4, 16,   0, 0,  8,  4,  4, 4,  0, 4},
	{PIX_FMT_X1R5G5B5, 16,   0, 0,  10, 5,  5, 5,  0, 5},
	{PIX_FMT_A1R5G5B5, 16,  15, 1,  10, 5,  5, 5,  0, 5},
	{PIX_FMT_R5G6B5,   16,   0, 0,  11, 5,  5, 6,  0, 5},
	{PIX_FMT_X8R8G8B8, 32,  24, 8,  16, 8,  8, 8,  0, 8},
	{PIX_FMT_A8R8G8B8, 32,   0, 0,  16, 8,  8, 8,  0, 8},
};

//#define DCFB_DEBUG 0

//#if (DCFB_DEBUG == 1)//DEBUG_MSG
#ifdef DEBUG_MSG
#define dcfb_print printk
#else
#define dcfb_print(...)
#endif


void write_register(uint32_t data, uint32_t addr)
{
	writel(data, reg_virtual + addr);
#if 0
	printk("Write [0x%08x] 0x%08x\n", addr, data);
#endif
}

uint32_t read_register(uint32_t addr)
{
	uint32_t data = 0;
	data = readl(reg_virtual + addr);

#if 0
	if (addr != INT_STATE) {
		printk("Read [0x%08x] 0x%08x\n", addr, data);
	}
#endif

	return data;
}

static int determine_best_pix_fmt(struct fb_var_screeninfo *var)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(pix_fmt_xlate); i++) {
		struct pix_fmt_info *info = &pix_fmt_xlate[i];

		if ((var->bits_per_pixel == info->bits_per_pixel) &&
		    (var->transp.offset == info->transp_offset) &&
		    (var->transp.length == info->transp_length) &&
		    (var->red.offset == info->red_offset) &&
		    (var->red.length == info->red_length) &&
		    (var->green.offset == info->green_offset) &&
		    (var->green.length == info->green_length) &&
		    (var->blue.offset == info->blue_offset) &&
		    (var->blue.length == info->blue_length)) {
			/* found format. */
			return info->pix_fmt;
		}
	}

	return 0;
}

static void set_pix_fmt(struct fb_var_screeninfo *var, int pix_fmt)
{
	size_t i;
	struct pix_fmt_info *info = NULL;

	info = &pix_fmt_xlate[PIX_FMT_NONE];
	for (i = 0; i < ARRAY_SIZE(pix_fmt_xlate); i++) {
		if (pix_fmt_xlate[i].pix_fmt == pix_fmt) {
			info = &pix_fmt_xlate[i];
			pr_debug("L:%d  pix_fmt_xlate:%ld  pix_fmt=%d\n", __LINE__, i, pix_fmt);
			break;
		}
	}

	var->bits_per_pixel = info->bits_per_pixel;
	var->transp.offset = info->transp_offset;
	var->transp.length = info->transp_length;
	var->red.offset = info->red_offset;
	var->red.length = info->red_length;
	var->green.offset = info->green_offset;
	var->green.length = info->green_length;
	var->blue.offset = info->blue_offset;
	var->blue.length = info->blue_length;
}

static int get_bpp_by_format(int pix_fmt)
{
	size_t i;
	struct pix_fmt_info *info = NULL;
	int bpp = 0;

	info = &pix_fmt_xlate[PIX_FMT_NONE];

	for (i = 0; i < ARRAY_SIZE(pix_fmt_xlate); i++) {
		if (pix_fmt_xlate[i].pix_fmt == pix_fmt) {
			info = &pix_fmt_xlate[i];
			pr_debug("L:%d pix_fmt_xlate:%ld pix_fmt=%d\n", __LINE__, i, pix_fmt);
			break;
		}
	}

	bpp = info->bits_per_pixel / 8;
	if (pix_fmt == PIX_FMT_UYVY)
		bpp = 2;
	if (pix_fmt == PIX_FMT_NV12)
		bpp = 1;

	return bpp;
}

static void set_mode(struct fb_var_screeninfo *var, struct fb_videomode *mode,
                     int pix_fmt)
{
	set_pix_fmt(var, pix_fmt);
	fb_videomode_to_var(var, mode);
	var->grayscale = 0;
}

static void set_graphics_start(struct fb_info *info, int xoffset, int yoffset)
{
	struct ultrafb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	int pixel_offset;

	pixel_offset = (yoffset * var->xres_virtual) + xoffset;

	fbi->fb_current_dma = info->fix.smem_start + (pixel_offset *
	                      (var->bits_per_pixel >> 3));
	dcfb_print("L:%d %s\nyoffset: %d ->  [ 0x%08llx ]\n", __LINE__, __FUNCTION__, yoffset,
	           fbi->fb_current_dma);
	pr_debug("ultrafb_Addr: [ 0x%08llx ] %d bpp: %d\n", fbi->fb_current_dma, yoffset, var->bits_per_pixel);
}

static void set_overlay_graphics_start(struct fb_info *info, int xoffset,
                                       int yoffset)
{
	struct ultrafb_info *fbi = info->par;
	struct fb_var_screeninfo *var = &info->var;
	int pixel_offset;

	pixel_offset = (yoffset * var->xres_virtual) + xoffset;
	fbi->overlay_current_dma = info->fix.smem_start + (pixel_offset *
	                           (var->bits_per_pixel >> 3));
	dcfb_print(" Overlay_Addr: [ 0x%08llx ] \n", fbi->overlay_current_dma);
	pr_debug("Overlay_Addr: [ 0x%08llx ] %d bpp: %d\n", fbi->overlay_current_dma, yoffset, var->bits_per_pixel);
}

//static int fb_set_var_one = 0;// 20230210 sc modify
static int ultrafb_open(struct fb_info *info, int user)
{
	struct ultrafb_info *fbi = info->par;

	gu8_fb0_open_cnt++;
	dcfb_print("L:%d %s -> fb0_open_cnt=%d, fb1_open_cnt=%d\n", __LINE__,
	       __FUNCTION__, gu8_fb0_open_cnt, gu8_fb1_open_cnt);

#if 0// 20230210 sc modify, set if 1
	if(fb_set_var_one==0) {
		fb_set_var(info, &info->var);
		fb_set_var_one = 1;
	}
#endif

	if (gu8_fb0_open_cnt == 1)
	{
		dcfb_print("******** ultrafb_open: clock enable   ********\n");
		fbi->enabled = true;
		clk_prepare_enable(fbi->dcuclk);
	}

	info->var.yoffset = 0;
	return 0;
}

static int ultrafb_check_var(struct fb_var_screeninfo *var,
                             struct fb_info *info)
{
	struct ultrafb_info *fbi = info->par;
	struct device *dev = fbi->dev;
	int pix_fmt;

	if(fbi->format < PIX_FMT_YUY2) { // only for RGB

		pix_fmt = determine_best_pix_fmt(var);
		if (!pix_fmt) {
			dev_err(dev, "unsupport pixel format\n");
			return -EINVAL;
		}
		fbi->format = pix_fmt;
	}

	/*
	 * Basic geometry sanity checks.
	 */
	if (var->xres_virtual > var->xres) {
		dev_err(dev, "unsupport stride\n");
		return -EINVAL;
	}

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	/*
	 * Check size of framebuffer.
	 */
	if (var->xres_virtual * var->yres_virtual * (var->bits_per_pixel >> 3)
	    > info->fix.smem_len) {
		dev_err(dev, "invalid framebuffer size requirement\n");
		return -EINVAL;
	}

	set_graphics_start(info, var->xoffset, var->yoffset);
	return 0;
}

static int ultrafb_set_par(struct fb_info *info)
{
	/* config dc register */
	uint32_t h_end, h_total, hsync_start, hsync_end;
	uint32_t v_end, v_total, vsync_start, vsync_end;
	struct fb_var_screeninfo *var = &info->var;
	struct ultrafb_info *fbi = info->par; 
	uint32_t data;
	int j = 0;

	info->fix.line_length = var->xres_virtual * var->bits_per_pixel / 8;
	info->fix.ypanstep = var->yres;
	//dcfb_print(" line_length: %d  bits_per_pixel:%d\n", info->fix.line_length, var->bits_per_pixel);

	/* Get the timing from var info */
	h_end = var->xres;
	h_total = var->xres + var->left_margin + var->right_margin + var->hsync_len;
	hsync_start = var->xres + var->right_margin;
	hsync_end = hsync_start + var->hsync_len;
	//dcfb_print(" HDISPLAY: (%d, %d, %d, %d)\n", h_end, h_total, hsync_start, hsync_end);

	v_end = var->yres;
	v_total = var->yres + var->upper_margin + var->lower_margin + var->vsync_len;
	vsync_start = var->yres + var->lower_margin;
	vsync_end = vsync_start + var->vsync_len;
	dcfb_print(" VDISPLAY: (%d, %d, %d, %d)\n", v_end, v_total, vsync_start, vsync_end);

	data = (h_total << 16) | h_end;
	write_register(data, HDISPLAY);
	dcfb_print(" 0x1430:[0x%08x]\n", read_register(HDISPLAY));

	if(gu32_sync_active & DISPLAY_FLAGS_HSYNC_LOW)
		data = (1 << 30) | (hsync_end << 15) | (hsync_start);
	else
		data = (1 << 31) | (1 << 30) | (hsync_end << 15) | (hsync_start);

	write_register(data, HSYNC);
	dcfb_print(" 0x1438:[0x%08x]\n", read_register(HSYNC));

	data = (v_total << 16) | v_end;
	write_register(data, VDISPLAY);
	dcfb_print(" 0x1440:[0x%08x]\n", read_register(VDISPLAY));

	if(gu32_sync_active & DISPLAY_FLAGS_VSYNC_LOW)
		data = (1 << 30) | (vsync_end << 15) | (vsync_start);
	else
		data = (1 << 31) | (1 << 30) | (vsync_end << 15) | (vsync_start);

	write_register(data, VSYNC);
        dcfb_print(" 0x1448:[0x%08x]\n", read_register(VSYNC));

	write_register(0x00000080, DBI_CONFIG);

	//write_register(0x00000005, DPI_CONFIG);
	write_register(gu32_dpiConfig, DPI_CONFIG);
	dcfb_print("\tDPI_CONFIG(0x14b8) =0x%08x\n", read_register(DPI_CONFIG));

	data = (1 << 0) | (0 << 1) | (1 << 4) | (0 << 5) | (1 << 8) | (0 << 9);
	write_register(data, PANEL_CONFIG);
	dcfb_print("PANEL_CONFIG(0x1418) =0x%08x\n", read_register(PANEL_CONFIG));

	data = 0x0;
	write_register(data, FRAMEBUFFER_UPLANAR_ADDRESS);

	data = 0x0;
	write_register(data, FRAMEBUFFER_VPLANAR_ADDRESS);

	data = 0x0;
	write_register(data, FRAMEBUFFER_USTRIDE);

	data = 0x0;
	write_register(data, FRAMEBUFFER_VSTRIDE);

	write_register(0, INDEXCOLOR_TABLEINDEX);

	data = (fbi->framebuffersize.height << 15) | (fbi->framebuffersize.width);
	write_register(data, FRAMEBUFFER_SIZE);

	if ((var->xres != fbi->framebuffersize.width)
	    || (var->yres != fbi->framebuffersize.height)) {
		data = ((fbi->framebuffersize.width - 1) << 16) / (var->xres - 1);
		write_register(data, FRAMEBUFFER_SCALEFACTORX);

		data = ((fbi->framebuffersize.height - 1) << 16) / (var->yres - 1);
		if (data > (3 << 16)) {
			data = (3 << 16);
		}
		write_register(data, FRAMEBUFFER_SCALEFACTORY);

		write_register(0, HORIFILTER_KERNELINDEX);

		for (j = 0; j < 128; j++) {
			data = fbi->synctable.horkernel[j];
			write_register(data, HORIFILTER_KERNEL);
		}

		write_register(0, VERTIFILTER_KERNELINDEX);

		for (j = 0; j < 128; j++) {
			data = fbi->synctable.verkernel[j];
			write_register(data, VERTIFILTER_KERNEL);
		}

		data = (0x8000 << 0) | (0x8000 << 16);
		write_register(data, FRAMEBUFFER_INITIALOFFSET);

		data = (fbi->filtertap.vertical_filter_tap << 0) |
		       (fbi->filtertap.horizontal_filter_tap << 4);
		write_register(data, FRAMEBUFFER_SCALEFCONFIG);
	}

	data = fbi->colorkey.colorkey_low;
	write_register(data, FRAMEBUFFER_COLORKEY);

	data = fbi->colorkey.colorkey_high;
	write_register(data, FRAMEBUFFER_COLORHIGHKEY);

	data = fbi->colorkey.bg_color;
	write_register(data, FRAMEBUFFER_BGCOLOR);

	data = 0x0;
	write_register(data, FRAMEBUFFER_CLEARVALUE);

	data = 1 << 0;
	write_register(data, DISPLAY_INTRENABLE);
	//dcfb_print("DISPLAY_INTRENABLE(0x1480) =0x%08x\n", read_register(DISPLAY_INTRENABLE));

	write_register(fbi->framebuffersize.stride, FRAMEBUFFER_STRIDE);
	//dcfb_print("FRAMEBUFFER_STRIDE(0x1408) =0x%08x\n", read_register(FRAMEBUFFER_STRIDE));
	return 0;
}

static int ultrafb_pan_display(struct fb_var_screeninfo *var,
                               struct fb_info *info)
{
	uint32_t data, format;
	struct ultrafb_info *fbi = info->par;
	int pixel_byte = 0;

	pr_debug("L:%d\t%s\n", __LINE__, __FUNCTION__);
	if (!fbi->enabled)
		return 0;

	pixel_byte = get_bpp_by_format(fbi->format);
	format = fbi->format;
	set_graphics_start(info, var->xoffset, var->yoffset);
	write_register(fbi->fb_current_dma, FRAMEBUFFER_ADDRESS);

	if(fbi->format == PIX_FMT_NV12) {
		data = read_register(FRAMEBUFFER_ADDRESS) + (var->xres * var->yres);
		write_register(data, FRAMEBUFFER_UPLANAR_ADDRESS);
		dcfb_print("UPLANAR_ADDRESS(0x1530) =0x%08x\n",
		           read_register(FRAMEBUFFER_UPLANAR_ADDRESS));
		data = var->xres;
		write_register(data, FRAMEBUFFER_USTRIDE);
		dcfb_print("USTRIDE(0x1800) =0x%08x\n", read_register(FRAMEBUFFER_USTRIDE));
	}
	dcfb_print(" >>>>> yoffset: %d, 0x1400: ( 0x%08x )\n", var->yoffset, 
	           read_register(FRAMEBUFFER_ADDRESS));


	data = (1 << 0) | (fbi->gamma_table.gamma_enable << 2) |(1 << 4) | (0 << 8) |
	       (fbi->colorkey.enable << 9) |
	       (fbi->rotangle << 11) | (fbi->tile_mode << 17) | (gu32_pix_swizzle << 23) |
	       (/*fbi->format*/format << 26) | (1 << 14);//cfli 0:argb -> 2:abgr

	write_register(data, FRAMEBUFFER_CONFIG);
	pr_debug("========= L:%d swizzle: %d FRAMEBUFFER_CONFIG(0x1518)=0x%08x\n", __LINE__,
		           gu32_pix_swizzle, read_register(FRAMEBUFFER_CONFIG));

	//var->yoffset = var->yoffset + var->yres;//cfli >>>> start value is 600
	//dcfb_print(">>>>> var->yoffset = 0x%x(%d)\n", var->yoffset, var->yoffset);
	if (var->yoffset > info->var.yres_virtual - var->yres) {
		var->yoffset = 0;
	}
	//fbi->gamma_table.gamma_enable = 0;
	//fbi->colorkey.enable = 0;
	//fbi->rotangle = DC_ROT_ANGLE_ROT0;

	return 0;
}

static int ultrafb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	uint32_t x, y, format;
	uint32_t data;
	int set = cursor->set;
	struct ultrafb_info *fbi = info->par;

	dcfb_print("L:%d %s\n", __LINE__, __FUNCTION__);

	if (cursor->image.width > CURSOR_SIZE || cursor->image.height > CURSOR_SIZE)
		return -EINVAL;

	write_register(fbi->cursor_start_dma, CURSOR_ADDRESS);

	if (set & FB_CUR_SETPOS) {
		y = cursor->image.dy;
		x = cursor->image.dx;

		data = (y << 16) | x;
		write_register(data, CURSOR_LOCATION);
	}

	/*
	 * FB_CUR_SETIMAGE - the cursor image has changed
	 * FB_CUR_SETCMAP  - the cursor colors has changed
	 * FB_CUR_SETSHAPE - the cursor bitmask has changed
	 */
	if (cursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETCMAP | FB_CUR_SETIMAGE)) {
		uint32_t image_size;
		uint32_t bg, fg;

		if (info->state != FBINFO_STATE_RUNNING)
			return 0;

		bg = cursor->image.bg_color;
		fg = cursor->image.fg_color;

		write_register(bg, CURSOR_BACKGROUND);
		write_register(fg, CURSOR_FOREGROUND);

		/* Use 32-bit operations on the data to improve performance */
		image_size = cursor->image.width * cursor->image.height;
		memcpy(fbi->cursor_virtual, (uint32_t *)cursor->image.data, image_size * 4);
	}

	if (cursor->enable) {
		/* Default ARGB8888 format */
		format = CURSOR_FMT_A8R8G8B8;
		data = format | (1 << 2) | (0 << 4) | (0 << 8) | (0 << 16);
		write_register(data, CURSOR_CONFIG);
	} else {
		data = (0 << 0) | (1 << 2) | (0 << 4) | (0 << 8) | (0 << 16);
		write_register(data, CURSOR_CONFIG);
	}

	return 0;
}

static int ultrafb_gamma(struct ultrafb_info *fbi)
{
	int i = 0;
	u32 data;

	dcfb_print("L:%d %s\n", __LINE__, __FUNCTION__);
	for (i = 0; i < GAMMA_INDEX_MAX; i++) {
		data = i;
		write_register(data, GAMMA_INDEX);

		data = (fbi->gamma_table.gamma[i][2] << 0) | (fbi->gamma_table.gamma[i][1] <<
		        10) |
		       (fbi->gamma_table.gamma[i][0] << 20);
		write_register(data, GAMMA_DATA);
	}
	return 0;
}

static int ultrafb_dither(struct ultrafb_info *fbi)
{
	u32 data;

	dcfb_print("L:%d %s\n", __LINE__, __FUNCTION__);
	if (fbi->dither_enable) {
		write_register(0x7B48F3C0, DISPLAY_DITHER_TABLE_LOW);
		write_register(0x596AD1E2, DISPLAY_DITHER_TABLE_HIGH);

		data = fbi->dither_enable << 31;
		write_register(data, DISPLAY_DITHER_CONFIG);
	} else {
		write_register(0, DISPLAY_DITHER_TABLE_LOW);
		write_register(0, DISPLAY_DITHER_TABLE_HIGH);
		write_register(0, DISPLAY_DITHER_CONFIG);
	}
	return 0;
}

static int ultrafb_blank(int blank, struct fb_info *info)
{
	u32 data, _new;

	data = read_register(FRAMEBUFFER_CONFIG);
	if (blank)
		_new = data & ~(1 << 0);
	else
		_new = data | (1 << 0);
	if (_new == data)
		return 0;

	write_register(_new, FRAMEBUFFER_CONFIG);
	pr_debug("========= L:%d FRAMEBUFFER_CONFIG(0x1518):0x%08x\n", __LINE__, read_register(FRAMEBUFFER_CONFIG));
	return 0;
}

static int ultrafb_output(struct ultrafb_info *fbi, u32 enable)
{
	u32 data;

	data = read_register(PANEL_CONFIG);
	if (enable == 0)  // output disable
	{
		fbi->enabled = false;

		data = read_register(PANEL_CONFIG);
		write_register(data & ~(1 << 8), PANEL_CONFIG);
		data = read_register(FRAMEBUFFER_CONFIG);
		write_register(data & ~(1 << 0), FRAMEBUFFER_CONFIG);	
		//dcfb_print("******** %d  enable:[ %d ] 0x%08x, 0x%08x   ********\n", enable, fbi->enabled, read_register(PANEL_CONFIG), read_register(FRAMEBUFFER_CONFIG));
	}
	else // output enable
	{
		fbi->enabled = true;

		data = read_register(PANEL_CONFIG);
		write_register(data |(1 << 8), PANEL_CONFIG);
		data = read_register(FRAMEBUFFER_CONFIG);
		write_register(data | (1 << 0), FRAMEBUFFER_CONFIG);
		//dcfb_print("******** %d  enable:[ %d ] 0x%08x, 0x%08x   ********\n", enable, fbi->enabled, read_register(PANEL_CONFIG), read_register(FRAMEBUFFER_CONFIG));
	}

	return 0;
}

static unsigned int chan_to_field(unsigned int chan, struct fb_bitfield *bf)
{
	return ((chan & 0xffff) >> (16 - bf->length)) << bf->offset;
}

static int ultrafb_setcolreg(unsigned int regno, unsigned int red,
                             unsigned int green,
                             unsigned int blue, unsigned int trans, struct fb_info *info)
{
	struct ultrafb_info *fbi = info->par;
	u32 val;

	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
		                      7471 * blue) >> 16;

	if (info->fix.visual == FB_VISUAL_TRUECOLOR && regno < 16) {
		val =  chan_to_field(red,   &info->var.red);
		val |= chan_to_field(green, &info->var.green);
		val |= chan_to_field(blue, &info->var.blue);
		fbi->pseudo_palette[regno] = val;
	}

	return 0;
}

static int ultrafb_mmap(struct fb_info *info, struct vm_area_struct * vma)
{
	unsigned long mmio_pgoff;
	unsigned long start;
	u32 len;
	int ret = 0;

	//dcfb_print("L:%d %s\n", __LINE__, __FUNCTION__);

#if 1
	ret = fb_set_var(info, &info->var);
	if (ret) {
		printk("unable to set display parameters\n");
		return ret;
	}
#endif

	start = info->fix.smem_start;
	len = info->fix.smem_len;
	mmio_pgoff = PAGE_ALIGN((start & ~PAGE_MASK) + len) >> PAGE_SHIFT;
#if 0
	if (vma->vm_pgoff >= mmio_pgoff) {
		if (info->var.accel_flags) {
			//mutex_unlock(&info->mm_lock);
			return -EINVAL;
		}
		vma->vm_pgoff -= mmio_pgoff;
		start = info->fix.mmio_start;
		len = info->fix.mmio_len;
	}
#endif
	dcfb_print("dcultrafb: mmap framebuffer [0x%lx  %d]  [0x%lx  %d]",
	           info->fix.smem_start, info->fix.smem_len,
	           info->fix.mmio_start, info->fix.mmio_len);

	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
	vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	dcfb_print("dcultrafb: mmap framebuffer %lx %d P(%lx)->V(%lx)\n",
	           start, len,
	           info->fix.smem_start + (vma->vm_pgoff << PAGE_SHIFT),
		   vma->vm_start);

	write_register(start, FRAMEBUFFER_ADDRESS);
	return vm_iomap_memory(vma, start, len);
}

static int ultrafb_ioctl(struct fb_info *info, unsigned int cmd,
                         unsigned long argp)
{
	int ret, j;
	u32 vblank_count, data, format;
	dc_frame_info size;
	dc_filter_tap filtertap;
	dc_sync_table table;
	struct fb_cursor cursor;
	bool ditherenable;
	dc_rot_angle rotangle;
	dc_tile_mode tilemode;
	dc_color_key colorkey;
	struct ultrafb_info *fbi = info->par;
	void __user *buf = (void __user *)argp;

	dcfb_print("\nL:%d %s: [ 0x%04x ]\n", __LINE__, __FUNCTION__, cmd);

	switch (cmd) {
	case ULTRAFBIO_CURSOR:
		//dcfb_print("(01):0x%04x\n", ULTRAFBIO_CURSOR);
		if (copy_from_user(&cursor, (void *)argp, sizeof(cursor)))
			return -EFAULT;

		cursor_data = kmalloc(CURSOR_SIZE * CURSOR_SIZE * 4, GFP_KERNEL);
		if (copy_from_user(cursor_data, (void *)cursor.image.data,
		                   CURSOR_SIZE * CURSOR_SIZE * 4))
			return -EFAULT;
		cursor.image.data = (char *)cursor_data;

		ret = ultrafb_cursor(info, &cursor);

		/* free buffer */
		kfree(cursor_data);
		return ret;
	case FBIO_WAITFORVSYNC:
		//dcfb_print("(02):0x%04x\n", FBIO_WAITFORVSYNC);
		vblank_count = fbi->vblank_count;
		dcfb_print("\t---> IN  vblank_cnt = %d\n", vblank_count);
		ret = wait_event_interruptible_timeout(vsync_wait,
		                                       vblank_count != fbi->vblank_count, HZ /10);
		dcfb_print("\tvblank_cnt = %d ---> OUT\n", vblank_count);
		if(ret == 0)
			return -ETIMEDOUT;
		return 0;
	case ULTRAFBIO_BUFFER_SIZE:
		//dcfb_print("(03):0x%04x\n", ULTRAFBIO_BUFFER_SIZE);
		if (copy_from_user(&size, (void *)argp, sizeof(size)))
			return -EFAULT;
		dcfb_print("BUFFER width = %d, height = %d, stride = %d\n", size.width,
		           size.height, size.stride);

		fbi->framebuffersize.width = size.width;
		fbi->framebuffersize.height = size.height;
		fbi->framebuffersize.stride = size.stride;
		return 0;
	case ULTRAFBIO_SCALE_FILTER_TAP:
		if (copy_from_user(&filtertap, (void *)argp, sizeof(filtertap)))
			return -EFAULT;

		fbi->filtertap.horizontal_filter_tap = filtertap.horizontal_filter_tap;
		fbi->filtertap.vertical_filter_tap = filtertap.vertical_filter_tap;
		return 0;
	case ULTRAFBIO_SYNC_TABLE:
		//dcfb_print("(05):0x%04x\n", ULTRAFBIO_SYNC_TABLE);
		if (copy_from_user(&table, (void *)argp, sizeof(table)))
			return -EFAULT;

		for (j = 0; j < 128; j++) {
			fbi->synctable.horkernel[j] = table.horkernel[j];
			fbi->synctable.verkernel[j] = table.verkernel[j];
		}
		return 0;
	case ULTRAFBIO_GAMMA:
		//dcfb_print("(06):0x%04x\n", ULTRAFBIO_GAMMA);
		if (copy_from_user(&fbi->gamma_table, (void *)argp, sizeof(dc_gamma_table)))
			return -EFAULT;

		dcfb_print("GAMMA enable = %d\n", fbi->gamma_table.gamma_enable);
		ret = ultrafb_gamma(fbi);
		return ret;
	case ULTRAFBIO_DITHER:
		//dcfb_print("(07):0x%04x\n", ULTRAFBIO_DITHER);
		if (copy_from_user(&ditherenable, (void *)argp, sizeof(ditherenable)))
			return -EFAULT;

		//dcfb_print("DITHER enable = %d\n", ditherenable);
		fbi->dither_enable = ditherenable;

		ret = ultrafb_dither(fbi);
		return ret;
	case ULTRAFBIO_ROTATION:
		//dcfb_print("(08):0x%04x\n", ULTRAFBIO_ROTATION);
		if (copy_from_user(&rotangle, (void *)argp, sizeof(rotangle)))
			return -EFAULT;
		fbi->rotangle = rotangle;
		return 0;
	case ULTRAFBIO_TILEMODE:
		if (copy_from_user(&tilemode, (void *)argp, sizeof(tilemode)))
			return -EFAULT;

		fbi->tile_mode = tilemode;
		return 0;
	case ULTRAFBIO_COLORKEY:
		//dcfb_print("(10):0x%04x\n", ULTRAFBIO_COLORKEY);
		if (copy_from_user(&colorkey, (void *)argp, sizeof(colorkey)))
			return -EFAULT;

		dcfb_print("COLORKEY enable = 0x%x, low = 0x%x, high = 0x%x, bg_color = 0x%x\n",
		    colorkey.enable, colorkey.colorkey_low, colorkey.colorkey_high, colorkey.bg_color);
		if (colorkey.enable)
			fbi->colorkey.enable = 0x2;
		else
			fbi->colorkey.enable = 0x0;

		fbi->colorkey.colorkey_low = colorkey.colorkey_low;
		fbi->colorkey.colorkey_high = colorkey.colorkey_high;
		fbi->colorkey.bg_color = colorkey.bg_color;
		return 0;
	case ULTRAFBIO_SET_FORMAT:
		//dcfb_print("(11):0x%04x\n", ULTRAFBIO_BUFFER_FORMAT);
		if (copy_from_user(&format, (void *)argp, sizeof(format)))
			return -EFAULT;
		fbi->format = format;
		//dcfb_print("IOCTL BUFFER format = %d\n", format);
		if(fbi->format == PIX_FMT_NV12) {
			data = read_register(FRAMEBUFFER_ADDRESS) + (fbi->framebuffersize.width *
			        fbi->framebuffersize.height);
			write_register(data, FRAMEBUFFER_UPLANAR_ADDRESS);
			//dcfb_print("IOCTL UPLANAR_ADDRESS(0x1530) =0x%08x\n", read_register(FRAMEBUFFER_UPLANAR_ADDRESS));
			data = fbi->framebuffersize.width;
			write_register(data, FRAMEBUFFER_USTRIDE);
			//dcfb_print("IOCTL USTRIDE(0x1800) =0x%08x\n", read_register(FRAMEBUFFER_USTRIDE));
		}
		return 0;
	case ULTRAFBIO_GET_FBADDR:
		dcfb_print("\nIOCTL Addr = 0x%08x\n", (u32)framebuffer_phys);
		if (copy_to_user(buf, &framebuffer_phys, sizeof(u32)))
			return -EFAULT;
		return 0;
	case ULTRAFBIO_GET_FORMAT:
		dcfb_print("\nIOCTL FORMAT = %d,  %d\n", gu32_fb_format, fbi->format);
		if (copy_to_user(buf, &gu32_fb_format, sizeof(u32)))
			return -EFAULT;
		return 0;

	case ULTRAFBIO_OUTPUT_OFF:
		dcfb_print("\nIOCTL OUTPUTOFF = %d, OUTPUT disable\n", gu32_OutputEnFlag);
		ultrafb_output(fbi, 0);
		return 0;

	case ULTRAFBIO_OUTPUT_ON:
		dcfb_print("\nIOCTL OUTPUTON = %d, OUTPUT enable\n", gu32_OutputEnFlag);
		ultrafb_output(fbi, 1);
		return 0;

	default:
		//dcfb_print("(11):default\n");
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int ultrafb_release(struct fb_info *info, int user)
{
	struct fb_var_screeninfo *var = &info->var;
	struct ultrafb_info *fbi = info->par;

	fbi->info = info;

	gu8_fb0_open_cnt--;
	dcfb_print("L:%d %s -> fb0_open_cnt=%d, fb1_open_cnt=%d\n", __LINE__,
	       __FUNCTION__, gu8_fb0_open_cnt, gu8_fb1_open_cnt);

	if ( (gu8_fb0_open_cnt == 0) && (ResetOffsetFlag == true))
	{
		dcfb_print("Reset var->yoffset\n");
		var->yoffset = 0;
		set_graphics_start(info, var->xoffset, var->yoffset);
		write_register(fbi->fb_current_dma, FRAMEBUFFER_ADDRESS);
	}

	if ( (gu8_fb0_open_cnt == 0) && (gu8_fb1_open_cnt == 0))
	{
		dcfb_print("******** ultrafb_release: overlay disable   ********\n");
		dcfb_print("******** ultrafb_release: clock   disable   ********\n");
		fbi->colorkey.enable = 0x0;
		fbi->enabled = false;
		write_register(read_register(FRAMEBUFFER_CONFIG) & ~(1 << 9), FRAMEBUFFER_CONFIG);
		write_register(read_register(OVERLAY_CONFIG) & ~(1 << 24), OVERLAY_CONFIG);
		clk_disable_unprepare(fbi->dcuclk);
	}

	return 0;
}

static struct fb_ops ultrafb_ops = {
	.owner = THIS_MODULE,
	.fb_open	= ultrafb_open,
	.fb_check_var = ultrafb_check_var,
	.fb_set_par = ultrafb_set_par,
	.fb_pan_display = ultrafb_pan_display,
	.fb_blank = ultrafb_blank,
	.fb_setcolreg = ultrafb_setcolreg,
	.fb_mmap = ultrafb_mmap,
	.fb_ioctl = ultrafb_ioctl,
	.fb_release = ultrafb_release,
};

static irqreturn_t ultrafb_handle_irq(int irq, void *dev_id)
{
	unsigned long irq_flags;
	struct ultrafb_info *fbi = dev_id;
	u32 status;

	spin_lock_irqsave(&fbi->lock, irq_flags);
#if 0
        if (read_register(FRAMEBUFFER_CONFIG) & 0x20)
                _underflow_cnt++;

        //if((fbi->vblank_count % 5000==0) && _underflow_cnt) {
	if(fbi->vblank_count % 500==0) {
		//printk("\t CFLi L:%d %s\n", __LINE__, __FUNCTION__);
                printk("     Underflow: 0x%08x, 0x%08x, [%d]\n", read_register(FRAMEBUFFER_CONFIG),read_register(OVERLAY_CONFIG), _underflow_cnt);
                //printk("OVERLAY_STRIDE: 0x%x, OVERLAY_TL: 0x%x, OVERLAY_BR: 0x%x\n", read_register(OVERLAY_STRIDE), read_register(OVERLAY_TL), read_register(OVERLAY_BR));
                _underflow_cnt = 0;
        }
#endif
	status = read_register(INT_STATE);
	if (status & 0x1) {
		fbi->vblank_count++;
		wake_up_interruptible(&vsync_wait);
	}
	spin_unlock_irqrestore(&fbi->lock, irq_flags);

	return status ? IRQ_HANDLED : IRQ_NONE;
}

static void set_overlay_var(struct fb_var_screeninfo *var,
                            struct fb_videomode *mode, int pix_fmt)
{
	set_pix_fmt(var, pix_fmt);

	var->xres = mode ->xres;
	var->yres = mode ->yres;
	var->xres_virtual = mode ->xres;
	var->yres_virtual = mode ->yres;
	var->xoffset = 0;
	var->yoffset = 0;
	var->height = -1;
	var->width = -1;
	var->vmode = FB_VMODE_NONINTERLACED;
	var->activate = FB_ACTIVATE_FORCE;
}

static int overlay_open(struct fb_info *info, int user)
{
	struct ultrafb_info *fbi = info->par;
	u32 data;

	dcfb_print("\n\nL:%d %s -> fb0_open_cnt=%d, fb1_open_cnt=%d\n", __LINE__,
	       __FUNCTION__, gu8_fb0_open_cnt, gu8_fb1_open_cnt);

	dcfb_print("\nL:%d dcu_clk @%ld Hz--------\n", __LINE__,
	       clk_get_rate(fbi->dcuclk));
	dcfb_print("L:%d dcup_clk@%ld Hz--------\n\n", __LINE__,
	       clk_get_rate(fbi->pixclock));

	gu8_fb0_open_cnt++;
	gu8_fb1_open_cnt++;
	dcfb_print("L:%d %s -> fb0_open_cnt=%d, fb1_open_cnt=%d\n", __LINE__,
	       __FUNCTION__, gu8_fb0_open_cnt, gu8_fb1_open_cnt);

	if ( (gu8_fb0_open_cnt == 1) && (gu8_fb1_open_cnt == 1))
	{
		dcfb_print("******** overlay_open: clock enable   ********\n");
		fbi->enabled = true;
		clk_prepare_enable(fbi->dcuclk);
	}
	if(gu32_OverlayEn)
		data = (read_register(OVERLAY_CONFIG) & ~(1 << 24)) | (1 << 24);
	else
		data = (read_register(OVERLAY_CONFIG) & ~(1 << 24)) ;

	if (gu8_fb1_open_cnt) // enable overlay
	{
		dcfb_print("******** overlay_open: overlay enable ********\n");
		fbi->enabled = true;
		write_register(data, OVERLAY_CONFIG);
	}

	if (gu32_ColorkeyEn)
		fbi->colorkey.enable = 0x2;
	else
		fbi->colorkey.enable = 0x0;
	//dcfb_print("L:%d %s colorkey = 0x%x\n", __LINE__, __FUNCTION__, fbi->colorkey.enable);
	data = read_register(OVERLAY_CONFIG) | fbi->colorkey.enable;
	write_register(data, OVERLAY_CONFIG);

	dcfb_print("L:%d %s OVERLAY_CONFIG = [ 0x%08x ], 0x%08x\n\n", __LINE__,
	       __FUNCTION__, data, read_register(OVERLAY_CONFIG));

	return 0;
}

static int overlay_check_var(struct fb_var_screeninfo *var,
                             struct fb_info *info)
{
	struct ultrafb_info *fbi = info->par;
	struct device *dev = fbi->dev;

	/*
	 * Basic geometry sanity checks.
	 */
	if (var->xres_virtual > var->xres) {
		dev_err(dev, "unsupport overlay stride\n");
		return -EINVAL;
	}

	if (var->xres_virtual < var->xres)
		var->xres_virtual = var->xres;
	if (var->yres_virtual < var->yres)
		var->yres_virtual = var->yres;

	/*
	 * Check size of framebuffer.
	 */
	if (var->xres_virtual * var->yres_virtual * (var->bits_per_pixel >> 3)
	    > info->fix.smem_len) {
		dev_err(dev, "invalid overlay size requirement\n");
		return -EINVAL;
	}

	set_overlay_graphics_start(info, var->xoffset, var->yoffset);
	return 0;
}

static int overlay_set_par(struct fb_info *info)
{
	/* config overlay register */
	struct ultrafb_info *fbi = info->par;
	uint32_t data, format;
	uint32_t rectx, recty;

	rectx = fbi->overlayrect.brx - fbi->overlayrect.tlx;
	recty = fbi->overlayrect.bry - fbi->overlayrect.tly;
	dcfb_print("L:%d overlay Rect: %dx%d (%d, %d) (%d, %d)\n", __LINE__,
	           rectx, recty, fbi->overlayrect.tlx, fbi->overlayrect.tly,
	           fbi->overlayrect.brx, fbi->overlayrect.bry);

	data = fbi->overlaysize.stride;
	write_register(data, OVERLAY_STRIDE);

	format = fbi->overlaysize.format;
	data = (format << 16) | (0 << 2) | (0 << 13) |
	        (0 << 0) | (0 << 25);

	write_register(data, OVERLAY_CONFIG);
	write_register(fbi->overlay_current_dma, OVERLAY_ADDRESS);

	dcfb_print("L:%d OVERLAY_STRIDE(0x1600) = %d\n", __LINE__,
	           read_register(OVERLAY_STRIDE));
	dcfb_print("L:%d OVERLAY_CONFIG(0x1540) = 0x%08x\n", __LINE__,
	           read_register(OVERLAY_CONFIG));

	data = 0;
	write_register(data, OVERLAY_USTRIDE);

	data = 0;
	write_register(data, OVERLAY_VSTRIDE);

	data = (fbi->overlayrect.tlx << 0) | (fbi->overlayrect.tly << 15);
	write_register(data, OVERLAY_TL);

	data = (fbi->overlayrect.brx << 0) | (fbi->overlayrect.bry << 15);
	write_register(data, OVERLAY_BR);


	switch(fbi->mode) {
	case DC_BLEND_MODE_CLEAR:
		data = (0 << 0) | (fbi->blending.global_alpha_mode << 3) | (0 << 5) | (0 << 8) |
		       (0 << 9) | (dst_global_alpha_mode << 10) | (0 << 12) | (0 << 15);
		break;
	case DC_BLEND_MODE_SRC:
		data = (0 << 0) | (fbi->blending.global_alpha_mode << 3) | (1 << 5) | (0 << 8) |
		       (0 << 9) | (dst_global_alpha_mode << 10) | (0 << 12) | (0 << 15);
		break;
	case DC_BLEND_MODE_DST:
		data = (0 << 0) | (fbi->blending.global_alpha_mode << 3) | (0 << 5) | (0 << 8) |
		       (0 << 9) | (dst_global_alpha_mode << 10) | (1 << 12) | (0 << 15);
		break;
	case DC_BLEND_MODE_SRC_OVER:
		data = (0 << 0) | (fbi->blending.global_alpha_mode << 3) | (1 << 5) | (0 << 8) |
		       (0 << 9) | (dst_global_alpha_mode << 10) | (3 << 12) | (0 << 15);
		break;
	case DC_BLEND_MODE_DST_OVER:
		data = (0 << 0) | (fbi->blending.global_alpha_mode << 3) | (3 << 5) | (0 << 8) |
		       (0 << 9) | (dst_global_alpha_mode << 10) | (1 << 12) | (0 << 15);
		break;
	case DC_BLEND_MODE_SRC_IN:
		data = (0 << 0) | (fbi->blending.global_alpha_mode << 3) | (2 << 5) | (0 << 8) |
		       (0 << 9) | (dst_global_alpha_mode << 10) | (0 << 12) | (0 << 15);
		break;
	case DC_BLEND_MODE_DST_IN:
		data = (0 << 0) | (fbi->blending.global_alpha_mode << 3) | (0 << 5) | (0 << 8) |
		       (0 << 9) | (dst_global_alpha_mode << 10) | (2 << 12) | (0 << 15);
		break;
	case DC_BLEND_MODE_SRC_OUT:
		data = (0 << 0) | (fbi->blending.global_alpha_mode << 3) | (3 << 5) | (0 << 8) |
		       (0 << 9) | (dst_global_alpha_mode << 10) | (0 << 12) | (0 << 15);
		break;
	}
	write_register(data, OVERLAY_ALPHA_BLEND_CONFIG);
	dcfb_print(" OVERLAY_ALPHA_BLEND_CONFIG(0x1580) = 0x%08x\n", read_register(OVERLAY_ALPHA_BLEND_CONFIG));

	data = fbi->blending.global_alpha_value;
	write_register(data, OVERLAY_SRC_GLOBAL_COLOR);

	data = dst_global_alpha_value;
	write_register(data, OVERLAY_DST_GLOBAL_COLOR);

	write_register(0, OVERLAY_CLEAR_VALUE);

	data = (fbi->overlaysize.height << 15) | (fbi->overlaysize.width);
	write_register(data, OVERLAY_SIZE);

	data = fbi->colorkey.colorkey_low;
	write_register(data, OVERLAY_COLOR_KEY);

	data = fbi->colorkey.colorkey_high;
	write_register(data, OVERLAY_COLOR_KEY_HIGH);

	write_register(0, OVERLAY_UPLANAR_ADDRESS);

	write_register(0, OVERLAY_VPLANAR_ADDRESS);

#if 0
	if ((rectx != fbi->overlaysize.width) || (recty != fbi->overlaysize.height)) {
		data = ((fbi->overlaysize.width - 1) << 16) / (rectx - 1);
		write_register(data, OVERLAY_SCALE_FACTOR_X);

		data = ((fbi->overlaysize.height - 1) << 16) / (recty - 1);
		if (data > (3 << 16)) {
			data = (3 << 16);
		}
		write_register(data, OVERLAY_SCALE_FACTOR_Y);

		write_register(0, OVERLAY_HORI_FILTER_KERNEL_INDEX);

		for (j = 0; j < 128; j++) {
			data = fbi->synctable.horkernel[j];
			write_register(data, OVERLAY_HORI_FILTER_KERNEL);
		}

		write_register(0, OVERLAY_VERTI_FILTER_KERNEL_INDEX);

		for (j = 0; j < 128; j++) {
			data = fbi->synctable.verkernel[j];
			write_register(data, OVERLAY_VERTI_FILTER_KERNEL);
		}

		data = (0x8000 << 0) | (0x8000 << 16);
		write_register(data, OVERLAY_INITIAL_OFFSET);

		data = (fbi->filtertap.vertical_filter_tap << 0) |
		       (fbi->filtertap.horizontal_filter_tap << 4) | (1 << 8);
		write_register(data, OVERLAY_SCALE_CONFIG);
	} else
		write_register(0, OVERLAY_SCALE_CONFIG);
#endif

	return 0;
}

static int overlay_pan_display(struct fb_var_screeninfo *var,
                               struct fb_info *info)
{
	uint32_t data, format;
	struct ultrafb_info *fbi = info->par;

	if (!fbi->enabled)
		return 0;
	set_overlay_graphics_start(info, var->xoffset, var->yoffset);

	write_register(fbi->overlay_current_dma, OVERLAY_ADDRESS);
	dcfb_print(" L:%d OVERLAY_ADDRESS(0x15c0)=0x%08x\n", __LINE__,
	           read_register(OVERLAY_ADDRESS));

	format = fbi->overlaysize.format;
	if(fbi->format == PIX_FMT_NV12) {
		data = read_register(OVERLAY_ADDRESS) + (var->xres * var->yres);
		write_register(data, OVERLAY_UPLANAR_ADDRESS);
		dcfb_print(" L:%d OVERLAY_UPLANAR_ADDRESS(0x1840) =0x%08x\n",
		           __LINE__, read_register(FRAMEBUFFER_UPLANAR_ADDRESS));
		data = var->xres;
		write_register(data, OVERLAY_USTRIDE);
		dcfb_print(" L:%d OVERLAY_USTRIDE(0x18C0) =0x%08x\n",
		           __LINE__, read_register(OVERLAY_USTRIDE));
	}

	if (((fbi->rotangle == DC_ROT_ANGLE_ROT90)
	     || (fbi->rotangle == DC_ROT_ANGLE_ROT270))
	    && (fbi->tile_mode != DC_TILE_MODE_LINEAR)) {
		return -EINVAL;
	} else {
		if (gu32_ColorkeyEn && (fbi->overlaysize.format == 0x6))
		{
			fbi->colorkey.enable = 0x2;
			gu32_ColorkeyEn = 1;
		}
		else
		{
			fbi->colorkey.enable = 0x0;
			gu32_ColorkeyEn = 0;
		}

		data = (fbi->colorkey.enable) | (format << 16) | (1 << 24) |
		        (0 << 13) | (0 << 25) | (fbi->tile_mode << 8);
		       
		write_register(data, OVERLAY_CONFIG);
		dcfb_print(" L:%d colorkeyEn = %d, OVERLAY_CONFIG (0x1540)=0x%08x\n", __LINE__,
		           fbi->colorkey.enable, read_register(OVERLAY_CONFIG));
	}

	var->yoffset = var->yoffset + var->yres; //cfli
	dcfb_print(" L:%d  >>>>> var->yoffset = %d\n",
	           __LINE__, var->yoffset);
	if (var->yoffset > info->var.yres_virtual - var->yres) {
		var->yoffset = 0;
	}

	//fbi->colorkey.enable = 0;
	//fbi->rotangle = DC_ROT_ANGLE_ROT0;
	//fbi->mode = DC_BLEND_MODE_SRC;

	return 0;
}

static int overlay_ioctl(struct fb_info *info, unsigned int cmd,
                         unsigned long argp)
{
	int j;
	dc_global_alpha alpha;
	dc_frame_info size;
	dc_overlay_rect coordinate;
	struct ultrafb_info *fbi = info->par;
	dc_alpha_blending_mode mode;
	dc_filter_tap filtertap;
	dc_sync_table table;
	dc_rot_angle rotangle;
	dc_tile_mode tilemode;
	dc_color_key colorkey;
	void __user *buf = (void __user *)argp;

	dcfb_print("\n\nL:%d %s overlay cmd = 0x%08x\n\n", __LINE__, __FUNCTION__, cmd);

	switch (cmd) {
	case ULTRAFBIO_BUFFER_SIZE:
		if (copy_from_user(&size, (void *)argp, sizeof(size)))
			return -EFAULT;

		dcfb_print("\noverlay_IOCTL overlaysize: %dx%d  stride = %d\n", size.width,
		       size.height, size.stride);
		fbi->overlaysize.width = size.width;
		fbi->overlaysize.height = size.height;
		fbi->overlaysize.stride = size.stride;
		return 0;
	case ULTRAFBIO_OVERLAY_RECT:
		if (copy_from_user(&coordinate, (void *)argp, sizeof(coordinate)))
			return -EFAULT;

		//dcfb_print("tlx = %d, tly = %d, brx = %d, bry = %d\n", coordinate.tlx, coordinate.tly, coordinate.brx, coordinate.bry);
		fbi->overlayrect.tlx = coordinate.tlx;
		fbi->overlayrect.tly = coordinate.tly;
		fbi->overlayrect.brx = coordinate.brx;
		fbi->overlayrect.bry = coordinate.bry;
		dcfb_print("\noverlay_IOCTL Rect: tl (%d, %d) br(%d, %d)\n",
		       fbi->overlayrect.tlx, fbi->overlayrect.tly, fbi->overlayrect.brx,
		       fbi->overlayrect.bry);
		return 0;
	case ULTRAFBIO_BLENDING_MODE:
		if (copy_from_user(&mode, (void *)argp, sizeof(mode)))
			return -EFAULT;

		//dcfb_print("mode = 0x%08x\n", mode);
		fbi->mode = mode;
		return 0;
	case ULTRAFBIO_GLOBAL_MODE_VALUE:
		if (copy_from_user(&alpha, (void *)argp, sizeof(alpha)))
			return -EFAULT;

		//dcfb_print("global_alpha_mode = 0x%08x, global_alpha_value = 0x%08x\n", alpha.global_alpha_mode, alpha.global_alpha_value);
		fbi->blending.global_alpha_mode = alpha.global_alpha_mode;
		fbi->blending.global_alpha_value = alpha.global_alpha_value;
		return 0;
	case ULTRAFBIO_SCALE_FILTER_TAP:
		if (copy_from_user(&filtertap, (void *)argp, sizeof(filtertap)))
			return -EFAULT;

		//dcfb_print("horizontal_filter_tap = 0x%08x, vertical_filter_tap = 0x%08x\n", filtertap.horizontal_filter_tap, filtertap.vertical_filter_tap);
		fbi->filtertap.horizontal_filter_tap = filtertap.horizontal_filter_tap;
		fbi->filtertap.vertical_filter_tap = filtertap.vertical_filter_tap;
		return 0;
	case ULTRAFBIO_SYNC_TABLE:
		if (copy_from_user(&table, (void *)argp, sizeof(table)))
			return -EFAULT;

		for (j = 0; j < 128; j++) {
			fbi->synctable.horkernel[j] = table.horkernel[j];
			fbi->synctable.verkernel[j] = table.verkernel[j];
		}
		return 0;
	case ULTRAFBIO_ROTATION:
		if (copy_from_user(&rotangle, (void *)argp, sizeof(rotangle)))
			return -EFAULT;

		//dcfb_print("rotangle = 0x%08x\n", rotangle);
		fbi->rotangle = rotangle;
		return 0;
	case ULTRAFBIO_TILEMODE:
		if (copy_from_user(&tilemode, (void *)argp, sizeof(tilemode)))
			return -EFAULT;

		//dcfb_print("tile_mode = 0x%08x\n", tilemode);
		fbi->tile_mode = tilemode;
		return 0;
	case ULTRAFBIO_COLORKEY:
		if (copy_from_user(&colorkey, (void *)argp, sizeof(colorkey)))
			return -EFAULT;

		dcfb_print("\noverlay_IOCTL colorkey_enable = 0x%08x, colorkey_low = 0x%08x, colorkey_high = 0x%08x\n",
		       colorkey.enable, colorkey.colorkey_low, colorkey.colorkey_high);
		if (colorkey.enable)
			fbi->colorkey.enable = 0x2;
		else
			fbi->colorkey.enable = 0x0;

		fbi->colorkey.colorkey_low = colorkey.colorkey_low;
		fbi->colorkey.colorkey_high = colorkey.colorkey_high;
		return 0;
	case ULTRAFBIO_GET_FBADDR:
		dcfb_print("\noverlay_IOCTL Addr = 0x%08x, 0x%08x\n", (u32)fbi->overlay_current_dma, (u32)overlay_phys);
		if (copy_to_user(buf, &overlay_phys, sizeof(u32)))
			return -EFAULT;
		return 0;
	case ULTRAFBIO_GET_FORMAT:
		dcfb_print("\noverlay_IOCTL FORMAT = 0x%08x\n", fbi->overlaysize.format);
		if (copy_to_user(buf, &fbi->overlaysize.format, sizeof(u32)))
			return -EFAULT;
		return 0;
	case ULTRAFBIO_OUTPUT_OFF:
		dcfb_print("\noverlay_IOCTL OUTPUTOFF = %d, OUTPUT disable\n", fbi->enabled);
		ultrafb_output(fbi, 0);
		return 0;
	case ULTRAFBIO_OUTPUT_ON:
		dcfb_print("\noverlay_IOCTL OUTPUTON = %d, OUTPUT enable\n", fbi->enabled);
		ultrafb_output(fbi, 1);
		return 0;
	default:
		return -EFAULT;
	}
}

static int overlay_release(struct fb_info *info, int user)
{
	struct fb_var_screeninfo *var = &info->var;
	struct ultrafb_info *fbi = info->par;
	u32 data;

	fbi->info = info;

	gu8_fb0_open_cnt--;
	gu8_fb1_open_cnt--;
	dcfb_print("L:%d %s -> fb0_open_cnt=%d, fb1_open_cnt=%d\n", __LINE__,
	       __FUNCTION__, gu8_fb0_open_cnt, gu8_fb1_open_cnt);

	if ( (gu8_fb1_open_cnt == 0) && (ResetOffsetFlag == true))
	{
		dcfb_print("Reset var->yoffset\n");
		var->yoffset = 0;
		set_overlay_graphics_start(info, var->xoffset, var->yoffset);
		write_register(fbi->overlay_current_dma, OVERLAY_ADDRESS);
	}

	if ((gu8_fb0_open_cnt == 0) && (gu8_fb1_open_cnt == 0))
	{
		dcfb_print("******** overlay_release: clock   disable ********\n");
		dcfb_print("******** overlay_release: overlay disable ********\n");
		fbi->colorkey.enable = 0;
		fbi->enabled = false;
		write_register(read_register(OVERLAY_CONFIG) | fbi->colorkey.enable, OVERLAY_CONFIG);
		data = (read_register(OVERLAY_CONFIG) & ~(1 << 24));
		write_register(data, OVERLAY_CONFIG);
		clk_disable_unprepare(fbi->dcuclk);
	}

	return 0;
}

static struct fb_ops overlay_ops = {
	.owner = THIS_MODULE,
	.fb_open	= overlay_open,
	.fb_check_var = overlay_check_var,
	.fb_set_par = overlay_set_par,
	.fb_pan_display = overlay_pan_display,
	.fb_ioctl = overlay_ioctl,
	.fb_release = overlay_release,
};

static int ultrafb_of_read_mode(struct platform_device *pdev,
                                struct fb_videomode *mode)
{
	int ret;
	struct display_timings *disp_timing;
	struct videomode vm;

	disp_timing = of_get_display_timings(pdev->dev.of_node);
	if (!disp_timing) {
		dev_err(&pdev->dev, "Failed to read timing\n");
		return -EINVAL;
	}

	ret = videomode_from_timings(disp_timing, &vm, disp_timing->native_mode);
	if (ret) {
		dev_err(&pdev->dev, "videomode_from_timings failed: %d\n",ret);
		return -EINVAL;
	}

	gulpixclkHz = vm.pixelclock;

	/* pixel clock in ps (pico seconds) */
	mode->pixclock = PICOS2KHZ(vm.pixelclock) * 1000;
	pr_debug("  -------- pixclock@ %ld Hz -> %d ps\n", vm.pixelclock, mode->pixclock);
	dcfb_print("%dx%d h/vsync-active:%d\n", vm.hactive, vm.vactive, vm.flags);
	dcfb_print("hsync_len,left_margin,right_margin= (%d, %d, %d)\n", vm.hsync_len,
			vm.hback_porch, vm.hfront_porch);
	dcfb_print("vsync_len,upper_margin,lower_margin=(%d, %d, %d)\n", vm.vsync_len,
			vm.vback_porch, vm.vfront_porch);

	mode->xres = vm.hactive;
	mode->yres = vm.vactive;
	mode->hsync_len = vm.hsync_len;
	mode->left_margin = vm.hback_porch;
	mode->right_margin = vm.hfront_porch;
	mode->vsync_len = vm.vsync_len;
	mode->upper_margin = vm.vback_porch;
	mode->lower_margin = vm.vfront_porch;

	gu32_sync_active = vm.flags;

	return 0;
}

static u32 calc_pixclk_div_params(u32 dcupsrcFreg, struct regmap *regmap, u32 *TargetFreq)
{
	u32 val, div_val, retFreq = 0, FactorIdx, tmpFreq;

	for (FactorIdx = 0; FactorIdx < (sizeof(DivideFactor)/4); FactorIdx++)
	{
		tmpFreq = dcupsrcFreg / DivideFactor[FactorIdx];
		if (tmpFreq > TargetFreq[0])
			continue;
		else if (tmpFreq < TargetFreq[0])
		{
			if (tmpFreq > retFreq)    
			{
				retFreq = tmpFreq;
				div_val = FactorIdx;
			}
        	}
	        else
        	{
			retFreq = tmpFreq;
			div_val = FactorIdx;
			break;
		}
	}

	TargetFreq[0] = retFreq;
	dcfb_print("\n  calc_pixclk_div_params div_val: %d, ( @%d Hz ), @%d Hz\n", div_val, retFreq, TargetFreq[0]);
	regmap_read(regmap, REG_CLK_CLKDIV0, &val);
	dcfb_print("  calc_pixclk_div_params before regmap_read div =0x%x\n", val);	
	val = (val &~(0x7<<16)) | (div_val<<16);
	regmap_write(regmap, REG_CLK_CLKDIV0, val);
	regmap_read(regmap, REG_CLK_CLKDIV0, &val);
	dcfb_print("  calc_pixclk_div_params after  regmap_read div =0x%x\n", val);

	return div_val;
}

//static int fb0_width; // 20230210 sc modify
static int dcultra_overlay_init(struct platform_device *pdev, char __iomem *virt_addr, dma_addr_t phy_addr)
{
	struct fb_info *info;
	struct ultrafb_info *fbi;
	struct fb_videomode *mode;
	int ret = 0;
	u32 format, width, height, tlx, tly, brx, bry;//bpp
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	u32 clock_target, dcpdiv, div_val;
        u32 retclock[1]= {0};
	struct regmap *regmap;
	u32 val;

	mode = devm_kmalloc(&pdev->dev, sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return -ENOENT;

	ret = ultrafb_of_read_mode(pdev, mode);
	if (ret) {
		dev_err(&pdev->dev, "overlay parse devicetree ultrafb_of_read_mode failed\n");
		return -ENOENT;
	}

	info = framebuffer_alloc(sizeof(struct ultrafb_info), &pdev->dev);
	if (info == NULL) {
		dev_err(&pdev->dev, "alloc overlay fb_info failed\n");
		return -ENOMEM;
	}

	ret = of_property_read_u32(pdev->dev.of_node, "overlay-pixel-fmt", &format);
	//ret |= of_property_read_u32(pdev->dev.of_node, "overlay-bits-per-pixel", &bpp);
	ret |= of_property_read_u32(pdev->dev.of_node, "overlay-width", &width);
	ret |= of_property_read_u32(pdev->dev.of_node, "overlay-height", &height);
	ret |= of_property_read_u32(pdev->dev.of_node, "overlay-rect-tlx", &tlx);
	ret |= of_property_read_u32(pdev->dev.of_node, "overlay-rect-tly", &tly);
	if (ret) {
		dev_err(&pdev->dev, "Failed to get Ovelay settings from DT\n");
		return -EINVAL;
	}

	if ((width + tlx > mode->xres) || (height + tly > mode->yres)) {
		dev_err(&pdev->dev, "Wrong overlay setting\n");
		return -EINVAL;
	}

	brx = width + tlx;
	bry = height + tly;

	dcfb_print("L:%d Overlayfb: %dx%d  format %d, Rect: (%d, %d) (%d, %d)\n",
	           __LINE__, width, height, format, tlx, tly, brx, bry);
	dcfb_print("mode->xres %d, mode->xres %d format %d\n",mode->xres,mode->yres,format);

	fbi = info->par;
	fbi->info = info;
	fbi->dev = info->dev = &pdev->dev;
	fbi->mode = DC_BLEND_MODE_SRC;
	fbi->blending.global_alpha_mode = 0;
	fbi->blending.global_alpha_value = 0;
	fbi->filtertap.horizontal_filter_tap = 5;
	fbi->filtertap.vertical_filter_tap = 3;
	fbi->overlayrect.brx = brx;
	fbi->overlayrect.bry = bry;
	fbi->overlayrect.tlx = tlx;
	fbi->overlayrect.tly = tly;
	fbi->overlaysize.height = height;
	fbi->overlaysize.width = width;
	if(format == 0x6 || format == 0x8)
		//fbi->overlaysize.stride = fb0_width * 4;// 20230210 sc modify
		fbi->overlaysize.stride = mode->xres/*width */ * 4;
	else if(format == 0x4 || format == 0x7)
		fbi->overlaysize.stride = mode->xres * 2;
	else
		fbi->overlaysize.stride = mode->xres;
	fbi->overlaysize.format = format;
	fbi->rotangle = 0;
	fbi->tile_mode = 0;

	/* set other parameter*/
	info->flags = FBINFO_PARTIAL_PAN_OK |FBINFO_HWACCEL_XPAN |
	              FBINFO_HWACCEL_YPAN;
	info->node = -1;
	info->pseudo_palette = NULL;
	info->fbops = &overlay_ops;

	/* enable Display Controller Ultra Core Clock Source */
	fbi->dcuclk = devm_clk_get(&pdev->dev, "dcu_gate");
	if (IS_ERR(fbi->dcuclk)) {
		ret = PTR_ERR(fbi->dcuclk);
		dev_err(&pdev->dev, "failed to get display core clk: %d\n", ret);
		return -ENOENT;
	}

	ret = clk_prepare_enable(fbi->dcuclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable display core clk: %d\n", ret);
		return -ENOENT;
	}
	dcfb_print("\nL:%d overlay dcu_clk epll/2 @%ld Hz\n", __LINE__,
	       clk_get_rate(fbi->dcuclk));

	// Get display pixel clock divider
	node = of_parse_phandle(pdev->dev.of_node, "nuvoton,clk", 0);
	if (node) {
		regmap = syscon_node_to_regmap(node);
		if (IS_ERR(regmap)) {
			return PTR_ERR(regmap);
		}
#if 0
		regmap_read(regmap, REG_CLK_CLKDIV0, &val);
		printk("    == overlay before regmap_read div =0x%x\n", val);
		regmap_read(regmap, 0xB0, &val);
		printk("    == overlay before regmap_read VPLL_CTL0:[ 0x%08x ]\n", val);
		regmap_read(regmap, 0xB4, &val);
		printk("    == overlay before regmap_read VPLL_CTL1:0x%02x\n", val);
#endif
	}

	fbi->pixclock = devm_clk_get(&pdev->dev, "dcup_div");
	if (IS_ERR(fbi->pixclock)) {
		dev_err(&pdev->dev, "failed to get dcup_div clk\n");
		return -ENOENT;
	}

	regmap_read(regmap, REG_CLK_CLKDIV0, &val);
	div_val = (val & 0x70000) >> 16;
	dcfb_print("     == overlay  CLKDIV0:0x%x, div_val: [ %d ]\n", val, div_val);
	clock_target = clk_get_rate(fbi->pixclock)*DivideFactor[div_val];
	retclock[0] = gulpixclkHz;
	dcpdiv = calc_pixclk_div_params(clock_target, regmap, retclock);
	dcfb_print("L:%d overlay clk_get_rate pixclk src @%ld Hz\n", __LINE__, clock_target);
	dcfb_print("       overlay calc_pixclk_div_params dcupdiv %d, retFreq ( @%d Hz )\n",dcpdiv, retclock[0]);
	dcfb_print("L:%d overlay clk_get_rate pixclk@%ld Hz, gulpixclkHz@%d Hz  retclock@%d Hz--------\n", __LINE__,
	       clk_get_rate(fbi->pixclock), gulpixclkHz, retclock[0]);
	clk_set_rate(fbi->pixclock, retclock[0]);
	dcfb_print("L:%d overlay clk_set_rate pixclk@%ld Hz --------\n", __LINE__, clk_get_rate(fbi->pixclock));
#if 0
	regmap_read(regmap, 0xB0, &val);
	printk("-----  overlay after  regmap_read VPLL_CTL0:[ 0x%08x ]\n", val);
	regmap_read(regmap, 0xB4, &val);
	printk("-----  overlay after  regmap_read VPLL_CTL1:0x%02x\n", val);
#endif

	/* enable Display Controller Pixel Clock */
	clk_prepare_enable(fbi->pixclock);
	if (IS_ERR(fbi->pixclock)) {
		dev_err(&pdev->dev, "failed to enable pixclksrc clk\n");
		return -ENOENT;
	}
	dcfb_print("-----  overlay pixclk [ @%ld Hz ] ---\n", clk_get_rate(fbi->pixclock));

#if 0
	regmap_read(regmap, REG_CLK_CLKDIV0, &val);
	printk("    == overlay  regmap_read div =0x%x\n", val);
	regmap_read(regmap, 0xB0, &val);
	printk("    == overlay  regmap_read VPLL_CTL0:[ 0x%08x ]\n", val);
	regmap_read(regmap, 0xB4, &val);
	printk("    == overlay  regmap_read VPLL_CTL1:0x%02x\n", val);  
#endif

	/* set var parameter*/
	set_overlay_var(&info->var, mode, fbi->overlaysize.format);

	/* set multibuffer. */
	info->var.xres_virtual = info->var.xres;
	info->var.yres_virtual = info->fix.smem_len /
	                         (info->var.xres_virtual * (info->var.bits_per_pixel >> 3)) / info->var.yres *
	                         info->var.yres;
	if (info->var.yres_virtual > gu32_bufferCnt * info->var.yres)
		info->var.yres_virtual = gu32_bufferCnt * info->var.yres;

	/* set fix parameter*/
	strlcpy(info->fix.id, "dcultra overlay", 16);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = info->var.yres;
	info->fix.ywrapstep = 0;
	info->fix.smem_len = PAGE_ALIGN(gu32_FB_SIZE);
	info->screen_base = virt_addr;
	overlay_phys = phy_addr;

	if (!info->screen_base) {
		dev_err(&pdev->dev, "%s: Failed to allocate overlay framebuffer\n", __func__);
		ret = -ENOMEM;
		goto on_error;
	}
	info->fix.smem_start = (unsigned long)overlay_phys;
	overlay_info = info;
	overlay_check_var(&info->var, info);
	overlay_set_par(info);

	write_register(fbi->overlaysize.stride, OVERLAY_STRIDE);
	dcfb_print("\n      L:%d %s OVERLAY_STRIDE(0x1600): %d\n", __LINE__, __FUNCTION__, 
	           read_register(OVERLAY_STRIDE));

	/* clear screen */
	fb_memset(info->screen_base, 0, info->fix.smem_len);

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register overlay fb_info\n");
		ret = -ENOMEM;
		goto on_error;
	}
	dev_info(&pdev->dev, "overlay at 0x%lx, %d bytes, mapped to 0x%llx\n",
	         info->fix.smem_start, info->fix.smem_len,
	         (u64)info->screen_base);

	dev_info(&pdev->dev, "fb%d: overlayfb registered\n", info->node);

	fbi->enabled = true;
	return 0;

on_error:
	framebuffer_release(overlay_info);

	return ret;
}

static int ultrafb_probe(struct platform_device *pdev)
{
	struct device_node *np;
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct resource r;
	ssize_t size;
	struct fb_info *info;
	struct ultrafb_info *fbi;
	struct fb_videomode *mode;
	int ret = 0;
	int pixel_byte = 0;
	struct resource *res = NULL;
	u32 bpp, pix_fmt, buswidth, clock_target, dcpdiv, div_val;
        u32 retclock[1]= {0};
	const char  *str;
	struct regmap *regmap;
	u32 val;
	struct gpio_desc *pk5_gpio;

	dev_info(&pdev->dev, "Nuvoton Framebuffer driver\n");

	//_Static_assert(PAGE_ALIGN(MAX_FB_SIZE) + 32 * 32 * 4 < 0x2000000, "error");

	gu8_fb0_open_cnt = 0;
	gu8_fb1_open_cnt = 0;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "no IO memory defined\n");
		return -ENOENT;
	}

	reg_base = res->start;
	reg_size = resource_size(res);

	ResetOffsetFlag = false;
	if (of_property_read_string(pdev->dev.of_node, "reset_offset_en", &str) == 0) {

		if (!strcmp("yes", str))
			ResetOffsetFlag = true;
	}
	dcfb_print("ResetOffsetFlag = %d\n", ResetOffsetFlag);

	ret = of_property_read_u32(pdev->dev.of_node, "bits-per-pixel", &bpp);
	//ret |= of_property_read_u32(pdev->dev.of_node, "bus-width", &buswidth);
	ret |= of_property_read_u32(pdev->dev.of_node, "dpi-config", &gu32_dpiConfig);
	if(gu32_dpiConfig == 0 || gu32_dpiConfig == 1 || gu32_dpiConfig == 2)
		buswidth = 16;
	else if(gu32_dpiConfig == 3 || gu32_dpiConfig == 4)
		buswidth = 18;
	else
		buswidth = 24;

	ret |= of_property_read_u32(pdev->dev.of_node, "swizzle", &gu32_pix_swizzle);
	ret |= of_property_read_u32(pdev->dev.of_node, "pixel-fmt", &pix_fmt);
	ret |= of_property_read_u32(pdev->dev.of_node, "buffer-num", &gu32_bufferCnt);
	pr_debug("========= bufferCnt= %d\n", gu32_bufferCnt);
	ret |= of_property_read_u32(pdev->dev.of_node, "colorkey-en", &gu32_ColorkeyEn);
	ret |= of_property_read_u32(pdev->dev.of_node, "overlay-en", &gu32_OverlayEn);
	pr_debug("========= OverlayEn=%d, ColorkeyEn=%d\n", gu32_OverlayEn,
	           gu32_ColorkeyEn);
	pr_debug("========= L:%d swizzle:%x, dpi: %d, buswidth: %d\n", __LINE__, gu32_pix_swizzle, gu32_dpiConfig, buswidth);

	if (ret) {
		dev_err(&pdev->dev, "Failed to read bpp, pixel_fmt and buswidth from DT\n");
		return -EINVAL;
	}

	if (bpp < 1 || bpp > 255) {
		dev_err(&pdev->dev, "Bits per pixel have to be between 1 and 255\n");
		return -EINVAL;
	}

	gu32_fb_format 	= pix_fmt;

#if 1 //for LVDS
	/* request optional panel power pin */
	pk5_gpio = devm_gpiod_get_optional(&pdev->dev, "pwr", GPIOD_OUT_HIGH);
	if (IS_ERR(pk5_gpio)) {
		ret = PTR_ERR(pk5_gpio);
		dev_err(&pdev->dev, "PK5 pwr pin failure: %d\n", ret);
	}
#endif

	mode = devm_kmalloc(&pdev->dev, sizeof(*mode), GFP_KERNEL);
	if (!mode)
		return -ENOMEM;

	// Get SYS_PDID
	node = of_parse_phandle(pdev->dev.of_node, "nuvoton,sys", 0);
	if (node) {
		regmap = syscon_node_to_regmap(node);
		if (IS_ERR(regmap)) {
			return PTR_ERR(regmap);
		}

		regmap_read(regmap, 0x0, &val);
		val = (val >> 16) & 0xff;
		pr_debug("\tPDID: 0x%02x\n", val);
		if(val == 0xa1 || val == 0x81 || val == 0x82) {
			dev_err(&pdev->dev, "Failed to find Nuvoton Framebuffer driver\n");
			return -EINVAL;
		}
	}

	ret = ultrafb_of_read_mode(pdev, mode);
	if (ret) {
		dev_err(&pdev->dev, "parse devicetree ultrafb_of_read_mode failed\n");
		return -ENOENT;
	}

	info = framebuffer_alloc(sizeof(struct ultrafb_info), &pdev->dev);
	if (info == NULL) {
		dev_err(&pdev->dev, "alloc framebuffer failed\n");
		return -ENOMEM;
	}

	fbi = info->par;
	fbi->info = info;
	fbi->dev = info->dev = &pdev->dev;
	fbi->format = gu32_fb_format;
	fbi->vblank_count = 0;
	fbi->rotangle = 0;
	fbi->tile_mode = 0;
	pixel_byte = get_bpp_by_format(fbi->format);
	fbi->framebuffersize.width = mode->xres;
	//fb0_width = fbi->framebuffersize.width;// 20230210 sc modify
	fbi->framebuffersize.height = mode->yres;
	if(fbi->format >= PIX_FMT_INDEX8)
		fbi->framebuffersize.stride = mode->xres;
	else
		fbi->framebuffersize.stride = mode->xres * pixel_byte;
	fbi->framebuffersize.format = fbi->format;

	if (gu32_ColorkeyEn && (fbi->framebuffersize.format == 0x6))
		fbi->colorkey.enable = 0x2;
	else
		fbi->colorkey.enable = 0x0;
	pr_debug("L:%d %s colorkey = 0x%x\n", __LINE__,
			 __FUNCTION__, fbi->colorkey.enable);
	if ((fbi->irq = platform_get_irq(pdev, 0)) < 0) {
		dev_err(&pdev->dev, "no IRQ defined\n");
		return -ENOENT;
	}
	pr_debug("DT: irq/reg_base/reg_size/format/swizzle = %d/0x%lx/0x%x/0x%x/%d\n", fbi->irq,
	           reg_base, reg_size, pix_fmt, gu32_pix_swizzle);

	/* enable Display Controller Ultra Core Clock Source */
	fbi->dcuclk = devm_clk_get(&pdev->dev, "dcu_gate");
	if (IS_ERR(fbi->dcuclk)) {
		ret = PTR_ERR(fbi->dcuclk);
		dev_err(&pdev->dev, "failed to get display core clk: %d\n", ret);
		return -ENOENT;
	}

	ret = clk_prepare_enable(fbi->dcuclk);
	if (ret) {
		dev_err(&pdev->dev, "failed to enable display core clk: %d\n", ret);
		return -ENOENT;
	}
	dcfb_print("\nL:%d dcu_clk epll/2 @%ld Hz--------\n", __LINE__,
	       clk_get_rate(fbi->dcuclk));

	// Get display pixel clock divider
	node = of_parse_phandle(pdev->dev.of_node, "nuvoton,clk", 0);
	if (node) {
		regmap = syscon_node_to_regmap(node);
		if (IS_ERR(regmap)) {
			return PTR_ERR(regmap);
		}
#if 0
		regmap_read(regmap, REG_CLK_CLKDIV0, &val);
		printk("    @@@@@ before regmap_read div =0x%x\n", val);
		regmap_read(regmap, 0xB0, &val);
		printk("    @@@@@ before regmap_read VPLL_CTL0:[ 0x%08x ]\n", val);
		regmap_read(regmap, 0xB4, &val);
		printk("    @@@@@ before regmap_read VPLL_CTL1:0x%02x\n", val);
#endif
	}

	fbi->pixclock = devm_clk_get(&pdev->dev, "dcup_div");
	if (IS_ERR(fbi->pixclock)) {
		dev_err(&pdev->dev, "failed to get dcup_div clk\n");
		return -ENOENT;
	}
//	dcfb_print("L:%d clk_get_rate pixclk src @%ld Hz\n", __LINE__,
//	       clk_get_rate(fbi->pixclock)*2);

	regmap_read(regmap, REG_CLK_CLKDIV0, &val);
	div_val = (val & 0x70000) >> 16;
	dcfb_print("      CLKDIV0:0x%x, div_val: [ %d ]\n", val, div_val);
	//clock_target = clk_get_rate(fbi->pixclock)*2;
	clock_target = clk_get_rate(fbi->pixclock)*DivideFactor[div_val];
	retclock[0] = gulpixclkHz;
	dcpdiv = calc_pixclk_div_params(clock_target, regmap, retclock);
	dcfb_print("\t    calc_pixclk_div_params dcupdiv %d, retFreq ( @ %d Hz )\n",dcpdiv, retclock[0]);
	dcfb_print("L:%d clk_get_rate pixclk@%ld Hz, gulpixclkHz@%ld Hz  retclock@%d Hz--------\n", __LINE__,
	       clk_get_rate(fbi->pixclock), gulpixclkHz, retclock[0]);
	clk_set_rate(fbi->pixclock, retclock[0]);
	dcfb_print("L:%d clk_set_rate pixclk@%ld Hz --------\n", __LINE__, clk_get_rate(fbi->pixclock));


	/* enable Display Controller Pixel Clock */
	clk_prepare_enable(fbi->pixclock);
	if (IS_ERR(fbi->pixclock)) {
		dev_err(&pdev->dev, "failed to enable pixclksrc clk\n");
		return -ENOENT;
	}
	dcfb_print("L:%d display pixclk @%ld Hz\n", __LINE__, clk_get_rate(fbi->pixclock));

	dev_info(&pdev->dev, "ultrafb dcuclk@ %ld Hz, dcupclk@ %ld Hz, panle freq@ %ld Hz\n",
	         clk_get_rate(fbi->dcuclk),clk_get_rate(fbi->pixclock), gulpixclkHz);
#if 0
	regmap_read(regmap, REG_CLK_CLKDIV0, &val);
	printk("    @@@@@        regmap_read div =0x%x\n", val);
	regmap_read(regmap, 0xB0, &val);
	printk("    @@@@@ after  regmap_read VPLL_CTL0:[ 0x%08x ]\n", val);
	regmap_read(regmap, 0xB4, &val);
	printk("    @@@@@ after  regmap_read VPLL_CTL1:0x%02x\n", val);
#endif

	/* NUA3500_RESET_DISP */
	fbi->reset = devm_reset_control_get(&pdev->dev, NULL);
	if (!IS_ERR(fbi->reset)) {
		ret = reset_control_assert(fbi->reset);
		udelay(100);
		ret = reset_control_deassert(fbi->reset);
	}

	fbi->enabled = true;

	/*
	 * Initialise static fb parameters.
	 */
	info->flags = FBINFO_PARTIAL_PAN_OK |
	              FBINFO_HWACCEL_XPAN | FBINFO_HWACCEL_YPAN;
	info->node = -1;
	strlcpy(info->fix.id, "Vivante dcultra", 16);
	info->fix.type = FB_TYPE_PACKED_PIXELS;
	info->fix.type_aux = 0;
	info->fix.xpanstep = 0;
	info->fix.ypanstep = 0;
	info->fix.ywrapstep = 0;
	info->fix.mmio_start = reg_base;
	info->fix.mmio_len = reg_size;
	info->fix.accel = FB_ACCEL_NONE;
	info->fbops = &ultrafb_ops;
	info->pseudo_palette = fbi->pseudo_palette;
	info->fix.visual = FB_VISUAL_TRUECOLOR;

	if(fbi->format == PIX_FMT_NV12)
		gu32_FB_SIZE = ((mode->xres * mode->yres * 3) / 2) * gu32_bufferCnt;
	else
		gu32_FB_SIZE = mode->xres * mode->yres * pixel_byte * gu32_bufferCnt;
	info->fix.smem_len = PAGE_ALIGN(gu32_FB_SIZE);

	ret = request_irq(fbi->irq, ultrafb_handle_irq, IRQF_SHARED, "ma35d1-fb", fbi);
	spin_lock_init(&fbi->lock);
	init_waitqueue_head(&vsync_wait);

	request_mem_region(res->start, resource_size(res), pdev->name);

	/* Get reserved memory region from Device-tree */
	np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
	if (!np) {
		dev_err(&pdev->dev, "No %s specified\n", "memory-region");
		return -ENOENT;
	}

	ret = of_address_to_resource(np, 0, &r);
	if (ret) {
	  dev_err(&pdev->dev, "No memory address assigned to the region\n");
	  return -ENOENT;
	}

	framebuffer_phys = r.start;
	size = resource_size(&r);
	info->screen_base = memremap(r.start, size, MEMREMAP_WB);
	if (!info->screen_base) {
		dev_err(&pdev->dev, "%s: Failed to allocate framebuffer\n", __func__);
		ret = -ENOMEM;
		goto on_error;
	}
	info->fix.smem_start = (unsigned long)framebuffer_phys;
	dev_info(&pdev->dev, "Reserved fb memory at 0x%lx, 0x%lx bytes, mapped to 0x%llx\n",
	         info->fix.smem_start, size, (u64)info->screen_base);

	//fbi->reg_virtual = (void __iomem *)ioremap_nocache(reg_base, reg_size);
	fbi->reg_virtual = (void __iomem *)ioremap(reg_base, reg_size);
	reg_virtual = fbi->reg_virtual;

	// cursor framebuffer
	fbi->cursor_start_dma = PAGE_ALIGN(info->fix.smem_start + info->fix.smem_len);
	fbi->cursor_virtual = dma_alloc_wc(&pdev->dev, CURSOR_SIZE * CURSOR_SIZE * 4,
	                                   &fbi->cursor_start_dma, GFP_KERNEL);
	if (!fbi->cursor_virtual) {
		dev_err(&pdev->dev, "%s: Failed to allocate cursor framebuffer\n", __func__);
		ret = -ENOMEM;
		goto on_error;
	}
	set_mode(&info->var, mode, fbi->format);

	/* set multibuffer. */
	info->var.xres_virtual = info->var.xres;
	info->var.yres_virtual = info->fix.smem_len /
	                         (info->var.xres_virtual * (info->var.bits_per_pixel >> 3)) / info->var.yres *
	                         info->var.yres;
	if (info->var.yres_virtual > gu32_bufferCnt * info->var.yres)
		info->var.yres_virtual = gu32_bufferCnt * info->var.yres;
	info->var.activate = FB_ACTIVATE_FORCE;

	ultrafb_check_var(&info->var, info);

	if (!IS_ERR(fbi->reset)) {
		write_register(0x0, FRAMEBUFFER_CONFIG);
		write_register(0x00071900, AQHICLOCKCONTROL);
		write_register(0x00070900, AQHICLOCKCONTROL);

		ultrafb_set_par(info);

		/* clear screen */
		fb_memset(info->screen_base, 0, info->fix.smem_len);
		//printk("\tCFLICFLICFLI DEBUG 1\n");

	}
	ret = fb_alloc_cmap(&info->cmap, 16, 0);
	if (ret) {
		dev_err(&pdev->dev, "Fail to alloc cmap: %d\n", ret);
		goto on_error;
	}

#if 1  // 20230210 sc modify, set if 0
	ret = fb_set_var(info, &info->var);
	if (ret) {
		dev_warn(&pdev->dev, "unable to set display parameters\n");
		goto on_error;
	}
#endif
	platform_set_drvdata(pdev, fbi);

	ret = register_framebuffer(info);
	if (ret < 0) {
		dev_err(&pdev->dev, "Failed to register dc-fb: %d\n", ret);
		ret = -ENXIO;
		goto on_error;
	}
	dev_info(&pdev->dev, "ultrafb at 0x%lx, %d bytes, mapped to 0x%0llX\n",
	         info->fix.smem_start, info->fix.smem_len,
	         (u64)info->screen_base);

	if(gu32_OverlayEn) {
		ret = dcultra_overlay_init(pdev,info->screen_base+info->fix.smem_len,info->fix.smem_start + info->fix.smem_len);
		if (ret < 0) {
			dev_err(&pdev->dev, "Failed to init overlay\n");
			goto on_error;
		}
	}

	// disable Display controller clk
	if (!IS_ERR(fbi->reset))
	{
		//printk("\tCFLICFLICFLI DEBUG 2\n");
		clk_disable_unprepare(fbi->dcuclk);
	}

	dev_info(&pdev->dev, "fb%d: ultrafb   registered\n", info->node);
	//dev_info(&pdev->dev, "fb%d: ultrafb   registered, CONFIG(0x1518):0x%08x\n\n", info->node, read_register(FRAMEBUFFER_CONFIG));
	pr_debug("DISP_VERSION: %s\n\n", DISP_VERSION);
	return 0;

on_error:
	fb_dealloc_cmap(&info->cmap);

	dma_free_wc(info->device, CURSOR_SIZE * CURSOR_SIZE * 4, fbi->cursor_virtual,
	            fbi->cursor_start_dma);
	iounmap(fbi->reg_virtual);
	if (res)
		release_mem_region(info->fix.smem_start, info->fix.smem_len);

	framebuffer_release(info);

	dev_err(&pdev->dev, "frame buffer device init failed with %d\n", ret);
	return ret;
}

static int ultrafb_remove(struct platform_device *pdev)
{
	struct ultrafb_info *fbi = platform_get_drvdata(pdev);
	struct fb_info *info;
	struct resource *res;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info = fbi->info;

	write_register(0, DISPLAY_INTRENABLE);
	write_register(0,FRAMEBUFFER_CONFIG);
	while(read_register(INT_STATE)&1)
		write_register(0xffffffff,INT_STATE);

	clk_disable_unprepare(fbi->pixclock);
	clk_disable_unprepare(fbi->dcuclk);

	if(gu32_OverlayEn)
		unregister_framebuffer(overlay_info);

	unregister_framebuffer(info);

	fb_dealloc_cmap(&info->cmap);

	dma_free_wc(&pdev->dev, CURSOR_SIZE * CURSOR_SIZE * 4, fbi->cursor_virtual,
	            fbi->cursor_start_dma);

	iounmap(fbi->reg_virtual);

	/* release irq */
	free_irq(fbi->irq, fbi);

	release_mem_region(res->start, resource_size(res));

	if(gu32_OverlayEn)
		framebuffer_release(overlay_info);

	framebuffer_release(info);

	dev_info(&pdev->dev, "Nuvoton Framebuffer driver remove\n");

	return 0;
}

static const struct of_device_id ma35d1_fb_of_match[] = {
	{.compatible = "nuvoton,ma35d1-fb" },
	{},
};
MODULE_DEVICE_TABLE(of, ma35d1_fb_of_match);

#if 0
#ifdef CONFIG_PM
static int ma35d1_ultrafb_suspend(struct platform_device *pdev,
                                 pm_message_t state)
{

	struct ultrafb_info *fbi = platform_get_drvdata(pdev);

	gpiod_direction_output(fbi->bl_gpio, 0);
	clk_disable_unprepare(fbi->dcuclk);

	return 0;
}

static int ma35d1_ultrafb_resume(struct platform_device *pdev)
{

	struct ultrafb_info *fbi = platform_get_drvdata(pdev);

	gpiod_direction_output(fbi->bl_gpio, 1);
	clk_prepare_enable(fbi->dcuclk);

	return 0;
}

#else

#define ma35d1_ultrafb_suspend 	NULL
#define ma35d1_ultrafb_resume	NULL

#endif
#endif

static struct platform_driver ultrafb_driver = {
	.driver   = {
		.name = "viv-dc",
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ma35d1_fb_of_match),
	},
	.probe = ultrafb_probe,
	.remove = ultrafb_remove,
	//.suspend = ma35d1_ultrafb_suspend,
	//.resume	= ma35d1_ultrafb_resume,
};


module_platform_driver(ultrafb_driver);

MODULE_DESCRIPTION("Vivante dcultra Driver");
MODULE_VERSION(DISP_VERSION);
MODULE_LICENSE("GPL");
