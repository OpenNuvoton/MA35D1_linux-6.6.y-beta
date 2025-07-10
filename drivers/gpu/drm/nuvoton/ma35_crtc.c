// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton DRM driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_vblank.h>

#include "ma35_drm.h"

/**
 *               Active                 Front           Sync           Back
 *              Region                 Porch                          Porch
 *     <-----------------------><----------------><-------------><-------------->
 *       //////////////////////|
 *      ////////////////////// |
 *     //////////////////////  |..................               ................
 *                                                _______________
 *     <----- [hv]display ----->
 *     <------------- [hv]sync_start ------------>
 *     <--------------------- [hv]sync_end --------------------->
 *     <-------------------------------- [hv]total ----------------------------->
 */
// pixel rate = htotal * vtotal * frame_rate

#define ma35_crtc(c) \
	container_of(c, struct ma35_crtc, drm_crtc)

static enum drm_mode_status
ma35_crtc_mode_valid(struct drm_crtc *drm_crtc,
			const struct drm_display_mode *mode)
{
	struct drm_device *drm_dev = drm_crtc->dev;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;

	// check drm_mode_status for some limitations

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		return MODE_NO_INTERLACE;

	if (mode->hdisplay > mode_config->max_width || mode->hdisplay < mode_config->min_width)
		return MODE_BAD_HVALUE;

	if (mode->vdisplay > mode_config->max_height || mode->vdisplay < mode_config->min_height)
		return MODE_BAD_VVALUE;

	if (mode->clock > MA35_MAX_PIXEL_CLK) {
		return MODE_CLOCK_HIGH;
	}

	return MODE_OK;
}

static int ma35_crtc_atomic_check(struct drm_crtc *drm_crtc,
					 struct drm_atomic_state *state)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	struct drm_crtc_state *crtc_state = drm_atomic_get_new_crtc_state(state, drm_crtc);
	struct drm_display_mode *mode = &crtc_state->mode;
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	int clk_rate;

	if (mode->clock > MA35_MAX_PIXEL_CLK)
		return MODE_CLOCK_HIGH;

	// check rounded pixel clock
	clk_rate = clk_round_rate(priv->dcupclk, mode->clock * 1000);
	if (clk_rate <= 0)
		return MODE_CLOCK_RANGE;

	adjusted_mode->clock = DIV_ROUND_UP(clk_rate, 1000);

	return 0;
}

static void ma35_crtc_atomic_enable(struct drm_crtc *drm_crtc,
				       struct drm_atomic_state *state)
{
	struct ma35_crtc *crtc = ma35_crtc(drm_crtc);
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	struct drm_crtc_state *new_state =
		drm_atomic_get_new_crtc_state(state, drm_crtc);
	struct drm_display_mode *mode = &new_state->adjusted_mode;
	struct drm_color_lut *lut;
	struct drm_connector *connector = &priv->interface->drm_connector;
	struct drm_display_info *display_info = &connector->display_info;
	int i, size;
	u32 reg;

	/* Timings */
	reg = FIELD_PREP(MA35_DISPLAY_TOTAL_MASK, mode->htotal) |
		  FIELD_PREP(MA35_DISPLAY_ACTIVE_MASK, mode->hdisplay);
	regmap_write(priv->regmap, MA35_HDISPLAY, reg);

	reg = MA35_SYNC_PULSE_ENABLE |
		  FIELD_PREP(MA35_SYNC_START_MASK, mode->hsync_start) |
		  FIELD_PREP(MA35_SYNC_END_MASK, mode->hsync_end);
	if (mode->flags & DRM_MODE_FLAG_NHSYNC)
		reg |= MA35_SYNC_POLARITY_BIT;
	regmap_write(priv->regmap, MA35_HSYNC, reg);

	reg = FIELD_PREP(MA35_DISPLAY_TOTAL_MASK, mode->vtotal) |
		  FIELD_PREP(MA35_DISPLAY_ACTIVE_MASK, mode->vdisplay);
	regmap_write(priv->regmap, MA35_VDISPLAY, reg);

	reg = MA35_SYNC_PULSE_ENABLE |
		  FIELD_PREP(MA35_SYNC_START_MASK, mode->vsync_start) |
		  FIELD_PREP(MA35_SYNC_END_MASK, mode->vsync_end);
	if (mode->flags & DRM_MODE_FLAG_NVSYNC)
		reg |= MA35_SYNC_POLARITY_BIT;
	regmap_write(priv->regmap, MA35_VSYNC, reg);

	/* Signals */
	reg = MA35_PANEL_DATA_ENABLE_ENABLE | MA35_PANEL_DATA_ENABLE |
		  MA35_PANEL_DATA_CLOCK_ENABLE;
	if (display_info->bus_flags & DRM_BUS_FLAG_DE_LOW)
		reg |= MA35_PANEL_DATA_ENABLE_POLARITY;

	if(display_info->bus_flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		reg |= MA35_PANEL_DATA_POLARITY;
	regmap_write(priv->regmap, MA35_PANEL_CONFIG, reg);

	/* Gamma */
	if (new_state->gamma_lut) {
		if (new_state->color_mgmt_changed) {
			lut = new_state->gamma_lut->data;
			size = new_state->gamma_lut->length / sizeof(struct drm_color_lut);

			for (i = 0; i < size; i++) {
				regmap_write(priv->regmap, MA35_GAMMA_INDEX, i); // auto increment in spec though
				// shift DRM gamma 16-bit values to 10-bit
				reg = FIELD_PREP(MA35_GAMMA_RED_MASK, lut[i].red >> 6) |
						FIELD_PREP(MA35_GAMMA_GREEN_MASK, lut[i].green >> 6) |
						FIELD_PREP(MA35_GAMMA_BLUE_MASK, lut[i].blue >> 6);
				regmap_write(priv->regmap, MA35_GAMMA_DATA, reg);
			}
		}
		/* Enable gamma */
		regmap_update_bits(priv->regmap, MA35_FRAMEBUFFER_CONFIG,
						   MA35_PRIMARY_GAMMA, MA35_PRIMARY_GAMMA);
	} else {
		/* Disable gamma */
		regmap_update_bits(priv->regmap, MA35_FRAMEBUFFER_CONFIG,
						   MA35_PRIMARY_GAMMA, 0);
	}

	/* Dither */
	if (crtc->dither_enable) {
		for (i = 0, reg = 0; i < MA35_DITHER_TABLE_ENTRY / 2; i++) {
			reg |= (crtc->dither_depth & MA35_DITHER_TABLE_MASK) << (i * 4);
		}
		regmap_write(priv->regmap, MA35_DISPLAY_DITHER_TABLE_LOW, reg);
		regmap_write(priv->regmap, MA35_DISPLAY_DITHER_TABLE_HIGH, reg);
		regmap_write(priv->regmap, MA35_DISPLAY_DITHER_CONFIG, MA35_DITHER_ENABLE);
	} else {
		regmap_write(priv->regmap, MA35_DISPLAY_DITHER_CONFIG, 0);
	}

	drm_crtc_vblank_on(drm_crtc);
}

static void ma35_crtc_atomic_disable(struct drm_crtc *drm_crtc,
					struct drm_atomic_state *state)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	struct drm_device *drm_dev = drm_crtc->dev;

	drm_crtc_vblank_off(drm_crtc);

	/* Disable and clear CRTC bits. */
	regmap_update_bits(priv->regmap, MA35_PANEL_CONFIG,
					   MA35_PANEL_DATA_ENABLE_ENABLE, 0);
	regmap_update_bits(priv->regmap, MA35_FRAMEBUFFER_CONFIG,
					   MA35_PRIMARY_GAMMA, 0);
	regmap_write(priv->regmap, MA35_DISPLAY_DITHER_CONFIG, 0);

	/* Consume any leftover event since vblank is now disabled. */
	if (drm_crtc->state->event && !drm_crtc->state->active) {
		spin_lock_irq(&drm_dev->event_lock);

		drm_crtc_send_vblank_event(drm_crtc, drm_crtc->state->event);
		drm_crtc->state->event = NULL;
		spin_unlock_irq(&drm_dev->event_lock);
	}
}

static void ma35_crtc_atomic_flush(struct drm_crtc *drm_crtc,
	struct drm_atomic_state *state)
{
	spin_lock_irq(&drm_crtc->dev->event_lock);
	if (drm_crtc->state->event) {
		if (drm_crtc_vblank_get(drm_crtc) == 0)
			drm_crtc_arm_vblank_event(drm_crtc, drm_crtc->state->event);
		else
			drm_crtc_send_vblank_event(drm_crtc, drm_crtc->state->event);

		drm_crtc->state->event = NULL;
	}
	spin_unlock_irq(&drm_crtc->dev->event_lock);
}

static bool ma35_crtc_get_scanout_position(struct drm_crtc *drm_crtc,
					   bool in_vblank_irq,
					   int *vpos,
					   int *hpos,
					   ktime_t *stime,
					   ktime_t *etime,
					   const struct drm_display_mode *mode)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	u32 reg;

	if (stime)
		*stime = ktime_get();

	regmap_read(priv->regmap, MA35_DISPLAY_CURRENT_LOCATION, &reg);

	*hpos = FIELD_GET(MA35_DISPLAY_CURRENT_X, reg);
	*vpos = FIELD_GET(MA35_DISPLAY_CURRENT_Y, reg);

	if (etime)
		*etime = ktime_get();

	return true;
}

static const struct drm_crtc_helper_funcs ma35_crtc_helper_funcs = {
	.mode_valid		= ma35_crtc_mode_valid,
	.atomic_check		= ma35_crtc_atomic_check,
	.atomic_enable		= ma35_crtc_atomic_enable,
	.atomic_disable		= ma35_crtc_atomic_disable,
	.atomic_flush		= ma35_crtc_atomic_flush,
	.get_scanout_position	= ma35_crtc_get_scanout_position,
};

static int ma35_crtc_enable_vblank(struct drm_crtc *drm_crtc)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);

	regmap_write(priv->regmap, MA35_DISPLAY_INTRENABLE,
			  MA35_CRTC_VBLANK);

	return 0;
}

static void ma35_crtc_disable_vblank(struct drm_crtc *drm_crtc)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);

	regmap_write(priv->regmap, MA35_DISPLAY_INTRENABLE, 0);
}

static u32 ma35_crtc_get_vblank_counter(struct drm_crtc *drm_crtc)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	u32 val;

	spin_lock(&priv->crtc->vblank_lock);
	val = priv->crtc->vblank_counter;
	spin_unlock(&priv->crtc->vblank_lock);

	return val;
}

static int ma35_crtc_gamma_set(struct drm_crtc *drm_crtc,
		  u16 *r, u16 *g, u16 *b, uint32_t size,
		  struct drm_modeset_acquire_ctx *ctx)
{
	struct ma35_drm *priv = ma35_drm(drm_crtc->dev);
	u32 reg;
	int i;

	if (size != MA35_GAMMA_TABLE_SIZE)
		return -EINVAL;

	regmap_write(priv->regmap, MA35_GAMMA_INDEX, 0); // auto increment

	for (i = 0; i < size; i++) {
		reg = FIELD_PREP(MA35_GAMMA_RED_MASK, r[i]) |
			  FIELD_PREP(MA35_GAMMA_GREEN_MASK, g[i]) |
			  FIELD_PREP(MA35_GAMMA_BLUE_MASK, b[i]);
		regmap_write(priv->regmap, MA35_GAMMA_DATA, reg);
	}

	return 0;
}

static int ma35_crtc_atomic_set_property(struct drm_crtc *drm_crtc,
					 struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t value)
{
	struct ma35_crtc *crtc = ma35_crtc(drm_crtc);

	if (property == crtc->dither_enable_prop) {
		crtc->dither_enable = value;
	} else if (property == crtc->dither_depth_prop) {
		crtc->dither_depth = value;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int ma35_crtc_atomic_get_property(struct drm_crtc *drm_crtc,
					 const struct drm_crtc_state *state,
					 struct drm_property *property,
					 uint64_t *value)
{
	struct ma35_crtc *crtc = ma35_crtc(drm_crtc);

	if (property == crtc->dither_enable_prop) {
		*value = crtc->dither_enable;
	} else if (property == crtc->dither_depth_prop) {
		*value = crtc->dither_depth;
	} else {
		return -EINVAL;
	}

	return 0;
}

static const struct drm_crtc_funcs ma35_crtc_funcs = {
	.reset			= drm_atomic_helper_crtc_reset,
	.destroy		= drm_crtc_cleanup,
	.set_config		= drm_atomic_helper_set_config,
	.page_flip		= drm_atomic_helper_page_flip,
	.atomic_duplicate_state	= drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_crtc_destroy_state,
	.enable_vblank		= ma35_crtc_enable_vblank,
	.disable_vblank		= ma35_crtc_disable_vblank,
	.get_vblank_counter = ma35_crtc_get_vblank_counter,
	.gamma_set      = ma35_crtc_gamma_set,
	.atomic_set_property = ma35_crtc_atomic_set_property,
	.atomic_get_property = ma35_crtc_atomic_get_property,
};

void ma35_crtc_vblank_handler(struct ma35_drm *priv)
{
	struct ma35_crtc *crtc = priv->crtc;

	if (!crtc)
		return;

	crtc->vblank_counter++;

	drm_crtc_handle_vblank(&crtc->drm_crtc);
}

static int ma35_crtc_create_properties(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct ma35_crtc *crtc = priv->crtc;
	struct drm_crtc *drm_crtc = &crtc->drm_crtc;

	crtc->dither_enable_prop = drm_property_create_bool(drm_dev, 0, "dither-enable");
	if (!crtc->dither_enable_prop) {
		drm_err(drm_dev, "Failed to create dither enable property\n");
		return -ENOMEM;
	}
	drm_object_attach_property(&drm_crtc->base, crtc->dither_enable_prop, false);
	crtc->dither_enable = false;

	crtc->dither_depth_prop = drm_property_create_range(drm_dev, 0, "dither-depth",
							0, 0xf);
	if (!crtc->dither_depth_prop) {
		drm_err(drm_dev, "Failed to create dither depth property\n");
		return -ENOMEM;
	}
	drm_object_attach_property(&drm_crtc->base, crtc->dither_depth_prop, 0);
	crtc->dither_depth = 0;

	return 0;
}

int ma35_crtc_init(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	struct ma35_crtc *crtc;
	struct ma35_layer *layer_primary, *layer_cursor;
	struct drm_plane *cursor_plane = NULL;
	int ret;

	crtc = devm_kzalloc(dev, sizeof(*crtc), GFP_KERNEL);
	if (!crtc)
		return -ENOMEM;

	priv->crtc = crtc;
	crtc->vblank_counter = 0;

	layer_primary = ma35_layer_get_from_type(priv, DRM_PLANE_TYPE_PRIMARY);
	if (!layer_primary) {
		drm_err(drm_dev, "Failed to get primary layer\n");
		return -EINVAL;
	}

	// optional
	layer_cursor = ma35_layer_get_from_type(priv, DRM_PLANE_TYPE_CURSOR);
	if (layer_cursor)
		cursor_plane = &layer_cursor->drm_plane;

	// attach primary and cursor
	ret = drm_crtc_init_with_planes(drm_dev, &crtc->drm_crtc,
					&layer_primary->drm_plane, cursor_plane,
					&ma35_crtc_funcs, NULL);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize CRTC\n");
		return ret;
	}

	// attach overlay
	ma35_overlay_attach_crtc(priv);

	// dither & gamma
	ret = ma35_crtc_create_properties(priv);
	if (ret)
		return ret;
	ret = drm_mode_crtc_set_gamma_size(&crtc->drm_crtc, MA35_GAMMA_TABLE_SIZE);
	if (ret)
		return ret;
	drm_crtc_enable_color_mgmt(&crtc->drm_crtc, 0, false, MA35_GAMMA_TABLE_SIZE);

	drm_crtc_helper_add(&crtc->drm_crtc, &ma35_crtc_helper_funcs);

	return 0;
}
