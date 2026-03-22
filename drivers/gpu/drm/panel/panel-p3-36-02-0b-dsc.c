// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct p3_36_02_0b_dsc {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
};

static inline
struct p3_36_02_0b_dsc *to_p3_36_02_0b_dsc(struct drm_panel *panel)
{
	return container_of(panel, struct p3_36_02_0b_dsc, panel);
}

static void p3_36_02_0b_dsc_reset(struct p3_36_02_0b_dsc *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(1000, 2000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int p3_36_02_0b_dsc_on(struct p3_36_02_0b_dsc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x01, 0x19);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x04c3);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x0a5f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x03, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde,
				     0x30, 0x14, 0x25, 0x10, 0x34, 0x25);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0,
				     0x84, 0x44, 0x00, 0x00, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91,
				     0xab, 0xf0, 0x00, 0x10, 0xc1, 0x00, 0x01,
				     0xcb, 0x00, 0x73, 0x00, 0x33, 0x06, 0x67,
				     0x1f, 0xb0, 0x11, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x30);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x04);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x14);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_set_tear_scanline_multi(&dsi_ctx, 0x0a60);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x44,
				     0x00, 0x30, 0x00, 0x30, 0x30, 0x03, 0xa8);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8c, 0x00, 0x00, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0xfe17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xef, 0x17, 0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x04);
	mipi_dsi_dcs_set_display_brightness_multi(&dsi_ctx, 0x6002);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int p3_36_02_0b_dsc_off(struct p3_36_02_0b_dsc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba,
				     0xff, 0xff, 0xff, 0xff, 0xff, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba,
				     0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
				     0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
				     0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
				     0xa3, 0xa3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x27);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba,
				     0xff, 0xff, 0xff, 0xff, 0xff, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba,
				     0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
				     0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
				     0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3, 0xa3,
				     0xa3, 0xa3);
	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 30);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int p3_36_02_0b_dsc_prepare(struct drm_panel *panel)
{
	struct p3_36_02_0b_dsc *ctx = to_p3_36_02_0b_dsc(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	p3_36_02_0b_dsc_reset(ctx);

	ret = p3_36_02_0b_dsc_on(ctx);
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

static int p3_36_02_0b_dsc_unprepare(struct drm_panel *panel)
{
	struct p3_36_02_0b_dsc *ctx = to_p3_36_02_0b_dsc(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = p3_36_02_0b_dsc_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode p3_36_02_0b_dsc_mode = {
	.clock = (1220 + 16 + 16 + 16) * (2656 + 16 + 4 + 20) * 120 / 1000,
	.hdisplay = 1220,
	.hsync_start = 1220 + 16,
	.hsync_end = 1220 + 16 + 16,
	.htotal = 1220 + 16 + 16 + 16,
	.vdisplay = 2656,
	.vsync_start = 2656 + 16,
	.vsync_end = 2656 + 16 + 4,
	.vtotal = 2656 + 16 + 4 + 20,
	.width_mm = 673,
	.height_mm = 1466,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int p3_36_02_0b_dsc_get_modes(struct drm_panel *panel,
				     struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &p3_36_02_0b_dsc_mode);
}

static const struct drm_panel_funcs p3_36_02_0b_dsc_panel_funcs = {
	.prepare = p3_36_02_0b_dsc_prepare,
	.unprepare = p3_36_02_0b_dsc_unprepare,
	.get_modes = p3_36_02_0b_dsc_get_modes,
};

static int p3_36_02_0b_dsc_bl_update_status(struct backlight_device *bl)
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
static int p3_36_02_0b_dsc_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops p3_36_02_0b_dsc_bl_ops = {
	.update_status = p3_36_02_0b_dsc_bl_update_status,
	.get_brightness = p3_36_02_0b_dsc_bl_get_brightness,
};

static struct backlight_device *
p3_36_02_0b_dsc_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 16383,
		.max_brightness = 16383,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &p3_36_02_0b_dsc_bl_ops, &props);
}

static int p3_36_02_0b_dsc_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct p3_36_02_0b_dsc *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct p3_36_02_0b_dsc, panel,
				   &p3_36_02_0b_dsc_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = p3_36_02_0b_dsc_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 2;

	/* TODO: Pass slice_per_pkt = 2 */
	ctx->dsc.slice_height = 16;
	ctx->dsc.slice_width = 610;
	/*
	 * TODO: hdisplay should be read from the selected mode once
	 * it is passed back to drm_panel (in prepare?)
	 */
	WARN_ON(1220 % ctx->dsc.slice_width);
	ctx->dsc.slice_count = 1220 / ctx->dsc.slice_width;
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

static void p3_36_02_0b_dsc_remove(struct mipi_dsi_device *dsi)
{
	struct p3_36_02_0b_dsc *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id p3_36_02_0b_dsc_of_match[] = {
	{ .compatible = "mdss,p3-36-02-0b-dsc" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, p3_36_02_0b_dsc_of_match);

static struct mipi_dsi_driver p3_36_02_0b_dsc_driver = {
	.probe = p3_36_02_0b_dsc_probe,
	.remove = p3_36_02_0b_dsc_remove,
	.driver = {
		.name = "panel-p3-36-02-0b-dsc",
		.of_match_table = p3_36_02_0b_dsc_of_match,
	},
};
module_mipi_dsi_driver(p3_36_02_0b_dsc_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for xiaomi p3 36 02 0b cmd mode dsc dsi panel");
MODULE_LICENSE("GPL");
