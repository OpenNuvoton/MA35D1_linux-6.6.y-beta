// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton DRM driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#ifndef _MA35_LAYER_H_
#define _MA35_LAYER_H_

#include <linux/of.h>
#include <linux/types.h>
#include <drm/drm_plane.h>
#include <drm/drm_property.h>

#define MA35_MAX_PLANES	3

struct ma35_drm;

struct ma35_plane_property {
	u32 fb_addr[MA35_MAX_PLANES];
	u32 fb_stride[MA35_MAX_PLANES];
	bool alpha;
	bool swizzle;
	bool colorkey;
	bool background;
};

struct ma35_layer_config {
	u32 depth;	// number of bits per pixel excluding padding bits
	u32 fourcc;	// save drm_plane_state->drm_framebuffer->format, used to check colorkey

	u32 blend_mode;	// this is Porter Duff, not the mode in drm_plane_state->pixel_blend_mode
	u16 alpha_mode[2];	// src, dst
	u32 src_color;	// used for alpha mode, check color is work or only alpha
	u32 dst_color;
	u32 swizzle;
	u32 colorkeylo;	// available for ARGB only
	u32 colorkeyup;

	u32 background;	// ARGB for primary, RGB for cursor
	u32 foreground;	// RGB for cursor, unused
	int32_t hotspot_x;
	int32_t hotspot_y;
};

struct ma35_layer {
	struct drm_plane drm_plane;
	struct list_head list;
	struct device_node *of_node;
	u32 index;
	u32 *formats; // available formats

	// custom properties
	struct drm_property *blend_mode_prop; // overlay
	struct drm_property *src_alpha_mode_prop; // overlay
	struct drm_property *dst_alpha_mode_prop; // overlay
	struct drm_property *src_color_prop; // overlay
	struct drm_property *dst_color_prop; // overlay
	struct drm_property *swizzle_prop; // primary, overlay
	struct drm_property *colorkeylo_prop; // primary, overlay
	struct drm_property *colorkeyup_prop;
	struct drm_property *background_prop; // primary
	struct drm_property *foreground_prop; // unused

	// operation
	struct ma35_layer_config config;
	phys_addr_t fb_base[MA35_MAX_PLANES]; // backup DMA address
};

enum ma35_format_enum {
	MA35_FORMAT_X4R4G4B4,	// DRM_FORMAT_XRGB4444
	MA35_FORMAT_A4R4G4B4,	// DRM_FORMAT_ARGB4444
	MA35_FORMAT_X1R5G5B5,	// DRM_FORMAT_XRGB1555
	MA35_FORMAT_A1R5G5B5,	// DRM_FORMAT_ARGB1555
	MA35_FORMAT_R5G6B5,		// DRM_FORMAT_RGB565
	MA35_FORMAT_X8R8G8B8,	// DRM_FORMAT_XRGB8888
	MA35_FORMAT_A8R8G8B8,	// DRM_FORMAT_ARGB8888
	MA35_FORMAT_YUY2,		// YUV422, DRM_FORMAT_YUYV
	MA35_FORMAT_UYVY,		// YUV422, DRM_FORMAT_UYVY
	MA35_FORMAT_INDEX8,
	MA35_FORMAT_MONOCHROME,
	MA35_FORMAT_YV12,		// YUV420, DRM_FORMAT_YVU420
	MA35_FORMAT_A8,
	MA35_FORMAT_NV12,		// YUV420, DRM_FORMAT_NV12
	MA35_FORMAT_NV16,		// YUV422, DRM_FORMAT_NV16
	MA35_FORMAT_RG16,
	MA35_FORMAT_R8,
	MA35_FORMAT_NV12_10BIT,
	MA35_FORMAT_A2R10G10B10, // DRM_FORMAT_ARGB2101010
	MA35_FORMAT_NV16_10BIT,
	MA35_FORMAT_INDEX1,
	MA35_FORMAT_INDEX2,
	MA35_FORMAT_INDEX4,
	MA35_FORMAT_P010,		// YUV420, DRM_FORMAT_P010
	MA35_FORMAT_NV12_10BIT_L1,
	MA35_FORMAT_NV16_10BIT_L1,
};

// output = src * a + dst * b
enum ma35_blend_mode_enum { // (a, b)
	MA35_ALPHA_CLEAR,	// (0, 0)
	MA35_ALPHA_SRC,		// (1, 0)
	MA35_ALPHA_DST,		// (0, 1)
	MA35_ALPHA_SRC_OVER,	// (1, 1-alpha_s)
	MA35_ALPHA_DST_OVER,	// (1-alpha_d, 1)
	MA35_ALPHA_SRC_IN,	// (alpha_d, 0)
	MA35_ALPHA_DST_IN,	// (0, alpha_s)
	MA35_ALPHA_SRC_OUT,	// (1-alpha_d, 0)
	MA35_ALPHA_DST_OUT,	// (0, 1-alpha_s)
	MA35_ALPHA_SRC_ATOP,	// (alpha_d, 1-alpha_s)
	MA35_ALPHA_DST_ATOP,	// (1-alpha_d, alpha_s)
	MA35_ALPHA_XOR,		// (1-alpha_d, 1-alpha_s)
};

#define MA35_ALPHA_BLEND_DISABLE		BIT(1)

#define MA35_SRC_ALPHA_MODE_INVERSED	BIT(0)
#define MA35_DST_ALPHA_MODE_INVERSED	BIT(9)

enum ma35_alpha_mode_enum {
	MA35_ALPHA_MODE_NONE, // pass-through
	MA35_ALPHA_MODE_GLOBAL, // substitute by global color
	// MA35_ALPHA_MODE_SCALED,
};
#define MA35_SRC_ALPHA_MODE		GENMASK(4, 3)
#define MA35_DST_ALPHA_MODE		GENMASK(11, 10)

enum ma35_alpha_blend_enum {
	MA35_ALPHA_BLEND_ZERO,
	MA35_ALPHA_BLEND_ONE,
	MA35_ALPHA_BLEND_NORMAL,
	MA35_ALPHA_BLEND_INVERSED,
	MA35_ALPHA_BLEND_COLOR,
	MA35_ALPHA_BLEND_COLOR_INVERSED,
	MA35_ALPHA_BLEND_SATURATED_ALPHA,
	MA35_ALPHA_BLEND_SATURATED_DEST_ALPHA,
};
#define MA35_SRC_BLENDING_MODE		GENMASK(7, 5)
#define MA35_DST_BLENDING_MODE		GENMASK(14, 12)

#define MA35_SRC_ALPHA_FACTOR_EN		BIT(8)
#define MA35_DST_ALPHA_FACTOR_EN		BIT(15)

// configs for blend modes
#define MA35_BLEND_MODE_CLEAR	0
#define MA35_BLEND_MODE_SRC	\
	FIELD_PREP(MA35_SRC_BLENDING_MODE, MA35_ALPHA_BLEND_ONE)
#define MA35_BLEND_MODE_DST \
	FIELD_PREP(MA35_DST_BLENDING_MODE, MA35_ALPHA_BLEND_ONE)
#define MA35_BLEND_MODE_SRC_OVER \
	FIELD_PREP(MA35_SRC_BLENDING_MODE, MA35_ALPHA_BLEND_ONE) | \
	FIELD_PREP(MA35_DST_BLENDING_MODE, MA35_ALPHA_BLEND_INVERSED)
#define MA35_BLEND_MODE_DST_OVER \
	FIELD_PREP(MA35_SRC_BLENDING_MODE, MA35_ALPHA_BLEND_INVERSED) | \
	FIELD_PREP(MA35_DST_BLENDING_MODE, MA35_ALPHA_BLEND_ONE)
#define MA35_BLEND_MODE_SRC_IN \
	FIELD_PREP(MA35_SRC_BLENDING_MODE, MA35_ALPHA_BLEND_NORMAL)
#define MA35_BLEND_MODE_DST_IN \
	FIELD_PREP(MA35_DST_BLENDING_MODE, MA35_ALPHA_BLEND_NORMAL)
#define MA35_BLEND_MODE_SRC_OUT \
	FIELD_PREP(MA35_SRC_BLENDING_MODE, MA35_ALPHA_BLEND_INVERSED)
#define MA35_BLEND_MODE_DST_OUT \
	FIELD_PREP(MA35_DST_BLENDING_MODE, MA35_ALPHA_BLEND_INVERSED)
#define MA35_BLEND_MODE_SRC_ATOP \
	FIELD_PREP(MA35_SRC_BLENDING_MODE, MA35_ALPHA_BLEND_NORMAL) | \
	FIELD_PREP(MA35_DST_BLENDING_MODE, MA35_ALPHA_BLEND_INVERSED)
#define MA35_BLEND_MODE_DST_ATOP \
	FIELD_PREP(MA35_SRC_BLENDING_MODE, MA35_ALPHA_BLEND_INVERSED) | \
	FIELD_PREP(MA35_DST_BLENDING_MODE, MA35_ALPHA_BLEND_NORMAL)
#define MA35_BLEND_MODE_XOR \
	FIELD_PREP(MA35_SRC_BLENDING_MODE, MA35_ALPHA_BLEND_INVERSED) | \
	FIELD_PREP(MA35_DST_BLENDING_MODE, MA35_ALPHA_BLEND_INVERSED)

// colorkey
#define MA35_COLORKEY_ENABLE	2
#define MA35_COLORKEY_DISABLE	0

#define MA35_CURSOR_SIZE		32
#define MA35_CURSOR_DEPTH		24

enum ma35_cursor_formats_enum {
	MA35_CURSOR_FORMAT_DISABLE,
	MA35_CURSOR_FORMAT_MASKED,
	MA35_CURSOR_FORMAT_A8R8G8B8,
};

#define MA35_CURSOR_FORMAT_MASK		GENMASK(1, 0)

#define MA35_CURSOR_HOTSPOT_X_MASK	GENMASK(20, 16)
#define MA35_CURSOR_HOTSPOT_Y_MASK	GENMASK(12, 8)
#define MA35_CURSOR_FORMAT_MASK		GENMASK(1, 0)
#define MA35_CURSOR_OWNER_MASK		BIT(4) // 1: owned by GPU
#define MA35_CURSOR_X_MASK		GENMASK(14, 0)
#define MA35_CURSOR_Y_MASK		GENMASK(30, 16)

#define MA35_PRIMARY_ENABLE		BIT(0)
#define MA35_PRIMARY_GAMMA		BIT(2)
#define MA35_PRIMARY_RESET		BIT(4)
#define MA35_PRIMARY_CLEAR		BIT(8)
#define MA35_PRIMARY_TRANSPARENCY_MASK	GENMASK(10, 9)
#define MA35_PRIMARY_SWIZZLE_MASK		GENMASK(25, 23)
#define MA35_PRIMARY_FORMAT_MASK		GENMASK(31, 26)

#define MA35_OVERLAY_ENABLE		BIT(24)
#define MA35_OVERLAY_CLEAR		BIT(25)
#define MA35_OVERLAY_FORMAT_MASK	GENMASK(21, 16)
#define MA35_OVERLAY_SWIZZLE_MASK	GENMASK(15, 13)
#define MA35_OVERLAY_TRANSPARENCY_MASK	GENMASK(1, 0)

#define MA35_SWIZZLE_ARGB_MASK		GENMASK(1, 0)
#define MA35_SWIZZLE_UV_MASK		BIT(2)

enum ma35_swizzles_enum {
	MA35_SWIZZLE_ARGB, // no swizzle
	MA35_SWIZZLE_RGBA,
	MA35_SWIZZLE_ABGR,
	MA35_SWIZZLE_BGRA,
	MA35_SWIZZLE_UV, // enable if not U in lower, V in upper
};

#define MA35_LAYER_FB_HEIGHT	GENMASK(29, 15)
#define MA35_LAYER_FB_WIDTH		GENMASK(14, 0)

#define MA35_OVERLAY_POSITION_Y_MASK	MA35_LAYER_FB_HEIGHT
#define MA35_OVERLAY_POSITION_X_MASK	MA35_LAYER_FB_WIDTH

struct ma35_layer *ma35_layer_get_from_index(struct ma35_drm *priv,
						   u32 index);
struct ma35_layer *ma35_layer_get_from_type(struct ma35_drm *priv,
							enum drm_plane_type type);
void ma35_overlay_attach_crtc(struct ma35_drm *priv);
int ma35_plane_init(struct ma35_drm *priv);

#endif
