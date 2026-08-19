// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022, Linaro Ltd
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */
#include <linux/auxiliary_bus.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/soc/qcom/pdr.h>
#include <linux/soc/qcom/pmic_glink.h>
#include <linux/math.h>
#include <linux/units.h>

#include <oplus_battmgr.h>

#define chg_err(fmt, ...)                                                      \
	printk(KERN_ERR "[OPLUS_CHG][%s]" fmt, __func__, ##__VA_ARGS__)

#define BATTMGR_CHEMISTRY_LEN	4
#define BATTMGR_STRING_LEN	128

enum oplus_battmgr_variant {
	OPLUS_BATTMGR_SM8450,
	OPLUS_BATTMGR_ADSP,
};

#define BATTMGR_CHG_CTRL_LIMIT_EN	0x48
#define CHARGE_CTRL_START_THR_MIN	50
#define CHARGE_CTRL_START_THR_MAX	95
#define CHARGE_CTRL_END_THR_MIN		55
#define CHARGE_CTRL_END_THR_MAX		100
#define CHARGE_CTRL_DELTA_SOC		5

struct qcom_battmgr_enable_request {
	struct pmic_glink_hdr hdr;
	__le32 battery_id;
	__le32 power_state;
	__le32 low_capacity;
	__le32 high_capacity;
};

struct qcom_battmgr_property_request {
	struct pmic_glink_hdr hdr;
	__le32 battery;
	__le32 property;
	__le32 value;
};

struct qcom_battmgr_update_request {
	struct pmic_glink_hdr hdr;
	__le32 battery_id;
};

struct qcom_battmgr_charge_time_request {
	struct pmic_glink_hdr hdr;
	__le32 battery_id;
	__le32 percent;
	__le32 reserved;
};

struct qcom_battmgr_discharge_time_request {
	struct pmic_glink_hdr hdr;
	__le32 battery_id;
	__le32 rate; /* 0 for current rate */
	__le32 reserved;
};

struct qcom_battmgr_charge_ctrl_request {
	struct pmic_glink_hdr hdr;
	__le32 enable;
	__le32 target_soc;
	__le32 delta_soc;
};

struct qcom_battmgr_message {
	struct pmic_glink_hdr hdr;
	union {
		struct {
			__le32 property;
			__le32 value;
			__le32 result;
		} intval;
		struct {
			__le32 property;
			char model[BATTMGR_STRING_LEN];
		} strval;
		struct {
			/*
			 * 0: mWh
			 * 1: mAh
			 */
			__le32 power_unit;
			__le32 design_capacity;
			__le32 last_full_capacity;
			/*
			 * 0 nonrechargable
			 * 1 rechargable
			 */
			__le32 battery_tech;
			__le32 design_voltage; /* mV */
			__le32 capacity_low;
			__le32 capacity_warning;
			__le32 cycle_count;
			/* thousandth of percent */
			__le32 accuracy;
			__le32 max_sample_time_ms;
			__le32 min_sample_time_ms;
			__le32 max_average_interval_ms;
			__le32 min_average_interval_ms;
			/* granularity between low and warning */
			__le32 capacity_granularity1;
			/* granularity between warning and full */
			__le32 capacity_granularity2;
			/*
			 * 0: no
			 * 1: cold
			 * 2: hot
			 */
			__le32 swappable;
			__le32 capabilities;
			char model_number[BATTMGR_STRING_LEN];
			char serial_number[BATTMGR_STRING_LEN];
			char battery_type[BATTMGR_STRING_LEN];
			char oem_info[BATTMGR_STRING_LEN];
			char battery_chemistry[BATTMGR_CHEMISTRY_LEN];
			char uid[BATTMGR_STRING_LEN];
			__le32 critical_bias;
			u8 day;
			u8 month;
			__le16 year;
			__le32 battery_id;
		} info;
		struct {
			/*
			 * BIT(0) discharging
			 * BIT(1) charging
			 * BIT(2) critical low
			 */
			__le32 battery_state;
			/* mWh or mAh, based on info->power_unit */
			__le32 capacity;
			__le32 rate;
			/* mv */
			__le32 battery_voltage;
			/*
			 * BIT(0) power online
			 * BIT(1) discharging
			 * BIT(2) charging
			 * BIT(3) battery critical
			 */
			__le32 power_state;
			/*
			 * 1: AC
			 * 2: USB
			 * 3: Wireless
			 */
			__le32 charging_source;
			__le32 temperature;
		} status;
		__le32 time;
		__le32 notification;
	};
};

#define BATTMGR_CHARGING_SOURCE_AC	1
#define BATTMGR_CHARGING_SOURCE_USB	2
#define BATTMGR_CHARGING_SOURCE_WIRELESS 3

enum qcom_battmgr_unit {
	QCOM_BATTMGR_UNIT_mWh = 0,
	QCOM_BATTMGR_UNIT_mAh = 1
};

struct qcom_battmgr_info {
	bool valid;

	bool present;
	unsigned int charge_type;
	unsigned int design_capacity;
	unsigned int last_full_capacity;
	unsigned int voltage_max_design;
	unsigned int voltage_max;
	unsigned int voltage_min;
	unsigned int capacity_low;
	unsigned int capacity_warning;
	unsigned int cycle_count;
	unsigned int charge_count;
	unsigned int charge_ctrl_start;
	unsigned int charge_ctrl_end;
	char model_number[BATTMGR_STRING_LEN];
	char serial_number[BATTMGR_STRING_LEN];
	char oem_info[BATTMGR_STRING_LEN];
	unsigned char technology;
	unsigned char day;
	unsigned char month;
	unsigned short year;
};

struct qcom_battmgr_status {
	unsigned int status;
	unsigned int health;
	unsigned int capacity;
	unsigned int percent;
	int current_now;
	int power_now;
	int power_avg;
	unsigned int voltage_now;
	unsigned int voltage_ocv;
	u32 thermal_fcc_ua;
	unsigned int temperature;
	unsigned int resistance;
	unsigned int soh_percent;

	unsigned int discharge_time;
	unsigned int charge_time;
};

struct qcom_battmgr_ac {
	bool online;
};

struct qcom_battmgr_usb {
	bool online;
	unsigned int voltage_now;
	unsigned int voltage_max;
	unsigned int current_now;
	unsigned int current_max;
	unsigned int current_limit;
	unsigned int usb_adap_type;
	unsigned int usb_temp;
};

struct qcom_battmgr_wireless {
	bool online;
	unsigned int voltage_now;
	unsigned int voltage_max;
	unsigned int current_now;
	unsigned int current_max;
};

struct qcom_battmgr {
	struct device *dev;
	struct pmic_glink_client *client;
	struct oplus_chip *chip;
	struct voocphy_manager *voocphy;
	struct vooc_chip *vooc;

	enum oplus_battmgr_variant variant;

	struct power_supply *ac_psy;
	struct power_supply *bat_psy;
	struct power_supply *usb_psy;
	struct power_supply *wls_psy;

	enum qcom_battmgr_unit unit;

	int error;
	struct completion ack;

	bool service_up;

	bool otg_online;
	bool pd_svooc;

	unsigned long long hvdcp_detect_time;
	unsigned long long hvdcp_detach_time;
	bool hvdcp_detect_ok;
	bool hvdcp_disable;
	struct delayed_work hvdcp_disable_work;
	bool adsp_voocphy_err_check;
	struct mutex chg_en_lock;
	bool chg_en;
	bool cid_status;
	bool force_svooc;

	int otg_scheme;
	bool pmic_is_pm7250b;
	bool common_charge_icl_support;
	int ffc_full_delta_iterm_ma;
	int ffc_full_delta_iterm_ma_low;
	int otg_boost_src;
	int otg_curr_limit_max;
	int otg_curr_limit_high;
	int otg_real_soc_min;
	int usbtemp_thread_100w_support;
	bool otg_prohibited;
	struct notifier_block	ssr_nb;
	void			*subsys_handle;
	int usb_in_status;
	int real_chg_type;

	struct qcom_battmgr_info info;
	struct qcom_battmgr_status status;
	struct qcom_battmgr_ac ac;
	struct qcom_battmgr_usb usb;
	struct qcom_battmgr_wireless wireless;

	struct work_struct enable_work;
	struct delayed_work adsp_crash_recover_work;
	struct delayed_work	otg_init_work;
	struct delayed_work	check_charger_out_work;
	struct delayed_work	adsp_voocphy_enable_check_work;


	/*
	 * @lock is used to prevent concurrent power supply requests to the
	 * firmware, as it then stops responding.
	 */
	struct mutex lock;
};

static int qcom_battmgr_request(struct qcom_battmgr *battmgr, void *data, size_t len)
{
	unsigned long left;
	int ret;

	reinit_completion(&battmgr->ack);

	battmgr->error = 0;

	ret = pmic_glink_send(battmgr->client, data, len);
	if (ret < 0)
		return ret;

	left = wait_for_completion_timeout(&battmgr->ack, HZ);
	if (!left)
		return -ETIMEDOUT;

	return battmgr->error;
}

static int qcom_battmgr_request_property(struct qcom_battmgr *battmgr, int opcode,
					 int property, u32 value)
{
	struct qcom_battmgr_property_request request = {
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_BATTMGR),
		.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP),
		.hdr.opcode = cpu_to_le32(opcode),
		.battery = cpu_to_le32(0),
		.property = cpu_to_le32(property),
		.value = cpu_to_le32(value),
	};

	return qcom_battmgr_request(battmgr, &request, sizeof(request));
}

static const u8 oplus_bat_prop_map[] = {
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

static int qcom_battmgr_bat_oplus_update(struct qcom_battmgr *battmgr,
					  enum power_supply_property psp)
{
	unsigned int prop;
	int ret;

	if (psp >= ARRAY_SIZE(oplus_bat_prop_map))
		return -EINVAL;

	prop = oplus_bat_prop_map[psp];

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_BATTERY_STATUS_GET, prop, 0);
	mutex_unlock(&battmgr->lock);

	return ret;
}

static int qcom_battmgr_bat_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	enum qcom_battmgr_unit unit = battmgr->unit;
	int ret;

	if (!battmgr->service_up)
		return -EAGAIN;

	ret = qcom_battmgr_bat_oplus_update(battmgr, psp);
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
		val->intval = battmgr->status.voltage_now * 1000;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		val->intval = battmgr->status.voltage_ocv;
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		val->intval = battmgr->status.current_now;
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
	case POWER_SUPPLY_PROP_VOLTAGE_MIN:
		val->intval = battmgr->info.voltage_min * 1000;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		val->intval = battmgr->info.charge_count * 1000;
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
		val->intval = battmgr->status.temperature;
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
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		val->intval = battmgr->status.charge_time;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_START_THRESHOLD:
		val->intval = battmgr->info.charge_ctrl_start;
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_END_THRESHOLD:
		val->intval = battmgr->info.charge_ctrl_end;
		break;
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

static int qcom_battmgr_set_charge_control(struct qcom_battmgr *battmgr,
					   u32 target_soc, u32 delta_soc)
{
	struct qcom_battmgr_charge_ctrl_request request = {
		.hdr.owner = cpu_to_le32(PMIC_GLINK_OWNER_BATTMGR),
		.hdr.type = cpu_to_le32(PMIC_GLINK_REQ_RESP),
		.hdr.opcode = cpu_to_le32(BATTMGR_CHG_CTRL_LIMIT_EN),
		.enable = cpu_to_le32(1),
		.target_soc = cpu_to_le32(target_soc),
		.delta_soc = cpu_to_le32(delta_soc),
	};

	return qcom_battmgr_request(battmgr, &request, sizeof(request));
}

static int qcom_battmgr_set_charge_start_threshold(struct qcom_battmgr *battmgr, int start_soc)
{
	u32 target_soc, delta_soc;
	int ret;

	start_soc = clamp(start_soc, CHARGE_CTRL_START_THR_MIN, CHARGE_CTRL_START_THR_MAX);

	/*
	 * If the new start threshold is larger than the old end threshold,
	 * move the end threshold one step (DELTA_SOC) after the new start
	 * threshold.
	 */
	if (start_soc > battmgr->info.charge_ctrl_end) {
		target_soc = start_soc + CHARGE_CTRL_DELTA_SOC;
		target_soc = min_t(u32, target_soc, CHARGE_CTRL_END_THR_MAX);
		delta_soc = target_soc - start_soc;
		delta_soc = min_t(u32, delta_soc, CHARGE_CTRL_DELTA_SOC);
	} else {
		target_soc =  battmgr->info.charge_ctrl_end;
		delta_soc = battmgr->info.charge_ctrl_end - start_soc;
	}

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_set_charge_control(battmgr, target_soc, delta_soc);
	mutex_unlock(&battmgr->lock);
	if (!ret) {
		battmgr->info.charge_ctrl_start = start_soc;
		battmgr->info.charge_ctrl_end = target_soc;
	}

	return 0;
}

static int qcom_battmgr_set_charge_end_threshold(struct qcom_battmgr *battmgr, int end_soc)
{
	u32 delta_soc = CHARGE_CTRL_DELTA_SOC;
	int ret;

	end_soc = clamp(end_soc, CHARGE_CTRL_END_THR_MIN, CHARGE_CTRL_END_THR_MAX);

	if (battmgr->info.charge_ctrl_start && end_soc > battmgr->info.charge_ctrl_start)
		delta_soc = end_soc - battmgr->info.charge_ctrl_start;

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_set_charge_control(battmgr, end_soc, delta_soc);
	mutex_unlock(&battmgr->lock);
	if (!ret) {
		battmgr->info.charge_ctrl_start = end_soc - delta_soc;
		battmgr->info.charge_ctrl_end = end_soc;
	}

	return 0;
}

static int qcom_battmgr_charge_control_thresholds_init(struct qcom_battmgr *battmgr)
{
	int ret;
	u8 en, end_soc, start_soc, delta_soc;

	ret = nvmem_cell_read_u8(battmgr->dev->parent, "charge_limit_en", &en);
	if (!ret && en != 0) {
		ret = nvmem_cell_read_u8(battmgr->dev->parent, "charge_limit_end", &end_soc);
		if (ret < 0)
			return ret;

		ret = nvmem_cell_read_u8(battmgr->dev->parent, "charge_limit_delta", &delta_soc);
		if (ret < 0)
			return ret;

		if (delta_soc >= end_soc)
			return -EINVAL;

		start_soc = end_soc - delta_soc;
		end_soc = clamp(end_soc, CHARGE_CTRL_END_THR_MIN, CHARGE_CTRL_END_THR_MAX);
		start_soc = clamp(start_soc, CHARGE_CTRL_START_THR_MIN, CHARGE_CTRL_START_THR_MAX);

		battmgr->info.charge_ctrl_start = start_soc;
		battmgr->info.charge_ctrl_end = end_soc;
	}

	return 0;
}

static int qcom_battmgr_bat_is_writeable(struct power_supply *psy,
					 enum power_supply_property prop)
{
	switch (prop) {
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		return 1;
	// case POWER_SUPPLY_PROP_CURRENT_NOW:
	// 	if (g_oplus_chip && g_oplus_chip->smart_charging_screenoff) {
	// 		return 1;
	// 	} else {
	// 		return 0;
	// 	}
	case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		return 1;
	default:
		break;
	}

	return 0;
}

static int qcom_battmgr_bat_set_property(struct power_supply *psy,
					 enum power_supply_property prop,
					 const union power_supply_propval *pval)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);

	if (!battmgr->service_up)
		return -EAGAIN;

	switch (prop) {
		// case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		// 	return battery_psy_set_charge_current(bcdev, pval->intval);
		// case POWER_SUPPLY_PROP_CURRENT_NOW:
		// 	if (g_oplus_chip && g_oplus_chip->smart_charging_screenoff) {
		// 		oplus_smart_charge_by_shell_temp(g_oplus_chip, pval->intval);
		// 		break;
		// 	} else {
		// 		return  -EINVAL;
		// 	}
		// case POWER_SUPPLY_PROP_TIME_TO_FULL_NOW:
		// 	if (g_oplus_chip) {
		// 		g_oplus_chip->time_to_full =
		// 		(pval->intval & TTF_VALUE_MASK) > 0 ? (pval->intval & TTF_VALUE_MASK) : 0;
		// 		if (pval->intval & TTF_UPDATE_UEVENT_BIT)
		// 			power_supply_changed(g_oplus_chip->batt_psy);
		// 	}
		// 	break;
		default:
			return -EINVAL;
	}

	return 0;
}

static const enum power_supply_property oplus_bat_props[] = {
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

static const struct power_supply_desc oplus_bat_psy_desc = {
	.name = "oplus-battmgr-bat",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = oplus_bat_props,
	.num_properties = ARRAY_SIZE(oplus_bat_props),
	.get_property = qcom_battmgr_bat_get_property,
	.set_property = qcom_battmgr_bat_set_property,
	.property_is_writeable = qcom_battmgr_bat_is_writeable,
};

static const u8 oplus_usb_prop_map[] = {
	[POWER_SUPPLY_PROP_ONLINE] = USB_ONLINE,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = USB_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = USB_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = USB_CURR_NOW,
	[POWER_SUPPLY_PROP_CURRENT_MAX] = USB_CURR_MAX,
	[POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT] = USB_INPUT_CURR_LIMIT,
	[POWER_SUPPLY_PROP_USB_TYPE] = USB_ADAP_TYPE,
	[POWER_SUPPLY_PROP_TEMP] = USB_TEMP,
};

static int qcom_battmgr_usb_oplus_update(struct qcom_battmgr *battmgr,
					  enum power_supply_property psp)
{
	unsigned int prop;
	int ret;

	if (psp >= ARRAY_SIZE(oplus_usb_prop_map))
		return -EINVAL;

	prop = oplus_usb_prop_map[psp];

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_USB_STATUS_GET, prop, 0);
	mutex_unlock(&battmgr->lock);

	return ret;
}

static int qcom_battmgr_usb_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	int ret;

	if (!battmgr->service_up)
		return -EAGAIN;

	ret = qcom_battmgr_usb_oplus_update(battmgr, psp);
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
		val->intval = battmgr->usb.usb_adap_type;
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = battmgr->usb.usb_temp;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const enum power_supply_property oplus_usb_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_USB_TYPE,
	POWER_SUPPLY_PROP_TEMP,
};

static const struct power_supply_desc oplus_usb_psy_desc = {
	.name = "oplus-battmgr-usb",
	.type = POWER_SUPPLY_TYPE_USB,
	.properties = oplus_usb_props,
	.num_properties = ARRAY_SIZE(oplus_usb_props),
	.get_property = qcom_battmgr_usb_get_property,
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

static const u8 oplus_wls_prop_map[] = {
	[POWER_SUPPLY_PROP_ONLINE] = WLS_ONLINE,
	[POWER_SUPPLY_PROP_VOLTAGE_NOW] = WLS_VOLT_NOW,
	[POWER_SUPPLY_PROP_VOLTAGE_MAX] = WLS_VOLT_MAX,
	[POWER_SUPPLY_PROP_CURRENT_NOW] = WLS_CURR_NOW,
	[POWER_SUPPLY_PROP_CURRENT_MAX] = WLS_CURR_MAX,
};

static int oplus_battmgr_wls_update(struct qcom_battmgr *battmgr,
					  enum power_supply_property psp)
{
	unsigned int prop;
	int ret;

	if (psp >= ARRAY_SIZE(oplus_wls_prop_map))
		return -EINVAL;

	prop = oplus_wls_prop_map[psp];

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_WLS_STATUS_GET, prop, 0);
	mutex_unlock(&battmgr->lock);

	return ret;
}

static int qcom_battmgr_wls_get_property(struct power_supply *psy,
					 enum power_supply_property psp,
					 union power_supply_propval *val)
{
	struct qcom_battmgr *battmgr = power_supply_get_drvdata(psy);
	int ret;

	if (!battmgr->service_up)
		return -EAGAIN;

	ret = oplus_battmgr_wls_update(battmgr, psp);
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
	default:
		return -EINVAL;
	}

	return 0;
}

static const enum power_supply_property oplus_wls_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static const struct power_supply_desc oplus_wls_psy_desc = {
	.name = "oplus-battmgr-wls",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = oplus_wls_props,
	.num_properties = ARRAY_SIZE(oplus_wls_props),
	.get_property = qcom_battmgr_wls_get_property,
};

static void qcom_battmgr_notification(struct qcom_battmgr *battmgr,
				      const struct qcom_battmgr_message *msg,
				      int len)
{
	size_t payload_len = len - sizeof(struct pmic_glink_hdr);
	unsigned int notification;
	struct oplus_chip *g_oplus_chip = battmgr->chip;

	if (payload_len != sizeof(msg->notification)) {
		dev_warn(battmgr->dev, "ignoring notification with invalid length\n");
		return;
	}

	notification = le32_to_cpu(msg->notification);
	notification &= 0xff;
	switch (notification) {
	case BC_BATTERY_STATUS_GET:
	case BC_GENERIC_NOTIFY:
		// pst = &bcdev->psy_list[PSY_TYPE_BATTERY];
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		break;
	case BC_USB_STATUS_GET:
		// pst = &bcdev->psy_list[PSY_TYPE_USB];
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		// schedule_delayed_work(&bcdev->usb_type_work, 0);
		break;
	case BC_WLS_STATUS_GET:
		// pst = &bcdev->psy_list[PSY_TYPE_WLS];
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		break;
	case BC_PD_SVOOC:
		printk(KERN_ERR "!!!:%s, should set pd_svooc\n", __func__);
		battmgr->pd_svooc = true;
		g_oplus_chip->pd_svooc = true;
		printk(KERN_ERR "!!!:%s, pd_svooc[%d]\n", __func__, battmgr->pd_svooc);
		break;
	case BC_ABNORMAL_PD_SVOOC_ADAPTER:
		printk(KERN_ERR "!!!:%s, is_abnormal_adapter\n", __func__);
		g_oplus_chip->is_abnormal_adapter = true;
		break;
	case BC_VOOC_STATUS_GET:
		// schedule_delayed_work(&bcdev->adsp_voocphy_status_work, 0);
		break;
	case BC_OTG_ENABLE:
		printk(KERN_ERR "!!!!!enable otg\n");
		// pst = &bcdev->psy_list[PSY_TYPE_USB];
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		battmgr->otg_online = true;
		battmgr->pd_svooc = false;
		// schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_OTG_DISABLE:
		printk(KERN_ERR "!!!!!disable otg\n");
		// pst = &bcdev->psy_list[PSY_TYPE_USB];
		pm_wakeup_dev_event(battmgr->dev, 50, true);
		battmgr->otg_online = false;
		// schedule_delayed_work(&bcdev->otg_vbus_enable_work, 0);
		break;
	case BC_ADSP_NOTIFY_TRACK:
		pr_info("!!!!!adsp track notify\n");
		// schedule_delayed_work(&bcdev->adsp_track_notify_work, 0);
		break;
	case BC_VOOC_VBUS_ADC_ENABLE:
		printk(KERN_ERR "!!!!!vooc_vbus_adc_enable\n");
		battmgr->adsp_voocphy_err_check = true;
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
		// chg_type = opchg_get_charger_type();
		// sub_chg_type = oplus_chg_get_charger_subtype();
		// battmgr->real_chg_type = chg_type | (sub_chg_type << 8);
		battmgr->hvdcp_detect_ok = true;
		break;
	case BC_TYPEC_STATE_CHANGE:
		printk(KERN_ERR "!!!!!typec_state_change_work\n");
		// schedule_delayed_work(&bcdev->typec_state_change_work, 0);
		break;
	case BC_PLUGIN_IRQ:
		printk(KERN_ERR "!!!!!oplus_plugin_irq_work\n");
		// schedule_delayed_work(&bcdev->plugin_irq_work, 0);
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
		dev_err(battmgr->dev, "unknown notification: %#x\n", notification);
		break;
	}
}

static void qcom_battmgr_oplus_callback(struct qcom_battmgr *battmgr,
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
		case BATT_CHG_CTRL_LIM_MAX:
			battmgr->status.thermal_fcc_ua = le32_to_cpu(resp->intval.value);
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
			battmgr->usb.usb_adap_type = le32_to_cpu(resp->intval.value);
			break;
		case USB_TEMP:
			battmgr->usb.usb_temp = le32_to_cpu(resp->intval.value);
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
		break;
	case BC_SET_NOTIFY_REQ:
	case BC_SHUTDOWN_NOTIFY:
		battmgr->error = 0;
		break;
	default:
		dev_warn(battmgr->dev, "unknown message %#x\n", opcode);
		break;
	}

out_complete:
	complete(&battmgr->ack);
}

static void qcom_battmgr_callback(const void *data, size_t len, void *priv)
{
	const struct pmic_glink_hdr *hdr = data;
	struct qcom_battmgr *battmgr = priv;
	unsigned int opcode = le32_to_cpu(hdr->opcode);

	if (opcode == BC_NOTIFY_IND)
		qcom_battmgr_notification(battmgr, data, len);
	else
		qcom_battmgr_oplus_callback(battmgr, data, len);
}

static void qcom_battmgr_enable_worker(struct work_struct *work)
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
}

static int oplus_ap_init_adsp_gague(struct qcom_battmgr *battmgr)
{
	int ret;

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_BATTERY_STATUS_SET, BATT_ADSP_GAUGE_INIT, 1);
	mutex_unlock(&battmgr->lock);

	if (ret)
		chg_err("init adsp gague fail, rc=%d\n", ret);
	else
		chg_err("init adsp gague sucess.");

	return ret;
}

void oplus_adsp_voocphy_reset_status_when_crash_recover(struct voocphy_manager *g_voocphy_chip)
{

	if (g_voocphy_chip->fast_chg_type != FASTCHG_CHARGER_TYPE_UNKOWN)
		g_voocphy_chip->fastchg_dummy_start = true;

	g_voocphy_chip->fastchg_ing = false;
	g_voocphy_chip->fastchg_start = false;
	g_voocphy_chip->fastchg_to_normal = false;
	g_voocphy_chip->fastchg_to_warm = false;

	return;
}

int oplus_adsp_voocphy_enable(struct qcom_battmgr *battmgr, bool enable)
{
	int ret;

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_USB_STATUS_SET, USB_VOOCPHY_ENABLE, enable);
	mutex_unlock(&battmgr->lock);
	if (ret) {
		chg_err("set enable adsp voocphy fail, rc=%d\n", ret);
	} else {
		chg_err("set enable adsp voocphy success, rc=%d\n", ret);
	}

	return ret;
}

static void oplus_otg_init_status_func(struct work_struct *work)
{
	struct qcom_battmgr *battmgr = container_of(to_delayed_work(work), struct qcom_battmgr, otg_init_work);
	struct oplus_chip *chip = battmgr->chip;
	int count = 20;
	int ret;

	// if (battmgr->otg_boost_src == OTG_BOOST_SOURCE_EXTERNAL) {
	// 	while (count--) {
	// 		if (is_wls_ocm_available(chip))
	// 			break;
 //
	// 		msleep(500);
	// 	}
	// }

	printk(KERN_ERR "!!!!oplus_otg_init_status_func, count[%d]\n", count);

	mutex_lock(&battmgr->lock);
	ret = qcom_battmgr_request_property(battmgr, BC_USB_STATUS_SET, USB_OTG_AP_ENABLE, 1);
	mutex_unlock(&battmgr->lock);
	if (ret) {
		chg_err("oplus_otg_ap_enable fail, rc=%d\n", ret);
	} else {
		chg_err("oplus_otg_ap_enable, rc=%d\n", ret);
	}

	// oplus_get_otg_online_status_with_cid_scheme();
	// if (bcdev->cid_status != 0) {
	// 	chg_err("Oplus_otg_ap_enable,flag bcdev->cid_status != 0\n");
	// 	oplus_ccdetect_enable();
	// }
}

int qpnp_get_prop_charger_voltage_now(struct qcom_battmgr *battmgr)
{
	int ret;
	static int vbus_volt = 0;
	// union oplus_chg_mod_propval pval = {0};
 //
	// if (oplus_chg_is_wls_present()) {
	// 	rc = oplus_chg_mod_get_property(chip->wls_ocm, OPLUS_CHG_PROP_VOLTAGE_NOW, &pval);
	// 	if (rc >= 0) {
	// 		return pval.intval;
	// 	}
	// }


	qcom_battmgr_usb_oplus_update(battmgr, POWER_SUPPLY_PROP_VOLTAGE_NOW);

	vbus_volt = battmgr->status.voltage_now / 1000;

	return vbus_volt;
}

static void oplus_check_charger_out_func(struct work_struct *work)
{
	struct qcom_battmgr *battmgr = container_of(to_delayed_work(work), struct qcom_battmgr, check_charger_out_work);
	struct voocphy_manager *voocphy = battmgr->voocphy;
	struct vooc_chip *vooc = battmgr->vooc;
	int chg_vol = 0;


	chg_vol = qpnp_get_prop_charger_voltage_now(battmgr);

	if (chg_vol >= 0 && chg_vol < 2000) {
		// if (voocphy->voocphy_bidirect_cp_support && vooc->fastchg_ing || voocphy->fastchg_start)
		// 	oplus_voocphy_chg_out_check_event_handle(true);
		// oplus_adsp_voocphy_clear_status();
		// oplus_chg_clear_abnormal_adapter_var();
		power_supply_changed(battmgr->bat_psy);
		chg_err("charger out, chg_vol:%d\n", chg_vol);
	}
}

static void oplus_adsp_crash_recover_func(struct work_struct *work)
{
	struct qcom_battmgr *battmgr = container_of(to_delayed_work(work), struct qcom_battmgr, adsp_crash_recover_work);
	struct oplus_chip *chip = battmgr->chip;
	struct voocphy_manager *g_voocphy_chip = battmgr->voocphy;

	// if (chip->voocphy_support == ADSP_VOOCPHY) {
		oplus_ap_init_adsp_gague(battmgr);
		oplus_adsp_voocphy_reset_status_when_crash_recover(g_voocphy_chip);
	// }
	chip->charger_type  = POWER_SUPPLY_TYPE_UNKNOWN;
	// if (chip->voocphy_support == ADSP_VOOCPHY)
		oplus_adsp_voocphy_enable(battmgr, true);

	schedule_delayed_work(&battmgr->otg_init_work, round_jiffies_relative(msecs_to_jiffies(2000)));
	// oplus_chg_wake_update_work();
	// schedule_delayed_work(&battmgr->adsp_voocphy_enable_check_work, round_jiffies_relative(msecs_to_jiffies(0)));
	// schedule_delayed_work(&battmgr->check_charger_out_work, round_jiffies_relative(msecs_to_jiffies(3000)));
	// if (oplus_ccdetect_check_is_gpio(chip) == true) {
	// 	oplus_ccdetect_before_irq_register(chip);
	// }
}

static void qcom_battmgr_pdr_notify(void *priv, int state)
{
	struct qcom_battmgr *battmgr = priv;

	if (state == SERVREG_SERVICE_STATE_UP) {
		battmgr->service_up = true;
		schedule_work(&battmgr->enable_work);
		schedule_delayed_work(&battmgr->adsp_crash_recover_work, round_jiffies_relative(msecs_to_jiffies(1500)));
	} else {
		battmgr->service_up = false;
	}
}

static int oplus_parse_dt(struct qcom_battmgr *battmgr)
{
	struct oplus_chip *chip = battmgr->chip;
	struct voocphy_manager *voocphy = battmgr->voocphy;
	struct device_node *node = battmgr->dev->of_node;
	int data;
	int rc;

	rc = of_property_read_u32(node, "oplus,voocphy_support", &data);
	if (rc < 0) {
		chip->voocphy_support = NO_VOOCPHY;
	} else {
		chip->voocphy_support = (uint8_t)data;
	}

	voocphy->voocphy_bidirect_cp_support = of_property_read_bool(node, "oplus_spec,voocphy_bidirect_cp_support");

	return 0;
}

static const struct of_device_id qcom_battmgr_of_variants[] = {
	{ .compatible = "oplus,sm8450-pmic-glink", .data = (void *)OPLUS_BATTMGR_SM8450 },
	/* Unmatched devices falls back to OPLUS_BATTMGR_ADSP */
	{}
};

static char *qcom_battmgr_battery[] = { "battery" };

static int qcom_battmgr_probe(struct auxiliary_device *adev,
			      const struct auxiliary_device_id *id)
{
	const struct power_supply_desc *psy_desc;
	struct power_supply_config psy_cfg_supply = {};
	struct power_supply_config psy_cfg = {};
	const struct of_device_id *match;
	struct qcom_battmgr *battmgr;
	struct device *dev = &adev->dev;
	int ret;

	battmgr = devm_kzalloc(dev, sizeof(*battmgr), GFP_KERNEL);
	if (!battmgr)
		return -ENOMEM;

	battmgr->dev = dev;

	psy_cfg.drv_data = battmgr;
	psy_cfg.fwnode = dev_fwnode(&adev->dev);

	psy_cfg_supply.drv_data = battmgr;
	psy_cfg_supply.fwnode = dev_fwnode(&adev->dev);
	psy_cfg_supply.supplied_to = qcom_battmgr_battery;
	psy_cfg_supply.num_supplicants = 1;

	battmgr->chip = devm_kzalloc(dev, sizeof(*battmgr->chip), GFP_KERNEL);
	if (!battmgr->chip) {
		pr_err("oplus_voocphy_manager devm_kzalloc failed.\n");
		return -ENOMEM;
	}


	battmgr->voocphy = devm_kzalloc(dev, sizeof(*battmgr->voocphy), GFP_KERNEL);
	if (!battmgr->voocphy) {
		pr_err("oplus_voocphy_manager devm_kzalloc failed.\n");
		return -ENOMEM;
	}

	// oplus_parse_dt(battmgr);

	INIT_WORK(&battmgr->enable_work, qcom_battmgr_enable_worker);
	INIT_DELAYED_WORK(&battmgr->adsp_crash_recover_work, oplus_adsp_crash_recover_func);
	INIT_DELAYED_WORK(&battmgr->otg_init_work, oplus_otg_init_status_func);
	INIT_DELAYED_WORK(&battmgr->check_charger_out_work, oplus_check_charger_out_func);
	// INIT_DELAYED_WORK(&battmgr->adsp_voocphy_enable_check_work, oplus_adsp_voocphy_enable_check_func);
	mutex_init(&battmgr->lock);
	init_completion(&battmgr->ack);

	match = of_match_device(qcom_battmgr_of_variants, dev->parent);
	if (match)
		battmgr->variant = (unsigned long)match->data;
	else
		battmgr->variant = OPLUS_BATTMGR_ADSP;

	ret = qcom_battmgr_charge_control_thresholds_init(battmgr);
	if (ret < 0)
		return dev_err_probe(dev, ret,
				     "failed to init battery charge control thresholds\n");


	battmgr->bat_psy = devm_power_supply_register(dev, &oplus_bat_psy_desc, &psy_cfg);
	if (IS_ERR(battmgr->bat_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->bat_psy),
				     "failed to register battery power supply\n");

	battmgr->usb_psy = devm_power_supply_register(dev, &oplus_usb_psy_desc, &psy_cfg_supply);
	if (IS_ERR(battmgr->usb_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->usb_psy),
					"failed to register USB power supply\n");

	battmgr->wls_psy = devm_power_supply_register(dev, &oplus_wls_psy_desc, &psy_cfg_supply);
	if (IS_ERR(battmgr->wls_psy))
		return dev_err_probe(dev, PTR_ERR(battmgr->wls_psy),
					"failed to register wireless charing power supply\n");

	battmgr->client = devm_pmic_glink_client_alloc(dev, PMIC_GLINK_OWNER_BATTMGR,
						       qcom_battmgr_callback,
						       qcom_battmgr_pdr_notify,
						       battmgr);
	if (IS_ERR(battmgr->client))
		return PTR_ERR(battmgr->client);

	pmic_glink_client_register(battmgr->client);

	return 0;
}

static const struct auxiliary_device_id qcom_battmgr_id_table[] = {
	{ .name = "pmic_glink.oplus-power-supply", },
	{},
};
MODULE_DEVICE_TABLE(auxiliary, qcom_battmgr_id_table);

static struct auxiliary_driver qcom_battmgr_driver = {
	.name = "oplus_pmic_glink_power_supply",
	.probe = qcom_battmgr_probe,
	.id_table = qcom_battmgr_id_table,
};

module_auxiliary_driver(qcom_battmgr_driver);

MODULE_DESCRIPTION("OnePlus PMIC GLINK battery manager driver");
MODULE_LICENSE("GPL");
