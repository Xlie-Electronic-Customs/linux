// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/gpio.h>
#include <linux/kthread.h>
#include <linux/interrupt.h>
#include "synaptics_tcm_oncell.h"


static struct syna_tcm_data *g_tcm_info[TP_SUPPORT_MAX] = {NULL};

extern struct device_hcd *syna_remote_device_init(
	struct syna_tcm_data *tcm_info);
extern int syna_remote_device_destory(struct syna_tcm_data *tcm_info);

static int syna_tcm_write_message(struct syna_tcm_data *tcm_info,
				  unsigned char command, unsigned char *payload,
				  unsigned int length, unsigned char **resp_buf,
				  unsigned int *resp_buf_size, unsigned int *resp_length,
				  unsigned int polling_delay_ms);
static void syna_tcm_test_report(struct syna_tcm_data *tcm_info);
static int syna_tcm_helper(struct syna_tcm_data *tcm_info);
static int syna_tcm_enable_report(struct syna_tcm_data *tcm_info,
				  enum report_type report_type, bool enable);

#define SYNA_TRANSFER_READ 1
#define SYNA_TRANSFER_WRITE 0
#define SYNA_PRINT_BUF_SIZE 32
static void syna_print_transfer_data(unsigned char *data, unsigned int length, unsigned int wr_rd)
{
	//char *print_buf = NULL;
	int i, cnt, offset, left;
	unsigned char print_buf[SYNA_PRINT_BUF_SIZE];

	if (!data)
		goto exit;

	//To reduce the log print, Do not print the report data except identify
	if ((data[0] == MESSAGE_MARKER) && (data[1] > REPORT_IDENTIFY))
		goto exit;

	offset = 0;
	cnt = snprintf(print_buf + offset, SYNA_PRINT_BUF_SIZE - offset, "%s",
			(wr_rd == SYNA_TRANSFER_READ) ? "R:" : "W:");
	offset += cnt;
	for (i = 0; i < length; i++) {
		left = SYNA_PRINT_BUF_SIZE - offset;
		if (left <= 5) {
			break;
		}
		cnt = snprintf(print_buf + offset, SYNA_PRINT_BUF_SIZE - offset, "%02x ", data[i]);
		offset += cnt;
	}
	cnt = snprintf(print_buf + offset, SYNA_PRINT_BUF_SIZE - offset, "\n");
	offset += cnt;
	TPD_INFO("%s", print_buf);

exit:
	return;
}

/**
 * touch_i2c_continue_read - Using for "read sequence bytes" through IIC
 * @client: Handle to slave device
 * @length: data size we want to read
 * @data: data read from IIC
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
static int touch_i2c_continue_read(struct i2c_client *client, unsigned short length,
			    unsigned char *data)
{
	int retval = 0;
	unsigned char retry;
	struct i2c_msg msg;
	struct touchpanel_data *ts;
	ts = i2c_get_clientdata(client);

	msg.addr = client->addr;
	msg.flags = I2C_M_RD;
	msg.len = length;
	msg.buf = data;

	for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
		if (i2c_transfer(client->adapter, &msg, 1) == 1) {
			retval = length;
			break;
		}
		msleep(20);
	}

	if (retry == MAX_I2C_RETRY_TIME) {
		TPD_INFO("%s: I2C read over retry limit\n", __func__);
		retval = -EIO;
	}

	return retval;
}

/**
 * touch_i2c_read - Using for "read data from ic after writing or not" through IIC
 * @client: Handle to slave device
 * @writebuf: buf to write
 * @writelen: data size we want to send
 * @readbuf:  buf we want save data
 * @readlen:  data size we want to receive
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer msg length(transfer success) or most likely negative errno(transfer EIO error)
 */
static inline int touch_i2c_read(struct i2c_client *client, char *writebuf,
			  unsigned short writelen, char *readbuf, unsigned short readlen)
{
	int retval;
	unsigned char retry;
	struct i2c_msg msg[2];
	struct i2c_msg message;
	struct touchpanel_data *ts = NULL;

	if (!client) {
		dump_stack();
		return -1;
	}

	ts = i2c_get_clientdata(client);
	if (!ts) {
		dump_stack();
		return -1;
	}

	mutex_lock(&ts->interface_data.bus_mutex);

	/*for writebuf buffer min is  FIX_I2C_LENGTH*/
	if (writelen > FIX_I2C_LENGTH) {
		if (ts->interface_data.read_w_buf_size < writelen) {
			if (ts->interface_data.read_w_buffer) {
				devm_kfree(&client->dev, (void **)&ts->interface_data.read_w_buffer);
				TPD_INFO("read w block_1, free once.\n");
			}

			ts->interface_data.read_w_buffer = devm_kzalloc(&client->dev, writelen,
							   GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.read_w_buffer) {
				ts->interface_data.read_w_buf_size = 0;
				TPD_INFO("read w block_1, kzalloc failed(len:%d, buf_size:%d).\n", writelen,
					 ts->interface_data.read_w_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.read_w_buf_size = writelen;
			TPD_INFO("read w block_1, kzalloc success(len:%d, buf_size:%d).\n", writelen,
				 ts->interface_data.read_w_buf_size);

		} else {
			memset(ts->interface_data.read_w_buffer, 0, writelen);
		}

	} else {
		if (ts->interface_data.read_w_buf_size > FIX_I2C_LENGTH) {
			devm_kfree(&client->dev, (void **)&ts->interface_data.read_w_buffer);
			ts->interface_data.read_w_buffer = devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
							   GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.read_w_buffer) {
				ts->interface_data.read_w_buf_size = 0;
				TPD_INFO("read w block_2, kzalloc failed(len:%d, buf_size:%d).\n", writelen,
					 ts->interface_data.read_w_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.read_w_buf_size = FIX_I2C_LENGTH;
			TPD_INFO("read w block_2, kzalloc success(len:%d, buf_size:%d).\n", writelen,
				 ts->interface_data.read_w_buf_size);

		} else {
			if (!ts->interface_data.read_w_buffer) {
				ts->interface_data.read_w_buffer = devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
								   GFP_KERNEL | GFP_DMA);

				if (!ts->interface_data.read_w_buffer) {
					ts->interface_data.read_w_buf_size = 0;
					TPD_INFO("read w block_3, kzalloc failed(len:%d, buf_size:%d).\n", writelen,
						 ts->interface_data.read_w_buf_size);
					mutex_unlock(&ts->interface_data.bus_mutex);
					return -ENOMEM;
				}

				ts->interface_data.read_w_buf_size = FIX_I2C_LENGTH;
				TPD_INFO("read w block_3, kzalloc success(len:%d, buf_size:%d).\n", writelen,
					 ts->interface_data.read_w_buf_size);

			} else {
				memset(ts->interface_data.read_w_buffer, 0, writelen);
			}
		}
	}

	memcpy(ts->interface_data.read_w_buffer, writebuf, writelen);

	/*for readbuf buffer min is  FIX_I2C_LENGTH*/
	if (readlen > FIX_I2C_LENGTH) {
		if (ts->interface_data.read_buf_size < readlen) {
			if (ts->interface_data.read_buf) {
				devm_kfree(&client->dev, (void **)&ts->interface_data.read_buf);
				TPD_INFO("read block_1, free once.\n");
			}

			ts->interface_data.read_buf = devm_kzalloc(&client->dev, readlen,
						      GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.read_buf) {
				ts->interface_data.read_buf_size = 0;
				TPD_INFO("read block_1, kzalloc failed(len:%d, buf_size:%d).\n", readlen,
					 ts->interface_data.read_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.read_buf_size = readlen;
			TPD_INFO("read block_1, kzalloc success(len:%d, buf_size:%d).\n", readlen,
				 ts->interface_data.read_buf_size);

		} else {
			memset(ts->interface_data.read_buf, 0, readlen);
		}

	} else {
		if (ts->interface_data.read_buf_size > FIX_I2C_LENGTH) {
			devm_kfree(&client->dev, (void **)&ts->interface_data.read_buf);
			ts->interface_data.read_buf = devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
						      GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.read_buf) {
				ts->interface_data.read_buf_size = 0;
				TPD_INFO("read block_2, kzalloc failed(len:%d, buf_size:%d).\n", readlen,
					 ts->interface_data.read_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.read_buf_size = FIX_I2C_LENGTH;
			TPD_INFO("read block_2, kzalloc success(len:%d, buf_size:%d).\n", readlen,
				 ts->interface_data.read_buf_size);

		} else {
			if (!ts->interface_data.read_buf) {
				ts->interface_data.read_buf = devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
							      GFP_KERNEL | GFP_DMA);

				if (!ts->interface_data.read_buf) {
					ts->interface_data.read_buf_size = 0;
					TPD_INFO("read block_3, kzalloc failed(len:%d, buf_size:%d).\n", readlen,
						 ts->interface_data.read_buf_size);
					mutex_unlock(&ts->interface_data.bus_mutex);
					return -ENOMEM;
				}

				ts->interface_data.read_buf_size = FIX_I2C_LENGTH;
				TPD_INFO("read block_3, kzalloc success(len:%d, buf_size:%d).\n", readlen,
					 ts->interface_data.read_buf_size);

			} else {
				memset(ts->interface_data.read_buf, 0, readlen);
			}
		}
	}
	if (writelen > 0) {
		msg[0].addr = client->addr;
		msg[0].flags = 0;
		msg[0].buf = ts->interface_data.read_w_buffer;
		msg[0].len = writelen;

		msg[1].addr = client->addr;
		msg[1].flags = I2C_M_RD;
		msg[1].len = readlen;
		msg[1].buf = ts->interface_data.read_buf;

		for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
			if (i2c_transfer(client->adapter, msg, 2) == 2) {
				retval = readlen;
				break;
			}

			msleep(20);
		}
	} else {
		message.addr = client->addr;
		message.flags = I2C_M_RD;
		message.len = readlen;
		message.buf = ts->interface_data.read_buf;
		for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
			if (i2c_transfer(client->adapter, &message, 1) == 1) {
				retval = 1;
				break;
			}

			msleep(20);
		}
	}

	if (retry == MAX_I2C_RETRY_TIME) {
		TPD_INFO("%s: I2C read over retry limit\n", __func__);
		retval = -EIO;
	}

	memcpy(readbuf, ts->interface_data.read_buf, readlen);

	mutex_unlock(&ts->interface_data.bus_mutex);
	return retval;
}

/**
 * touch_i2c_read_block - Using for "read word" through IIC
 * @client: Handle to slave device
 * @addr: addr to write
 * @length: data size we want to send
 * @data: data we want to send
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
static int touch_i2c_read_block(struct i2c_client *client, u16 addr,
			 unsigned short length, unsigned char *data)
{
	int retval = 0;
	unsigned char buffer[2] = {(addr >> 8) & 0xff, addr & 0xff};
	struct touchpanel_data *ts = NULL;

	if (!client) {
		dump_stack();
		return -1;
	}

	ts = i2c_get_clientdata(client);
	if (!ts) {
		dump_stack();
		return -1;
	}

	if (!ts->interface_data.register_is_16bit) { /* if register is 8bit*/
		retval = touch_i2c_read(client, &buffer[1], 1, data, length);

	} else {
		retval = touch_i2c_read(client, buffer, 2, data, length);
	}

	return retval;
}

/**
 * touch_i2c_write_block - Using for "read word" through IIC
 * @client: Handle to slave device
 * @addr: addr to write
 * @length: data size we want to send
 * @data: data we want to send
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
static int touch_i2c_write_block(struct i2c_client *client, u16 addr,
			  unsigned short length, unsigned char const *data)
{
	int retval = 0;
	unsigned char retry;
	unsigned int total_length = 0;
	struct i2c_msg msg[1];
	struct touchpanel_data *ts = NULL;

	if (!client) {
		dump_stack();
		return -1;
	}

	ts = i2c_get_clientdata(client);
	if (!ts) {
		dump_stack();
		return -1;
	}

	mutex_lock(&ts->interface_data.bus_mutex);

	total_length = length + (ts->interface_data.register_is_16bit ? 2 : 1);

	if (total_length > FIX_I2C_LENGTH) {
		if (ts->interface_data.write_buf_size < total_length) {
			if (ts->interface_data.write_buf) {
				devm_kfree(&client->dev, (void **)&ts->interface_data.write_buf);
				TPD_INFO("write block_1, free once.\n");
			}

			ts->interface_data.write_buf = devm_kzalloc(&client->dev, total_length,
						       GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.write_buf) {
				ts->interface_data.write_buf_size = 0;
				TPD_INFO("write block_1, kzalloc failed(len:%d, buf_size:%d).\n", total_length,
					 ts->interface_data.write_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.write_buf_size = total_length;
			TPD_INFO("write block_1, kzalloc success(len:%d, buf_size:%d).\n", total_length,
				 ts->interface_data.write_buf_size);

		} else {
			memset(ts->interface_data.write_buf, 0, total_length);
		}

	} else {
		if (ts->interface_data.write_buf_size > FIX_I2C_LENGTH) {
			devm_kfree(&client->dev, (void **)&ts->interface_data.write_buf);
			ts->interface_data.write_buf = devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
						       GFP_KERNEL | GFP_DMA);

			if (!ts->interface_data.write_buf) {
				ts->interface_data.write_buf_size = 0;
				TPD_INFO("write block_2, kzalloc failed(len:%d, buf_size:%d).\n", total_length,
					 ts->interface_data.write_buf_size);
				mutex_unlock(&ts->interface_data.bus_mutex);
				return -ENOMEM;
			}

			ts->interface_data.write_buf_size = FIX_I2C_LENGTH;
			TPD_INFO("write block_2, kzalloc success(len:%d, buf_size:%d).\n", total_length,
				 ts->interface_data.write_buf_size);

		} else {
			if (!ts->interface_data.write_buf) {
				ts->interface_data.write_buf = devm_kzalloc(&client->dev, FIX_I2C_LENGTH,
							       GFP_KERNEL | GFP_DMA);

				if (!ts->interface_data.write_buf) {
					ts->interface_data.write_buf_size = 0;
					TPD_INFO("write block_3, kzalloc failed(len:%d, buf_size:%d).\n", total_length,
						 ts->interface_data.write_buf_size);
					mutex_unlock(&ts->interface_data.bus_mutex);
					return -ENOMEM;
				}

				ts->interface_data.write_buf_size = FIX_I2C_LENGTH;
				TPD_INFO("write block_3, kzalloc success(len:%d, buf_size:%d).\n", total_length,
					 ts->interface_data.write_buf_size);

			} else {
				memset(ts->interface_data.write_buf, 0, total_length);
			}
		}
	}

	msg[0].addr = client->addr;
	msg[0].flags = 0;
	msg[0].buf = ts->interface_data.write_buf;

	if (!ts->interface_data.register_is_16bit) { /* if register is 8bit*/
		msg[0].len = length + 1;
		msg[0].buf[0] = addr & 0xff;

		memcpy(&ts->interface_data.write_buf[1], &data[0], length);

	} else {
		msg[0].len = length + 2;
		msg[0].buf[0] = (addr >> 8) & 0xff;
		msg[0].buf[1] = addr & 0xff;

		memcpy(&ts->interface_data.write_buf[2], &data[0], length);
	}

	for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
		if (i2c_transfer(client->adapter, msg, 1) == 1) {
			retval = length;
			break;
		}

		msleep(20);
	}

	if (retry == MAX_I2C_RETRY_TIME) {
		TPD_INFO("%s: I2C write over retry limit\n", __func__);
		retval = -EIO;
	}

	mutex_unlock(&ts->interface_data.bus_mutex);
	return retval;
}

/**
 * touch_i2c_continue_write - Using for "write sequence bytes" through IIC
 * @client: Handle to slave device
 * @length: data size we want to write
 * @data: data write to IIC
 *
 * Actully, This function call i2c_transfer for IIC transfer,
 * Returning transfer length(transfer success) or most likely negative errno(transfer error)
 */
static int touch_i2c_continue_write(struct i2c_client *client, unsigned short length,
			     unsigned char *data)
{
	int retval = 0;
	unsigned char retry;
	struct i2c_msg msg;
	struct touchpanel_data *ts;
	ts = i2c_get_clientdata(client);

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = data;
	msg.len = length;

	for (retry = 0; retry < MAX_I2C_RETRY_TIME; retry++) {
		if (i2c_transfer(client->adapter, &msg, 1) == 1) {
			retval = length;
			break;
		}

		msleep(20);
	}

	if (retry == MAX_I2C_RETRY_TIME) {
		TPD_INFO("%s: I2C write over retry limit\n", __func__);
		retval = -EIO;
	}

	return retval;
}

inline int syna_tcm_rmi_read(struct syna_tcm_data *tcm_info,
			     unsigned short addr, unsigned char *data, unsigned int length)
{
	int retval = 0;
	unsigned short tmp_addr = tcm_info->client->addr;

	tcm_info->client->addr = tcm_info->ubl_addr;
	retval = touch_i2c_read_block(tcm_info->client, addr, length, data);
	tcm_info->client->addr = tmp_addr;

	return retval;
}

inline int syna_tcm_rmi_write(struct syna_tcm_data *tcm_info,
			      unsigned short addr, unsigned char *data, unsigned int length)
{
	int retval = 0;
	unsigned short tmp_addr = tcm_info->client->addr;

	tcm_info->client->addr = tcm_info->ubl_addr;
	retval = touch_i2c_write_block(tcm_info->client, addr, length, data);
	tcm_info->client->addr = tmp_addr;

	return retval;
}

static inline int syna_tcm_read(struct syna_tcm_data *tcm_info,
				unsigned char *data, unsigned int length)
{
	int retval = 0;
	retval = touch_i2c_continue_read(tcm_info->client, length, data);
	if (retval < 0)
		goto exit;

	if (*tcm_info->loading_fw) {
		syna_print_transfer_data(data, length, SYNA_TRANSFER_READ);
	}

exit:
    return retval;
}

static inline int syna_tcm_write(struct syna_tcm_data *tcm_info,
				 unsigned char *data, unsigned int length)
{

	if (*tcm_info->loading_fw) {
	    syna_print_transfer_data(data, length, SYNA_TRANSFER_WRITE);
	}
	return touch_i2c_continue_write(tcm_info->client, length, data);
}

/**
 * syna_get_report_data - Retrieve data from touch report
 *
 * @tcm_info: handle of tcm module
 * @offset: start bit of retrieved data
 * @bits: total bits of retrieved data
 * @data: pointer of data, at most 4 byte
 * Retrieve data from the touch report based on the bit offset and bit length
 * information from the touch report configuration.
 */
static int syna_get_report_data(struct syna_tcm_data *tcm_info,
				unsigned int offset,
				unsigned int bits, unsigned int *data)
{
	unsigned char mask = 0;
	unsigned char byte_data = 0;
	unsigned int output_data = 0;
	unsigned int bit_offset = offset % 8;
	unsigned int byte_offset = offset / 8;
	unsigned int data_bits = 0;
	unsigned int available_bits = 0;
	unsigned int remaining_bits = bits;
	unsigned char *touch_report = tcm_info->report.buffer.buf;
	int retval = 0;

	if (bits == 0 || bits > 32) {
		TPD_DEBUG("larger than 32 bits:%d\n", bits);
		retval = tp_memcpy((unsigned char *)data, bits / 8, &touch_report[byte_offset],
				   bits / 8, bits / 8);

		if (retval < 0) {
			TPD_INFO("Failed to copy report data\n");
			return retval;
		}

		return 0;
	}

	if (offset + bits > tcm_info->report.buffer.data_length * 8) {
		TPD_DEBUG("offset and bits beyond total read length\n");
		*data = 0;
		return 0;
	}

	while (remaining_bits) {
		byte_data = touch_report[byte_offset];
		byte_data >>= bit_offset;

		available_bits = 8 - bit_offset;
		data_bits = MIN(available_bits, remaining_bits);
		mask = 0xff >> (8 - data_bits);

		byte_data &= mask;

		output_data |= byte_data << (bits - remaining_bits);

		bit_offset = 0;
		byte_offset += 1;
		remaining_bits -= data_bits;
	}

	*data = output_data;

	return 0;
}

/**
 * touch_parse_report() - Parse touch report
 *
 * Traverse through the touch report configuration and parse the touch report
 * generated by the device accordingly to retrieve the touch data.
 */
static int syna_parse_report(struct syna_tcm_data *tcm_info)
{
	int i = 0;
	int retval = 0;
	bool active_only = false, num_of_active_objects = false;
	unsigned char code;
	unsigned int size = 0, idx = 0, obj = 0;
	unsigned int next = 0, data = 0, bits = 0, offset = 0, objects = 0;
	unsigned char  grip_data[4];
	unsigned int active_objects = 0;
	unsigned int report_size = 0, config_size = 0;
	unsigned char *config_data = NULL;
	struct touch_hcd *touch_hcd = NULL;
	struct touch_data *touch_data = NULL;
	struct object_data *object_data = NULL;
	static unsigned int end_of_foreach = 0;

	touch_hcd = tcm_info->touch_hcd;
	touch_data = &touch_hcd->touch_data;
	object_data = touch_hcd->touch_data.object_data;
	config_data = tcm_info->config.buf;
	config_size = tcm_info->config.data_length;
	report_size = tcm_info->report.buffer.data_length;
	size = sizeof(*object_data) * touch_hcd->max_objects;
	memset(touch_hcd->touch_data.object_data, 0x00, size);

	while (idx < config_size) {
		code = config_data[idx++];
		switch (code) {
		case TOUCH_END:
			goto exit;

		case TOUCH_FOREACH_ACTIVE_OBJECT:
			obj = 0;
			next = idx;
			active_only = true;
			break;

		case TOUCH_FOREACH_OBJECT:
			obj = 0;
			next = idx;
			active_only = false;
			break;

		case TOUCH_FOREACH_END:
			end_of_foreach = idx;

			if (active_only) {
				if (num_of_active_objects) {
					objects++;

					if (objects < active_objects) {
						idx = next;
					}

				} else if (offset < report_size * 8) {
					idx = next;
				}

			} else {
				obj++;

				if (obj < touch_hcd->max_objects) {
					idx = next;
				}
			}

			break;

		case TOUCH_PAD_TO_NEXT_BYTE:
			offset = ceil_div(offset, 8) * 8;
			break;

		case TOUCH_TIMESTAMP:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get timestamp\n");
				return retval;
			}

			touch_data->timestamp = data;
			offset += bits;
			break;

		case TOUCH_OBJECT_N_INDEX:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &obj);

			if (retval < 0) {
				TPD_INFO("Failed to get object index\n");
				return retval;
			}

			if (obj >= touch_hcd->max_objects) {
				TPD_INFO("Object index error 0x%0X\n", obj);
				return -1;
			}

			offset += bits;
			break;

		case TOUCH_OBJECT_N_CLASSIFICATION:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);
			if (tcm_info->ts->palm_to_sleep_enable
				&& data == PALM_FLAG
				&& !tcm_info->ts->is_suspended) {
				TPD_DEBUG("%s:detect palm,now to sleep\n", __func__);
				tcm_info->palm_to_sleep_state = PALM_TO_SLEEP;
			} else if (data == PALM_FLAG) {
				tcm_info->palm_hold_report = 1;
			} else if (data == 0) {
				tcm_info->palm_hold_report = 0;
			}

			if (retval < 0) {
				TPD_INFO("Failed to get object classification\n");
				return retval;
			}
			if (obj >= touch_hcd->max_objects) {
				TPD_INFO("obj> max_obj!! obj[%d]Report Data[%d]:", obj, report_size);
				if (tp_debug != 0) {
					for (i = 0; i < report_size; i++) {
						TPD_INFO("syna data:[0x%2x]", tcm_info->report.buffer.buf[i]);
					}
				}
				return -1;
			}
			object_data[obj].status = data;
			offset += bits;
			break;

		case TOUCH_OBJECT_N_X_POSITION:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get object x position\n");
				return retval;
			}

			object_data[obj].x_pos = data;
			offset += bits;
			break;

		case TOUCH_OBJECT_N_Y_POSITION:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get object y position\n");
				return retval;
			}

			object_data[obj].y_pos = data;
			offset += bits;
			break;

		case TOUCH_OBJECT_N_Z:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get object z\n");
				return retval;
			}

			object_data[obj].z = data;
			offset += bits;
			break;

		case TOUCH_OBJECT_N_X_WIDTH:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get object x width\n");
				return retval;
			}

			object_data[obj].x_width = data;
			offset += bits;
			break;

		case TOUCH_OBJECT_N_Y_WIDTH:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get object y width\n");
				return retval;
			}

			object_data[obj].y_width = data;
			offset += bits;
			break;
		case  TOUCH_REPORT_CUSTOMER_GRIP_INFO:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, (unsigned int *)(&grip_data[0]));
			if (retval < 0) {
				TPD_INFO("Failed to get Grip info\n");
				return retval;
			}
			object_data[obj].eyWidth  = grip_data[0];
			object_data[obj].exWidth  = grip_data[1];
			object_data[obj].yERatio  = grip_data[2];
			object_data[obj].xERatio  = grip_data[3];
			offset += bits;
			break;
		case TOUCH_OBJECT_N_TX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get object tx position\n");
				return retval;
			}

			object_data[obj].tx_pos = data;
			offset += bits;
			break;

		case TOUCH_OBJECT_N_RX_POSITION_TIXELS:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get object rx position\n");
				return retval;
			}

			object_data[obj].rx_pos = data;
			offset += bits;
			break;

		case TOUCH_0D_BUTTONS_STATE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get 0D buttons state\n");
				return retval;
			}

			touch_data->buttons_state = data;
			offset += bits;
			break;

		case TOUCH_GESTURE_DOUBLE_TAP:
		case TOUCH_REPORT_GESTURE_SWIPE:
		case TOUCH_REPORT_GESTURE_CIRCLE:
		case TOUCH_REPORT_GESTURE_UNICODE:
		case TOUCH_REPORT_GESTURE_VEE:
		case TOUCH_REPORT_GESTURE_TRIANGLE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get gesture double tap\n");
				return retval;
			}

			touch_data->lpwg_gesture = data;
			offset += bits;
			break;

		case TOUCH_REPORT_GESTURE_INFO:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits,
						      (unsigned int *)(&touch_data->extra_gesture_info[0]));

			if (retval < 0) {
				TPD_INFO("Failed to get gesture double tap\n");
				return retval;
			}

			offset += bits;
			break;

		case TOUCH_REPORT_GESTURE_COORDINATE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits,
						      (unsigned int *)(&touch_data->data_point[0]));

			if (retval < 0) {
				TPD_INFO("Failed to get gesture double tap\n");
				return retval;
			}

			offset += bits;
			break;

		case TOUCH_FRAME_RATE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get frame rate\n");
				return retval;
			}

			touch_data->frame_rate = data;
			offset += bits;
			break;

		case TOUCH_POWER_IM:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get power IM\n");
				return retval;
			}

			touch_data->power_im = data;
			offset += bits;
			break;

		case TOUCH_CID_IM:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get CID IM\n");
				return retval;
			}

			touch_data->cid_im = data;
			offset += bits;
			break;

		case TOUCH_RAIL_IM:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get rail IM\n");
				return retval;
			}

			touch_data->rail_im = data;
			offset += bits;
			break;

		case TOUCH_CID_VARIANCE_IM:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get CID variance IM\n");
				return retval;
			}

			touch_data->cid_variance_im = data;
			offset += bits;
			break;

		case TOUCH_NSM_FREQUENCY:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get NSM frequency\n");
				return retval;
			}

			touch_data->nsm_frequency = data;
			offset += bits;
			break;

		case TOUCH_NSM_STATE:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get NSM state\n");
				return retval;
			}

			touch_data->nsm_state = data;
			offset += bits;
			break;

		case TOUCH_NUM_OF_ACTIVE_OBJECTS:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get number of active objects\n");
				return retval;
			}

			active_objects = data;
			num_of_active_objects = true;
			touch_data->num_of_active_objects = data;
			offset += bits;

			if (touch_data->num_of_active_objects == 0) {
				idx = end_of_foreach;
			}

			break;

		case TOUCH_NUM_OF_CPU_CYCLES_USED_SINCE_LAST_FRAME:
			bits = config_data[idx++];
			retval = syna_get_report_data(tcm_info, offset, bits, &data);

			if (retval < 0) {
				TPD_INFO("Failed to get number of CPU cycles used since last frame\n");
				return retval;
			}

			touch_data->num_of_cpu_cycles = data;
			offset += bits;
			break;

		case TOUCH_TUNING_GAUSSIAN_WIDTHS:
			bits = config_data[idx++];
			offset += bits;
			break;

		case TOUCH_TUNING_SMALL_OBJECT_PARAMS:
			bits = config_data[idx++];
			offset += bits;
			break;

		case TOUCH_TUNING_0D_BUTTONS_VARIANCE:
			bits = config_data[idx++];
			offset += bits;
			break;
		default:
			break;
		}
	}

exit:
	return 0;
}

static int syna_get_input_params(struct syna_tcm_data *tcm_info)
{
	int retval;

	LOCK_BUFFER(tcm_info->config);

	retval = syna_tcm_write_message(tcm_info, CMD_GET_TOUCH_REPORT_CONFIG,
					NULL, 0, &tcm_info->config.buf, &tcm_info->config.buf_size,
					&tcm_info->config.data_length, 0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_GET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(tcm_info->config);
		return retval;
	}

	UNLOCK_BUFFER(tcm_info->config);

	return 0;
}

static int syna_set_default_report_config(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	int length = 0;

	LOCK_BUFFER(tcm_info->config);

	length = tcm_info->default_config.buf_size;

	if (tcm_info->default_config.buf) {
		retval = syna_tcm_alloc_mem(&tcm_info->config, length);

		if (retval < 0) {
			TPD_INFO("Failed to alloc mem\n");
			goto exit;
		}

		memcpy(tcm_info->config.buf, tcm_info->default_config.buf, length);
		tcm_info->config.buf_size = tcm_info->default_config.buf_size;
		tcm_info->config.data_length = tcm_info->default_config.data_length;
	}

exit:
	UNLOCK_BUFFER(tcm_info->config);

	return retval;
}

static int syna_get_default_report_config(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned int length;

	length = le2_to_uint(tcm_info->app_info.max_touch_report_config_size);

	LOCK_BUFFER(tcm_info->default_config);

	retval = syna_tcm_write_message(tcm_info,
					CMD_GET_TOUCH_REPORT_CONFIG,
					NULL,
					0,
					&tcm_info->default_config.buf,
					&tcm_info->default_config.buf_size,
					&tcm_info->default_config.data_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_GET_TOUCH_REPORT_CONFIG));
		goto exit;
	}

exit:
	UNLOCK_BUFFER(tcm_info->default_config);
	return retval;
}

static int syna_set_normal_report_config(struct syna_tcm_data *tcm_info)
{
	int retval;
	unsigned int idx = 0;
	unsigned int length;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;

	TPD_DEBUG("%s:set normal report\n", __func__);
	length = le2_to_uint(tcm_info->app_info.max_touch_report_config_size);

	if (length < TOUCH_REPORT_CONFIG_SIZE) {
		TPD_INFO("Invalid maximum touch report config size\n");
		return -EINVAL;
	}

	LOCK_BUFFER(touch_hcd->out);

	retval = syna_tcm_alloc_mem(&touch_hcd->out, length);

	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for touch_hcd->out.buf\n");
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	touch_hcd->out.buf[idx++] = TOUCH_GESTURE_DOUBLE_TAP;
	touch_hcd->out.buf[idx++] = 8;

	if (0 == tcm_info->normal_config_version) {
		touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_INFO;
		touch_hcd->out.buf[idx++] = 48;
	}
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_WIDTH;
	touch_hcd->out.buf[idx++] = 12;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_WIDTH;
	touch_hcd->out.buf[idx++] = 12;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_CUSTOMER_GRIP_INFO;
	touch_hcd->out.buf[idx++] = 32;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
	touch_hcd->out.buf[idx++] = TOUCH_END;

	LOCK_BUFFER(touch_hcd->resp);

	retval = syna_tcm_write_message(tcm_info,
					CMD_SET_TOUCH_REPORT_CONFIG,
					touch_hcd->out.buf,
					length,
					&touch_hcd->resp.buf,
					&touch_hcd->resp.buf_size,
					&touch_hcd->resp.data_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_SET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(touch_hcd->resp);
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(touch_hcd->resp);
	UNLOCK_BUFFER(touch_hcd->out);

	return retval;
}

static int syna_set_gesture_report_config(struct syna_tcm_data *tcm_info)
{
	int retval;
	unsigned int idx = 0;
	unsigned int length;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;

	TPD_DEBUG("%s: set gesture report\n", __func__);
	length = le2_to_uint(tcm_info->app_info.max_touch_report_config_size);

	if (length < TOUCH_REPORT_CONFIG_SIZE) {
		TPD_INFO("Invalid maximum touch report config size\n");
		return -EINVAL;
	}

	LOCK_BUFFER(touch_hcd->out);

	retval = syna_tcm_alloc_mem(&touch_hcd->out, length);

	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for touch_hcd->out.buf\n");
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	touch_hcd->out.buf[idx++] = TOUCH_GESTURE_DOUBLE_TAP;
	touch_hcd->out.buf[idx++] = 8;
	/* touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_CIRCLE;*/
	/* touch_hcd->out.buf[idx++] = 1;*/
	/* touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_SWIPE;*/
	/* touch_hcd->out.buf[idx++] = 1;*/
	/* touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_UNICODE;*/
	/* touch_hcd->out.buf[idx++] = 1;*/
	/* touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_VEE;*/
	/* touch_hcd->out.buf[idx++] = 1;*/
	/* touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_TRIANGLE;*/
	/* touch_hcd->out.buf[idx++] = 1;*/
	/* touch_hcd->out.buf[idx++] = TOUCH_PAD_TO_NEXT_BYTE;*/
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_INFO;
	touch_hcd->out.buf[idx++] = 48;
	touch_hcd->out.buf[idx++] = TOUCH_REPORT_GESTURE_COORDINATE;
	touch_hcd->out.buf[idx++] = 192;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_ACTIVE_OBJECT;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_INDEX;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_CLASSIFICATION;
	touch_hcd->out.buf[idx++] = 4;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_X_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_OBJECT_N_Y_POSITION;
	touch_hcd->out.buf[idx++] = 16;
	touch_hcd->out.buf[idx++] = TOUCH_FOREACH_END;
	touch_hcd->out.buf[idx++] = TOUCH_END;

	LOCK_BUFFER(touch_hcd->resp);

	retval = syna_tcm_write_message(tcm_info,
					CMD_SET_TOUCH_REPORT_CONFIG,
					touch_hcd->out.buf,
					length,
					&touch_hcd->resp.buf,
					&touch_hcd->resp.buf_size,
					&touch_hcd->resp.data_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_SET_TOUCH_REPORT_CONFIG));
		UNLOCK_BUFFER(touch_hcd->resp);
		UNLOCK_BUFFER(touch_hcd->out);
		return retval;
	}

	UNLOCK_BUFFER(touch_hcd->resp);
	UNLOCK_BUFFER(touch_hcd->out);

	return 0;
}

static int syna_set_input_reporting(struct syna_tcm_data *tcm_info, bool suspend)
{
	int retval = 0;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;

	TPD_DEBUG("%s: mode 0x%x, state %d\n", __func__, tcm_info->id_info.mode,
		  suspend);

	if (tcm_info->id_info.mode != MODE_APPLICATION
	    || tcm_info->app_status != APP_STATUS_OK) {
		TPD_INFO("Application firmware not running\n");
		return 0;
	}

	touch_hcd->report_touch = false;

	mutex_lock(&touch_hcd->report_mutex);

	if (!suspend) {
		retval = syna_set_normal_report_config(tcm_info);

		if (retval < 0) {
			TPD_INFO("Failed to set report config\n");
			goto default_config;
		}

	} else {
		retval = syna_set_gesture_report_config(tcm_info);

		if (retval < 0) {
			TPD_INFO("Failed to set report config\n");
			goto default_config;
		}
	}

	retval = syna_get_input_params(tcm_info);

	if (retval < 0) {
		TPD_INFO("Failed to get input parameters\n");
	}

	goto exit;

default_config:
	/*if failed to set report config, use default report config */
	retval = syna_set_default_report_config(tcm_info);

	if (retval < 0) {
		TPD_INFO("Failed to set default report config");
	}

exit:
	mutex_unlock(&touch_hcd->report_mutex);

	touch_hcd->report_touch = retval < 0 ? false : true;

	return retval;
}

static void syna_set_trigger_reason(struct syna_tcm_data *tcm_info,
				    irq_reason trigger_reason)
{
	SET_BIT(tcm_info->trigger_reason, trigger_reason);
}

static void syna_tcm_resize_chunk_size(struct syna_tcm_data *tcm_info)
{
	unsigned int max_write_size;

	max_write_size = le2_to_uint(tcm_info->id_info.max_write_size);
	tcm_info->wr_chunk_size = MIN(max_write_size, WR_CHUNK_SIZE);

	if (tcm_info->wr_chunk_size == 0) {
		tcm_info->wr_chunk_size = max_write_size;
	}
}

static int syna_async_work_callback(struct syna_tcm_data *tcm_info)
{

	if (!tcm_info) {
		return 0;
	}

	TPD_INFO("%s: async work enter\n", __func__);
	if (tcm_info->first_sync_flag) {
		tcm_info->first_sync_flag = false;
		return 0;
	}
	//reinit_completion(&tcm_info->resume_complete);
	if (tcm_info->suspend_state == TP_RESUME_COMPLETE) {
		TPD_INFO("%s: *tcm_info->suspend_state %d \n", __func__, tcm_info->suspend_state);
		complete(&tcm_info->resume_complete);
		return 0;
	}

	TPD_INFO("%s: async work exit\n", __func__);

	queue_work(tcm_info->async_workqueue, &tcm_info->async_work);
	return 0 ;
}

/**
 * syna_tcm_dispatch_report() - dispatch report received from device
 *
 * @tcm_info: handle of core module
 *
 * The report generated by the device is forwarded to the synchronous inbox of
 * each registered application module for further processing. In addition, the
 * report notifier thread is woken up for asynchronous notification of the
 * report occurrence.
 */
static void syna_tcm_dispatch_report(struct syna_tcm_data *tcm_info)
{
	int ret = 0;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touch_data *touch_data = &touch_hcd->touch_data;

	LOCK_BUFFER(tcm_info->in);
	LOCK_BUFFER(tcm_info->report.buffer);

	tcm_info->report.buffer.buf = &tcm_info->in.buf[MESSAGE_HEADER_SIZE];
	tcm_info->report.buffer.buf_size = tcm_info->in.buf_size - MESSAGE_HEADER_SIZE;
	tcm_info->report.buffer.data_length = tcm_info->payload_length;
	tcm_info->report.id = tcm_info->report_code;

	if (tcm_info->report.id == REPORT_TOUCH) {
		if (*tcm_info->loading_fw) {
		    TPD_INFO("%s: disable touch when TP loading_fw !\n", __func__);
		    goto exit;
		}
		ret = syna_parse_report(tcm_info);

		if (ret < 0) {
			TPD_INFO("Failed to parse report\n");
			goto exit;
		}

		if (*tcm_info->in_suspend) {
			syna_set_trigger_reason(tcm_info, IRQ_GESTURE);

		} else {
			syna_set_trigger_reason(tcm_info, IRQ_TOUCH);

			if (tcm_info->palm_to_sleep_state == PALM_TO_SLEEP) {
				syna_set_trigger_reason(tcm_info, IRQ_PALM);
				tcm_info->palm_to_sleep_state = PALM_TO_DEFAULT;
				TPD_DEBUG("%s:PALM_TO_DEFAULT\n", __func__);
			}
			if (touch_data->lpwg_gesture == TOUCH_HOLD_UP
			    || touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
				syna_set_trigger_reason(tcm_info, IRQ_FINGERPRINT);
			}
		}

	} else if (tcm_info->report.id == REPORT_IDENTIFY) {
		if (tcm_info->id_info.mode == MODE_APPLICATION) {
			syna_async_work_callback(tcm_info);
			syna_set_trigger_reason(tcm_info, IRQ_IGNORE);
		}
	} else if (tcm_info->report.id == REPORT_TOUCH_HOLD) {
		syna_set_trigger_reason(tcm_info, IRQ_FINGERPRINT);

	} else if (tcm_info->report.id == REPORT_LOG) {
		syna_set_trigger_reason(tcm_info, IRQ_FW_HEALTH);

	} else {
		syna_tcm_test_report(tcm_info);
	}

exit:
	UNLOCK_BUFFER(tcm_info->report.buffer);
	UNLOCK_BUFFER(tcm_info->in);
	return;
}


/**
 * syna_tcm_dispatch_response() - dispatch response received from device
 *
 * @tcm_info: handle of core module
 *
 * The response to a command is forwarded to the sender of the command.
 */
static void syna_tcm_dispatch_response(struct syna_tcm_data *tcm_info)
{
	int retval = 0;

	if (atomic_read(&tcm_info->command_status) != CMD_BUSY) {
		tcm_info->trigger_reason = 0;
		return;
	}

	LOCK_BUFFER(tcm_info->resp);

	if (tcm_info->payload_length == 0) {
		UNLOCK_BUFFER(tcm_info->resp);
		atomic_set(&tcm_info->command_status, CMD_IDLE);
		goto exit;
	}

	retval = syna_tcm_alloc_mem(&tcm_info->resp, tcm_info->payload_length);

	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for tcm_info->resp.buf\n");
		UNLOCK_BUFFER(tcm_info->resp);
		atomic_set(&tcm_info->command_status, CMD_ERROR);
		goto exit;
	}

	LOCK_BUFFER(tcm_info->in);

	retval = tp_memcpy(tcm_info->resp.buf, tcm_info->resp.buf_size,
			   &tcm_info->in.buf[MESSAGE_HEADER_SIZE],
			   tcm_info->in.buf_size - MESSAGE_HEADER_SIZE, tcm_info->payload_length);

	if (retval < 0) {
		TPD_INFO("Failed to copy payload\n");
		UNLOCK_BUFFER(tcm_info->in);
		UNLOCK_BUFFER(tcm_info->resp);
		atomic_set(&tcm_info->command_status, CMD_ERROR);
		goto exit;
	}

	tcm_info->resp.data_length = tcm_info->payload_length;

	UNLOCK_BUFFER(tcm_info->in);
	UNLOCK_BUFFER(tcm_info->resp);

	atomic_set(&tcm_info->command_status, CMD_IDLE);

exit:
	complete(&tcm_info->response_complete);
	tcm_info->trigger_reason = 0;

	return;
}

/**
 * syna_tcm_dispatch_message() - dispatch message received from device
 *
 * @tcm_info: handle of core module
 *
 * The information received in the message read in from the device is dispatched
 * to the appropriate destination based on whether the information represents a
 * report or a response to a command.
 */
static void syna_tcm_dispatch_message(struct syna_tcm_data *tcm_info)
{
	int retval;
	unsigned int payload_length;

	if (tcm_info->report_code == REPORT_IDENTIFY) {
		tcm_info->identify_state = 1;
		payload_length = tcm_info->payload_length;

		LOCK_BUFFER(tcm_info->in);

		retval = tp_memcpy((unsigned char *)&tcm_info->id_info,
				   sizeof(tcm_info->id_info),
				   &tcm_info->in.buf[MESSAGE_HEADER_SIZE],
				   tcm_info->in.buf_size - MESSAGE_HEADER_SIZE,
				   MIN(sizeof(tcm_info->id_info), payload_length));

		if (retval < 0) {
			TPD_INFO("Failed to copy identification info\n");
			UNLOCK_BUFFER(tcm_info->in);
			return;
		}

		UNLOCK_BUFFER(tcm_info->in);

		syna_tcm_resize_chunk_size(tcm_info);
		TPD_INFO("Received identify report (firmware mode = 0x%02x), CMD:%d\n",
			 tcm_info->id_info.mode, tcm_info->command);
		if (0x0b == tcm_info->id_info.mode) {
			tcm_info->firmware_mode_count++;
			if (!tcm_info->upload_flag && tcm_info->firmware_mode_count >= FIRMWARE_MODE_BL_MAX) {
				tcm_info->upload_flag = 1;
			}
		}
		if (atomic_read(&tcm_info->command_status) == CMD_BUSY) {
			switch (tcm_info->command) {
			case CMD_RESET:
			case CMD_RUN_BOOTLOADER_FIRMWARE:
			case CMD_RUN_APPLICATION_FIRMWARE:
				atomic_set(&tcm_info->command_status, CMD_IDLE);
				complete(&tcm_info->response_complete);
				break;

			default:
				TPD_INFO("Device has been reset\n");
				atomic_set(&tcm_info->command_status, CMD_ERROR);
				complete(&tcm_info->response_complete);
				break;
			}
		}

		if (tcm_info->id_info.mode == MODE_HOST_DOWNLOAD) {
			return;
		}

		syna_tcm_helper(tcm_info);
	}

	if (tcm_info->report_code >= REPORT_IDENTIFY) {
		syna_tcm_dispatch_report(tcm_info);

	} else {
		syna_tcm_dispatch_response(tcm_info);
	}

	return;
}

/**
 * syna_tcm_continued_read() - retrieve entire payload from device
 *
 * @tcm_info: handle of core module
 *
 * Read transactions are carried out until the entire payload is retrieved from
 * the device and stored in the handle of the core module.
 */
static int syna_tcm_continued_read(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned char marker = 0, code = 0;
	unsigned int idx = 0, offset = 0, chunks = 0;
	unsigned int chunk_space = 0, xfer_length = 0, total_length = 0,
		     remaining_length = 0;

	total_length = MESSAGE_HEADER_SIZE + tcm_info->payload_length + 1;
	remaining_length = total_length - tcm_info->read_length;

	LOCK_BUFFER(tcm_info->in);

	retval = syna_tcm_realloc_mem(&tcm_info->in, total_length);

	if (retval < 0) {
		TPD_INFO("Failed to reallocate memory for tcm_info->in.buf\n");
		UNLOCK_BUFFER(tcm_info->in);
		return retval;
	}

	/* available chunk space for payload = total chunk size minus header
	 * marker byte and header code byte */
	if (tcm_info->rd_chunk_size == 0) {
		chunk_space = remaining_length;

	} else {
		chunk_space = tcm_info->rd_chunk_size - 2;
	}

	chunks = ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	offset = tcm_info->read_length;

	LOCK_BUFFER(tcm_info->temp);

	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space) {
			xfer_length = chunk_space;

		} else {
			xfer_length = remaining_length;
		}

		if (xfer_length == 1) {
			tcm_info->in.buf[offset] = MESSAGE_PADDING;
			offset += xfer_length;
			remaining_length -= xfer_length;
			continue;
		}

		retval = syna_tcm_alloc_mem(&tcm_info->temp, xfer_length + 2);

		if (retval < 0) {
			TPD_INFO("Failed to allocate memory for tcm_info->temp.buf\n");
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return retval;
		}

		retval = touch_i2c_continue_read(tcm_info->client, xfer_length + 2,
						 tcm_info->temp.buf);

		if (retval < 0) {
			TPD_INFO("Failed to read from device\n");
			TPD_INFO("Failed to read from device.#1 ATTN:%d\n", gpio_get_value(tcm_info->hw_res->irq_gpio));
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return retval;
		}
		if (*tcm_info->loading_fw) {
			syna_print_transfer_data(tcm_info->temp.buf, xfer_length + 2, SYNA_TRANSFER_READ);
		}
		marker = tcm_info->temp.buf[0];
		code = tcm_info->temp.buf[1];

		if (marker != MESSAGE_MARKER) {
			TPD_INFO("Incorrect header marker (0x%02x)\n", marker);
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return -EIO;
		}

		if (code != STATUS_CONTINUED_READ) {
			TPD_INFO("Incorrect header code (0x%02x)\n", code);
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return -EIO;
		}

		retval = tp_memcpy(&tcm_info->in.buf[offset], total_length - offset,
				   &tcm_info->temp.buf[2], xfer_length, xfer_length);

		if (retval < 0) {
			TPD_INFO("Failed to copy payload\n");
			UNLOCK_BUFFER(tcm_info->temp);
			UNLOCK_BUFFER(tcm_info->in);
			return retval;
		}

		offset += xfer_length;

		remaining_length -= xfer_length;
	}

	UNLOCK_BUFFER(tcm_info->temp);
	UNLOCK_BUFFER(tcm_info->in);

	return 0;
}

/**
 * syna_tcm_raw_read() - retrieve specific number of data bytes from device
 *
 * @tcm_info: handle of core module
 * @in_buf: buffer for storing data retrieved from device
 * @length: number of bytes to retrieve from device
 *
 * Read transactions are carried out until the specific number of data bytes are
 * retrieved from the device and stored in in_buf.
 */
static int syna_tcm_raw_read(struct syna_tcm_data *tcm_info,
			     unsigned char *in_buf, unsigned int length)
{
	int retval = 0;
	unsigned char code = 0;
	unsigned int idx = 0, offset = 0;
	unsigned int chunks = 0, chunk_space = 0;
	unsigned int xfer_length = 0, remaining_length = 0;

	if (length < 2) {
		TPD_INFO("Invalid length information\n");
		return -EINVAL;
	}

	/* minus header marker byte and header code byte */
	remaining_length = length - 2;

	/* available chunk space for data = total chunk size minus header marker
	 * byte and header code byte */
	if (tcm_info->rd_chunk_size == 0) {
		chunk_space = remaining_length;

	} else {
		chunk_space = tcm_info->rd_chunk_size - 2;
	}

	chunks = ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	offset = 0;

	LOCK_BUFFER(tcm_info->temp);

	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space) {
			xfer_length = chunk_space;

		} else {
			xfer_length = remaining_length;
		}

		if (xfer_length == 1) {
			in_buf[offset] = MESSAGE_PADDING;
			offset += xfer_length;
			remaining_length -= xfer_length;
			continue;
		}

		retval = syna_tcm_alloc_mem(&tcm_info->temp, xfer_length + 2);

		if (retval < 0) {
			TPD_INFO("Failed to allocate memory for tcm_info->temp.buf\n");
			UNLOCK_BUFFER(tcm_info->temp);
			return retval;
		}

		retval = syna_tcm_read(tcm_info, tcm_info->temp.buf, xfer_length + 2);

		if (retval < 0) {
			TPD_INFO("Failed to read from device\n");
			TPD_INFO("Failed to read from device.#2 ATTN:%d\n", gpio_get_value(tcm_info->hw_res->irq_gpio));
			UNLOCK_BUFFER(tcm_info->temp);
			return retval;
		}

		code = tcm_info->temp.buf[1];

		if (idx == 0) {
			retval = tp_memcpy(&in_buf[0], length, &tcm_info->temp.buf[0], xfer_length + 2,
					   xfer_length + 2);

		} else {
			if (code != STATUS_CONTINUED_READ) {
				TPD_INFO("Incorrect header code (0x%02x)\n", code);
				UNLOCK_BUFFER(tcm_info->temp);
				return -EIO;
			}

			retval = tp_memcpy(&in_buf[offset],
					   length - offset, &tcm_info->temp.buf[2],
					   xfer_length, xfer_length);
		}

		if (retval < 0) {
			TPD_INFO("Failed to copy data\n");
			UNLOCK_BUFFER(tcm_info->temp);
			return retval;
		}

		if (idx == 0) {
			offset += (xfer_length + 2);

		} else {
			offset += xfer_length;
		}

		remaining_length -= xfer_length;
	}

	UNLOCK_BUFFER(tcm_info->temp);

	return 0;
}

/**
 * syna_tcm_raw_write() - write command/data to device without receiving
 * response
 *
 * @tcm_info: handle of core module
 * @command: command to send to device
 * @data: data to send to device
 * @length: length of data in bytes
 *
 * A command and its data, if any, are sent to the device.
 */
static int syna_tcm_raw_write(struct syna_tcm_data *tcm_info,
			      unsigned char command,
			      unsigned char *data, unsigned int length)
{
	int retval = 0;
	char *report = NULL;
	unsigned int idx = 0, chunks = 0, chunk_space = 0;
	unsigned int xfer_length = 0, remaining_length = length;

	/* available chunk space for data = total chunk size minus command byte */
	if (tcm_info->wr_chunk_size == 0) {
		chunk_space = remaining_length;

	} else {
		chunk_space = tcm_info->wr_chunk_size - 1;
	}

	chunks = ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	LOCK_BUFFER(tcm_info->out);

	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space) {
			xfer_length = chunk_space;

		} else {
			xfer_length = remaining_length;
		}

		retval = syna_tcm_alloc_mem(&tcm_info->out, xfer_length + 1);

		if (retval < 0) {
			TPD_INFO("Failed to allocate memory for tcm_info->out.buf\n");
			UNLOCK_BUFFER(tcm_info->out);
			return retval;
		}

		if (idx == 0) {
			tcm_info->out.buf[0] = command;

		} else {
			tcm_info->out.buf[0] = CMD_CONTINUE_WRITE;
		}

		if (xfer_length) {
			retval = tp_memcpy(&tcm_info->out.buf[1],
					   xfer_length,
					   &data[idx * chunk_space],
					   remaining_length,
					   xfer_length);

			if (retval < 0) {
				TPD_INFO("Failed to copy data\n");
				UNLOCK_BUFFER(tcm_info->out);
				return retval;
			}
		}

		retval = syna_tcm_write(tcm_info, tcm_info->out.buf, xfer_length + 1);

		if (retval < 0) {
			report = tp_kzalloc(30, GFP_KERNEL);
			if (report) {
				snprintf(report, 30, "raw_write_err_%2x", command);
				tp_kfree((void **)&report);
			}
			TPD_INFO("Failed to write to device\n");
			UNLOCK_BUFFER(tcm_info->out);
			return retval;
		}

		remaining_length -= xfer_length;
	}

	UNLOCK_BUFFER(tcm_info->out);

	return 0;
}

/*add this for debug. remove before pvt*/
/*
static void syna_tcm_debug_message(char *buf, int len)
{
    int i = 0;
    char buffer[161] = {0};

    for (i = 0; i < len; i++) {
        if (i > 32)
            break;

        sprintf(&buffer[5 * i], "0x%02x ", buf[i]);
    }

    if (len > 0)
        TPD_INFO("payload data: %s\n", buffer);
}
*/

/**
 * syna_tcm_read_message() - read message from device
 *
 * @tcm_info: handle of core module
 * @in_buf: buffer for storing data in raw read mode
 * @length: length of data in bytes in raw read mode
 *
 * If in_buf is not NULL, raw read mode is used and syna_tcm_raw_read() is
 * called. Otherwise, a message including its entire payload is retrieved from
 * the device and dispatched to the appropriate destination.
 */
static int syna_tcm_read_message(struct syna_tcm_data *tcm_info,
				 unsigned char *in_buf, unsigned int length)
{
	int retval = 0;
	unsigned int total_length = 0;
	struct syna_tcm_message_header *header = NULL;

	TPD_DEBUG("%s\n", __func__);
	mutex_lock(&tcm_info->rw_mutex);

	if (in_buf != NULL) {
		retval = syna_tcm_raw_read(tcm_info, in_buf, length);
		goto exit;
	}

	LOCK_BUFFER(tcm_info->in);

	retval = touch_i2c_continue_read(tcm_info->client, tcm_info->read_length,
					 tcm_info->in.buf);

	if (retval < 0) {
		TPD_INFO("Failed to read from device\n");
		TPD_INFO("Failed to read from device. #3 ATTN:%d\n", gpio_get_value(tcm_info->hw_res->irq_gpio));
		UNLOCK_BUFFER(tcm_info->in);
		ssleep(5);
		goto exit;
	}
	if (*tcm_info->loading_fw) {
		syna_print_transfer_data(tcm_info->in.buf, tcm_info->read_length, SYNA_TRANSFER_READ);
	}
	header = (struct syna_tcm_message_header *)tcm_info->in.buf;

	if (header->marker != MESSAGE_MARKER) {
		TPD_INFO("wrong header marker:0x%02x, code:0x%02x, len:%u\n", header->marker, header->code, *header->length);
		UNLOCK_BUFFER(tcm_info->in);
		retval = -ENXIO;
		ssleep(5);
		goto exit;
	}

	tcm_info->report_code = header->code;
	tcm_info->payload_length = le2_to_uint(header->length);
	TPD_DEBUG("Header code = 0x%02x Payload len = %d\n", tcm_info->report_code,
		  tcm_info->payload_length);

	if (tcm_info->report_code <= STATUS_ERROR
	    || tcm_info->report_code == STATUS_INVALID) {
		switch (tcm_info->report_code) {
		case STATUS_OK:
			break;

		case STATUS_CONTINUED_READ:

		/*TPD_INFO("Out-of-sync continued read\n");*/
		case STATUS_IDLE:
		case STATUS_BUSY:
			tcm_info->payload_length = 0;
			UNLOCK_BUFFER(tcm_info->in);
			retval = 0;
			goto exit;

		default:
			TPD_INFO("Incorrect header code (0x%02x)\n", tcm_info->report_code);

			if (tcm_info->report_code != STATUS_ERROR) {
				UNLOCK_BUFFER(tcm_info->in);
				retval = -EIO;
				goto exit;
			}
		}
	}

	total_length = MESSAGE_HEADER_SIZE + tcm_info->payload_length + 1;

#ifdef PREDICTIVE_READING

	if (total_length <= tcm_info->read_length) {
		goto check_padding;

	} else if (total_length - 1 == tcm_info->read_length) {
		tcm_info->in.buf[total_length - 1] = MESSAGE_PADDING;
		goto check_padding;
	}

#else

	if (tcm_info->payload_length == 0) {
		tcm_info->in.buf[total_length - 1] = MESSAGE_PADDING;
		goto check_padding;
	}

#endif

	UNLOCK_BUFFER(tcm_info->in);

	retval = syna_tcm_continued_read(tcm_info);

	if (retval < 0) {
		TPD_INFO("Failed to do continued read\n");
		goto exit;
	}

	LOCK_BUFFER(tcm_info->in);

	tcm_info->in.buf[0] = MESSAGE_MARKER;

	tcm_info->in.buf[1] = tcm_info->report_code;

	tcm_info->in.buf[2] = (unsigned char)tcm_info->payload_length;

	tcm_info->in.buf[3] = (unsigned char)(tcm_info->payload_length >> 8);

check_padding:
	if (tcm_info->in.buf[total_length - 1] != MESSAGE_PADDING) {
		TPD_INFO("Incorrect message padding byte (0x%02x)\n",
			 tcm_info->in.buf[total_length - 1]);
		UNLOCK_BUFFER(tcm_info->in);
		retval = -EIO;
		goto exit;
	}

	UNLOCK_BUFFER(tcm_info->in);

#ifdef PREDICTIVE_READING
	total_length = MAX(total_length, MIN_READ_LENGTH);
	tcm_info->read_length = MIN(total_length, tcm_info->rd_chunk_size);

	if (tcm_info->rd_chunk_size == 0) {
		tcm_info->read_length = total_length;
	}

#endif

	/*add for debug, remove before pvt*/
	/*if (LEVEL_BASIC != tp_debug) {*/
	/*    syna_tcm_debug_message(&tcm_info->in.buf[4], tcm_info->payload_length);*/
	/*}*/

	syna_tcm_dispatch_message(tcm_info);

	retval = 0;

exit:

	if ((retval < 0) && (atomic_read(&tcm_info->command_status) == CMD_BUSY)) {
		atomic_set(&tcm_info->command_status, CMD_ERROR);
		complete(&tcm_info->response_complete);
	}

	mutex_unlock(&tcm_info->rw_mutex);

	return retval;
}

/**
 * syna_tcm_write_message() - write message to device and receive response
 *
 * @tcm_info: handle of core module
 * @command: command to send to device
 * @payload: payload of command
 * @length: length of payload in bytes
 * @resp_buf: buffer for storing command response
 * @resp_buf_size: size of response buffer in bytes
 * @resp_length: length of command response in bytes
 * @polling_delay_ms: delay time after sending command before resuming polling
 *
 * If resp_buf is NULL, raw write mode is used and syna_tcm_raw_write() is
 * called. Otherwise, a command and its payload, if any, are sent to the device
 * and the response to the command generated by the device is read in.
 */
static int syna_tcm_write_message(struct syna_tcm_data *tcm_info,
				  unsigned char command, unsigned char *payload,
				  unsigned int length, unsigned char **resp_buf,
				  unsigned int *resp_buf_size, unsigned int *resp_length,
				  unsigned int timeout)
{
	int retval = 0;
	char *report = NULL;
	unsigned int idx = 0, chunks = 0, chunk_space = 0;
	unsigned int xfer_length = 0, remaining_length = 0;
	unsigned int command_status = 0;
	unsigned int timeout_ms = 0;

	mutex_lock(&tcm_info->command_mutex);
	mutex_lock(&tcm_info->rw_mutex);

	if (resp_buf == NULL) {
		retval = syna_tcm_raw_write(tcm_info, command, payload, length);
		mutex_unlock(&tcm_info->rw_mutex);
		goto exit;
	}

	atomic_set(&tcm_info->command_status, CMD_BUSY);
	reinit_completion(&tcm_info->response_complete);
	tcm_info->command = command;

	LOCK_BUFFER(tcm_info->resp);

	tcm_info->resp.buf = *resp_buf;
	tcm_info->resp.buf_size = *resp_buf_size;
	tcm_info->resp.data_length = 0;

	UNLOCK_BUFFER(tcm_info->resp);

	/* adding two length bytes as part of payload */
	remaining_length = length + 2;

	/* available chunk space for payload = total chunk size minus command
	 * byte */
	if (tcm_info->wr_chunk_size == 0) {
		chunk_space = remaining_length;

	} else {
		chunk_space = tcm_info->wr_chunk_size - 1;
	}

	chunks = ceil_div(remaining_length, chunk_space);

	chunks = chunks == 0 ? 1 : chunks;

	TPD_DEBUG("%s:Command = 0x%02x\n", __func__, command);

	LOCK_BUFFER(tcm_info->out);

	for (idx = 0; idx < chunks; idx++) {
		if (remaining_length > chunk_space) {
			xfer_length = chunk_space;

		} else {
			xfer_length = remaining_length;
		}

		retval = syna_tcm_alloc_mem(&tcm_info->out, xfer_length + 1);

		if (retval < 0) {
			TPD_INFO("Failed to allocate memory for tcm_info->out.buf\n");
			UNLOCK_BUFFER(tcm_info->out);
			mutex_unlock(&tcm_info->rw_mutex);
			goto exit;
		}

		if (idx == 0) {
			tcm_info->out.buf[0] = command;
			tcm_info->out.buf[1] = (unsigned char)length;
			tcm_info->out.buf[2] = (unsigned char)(length >> 8);

			if (xfer_length > 2) {
				retval = tp_memcpy(&tcm_info->out.buf[3],
						   xfer_length - 2,
						   payload,
						   remaining_length - 2,
						   xfer_length - 2);

				if (retval < 0) {
					TPD_INFO("Failed to copy payload\n");
					UNLOCK_BUFFER(tcm_info->out);
					mutex_unlock(&tcm_info->rw_mutex);
					goto exit;
				}
			}

		} else {
			tcm_info->out.buf[0] = CMD_CONTINUE_WRITE;

			retval = tp_memcpy(&tcm_info->out.buf[1],
					   xfer_length,
					   &payload[idx * chunk_space - 2],
					   remaining_length,
					   xfer_length);

			if (retval < 0) {
				TPD_INFO("Failed to copy payload\n");
				UNLOCK_BUFFER(tcm_info->out);
				mutex_unlock(&tcm_info->rw_mutex);
				goto exit;
			}
		}

		retval = syna_tcm_write(tcm_info, tcm_info->out.buf, xfer_length + 1);

		if (retval < 0) {
			report = tp_kzalloc(30, GFP_KERNEL);
			if (report) {
				snprintf(report, 30, "write_msg_err_wr%2x", command);
				tp_kfree((void **)&report);
			}
			TPD_INFO("Failed to write to device\n");
			UNLOCK_BUFFER(tcm_info->out);
			mutex_unlock(&tcm_info->rw_mutex);
			goto exit;
		}

		remaining_length -= xfer_length;
	}

	UNLOCK_BUFFER(tcm_info->out);

	mutex_unlock(&tcm_info->rw_mutex);

	if (timeout == 0) {
		timeout_ms = RESPONSE_TIMEOUT_MS_DEFAULT;

	} else {
		timeout_ms = timeout;
	}

	retval = wait_for_completion_timeout(&tcm_info->response_complete,
					     msecs_to_jiffies(timeout_ms));

	if (retval == 0) {
		TPD_INFO("Timed out waiting for response (command 0x%02x)\n",
			 tcm_info->command);
		report = tp_kzalloc(30, GFP_KERNEL);
		if (report) {
			snprintf(report, 30, "write_msg_err_wait%2x", tcm_info->command);
			tp_kfree((void **)&report);
		}
		TPD_INFO("Timed out waiting for response (command 0x%02x), ATTN:%d\n",
			 tcm_info->command, gpio_get_value(tcm_info->hw_res->irq_gpio));
		retval = -EIO;

	} else {
		command_status = atomic_read(&tcm_info->command_status);

		if (command_status != CMD_IDLE ||
		    tcm_info->report_code == STATUS_ERROR) {
			TPD_INFO("Failed to get valid response\n");
			report = tp_kzalloc(30, GFP_KERNEL);
			if (report) {
				snprintf(report, 30, "write_msg_err_resp%2x", tcm_info->command);
				tp_kfree((void **)&report);
			}
			TPD_INFO("Failed to get valid response, status:%d, code:%d, retval:%d\n",
				command_status, tcm_info->report_code, retval);
			retval = -EIO;
			goto exit;
		}

		retval = 0;
	}

exit:

	if (command_status == CMD_IDLE) {
		LOCK_BUFFER(tcm_info->resp);

		if (tcm_info->report_code == STATUS_ERROR) {
			if (tcm_info->resp.data_length) {
				TPD_INFO("Error code = 0x%02x\n",
					 tcm_info->resp.buf[0]);
			}
		}

		if (resp_buf != NULL) {
			*resp_buf = tcm_info->resp.buf;
			*resp_buf_size = tcm_info->resp.buf_size;
			*resp_length = tcm_info->resp.data_length;
		}

		UNLOCK_BUFFER(tcm_info->resp);
	}

	tcm_info->command = CMD_NONE;
	atomic_set(&tcm_info->command_status, CMD_IDLE);
	mutex_unlock(&tcm_info->command_mutex);

	return retval;
}

static int syna_tcm_get_app_info(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;
	unsigned int timeout = APP_STATUS_POLL_TIMEOUT_MS;

get_app_info:
	retval = syna_tcm_write_message(tcm_info,
					CMD_GET_APPLICATION_INFO,
					NULL,
					0,
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_GET_APPLICATION_INFO));
		goto exit;
	}

	retval = tp_memcpy((unsigned char *)&tcm_info->app_info,
			   sizeof(tcm_info->app_info),
			   resp_buf,
			   resp_buf_size,
			   MIN(sizeof(tcm_info->app_info), resp_length));

	if (retval < 0) {
		TPD_INFO("Failed to copy application info\n");
		goto exit;
	}

	tcm_info->app_status = le2_to_uint(tcm_info->app_info.status);

	if (tcm_info->app_status == APP_STATUS_BOOTING
	    || tcm_info->app_status == APP_STATUS_UPDATING) {
		if (timeout > 0) {
			msleep(APP_STATUS_POLL_MS);
			timeout -= APP_STATUS_POLL_MS;
			goto get_app_info;
		}
	}

	retval = 0;

exit:
	tp_kfree((void **)&resp_buf);

	return retval;
}

static int syna_tcm_get_boot_info(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

	retval = syna_tcm_write_message(tcm_info,
					CMD_GET_BOOT_INFO,
					NULL,
					0,
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_GET_BOOT_INFO));
		goto exit;
	}

	retval = tp_memcpy((unsigned char *)&tcm_info->boot_info,
			   sizeof(tcm_info->boot_info),
			   resp_buf,
			   resp_buf_size,
			   MIN(sizeof(tcm_info->boot_info), resp_length));

	if (retval < 0) {
		TPD_INFO("Failed to copy boot info\n");
		goto exit;
	}

	retval = 0;

exit:
	tp_kfree((void **)&resp_buf);

	return retval;
}

static int syna_tcm_identify(struct syna_tcm_data *tcm_info, bool id)
{
	int retval;
	unsigned char *resp_buf;
	unsigned int resp_buf_size;
	unsigned int resp_length;

	resp_buf = NULL;
	resp_buf_size = 0;

	mutex_lock(&tcm_info->identify_mutex);

	if (!id) {
		goto get_info;
	}

	retval = syna_tcm_write_message(tcm_info,
					CMD_IDENTIFY,
					NULL,
					0,
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_IDENTIFY));
		goto exit;
	}

	retval = tp_memcpy((unsigned char *)&tcm_info->id_info,
			   sizeof(tcm_info->id_info),
			   resp_buf,
			   resp_buf_size,
			   MIN(sizeof(tcm_info->id_info), resp_length));

	if (retval < 0) {
		TPD_INFO("Failed to copy identification info\n");
		goto exit;
	}

	syna_tcm_resize_chunk_size(tcm_info);

get_info:

	if (tcm_info->id_info.mode == MODE_APPLICATION) {
		retval = syna_tcm_get_app_info(tcm_info);

		if (retval < 0) {
			TPD_INFO("Failed to get application info\n");
			goto exit;
		}

	} else {
		retval = syna_tcm_get_boot_info(tcm_info);

		if (retval < 0) {
			TPD_INFO("Failed to get boot info\n");
			goto exit;
		}
	}

	retval = 0;

exit:
	mutex_unlock(&tcm_info->identify_mutex);

	tp_kfree((void **)&resp_buf);

	return retval;
}

static int syna_tcm_run_application_firmware(struct syna_tcm_data *tcm_info)
{
	int retval = 0;
	bool retry = true;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

retry:
	retval = syna_tcm_write_message(tcm_info,
					CMD_RUN_APPLICATION_FIRMWARE,
					NULL,
					0,
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_RUN_APPLICATION_FIRMWARE));
		goto exit;
	}

	retval = syna_tcm_identify(tcm_info, false);

	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		goto exit;
	}

	if (tcm_info->id_info.mode != MODE_APPLICATION) {
		TPD_INFO("Failed to run application firmware (boot status = 0x%02x)\n",
			 tcm_info->boot_info.status);

		if (retry) {
			retry = false;
			goto retry;
		}

		retval = -EINVAL;
		goto exit;

	} else if (tcm_info->app_status != APP_STATUS_OK) {
		TPD_INFO("Application status = 0x%02x\n", tcm_info->app_status);
	}

	retval = 0;

exit:
	tp_kfree((void **)&resp_buf);

	return retval;
}

static int syna_tcm_get_dynamic_config(struct syna_tcm_data *tcm_info,
				       enum dynamic_config_id id, unsigned short *value)
{
	int retval = 0;
	char *report = NULL;
	unsigned char out_buf = (unsigned char)id;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

	retval = syna_tcm_write_message(tcm_info,
					CMD_GET_DYNAMIC_CONFIG,
					&out_buf,
					sizeof(out_buf),
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					RESPONSE_TIMEOUT_MS_SHORT);

	if (retval < 0 || resp_length < 2) {
		retval = -EINVAL;
		TPD_INFO("Failed to read dynamic config\n");
		report = tp_kzalloc(30, GFP_KERNEL);
		if (report) {
			snprintf(report, 30, "get_dc_err_%2x", (unsigned int)id);
			tp_kfree((void **)&report);
		}
		goto exit;
	}

	*value = (unsigned short)le2_to_uint(resp_buf);
exit:
	tp_kfree((void **)&resp_buf);
	return retval;
}

static int syna_tcm_set_dynamic_config(struct syna_tcm_data *tcm_info,
				       enum dynamic_config_id id, unsigned short value)
{
	int retval = 0;
	char *report = NULL;
	unsigned char out_buf[3] = {0};
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;

	TPD_DEBUG("%s:config 0x%x, value %d\n", __func__, id, value);

	out_buf[0] = (unsigned char)id;
	out_buf[1] = (unsigned char)value;
	out_buf[2] = (unsigned char)(value >> 8);

	retval = syna_tcm_write_message(tcm_info,
					CMD_SET_DYNAMIC_CONFIG,
					out_buf,
					sizeof(out_buf),
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					RESPONSE_TIMEOUT_MS_SHORT);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_SET_DYNAMIC_CONFIG));
		report = tp_kzalloc(30, GFP_KERNEL);
		if (report) {
			snprintf(report, 30, "set_dc_err_%2x", (unsigned int)id);
			tp_kfree((void **)&report);
		}
		goto exit;
	}

exit:
	tp_kfree((void **)&resp_buf);

	return retval;
}

static int syna_tcm_sleep(struct syna_tcm_data *tcm_info, bool en)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0, resp_length = 0;
	unsigned char command = en ? CMD_ENTER_DEEP_SLEEP : CMD_EXIT_DEEP_SLEEP;

	TPD_INFO("%s: %s .\n", __func__, en ? "enter" : "exit");

	retval = syna_tcm_write_message(tcm_info,
					command,
					NULL,
					0,
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n",
			 en ? STR(CMD_ENTER_DEEP_SLEEP) : STR(CMD_EXIT_DEEP_SLEEP));
		goto exit;
	}

exit:
	tp_kfree((void **)&resp_buf);

	return retval;
}

static int syna_report_refresh_switch(void *chip_data, int fps)
{
	int retval = 0;
	unsigned short send_value = 1;
	int i = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	TPD_DEBUG("%s: refresh_switch: %d HZ!\n", __func__, fps);
	if (tcm_info == NULL) {
		return -1;
	}
	tcm_info->display_refresh_rate = fps;

	if (!*tcm_info->in_suspend && !tcm_info->game_mode) {
		for (i = 0; i < tcm_info->fps_report_rate_num; i = i + 2) {
			if (fps == tcm_info->fps_report_rate_array[i]) {
				send_value = tcm_info->fps_report_rate_array[i + 1];
			}
		}
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_SET_REPORT_FRE, send_value);
		if (retval < 0) {
			TPD_INFO("Failed to set dynamic report frequence config\n");
		}
		TPD_INFO("%s: refresh_switch: %d HZ %s!\n", __func__, fps, retval < 0 ? "failed" : "success");
	}
	return retval;
}

static void syna_rate_white_list_ctrl(void *chip_data, int value)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short send_value = 1;
	int retval = 0;

	if (tcm_info == NULL) {
		return;
	}

	if (*tcm_info->in_suspend || tcm_info->game_mode) {
		return;
	}
	switch (value) {
	case 0: // 60Hz
		send_value = 1;
		break;
	case 1: // 120Hz
		send_value = 2;
		break;
	case 2: // 90Hz
		send_value = 3;
		break;
	default:
		return;
	}

	retval = syna_tcm_set_dynamic_config(tcm_info,
					     DC_SET_REPORT_FRE,
					     send_value);
	if (retval < 0) {
		TPD_INFO("Failed to set dynamic report frequence config\n");
	}
	TPD_INFO("%s: DC_SET_REPORT_FRE: %d  %s!\n",
		 __func__, send_value, retval < 0 ? "failed" : "success");
}

static int synaptics_resetgpio_set(struct hw_resource *hw_res, bool on)
{
	if (gpio_is_valid(hw_res->reset_gpio)) {
		TPD_DEBUG("Set the reset_gpio \n");
		gpio_direction_output(hw_res->reset_gpio, on);
	}

	return 0;
}

static int syna_tcm_reset(void *chip_data)
{
	int retval = 0;
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0;
	unsigned int resp_length = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	mutex_lock(&tcm_info->reset_mutex);

#ifndef CONFIG_ARCH_QTI_VM
	TPD_INFO("start hd reset...\n");
	synaptics_resetgpio_set(tcm_info->hw_res, false);
	msleep(POWEWRUP_TO_RESET_TIME);
	synaptics_resetgpio_set(tcm_info->hw_res, true);
	msleep(RESET_TO_NORMAL_TIME);
        TPD_INFO("End hd reset...\n");

	retval = syna_tcm_identify(tcm_info, false);
#else
	retval = syna_tcm_identify(tcm_info, true);
#endif

	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		goto exit;
	}

	if (tcm_info->id_info.mode == MODE_APPLICATION) {
		goto dispatch_reset;
	}

	retval = syna_tcm_write_message(tcm_info,
					CMD_RUN_APPLICATION_FIRMWARE,
					NULL,
					0,
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write command %s\n", STR(CMD_RUN_APPLICATION_FIRMWARE));
	}

	retval = syna_tcm_identify(tcm_info, false);

	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		goto exit;
	}

dispatch_reset:
	TPD_INFO("Firmware mode = 0x%02x, boot status 0x%02x, app status 0x%02x\n",
		 tcm_info->id_info.mode,
		 tcm_info->boot_info.status,
		 tcm_info->app_status);

exit:
	mutex_unlock(&tcm_info->reset_mutex);

	tp_kfree((void **)&resp_buf);

	return retval;
}

static int syna_get_chip_info(void *chip_data)
{
	int ret = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	TPD_INFO("%s: Enter\n", __func__);

	ret = syna_tcm_reset(tcm_info);  /* reset to get bootloader info or boot info*/

	if (ret < 0) {
		TPD_INFO("failed to reset device\n");
	}

	ret = syna_get_default_report_config(tcm_info);

	if (ret < 0) {
		TPD_INFO("failed to get default report config\n");
	}
#ifdef CONFIG_ARCH_QTI_VM
	ret = syna_set_normal_report_config(tcm_info);
	if (ret < 0) {
		TPD_INFO("failed to set normal report config\n");
	}
	ret = syna_get_input_params(tcm_info);
	if (ret < 0) {
		TPD_INFO("Failed to get input parameters\n");
	}
#endif

	return 0;
}

static int syna_get_vendor(void *chip_data, struct panel_info *panel_data)
{
	return 0;
}

static u32 syna_trigger_reason(void *chip_data, int gesture_enable,
			       int is_suspended)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	tcm_info->trigger_reason = 0;
	syna_tcm_read_message(tcm_info, NULL, 0);

	if (tcm_info->int_check_support && !tcm_info->probe_done) {
		tcm_info->int_check_in_probe = 1;
	}
	return tcm_info->trigger_reason;
}

static int syna_get_touch_points(void *chip_data, struct point_info *points,
				 int max_num)
{
	unsigned int idx = 0, status = 0, i = 0;
	struct object_data *object_data = NULL;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touchpanel_snr *snr = tcm_info->snr;

	if (points == NULL) {
		return tcm_info->obj_attention;
	}

	if (tcm_info->palm_hold_report) {
		TPD_DEBUG("palm mode!\n");
		return 0;
	}

	if (tcm_info->snr_read_support) {
		for (i = 0; i < max_num; i++) {
			snr[i].point_status = 0;
		}
	}
	object_data = touch_hcd->touch_data.object_data;

	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		status = object_data[idx].status;

		if (status != LIFT) {
			tcm_info->obj_attention |= (0x1 << idx);

		} else {
			if ((~tcm_info->obj_attention) & ((0x1) << idx)) {
				continue;

			} else {
				tcm_info->obj_attention &= (~(0x1 << idx));
			}
		}

		points[idx].x = object_data[idx].x_pos;
		points[idx].y = object_data[idx].y_pos;
		points[idx].touch_major = max(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].width_major = min(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].tx_press = object_data[idx].exWidth;
		points[idx].rx_press = object_data[idx].eyWidth;
		points[idx].tx_er = object_data[idx].xERatio;
		points[idx].rx_er = object_data[idx].yERatio;
		points[idx].status = 1;
		if (tcm_info->snr_read_support) {
			if (snr[idx].doing && points[idx].x && points[idx].y) {
				snr[idx].point_status = 1;
				snr[idx].x = points[idx].x;
				snr[idx].y = points[idx].y;
				snr[idx].channel_x = snr[idx].x * tcm_info->hw_res->tx_num / tcm_info->chip_resolution_info->max_x;
				snr[idx].channel_y = snr[idx].y * tcm_info->hw_res->rx_num / tcm_info->chip_resolution_info->max_y;
				TPD_DEBUG("snr: [%d %d, %d] {%d %d} obj 0x%x status %d\n", snr[idx].x, snr[idx].y, idx,
						snr[idx].channel_x, snr[idx].channel_y, tcm_info->obj_attention, status);
			}
		}
	}

	return tcm_info->obj_attention;
}

static int syna_get_touch_points_auto(void *chip_data,
				      struct point_info *points,
				      int max_num,
				      struct resolution_info *resolution_info)
{
	unsigned int idx = 0, status = 0, i = 0;
	struct object_data *object_data = NULL;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touchpanel_snr *snr = tcm_info->snr;

	int max_x_inchip = 0;
	int max_y_inchip = 0;
	int max_x = 0;
	int max_y = 0;

	if (points == NULL) {
		return tcm_info->obj_attention;
	}

	if (tcm_info->palm_hold_report) {
		TPD_DEBUG("palm mode!\n");
		return 0;
	}

	if (tcm_info->snr_read_support) {
		for (i = 0; i < max_num; i++) {
			snr[i].point_status = 0;
		}
	}
	object_data = touch_hcd->touch_data.object_data;

	max_x_inchip = le2_to_uint(tcm_info->app_info.max_x) + 1;
	max_y_inchip = le2_to_uint(tcm_info->app_info.max_y) + 1;
	max_x = resolution_info->max_x;
	max_y = resolution_info->max_y;


	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		status = object_data[idx].status;
		if (status != LIFT) {
			tcm_info->obj_attention |= (0x1 << idx);
		} else {
			if ((~tcm_info->obj_attention) & ((0x1) << idx)) {
				continue;
			} else {
				tcm_info->obj_attention &= (~(0x1 << idx));
			}
		}

		if (max_x_inchip == max_x) {
			points[idx].x = object_data[idx].x_pos;
		} else {
			points[idx].x = (object_data[idx].x_pos * max_x) / max_x_inchip;
		}
		if (max_y_inchip == max_y) {
			points[idx].y = object_data[idx].y_pos;
		} else {
			points[idx].y = (object_data[idx].y_pos * max_y) / max_y_inchip;
		}
		points[idx].touch_major = max(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].width_major = min(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].tx_press = object_data[idx].exWidth;
		points[idx].rx_press = object_data[idx].eyWidth;
		points[idx].tx_er = object_data[idx].xERatio;
		points[idx].rx_er = object_data[idx].yERatio;
		points[idx].status = 1;
		if (tcm_info->snr_read_support) {
			if (snr[idx].doing && points[idx].x && points[idx].y) {
				snr[idx].point_status = 1;
				snr[idx].x = points[idx].x;
				snr[idx].y = points[idx].y;
				snr[idx].channel_x = snr[idx].x * tcm_info->hw_res->tx_num / tcm_info->chip_resolution_info->max_x;
				snr[idx].channel_y = snr[idx].y * tcm_info->hw_res->rx_num / tcm_info->chip_resolution_info->max_y;
				TPD_DEBUG("snr: [%d %d, %d] {%d %d} obj 0x%x status %d\n", snr[idx].x, snr[idx].y, idx,
						snr[idx].channel_x, snr[idx].channel_y, tcm_info->obj_attention, status);
			}
		}
	}

	return tcm_info->obj_attention;
}

static int syna_get_touch_points_help(void *chip_data,
				      struct point_info *points,
				      int max_num,
				      struct resolution_info *resolution_info)
{
	unsigned int idx = 0, status = 0;
	struct object_data *object_data = NULL;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	unsigned int obj_attention = 0x00;
	int max_x_inchip = 0;
	int max_y_inchip = 0;
	int max_x = 0;
	int max_y = 0;

	if (points == NULL) {
		return obj_attention;
	}
	object_data = touch_hcd->touch_data.object_data;

	max_x_inchip = le2_to_uint(tcm_info->app_info.max_x) + 1;
	max_y_inchip = le2_to_uint(tcm_info->app_info.max_y) + 1;
	max_x = resolution_info->max_x;
	max_y = resolution_info->max_y;


	for (idx = 0; idx < touch_hcd->max_objects; idx++) {
		status = object_data[idx].status;
		if (status == LIFT) {
			points[idx].status= 0;
			continue;
		}
		obj_attention |= (0x1 << idx);

		if (max_x_inchip == max_x) {
			points[idx].x = object_data[idx].x_pos;
		} else {
			points[idx].x = (object_data[idx].x_pos * max_x) / max_x_inchip;
		}
		if (max_y_inchip == max_y) {
			points[idx].y = object_data[idx].y_pos;
		} else {
			points[idx].y = (object_data[idx].y_pos * max_y) / max_y_inchip;
		}
		points[idx].touch_major = max(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].width_major = min(object_data[idx].x_width, object_data[idx].y_width);
		points[idx].tx_press = object_data[idx].exWidth;
		points[idx].rx_press = object_data[idx].eyWidth;
		points[idx].tx_er = object_data[idx].xERatio;
		points[idx].rx_er = object_data[idx].yERatio;
		points[idx].status = 1;
	}

	return obj_attention;
}

static int syna_tcm_set_gesture_mode(struct syna_tcm_data *tcm_info, int enable)
{
	int retval = 0;

	/*this command may take too much time, if needed can add flag to skip this */
	TPD_INFO("%s: enable(%d), mask 0x%0X\n", __func__, enable, tcm_info->gesture_mask);

	if (enable) {
		retval = syna_tcm_sleep(tcm_info, false);

		if (retval < 0) {
			TPD_INFO("%s: Failed to exit sleep mode\n", __func__);
			return retval;
		}

		retval = syna_set_input_reporting(tcm_info, true);

		if (retval < 0) {
			TPD_INFO("%s: Failed to set input reporting\n", __func__);
			return retval;
		}

		retval = syna_tcm_set_dynamic_config(tcm_info, DC_IN_WAKEUP_GESTURE_MODE, true);

		if (retval < 0) {
			TPD_INFO("%s: Failed to set dynamic gesture config\n", __func__);
			return retval;
		}

		retval = syna_tcm_set_dynamic_config(tcm_info,
						     DC_GESTURE_MASK,
						     tcm_info->gesture_mask);

		if (retval < 0) {
			TPD_INFO("%s: Failed to set dynamic gesture mask config\n", __func__);
			return retval;
		}

		if (tp_debug != LEVEL_DEBUG) {
			retval = syna_tcm_enable_report(tcm_info, REPORT_LOG, false);

			if (retval < 0) {
				TPD_INFO("Failed to set disable log report\n");
				return retval;
			}
		}
	}

	return retval;
}

static void syna_tcm_enable_gesture_mask(void *chip_data, uint32_t enable)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	/*this command may take too much time, if needed can add flag to skip this */
	TPD_INFO("%s: enable(%d), mask 0x%0X\n", __func__, enable, tcm_info->gesture_mask);

	if (enable) {
		retval = syna_tcm_set_dynamic_config(tcm_info,
						     DC_GESTURE_MASK,
						     tcm_info->gesture_mask);

		if (retval < 0) {
			TPD_INFO("%s: Failed to set dynamic gesture mask config\n", __func__);
		}

	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_GESTURE_MASK, 0x0000);

		if (retval < 0) {
			TPD_INFO("%s: Failed to set dynamic gesture mask config\n", __func__);
		}
	}
}

static int syna_tcm_set_game_mode(struct syna_tcm_data *tcm_info, int enable)
{
	int retval = 0;
	unsigned short regval = 0;
	uint16_t report_rate = 0;
	struct touchpanel_data *ts = NULL;

	if (!tcm_info || !tcm_info->client) {
		TPD_INFO("tcm_info or tcm_info->client is NULL\n");
		return -1;
	}

	ts = i2c_get_clientdata(tcm_info->client);
	if (!ts) {
		TPD_INFO("ts is NULL\n");
		return -1;
	}
	//unsigned short noise_length = 0;

	tcm_info->game_mode = !!enable;
	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get DC_ERROR_PRIORITY val\n");
		return retval;
	}
	TPD_INFO("%s: enable[%d], now reg status[0x%x]\n", __func__, tcm_info->game_mode, regval);

	if (enable) {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, regval|0x01);

		if (retval < 0) {
			TPD_INFO("Failed to set dynamic error priority config\n");
			return retval;
		}

		/*noise_length = 0x0A;

		retval = syna_tcm_set_dynamic_config(tcm_info, DC_NOISE_LENGTH, noise_length);

		if (retval < 0) {
			TPD_INFO("Failed to set dynamic noise length config\n");
			return retval;
		}*/


		if (tcm_info->switch_game_rate_support) {/*tcm_info->game_rate_switch_support*/
	    	switch (ts->noise_level) {
	    	case 180:
		    report_rate = 3;
		    break;
	    	case 300:
		    report_rate = 1;
		    break;
	    	case 600:
		    report_rate = 9;
		    break;
	    	default:
		    report_rate = tcm_info->game_rate;
		    break;
	    	}
	    	TPD_INFO("%s:set report_rate:%d", __func__, report_rate);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_SET_REPORT_FRE, report_rate);
		if (retval < 0) {
		    TPD_INFO("Failed to set dynamic report frequence config\n");
		    return retval;
		}
	} else {
	    retval = syna_tcm_set_dynamic_config(tcm_info, DC_SET_REPORT_FRE, tcm_info->game_rate);
	    if (retval < 0) {
		TPD_INFO("Failed to set dynamic report frequence config\n");
                return retval;
	    }
	}
	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, regval&0xF0);

		if (retval < 0) {
			TPD_INFO("Failed to set dynamic error priority config\n");
			return retval;
		}

		/*retval = syna_tcm_set_dynamic_config(tcm_info, DC_NOISE_LENGTH,
						     tcm_info->default_noise_length);

		if (retval < 0) {
			TPD_INFO("Failed to set dynamic noise length config\n");
			return retval;
		}*/

		syna_report_refresh_switch(tcm_info, tcm_info->display_refresh_rate);

		if (retval < 0) {
			TPD_INFO("Failed to set dynamic report frequence config\n");
			return retval;
		}
	}

	return retval;
}

static int syna_tcm_set_high_frame_rate(void *chip_data, int level, int time)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short regval = 0;
	int retval = 0;
	int temp_time = 0;

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get high frame config\n");
		return 0;
	}

	if (level > 0) {
		temp_time = time;
		if (0 == temp_time) {
			temp_time = 12;
		} else if (temp_time > 0) {
			temp_time = temp_time / 5;
		}
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, regval|0x02|(temp_time << 8));
		if (retval < 0) {
			TPD_INFO("Failed to enable high frame config\n");
			goto OUT;
		}
	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, regval&0xfffd);
		if (retval < 0) {
			TPD_INFO("Failed to disable high frame config\n");
			goto OUT;
		}
	}

	TPD_INFO("synaptics %s high frame success lv to %d, time to %d",
		level > 0 ? "enable" : "disable", level, time);

OUT:
	return 0;
}


static int syna_tcm_normal_mode(struct syna_tcm_data *tcm_info)
{
	int retval;

	TPD_INFO("%s : enter\n", __func__);
	retval = syna_set_input_reporting(tcm_info, false);

	if (retval < 0) {
		TPD_INFO("Failed to set input reporting\n");
		return retval;
	}

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_IN_WAKEUP_GESTURE_MODE,
					     false);

	if (retval < 0) {
		TPD_INFO("Failed to set dynamic gesture config\n");
		return retval;
	}

	syna_report_refresh_switch(tcm_info, tcm_info->display_refresh_rate);

	retval = syna_tcm_enable_report(tcm_info, REPORT_LOG, true);

	if (retval < 0) {
		TPD_INFO("Failed to set enable log report\n");
		return retval;
	}

	return retval;
}

static int syna_corner_limit_handle(struct syna_tcm_data *tcm_info, int enable)
{
	int ret = -1;

	if (LANDSCAPE_SCREEN_90 == enable) {
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL,
						  0x01);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_SEL, 0x0F);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_SEL\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_X, 0x0A);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_X\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_Y, 0x0A);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_Y\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_DARK_ZONE_ENABLE, 0x03);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_DARK_ZONE_ENABLE\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, 0xFF);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_X\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_Y, 0x44);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_Y\n", __func__);
			return ret;
		}

	} else if (LANDSCAPE_SCREEN_270 == enable) {
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL,
						  0x01);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_SEL, 0x0F);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_SEL\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_X, 0x0A);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_X\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_Y, 0x0A);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_Y\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_DARK_ZONE_ENABLE, 0x0C);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_DARK_ZONE_ENABLE\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, 0xFF);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_X\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_Y, 0x44);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_Y\n", __func__);
			return ret;
		}

	} else if (VERTICAL_SCREEN == enable) {
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL,
						  0x00);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ROATE_TO_HORIZONTAL_LEVEL\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_SEL, 0x03);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_SEL\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_X, 0x0A);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_X\n", __func__);
			return ret;
		}

		/*ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_U, 0x32);*/
		/*if (ret < 0) {*/
		/*    TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_U\n", __func__);*/
		/*    return ret;*/
		/*}*/
		/*ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ABS_DARK_V, 0x64);*/
		/*if (ret < 0) {*/
		/*    TPD_INFO("%s:failed to set DC_GRIP_ABS_DARK_V\n", __func__);*/
		/*    return ret;*/
		/*}*/
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_DARK_ZONE_ENABLE, 0x05);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_DARK_ZONE_ENABLE\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_X, 0x24);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_X\n", __func__);
			return ret;
		}

		ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_DARK_ZONE_Y, 0xF5);

		if (ret < 0) {
			TPD_INFO("%s:failed to set DC_GRIP_DARK_ZONE_Y\n", __func__);
			return ret;
		}
	}

	return ret;
}

static int syna_enable_edge_limit(struct syna_tcm_data *tcm_info, int enable)
{
	int ret = 0;
	TPD_INFO("%s: enter\n", __func__);

	ret = syna_tcm_set_dynamic_config(tcm_info, DC_GRIP_ENABLED, 0x01);

	if (ret < 0) {
		TPD_INFO("%s:failed to enable grip suppression\n", __func__);
		return ret;
	}

	ret = syna_corner_limit_handle(tcm_info, enable);

	if (ret < 0) {
		TPD_INFO("%s:failed to set grip suppression para\n", __func__);
		return ret;
	}

	return ret;
}

static int syna_mode_switch(void *chip_data, work_mode mode, int flag)
{
	int ret = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	switch (mode) {
	case MODE_NORMAL:
		if (!*tcm_info->in_suspend) {
			ret = syna_tcm_normal_mode(tcm_info);

			if (ret < 0) {
				tcm_info->error_state_count++;
				TPD_INFO("normal mode switch failed\n");
				if (tcm_info->error_state_count >= ERROR_STATE_MAX) {
					syna_tcm_reset(tcm_info); /*ic state err, need to reset the IC*/
				}
			}
			tcm_info->error_state_count = 0;
		}

		break;

	case MODE_GESTURE:
		if (*tcm_info->in_suspend) {
			ret = syna_tcm_set_gesture_mode(tcm_info, flag);

			if (ret < 0) {
				TPD_INFO("%s:Failed to set gesture mode\n", __func__);
			}
		}

		break;

	case MODE_SLEEP:
		ret = syna_tcm_sleep(tcm_info, flag);

		if (ret < 0) {
			TPD_INFO("%s: failed to switch to sleep", __func__);
		}

		break;

	case MODE_CHARGE:
		ret = syna_tcm_set_dynamic_config(tcm_info, DC_CHARGER_CONNECTED, flag ? 1 : 0);

		if (ret < 0) {
			TPD_INFO("%s:failed to set charger mode\n", __func__);
		}

		break;

	case MODE_EDGE:
		ret = syna_enable_edge_limit(tcm_info, flag);
		if (ret < 0) {
			TPD_INFO("%s: failed to enable edg limit.\n", __func__);
		}

		break;

	case MODE_GAME:
		ret = syna_tcm_set_game_mode(tcm_info, flag);

		if (ret < 0) {
			TPD_INFO("%s:failed to set game mode\n", __func__);
		}

		break;

	default:
		break;
	}

	return 0;
}

static int syna_ftm_process(void *chip_data)
{
	TPD_INFO("%s: go into sleep\n", __func__);
	syna_get_chip_info(chip_data);
	syna_mode_switch(chip_data, MODE_SLEEP, true);
	return 0;
}

static int syna_tcm_reinit_device(void *chip_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	complete_all(&tcm_info->response_complete);
	complete_all(&tcm_info->report_complete);

	return 0;
}

static int syna_power_control(void *chip_data, bool enable)
{
	int ret = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	TPD_DEBUG("%s: %d\n", __func__, enable);

	if (true == enable) {
		ret = tp_powercontrol_avdd(tcm_info->hw_res, true);

		if (ret) {
			return -1;
		}

		ret = tp_powercontrol_vddi(tcm_info->hw_res, true);

		if (ret) {
			return -1;
		}

		synaptics_resetgpio_set(tcm_info->hw_res, false);
		msleep(POWEWRUP_TO_RESET_TIME);
		synaptics_resetgpio_set(tcm_info->hw_res, true);
		msleep(RESET_TO_NORMAL_TIME);

	} else {
		synaptics_resetgpio_set(tcm_info->hw_res, false);
		disable_irq(tcm_info->ts->irq);
		ret = tp_powercontrol_vddi(tcm_info->hw_res, false);

		if (ret) {
			return -1;
		}

		ret = tp_powercontrol_avdd(tcm_info->hw_res, false);

		if (ret) {
			return -1;
		}
	}

	return ret;
}

static fw_check_state syna_fw_check(void *chip_data,
				    struct resolution_info *resolution_info,
				    struct panel_info *panel_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	u16 config = 0;
	int retval = 0;
	int retfw = 0;

	TPD_INFO("fw id %d, custom config id 0x%s\n", panel_data->tp_fw,
		 (char *)(tcm_info->app_info.customer_config_id));

	if (strlen(tcm_info->app_info.customer_config_id) == 0) {
		return FW_ABNORMAL;
	}

	retfw = sscanf(tcm_info->app_info.customer_config_id, "%x", &panel_data->tp_fw);

	if (retfw != 1)
		return FW_ABNORMAL;

	if (panel_data->tp_fw == 0) {
		return FW_ABNORMAL;
	}

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_NOISE_LENGTH, &config);

	if (retval < 0) {
		TPD_INFO("Failed to get default noise length\n");
		return FW_ABNORMAL;
	}

	tcm_info->default_noise_length = config;

	return FW_NORMAL;
}

static int syna_tcm_helper(struct syna_tcm_data *tcm_info)
{
	if (tcm_info->id_info.mode != MODE_APPLICATION
	    && !mutex_is_locked(&tcm_info->reset_mutex)) {
		TPD_INFO("%s: use helper\n", __func__);
		queue_work(tcm_info->helper_workqueue, &tcm_info->helper_work);
	}

	return 0;
}

static void syna_tcm_helper_work(struct work_struct *work)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = container_of(work, struct syna_tcm_data,
					 helper_work);

	mutex_lock(&tcm_info->reset_mutex);
	retval = syna_tcm_run_application_firmware(tcm_info);

	if (retval < 0) {
		TPD_INFO("Failed to switch to app mode\n");
	}

	mutex_unlock(&tcm_info->reset_mutex);
}

static int syna_tcm_async_work(void *chip_data)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	if (!tcm_info || tcm_info->id_info.mode != MODE_APPLICATION) {
		return 0;
	}

	if (tcm_info->boot_flag) {
		tcm_info->boot_flag = false;
		return 0;
	}

	retval = syna_tcm_identify(tcm_info, false);
	if (retval < 0) {
		TPD_INFO("Failed to do identification\n");
		return retval;
	}
	tp_fw_auto_reset_handle(tcm_info->ts);
	//syna_set_trigger_reason(tcm_info, IRQ_IGNORE);
	//syna_set_trigger_reason(tcm_info, IRQ_FW_AUTO_RESET);
	TPD_INFO("%s  exit\n", __func__);
	return 0;
}

static int syna_tcm_enable_report(struct syna_tcm_data *tcm_info,
				  enum report_type report_type, bool enable)
{
	int retval;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;
	unsigned char out[2] = {0};
	unsigned char *resp_buf = NULL;
	unsigned int resp_buf_size = 0;
	unsigned int resp_length = 0;

	test_hcd->report_index = 0;
	test_hcd->report_type = report_type;

	out[0] = test_hcd->report_type;

	retval = syna_tcm_write_message(tcm_info,
					enable ? CMD_ENABLE_REPORT : CMD_DISABLE_REPORT,
					out,
					1,
					&resp_buf,
					&resp_buf_size,
					&resp_length,
					0);

	if (retval < 0) {
		TPD_INFO("Failed to write message %s\n",
			 enable ? STR(CMD_ENABLE_REPORT) : STR(CMD_DISABLE_REPORT));
	}

	return retval;
}

static void syna_tcm_enable_fingerprint(void *chip_data, uint32_t enable)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	TPD_INFO("%s: enable(%d)\n", __func__, enable);

	if (enable) {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_TOUCH_HOLD,
						     *tcm_info->in_suspend ? 0x01 : 0x02);

		if (retval < 0) {
			TPD_INFO("Failed to set dynamic touch and hold config\n");
			return;
		}

		/*retval = syna_tcm_enable_report(tcm_info, REPORT_TOUCH_HOLD,
						*tcm_info->in_suspend ? false : true);

		if (retval < 0) {
			TPD_INFO("Failed to set enable touch and hold report\n");
			return;
		}*/

	} else {
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_TOUCH_HOLD, 0x00);

		if (retval < 0) {
			TPD_INFO("Failed to set dynamic touch and hold config\n");
			return;
		}

		/*retval = syna_tcm_enable_report(tcm_info, REPORT_TOUCH_HOLD, false);

		if (retval < 0) {
			TPD_INFO("Failed to set disable touch and hold report\n");
			return;
		}*/
	}

	return;
}

static void syna_tcm_fingerprint_info(void *chip_data,
				      struct fp_underscreen_info *fp_tpinfo)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touch_data *touch_data = &touch_hcd->touch_data;
	u8 *fp_buf = touch_data->extra_gesture_info;

	if (!fp_tpinfo) {
		return;
	}

	if (tcm_info->report.buffer.data_length < 8
	    && touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
		TPD_INFO("%s: invalid fingerprint buf length\n", __func__);
		return;
	}

	if (touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
		fp_tpinfo->touch_state = FINGERPRINT_DOWN_DETECT;
		fp_tpinfo->x = fp_buf[0] | fp_buf[1] << 8;
		fp_tpinfo->y = fp_buf[2] | fp_buf[3] << 8;
		fp_tpinfo->area_rate = fp_buf[4] | fp_buf[5] << 8;

	} else if (touch_data->lpwg_gesture == TOUCH_HOLD_UP) {
		fp_tpinfo->touch_state = FINGERPRINT_UP_DETECT;
	}

	return;
}

static void syna_tcm_fingerprint_info_auto(void *chip_data,
		struct fp_underscreen_info *fp_tpinfo,
		struct resolution_info *resolution_info)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touch_data *touch_data = &touch_hcd->touch_data;
	u8 *fp_buf = touch_data->extra_gesture_info;

	int max_x_inchip = 0;
	int max_y_inchip = 0;
	int max_x = 0;
	int max_y = 0;

	if (!fp_tpinfo) {
		return;
	}

	if (tcm_info->report.buffer.data_length < 8
	    && touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
		TPD_INFO("%s: invalid fingerprint buf length\n", __func__);
		return;
	}

	if (touch_data->lpwg_gesture != TOUCH_HOLD_DOWN
	    && touch_data->lpwg_gesture != TOUCH_HOLD_UP) {
		return;
	}

	max_x_inchip = le2_to_uint(tcm_info->app_info.max_x) + 1;
	max_y_inchip = le2_to_uint(tcm_info->app_info.max_y) + 1;
	max_x = resolution_info->LCD_WIDTH;
	max_y = resolution_info->LCD_HEIGHT;

	if (touch_data->lpwg_gesture == TOUCH_HOLD_DOWN) {
		fp_tpinfo->touch_state = FINGERPRINT_DOWN_DETECT;
		fp_tpinfo->x = fp_buf[0] | fp_buf[1] << 8;
		fp_tpinfo->y = fp_buf[2] | fp_buf[3] << 8;
		fp_tpinfo->area_rate = fp_buf[4] | fp_buf[5] << 8;
	} else if (touch_data->lpwg_gesture == TOUCH_HOLD_UP) {
		fp_tpinfo->touch_state = FINGERPRINT_UP_DETECT;
		fp_tpinfo->x = fp_buf[0] | fp_buf[1] << 8;
		fp_tpinfo->y = fp_buf[2] | fp_buf[3] << 8;
		fp_tpinfo->area_rate = fp_buf[4] | fp_buf[5] << 8;
	}

	if (max_x_inchip != max_x) {
		fp_tpinfo->x = (fp_tpinfo->x * max_x) / max_x_inchip;
	}

	if (max_y_inchip != max_y) {
		fp_tpinfo->y = (fp_tpinfo->y * max_y) / max_y_inchip;
	}

	return;
}

static void syna_tcm_get_health_info(void *chip_data,
				     struct monitor_data *mon_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct health_info *health_info = (struct health_info *)
					  tcm_info->report.buffer.buf;
	int data_length = tcm_info->report.buffer.data_length;
	struct health_info *health_local = &tcm_info->health_info;
	int i = 0;
	int retval;

	if (data_length < 20) {
		TPD_INFO("%s: invalid health debug buf length\n", __func__);
		return;
	}


	retval = tp_memcpy(health_local, sizeof(struct health_info), health_info,
			   sizeof(struct health_info), sizeof(struct health_info));
	if (retval < 0) {
		TPD_INFO("%s: tp_memcpy is error!\n", __func__);
	}

	if (tp_debug != 0) {
		for (i = 0; i < data_length; i++) {
			TPD_INFO("[0x%x], ", tcm_info->report.buffer.buf[i]);
		}
	}
}

static int syna_get_gesture_info(void *chip_data, struct gesture_info *gesture)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	struct touch_hcd *touch_hcd = tcm_info->touch_hcd;
	struct touch_data *touch_data = &touch_hcd->touch_data;

	gesture->clockwise = 2;

	switch (touch_data->lpwg_gesture) {
	case DTAP_DETECT:
		gesture->gesture_type = DOU_TAP;
		break;

	case CIRCLE_DETECT:
		gesture->gesture_type = CIRCLE_GESTURE;

		if (touch_data->extra_gesture_info[2] == 0x10) {
			gesture->clockwise = 1;

		} else if (touch_data->extra_gesture_info[2] == 0x20) {
			gesture->clockwise = 0;
		}

		break;

	case SWIPE_DETECT:
		if (touch_data->extra_gesture_info[4] == 0x41) { /*x+*/
			gesture->gesture_type = LEFT2RIGHT_SWIP;

		} else if (touch_data->extra_gesture_info[4] == 0x42) { /*x-*/
			gesture->gesture_type = RIGHT2LEFT_SWIP;

		} else if (touch_data->extra_gesture_info[4] == 0x44) { /*y+*/
			gesture->gesture_type = UP2DOWN_SWIP;

		} else if (touch_data->extra_gesture_info[4] == 0x48) { /*y-*/
			gesture->gesture_type = DOWN2UP_SWIP;

		} else if (touch_data->extra_gesture_info[4] == 0x81) { /*2x-*/
			gesture->gesture_type = DOU_SWIP;

		} else if (touch_data->extra_gesture_info[4] == 0x82) { /*2x+*/
			gesture->gesture_type = DOU_SWIP;

		} else if (touch_data->extra_gesture_info[4] == 0x84) { /*2y+*/
			gesture->gesture_type = DOU_SWIP;

		} else if (touch_data->extra_gesture_info[4] == 0x88) { /*2y-*/
			gesture->gesture_type = DOU_SWIP;
		}

		break;

	case M_UNICODE:
		gesture->gesture_type = M_GESTRUE;
		break;

	case W_UNICODE:
		gesture->gesture_type = W_GESTURE;
		break;

	case VEE_DETECT:
		if (touch_data->extra_gesture_info[2] == 0x02) { /*up*/
			gesture->gesture_type = UP_VEE;

		} else if (touch_data->extra_gesture_info[2] == 0x01) { /*down*/
			gesture->gesture_type = DOWN_VEE;

		} else if (touch_data->extra_gesture_info[2] == 0x08) { /*left*/
			gesture->gesture_type = LEFT_VEE;

		} else if (touch_data->extra_gesture_info[2] == 0x04) { /*right*/
			gesture->gesture_type = RIGHT_VEE;
		}

		break;

	case TOUCH_HOLD_DOWN:
		gesture->gesture_type = FINGER_PRINTDOWN;
		break;

	case TOUCH_HOLD_UP:
		gesture->gesture_type = FRINGER_PRINTUP;
		break;

	case HEART_DETECT:
		gesture->gesture_type = HEART;

		if (touch_data->extra_gesture_info[2] == 0x10) {
			gesture->clockwise = 1;

		} else if (touch_data->extra_gesture_info[2] == 0x20) {
			gesture->clockwise = 0;
		}
		break;

	case STAP_DETECT:
		gesture->gesture_type = SINGLE_TAP;
		break;

	case S_UNICODE:
		gesture->gesture_type = S_GESTURE;
		break;

	case TRIANGLE_DETECT:
	default:
		TPD_DEBUG("not support\n");
		break;
	}

	if (gesture->gesture_type != UNKOWN_GESTURE) {
		gesture->Point_start.x = (touch_data->data_point[0] | (touch_data->data_point[1]
					  << 8));
		gesture->Point_start.y = (touch_data->data_point[2] | (touch_data->data_point[3]
					  << 8));
		gesture->Point_end.x    = (touch_data->data_point[4] |
					   (touch_data->data_point[5] << 8));
		gesture->Point_end.y    = (touch_data->data_point[6] |
					   (touch_data->data_point[7] << 8));
		gesture->Point_1st.x    = (touch_data->data_point[8] |
					   (touch_data->data_point[9] << 8));
		gesture->Point_1st.y    = (touch_data->data_point[10] |
					   (touch_data->data_point[11] << 8));
		gesture->Point_2nd.x    = (touch_data->data_point[12] |
					   (touch_data->data_point[13] << 8));
		gesture->Point_2nd.y    = (touch_data->data_point[14] |
					   (touch_data->data_point[15] << 8));
		gesture->Point_3rd.x    = (touch_data->data_point[16] |
					   (touch_data->data_point[17] << 8));
		gesture->Point_3rd.y    = (touch_data->data_point[18] |
					   (touch_data->data_point[19] << 8));
		gesture->Point_4th.x    = (touch_data->data_point[20] |
					   (touch_data->data_point[21] << 8));
		gesture->Point_4th.y    = (touch_data->data_point[22] |
					   (touch_data->data_point[23] << 8));
	}

	if (gesture->gesture_type == SINGLE_TAP || gesture->gesture_type == DOU_TAP) {
		gesture->Point_start.x = (touch_data->extra_gesture_info[0] | (touch_data->extra_gesture_info[1] << 8));
		gesture->Point_start.y = (touch_data->extra_gesture_info[2] | (touch_data->extra_gesture_info[3] << 8));
	}

	TPD_INFO("lpwg:0x%x, type:%d, clockwise: %d, points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n",
		 touch_data->lpwg_gesture, gesture->gesture_type, gesture->clockwise, \
		 gesture->Point_start.x, gesture->Point_start.y, \
		 gesture->Point_end.x, gesture->Point_end.y, \
		 gesture->Point_1st.x, gesture->Point_1st.y, \
		 gesture->Point_2nd.x, gesture->Point_2nd.y, \
		 gesture->Point_3rd.x, gesture->Point_3rd.y, \
		 gesture->Point_4th.x, gesture->Point_4th.y);

	return 0;
}

static int syna_get_gesture_info_auto(void *chip_data,
				      struct gesture_info *gesture,
				      struct resolution_info *resolution_info)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	int max_x_inchip = 0;
	int max_y_inchip = 0;
	int max_x = 0;
	int max_y = 0;

	max_x_inchip = le2_to_uint(tcm_info->app_info.max_x) + 1;
	max_y_inchip = le2_to_uint(tcm_info->app_info.max_y) + 1;
	max_x = resolution_info->max_x;
	max_y = resolution_info->max_y;


	syna_get_gesture_info(chip_data, gesture);

	if (max_x_inchip == max_x && max_y_inchip == max_y) {
		return 0;
	}

	if (gesture->gesture_type == UNKOWN_GESTURE) {
		return 0;
	}

	if (max_x_inchip != max_x) {
		gesture->Point_start.x = (gesture->Point_start.x * max_x) / max_x_inchip;
		gesture->Point_end.x    = (gesture->Point_end.x * max_x) / max_x_inchip;
		gesture->Point_1st.x    = (gesture->Point_1st.x * max_x) / max_x_inchip;
		gesture->Point_2nd.x    = (gesture->Point_2nd.x * max_x) / max_x_inchip;
		gesture->Point_3rd.x    = (gesture->Point_3rd.x * max_x) / max_x_inchip;
		gesture->Point_4th.x    = (gesture->Point_4th.x * max_x) / max_x_inchip;
	}

	if (max_y_inchip != max_y) {
		gesture->Point_start.y = (gesture->Point_start.y * max_y) / max_y_inchip;
		gesture->Point_end.y    = (gesture->Point_end.y * max_y) / max_y_inchip;
		gesture->Point_1st.y    = (gesture->Point_1st.y * max_y) / max_y_inchip;
		gesture->Point_2nd.y    = (gesture->Point_2nd.y * max_y) / max_y_inchip;
		gesture->Point_3rd.y    = (gesture->Point_3rd.y * max_y) / max_y_inchip;
		gesture->Point_4th.y    = (gesture->Point_4th.y * max_y) / max_y_inchip;
	}

	TPD_INFO("changed points: (%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)(%d, %d)\n",
		 gesture->Point_start.x, gesture->Point_start.y, \
		 gesture->Point_end.x, gesture->Point_end.y, \
		 gesture->Point_1st.x, gesture->Point_1st.y, \
		 gesture->Point_2nd.x, gesture->Point_2nd.y, \
		 gesture->Point_3rd.x, gesture->Point_3rd.y, \
		 gesture->Point_4th.x, gesture->Point_4th.y);

	return 0;
}

static void syna_tcm_test_report(struct syna_tcm_data *tcm_info)
{
	int retval;
	unsigned int offset, report_size;
	struct syna_tcm_test *test_hcd = tcm_info->test_hcd;

	if (tcm_info->report.id != test_hcd->report_type) {
		TPD_INFO("Not request report type\n");
		return;
	}

	report_size = tcm_info->report.buffer.data_length;
	LOCK_BUFFER(test_hcd->report);

	if (test_hcd->report_index == 0) {
		retval = syna_tcm_alloc_mem(&test_hcd->report,
					    report_size * test_hcd->num_of_reports);

		if (retval < 0) {
			TPD_INFO("Failed to allocate memory\n");
			UNLOCK_BUFFER(test_hcd->report);
			return;
		}
	}

	if (test_hcd->report_index < test_hcd->num_of_reports) {
		offset = report_size * test_hcd->report_index;
		retval = tp_memcpy(test_hcd->report.buf + offset,
				   test_hcd->report.buf_size - offset,
				   tcm_info->report.buffer.buf,
				   tcm_info->report.buffer.buf_size,
				   tcm_info->report.buffer.data_length);

		if (retval < 0) {
			TPD_INFO("Failed to copy report data\n");

			UNLOCK_BUFFER(test_hcd->report);
			return;
		}

		test_hcd->report_index++;
		test_hcd->report.data_length += report_size;
	}

	UNLOCK_BUFFER(test_hcd->report);

	if (test_hcd->report_index == test_hcd->num_of_reports) {
		complete(&tcm_info->report_complete);
	}

	return;
}

static int syna_device_report_touch(struct syna_tcm_data *tcm_info)
{
	int ret = syna_parse_report(tcm_info);

	if (ret < 0) {
		TPD_INFO("Failed to parse report\n");
		return -EINVAL;
	}

	syna_set_trigger_reason(tcm_info, IRQ_TOUCH);

	return 0;
}

static int syna_resume_prepare(void *chip_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	tcm_info->palm_hold_report = 0;
	TPD_DEBUG("%s: clear palm_hold_report.\n", __func__);
	reinit_completion(&tcm_info->resume_complete);
	return 0;
}

static int syna_specific_resume_operate(void *chip_data, struct specific_resume_data *p)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	int timed_out = 0;

	tcm_info->suspend_state = p->suspend_state;

	timed_out = wait_for_completion_timeout(&tcm_info->resume_complete, 0.5 * HZ); //wait resume over for 0.5s

	if ((0 == timed_out) || (tcm_info->resume_complete.done)) {
		TPD_INFO("resume state, timed_out:%d, done:%d\n", timed_out, tcm_info->resume_complete.done);
	}
	return 0;
}

static void syna_set_touch_direction(void *chip_data, uint8_t dir)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	tcm_info->touch_direction = dir;
}

static uint8_t syna_get_touch_direction(void *chip_data)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	return tcm_info->touch_direction;
}

void syna_freq_hop_trigger(void *chip_data)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	TPD_INFO("%s : send cmd to tigger frequency hopping here!!!\n", __func__);

	switch (tcm_info->freq_point) {
	case 0:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, tcm_info->freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, tcm_info->freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		tcm_info->freq_point = 1;
		break;

	case 1:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, tcm_info->freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, tcm_info->freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		tcm_info->freq_point = 2;
		break;

	case 2:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, tcm_info->freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, tcm_info->freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		tcm_info->freq_point = 3;
		break;
	case 3:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, tcm_info->freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING, tcm_info->freq_point);
		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}
		tcm_info->freq_point = 4;
		break;

	case 4:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, tcm_info->freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING,
						     tcm_info->freq_point);

		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}

		tcm_info->freq_point = 5;
		break;

	case 5:
		TPD_INFO("%s : Hop to frequency : %d\n", __func__, tcm_info->freq_point);
		retval = syna_tcm_set_dynamic_config(tcm_info, DC_FREQUENCE_HOPPING,
						     tcm_info->freq_point);

		if (retval < 0) {
			TPD_INFO("Failed to hop frequency\n");
		}

		tcm_info->freq_point = 0;
		break;

	default:
		break;
	}
}

static void syna_force_water_mode(void *chip_data, bool enable)
{
	int retval = 0;
	unsigned short regval = 0;
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;

	TPD_INFO("%s: %s force water mode.\n", __func__, enable ? "Enter" : "Exit");

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get water mode config\n");
		return;
	}

	if (enable)  {
		regval = regval | 0x04;
	} else {
		regval = regval & 0xfb;
	}
	retval = syna_tcm_set_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, regval);
	if (retval < 0) {
		TPD_INFO("Failed to set water mode config\n");
		return;
	}

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get water mode config\n");
		return;
	}
	TPD_INFO("%s: now reg_val=0x%x", __func__, regval);
}

static int syna_tcm_smooth_lv_set(void *chip_data, int level)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short regval = 0;
	int retval = 0;

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get smooth config\n");
		tcm_info->error_state_count++;
		return 0;
	}
	tcm_info->error_state_count = 0;

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_ERROR_PRIORITY, (level<<4)|(regval&0x01));
	if (retval < 0) {
		TPD_INFO("Failed to set smooth config\n");
		return 0;
	}

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_ERROR_PRIORITY, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get smooth config\n");
		return 0;
	}
	TPD_INFO("OK synaptics smooth lv to %d, now reg_val:0x%x", level, regval);
	return 0;
}

static int syna_tcm_sensitive_lv_set(void *chip_data, int level)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short regval = 0;
	int retval = 0;

	retval = syna_tcm_set_dynamic_config(tcm_info, DC_NOISE_LENGTH, level);
	if (retval < 0) {
		TPD_INFO("Failed to set sensitive config\n");
		tcm_info->error_state_count++;
		return 0;
	}
	tcm_info->error_state_count = 0;

	retval = syna_tcm_get_dynamic_config(tcm_info, DC_NOISE_LENGTH, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get sensitive config\n");
		return 0;
	}
	TPD_INFO("OK synaptics sensitive lv to %d, now reg_val:%d", level, regval);

	return 0;
}

static void syna_set_gesture_state(void *chip_data, int state)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	uint16_t state_inchip = 0;

	SET_GESTURE_BIT(state, DOU_TAP, state_inchip, 0);
	SET_GESTURE_BIT(state, UP_VEE, state_inchip, 2);
	SET_GESTURE_BIT(state, DOWN_VEE, state_inchip, 1);
	SET_GESTURE_BIT(state, LEFT_VEE, state_inchip, 3);
	SET_GESTURE_BIT(state, RIGHT_VEE, state_inchip, 4);
	SET_GESTURE_BIT(state, CIRCLE_GESTURE, state_inchip, 5);
	SET_GESTURE_BIT(state, DOU_SWIP, state_inchip, 6);
	SET_GESTURE_BIT(state, LEFT2RIGHT_SWIP, state_inchip, 7);
	SET_GESTURE_BIT(state, RIGHT2LEFT_SWIP, state_inchip, 8);
	SET_GESTURE_BIT(state, UP2DOWN_SWIP, state_inchip, 9);
	SET_GESTURE_BIT(state, DOWN2UP_SWIP, state_inchip, 10);
	SET_GESTURE_BIT(state, M_GESTRUE, state_inchip, 11);
	SET_GESTURE_BIT(state, W_GESTURE, state_inchip, 12);
	SET_GESTURE_BIT(state, SINGLE_TAP, state_inchip, 13);
	SET_GESTURE_BIT(state, HEART, state_inchip, 14);
	SET_GESTURE_BIT(state, S_GESTURE, state_inchip, 15);

	tcm_info->gesture_mask = state_inchip;
	TPD_INFO("%s:state:%d, gesture_mask is 0x%0X!\n", __func__, state, tcm_info->gesture_mask);
}

static int syna_tcm_send_temperature(void *chip_data, int temp, bool status)
{
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	unsigned short regval = 0;
	int retval = 0;
	unsigned short temp_mode = 0;

	/*enable low tempreratue*/
	if (temp <= tcm_info->syna_tempepratue[0]) {
		temp_mode = 1;
		tcm_info->syna_low_temp_enable ++;
		tcm_info->syna_low_temp_disable = 0;
	/*disable low tempreratue*/
	} else if (temp > tcm_info->syna_tempepratue[1]) {
		temp_mode = 0;
		tcm_info->syna_low_temp_enable = 0;
		tcm_info->syna_low_temp_disable ++;
	} else {
		if (tcm_info->syna_low_temp_enable > 0) {
			temp_mode = 1;
			tcm_info->syna_low_temp_enable ++;
			tcm_info->syna_low_temp_disable = 0;
		} else {
			temp_mode = 0;
			tcm_info->syna_low_temp_enable = 0;
			tcm_info->syna_low_temp_disable ++;
		}
	}

	if (tcm_info->syna_low_temp_enable > 1 || tcm_info->syna_low_temp_disable > 1) {
		TPD_DEBUG("enable or disable low temp mode is more than one time\n");
		return 0;
	}
	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get temperature config\n");
		return 0;
	}

	if (1 == temp_mode)  {
		temp_mode = regval | 0x02;
	} else {
		temp_mode = regval & 0xfd;
	}
	retval = syna_tcm_set_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, temp_mode);
	if (retval < 0) {
		TPD_INFO("Failed to set temperature config\n");
		return 0;
	}

 	retval = syna_tcm_get_dynamic_config(tcm_info, DC_LOW_TEMP_ENABLE, &regval);
	if (retval < 0) {
		TPD_INFO("Failed to get temperature config\n");
		return 0;
	}
	TPD_INFO("OK synaptics temperature to %d, now reg_val:%d", temp_mode, regval);

	return retval;
}

static struct oplus_touchpanel_operations syna_tcm_ops = {
	.ftm_process			= syna_ftm_process,
	.get_vendor			= syna_get_vendor,
	.get_chip_info			= syna_get_chip_info,
	.get_touch_points		= syna_get_touch_points,
	.get_gesture_info		= syna_get_gesture_info,
	.power_control			= syna_power_control,
	.reset				= syna_tcm_reset,
	.trigger_reason			= syna_trigger_reason,
	.mode_switch			= syna_mode_switch,
	.fw_check			= syna_fw_check,
	.reinit_device			= syna_tcm_reinit_device,
	.enable_fingerprint		= syna_tcm_enable_fingerprint,
	.screenon_fingerprint_info	= syna_tcm_fingerprint_info,
	.health_report			= syna_tcm_get_health_info,
	.set_touch_direction		= syna_set_touch_direction,
	.get_touch_direction		= syna_get_touch_direction,
	.freq_hop_trigger		= syna_freq_hop_trigger,
	.force_water_mode		= syna_force_water_mode,
	.enable_gesture_mask		= syna_tcm_enable_gesture_mask,
	.speed_up_resume_prepare	= syna_resume_prepare,
	.specific_resume_operate	= syna_specific_resume_operate,
	.smooth_lv_set			= syna_tcm_smooth_lv_set,
	.sensitive_lv_set		= syna_tcm_sensitive_lv_set,
	.get_touch_points_auto		= syna_get_touch_points_auto,
	.get_gesture_info_auto		= syna_get_gesture_info_auto,
	.screenon_fingerprint_info_auto	= syna_tcm_fingerprint_info_auto,
	.tp_refresh_switch		= syna_report_refresh_switch,
	.rate_white_list_ctrl		= syna_rate_white_list_ctrl,
	.set_gesture_state         	= syna_set_gesture_state,
	.get_touch_points_help		= syna_get_touch_points_help,
	.set_high_frame_rate            = syna_tcm_set_high_frame_rate,
	.send_temperature		= syna_tcm_send_temperature,
};

static void syna_async_work_lock(struct work_struct *work)
{
	struct syna_tcm_data *tcm_info = container_of(work, struct syna_tcm_data,
					 async_work);

	syna_tcm_async_work(tcm_info);
}

static void init_chip_dts(struct device *dev, void *chip_data)
{
	int rc;
	int ret = 0;
	struct device_node *np;
	struct device_node *chip_np;
	int i = 0;
	int temp_array[FPS_REPORT_NUM];
	struct syna_tcm_data *tcm_info = (struct syna_tcm_data *)chip_data;
	np = dev->of_node;

	tcm_info->snr_read_support = of_property_read_bool(np, "snr_read_support");

	chip_np = of_get_child_by_name(np, "S3908");

	if (!chip_np) {
		tcm_info->display_refresh_rate = 60;
		tcm_info->game_rate = 1;
		tcm_info->default_gesture_mask = 0xFFFF;
		tcm_info->gesture_mask = tcm_info->default_gesture_mask;
		/*default :1:120hz 2:180hz is for 19101 19191 20131*/
		tcm_info->fps_report_rate_num = 6;
		tcm_info->fps_report_rate_array[0] = 60;
		tcm_info->fps_report_rate_array[1] = 1;
		tcm_info->fps_report_rate_array[2] = 90;
		tcm_info->fps_report_rate_array[3] = 3;
		tcm_info->fps_report_rate_array[4] = 120;
		tcm_info->fps_report_rate_array[5] = 2;
		tcm_info->syna_tempepratue[0] = 5;
		tcm_info->syna_tempepratue[1] = 15;
		tcm_info->syna_low_temp_enable = 0;
		tcm_info->syna_low_temp_disable = 1;
		tcm_info->normal_config_version = 0;
		tcm_info->int_check_support = 0;
		return;
	}

	rc = of_property_read_u32(chip_np, "report_rate_default", &tcm_info->display_refresh_rate);
	if (rc < 0) {
		tcm_info->display_refresh_rate = 60;
	}
	TPD_BOOT_INFO("default rate %d\n", tcm_info->display_refresh_rate);

	rc = of_property_read_u32(chip_np, "report_rate_game_value", &ret);
	if (rc < 0) {
		ret = 1;
	}
	TPD_BOOT_INFO("default game value %d\n", ret);
	tcm_info->game_rate = ret;
	tcm_info->switch_game_rate_support = of_property_read_bool(chip_np, "switch_report_rate");
	rc = of_property_read_u32(chip_np, "default_gesture_mask", &ret);
	if (rc < 0) {
		ret = 0xFFFF;
	}
	TPD_BOOT_INFO("default gesture mask value %d\n", ret);
	tcm_info->default_gesture_mask = (uint16_t)(ret & 0xFFFF);
	tcm_info->gesture_mask = tcm_info->default_gesture_mask;
	rc = of_property_count_u32_elems(chip_np, "fps_report_rate");
	tcm_info->fps_report_rate_num = rc;

	if (tcm_info->fps_report_rate_num > 0 && tcm_info->fps_report_rate_num <= FPS_REPORT_NUM
		&& !(tcm_info->fps_report_rate_num % 2)) {
		rc = of_property_read_u32_array(chip_np, "fps_report_rate", temp_array, tcm_info->fps_report_rate_num);
		if (rc) {
			TPD_BOOT_INFO("fps_report_rate not specified %d\n", rc);
		} else {
			for (i = 0; i < tcm_info->fps_report_rate_num; i++) {
				tcm_info->fps_report_rate_array[i] = temp_array[i];
				TPD_BOOT_INFO("fps_report_rate is: %d\n", tcm_info->fps_report_rate_array[i]);
			}
		}
	} else {
	    /*default :1:120hz 2:180hz is for 19101 19191 20131*/
	    tcm_info->fps_report_rate_num = 6;
		tcm_info->fps_report_rate_array[0] = 60;
		tcm_info->fps_report_rate_array[1] = 1;
		tcm_info->fps_report_rate_array[2] = 90;
		tcm_info->fps_report_rate_array[3] = 3;
		tcm_info->fps_report_rate_array[4] = 120;
		tcm_info->fps_report_rate_array[5] = 2;
		TPD_BOOT_INFO("fps_report_rate is not dubole %d\n", tcm_info->fps_report_rate_num);
	}
	tcm_info->syna_tempepratue[0] = 5;
	tcm_info->syna_tempepratue[1] = 15;
	tcm_info->syna_low_temp_enable = 0;
	tcm_info->syna_low_temp_disable = 1;
	rc = of_property_read_u32(chip_np, "normal_config_version", &tcm_info->normal_config_version);
	if (rc < 0) {
		tcm_info->normal_config_version = 0;
		TPD_BOOT_INFO("normal_config_version %d\n", tcm_info->normal_config_version);
	}
	rc = of_property_read_u32(chip_np, "int_check_support", &tcm_info->int_check_support);
	if (rc < 0) {
		tcm_info->int_check_support = 0;
		TPD_BOOT_INFO("int_check_support %d\n", tcm_info->int_check_support);
	}
}

static int syna_tcm_probe(struct i2c_client *client)
{
	int retval = 0;
	struct syna_tcm_data *tcm_info = NULL;
	struct touchpanel_data *ts = NULL;
	struct device_hcd *device_hcd = NULL;

	TPD_BOOT_INFO("%s: enter\n", __func__);

	/*1. alloc mem for tcm_data*/
	tcm_info = kzalloc(sizeof(*tcm_info), GFP_KERNEL);

	if (!tcm_info) {
		TPD_INFO("no more memory\n");
		return -ENOMEM;
	}

	/*2. alloc mem for touchpanel_data */
	ts = common_touch_data_alloc();

	if (ts == NULL) {
		TPD_INFO("failed to alloc common data\n");
		goto ts_alloc_failed;
	}

	/*3. init member of ts*/
	ts->dev = &client->dev;
	ts->client = client;
	ts->irq = client->irq;
	ts->chip_data = tcm_info;
	i2c_set_clientdata(client, ts);
	/* add input_dev info */
	ts->id.bustype = BUS_I2C;
	ts->id.vendor = SYNAPTICS;
	ts->id.product = S3908;

	ts->ts_ops = &syna_tcm_ops;
	tcm_info->ts = ts;

	/*4. init member of tcm_info*/
	tcm_info->client = client;
	tcm_info->hw_res = &ts->hw_res;
	tcm_info->ubl_addr = 0x2c;
	tcm_info->rd_chunk_size = RD_CHUNK_SIZE;
	tcm_info->wr_chunk_size = WR_CHUNK_SIZE;
	tcm_info->read_length = MIN_READ_LENGTH;
	tcm_info->in_suspend = &ts->is_suspended;
	tcm_info->loading_fw = &ts->loading_fw;
	//tcm_info->display_refresh_rate = 90;
	tcm_info->game_mode = false;
	tcm_info->boot_flag = true;
	tcm_info->first_sync_flag = true;
	tcm_info->snr = ts->snr;
	tcm_info->int_check_support = 1;/*irq triger before dts*/
	tcm_info->palm_hold_report = 0;

	atomic_set(&tcm_info->command_status, CMD_IDLE);
	mutex_init(&tcm_info->reset_mutex);
	mutex_init(&tcm_info->rw_mutex);
	mutex_init(&tcm_info->command_mutex);
	mutex_init(&tcm_info->identify_mutex);
	init_completion(&tcm_info->response_complete);
	init_completion(&tcm_info->report_complete);

	init_completion(&tcm_info->resume_complete);
	tcm_info->async_workqueue = create_singlethread_workqueue("syna_async");

	if (!tcm_info->async_workqueue) {
		retval = -ENOMEM;
		goto err_async_workqueue;
	}

	INIT_WORK(&tcm_info->async_work, syna_async_work_lock);

	INIT_BUFFER(tcm_info->in, false);
	INIT_BUFFER(tcm_info->out, false);
	INIT_BUFFER(tcm_info->resp, true);
	INIT_BUFFER(tcm_info->temp, false);
	INIT_BUFFER(tcm_info->config, false);
	INIT_BUFFER(tcm_info->default_config, false);
	INIT_BUFFER(tcm_info->report.buffer, true);

	/*5. alloc mem for reading in buffer*/
	LOCK_BUFFER(tcm_info->in);
	retval = syna_tcm_alloc_mem(&tcm_info->in, MAX_READ_LENGTH);
	TPD_BOOT_INFO("%s read_length:%d\n", __func__, tcm_info->read_length);

	if (retval < 0) {
		TPD_INFO("Failed to allocate memory for tcm_info->in.buf\n");
		UNLOCK_BUFFER(tcm_info->in);
		goto err_malloc_inbuffer;
	}

	UNLOCK_BUFFER(tcm_info->in);

	/*6. create workqueue and init helper work*/
	tcm_info->helper_workqueue = create_singlethread_workqueue("syna_tcm_helper");
	if (!tcm_info->helper_workqueue) {
		retval = -ENOMEM;
		goto err_helper_workqueue;
	}

	INIT_WORK(&tcm_info->helper_work, syna_tcm_helper_work);

	/*7. alloc mem for touch_hcd and init it's member*/
	tcm_info->touch_hcd = (struct touch_hcd *)devm_kzalloc(ts->dev,
			      sizeof(struct touch_hcd), GFP_KERNEL);

	if (!tcm_info->touch_hcd) {
		retval = -ENOMEM;
		goto err_malloc_touchhcd;
	}

	INIT_BUFFER(tcm_info->touch_hcd->out, false);
	INIT_BUFFER(tcm_info->touch_hcd->resp, false);
	mutex_init(&tcm_info->touch_hcd->report_mutex);
	retval = of_property_read_u32(ts->dev->of_node, "touchpanel,max-num-support",
			     &tcm_info->touch_hcd->max_objects);
	if (retval < 0) {
		tcm_info->touch_hcd->max_objects = 10;
		TPD_BOOT_INFO("touchpanel,max-num-support %u\n", tcm_info->touch_hcd->max_objects);
	}
	tcm_info->touch_hcd->touch_data.object_data =
		(struct object_data *)devm_kzalloc(ts->dev,
				sizeof(struct object_data) * tcm_info->touch_hcd->max_objects, GFP_KERNEL);

	if (!tcm_info->touch_hcd->touch_data.object_data) {
		retval = -ENOMEM;
		goto err_malloc_object_data;
	}

	/*8. alloc mem for test_hcd and it's member*/
	tcm_info->test_hcd = (struct syna_tcm_test *)devm_kzalloc(ts->dev,
			     sizeof(struct syna_tcm_test), GFP_KERNEL);

	if (!tcm_info->test_hcd) {
		retval = -ENOMEM;
		goto err_malloc_test;
	}

	INIT_BUFFER(tcm_info->test_hcd->report, false);
	INIT_BUFFER(tcm_info->test_hcd->test_resp, false);
	INIT_BUFFER(tcm_info->test_hcd->test_out, false);

	/*9. register common part of touchpanel driver*/
	retval = register_common_touch_device(ts);

	if (retval < 0 && (retval != -EFTM)) {
		TPD_INFO("Failed to init device information\n");
		goto err_malloc_register;
	}

	tcm_info->monitor_data = &ts->monitor_data;
	tcm_info->exception_data = &ts->exception_data;

	tcm_info->tp_index = ts->tp_index;
	init_chip_dts(ts->dev, tcm_info);

	tcm_info->chip_resolution_info = &ts->resolution_info;

	/*12. create remote device file and init it's callback*/
	device_hcd = syna_remote_device_init(tcm_info);

	if (device_hcd) {
		device_hcd->irq = tcm_info->client->irq;
		device_hcd->read_message = syna_tcm_read_message;
		device_hcd->write_message = syna_tcm_write_message;
		device_hcd->reset = syna_tcm_reset;
		device_hcd->report_touch = syna_device_report_touch;
		device_hcd->tp_index = ts->tp_index;
	}
	tcm_info->loading_fw  = &ts->loading_fw;

	tcm_info->probe_done = 1;

	g_tcm_info[tcm_info->tp_index] = tcm_info;

	return 0;

err_malloc_register:
err_malloc_test:
err_malloc_object_data:
err_malloc_touchhcd:
	cancel_work_sync(&tcm_info->helper_work);
	flush_workqueue(tcm_info->helper_workqueue);
	destroy_workqueue(tcm_info->helper_workqueue);

err_helper_workqueue:
	RELEASE_BUFFER(tcm_info->in);

err_malloc_inbuffer:
	cancel_work_sync(&tcm_info->async_work);
	flush_workqueue(tcm_info->async_workqueue);
	destroy_workqueue(tcm_info->async_workqueue);

err_async_workqueue:
	i2c_set_clientdata(client, NULL);
	common_touch_data_free(ts);
	ts = NULL;

ts_alloc_failed:
	if (tcm_info) {
		kfree(tcm_info);
		tcm_info = NULL;
	}
	return retval;
}

static void syna_tcm_tp_shutdown(struct i2c_client *client)
{
	struct touchpanel_data *ts = NULL;

	if (!client) {
		TPD_INFO("%s client is NULL\n", __func__);
		return;
	}

	ts = i2c_get_clientdata(client);
	if (!ts) {
		TPD_INFO("%s ts is NULL\n", __func__);
		return;
	}

	TPD_INFO("%s is called\n", __func__);
	tp_shutdown(ts);
}

static void syna_tcm_remove(struct i2c_client *client)
{
	struct touchpanel_data *ts = NULL;
	struct syna_tcm_data *tcm_info = NULL;

	if (!client) {
		TPD_INFO("%s client is NULL\n", __func__);
		return;
	}

	ts = i2c_get_clientdata(client);
	if (!ts) {
		TPD_INFO("%s ts is NULL\n", __func__);
		return;
	}

	tcm_info = (struct syna_tcm_data *)ts->chip_data;

	RELEASE_BUFFER(tcm_info->report.buffer);
	RELEASE_BUFFER(tcm_info->config);
	RELEASE_BUFFER(tcm_info->temp);
	RELEASE_BUFFER(tcm_info->resp);
	RELEASE_BUFFER(tcm_info->out);
	RELEASE_BUFFER(tcm_info->in);

	if (ts) {
		unregister_common_touch_device(ts);
		common_touch_data_free(ts);
	}

	tp_kfree((void **)&tcm_info);
	i2c_set_clientdata(client, NULL);
}

static int syna_i2c_suspend(struct device *dev)
{
#ifndef CONFIG_ARCH_QTI_VM
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s: is called\n", __func__);
	tp_pm_suspend(ts);
#endif
	return 0;
}

static int syna_i2c_resume(struct device *dev)
{
#ifndef CONFIG_ARCH_QTI_VM
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	TPD_INFO("%s is called\n", __func__);
	tp_pm_resume(ts);
#endif
	return 0;
}


static const struct dev_pm_ops syna_pm_ops = {
	.suspend = syna_i2c_suspend,
	.resume = syna_i2c_resume,
};

static const struct i2c_device_id syna_tmc_id[] = {
	{ TPD_DEVICE, 0 },
	{ }
};

static struct of_device_id syna_match_table[] = {
	{ .compatible = TPD_DEVICE, },
	{ .compatible = "synaptics-s3908", },
	{ }
};

static struct i2c_driver syna_i2c_driver = {
	.probe      = syna_tcm_probe,
	.remove     = syna_tcm_remove,
	.id_table   = syna_tmc_id,
	.shutdown   = syna_tcm_tp_shutdown,
	.driver     = {
		.name   = TPD_DEVICE,
		.of_match_table =  syna_match_table,
		.pm = &syna_pm_ops,
	},
};


static int __init tp_driver_init_syna_tcm(void)
{
	TPD_INFO("%s is called\n", __func__);

	if (i2c_add_driver(&syna_i2c_driver) != 0) {
		TPD_INFO("%s: unable to add i2c driver.\n", __func__);
		goto OUT;
	}
OUT:
	return 0;
}

static void __exit tp_driver_exit_syna_tcm(void)
{
	TPD_INFO("%s is called\n", __func__);

	i2c_del_driver(&syna_i2c_driver);
	return;
}

module_init(tp_driver_init_syna_tcm);
module_exit(tp_driver_exit_syna_tcm);

MODULE_DESCRIPTION("Touchscreen Synaptics tcm oncell Driver");
MODULE_LICENSE("GPL");
