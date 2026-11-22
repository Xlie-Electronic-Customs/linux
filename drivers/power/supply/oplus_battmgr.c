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

static const int usb_prop_map[];


static int write_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id, u32 val)
{
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	int ret;

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, pst->opcode_set, prop_id, val);
	mutex_unlock(&battmgr->lock);

	return ret;
}

static int read_property_id(struct battery_chg_dev *bcdev,
			struct psy_state *pst, u32 prop_id)
{
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	int ret;

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, pst->opcode_get, prop_id, 0);
	mutex_unlock(&battmgr->lock);

	return ret;
}

static int oem_read_buffer(struct battery_chg_dev *bcdev)
{
	struct qcom_battmgr *battmgr = bcdev->battmgr;

	struct oem_read_buffer_req_msg req_msg = {
		.data_size = sizeof(bcdev->read_buffer_dump.data_buffer),
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_BATTMGR),
		.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP),
		.hdr.opcode = cpu_to_le32(OEM_OPCODE_READ_BUFFER),
	};

	pr_err("oem_read_buffer\n");

	return qcom_battmgr_request(battmgr, &req_msg, sizeof(req_msg));
}

static void handle_oem_read_buffer(struct battery_chg_dev *bcdev,
	const struct oem_read_buffer_resp_msg *resp_msg, size_t len)
{
	u32 buf_len;

	/*pr_err("correct length received: %zu expected: %u\n", len,
		sizeof(bcdev->read_buffer_dump));*/

	if (len > sizeof(bcdev->read_buffer_dump)) {
		pr_err("Incorrect length received: %zu expected: %zu\n", len,
		sizeof(bcdev->read_buffer_dump));
		return;
	}

	buf_len = resp_msg->data_size;
	if (buf_len > sizeof(bcdev->read_buffer_dump.data_buffer)) {
		pr_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}

	/*pr_err("buf length: %u\n", buf_len);*/
	if (buf_len == 0) {
		pr_err("Incorrect buffer length: %u\n", buf_len);
		return;
	}
	memcpy(bcdev->read_buffer_dump.data_buffer, resp_msg->data_buffer, buf_len);
	complete(&bcdev->oem_read_ack);

	printk(KERN_ERR "%s : ----temp[%d], current[%d], vol[%d], soc[%d], rm[%d], chg_cyc[%d], fcc[%d], cc[%d], soh[%d], \
		suspend[%d], oplus_UsbCommCapable[%d], oplus_pd_svooc[%d], typec_mode[%d], vol_min[%d], something[%d]", __func__,
		bcdev->read_buffer_dump.data_buffer[0], bcdev->read_buffer_dump.data_buffer[1], bcdev->read_buffer_dump.data_buffer[2],
		bcdev->read_buffer_dump.data_buffer[3], bcdev->read_buffer_dump.data_buffer[4], bcdev->read_buffer_dump.data_buffer[5],
		bcdev->read_buffer_dump.data_buffer[6], bcdev->read_buffer_dump.data_buffer[7], bcdev->read_buffer_dump.data_buffer[8],
		bcdev->read_buffer_dump.data_buffer[9], bcdev->read_buffer_dump.data_buffer[10], bcdev->read_buffer_dump.data_buffer[11],
		bcdev->read_buffer_dump.data_buffer[12], bcdev->read_buffer_dump.data_buffer[13], bcdev->read_buffer_dump.data_buffer[14]);
}

static int oplus_ap_init_adsp_gague(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_ADSP_GAUGE_INIT, 1);
	if (rc)
		chg_err("init adsp gague fail, rc=%d\n", rc);
	else
		chg_err("init adsp gague sucess.");

	return rc;
}

static int smbchg_charging_enable(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	mutex_lock(&bcdev->chg_en_lock);
	rc = write_property_id(bcdev, pst, BATT_CHG_EN, 1);
	if (rc)
		chg_err("set enable charging fail, rc=%d\n", rc);
	else {
		bcdev->chg_en = true;
		chg_err("set enable charging sucess.");
	}
	mutex_unlock(&bcdev->chg_en_lock);

	return rc;
}

static int oplus_adsp_voocphy_enable(struct battery_chg_dev *bcdev, bool enable)
{
	int rc = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	rc = write_property_id(bcdev, pst, USB_VOOCPHY_ENABLE, enable);
	if (rc) {
		chg_err("set enable adsp voocphy fail, rc=%d\n", rc);
	} else {
		chg_err("set enable adsp voocphy success, rc=%d\n", rc);
	}

	return rc;
}

static bool fg_bq27541_get_battery_authenticate(struct battery_chg_dev *bcdev)
{
	int rc = 0;

	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = read_property_id(bcdev, pst, BATT_BATTERY_AUTH);
	if (rc < 0) {
		chg_err("read battery auth fail, rc=%d\n", rc);
		return false;
	}
	chg_err("read battery auth success, auth=%d\n", pst->prop[BATT_BATTERY_AUTH]);

	return pst->prop[BATT_BATTERY_AUTH];
}

static int qpnp_get_prop_charger_voltage_now(struct battery_chg_dev *bcdev)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int ret;

	ret = read_property_id(bcdev, pst, USB_VOLT_NOW);
	if (ret) {
		chg_err("read usb vbus_volt fail, ret=%d\n", ret);
		return 0;
	}

	return pst->prop[USB_VOLT_NOW];
}

static int smbchg_lcm_en(struct battery_chg_dev *bcdev, int en)
{
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int ret;

	ret = write_property_id(bcdev, pst, USB_POWER_SUPPLY_RELEASE_FIXED_FREQUENCE, en);
	if (ret)
		pr_err("set lcm to %u error, ret=%d\n", en, ret);
	else
		pr_info("set lcm to %d \n", en);

	return ret;
}

static int smbchg_usb_suspend_enable(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	int prop_id = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = usb_prop_map[POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT];
	rc = write_property_id(bcdev, pst, prop_id, 0);
	if (rc)
		chg_err("set suspend fail, rc=%d\n", rc);
	else
		chg_err("set chg suspend\n");

	return rc;
}

static int smbchg_usb_suspend_disable(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	int prop_id = 0;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	prop_id = usb_prop_map[POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT];
	rc = write_property_id(bcdev, pst, prop_id, 0xFFFFFFFF);
	if (rc)
		chg_err("set unsuspend to fail, rc=%d\n", rc);
	else
		chg_err("set chg unsuspend\n");

	return rc;
}

static int smbchg_set_fastchg_current_raw(struct battery_chg_dev *bcdev, int current_ma)
{
	int rc = 0;

	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

	rc = write_property_id(bcdev, pst, BATT_CHG_CTRL_LIM, current_ma * 1000);
	if (rc)
		chg_err("set fcc to %d mA fail, rc=%d\n", current_ma, rc);
	else
		chg_err("set fcc to %d mA\n", current_ma);

	return rc;
}

static int opchg_get_charger_type(struct battery_chg_dev *bcdev)
{
	int rc = 0;
	int prop_id = 0;
	static int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	struct oplus_chg_chip *chip = bcdev->chip;;

	if (!chip) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	}

	if (!chip->charger_exist) {
		return POWER_SUPPLY_TYPE_UNKNOWN;
	}


	prop_id = usb_prop_map[POWER_SUPPLY_PROP_USB_TYPE];
	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0) {
		chg_err("read usb charger_type fail, rc=%d\n", rc);
		goto get_type_done;
	}
	switch (pst->prop[prop_id]) {
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

static void oplus_adsp_voocphy_turn_off(struct battery_chg_dev *bcdev)
{
	struct oplus_voocphy_manager *chip = bcdev->voocphy_chip;

	if (!chip)
		return;

	chg_info("oplus_adsp_voocphy_turn_off\n");

	chip->fastchg_dummy_start = false;
	chip->fastchg_ing = false;
	chip->fastchg_start = false;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm = false;
	chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;

	if (bcdev->chip->stop_chg == 0)
		smbchg_usb_suspend_enable(bcdev);
	else
		smbchg_usb_suspend_disable(bcdev);
}

static void oplus_first_enable_adsp_voocphy(struct battery_chg_dev *bcdev)
{/*
	if (!g_charger_chip || (oplus_chg_get_voocphy_support() != ADSP_VOOCPHY))
		return;*/

	if (!bcdev->chip->first_enabled_adspvoocphy) {
		bcdev->chip->first_enabled_adspvoocphy = true;
		oplus_adsp_voocphy_turn_off(bcdev);
		smbchg_usb_suspend_enable(bcdev);
		msleep(1500);
		smbchg_usb_suspend_disable(bcdev);
		return;
	}
}

static void oplus_adsp_voocphy_clear_status(struct oplus_voocphy_manager *voocphy_chip)
{
	if (!voocphy_chip)
		return;

	voocphy_chip->fastchg_dummy_start = false;
	voocphy_chip->btb_temp_over = false;
	voocphy_chip->vbatt_ovp_status = false;
	voocphy_chip->fastchg_ing = false;
	voocphy_chip->fastchg_start = false;
	voocphy_chip->fastchg_to_normal = false;
	voocphy_chip->fastchg_to_warm = false;
	voocphy_chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
}


static int oplus_chg_update_slow(struct oplus_chg_chip *chip)
{
	int ibat_sum = 0;
	int ibat_average;
	bool slow = 0;
	int i;

	for (i = 0; i < (ARRAY_SIZE(chip->ibat_save) - 1); i++) {
		chip->ibat_save[i] = chip->ibat_save[i + 1];
		ibat_sum += chip->ibat_save[i];
	}
	// if (oplus_switching_support_parallel_chg() && chip->sub_batt_temperature != FG_I2C_ERROR) {
	// 	chip->ibat_save[i] = chip->icharging + chip->sub_batt_icharging;
	// } else
		chip->ibat_save[i] = chip->icharging;
	ibat_sum += chip->ibat_save[i];
	ibat_average = ibat_sum / ARRAY_SIZE(chip->ibat_save);


	slow = !(/*(chip->wireless_support && oplus_wpc_get_wireless_charge_start() == true) || */ chip->charger_exist ||
		 (ibat_average >= 500) || (chip->soc <= 15) || (chip->tbatt_temp >= 500) || (chip->soc >= 90));

	pr_debug("ibat_average = %d, slow = %d\n", ibat_average, slow);

	return slow ? OPLUS_CHG_UPDATE_NO_CHARGE_INTERVAL_SEC : OPLUS_CHG_UPDATE_INTERVAL_SEC;
}

static void lcm_frequency_ctrl(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork, struct battery_chg_dev, ctrl_lcm_frequency);
	static int lcm_en_flag = LCM_EN_DEAFULT;
	struct oplus_chg_chip *chip = bcdev->chip;
	static int check_count = 0;

	if (!chip)
		return;

	check_count++;
	if (check_count > LCM_CHECK_COUNT) {
		lcm_en_flag = LCM_EN_DEAFULT;
		check_count = 0;
	}

	if (qpnp_get_prop_charger_voltage_now(bcdev) > 2500000 /* || oplus_chg_is_wls_present() */) {
		if (chip->sw_full /* || oplus_pps_get_ffc_started() == true */ || chip->hw_full_by_sw || !bcdev->chg_en) {
			if (lcm_en_flag != LCM_EN_ENABLE) {
				lcm_en_flag = LCM_EN_ENABLE;
				smbchg_lcm_en(bcdev, true);
				pr_info("lcm_en_flag:%d\n", lcm_en_flag);
			}
		} else {
			if (lcm_en_flag != LCM_EN_DISABLE) {
				lcm_en_flag = LCM_EN_DISABLE;
				smbchg_lcm_en(bcdev, false);
				pr_info(" lcm_en_flag:%d\n", lcm_en_flag);
			}
		}

		mod_delayed_work(system_highpri_wq, &bcdev->ctrl_lcm_frequency,
						 5000);
	} else {
			if (lcm_en_flag != LCM_EN_ENABLE) {
				lcm_en_flag = LCM_EN_ENABLE;
				smbchg_lcm_en(bcdev, true);
				pr_info(" lcm_en_flag:%d\n", lcm_en_flag);
			}
	}
}

static void oplus_chg_update_work(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork, struct battery_chg_dev, update_work);
	struct oplus_chg_chip *chip = bcdev->chip;
	static bool charge_cfg = false;
	int rc, adap_type, online, ret;
	int icl_ua, fcc_ua;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];

	if (bcdev->usb_in_status && chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN)
		chip->charger_exist = true;
	else {
		chip->charger_exist = false;
		chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	}

	msleep(1000);

	online = pst->prop[USB_ONLINE];



	ret = read_property_id(bcdev, pst, USB_ADAP_TYPE);
	if (ret) {
		chg_err("read usb adap type fail, ret=%d\n", ret);
		return;
	}
	adap_type = pst->prop[USB_ADAP_TYPE];

	chg_err("ADAP TYPE: %d\n", adap_type);


	if (!charge_cfg && online) {

		switch (adap_type) {
                case POWER_SUPPLY_USB_TYPE_PD:
                case POWER_SUPPLY_USB_TYPE_PD_DRP:
                case POWER_SUPPLY_USB_TYPE_PD_PPS:
                case POWER_SUPPLY_USB_TYPE_PD_SDP:
                        chip->pd_volt = PDO_9V;
                        icl_ua = PDO_3A;
                        fcc_ua = PDO_3A;
                        chg_err("oplus: PD/PPS adapter detected (type=%d), requesting 9V/3A\n",
                                 adap_type);
			rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY], BATT_SET_PDO, chip->pd_volt);
			if (rc)
				chg_err("set PDO 9V fail, rc=%d\n", rc);
			else
				chg_err("set PDO 9V OK\n");
                        break;
                case POWER_SUPPLY_USB_TYPE_DCP:
                        icl_ua = 7300;
                        fcc_ua = 7300;
                        chg_err("oplus: DCP adapter detected, setting 7.3A ICL (VOOC/SuperVOOC)\n");
                        break;
                case POWER_SUPPLY_USB_TYPE_CDP:
                        icl_ua = 1500;
                        fcc_ua = 1500;
                        chg_err("oplus: CDP adapter detected, setting 1.5A ICL\n");
                        break;
                case POWER_SUPPLY_USB_TYPE_C:
                        icl_ua = 1500;
                        fcc_ua = 2000;
                        chg_err("oplus: Type-C adapter detected, setting 1.5A ICL\n");
                        break;
                case POWER_SUPPLY_USB_TYPE_SDP:
                default:
                        icl_ua = 500;
                        fcc_ua = 500;
                        chg_err("oplus: SDP/unknown adapter (type=%d), using defaults\n",
                                 adap_type);
                        break;
                }

                rc = write_property_id(bcdev, pst, USB_INPUT_CURR_LIMIT, icl_ua * 1000);
		if (rc)
			chg_err("set pd icl to %d mA fail, rc=%d\n", icl_ua, rc);
		else
			chg_err("set pd icl to %d mA\n", icl_ua);

		smbchg_charging_enable(bcdev);

		if (adap_type == POWER_SUPPLY_USB_TYPE_DCP)
			oplus_adsp_voocphy_enable(bcdev, true);
		else
			smbchg_set_fastchg_current_raw(bcdev, fcc_ua);

		charge_cfg = true;
	}
	else if (charge_cfg && !online)
		charge_cfg = false;



	oem_read_buffer(bcdev);


	// oplus_charger_detect_check(chip);
	// oplus_chg_get_battery_data(chip);
	// oplus_check_battery_vol_diff(chip);
	// if (chip->charger_exist) {
	// 	oplus_chg_aicl_check(chip);
	// 	oplus_chg_protection_check(chip);
	// 	oplus_chg_check_tbatt_normal_status(chip);
	// 	oplus_chg_check_status_full(chip);
	// 	oplus_chg_battery_notify_check(chip);
	// 	oplus_comm_check_fgreset(chip);
	// 	oplus_chg_pd_config(chip);
	// } else {
	// 	oplus_gauge_bqfs_data_check();
	// 	aicl_delay_count = 0;
	// }
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
	/* run again after interval */
	if (timer_pending(&bcdev->update_work.timer) && !delayed_work_pending(&bcdev->update_work)) {
		mod_delayed_work(system_wq, &bcdev->update_work,
				 OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	} else {
		schedule_delayed_work(&bcdev->update_work, OPLUS_CHG_UPDATE_INTERVAL(oplus_chg_update_slow(chip)));
	}
}

#define MAX_VBUS_CHECK_COUNTS			4
#define VOLTAGE_800MV				800
#define GET_INFO_FROMADS_MAXIMIT		3

static void oplus_plugin_irq_work(struct work_struct *work)
{
	int rc = 0;
	struct delayed_work *dwork = to_delayed_work(work);
	struct battery_chg_dev *bcdev = container_of(dwork, struct battery_chg_dev, plugin_irq_work);
	struct qcom_battmgr *battmgr = bcdev->battmgr;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];;
	struct oplus_chg_chip *chip = bcdev->chip;
	struct oplus_voocphy_manager *voocphy_chip = bcdev->voocphy_chip;
	static bool usb_pre_plugin_status = false, usb_plugin_status = false;

	if (!chip) {
		chg_err("chip is NULL!\n");
		return;
	}

	bcdev->plugin_already_run = true;

	rc = read_property_id(bcdev, pst, USB_IN_STATUS);
	if (rc) {
		bcdev->usb_in_status = false;
		chg_err("read usb_in_status fail, rc=%d\n", rc);
		return;
	}
	if (pst->prop[USB_IN_STATUS] > 0) {
		bcdev->usb_in_status = 1;
	} else {
		bcdev->usb_in_status = 0;
		bcdev->pd_svooc = false;
	}
	usb_plugin_status = pst->prop[USB_IN_STATUS] & 0xff;

	voocphy_chip->adspvoocphy_fastchg_start = false;
	chg_err("!!!prop[%d], usb_online[%d]\n", pst->prop[USB_IN_STATUS], bcdev->usb_in_status);
	// if (oplus_voocphy_get_bidirect_cp_support()) {
	// 	chg_err("is_abnormal_adapter[%d]\n", chip->is_abnormal_adapter);
	// 	oplus_chg_check_break(usb_plugin_status);
	// 	if (!bcdev->usb_in_status) {
	// 		if (oplus_vooc_get_fastchg_started() == true) {
	// 			chg_err("!!!Air charging happen,need check charger out\n");
	// 			schedule_delayed_work(&bcdev->check_charger_out_work,
	// 						round_jiffies_relative(msecs_to_jiffies(1500)));
	// 		} else {
	// 			schedule_delayed_work(&bcdev->check_charger_out_work,
	// 						round_jiffies_relative(msecs_to_jiffies(3000)));
	// 		}
	// 	}
	// }
	bcdev->real_chg_type = POWER_SUPPLY_TYPE_UNKNOWN;

	mod_delayed_work(system_highpri_wq, &bcdev->ctrl_lcm_frequency, 50);

	if (battmgr->usb_psy) {
		if (bcdev->usb_in_status == 1 && chip->charger_type == POWER_SUPPLY_TYPE_WIRELESS)
				chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		else if (!bcdev->usb_in_status && chip->charger_volt < 3500)
                                       bcdev->pd_svooc = false; //remove svooc flag
	}

	chg_err("!!!usb_pre_plugin_status[%d], usb_plugin_status[%d]\n", usb_pre_plugin_status, usb_plugin_status);
	if (usb_pre_plugin_status != usb_plugin_status || !usb_plugin_status) {
		if (usb_plugin_status) {
			chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
			chip->real_charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
		}
		mod_delayed_work(system_wq, &bcdev->update_work, 0);
	}
	usb_pre_plugin_status = usb_plugin_status;

	if (bcdev->usb_in_status  == 1) {
		if (chip->usbtemp_wq_init_finished) {
			chip->usbtemp_check = true;
			// oplus_wake_up_usbtemp_thread();
		}
		// oplus_pps_set_power(OPLUS_PPS_POWER_CLR, 0, 0);
		// oplus_pps_hardware_init();
	} else {
		chip->usbtemp_check = false;

		// oplus_pps_set_power(OPLUS_PPS_POWER_CLR, 0, 0);

		bcdev->pd_svooc = false;
		// oplus_pps_stop_disconnect();
		// oplus_pps_variables_reset(true);
		bcdev->hvdcp_detach_time = cpu_clock(smp_processor_id()) / CPU_CLOCK_TIME_MS;
		printk(KERN_ERR "!!! %s: the hvdcp_detach_time:%llu, detect time %llu \n", __func__, bcdev->hvdcp_detach_time, bcdev->hvdcp_detect_time);
		if (bcdev->hvdcp_detach_time - bcdev->hvdcp_detect_time <= OPLUS_HVDCP_DETECT_TO_DETACH_TIME) {
			bcdev->hvdcp_disable = true;
			schedule_delayed_work(&bcdev->hvdcp_disable_work, OPLUS_HVDCP_DISABLE_INTERVAL);
		} else {
			bcdev->hvdcp_detect_ok = false;
			bcdev->hvdcp_detect_time = 0;
			bcdev->hvdcp_disable = false;
		}
		bcdev->adsp_voocphy_err_check = false;
		bcdev->pd_type_checked = false;
		// cancel_delayed_work_sync(&bcdev->pd_type_check_work);
		// cancel_delayed_work_sync(&bcdev->adsp_voocphy_err_work);
	}
	printk(KERN_ERR "!!!pd_svooc[%d]\n", bcdev->pd_svooc);
}

static const int battery_prop_map[] = {
	[POWER_SUPPLY_PROP_STATUS]			= BATT_STATUS,
	[POWER_SUPPLY_PROP_HEALTH]			= BATT_HEALTH,
	[POWER_SUPPLY_PROP_PRESENT]			= BATT_PRESENT,
	[POWER_SUPPLY_PROP_CHARGE_TYPE]			= BATT_CHG_TYPE,
	[POWER_SUPPLY_PROP_CAPACITY]			= BATT_CAPACITY,
	[POWER_SUPPLY_PROP_VOLTAGE_OCV]			= BATT_VOLT_OCV,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW]			= BATT_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX]			= BATT_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW]			= BATT_CURR_NOW,
	[POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT]	= BATT_CHG_CTRL_LIM,
	[POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX]	= BATT_CHG_CTRL_LIM_MAX,
	[POWER_SUPPLY_PROP_TEMP]			= BATT_TEMP,
	[POWER_SUPPLY_PROP_TECHNOLOGY]			= BATT_TECHNOLOGY,
	[POWER_SUPPLY_PROP_CHARGE_COUNTER]		= BATT_CHG_COUNTER,
	[POWER_SUPPLY_PROP_CYCLE_COUNT]			= BATT_CYCLE_COUNT,
	[POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN]		= BATT_CHG_FULL_DESIGN,
	[POWER_SUPPLY_PROP_CHARGE_FULL]			= BATT_CHG_FULL,
	[POWER_SUPPLY_PROP_MODEL_NAME]			= BATT_MODEL_NAME,
	[POWER_SUPPLY_PROP_TIME_TO_FULL_AVG]		= BATT_TTF_AVG,
	[POWER_SUPPLY_PROP_TIME_TO_EMPTY_AVG]		= BATT_TTE_AVG,
	[POWER_SUPPLY_PROP_POWER_NOW]			= BATT_POWER_NOW,
	[POWER_SUPPLY_PROP_POWER_AVG]			= BATT_POWER_AVG,
};

static int __battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					u32 fcc_ua)
{
	int rc;

	if (bcdev->restrict_chg_en) {
		fcc_ua = min_t(u32, fcc_ua, bcdev->restrict_fcc_ua);
		fcc_ua = min_t(u32, fcc_ua, bcdev->thermal_fcc_ua);
	}

	rc = write_property_id(bcdev, &bcdev->psy_list[PSY_TYPE_BATTERY],
				BATT_CHG_CTRL_LIM, fcc_ua);
	if (rc < 0)
		chg_err("Failed to set FCC %u, rc=%d\n", fcc_ua, rc);
	else
		pr_debug("Set FCC to %u uA\n", fcc_ua);

	return rc;
}

static int battery_psy_set_charge_current(struct battery_chg_dev *bcdev,
					int val)
{
	int rc;
	u32 fcc_ua, prev_fcc_ua;

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

	rc = __battery_psy_set_charge_current(bcdev, fcc_ua);
	if (!rc)
		bcdev->curr_thermal_level = val;
	else
		bcdev->thermal_fcc_ua = prev_fcc_ua;

	return rc;
}

static int battery_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	struct oplus_chg_chip *chip = bcdev->chip;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
	struct psy_state *pst_usb = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc;

	pval->intval = -ENODATA;

	prop_id = pst->map[prop];
	if (prop_id < 0)
		return prop_id;
	if (bcdev->soccp_support)
		return 0;
	else
		rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	switch (prop) {
	case POWER_SUPPLY_PROP_MODEL_NAME:
		pval->strval = pst->model;
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (bcdev->read_buffer_dump.data_buffer[3]) {
			chip->soc = DIV_ROUND_CLOSEST(bcdev->read_buffer_dump.data_buffer[3], 100);
			pval->intval = chip->soc;
		} else
			pval->intval = pst->prop[prop_id];
		break;
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		pval->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		if (bcdev->read_buffer_dump.data_buffer[3] == 0)
			pval->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		chip->tbatt_temp = (int)bcdev->read_buffer_dump.data_buffer[0];
		pval->intval = chip->tbatt_temp;
		break;
	case POWER_SUPPLY_PROP_STATUS:
		rc = read_property_id(bcdev, pst_usb, USB_ONLINE);
		if (rc < 0)
			return rc;

		if (pst_usb->prop[USB_ONLINE] && chip->soc >= 100)
			pval->intval = POWER_SUPPLY_STATUS_FULL;
		// else if (pst_usb->prop[USB_IN_STATUS] && !fg_bq27541_get_battery_authenticate(bcdev))
		// 	pval->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		else if (pst_usb->prop[USB_ONLINE])
			pval->intval = POWER_SUPPLY_STATUS_CHARGING;
		else
			pval->intval = POWER_SUPPLY_STATUS_DISCHARGING;
		break;
	// case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
	// 	pval->intval = bcdev->curr_thermal_level;
	// 	break;
	// case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
	// 	pval->intval = bcdev->num_thermal_levels;
	// 	break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (!bcdev->usb_in_status)
			chip->icharging = (int)bcdev->read_buffer_dump.data_buffer[1];
		else
			chip->icharging = (int)pst->prop[prop_id];
		pval->intval = -chip->icharging;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if ((int)bcdev->read_buffer_dump.data_buffer[2])
			pval->intval = (int)bcdev->read_buffer_dump.data_buffer[2];
		else
			pval->intval = pst->prop[prop_id];
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		if ((int)bcdev->read_buffer_dump.data_buffer[5])
			pval->intval = (int)bcdev->read_buffer_dump.data_buffer[5];
		else
			pval->intval = pst->prop[prop_id];
		break;
	default:
		pval->intval = pst->prop[prop_id];
	 	break;
	}

	return rc;
}

static int battery_psy_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *pval)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return battery_psy_set_charge_current(battmgr->bcdev, pval->intval);
	default:
		return -EINVAL;
	}

	return 0;
}

static int battery_psy_prop_is_writeable(struct power_supply *psy,
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

static const struct power_supply_desc batt_psy_desc = {
	.name			= "battery",
	.type			= POWER_SUPPLY_TYPE_BATTERY,
	.properties		= battery_props,
	.num_properties		= ARRAY_SIZE(battery_props),
	.get_property		= battery_psy_get_prop,
	.set_property		= battery_psy_set_prop,
	.property_is_writeable	= battery_psy_prop_is_writeable,
};


static const int usb_prop_map[] = {
	[POWER_SUPPLY_PROP_ONLINE] = USB_ONLINE,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = USB_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = USB_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = USB_CURR_NOW,
	[POWER_SUPPLY_PROP_CURRENT_MAX] = USB_CURR_MAX,
	[POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT] = USB_INPUT_CURR_LIMIT,
	[POWER_SUPPLY_PROP_USB_TYPE] = USB_ADAP_TYPE,
	[POWER_SUPPLY_PROP_TEMP] = USB_TEMP,
};

static int usb_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_USB];
	int prop_id, rc;

	pval->intval = -ENODATA;

	// prop_id = get_property_id(pst, prop);
	prop_id = pst->map[prop];
	if (prop_id < 0)
		return prop_id;

	if (bcdev->soccp_support)
		return 0;
	else
		rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	if (prop_id == USB_ONLINE) {
		mod_delayed_work(system_wq, &bcdev->update_work, 0);
		bcdev->usb_in_status = pst->prop[prop_id];
	}

	pval->intval = pst->prop[prop_id];

	return 0;
}

static enum power_supply_property usb_props[] = {
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
	.name			= "usb",
	.type			= POWER_SUPPLY_TYPE_USB,
	.properties		= usb_props,
	.num_properties		= ARRAY_SIZE(usb_props),
	.get_property		= usb_psy_get_prop,
	.usb_types		= BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN) |
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

static const int sm8350_wls_prop_map[] = {
	[POWER_SUPPLY_PROP_ONLINE] = WLS_ONLINE,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW]	= WLS_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX]	= WLS_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW]	= WLS_CURR_NOW,
	[POWER_SUPPLY_PROP_CURRENT_MAX] = WLS_CURR_MAX,
};

static int wls_psy_get_prop(struct power_supply *psy,
		enum power_supply_property prop,
		union power_supply_propval *pval)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	struct psy_state *pst = &bcdev->psy_list[PSY_TYPE_WLS];
	int prop_id, rc;

	pval->intval = -ENODATA;

	// prop_id = get_property_id(pst, prop);
	prop_id = pst->map[prop];
	if (prop_id < 0)
		return prop_id;

	rc = read_property_id(bcdev, pst, prop_id);
	if (rc < 0)
		return rc;

	pval->intval = pst->prop[prop_id];

	return 0;
}

static enum power_supply_property sm8350_wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc sm8350_wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_WIRELESS,
	.properties		= sm8350_wls_props,
	.num_properties		= ARRAY_SIZE(sm8350_wls_props),
	.get_property		= wls_psy_get_prop,
};

static const int sm8550_wls_prop_map[WLS_PROP_MAX] = {
	[WLS_ONLINE]		= POWER_SUPPLY_PROP_ONLINE,
	[WLS_VOLT_NOW]		= POWER_SUPPLY_PROP_VOLTAGE_NOW,
	[WLS_VOLT_MAX]		= POWER_SUPPLY_PROP_VOLTAGE_MAX,
	[WLS_CURR_NOW]		= POWER_SUPPLY_PROP_CURRENT_NOW,
	[WLS_CURR_MAX]		= POWER_SUPPLY_PROP_CURRENT_MAX,
	[WLS_INPUT_CURR_LIMIT]	= POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	[WLS_CONN_TEMP]		= POWER_SUPPLY_PROP_TEMP,
};

static enum power_supply_property sm8550_wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_PRESENT,
};

static const struct power_supply_desc sm8550_wls_psy_desc = {
	.name			= "wireless",
	.type			= POWER_SUPPLY_TYPE_WIRELESS,
	.properties		= sm8550_wls_props,
	.num_properties		= ARRAY_SIZE(sm8550_wls_props),
	.get_property		= wls_psy_get_prop,
};

static const int oplus_prop_map[OPLUS_PROPERTY_MAX];

static void handle_notification(struct qcom_battmgr *battmgr, const struct qcom_battmgr_message *msg,
				int len)
{
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	int chg_type;
	int sub_chg_type;
	size_t payload_len = len - sizeof(struct pmic_glink_hdr);
	unsigned int notification;
	struct psy_state *pst = NULL;


	if (payload_len != sizeof(msg->notification)) {
		dev_warn(battmgr->dev, "Incorrect response length %u\n", len);
		return;
	}

	notification = le32_to_cpu(msg->notification);
	notification &= 0xff;
	switch (notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		// schedule_delayed_work(&bcdev->usb_type_work, 0);
		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		break;
	case BC_PD_SVOOC:
		printk(KERN_ERR "!!!:%s, should set pd_svooc\n", __func__);
		bcdev->pd_svooc = true;
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
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		bcdev->otg_online = true;
		bcdev->pd_svooc = false;
		// schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_OTG_DISABLE:
		printk(KERN_ERR "!!!!!disable otg\n");
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		bcdev->otg_online = false;
		// schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_ADSP_NOTIFY_TRACK:
		pr_info("!!!!!adsp track notify\n");
		// schedule_delayed_work(&bcdev->adsp_track_notify_work, 0);
		break;
	case BC_VOOC_VBUS_ADC_ENABLE:
		printk(KERN_ERR "!!!!!vooc_vbus_adc_enable\n");
		bcdev->adsp_voocphy_err_check = true;
		// oplus_adsp_voocphy_set_fastchg_start(true);
		// cancel_delayed_work_sync(&bcdev->adsp_voocphy_err_work);
		// schedule_delayed_work(&bcdev->adsp_voocphy_err_work, msecs_to_jiffies(8500));
		// if (is_ext_chg_ops()) {
		// 	oplus_chg_disable_charge();
		// 	oplus_chg_suspend_charger();/*excute in glink loop for real time*/
		// } else {
		// 	schedule_delayed_work(&bcdev->vbus_adc_enable_work, 0);/*excute in work to avoid glink dead loop*/
		// }
		break;
	case BC_CID_DETECT:
		printk(KERN_ERR "!!!!!cid detect || no detect\n");
		// schedule_delayed_work(&bcdev->cid_status_change_work, 0);
		break;
	case BC_QC_DETECT:
		chg_type = opchg_get_charger_type(bcdev);
		// sub_chg_type = oplus_chg_get_charger_subtype();
		// bcdev->real_chg_type = chg_type | (sub_chg_type << 8);
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
		// schedule_delayed_work(&bcdev->chg_status_send_work, 0);
		break;
	case BC_ADSP_NOTIFY_AP_SUSPEND_CHG:
		printk(KERN_ERR "!!!!!oplus_apsd_notify_ap_suspend_chg\n");
		// oplus_chg_set_adsp_notify_ap_suspend();
		break;
	case BC_PD_SOFT_RESET:
		printk(KERN_ERR "!!!!!PD hard reset happend\n");
		break;
	case PD_SOURCECAP_DONE:
		// schedule_delayed_work(&bcdev->pd_set_aicl_work, 0);
		break;
	case BC_CHG_STATUS_SET:
		// schedule_delayed_work(&bcdev->unsuspend_usb_work, 0);
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
		// oplus_chg_wake_update_work();
		break;
	default:
		break;
	}

	if (pst && pst->psy) {
		/*
		 * For charger mode, keep the device awake at least for 50 ms
		 * so that device won't enter suspend when a non-SDP charger
		 * is removed. This would allow the userspace process like
		 * "charger" to be able to read power supply uevents to take
		 * appropriate actions (e.g. shutting down when the charger is
		 * unplugged).
		 */
// #ifndef OPLUS_FEATURE_CHG_BASIC
		// power_supply_changed(pst->psy);
// #endif
		pm_wakeup_dev_event(bcdev->dev, 50, true);
	}
}

static bool validate_message(struct battery_chg_dev *bcdev, const struct battery_charger_resp_msg *resp_msg,
				size_t len)
{
	if (len != sizeof(*resp_msg)) {
		pr_err("Incorrect response length %zu for opcode %#x\n", len,
			resp_msg->hdr.opcode);
		return false;
	}

	if (resp_msg->ret_code) {
		pr_err("Error in response for opcode %#x prop_id %u, rc=%d\n",
			resp_msg->hdr.opcode, resp_msg->property_id,
			(int)resp_msg->ret_code);
		return false;
	}

	return true;
}

static void handle_message(struct battery_chg_dev *bcdev, const void *data,
				size_t len)
{
	const struct battery_charger_resp_msg *resp_msg = data;
	const struct battery_model_resp_msg *model_resp_msg = data;
	const struct wireless_fw_check_resp *fw_check_msg;
	const struct wireless_fw_push_buf_resp *fw_resp_msg;
	const struct wireless_fw_update_status *fw_update_msg;
	const struct wireless_fw_get_version_resp *fw_ver_msg;
	const struct psy_state *pst;
	bool ack_set = false;

	switch (resp_msg->hdr.opcode) {
	case BC_BATTERY_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_BATTERY];

		/* Handle model response uniquely as it's a string */
		if (pst->model && len == sizeof(*model_resp_msg)) {
			memcpy(pst->model, model_resp_msg->model, MAX_STR_LEN);
			ack_set = true;
			break;
		}

		/* Other response should be of same type as they've u32 value */
		if (validate_message(bcdev, resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_USB_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_USB];
		if (validate_message(bcdev, resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_WLS_STATUS_GET:
		pst = &bcdev->psy_list[PSY_TYPE_WLS];
		if (validate_message(bcdev, resp_msg, len) &&
		    resp_msg->property_id < pst->prop_count) {
			pst->prop[resp_msg->property_id] = resp_msg->value;
			ack_set = true;
		}

		break;
	case BC_BATTERY_STATUS_SET:
	case BC_USB_STATUS_SET:
	case BC_WLS_STATUS_SET:
		if (validate_message(bcdev, data, len))
			ack_set = true;

		break;
	case BC_SET_NOTIFY_REQ:
	case BC_SHUTDOWN_NOTIFY:
		/* Always ACK response for notify request */
		ack_set = true;
		break;
	case BC_WLS_FW_CHECK_UPDATE:
		if (len == sizeof(*fw_check_msg)) {
			fw_check_msg = data;
			if (fw_check_msg->ret_code == 1)
				bcdev->wls_fw_update_reqd = true;
			ack_set = true;
		} else {
			chg_err("Incorrect response length %zu for wls_fw_check_update\n",
				len);
		}
		break;
	case BC_WLS_FW_PUSH_BUF_RESP:
		if (len == sizeof(*fw_resp_msg)) {
			fw_resp_msg = data;
			if (fw_resp_msg->fw_update_status == 1)
				complete(&bcdev->fw_buf_ack);
		} else {
			chg_err("Incorrect response length %zu for wls_fw_push_buf_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_UPDATE_STATUS_RESP:
		if (len == sizeof(*fw_update_msg)) {
			fw_update_msg = data;
			if (fw_update_msg->fw_update_done == 1)
				complete(&bcdev->fw_update_ack);
		} else {
			chg_err("Incorrect response length %zu for wls_fw_update_status_resp\n",
				len);
		}
		break;
	case BC_WLS_FW_GET_VERSION:
		if (len == sizeof(*fw_ver_msg)) {
			fw_ver_msg = data;
			bcdev->wls_fw_version = fw_ver_msg->fw_version;
			ack_set = true;
		} else {
			chg_err("Incorrect response length %zu for wls_fw_get_version\n",
				len);
		}
		break;
	default:
		chg_err("Unknown opcode: %u\n", resp_msg->hdr.opcode);
		break;
	}

	if (ack_set || bcdev->error_prop)
		complete(&bcdev->battmgr->ack);
}

static void battery_chg_callback(const void *data, size_t len, void *priv)
{
	const struct pmic_glink_hdr *hdr = data;
	struct qcom_battmgr *battmgr = priv;
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	unsigned int opcode = le32_to_cpu(hdr->opcode);

	if (!bcdev->is_chargepd_ready)
		bcdev->is_chargepd_ready = true;

	switch (opcode) {
		case BC_NOTIFY_IND:
			handle_notification(battmgr, data, len);
			break;
		case OEM_OPCODE_READ_BUFFER:
			handle_oem_read_buffer(bcdev, data, len);
			break;
		// case BCC_OPCODE_READ_BUFFER:
		// 	handle_bcc_read_buffer(bcdev, data, len);
		// 	break;
		// case TRACK_OPCODE_READ_BUFFER:
		// 	if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8450 || battmgr->bcdev->variant == OPLUS_BATTMGR_SM8550)
		// 		handle_adsp_track_read_buffer(bcdev, data, len);
		// 	else
		// 		handle_ufcs_read_buffer(bcdev, data, len);
		// 	break;
		// case PPS_OPCODE_READ_BUFFER:
		// 	handle_pps_read_buffer(bcdev, data, len);
		// 	break;
		// case AP_OPCODE_READ_BUFFER:
		// 	if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8550)
		// 		handle_oem_read_pmic_buffer(bcdev, data, len);
		// 	else
		// 		handle_ap_read_buffer(bcdev, data, len);
		// 	break;
		// case AP_OPCODE_WRITE_BUFFER:
		// 	handle_ap_write_buffer(bcdev, data, len);
		// 	break;
		// case OPLUS_OPCODE_SET_REQ:
		// case OPLUS_OPCODE_GET_REQ:
		// 	oplus_handle_message(bcdev, data, len);
		// 	break;
		// case OPLUS_OPCODE_GET_SINK_MSG:
		// 	chg_info("OPLUS_OPCODE_GET_SINK_MSG handle\n");
		// 	handle_rechg_msg_handle(bcdev, data, len);
		// 	break;
		default:
			handle_message(bcdev, data, len);
	}
}

static void battery_chg_notify_enable(struct qcom_battmgr *battmgr)
{
	struct qcom_battmgr_enable_request req = {
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_BATTMGR),
		.hdr.type = cpu_to_le32(PMIC_GLINK_NOTIFY),
		.hdr.opcode = cpu_to_le32(BC_SET_NOTIFY_REQ),
	};
	int ret;

	ret = qcom_battmgr_request(battmgr, &req, sizeof(req));
	if (ret)
		dev_err(battmgr->dev, "Failed to enable notification ret=%d\n", ret);
}

static void oplus_battmgr_enable_worker(struct work_struct *work)
{
	struct qcom_battmgr *battmgr = container_of(work, struct qcom_battmgr, enable_work);
	struct battery_chg_dev *bcdev = battmgr->bcdev;

	battery_chg_notify_enable(battmgr);
	oplus_ap_init_adsp_gague(bcdev);
	smbchg_charging_enable(bcdev);
	oplus_adsp_voocphy_enable(bcdev, true);
	mod_delayed_work(system_wq, &bcdev->update_work, 0);
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
	struct oplus_chg_chip *chip;
	struct oplus_voocphy_manager *voocphy_chip;
	struct device *dev = &adev->dev;
	int i, ret;

	battmgr = devm_kzalloc(dev, sizeof(*battmgr), GFP_KERNEL);
	if (!battmgr)
		return -ENOMEM;

	battmgr->dev = dev;

	bcdev = devm_kzalloc(dev, sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev)
		return -ENOMEM;

	battmgr->bcdev = bcdev;
	bcdev->battmgr = battmgr;

	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	bcdev->chip = chip;

	voocphy_chip = devm_kzalloc(dev, sizeof(*voocphy_chip), GFP_KERNEL);
	if (!voocphy_chip)
		return -ENOMEM;

	bcdev->voocphy_chip = voocphy_chip;

	psy_cfg.drv_data = battmgr;
	psy_cfg.fwnode = dev_fwnode(&adev->dev);

	psy_cfg_supply.drv_data = battmgr;
	psy_cfg_supply.fwnode = dev_fwnode(&adev->dev);
	psy_cfg_supply.supplied_to = oplus_battmgr_battery;
	psy_cfg_supply.num_supplicants = 1;

	bcdev->psy_list[PSY_TYPE_BATTERY].map = battery_prop_map;
	bcdev->psy_list[PSY_TYPE_BATTERY].prop_count = BATT_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_get = BC_BATTERY_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_BATTERY].opcode_set = BC_BATTERY_STATUS_SET;

	bcdev->psy_list[PSY_TYPE_USB].map = usb_prop_map;
	bcdev->psy_list[PSY_TYPE_USB].prop_count = USB_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_USB].opcode_get = BC_USB_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_USB].opcode_set = BC_USB_STATUS_SET;

	if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8450)
		bcdev->psy_list[PSY_TYPE_WLS].map = sm8350_wls_prop_map;
	else
		bcdev->psy_list[PSY_TYPE_WLS].map = sm8550_wls_prop_map;
	bcdev->psy_list[PSY_TYPE_WLS].prop_count = WLS_PROP_MAX;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_get = BC_WLS_STATUS_GET;
	bcdev->psy_list[PSY_TYPE_WLS].opcode_set = BC_WLS_STATUS_SET;

	bcdev->psy_list[PSY_TYPE_OPLUS].map = oplus_prop_map;
	bcdev->psy_list[PSY_TYPE_OPLUS].prop_count = OPLUS_PROPERTY_MAX;
	bcdev->psy_list[PSY_TYPE_OPLUS].opcode_get = OPLUS_OPCODE_GET_REQ;
	bcdev->psy_list[PSY_TYPE_OPLUS].opcode_set = OPLUS_OPCODE_SET_REQ;

	for (i = 0; i < PSY_TYPE_MAX; i++) {
		bcdev->psy_list[i].prop =
			devm_kcalloc(dev, bcdev->psy_list[i].prop_count,
					sizeof(u32), GFP_KERNEL);
		if (!bcdev->psy_list[i].prop)
			return -ENOMEM;
	}

	bcdev->psy_list[PSY_TYPE_BATTERY].model = devm_kzalloc(dev, MAX_STR_LEN, GFP_KERNEL);
	if (!bcdev->psy_list[PSY_TYPE_BATTERY].model)
		return -ENOMEM;

	init_completion(&bcdev->oem_read_ack);
	mutex_init(&battmgr->lock);
	init_completion(&battmgr->ack);

	// INIT_WORK(&bcdev->subsys_up_work, battery_chg_subsys_up_work);
	// INIT_WORK(&bcdev->usb_type_work, battery_chg_update_usb_type_work);
	// INIT_WORK(&bcdev->plc_status_update_work, oplus_chg_adsp_plc_status_update_work);
 //
	// INIT_WORK(&bcdev->gauge_cali_track_by_plug_work, oplus_plat_gauge_cali_track_by_plug_work);
	// INIT_WORK(&bcdev->gauge_cali_track_by_full_work, oplus_plat_gauge_cali_track_by_full_work);
	// INIT_DELAYED_WORK(&bcdev->adsp_voocphy_status_work, oplus_adsp_voocphy_status_func);
	// INIT_DELAYED_WORK(&bcdev->unsuspend_usb_work, oplus_unsuspend_usb_work);
	// INIT_DELAYED_WORK(&bcdev->otg_init_work, oplus_otg_init_status_func);
	// INIT_DELAYED_WORK(&bcdev->cid_status_change_work, oplus_cid_status_change_work);
	// INIT_DELAYED_WORK(&bcdev->adsp_crash_recover_work, oplus_adsp_crash_recover_func);
	// INIT_DELAYED_WORK(&bcdev->crash_track_work, oplus_crash_track_work);
	// INIT_DELAYED_WORK(&bcdev->voocphy_enable_check_work, oplus_voocphy_enable_check_func);
	// INIT_DELAYED_WORK(&bcdev->otg_vbus_enable_work, otg_notification_handler);
	// INIT_DELAYED_WORK(&bcdev->hvdcp_disable_work, oplus_hvdcp_disable_work);
	// INIT_DELAYED_WORK(&bcdev->pd_only_check_work, oplus_pd_only_check_work);
	// INIT_DELAYED_WORK(&bcdev->otg_status_check_work, oplus_otg_status_check_work);
	// INIT_DELAYED_WORK(&bcdev->vbus_adc_enable_work, oplus_vbus_enable_adc_work);
	// INIT_DELAYED_WORK(&bcdev->oem_lcm_en_check_work, oplus_oem_lcm_en_check_work);
	// INIT_DELAYED_WORK(&bcdev->voocphy_err_work, oplus_voocphy_err_work);
	// INIT_DELAYED_WORK(&bcdev->pd_type_check_work, oplus_pd_type_check_work);
	INIT_DELAYED_WORK(&bcdev->ctrl_lcm_frequency, lcm_frequency_ctrl);
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

	INIT_DELAYED_WORK(&bcdev->update_work, oplus_chg_update_work);

	chip->charger_type = POWER_SUPPLY_TYPE_UNKNOWN;


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

	battmgr->bat_psy = devm_power_supply_register(dev, &batt_psy_desc, &psy_cfg);
	if (IS_ERR(battmgr->bat_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->bat_psy),
				     "failed to register battery power supply\n");

	battmgr->usb_psy = devm_power_supply_register(dev, &usb_psy_desc, &psy_cfg_supply);
	if (IS_ERR(battmgr->usb_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->usb_psy),
				     "failed to register USB power supply\n");

	if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8450)
		psy_desc = &sm8350_wls_psy_desc;
	else
		psy_desc = &sm8550_wls_psy_desc;

	battmgr->wls_psy = devm_power_supply_register(dev, psy_desc, &psy_cfg_supply);
	if (IS_ERR(battmgr->wls_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->wls_psy),
				     "failed to register wireless charing power supply\n");

	ret = devm_work_autocancel(dev, &battmgr->enable_work,
				   oplus_battmgr_enable_worker);
	if (ret)
		return ret;

	battmgr->client = devm_pmic_glink_client_alloc(dev, PMIC_GLINK_OWNER_BATTMGR,
						       battery_chg_callback,
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
