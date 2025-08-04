// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/iopoll.h>
#include <linux/reset.h>
#include "iris_instance.h"
#include "iris_vpu_common.h"
#include "iris_vpu_register_defines.h"

#define WRAPPER_EFUSE_MONITOR			(WRAPPER_BASE_OFFS + 0x08)
#define AON_WRAPPER_MVP_NOC_RESET_SYNCRST	(AON_MVP_NOC_RESET + 0x08)
#define CPU_CS_APV_BRIDGE_SYNC_RESET		(CPU_BASE_OFFS + 0x174)
#define DISABLE_VIDEO_APV_BIT			BIT(27)
#define DISABLE_VIDEO_VPP1_BIT			BIT(28)
#define DISABLE_VIDEO_VPP0_BIT			BIT(29)
#define CORE_CLK_HALT				BIT(0)
#define APV_CLK_HALT				BIT(1)
#define CORE_PWR_ON				BIT(1)

static int iris_vpu4x_genpd_set_hwmode(struct iris_core *core, bool hw_mode)
{
	u32 value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);
	int ret;

	ret = dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN], hw_mode);
	if (ret)
		return ret;

	if (!(value & DISABLE_VIDEO_VPP0_BIT)) {
		ret = dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs
					      [IRIS_VPP0_HW_POWER_DOMAIN], hw_mode);
		if (ret)
			goto restore_hw_domain_mode;
	}

	if (!(value & DISABLE_VIDEO_VPP1_BIT)) {
		ret = dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs
					      [IRIS_VPP1_HW_POWER_DOMAIN], hw_mode);
		if (ret)
			goto restore_vpp0_domain_mode;
	}

	if (!(value & DISABLE_VIDEO_APV_BIT)) {
		ret = dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs
					      [IRIS_APV_HW_POWER_DOMAIN], hw_mode);
		if (ret)
			goto restore_vpp1_domain_mode;
	}

	return 0;

restore_vpp1_domain_mode:
	if (!(value & DISABLE_VIDEO_VPP1_BIT))
		dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs[IRIS_VPP1_HW_POWER_DOMAIN],
					!hw_mode);
restore_vpp0_domain_mode:
	if (!(value & DISABLE_VIDEO_VPP0_BIT))
		dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs[IRIS_VPP0_HW_POWER_DOMAIN],
					!hw_mode);
restore_hw_domain_mode:
	dev_pm_genpd_set_hwmode(core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN], !hw_mode);

	return ret;
}

static int iris_vpu4x_power_on_apv(struct iris_core *core)
{
	u32 value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);
	int ret;

	if (value & DISABLE_VIDEO_APV_BIT)
		return 0;

	ret = iris_enable_power_domains(core,
					core->pmdomain_tbl->pd_devs[IRIS_APV_HW_POWER_DOMAIN]);
	if (ret)
		return ret;

	ret = iris_prepare_enable_clock(core, IRIS_APV_HW_CLK);
	if (ret)
		goto disable_apv_hw_power_domain;

	return 0;

disable_apv_hw_power_domain:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_APV_HW_POWER_DOMAIN]);

	return ret;
}

static void iris_vpu4x_power_off_apv(struct iris_core *core)
{
	bool handshake_done, handshake_busy;
	u32 value, count = 0;

	value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);

	if (value & DISABLE_VIDEO_APV_BIT)
		return;

	value = readl(core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);

	if (value & APV_CLK_HALT)
		writel(0x0, core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);

	do {
		writel(REQ_POWER_DOWN_PREP, core->reg_base + AON_WRAPPER_MVP_NOC_LPI_CONTROL);
		usleep_range(10, 20);
		value = readl(core->reg_base + AON_WRAPPER_MVP_NOC_LPI_STATUS);

		handshake_done = value & NOC_LPI_STATUS_DONE;
		handshake_busy = value & (NOC_LPI_STATUS_DENY | NOC_LPI_STATUS_ACTIVE);

		if (handshake_done || !handshake_busy)
			break;

		writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_LPI_CONTROL);
		usleep_range(10, 20);

	} while (++count < 1000);

	if (!handshake_done && handshake_busy)
		dev_err(core->dev, "LPI handshake timeout\n");

	writel(0x080200, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);
	readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
			   value, value & 0x080200, 200, 2000);

	writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_SYNCRST);
	writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);
	readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
			   value, value == 0x0, 200, 2000);

	writel(CORE_BRIDGE_SW_RESET | CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base +
	       CPU_CS_APV_BRIDGE_SYNC_RESET);
	writel(CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base + CPU_CS_APV_BRIDGE_SYNC_RESET);
	writel(0x0, core->reg_base + CPU_CS_APV_BRIDGE_SYNC_RESET);

	iris_disable_unprepare_clock(core, IRIS_APV_HW_CLK);
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_APV_HW_POWER_DOMAIN]);
}

static void iris_vpu4x_ahb_sync_reset_apv(struct iris_core *core)
{
	u32 value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);

	if (value & DISABLE_VIDEO_APV_BIT)
		return;

	writel(CORE_BRIDGE_SW_RESET | CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base +
	       CPU_CS_APV_BRIDGE_SYNC_RESET);
	writel(CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base + CPU_CS_APV_BRIDGE_SYNC_RESET);
	writel(0x0, core->reg_base + CPU_CS_APV_BRIDGE_SYNC_RESET);
}

static void iris_vpu4x_ahb_sync_reset_hardware(struct iris_core *core)
{
	writel(CORE_BRIDGE_SW_RESET | CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base +
	       CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(0x0, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
}

static int iris_vpu4x_enable_hardware_clocks(struct iris_core *core)
{
	u32 value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);
	int ret;

	ret = iris_prepare_enable_clock(core, IRIS_AXI_CLK);
	if (ret)
		return ret;

	ret = iris_prepare_enable_clock(core, IRIS_HW_FREERUN_CLK);
	if (ret)
		goto disable_axi_clock;

	ret = iris_prepare_enable_clock(core, IRIS_HW_CLK);
	if (ret)
		goto disable_hw_free_run_clock;

	ret = iris_prepare_enable_clock(core, IRIS_BSE_HW_CLK);
	if (ret)
		goto disable_hw_clock;

	if (!(value & DISABLE_VIDEO_VPP0_BIT)) {
		ret = iris_prepare_enable_clock(core, IRIS_VPP0_HW_CLK);
		if (ret)
			goto disable_bse_hw_clock;
	}

	if (!(value & DISABLE_VIDEO_VPP1_BIT)) {
		ret = iris_prepare_enable_clock(core, IRIS_VPP1_HW_CLK);
		if (ret)
			goto disable_vpp0_hw_clock;
	}

	return 0;

disable_vpp0_hw_clock:
	iris_disable_unprepare_clock(core, IRIS_VPP0_HW_CLK);
disable_bse_hw_clock:
	iris_disable_unprepare_clock(core, IRIS_BSE_HW_CLK);
disable_hw_clock:
	iris_disable_unprepare_clock(core, IRIS_HW_CLK);
disable_hw_free_run_clock:
	iris_disable_unprepare_clock(core, IRIS_HW_FREERUN_CLK);
disable_axi_clock:
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);

	return ret;
}

static void iris_vpu4x_disable_hardware_clocks(struct iris_core *core)
{
	u32 value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);

	if (!(value & DISABLE_VIDEO_VPP1_BIT))
		iris_disable_unprepare_clock(core, IRIS_VPP1_HW_CLK);

	if (!(value & DISABLE_VIDEO_VPP0_BIT))
		iris_disable_unprepare_clock(core, IRIS_VPP0_HW_CLK);

	iris_disable_unprepare_clock(core, IRIS_BSE_HW_CLK);
	iris_disable_unprepare_clock(core, IRIS_HW_CLK);
	iris_disable_unprepare_clock(core, IRIS_HW_FREERUN_CLK);
	iris_disable_unprepare_clock(core, IRIS_AXI_CLK);
}

static int iris_vpu4x_power_on_hardware(struct iris_core *core)
{
	u32 value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);
	int ret;

	ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);
	if (ret)
		return ret;

	if (!(value & DISABLE_VIDEO_VPP0_BIT)) {
		ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs
						[IRIS_VPP0_HW_POWER_DOMAIN]);
		if (ret)
			goto disable_hw_power_domain;
	}

	if (!(value & DISABLE_VIDEO_VPP1_BIT)) {
		ret = iris_enable_power_domains(core, core->pmdomain_tbl->pd_devs
						[IRIS_VPP1_HW_POWER_DOMAIN]);
		if (ret)
			goto disable_vpp0_power_domain;
	}

	ret = iris_vpu4x_enable_hardware_clocks(core);
	if (ret)
		goto disable_vpp1_power_domain;

	ret = iris_vpu4x_power_on_apv(core);
	if (ret)
		goto disable_hw_clocks;

	iris_vpu4x_ahb_sync_reset_apv(core);
	iris_vpu4x_ahb_sync_reset_hardware(core);

	ret = iris_vpu4x_genpd_set_hwmode(core, true);
	if (ret)
		goto disable_apv_power_domain;

	return 0;

disable_apv_power_domain:
	iris_vpu4x_power_off_apv(core);
disable_hw_clocks:
	iris_vpu4x_disable_hardware_clocks(core);
disable_vpp1_power_domain:
	if (!(value & DISABLE_VIDEO_VPP1_BIT))
		iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs
						[IRIS_VPP1_HW_POWER_DOMAIN]);
disable_vpp0_power_domain:
	if (!(value & DISABLE_VIDEO_VPP0_BIT))
		iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs
						[IRIS_VPP0_HW_POWER_DOMAIN]);
disable_hw_power_domain:
	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);

	return ret;
}

static void iris_vpu4x_power_off_hardware(struct iris_core *core)
{
	bool handshake_done, handshake_busy;
	u32 value, count = 0;

	iris_vpu4x_genpd_set_hwmode(core, false);

	iris_vpu4x_power_off_apv(core);

	value = readl(core->reg_base + WRAPPER_CORE_POWER_STATUS);

	if (!(value & CORE_PWR_ON))
		goto disable_clocks_and_power;

	value = readl(core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);

	if (value & CORE_CLK_HALT)
		writel(0x0, core->reg_base + WRAPPER_CORE_CLOCK_CONFIG);

	readl_poll_timeout(core->reg_base + VCODEC_SS_IDLE_STATUSN, value,
			   value & 0x7103, 2000, 20000);

	do {
		writel(REQ_POWER_DOWN_PREP, core->reg_base + AON_WRAPPER_MVP_NOC_LPI_CONTROL);
		usleep_range(10, 20);
		value = readl(core->reg_base + AON_WRAPPER_MVP_NOC_LPI_STATUS);

		handshake_done = value & NOC_LPI_STATUS_DONE;
		handshake_busy = value & (NOC_LPI_STATUS_DENY | NOC_LPI_STATUS_ACTIVE);

		if (handshake_done || !handshake_busy)
			break;

		writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_LPI_CONTROL);
		usleep_range(10, 20);

	} while (++count < 1000);

	if (!handshake_done && handshake_busy)
		dev_err(core->dev, "LPI handshake timeout\n");

	writel(0x070103, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);
	readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
			   value, value == 0x070103, 200, 2000);

	writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_SYNCRST);
	writel(0x0, core->reg_base + AON_WRAPPER_MVP_NOC_RESET_REQ);
	readl_poll_timeout(core->reg_base + AON_WRAPPER_MVP_NOC_RESET_ACK,
			   value, value == 0x0, 200, 2000);

	writel(CORE_BRIDGE_SW_RESET | CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base +
	       CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(CORE_BRIDGE_HW_RESET_DISABLE, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);
	writel(0x0, core->reg_base + CPU_CS_AHB_BRIDGE_SYNC_RESET);

disable_clocks_and_power:
	iris_vpu4x_disable_hardware_clocks(core);

	value = readl(core->reg_base + WRAPPER_EFUSE_MONITOR);

	if (!(value & DISABLE_VIDEO_VPP1_BIT))
		iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs
					   [IRIS_VPP1_HW_POWER_DOMAIN]);

	if (!(value & DISABLE_VIDEO_VPP0_BIT))
		iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs
					   [IRIS_VPP0_HW_POWER_DOMAIN]);

	iris_disable_power_domains(core, core->pmdomain_tbl->pd_devs[IRIS_HW_POWER_DOMAIN]);
}

const struct vpu_ops iris_vpu4x_ops = {
	.power_off_hw = iris_vpu4x_power_off_hardware,
	.power_on_hw = iris_vpu4x_power_on_hardware,
	.power_off_controller = iris_vpu35_vpu4x_power_off_controller,
	.power_on_controller = iris_vpu35_vpu4x_power_on_controller,
	.program_bootup_registers = iris_vpu35_vpu4x_program_bootup_registers,
	.calc_freq = iris_vpu3x_vpu4x_calculate_frequency,
};
