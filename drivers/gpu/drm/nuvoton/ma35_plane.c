// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton DRM driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/of.h>
#include <linux/types.h>

#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_blend.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_atomic_helper.h>
#include <drm/drm_plane.h>
#include <drm/drm_print.h>

#include "ma35_drm.h"

#define ma35_layer(p) \
	container_of(p, struct ma35_layer, drm_plane)

static uint32_t ma35_layer_formats[] = {
	/* rgb32 */
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB2101010,
	/* rgb16 */
	DRM_FORMAT_XRGB4444,
	DRM_FORMAT_ARGB4444,
	DRM_FORMAT_XRGB1555,
	DRM_FORMAT_ARGB1555,
	DRM_FORMAT_RGB565,
	/* yuv */
	DRM_FORMAT_YUYV,
	DRM_FORMAT_UYVY,
	DRM_FORMAT_YVU420,
	DRM_FORMAT_NV12,
	DRM_FORMAT_NV16,
	DRM_FORMAT_P010,
};

static uint32_t ma35_cursor_formats[] = {
	DRM_FORMAT_XRGB8888,
	/* MASK */
};

static struct ma35_plane_property ma35_plane_properties[] = {
	{ // overlay
		.fb_addr    = { MA35_OVERLAY_ADDRESS,
						MA35_OVERLAY_UPLANAR_ADDRESS,
						MA35_OVERLAY_VPLANAR_ADDRESS },
		.fb_stride  = { MA35_OVERLAY_STRIDE,
						MA35_OVERLAY_USTRIDE,
						MA35_OVERLAY_VSTRIDE },
		.alpha      = true,
		.swizzle    = true,
		.colorkey   = true, // ARGB only, replaced with primary
		.background = false,
	},
	{ // primary
		.fb_addr    = { MA35_FRAMEBUFFER_ADDRESS,
						MA35_FRAMEBUFFER_UPLANAR_ADDRESS,
						MA35_FRAMEBUFFER_VPLANAR_ADDRESS },
		.fb_stride  = { MA35_FRAMEBUFFER_STRIDE,
						MA35_FRAMEBUFFER_USTRIDE,
						MA35_FRAMEBUFFER_VSTRIDE },
		.alpha      = false,
		.swizzle    = true,
		.colorkey   = true, // ARGB only, replaced with background
		.background = true,
	},
	{ // cursor
		.alpha      = false,
		.swizzle    = false,
		.colorkey   = false,
		.background = true, // and foreground
	},
};

static const struct drm_prop_enum_list ma35_blend_modes[] = {
	{ MA35_ALPHA_CLEAR, "CLEAR" },
	{ MA35_ALPHA_SRC, "SRC" },
	{ MA35_ALPHA_DST, "DST" },
	{ MA35_ALPHA_SRC_OVER, "SRC_OVER" },
	{ MA35_ALPHA_DST_OVER, "DST_OVER" },
	{ MA35_ALPHA_SRC_IN, "SRC_IN" },
	{ MA35_ALPHA_DST_IN, "DST_IN" },
	{ MA35_ALPHA_SRC_OUT, "SRC_OUT" },
	{ MA35_ALPHA_DST_OUT, "DST_OUT" },
	{ MA35_ALPHA_SRC_ATOP, "SRC_ATOP" },
	{ MA35_ALPHA_DST_ATOP, "DST_ATOP" },
	{ MA35_ALPHA_XOR, "XOR" },
};

static const struct drm_prop_enum_list ma35_alpha_modes[] = {
	{ MA35_ALPHA_MODE_NONE, "None" },
	{ MA35_ALPHA_MODE_GLOBAL, "Coverage" },
	// { MA35_ALPHA_MODE_SCALED, "Pre-multiplied" },
};

static const struct drm_prop_enum_list ma35_swizzles[] = {
	{ MA35_SWIZZLE_ARGB, "ARGB" },
	{ MA35_SWIZZLE_RGBA, "RGBA" },
	{ MA35_SWIZZLE_ABGR, "ABGR" },
	{ MA35_SWIZZLE_BGRA, "BGRA" },
	{ MA35_SWIZZLE_UV, "UV" },
};

static int ma35_layer_format_validate(u32 fourcc, u32 *format, u32 *bpp)
{
	u32 ret = 0;

	switch (fourcc) {
	case DRM_FORMAT_XRGB4444:
		*format = MA35_FORMAT_X4R4G4B4;
		*bpp = 16;
		break;
	case DRM_FORMAT_ARGB4444:
		*format = MA35_FORMAT_A4R4G4B4;
		*bpp = 16;
		break;
	case DRM_FORMAT_XRGB1555:
		*format = MA35_FORMAT_X1R5G5B5;
		*bpp = 16;
		break;
	case DRM_FORMAT_ARGB1555:
		*format = MA35_FORMAT_A1R5G5B5;
		*bpp = 16;
		break;
	case DRM_FORMAT_RGB565:
		*format = MA35_FORMAT_R5G6B5;
		*bpp = 16;
		break;
	case DRM_FORMAT_XRGB8888:
		*format = MA35_FORMAT_X8R8G8B8;
		*bpp = 32;
		break;
	case DRM_FORMAT_ARGB8888:
		*format = MA35_FORMAT_A8R8G8B8;
		*bpp = 32;
		break;
	case DRM_FORMAT_ARGB2101010:
		*format = MA35_FORMAT_A2R10G10B10;
		*bpp = 32;
		break;
	case DRM_FORMAT_YUYV:
		*format = MA35_FORMAT_YUY2;
		*bpp = 16;
		break;
	case DRM_FORMAT_UYVY:
		*format = MA35_FORMAT_UYVY;
		*bpp = 16;
		break;
	case DRM_FORMAT_YVU420:
		*format = MA35_FORMAT_YV12;
		*bpp = 16;
		break;
	case DRM_FORMAT_NV12:
		*format = MA35_FORMAT_NV12;
		*bpp = 16;
		break;
	case DRM_FORMAT_NV16:
		*format = MA35_FORMAT_NV16;
		*bpp = 16;
		break;
	case DRM_FORMAT_P010:
		*format = MA35_FORMAT_P010;
		*bpp = 16;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int ma35_layer_blend_mode_select(u32 mode, u32 *reg)
{
	u32 ret = 0;

	switch (mode) {
	case MA35_ALPHA_CLEAR:
		*reg = MA35_BLEND_MODE_CLEAR;
		break;
	case MA35_ALPHA_SRC:
		*reg = MA35_BLEND_MODE_SRC;
		break;
	case MA35_ALPHA_DST:
		*reg = MA35_BLEND_MODE_DST;
		break;
	case MA35_ALPHA_SRC_OVER:
		*reg = MA35_BLEND_MODE_SRC_OVER;
		break;
	case MA35_ALPHA_DST_OVER:
		*reg = MA35_BLEND_MODE_DST_OVER;
		break;
	case MA35_ALPHA_SRC_IN:
		*reg = MA35_BLEND_MODE_SRC_IN;
		break;
	case MA35_ALPHA_DST_IN:
		*reg = MA35_BLEND_MODE_DST_IN;
		break;
	case MA35_ALPHA_SRC_OUT:
		*reg = MA35_BLEND_MODE_SRC_OUT;
		break;
	case MA35_ALPHA_DST_OUT:
		*reg = MA35_BLEND_MODE_DST_OUT;
		break;
	case MA35_ALPHA_SRC_ATOP:
		*reg = MA35_BLEND_MODE_SRC_ATOP;
		break;
	case MA35_ALPHA_DST_ATOP:
		*reg = MA35_BLEND_MODE_DST_ATOP;
		break;
	case MA35_ALPHA_XOR:
		*reg = MA35_BLEND_MODE_XOR;
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

/**
 * check for coordinate, colorspace(done at init), format, buffer size
 * primary: colorkey, background
 * overlay: alpha, colorkey
 * cursor: cursor size, hotspot
 */
static int ma35_plane_atomic_check(struct drm_plane *drm_plane,
				      struct drm_atomic_state *state)
{
	struct drm_device *drm_dev = drm_plane->dev;
	struct ma35_layer *layer = ma35_layer(drm_plane);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_crtc *crtc = new_state->crtc;
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_crtc_state *crtc_state;
	bool can_position;

	if (!crtc)
		return 0;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	if (new_state->crtc_x < 0 || new_state->crtc_y < 0) {
		drm_err(drm_dev,
			"Negative on-CRTC positions are not supported.\n");
		return -EINVAL;
	}

	if (layer->config.blend_mode > MA35_ALPHA_XOR) {
		drm_err(drm_dev, "Invalid blend mode\n");
		return -EINVAL;
	}

	if (layer->config.swizzle > MA35_SWIZZLE_UV) {
		drm_err(drm_dev, "Invalid swizzle mode\n");
		return -EINVAL;
	}

	if ((layer->config.swizzle & MA35_SWIZZLE_ARGB_MASK) &&
		fb->format->is_yuv) {
		drm_err(drm_dev, "Invalid swizzle mode for RGB format\n");
		return -EINVAL;
	}

	if ((layer->config.swizzle & MA35_SWIZZLE_UV_MASK) &&
		!fb->format->is_yuv) {
		drm_err(drm_dev, "Invalid swizzle mode for YUV format\n");
		return -EINVAL;
	}

	can_position = (drm_plane->type != DRM_PLANE_TYPE_PRIMARY);
	return drm_atomic_helper_check_plane_state(new_state, crtc_state,
						  DRM_PLANE_NO_SCALING, DRM_PLANE_NO_SCALING,
						  can_position, true);
}

static int ma35_cursor_plane_atomic_check(struct drm_plane *drm_plane,
				      struct drm_atomic_state *state)
{
	struct drm_device *drm_dev = drm_plane->dev;
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_framebuffer *fb = new_state->fb;
	struct drm_crtc *crtc = new_state->crtc;
	struct drm_crtc_state *crtc_state;

	// allowed for user deattach cursor plane
	if(!fb)
		return 0;

	if (!crtc)
		return -EINVAL;

	crtc_state = drm_atomic_get_new_crtc_state(state, crtc);
	if (WARN_ON(!crtc_state))
		return -EINVAL;

	if (fb->format->format != DRM_FORMAT_XRGB8888) {
		drm_err(drm_dev, "Invalid cursor format\n");
		return -EINVAL;
	}

	if (new_state->crtc_w != MA35_CURSOR_SIZE || new_state->crtc_h != MA35_CURSOR_SIZE) {
		drm_err(drm_dev, "Unsupported cursor size: %ux%u\n",
				new_state->crtc_w, new_state->crtc_h);
		return -EINVAL;
	}

	return drm_atomic_helper_check_plane_state(new_state, crtc_state,
						  DRM_PLANE_NO_SCALING, DRM_PLANE_NO_SCALING,
						  true, true);
}

static int ma35_cursor_plane_atomic_async_check(struct drm_plane *drm_plane,
				      struct drm_atomic_state *state)
{
	return ma35_cursor_plane_atomic_check(drm_plane, state);
}

static void ma35_overlay_position_update(struct ma35_drm *priv,
						int x, int y, uint32_t w, uint32_t h)
{
	u32 reg;
	int right, bottom;

	right = x + w;
	bottom = y + h;

	x = (x < 0) ? 0 : x;
	y = (y < 0) ? 0 : y;
	right = (right < 0) ? 0 : right;
	bottom = (bottom < 0) ? 0 : bottom;

	reg = FIELD_PREP(MA35_OVERLAY_POSITION_X_MASK, x) |
		  FIELD_PREP(MA35_OVERLAY_POSITION_Y_MASK, y);
	regmap_write(priv->regmap, MA35_OVERLAY_TL, reg);

	reg = FIELD_PREP(MA35_OVERLAY_POSITION_X_MASK, right) |
		  FIELD_PREP(MA35_OVERLAY_POSITION_Y_MASK, bottom);
	regmap_write(priv->regmap, MA35_OVERLAY_BR, reg);
}

/**
 * Update the following properties:
 * buffer address
 * primary: buffer stride, format, swizzle, colorkey
 * overlay: buffer stride, format, swizzle, alpha, colorkey
 * cursor: hotspot related
 * @ crtc
 * timing, gamma
 */
static void ma35_plane_atomic_update(struct drm_plane *drm_plane,
					struct drm_atomic_state *state)
{
	struct ma35_layer *layer = ma35_layer(drm_plane);
	struct ma35_drm *priv = ma35_drm(drm_plane->dev);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_framebuffer *fb = new_state->fb;
	u32 format, bpp, reg;
	u32 *preg;

	layer->config.fourcc = fb->format->format;
	ma35_layer_format_validate(fb->format->format, &format, &bpp);

	if (drm_plane->type == DRM_PLANE_TYPE_PRIMARY) {
		reg = FIELD_PREP(MA35_PRIMARY_FORMAT_MASK, format) |
			  FIELD_PREP(MA35_PRIMARY_SWIZZLE_MASK, layer->config.swizzle) |
			  MA35_PRIMARY_RESET | MA35_PRIMARY_ENABLE;
		if(layer->config.colorkeylo || layer->config.colorkeyup) {
			reg |= FIELD_PREP(MA35_PRIMARY_TRANSPARENCY_MASK, MA35_COLORKEY_ENABLE);
		} else {
			reg |= FIELD_PREP(MA35_PRIMARY_TRANSPARENCY_MASK, MA35_COLORKEY_DISABLE);
		}
		regmap_write(priv->regmap, MA35_FRAMEBUFFER_CONFIG, reg);

		reg = FIELD_PREP(MA35_LAYER_FB_HEIGHT, fb->height) |
			  FIELD_PREP(MA35_LAYER_FB_WIDTH, fb->width);
		regmap_write(priv->regmap, MA35_FRAMEBUFFER_SIZE, reg);

		// background
		regmap_write(priv->regmap, MA35_FRAMEBUFFER_BGCOLOR, layer->config.background);

		// clear value, unused
		regmap_write(priv->regmap, MA35_FRAMEBUFFER_CLEARVALUE, 0);

		// colorkey
		regmap_write(priv->regmap, MA35_FRAMEBUFFER_COLORKEY, layer->config.colorkeylo);
		regmap_write(priv->regmap, MA35_FRAMEBUFFER_COLORHIGHKEY, layer->config.colorkeyup);
	} else if (drm_plane->type == DRM_PLANE_TYPE_OVERLAY) {
		reg = FIELD_PREP(MA35_OVERLAY_FORMAT_MASK, format) |
			  FIELD_PREP(MA35_OVERLAY_SWIZZLE_MASK, layer->config.swizzle) |
			  MA35_OVERLAY_ENABLE;
		if(layer->config.colorkeylo || layer->config.colorkeyup) {
			reg |= FIELD_PREP(MA35_OVERLAY_TRANSPARENCY_MASK, MA35_COLORKEY_ENABLE);
		} else {
			reg |= FIELD_PREP(MA35_OVERLAY_TRANSPARENCY_MASK, MA35_COLORKEY_DISABLE);
		}
		regmap_write(priv->regmap, MA35_OVERLAY_CONFIG, reg);

		reg = FIELD_PREP(MA35_LAYER_FB_HEIGHT, fb->height) |
			FIELD_PREP(MA35_LAYER_FB_WIDTH, fb->width);
		regmap_write(priv->regmap, MA35_OVERLAY_SIZE, reg);
		// can_position
		ma35_overlay_position_update(priv, new_state->crtc_x, new_state->crtc_y,
			new_state->crtc_w, new_state->crtc_h);
		// alpha blending
		if (fb->format->format == DRM_FORMAT_ARGB8888) {
			ma35_layer_blend_mode_select(layer->config.blend_mode, &reg);
			reg |= FIELD_PREP(MA35_SRC_ALPHA_MODE, (u32)layer->config.alpha_mode[0]) |
				   FIELD_PREP(MA35_DST_ALPHA_MODE, (u32)layer->config.alpha_mode[1]);
			regmap_write(priv->regmap, MA35_OVERLAY_ALPHA_BLEND_CONFIG, reg);

			regmap_write(priv->regmap, MA35_OVERLAY_SRC_GLOBAL_COLOR, layer->config.src_color);
			regmap_write(priv->regmap, MA35_OVERLAY_DST_GLOBAL_COLOR, layer->config.dst_color);
		} else {
			regmap_update_bits(priv->regmap, MA35_OVERLAY_ALPHA_BLEND_CONFIG,
				MA35_ALPHA_BLEND_DISABLE, MA35_ALPHA_BLEND_DISABLE);
		}

		// clear value, unused
		regmap_write(priv->regmap, MA35_OVERLAY_CLEAR_VALUE, 0);

		// colorkey
		regmap_write(priv->regmap, MA35_OVERLAY_COLOR_KEY, layer->config.colorkeylo);
		regmap_write(priv->regmap, MA35_OVERLAY_COLOR_KEY_HIGH, layer->config.colorkeyup);
	}

	// retrieves DMA address set by userspace, see drm_mode_fb_cmd2
	for (int i = 0; i < fb->format->num_planes; i++) {
		layer->fb_base[i] = drm_fb_dma_get_gem_addr(fb, new_state, i);
		preg = ma35_plane_properties[drm_plane->type].fb_addr;
		regmap_write(priv->regmap, preg[i], layer->fb_base[i]);
		preg = ma35_plane_properties[drm_plane->type].fb_stride;
		regmap_write(priv->regmap, preg[i], fb->pitches[i]);
	}
}

static void ma35_cursor_position_update(struct ma35_drm *priv, int x, int y)
{
	u32 reg;

	x = (x < 0) ? 0 : x;
	y = (y < 0) ? 0 : y;

	reg = FIELD_PREP(MA35_CURSOR_X_MASK, x) |
		  FIELD_PREP(MA35_CURSOR_Y_MASK, y);
	regmap_write(priv->regmap, MA35_CURSOR_LOCATION, reg);
}

static void ma35_cursor_plane_atomic_update(struct drm_plane *drm_plane,
					struct drm_atomic_state *state)
{
	struct ma35_layer *layer = ma35_layer(drm_plane);
	struct ma35_drm *priv = ma35_drm(drm_plane->dev);
	struct drm_plane_state *old_state =
		drm_atomic_get_old_plane_state(state, drm_plane);
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_framebuffer *old_fb = old_state->fb;
	struct drm_framebuffer *new_fb = new_state->fb;

	if (!new_state->visible) {
		regmap_update_bits(priv->regmap, MA35_CURSOR_CONFIG,
			MA35_CURSOR_FORMAT_MASK, MA35_CURSOR_FORMAT_DISABLE);
		return;
	}

	// update position
	ma35_cursor_position_update(priv, new_state->crtc_x, new_state->crtc_y);

	// check new_state is different from old_state for dimensions or format changed
	if (!old_fb || old_fb != new_fb) {
		layer->fb_base[0] = drm_fb_dma_get_gem_addr(new_fb, new_state, 0);
		regmap_write(priv->regmap, MA35_CURSOR_ADDRESS, layer->fb_base[0]);

		regmap_update_bits(priv->regmap, MA35_CURSOR_CONFIG,
			MA35_CURSOR_FORMAT_MASK, MA35_CURSOR_FORMAT_A8R8G8B8);
	}
}

static void ma35_cursor_plane_atomic_async_update(struct drm_plane *drm_plane,
					struct drm_atomic_state *state)
{
	struct ma35_layer *layer = ma35_layer(drm_plane);
	struct ma35_drm *priv = ma35_drm(drm_plane->dev);
	struct drm_plane_state *old_state = drm_plane->state;
	struct drm_plane_state *new_state =
		drm_atomic_get_new_plane_state(state, drm_plane);
	struct drm_framebuffer *old_fb = old_state->fb;
	struct drm_framebuffer *new_fb = new_state->fb;

	// update the current one with the new plane state
	old_state->crtc_x = new_state->crtc_x;
	old_state->crtc_y = new_state->crtc_y;
	old_state->crtc_h = new_state->crtc_h;
	old_state->crtc_w = new_state->crtc_w;
	old_state->src_x = new_state->src_x;
	old_state->src_y = new_state->src_y;
	old_state->src_h = new_state->src_h;
	old_state->src_w = new_state->src_w;
	// swap current and new framebuffers
	swap(old_fb, new_fb);

	if (!new_state->visible) {
		regmap_update_bits(priv->regmap, MA35_CURSOR_CONFIG,
			MA35_CURSOR_FORMAT_MASK, MA35_CURSOR_FORMAT_DISABLE);
		return;
	}

	// update position
	ma35_cursor_position_update(priv, new_state->crtc_x, new_state->crtc_y);

	// check new_state is different from old_state for dimensions or format changed
	if (!old_fb || old_fb != new_fb) {
		layer->fb_base[0] = drm_fb_dma_get_gem_addr(new_fb, new_state, 0);
		regmap_write(priv->regmap, MA35_CURSOR_ADDRESS, layer->fb_base[0]);

		regmap_update_bits(priv->regmap, MA35_CURSOR_CONFIG,
			MA35_CURSOR_FORMAT_MASK, MA35_CURSOR_FORMAT_A8R8G8B8);
	}
}

static void ma35_plane_atomic_disable(struct drm_plane *drm_plane,
					 struct drm_atomic_state *state)
{
	struct ma35_drm *priv = ma35_drm(drm_plane->dev);

	regmap_update_bits(priv->regmap, MA35_FRAMEBUFFER_CONFIG,
		MA35_PRIMARY_ENABLE, 0);
}

static void ma35_cursor_plane_atomic_disable(struct drm_plane *drm_plane,
					 struct drm_atomic_state *state)
{
	struct ma35_drm *priv = ma35_drm(drm_plane->dev);

	regmap_update_bits(priv->regmap, MA35_CURSOR_CONFIG,
		MA35_CURSOR_FORMAT_MASK, MA35_CURSOR_FORMAT_DISABLE);
}

static struct drm_plane_helper_funcs ma35_plane_helper_funcs = {
	.atomic_check		= ma35_plane_atomic_check,
	.atomic_update		= ma35_plane_atomic_update,
	.atomic_disable		= ma35_plane_atomic_disable,
};

static struct drm_plane_helper_funcs ma35_cursor_plane_helper_funcs = {
	.atomic_check		= ma35_cursor_plane_atomic_check,
	.atomic_update		= ma35_cursor_plane_atomic_update,
	.atomic_disable		= ma35_cursor_plane_atomic_disable,
	.atomic_async_check		= ma35_cursor_plane_atomic_async_check,
	.atomic_async_update	= ma35_cursor_plane_atomic_async_update,
};

static int ma35_plane_set_property(struct drm_plane *drm_plane,
	struct drm_plane_state *state, struct drm_property *property,
	uint64_t val)
{
	struct ma35_layer *layer = ma35_layer(drm_plane);

	if (property == layer->blend_mode_prop) {
		layer->config.blend_mode = val;
	} else if (property == layer->src_alpha_mode_prop) {
		layer->config.alpha_mode[0] = val;
	} else if (property == layer->dst_alpha_mode_prop) {
		layer->config.alpha_mode[1] = val;
	} else if (property == layer->src_color_prop) {
		layer->config.src_color = val;
	} else if (property == layer->dst_color_prop) {
		layer->config.dst_color = val;
	} else if (property == layer->swizzle_prop) {
		layer->config.swizzle = val;
	} else if (property == layer->colorkeylo_prop) {
		layer->config.colorkeylo = val;
	} else if (property == layer->colorkeyup_prop) {
		layer->config.colorkeyup = val;
	} else if (property == layer->background_prop) {
		layer->config.background = val;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int ma35_plane_get_property(struct drm_plane *drm_plane,
	const struct drm_plane_state *state, struct drm_property *property,
	uint64_t *val)
{
	struct ma35_layer *layer = ma35_layer(drm_plane);

	if (property == layer->blend_mode_prop) {
		*val = layer->config.blend_mode;
	} else if (property == layer->src_alpha_mode_prop) {
		*val = layer->config.alpha_mode[0];
	} else if (property == layer->dst_alpha_mode_prop) {
		*val = layer->config.alpha_mode[1];
	} else if (property == layer->src_color_prop) {
		*val = layer->config.src_color;
	} else if (property == layer->dst_color_prop) {
		*val = layer->config.dst_color;
	} else if (property == layer->swizzle_prop) {
		*val = layer->config.swizzle;
	} else if (property == layer->colorkeylo_prop) {
		*val = layer->config.colorkeylo;
	} else if (property == layer->colorkeyup_prop) {
		*val = layer->config.colorkeyup;
	} else if (property == layer->background_prop) {
		*val = layer->config.background;
	} else {
		return -EINVAL;
	}

	return 0;
}

static const struct drm_plane_funcs ma35_plane_funcs = {
	.update_plane		= drm_atomic_helper_update_plane,
	.disable_plane		= drm_atomic_helper_disable_plane,
	.destroy		= drm_plane_cleanup,
	.reset			= drm_atomic_helper_plane_reset,
	.atomic_duplicate_state	= drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_plane_destroy_state,
	.atomic_set_property = ma35_plane_set_property,
	.atomic_get_property = ma35_plane_get_property,
};

static int ma35_layer_create_properties(struct ma35_drm *priv,
				      struct ma35_layer *layer)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct drm_plane *drm_plane = &layer->drm_plane;
	int ret = 0;

	layer->formats = ma35_layer_formats;
	layer->config.depth = mode_config->preferred_depth;

	if (ma35_plane_properties[drm_plane->type].alpha) {
		layer->blend_mode_prop = drm_property_create_enum(drm_dev, 0, "porter-duff-blend-mode",
							ma35_blend_modes, ARRAY_SIZE(ma35_blend_modes));
		if (!layer->blend_mode_prop) {
			drm_err(drm_dev, "Failed to create blend mode property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->blend_mode_prop, MA35_ALPHA_SRC_OVER);
		layer->config.blend_mode = MA35_ALPHA_SRC_OVER;

		layer->src_alpha_mode_prop = drm_property_create_enum(drm_dev, 0, "source-alpha-mode",
							ma35_alpha_modes, ARRAY_SIZE(ma35_alpha_modes));
		if (!layer->src_alpha_mode_prop) {
			drm_err(drm_dev, "Failed to create source alpha property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->src_alpha_mode_prop, MA35_ALPHA_MODE_NONE);
		layer->config.alpha_mode[0] = MA35_ALPHA_MODE_NONE;

		layer->dst_alpha_mode_prop = drm_property_create_enum(drm_dev, 0, "destination-alpha-mode",
							ma35_alpha_modes, ARRAY_SIZE(ma35_alpha_modes));
		if (!layer->dst_alpha_mode_prop) {
			drm_err(drm_dev, "Failed to create destination alpha property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->dst_alpha_mode_prop, MA35_ALPHA_MODE_NONE);
		layer->config.alpha_mode[1] = MA35_ALPHA_MODE_NONE;

		layer->src_color_prop = drm_property_create_range(drm_dev, 0, "source-global-color",
							0, 0xffffffff);
		if (!layer->src_color_prop) {
			drm_err(drm_dev, "Failed to create source color property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->src_color_prop, 0);
		layer->config.src_color = 0;

		layer->dst_color_prop = drm_property_create_range(drm_dev, 0, "destination-global-color",
							0, 0xffffffff);
		if (!layer->dst_color_prop) {
			drm_err(drm_dev, "Failed to create destination color property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->dst_color_prop, 0);
		layer->config.dst_color = 0;
	}

	if (ma35_plane_properties[drm_plane->type].swizzle) {
		layer->swizzle_prop = drm_property_create_enum(drm_dev, 0, "swizzle",
							ma35_swizzles, ARRAY_SIZE(ma35_swizzles));
		if (!layer->swizzle_prop) {
			drm_err(drm_dev, "Failed to create swizzle property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->swizzle_prop, MA35_SWIZZLE_ARGB);
		layer->config.swizzle = MA35_SWIZZLE_ARGB;
	}

	if (ma35_plane_properties[drm_plane->type].colorkey) {
		layer->colorkeylo_prop = drm_property_create_range(drm_dev, 0, "colorkey-lower-bound",
							0, 0xffffffff);
		if (!layer->colorkeylo_prop) {
			drm_err(drm_dev, "Failed to create colorkey property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->colorkeylo_prop, 0);
		layer->config.colorkeylo = 0;

		layer->colorkeyup_prop = drm_property_create_range(drm_dev, 0, "colorkey-upper-bound",
							0, 0xffffffff);
		if (!layer->colorkeyup_prop) {
			drm_err(drm_dev, "Failed to create colorkey property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->colorkeyup_prop, 0);
		layer->config.colorkeyup = 0;
	}

	if (ma35_plane_properties[drm_plane->type].background) {
		layer->background_prop = drm_property_create_range(drm_dev, 0, "background",
							0, 0xffffffff);
		if (!layer->background_prop) {
			drm_err(drm_dev, "Failed to create background property\n");
			return -ENOMEM;
		}
		drm_object_attach_property(&drm_plane->base, layer->background_prop, 0);
		layer->config.background = 0;
	}

	return ret;
}

static int ma35_cursor_of_parse(struct ma35_drm *priv,
				      struct ma35_layer *layer)
{
	struct device_node *of_node = layer->of_node;
	u32 offset;
	int ret = 0;

	// just retrieve the default values
	layer->formats = ma35_cursor_formats;
	layer->config.depth = MA35_CURSOR_DEPTH; // no alpha
	layer->config.fourcc = DRM_FORMAT_XRGB8888; // no other format

	/* Hotspot properties created by cursor plane are not supported by v6.6,
	 * so we parse them from device tree.
	 */
	ret = of_property_read_u32(of_node, "HOTSPOT_X", &offset);
	if (ret)
		layer->config.hotspot_x = 0;
	else {
		if (offset >= MA35_CURSOR_SIZE)
			return -1;
		layer->config.hotspot_x = offset;
	}

	ret = of_property_read_u32(of_node, "HOTSPOT_Y", &offset);
	if (ret)
		layer->config.hotspot_y = 0;
	else {
		if (offset >= MA35_CURSOR_SIZE)
			return -1;
		layer->config.hotspot_y = offset;
	}

	u32 reg;
	reg = FIELD_PREP(MA35_CURSOR_HOTSPOT_X_MASK, layer->config.hotspot_x) |
		  FIELD_PREP(MA35_CURSOR_HOTSPOT_Y_MASK, layer->config.hotspot_y);
	regmap_write(priv->regmap, MA35_CURSOR_CONFIG, reg);

	return 0;
}

struct ma35_layer *ma35_layer_get_from_index(struct ma35_drm *priv,
						   u32 index)
{
	struct ma35_layer *layer;

	list_for_each_entry(layer, &priv->layers_list, list) {
		if (layer->index == index)
			return layer;
	}

	return NULL;
}

struct ma35_layer *ma35_layer_get_from_type(struct ma35_drm *priv, enum drm_plane_type type)
{
	struct ma35_layer *layer;
	struct drm_plane *drm_plane;

	list_for_each_entry(layer, &priv->layers_list, list) {
		drm_plane = &layer->drm_plane;
		if (drm_plane->type == type)
			return layer;
	}

	return NULL;
}

static int ma35_layer_create(struct ma35_drm *priv,
			      struct device_node *of_node, u32 index,
			      enum drm_plane_type type)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	struct ma35_layer *layer = NULL;
	const char *str;
	int ret;

	layer = devm_kzalloc(dev, sizeof(*layer), GFP_KERNEL);
	if (!layer) {
		ret = -ENOMEM;
		goto error;
	}

	layer->of_node = of_node;
	layer->index = index;

	if (type == DRM_PLANE_TYPE_CURSOR) {
		ret = drm_universal_plane_init(drm_dev, &layer->drm_plane,
			1 << MA35_DEFAULT_CRTC_ID,
			&ma35_plane_funcs, ma35_cursor_formats,
			ARRAY_SIZE(ma35_cursor_formats), NULL, type, NULL);
		if (ret) {
			drm_err(drm_dev, "Failed to initialize layer plane\n");
			return ret;
		}

		if (ma35_cursor_of_parse(priv, layer)) {
			drm_err(drm_dev, "Unsupported cursor hotspot offset\n");
			goto error;
		}
		drm_plane_helper_add(&layer->drm_plane, &ma35_cursor_plane_helper_funcs);
	} else {
		ret = drm_universal_plane_init(drm_dev, &layer->drm_plane,
			1 << MA35_DEFAULT_CRTC_ID,
			&ma35_plane_funcs, ma35_layer_formats,
			ARRAY_SIZE(ma35_layer_formats), NULL, type, NULL);
		if (ret) {
			drm_err(drm_dev, "Failed to initialize layer plane\n");
			return ret;
		}

		if (ma35_layer_create_properties(priv, layer)) {
			drm_err(drm_dev, "Failed to parse config for layer #%d\n",
				index);
			goto error;
		}
		drm_plane_helper_add(&layer->drm_plane, &ma35_plane_helper_funcs);
	}

	drm_plane_create_zpos_immutable_property(&layer->drm_plane, index);

	of_property_read_string(of_node, "layer-type", &str);
	drm_info(drm_dev, "Registering layer #%d as %s\n", index, str);

	list_add_tail(&layer->list, &priv->layers_list);

	return 0;

error:
	if (layer)
		devm_kfree(dev, layer);

	return ret;
}

static void ma35_layer_fini(struct ma35_drm *priv,
			       struct ma35_layer *layer)
{
	struct device *dev = priv->drm_dev.dev;

	list_del(&layer->list);
	devm_kfree(dev, layer);
}

/* attach overlay to crtc after crtc is init */
void ma35_overlay_attach_crtc(struct ma35_drm *priv)
{
	uint32_t possible_crtcs = drm_crtc_mask(&priv->crtc->drm_crtc);
	struct ma35_layer *layer;
	struct drm_plane *drm_plane;

	list_for_each_entry(layer, &priv->layers_list, list) {
		drm_plane = &layer->drm_plane;
		if (drm_plane->type != DRM_PLANE_TYPE_OVERLAY)
			continue;

		drm_plane->possible_crtcs = possible_crtcs;
	}
}

static bool ma35_of_node_is_layer(struct device_node *of_node)
{
	return !of_node_cmp(of_node->name, "layer");
}

static int ma35_get_layer_type(const char *type_str, enum drm_plane_type *type)
{
	int ret = 0;

	if (!strcmp(type_str, "primary"))
		*type = DRM_PLANE_TYPE_PRIMARY;
	else if (!strcmp(type_str, "overlay"))
		*type = DRM_PLANE_TYPE_OVERLAY;
	else if (!strcmp(type_str, "cursor"))
		*type = DRM_PLANE_TYPE_CURSOR;
	else
		ret = -1;

	return ret;
}

int ma35_plane_init(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *of_node = dev->of_node;
	struct device_node *layer_node = NULL;
	struct device_node *layers_node;
	struct ma35_layer *layer, *next;
	const char *layer_type_str;
	enum drm_plane_type type;
	u32 index;
	int ret;

	layers_node = of_get_child_by_name(of_node, "layers"); // refer to yaml layers node
	if (!layers_node) {
		drm_err(drm_dev, "No layers node found in the description\n");
		ret = -ENODEV;
		goto error;
	}

	for_each_child_of_node(layers_node, layer_node) {
		index = 0;

		if (!ma35_of_node_is_layer(layer_node))
			continue;

		ret = of_property_read_u32(layer_node, "layer-id", &index); // index in layer node
		if (ret)
			continue;

		ret = of_property_read_string(layer_node, "layer-type", &layer_type_str); // layer type
		if (ret) {
			dev_err(dev, "Failed to read 'layer-type' from node %s\n",
				layer_node->name);
			continue;
		}

		ret = ma35_get_layer_type(layer_type_str, &type);
		if(ret) {
			dev_err(dev, "Unknown layer type %s\n", layer_type_str);
			continue;
		}

		// check if layer already exists
		layer = ma35_layer_get_from_index(priv, index);
		if (layer) {
			drm_err(drm_dev, "Duplicated entry for layer#%d\n", // we support one layer per index
				index);
			continue;
		}

		ret = ma35_layer_create(priv, layer_node, index, type);
		if (ret) {
			of_node_put(layers_node);
			goto error;
		}
	}

	of_node_put(layers_node);

	return 0;

error:
	list_for_each_entry_safe(layer, next, &priv->layers_list, list)
		ma35_layer_fini(priv, layer);

	return ret;
}
