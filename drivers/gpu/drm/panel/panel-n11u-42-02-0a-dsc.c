// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct n11u_42_02_0a_dsc {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
};

static inline
struct n11u_42_02_0a_dsc *to_n11u_42_02_0a_dsc(struct drm_panel *panel)
{
	return container_of(panel, struct n11u_42_02_0a_dsc, panel);
}

static void n11u_42_02_0a_dsc_reset(struct n11u_42_02_0a_dsc *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(11000, 12000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(11000, 12000);
}

static int n11u_42_02_0a_dsc_on(struct n11u_42_02_0a_dsc *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	mipi_dsi_dcs_write_seq(dsi, 0x57, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xc2, 0x22, 0x80, 0x3b, 0x01, 0x81);
	mipi_dsi_dcs_write_seq(dsi, 0xc7, 0x13);
	mipi_dsi_dcs_write_seq(dsi, 0xcb,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xcc,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xce,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xd3, 0x80, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xd3, 0x10, 0x00, 0xb4, 0x00, 0x90);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xd3, 0x80, 0x40, 0x80, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x08);
	mipi_dsi_dcs_write_seq(dsi, 0xde, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq(dsi, 0xde, 0x10, 0x34, 0x25, 0x30, 0x14, 0x25);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xb1, 0x60, 0x60, 0x00, 0x60, 0x60, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xc0,
			       0x00, 0x01, 0x08, 0xaa, 0xaa, 0xaa, 0xaa, 0x55,
			       0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
			       0xaa);
	mipi_dsi_dcs_write_seq(dsi, 0xb0, 0x04, 0x44, 0x78, 0x70, 0x40);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0xc2, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0xb4, 0x1f);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xf5, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xc4, 0xe3);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x18);
	mipi_dsi_dcs_write_seq(dsi, 0xf8, 0x19);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xc5, 0x0b, 0x0b, 0x0b);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0xc6,
			       0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x1b);
	mipi_dsi_dcs_write_seq(dsi, 0xf4, 0x55);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x19);
	mipi_dsi_dcs_write_seq(dsi, 0xf2, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x18);
	mipi_dsi_dcs_write_seq(dsi, 0xfb, 0x04);
	mipi_dsi_dcs_write_seq(dsi, 0xff, 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x07);
	mipi_dsi_dcs_write_seq(dsi, 0xfc, 0x08);

	ret = mipi_dsi_dcs_set_column_address(dsi, 0x0000, 0x059f);
	if (ret < 0) {
		dev_err(dev, "Failed to set column address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_page_address(dsi, 0x0000, 0x0c7f);
	if (ret < 0) {
		dev_err(dev, "Failed to set page address: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x90, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0x91,
			       0xab, 0xf0, 0x00, 0x14, 0xd1, 0x00, 0x01, 0xde,
			       0x00, 0xa3, 0x00, 0x3c, 0x05, 0x7a, 0x15, 0x9a,
			       0x11, 0x50);

	ret = mipi_dsi_dcs_set_tear_on(dsi, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	if (ret < 0) {
		dev_err(dev, "Failed to set tear on: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x5a, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x3b, 0x00, 0x18, 0x00, 0x10);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_CONTROL_DISPLAY, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x51, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff);
	mipi_dsi_dcs_write_seq(dsi, 0x9c, 0x01);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_WRITE_MEMORY_START, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x2f, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x5f, 0x00);
	mipi_dsi_dcs_write_seq(dsi, MIPI_DCS_SET_GAMMA_CURVE, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0x8b, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xc0, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x31);
	mipi_dsi_dcs_write_seq(dsi, 0xdf, 0x21);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xcc, 0x30);
	mipi_dsi_dcs_write_seq(dsi, 0xce, 0x01);
	msleep(20);
	mipi_dsi_dcs_write_seq(dsi, 0xcc, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xc3,
			       0x90, 0x04, 0x20, 0x13, 0xff, 0x00, 0x04, 0x20,
			       0x0e, 0xff, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x0b);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x06, 0x38, 0x1d, 0x06, 0x58, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x11);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x06, 0x45, 0x1d, 0x06, 0x41, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x06, 0x38, 0x1d, 0x06, 0x58, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x1d);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x06, 0x38, 0x1d, 0x06, 0x41, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x23);
	mipi_dsi_dcs_write_seq(dsi, 0xc3, 0x06, 0x38, 0x1d, 0x06, 0x58, 0x17);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xbe, 0x45);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xbe, 0x88);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x1a);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x16);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x15);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x23);
	mipi_dsi_dcs_write_seq(dsi, 0xb5, 0x15);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0xd2, 0x00, 0x00, 0x00, 0x01, 0x20);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq(dsi, 0xd2, 0x01, 0x01, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x0e);
	mipi_dsi_dcs_write_seq(dsi, 0xd2, 0x03, 0x00, 0x10, 0x03, 0xe4);
	mipi_dsi_dcs_write_seq(dsi, 0xe4,
			       0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x33);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x0a);
	mipi_dsi_dcs_write_seq(dsi, 0xe4, 0x80, 0x03, 0x00, 0x10, 0x00, 0x53);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq(dsi, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x0f);
	mipi_dsi_dcs_write_seq(dsi, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x03);
	mipi_dsi_dcs_write_seq(dsi, 0xc7, 0x37);
	mipi_dsi_dcs_write_seq(dsi, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x18);
	mipi_dsi_dcs_write_seq(dsi, 0xb4,
			       0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
			       0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
			       0x00, 0x10, 0x00, 0x10, 0x00, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x46);
	mipi_dsi_dcs_write_seq(dsi, 0xb4,
			       0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
			       0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
			       0x00, 0x10, 0x00, 0x10, 0x00, 0x10);
	mipi_dsi_dcs_write_seq(dsi, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq(dsi, 0xe4, 0x00, 0x00);

	return 0;
}

static int n11u_42_02_0a_dsc_off(struct n11u_42_02_0a_dsc *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	mipi_dsi_dcs_write_seq(dsi, 0x28, 0x00);
	msleep(20);
	mipi_dsi_dcs_write_seq(dsi, 0x10, 0x00);
	msleep(100);

	return 0;
}

static int n11u_42_02_0a_dsc_prepare(struct drm_panel *panel)
{
	struct n11u_42_02_0a_dsc *ctx = to_n11u_42_02_0a_dsc(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	n11u_42_02_0a_dsc_reset(ctx);

	ret = n11u_42_02_0a_dsc_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		return ret;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "failed to enable compression mode: %d\n", ret);
		return ret;
	}

	msleep(28); /* TODO: Is this panel-dependent? */

	return 0;
}

static int n11u_42_02_0a_dsc_unprepare(struct drm_panel *panel)
{
	struct n11u_42_02_0a_dsc *ctx = to_n11u_42_02_0a_dsc(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = n11u_42_02_0a_dsc_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode n11u_42_02_0a_dsc_mode = {
	.clock = (1440 + 8 + 12 + 8) * (3200 + 16 + 14 + 6) * 60 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 8,
	.hsync_end = 1440 + 8 + 12,
	.htotal = 1440 + 8 + 12 + 8,
	.vdisplay = 3200,
	.vsync_start = 3200 + 16,
	.vsync_end = 3200 + 16 + 14,
	.vtotal = 3200 + 16 + 14 + 6,
	.width_mm = 695,
	.height_mm = 1545,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int n11u_42_02_0a_dsc_get_modes(struct drm_panel *panel,
				       struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &n11u_42_02_0a_dsc_mode);
}

static const struct drm_panel_funcs n11u_42_02_0a_dsc_panel_funcs = {
	.prepare = n11u_42_02_0a_dsc_prepare,
	.unprepare = n11u_42_02_0a_dsc_unprepare,
	.get_modes = n11u_42_02_0a_dsc_get_modes,
};

static int n11u_42_02_0a_dsc_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int n11u_42_02_0a_dsc_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops n11u_42_02_0a_dsc_bl_ops = {
	.update_status = n11u_42_02_0a_dsc_bl_update_status,
	.get_brightness = n11u_42_02_0a_dsc_bl_get_brightness,
};

static struct backlight_device *
n11u_42_02_0a_dsc_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &n11u_42_02_0a_dsc_bl_ops, &props);
}

static int n11u_42_02_0a_dsc_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct n11u_42_02_0a_dsc *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, dev, &n11u_42_02_0a_dsc_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = n11u_42_02_0a_dsc_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 2;

	/* TODO: Pass slice_per_pkt = 2 */
	ctx->dsc.slice_height = 20;
	ctx->dsc.slice_width = 720;
	/*
	 * TODO: hdisplay should be read from the selected mode once
	 * it is passed back to drm_panel (in prepare?)
	 */
	WARN_ON(1440 % ctx->dsc.slice_width);
	ctx->dsc.slice_count = 1440 / ctx->dsc.slice_width;
	ctx->dsc.bits_per_component = 10;
	ctx->dsc.bits_per_pixel = 8 << 4; /* 4 fractional bits */
	ctx->dsc.block_pred_enable = true;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void n11u_42_02_0a_dsc_remove(struct mipi_dsi_device *dsi)
{
	struct n11u_42_02_0a_dsc *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id n11u_42_02_0a_dsc_of_match[] = {
	{ .compatible = "mdss,n11u-42-02-0a-dsc" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n11u_42_02_0a_dsc_of_match);

static struct mipi_dsi_driver n11u_42_02_0a_dsc_driver = {
	.probe = n11u_42_02_0a_dsc_probe,
	.remove = n11u_42_02_0a_dsc_remove,
	.driver = {
		.name = "panel-n11u-42-02-0a-dsc",
		.of_match_table = n11u_42_02_0a_dsc_of_match,
	},
};
module_mipi_dsi_driver(n11u_42_02_0a_dsc_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for xiaomi n11u 42 02 0a cmd mode dsc dsi panel");
MODULE_LICENSE("GPL");
