// SPDX-License-Identifier: GPL-2.0-only

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct oneplus_aa601 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct gpio_descs *enable_gpios;
	struct regulator_bulk_data *supplies;
};

static const struct regulator_bulk_data oneplus_aa601_supplies[] = {
	{ .supply = "vddio" },
	{ .supply = "vdd" },
	{ .supply = "vci" },
};

static inline struct oneplus_aa601 *to_oneplus_aa601(struct drm_panel *panel)
{
	return container_of(panel, struct oneplus_aa601, panel);
}

static void oneplus_aa601_reset(struct oneplus_aa601 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(25);
}

static int oneplus_aa601_power_on(struct oneplus_aa601 *ctx)
{
	int ret;

	ret = regulator_enable(ctx->supplies[0].consumer);
	if (ret)
		return ret;
	usleep_range(3000, 4000);

	ret = regulator_enable(ctx->supplies[1].consumer);
	if (ret)
		goto err_dis_vddio;
	usleep_range(3000, 4000);

	gpiod_set_value_cansleep(ctx->enable_gpios->desc[0], 1);
	usleep_range(3000, 4000);

	gpiod_set_value_cansleep(ctx->enable_gpios->desc[1], 1);
	usleep_range(3000, 4000);

	ret = regulator_enable(ctx->supplies[2].consumer);
	if (ret)
		goto err_dis_gpios;
	usleep_range(10000, 11000);

	return 0;

err_dis_gpios:
	gpiod_set_value_cansleep(ctx->enable_gpios->desc[1], 0);
	gpiod_set_value_cansleep(ctx->enable_gpios->desc[0], 0);
	regulator_disable(ctx->supplies[1].consumer);
err_dis_vddio:
	regulator_disable(ctx->supplies[0].consumer);

	return ret;
}

static void oneplus_aa601_power_off(struct oneplus_aa601 *ctx)
{
	regulator_disable(ctx->supplies[2].consumer);
	usleep_range(3000, 4000);

	regulator_disable(ctx->supplies[1].consumer);
	usleep_range(3000, 4000);

	regulator_disable(ctx->supplies[0].consumer);

	gpiod_set_value_cansleep(ctx->enable_gpios->desc[1], 0);
	usleep_range(3000, 4000);

	gpiod_set_value_cansleep(ctx->enable_gpios->desc[0], 0);
	usleep_range(1000, 2000);
}

static int oneplus_aa601_on(struct oneplus_aa601 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0xab, 0x30, 0x80, 0x0a, 0xd4, 0x04, 0xf8, 0x00, 0x16, 0x02, 0x7c, 0x02, 0x7c, 0x02, 0x00, 0x02, 0x59, 0x00, 0x20, 0x02, 0x2e, 0x00, 0x08, 0x00, 0x0d, 0x04, 0xf4, 0x03, 0xed, 0x18, 0x00, 0x10, 0xf0, 0x07, 0x10, 0x20, 0x00, 0x06, 0x0f, 0x0f, 0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79, 0x7b, 0x7d, 0x7e, 0x02, 0x02, 0x22, 0x00, 0x2a, 0x40, 0x2a, 0xbe, 0x3a, 0xfc, 0x3a, 0xfa, 0x3a, 0xf8, 0x3b, 0x38, 0x3b, 0x78, 0x3b, 0xb6, 0x4b, 0xb6, 0x4b, 0xf4, 0x4b, 0xf4, 0x6c, 0x34, 0x84, 0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x4f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x62);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa4, 0x38);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa0, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd3, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x45, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xee, 0xff, 0xfd, 0xff, 0xfd, 0xf9, 0xfa, 0xbe, 0x76);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x22);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x01, 0x00, 0x11, 0x10, 0x3c, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd9, 0x00, 0xfc, 0x11, 0x10, 0x30, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0x84, 0x94, 0xbb, 0x00, 0x20, 0xc4, 0xa1, 0xcc, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdd, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0xf1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x02, 0x00, 0x5f, 0x21, 0x00, 0x28);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5e, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x61, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6d, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x35);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x53, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xed, 0xf5, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x83, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x00, 0xd0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x34);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x89, 0x41, 0x00, 0xc4, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x8c, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8c, 0x80, 0x07, 0x00, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0x15, 0x35, 0x55, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x00, 0x0b, 0x0a, 0x0c, 0x02, 0x0f, 0xff, 0xff, 0x0c, 0x02, 0xff, 0xff, 0x00, 0xff, 0x00, 0x0b, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8f, 0x03, 0x30, 0x8c, 0x19, 0x0f, 0x18, 0x00, 0x00, 0x19, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x02, 0x30, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x00, 0x0b, 0x0a, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x03, 0x30, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x29);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x74, 0x2c, 0x2a, 0x2a, 0x28, 0x28, 0x26, 0x26);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x24, 0x24, 0x22, 0x22, 0x20, 0x20, 0x1e, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x1c, 0x1c, 0x1a, 0x1a, 0x18, 0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x60, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x7d, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x89, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x49);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x00, 0x08, 0x0c, 0x1c, 0x3c, 0x7c, 0x8c, 0xcc, 0x0c, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x00, 0x00, 0x9e, 0x00, 0xe5, 0x00, 0x6c, 0x01, 0x41, 0x02, 0xa6, 0x03, 0x8b, 0x05, 0xdd, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x00, 0x00, 0x9e, 0x00, 0xe5, 0x00, 0x6c, 0x01, 0x41, 0x02, 0xa6, 0x03, 0x8b, 0x05, 0xdd, 0x06, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x80, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0x80, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa0, 0x40, 0x40, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa1, 0x40, 0x40, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa2, 0x40, 0x40, 0x40);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa6, 0x3f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa7, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xad, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xae, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaf, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd9, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xda, 0xf7);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdb, 0x9c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x5a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x10, 0x10, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0x48, 0xf6, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x08, 0x02, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x08, 0x02, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x89, 0x08, 0x02, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x30, 0x00, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0x10, 0x10, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8c, 0x1a, 0x1e, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8d, 0x10, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0x0b, 0x06, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8f, 0x28, 0xf9, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x12, 0x10, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x1d, 0x12, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x10, 0x05, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x08, 0xff, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x02, 0xff, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0x20, 0x0d, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0x0a, 0x0d, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x97, 0x0a, 0x0d, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x98, 0x12, 0x14, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0x14, 0x0e, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0x0a, 0x06, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9c, 0x15, 0x10, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x12, 0x14, 0x2e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e, 0x10, 0x26, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x1d, 0x14, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa0, 0x14, 0x0a, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa1, 0x14, 0x0a, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa2, 0x12, 0x08, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa3, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa4, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa5, 0x06, 0xf6, 0xf5);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa6, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa7, 0x02, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa8, 0x00, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa9, 0x00, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaa, 0x08, 0xf5, 0xf0);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xab, 0x06, 0x06, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xac, 0x08, 0x07, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xad, 0x02, 0x02, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xae, 0x12, 0x14, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaf, 0x18, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x16, 0x16, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x04, 0x02, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x20, 0x1a, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x1c, 0x16, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x12, 0x10, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x06, 0x02, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x06, 0x02, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x06, 0x0d, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x12, 0x10, 0x13);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x12, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbc, 0x12, 0x10, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x12, 0x0e, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x06, 0x06, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbf, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x12, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x12, 0x16, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x12, 0x16, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x12, 0x10, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4, 0x18, 0x18, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x12, 0x0e, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x16, 0x1a, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc9, 0x10, 0xfc, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcb, 0x10, 0xf6, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x0a, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x08, 0x06, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x0d, 0x00, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcf, 0x04, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd0, 0x04, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x02, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x16, 0x16, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd3, 0x16, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd4, 0x14, 0x14, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd5, 0x10, 0x10, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd7, 0x16, 0x18, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x16, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd9, 0x12, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xda, 0x0d, 0x0a, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdb, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdd, 0x0d, 0x12, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0x0d, 0x10, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x0a, 0x10, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x12, 0x20, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x0d, 0x14, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x0d, 0x16, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe5, 0x0d, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x0d, 0x10, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe7, 0x0d, 0x10, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe8, 0x0d, 0x12, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe9, 0x0a, 0x10, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea, 0x06, 0x10, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xeb, 0x02, 0x04, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x5b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x0d, 0xfc, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x04, 0x02, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x0d, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x04, 0x04, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x04, 0x04, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0x03, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x06, 0x05, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x05, 0x03, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x89, 0x02, 0x03, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x12, 0x1a, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0x14, 0x18, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8c, 0x14, 0x18, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8d, 0x10, 0x12, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0x08, 0x0a, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8f, 0x1e, 0x34, 0x2c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x1a, 0x1e, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x18, 0x1e, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x14, 0x18, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x0c, 0x0e, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0x06, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0x10, 0x18, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x97, 0x12, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x98, 0x18, 0x1e, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0x0d, 0x14, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0x06, 0x0c, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9c, 0x14, 0x18, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x16, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e, 0x0d, 0x16, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x0d, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa0, 0x0d, 0x16, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa1, 0x18, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa2, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa3, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa4, 0x05, 0x00, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa5, 0x08, 0x03, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa6, 0x02, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa7, 0x06, 0x03, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa8, 0x02, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa9, 0x02, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaa, 0x06, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xab, 0x06, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xac, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xad, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xae, 0x12, 0x12, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaf, 0x14, 0x16, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x12, 0x14, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x04, 0x02, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x04, 0x02, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x30, 0x32, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x18, 0x1c, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x12, 0x14, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x02, 0x02, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x02, 0x02, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x0d, 0x14, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x0f, 0x12, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x14, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbc, 0x04, 0x08, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x00, 0x02, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x01, 0x03, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbf, 0x01, 0x03, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x10, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x14, 0x16, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x16, 0x14, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x10, 0x12, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4, 0x02, 0x0a, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x00, 0x02, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x00, 0x0a, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x06, 0x00, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc9, 0x05, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcb, 0x04, 0x04, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x0c, 0x0c, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcf, 0x06, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd0, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd1, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x12, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd3, 0x12, 0x12, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd4, 0x14, 0x16, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd5, 0x06, 0x06, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd6, 0x02, 0x02, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd7, 0x20, 0x24, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x1a, 0x1e, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd9, 0x18, 0x1a, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xda, 0x08, 0x04, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdb, 0x06, 0x02, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdc, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdd, 0x12, 0x18, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xde, 0x14, 0x1b, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x10, 0x1a, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe0, 0x10, 0x1a, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe1, 0x0a, 0x0a, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe2, 0x02, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe3, 0x00, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x14, 0x18, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe5, 0x10, 0x1a, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe6, 0x1a, 0x14, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe7, 0x18, 0x16, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe8, 0x14, 0x10, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe9, 0x0c, 0x0e, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xea, 0x14, 0x12, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xeb, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x5c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x80, 0x03, 0x01, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x81, 0x04, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x82, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x83, 0x01, 0x03, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x84, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x85, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x86, 0x0e, 0x10, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x87, 0x06, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x88, 0x06, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x89, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8a, 0x16, 0x16, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0x16, 0x18, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8c, 0x14, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8d, 0x0e, 0x0e, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8e, 0x06, 0x08, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8f, 0x10, 0x14, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x1a, 0x1e, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91, 0x18, 0x1a, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x92, 0x10, 0x12, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x93, 0x0a, 0x0a, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x94, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x95, 0x14, 0x24, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x96, 0x18, 0x18, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x97, 0x18, 0x24, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x98, 0x16, 0x1d, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x99, 0x14, 0x1d, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9a, 0x08, 0x10, 0x0d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9b, 0x06, 0x0a, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9c, 0x12, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9d, 0x18, 0x1e, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9e, 0x14, 0x1c, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9f, 0x10, 0x12, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa0, 0x12, 0x12, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa1, 0x12, 0x12, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa2, 0x0e, 0x10, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa3, 0x06, 0x08, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa4, 0x02, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa5, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa6, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa7, 0x01, 0x01, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa8, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xa9, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaa, 0x04, 0x06, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xab, 0x08, 0x08, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xac, 0x06, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xad, 0x04, 0x06, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xae, 0x14, 0x16, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xaf, 0x12, 0x1a, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb0, 0x14, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb1, 0x04, 0x04, 0x0c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2, 0x06, 0x02, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb3, 0x18, 0x1a, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x1a, 0x1e, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x18, 0x1a, 0x14);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb6, 0x06, 0x04, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb7, 0x06, 0x02, 0x08);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb8, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb9, 0x0d, 0x1d, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xba, 0x12, 0x1d, 0x1e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbb, 0x14, 0x13, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbc, 0x04, 0x0c, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbd, 0x04, 0x0c, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x04, 0x09, 0x06);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbf, 0x00, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x12, 0x18, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc1, 0x16, 0x12, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x1c, 0x16, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3, 0x10, 0x14, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4, 0x14, 0x0a, 0x1c);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x0e, 0x08, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6, 0x0a, 0x04, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc8, 0x04, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc9, 0x0a, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xca, 0x0a, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcb, 0x0a, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcd, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x02, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcf, 0x02, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);

	return dsi_ctx.accum_err;
}

static int oneplus_aa601_off(struct oneplus_aa601 *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int oneplus_aa601_prepare(struct drm_panel *panel)
{
	struct oneplus_aa601 *ctx = to_oneplus_aa601(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	ret = oneplus_aa601_power_on(ctx);
	if (ret) {
		dev_err(dev, "Failed to power on panel: %d\n", ret);
		return ret;
	}

	oneplus_aa601_reset(ctx);

	ret = oneplus_aa601_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		goto err_power_off;
	}

	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);

	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(panel->dev, "failed to transmit PPS: %d\n", ret);
		goto err_power_off;
	}

	ret = mipi_dsi_compression_mode(ctx->dsi, true);
	if (ret < 0) {
		dev_err(dev, "failed to enable compression mode: %d\n", ret);
		goto err_power_off;
	}

	ret = mipi_dsi_dcs_set_display_on(ctx->dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		goto err_power_off;
	}
	msleep(20);

	msleep(28);

	return 0;

err_power_off:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	oneplus_aa601_power_off(ctx);
	return ret;
}

static int oneplus_aa601_unprepare(struct drm_panel *panel)
{
	struct oneplus_aa601 *ctx = to_oneplus_aa601(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = oneplus_aa601_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	oneplus_aa601_power_off(ctx);

	return 0;
}

static const struct drm_display_mode oneplus_aa601_mode = {
	.clock = (1272 + 26 + 2 + 26) * (2772 + 58 + 2 + 42) * 165 / 1000,
	.hdisplay = 1272,
	.hsync_start = 1272 + 26,
	.hsync_end = 1272 + 26 + 2,
	.htotal = 1272 + 26 + 2 + 26,
	.vdisplay = 2772,
	.vsync_start = 2772 + 58,
	.vsync_end = 2772 + 58 + 2,
	.vtotal = 2772 + 58 + 2 + 42,
	.width_mm = 71,
	.height_mm = 157,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int oneplus_aa601_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	return drm_connector_helper_get_modes_fixed(connector, &oneplus_aa601_mode);
}

static const struct drm_panel_funcs oneplus_aa601_panel_funcs = {
	.prepare = oneplus_aa601_prepare,
	.unprepare = oneplus_aa601_unprepare,
	.get_modes = oneplus_aa601_get_modes,
};

static int oneplus_aa601_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct oneplus_aa601 *ctx = mipi_dsi_get_drvdata(dsi);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	if (!ctx->panel.prepared)
		return 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static int oneplus_aa601_bl_get_brightness(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct oneplus_aa601 *ctx = mipi_dsi_get_drvdata(dsi);
	u16 brightness;
	int ret;

	if (!ctx->panel.prepared)
		return 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_get_display_brightness_large(dsi, &brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return brightness;
}

static const struct backlight_ops oneplus_aa601_bl_ops = {
	.update_status = oneplus_aa601_bl_update_status,
	.get_brightness = oneplus_aa601_bl_get_brightness,
};

static struct backlight_device *
oneplus_aa601_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 2047,
		.max_brightness = 3502, /* 15% below 4095 to limit burn-in */
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &oneplus_aa601_bl_ops, &props);
}

static int oneplus_aa601_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct oneplus_aa601 *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct oneplus_aa601, panel,
				   &oneplus_aa601_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->enable_gpios = devm_gpiod_get_array(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->enable_gpios))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpios),
				     "Failed to get enable-gpios\n");

	ret = devm_regulator_bulk_get_const(dev,
					    ARRAY_SIZE(oneplus_aa601_supplies),
					    oneplus_aa601_supplies,
					    &ctx->supplies);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get panel supplies\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB101010;
	dsi->mode_flags = MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;

	ctx->panel.backlight = oneplus_aa601_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 2; /* DSC 1.2 */

	ctx->dsc.slice_height = 22;
	ctx->dsc.slice_width = 636;
	ctx->dsi->dsc_slice_per_pkt = 2;
	ctx->dsc.slice_count = 2;
	ctx->dsc.convert_rgb = true;
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

static void oneplus_aa601_remove(struct mipi_dsi_device *dsi)
{
	struct oneplus_aa601 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id oneplus_aa601_of_match[] = {
	{ .compatible = "oneplus,aa601-p-7-a0020" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, oneplus_aa601_of_match);

static struct mipi_dsi_driver oneplus_aa601_driver = {
	.probe = oneplus_aa601_probe,
	.remove = oneplus_aa601_remove,
	.driver = {
		.name = "panel-oneplus-aa601",
		.of_match_table = oneplus_aa601_of_match,
	},
};
module_mipi_dsi_driver(oneplus_aa601_driver);

MODULE_AUTHOR("idusergod <artem.martyanov06@gmail.com>");
MODULE_DESCRIPTION("DRM driver for OnePlus AA601 P 7 A0020 DSC panel");
MODULE_LICENSE("GPL");
