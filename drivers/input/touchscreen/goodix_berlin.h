/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Goodix Touchscreen Driver
 * Copyright (C) 2020 - 2021 Goodix, Inc.
 * Copyright (C) 2023 Linaro Ltd.
 *
 * Based on goodix_berlin_berlin driver.
 */

#ifndef __GOODIX_BERLIN_H_
#define __GOODIX_BERLIN_H_

#include <linux/pm.h>

#define GOODIX_BERLIN_FW_VERSION_INFO_ADDR_A	0x1000C
#define GOODIX_BERLIN_FW_VERSION_INFO_ADDR_D	0x10014

#define GOODIX_BERLIN_IC_INFO_ADDR_A		0x10068
#define GOODIX_BERLIN_IC_INFO_ADDR_D		0x10070

/*
 * Firmware command protocol, used to push the touch "config" blob into a
 * running Berlin IC.  Values taken from the vendor goodix_brl_hw.c
 * (brl_send_config()/brl_send_cmd()).
 */
#define GOODIX_BERLIN_CMD_ACK_OK			0x80
#define GOODIX_BERLIN_CMD_ACK_BUSY			0x02
#define GOODIX_BERLIN_CMD_ACK_BUFFER_OVERFLOW		0x03

#define GOODIX_BERLIN_CONFIG_CMD_START		0x04
#define GOODIX_BERLIN_CONFIG_CMD_WRITE		0x05
#define GOODIX_BERLIN_CONFIG_CMD_EXIT		0x06

#define GOODIX_BERLIN_CONFIG_CMD_LEN		4
#define GOODIX_BERLIN_CONFIG_CMD_STATUS_PASS	0x80
#define GOODIX_BERLIN_CONFIG_CMD_WAIT_RETRY	20
#define GOODIX_BERLIN_CMD_RETRY		6

/* struct goodix_ts_cmd: state, ack, len, cmd, data[MAX_CMD_DATA_LEN] */
#define GOODIX_BERLIN_CMD_MAX_DATA_LEN		10

/* Goodix "TS_CFG_BIN" container, see goodix_cfg_bin.c */
#define GOODIX_BERLIN_CFG_BIN_HEAD_LEN		16
#define GOODIX_BERLIN_CFG_PKG_CONST_LEN		56
#define GOODIX_BERLIN_CFG_PKG_REG_LEN		65
#define GOODIX_BERLIN_CFG_PKG_HEAD_LEN		(GOODIX_BERLIN_CFG_PKG_CONST_LEN + \
						 GOODIX_BERLIN_CFG_PKG_REG_LEN)

/*
 * Firmware-update ("ISP flashing") protocol, ported from the vendor
 * goodix_brl_fwupdate.c as shipped in LineageOS' goodix_berlin_driver.
 * The IC only produces touch reports after its on-board config has been
 * (re)flashed through the ISP at least once, and the vendor driver does a
 * full firmware+config flash on every boot.
 */
#define GOODIX_BERLIN_FLASH_HEADER_SIZE		512
#define GOODIX_BERLIN_FLASH_SUBSYS_INFO_OFFSET	42
#define GOODIX_BERLIN_FLASH_SUBSYS_INFO_SIZE	10
#define GOODIX_BERLIN_FLASH_SUBSYS_MAX_NUM	47
#define GOODIX_BERLIN_FLASH_FILE_CKSUM_OFFSET	8
#define GOODIX_BERLIN_FLASH_MAX_SUBSYS		4096

/* Berlin D revision addresses */
#define GOODIX_BERLIN_FLASH_CMD_REG_D		0x12400
#define GOODIX_BERLIN_FLASH_BUF_REG_D		0x12410
#define GOODIX_BERLIN_CONFIG_DATA_ADDR_D	0x3E000
#define GOODIX_BERLIN_ISP_RAM_ADDR_D		0x23800
#define GOODIX_BERLIN_MISCTL_REG_D		0xD804
#define GOODIX_BERLIN_MISCTL_EN_D		0x20700000
#define GOODIX_BERLIN_WATCHDOG_REG_D		0xCC54
#define GOODIX_BERLIN_HOLD_CPU_REG_W		0x0002
#define GOODIX_BERLIN_HOLD_CPU_REG_R		0x2000
#define GOODIX_BERLIN_CPU_RUN_FROM		0x10000

#define GOODIX_BERLIN_FLASH_CMD_LEN		11
#define GOODIX_BERLIN_FLASH_CMD_WRITE		0xBB
#define GOODIX_BERLIN_FLASH_ACK_CHK_PASS	0xEE
#define GOODIX_BERLIN_FLASH_WSTAT_WRITE_OK	0xEE
#define GOODIX_BERLIN_FLASH_WSTAT_CHK_FAIL	0x33
#define GOODIX_BERLIN_FLASH_WSTAT_WRITE_ERR	0x55

#define GOODIX_BERLIN_CONFIG_DATA_TYPE		4

struct goodix_berlin_flash_cmd {
	u8 status;
	u8 ack;
	u8 len;
	u8 cmd;
	u8 fw_type;
	__le16 fw_len;
	__le32 fw_addr;
	/* the vendor appends a u8 checksum at buf[11..12] and writes the
	 * whole 16-byte union; keep the tail so it stays in-struct */
	u8 checksum[2];
	u8 pad[3];
} __packed;

struct goodix_berlin_fw_subsys {
	u8 type;
	u32 size;
	u32 flash_addr;
	const u8 *data;
};

struct goodix_berlin_fw_summary {
	u32 num_subsys;
	struct goodix_berlin_fw_subsys subsys[GOODIX_BERLIN_FLASH_SUBSYS_MAX_NUM];
};

struct goodix_berlin_ic_data {
	int fw_version_info_addr;
	int ic_info_addr;
	ssize_t read_dummy_len;
	ssize_t read_prefix_len;
};

struct device;
struct input_id;
struct regmap;

int goodix_berlin_probe(struct device *dev, int irq, const struct input_id *id,
			struct regmap *regmap,
			const struct goodix_berlin_ic_data *ic_data);

extern const struct dev_pm_ops goodix_berlin_pm_ops;
extern const struct attribute_group *goodix_berlin_groups[];

#endif
