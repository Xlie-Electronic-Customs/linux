// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "../touchpanel_common.h"
#include "synaptics_common.h"
#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/version.h>
/*******Part0:LOG TAG Declear********************/
#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "synaptics_common"
#else
#define TPD_DEVICE "synaptics_common"
#endif
/*******Part1:Call Back Function implement*******/

static unsigned int extract_uint_le(const unsigned char *ptr)
{
	return (unsigned int)ptr[0] +
	       (unsigned int)ptr[1] * 0x100 +
	       (unsigned int)ptr[2] * 0x10000 +
	       (unsigned int)ptr[3] * 0x1000000;
}

/*************************************auto test Funtion**************************************/

/*************************************TCM Firmware Parse Funtion**************************************/
int synaptics_parse_header_v2(struct image_info *image_info,
			      const unsigned char *fw_image)
{
	struct image_header_v2 *header;
	unsigned int magic_value;
	unsigned int number_of_areas;
	unsigned int i = 0;
	unsigned int addr;
	unsigned int length;
	unsigned int checksum;
	unsigned int flash_addr;
	const unsigned char *content;
	struct area_descriptor *descriptor;
	int offset = sizeof(struct image_header_v2);

	header = (struct image_header_v2 *)fw_image;
	magic_value = le4_to_uint(header->magic_value);

	if (magic_value != IMAGE_FILE_MAGIC_VALUE) {
		pr_err("invalid magic number %d\n", magic_value);
		return -EINVAL;
	}

	number_of_areas = le4_to_uint(header->num_of_areas);

	for (i = 0; i < number_of_areas; i++) {
		addr = le4_to_uint(fw_image + offset);
		descriptor = (struct area_descriptor *)(fw_image + addr);
		offset += 4;

		magic_value =  le4_to_uint(descriptor->magic_value);

		if (magic_value != FLASH_AREA_MAGIC_VALUE) {
			continue;
		}

		length = le4_to_uint(descriptor->length);
		content = (unsigned char *)descriptor + sizeof(*descriptor);
		flash_addr = le4_to_uint(descriptor->flash_addr_words) * 2;
		checksum = le4_to_uint(descriptor->checksum);

		if (0 == strncmp((char *)descriptor->id_string,
				 BOOT_CONFIG_ID,
				 strlen(BOOT_CONFIG_ID))) {
			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				pr_err("Boot config checksum error\n");
				return -EINVAL;
			}

			image_info->boot_config.size = length;
			image_info->boot_config.data = content;
			image_info->boot_config.flash_addr = flash_addr;
			pr_info("Boot config size = %d, address = 0x%08x\n", length, flash_addr);

		} else if (0 == strncmp((char *)descriptor->id_string,
					APP_CODE_ID,
					strlen(APP_CODE_ID))) {
			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				pr_err("Application firmware checksum error\n");
				return -EINVAL;
			}

			image_info->app_firmware.size = length;
			image_info->app_firmware.data = content;
			image_info->app_firmware.flash_addr = flash_addr;
			pr_info("Application firmware size = %d address = 0x%08x\n", length,
				flash_addr);

		} else if (0 == strncmp((char *)descriptor->id_string,
					APP_CONFIG_ID,
					strlen(APP_CONFIG_ID))) {
			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				pr_err("Application config checksum error\n");
				return -EINVAL;
			}

			image_info->app_config.size = length;
			image_info->app_config.data = content;
			image_info->app_config.flash_addr = flash_addr;
			pr_info("Application config size = %d address = 0x%08x\n", length, flash_addr);

		} else if (0 == strncmp((char *)descriptor->id_string,
					DISP_CONFIG_ID,
					strlen(DISP_CONFIG_ID))) {
			if (checksum != (crc32(~0, content, length) ^ ~0)) {
				pr_err("Display config checksum error\n");
				return -EINVAL;
			}

			image_info->disp_config.size = length;
			image_info->disp_config.data = content;
			image_info->disp_config.flash_addr = flash_addr;
			pr_info("Display config size = %d address = 0x%08x\n", length, flash_addr);
		}
	}

	return 0;
}
EXPORT_SYMBOL(synaptics_parse_header_v2);
/**********************************RMI Firmware Parse Funtion*****************************************/
void synaptics_parse_header(struct image_header_data *header,
			    const unsigned char *fw_image)
{
	struct image_header *data = (struct image_header *)fw_image;

	header->checksum = extract_uint_le(data->checksum);
	TPD_DEBUG(" checksume is %x", header->checksum);

	header->bootloader_version = data->bootloader_version;
	TPD_DEBUG(" bootloader_version is %d\n", header->bootloader_version);

	header->firmware_size = extract_uint_le(data->firmware_size);
	TPD_DEBUG(" firmware_size is %x\n", header->firmware_size);

	header->config_size = extract_uint_le(data->config_size);
	TPD_DEBUG(" header->config_size is %x\n", header->config_size);

	/* only available in s4322 , reserved in other, begin*/
	header->bootloader_offset = extract_uint_le(data->bootloader_addr);
	header->bootloader_size = extract_uint_le(data->bootloader_size);
	TPD_DEBUG(" header->bootloader_offset is %x\n", header->bootloader_offset);
	TPD_DEBUG(" header->bootloader_size is %x\n", header->bootloader_size);

	header->disp_config_offset = extract_uint_le(data->dsp_cfg_addr);
	header->disp_config_size = extract_uint_le(data->dsp_cfg_size);
	TPD_DEBUG(" header->disp_config_offset is %x\n", header->disp_config_offset);
	TPD_DEBUG(" header->disp_config_size is %x\n", header->disp_config_size);
	/* only available in s4322 , reserved in other ,  end*/

	memcpy(header->product_id, data->product_id, sizeof(data->product_id));
	header->product_id[sizeof(data->product_id)] = 0;

	memcpy(header->product_info, data->product_info, sizeof(data->product_info));

	header->contains_firmware_id = data->options_firmware_id;
	TPD_DEBUG(" header->contains_firmware_id is %x\n",
		  header->contains_firmware_id);

	if (header->contains_firmware_id) {
		header->firmware_id = extract_uint_le(data->firmware_id);
	}

	return;
}

static int tp_RT251_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts) {
		return 0;
	}

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops) {
		return 0;
	}

	if (!debug_info_ops->reserve1) {
		seq_printf(s, "Not support RT251 proc node\n");
		return 0;
	}

	disable_irq_nosync(ts->client->irq);
	mutex_lock(&ts->mutex);
	debug_info_ops->reserve1(s, ts->chip_data);
	mutex_unlock(&ts->mutex);
	enable_irq(ts->client->irq);

	return 0;
}

static int RT251_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_RT251_read_func, pde_data(inode));
}

DECLARE_PROC_OPS(tp_RT251_proc_fops, RT251_open, seq_read, NULL, single_release);

static int tp_RT76_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts) {
		return 0;
	}

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops) {
		return 0;
	}

	if (!debug_info_ops->reserve2) {
		seq_printf(s, "Not support RT76 proc node\n");
		return 0;
	}

	disable_irq_nosync(ts->client->irq);
	mutex_lock(&ts->mutex);
	debug_info_ops->reserve2(s, ts->chip_data);
	mutex_unlock(&ts->mutex);
	enable_irq(ts->client->irq);

	return 0;
}

static int RT76_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_RT76_read_func, pde_data(inode));
}

DECLARE_PROC_OPS(tp_RT76_proc_fops, RT76_open, seq_read, NULL, single_release);

static int tp_DRT_read_func(struct seq_file *s, void *v)
{
	struct touchpanel_data *ts = s->private;
	struct debug_info_proc_operations *debug_info_ops;

	if (!ts) {
		return 0;
	}

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops) {
		return 0;
	}

	if (!debug_info_ops->reserve4) {
		seq_printf(s, "Not support RT76 proc node\n");
		return 0;
	}

	if (ts->is_suspended && (ts->gesture_enable != 1)) {
		seq_printf(s, "In suspend state, and gesture not enable\n");
		return 0;
	}

	if (ts->int_mode == BANNABLE) {
		disable_irq_nosync(ts->irq);
	}

	mutex_lock(&ts->mutex);
	debug_info_ops->reserve4(s, ts->chip_data);
	mutex_unlock(&ts->mutex);

	if (ts->int_mode == BANNABLE) {
		enable_irq(ts->client->irq);
	}

	return 0;
}

static int DRT_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_DRT_read_func, pde_data(inode));
}

DECLARE_PROC_OPS(tp_DRT_proc_fops, DRT_open, seq_read, NULL, single_release);

static ssize_t proc_touchfilter_control_read(struct file *file,
		char __user *user_buf, size_t count, loff_t *ppos)
{
	ssize_t ret = 0;
	char page[PAGESIZE] = {0};
	struct touchpanel_data *ts = pde_data(file_inode(file));
	struct synaptics_proc_operations *syn_ops;

	if (!ts) {
		return 0;
	}

	syn_ops = (struct synaptics_proc_operations *)ts->private_data;

	if (!syn_ops->get_touchfilter_state) {
		return 0;
	}

	snprintf(page, PAGESIZE - 1, "%hhu.\n",
		 syn_ops->get_touchfilter_state(ts->chip_data));
	ret = simple_read_from_buffer(user_buf, count, ppos, page, strlen(page));

	return ret;
}

static ssize_t proc_touchfilter_control_write(struct file *file,
		const char __user *buffer, size_t count, loff_t *ppos)
{
	char buf[8] = {0};
	int temp = 0;
	struct touchpanel_data *ts = pde_data(file_inode(file));
	struct synaptics_proc_operations *syn_ops;

	if (!ts) {
		return count;
	}

	syn_ops = (struct synaptics_proc_operations *)ts->private_data;

	if (!syn_ops->set_touchfilter_state) {
		return count;
	}

	if (count > 2) {
		return count;
	}

	if (copy_from_user(buf, buffer, count)) {
		TPD_DEBUG("%s: read proc input error.\n", __func__);
		return count;
	}

	sscanf(buf, "%d", &temp);
	mutex_lock(&ts->mutex);
	TPD_INFO("%s: value = %d\n", __func__, temp);
	syn_ops->set_touchfilter_state(ts->chip_data, temp);
	mutex_unlock(&ts->mutex);

	return count;
}

DECLARE_PROC_OPS(touch_filter_proc_fops, simple_open, proc_touchfilter_control_read, proc_touchfilter_control_write, NULL);

int synaptics_create_proc(struct touchpanel_data *ts,
			  struct synaptics_proc_operations *syna_ops)
{
	int ret = 0;

	/* touchpanel_auto_test interface*/
	struct proc_dir_entry *prEntry_tmp = NULL;
	ts->private_data = syna_ops;

	/* show RT251 interface*/
	prEntry_tmp = proc_create_data("RT251", 0666, ts->prEntry_debug_tp,
				       &tp_RT251_proc_fops, ts);

	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	/* show RT76 interface*/
	prEntry_tmp = proc_create_data("RT76", 0666, ts->prEntry_debug_tp,
				       &tp_RT76_proc_fops, ts);

	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	prEntry_tmp = proc_create_data("DRT", 0666, ts->prEntry_debug_tp,
				       &tp_DRT_proc_fops, ts);

	if (prEntry_tmp == NULL) {
		ret = -ENOMEM;
		TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
	}

	if (ts->face_detect_support) {
		prEntry_tmp = proc_create_data("touch_filter", 0666, ts->prEntry_tp,
					       &touch_filter_proc_fops, ts);

		if (prEntry_tmp == NULL) {
			ret = -ENOMEM;
			TPD_INFO("%s: Couldn't create proc entry, %d\n", __func__, __LINE__);
		}
	}

	return ret;
}
EXPORT_SYMBOL(synaptics_create_proc);

int synaptics_remove_proc(struct touchpanel_data *ts,
			  struct synaptics_proc_operations *syna_ops)
{
	if (!ts) {
		return -EINVAL;
	}

	remove_proc_entry("RT251", ts->prEntry_debug_tp);
	remove_proc_entry("RT76", ts->prEntry_debug_tp);
	remove_proc_entry("DRT", ts->prEntry_debug_tp);

	if (ts->face_detect_support) {
		remove_proc_entry("touch_filter", ts->prEntry_tp);
	}

	return 0;
}
EXPORT_SYMBOL(synaptics_remove_proc);

MODULE_DESCRIPTION("Touchscreen Synaptics Common Interface");
MODULE_LICENSE("GPL");
