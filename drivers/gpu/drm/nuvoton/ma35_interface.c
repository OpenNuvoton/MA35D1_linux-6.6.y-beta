// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton DRM driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#include <linux/types.h>
#include <linux/clk.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_drv.h>
#include <drm/drm_edid.h>
#include <drm/drm_encoder.h>
#include <drm/drm_gem_dma_helper.h>
#include <drm/drm_modeset_helper_vtables.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#include "ma35_drm.h"

#define ma35_encoder(e) \
	container_of(e, struct ma35_interface, drm_encoder)
#define ma35_connector(c) \
	container_of(c, struct ma35_interface, drm_connector)

static const struct drm_prop_enum_list ma35_dpi_modes[] = {
	{ MA35_DPI_D16CFG1, "D16CFG1" },
	{ MA35_DPI_D16CFG2, "D16CFG2" },
	{ MA35_DPI_D16CFG3, "D16CFG3" },
	{ MA35_DPI_D18CFG1, "D18CFG1" },
	{ MA35_DPI_D18CFG2, "D18CFG2" },
	{ MA35_DPI_D24, "D24" },
};

static int ma35_encoder_atomic_check(struct drm_encoder *encoder,
	struct drm_crtc_state *crtc_state,
	struct drm_connector_state *conn_state)
{
	struct drm_device *drm_dev = encoder->dev;
	struct ma35_interface *interface = ma35_encoder(encoder);

	if (interface->dpi_mode > MA35_DPI_D24) {
		drm_err(drm_dev, "Invalid DPI mode\n");
		return -EINVAL;
	}

	return 0;
}

static void ma35_encoder_atomic_enable(struct drm_encoder *encoder,
	struct drm_atomic_state *state)
{
	struct ma35_drm *priv = ma35_drm(encoder->dev);
	struct ma35_interface *interface = ma35_encoder(encoder);
	u32 reg;

	reg = FIELD_PREP(MA35_DPI_FORMAT_MASK, interface->dpi_mode);
	regmap_write(priv->regmap, MA35_DPI_CONFIG, reg);
}

static void ma35_encoder_mode_set(struct drm_encoder *encoder,
	struct drm_crtc_state *crtc_state,
	struct drm_connector_state *conn_state)
{
	struct drm_device *drm_dev = encoder->dev;
	struct ma35_drm *priv = ma35_drm(drm_dev);
	struct drm_display_mode *adjusted_mode = &crtc_state->adjusted_mode;
	int result;

	// pixel clock it checked in crtc
	clk_set_rate(priv->dcupclk, adjusted_mode->clock * 1000);
	result = DIV_ROUND_UP(clk_get_rate(priv->dcupclk), 1000);
	drm_dbg(drm_dev, "Pixel clock: %d kHz; request : %d kHz\n", result, adjusted_mode->clock);
}

static const struct drm_encoder_helper_funcs ma35_encoder_helper_funcs = {
	.atomic_check		= ma35_encoder_atomic_check,
	.atomic_enable		= ma35_encoder_atomic_enable,
	.atomic_mode_set	= ma35_encoder_mode_set,
};

static int ma35_connector_atomic_set_property(struct drm_connector *connector,
	struct drm_connector_state *state,
	struct drm_property *property,
	uint64_t val)
{
	struct ma35_interface *interface = ma35_connector(connector);

	if (property == interface->dpi_prop) {
		interface->dpi_mode = val;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int ma35_connector_atomic_get_property(struct drm_connector *connector,
	const struct drm_connector_state *state,
	struct drm_property *property,
	uint64_t *val)
{
	struct ma35_interface *interface = ma35_connector(connector);

	if (property == interface->dpi_prop) {
		*val = interface->dpi_mode;
	} else {
		return -EINVAL;
	}

	return 0;
}

static const struct drm_connector_funcs ma35_connector_funcs = {
	.reset			= drm_atomic_helper_connector_reset,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
	.atomic_set_property	= ma35_connector_atomic_set_property,
	.atomic_get_property	= ma35_connector_atomic_get_property,
};

static int ma35_connector_get_modes(struct drm_connector *drm_connector)
{
	struct ma35_drm *priv = ma35_drm(drm_connector->dev);
	struct drm_device *drm_dev = &priv->drm_dev;
	struct drm_mode_config *mode_config = &drm_dev->mode_config;
	struct ma35_interface *interface = ma35_connector(drm_connector);
	int count;

	if (!interface->drm_bridge_panel) {
		/* Use the default modes list from DRM */
		count = drm_add_modes_noedid(drm_connector, mode_config->max_width, mode_config->max_height);
		drm_set_preferred_mode(drm_connector, mode_config->max_width, mode_config->max_height);

		return count;
	} else {
		return drm_bridge_get_modes(interface->drm_bridge_panel, drm_connector);
	}
}

static const struct drm_connector_helper_funcs ma35_connector_helper_funcs = {
	.get_modes		= ma35_connector_get_modes,
};

static void ma35_encoder_attach_crtc(struct ma35_drm *priv)
{
	uint32_t possible_crtcs = drm_crtc_mask(&priv->crtc->drm_crtc);

	priv->interface->drm_encoder.possible_crtcs = possible_crtcs;
}

static int ma35_connector_create_properties(struct ma35_drm *priv)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct ma35_interface *interface = priv->interface;
	struct drm_connector *drm_connector = &interface->drm_connector;

	// dpi format
	interface->dpi_prop = drm_property_create_enum(drm_dev, 0, "dpi-format",
					ma35_dpi_modes, ARRAY_SIZE(ma35_dpi_modes));
	if (!interface->dpi_prop) {
		drm_err(drm_dev, "Failed to create dpi format property\n");
		return -ENOMEM;
	}

	drm_object_attach_property(&drm_connector->base, interface->dpi_prop, MA35_DPI_D24);
	interface->dpi_mode = MA35_DPI_D24;

	return 0;
}

static int ma35_bridge_try_attach(struct ma35_drm *priv, struct ma35_interface *interface)
{
	struct drm_device *drm_dev = &priv->drm_dev;
	struct device *dev = drm_dev->dev;
	struct device_node *of_node = dev->of_node;
	struct drm_bridge *drm_bridge;
	int ret;

	// discovering a bridge from DT: for example, DPI to LVDS, or simple pane
	drm_bridge = devm_drm_of_get_bridge(dev, of_node, 0, 0); // port, endpoint

	if (IS_ERR(drm_bridge)) {
		interface->drm_bridge_panel = NULL;
		drm_info(drm_dev, "Failde to parse remote endpoint\n");
	} else {
		interface->drm_bridge_panel = drm_bridge;
		ret = drm_bridge_attach(&interface->drm_encoder, drm_bridge,
					interface->drm_bridge, 0);
		if (ret) {
			drm_err(drm_dev, "Failed to attach bridge to encoder\n");
			return ret;
		}
	}

	return 0;
}

/*
 * encoder -> connector -> [simple panel or bridge]
 */
int ma35_interface_init(struct ma35_drm *priv)
{
	struct ma35_interface *interface;
	struct drm_device *drm_dev = &priv->drm_dev;
	struct drm_encoder *drm_encoder;
	int ret;

	// encoder, connector
	interface = drmm_simple_encoder_alloc(drm_dev,
					struct ma35_interface, drm_encoder, DRM_MODE_ENCODER_DPI);
	if (!interface) {
		drm_err(drm_dev, "Failed to initialize encoder\n");
		goto error_early;
	}
	priv->interface = interface;
	drm_encoder = &interface->drm_encoder;
	drm_encoder_helper_add(drm_encoder,
			       &ma35_encoder_helper_funcs);

	// attach encoder to crtc
	ma35_encoder_attach_crtc(priv);

	ret = drm_connector_init(drm_dev, &interface->drm_connector,
					 &ma35_connector_funcs,
					 DRM_MODE_CONNECTOR_DPI);
	if (ret) {
		drm_err(drm_dev, "Failed to initialize connector\n");
		goto error_encoder;
	}
	drm_connector_helper_add(&interface->drm_connector,
						&ma35_connector_helper_funcs);
	ret = drm_connector_attach_encoder(&interface->drm_connector,
						drm_encoder);
	if (ret) {
		drm_err(drm_dev,
			"Failed to attach connector to encoder\n");
		goto error_encoder;
	}

	ret = ma35_connector_create_properties(priv);
	if (ret)
		goto error_encoder;

	// attach bridge to encoder if found one in device tree
	ret = ma35_bridge_try_attach(priv, interface);

	return ret;

error_encoder:
	drm_encoder_cleanup(drm_encoder);

error_early:
	return ret;
}
