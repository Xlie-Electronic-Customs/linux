// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2020 Synaptics Incorporated.
 * Copyright (C) 2026 Luka Panio <lukapanio@gmail.com>
 */

#ifndef __SYNAPTICS_TCM_H_
#define __SYNAPTICS_TCM_H_

#include <linux/pm.h>
#include <linux/input/touchscreen.h>
#define SYNAPTICS_TCM_V1_MESSAGE_PADDING 0x5a
#define SYNAPTICS_TCM_V1_MESSAGE_MARKER 0xa5

#define SYNAPTICS_TCM_MSG_CRC_LENGTH (2)
#define SYNAPTICS_TCM_EXTRA_RC_LENGTH (1)

#define SYNAPTICS_TCM_MODE_APPLICATION_FIRMWARE 1

#define MAX_SIZE_CONFIG_ID (16)

#define CMD_GET_APPLICATION_INFO 0x20
#define CMD_GET_TOUCH_REPORT_CONFIG 0x25
#define CMD_ENABLE 0x05
#define CMD_DISABLE 0x06

struct synaptics_tcm_report_point {
	u8 id;
	__le16 x;
	__le16 y;
	u8 wx;
	u8 wy;
	u8 unknown;
} __packed;

enum synaptics_tcm_report_type {
	REPORT_IDENTIFY = 0x10,
	REPORT_TOUCH = 0x11,
	REPORT_DELTA = 0x12,
	REPORT_RAW = 0x13,
	REPORT_DEBUG = 0x14,
	REPORT_HBP_ACTIVE_FRAME = 0x23,
	REPORT_LOG = 0x9f,
	REPORT_POWER_STATE_INFO = 0xFE,
	REPORT_DIFF  = 0xaa,
};

enum touch_report_code {

	TOUCH_REPORT_END = 0x00,
	TOUCH_REPORT_FOREACH_ACTIVE_OBJECT = 0x01,
	TOUCH_REPORT_FOREACH_OBJECT = 0x02,
	TOUCH_REPORT_FOREACH_END = 0x03,
	TOUCH_REPORT_PAD_TO_NEXT_BYTE = 0x04,

	TOUCH_REPORT_TIMESTAMP = 0x05,
	TOUCH_REPORT_OBJECT_N_INDEX = 0x06,
	TOUCH_REPORT_OBJECT_N_CLASSIFICATION = 0x07,
	TOUCH_REPORT_OBJECT_N_X_POSITION = 0x08,
	TOUCH_REPORT_OBJECT_N_Y_POSITION = 0x09,
	TOUCH_REPORT_OBJECT_N_Z = 0x0a,
	TOUCH_REPORT_OBJECT_N_X_WIDTH = 0x0b,
	TOUCH_REPORT_OBJECT_N_Y_WIDTH = 0x0c,
	TOUCH_REPORT_OBJECT_N_TX_POSITION_TIXELS = 0x0d,
	TOUCH_REPORT_OBJECT_N_RX_POSITION_TIXELS = 0x0e,
	TOUCH_REPORT_0D_BUTTONS_STATE = 0x0f,
	TOUCH_REPORT_GESTURE_ID = 0x10,
	TOUCH_REPORT_FRAME_RATE = 0x11,
	TOUCH_REPORT_POWER_IM = 0x12,
	TOUCH_REPORT_CID_IM = 0x13,
	TOUCH_REPORT_RAIL_IM = 0x14,
	TOUCH_REPORT_CID_VARIANCE_IM = 0x15,
	TOUCH_REPORT_NSM_FREQUENCY_INDEX = 0x16,
	TOUCH_REPORT_NSM_STATE = 0x17,
	TOUCH_REPORT_NUM_OF_ACTIVE_OBJECTS = 0x18,
	TOUCH_REPORT_CPU_CYCLES_USED_SINCE_LAST_FRAME = 0x19,
	TOUCH_REPORT_FACE_DETECT = 0x1a,
	TOUCH_REPORT_GESTURE_DATA = 0x1b,
	TOUCH_REPORT_FORCE_MEASUREMENT = 0x1c,
	TOUCH_REPORT_FINGERPRINT_AREA_MEET = 0x1d,
	TOUCH_REPORT_SENSING_MODE = 0x1e,
	TOUCH_REPORT_KNOB_DATA = 0x24,
};

struct synaptics_tcm_application_info {
	unsigned char version[2];
	unsigned char status[2];
	unsigned char static_config_size[2];
	unsigned char dynamic_config_size[2];
	unsigned char app_config_start_write_block[2];
	unsigned char app_config_size[2];
	unsigned char max_touch_report_config_size[2];
	unsigned char max_touch_report_payload_size[2];
	unsigned char customer_config_id[MAX_SIZE_CONFIG_ID];
	unsigned char max_x[2];
	unsigned char max_y[2];
	unsigned char max_objects[2];
	unsigned char num_of_buttons[2];
	unsigned char num_of_image_rows[2];
	unsigned char num_of_image_cols[2];
	unsigned char has_hybrid_data[2];
	unsigned char num_of_force_elecs[2];
};

struct synaptics_tcm_identification_info {
	unsigned char version;
	unsigned char mode;
	unsigned char part_number[16];
	unsigned char build_id[4];
	unsigned char max_write_size[2];

	unsigned char max_read_size[2];
	unsigned char reserved[6];
};

struct synaptics_tcm_v1_message_header {
	unsigned char marker;
	unsigned char code;
	unsigned char length[2];
};

struct synaptics_tcm_bus_ops {
	void *dev;
	size_t max_xfer_size;
	int (*read)(struct synaptics_tcm_bus_ops *bus_ops, void *buf, size_t len);
	int (*write)(struct synaptics_tcm_bus_ops *bus_ops, void *buf, size_t len);
};

struct synaptics_tcm_core {
	struct device *dev;
	struct synaptics_tcm_bus_ops *bus;
	struct regulator *avdd;
	struct regulator *vdd;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *avdd_gpio;
	int irq;
	bool has_crc;
	bool has_rxtra_rc;
	size_t max_rx_size;
	u16 max_x;
	u16 max_y;
	struct input_dev *input;
	struct touchscreen_properties props;
	const char *firmware_name;
	unsigned int power_delay_ms;
	unsigned int reset_active_ms;
	unsigned int reset_delay_ms;
	unsigned int spi_byte_delay_us;
	unsigned int spi_block_delay_us;
	bool use_hbp_mode;
	int reset_on_state;
	unsigned char *touch_config;
	unsigned int touch_config_len;
	unsigned int max_objects;
	unsigned long active_slots;
	bool touch_config_valid;
};

int synaptics_tcm_probe(struct device *dev, int irq,
			struct synaptics_tcm_bus_ops *bus_ops);

#endif
