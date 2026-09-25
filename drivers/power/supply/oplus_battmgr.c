// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2026, Nazar Kompanets <xlie7669@gmail.com>
 */

#include <linux/auxiliary_bus.h>
#include <linux/devm-helpers.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include <linux/math.h>
#include <linux/units.h>

#include "qcom_battmgr.h"
#include "oplus_battmgr.h"

static int __battery_psy_set_charge_current(struct qcom_battmgr *battmgr,
					u32 fcc_ua)
{
	struct battery_chg_dev *bcdev = battmgr->bcdev;
	int ret;

	if (bcdev->restrict_chg_en) {
		fcc_ua = min_t(u32, fcc_ua, bcdev->restrict_fcc_ua);
		fcc_ua = min_t(u32, fcc_ua, bcdev->thermal_fcc_ua);
	}

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_BATTERY_STATUS_SET, BATT_CHG_CTRL_LIM, fcc_ua);
	mutex_unlock(&battmgr->lock);

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
	int ret;

	if (psp >= ARRAY_SIZE(bat_prop_map))
		return -EINVAL;

	prop = bat_prop_map[psp];

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_BATTERY_STATUS_GET, prop, 0);
	mutex_unlock(&battmgr->lock);

	return ret;
}

static int battery_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	enum qcom_battmgr_unit unit = battmgr->unit;
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
	case POWER_SUPPLY_PROP_CHARGE_FULL_DESIGN:
		if (unit != QCOM_BATTMGR_UNIT_mAh)
			return -ENODATA;
		val->intval = battmgr->info.design_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		if (unit != QCOM_BATTMGR_UNIT_mAh)
			return -ENODATA;
		val->intval = battmgr->info.last_full_capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_EMPTY:
		if (unit != QCOM_BATTMGR_UNIT_mAh)
			return -ENODATA;
		val->intval = battmgr->info.capacity_low;
		break;
	case POWER_SUPPLY_PROP_CHARGE_NOW:
		if (unit != QCOM_BATTMGR_UNIT_mAh)
			return -ENODATA;
		val->intval = battmgr->status.capacity;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = battmgr->info.charge_count;
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL_DESIGN:
		if (unit != QCOM_BATTMGR_UNIT_mWh)
			return -ENODATA;
		val->intval = battmgr->info.design_capacity;
		break;
	case POWER_SUPPLY_PROP_ENERGY_FULL:
		if (unit != QCOM_BATTMGR_UNIT_mWh)
			return -ENODATA;
		val->intval = battmgr->info.last_full_capacity;
		break;
	case POWER_SUPPLY_PROP_ENERGY_EMPTY:
		if (unit != QCOM_BATTMGR_UNIT_mWh)
			return -ENODATA;
		val->intval = battmgr->info.capacity_low;
		break;
	case POWER_SUPPLY_PROP_ENERGY_NOW:
		if (unit != QCOM_BATTMGR_UNIT_mWh)
			return -ENODATA;
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
	int ret;

	if (psp >= ARRAY_SIZE(usb_prop_map))
		return -EINVAL;

	prop = usb_prop_map[psp];

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_BATTERY_STATUS_GET, prop, 0);
	mutex_unlock(&battmgr->lock);

	return ret;
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

static const struct power_supply_desc usb_psy_desc = {
	.name = "oplus-battmgr-usb",
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
	int ret;


	if (psp >= ARRAY_SIZE(sm8450_wls_prop_map))
		return -EINVAL;

	prop = sm8450_wls_prop_map[psp];

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_WLS_STATUS_GET, prop, 0);
	mutex_unlock(&battmgr->lock);

	return ret;
}

static int oplus_battmgr_wls_adsp_update(struct qcom_battmgr *battmgr,
					  enum power_supply_property psp)
{
	unsigned int prop;
	int ret;


	if (psp >= ARRAY_SIZE(adsp_wls_prop_map))
		return -EINVAL;

	prop = adsp_wls_prop_map[psp];

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_WLS_STATUS_GET, prop, 0);
	mutex_unlock(&battmgr->lock);

	return ret;
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
		power_supply_changed(battmgr->usb_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		// schedule_work(&bcdev->usb_type_work);
		break;
	case BC_WLS_STATUS_GET:
		power_supply_changed(battmgr->wls_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		break;
	case BC_PD_SVOOC:
		bcdev->pd_svooc = true;
		// if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8650 || battmgr->bcdev->variant == OPLUS_BATTMGR_SM8750)
		// 	oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_SVID);
		chg_info("pd_svooc = %d\n", bcdev->pd_svooc);
		break;
	case BC_ABNORMAL_PD_SVOOC_ADAPTER:
		printk(KERN_ERR "!!!:%s, is_abnormal_adapter\n", __func__);
		// g_oplus_chip->is_abnormal_adapter = true;
		break;
	case BC_VOOC_STATUS_GET:
		// schedule_delayed_work(&bcdev->adsp_voocphy_status_work, 0);
		break;
	case BC_OTG_ENABLE:
		chg_info("enable otg\n");
		power_supply_changed(battmgr->usb_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		bcdev->otg_online = true;
		bcdev->pd_svooc = false;
/*
		if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8650 || battmgr->bcdev->variant == OPLUS_BATTMGR_SM8750) {
			ret = oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_OTG_ENABLE);
			if (ret != -EAGAIN && is_usb_psy_available(bcdev))
				power_supply_changed(battmgr->usb_psy);
			else
				schedule_work(&bcdev->wired_otg_enable_work);
		} else
			schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);*/
		break;
	case BC_OTG_DISABLE:
		chg_info("disable otg\n");
		power_supply_changed(battmgr->usb_psy);
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		bcdev->otg_online = false;

		// if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8650 || battmgr->bcdev->variant == OPLUS_BATTMGR_SM8750) {
		// 	oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_OTG_ENABLE);
		// 	schedule_delayed_work(&bcdev->cid_status_change_work, msecs_to_jiffies(800));
		// 	if (is_usb_psy_available(bcdev))
		// 		power_supply_changed(battmgr->usb_psy);
		// 	bcdev->reverse_enable = false;
		// 	oplus_chg_ic_virq_trigger(bcdev->reverse_chg_ic_dev, OPLUS_IC_VIRQ_REVERSE_ENABLE);
		// } else
		// 	schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_ADSP_NOTIFY_TRACK:
		pr_info("!!!!!adsp track notify\n");
		// schedule_delayed_work(&bcdev->adsp_track_notify_work, 0);
		break;
	case BC_VOOC_VBUS_ADC_ENABLE:
		chg_info("BC_VOOC_VBUS_ADC_ENABLE\n");
		bcdev->voocphy_err_check = true;
		// cancel_delayed_work_sync(&bcdev->voocphy_err_work);
		// schedule_delayed_work(&bcdev->voocphy_err_work, msecs_to_jiffies(8500));
		// if (bcdev->is_external_chg) {
		// 	/* excute in glink loop for real time */
		// 	oplus_chg_disable_charger(true, FASTCHG_VOTER);
		// 	oplus_chg_suspend_charger(true, FASTCHG_VOTER);
		// } else {
		// 	/* excute in work to avoid glink dead loop */
		// 	schedule_delayed_work(&bcdev->vbus_adc_enable_work, 0);
		// }
		break;
	case BC_CID_DETECT:
		chg_info("cid detect\n");
		// schedule_delayed_work(&bcdev->cid_status_change_work, 0);
		// if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8650 || battmgr->bcdev->variant == OPLUS_BATTMGR_SM8750)
		// 	oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_CC_DETECT);
		break;
	case BC_QC_DETECT:
		bcdev->hvdcp_detect_ok = true;
		break;
	case BC_TYPEC_STATE_CHANGE:
		// if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8650 || battmgr->bcdev->variant == OPLUS_BATTMGR_SM8750) {
		// 	oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_CC_CHANGED);
		// 	oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_TYPEC_STATE);
		// } else
		// 	schedule_delayed_work(&bcdev->typec_state_change_work, 0);
		break;
	case BC_POWER_ROLE_STATUS:
		chg_info("BC_POWER_ROLE_STATUS\n");
		// oplus_chg_ic_virq_trigger(bcdev->buck_ic, OPLUS_IC_VIRQ_POWER_ROLE_STATUS);
		break;
	case BC_PLUGIN_IRQ:
		chg_info("BC_PLUGIN_IRQ\n");
		// schedule_delayed_work(&bcdev->plugin_irq_work, 0);
		break;
	case BC_APSD_DONE:
		// if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8650 || battmgr->bcdev->variant == OPLUS_BATTMGR_SM8750)
		// 	bcdev->bc12_completed = true;
		// else
		// 	schedule_delayed_work(&bcdev->apsd_done_work, 0);
		chg_info("BC_APSD_DONE\n");
		break;
	case BC_CHG_STATUS_GET:
		chg_info("BC_CHG_STATUS_GET");
		// schedule_delayed_work(&bcdev->chg_status_send_work, 0);
		break;
	case BC_ADSP_NOTIFY_AP_SUSPEND_CHG:
		printk(KERN_ERR "!!!!!oplus_apsd_notify_ap_suspend_chg\n");
		// oplus_chg_set_adsp_notify_ap_suspend();
		break;
	case BC_PD_SOFT_RESET:
		printk(KERN_ERR "!!!!!PD hard reset happend\n");
		break;
	case BC_CHG_STATUS_SET:
		chg_info("BC_CHG_STATUS_SET");
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
	case BC_UFCS_TEST_MODE_TRUE:
		bcdev->ufcs_test_mode = true;
		chg_info("ufcs test mode change = %d\n", bcdev->ufcs_test_mode);
		break;
	case BC_UFCS_TEST_MODE_FALSE:
		bcdev->ufcs_test_mode = false;
		chg_info("ufcs test mode change = %d\n", bcdev->ufcs_test_mode);
		break;
	case BC_UFCS_POWER_READY:
		bcdev->ufcs_power_ready = true;
		chg_info("ufcs power ready = %d\n", bcdev->ufcs_power_ready);
		break;
	case BC_UFCS_HANDSHAKE_OK:
		bcdev->ufcs_handshake_ok = true;
		chg_info("ufcs handshake ok = %d\n", bcdev->ufcs_handshake_ok);
		break;
	case BC_VOOC_GAN_MOS_ERROR:
		// voocphy_push_gan_mos_err(bcdev->buck_ic);
		chg_err("gan_mos_err\n");
		break;
	case BC_UFCS_DISABLE_MOS:
		// if (oplus_cpa_get_protocol_allow(bcdev) != CHG_PROTOCOL_VOOC) {
		// 	chg_info("ufcs exit and disabe mos");
		// 	plat_ufcs_send_state(PLAT_UFCS_NOTIFY_EXIT, NULL);
		// }
		// schedule_delayed_work(&bcdev->publish_close_cp_item_work, 0);
		break;
	case BC_UFCS_PDO_READY:
		bcdev->ufcs_pdo_ready = true;
		chg_info("ufcs pdo ready = %d\n", bcdev->ufcs_pdo_ready);
		break;
	case BC_UFCS_VERIFY_AUTH_READY:
		bcdev->ufcs_verify_auth_ready = true;
		break;
	case BC_UFCS_PWR_INFO_READY:
		bcdev->ufcs_power_info_ready = true;
		chg_info("ufcs power info ready = %d\n", bcdev->ufcs_power_info_ready);
		break;
	case BC_UFCS_VDM_EMARK_READY:
		bcdev->ufcs_vdm_emark_ready = true;
		chg_info("ufcs vnd emark ready = %d\n", bcdev->ufcs_vdm_emark_ready);
		break;
	case BC_BATTERY_RESET_START:
		// if (oplus_chg_get_voocphy_support(bcdev) == ADSP_VOOCPHY &&
		//     oplus_get_ufcs_charging(bcdev) && !bcdev->adspfg_i2c_reset_processing) {
		// 	bcdev->adspfg_i2c_reset_processing = true;
		// 	bcdev->adspfg_i2c_reset_notify_done = false;
		// 	schedule_delayed_work(&bcdev->check_adspfg_status, 0);
		// }
		break;
	case PD_SOURCECAP_DONE:
		chg_info("PD_SOURCECAP_DONE\n");
		// if (battmgr->bcdev->variant == OPLUS_BATTMGR_SM8650 || battmgr->bcdev->variant == OPLUS_BATTMGR_SM8750) {
		// 	if (oplus_chg_get_common_charge_icl_support_flags())
		// 		schedule_delayed_work(&bcdev->sourcecap_done_work, 0);
		// } else
		// 	schedule_delayed_work(&bcdev->pd_set_aicl_work, 0);
		break;
	case REQUEST_QOS:
		chg_info("REQUEST_QOS\n");
		// cancel_delayed_work(&bcdev->release_qos_work);
		// schedule_delayed_work(&bcdev->request_qos_work, 0);
		break;
	case RELEASE_QOS:
		chg_info("RELEASE_QOS\n");
		// cancel_delayed_work(&bcdev->request_qos_work);
		// schedule_delayed_work(&bcdev->release_qos_work, 0);
		break;
	case HMAC_UPDATE:
		// oplus_chg_ic_virq_trigger(bcdev->gauge_ic, OPLUS_IC_VIRQ_HMAC_UPDATE);
		break;
	case GAUGE_INITED:
		chg_info("GAUGE_INITED notify\n");
		// cancel_delayed_work(&bcdev->gauge_register_work);
		// schedule_delayed_work(&bcdev->gauge_register_work, 0);
		break;
	case UFCS_EXIT_MODE_NOTIFY:
		chg_info("ufcs reset notify\n");
		// schedule_delayed_work(&bcdev->ufcs_reset_work, msecs_to_jiffies(500));
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
		case USB_TYPE:
			battmgr->usb.usb_type = le32_to_cpu(resp->intval.value);
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

	psy_cfg.drv_data = battmgr;
	psy_cfg.fwnode = dev_fwnode(&adev->dev);

	psy_cfg_supply.drv_data = battmgr;
	psy_cfg_supply.fwnode = dev_fwnode(&adev->dev);
	psy_cfg_supply.supplied_to = oplus_battmgr_battery;
	psy_cfg_supply.num_supplicants = 1;

	mutex_init(&battmgr->lock);
	init_completion(&battmgr->ack);

	match = of_match_device(oplus_battmgr_of_variants, dev->parent);
	if (match)
		bcdev->variant = (unsigned long)match->data;
	else
		bcdev->variant = OPLUS_BATTMGR_SM8650;

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
				   qcom_battmgr_enable_worker);
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
