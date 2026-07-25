// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRM panel driver for the OnePlus 15 (kaanapali / oneplus-infiniti) AMOLED
 * display, DSI command mode + DSC 1.2.
 *
 * The phone ships with one of (at least) three panel sources sharing the
 * same "A0020" DDIC family, distinguished only at runtime:
 *
 *   AC180 "P_3"  1264x2780@120           DSC 1.2 4:4:4  slice 632x20
 *   AD296 "P_3"  1272x2772@60..165       DSC 1.2 NATIVE 4:2:2 slice 636x22
 *   AA601 "P_7"  1272x2772@60..165       DSC 1.2 NATIVE 4:2:2 slice 636x22
 *
 * All values marked [DTBO] are decoded verbatim from the stock
 * CPH2749_16.0.0.205 dtbo (panel nodes qcom,mdss_dsi_panel_{AC180,AD296,
 * AA601}_*_A0020_dsc_cmd, timing@sdc_fhd_120). Values marked [INFERRED] or
 * [ADDED] are our choices and must be validated on hardware.
 *
 * !! DRAFT - NOT YET RUN ON HARDWARE !!
 *
 * Template: panel-novatek-nt37801.c (same SoC generation, cmd mode + DSC).
 *
 * NOTE (AD296/AA601): native 4:2:2 DSC requires dsi_host.c to stop forcing
 * 4:4:4 pre-SCR RC parameters (see accompanying dsi-host-dsc-params.patch);
 * without that patch, probe of those variants fails on purpose.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <drm/display/drm_dsc.h>
#include <drm/display/drm_dsc_helper.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

#include <video/mipi_display.h>

/*
 * [DTBO] qcom,mdss-dsi-bl-max-level = <0xffe>, 12-bit DBV;
 * oplus,dsi-brightness-default-level = <0x666>.
 * [DTBO] qcom,mdss-dsi-bl-inverted-dbv: downstream byte-swaps the DBV before
 * mipi_dsi_dcs_set_display_brightness() (which is little-endian), so the
 * WIRE format is MSB-first => mainline's _large() variant. Cross-checked
 * against the stock hbm-on command "51 0e e0" (0x0ee0 = 3808, sane 12-bit).
 */
#define OP15_BL_MAX_DBV		0xffe
#define OP15_BL_DEFAULT_DBV	0x666

struct op15_panel_desc {
	const char *name;
	const struct drm_display_mode *mode;
	void (*dsc_config)(struct drm_dsc_config *dsc);
	int (*init)(struct mipi_dsi_multi_context *dsi_ctx);
};

struct op15_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	const struct op15_panel_desc *desc;
	struct drm_dsc_config dsc;
	struct gpio_desc *reset_gpio;
	struct regulator *vddio;	/* [DTBO] 1.8 V, pmh0101_l12 */
	struct regulator *vdd;		/* [DTBO] 1.2 V, pmh0101_l11 */
	struct regulator *vci;		/* [DTBO] 3.0 V, pmh0101_l13 */
	bool first_prepare_done;	/* [ADDED] D15 reset-from-on ran once */
};

static inline struct op15_panel *to_op15_panel(struct drm_panel *panel)
{
	return container_of(panel, struct op15_panel, panel);
}

/*
 * [DTBO] qcom,mdss-dsi-reset-sequence = <1 2  0 5  1 25> (level, ms) with
 * oplus,panel-reset-position = <2> (reset after all rails are up), plus
 * qcom,mdss-dsi-init-delay-us = <1000> before the first command.
 * reset-gpios in DT is GPIO_ACTIVE_LOW, so gpiod value 1 = panel in reset.
 */
static void op15_panel_reset(struct op15_panel *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);	/* pin high */
	usleep_range(2000, 3000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);	/* pin low */
	usleep_range(5000, 6000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);	/* pin high */
	usleep_range(25000, 26000);
	usleep_range(1000, 2000);			/* init-delay-us */
}

/*
 * [DTBO] AC180 qcom,mdss-dsi-on-command, timing@sdc_fhd_120
 * (merged.dts:38816), qcom,mdss-dsi-on-command-state = "dsi_lp_mode".
 * 63 commands decoded from the 7-byte-header qcom blob; the 0x81 long write
 * on page 07 is the DDIC's DSC PPS container (bytes 7.. = standard 94-byte
 * DSC 1.2 PPS: 1264x2780, slice 632x20, 10 bpc, 8 bpp, block pred, 4:4:4).
 */
static int op15_ac180_init(struct mipi_dsi_multi_context *dsi_ctx)
{
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x20);	/* page 20 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbf, 0x06);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x94, 0x00, 0xd0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x1f);	/* page 1f */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x82, 0x00, 0x20);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x87, 0x09);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x88, 0x34);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x89, 0xc1, 0x80, 0xc0, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8a, 0x8c, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8c, 0x80, 0x07, 0x00, 0x40);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8e, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x96, 0x15, 0x35, 0x55, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x97, 0x11, 0x33, 0x22, 0x01, 0x09);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83,
				     0x00, 0x0b, 0x0a, 0x0c, 0x02, 0xff, 0xff, 0xff, 0x0c, 0x02, 0xff, 0xff,
				     0x00, 0xff, 0x00, 0x0b, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8f,
				     0x03, 0x2d, 0x8c, 0x19, 0x07, 0x00, 0x00, 0x00, 0x19, 0x07, 0x00, 0x00,
				     0x00, 0x00, 0x02, 0x2d, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85, 0x00, 0x0b, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x91, 0x03, 0x2d, 0x72);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x29);	/* page 29 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x82,
				     0x90, 0x90, 0x6c, 0x28, 0x14, 0x14, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83,
				     0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x84,
				     0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x08);	/* page 08 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc8, 0x62);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x07);	/* page 07 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8a, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8b, 0x21, 0xe0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x81,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0xab, 0x30, 0x80, 0x0a,
				     0xdc, 0x04, 0xf0, 0x00, 0x14, 0x02, 0x78, 0x02, 0x78, 0x02, 0x00, 0x02,
				     0x57, 0x00, 0x20, 0x01, 0xf8, 0x00, 0x08, 0x00, 0x0d, 0x05, 0x7a, 0x04,
				     0x4f, 0x18, 0x00, 0x10, 0xe0, 0x07, 0x10, 0x20, 0x00, 0x06, 0x0f, 0x0f,
				     0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79,
				     0x7b, 0x7d, 0x7e, 0x02, 0x02, 0x22, 0x00, 0x2a, 0x40, 0x2a, 0xbe, 0x3a,
				     0xfc, 0x3a, 0xfa, 0x3a, 0xf8, 0x3b, 0x38, 0x3b, 0x78, 0x3b, 0xb6, 0x4b,
				     0xb6, 0x4b, 0xf4, 0x4b, 0xf4, 0x6c, 0x34, 0x84, 0x74, 0x74, 0x00, 0x00,
				     0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x21);	/* page 21 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa4, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x5e, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x61, 0x07);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x53, 0x20);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);	/* page 2d */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x97, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb1, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x98, 0x03, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x99, 0x05, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x9a, 0x0b, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x9b, 0x17, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa1, 0x01, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb2,
				     0x02);	/* dt type 15 w/ 3 bytes; wire carries 2 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb3,
				     0x04);	/* dt type 15 w/ 3 bytes; wire carries 2 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb4,
				     0x08);	/* dt type 15 w/ 3 bytes; wire carries 2 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb5,
				     0x11);	/* dt type 15 w/ 3 bytes; wire carries 2 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xba, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb, 0x01, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x02);	/* page 02 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb0,
				     0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);	/* page 2d */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf2, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_msleep(dsi_ctx, 2);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x11, 0x00);
	mipi_dsi_msleep(dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x29, 0x00);
	mipi_dsi_msleep(dsi_ctx, 20);

	return dsi_ctx->accum_err;
}

/*
 * [DTBO] AD296 qcom,mdss-dsi-on-command, timing@sdc_fhd_120, state
 * dsi_lp_mode. 242 commands; note exit_sleep_mode (11h, 120 ms) sits
 * mid-sequence with a long calibration block after it - replayed verbatim.
 */
static int op15_ad296_init(struct mipi_dsi_multi_context *dsi_ctx)
{
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x08);	/* page 08 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc8, 0x62);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x5f);	/* page 5f */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x81, 0x52);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x07);	/* page 07 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8a, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8b, 0x21, 0xe0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x81,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0xab, 0x21, 0x00, 0x0a,
				     0xd4, 0x04, 0xf8, 0x00, 0x16, 0x02, 0x7c, 0x02, 0x7c, 0x01, 0x55, 0x01,
				     0xd7, 0x00, 0x0a, 0x01, 0x2d, 0x00, 0x35, 0x00, 0x0d, 0x04, 0xf4, 0x16,
				     0x3b, 0x08, 0x00, 0x0c, 0x00, 0x07, 0x10, 0x20, 0x00, 0x06, 0x0f, 0x0f,
				     0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79,
				     0x7b, 0x7d, 0x7e, 0x00, 0x82, 0x11, 0x40, 0x19, 0xc0, 0x22, 0x3e, 0x32,
				     0x7c, 0x3a, 0xba, 0x3a, 0xf8, 0x3b, 0x38, 0x3b, 0x38, 0x3b, 0x76, 0x4b,
				     0x76, 0x4b, 0x74, 0x4b, 0x74, 0x5b, 0xb4, 0x73, 0xf4, 0x01, 0x00, 0x00,
				     0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x91, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x14);	/* page 14 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x80, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x82, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x51);	/* page 51 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa0, 0x98, 0xa9, 0xa8, 0xa4, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfe, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa1, 0xa4, 0xa0, 0x73, 0x6e, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfe, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa2, 0x66, 0x67, 0x67, 0x68, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfe, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x52);	/* page 52 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa1, 0x50, 0x50, 0x50, 0x50, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa2, 0x50, 0x50, 0x50, 0x50, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc0, 0x7c, 0xd2, 0x9e, 0x50, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc1, 0x50, 0x50, 0x52, 0x57, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc2, 0x53, 0x53, 0x51, 0x54, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfe, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);	/* page 2d */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xec, 0x01, 0xff, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x94, 0xf5, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x31);	/* page 31 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa0, 0xa3);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x07);	/* page 07 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe5, 0x05);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x4d, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x20);	/* page 20 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbf, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x94, 0x00, 0xd0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x1f);	/* page 1f */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x82, 0x00, 0x20);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x87, 0x09);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x88, 0x34);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x89, 0x41, 0x00, 0xc4, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8a, 0x8c, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8c, 0x80, 0x07, 0x00, 0x40);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8e, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x96, 0x15, 0x35, 0x55, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x97, 0x11, 0x33, 0x22, 0x01, 0x09);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83,
				     0x00, 0x0b, 0x0a, 0x0c, 0x02, 0xff, 0xff, 0xff, 0x0c, 0x02, 0xff, 0xff,
				     0x00, 0xff, 0x00, 0x0b, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8f,
				     0x03, 0x30, 0x8c, 0x19, 0x0f, 0x00, 0x00, 0x00, 0x19, 0x0f, 0x00, 0x00,
				     0x00, 0x00, 0x02, 0x30, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85,
				     0x00, 0x0b, 0x0a, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x91, 0x03, 0x30, 0x20);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x29);	/* page 29 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x82,
				     0x7c, 0x38, 0x24, 0x14, 0x14, 0x14, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83,
				     0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x84,
				     0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85, 0x14, 0x14, 0x14, 0x14, 0x14, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x2b);	/* page 2b */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc6, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc9, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xca, 0x0f, 0x00, 0xf1, 0xf9);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcd, 0x3f, 0xf7, 0xf7, 0xfa);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe4, 0x3f, 0xf0, 0xf0, 0xf3);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe7, 0x3c, 0xe0, 0xf8, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xea, 0x3c, 0xe0, 0xf8, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xed, 0x3c, 0xe0, 0xf8, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe1, 0x3f, 0xf0, 0xee, 0xf3);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe2, 0x3f, 0xf3, 0xfa, 0xe4);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe5, 0x3f, 0xf3, 0xec, 0xe0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);	/* page 2d */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x98, 0x03, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa0, 0x00, 0x01, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb3, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb4, 0x11);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb5, 0x59);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xba, 0x00, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb, 0x02, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x06);	/* page 06 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc6, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x08);	/* page 08 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2, 0x05);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd3, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe0, 0x22);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x06);	/* page 06 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa0, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x21);	/* page 21 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa4, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x5e, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x61, 0x07);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x35, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x53, 0x20);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x17);	/* page 17 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa0, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x22);	/* page 22 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd8, 0x01, 0x00, 0x11, 0x10, 0x3c, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd9, 0x00, 0xfc, 0x11, 0x10, 0x30, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdc,
				     0x84, 0x94, 0xbb, 0x00, 0x20, 0xc4, 0xa1, 0xcc, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xde, 0xf1);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe0, 0x02, 0x00, 0x5f, 0x21, 0x00, 0x28);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x7d, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x22);	/* page 22 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd8, 0x01, 0x00, 0x11, 0x10, 0x3c, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd9, 0x00, 0xfc, 0x11, 0x10, 0x30, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdc,
				     0x84, 0x94, 0xbb, 0x00, 0x20, 0xc4, 0xa1, 0xcc, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xde, 0xf1);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe0, 0x02, 0x00, 0x5f, 0x21, 0x00, 0x28);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x7d, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x7d, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);	/* page 2d */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf2, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x11);
	mipi_dsi_msleep(dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x49);	/* page 49 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x82,
				     0x4c, 0x00, 0x41, 0x02, 0xa6, 0x03, 0xdd, 0x06, 0xde, 0x06, 0xdf, 0x06,
				     0xe0, 0x06, 0xe1, 0x06, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85, 0x80, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x86, 0x80, 0x00, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x5a);	/* page 5a */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x81, 0x0d, 0x0d, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83, 0x0d, 0x0d, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8a, 0x14, 0x16, 0x0d);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8b, 0x16, 0x16, 0x0e);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8c, 0x15, 0x17, 0x0e);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8d, 0x10, 0x16, 0x0b);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x95, 0x26, 0x2a, 0x26);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xaa, 0x06, 0x1a, 0x06);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xaf, 0x13, 0x1a, 0x12);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb0, 0x13, 0x16, 0x13);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb1, 0x13, 0x15, 0x13);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb2, 0x17, 0x15, 0x16);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc8, 0x02, 0x06, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc9, 0x04, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xca, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcb, 0x07, 0x0d, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcc, 0xfa, 0xfa, 0xfa);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcd, 0xfa, 0xfa, 0xfa);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xce, 0x02, 0x04, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2, 0x09, 0x06, 0x06);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd4, 0x08, 0x0a, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd5, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd6, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xda, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdb, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdc, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe2, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe3, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x5b);	/* page 5b */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x80, 0x02, 0x06, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x81, 0x04, 0x06, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83, 0x04, 0x08, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x84, 0x01, 0x02, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85, 0x01, 0x02, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x86, 0x0a, 0x0a, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8a, 0x1a, 0x16, 0x12);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8b, 0x16, 0x14, 0x12);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8c, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8d, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8e, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x92, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x93, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x99, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x9a, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x9b, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa4, 0x06, 0x08, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa5, 0x07, 0x0a, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb0, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb1, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb2, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb6, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb7, 0x00, 0x01, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb8, 0x00, 0x01, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9, 0x16, 0x12, 0x16);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbe, 0x00, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbf, 0x00, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc9, 0x06, 0x0a, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcb, 0x02, 0x04, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd4, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd5, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd6, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xda, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdb, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe0, 0x08, 0x0a, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe1, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe2, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe3, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x5c);	/* page 5c */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8c, 0x02, 0x02, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8d, 0x03, 0x03, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8e, 0x02, 0x02, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8f, 0x15, 0x16, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x90, 0x0d, 0x0d, 0x0f);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x91, 0x0a, 0x0f, 0x0a);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x92, 0x02, 0x02, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x93, 0xfd, 0x00, 0xff);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x94, 0xfd, 0x00, 0xff);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x95, 0x14, 0x1b, 0x1d);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x96, 0x12, 0x1a, 0x17);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x97, 0x12, 0x18, 0x16);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x98, 0x0a, 0x18, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x99, 0x04, 0x06, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x9a, 0x00, 0x03, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x9b, 0x00, 0x03, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb1, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb2, 0x01, 0x01, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb6, 0x02, 0x04, 0x02);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb7, 0x00, 0x04, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb8, 0x00, 0x04, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xb9, 0x1b, 0x2e, 0x19);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xba, 0x28, 0x2e, 0x16);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbb, 0x12, 0x18, 0x16);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbc, 0x21, 0x25, 0x12);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbd, 0x00, 0x04, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbe, 0x00, 0x04, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xbf, 0x00, 0x04, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xca, 0x04, 0x08, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xcb, 0x04, 0x04, 0x04);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x29);
	mipi_dsi_msleep(dsi_ctx, 20);

	return dsi_ctx->accum_err;
}

/*
 * [DTBO] AA601 qcom,mdss-dsi-on-command, timing@sdc_fhd_120, state
 * dsi_lp_mode. 85 commands.
 */
static int op15_aa601_init(struct mipi_dsi_multi_context *dsi_ctx)
{
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x13);	/* page 13 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf9, 0x4c);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x17);	/* page 17 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa0, 0x0e);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x07);	/* page 07 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8a, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8b, 0x21, 0xe0);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x80,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0xab, 0x30, 0x80, 0x0a,
				     0xd4, 0x04, 0xf8, 0x00, 0x16, 0x02, 0x7c, 0x02, 0x7c, 0x02, 0x00, 0x02,
				     0x59, 0x00, 0x20, 0x02, 0x2e, 0x00, 0x08, 0x00, 0x0d, 0x04, 0xf4, 0x03,
				     0xed, 0x18, 0x00, 0x10, 0xf0, 0x07, 0x10, 0x20, 0x00, 0x06, 0x0f, 0x0f,
				     0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79,
				     0x7b, 0x7d, 0x7e, 0x02, 0x02, 0x22, 0x00, 0x2a, 0x40, 0x2a, 0xbe, 0x3a,
				     0xfc, 0x3a, 0xfa, 0x3a, 0xf8, 0x3b, 0x38, 0x3b, 0x78, 0x3b, 0xb6, 0x4b,
				     0xb6, 0x4b, 0xf4, 0x4b, 0xf4, 0x6c, 0x34, 0x84, 0x74, 0x00, 0x00, 0x00,
				     0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x81,
				     0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0xab, 0x21, 0x00, 0x0a,
				     0xd4, 0x04, 0xf8, 0x00, 0x16, 0x02, 0x7c, 0x02, 0x7c, 0x01, 0x55, 0x01,
				     0xd7, 0x00, 0x0a, 0x01, 0x2d, 0x00, 0x35, 0x00, 0x0d, 0x04, 0xf4, 0x16,
				     0x3b, 0x08, 0x00, 0x0c, 0x00, 0x07, 0x10, 0x20, 0x00, 0x06, 0x0f, 0x0f,
				     0x33, 0x0e, 0x1c, 0x2a, 0x38, 0x46, 0x54, 0x62, 0x69, 0x70, 0x77, 0x79,
				     0x7b, 0x7d, 0x7e, 0x00, 0x82, 0x11, 0x40, 0x19, 0xc0, 0x22, 0x3e, 0x32,
				     0x7c, 0x3a, 0xba, 0x3a, 0xf8, 0x3b, 0x38, 0x3b, 0x38, 0x3b, 0x76, 0x4b,
				     0x76, 0x4b, 0x74, 0x4b, 0x74, 0x5b, 0xb4, 0x73, 0xf4, 0x01, 0x00, 0x00,
				     0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x07);	/* page 07 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x91, 0x03);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x14);	/* page 14 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x82, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x14);	/* page 14 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x80, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x4f);	/* page 4f */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x81, 0x06);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x08);	/* page 08 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc8, 0x62);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x21);	/* page 21 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa4, 0x38);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x23);	/* page 23 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85, 0x15);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x01);	/* page 01 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xfb, 0x08);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x06);	/* page 06 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xa0, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc6, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x08);	/* page 08 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd2, 0x05);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd3, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x22);	/* page 22 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd8, 0x01, 0x00, 0x11, 0x10, 0x3c, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xd9, 0x00, 0xfc, 0x11, 0x10, 0x30, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdc,
				     0x84, 0x94, 0xbb, 0x00, 0x20, 0xc4, 0xa1, 0xcc, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdd, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xde, 0xf1);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xdf, 0x80);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xe0, 0x02, 0x00, 0x5f, 0x21, 0x00, 0x28);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x7d, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x5e, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x61, 0x07);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x6d, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x35);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x53, 0x20);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);	/* page 2d */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xec, 0x01, 0xff, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x94, 0xf5, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x20);	/* page 20 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc8, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xc7, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x94, 0x00, 0xd0, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x1f);	/* page 1f */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x87, 0x09);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x88, 0x34);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x89, 0x41, 0x00, 0xc4, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8a, 0x8c, 0x14);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8c, 0x80, 0x07, 0x00, 0x40);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8e, 0x10);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x95, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x96, 0x15, 0x35, 0x55, 0x01);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83,
				     0x00, 0x0b, 0x0a, 0x0c, 0x02, 0x0f, 0xff, 0xff, 0x0c, 0x02, 0xff, 0xff,
				     0x00, 0xff, 0x00, 0x0b, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x8f,
				     0x03, 0x30, 0x8c, 0x19, 0x0f, 0x18, 0x00, 0x00, 0x19, 0x0f, 0x00, 0x00,
				     0x00, 0x00, 0x02, 0x30, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85,
				     0x00, 0x0b, 0x0a, 0xff, 0xff, 0xff, 0xff, 0xff);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x91,
				     0x03, 0x30, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x29);	/* page 29 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x82,
				     0x74, 0x2c, 0x2a, 0x2a, 0x28, 0x28, 0x26, 0x26);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83,
				     0x24, 0x24, 0x22, 0x22, 0x20, 0x20, 0x1e, 0x1e);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x84,
				     0x1c, 0x1c, 0x1a, 0x1a, 0x18, 0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x85, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x60, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x7d, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x2d);	/* page 2d */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x83, 0x00, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xf2, 0x00);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0xff, 0x5a, 0xa5, 0x00);	/* page 00 */
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x11);
	mipi_dsi_msleep(dsi_ctx, 120);
	mipi_dsi_dcs_write_seq_multi(dsi_ctx, 0x29);
	mipi_dsi_msleep(dsi_ctx, 20);

	return dsi_ctx->accum_err;
}

/*
 * DSC configuration, decoded from the PPS blob each panel's own init
 * sequence carries (the 0x81 write on page 07) - so encoder (DPU) and
 * decoder (DDIC) agree byte-for-byte. All fields [DTBO] unless noted.
 *
 * The "derived" fields (slice_chunk_size, nfl/slice bpg offsets, scale
 * intervals, initial_dec_delay, final_offset) are NOT set here: with the
 * dsi-host-dsc-params.patch applied, drm_dsc_compute_rc_parameters()
 * regenerates every one of them bit-identically to the stock PPS (verified
 * numerically offline for both variants).
 */

/* Shared constants across all three panels' stock PPS */
static void op15_dsc_common(struct drm_dsc_config *dsc)
{
	static const u16 rc_buf_thresh[] = {
		14, 28, 42, 56, 70, 84, 98, 105, 112, 119, 121, 123, 125, 126,
	};
	int i;

	dsc->dsc_version_major = 1;
	dsc->dsc_version_minor = 2;
	dsc->bits_per_component = 10;
	dsc->line_buf_depth = 11;
	dsc->block_pred_enable = true;
	dsc->slice_count = 2;
	dsc->mux_word_size = 48;
	dsc->rc_model_size = 8192;
	dsc->rc_edge_factor = 6;
	dsc->rc_quant_incr_limit0 = 15;
	dsc->rc_quant_incr_limit1 = 15;
	dsc->rc_tgt_offset_high = 3;
	dsc->rc_tgt_offset_low = 3;
	dsc->flatness_min_qp = 7;
	dsc->flatness_max_qp = 16;
	/* stock uses 13 (slice-height-adjusted), not the table's 12/15 */
	dsc->first_line_bpg_offset = 13;

	for (i = 0; i < ARRAY_SIZE(rc_buf_thresh); i++)
		dsc->rc_buf_thresh[i] = rc_buf_thresh[i];
}

static void op15_dsc_set_ranges(struct drm_dsc_config *dsc, const u8 (*r)[3])
{
	int i;

	for (i = 0; i < DSC_NUM_BUF_RANGES; i++) {
		dsc->rc_range_params[i].range_min_qp = r[i][0];
		dsc->rc_range_params[i].range_max_qp = r[i][1];
		dsc->rc_range_params[i].range_bpg_offset = r[i][2];
	}
}

/* [DTBO] AC180: DSC 1.2 4:4:4, 8 bpp, 10 bpc - equals the standard VESA
 * DSC 1.2 RC table (drm rc_parameters_1_2_444 for 8bpp/10bpc). */
static void op15_ac180_dsc(struct drm_dsc_config *dsc)
{
	/* bpg offsets in 6-bit two's complement, as in the PPS */
	static const u8 ranges[DSC_NUM_BUF_RANGES][3] = {
		{ 0,  8,  2 }, { 4,  8,  0 }, { 5,  9,  0 }, { 5, 10, 62 },
		{ 7, 11, 60 }, { 7, 11, 58 }, { 7, 11, 56 }, { 7, 12, 56 },
		{ 7, 13, 56 }, { 7, 14, 54 }, { 9, 14, 54 }, { 9, 15, 52 },
		{ 9, 15, 52 }, { 13, 16, 52 }, { 16, 17, 52 },
	};

	op15_dsc_common(dsc);
	dsc->slice_width = 632;
	dsc->slice_height = 20;
	dsc->bits_per_pixel = 8 << 4;
	dsc->convert_rgb = 1;
	dsc->initial_xmit_delay = 512;
	dsc->initial_offset = 6144;
	dsc->initial_scale_value = 32;
	op15_dsc_set_ranges(dsc, ranges);
}

/* [DTBO] AD296 + AA601: DSC 1.2 NATIVE 4:2:2 (convert_rgb = 0), effective
 * 8 bpp (PPS bpp field = 16 per the 4:2:2 doubling convention), 10 bpc -
 * equals drm rc_parameters_1_2_422 for 8bpp/10bpc.
 * REQUIRES the dsi_host native-422 patch; unpatched mainline forces 4:4:4. */
static void op15_ad296_dsc(struct drm_dsc_config *dsc)
{
	static const u8 ranges[DSC_NUM_BUF_RANGES][3] = {
		{ 0,  2,  2 }, { 2,  5,  0 }, { 3,  7,  0 }, { 4,  8, 62 },
		{ 6,  9, 60 }, { 7, 10, 58 }, { 7, 11, 56 }, { 7, 12, 56 },
		{ 7, 12, 56 }, { 7, 13, 54 }, { 9, 13, 54 }, { 9, 13, 52 },
		{ 9, 13, 52 }, { 11, 14, 52 }, { 14, 15, 52 },
	};

	op15_dsc_common(dsc);
	dsc->slice_width = 636;
	dsc->slice_height = 22;
	dsc->bits_per_pixel = 16 << 4;	/* 2 * 8 bpp, native-422 convention */
	dsc->convert_rgb = 0;
	dsc->native_422 = 1;
	dsc->initial_xmit_delay = 341;
	dsc->initial_offset = 2048;
	dsc->initial_scale_value = 10;
	op15_dsc_set_ranges(dsc, ranges);
}

static int op15_panel_on(struct op15_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };
	const struct drm_display_mode *mode = ctx->desc->mode;

	/* [DTBO] on-command-state = dsi_lp_mode */
	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ctx->desc->init(&dsi_ctx);

	/*
	 * [ADDED] not in the stock init: reset the DDIC partial-update
	 * window to full screen. The zombie-splash investigation (M2)
	 * left open whether a stale partial window truncates commits;
	 * this makes the driver independent of bootloader DDIC state.
	 */
	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0,
					      mode->hdisplay - 1);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0,
					    mode->vdisplay - 1);

	/*
	 * [ADDED] mid-range brightness so first light is visible;
	 * stock leaves DBV at 0 until Android's first frame
	 * (qcom,bl-update-flag = "delay_until_first_frame").
	 */
	mipi_dsi_dcs_write_seq_multi(&dsi_ctx, MIPI_DCS_SET_DISPLAY_BRIGHTNESS,
				     OP15_BL_DEFAULT_DBV >> 8,
				     OP15_BL_DEFAULT_DBV & 0xff);

	return dsi_ctx.accum_err;
}

/* [DTBO] qcom,mdss-dsi-off-command (identical on all three panels) */
static int op15_panel_power_down(struct op15_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = dsi };

	/* [DTBO] off-command-state = dsi_hs_mode */
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 20);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 120);

	return dsi_ctx.accum_err;
}

static int op15_panel_off(struct op15_panel *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;

	/*
	 * [DIAG-GRAM] The decisive measurement, taken HERE (panel_off runs after
	 * a whole session of pushed frames; the prepare-time [DIAG] runs before
	 * any pixel ever moves).
	 *
	 * THE HOLE THIS FILLS: every "the DDIC receives data" claim so far is
	 * LP-mode only (DCS reads/writes over LP+BTA). INTF_FRAME_COUNT and the
	 * INTF MISR live INSIDE the DPU, UPSTREAM of the DSI controller and PHY --
	 * they cannot show that one intact HS byte ever reached the panel. Seven
	 * transport hypotheses were refuted against evidence that structurally
	 * could not confirm delivery.
	 *
	 * Reading the DDIC's own frame memory partitions the ENTIRE remaining
	 * fault space in one shot:
	 *   GRAM VARIES with content => HS + DSC decode WORK; "white" is a
	 *                               post-GRAM display-MODE fault.
	 *   GRAM CONSTANT            => data never lands => payload corruption
	 *                               or the HS burst never arrives (PHY).
	 * Supporting reads: 0x05 RDNUMED (DSI error count -- nonzero => HS
	 * arrives corrupted); 0x09 RDDST (display status -- is white a MODE?).
	 */
	{
		u8 numed = 0, rddst[4] = {}, gram[8] = {};
		int rn, rs, rg;
		/* full-window CASET/PASET, then read a few bytes of GRAM */
		static const u8 caset[] = { 0x00, 0x00, 0x00, 0x03 };
		static const u8 paset[] = { 0x00, 0x00, 0x00, 0x00 };

		rn = mipi_dsi_dcs_read(dsi, 0x05, &numed, 1);
		rs = mipi_dsi_dcs_read(dsi, 0x09, rddst, sizeof(rddst));
		mipi_dsi_dcs_write(dsi, 0x2a, caset, sizeof(caset));
		mipi_dsi_dcs_write(dsi, 0x2b, paset, sizeof(paset));
		rg = mipi_dsi_dcs_read(dsi, 0x2e, gram, sizeof(gram));
		dev_info(&dsi->dev,
			 "[DIAG-GRAM] RDNUMED(05h) rc=%d err_count=0x%02x | RDDST(09h) rc=%d %*ph | GRAM(2Eh) rc=%d %*ph\n",
			 rn, numed, rs, (int)sizeof(rddst), rddst,
			 rg, (int)sizeof(gram), gram);
	}

	return op15_panel_power_down(ctx);
}

static int op15_panel_prepare(struct drm_panel *panel)
{
	struct op15_panel *ctx = to_op15_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	struct drm_dsc_picture_parameter_set pps;
	int ret;

	/*
	 * [DTBO] oplus,panel-power-on-sequence =
	 *   1ms, vddio, 3ms, vdd, 3ms, gpio1, 3ms, gpio2, 3ms, vci, 10ms.
	 * gpio1 (pmk8850 GPIO1) and gpio2 (pmh0101 GPIO4) gate additional
	 * OLED rails; v1 deliberately does NOT touch them - the bootloader
	 * leaves them enabled and nothing in this driver disables them.
	 * regulator_bulk_enable() is parallel, so enable one by one to
	 * keep the stock ordering/delays.
	 */
	/*
	 * [TEMPORARY WORKAROUND -- not for upstream; voltage belongs in DT
	 * constraints and this WILL be rejected upstream. Remove once the DT
	 * actually reaches the kernel.]
	 *
	 * Force panel vddio to 1.8 V from CODE. The DTS pins pmh0101_l12 at
	 * 1800000/1800000, but our DTS is INERT on this device: the kernel boots
	 * the dtb EMBEDDED in the Mu-Silicium image (linux @3eb9e167), not the
	 * closure's. That dtb allows 1200000-1800000, and machine_constraints_
	 * voltage() only corrects a voltage OUTSIDE the range -- 1.2 V sits
	 * legally inside it, so the bootloader's value is never touched and the
	 * rail parks at its floor. Verified live:
	 *   regulator_summary: pmh0101_l12 ... 1200mV ... 1200mV 1800mV
	 *
	 * 1.2 V is 27% BELOW Qualcomm's own minimum for this exact rail:
	 * kaanapali-mtp.dts uses the identical PMIC (qcom,pmh0101-rpmh-regulators,
	 * pmic-id "B_E0") and identical ldo12 as the mdss_dsi0 panel vddio-supply,
	 * named vreg_l12b_1p8 and constrained 1650000-1800000. The 1200000 floor
	 * appears copy-pasted from l11 (which genuinely is a 1.2 V rail -- the
	 * panel's vdd).
	 *
	 * Safe by construction: regulator_check_voltage() clamps the request to
	 * the DT max, so this cannot drive the rail above 1.8 V. Placed BEFORE
	 * regulator_enable() so the rail comes up already at 1.8 V rather than
	 * stepping under a live DDIC. Non-fatal: on failure we simply get today's
	 * 1.2 V behaviour. (If the closure DTB ever wins, min==max removes
	 * REGULATOR_CHANGE_VOLTAGE and this degrades to a harmless no-op.)
	 */
	ret = regulator_set_voltage(ctx->vddio, 1800000, 1800000);
	if (ret)
		dev_warn(dev, "failed to set vddio to 1.8V: %d\n", ret);

	usleep_range(1000, 2000);
	ret = regulator_enable(ctx->vddio);
	if (ret)
		return ret;
	usleep_range(3000, 4000);
	ret = regulator_enable(ctx->vdd);
	if (ret)
		goto err_vddio;
	usleep_range(3000 + 3000 + 3000, 10000);	/* incl. gpio1/gpio2 slots */
	ret = regulator_enable(ctx->vci);
	if (ret)
		goto err_vdd;
	usleep_range(10000, 11000);

	/*
	 * [ADDED] D15 reset-from-on. On the boot-time FIRST prepare the DDIC
	 * is already awake and scanning: the bootloader lit the splash and
	 * left the rails up, so the regulator_enable()s above are refcount-
	 * only and the RESX cycle below is the only reset the DDIC gets.
	 * Initializing straight onto that warm ON-state deterministically
	 * ends in white static, while a re-run of the full unprepare/prepare
	 * sequence fixes it even at a 0 ms gap (2026-07-21 live
	 * discriminator; the rail-discharge-time theory is refuted). The
	 * re-run's delta is the DCS power-down (0x28 display-off + 0x10
	 * sleep-in, stock delays) preceding RESX -- so issue exactly that
	 * before the first init. On a genuinely cold DDIC the two commands
	 * land on a sleeping chip and change nothing. Non-fatal: on error,
	 * fall through to the previous behaviour.
	 */
	if (!ctx->first_prepare_done) {
		ctx->first_prepare_done = true;
		dev_info(dev, "reset-from-on: DCS power-down of possibly-warm DDIC before first init\n");
		ret = op15_panel_power_down(ctx);
		if (ret < 0)
			dev_warn(dev, "reset-from-on power-down failed: %d\n",
				 ret);
	}

	op15_panel_reset(ctx);

	ret = op15_panel_on(ctx);
	if (ret < 0)
		goto err;

	/*
	 * Standard DSC PPS (DT 0x0A) goes out AFTER the on-command, in LP --
	 * the exact downstream wire, and a configuration never yet tested.
	 *
	 * Downstream: dsi_display.c calls dsi_panel_update_pps AFTER the enable
	 * path, and the PPS cmd-set state defaults to LP. The on-command stream
	 * (incl. the vendor 0x81 PPS, the vendor DSC arming writes 8a 01 /
	 * 8b 21 e0 / 91 03, and display_on 0x29) runs first.
	 *
	 * Ordering matters for a non-obvious reason: op15_panel_on() sets
	 * MIPI_DSI_MODE_LPM, and mipi_dsi_device_transfer() picks LP-vs-HS from
	 * the CURRENT flag. Sending the PPS before panel_on (as an earlier revision did)
	 * transmits it in HS -- downstream never does that. Sent here, LPM is
	 * set, so it goes out in LP as stock does.
	 *
	 * We also do NOT send a DSI Compression-Mode Command (DT 0x07,
	 * mipi_dsi_compression_mode()): a full stock-dtbo + downstream-SDE diff
	 * shows downstream NEVER transmits that packet on this panel. Stock arms
	 * DSC entirely via the vendor init above.
	 */
	drm_dsc_pps_payload_pack(&pps, &ctx->dsc);
	ret = mipi_dsi_picture_parameter_set(ctx->dsi, &pps);
	if (ret < 0) {
		dev_err(dev, "failed to transmit PPS: %d\n", ret);
		goto err;
	}

	msleep(28);	/* [INFERRED] carried over from nt37801 template */

	/*
	 * [DIAG] Constant-white bring-up: the DPU emits a correct, content-
	 * varying compressed stream (INTF MISR) and the DSI controller + PPS +
	 * DSC encoder are all byte-correct, yet the panel shows its power-on
	 * white default. Read back DDIC state to partition {DDIC alive but not
	 * decoding} vs {HS/PHY/link dead}: power-mode 0x0A == 0x9C means
	 * booster/sleep-out/display-on/normal (DDIC processed 0x11/0x29 and the
	 * LP+BTA link works); a -110 timeout localizes the fault to the link/PHY.
	 */
	{
		u8 pmode = 0, smode = 0, diag = 0;
		u8 id[3] = {}, ddb[8] = {}, serial[8] = {};
		int rp, rs, rd, r1, r2, r3, rb, rn;

		rp = mipi_dsi_dcs_get_power_mode(ctx->dsi, &pmode);
		rs = mipi_dsi_dcs_read(ctx->dsi, 0x0e, &smode, 1);
		rd = mipi_dsi_dcs_read(ctx->dsi, 0x0f, &diag, 1);
		dev_info(dev,
			 "[DIAG] power_mode(0Ah) rc=%d val=0x%02x | signal_mode(0Eh) rc=%d val=0x%02x | diag(0Fh) rc=%d val=0x%02x\n",
			 rp, pmode, rs, smode, rd, diag);

		/*
		 * [DIAG-ID] Which DDIC is actually fitted? Our DT hardcodes
		 * oneplus,ad296-a0020 with an "identity unresolved" note, but the
		 * downstream TOUCH firmware for this unit is named "AA601" -- and
		 * AD296/AA601 share geometry+PPS while their vendor init sequences
		 * DIFFER (AA601 writes both PPS slots + different gamma). Replaying
		 * the wrong init would leave the DDIC unconfigured -> white default.
		 * Read the standard IDs, plus the vendor serial recipe
		 * (oplus,dsi-serial-number-reg 0x80, 7 bytes, after a page switch).
		 */
		r1 = mipi_dsi_dcs_read(ctx->dsi, 0xda, &id[0], 1);
		r2 = mipi_dsi_dcs_read(ctx->dsi, 0xdb, &id[1], 1);
		r3 = mipi_dsi_dcs_read(ctx->dsi, 0xdc, &id[2], 1);
		rb = mipi_dsi_dcs_read(ctx->dsi, 0x04, ddb, sizeof(ddb));
		dev_info(dev,
			 "[DIAG-ID] RDID1(DAh) rc=%d 0x%02x | RDID2(DBh) rc=%d 0x%02x | RDID3(DCh) rc=%d 0x%02x | DDB(04h) rc=%d %*ph\n",
			 r1, id[0], r2, id[1], r3, id[2], rb, (int)sizeof(ddb), ddb);

		/* vendor serial: page-switch (ff 5a a5 1d) then read 0x80 x7 */
		{
			static const u8 pg1d[] = { 0x5a, 0xa5, 0x1d };
			static const u8 pg00[] = { 0x5a, 0xa5, 0x00 };

			mipi_dsi_dcs_write(ctx->dsi, 0xff, pg1d, sizeof(pg1d));
			rn = mipi_dsi_dcs_read(ctx->dsi, 0x80, serial, 7);
			mipi_dsi_dcs_write(ctx->dsi, 0xff, pg00, sizeof(pg00));
		}
		dev_info(dev, "[DIAG-ID] vendor serial(80h,pg1d) rc=%d %*ph\n",
			 rn, 7, serial);
	}

	return 0;

err:
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_disable(ctx->vci);
err_vdd:
	regulator_disable(ctx->vdd);
err_vddio:
	regulator_disable(ctx->vddio);

	return ret;
}

static int op15_panel_unprepare(struct drm_panel *panel)
{
	struct op15_panel *ctx = to_op15_panel(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = op15_panel_off(ctx);
	if (ret < 0)
		dev_err(dev, "failed to de-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	/*
	 * [DTBO] power-off order: vci, 3ms, (gpio2, gpio1 - untouched here),
	 * vdd, 3ms, vddio, 1ms.
	 */
	regulator_disable(ctx->vci);
	usleep_range(3000 + 6000, 10000);
	regulator_disable(ctx->vdd);
	usleep_range(3000, 4000);
	regulator_disable(ctx->vddio);

	return 0;
}

/*
 * [DTBO] AC180 timing@sdc_fhd_120: 1264x2780, hfp/hpw/hbp 26/2/26,
 * vfp/vpw/vbp 58/2/42, 120 Hz. Physical 71 x 157 mm.
 *
 * [INFERRED] hfp is PADDED 26 -> 122 and the clock raised accordingly:
 * mainline derives the DSI byte clock from mode->clock scaled by the DSC
 * ratio, which with the honest porches lands at ~813 Mbps/lane - below the
 * ~980 Mbps this panel needs to finish a compressed frame within the
 * 7.3 ms transfer window at 120 Hz (downstream pins 1012 Mbps). The pad
 * brings the link to ~1010 Mbps/lane. In command mode the porches are
 * transfer overhead, not real panel timing (see dsi_host.c
 * dsi_adjust_pclk_for_compression). Honest-mode alternative kept below.
 */
static const struct drm_display_mode op15_ac180_mode = {
	.clock = (1264 + 122 + 2 + 26) * (2780 + 58 + 2 + 42) * 120 / 1000,
	.hdisplay = 1264,
	.hsync_start = 1264 + 122,
	.hsync_end = 1264 + 122 + 2,
	.htotal = 1264 + 122 + 2 + 26,
	.vdisplay = 2780,
	.vsync_start = 2780 + 58,
	.vsync_end = 2780 + 58 + 2,
	.vtotal = 2780 + 58 + 2 + 42,
	.width_mm = 71,
	.height_mm = 157,
	.type = DRM_MODE_TYPE_DRIVER,
};

/*
 * [DTBO] AD296/AA601 timing@sdc_fhd_120: 1272x2772, same porches, 120 Hz.
 * (Panel also supports 60/90/144/165; single mode for bring-up.)
 * No pad needed: with native-422 the bpp-based ratio in
 * dsi_adjust_pclk_for_compression() over-provisions the link (~1.5 Gbps
 * vs the 1.11 Gbps downstream pin) - wasteful but on the safe side.
 * [VERIFY] that rate is within the 3 nm PHY PLL range.
 */
/*
 * [OP15] h-porches PADDED 26 -> 56 to pin the DSI link rate to the stock
 * operating point. With the dsi_adjust_pclk_for_compression() fix, the derived
 * byte clock reduces to 90 * vtotal * new_htotal; htotal 1386 lands new_htotal
 * = 538 -> 139,158,882 Hz -> 1113.27 Mbps/lane = stock's
 * qcom,mdss-dsi-panel-clockrate (1112.6) +0.06%, at a true 120 Hz. Unpadded
 * (26) derives 989 Mbps -- self-consistent but BELOW the 1112.6-1363.2 window
 * this DDIC is proven to run. In cmd mode the h-porches program nothing (only
 * STREAM0_CTRL/TOTAL are written; no INTF timing engine), so they exist only as
 * the transfer-overhead term feeding this clock calc -- no MDP FIFO exposure
 * (per-line budget 538*3 = 1614 B >= 1279 B needed; +30.8% sustained margin).
 */
static const struct drm_display_mode op15_ad296_mode = {
	.clock = (1272 + 56 + 2 + 56) * (2772 + 58 + 2 + 42) * 120 / 1000,
	.hdisplay = 1272,
	.hsync_start = 1272 + 56,
	.hsync_end = 1272 + 56 + 2,
	.htotal = 1272 + 56 + 2 + 56,
	.vdisplay = 2772,
	.vsync_start = 2772 + 58,
	.vsync_end = 2772 + 58 + 2,
	.vtotal = 2772 + 58 + 2 + 42,
	.width_mm = 71,
	.height_mm = 157,
	.type = DRM_MODE_TYPE_DRIVER,
};

static int op15_panel_get_modes(struct drm_panel *panel,
				struct drm_connector *connector)
{
	struct op15_panel *ctx = to_op15_panel(panel);

	return drm_connector_helper_get_modes_fixed(connector,
						    ctx->desc->mode);
}

static const struct drm_panel_funcs op15_panel_funcs = {
	.prepare = op15_panel_prepare,
	.unprepare = op15_panel_unprepare,
	.get_modes = op15_panel_get_modes,
};

static int op15_panel_bl_update_status(struct backlight_device *bl)
{
	struct mipi_dsi_device *dsi = bl_get_data(bl);
	u16 brightness = backlight_get_brightness(bl);
	int ret;

	/* DBV goes out in HS mode, MSB first (see OP15_BL_* comment) */
	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_brightness_large(dsi, brightness);
	if (ret < 0)
		return ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct backlight_ops op15_panel_bl_ops = {
	.update_status = op15_panel_bl_update_status,
};

static struct backlight_device *
op15_panel_create_backlight(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	const struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.brightness = OP15_BL_DEFAULT_DBV,
		.max_brightness = OP15_BL_MAX_DBV,
	};

	return devm_backlight_device_register(dev, dev_name(dev), dev, dsi,
					      &op15_panel_bl_ops, &props);
}

static const struct op15_panel_desc op15_ac180_desc = {
	.name = "AC180 P_3 A0020",
	.mode = &op15_ac180_mode,
	.dsc_config = op15_ac180_dsc,
	.init = op15_ac180_init,
};

/*
 * PANEL IDENTITY: RESOLVED = AD296 (2026-07-16, from artifacts).
 *
 * The AA601 theory is DEAD. The production dtbo's touch-IC nodes carry
 * platform_support_project_commandline = "mdss_dsi_panel_AD296_P_3_A0020_dsc_cmd",
 * "mdss_dsi_panel_AC180_P_3_A0020_dsc_cmd" for our project id (0x611f) -- there is
 * NO AA601 entry anywhere, and the oplus touch driver only binds when the
 * bootloader's panel cmdline matches that whitelist. A unit fitted with AA601 glass
 * would ship with DEAD TOUCH: impossible for production hardware. The
 * firmware_name = "AA601" in the touch node (the sole basis for the theory) is a
 * static project-wide label for the shared touch FW blob, present on every unit --
 * a red herring. Whitelist n geometry (1272x2772 rules out AC180's 1264x2780)
 * = AD296. The identity recipes (serial 0x80, btb 0x82, status 0x0A, err 0x0E) are
 * byte-identical between the AD296 and AA601 nodes, so RDID/serial cannot
 * discriminate them -- don't try.
 *
 * A code-routed AA601-init test was run anyway (since DTS edits are inert):
 * still constant white, exactly as the artifact analysis predicted. Reverted.
 */
static const struct op15_panel_desc op15_ad296_desc = {
	.name = "AD296 P_3 A0020",
	.mode = &op15_ad296_mode,
	.dsc_config = op15_ad296_dsc,
	.init = op15_ad296_init,
};

static const struct op15_panel_desc op15_aa601_desc = {
	.name = "AA601 P_7 A0020",
	.mode = &op15_ad296_mode,	/* same geometry as AD296 */
	.dsc_config = op15_ad296_dsc,	/* same PPS as AD296 */
	.init = op15_aa601_init,
};

static int op15_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct op15_panel *ctx;
	int ret;

	ctx = devm_drm_panel_alloc(dev, struct op15_panel, panel,
				   &op15_panel_funcs,
				   DRM_MODE_CONNECTOR_DSI);
	if (IS_ERR(ctx))
		return PTR_ERR(ctx);

	ctx->desc = of_device_get_match_data(dev);
	if (!ctx->desc)
		return -ENODEV;

	ctx->vddio = devm_regulator_get(dev, "vddio");
	if (IS_ERR(ctx->vddio))
		return PTR_ERR(ctx->vddio);
	ctx->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(ctx->vdd))
		return PTR_ERR(ctx->vdd);
	ctx->vci = devm_regulator_get(dev, "vci");
	if (IS_ERR(ctx->vci))
		return PTR_ERR(ctx->vci);

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset-gpios\n");

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	/* [DTBO] 4 lanes, lane_map_0123, burst; no EOT append downstream */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	ctx->panel.prepare_prev_first = true;
	ctx->panel.backlight = op15_panel_create_backlight(dsi);
	if (IS_ERR(ctx->panel.backlight))
		return dev_err_probe(dev, PTR_ERR(ctx->panel.backlight),
				     "failed to create backlight\n");

	drm_panel_add(&ctx->panel);

	/* This panel only works compressed; unconditionally enable DSC */
	dsi->dsc = &ctx->dsc;
	ctx->desc->dsc_config(&ctx->dsc);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "failed to attach to DSI host\n");
	}

	return 0;
}

static void op15_panel_remove(struct mipi_dsi_device *dsi)
{
	struct op15_panel *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

/*
 * [INFERRED] compatible strings are provisional: AC180/AD296/AA601 are
 * OPPO/OnePlus part codes; the true panel-vendor names (BOE/Tianma/...)
 * and hence proper upstream compatibles are not yet known.
 */
static const struct of_device_id op15_panel_of_match[] = {
	{ .compatible = "oneplus,ac180-a0020", .data = &op15_ac180_desc },
	{ .compatible = "oneplus,ad296-a0020", .data = &op15_ad296_desc },
	{ .compatible = "oneplus,aa601-a0020", .data = &op15_aa601_desc },
	{}
};
MODULE_DEVICE_TABLE(of, op15_panel_of_match);

static struct mipi_dsi_driver op15_panel_driver = {
	.probe = op15_panel_probe,
	.remove = op15_panel_remove,
	.driver = {
		.name = "panel-oneplus15-a0020",
		.of_match_table = op15_panel_of_match,
	},
};
module_mipi_dsi_driver(op15_panel_driver);

MODULE_DESCRIPTION("DRM panel driver for the OnePlus 15 A0020 AMOLED panels");
MODULE_LICENSE("GPL");
