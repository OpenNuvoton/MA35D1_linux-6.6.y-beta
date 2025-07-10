// SPDX-License-Identifier: GPL-2.0+
/*
 * Nuvoton DRM driver
 *
 * Copyright (C) 2025 Nuvoton Technology Corp.
 *
 * Author: Joey Lu <yclu4@nuvoton.com>
 */

#ifndef _MA35_INTERFACE_H_
#define _MA35_INTERFACE_H_

#include <drm/drm_bridge.h>
#include <drm/drm_connector.h>
#include <drm/drm_encoder.h>

enum ma35_dpi_format_enum {
	MA35_DPI_D16CFG1,
	MA35_DPI_D16CFG2,
	MA35_DPI_D16CFG3,
	MA35_DPI_D18CFG1,
	MA35_DPI_D18CFG2,
	MA35_DPI_D24,
};

#define MA35_DPI_FORMAT_MASK GENMASK(2, 0)

struct ma35_drm;

struct ma35_interface {
	struct drm_encoder drm_encoder;
	struct drm_connector drm_connector;
	struct drm_property *dpi_prop;
	u32 dpi_mode;

	struct drm_bridge *drm_bridge;
	struct drm_bridge *drm_bridge_panel;
};

void ma35_interface_attach_crtc(struct ma35_drm *priv);
int ma35_interface_init(struct ma35_drm *priv);

#endif
