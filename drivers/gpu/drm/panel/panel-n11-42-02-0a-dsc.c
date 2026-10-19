// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct n11_42_02_0a_dsc {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator_bulk_data supplies[4];
	bool supplies_ok;
	bool prepared;
	unsigned int refresh_rate;
};

static inline
struct n11_42_02_0a_dsc *to_n11_42_02_0a_dsc(struct drm_panel *panel)
{
	return container_of_const(panel, struct n11_42_02_0a_dsc, panel);
}

static void n11_42_02_0a_dsc_reset(struct n11_42_02_0a_dsc *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(11000, 11100);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(1000, 1100);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(11000, 11100);
}

static int n11_42_02_0a_dsc_timing_switch(struct n11_42_02_0a_dsc *ctx,
					   unsigned int rate);

static int n11_42_02_0a_dsc_on(struct n11_42_02_0a_dsc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2,
				     0x00, 0x00, 0x00, 0x00, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x01, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2,
				     0x00, 0x00, 0x10, 0x13, 0x32);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4,
				     0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4,
				     0x80, 0x03, 0x00, 0x10, 0x00, 0x53);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4, 0x1f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf5, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc4, 0xe3);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf8, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x2a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc5, 0x0b, 0x0b, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc6,
				     0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55,
				     0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x80);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf4, 0x55);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x19);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf2, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfb, 0x04);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xff, 0xaa, 0x55, 0xa5, 0x81);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xfc, 0x08);
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0x0000, 0x059f);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0x0000, 0x0c7f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x90, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x91,
				     0xab, 0xa8, 0x00, 0x20, 0xd2, 0x00, 0x02,
				     0x86, 0x03, 0x5e, 0x00, 0x0a, 0x03, 0x5b,
				     0x02, 0x63, 0x10, 0xf0);
	mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5a, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x3b, 0x00, 0x18, 0x00, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_CONTROL_DISPLAY,
				     0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x51,
				     0x00, 0x00, 0x00, 0x00, 0x0f, 0xff);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x9c, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_WRITE_MEMORY_START,
				     0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb2,
				     0x0f, 0x2f, 0x4f, 0x0b, 0x03, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x2f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x5f, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_GAMMA_CURVE, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x8b, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x31);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xdf, 0x21);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x01);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x90, 0x04, 0x20, 0x13, 0xff, 0x00, 0x04,
				     0x20, 0x0e, 0xff, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0b);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x06, 0x38, 0x1d, 0x06, 0x58, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x11);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x06, 0x45, 0x1d, 0x06, 0x41, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x06, 0x38, 0x1d, 0x06, 0x58, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1d);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x06, 0x38, 0x1d, 0x06, 0x41, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc3,
				     0x06, 0x38, 0x1d, 0x06, 0x58, 0x17);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x07);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0xb1);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x45);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xbe, 0x88);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x1a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x16);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x23);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb5, 0x15);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2,
				     0x00, 0x00, 0x00, 0x01, 0x20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x05);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2, 0x01, 0x01, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0e);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd2,
				     0x03, 0x00, 0x10, 0x03, 0xe4);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4,
				     0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x33);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4,
				     0x80, 0x03, 0x00, 0x10, 0x00, 0x53);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x09);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x0f);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xd8, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc7, 0x37);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0,
				     0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x18);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4,
				     0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00,
				     0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
				     0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00,
				     0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x46);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xb4,
				     0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00,
				     0x10, 0x00, 0x10, 0x00, 0x10, 0x00, 0x10,
				     0x00, 0x10, 0x00, 0x10, 0x00, 0x10, 0x00,
				     0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xe4, 0x00, 0x00);
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);

	/*
	 * The vendor switches the panel at runtime, after the panel is already
	 * displaying; the sequence inside the init table sits before sleep-out
	 * and looks like it is ignored.  Repeat the requested timing here so a
	 * mode set to 120 Hz actually leaves the panel at 120 Hz.
	 */
	if (ctx->refresh_rate != 60) {
		mipi_dsi_msleep(&dsi_ctx, 50);
		n11_42_02_0a_dsc_timing_switch(ctx, ctx->refresh_rate);
		mipi_dsi_msleep(&dsi_ctx, 50);
	}

	return dsi_ctx.accum_err;
}

static int n11_42_02_0a_dsc_off(struct n11_42_02_0a_dsc *ctx)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x28, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x10, 0x00);
	mipi_dsi_msleep(&dsi_ctx, 100);

	return dsi_ctx.accum_err;
}

/*
 * The panel runs its own frame rate: the host only pushes frames and waits
 * for TE, so switching the panel's internal timing is enough to move the
 * whole pipeline to 120 Hz.  The vendor's two timings share every porch and
 * the same DSI clock rate, and their switch commands differ in three bytes
 * (0x2f, 0x26 and 0xdf), which is what this writes.
 */
static int n11_42_02_0a_dsc_timing_switch(struct n11_42_02_0a_dsc *ctx,
					   unsigned int rate)
{
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	unsigned int saved_flags = ctx->dsi->mode_flags;
	u8 buf[2];

	/*
	 * The vendor sends this cmd set in hs mode; keep the change local so the
	 * on/off sequences stay in lp mode.
	 */
	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	/*
	 * The vendor ships three timings on this panel.  Their switch tables
	 * differ in three bytes (and the 90 Hz one has no 0x26 at all):
	 *
	 *   60 Hz: 2f 02, 26 02, 8b 00, df 21
	 *   90 Hz: 2f 01,          8b 10, df 20
	 *  120 Hz: 2f 00, 26 00, 8b 00, df 20
	 */
	buf[0] = 0x2f;
	buf[1] = rate == 60 ? 0x02 : (rate == 90 ? 0x01 : 0x00);
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, buf, ARRAY_SIZE(buf));

	if (rate != 90) {
		buf[0] = MIPI_DCS_SET_GAMMA_CURVE;
		buf[1] = rate == 60 ? 0x02 : 0x00;
		mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, buf, ARRAY_SIZE(buf));
	}

	buf[0] = 0x8b;
	buf[1] = rate == 90 ? 0x10 : 0x00;
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, buf, ARRAY_SIZE(buf));

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x00);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x03);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xc0, 0x10);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0x6f, 0x31);

	buf[0] = 0xdf;
	buf[1] = rate == 60 ? 0x21 : 0x20;
	mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, buf, ARRAY_SIZE(buf));

	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x30);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xce, 0x01);

	/* CE 01 carries a 20 ms wait; the first switch then waits one frame and
	 * closes with the auto-update-gamma command (CC back to 0).
	 */
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_msleep(&dsi_ctx, 1000000 / rate);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xf0, 0x55, 0xaa, 0x52, 0x08, 0x02);
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, 0xcc, 0x00);

	ctx->dsi->mode_flags = saved_flags;

	return dsi_ctx.accum_err;
}

static ssize_t refresh_rate_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct n11_42_02_0a_dsc *ctx = mipi_dsi_get_drvdata(to_mipi_dsi_device(dev));

	return sysfs_emit(buf, "%u\n", ctx->refresh_rate);
}

static ssize_t refresh_rate_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct n11_42_02_0a_dsc *ctx = mipi_dsi_get_drvdata(to_mipi_dsi_device(dev));
	unsigned int rate;
	int ret;

	if (kstrtouint(buf, 0, &rate))
		return -EINVAL;
	if (rate != 60 && rate != 90 && rate != 120)
		return -EINVAL;

	/*
	 * The switch is a DCS sequence; sending it while the panel is powered
	 * down would be pointless and might confuse the next prepare().
	 */
	if (!ctx->prepared)
		return -EBUSY;
	if (rate == ctx->refresh_rate)
		return count;

	ret = n11_42_02_0a_dsc_timing_switch(ctx, rate);
	if (ret < 0)
		return ret;

	ctx->refresh_rate = rate;
	dev_info(dev, "panel timing switched to %u Hz\n", rate);

	return count;
}
static DEVICE_ATTR(refresh_rate, 0600, refresh_rate_show, refresh_rate_store);

static int n11_42_02_0a_dsc_prepare(struct drm_panel *panel)
{
	struct n11_42_02_0a_dsc *ctx = to_n11_42_02_0a_dsc(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	/*
	 * The panel must be powered before it is reset, and powered off only
	 * after the display has been turned off.
	 */
	if (ctx->supplies_ok) {
		ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies),
					    ctx->supplies);
		if (ret < 0) {
			dev_err(dev, "Failed to enable panel supplies: %d\n", ret);
			return ret;
		}
	}

	n11_42_02_0a_dsc_reset(ctx);

	ret = n11_42_02_0a_dsc_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		if (ctx->supplies_ok)
			regulator_bulk_disable(ARRAY_SIZE(ctx->supplies),
					       ctx->supplies);
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

	ctx->prepared = true;

	/* apply whatever brightness the core already holds */
	if (ctx->panel.backlight)
		backlight_update_status(ctx->panel.backlight);

	return 0;
}

static int n11_42_02_0a_dsc_unprepare(struct drm_panel *panel)
{
	struct n11_42_02_0a_dsc *ctx = to_n11_42_02_0a_dsc(panel);

	ctx->prepared = false;
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = n11_42_02_0a_dsc_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	/* railed off only after the panel has been told to power down */
	if (ctx->supplies_ok)
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode n11_42_02_0a_dsc_mode = {
	.clock = (1440 + 8 + 12 + 8) * (3200 + 16 + 14 + 6) * 60 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 8,
	.hsync_end = 1440 + 8 + 12,
	.htotal = 1440 + 8 + 12 + 8,
	.vdisplay = 3200,
	.vsync_start = 3200 + 16,
	.vsync_end = 3200 + 16 + 14,
	.vtotal = 3200 + 16 + 14 + 6,
	.width_mm = 70,
	.height_mm = 155,
	.type = DRM_MODE_TYPE_DRIVER,
};

/*
 * The second timing the vendor ships.  Every porch and the dsi clock rate are
 * identical to the 60 Hz one; only the panel's internal timing switch differs,
 * so the compositor is the only thing that has to be told about 120 Hz.
 */
static const struct drm_display_mode n11_42_02_0a_dsc_mode_90 = {
	.clock = (1440 + 8 + 12 + 8) * (3200 + 16 + 14 + 6) * 90 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 8,
	.hsync_end = 1440 + 8 + 12,
	.htotal = 1440 + 8 + 12 + 8,
	.vdisplay = 3200,
	.vsync_start = 3200 + 16,
	.vsync_end = 3200 + 16 + 14,
	.vtotal = 3200 + 16 + 14 + 6,
	.width_mm = 70,
	.height_mm = 155,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static const struct drm_display_mode n11_42_02_0a_dsc_mode_120 = {
	.clock = (1440 + 8 + 12 + 8) * (3200 + 16 + 14 + 6) * 120 / 1000,
	.hdisplay = 1440,
	.hsync_start = 1440 + 8,
	.hsync_end = 1440 + 8 + 12,
	.htotal = 1440 + 8 + 12 + 8,
	.vdisplay = 3200,
	.vsync_start = 3200 + 16,
	.vsync_end = 3200 + 16 + 14,
	.vtotal = 3200 + 16 + 14 + 6,
	.width_mm = 70,
	.height_mm = 155,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int n11_42_02_0a_dsc_get_modes(struct drm_panel *panel,
				      struct drm_connector *connector)
{
	struct n11_42_02_0a_dsc *ctx = to_n11_42_02_0a_dsc(panel);

	/*
	 * Only expose the timing the panel is actually switched to.  The panel
	 * cannot follow a runtime mode change yet (drm_panel's enable callback
	 * does not see the mode), and letting userspace pick 60 Hz while the
	 * panel stays at 90 makes phoc turn the output off.
	 */
	if (ctx->refresh_rate == 60)
		return drm_connector_helper_get_modes_fixed(connector,
							&n11_42_02_0a_dsc_mode);
	if (ctx->refresh_rate == 120)
		return drm_connector_helper_get_modes_fixed(connector,
							&n11_42_02_0a_dsc_mode_120);

	return drm_connector_helper_get_modes_fixed(connector,
						    &n11_42_02_0a_dsc_mode_90);
}

static const struct drm_panel_funcs n11_42_02_0a_dsc_panel_funcs = {
	.prepare = n11_42_02_0a_dsc_prepare,
	.unprepare = n11_42_02_0a_dsc_unprepare,
	.get_modes = n11_42_02_0a_dsc_get_modes,
};

static int n11_42_02_0a_dsc_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	struct n11_42_02_0a_dsc *ctx = mipi_dsi_get_drvdata(dsi);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	/*
	 * systemd-backlight restores the saved value during early boot, long
	 * before the panel is powered.  Talking to the panel then fails and
	 * makes the unit report a failure every boot; keep the value and let
	 * prepare() apply it once the DSI link is up.
	 */
	if (!ctx->prepared)
		return 0;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

// TODO: Check if /sys/class/backlight/.../actual_brightness actually returns
// correct values. If not, remove this function.
static int n11_42_02_0a_dsc_bl_get_brightness(struct backlight_device *bl)
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

static const struct backlight_ops n11_42_02_0a_dsc_bl_ops = {
	.update_status = n11_42_02_0a_dsc_bl_update_status,
	.get_brightness = n11_42_02_0a_dsc_bl_get_brightness,
};

static struct backlight_device *
n11_42_02_0a_dsc_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = 4095,
		.max_brightness = 4095,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &n11_42_02_0a_dsc_bl_ops, &props);
}

static int n11_42_02_0a_dsc_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct n11_42_02_0a_dsc *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct n11_42_02_0a_dsc, panel,
				   &n11_42_02_0a_dsc_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	/*
	 * Power-on order per the vendor: vddio -> vddd-gpio -> vddd-l11b -> vci.
	 * All four rails are described in the board DT.  vddd-gpio is a
	 * GPIO42-gated 1.07V fixed regulator (display_panel_vddd in the factory
	 * DTBO, fragment@45) that the vendor marks regulator-boot-on.
	 * regulator_bulk_enable() enables
	 * them in array order; regulator_bulk_disable() tears them down in
	 * reverse order (vci first), which is what the vendor asks for.
	 *
	 * Before this was added the driver never touched the supplies at all.
	 * With the supplies absent from the driver:
	 *
	 *     vreg_l12b_1p8 (vddio)  state=enabled  use_count=1
	 *     vreg_l13b_3p0 (vci)    state=unknown  use_count=0
	 *     vreg_l11b_1p2 (vdd)    state=unknown  use_count=0
	 *
	 * i.e. the panel was running on whatever the bootloader happened to
	 * leave powered.  That is the leading explanation for the intermittent
	 * DPU "frame done timeout" / corrupted-panel boots: when the DDIC comes
	 * up on a marginal rail it stops acknowledging frames.
	 */
	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vddd-gpio";
	ctx->supplies[2].supply = "vddd-l11b";
	ctx->supplies[3].supply = "vci";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0) {
		/*
		 * Deliberately non-fatal: before this change the driver never
		 * touched the supplies at all, so failing the probe here would
		 * be a regression (no panel at all) if a supply name ever
		 * changes.  Log loudly and carry on with the old behaviour.
		 */
		dev_warn(dev, "Failed to get panel supplies: %d -- running unpowered as before\n",
			 ret);
	} else {
		ctx->supplies_ok = true;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO_BURST |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS | MIPI_DSI_MODE_LPM |
			  MIPI_DSI_MODE_DSC_ALL_SLICES_IN_PKT;
	ctx->panel.prepare_prev_first = true;
	/* The vendor panel default is 60 Hz. */
	ctx->refresh_rate = 60;

	ctx->panel.backlight = n11_42_02_0a_dsc_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "Failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only supports DSC; unconditionally enable it */
	dsi->dsc = &ctx->dsc;

	ctx->dsc.dsc_version_major = 1;
	ctx->dsc.dsc_version_minor = 2;

	/* TODO: Pass slice_per_pkt = 2 */
	ctx->dsc.slice_height = 32;
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

	ret = device_create_file(&dsi->dev, &dev_attr_refresh_rate);
	if (ret)
		dev_warn(dev, "Failed to create refresh_rate attribute: %d\n", ret);

	return 0;
}

static void n11_42_02_0a_dsc_remove(struct mipi_dsi_device *dsi)
{
	struct n11_42_02_0a_dsc *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	device_remove_file(&dsi->dev, &dev_attr_refresh_rate);

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id n11_42_02_0a_dsc_of_match[] = {
	{ .compatible = "mdss,n11-42-02-0a-dsc" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, n11_42_02_0a_dsc_of_match);

static struct mipi_dsi_driver n11_42_02_0a_dsc_driver = {
	.probe = n11_42_02_0a_dsc_probe,
	.remove = n11_42_02_0a_dsc_remove,
	.driver = {
		.name = "panel-n11-42-02-0a-dsc",
		.of_match_table = n11_42_02_0a_dsc_of_match,
	},
};
module_mipi_dsi_driver(n11_42_02_0a_dsc_driver);

MODULE_DESCRIPTION("DRM driver for xiaomi n11 42 02 0a cmd mode dsc dsi panel");
MODULE_LICENSE("GPL");
