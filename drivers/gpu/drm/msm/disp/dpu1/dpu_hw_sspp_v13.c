/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/debugfs.h>

#include "dpu_hwio.h"
#include "dpu_hw_catalog.h"
#include "dpu_hw_lm.h"
#include "dpu_hw_sspp.h"
#include "dpu_kms.h"

#include <drm/drm_file.h>
#include <drm/drm_managed.h>

#include <linux/soc/qcom/ubwc.h>

#define SDE_FETCH_CONFIG_RESET_VALUE   0x00000087

/* CMN Registers -> Source Surface Processing Pipe Common SSPP registers */
/*      Name	                                Offset */
#define SSPP_CMN_CLK_CTRL	                    0x0
#define SSPP_CMN_CLK_STATUS	                    0x4
#define SSPP_CMN_MULTI_REC_OP_MODE	            0x10
#define SSPP_CMN_ADDR_CONFIG	                0x14
#define SSPP_CMN_CAC_CTRL	                    0x20
#define SSPP_CMN_SYS_CACHE_MODE	                0x24
#define SSPP_CMN_QOS_CTRL	                    0x28
#define SSPP_CMN_DANGER_LUT	                    0x2C
#define SSPP_CMN_SAFE_LUT	                    0x30

#define SSPP_CMN_FILL_LEVEL_SCALE	            0x3C
#define SSPP_CMN_FILL_LEVELS	                0x40
#define SSPP_CMN_STATUS	                        0x44
#define SSPP_CMN_FETCH_DMA_RD_OTS	            0x48
#define SSPP_CMN_FETCH_DTB_WR_PLANE0	        0x4C
#define SSPP_CMN_FETCH_DTB_WR_PLANE1	        0x50
#define SSPP_CMN_FETCH_DTB_WR_PLANE2	        0x54
#define SSPP_CMN_DTB_UNPACK_RD_PLANE0	        0x58
#define SSPP_CMN_DTB_UNPACK_RD_PLANE1	        0x5C
#define SSPP_CMN_DTB_UNPACK_RD_PLANE2	        0x60
#define SSPP_CMN_UNPACK_LINE_COUNT	            0x64
#define SSPP_CMN_TPG_CONTROL	                0x68
#define SSPP_CMN_TPG_CONFIG	                    0x6C
#define SSPP_CMN_TPG_COMPONENT_LIMITS	        0x70
#define SSPP_CMN_TPG_RECTANGLE	                0x74
#define SSPP_CMN_TPG_BLACK_WHITE_PATTERN_FRAMES	0x78
#define SSPP_CMN_TPG_RGB_MAPPING	            0x7C
#define SSPP_CMN_TPG_PATTERN_GEN_INIT_VAL	    0x80

/* REC Register set */
/*      Name	                                Offset */
#define SSPP_REC_SRC_FORMAT	                        0x0
#define SSPP_REC_SRC_UNPACK_PATTERN	                0x4
#define SSPP_REC_SRC_OP_MODE	                    0x8
#define SSPP_REC_SRC_CONSTANT_COLOR	                0xC
#define SSPP_REC_SRC_IMG_SIZE	                    0x10
#define SSPP_REC_SRC_SIZE	                        0x14
#define SSPP_REC_SRC_XY	                            0x18
#define SSPP_REC_OUT_SIZE	                        0x1C
#define SSPP_REC_OUT_XY	                            0x20
#define SSPP_REC_SW_PIX_EXT_LR	                    0x24
#define SSPP_REC_SW_PIX_EXT_TB	                    0x28
#define SSPP_REC_SRC_SIZE_ODX	                    0x30
#define SSPP_REC_SRC_XY_ODX	                        0x34
#define SSPP_REC_OUT_SIZE_ODX	                    0x38
#define SSPP_REC_OUT_XY_ODX	                        0x3C
#define SSPP_REC_SW_PIX_EXT_LR_ODX	                0x40
#define SSPP_REC_SW_PIX_EXT_TB_ODX	                0x44
#define SSPP_REC_PRE_DOWN_SCALE	                    0x48
#define SSPP_REC_SRC0_ADDR	                        0x4C
#define SSPP_REC_SRC1_ADDR	                        0x50
#define SSPP_REC_SRC2_ADDR	                        0x54
#define SSPP_REC_SRC3_ADDR	                        0x58
#define SSPP_REC_SRC_YSTRIDE0	                    0x5C
#define SSPP_REC_SRC_YSTRIDE1	                    0x60
#define SSPP_REC_CURRENT_SRC0_ADDR	                0x64
#define SSPP_REC_CURRENT_SRC1_ADDR	                0x68
#define SSPP_REC_CURRENT_SRC2_ADDR	                0x6C
#define SSPP_REC_CURRENT_SRC3_ADDR	                0x70
#define SSPP_REC_SRC_ADDR_SW_STATUS	                0x74
#define SSPP_REC_CDP_CNTL	                        0x78
#define SSPP_REC_TRAFFIC_SHAPER	                    0x7C
#define SSPP_REC_TRAFFIC_SHAPER_PREFILL	            0x80
#define SSPP_REC_PD_MEM_ALLOC	                    0x84
#define SSPP_REC_QOS_CLAMP	                        0x88
#define SSPP_REC_UIDLE_CTRL_VALUE	                0x8C
#define SSPP_REC_UBWC_STATIC_CTRL	                0x90
#define SSPP_REC_UBWC_STATIC_CTRL_OVERRIDE	        0x94
#define SSPP_REC_UBWC_STATS_ROI	                    0x98
#define SSPP_REC_UBWC_STATS_WORST_TILE_ROW_BW_ROI0	0x9C
#define SSPP_REC_UBWC_STATS_TOTAL_BW_ROI0	        0xA0
#define SSPP_REC_UBWC_STATS_WORST_TILE_ROW_BW_ROI1	0xA4
#define SSPP_REC_UBWC_STATS_TOTAL_BW_ROI1	        0xA8
#define SSPP_REC_UBWC_STATS_WORST_TILE_ROW_BW_ROI2	0xAC
#define SSPP_REC_UBWC_STATS_TOTAL_BW_ROI2	        0xB0
#define SSPP_REC_EXCL_REC_CTRL	                    0xB4
#define SSPP_REC_EXCL_REC_SIZE	                    0xB8
#define SSPP_REC_EXCL_REC_XY	                    0xBC
#define SSPP_REC_LINE_INSERTION_CTRL	            0xC0
#define SSPP_REC_LINE_INSERTION_OUT_SIZE	        0xC4
#define SSPP_REC_FETCH_PIPE_ACTIVE	                0xC8
#define SSPP_REC_META_ERROR_STATUS	                0xCC
#define SSPP_REC_UBWC_ERROR_STATUS	                0xD0
#define SSPP_REC_FLUSH_CTRL	                        0xD4
#define SSPP_REC_INTR_EN	                        0xD8
#define SSPP_REC_INTR_STATUS	                    0xDC
#define SSPP_REC_INTR_CLEAR	                        0xE0
#define SSPP_REC_HSYNC_STATUS	                    0xE4
#define SSPP_REC_FP16_CONFIG	                    0x150
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_R_0	        0x154
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_R_1	        0x158
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_G_0	        0x15C
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_G_1	        0x160
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_B_0	        0x164
#define SSPP_REC_FP16_CSC_MATRIX_COEFF_B_1	        0x168
#define SSPP_REC_FP16_CSC_PRE_CLAMP_R	            0x16C
#define SSPP_REC_FP16_CSC_PRE_CLAMP_G	            0x170
#define SSPP_REC_FP16_CSC_PRE_CLAMP_B	            0x174
#define SSPP_REC_FP16_CSC_POST_CLAMP	            0x178

/* SSPP_DGM */
#define SSPP_DGM_0                         0x9F0
#define SSPP_DGM_1                         0x19F0
#define SSPP_DGM_SIZE                      0x420
#define SSPP_DGM_CSC_0                     0x800
#define SSPP_DGM_CSC_1                     0x1800
#define SSPP_DGM_CSC_SIZE                  0xFC
#define VIG_GAMUT_SIZE                     0x1CC
#define SSPP_UCSC_SIZE                     0x80

#define MDSS_MDP_OP_DEINTERLACE            BIT(22)
#define MDSS_MDP_OP_DEINTERLACE_ODD        BIT(23)
#define MDSS_MDP_OP_IGC_ROM_1              BIT(18)
#define MDSS_MDP_OP_IGC_ROM_0              BIT(17)
#define MDSS_MDP_OP_IGC_EN                 BIT(16)
#define MDSS_MDP_OP_FLIP_UD                BIT(14)
#define MDSS_MDP_OP_FLIP_LR                BIT(13)
#define MDSS_MDP_OP_SPLIT_ORDER            BIT(4)
#define MDSS_MDP_OP_BWC_EN                 BIT(0)
#define MDSS_MDP_OP_ROT_90                 BIT(15)
#define MDSS_MDP_OP_PE_OVERRIDE            BIT(31)
#define MDSS_MDP_OP_BWC_LOSSLESS           (0 << 1)
#define MDSS_MDP_OP_BWC_Q_HIGH             (1 << 1)
#define MDSS_MDP_OP_BWC_Q_MED              (2 << 1)

#define SSPP_DECIMATION_CONFIG             0xB4

#define SSPP_VIG_OP_MODE                   0x4
#define SSPP_VIG_CSC_10_OP_MODE            0x0
#define SSPP_TRAFFIC_SHAPER_BPC_MAX        0xFF

#define SSPP_QOS_CTRL_DANGER_SAFE_EN       BIT(0)
/*
 * Definitions for ViG op modes
 */
#define VIG_OP_CSC_DST_DATAFMT BIT(19)
#define VIG_OP_CSC_SRC_DATAFMT BIT(18)
#define VIG_OP_CSC_EN          BIT(17)
#define VIG_OP_MEM_PROT_CONT   BIT(15)
#define VIG_OP_MEM_PROT_VAL    BIT(14)
#define VIG_OP_MEM_PROT_SAT    BIT(13)
#define VIG_OP_MEM_PROT_HUE    BIT(12)
#define VIG_OP_HIST            BIT(8)
#define VIG_OP_SKY_COL         BIT(7)
#define VIG_OP_FOIL            BIT(6)
#define VIG_OP_SKIN_COL        BIT(5)
#define VIG_OP_PA_EN           BIT(4)
#define VIG_OP_PA_SAT_ZERO_EXP BIT(2)
#define VIG_OP_MEM_PROT_BLEND  BIT(1)

/*
 * Definitions for CSC 10 op modes
 */
#define SSPP_VIG_CSC_10_OP_MODE            0x0
#define VIG_CSC_10_SRC_DATAFMT BIT(1)
#define VIG_CSC_10_EN          BIT(0)
#define CSC_10BIT_OFFSET       4

static inline u32 _sspp_calculate_rect_off(enum dpu_sspp_multirect_index rect_index, struct dpu_hw_sspp *ctx)
{
	return (rect_index == DPU_SSPP_RECT_SOLO || rect_index == DPU_SSPP_RECT_0) ?
			ctx->cap->sblk->sspp_rec0_blk.base : ctx->cap->sblk->sspp_rec1_blk.base;
}

void dpu_hw_sspp_setup_multirect_v13(struct dpu_sw_pipe *pipe)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 offset = ctx->cap->sblk->cmn_blk.base;
	u32 mode_mask;

	if (!ctx)
		return;

	if (pipe->multirect_index == DPU_SSPP_RECT_SOLO) {
		/**
		 * if rect index is RECT_SOLO, we cannot expect a
		 * virtual plane sharing the same SSPP id. So we go
		 * and disable multirect
		 */
		mode_mask = 0;
	} else {
		mode_mask = DPU_REG_READ(&ctx->hw, offset + SSPP_CMN_MULTI_REC_OP_MODE);
		mode_mask |= pipe->multirect_index;
		if (pipe->multirect_mode == DPU_SSPP_MULTIRECT_TIME_MX)
			mode_mask |= BIT(2);
		else
			mode_mask &= ~BIT(2);
	}

	DPU_REG_WRITE(&ctx->hw, offset + SSPP_CMN_MULTI_REC_OP_MODE, mode_mask);
}


void dpu_hw_sspp_setup_sourceaddress_v13(struct dpu_sw_pipe *pipe,
		struct dpu_hw_fmt_layout *layout)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	int i;
	u32 addr, ystride0, ystride1;

	if (!ctx)
		return;

	addr = _sspp_calculate_rect_off(pipe->multirect_index, ctx);

	for (i = 0; i < ARRAY_SIZE(layout->plane_addr); i++)
		DPU_REG_WRITE(&ctx->hw, addr + SSPP_REC_SRC0_ADDR + i * 0x4,
				layout->plane_addr[i]);
	
	ystride0 = (layout->plane_pitch[0]) | (layout->plane_pitch[2] << 16);
	ystride1 = (layout->plane_pitch[1]) | (layout->plane_pitch[3] << 16);

	DPU_REG_WRITE(&ctx->hw, addr + SSPP_REC_SRC_YSTRIDE0, ystride0);
	DPU_REG_WRITE(&ctx->hw, addr + SSPP_REC_SRC_YSTRIDE1, ystride1);
}

void dpu_hw_sspp_setup_pe_config_v13(struct dpu_hw_sspp *ctx, //done
		struct dpu_hw_pixel_ext *pe_ext)
{
	struct dpu_hw_blk_reg_map *c;
	u8 color;
	u32 lr_pe[4], tb_pe[4], tot_req_pixels[4];
	const u32 bytemask = 0xff;
	const u32 shortmask = 0xffff;
	u32 offset = ctx->cap->sblk->sspp_rec0_blk.base;

	if (!ctx || !pe_ext)
		return;

	c = &ctx->hw;
	/* program SW pixel extension override for all pipes*/
	for (color = 0; color < DPU_MAX_PLANES; color++) {
		/* color 2 has the same set of registers as color 1 */
		if (color == 2)
			continue;

		lr_pe[color] = ((pe_ext->right_ftch[color] & bytemask) << 24)|
			((pe_ext->right_rpt[color] & bytemask) << 16)|
			((pe_ext->left_ftch[color] & bytemask) << 8)|
			(pe_ext->left_rpt[color] & bytemask);

		tb_pe[color] = ((pe_ext->btm_ftch[color] & bytemask) << 24)|
			((pe_ext->btm_rpt[color] & bytemask) << 16)|
			((pe_ext->top_ftch[color] & bytemask) << 8)|
			(pe_ext->top_rpt[color] & bytemask);

		tot_req_pixels[color] = (((pe_ext->roi_h[color] +
			pe_ext->num_ext_pxls_top[color] +
			pe_ext->num_ext_pxls_btm[color]) & shortmask) << 16) |
			((pe_ext->roi_w[color] +
			pe_ext->num_ext_pxls_left[color] +
			pe_ext->num_ext_pxls_right[color]) & shortmask);
	}

	/* color 0 */
	DPU_REG_WRITE(c, SSPP_REC_SW_PIX_EXT_LR + offset, lr_pe[0]);
	DPU_REG_WRITE(c, SSPP_REC_SW_PIX_EXT_TB + offset, tb_pe[0]);

	/* color 1 and color 2 */
	DPU_REG_WRITE(c, SSPP_REC_SW_PIX_EXT_LR_ODX + offset, lr_pe[1]);
	DPU_REG_WRITE(c, SSPP_REC_SW_PIX_EXT_TB_ODX + offset, tb_pe[1]);

}

void dpu_hw_sspp_setup_format_v13(struct dpu_sw_pipe *pipe, //done
		const struct msm_format *fmt, u32 flags)
{

	struct dpu_hw_sspp *ctx = pipe->sspp;
	struct dpu_hw_blk_reg_map *c;
	u32 chroma_samp, unpack, src_format;
	u32 opmode = 0;
	u32 fast_clear = 0;
	u32 offset;

	if (!ctx || !fmt)
		return;

	offset = _sspp_calculate_rect_off(pipe->multirect_index, ctx);

	c = &ctx->hw;

	opmode = DPU_REG_READ(c, offset + SSPP_REC_SRC_OP_MODE);
	opmode &= ~(MDSS_MDP_OP_FLIP_LR | MDSS_MDP_OP_FLIP_UD |
			MDSS_MDP_OP_BWC_EN | MDSS_MDP_OP_PE_OVERRIDE
			| MDSS_MDP_OP_ROT_90);

	if (flags & DPU_SSPP_FLIP_LR)
		opmode |= MDSS_MDP_OP_FLIP_LR;
	if (flags & DPU_SSPP_FLIP_UD)
		opmode |= MDSS_MDP_OP_FLIP_UD;
	if (flags & DPU_SSPP_ROT_90)
		opmode |= MDSS_MDP_OP_ROT_90;

	chroma_samp = fmt->chroma_sample;
	if (flags & DPU_SSPP_SOURCE_ROTATED_90) {
		if (chroma_samp == CHROMA_H2V1)
			chroma_samp = CHROMA_H1V2;
		else if (chroma_samp == CHROMA_H1V2)
			chroma_samp = CHROMA_H2V1;
	}

	src_format = (chroma_samp << 23) | (fmt->fetch_type << 19) |
		(fmt->bpc_a << 6) | (fmt->bpc_r_cr << 4) |
		(fmt->bpc_b_cb << 2) | (fmt->bpc_g_y << 0);

	if (flags & DPU_SSPP_ROT_90)
		src_format |= BIT(11); /* ROT90 */

	if (fmt->alpha_enable && fmt->fetch_type == MDP_PLANE_INTERLEAVED)
		src_format |= BIT(8); /* SRCC3_EN */

	if (flags & DPU_SSPP_SOLID_FILL)
		src_format |= BIT(22);

	unpack = (fmt->element[3] << 24) | (fmt->element[2] << 16) |
		(fmt->element[1] << 8) | (fmt->element[0] << 0);
	src_format |= ((fmt->unpack_count - 1) << 12) |
		((fmt->flags & MSM_FORMAT_FLAG_UNPACK_TIGHT ? 1 : 0) << 17) |
		((fmt->flags & MSM_FORMAT_FLAG_UNPACK_ALIGN_MSB ? 1 : 0) << 18) |
		((fmt->bpp - 1) << 9);

	if (fmt->fetch_mode != MDP_FETCH_LINEAR) {
		if (MSM_FORMAT_IS_UBWC(fmt))
			opmode |= MDSS_MDP_OP_BWC_EN;
		src_format |= (fmt->fetch_mode & 3) << 30; /*FRAME_FORMAT */
	
		switch (ctx->ubwc->ubwc_enc_version) {
		case UBWC_1_0:
			fast_clear = fmt->alpha_enable ? BIT(31) : 0;
			DPU_REG_WRITE(c, offset + SSPP_REC_UBWC_STATIC_CTRL,
					fast_clear | (ctx->ubwc->ubwc_swizzle & 0x1) |
					BIT(8) |
					(ctx->ubwc->highest_bank_bit << 4));
			break;
		case UBWC_2_0:
			fast_clear = fmt->alpha_enable ? BIT(31) : 0;
			DPU_REG_WRITE(c, offset + SSPP_REC_UBWC_STATIC_CTRL,
					fast_clear | (ctx->ubwc->ubwc_swizzle) |
					(ctx->ubwc->highest_bank_bit << 4));
			break;
		case UBWC_3_0:
			DPU_REG_WRITE(c, offset + SSPP_REC_UBWC_STATIC_CTRL,
					BIT(30) | (ctx->ubwc->ubwc_swizzle) |
					(ctx->ubwc->highest_bank_bit << 4));
			break;
		case UBWC_4_0:
		case UBWC_5_0:
			DPU_REG_WRITE(c, offset + SSPP_REC_UBWC_STATIC_CTRL,
					MSM_FORMAT_IS_YUV(fmt) ? 0 : BIT(30));
			break;
		}
	}

	opmode |= MDSS_MDP_OP_PE_OVERRIDE;

	/* if this is YUV pixel format, enable CSC */
	if (MSM_FORMAT_IS_YUV(fmt))
		src_format |= BIT(15);

	if (MSM_FORMAT_IS_DX(fmt))
		src_format |= BIT(14);

	/* update scaler opmode, if appropriate */
	if (test_bit(DPU_SSPP_CSC, &ctx->cap->features))
		_sspp_setup_opmode(ctx, VIG_OP_CSC_EN | VIG_OP_CSC_SRC_DATAFMT,
			MSM_FORMAT_IS_YUV(fmt));
	else if (test_bit(DPU_SSPP_CSC_10BIT, &ctx->cap->features))
		_sspp_setup_csc10_opmode(ctx,
			VIG_CSC_10_EN | VIG_CSC_10_SRC_DATAFMT,
			MSM_FORMAT_IS_YUV(fmt));

	DPU_REG_WRITE(c, offset + SSPP_REC_SRC_FORMAT, src_format);
	DPU_REG_WRITE(c, offset + SSPP_REC_SRC_UNPACK_PATTERN, unpack);
	DPU_REG_WRITE(c, offset + SSPP_REC_SRC_OP_MODE, opmode);

	/* clear previous UBWC error */
	DPU_REG_WRITE(c, offset + SSPP_REC_UBWC_ERROR_STATUS, BIT(31));
}

void dpu_hw_sspp_setup_cdp_v13(struct dpu_sw_pipe *pipe,
				  const struct msm_format *fmt,
				  bool enable)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	u32 offset = 0;

	if (!ctx)
		return;

	offset = _sspp_calculate_rect_off(pipe->multirect_index, ctx);
	dpu_setup_cdp(&ctx->hw, offset + SSPP_REC_CDP_CNTL, fmt, enable);
}

bool dpu_hw_sspp_setup_clk_force_ctrl_v13(struct dpu_hw_sspp *ctx, bool enable)
{
	static const struct dpu_clk_ctrl_reg sspp_clk_ctrl = {
		.reg_off = SSPP_CMN_CLK_CTRL,
		.bit_off = 0
	};

	return dpu_hw_clk_force_ctrl(&ctx->hw, &sspp_clk_ctrl, enable);
}

void dpu_hw_sspp_setup_rects_v13(struct dpu_sw_pipe *pipe,
		struct dpu_sw_pipe_cfg *cfg)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	struct dpu_hw_blk_reg_map *c;
	u32 src_size, src_xy, dst_size, dst_xy;
	u32 offset;

	if (!ctx || !cfg)
		return;

	c = &ctx->hw;

	offset = _sspp_calculate_rect_off(pipe->multirect_index, ctx);

	/* src and dest rect programming */
	src_xy = (cfg->src_rect.y1 << 16) | cfg->src_rect.x1;
	src_size = (drm_rect_height(&cfg->src_rect) << 16) |
		   drm_rect_width(&cfg->src_rect);
	dst_xy = (cfg->dst_rect.y1 << 16) | cfg->dst_rect.x1;
	dst_size = (drm_rect_height(&cfg->dst_rect) << 16) |
		drm_rect_width(&cfg->dst_rect);

	/* rectangle register programming */
	DPU_REG_WRITE(c, offset + SSPP_REC_SRC_SIZE, src_size);
	DPU_REG_WRITE(c, offset + SSPP_REC_SRC_XY, src_xy);
	DPU_REG_WRITE(c, offset + SSPP_REC_OUT_SIZE, dst_size);
	DPU_REG_WRITE(c, offset + SSPP_REC_OUT_XY, dst_xy);
}

void dpu_hw_sspp_setup_solidfill_v13(struct dpu_sw_pipe *pipe, u32 color)
{
	struct dpu_hw_sspp *ctx = pipe->sspp;
	struct dpu_hw_fmt_layout cfg;
	u32 offset;

	if (!ctx)
		return;

	offset = _sspp_calculate_rect_off(pipe->multirect_index, ctx);

	/* cleanup source addresses */
	memset(&cfg, 0, sizeof(cfg));
	ctx->ops.setup_sourceaddress(pipe, &cfg);


	DPU_REG_WRITE(&ctx->hw, offset + SSPP_REC_SRC_CONSTANT_COLOR, color);
}

void dpu_hw_sspp_setup_qos_lut_v13(struct dpu_hw_sspp *ctx,
				      struct dpu_hw_qos_cfg *cfg)
{
	if (!ctx || !cfg)
		return;

	_dpu_hw_setup_qos_lut_v13(&ctx->hw, 0,
			      ctx->mdss_ver->core_major_ver >= 4,
			      cfg);
}

void dpu_hw_sspp_setup_qos_ctrl_v13(struct dpu_hw_sspp *ctx,
				       bool danger_safe_en)
{
	if (!ctx)
		return;

	DPU_REG_WRITE(&ctx->hw, SSPP_CMN_QOS_CTRL,
		      danger_safe_en ? SSPP_QOS_CTRL_DANGER_SAFE_EN : 0);
}