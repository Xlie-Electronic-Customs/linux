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

struct panel_samsung_amb670yf07_1440_3216_dsc {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
};

static inline
struct panel_samsung_amb670yf07_1440_3216_dsc *to_panel_samsung_amb670yf07_1440_3216_dsc(struct drm_panel *panel)
{
	return container_of(panel, struct panel_samsung_amb670yf07_1440_3216_dsc, panel);
}

static void panel_samsung_amb670yf07_1440_3216_dsc_reset(struct panel_samsung_amb670yf07_1440_3216_dsc *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
}

static int panel_samsung_amb670yf07_1440_3216_dsc_on(struct panel_samsung_amb670yf07_1440_3216_dsc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e,
				     0x12, 0x00, 0x00, 0xab, 0x30, 0x80, 0x09,
				     0x6c, 0x04, 0x38, 0x00, 0x24, 0x02, 0x1c,
				     0x02, 0x1c, 0x02, 0x00, 0x02, 0x3b, 0x00,
				     0x20, 0x03, 0x35, 0x00, 0x07, 0x00, 0x0e,
				     0x03, 0x34, 0x02, 0xd4, 0x18, 0x00, 0x10,
				     0xf0, 0x07, 0x10, 0x20, 0x00, 0x06, 0x0f,
				     0x0f, 0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46,
				     0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b,
				     0x7d, 0x7e, 0x02, 0x02, 0x22, 0x00, 0x2a,
				     0x40, 0x2a, 0xbe, 0x3a, 0xfc, 0x3a, 0xfa,
				     0x3a, 0xf8, 0x3b, 0x38, 0x3b, 0x78, 0x3b,
				     0xb6, 0x4b, 0xb6, 0x4b, 0xf4, 0x4b, 0xf4,
				     0x6c, 0x34, 0x84, 0x74, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x01);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 6000, 7000);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x22, 0xb9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0xa1, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x3a, 0xb9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x26, 0xb9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x24, 0xb9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x38, 0xb9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x2a, 0xb9);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_msleep(&dsi_ctx, 121);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x16, 0xf2);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x1b, 0x50);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x08, 0xcb);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcb, 0x24);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x23, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x10, 0xbd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x16, 0xbd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x77);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x14, 0xbd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x0437);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x096b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x89);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x24, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0xfd, 0x00, 0xfd, 0x00, 0xfd, 0x00, 0xfd,
				     0x00, 0xfd, 0x00, 0xfd, 0x00, 0xfd, 0x00,
				     0xfd, 0x00, 0xfd);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x3a, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0xb0, 0x00, 0xb0, 0x00, 0xb0, 0x00, 0xb0,
				     0x00, 0xb0, 0x00, 0xb0, 0x00, 0xb0, 0x00,
				     0xb0, 0x00, 0xb0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x50, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x43, 0x00, 0x43, 0x00, 0x43, 0x00, 0x43,
				     0x00, 0x43, 0x00, 0x43, 0x00, 0x43, 0x00,
				     0x43, 0x00, 0x43);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x61, 0xc3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0xcc, 0xcc, 0xcc, 0xcc, 0xc0, 0xfe, 0x00,
				     0xfe, 0x00, 0xfe, 0x00, 0xfe, 0x00, 0xfe,
				     0x00, 0xfe, 0x00, 0xfe, 0x00, 0xfe, 0x00,
				     0xfe);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x2b, 0xf6);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf6, 0x60, 0x63, 0x69);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x46, 0xf4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x18, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0d, 0x63);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x0c, 0x63);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x63, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf7, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_POWER_SAVE, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x5a, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x52, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x00, 0x54, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x1f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0xa5, 0xa5);

	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	return dsi_ctx.accum_err;
}

static int panel_samsung_amb670yf07_1440_3216_dsc_off(struct panel_samsung_amb670yf07_1440_3216_dsc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 21);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 101);

	return dsi_ctx.accum_err;
}

static int panel_samsung_amb670yf07_1440_3216_dsc_prepare(struct drm_panel *panel)
{
	struct panel_samsung_amb670yf07_1440_3216_dsc *ctx = to_panel_samsung_amb670yf07_1440_3216_dsc(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	panel_samsung_amb670yf07_1440_3216_dsc_reset(ctx);

	ret = panel_samsung_amb670yf07_1440_3216_dsc_on(ctx);
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

static int panel_samsung_amb670yf07_1440_3216_dsc_unprepare(struct drm_panel *panel)
{
	struct panel_samsung_amb670yf07_1440_3216_dsc *ctx = to_panel_samsung_amb670yf07_1440_3216_dsc(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = panel_samsung_amb670yf07_1440_3216_dsc_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	return 0;
}

static const struct drm_display_mode panel_samsung_amb670yf07_1440_3216_dsc_modes[] = {
	{ /* 120Hz mode */
		.clock = (1080 + 64 + 8 + 54) * (2412 + 2 + 2 + 8) * 120 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 64,
		.hsync_end = 1080 + 64 + 8,
		.htotal = 1080 + 64 + 8 + 54,
		.vdisplay = 2412,
		.vsync_start = 2412 + 2,
		.vsync_end = 2412 + 2 + 2,
		.vtotal = 2412 + 2 + 2 + 8,
		.width_mm = 70,
		.height_mm = 156,
		.type = DRM_MODE_TYPE_DRIVER,
	},
	{ /* 90Hz mode */
		.clock = (1080 + 64 + 8 + 54) * (2412 + 2 + 2 + 8) * 90 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 64,
		.hsync_end = 1080 + 64 + 8,
		.htotal = 1080 + 64 + 8 + 54,
		.vdisplay = 2412,
		.vsync_start = 2412 + 2,
		.vsync_end = 2412 + 2 + 2,
		.vtotal = 2412 + 2 + 2 + 8,
		.width_mm = 70,
		.height_mm = 156,
		.type = DRM_MODE_TYPE_DRIVER,
	},
	{ /* 60Hz mode */
		.clock = (1080 + 64 + 8 + 54) * (2412 + 2 + 2 + 8) * 60 / 1000,
		.hdisplay = 1080,
		.hsync_start = 1080 + 64,
		.hsync_end = 1080 + 64 + 8,
		.htotal = 1080 + 64 + 8 + 54,
		.vdisplay = 2412,
		.vsync_start = 2412 + 2,
		.vsync_end = 2412 + 2 + 2,
		.vtotal = 2412 + 2 + 2 + 8,
		.width_mm = 70,
		.height_mm = 156,
		.type = DRM_MODE_TYPE_DRIVER,
	},
};

static int panel_samsung_amb670yf07_1440_3216_dsc_get_modes(struct drm_panel *panel,
							    struct drm_connector *connector)
{
	int count = 0;

	for (int i = 0; i < ARRAY_SIZE(panel_samsung_amb670yf07_1440_3216_dsc_modes); i++)
		count += drm_connector_helper_get_modes_fixed(connector,
						    &panel_samsung_amb670yf07_1440_3216_dsc_modes[i]);

	return count;
}

static const struct drm_panel_funcs panel_samsung_amb670yf07_1440_3216_dsc_panel_funcs = {
	.prepare = panel_samsung_amb670yf07_1440_3216_dsc_prepare,
	.unprepare = panel_samsung_amb670yf07_1440_3216_dsc_unprepare,
	.get_modes = panel_samsung_amb670yf07_1440_3216_dsc_get_modes,
};

static int panel_samsung_amb670yf07_1440_3216_dsc_bl_update_status(struct backlight_device *bl)
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
static int panel_samsung_amb670yf07_1440_3216_dsc_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops panel_samsung_amb670yf07_1440_3216_dsc_bl_ops = {
	.update_status = panel_samsung_amb670yf07_1440_3216_dsc_bl_update_status,
	.get_brightness = panel_samsung_amb670yf07_1440_3216_dsc_bl_get_brightness,
};

static struct backlight_device *
panel_samsung_amb670yf07_1440_3216_dsc_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 400,
		.max_brightness = 2047,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &panel_samsung_amb670yf07_1440_3216_dsc_bl_ops, &props);
}

static int panel_samsung_amb670yf07_1440_3216_dsc_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct panel_samsung_amb670yf07_1440_3216_dsc *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct panel_samsung_amb670yf07_1440_3216_dsc, panel,
				   &panel_samsung_amb670yf07_1440_3216_dsc_panel_funcs,
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
			  MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = panel_samsung_amb670yf07_1440_3216_dsc_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 2;

	/* TODO: Pass slice_per_pkt = 2 */
	ctx->dsc.slice_height = 36;
	ctx->dsc.slice_width = 540;
	/*
	 * TODO: hdisplay should be read from the selected mode once
	 * it is passed back to drm_panel (in prepare?)
	 */
	WARN_ON(1080 % ctx->dsc.slice_width);
	ctx->dsc.slice_count = 1080 / ctx->dsc.slice_width;
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

static void panel_samsung_amb670yf07_1440_3216_dsc_remove(struct mipi_dsi_device *dsi)
{
	struct panel_samsung_amb670yf07_1440_3216_dsc *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id panel_samsung_amb670yf07_1440_3216_dsc_of_match[] = {
	{ .compatible = "panel,samsung-amb670yf07-1440-3216-dsc" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, panel_samsung_amb670yf07_1440_3216_dsc_of_match);

static struct mipi_dsi_driver panel_samsung_amb670yf07_1440_3216_dsc_driver = {
	.probe = panel_samsung_amb670yf07_1440_3216_dsc_probe,
	.remove = panel_samsung_amb670yf07_1440_3216_dsc_remove,
	.driver = {
		.name = "panel-panel-samsung-amb670yf07-1440-3216-dsc",
		.of_match_table = panel_samsung_amb670yf07_1440_3216_dsc_of_match,
	},
};
module_mipi_dsi_driver(panel_samsung_amb670yf07_1440_3216_dsc_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for samsung S6E3HC3 dsc cmd mode panel");
MODULE_LICENSE("GPL");
