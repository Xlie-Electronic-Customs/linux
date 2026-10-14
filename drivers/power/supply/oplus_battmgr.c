// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, Nazar Kompanets <xlie7669@gmail.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/devm-helpers.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/pinctrl/consumer.h>
#include <linux/gpio.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/math.h>
#include <linux/units.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>

#include "qcom_battmgr.h"
#include "oplus_battmgr.h"

static const u8 usb_prop_map[USB_PROP_MAX];

static int oplus_battmgr_request_property(struct qcom_battmgr *battmgr, int opcode,
					 int property, u32 value)
{
	int ret;

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, opcode, property, value);
	mutex_unlock(&battmgr->lock);
	return ret;
}


static int oplus_ap_init_adsp_gague(struct qcom_battmgr *battmgr)
{
	int ret;

	ret = oplus_battmgr_request_property(battmgr, BC_BATTERY_STATUS_SET, BATT_ADSP_GAUGE_INIT, 1);
	if (ret)
		chg_err("init adsp gague fail, ret=%d\n", ret);
	else
		chg_err("init adsp gague sucess.");

	return ret;
}

static int oplus_adsp_voocphy_enable(struct qcom_battmgr *battmgr, bool enable)
{
	int ret;

	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_SET, USB_VOOCPHY_ENABLE, enable);
	if (ret)
		chg_err("set enable adsp voocphy fail, ret=%d\n", ret);
	else
		chg_err("set enable adsp voocphy success, ret=%d\n", ret);

	return ret;
}

static int smbchg_charging_disable(struct qcom_battmgr *battmgr)
{
	int ret;
	struct battery_chg_dev *bcdev = battmgr->bcdev;

	mutex_lock(&bcdev->chg_en_lock);
	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_SET, BATT_CHG_EN, 0);
	if (ret)
		chg_err("set disable charging fail, ret=%d\n", ret);
	else {
		bcdev->chg_en = false;
		chg_err("set disable charging sucess.\n");
	}
	mutex_unlock(&bcdev->chg_en_lock);

	return ret;
}

static int smbchg_charging_enable(struct qcom_battmgr *battmgr)
{
	int ret;
	struct battery_chg_dev *bcdev = battmgr->bcdev;

	mutex_lock(&bcdev->chg_en_lock);
	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_SET, BATT_CHG_EN, 1);
	if (ret)
		chg_err("set enable charging fail, ret=%d\n", ret);
	else {
		bcdev->chg_en = true;
		chg_err("set enable charging sucess.");
	}
	mutex_unlock(&bcdev->chg_en_lock);

	return ret;
}

static int smbchg_get_charge_enable(struct qcom_battmgr *battmgr)
{
	int ret;
	struct battery_chg_dev *bcdev = battmgr->bcdev;

	mutex_lock(&bcdev->chg_en_lock);
	ret = bcdev->chg_en;
	mutex_unlock(&bcdev->chg_en_lock);

	return ret;
}

static int smbchg_usb_suspend_enable(struct qcom_battmgr *battmgr)
{
	unsigned int prop;
	int ret;

	prop = usb_prop_map[POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT];
	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_SET, prop, 0);
	if (ret)
		chg_err("set suspend fail, ret=%d\n", ret);
	else
		chg_err("set chg suspend\n");

	return ret;
}

static int smbchg_usb_suspend_disable(struct qcom_battmgr *battmgr)
{
	unsigned int prop;
	int ret;

	prop = usb_prop_map[POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT];
	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_SET, prop, 0xFFFFFFFF);
	if (ret)
		chg_err("set unsuspend to fail, ret=%d\n", ret);
	else
		chg_err("set chg unsuspend\n");

	return ret;
}

static bool smbchg_usb_check_suspend_charger(struct qcom_battmgr *battmgr)
{
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	int ret;

	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, USB_SUSPEND_PMIC, 0);
	if (ret)
		chg_err("read chg suspend status fail, ret=%d\n", ret);
	else
		chg_err("read chg suspend status[%d]\n", bcdev->usb.usb_suspend_pmic);

	return bcdev->usb.usb_suspend_pmic;
}

static int oplus_otg_ap_enable(struct qcom_battmgr *battmgr, bool enable)
{
	int ret;

	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_SET, USB_OTG_AP_ENABLE, enable);
	if (ret)
		chg_err("oplus_otg_ap_enable fail, ret=%d\n", ret);
	else
		chg_err("oplus_otg_ap_enable, ret=%d\n", ret);

	// oplus_get_otg_online_status_with_cid_scheme();
	// if (bcdev->cid_status != 0) {
	// 	chg_err("Oplus_otg_ap_enable,flag bcdev->cid_status != 0\n");
	// 	oplus_ccdetect_enable();
	// }

	return ret;
}

static void oplus_otg_init_status_func(struct work_struct *work)
{
	int count = 20;
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, otg_init_work.work);
	// struct oplus_chg_chip *chip = g_oplus_chip;
 //
	// if (!bcdev || !chip) {
	// 	pr_err("bcdev is null or chip is null, return\n");
	// 	return;
	// }

	// if (bcdev->otg_boost_src == OTG_BOOST_SOURCE_EXTERNAL) {
	// 	while (count--) {
	// 		if (is_wls_ocm_available(chip))
	// 			break;
 //
	// 		msleep(500);
	// 	}
	// }

	printk(KERN_ERR "!!!!oplus_otg_init_status_func, count[%d]\n", count);
	oplus_otg_ap_enable(bcdev->battmgr, true);
}

static int oplus_adsp_voocphy_reset_again(struct qcom_battmgr *battmgr)
{
	int ret;
	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_SET, USB_VOOCPHY_RESET_AGAIN, true);
	if (ret)
		chg_err("set adsp voocphy reset again fail, ret=%d\n", ret);
	else
		chg_err("set adsp voocphy reset again success, ret=%d\n", ret);

	return ret;
}

static int opchg_get_charger_type(struct battery_chg_dev *bcdev)
{
	int ret;
	unsigned int prop;
	static int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	struct oplus_chg_chip *chip = bcdev->chip;

	if (!chip) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	}

	// if (!chip->charger_exist) {
	// 	return POWER_SUPPLY_TYPE_UNKNOWN;
	// }

	if (!chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	}

	prop = usb_prop_map[POWER_SUPPLY_PROP_USB_TYPE];
	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, prop, 0);
	if (ret) {
		chg_err("read usb charger_type fail, ret=%d\n", ret);
		goto get_type_done;
	}
	switch (battmgr->usb.usb_type) {
	case POWER_SUPPLY_USB_TYPE_UNKNOWN:
		charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		break;
	case POWER_SUPPLY_USB_TYPE_SDP:
		charger_type = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		charger_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case POWER_SUPPLY_USB_TYPE_PD_SDP:
		charger_type = POWER_SUPPLY_TYPE_USB_PD_SDP;
		break;
	default:
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	}

get_type_done:
	if (bcdev->pd_svooc == true) {
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
	}

	// if (chip && chip->wireless_support &&
	// 		(oplus_wpc_get_wireless_charge_start() == true || oplus_chg_is_wls_present()))
	// 	charger_type = POWER_SUPPLY_TYPE_WIRELESS;
	return charger_type;
}

static int oplus_chg_get_charger_subtype(struct battery_chg_dev *bcdev)
{
	int ret;
	unsigned int prop;
	static int charg_subtype = CHARGER_SUBTYPE_DEFAULT;
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	struct oplus_chg_chip *chip = bcdev->chip;

	if (!chip) {
		return CHARGER_SUBTYPE_DEFAULT;
	}

	// if (!chip->charger_exist)
	// 	return CHARGER_SUBTYPE_DEFAULT;

	if (!chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
		return CHARGER_SUBTYPE_DEFAULT;

	// charg_subtype = oplus_ufcs_get_fastchg_type();
	// if (charg_subtype != CHARGER_SUBTYPE_DEFAULT)
	// 	return CHARGER_SUBTYPE_UFCS;


	prop = usb_prop_map[POWER_SUPPLY_PROP_USB_TYPE];
	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, prop, 0);
	if (ret) {
		chg_err("read charger type fail, ret=%d\n", ret);
		if (!chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
			charg_subtype = CHARGER_SUBTYPE_DEFAULT;
		return charg_subtype;
	}
	switch (battmgr->usb.usb_type) {
		case POWER_SUPPLY_USB_TYPE_PD:
		case POWER_SUPPLY_USB_TYPE_PD_DRP:
			charg_subtype = CHARGER_SUBTYPE_PD;
			break;
		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			charg_subtype = CHARGER_SUBTYPE_PD;
			// if (oplus_pps_get_chg_status() != PPS_NOT_SUPPORT
			// 		&& oplus_get_pps_type() == true
			// 		&& (chip->pd_svooc || oplus_pps_check_third_pps_support()))
			// 	charg_subtype = CHARGER_SUBTYPE_PPS;
			break;
		default:
			charg_subtype = CHARGER_SUBTYPE_DEFAULT;
			break;
	}

	if (charg_subtype == CHARGER_SUBTYPE_DEFAULT) {
		ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, USB_ADAP_SUBTYPE, 0);
		if (ret) {
			chg_err("read charger subtype fail, ret=%d\n", ret);
			// if (!chip->charger_exist)
			if (!chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
				charg_subtype = CHARGER_SUBTYPE_DEFAULT;
			return charg_subtype;
		}
		switch (bcdev->usb.usb_subtype) {
			case CHARGER_SUBTYPE_FASTCHG_VOOC:
				charg_subtype = CHARGER_SUBTYPE_FASTCHG_VOOC;
				break;
			case CHARGER_SUBTYPE_FASTCHG_SVOOC:
				charg_subtype = CHARGER_SUBTYPE_FASTCHG_SVOOC;
				break;
			case CHARGER_SUBTYPE_QC:
				charg_subtype = CHARGER_SUBTYPE_QC;
				break;
			default:
				charg_subtype = CHARGER_SUBTYPE_DEFAULT;
				break;
		}
	}

	return charg_subtype;
}

static int oplus_sm8150_get_pd_type(struct battery_chg_dev *bcdev)
{
	int ret;
	unsigned int prop;
	static int is_pd_type = 0;
	struct qcom_battmgr *battmgr = bcdev->battmgr;

	prop = usb_prop_map[POWER_SUPPLY_PROP_USB_TYPE];
	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, prop, 0);
	if (ret) {
		chg_err("read usb pd_type fail, ret=%d\n", ret);
		// if (!chip->charger_exist)
		if (!bcdev->chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
			is_pd_type = 0;
		return is_pd_type;
	}
	chg_err("oplus_sm8150_get_pd_type, pst->prop[prop_id]=%d\n", battmgr->usb.usb_type);
	switch (battmgr->usb.usb_type) {
		case POWER_SUPPLY_USB_TYPE_PD:
		case POWER_SUPPLY_USB_TYPE_PD_DRP:
			is_pd_type = PD_ACTIVE;
			break;
		case POWER_SUPPLY_USB_TYPE_PD_PPS:
			// if (oplus_pps_get_chg_status() != PPS_NOT_SUPPORT
			// 		&& oplus_get_pps_type() == true) {
			// 	is_pd_type = PD_PPS_ACTIVE;
			// } else {
				is_pd_type = PD_ACTIVE;
			// }
			break;
		default:
			is_pd_type = 0;
			break;
	}

	return is_pd_type;
}

static void oplus_chg_status_send_adsp_work(struct work_struct *work)
{
	int ret;
	struct battery_chg_dev *bcdev = container_of(work,
						     struct battery_chg_dev, chg_status_send_work.work);
	bool chg_enable = false, chg_suspend = true, chg_status = false;
	struct qcom_battmgr *battmgr = bcdev->battmgr;

	chg_enable = smbchg_get_charge_enable(battmgr);
	chg_suspend = smbchg_usb_check_suspend_charger(battmgr);
	if (!bcdev->chip->adsp_notify_ap_suspend && chg_enable && !chg_suspend)
		chg_status = true;

	ret = oplus_battmgr_request_property(battmgr, BC_BATTERY_STATUS_SET, BATT_SEND_CHG_STATUS, chg_status);
	if (ret)
		chg_err("send chg status fail, ret=%d\n", ret);
	else
		chg_err("send chg status success, chg_status=%d\n", chg_status);
}

static bool oplus_chg_is_usb_present(struct battery_chg_dev *bcdev)
{
	bool vbus_rising = false;
	struct oplus_chg_chip *chip = bcdev->chip;

	if (!chip) {
		return false;
	}

	vbus_rising = bcdev->usb_in_status;

	// if (vbus_rising == false && (oplus_wpc_get_wireless_charge_start() || oplus_chg_is_wls_present())) {
	// 	chg_err("USBIN_PLUGIN_RT_STS_BIT low but wpc has started\n");
	// 	vbus_rising = true;
	// }

	// if ((oplus_chg_get_voocphy_support() != ADSP_VOOCPHY) &&
	//     vbus_rising == false && oplus_vooc_get_fastchg_started() == true) {
	// 	chg_err("USBIN_PLUGIN_RT_STS_BIT low but fastchg started true and chg vol > 2V\n");
	// 	vbus_rising = true;
	// }

	// if (chip->support_check_usbin_status)
	// 	schedule_delayed_work(&bcdev->check_abnormal_usbin_status_work, 0);

	return vbus_rising;
}

static bool oplus_chg_wls_is_present(struct battery_chg_dev *bcdev)
{
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	int ret;
	int usb_sns_status = 0;

	if (bcdev->soccp_support)
		ret = oplus_battmgr_request_property(battmgr, OPLUS_OPCODE_GET_REQ, OPLUS_USB_SNS_STATUS, 0);
	else
		ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, USB_SNS_STATUS, 0);

	// if (bcdev->soccp_support)
	// 	usb_sns_status = bcdev->oplus_psy.prop[OPLUS_USB_SNS_STATUS];
	// else
		usb_sns_status = bcdev->usb.usb_sns_status;

	if (usb_sns_status == 0)
		return true;
	else
		return false;
}



void oplus_chg_variables_reset(struct oplus_chg_chip *chip, bool in)
{
	int i;
	// oplus_ufcs_variables_reset(in);
	if (in) {
		schedule_delayed_work(&chip->check_pd_svooc_work, msecs_to_jiffies(PD_SVOOC_CHECK_TIME));
		chip->charger_exist = true;
		chip->chging_on = true;
		chip->slave_charger_enable = false;
		chip->read_by_reg = 0;

		if (chip->hmac == false && chip->external_gauge) {
			chip->hmac = oplus_gauge_get_batt_hmac();
			if (oplus_switching_support_parallel_chg() &&
			    chip->balancing_bat_status == PARALLEL_BAT_BALANCE_ERROR_STATUS8)
				chip->hmac = false;
		}

		if (!oplus_chg_show_vooc_logo_ornot() && chip->pre_charger_exist == false) {
			chip->quick_mode_time = current_kernel_time();
			chip->start_time = chip->quick_mode_time.tv_sec;
			chip->quick_mode_gain_time_ms = 0;
			chip->quick_mode_start_time = 0;
			chip->quick_mode_stop_time = 0;
			chip->quick_mode_start_cap = -1;
			chip->quick_mode_stop_cap = -1;
			chip->quick_mode_stop_temp = -1;
			chip->quick_mode_stop_soc = -1;
		}
		if (chip->balancing_bat_status != PARALLEL_BAT_BALANCE_ERROR_STATUS8 &&
		    chip->balancing_bat_status != PARALLEL_BAT_BALANCE_ERROR_STATUS9) {
			chip->balancing_bat_stop_chg = 0;
			chip->balancing_bat_status = -1;
			chip->balancing_bat_stop_fastchg = 0;
		}
		oplus_first_enable_adsp_voocphy();
		oplus_comm_fginfo_reset(chip);
	} else {
		chip->check_pd_svooc_complete = false;
		cancel_delayed_work(&chip->check_pd_svooc_work);
		// if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY)
			oplus_adsp_voocphy_clear_status();

		if (!oplus_chg_show_vooc_logo_ornot()) {
			if (chip->decimal_control) {
				cancel_delayed_work_sync(&g_charger_chip->ui_soc_decimal_work);
				chip->last_decimal_ui_soc = (chip->ui_soc_integer + chip->ui_soc_decimal);
				oplus_chg_ui_soc_decimal_deinit();
				pr_err("[oplus_chg_variables_reset]cancel last_decimal_ui_soc:%d",
				       chip->last_decimal_ui_soc);
			}
			chip->calculate_decimal_time = 0;
			chip->bms_heat_temp_compensation = 0;
			mutex_lock(&chip->slow_chg_mutex);
			chip->slow_chg_pct = 0;
			chip->slow_chg_watt = 0;
			chip->slow_chg_enable = false;
			chip->slow_chg_batt_limit = 0;
			mutex_unlock(&chip->slow_chg_mutex);
		}
		if ((chip->mmi_fastchg == 1) && (chip->chg_cycle_status & CHG_CYCLE_VOTER__USER)) {
			chip->chg_cycle_status &= ~(int)CHG_CYCLE_VOTER__USER;
			if (!chip->chg_cycle_status) {
				chip->stop_chg = 1;
				// if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY)
					oplus_adsp_voocphy_turn_on();
			}
		}
		chip->unwakelock_chg = 0;
		chip->allow_swtich_to_fastchg = 1;
		chip->charger_exist = false;
		chip->chging_on = false;
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		chip->charger_subtype = CHARGER_SUBTYPE_DEFAULT;
		chip->real_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		chip->usbtemp_batt_current = 0;
		chip->usbtemp_pre_batt_current = 0;
		chip->usbtemp_change_across_unplug = true;
		vbatt_higherthan_4180mv = false;
		mcu_update = false;
		suspend_charger = false;
		chip->pd_svooc = false;
		chip->notify_flag = 0;
		chip->fg_soft_reset_done = true;
		if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		    oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
			oplus_voocphy_clear_variables();
		}
	}
	// if (chip->plc_support && chip->mmi_fastchg) {
	// 	if (chip->charger_exist) {
	// 		chip->curr_plc_status = PLC_STATUS_DISABLE;
	// 		chip->plc_status = PLC_STATUS_DISABLE;
	// 	} else {
	// 		chip->curr_plc_status = PLC_STATUS_NOT_ALLOW;
	// 		chip->plc_status = PLC_STATUS_NOT_ALLOW;
	// 	}
	// }
	chip->pre_charger_exist = chip->charger_exist;
	chip->qc_abnormal_check_count = 0;
	chip->limits.vbatt_pdqc_to_9v_thr = oplus_get_vbatt_pdqc_to_9v_thr();
	chip->sw_full_count = 0;
	chip->sw_full = false;
	chip->full_data.vbat_counts_hw = 0;
	chip->full_data.sub_vbat_counts_hw = 0;
	chip->full_data.hw_full = false;
	chip->full_data.sub_hw_full = false;
	chip->full_data.clear_full_check_count = 0;
	chip->hw_full_by_sw = false;
	chip->sw_sub_batt_full_count = 0;
	chip->sw_sub_batt_full = false;
	chip->hw_sub_batt_full_by_sw = false;
	chip->vchg_status = CHARGER_STATUS__GOOD;
	chip->batt_full = false;
	chip->real_batt_full = false;
	chip->tbatt_when_full = 200;
	chip->recharge_after_ffc = false;
	chip->tbatt_status = BATTERY_STATUS__NORMAL;
	chip->tbatt_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->tbatt_normal_status = BATTERY_STATUS__NORMAL_PHASE1;
	chip->tbatt_normal_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->tbatt_cold_status = BATTERY_STATUS__COLD_PHASE1;
	chip->tbatt_cold_pre_shake = TBATT_PRE_SHAKE_INVALID;
	chip->low_temp_check_jiffies = jiffies;
	chip->vbatt_over = 0;
	chip->total_time = 0;
	chip->chging_over_time = 0;
	chip->in_rechging = 0;
	chip->adsp_notify_ap_suspend = 0;
	chip->stop_voter = 0x00;
	chip->charging_state = CHARGING_STATUS_CCCV;
	chip->pre_chg_up_limit_mmi_val = 0;
	chip->usb_aicl_enable_flag = false;
	if (chip->mmi_fastchg == 0) {
		chip->mmi_chg = 0;
		// if (oplus_chg_get_voocphy_support() == ADSP_VOOCPHY)
			oplus_adsp_voocphy_turn_off();
	} else {
		if (!in)
			chip->mmi_chg = 1;
	}
	chip->unwakelock_chg = 0;
	chip->notify_code = 0;
	chip->check_time_sec = 0;
	chip->fastchg_check_first_time = true;
	if (!((chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) || (oplus_vooc_get_fastchg_started() == true) ||
	      (oplus_vooc_get_fastchg_to_warm() == true) || (oplus_vooc_get_fastchg_dummy_started() == true) ||
	      (oplus_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) ||
	      (oplus_vooc_get_btb_temp_over() == true) || oplus_quirks_keep_connect_status())) {
		chip->cool_down = 0;
		chip->cool_down_done = false;
	}
	chip->cool_down_force_5v = false;
	chip->limits.cold_bat_decidegc = chip->anti_shake_bound.cold_bound;
	chip->limits.freeze_bat_decidegc = chip->anti_shake_bound.freeze_bound;
	chip->limits.little_cold_bat_decidegc = chip->anti_shake_bound.little_cold_bound;
	chip->limits.cool_bat_decidegc = chip->anti_shake_bound.cool_bound;
	chip->limits.little_cool_bat_decidegc = chip->anti_shake_bound.little_cool_bound;
	chip->limits.normal_bat_decidegc = chip->anti_shake_bound.normal_bound;
	chip->limits.warm_bat_decidegc = chip->anti_shake_bound.warm_bound;
	chip->limits.hot_bat_decidegc = chip->anti_shake_bound.hot_bound;

	chip->limits.normal_phase1_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase1_bound;
	chip->limits.normal_phase2_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase2_bound;
	chip->limits.normal_phase3_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase3_bound;
	chip->limits.normal_phase4_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase4_bound;
	chip->limits.normal_phase5_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase5_bound;
	chip->limits.normal_phase6_bat_decidegc = chip->tbatt_normal_anti_shake_bound.phase6_bound;

	chip->limits.vfloat_over_counts = 0;
	chip->limits.led_warm_bat_decidegc_antishake = chip->limits.led_warm_bat_decidegc;
	chip->limits.led_high_bat_decidegc_antishake = chip->limits.led_high_bat_decidegc;
	chip->led_temp_change = false;
	chip->limits.vooc_warm_bat_decidegc_antishake = chip->limits.vooc_warm_bat_decidegc;
	chip->limits.vooc_normal_bat_decidegc_antishake = chip->limits.vooc_normal_bat_decidegc;
	chip->vooc_temp_change = false;
	if (chip->temperature > chip->limits.led_high_bat_decidegc) {
		chip->led_temp_status = LED_TEMP_STATUS__HIGH;
	} else if (chip->temperature > chip->limits.led_warm_bat_decidegc) {
		chip->led_temp_status = LED_TEMP_STATUS__WARM;
	} else {
		chip->led_temp_status = LED_TEMP_STATUS__NORMAL;
	}
	chip->dod0_counts = 0;
	chip->fastchg_to_ffc = false;
	chip->fastchg_ffc_status = 0;
	chip->chg_ctrl_by_lcd = chip->chg_ctrl_by_lcd_default;
	chip->chg_ctrl_by_vooc = chip->chg_ctrl_by_vooc_default;
	chip->ffc_temp_status = FFC_TEMP_STATUS__NORMAL;
	chip->vooc_temp_status = VOOC_TEMP_STATUS__NORMAL;
	chip->limits.iterm_ma = chip->limits.default_iterm_ma;
	chip->limits.sub_iterm_ma = chip->limits.default_sub_iterm_ma;
	chip->limits.temp_normal_fastchg_current_ma = chip->limits.default_temp_normal_fastchg_current_ma;
	chip->limits.normal_vfloat_sw_limit = chip->limits.default_normal_vfloat_sw_limit;
	chip->limits.temp_normal_vfloat_mv = chip->limits.default_temp_normal_vfloat_mv;
	chip->limits.normal_vfloat_over_sw_limit = chip->limits.default_normal_vfloat_over_sw_limit;
	chip->limits.temp_little_cool_fastchg_current_ma = chip->limits.default_temp_little_cool_fastchg_current_ma;
	chip->limits.little_cool_vfloat_sw_limit = chip->limits.default_little_cool_vfloat_sw_limit;
	chip->limits.temp_little_cool_vfloat_mv = chip->limits.default_temp_little_cool_vfloat_mv;
	chip->limits.little_cool_vfloat_over_sw_limit = chip->limits.default_little_cool_vfloat_over_sw_limit;
	chip->limits.temp_little_cool_fastchg_current_ma_high =
		chip->limits.default_temp_little_cool_fastchg_current_ma_high;
	chip->limits.temp_little_cool_fastchg_current_ma_low =
		chip->limits.default_temp_little_cool_fastchg_current_ma_low;
	chip->limits.temp_little_cold_fastchg_current_ma_high =
		chip->limits.default_temp_little_cold_fastchg_current_ma_high;
	chip->limits.temp_little_cold_fastchg_current_ma_low =
		chip->limits.default_temp_little_cold_fastchg_current_ma_low;
	chip->limits.temp_cold_fastchg_current_ma_high = chip->limits.default_temp_cold_fastchg_current_ma_high;
	chip->limits.temp_cold_fastchg_current_ma_low = chip->limits.default_temp_cold_fastchg_current_ma_low;
	chip->limits.temp_cool_fastchg_current_ma_high = chip->limits.default_temp_cool_fastchg_current_ma_high;
	chip->limits.temp_cool_fastchg_current_ma_low = chip->limits.default_temp_cool_fastchg_current_ma_low;
	chip->limits.temp_warm_fastchg_current_ma = chip->limits.default_temp_warm_fastchg_current_ma;
	chip->limits.input_current_charger_ma = chip->limits.default_input_current_charger_ma;
	chip->limits.input_current_vooc_ma_high = chip->limits.default_input_current_vooc_ma_high;
	chip->limits.input_current_vooc_ma_warm = chip->limits.default_input_current_vooc_ma_warm;
	chip->limits.input_current_vooc_ma_normal = chip->limits.default_input_current_vooc_ma_normal;
	chip->limits.pd_input_current_charger_ma = chip->limits.default_pd_input_current_charger_ma;
	chip->limits.qc_input_current_charger_ma = chip->limits.default_qc_input_current_charger_ma;
	oplus_chg_aging_ffc_variable_reset(chip);
	reset_mcu_delay = 0;
	efttest_count = 1;
	high_vol_light = 0;
	chip->pmic_spmi.aicl_suspend = false;
	oplus_chg_battery_authenticate_check(chip);
	chip->chargerid_volt = 0;
	chip->chargerid_volt_got = false;
	chip->short_c_batt.in_idle = true; /* defualt in idle for userspace */
	chip->short_c_batt.cv_satus = false; /* defualt not in cv chg */
	chip->short_c_batt.disable_rechg = false;
	chip->short_c_batt.limit_chg = false;
	chip->short_c_batt.limit_rechg = false;
	chip->chg_ctrl_by_cool_down = false;
	chip->smart_charge_user = SMART_CHARGE_USER_OTHER;
	chip->usbtemp_cool_down = 0;
	chip->pd_chging = false;
	chip->pps_to_pd_chging = false;
	chip->pd_wait_svid = true;
	chip->vooc_start_fail = false;
	for (i = 0; i < ARRAY_SIZE(chip->ibat_save); i++)
		chip->ibat_save[i] = 500;
	chip->pd_wait_svid = true;
	chg_ctrl_by_sale_mode = false;
	chip->pd_adapter_support_9v = true;
	chip->vooc_start_fail = false;
	chip->cool_down_check_done = false;
	chip->pd_authentication = -ENODATA;
	chip->pps_force_svooc = false;
	chip->usbtemp_curr_status = 0;
	oplus_pps_variables_reset(in);
	chip->bcc_cool_down = 0;
	chip->bcc_curr_done = BCC_CURR_DONE_UNKNOW;
	chip->limits.force_input_current_ma = 0;
	chip->batt_target_curr = 0;
	chip->pre_charging_current = 0;
	chip->aicl_done = false;
	if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL) {
		oplus_chg_parellel_variables_reset();
		oplus_init_parallel_temp_threshold();
	}
	chip->parallel_error_flag &= ~(REASON_SOC_NOT_FULL);
	chip->soc_not_full_report = false;
	chip->voocphy_notify_ap_suspend = false;
	chg_err("charger detect is %d\n", in);
}



int oplus_chg_set_pd_config(struct battery_chg_dev *bcdev)
{
	int ret;
	struct oplus_chg_chip *chip = bcdev->chip;
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	// int vbatt_pdqc_to_9v_thr_dt = oplus_get_vbatt_pdqc_to_9v_thr();

	if (!chip) {
		chg_err("chip is NULL!\n");
		return -1;
	}
	// bcdev = chip->pmic_spmi.bcdev_chip;
	// pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	// if (!is_ext_chg_ops() && (vbatt_pdqc_to_9v_thr_dt > 4100)) {
	// 	chip->limits.vbatt_pdqc_to_9v_thr = vbatt_pdqc_to_9v_thr_dt;
	// }

	// if (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr) {
	if (chip->batt_volt < 5000) {
		// if (oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
		//     oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) {
		// 	oplus_voocphy_set_pdqc_config();
		// }
		// if(is_ext_chg_ops())
		smbchg_usb_suspend_enable(battmgr);
		// oplus_chg_config_charger_vsys_threshold(0x02);/*set Vsys Skip threshold 104%*/
		chip->pd_volt = PDO_9V;
		// rc = write_property_id(bcdev, pst, BATT_SET_PDO, chip->pd_volt);
		return oplus_battmgr_request_property(battmgr, BC_BATTERY_STATUS_SET, BATT_SET_PDO, chip->pd_volt);
		if (ret)
			chg_err("set PDO 9V fail, ret=%d\n", ret);
		else
			chg_err("set PDO 9V OK\n");
		msleep(300);

		// if (chip->pdqc_9v_voltage_adaptive && is_ext_chg_ops())
		// 	oplus_chg_pdqc9v_vindpm_vol_switch(OPLUS_PDQC_5VTO9V);

		// if(is_ext_chg_ops())
		smbchg_usb_suspend_disable(battmgr);
	}

	return ret;
}




#define UNKNOW_TYPE_DETECT_RETRY_TIME 2
void oplus_charger_detect_check(struct battery_chg_dev *bcdev)
{
	struct voocphy_manager *vooc = bcdev->vooc_chip;
	static bool charger_resumed = true;
	static int unknow_type_check = 0;

	if (oplus_chg_is_usb_present(bcdev)) {
		// oplus_chg_set_awake(chip, true);
		// if (chip->wireless_support && (oplus_wpc_get_fw_updating() || oplus_chg_is_wls_fw_upgrading())) {
		// 	return;
		// }

		// if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL)
		// 	parallel_chg_check_balance_bat_status();

		if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
			// noplug_temperature = oplus_get_report_batt_temp();
			// noplug_batt_volt_max = chip->batt_volt_max;
			// noplug_batt_volt_min = chip->batt_volt_min;
			oplus_chg_variables_reset(chip, true);
			oplus_charger_dec_cv_check(chip);

			chip->charger_type = opchg_get_charger_type(bcdev);

			charger_xlog_printk(CHG_LOG_CRTI, "Charger in 1 charger_type=%d, unknow_type_check[%d]\n",
					    chip->charger_type, unknow_type_check);
			if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
				unknow_type_check++;
			else
				unknow_type_check = 0;

			if (unknow_type_check >= UNKNOW_TYPE_DETECT_RETRY_TIME && (vooc->fastchg_dummy_started == true ||
			     vooc->fastchg_to_normal == true || vooc->fastchg_to_warm == true || vooc->btb_temp_over == true))
				chip->charger_type = POWER_SUPPLY_TYPE_USB_DCP;

			if (vooc->fastchg_dummy_started == true &&
				  vooc->reset_adapter  == true) {
				charger_xlog_printk(CHG_LOG_CRTI, "dummy_start is true,don't turn on charge here\n");
				oplus_chg_turn_on_charging(chip);
				oplus_chg_switch(chip);
			} else if (oplus_wpc_get_wireless_charge_start() == false && !oplus_chg_is_wls_present()) {
				charger_xlog_printk(CHG_LOG_CRTI, "oplus_wpc_get_wireless_charge_start == false\n");
				charger_resumed = true;
				oplus_chg_turn_on_charging(chip);
				oplus_chg_switch(chip);
			} else
				oplus_chg_switch(chip);
			// if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL &&
			//     oplus_support_normal_batt_spec_check()) {
			// 	chip->batt_target_curr = 0;
			// 	chip->pre_charging_current = 0;
			// 	oplus_chg_parellel_variables_reset();
			// 	schedule_delayed_work(&chip->parallel_batt_chg_check_work,
			// 			      OPLUS_CHG_UPDATE_INTERVAL(PARALLEL_CHECK_TIME));
			// }
		} else {
			if (vooc->fastchg_started == false && charger_resumed == false) {
				charger_resumed = true;
				// if (oplus_vooc_get_reply_bits() == 7) {
				// 	oplus_chg_switch(chip);
				// 	oplus_chg_turn_on_charging(chip);
				// } else {
					oplus_chg_turn_on_charging(chip);
					oplus_chg_switch(chip);
				// }

			} else
				oplus_chg_switch(chip);
		}
		
	} else {
		// if (oplus_switching_support_parallel_chg() == PARALLEL_MOS_CTRL) {
		// 	parallel_chg_check_balance_bat_status();
		// 	if (oplus_support_normal_batt_spec_check()) {
		// 		chip->batt_target_curr = 0;
		// 		chip->pre_charging_current = 0;
		// 		oplus_chg_parellel_variables_reset();
		// 		cancel_delayed_work_sync(&chip->parallel_batt_chg_check_work);
		// 	}
		// }

		unknow_type_check = 0;
		oplus_chg_variables_reset(chip, false);
		if (!chip->mmi_fastchg)
			oplus_mmi_fastchg_in(chip);


		if (chip->chg_ops->get_charging_enable() == true)
			oplus_chg_turn_off_charging(chip);

		// oplus_chg_set_awake(chip, false);

	}
}

static void oplus_chg_update_work(struct battery_chg_dev *bcdev)
{
	struct oplus_chg_chip *chip = bcdev->chip;

	// oplus_charger_detect_check(bcdev);
	// oplus_chg_get_battery_data(chip);
	// oplus_check_battery_vol_diff(chip);
	// if (chip->charger_exist) {
	if (chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
		// oplus_chg_aicl_check(chip);
		// oplus_chg_protection_check(chip);
		// oplus_chg_check_tbatt_normal_status(chip);
		// oplus_chg_check_status_full(chip);
		// oplus_chg_battery_notify_check(chip);
		// oplus_comm_check_fgreset(chip);
		// oplus_chg_pd_config(bcdev);
	} else {
		// oplus_gauge_bqfs_data_check();
		// aicl_delay_count = 0;
	}
	// oplus_chg_dual_charger_config(chip);
	// oplus_chg_qc_config(chip);
	// oplus_chg_pdqc_to_normal(chip);
	// oplus_chg_ibatt_check_and_set(chip);
	// if (chip->shortc_thread)
	// 	wake_up_process(chip->shortc_thread);
	// oplus_chg_battery_update_status(chip);
	// oplus_chg_kpoc_power_off_check(chip);
	// oplus_chg_cool_down_match_err_check(chip);
	// oplus_chg_gauge_update_check(chip, false);
	// oplus_chg_other_thing(chip);
	// /* run again after interval */
	// if (timer_pending(&chip->update_work.timer) && !delayed_work_pending(&chip->update_work)) {
	// 	mod_delayed_work(system_wq, &chip->update_work,
	// 			 OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	// } else {
	// 	schedule_delayed_work(&chip->update_work, OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	// }
}

static void oplus_plugin_irq_work(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work, struct battery_chg_dev, plugin_irq_work.work);
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	static bool usb_pre_plugin_status = false, usb_plugin_status = false;
	int ret;

	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, USB_IN_STATUS, 0);
	if (ret) {
		bcdev->usb_in_status = false;
		chg_err("read usb_in_status fail, ret=%d\n", ret);
		return;
	}
	if (bcdev->usb.usb_in_status > 0) {
		bcdev->usb_in_status = 1;
	} else {
		bcdev->usb_in_status = 0;
		bcdev->pd_svooc = false;
	}
	usb_plugin_status = bcdev->usb.usb_in_status & 0xff;

	// oplus_adsp_voocphy_set_fastchg_start(false);
	chg_err("!!!prop[%d], usb_online[%d]\n", bcdev->usb.usb_in_status, bcdev->usb_in_status);
	// oplus_quirks_notify_plugin(bcdev->usb_in_status);
	// oplus_chg_track_check_wired_charging_break(usb_plugin_status);

	bcdev->real_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;
	if (bcdev && bcdev->ctrl_lcm_frequency.work.func) {
		mod_delayed_work(system_highpri_wq, &bcdev->ctrl_lcm_frequency, 50);
	}
	// if (bcdev->usb_ocm) {
	// 	if (bcdev->usb_in_status == 1) {
	// 		if (g_oplus_chip && g_oplus_chip->charger_type == POWER_SUPPLY_TYPE_WIRELESS)
	// 			g_oplus_chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	// 		oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_ONLINE);
	// 	} else {
	// 		if ((oplus_get_wired_chg_present() == false)
 //                               && (g_oplus_chip->charger_volt < 3500)) {
 //                                       bcdev->pd_svooc = false; //remove svooc flag
	// 		};
	// 		oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_OFFLINE);
	// 	}
	// }

	chg_err("!!!usb_pre_plugin_status[%d], usb_plugin_status[%d]\n", usb_pre_plugin_status, usb_plugin_status);
	if (usb_pre_plugin_status != usb_plugin_status || !usb_plugin_status) {
		// if (usb_plugin_status)
		// 	oplus_chg_set_charger_type_unknown();
		oplus_chg_update_work(bcdev);
	}
	usb_pre_plugin_status = usb_plugin_status;

	// if (bcdev->usb_in_status  == 1) {
	// 	if (bcdev->usbtemp_wq_init_finished) {
	// 		bcdev->usbtemp_check = true;
	// 		oplus_wake_up_usbtemp_thread();
	// 	}
	// } else {
	// 	bcdev->usbtemp_check = false;
	// 	if (oplus_chg_get_voocphy_support() != ADSP_VOOCPHY) {
	// 		if (oplus_vooc_get_fastchg_started() == true &&
	// 		    oplus_vooc_get_fastchg_dummy_started() == false &&
	// 		    oplus_vooc_get_fastchg_to_normal() == false &&
	// 		    oplus_vooc_get_fastchg_to_warm() == false) {      /*plug out by normal*/
	// 			printk(KERN_ERR "[%s]: plug out normal\n", __func__);
	// 			smbchg_set_chargerid_switch_val(0);
	// 			bcdev->chargerid_volt = 0;
	// 			bcdev->chargerid_volt_got = false;
	// 			bcdev->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	// 			oplus_chg_update_work();
	// 		} else if (oplus_vooc_get_fastchg_started() == false) {
	// 			printk(KERN_ERR "[%s]: plug out fastchg_to_normal/warm/dummy or not vooc\n", __func__);
	// 			if (oplus_switching_support_parallel_chg()) {
	// 				cancel_delayed_work_sync(&bcdev->update_work);
	// 			}
	// 			oplus_vooc_reset_fastchg_after_usbout();
	// 			smbchg_set_chargerid_switch_val(0);
	// 			bcdev->chargerid_volt = 0;
	// 			bcdev->chargerid_volt_got = false;
	// 			bcdev->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	// 			oplus_chg_update_work();
	// 		}
	// 	}
	// }

	bcdev->pd_svooc = false;
	bcdev->hvdcp_detach_time = cpu_clock(smp_processor_id()) / CPU_CLOCK_TIME_MS;
	printk(KERN_ERR "!!! %s: the hvdcp_detach_time:%llu, detect time %llu \n", __func__, bcdev->hvdcp_detach_time, bcdev->hvdcp_detect_time);
	if (bcdev->hvdcp_detach_time - bcdev->hvdcp_detect_time <= OPLUS_HVDCP_DETECT_TO_DETACH_TIME) {
		bcdev->hvdcp_disable = true;
		// schedule_delayed_work(&bcdev->hvdcp_disable_work, OPLUS_HVDCP_DISABLE_INTERVAL);
	} else {
		bcdev->hvdcp_detect_ok = false;
		bcdev->hvdcp_detect_time = 0;
		bcdev->hvdcp_disable = false;
	}
	bcdev->adsp_voocphy_err_check = false;
	bcdev->pd_type_checked = false;
	// cancel_delayed_work_sync(&bcdev->pd_type_check_work);
	cancel_delayed_work_sync(&bcdev->adsp_voocphy_err_work);

	printk(KERN_ERR "!!!pd_svooc[%d]\n", bcdev->pd_svooc);
}

static void oplus_chg_check_pd_svooc_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_chg_chip *chip = container_of(dwork, struct oplus_chg_chip, check_pd_svooc_work);

	if (!chip) {
		chg_err("chip is NULL\n");
		return;
	}

	chip->check_pd_svooc_complete = true;
}

static void oplus_set_otg_boost_en_val(struct battery_chg_dev *bcdev, int value)
{

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_gpio)) {
		chg_err("otg_boost_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_active)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_boost_en_sleep)) {
		chg_err("otg_boost_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpiod_direction_output(bcdev->oplus_custom_gpio.otg_boost_en_gpio , 1);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_boost_en_active);
	} else {
		gpiod_direction_output(bcdev->oplus_custom_gpio.otg_boost_en_gpio, 0);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_boost_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_boost_en_sleep);
	}

	// chg_err("<~OTG~>set value:%d, gpio_val:%d\n", value,
	// 	gpio_get_value(bcdev->oplus_custom_gpio.otg_boost_en_gpio));
}

static void oplus_set_otg_ovp_en_val(struct battery_chg_dev *bcdev, int value)
{


	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_gpio)) {
		chg_err("otg_ovp_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_active)
		|| IS_ERR_OR_NULL(bcdev->oplus_custom_gpio.otg_ovp_en_sleep)) {
		chg_err("otg_ovp_en pinctrl null, return\n");
		return;
	}

	if (value) {
		gpiod_direction_output(bcdev->oplus_custom_gpio.otg_ovp_en_gpio , 1);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_ovp_en_active);
	} else {
		gpiod_direction_output(bcdev->oplus_custom_gpio.otg_ovp_en_gpio, 0);
		pinctrl_select_state(bcdev->oplus_custom_gpio.otg_ovp_en_pinctrl,
				bcdev->oplus_custom_gpio.otg_ovp_en_sleep);
	}

	// chg_err("<~OTG~>set value:%d, gpio_val:%d\n", value,
	// 	gpio_get_value(bcdev->oplus_custom_gpio.otg_ovp_en_gpio));
}

static void otg_notification_handler(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work, struct battery_chg_dev, otg_vbus_enable_work.work);
	// struct oplus_chg_chip *chip = g_oplus_chip;
	bool enable = false;
	int (*otg_func_ptr)(void) = NULL;

	if (!bcdev /*|| !chip*/) {
		pr_err("bcdev is null or chip is null, return\n");
		return;
	}

	if (bcdev->otg_boost_src == OTG_BOOST_SOURCE_EXTERNAL) {
		if (bcdev->otg_online) {
			if (bcdev->usb_ocm) {
				// oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_OTG_ENABLE);
				msleep(50);
			}
			oplus_set_otg_ovp_en_val(bcdev, 1);
			oplus_set_otg_boost_en_val(bcdev, 1);
		} else {
			oplus_set_otg_ovp_en_val(bcdev, 0);
			oplus_set_otg_boost_en_val(bcdev, 0);
			if (bcdev->usb_ocm) {
				msleep(50);
				// oplus_chg_global_event(bcdev->usb_ocm, OPLUS_CHG_EVENT_OTG_DISABLE);
			}
		}
	} else if (bcdev->otg_boost_src == OTG_BOOST_SOURCE_PMIC) {
		enable = (bcdev->otg_prohibited ? false : bcdev->otg_online);
		// oplus_voocphy_set_chg_auto_mode(enable);
		// otg_func_ptr = (enable ? chip->chg_ops->otg_enable : chip->chg_ops->otg_disable);
		if (otg_func_ptr != NULL) {
			otg_func_ptr();
		}
	}
}

static void oplus_adsp_voocphy_err_work(struct work_struct *work)
{

	struct battery_chg_dev *bcdev = container_of(work, struct battery_chg_dev, adsp_voocphy_err_work.work);
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	struct oplus_chg_chip *chip = bcdev->chip;

	if (!chip) {
		printk(KERN_ERR "!!!chip null, oplus_adsp_voocphy_err_work\n");
		return;
	}

	chg_err("!!!call\n");
	if (/*oplus_voocphy_get_fastchg_ing() == false && */bcdev->adsp_voocphy_err_check) {
		chg_err("!!!happend\n");
		bcdev->adsp_voocphy_err_check = false;
		smbchg_usb_suspend_enable(battmgr);
		usleep_range(1000000, 1000010);
		// if (chip->mmi_chg) {
			smbchg_usb_suspend_disable(battmgr);
			smbchg_charging_enable(battmgr);
			oplus_adsp_voocphy_reset_again(battmgr);
		// }
	}
}

static struct power_supply_desc usb_psy_desc;

static void battery_chg_update_usb_type_work(struct work_struct *work)
{
	int ret;
	int chg_type;
	int sub_chg_type;
	static int last_usb_adap_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	struct battery_chg_dev *bcdev = container_of(work,
					struct battery_chg_dev, usb_type_work.work);
	struct qcom_battmgr *battmgr = bcdev->battmgr;

	// struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, USB_ADAP_TYPE, 0);
	if (ret) {
		pr_err("Failed to read USB_ADAP_TYPE ret=%d\n", ret);
		return;
	}

	/* Reset usb_icl_ua whenever USB adapter type changes */
	if (battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_SDP &&
	    battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_PD)
		bcdev->usb_icl_ua = 0;

	chg_err("usb_adap_type: %u\n", battmgr->usb.usb_type);

	chg_type = opchg_get_charger_type(bcdev);
	sub_chg_type = oplus_chg_get_charger_subtype(bcdev);
	bcdev->real_chg_type = chg_type | (sub_chg_type << 8);

	switch (battmgr->usb.usb_type) {
	case POWER_SUPPLY_USB_TYPE_SDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_USB_TYPE_DCP:
	case POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3:
	case QTI_POWER_SUPPLY_USB_TYPE_HVDCP_3P5:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	case POWER_SUPPLY_USB_TYPE_CDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case POWER_SUPPLY_USB_TYPE_ACA:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_ACA;
		break;
	case POWER_SUPPLY_USB_TYPE_C:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_TYPE_C;
		break;
	case POWER_SUPPLY_USB_TYPE_PD:
	case POWER_SUPPLY_USB_TYPE_PD_DRP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_PD;
		if (!bcdev->pd_type_checked) {
			bcdev->pd_type_checked = true;
			chg_err("start check pd type\n");
			schedule_delayed_work(&bcdev->pd_type_check_work, OPLUS_PD_TYPE_CHECK_INTERVAL);
		}
		break;
	case POWER_SUPPLY_USB_TYPE_PD_PPS:
	case POWER_SUPPLY_USB_TYPE_PD_SDP:
		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_PD;
		break;
	default:
		ret = oplus_battmgr_request_property(battmgr, BC_USB_STATUS_GET, USB_ONLINE, 0);
		if (ret) {
			pr_err("Failed to read USB_ONLINE ret=%d\n", ret);
			return;
		}
		if (battmgr->usb.online == 0) {
			/*pr_err("lizhijie USB_ONLINE 00000\n");*/
			// if (!(((oplus_chg_get_voocphy_support() == ADSP_VOOCPHY ||
			// 	oplus_chg_get_voocphy_support() == AP_SINGLE_CP_VOOCPHY ||
			// 	oplus_chg_get_voocphy_support() == AP_DUAL_CP_VOOCPHY) &&
			// 	g_oplus_chip && g_oplus_chip->mmi_fastchg == 0) || oplus_quirks_keep_connect_status()))
			// 	usb_psy_desc.type = POWER_SUPPLY_TYPE_UNKNOWN;
		}
		break;
	}

	if (usb_psy_desc.type != POWER_SUPPLY_TYPE_UNKNOWN) {
		if (chg_type == POWER_SUPPLY_TYPE_USB_DCP
			&& battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_DCP
			&& bcdev->hvdcp_disable == false) {
			usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
		} else if (chg_type == POWER_SUPPLY_TYPE_USB
			&& battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_SDP) {
			usb_psy_desc.type = POWER_SUPPLY_TYPE_USB;
		} else if (chg_type == POWER_SUPPLY_TYPE_USB_CDP
			&& battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_CDP) {
			usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_CDP;
		}
	}

	// if (usb_psy_desc.type == POWER_SUPPLY_TYPE_UNKNOWN &&
	//     (oplus_chg_get_voocphy_support() != ADSP_VOOCPHY)) {
	// 	chg_err("!!! usb_psy_desc.type: [%d]to_warm[%d]dummy[%d]to_normal[%d]started[%d]\n",
	// 		usb_psy_desc.type,
	// 		oplus_vooc_get_fastchg_to_warm(),
	// 		oplus_vooc_get_fastchg_dummy_started(),
	// 		oplus_vooc_get_fastchg_to_normal(),
	// 		oplus_vooc_get_fastchg_started());
	// 	if (oplus_vooc_get_fastchg_to_warm() == true ||
	// 	    oplus_vooc_get_fastchg_dummy_started() == true ||
	// 	    oplus_vooc_get_fastchg_to_normal() == true ||
	// 	    oplus_vooc_get_fastchg_started() == true) {
	// 		usb_psy_desc.type = POWER_SUPPLY_TYPE_USB_DCP;
	// 		chg_err("!!! usb_psy_desc.type: [%d]\n", usb_psy_desc.type);
	// 	}
	// }

	if (!((last_usb_adap_type == POWER_SUPPLY_USB_TYPE_PD_PPS
	    || last_usb_adap_type == POWER_SUPPLY_USB_TYPE_PD
	    || last_usb_adap_type == POWER_SUPPLY_USB_TYPE_PD_DRP)
	    && battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_PD_PPS
	    && battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_PD
	    && battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_PD_DRP)
	    && battmgr->usb.usb_type != POWER_SUPPLY_USB_TYPE_UNKNOWN
	    && last_usb_adap_type != battmgr->usb.usb_type) {
		chg_err("oplus_chg_wake_update_work\n");
		power_supply_changed(battmgr->bat_psy);
		oplus_chg_update_work(bcdev);
	}
	last_usb_adap_type = battmgr->usb.usb_type;
}

static int __battery_psy_set_charge_current(struct qcom_battmgr *battmgr,
					u32 fcc_ua)
{
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	int ret;

	if (bcdev->restrict_chg_en) {
		fcc_ua = min_t(u32, fcc_ua, bcdev->restrict_fcc_ua);
		fcc_ua = min_t(u32, fcc_ua, bcdev->thermal_fcc_ua);
	}

	ret = oplus_battmgr_request_property(battmgr, BC_BATTERY_STATUS_SET, BATT_CHG_CTRL_LIM, fcc_ua);

	if (ret < 0)
		chg_err("Failed to set FCC %u, ret=%d\n", fcc_ua, ret);
	else
		pr_debug("Set FCC to %u uA\n", fcc_ua);

	return ret;
}

static int battery_psy_set_charge_current(struct qcom_battmgr *battmgr,
					int val)
{
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	u32 fcc_ua, prev_fcc_ua;
	int ret;

	if (!bcdev->num_thermal_levels)
		return 0;

	if (bcdev->num_thermal_levels < 0) {
		chg_err("Incorrect num_thermal_levels\n");
		return -EINVAL;
	}

	if (val < 0 || val > bcdev->num_thermal_levels)
		return -EINVAL;

	fcc_ua = bcdev->thermal_levels[val];
	prev_fcc_ua = bcdev->thermal_fcc_ua;
	bcdev->thermal_fcc_ua = fcc_ua;

	ret = __battery_psy_set_charge_current(battmgr, fcc_ua);
	if (!ret)
		bcdev->curr_thermal_level = val;
	else
		bcdev->thermal_fcc_ua = prev_fcc_ua;

	return ret;
}

static const u8 bat_prop_map[] = {
	[POWER_SUPPLY_PROP_STATUS] = BATT_STATUS,
	[POWER_SUPPLY_PROP_HEALTH] = BATT_HEALTH,
	[POWER_SUPPLY_PROP_PRESENT] = BATT_PRESENT,
	[POWER_SUPPLY_PROP_CHARGE_TYPE] = BATT_CHG_TYPE,
	[POWER_SUPPLY_PROP_CAPACITY] = BATT_CAPACITY,
	[POWER_SUPPLY_PROP_VOLTAGE_OCV] = BATT_VOLT_OCV,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = BATT_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = BATT_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = BATT_CURR_NOW,
	[POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT] = BATT_CHG_CTRL_LIM,
	[POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX] = BATT_CHG_CTRL_LIM_MAX,
	[POWER_SUPPLY_PROP_TEMP] = BATT_TEMP,
	[POWER_SUPPLY_PROP_TECHNOLOGY] = BATT_TECHNOLOGY,
	[POWER_SUPPLY_PROP_CHARGE_COUNTER] =  BATT_CHG_COUNTER,
	[POWER_SUPPLY_PROP_CYCLE_COUNT] = BATT_CYCLE_COUNT,
	[POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN] =  BATT_CHG_FULL_DESIGN,
	[POWER_SUPPLY_PROP_CHARGE_FULL] = BATT_CHG_FULL,
	[POWER_SUPPLY_PROP_MODEL_NAME] = BATT_MODEL_NAME,
	[POWER_SUPPLY_PROP_TIME_TO_FULL_AVG] = BATT_TTF_AVG,
	[POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG] = BATT_TTE_AVG,
	[POWER_SUPPLY_PROP_POWER_NOW] = BATT_POWER_NOW,
	[POWER_SUPPLY_PROP_POWER_AVG] = BATT_POWER_AVG,
};

static int bat_update(struct qcom_battmgr *battmgr,
					  enum power_supply_property psp)
{
	unsigned int prop;

	if (psp >= ARRAY_SIZE(bat_prop_map))
		return -EINVAL;

	prop = bat_prop_map[psp];

	return oplus_battmgr_request_property(battmgr, BC_BATTERY_STATUS_GET, prop, 0);;
}

static int battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	int ret;

	if (!battmgr->service_up)
		return -EAGAIN;

	ret = bat_update(battmgr, psp);
	if (ret < 0)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = battmgr->status.status;
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		val->intval = battmgr->info.charge_type;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = battmgr->status.health;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		val->intval = battmgr->info.present;
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = battmgr->info.technology;
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		val->intval = battmgr->info.cycle_count;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		val->intval = battmgr->info.voltage_max_design;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = battmgr->info.voltage_max;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battmgr->status.voltage_now;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = battmgr->status.voltage_ocv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = battmgr->status.current_now;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		val->intval = battmgr->bcdev->curr_thermal_level;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		val->intval = battmgr->bcdev->num_thermal_levels;
		break;
	case POWER_SUPPLY_PROP_POWER_NOW:
		val->intval = battmgr->status.power_now;
		break;
	case POWER_SUPPLY_PROP_POWER_AVG:
		val->intval = battmgr->status.power_avg;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		val->intval = battmgr->info.design_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		val->intval = battmgr->info.last_full_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		val->intval = battmgr->info.capacity_low;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		val->intval = battmgr->status.capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = battmgr->info.charge_count;
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		val->intval = battmgr->info.design_capacity;
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		val->intval = battmgr->info.last_full_capacity;
		break;
	case POWER_SUPPLY_PROP_ENERGY_EMPTY:
		val->intval = battmgr->info.capacity_low;
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		val->intval = battmgr->status.capacity;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (battmgr->status.percent == (unsigned int)-1)
			return -ENODATA;
		val->intval = battmgr->status.percent;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = DIV_ROUND_CLOSEST((int)battmgr->status.temperature, 10);
		break;
	case POWER_SUPPLY_PROP_INTERNAL_RESISTANCE:
		val->intval = battmgr->status.resistance;
		break;
	case POWER_SUPPLY_PROP_STATE_OF_HEALTH:
		val->intval = battmgr->status.soh_percent;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG:
		val->intval = battmgr->status.discharge_time;
		break;
	case POWER_SUPPLY_PROP_TIME_TO_FULL_AVG:
		val->intval = battmgr->status.charge_time;
		break;;
	case POWER_SUPPLY_PROP_MANUFACTURE_YEAR:
		val->intval = battmgr->info.year;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURE_MONTH:
		val->intval = battmgr->info.month;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURE_DAY:
		val->intval = battmgr->info.day;
		break;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = battmgr->info.model_number;
		break;
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = battmgr->info.oem_info;
		break;
	case POWER_SUPPLY_PROP_SERIAL_NUMBER:
		val->strval = battmgr->info.serial_number;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int battery_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *pval)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);

	if (!battmgr->service_up)
		return -EAGAIN;

	switch (psp) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return battery_psy_set_charge_current(battmgr, pval->intval);
	default:
		return -EINVAL;
	}

	return 0;
}

static int battery_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	default:
		break;
	}

	return 0;
}

static enum power_supply_property battery_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_TIME_TO_FULL_AVG,
	POWER_SUPPLY_PROP_TIME_TO_FULL_NOW,
	POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG,
	POWER_SUPPLY_PROP_POWER_NOW,
	POWER_SUPPLY_PROP_POWER_AVG,
	POWER_SUPPLY_PROP_CHARGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};

static const struct power_supply_desc battery_psy_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = battery_props,
	.num_properties = ARRAY_SIZE(battery_props),
	.get_property = battery_get_property,
	.set_property		= battery_set_prop,
	.property_is_writeable	= battery_prop_is_writeable,
};

static const u8 usb_prop_map[USB_PROP_MAX] = {
	[POWER_SUPPLY_PROP_ONLINE] = USB_ONLINE,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = USB_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = USB_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = USB_CURR_NOW,
	[POWER_SUPPLY_PROP_CURRENT_MAX] = USB_CURR_MAX,
	[POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT] = USB_INPUT_CURR_LIMIT,
	[POWER_SUPPLY_PROP_USB_TYPE] = USB_ADAP_TYPE,
	[POWER_SUPPLY_PROP_TEMP] = USB_TEMP,
};

static const u8 sm8450_wls_prop_map[] = {
	[POWER_SUPPLY_PROP_ONLINE] = WLS_ONLINE,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = WLS_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = WLS_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = WLS_CURR_NOW,
	[POWER_SUPPLY_PROP_CURRENT_MAX] = WLS_CURR_MAX,
};

static const u8 adsp_wls_prop_map[] = {
	[POWER_SUPPLY_PROP_ONLINE] = WLS_ONLINE,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = WLS_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = WLS_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = WLS_CURR_NOW,
	[POWER_SUPPLY_PROP_CURRENT_MAX] = WLS_CURR_MAX,
	[POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT] = WLS_INPUT_CURR_LIMIT,
	[POWER_SUPPLY_PROP_TEMP] = WLS_CONN_TEMP,
};


static int oplus_battmgr_usb_update(struct qcom_battmgr *battmgr,
					  enum power_supply_property psp)
{
	unsigned int prop;

	if (psp >= ARRAY_SIZE(usb_prop_map))
		return -EINVAL;

	prop = usb_prop_map[psp];

	return oplus_battmgr_request_property(battmgr, BC_BATTERY_STATUS_GET, prop, 0);
}

static int usb_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	int ret;

	if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8750)
		return 0;

	if (!battmgr->service_up)
		return -EAGAIN;

	ret = oplus_battmgr_usb_update(battmgr, psp);
	if (ret)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = battmgr->usb.online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battmgr->usb.voltage_now;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = battmgr->usb.voltage_max;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = battmgr->usb.current_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = battmgr->usb.current_max;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = battmgr->usb.current_limit;
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = battmgr->usb.usb_type;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = DIV_ROUND_CLOSEST((int)battmgr->usb.temp, 10);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const enum power_supply_property usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TEMP,
};

static struct power_supply_desc usb_psy_desc = {
	.name = "usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = usb_props,
	.num_properties = ARRAY_SIZE(usb_props),
	.get_property = usb_get_property,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) |
		     BIT(POWER_SUPPLY_USB_TYPE_SDP)     |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP)     |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP)     |
		     BIT(POWER_SUPPLY_USB_TYPE_ACA)     |
		     BIT(POWER_SUPPLY_USB_TYPE_C)       |
		     BIT(POWER_SUPPLY_USB_TYPE_PD)      |
		     BIT(POWER_SUPPLY_USB_TYPE_PD_DRP)  |
		     BIT(POWER_SUPPLY_USB_TYPE_PD_PPS)  |
		     BIT(POWER_SUPPLY_USB_TYPE_APPLE_BRICK_ID),
};

static int oplus_battmgr_wls_sm8450_update(struct qcom_battmgr *battmgr,
					  enum power_supply_property psp)
{
	unsigned int prop;


	if (psp >= ARRAY_SIZE(sm8450_wls_prop_map))
		return -EINVAL;

	prop = sm8450_wls_prop_map[psp];

	return oplus_battmgr_request_property(battmgr, BC_WLS_STATUS_GET, prop, 0);;
}

static int oplus_battmgr_wls_adsp_update(struct qcom_battmgr *battmgr,
					  enum power_supply_property psp)
{
	unsigned int prop;


	if (psp >= ARRAY_SIZE(adsp_wls_prop_map))
		return -EINVAL;

	prop = adsp_wls_prop_map[psp];

	return oplus_battmgr_request_property(battmgr, BC_WLS_STATUS_GET, prop, 0);
}

static int wls_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	int ret;

	if (!battmgr->service_up)
		return -EAGAIN;


	if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8450)
		ret = oplus_battmgr_wls_sm8450_update(battmgr, psp);
	else
		ret = oplus_battmgr_wls_adsp_update(battmgr, psp);
	if (ret < 0)
		return ret;

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = battmgr->wireless.online;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = battmgr->wireless.voltage_now;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = battmgr->wireless.voltage_max;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = battmgr->wireless.current_now;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = battmgr->wireless.current_max;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = battmgr->wireless.current_limit;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battmgr->wireless.temp;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const enum power_supply_property sm8450_wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc sm8450_wls_psy_desc = {
	.name = "wireless",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = sm8450_wls_props,
	.num_properties = ARRAY_SIZE(sm8450_wls_props),
	.get_property = wls_get_property,
};

static const enum power_supply_property adsp_wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc adsp_wls_psy_desc = {
	.name = "wireless",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = adsp_wls_props,
	.num_properties = ARRAY_SIZE(adsp_wls_props),
	.get_property = wls_get_property,
};

static void oplus_battmgr_notification(struct qcom_battmgr *battmgr,
				      const struct qcom_battmgr_message *msg,
				      int len)
{
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	size_t payload_len = len - sizeof(struct pmic_glink_hdr);
	unsigned int notification;

	int chg_type;
	int sub_chg_type;

	if (payload_len != sizeof(msg->notification)) {
		dev_warn(battmgr->dev, "ignoring notification with invalid length\n");
		return;
	}

	notification = le32_to_cpu(msg->notification);
	notification &= 0xff;
	switch (notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		power_supply_changed(battmgr->bat_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		break;
	case BC_USB_STATUS_GET:
		schedule_delayed_work(&bcdev->usb_type_work, 0);
		power_supply_changed(battmgr->usb_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		break;
	case BC_WLS_STATUS_GET:
		power_supply_changed(battmgr->wls_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		break;
	case BC_PD_SVOOC:
		printk(KERN_ERR "!!!:%s, should set pd_svooc\n", __func__);
		bcdev->pd_svooc = true;
		// g_oplus_chip->pd_svooc = true;
		printk(KERN_ERR "!!!:%s, pd_svooc[%d]\n", __func__, bcdev->pd_svooc);
		break;
	case BC_ABNORMAL_PD_SVOOC_ADAPTER:
		printk(KERN_ERR "!!!:%s, is_abnormal_adapter\n", __func__);
		// g_oplus_chip->is_abnormal_adapter = true;
		break;
	case BC_VOOC_STATUS_GET:
		// schedule_delayed_work(&bcdev->adsp_voocphy_status_work, 0);
		break;
	case BC_OTG_ENABLE:
		printk(KERN_ERR "!!!!!enable otg\n");
		power_supply_changed(battmgr->usb_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		bcdev->otg_online = true;
		bcdev->pd_svooc = false;
		schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_OTG_DISABLE:
		printk(KERN_ERR "!!!!!disable otg\n");
		power_supply_changed(battmgr->usb_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		bcdev->otg_online = false;
		schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_ADSP_NOTIFY_TRACK:
		pr_info("!!!!!adsp track notify\n");
		// schedule_delayed_work(&bcdev->adsp_track_notify_work, 0);
		break;
	case BC_VOOC_VBUS_ADC_ENABLE:
		printk(KERN_ERR "!!!!!vooc_vbus_adc_enable\n");
		bcdev->adsp_voocphy_err_check = true;
		bcdev->vooc_chip.adspvoocphy_fastchg_start = true;
		cancel_delayed_work_sync(&bcdev->adsp_voocphy_err_work);
		schedule_delayed_work(&bcdev->adsp_voocphy_err_work, msecs_to_jiffies(8500));
		smbchg_charging_disable(battmgr);
		smbchg_usb_suspend_enable(battmgr);/*excute in glink loop for real time*/
		break;
	case BC_CID_DETECT:
		printk(KERN_ERR "!!!!!cid detect || no detect\n");
		// schedule_delayed_work(&bcdev->cid_status_change_work, 0);
		break;
	case BC_QC_DETECT:
		chg_type = opchg_get_charger_type(bcdev);
		sub_chg_type = oplus_chg_get_charger_subtype(bcdev);
		bcdev->real_chg_type = chg_type | (sub_chg_type << 8);
		bcdev->hvdcp_detect_ok = true;
		break;
	case BC_TYPEC_STATE_CHANGE:
		printk(KERN_ERR "!!!!!typec_state_change_work\n");
		// schedule_delayed_work(&bcdev->typec_state_change_work, 0);
		break;
	case BC_PLUGIN_IRQ:
		printk(KERN_ERR "!!!!!oplus_plugin_irq_work\n");
		schedule_delayed_work(&bcdev->plugin_irq_work, 0);
		break;
	case BC_APSD_DONE:
		printk(KERN_ERR "!!!!!oplus_apsd_done_work\n");
		// schedule_delayed_work(&bcdev->apsd_done_work, 0);
		break;
	case BC_CHG_STATUS_GET:
		schedule_delayed_work(&bcdev->chg_status_send_work, 0);
		break;
	case BC_ADSP_NOTIFY_AP_SUSPEND_CHG:
		printk(KERN_ERR "!!!!!oplus_apsd_notify_ap_suspend_chg\n");
		bcdev->chip->adsp_notify_ap_suspend = true;
		// oplus_chg_set_adsp_notify_ap_suspend();
		break;
	case BC_PD_SOFT_RESET:
		printk(KERN_ERR "!!!!!PD hard reset happend\n");
		break;
	case PD_SOURCECAP_DONE:
		// schedule_delayed_work(&bcdev->pd_set_aicl_work, 0);
		break;
	case BC_CHG_STATUS_SET:
		smbchg_usb_suspend_disable(battmgr);
		break;
	case BC_ADSP_NOTIFY_AP_CP_BYPASS_INIT:
		printk(KERN_ERR "!!!!!BC_ADSP_NOTIFY_AP_CP_BYPASS_INIT\n");
		// if (g_oplus_chip && (oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
		// 	oplus_pps_get_support_type() == PPS_SUPPORT_3CP))
		// 	oplus_pps_cp_mode_init(PPS_BYPASS_MODE);
		break;
	case BC_ADSP_NOTIFY_AP_CP_MOS_ENABLE:
		printk(KERN_ERR "!!!!!BC_ADSP_NOTIFY_AP_CP_MOS_ENABLE\n");
		// if (g_oplus_chip && (oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
		// 	oplus_pps_get_support_type() == PPS_SUPPORT_3CP)) {
		// 	oplus_pps_set_svooc_mos_enable(true);
		// }
		break;
	case BC_ADSP_NOTIFY_AP_CP_MOS_DISABLE:
		printk(KERN_ERR "!!!!!BC_ADSP_NOTIFY_AP_CP_MOS_DISABLE\n");
		// if (g_oplus_chip && (oplus_pps_get_support_type() == PPS_SUPPORT_2CP ||
		// 	oplus_pps_get_support_type() == PPS_SUPPORT_3CP)) {
		// 	oplus_pps_set_pps_mos_enable(false);
		// }
		break;
	case BC_PPS_OPLUS:
		printk(KERN_ERR "!!!!!BC_PPS_OPLUS\n");
		oplus_chg_update_work(bcdev);
		break;
	default:
		dev_err(battmgr->dev, "unknown notification: %#x\n", notification);
		break;
	}
}

static void battmgr_callback(struct qcom_battmgr *battmgr,
					 const struct qcom_battmgr_message *resp,
					 size_t len)
{
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	unsigned int property;
	unsigned int opcode = le32_to_cpu(resp->hdr.opcode);
	size_t payload_len = len - sizeof(struct pmic_glink_hdr);
	unsigned int val;

	if (payload_len < sizeof(__le32)) {
		dev_warn(battmgr->dev, "invalid payload length for %#x: %zd\n",
			 opcode, len);
		return;
	}

	switch (opcode) {
	case BC_BATTERY_STATUS_GET:
		property = le32_to_cpu(resp->intval.property);
		if (property == BATT_MODEL_NAME) {
			if (payload_len != sizeof(resp->strval)) {
				dev_warn(battmgr->dev,
					 "invalid payload length for BATT_MODEL_NAME request: %zd\n",
					 payload_len);
				battmgr->error = -ENODATA;
				return;
			}
		} else {
			if (payload_len != sizeof(resp->intval)) {
				dev_warn(battmgr->dev,
					 "invalid payload length for %#x request: %zd\n",
					 property, payload_len);
				battmgr->error = -ENODATA;
				return;
			}

			battmgr->error = le32_to_cpu(resp->intval.result);
			if (battmgr->error)
				goto out_complete;
		}

		switch (property) {
		case BATT_STATUS:
			battmgr->status.status = le32_to_cpu(resp->intval.value);
			break;
		case BATT_HEALTH:
			battmgr->status.health = le32_to_cpu(resp->intval.value);
			break;
		case BATT_PRESENT:
			battmgr->info.present = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CHG_TYPE:
			battmgr->info.charge_type = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CAPACITY:
			battmgr->status.percent = le32_to_cpu(resp->intval.value) / 100;
			break;
		case BATT_SOH:
			battmgr->status.soh_percent = le32_to_cpu(resp->intval.value);
			break;
		case BATT_VOLT_OCV:
			battmgr->status.voltage_ocv = le32_to_cpu(resp->intval.value);
			break;
		case BATT_VOLT_NOW:
			battmgr->status.voltage_now = le32_to_cpu(resp->intval.value);
			break;
		case BATT_VOLT_MAX:
			battmgr->info.voltage_max = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CURR_NOW:
			battmgr->status.current_now = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CHG_CTRL_LIM:
			battmgr->bcdev->curr_thermal_level = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CHG_CTRL_LIM_MAX:
			battmgr->bcdev->num_thermal_levels = le32_to_cpu(resp->intval.value);
			break;
		case BATT_TEMP:
			val = le32_to_cpu(resp->intval.value);
			battmgr->status.temperature = DIV_ROUND_CLOSEST(val, 10);
			break;
		case BATT_TECHNOLOGY:
			battmgr->info.technology = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CHG_COUNTER:
			battmgr->info.charge_count = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CYCLE_COUNT:
			battmgr->info.cycle_count = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CHG_FULL_DESIGN:
			battmgr->info.design_capacity = le32_to_cpu(resp->intval.value);
			break;
		case BATT_CHG_FULL:
			battmgr->info.last_full_capacity = le32_to_cpu(resp->intval.value);
			break;
		case BATT_MODEL_NAME:
			strscpy(battmgr->info.model_number, resp->strval.model, BATTMGR_STRING_LEN);
			break;
		case BATT_TTF_AVG:
			battmgr->status.charge_time = le32_to_cpu(resp->intval.value);
			break;
		case BATT_TTE_AVG:
			battmgr->status.discharge_time = le32_to_cpu(resp->intval.value);
			break;
		case BATT_RESISTANCE:
			battmgr->status.resistance = le32_to_cpu(resp->intval.value);
			break;
		case BATT_POWER_NOW:
			battmgr->status.power_now = le32_to_cpu(resp->intval.value);
			break;
		case BATT_POWER_AVG:
			battmgr->status.power_avg = le32_to_cpu(resp->intval.value);
			break;
		default:
			dev_warn(battmgr->dev, "unknown property %#x\n", property);
			break;
		}
		break;
	case BC_USB_STATUS_GET:
		property = le32_to_cpu(resp->intval.property);
		if (payload_len != sizeof(resp->intval)) {
			dev_warn(battmgr->dev,
				 "invalid payload length for %#x request: %zd\n",
				 property, payload_len);
			battmgr->error = -ENODATA;
			return;
		}

		battmgr->error = le32_to_cpu(resp->intval.result);
		if (battmgr->error)
			goto out_complete;

		switch (property) {
		case USB_ONLINE:
			battmgr->usb.online = le32_to_cpu(resp->intval.value);
			break;
		case USB_VOLT_NOW:
			battmgr->usb.voltage_now = le32_to_cpu(resp->intval.value);
			break;
		case USB_VOLT_MAX:
			battmgr->usb.voltage_max = le32_to_cpu(resp->intval.value);
			break;
		case USB_CURR_NOW:
			battmgr->usb.current_now = le32_to_cpu(resp->intval.value);
			break;
		case USB_CURR_MAX:
			battmgr->usb.current_max = le32_to_cpu(resp->intval.value);
			break;
		case USB_INPUT_CURR_LIMIT:
			battmgr->usb.current_limit = le32_to_cpu(resp->intval.value);
			break;
		case USB_ADAP_TYPE:
			battmgr->usb.usb_type = le32_to_cpu(resp->intval.value);
			break;
		case USB_TEMP:
			battmgr->usb.temp = le32_to_cpu(resp->intval.value);
			break;
		case USB_IN_STATUS:
			bcdev->usb.usb_in_status = le32_to_cpu(resp->intval.value);
			break;
		case USB_SUSPEND_PMIC:
			bcdev->usb.usb_suspend_pmic = le32_to_cpu(resp->intval.value);
			break;
		case USB_ADAP_SUBTYPE:
			bcdev->usb.usb_subtype = le32_to_cpu(resp->intval.value);
			break;
		default:
			dev_warn(battmgr->dev, "unknown property %#x\n", property);
			break;
		}
		break;
	case BC_WLS_STATUS_GET:
		property = le32_to_cpu(resp->intval.property);
		if (payload_len != sizeof(resp->intval)) {
			dev_warn(battmgr->dev,
				 "invalid payload length for %#x request: %zd\n",
				 property, payload_len);
			battmgr->error = -ENODATA;
			return;
		}

		battmgr->error = le32_to_cpu(resp->intval.result);
		if (battmgr->error)
			goto out_complete;

		switch (property) {
		case WLS_ONLINE:
			battmgr->wireless.online = le32_to_cpu(resp->intval.value);
			break;
		case WLS_VOLT_NOW:
			battmgr->wireless.voltage_now = le32_to_cpu(resp->intval.value);
			break;
		case WLS_VOLT_MAX:
			battmgr->wireless.voltage_max = le32_to_cpu(resp->intval.value);
			break;
		case WLS_CURR_NOW:
			battmgr->wireless.current_now = le32_to_cpu(resp->intval.value);
			break;
		case WLS_CURR_MAX:
			battmgr->wireless.current_max = le32_to_cpu(resp->intval.value);
			break;
		default:
			dev_warn(battmgr->dev, "unknown property %#x\n", property);
			break;
		}
		break;
	case BC_BATTERY_STATUS_SET:
	case BC_USB_STATUS_SET:
	case BC_WLS_STATUS_SET:
		property = le32_to_cpu(resp->intval.property);
		battmgr->error = le32_to_cpu(resp->intval.result);
		dev_dbg(battmgr->dev, "property set resopnse: opcode: 0x%x, property: %u, result: %u\n", opcode, property, battmgr->error);
		if (battmgr->error)
			goto out_complete;
		break;
	case BC_SET_NOTIFY_REQ:
		battmgr->error = 0;
		break;
	default:
		dev_warn(battmgr->dev, "unknown message %#x\n", opcode);
		break;
	}

out_complete:
	complete(&battmgr->ack);
}

static void oplus_battmgr_callback(const void *data, size_t len, void *priv)
{
	const struct pmic_glink_hdr *hdr = data;
	struct qcom_battmgr *battmgr = priv;
	unsigned int opcode = le32_to_cpu(hdr->opcode);

	if (opcode == BC_NOTIFY_IND)
		oplus_battmgr_notification(battmgr, data, len);
	else
		battmgr_callback(battmgr, data, len);
}

static void oplus_battmgr_enable_worker(struct work_struct *work)
{
	struct qcom_battmgr *battmgr = container_of(work, struct qcom_battmgr, enable_work);
	struct qcom_battmgr_enable_request req = {
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_BATTMGR),
		.hdr.type = cpu_to_le32(PMIC_GLINK_NOTIFY),
		.hdr.opcode = cpu_to_le32(BC_SET_NOTIFY_REQ),
	};
	int ret;

	ret = qcom_battmgr_request(battmgr, &req, sizeof(req));
	if (ret)
		dev_err(battmgr->dev, "failed to request power notifications\n");

	schedule_delayed_work(&battmgr->bcdev->adsp_crash_recover_work, round_jiffies_relative(msecs_to_jiffies(1500)));
}

static void oplus_adsp_crash_recover_func(struct work_struct *work)
{
	struct battery_chg_dev *bcdev = container_of(work, struct battery_chg_dev, adsp_crash_recover_work.work);
	struct oplus_chg_chip *chip = bcdev->chip;

	if (!chip) {
		printk(KERN_ERR "!!!chip null\n");
		return;
	}

	// if (chip->voocphy_support == ADSP_VOOCPHY) {
		oplus_ap_init_adsp_gague(bcdev->battmgr);
		// oplus_adsp_voocphy_reset_status_when_crash_recover();
	// }
	chip->charger_type  = POWER_SUPPLY_TYPE_UNKNOWN;
	// if (chip->voocphy_support == ADSP_VOOCPHY)
		oplus_adsp_voocphy_enable(bcdev->battmgr, true);

	schedule_delayed_work(&bcdev->otg_init_work, round_jiffies_relative(msecs_to_jiffies(2000)));
	oplus_chg_update_work(bcdev);
	// schedule_delayed_work(&bcdev->adsp_voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(0)));
	// schedule_delayed_work(&bcdev->check_charger_out_work, round_jiffies_relative(msecs_to_jiffies(3000)));
	// if (oplus_ccdetect_check_is_gpio(chip) == true)
	// 	oplus_ccdetect_before_irq_register(chip);
}

static const struct of_device_id oplus_battmgr_of_variants[] = {
	{ .compatible = "oplus,kaanapali-pmic-glink", .data = (void *)OPLUS_BATTMGR_SM8750 },
	{ .compatible = "oplus,sm8750-pmic-glink", .data = (void *)OPLUS_BATTMGR_SM8750 },
	{ .compatible = "oplus,sm8550-pmic-glink", .data = (void *)OPLUS_BATTMGR_SM8550 },
	{ .compatible = "oplus,sm8450-pmic-glink", .data = (void *)OPLUS_BATTMGR_SM8450 },
	/* Unmatched devices falls back to OPLUS_BATTMGR_SM8650 */
	{}
};

static char *oplus_battmgr_battery[] = { "battery" };

static int oplus_battmgr_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	const struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg_supply = {};
	struct power_supply_config psy_cfg = {};
	const struct of_device_id *match;
	struct qcom_battmgr *battmgr;
	struct battery_chg_dev *bcdev;
	struct device *dev = &adev->dev;
	int ret;

	battmgr = devm_kzalloc(dev, sizeof(*battmgr), GFP_KERNEL);
	if (!battmgr)
		return -ENOMEM;

	battmgr->dev = dev;

	bcdev = devm_kzalloc(dev, sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev)
		return -ENOMEM;

	battmgr->bcdev = bcdev;
	bcdev->battmgr = battmgr;

	bcdev->chip = devm_kzalloc(dev, sizeof(*bcdev->chip), GFP_KERNEL);
	if (!bcdev->chip)
		return -ENOMEM;

	psy_cfg.drv_data = battmgr;
	psy_cfg.fwnode = dev_fwnode(&adev->dev);

	psy_cfg_supply.drv_data = battmgr;
	psy_cfg_supply.fwnode = dev_fwnode(&adev->dev);
	psy_cfg_supply.supplied_to = oplus_battmgr_battery;
	psy_cfg_supply.num_supplicants = 1;

	mutex_init(&battmgr->lock);
	init_completion(&battmgr->ack);

	// INIT_DELAYED_WORK(&bcdev->typec_state_change_work, oplus_typec_state_change_work);
	INIT_DELAYED_WORK(&bcdev->chg_status_send_work, oplus_chg_status_send_adsp_work);
	// INIT_DELAYED_WORK(&bcdev->pd_set_aicl_work, oplus_chg_pd_set_aicl_work);
	// INIT_WORK(&bcdev->subsys_up_work, battery_chg_subsys_up_work);
	INIT_DELAYED_WORK(&bcdev->usb_type_work, battery_chg_update_usb_type_work);
	// INIT_WORK(&bcdev->plc_status_update_work, oplus_chg_adsp_plc_status_update_work);
	// INIT_WORK(&bcdev->gauge_cali_track_by_plug_work, oplus_plat_gauge_cali_track_by_plug_work);
	// INIT_WORK(&bcdev->gauge_cali_track_by_full_work, oplus_plat_gauge_cali_track_by_full_work);
	// INIT_DELAYED_WORK(&bcdev->adsp_voocphy_status_work, oplus_adsp_voocphy_status_func);
	// INIT_DELAYED_WORK(&bcdev->unsuspend_usb_work, oplus_unsuspend_usb_work);
	INIT_DELAYED_WORK(&bcdev->otg_init_work, oplus_otg_init_status_func);
	// INIT_DELAYED_WORK(&bcdev->cid_status_change_work, oplus_cid_status_change_work);
	INIT_DELAYED_WORK(&bcdev->adsp_crash_recover_work, oplus_adsp_crash_recover_func);
	// INIT_DELAYED_WORK(&bcdev->crash_track_work, oplus_crash_track_work);
	// INIT_DELAYED_WORK(&bcdev->voocphy_enable_check_work, oplus_voocphy_enable_check_func);
	// INIT_DELAYED_WORK(&bcdev->adsp_voocphy_enable_check_work, oplus_adsp_voocphy_enable_check_func);
	INIT_DELAYED_WORK(&bcdev->otg_vbus_enable_work, otg_notification_handler);
	// INIT_DELAYED_WORK(&bcdev->hvdcp_disable_work, oplus_hvdcp_disable_work);
	// INIT_DELAYED_WORK(&bcdev->pd_only_check_work, oplus_pd_only_check_work);
	// INIT_DELAYED_WORK(&bcdev->otg_status_check_work, oplus_otg_status_check_work);
	// INIT_DELAYED_WORK(&bcdev->vbus_adc_enable_work, oplus_vbus_enable_adc_work);
	INIT_DELAYED_WORK(&bcdev->adsp_voocphy_err_work, oplus_adsp_voocphy_err_work);
	// INIT_DELAYED_WORK(&bcdev->oem_lcm_en_check_work, oplus_oem_lcm_en_check_work);
	// INIT_DELAYED_WORK(&bcdev->voocphy_err_work, oplus_voocphy_err_work);
	// INIT_DELAYED_WORK(&bcdev->ctrl_lcm_frequency, oplus_chg_ctrl_lcm_work);
	INIT_DELAYED_WORK(&bcdev->plugin_irq_work, oplus_plugin_irq_work);
	// INIT_DELAYED_WORK(&bcdev->recheck_input_current_work, oplus_recheck_input_current_work);
	// INIT_DELAYED_WORK(&bcdev->vbus_collapse_rerun_icl_work, oplus_vbus_collapse_rerun_icl_work);
	// INIT_DELAYED_WORK(&bcdev->check_adspfg_status, oplus_check_adspfg_status_work);
	// INIT_DELAYED_WORK(&bcdev->publish_close_cp_item_work, oplus_publish_close_cp_item_work);
	// INIT_DELAYED_WORK(&bcdev->hboost_notify_work, oplus_hboost_notify_work);
	// INIT_DELAYED_WORK(&bcdev->sourcecap_done_work, oplus_sourcecap_done_work);
	// INIT_DELAYED_WORK(&bcdev->pdo_update_work, oplus_pdo_update_work);
	// INIT_DELAYED_WORK(&bcdev->sourcecap_suspend_recovery_work, oplus_sourcecap_suspend_recovery_work);
	// INIT_DELAYED_WORK(&bcdev->update_pd_svooc_work, oplus_update_pd_svooc_work);
	// INIT_DELAYED_WORK(&bcdev->iterm_timeout_work, oplus_iterm_timeout_work);
	// INIT_DELAYED_WORK(&bcdev->request_qos_work, oplus_request_qos_work);
	// INIT_DELAYED_WORK(&bcdev->release_qos_work, oplus_release_qos_work);
	// INIT_WORK(&bcdev->wired_otg_enable_work, oplus_wired_otg_enable_work);
	// INIT_DELAYED_WORK(&bcdev->ufcs_reset_work, oplus_ufcs_reset_work);
	// INIT_DELAYED_WORK(&bcdev->gauge_register_work, oplus_gauge_register_work);
	// INIT_DELAYED_WORK(&bcdev->update_common_charge_flag_work, oplus_update_common_charge_flag_work);
	// INIT_DELAYED_WORK(&bcdev->check_abnormal_usbin_status_work, oplus_check_abnormal_usbin_status_work);
	// INIT_DELAYED_WORK(&bcdev->source_pdo_check_work, oplus_source_pdo_check_work);
	// INIT_DELAYED_WORK(&bcdev->vchg_trig_work, oplus_vchg_trig_work);
	// INIT_DELAYED_WORK(&bcdev->adsp_track_notify_work, adsp_track_notification_handler);
	INIT_DELAYED_WORK(&chip->check_pd_svooc_work, oplus_chg_check_pd_svooc_work);


	match = of_match_device(oplus_battmgr_of_variants, dev->parent);
	if (match)
		bcdev->variant = (unsigned long)match->data;
	else
		bcdev->variant = OPLUS_BATTMGR_SM8650;

	if (bcdev->variant == OPLUS_BATTMGR_SM8750)
		bcdev->soccp_support = true;

	// ret = qcom_battmgr_charge_control_thresholds_init(battmgr);
	// if (ret < 0)
	// 	return dev_err_probe(dev, ret,
	// 			     "failed to init battery charge control thresholds\n");

	battmgr->bat_psy = devm_power_supply_register(dev, &battery_psy_desc, &psy_cfg);
	if (IS_ERR(battmgr->bat_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->bat_psy),
				     "failed to register battery power supply\n");

	battmgr->usb_psy = devm_power_supply_register(dev, &usb_psy_desc, &psy_cfg_supply);
	if (IS_ERR(battmgr->usb_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->usb_psy),
				     "failed to register USB power supply\n");

	if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8450)
		psy_desc = &sm8450_wls_psy_desc;
	else
		psy_desc = &adsp_wls_psy_desc;

	battmgr->wls_psy = devm_power_supply_register(dev, psy_desc, &psy_cfg_supply);
	if (IS_ERR(battmgr->wls_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->wls_psy),
				     "failed to register wireless charing power supply\n");

	ret = devm_work_autocancel(dev, &battmgr->enable_work,
				   oplus_battmgr_enable_worker);
	if (ret)
		return ret;

	battmgr->client = devm_pmic_glink_client_alloc(dev, PMIC_GLINK_OWNER_BATTMGR,
						       oplus_battmgr_callback,
						       qcom_battmgr_pdr_notify,
						       battmgr);
	if (IS_ERR(battmgr->client))
		return PTR_ERR(battmgr->client);

	pmic_glink_client_register(battmgr->client);

	return 0;
}

static const struct auxiliary_device_id oplus_battmgr_id_table[] = {
	{ .name = "pmic_glink.oplus-power-supply", },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, oplus_battmgr_id_table);

static struct auxiliary_driver oplus_battmgr_driver = {
	.name = "oplus_pmic_glink_power_supply",
	.probe = oplus_battmgr_probe,
	.id_table = oplus_battmgr_id_table,
};

module_auxiliary_driver(oplus_battmgr_driver);

MODULE_DESCRIPTION("OnePlus PMIC GLINK battery manager driver");
MODULE_LICENSE("GPL");
