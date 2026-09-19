/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef __IRIS_PLATFORM_SM8450_H__
#define __IRIS_PLATFORM_SM8450_H__

static struct platform_inst_caps platform_inst_cap_sm8450 = {
	.min_frame_width = 96,
	.max_frame_width = 8192,
	.min_frame_height = 96,
	.max_frame_height = 8192,
	.max_mbpf = (8192 * 4352) / 256,
	.mb_cycles_vsp = 25,
	.mb_cycles_vpp = 200,
	.mb_cycles_fw = 326389,
	.mb_cycles_fw_vpp = 44156,
	.max_frame_rate = MAXIMUM_FPS,
	.max_operating_rate = MAXIMUM_FPS,
};

#endif
