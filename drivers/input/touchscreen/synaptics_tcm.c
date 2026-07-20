// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2020 Synaptics Incorporated.
 * Copyright (C) 2026 Luka Panio <lukapanio@gmail.com>
 */

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/input/touchscreen.h>
#include <linux/input/mt.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/err.h>
#include <linux/minmax.h>
#include <linux/math.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include "synaptics_tcm.h"

#define TCM_OBJ_FINGER			1
#define TCM_OBJ_GLOVED			2

static int synaptics_tcm_power_on(struct synaptics_tcm_core *ts)
{
	int ret;

	if (ts->vdd) {
		ret = regulator_enable(ts->vdd);
		if (ret) {
			dev_err(ts->dev, "Failed to enable vdd: %d\n", ret);
			return ret;
		}
	}

	if (ts->avdd) {
		ret = regulator_enable(ts->avdd);
		if (ret) {
			dev_err(ts->dev, "Failed to enable avdd: %d\n", ret);
			if (ts->vdd)
				regulator_disable(ts->vdd);
			return ret;
		}
	} else if (ts->avdd_gpio) {
		gpiod_set_value_cansleep(ts->avdd_gpio, 1);
	}

	msleep(ts->power_delay_ms ? ts->power_delay_ms : 200);


	if (ts->reset_gpio) {
		gpiod_set_value_cansleep(ts->reset_gpio, ts->reset_on_state);
		msleep(ts->reset_active_ms ? ts->reset_active_ms : 10);
		gpiod_set_value_cansleep(ts->reset_gpio, !ts->reset_on_state);
		msleep(ts->reset_delay_ms ? ts->reset_delay_ms : 80);
	}

	return 0;
}

static void synaptics_tcm_power_off(struct synaptics_tcm_core *ts)
{
	if (ts->avdd)
		regulator_disable(ts->avdd);
	else if (ts->avdd_gpio)
		gpiod_set_value_cansleep(ts->avdd_gpio, 0);
	if (ts->vdd)
		regulator_disable(ts->vdd);
}

static int synaptics_tcm_read_header(struct synaptics_tcm_core *ts, unsigned char *buf) {
	int ret;
	ret = ts->bus->read(ts->bus, buf, 4);
	if (ret < 0) {
		dev_err(ts->dev, "Failed to read header: %d\n", ret);
		return ret;
	}
	return 0;
}

static int synaptics_tcm_write_cmd(struct synaptics_tcm_core *ts,
				   unsigned char cmd,
				   const unsigned char *payload, size_t plen)
{
	unsigned char *buf;
	size_t total = 3 + plen;
	int ret;

	buf = kzalloc(total, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = cmd;
	put_unaligned_le16((u16)plen, &buf[1]);
	if (payload && plen)
		memcpy(&buf[3], payload, plen);

	ret = ts->bus->write(ts->bus, buf, total);
	kfree(buf);
	return ret;
}

static int synaptics_tcm_continue_read(struct synaptics_tcm_core *ts, size_t len,
					unsigned char *buf) {
	int ret = 0;
	int i;
	size_t total_length, remaining_length, chunk_space, xfer_length, chunks;
	size_t offset, copy_len;
	unsigned char *tmp_buf;

	ret = 0;

	total_length = len + 1;

	if(ts->has_crc) {
		total_length += SYNAPTICS_TCM_MSG_CRC_LENGTH;
	}

	if(ts->has_rxtra_rc) {
		total_length += SYNAPTICS_TCM_EXTRA_RC_LENGTH + 1;
	}

	if(ts->has_crc || ts->has_rxtra_rc) {
		total_length += 1;
	}

	if(ts->max_rx_size == 0) {
		chunk_space = total_length;
	} else {
		chunk_space = ts->max_rx_size - 2;
	}

	chunks = DIV_ROUND_UP(total_length, chunk_space);
	if (chunks < 1)
		chunks = 1;
	remaining_length = total_length;
	tmp_buf = kzalloc(chunk_space + 2, GFP_KERNEL);
	if (!tmp_buf)
		return -ENOMEM;

	for (i = 0; i < chunks; i++) {
		xfer_length = min(remaining_length, chunk_space);
		offset = total_length - remaining_length;


		if (xfer_length == 1) {
			buf[total_length - remaining_length] = SYNAPTICS_TCM_V1_MESSAGE_PADDING;
			remaining_length -= xfer_length;
			continue;
		}

		ret = ts->bus->read(ts->bus, tmp_buf, xfer_length + 2);

		if (ret) {
			dev_err(ts->dev, "Failed to continue read: %d\n", ret);
			goto exit;
		}

		copy_len = min(xfer_length, len - offset);
		memcpy(buf + offset, tmp_buf + 2, copy_len);

		remaining_length -= xfer_length;
	}

exit:
	kfree(tmp_buf);
	return ret;

}

static int synaptics_tcm_identify_chip(struct synaptics_tcm_core *ts)
{
	int ret;
	struct synaptics_tcm_v1_message_header header;
	struct synaptics_tcm_identification_info chipid;

	unsigned char cmd = ts->use_hbp_mode ? 0x07 : 0x02;

	ret = synaptics_tcm_write_cmd(ts, cmd, NULL, 0);
	if (ret) {
		dev_err(ts->dev, "Failed to write identify cmd 0x%02x: %d\n",
			cmd, ret);
		return ret;
	}

	ret = synaptics_tcm_read_header(ts, (unsigned char *)&header);
	if (ret)
		return ret;

	if (header.marker != SYNAPTICS_TCM_V1_MESSAGE_MARKER) {
		dev_warn(ts->dev, "Invalid header marker 0x%x\n", header.marker);
		return -EINVAL;
	}

	if (header.code == REPORT_IDENTIFY ||
	    header.code == REPORT_HBP_ACTIVE_FRAME) {
		unsigned int len = get_unaligned_le16(header.length);

		ret = synaptics_tcm_continue_read(ts, len,
						  (unsigned char *)&chipid);
		if (ret) {
			dev_err(ts->dev, "Failed to read IDENTIFY payload: %d\n",
				ret);
			return ret;
		}
	}

	if (chipid.mode != 0 &&
	    chipid.mode != SYNAPTICS_TCM_MODE_APPLICATION_FIRMWARE)
		dev_info(ts->dev, "Chip mode: 0x%x\n", chipid.mode);

	return 0;
}

static int synaptics_tcm_get_fw_config(struct synaptics_tcm_core *ts)
{
	int ret;
	struct synaptics_tcm_v1_message_header header;
	struct synaptics_tcm_application_info appinfo;

	ret = synaptics_tcm_write_cmd(ts, CMD_GET_APPLICATION_INFO, NULL, 0);
	if (ret) {
		dev_err(ts->dev, "Failed to write CMD_GET_APPLICATION_INFO: %d\n", ret);
		return ret;
	}

	msleep(40);

	ret = synaptics_tcm_read_header(ts, (unsigned char *)&header);
	if (ret)
		return ret;

	if (get_unaligned_le16(header.length) + 2 <
	    sizeof(struct synaptics_tcm_application_info)) {
		dev_err(ts->dev, "Wrong message len for CMD_GET_APPLICATION_INFO\n");
		return -ENODATA;
	}

	ret = synaptics_tcm_continue_read(ts,
			sizeof(struct synaptics_tcm_application_info),
			(unsigned char *)&appinfo);
	if (ret) {
		dev_err(ts->dev, "Failed to read CMD_GET_APPLICATION_INFO: %d\n",
			ret);
		return ret;
	}

	ts->max_x = get_unaligned_le16(appinfo.max_x);
	ts->max_y = get_unaligned_le16(appinfo.max_y);
	ts->max_objects = get_unaligned_le16(appinfo.max_objects);
	if (ts->max_objects == 0)
		ts->max_objects = 10;

	dev_info(ts->dev,
		 "Synaptics TCM firmware version: %u status: %u max_x: %u max_y: %u\n",
		 get_unaligned_le16(appinfo.version),
		 get_unaligned_le16(appinfo.status), ts->max_x, ts->max_y);
	return 0;
}

#define REPORT_HDR_LEN 16
#define OBJ_LEN 8

static int synaptics_tcm_handle_touch_report(struct synaptics_tcm_core *ts,
					     unsigned char *payload,
					     unsigned int payload_len)
{
	int i, num_objs;
	struct synaptics_tcm_report_point *report;
	size_t data_off = 0;

	if (!ts->input || !payload || payload_len < 4)
		goto sync_out;


	if (payload_len > REPORT_HDR_LEN)
		data_off = REPORT_HDR_LEN;
	report = (struct synaptics_tcm_report_point *)(payload + data_off);
	num_objs = (payload_len - data_off) / OBJ_LEN;
	if (num_objs <= 0 || num_objs > 10) {

		data_off = 0;
		report = (struct synaptics_tcm_report_point *)payload;
		num_objs = payload_len / OBJ_LEN;
		if (num_objs <= 0 || num_objs > 10)
			goto sync_out;
	}


	if (payload_len > data_off + 1) {
		u8 n = payload[data_off];
		if (n > 0 && n <= 10 && (data_off + 1 + n * OBJ_LEN) <= (payload_len + 4)) {
			data_off += 1;
			report = (struct synaptics_tcm_report_point *)(payload + data_off);
			num_objs = n;
		}
	}

	for (i = 0; i < num_objs; i++) {
		u8 id = report[i].id & 0x0f;
		__le16 x = report[i].x;
		__le16 y = report[i].y;
		u8 wx = report[i].wx;
		u8 wy = report[i].wy;


		input_mt_slot(ts->input, id);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
		touchscreen_report_pos(ts->input, &ts->props, le16_to_cpu(x), le16_to_cpu(y), true);
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR, wx ? wx : 50);
		input_report_abs(ts->input, ABS_MT_TOUCH_MINOR, wy ? wy : 50);
	}

sync_out:
	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
	return 0;
}

#define MAX_TOUCH_OBJECTS	20

struct tcm_touch_point {
	u16 x;
	u16 y;
	u8 wx;
	u8 wy;
	bool active;
};

static unsigned int synaptics_tcm_get_touch_data(const unsigned char *buf,
						 unsigned int buf_len,
						 unsigned int bit_offset,
						 unsigned int bits)
{
	unsigned int data = 0;
	unsigned int remaining = bits;
	unsigned int bit_in_byte = bit_offset % 8;
	unsigned int byte_offset = bit_offset / 8;

	if (bits == 0)
		return 0;


	if (bit_offset + bits > buf_len * 8)
		return 0;


	while (remaining) {
		unsigned int chunk = min_t(unsigned int, remaining, 8 - bit_in_byte);
		unsigned int mask = (1u << chunk) - 1;
		unsigned int val = (buf[byte_offset] >> bit_in_byte) & mask;

		data |= val << (bits - remaining);
		remaining -= chunk;
		byte_offset++;
		bit_in_byte = 0;
	}
	return data;
}

static unsigned int synaptics_tcm_fetch_field(const unsigned char *payload,
					      unsigned int payload_len,
					      unsigned int *bit_offset,
					      unsigned int bits)
{
	unsigned int val = synaptics_tcm_get_touch_data(payload, payload_len,
							*bit_offset, bits);

	*bit_offset += bits;
	return val;
}

static int synaptics_tcm_preserve_touch_report_config(struct synaptics_tcm_core *ts)
{
	struct synaptics_tcm_v1_message_header header;
	unsigned char *cfg = NULL;
	unsigned int len;
	int ret;

	ret = synaptics_tcm_write_cmd(ts, CMD_GET_TOUCH_REPORT_CONFIG, NULL, 0);
	if (ret) {
		dev_warn(ts->dev, "GET_TOUCH_REPORT_CONFIG write failed: %d\n", ret);
		return ret;
	}

	msleep(40);

	ret = synaptics_tcm_read_header(ts, (unsigned char *)&header);
	if (ret)
		return ret;

	len = get_unaligned_le16(header.length);
	if (len == 0 || len > 1024) {
		dev_warn(ts->dev, "touch report config bad length %u\n", len);
		return -ENODATA;
	}

	cfg = kzalloc(len, GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	ret = synaptics_tcm_continue_read(ts, len, cfg);
	if (ret) {
		dev_warn(ts->dev, "touch report config read failed: %d\n", ret);
		kfree(cfg);
		return ret;
	}

	kfree(ts->touch_config);
	ts->touch_config = cfg;
	ts->touch_config_len = len;
	ts->touch_config_valid = true;

	dev_info(ts->dev, "touch report config preserved (%u bytes)\n", len);
	return 0;
}

static void synaptics_tcm_report_input(struct synaptics_tcm_core *ts,
				       struct tcm_touch_point *points,
				       unsigned int max);

static int synaptics_tcm_parse_touch_report(struct synaptics_tcm_core *ts,
					    unsigned char *payload,
					    unsigned int payload_len)
{
	const unsigned char *cfg = ts->touch_config;
	unsigned int cfg_len = ts->touch_config_len;
	struct tcm_touch_point points[MAX_TOUCH_OBJECTS];
	unsigned int bit_offset = 0;
	unsigned int i = 0;
	unsigned int obj_idx = 0;
	unsigned int foreach_start = 0;
	unsigned int max_objs;
	unsigned int bits, val;
	int num_active = 0;
	int active_processed = 0;
	int foreach_end = -1;
	bool active_only = false;
	bool have_num_active = false;
	bool in_foreach = false;

	if (!cfg || cfg_len == 0 || !payload || payload_len == 0)
		return -EINVAL;

	max_objs = min_t(unsigned int, ts->max_objects, MAX_TOUCH_OBJECTS);
	memset(points, 0, sizeof(points));


	while (i < cfg_len) {
		unsigned char code = cfg[i++];

		switch (code) {
		case TOUCH_REPORT_END:
			goto done;

		case TOUCH_REPORT_FOREACH_ACTIVE_OBJECT:
			active_only = true;
			in_foreach = true;
			foreach_start = i;
			active_processed = 0;
			break;

		case TOUCH_REPORT_FOREACH_OBJECT:
			active_only = false;
			in_foreach = true;
			foreach_start = i;
			active_processed = 0;
			break;

		case TOUCH_REPORT_FOREACH_END:
			foreach_end = i;
			if (!in_foreach)
				break;
			active_processed++;
			if (active_only) {
				if (have_num_active) {
					if (active_processed < num_active)
						i = foreach_start;
				} else if (bit_offset < payload_len * 8) {
					i = foreach_start;
				}
			} else if (active_processed < (int)max_objs) {
				i = foreach_start;
			}
			break;

		case TOUCH_REPORT_PAD_TO_NEXT_BYTE:
			bit_offset = DIV_ROUND_UP(bit_offset, 8) * 8;
			break;

		case TOUCH_REPORT_NUM_OF_ACTIVE_OBJECTS:
			if (i >= cfg_len)
				goto done;
			bits = cfg[i++];
			val = synaptics_tcm_fetch_field(payload, payload_len,
							&bit_offset, bits);
			num_active = val;
			have_num_active = true;
						if (num_active == 0) {
				if (foreach_end > 0)
					i = foreach_end;
				else
					goto done;
			}
			break;

		case TOUCH_REPORT_OBJECT_N_INDEX:
			if (i >= cfg_len)
				goto done;
			bits = cfg[i++];
			val = synaptics_tcm_fetch_field(payload, payload_len,
							&bit_offset, bits);
			obj_idx = val;
			if (obj_idx >= MAX_TOUCH_OBJECTS)
				obj_idx = MAX_TOUCH_OBJECTS - 1;
						points[obj_idx].active = true;
			break;

		case TOUCH_REPORT_OBJECT_N_CLASSIFICATION:
			if (i >= cfg_len)
				goto done;
			bits = cfg[i++];
			val = synaptics_tcm_fetch_field(payload, payload_len,
							&bit_offset, bits);
			if (obj_idx >= MAX_TOUCH_OBJECTS)
				break;
						points[obj_idx].active = (val == TCM_OBJ_FINGER ||
						  val == TCM_OBJ_GLOVED);
			break;

		case TOUCH_REPORT_OBJECT_N_X_POSITION:
			if (i >= cfg_len)
				goto done;
			bits = cfg[i++];
			val = synaptics_tcm_fetch_field(payload, payload_len,
							&bit_offset, bits);
			if (obj_idx < MAX_TOUCH_OBJECTS)
				points[obj_idx].x = val;
			break;

		case TOUCH_REPORT_OBJECT_N_Y_POSITION:
			if (i >= cfg_len)
				goto done;
			bits = cfg[i++];
			val = synaptics_tcm_fetch_field(payload, payload_len,
							&bit_offset, bits);
			if (obj_idx < MAX_TOUCH_OBJECTS)
				points[obj_idx].y = val;
			break;

		case TOUCH_REPORT_OBJECT_N_X_WIDTH:
			if (i >= cfg_len)
				goto done;
			bits = cfg[i++];
			val = synaptics_tcm_fetch_field(payload, payload_len,
							&bit_offset, bits);
			if (obj_idx < MAX_TOUCH_OBJECTS)
				points[obj_idx].wx = val;
			break;

		case TOUCH_REPORT_OBJECT_N_Y_WIDTH:
			if (i >= cfg_len)
				goto done;
			bits = cfg[i++];
			val = synaptics_tcm_fetch_field(payload, payload_len,
							&bit_offset, bits);
			if (obj_idx < MAX_TOUCH_OBJECTS)
				points[obj_idx].wy = val;
			break;

		default:

			if (i >= cfg_len)
				goto done;
			bits = cfg[i++];
			synaptics_tcm_fetch_field(payload, payload_len,
						  &bit_offset, bits);
			break;
		}
	}

done:
	synaptics_tcm_report_input(ts, points, MAX_TOUCH_OBJECTS);
	return 0;
}

static void synaptics_tcm_report_input(struct synaptics_tcm_core *ts,
				       struct tcm_touch_point *points,
				       unsigned int max)
{
	unsigned long cur_slots = 0;
	unsigned int i;
	unsigned int nslots = min_t(unsigned int, ts->max_objects, max);

	if (!ts->input)
		return;

	for (i = 0; i < nslots; i++) {
		if (!points[i].active)
			continue;
		input_mt_slot(ts->input, i);
		input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, true);
		touchscreen_report_pos(ts->input, &ts->props,
				       points[i].x, points[i].y, true);
		input_report_abs(ts->input, ABS_MT_TOUCH_MAJOR,
				 points[i].wx ? points[i].wx : 50);
		input_report_abs(ts->input, ABS_MT_TOUCH_MINOR,
				 points[i].wy ? points[i].wy : 50);
		__set_bit(i, &cur_slots);
	}

	for_each_set_bit(i, &ts->active_slots, nslots) {
		if (!test_bit(i, &cur_slots)) {
			input_mt_slot(ts->input, i);
			input_mt_report_slot_state(ts->input, MT_TOOL_FINGER, false);
		}
	}
	ts->active_slots = cur_slots;

	input_mt_sync_frame(ts->input);
	input_sync(ts->input);
}

static int synaptics_tcm_enable_report(struct synaptics_tcm_core *ts,
				       unsigned char report_code, bool enable)
{
	unsigned char payload = report_code;
	unsigned char cmd = enable ? CMD_ENABLE : CMD_DISABLE;

	return synaptics_tcm_write_cmd(ts, cmd, &payload, 1);
}

static irqreturn_t synaptics_tcm_irq(int irq, void *data)
{
	int ret = 0;
	unsigned int payload_length = 0;
	unsigned char *payload = NULL;
	struct synaptics_tcm_v1_message_header header;
	struct synaptics_tcm_core *ts = data;

	ret = synaptics_tcm_read_header(ts, (unsigned char *)&header);
	if (ret) {
		dev_err(ts->dev, "Failed to read header: %d\n", ret);
		return IRQ_NONE;
	}

	payload_length = get_unaligned_le16(header.length);


	if (payload_length > 0) {
		payload = kzalloc(payload_length, GFP_KERNEL);
		if (!payload)
			return IRQ_NONE;
		ret = synaptics_tcm_continue_read(ts, payload_length, payload);
		if (ret) {
			dev_err(ts->dev, "Failed to read payload: %d\n", ret);
			kfree(payload);
			return IRQ_NONE;
		}
	}

	if ((header.code == REPORT_TOUCH || header.code == REPORT_HBP_ACTIVE_FRAME) &&
	    payload_length > 0 && payload) {
		if (ts->touch_config_valid)
			synaptics_tcm_parse_touch_report(ts, payload, payload_length);
		else
			synaptics_tcm_handle_touch_report(ts, payload, payload_length);
	} else if (header.code == REPORT_IDENTIFY) {
		dev_dbg(ts->dev, "Received identify report (len=%u)\n", payload_length);
	}

	kfree(payload);
	return IRQ_HANDLED;
}

static int synaptics_tcm_input_dev_config(struct synaptics_tcm_core *ts)
{
	int ret;

	ts->input = devm_input_allocate_device(ts->dev);
	if (!ts->input)
		return -ENOMEM;

	input_set_drvdata(ts->input, ts);

	ts->input->name = "Synaptics TCM Capacitive TouchScreen";
	ts->input->phys = "input/ts";

	ts->input->id.bustype = BUS_SPI;

	input_set_abs_params(ts->input, ABS_MT_POSITION_X, 0, ts->max_x, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_POSITION_Y, 0, ts->max_y, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(ts->input, ABS_MT_TOUCH_MINOR, 0, 255, 0, 0);

	touchscreen_parse_properties(ts->input, true, &ts->props);

	ret = input_mt_init_slots(ts->input,
				  min_t(unsigned int, ts->max_objects, MAX_TOUCH_OBJECTS),
				  INPUT_MT_DIRECT | INPUT_MT_DROP_UNUSED);
	if (ret)
		return ret;

	ret = input_register_device(ts->input);
	if (ret)
		return ret;

	return 0;
}

static void synaptics_tcm_parse_dt(struct device *dev, struct synaptics_tcm_core *ts)
{
	struct device_node *np = dev->of_node;
	const char *name;
	u32 val;

	ts->power_delay_ms = 200;
	ts->reset_active_ms = 10;
	ts->reset_delay_ms = 80;
	ts->spi_byte_delay_us = 0;
	ts->spi_block_delay_us = 0;
	ts->use_hbp_mode = false;
	ts->reset_on_state = 1;
	ts->max_objects = 10;

	if (!np)
		return;

	if (!of_property_read_string(np, "firmware-name", &name) ||
	    !of_property_read_string(np, "firmware_name", &name))
		ts->firmware_name = name;

	if (!of_property_read_u32(np, "synaptics,power-delay-ms", &val) ||
	    !of_property_read_u32(np, "power-delay-ms", &val))
		ts->power_delay_ms = val;

	if (!of_property_read_u32(np, "synaptics,reset-active-ms", &val))
		ts->reset_active_ms = val;
	if (!of_property_read_u32(np, "synaptics,reset-delay-ms", &val))
		ts->reset_delay_ms = val;



	if (!of_property_read_u32(np, "synaptics,spi-byte-delay-us", &val))
		ts->spi_byte_delay_us = val;
	if (!of_property_read_u32(np, "synaptics,spi-block-delay-us", &val))
		ts->spi_block_delay_us = val;

	if (of_device_is_compatible(np, "synaptics,tcm-spi-hbp") ||
	    of_device_is_compatible(np, "synaptics-s3910") ||
	    of_property_read_bool(np, "hbp,devices"))
		ts->use_hbp_mode = true;


	ts->avdd_gpio = devm_gpiod_get_optional(dev, "synaptics,avdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ts->avdd_gpio))
		ts->avdd_gpio = NULL;
}

int synaptics_tcm_probe(struct device *dev, int irq,
			struct synaptics_tcm_bus_ops *bus_ops)
{
	struct synaptics_tcm_core *ts;
	int ret;

	ts = devm_kzalloc(dev, sizeof(*ts), GFP_KERNEL);
	if (!ts)
		return -ENOMEM;

	if (!bus_ops)
		return -EINVAL;

	ts->dev = dev;
	ts->irq = irq;
	ts->bus = bus_ops;
	dev_set_drvdata(dev, ts);

	synaptics_tcm_parse_dt(dev, ts);

	ts->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ts->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ts->reset_gpio),
				     "Failed to request reset gpio\n");

	ts->avdd = devm_regulator_get_optional(dev, "avdd");
	if (IS_ERR(ts->avdd)) {
		ts->avdd = devm_regulator_get_optional(dev, "avdd-supply");
		if (IS_ERR(ts->avdd)) {
			ts->avdd = devm_regulator_get_optional(dev, "synaptics,avdd-supply");
			if (IS_ERR(ts->avdd))
				ts->avdd = NULL;
		}
	}

	ts->vdd = devm_regulator_get_optional(dev, "vdd");
	if (IS_ERR(ts->vdd)) {
		ts->vdd = devm_regulator_get_optional(dev, "vdd-supply");
		if (IS_ERR(ts->vdd)) {
			ts->vdd = devm_regulator_get_optional(dev, "synaptics,vdd-name");
			if (IS_ERR(ts->vdd))
				ts->vdd = NULL;
		}
	}

	ret = synaptics_tcm_power_on(ts);
	if (ret)
		dev_err(dev, "power on failed: %d\n", ret);

	ret = synaptics_tcm_identify_chip(ts);
	if (ret)
		dev_warn(dev, "identify failed: %d\n", ret);

	ret = synaptics_tcm_get_fw_config(ts);
	if (ret)
		dev_warn(dev, "get app info failed: %d\n", ret);

	if (ts->max_x == 0)
		ts->max_x = 12640;
	if (ts->max_y == 0)
		ts->max_y = 27800;

		ret = synaptics_tcm_preserve_touch_report_config(ts);
	if (ret)
		dev_warn(dev, "touch report config fetch failed: %d\n", ret);

	ret = synaptics_tcm_input_dev_config(ts);
	if (ret) {
		dev_err(dev, "input device setup failed: %d\n", ret);
		return ret;
	}

		ret = devm_request_threaded_irq(dev, ts->irq, NULL, synaptics_tcm_irq,
					IRQF_ONESHOT | IRQF_TRIGGER_LOW,
					"synaptics-tcm", ts);
	if (ret) {
		dev_err(dev, "request threaded irq failed: %d\n", ret);
		return ret;
	}

		ret = synaptics_tcm_enable_report(ts, REPORT_TOUCH, true);
	if (ret)
		dev_warn(dev, "enable touch report failed: %d\n", ret);

	return 0;
}
EXPORT_SYMBOL_GPL(synaptics_tcm_probe);

static int __maybe_unused synaptics_tcm_suspend(struct device *dev)
{
	struct synaptics_tcm_core *ts = dev_get_drvdata(dev);
	if (!ts)
		return 0;

	disable_irq(ts->irq);
	synaptics_tcm_enable_report(ts, REPORT_TOUCH, false);
	synaptics_tcm_power_off(ts);
	return 0;
}

static int __maybe_unused synaptics_tcm_resume(struct device *dev)
{
	struct synaptics_tcm_core *ts = dev_get_drvdata(dev);
	int ret;
	if (!ts)
		return 0;

	ret = synaptics_tcm_power_on(ts);
	if (ret)
		dev_warn(dev, "resume power on failed: %d\n", ret);

		synaptics_tcm_preserve_touch_report_config(ts);

	enable_irq(ts->irq);
	synaptics_tcm_enable_report(ts, REPORT_TOUCH, true);
	return 0;
}

EXPORT_GPL_SIMPLE_DEV_PM_OPS(synaptics_tcm_pm_ops,
			     synaptics_tcm_suspend, synaptics_tcm_resume);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synaptics TCM SPI Touchscreen driver");
MODULE_AUTHOR("Luka Panio <lukapanio@gmail.com>");
