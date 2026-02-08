// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include "synaptics_common.h"
#include "synaptics_tcm_oncell.h"
#include <linux/crc32.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/spi/spi.h>
#include <linux/input/mt.h>
#include <linux/pinctrl/consumer.h>
#include <linux/vmalloc.h>
/*******Part0:LOG TAG Declear********************/
#ifdef TPD_DEVICE
#undef TPD_DEVICE
#define TPD_DEVICE "synaptics_common"
#else
#define TPD_DEVICE "synaptics_common"
#endif

#define PLATFORM_DRIVER_NAME "synaptics_tcm_oncell"
#define CONCURRENT true

#define DEVICE_IOC_MAGIC 's'
#define DEVICE_IOC_RESET _IO(DEVICE_IOC_MAGIC, 0) /* 0x00007300 */
#define DEVICE_IOC_IRQ _IOW(DEVICE_IOC_MAGIC, 1, int) /* 0x40047301 */
#define DEVICE_IOC_RAW _IOW(DEVICE_IOC_MAGIC, 2, int) /* 0x40047302 */
#define DEVICE_IOC_CONCURRENT _IOW(DEVICE_IOC_MAGIC, 3, int) /* 0x40047303 */

#define GET_TOUCH(cur_tp_index) \
((match_panel_index[cur_tp_index].c_panel_index == (cur_tp_index)) \
	? match_panel_index[cur_tp_index].match_touch \
	: PANEL_EVENT_NOTIFIER_CLIENT_PRIMARY_TOUCH)

int cur_tp_index = 0;
unsigned int tp_debug = 1;

struct touchpanel_data *g_tp[TP_SUPPORT_MAX] = {NULL};
static DEFINE_MUTEX(tp_core_lock);

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

static struct device_hcd *g_device_hcd[TP_SUPPORT_MAX] = {NULL};

static void device_capture_touch_report(struct device_hcd *device_hcd,
					unsigned int count)
{
	int retval;
	unsigned char id;
	unsigned int idx;
	unsigned int size;
	unsigned char *data;
	struct syna_tcm_data *tcm_info = device_hcd->tcm_info;
	static bool report;
	static unsigned int offset;
	static unsigned int remaining_size;

	if (count < 2) {
		return;
	}

	data = &device_hcd->resp.buf[0];

	if (data[0] != MESSAGE_MARKER) {
		return;
	}

	id = data[1];
	size = 0;

	LOCK_BUFFER(device_hcd->report);

	switch (id) {
	case REPORT_TOUCH:
		if (count >= 4) {
			remaining_size = le2_to_uint(&data[2]);

		} else {
			report = false;
			goto exit;
		}

		retval = syna_tcm_alloc_mem(&device_hcd->report, remaining_size);

		if (retval < 0) {
			pr_err("Failed to allocate memory for device_hcd->report.buf\n");
			report = false;
			goto exit;
		}

		idx = 4;
		size = count - idx;
		offset = 0;
		report = true;
		break;

	case STATUS_CONTINUED_READ:
		if (report == false) {
			goto exit;
		}

		if (count >= 2) {
			idx = 2;
			size = count - idx;
		}

		break;

	default:
		goto exit;
	}

	if (size) {
		size = MIN(size, remaining_size);
		retval = tp_memcpy(&device_hcd->report.buf[offset],
				   device_hcd->report.buf_size - offset,
				   &data[idx],
				   count - idx,
				   size);

		if (retval < 0) {
			pr_err("Failed to copy touch report data\n");
			report = false;
			goto exit;

		} else {
			offset += size;
			remaining_size -= size;
			device_hcd->report.data_length += size;
		}
	}

	if (remaining_size) {
		goto exit;
	}

	LOCK_BUFFER(tcm_info->report.buffer);

	tcm_info->report.buffer.buf = device_hcd->report.buf;
	tcm_info->report.buffer.buf_size = device_hcd->report.buf_size;
	tcm_info->report.buffer.data_length = device_hcd->report.data_length;

	if (device_hcd->report_touch) {
		device_hcd->report_touch(tcm_info);
	}

	UNLOCK_BUFFER(tcm_info->report.buffer);

	report = false;

exit:
	UNLOCK_BUFFER(device_hcd->report);

	return;
}

static int device_capture_touch_report_config(struct device_hcd *device_hcd,
		unsigned int count)
{
	int retval;
	unsigned int size;
	unsigned char *data;
	struct syna_tcm_data *tcm_info = device_hcd->tcm_info;

	if (device_hcd->raw_mode) {
		if (count < 3) {
			pr_err("Invalid write data\n");
			return -EINVAL;
		}

		size = le2_to_uint(&device_hcd->out.buf[1]);

		if (count - 3 < size) {
			pr_err("Incomplete write data\n");
			return -EINVAL;
		}

		if (!size) {
			return 0;
		}

		data = &device_hcd->out.buf[3];

	} else {
		size = count - 1;

		if (!size) {
			return 0;
		}

		data = &device_hcd->out.buf[1];
	}

	LOCK_BUFFER(tcm_info->config);

	retval = syna_tcm_alloc_mem(&tcm_info->config, size);

	if (retval < 0) {
		pr_err("Failed to allocate memory for tcm_info->config.buf\n");
		UNLOCK_BUFFER(tcm_info->config);
		return retval;
	}

	retval = tp_memcpy(tcm_info->config.buf,
			   tcm_info->config.buf_size,
			   data,
			   size,
			   size);

	if (retval < 0) {
		pr_err("Failed to copy touch report config data\n");
		UNLOCK_BUFFER(tcm_info->config);
		return retval;
	}

	tcm_info->config.data_length = size;

	UNLOCK_BUFFER(tcm_info->config);

	return 0;
}

static long device_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	struct device_hcd *device_hcd  = NULL;
	struct syna_tcm_data *tcm_info = NULL;

	device_hcd = filp->private_data;
	tcm_info = device_hcd->tcm_info;

	pr_info("%s: 0x%x\n", __func__, cmd);

	mutex_lock(&device_hcd->extif_mutex);

	switch (cmd) {
	case DEVICE_IOC_RESET:
		retval = device_hcd->reset(tcm_info);
		break;

	case DEVICE_IOC_IRQ:
		if (arg == 0) {
			if (device_hcd->flag == 1) {
				disable_irq(device_hcd->irq);
				device_hcd->flag = 0;
			}

		} else if (arg == 1) {
			if (device_hcd->flag == 0) {
				enable_irq(device_hcd->irq);
				device_hcd->flag = 1;
			}
		}

		break;

	case DEVICE_IOC_RAW:
		if (arg == 0) {
			device_hcd->raw_mode = false;

		} else if (arg == 1) {
			device_hcd->raw_mode = true;
		}

		break;

	case DEVICE_IOC_CONCURRENT:
		if (arg == 0) {
			device_hcd->concurrent = false;

		} else if (arg == 1) {
			device_hcd->concurrent = true;
		}

		break;

	default:
		retval = -ENOTTY;
		break;
	}

	mutex_unlock(&device_hcd->extif_mutex);

	return retval;
}

static loff_t device_llseek(struct file *filp, loff_t off, int whence)
{
	return -EINVAL;
}

static ssize_t device_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos)
{
	int retval;
	struct device_hcd *device_hcd  = NULL;
	struct syna_tcm_data *tcm_info = NULL;

	if (count == 0) {
		return 0;
	}

	device_hcd = filp->private_data;
	tcm_info = device_hcd->tcm_info;

	mutex_lock(&device_hcd->extif_mutex);

	LOCK_BUFFER(device_hcd->resp);

	if (device_hcd->raw_mode) {
		retval = syna_tcm_alloc_mem(&device_hcd->resp, count);

		if (retval < 0) {
			pr_err("Failed to allocate memory for device_hcd->resp.buf\n");
			UNLOCK_BUFFER(device_hcd->resp);
			goto exit;
		}

		retval = device_hcd->read_message(tcm_info,
						  device_hcd->resp.buf,
						  count);

		if (retval < 0) {
			pr_err("Failed to read message\n");
			UNLOCK_BUFFER(device_hcd->resp);
			goto exit;
		}

	} else {
		if (count != device_hcd->resp.data_length) {
			pr_err("Invalid length information\n");
			UNLOCK_BUFFER(device_hcd->resp);
			retval = -EINVAL;
			goto exit;
		}
	}

	if (copy_to_user(buf, device_hcd->resp.buf, count)) {
		pr_err("Failed to copy data to user space\n");
		UNLOCK_BUFFER(device_hcd->resp);
		retval = -EINVAL;
		goto exit;
	}

	if (!device_hcd->concurrent) {
		goto skip_concurrent;
	}

	if (device_hcd->report_touch == NULL) {
		pr_err("Unable to report touch\n");
		device_hcd->concurrent = false;
	}

	if (device_hcd->raw_mode) {
		device_capture_touch_report(device_hcd, count);
	}

skip_concurrent:
	UNLOCK_BUFFER(device_hcd->resp);

	retval = count;

exit:
	mutex_unlock(&device_hcd->extif_mutex);

	return retval;
}

static ssize_t device_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos)
{
	int retval;
	struct device_hcd *device_hcd  = NULL;
	struct syna_tcm_data *tcm_info = NULL;

	if (count == 0) {
		return 0;
	}

	device_hcd = filp->private_data;
	tcm_info = device_hcd->tcm_info;

	mutex_lock(&device_hcd->extif_mutex);

	LOCK_BUFFER(device_hcd->out);

	retval = syna_tcm_alloc_mem(&device_hcd->out, count == 1 ? count + 1 : count);

	if (retval < 0) {
		pr_err("Failed to allocate memory for device_hcd->out.buf\n");
		UNLOCK_BUFFER(device_hcd->out);
		goto exit;
	}

	if (copy_from_user(device_hcd->out.buf, buf, count)) {
		pr_err("Failed to copy data from user space\n");
		UNLOCK_BUFFER(device_hcd->out);
		retval = -EINVAL;
		goto exit;
	}

	LOCK_BUFFER(device_hcd->resp);

	pr_info("%s: cmd 0x%x\n", __func__, device_hcd->out.buf[0]);

	if (device_hcd->raw_mode) {
		retval = device_hcd->write_message(tcm_info,
						   device_hcd->out.buf[0],
						   &device_hcd->out.buf[1],
						   count == 1 ? count : count - 1,
						   NULL,
						   NULL,
						   NULL,
						   0);

	} else {
		mutex_lock(&tcm_info->reset_mutex);
		retval = device_hcd->write_message(tcm_info,
						   device_hcd->out.buf[0],
						   &device_hcd->out.buf[1],
						   count == 1 ? count : count - 1,
						   &device_hcd->resp.buf,
						   &device_hcd->resp.buf_size,
						   &device_hcd->resp.data_length,
						   0);
		mutex_unlock(&tcm_info->reset_mutex);
	}

	if (device_hcd->out.buf[0] == CMD_ERASE_FLASH) {
		msleep(500);
	}

	if (retval < 0) {
		pr_err("Failed to write command 0x%02x\n",
		       device_hcd->out.buf[0]);
		UNLOCK_BUFFER(device_hcd->resp);
		UNLOCK_BUFFER(device_hcd->out);
		goto exit;
	}

	if (count && device_hcd->out.buf[0] == CMD_SET_TOUCH_REPORT_CONFIG) {
		retval = device_capture_touch_report_config(device_hcd, count);

		if (retval < 0) {
			pr_err("Failed to capture touch report config\n");
		}
	}

	UNLOCK_BUFFER(device_hcd->out);

	if (device_hcd->raw_mode) {
		retval = count;

	} else {
		retval = device_hcd->resp.data_length;
	}

	UNLOCK_BUFFER(device_hcd->resp);

exit:
	mutex_unlock(&device_hcd->extif_mutex);

	return retval;
}

static int device_open(struct inode *inode, struct file *filp)
{
	int retval;
	struct device_hcd *device_hcd =
		container_of(inode->i_cdev, struct device_hcd, char_dev);

	filp->private_data = device_hcd;

	mutex_lock(&device_hcd->extif_mutex);

	if (device_hcd->ref_count < 1) {
		device_hcd->ref_count++;
		retval = 0;

	} else {
		retval = -EACCES;
	}

	device_hcd->flag = 1;

	mutex_unlock(&device_hcd->extif_mutex);

	return retval;
}

static int device_release(struct inode *inode, struct file *filp)
{
	struct device_hcd *device_hcd =
		container_of(inode->i_cdev, struct device_hcd, char_dev);

	mutex_lock(&device_hcd->extif_mutex);

	if (device_hcd->ref_count) {
		device_hcd->ref_count--;
	}

	mutex_unlock(&device_hcd->extif_mutex);

	return 0;
}

static int device_create_class(struct device_hcd *device_hcd)
{
	if (device_hcd->class != NULL) {
		return 0;
	}

	device_hcd->class = class_create(PLATFORM_DRIVER_NAME);

	if (IS_ERR(device_hcd->class)) {
		pr_err("Failed to create class\n");
		return -ENODEV;
	}

	return 0;
}

static const struct file_operations device_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = device_ioctl,
	.llseek = device_llseek,
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release,
};

static int device_init(struct syna_tcm_data *tcm_info)
{
	int retval;
	dev_t dev_num;
	struct device_hcd *device_hcd = NULL;

	device_hcd = kzalloc(sizeof(*device_hcd), GFP_KERNEL);
	if (!device_hcd) {
		pr_err("Failed to allocate memory for device_hcd\n");
		return -ENOMEM;
	}
        memset(device_hcd,0,sizeof(*device_hcd));
	device_hcd->rmidev_major_num = 0;

	mutex_init(&device_hcd->extif_mutex);
	device_hcd->tp_index = tcm_info->tp_index;

	device_hcd->tcm_info = tcm_info;
	device_hcd->concurrent = CONCURRENT;

	INIT_BUFFER(device_hcd->out, false);
	INIT_BUFFER(device_hcd->resp, false);
	INIT_BUFFER(device_hcd->report, false);

	if (device_hcd->rmidev_major_num) {
		dev_num = MKDEV(device_hcd->rmidev_major_num, device_hcd->tp_index);
		retval = register_chrdev_region(dev_num, 1,
						PLATFORM_DRIVER_NAME);

		if (retval < 0) {
			pr_err("Failed to register char device\n");
			goto err_register_chrdev_region;
		}

	} else {
		retval = alloc_chrdev_region(&dev_num, device_hcd->tp_index, 1,
					     PLATFORM_DRIVER_NAME);

		if (retval < 0) {
			pr_err("Failed to allocate char device\n");
			goto err_alloc_chrdev_region;
		}

		device_hcd->rmidev_major_num = MAJOR(dev_num);
	}

	device_hcd->dev_num = dev_num;

	cdev_init(&device_hcd->char_dev, &device_fops);

	retval = cdev_add(&device_hcd->char_dev, dev_num, 1);

	if (retval < 0) {
		pr_err("Failed to add char device\n");
		goto err_add_chardev;
	}

	retval = device_create_class(device_hcd);

	if (retval < 0) {
		pr_err("Failed to create class\n");
		goto err_create_class;
	}

	device_hcd->device = device_create(device_hcd->class, NULL,
					   device_hcd->dev_num, NULL, "tcm%d",
					   MINOR(device_hcd->dev_num));

	if (IS_ERR(device_hcd->device)) {
		pr_err("Failed to create device\n");
		retval = -ENODEV;
		goto err_create_device;
	}

	g_device_hcd[device_hcd->tp_index] = device_hcd;
	return 0;

err_create_device:
	class_destroy(device_hcd->class);

err_create_class:
	cdev_del(&device_hcd->char_dev);

err_add_chardev:
	unregister_chrdev_region(dev_num, 1);

err_alloc_chrdev_region:
err_register_chrdev_region:
	RELEASE_BUFFER(device_hcd->report);
	RELEASE_BUFFER(device_hcd->resp);
	RELEASE_BUFFER(device_hcd->out);

	kfree(g_device_hcd[device_hcd->tp_index]);
	g_device_hcd[device_hcd->tp_index] = NULL;

	return retval;
}

struct device_hcd *syna_remote_device_init(struct syna_tcm_data *tcm_info)
{
	device_init(tcm_info);

	return g_device_hcd[tcm_info->tp_index];
}

int syna_remote_device_destory(struct syna_tcm_data *tcm_info)
{
	struct device_hcd *device_hcd = NULL;
	device_hcd = g_device_hcd[tcm_info->tp_index];

	if (!device_hcd) {
		return 0;
	}

	device_destroy(device_hcd->class, device_hcd->dev_num);

	class_destroy(device_hcd->class);

	cdev_del(&device_hcd->char_dev);

	unregister_chrdev_region(device_hcd->dev_num, 1);

	RELEASE_BUFFER(device_hcd->report);
	RELEASE_BUFFER(device_hcd->resp);
	RELEASE_BUFFER(device_hcd->out);

	kfree(device_hcd);
	g_device_hcd[tcm_info->tp_index] = NULL;

	return 0;
}

/**
 * init_parse_dts - parse dts, get resource defined in Dts
 * @dev: i2c_client->dev using to get device tree
 * @ts: touchpanel_data, using for common driver
 *
 * If there is any Resource needed by chip_data, we can add a call-back func in this function
 * Do not care the result : Returning void type
 */
static int init_parse_dts(struct device *dev, struct touchpanel_data *ts)
{
	int rc, ret = 0;
	struct device_node *np;
	int temp_array[8];
	int tx_rx_num[2];
	int val = 0;
	int i = 0;
	np = dev->of_node;


	/* tp_index*/
	rc = of_property_read_u32(np, "touchpanel,tp-index", &ts->tp_index);

	if (rc) {
		TPD_BOOT_INFO("ts->tp_index not specified\n");
		ts->tp_index = 0;

	} else {
		if (ts->tp_index >= TP_SUPPORT_MAX) {
			TPD_INFO("ts->tp_index is big than %d\n", TP_SUPPORT_MAX);
			ts->tp_index = 0;
		}
	}

	cur_tp_index = ts->tp_index;
	TPD_BOOT_INFO("ts->tp_index is %d\n",  cur_tp_index);

	ts->register_is_16bit       = of_property_read_bool(np, "register-is-16bit");
	ts->esd_handle_support      = of_property_read_bool(np, "esd_handle_support");

	ts->black_gesture_indep_support = of_property_read_bool(np,
								"black_gesture_indep_support");
	ts->game_switch_support     = of_property_read_bool(np, "game_switch_support");
	ts->is_noflash_ic           = of_property_read_bool(np, "noflash_support");
	ts->face_detect_support     = of_property_read_bool(np, "face_detect_support");
	ts->sec_long_low_trigger     = of_property_read_bool(np,
				       "sec_long_low_trigger");
	ts->fingerprint_underscreen_support = of_property_read_bool(np,
					      "fingerprint_underscreen_support");
	ts->fingerprint_not_report_in_suspend = of_property_read_bool(np,
					      "fingerprint_not_report_in_suspend");
	ts->suspend_gesture_cfg   = of_property_read_bool(np, "suspend_gesture_cfg");
	ts->freq_hop_simulate_support = of_property_read_bool(np,
					"freq_hop_simulate_support");
	ts->irq_trigger_hdl_support = of_property_read_bool(np,
				      "irq_trigger_hdl_support");
	ts->noise_modetest_support = of_property_read_bool(np,
				     "noise_modetest_support");
	ts->grip_no_driver_support = of_property_read_bool(np, "grip_no_driver_support");
	ts->report_rate_white_list_support = of_property_read_bool(np,
					     "report_rate_white_list_support");

	ts->lcd_tp_refresh_support = of_property_read_bool(np, "lcd_tp_refresh_support");
	ts->enable_point_auto_change = of_property_read_bool(np, "enable_point_auto_change");
	ts->snr_read_support = of_property_read_bool(np, "snr_read_support");
	ts->major_rate_limit_support = of_property_read_bool(np, "major_rate_limit_support");
	ts->palm_to_sleep_support = of_property_read_bool(np, "palm_to_sleep_support");
	ts->tp_data_record_support = of_property_read_bool(np, "tp_data_record_support");
	ts->skip_reinit_device_support = of_property_read_bool(np, "skip_reinit_device_support");
	ts->suspend_work_support = of_property_read_bool(np, "suspend_work_support");
	ts->fp_disable_after_resume = of_property_read_bool(np, "fp_disable_after_resume");
	ts->edge_pull_out_support = of_property_read_bool(np, "edge_pull_out_support");
	ts->diaphragm_touch_support = of_property_read_bool(np, "diaphragm_touch_support");

	ts->trusted_touch_support = false;

	ts->sportify_aod_gesture_support = of_property_read_bool(np,
						 "sportify_aod_gesture_support");
	if (!ts->sportify_aod_gesture_support) {
		TP_INFO(ts->tp_index, "not support sportify_aod_gesture\n");
	}

	ts->regulator_count_not_support = of_property_read_bool(np, "regulator_count_not_support");

	ts->force_bus_ready_support = of_property_read_bool(np, "force_bus_ready_support");

	rc = of_property_read_u32(np, "vcc_1v8_volt", &ts->hw_res.vddi_volt);
	if (rc < 0) {
		ts->hw_res.vddi_volt = 0;
		TP_BOOT_INFO(ts->tp_index, "vdd_1v8_volt not defined\n");
	}

	ts->pen_support = of_property_read_bool(np, "pen_support");
	ts->pen_support_opp = of_property_read_bool(np, "pen_support_opp");
	ts->bus_ready_check_support = of_property_read_bool(np, "bus_ready_check_support");
	TP_INFO(ts->tp_index, "bus_ready_check_support is %d\n", ts->bus_ready_check_support);

	ts->tp_lcd_suspend_in_lp_support = of_property_read_bool(np, "tp_lcd_suspend_in_lp_support");
	TP_INFO(ts->tp_index, "tp_lcd_suspend_in_lp_support is %d\n", ts->tp_lcd_suspend_in_lp_support);

	rc = of_property_read_u32(np, "vdd_2v8_volt", &ts->hw_res.vdd_volt);
	if (rc < 0) {
		ts->hw_res.vdd_volt = 0;
		TP_BOOT_INFO(ts->tp_index, "vdd_2v8_volt not defined\n");
	}

	/* irq gpio*/
	ts->hw_res.irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);

	if (gpio_is_valid(ts->hw_res.irq_gpio)) {
		TP_BOOT_INFO(ts->tp_index, "irq-gpio valid!X\n");
		rc = gpio_request(ts->hw_res.irq_gpio, "tp_irq_gpio");

		if (rc) {
			TP_INFO(ts->tp_index, "unable to request gpio [%d]\n", ts->hw_res.irq_gpio);
		}

	} else {
		TP_INFO(ts->tp_index, "irq-gpio not specified in dts\n");
	}

	/* reset gpio*/
	ts->hw_res.reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);
	if (gpio_is_valid(ts->hw_res.reset_gpio)) {
		rc = gpio_request(ts->hw_res.reset_gpio, "reset-gpio");

		if (rc) {
			TP_INFO(ts->tp_index, "unable to request gpio [%d]\n", ts->hw_res.reset_gpio);
		}

	} else {
		TP_INFO(ts->tp_index, "ts->reset-gpio not specified\n");
	}

	TP_BOOT_INFO(ts->tp_index, "%s : irq_gpio = %d, irq_flags = 0x%x, reset_gpio = %d\n",
		 __func__, ts->hw_res.irq_gpio, ts->irq_flags, ts->hw_res.reset_gpio);

	/* spi cs gpio */
	ts->hw_res.cs_gpio = of_get_named_gpio(np, "cs-gpio", 0);
	if (gpio_is_valid(ts->hw_res.cs_gpio)) {
		rc = gpio_request(ts->hw_res.cs_gpio, "cs-gpio");
		if (rc)
			TP_INFO(ts->tp_index, "unable to request gpio [%d]\n", ts->hw_res.cs_gpio);
		else
			TP_BOOT_INFO(ts->tp_index, "%s : irq_gpio = %d\n", __func__, ts->hw_res.cs_gpio);
	} else {
		TP_BOOT_INFO(ts->tp_index, "ts->cs-gpio not specified\n");
	}
	ts->hw_res.pinctrl = devm_pinctrl_get(dev);

	if (IS_ERR_OR_NULL(ts->hw_res.pinctrl)) {
		TP_INFO(ts->tp_index, "Getting pinctrl handle failed");
	} else {
		ts->hw_res.pin_set_high = pinctrl_lookup_state(ts->hw_res.pinctrl,
					  "pin_set_high");

		if (IS_ERR_OR_NULL(ts->hw_res.pin_set_high)) {
			TP_BOOT_INFO(ts->tp_index, "Failed to get the high state pinctrl handle\n");
		}


		ts->hw_res.pin_set_low = pinctrl_lookup_state(ts->hw_res.pinctrl,
					 "pin_set_low");

		if (IS_ERR_OR_NULL(ts->hw_res.pin_set_low)) {
			TP_BOOT_INFO(ts->tp_index, " Failed to get the low state pinctrl handle\n");
		}

		ts->hw_res.pin_cs_high = pinctrl_lookup_state(ts->hw_res.pinctrl,
					  "pin_cs_high");

		if (IS_ERR_OR_NULL(ts->hw_res.pin_cs_high)) {
			TP_BOOT_INFO(ts->tp_index, "Failed to get the cs-gpio high state pinctrl\n");
		}


		ts->hw_res.pin_cs_low = pinctrl_lookup_state(ts->hw_res.pinctrl,
					 "pin_cs_low");

		if (IS_ERR_OR_NULL(ts->hw_res.pin_cs_low)) {
			TP_BOOT_INFO(ts->tp_index, " Failed to get the cs-gpio low state pinctrl \n");
		}

		ts->hw_res.pin_set_nopull = pinctrl_lookup_state(ts->hw_res.pinctrl,
					    "pin_set_nopull");

		if (IS_ERR_OR_NULL(ts->hw_res.pin_set_nopull)) {
			TP_BOOT_INFO(ts->tp_index, "Failed to get the input state pinctrl handle\n");
		}

		/* active spi mode */
		ts->hw_res.pin_spi_mode_active = pinctrl_lookup_state(ts->hw_res.pinctrl, PINCTRL_STATE_SPI_ACTIVE);
		if (IS_ERR_OR_NULL(ts->hw_res.pin_spi_mode_active)) {
			rc = PTR_ERR(ts->hw_res.pin_spi_mode_active);
			TPD_BOOT_INFO("Failed to get pinctrl state:%s, r:%d",
					PINCTRL_STATE_SPI_ACTIVE, rc);
			ts->hw_res.pin_spi_mode_active = NULL;
		}

		/* int active state */
		ts->hw_res.pin_int_sta_active = pinctrl_lookup_state(ts->hw_res.pinctrl, PINCTRL_STATE_INT_ACTIVE);
		if (IS_ERR_OR_NULL(ts->hw_res.pin_int_sta_active)) {
			rc = PTR_ERR(ts->hw_res.pin_int_sta_active);
			TPD_BOOT_INFO("Failed to get pinctrl state:%s, r:%d",
					PINCTRL_STATE_INT_ACTIVE, rc);
			ts->hw_res.pin_int_sta_active = NULL;
		}

		/* rst active state */
		ts->hw_res.pin_rst_sta_active = pinctrl_lookup_state(ts->hw_res.pinctrl, PINCTRL_STATE_RST_ACTIVE);
		if (IS_ERR_OR_NULL(ts->hw_res.pin_rst_sta_active)) {
			rc = PTR_ERR(ts->hw_res.pin_rst_sta_active);
			TPD_BOOT_INFO("Failed to get pinctrl state:%s, r:%d",
					PINCTRL_STATE_RST_ACTIVE, rc);
			ts->hw_res.pin_rst_sta_active = NULL;
		}
		TPD_BOOT_INFO("success get avtive pinctrl state");

		/* suspend spi mode */
		ts->hw_res.pin_spi_mode_suspend = pinctrl_lookup_state(ts->hw_res.pinctrl, PINCTRL_STATE_SPI_SUSPEND);
		if (IS_ERR_OR_NULL(ts->hw_res.pin_spi_mode_suspend)) {
			rc = PTR_ERR(ts->hw_res.pin_spi_mode_suspend);
			TPD_BOOT_INFO("Failed to get pinctrl state:%s, r:%d",
					PINCTRL_STATE_SPI_SUSPEND, rc);
			ts->hw_res.pin_spi_mode_suspend = NULL;
		}

		/* int suspend state */
		ts->hw_res.pin_int_sta_suspend = pinctrl_lookup_state(ts->hw_res.pinctrl, PINCTRL_STATE_INT_SUSPEND);
		if (IS_ERR_OR_NULL(ts->hw_res.pin_int_sta_suspend)) {
			rc = PTR_ERR(ts->hw_res.pin_int_sta_suspend);
			TPD_BOOT_INFO("Failed to get pinctrl state:%s, r:%d",
					PINCTRL_STATE_INT_SUSPEND, rc);
			ts->hw_res.pin_int_sta_suspend = NULL;
		}

		/* suspend spi mode */
		ts->hw_res.pin_rst_sta_suspend = pinctrl_lookup_state(ts->hw_res.pinctrl, PINCTRL_STATE_RST_SUSPEND);
		if (IS_ERR_OR_NULL(ts->hw_res.pin_rst_sta_suspend)) {
			rc = PTR_ERR(ts->hw_res.pin_rst_sta_suspend);
			TPD_BOOT_INFO("Failed to get pinctrl state:%s, r:%d",
					PINCTRL_STATE_RST_SUSPEND, rc);
			ts->hw_res.pin_rst_sta_suspend = NULL;
		}
		TPD_BOOT_INFO("success get suspend pinctrl state");
	}

	ts->hw_res.enable_avdd_gpio = of_get_named_gpio(np, "enable2v8_gpio", 0);

	if (ts->hw_res.enable_avdd_gpio < 0) {
		TP_BOOT_INFO(ts->tp_index, "ts->hw_res.enable2v8_gpio not specified\n");

	} else {
		if (gpio_is_valid(ts->hw_res.enable_avdd_gpio)) {
			rc = gpio_request(ts->hw_res.enable_avdd_gpio, "vdd2v8-gpio");

			if (rc) {
				TP_INFO(ts->tp_index, "unable to request gpio [%d] %d\n", ts->hw_res.enable_avdd_gpio, rc);
			}
		}
	}

	ts->hw_res.enable_vddi_gpio = of_get_named_gpio(np, "enable1v8_gpio", 0);

	if (ts->hw_res.enable_vddi_gpio < 0) {
		TP_BOOT_INFO(ts->tp_index, "ts->hw_res.enable1v8_gpio not specified\n");

	} else {
		if (gpio_is_valid(ts->hw_res.enable_vddi_gpio)) {
			rc = gpio_request(ts->hw_res.enable_vddi_gpio, "vcc1v8-gpio");

			if (rc) {
				TP_INFO(ts->tp_index, "unable to request gpio [%d], %d\n", ts->hw_res.enable_vddi_gpio, rc);
			}
		}
	}

	/* interrupt mode*/
	ts->int_mode = BANNABLE;
	rc = of_property_read_u32(np, "touchpanel,int-mode", &val);

	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "int-mode not specified\n");

	} else {
		if (val < INTERRUPT_MODE_MAX) {
			ts->int_mode = val;
		}
	}

	/*parse chip name*/
	rc = of_property_read_u32(np, "chip-num", &ts->panel_data.chip_num);

	if (rc)  {
		TP_BOOT_INFO(ts->tp_index, "panel_type not specified, need to default 1");
		ts->panel_data.chip_num = 1;
	}

	TP_BOOT_INFO(ts->tp_index, "find %d num chip in dts", ts->panel_data.chip_num);

	for (i = 0; i < ts->panel_data.chip_num; i++) {
		/*ts->panel_data.chip_name[i] = devm_kzalloc(dev, 100, GFP_KERNEL);

		if (ts->panel_data.chip_name[i] == NULL) {
			TPD_INFO("panel_data.chip_name kzalloc error\n");
			devm_kfree(dev, ts->panel_data.chip_name[i]);
			goto dts_match_error;
		}*/

		rc = of_property_read_string_index(np, "chip-name", i,
						   (const char **)&ts->panel_data.chip_name[i]);
		TP_BOOT_INFO(ts->tp_index, "panel_data.chip_name = %s\n", ts->panel_data.chip_name[i]);

		if (rc) {
			TP_BOOT_INFO(ts->tp_index, "chip-name not specified");
		}
	}

	rc = of_property_read_u32(np, "touchpanel,irq_need_dev_resume_time", &ts->irq_need_dev_resume_time);
	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "ts->irq_need_dev_resume_time not specified\n");
		ts->irq_need_dev_resume_time = 50;
	}
	TP_INFO(ts->tp_index, "ts->irq_need_dev_resume_time = %d ms\n", ts->irq_need_dev_resume_time);

	/* resolution info*/
	rc = of_property_read_u32(np, "touchpanel,max-num-support", &ts->max_num);

	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "ts->max_num not specified\n");
		ts->max_num = 10;
	}

	rc = of_property_read_u32_array(np, "touchpanel,tx-rx-num", tx_rx_num, 2);

	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "tx-rx-num not set\n");
		ts->hw_res.tx_num = 0;
		ts->hw_res.rx_num = 0;

	} else {
		ts->hw_res.tx_num = tx_rx_num[0];
		ts->hw_res.rx_num = tx_rx_num[1];
	}

	TP_BOOT_INFO(ts->tp_index, "tx_num = %d, rx_num = %d \n", ts->hw_res.tx_num, ts->hw_res.rx_num);

	rc = of_property_read_u32_array(np, "touchpanel,display-coords", temp_array, 2);

	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "Lcd size not set\n");
		ts->resolution_info.LCD_WIDTH = 0;
		ts->resolution_info.LCD_HEIGHT = 0;

	} else {
		ts->resolution_info.LCD_WIDTH = temp_array[0];
		ts->resolution_info.LCD_HEIGHT = temp_array[1];
	}

	rc = of_property_read_u32_array(np, "touchpanel,panel-coords", temp_array, 2);

	if (rc) {
		ts->resolution_info.max_x = 0;
		ts->resolution_info.max_y = 0;

	} else {
		ts->resolution_info.max_x = temp_array[0];
		ts->resolution_info.max_y = temp_array[1];
	}

	rc = of_property_read_u32_array(np, "touchpanel,touchmajor-limit", temp_array,
					2);

	if (rc) {
		ts->touch_major_limit.width_range = 0;
		ts->touch_major_limit.height_range = 54;    /*set default value*/

	} else {
		ts->touch_major_limit.width_range = temp_array[0];
		ts->touch_major_limit.height_range = temp_array[1];
	}

	TP_BOOT_INFO(ts->tp_index, "LCD_WIDTH = %d, LCD_HEIGHT = %d, max_x = %d, max_y = %d, limit_witdh = %d, limit_height = %d\n",
		 ts->resolution_info.LCD_WIDTH, ts->resolution_info.LCD_HEIGHT,
		 ts->resolution_info.max_x, ts->resolution_info.max_y, \
		 ts->touch_major_limit.width_range, ts->touch_major_limit.height_range);

	/* virturl key Related*/
	rc = of_property_read_u32_array(np, "touchpanel,button-type", temp_array, 2);

	if (rc < 0) {
		TP_BOOT_INFO(ts->tp_index, "error:button-type should be setting in dts!");

	} else {
		ts->vk_type = temp_array[0];
		ts->vk_bitmap = temp_array[1] & 0xFF;

		if (ts->vk_type == TYPE_PROPERTIES) {
			rc = of_property_read_u32_array(np, "touchpanel,button-map", temp_array, 8);

			if (rc) {
				TP_BOOT_INFO(ts->tp_index, "button-map not set\n");

			} else {
				ts->button_map.coord_menu.x = temp_array[0];
				ts->button_map.coord_menu.y = temp_array[1];
				ts->button_map.coord_home.x = temp_array[2];
				ts->button_map.coord_home.y = temp_array[3];
				ts->button_map.coord_back.x = temp_array[4];
				ts->button_map.coord_back.y = temp_array[5];
				ts->button_map.width_x = temp_array[6];
				ts->button_map.height_y = temp_array[7];
			}
		}
	}

	/*touchkey take tx num and rx num*/
	rc = of_property_read_u32_array(np, "touchpanel.button-TRx", temp_array, 2);

	if (rc < 0) {
		TP_BOOT_INFO(ts->tp_index, "error:button-TRx should be setting in dts!\n");
		ts->hw_res.key_tx = 0;
		ts->hw_res.key_rx = 0;

	} else {
		ts->hw_res.key_tx = temp_array[0];
		ts->hw_res.key_rx = temp_array[1];
		TP_BOOT_INFO(ts->tp_index, "key_tx is %d, key_rx is %d\n", ts->hw_res.key_tx, ts->hw_res.key_rx);
	}
	rc = of_property_read_u32_array(np, "touchpanel,smooth-level", temp_array, SMOOTH_LEVEL_NUM);
	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "smooth_level_array not specified %d\n", rc);
	} else {
		ts->smooth_level_array_support = true;
		for (i=0; i < SMOOTH_LEVEL_NUM; i++) {
			ts->smooth_level_array[i] = temp_array[i];
		}

		rc = of_property_read_u32_array(np,
						"touchpanel,smooth-level-charging",
						temp_array,
						SMOOTH_LEVEL_NUM);
		if (rc) {
			TP_INFO(ts->tp_index, "smooth_level_charging_array not specified %d\n", rc);
			for (i=0; i < SMOOTH_LEVEL_NUM; i++) {
				ts->smooth_level_charging_array[i] = ts->smooth_level_array[i];
			}
		} else {
			for (i=0; i < SMOOTH_LEVEL_NUM; i++) {
				ts->smooth_level_charging_array[i] = temp_array[i];
			}
		}
		ts->smooth_level_used_array = (u32 *)&(ts->smooth_level_array);
	}

	rc = of_property_read_u32_array(np, "touchpanel,sensitive-level", temp_array, SENSITIVE_LEVEL_NUM);
	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "sensitive_level_array not specified %d\n", rc);
	} else {
		ts->sensitive_level_array_support = true;
		for (i=0; i < SENSITIVE_LEVEL_NUM; i++) {
			ts->sensitive_level_array[i] = temp_array[i];
		}

		rc = of_property_read_u32_array(np,
						"touchpanel,sensitive-level-charging",
						temp_array,
						SENSITIVE_LEVEL_NUM);
		if (rc) {
			TP_BOOT_INFO(ts->tp_index, "sensitive_charging_array not specified %d\n", rc);
			for (i=0; i < SENSITIVE_LEVEL_NUM; i++) {
				ts->sensitive_level_charging_array[i] = ts->sensitive_level_array[i];
			}
		} else {
			for (i=0; i < SENSITIVE_LEVEL_NUM; i++) {
				ts->sensitive_level_charging_array[i] = temp_array[i];
			}
		}
		ts->sensitive_level_used_array = (u32 *)&(ts->sensitive_level_array);
	}

	rc = of_property_read_u32_array(np, "touchpanel,game_perf_para_default", temp_array, 2);

	if (rc < 0) {
		TP_BOOT_INFO(ts->tp_index, "error:game_perf_para_default should be setting in dts!\n");
		ts->sensitive_level_default = 0;
		ts->smooth_level_default = 0;

	} else {
		ts->sensitive_level_default = temp_array[0];
		ts->smooth_level_default = temp_array[1];
		TP_BOOT_INFO(ts->tp_index, "sensitive_level_default is %d, smooth_level_default is %d\n",
			ts->sensitive_level_default, ts->smooth_level_default);
	}

	/*set disable suspend irq handler parameter, for of_property_read_bool return 1 when success and return 0 when item is not exist*/
	ts->disable_suspend_irq_handler = of_property_read_bool(np, "disable_suspend_irq_handler_support");

	/*set incell panel parameter, for of_property_read_bool return 1 when success and return 0 when item is not exist*/
	ts->is_incell_panel = of_property_read_bool(np, "incell_screen");
	rc = ts->is_incell_panel;

	if (rc > 0) {
		TP_BOOT_INFO(ts->tp_index, "panel is incell!\n");
		ts->is_incell_panel = 1;

	} else {
		TP_BOOT_INFO(ts->tp_index, "panel is oncell!\n");
		ts->is_incell_panel = 0;
	}

	ts->tp_ic_type = TYPE_ONCELL;
	rc = of_property_read_u32(np, "touchpanel,tp_ic_type", &val);

	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "tp_ic_type not specified\n");

	} else {
		if (val < TYPE_IC_MAX) {
			ts->tp_ic_type = val;
		}
	}

	rc = of_property_read_u32(np, "touchpanel,single-optimized-time", &ts->single_optimized_time);
	if (rc) {
		TP_BOOT_INFO(ts->tp_index, "ts->single_optimized_time not specified\n");
		ts->single_optimized_time = 0;
		ts->optimized_show_support = false;
	} else {
		ts->total_operate_times = 0;
		ts->optimized_show_support = true;
	}

    rc = of_property_read_u32(np, "touchpanel,high-frame-rate-time", &ts->high_frame_rate_time);
    if (rc) {
        TP_BOOT_INFO(ts->tp_index, "ts->high_frame_rate_time not specified or support\n");
        ts->high_frame_rate_time = 0;
        ts->high_frame_rate_support = false;
    } else {
        ts->high_frame_rate_support = true;
    }

    rc = of_property_read_u32(np, "report_rate_limit", &ts->panel_data.report_rate_limit);
    if (rc) {
        TPD_BOOT_INFO("report_rate_limit not specified,we will set it 60hz\n");
        ts->monitor_data.RATE_MIN = 60;
    } else {
        ts->monitor_data.RATE_MIN = ts->panel_data.report_rate_limit;
    }

	rc = of_property_read_string(np, "touchpanel,touch-environment", (char const **)&ts->touch_environment);
	if (rc) {
		dev_warn(ts->dev,
			"%s: No trusted touch mode environment\n", __func__);
		ts->touch_environment = "pvm";
	}
	else {
		TPD_BOOT_INFO("touch_environment:%s\n", ts->touch_environment);
	}

	return ret;

// dts_match_error:
// 	return -1;
}

static void lcd_tp_refresh_work(struct work_struct *work)
{
	struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
				     tp_refresh_work);

	if (ts->int_mode == BANNABLE) {
		mutex_lock(&ts->mutex);
	}
	if (!ts->is_suspended && (ts->suspend_state == TP_SPEEDUP_RESUME_COMPLETE)
		&& !ts->loading_fw) {
		ts->ts_ops->tp_refresh_switch(ts->chip_data, ts->lcd_fps);
	}
	if (ts->int_mode == BANNABLE) {
		mutex_unlock(&ts->mutex);
	}
}

static int init_power_control(struct touchpanel_data *ts)
{
	int ret = 0;
	/* 1.8v*/
	ts->hw_res.vddi = regulator_get(ts->dev, "vcc_1v8");

	if (IS_ERR_OR_NULL(ts->hw_res.vddi)) {
		TP_BOOT_INFO(ts->tp_index, "Regulator get failed vcc_1v8, ret = %d\n", ret);

	} else {
		if (regulator_count_voltages(ts->hw_res.vddi) > 0 || ts->regulator_count_not_support) {

			if (ts->hw_res.vddi_volt) {
				ret = regulator_set_voltage(ts->hw_res.vddi, ts->hw_res.vddi_volt,
							    ts->hw_res.vddi_volt);
			} else {
				ret = regulator_set_voltage(ts->hw_res.vddi, 1800000, 1800000);
			}

			if (ret) {
				dev_err(ts->dev, "Regulator set_vtg failed vcc_i2c rc = %d\n", ret);
				goto err;
			}

			ret = regulator_set_load(ts->hw_res.vddi, 200000);

			if (ret < 0) {
				dev_err(ts->dev, "Failed to set vcc_1v8 mode(rc:%d)\n", ret);
				goto err;
			}
		} else {
			TPD_BOOT_INFO("regulator_count_voltages is not support\n");
		}
	}

	/* vdd 2.8v*/
	ts->hw_res.avdd = regulator_get(ts->dev, "vdd_2v8");

	if (IS_ERR_OR_NULL(ts->hw_res.avdd)) {
		TP_BOOT_INFO(ts->tp_index, "Regulator vdd2v8 get failed, ret = %d\n", ret);

	} else {
		if (regulator_count_voltages(ts->hw_res.avdd) > 0) {
			TP_BOOT_INFO(ts->tp_index, "set avdd voltage to %d uV\n", ts->hw_res.vdd_volt);

			if (ts->hw_res.vdd_volt) {
				ret = regulator_set_voltage(ts->hw_res.avdd, ts->hw_res.vdd_volt,
							    ts->hw_res.vdd_volt);

			} else {
				ret = regulator_set_voltage(ts->hw_res.avdd, 3100000, 3100000);
			}

			if (ret) {
				dev_err(ts->dev, "Regulator set_vtg failed vdd rc = %d\n", ret);
				goto err;
			}

			ret = regulator_set_load(ts->hw_res.avdd, 200000);

			if (ret < 0) {
				dev_err(ts->dev, "Failed to set vdd_2v8 mode(rc:%d)\n", ret);
				goto err;
			}
		} else {
			TPD_BOOT_INFO("regulator_count_voltages is not support\n");
		}
	}

	return 0;

err:
	return ret;
}

static int tp_power_init(struct touchpanel_data *pdata)
{
	struct touchpanel_data *ts = pdata;
	int ret = -1;

	if (!ts) {
		return ret;
	}

	ret = init_power_control(ts);

	if (ret) {
		ret = -EINVAL;
		TP_INFO(ts->tp_index, "%s: tp power init failed.\n", __func__);
		return ret;
	}

	if (!ts->ts_ops->power_control) {
		ret = -EINVAL;
		TP_INFO(ts->tp_index, "tp power_control NULL!\n");
		return ret;
	}

	ret = ts->ts_ops->power_control(ts->chip_data, true);

	if (ret) {
		ret = -EINVAL;
		TP_INFO(ts->tp_index, "%s: tp power init failed.\n", __func__);
		return ret;
	}

	return 0;
}

static int oplus_gpio_resume(struct touchpanel_data *ts)
{
	int r = 0;

	if (ts->hw_res.pin_spi_mode_active != NULL) {
		r = pinctrl_select_state(ts->hw_res.pinctrl, ts->hw_res.pin_spi_mode_active);
		if (r < 0) {
			TPD_INFO("Failed to select pin_spi_mode_active, r:%d", r);
			goto err_resume_pinctrl;
		}
		TPD_INFO("success set resume for pin_spi_mode_active");
	}

	if (ts->hw_res.pin_int_sta_active != NULL) {
		r = pinctrl_select_state(ts->hw_res.pinctrl, ts->hw_res.pin_int_sta_active);
		if (r < 0) {
			TPD_INFO("Failed to select pin_int_sta_active, r:%d", r);
			goto err_resume_pinctrl;
		}
		TPD_INFO("success set resume for pin_int_sta_active");
	}

	if (ts->hw_res.pin_rst_sta_active != NULL) {
		r = pinctrl_select_state(ts->hw_res.pinctrl, ts->hw_res.pin_rst_sta_active);
		if (r < 0) {
			TPD_INFO("Failed to select pin_rst_sta_active, r:%d", r);
			goto err_resume_pinctrl;
		}
		TPD_INFO("success set resume for pin_rst_sta_active");
	}

err_resume_pinctrl:
		return r;
}

static int tp_paneldata_init(struct touchpanel_data *pdata)
{
	struct touchpanel_data *ts = pdata;
	int ret = -1;

	if (!ts) {
		return ret;
	}

	/*step7 : Alloc fw_name/devinfo memory space*/
	ts->panel_data.fw_name = devm_kzalloc(ts->dev, MAX_FW_NAME_LENGTH,
				 GFP_KERNEL);

	if (ts->panel_data.fw_name == NULL) {
		ret = -ENOMEM;
		TP_INFO(ts->tp_index, "panel_data.fw_name kzalloc error\n");
		return ret;
	}

	return 0;
}

/*******Part3:Function  Area********************************/
static struct touchpanel_data *get_ts_data(unsigned int tp_index)
{
	struct touchpanel_data *ts = NULL;

	mutex_lock(&tp_core_lock);
	if (tp_index >= TP_SUPPORT_MAX) {
		TPD_INFO("tp_index:%u, is beyond %d,\n", tp_index, TP_SUPPORT_MAX);
		ts = g_tp[0];
	} else {
		ts = g_tp[tp_index];
	}
	mutex_unlock(&tp_core_lock);
	return ts;
}

static ssize_t cap_vk_show(struct kobject *kobj, struct kobj_attribute *attr,
			   char *buf)
{
	struct button_map *button_map = NULL;
	struct touchpanel_data *ts = NULL;
	int i = 0;
	int retval = 0;
	int count = 0;

	for (i = 0; i < TP_SUPPORT_MAX;  i++) {
		ts = get_ts_data(i);

		if (!ts) {
			continue;
		}

		if (ts->tp_index == i) {
			button_map = &ts->button_map;
			retval =  snprintf(buf, PAGESIZE - count,
					   __stringify(EV_KEY) ":" __stringify(KEY_MENU)   ":%d:%d:%d:%d"
					   ":" __stringify(EV_KEY) ":" __stringify(KEY_HOMEPAGE)   ":%d:%d:%d:%d"
					   ":" __stringify(EV_KEY) ":" __stringify(KEY_BACK)   ":%d:%d:%d:%d"
					   "\n", button_map->coord_menu.x, button_map->coord_menu.y, button_map->width_x,
					   button_map->height_y, \
					   button_map->coord_home.x, button_map->coord_home.y, button_map->width_x,
					   button_map->height_y, \
					   button_map->coord_back.x, button_map->coord_back.y, button_map->width_x,
					   button_map->height_y);

			if (retval < 0) {
				return retval;
			}

			buf += retval;
			count += retval;
		}
	}

	return count;
}

static struct kobj_attribute virtual_keys_attr = {
	.attr = {
		.name = "virtualkeys."TPD_DEVICE,
		.mode = S_IRUGO,
	},
	.show = &cap_vk_show,
};

static struct attribute *properties_attrs[] = {
	&virtual_keys_attr.attr,
	NULL
};

static struct attribute_group properties_attr_group = {
	.attrs = properties_attrs,
};

/**
 * init_input_device - Using for register input device
 * @ts: touchpanel_data struct using for common driver
 *
 * we should using this function setting input report capbility && register input device
 * Returning zero(success) or negative errno(failed)
 */
static int init_input_device(struct touchpanel_data *ts)
{
	int ret = 0;
	struct kobject *vk_properties_kobj;
	static  bool board_properties = false;

	TP_BOOT_INFO(ts->tp_index, "%s is called\n", __func__);
	ts->input_dev = devm_input_allocate_device(ts->dev);

	if (ts->input_dev == NULL) {
		ret = -ENOMEM;
		TP_INFO(ts->tp_index, "Failed to allocate input device\n");
		return ret;
	}

	ts->kpd_input_dev  = devm_input_allocate_device(ts->dev);

	if (ts->kpd_input_dev == NULL) {
		ret = -ENOMEM;
		TP_INFO(ts->tp_index, "Failed to allocate key input device\n");
		return ret;
	}

	if (ts->face_detect_support) {
		ts->ps_input_dev  = devm_input_allocate_device(ts->dev);

		if (ts->ps_input_dev == NULL) {
			ret = -ENOMEM;
			TP_INFO(ts->tp_index, "Failed to allocate ps input device\n");
			return ret;
		}
		if (!ts->tp_index) {
			ts->ps_input_dev->name = TPD_DEVICE"_ps";
		} else {
			ts->ps_input_dev->name = TPD_DEVICE"_ps1";
		}
		set_bit(EV_MSC, ts->ps_input_dev->evbit);
		set_bit(MSC_RAW, ts->ps_input_dev->mscbit);
	}

	if (!ts->tp_index) {
		ts->input_dev->name = TPD_DEVICE;
	} else {
		ts->input_dev->name = TPD_DEVICE"1";
	}
	ts->input_dev->id.bustype = ts->id.bustype;
	ts->input_dev->id.vendor = ts->id.vendor;
	ts->input_dev->id.product = ts->id.product;
	set_bit(EV_SYN, ts->input_dev->evbit);
	set_bit(EV_ABS, ts->input_dev->evbit);
	set_bit(EV_KEY, ts->input_dev->evbit);
	set_bit(ABS_MT_TOUCH_MAJOR, ts->input_dev->absbit);
	set_bit(ABS_MT_WIDTH_MAJOR, ts->input_dev->absbit);
	set_bit(ABS_MT_POSITION_X, ts->input_dev->absbit);
	set_bit(ABS_MT_POSITION_Y, ts->input_dev->absbit);
	set_bit(ABS_MT_PRESSURE, ts->input_dev->absbit);
	set_bit(ABS_TOUCH_COST_TIME_KERNEL, ts->input_dev->absbit);
	/*set_bit(ABS_TOUCH_COST_TIME_ALGO, ts->input_dev->absbit);
	set_bit(ABS_TOUCH_COST_TIME_DAEMON, ts->input_dev->absbit);*/
	set_bit(ABS_MT_TOOL_TYPE, ts->input_dev->absbit);
	set_bit(INPUT_PROP_DIRECT, ts->input_dev->propbit);
	set_bit(BTN_TOOL_FINGER, ts->input_dev->keybit);
	set_bit(BTN_TOUCH, ts->input_dev->keybit);

	if (ts->palm_to_sleep_support)
		set_bit(KEY_SLEEP, ts->input_dev->keybit);

	if (!ts->tp_index) {
		ts->kpd_input_dev->name = TPD_DEVICE"_kpd";
	} else {
		ts->kpd_input_dev->name = TPD_DEVICE"_kpd1";
	}
	set_bit(EV_KEY, ts->kpd_input_dev->evbit);
	set_bit(EV_SYN, ts->kpd_input_dev->evbit);

	switch (ts->vk_type) {
	case TYPE_PROPERTIES : {
		if (!board_properties) {
			/*If more ic support more key, but have only one path*/
			TP_INFO(ts->tp_index, "Type 1: using board_properties\n");
			vk_properties_kobj = kobject_create_and_add("board_properties", NULL);

			if (vk_properties_kobj) {
				ret = sysfs_create_group(vk_properties_kobj, &properties_attr_group);
			}

			if (!vk_properties_kobj || ret) {
				TP_INFO(ts->tp_index, "failed to create board_properties\n");
			}

			board_properties = true;
		}

		break;
	}

	case TYPE_AREA_SEPRATE: {
		TPD_DEBUG("Type 2:using same IC (button zone &&  touch zone are seprate)\n");

		if (CHK_BIT(ts->vk_bitmap, BIT_MENU)) {
			set_bit(KEY_MENU, ts->kpd_input_dev->keybit);
		}

		if (CHK_BIT(ts->vk_bitmap, BIT_HOME)) {
			set_bit(KEY_HOMEPAGE, ts->kpd_input_dev->keybit);
		}

		if (CHK_BIT(ts->vk_bitmap, BIT_BACK)) {
			set_bit(KEY_BACK, ts->kpd_input_dev->keybit);
		}

		break;
	}

	default :
		break;
	}

	ret = input_mt_init_slots(ts->input_dev, ts->max_num, 0);
	if (!ret)
		TP_BOOT_INFO(ts->tp_index, "%s: input_mt_init_slots return %d\n", __func__, ret);
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	//input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0, 255, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0,
			     ts->resolution_info.max_x - 1, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0,
			     ts->resolution_info.max_y - 1, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_TOUCH_COST_TIME_KERNEL, 0, MAX_TOUCH_COST_TIME, 0, 0);
	/*input_set_abs_params(ts->input_dev, ABS_TOUCH_COST_TIME_ALGO, 0, MAX_TOUCH_COST_TIME, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_TOUCH_COST_TIME_DAEMON, 0, MAX_TOUCH_COST_TIME, 0, 0);*/
	input_set_drvdata(ts->input_dev, ts);
	input_set_drvdata(ts->kpd_input_dev, ts);

	if (input_register_device(ts->input_dev)) {
		TP_INFO(ts->tp_index, "%s: Failed to register input device\n", __func__);
		input_free_device(ts->input_dev);
		return -1;
	}

	if (input_register_device(ts->kpd_input_dev)) {
		TP_INFO(ts->tp_index, "%s: Failed to register key input device\n", __func__);
		input_free_device(ts->kpd_input_dev);
		return -1;
	}

	if (ts->face_detect_support) {
		if (input_register_device(ts->ps_input_dev)) {
			TP_INFO(ts->tp_index, "%s: Failed to register ps input device\n", __func__);
			input_free_device(ts->ps_input_dev);
			return -1;
		}
	}

	return 0;
}

/**
 * init_touch_interfaces - Using for Register IIC interface
 * @dev: i2c_client->dev using to alloc memory for dma transfer
 * @flag_register_16bit: bool param to detect whether this device using 16bit IIC address or 8bit address
 *
 * Actully, This function don't have many operation, we just detect device address length && alloc DMA memory for MTK platform
 * Returning zero(sucess) or -ENOMEM(memory alloc failed)
 */
static int init_touch_interfaces(struct device *dev, bool flag_register_16bit)
{
	struct touchpanel_data *ts = dev_get_drvdata(dev);

	if (!ts) {
		return -1;
	}

	ts->interface_data.register_is_16bit = flag_register_16bit;
	mutex_init(&ts->interface_data.bus_mutex);

	return 0;
}

/**
 * input_report_key_oplus - Using for report virtual key
 * @work: work struct using for this thread
 *
 * before report virtual key, detect whether touch_area has been touched
 * Do not care the result: Return void type
 */
void input_report_key_oplus(struct touchpanel_data *ts, unsigned int code,
			    int value)
{
	if (!ts) {
		return;
	}

	if (value) { /*report Key[down]*/
		if (ts->view_area_touched == 0) {
			input_report_key(ts->kpd_input_dev, code, value);

		} else {
			TP_INFO(ts->tp_index, "Sorry,tp is touch down,can not report touch key\n");
		}

	} else {
		input_report_key(ts->kpd_input_dev, code, value);
	}
}

static void tp_btnkey_release(struct touchpanel_data *ts)
{
	if (CHK_BIT(ts->vk_bitmap, BIT_MENU)) {
		input_report_key_oplus(ts, KEY_MENU, 0);
	}

	if (CHK_BIT(ts->vk_bitmap, BIT_HOME)) {
		input_report_key_oplus(ts, KEY_HOMEPAGE, 0);
	}

	if (CHK_BIT(ts->vk_bitmap, BIT_BACK)) {
		input_report_key_oplus(ts, KEY_BACK, 0);
	}

	input_sync(ts->kpd_input_dev);
}

static void tp_touch_release(struct touchpanel_data *ts)
{
	int i = 0;


	mutex_lock(&ts->report_mutex);

	if (ts->is_pen_connected && ts->pen_mode_tp_state == CANCLE_TP) {
		ts->pen_mode_tp_state = DEFAULT;
		input_mt_slot(ts->input_dev, 0);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_PALM, 1);
		input_sync(ts->input_dev);
		TP_INFO(ts->tp_index,
		"detect touch up in pen mode, need send 'cancle' to apk\n");
	}

	for (i = 0; i < ts->max_num; i++) {
		input_mt_slot(ts->input_dev, i);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
	}

	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
	input_sync(ts->input_dev);
	mutex_unlock(&ts->report_mutex);

	TP_INFO(ts->tp_index,
		"release all touch point and key, clear tp touch down flag\n");
	ts->view_area_touched = 0; /*realse all touch point,must clear this flag*/
	ts->touch_count = 0;
	ts->irq_slot = 0;
}

void operate_mode_switch(struct touchpanel_data *ts)
{
	if (!ts->ts_ops->mode_switch) {
		TP_INFO(ts->tp_index, "not support ts_ops->mode_switch callback\n");
		return;
	}

	if (ts->is_suspended) {
		if (TP_ALL_GESTURE_SUPPORT) {
			if (TP_ALL_GESTURE_ENABLE) {
				ts->ts_ops->mode_switch(ts->chip_data, MODE_GESTURE, ts->gesture_enable
							|| ts->fp_enable);
				ts->ts_ops->mode_switch(ts->chip_data, MODE_NORMAL, true);

				if (ts->fingerprint_underscreen_support) {
					ts->ts_ops->enable_fingerprint(ts->chip_data, !!ts->fp_enable);
				}

				if (((ts->gesture_enable & 0x01) != 1) && ts->ts_ops->enable_gesture_mask) {
					ts->ts_ops->enable_gesture_mask(ts->chip_data, 0);
				}

			} else {
				ts->ts_ops->mode_switch(ts->chip_data, MODE_GESTURE, false);
				ts->ts_ops->mode_switch(ts->chip_data, MODE_SLEEP, true);
			}

		} else {
			ts->ts_ops->mode_switch(ts->chip_data, MODE_SLEEP, true);
		}

	} else {
		if (ts->face_detect_support) {
			if (ts->fd_enable) {
				input_event(ts->ps_input_dev, EV_MSC, MSC_RAW, 0);
				input_sync(ts->ps_input_dev);
			}

			ts->ts_ops->mode_switch(ts->chip_data, MODE_FACE_DETECT, ts->fd_enable == 1);
		}

		if (ts->fingerprint_underscreen_support) {
			ts->ts_ops->enable_fingerprint(ts->chip_data, !!ts->fp_enable);
		}

		if (ts->smooth_level_array_support && ts->ts_ops->smooth_lv_set) {
			ts->ts_ops->smooth_lv_set(ts->chip_data, ts->smooth_level_used_array[ts->smooth_level_chosen]);
		}

		if (ts->sensitive_level_array_support && ts->ts_ops->sensitive_lv_set) {
			ts->ts_ops->sensitive_lv_set(ts->chip_data, ts->sensitive_level_used_array[ts->sensitive_level_chosen]);
		}
		if (ts->diaphragm_touch_support && ts->ts_ops->diaphragm_touch_lv_set) {
			ts->ts_ops->diaphragm_touch_lv_set(ts->chip_data, ts->diaphragm_touch_level_chosen);
		}
		if (ts->lcd_tp_refresh_support && ts->ts_ops->tp_refresh_switch) {
			ts->ts_ops->tp_refresh_switch(ts->chip_data, ts->lcd_fps);
		}

		ts->ts_ops->mode_switch(ts->chip_data, MODE_NORMAL, true);
	}
}


/**
 * esd_handle_switch - open or close esd thread
 * @esd_info: touchpanel_data, using for common driver resource
 * @on: bool variable using for  indicating open or close esd check function.
 *     true:open;
 *     false:close;
 */
void esd_handle_switch(struct esd_information *esd_info, bool on)
{
	mutex_lock(&esd_info->esd_lock);

	if (on) {
		if (!esd_info->esd_running_flag) {
			esd_info->esd_running_flag = 1;

			TPD_INFO("Esd protector started, cycle: %d s\n", esd_info->esd_work_time / HZ);
			queue_delayed_work(esd_info->esd_workqueue, &esd_info->esd_check_work,
					   esd_info->esd_work_time);
		}

	} else {
		if (esd_info->esd_running_flag) {
			esd_info->esd_running_flag = 0;

			TPD_INFO("Esd protector stoped!\n");
			cancel_delayed_work(&esd_info->esd_check_work);
		}
	}

	mutex_unlock(&esd_info->esd_lock);
}

static bool monitor_irq_bus_ready(struct touchpanel_data *ts)
{
	struct monitor_data *moni = NULL;

	moni = &ts->monitor_data;

	/*device suspend start*/
	if (false == ts->bus_ready) {
		moni->irq_need_dev_resume_all_count++;
		moni->irq_bus_not_ready_count++;
		TP_INFO(ts->tp_index, "The device not resume %d ms!", ts->irq_need_dev_resume_time);
		return false;
	} else {/*device resume end*/
		if (moni->irq_bus_not_ready_count > moni->irq_need_dev_resume_max_count) {
			moni->irq_need_dev_resume_max_count = moni->irq_bus_not_ready_count;
		}
		moni->irq_bus_not_ready_count = 0;
		return true;
	}
	return true;
}

static void tp_btnkey_handle(struct touchpanel_data *ts)
{
	u8 touch_state = 0;

	if (ts->vk_type != TYPE_AREA_SEPRATE) {
		TPD_DEBUG("TP vk_type not proper, checktouchpanel, button-type\n");

		return;
	}

	if (!ts->ts_ops->get_keycode) {
		TP_INFO(ts->tp_index, "not support ts->ts_ops->get_keycode callback\n");

		return;
	}

	touch_state = ts->ts_ops->get_keycode(ts->chip_data);

	if (CHK_BIT(ts->vk_bitmap, BIT_MENU)) {
		input_report_key_oplus(ts, KEY_MENU, CHK_BIT(touch_state, BIT_MENU));
	}

	if (CHK_BIT(ts->vk_bitmap, BIT_HOME)) {
		input_report_key_oplus(ts, KEY_HOMEPAGE, CHK_BIT(touch_state, BIT_HOME));
	}

	if (CHK_BIT(ts->vk_bitmap, BIT_BACK)) {
		input_report_key_oplus(ts, KEY_BACK, CHK_BIT(touch_state, BIT_BACK));
	}

	input_sync(ts->kpd_input_dev);
}

static void tp_palm_to_sleep_inScreenLock(struct touchpanel_data *ts)
{
	char *palm_report = NULL;

	if (ts->palm_to_sleep_support) {
		input_report_key(ts->input_dev, KEY_SLEEP, 1);
		input_sync(ts->input_dev);
		input_report_key(ts->input_dev, KEY_SLEEP, 0);
		input_sync(ts->input_dev);
		palm_report = tp_kzalloc(30, GFP_KERNEL);
		if (palm_report) {
			snprintf(palm_report, 30, "palm_to_sleep_in_screenLock");
			tp_kfree((void **)&palm_report);
		}
	}
}

static void tp_rate_calc(struct touchpanel_data *ts, tp_rate tp_rate_type)
{
	switch(tp_rate_type) {
	case TP_RATE_START:
		ts->curr_time = ktime_to_ms(ktime_get());
		break;
	case TP_RATE_CLEAR:
		ts->irq_num = 0;
		break;
	case TP_RATE_CALC:
		if (ts->irq_interval == 0) {
			ts->irq_interval = ts->curr_time;
		}
		ts->irq_interval = ts->curr_time - ts->irq_interval;
		ts->irq_handle_time = ktime_to_ms(ktime_get()) - ts->curr_time;
		if(ts->irq_num > 0 && ts->monitor_data.in_game_mode && ts->irq_num % 10 == 0) {
			TPD_INFO("rate = %llu,ts->irq_handle_time =%llu,curr_time = %llu\n", 1000/ts->irq_interval, ts->irq_handle_time, ts->curr_time);
		} else {
			if (ts->irq_num > 0 && ts->irq_num % 100 == 0) {
				TPD_INFO("rate = %llu,ts->irq_handle_time =%llu,curr_time = %llu\n", 1000/ts->irq_interval, ts->irq_handle_time, ts->curr_time);
			}
		}

		ts->irq_interval = ts->curr_time;
		ts->irq_num++;
		break;
	default:
		break;
	}
}

static inline void tp_touch_down(struct touchpanel_data *ts, struct point_info points, int touch_report_num, int id)
{
	int cost_time = 0;
	if (ts->input_dev == NULL) {
		return;
	}

	input_report_key(ts->input_dev, BTN_TOUCH, 1);
	input_report_key(ts->input_dev, BTN_TOOL_FINGER, 1);

	if (touch_report_num == 1) {
		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, points.width_major);
		ts->last_width_major = points.width_major;
		ts->last_touch_major = points.touch_major;

	} else if (!(touch_report_num & 0x7f) || touch_report_num == 30) {
		/*if touch_report_num == 127, every 127 points, change width_major*/
		/*down and keep long time, auto repeat per 5 seconds, for weixing*/
		/*report move event after down event, for weixing voice delay problem, 30 -> 300ms in order to avoid the intercept by shortcut*/
		if (ts->last_width_major == points.width_major) {
			ts->last_width_major = points.width_major + 1;

		} else {
			ts->last_width_major = points.width_major;
		}

		input_report_abs(ts->input_dev, ABS_MT_WIDTH_MAJOR, ts->last_width_major);
	}

	if (ts->major_rate_limit_support && !!(ts->noise_level) && ts->tp_ic_touch_num == ts->last_tp_ic_touch_num && \
					points.x == ts->last_x_y_point[id].x && points.y == ts->last_x_y_point[id].y) {
		if (!(touch_report_num & ts->major_rate_limit_times)) {
			ts->last_touch_major = points.touch_major;
		}
	} else {
		ts->last_touch_major = points.touch_major;
	}

	if ((points.x > ts->touch_major_limit.width_range)
	    && (points.x < ts->resolution_info.max_x - ts->touch_major_limit.width_range)
	    && \
	    (points.y > ts->touch_major_limit.height_range)
	    && (points.y < ts->resolution_info.max_y -
		ts->touch_major_limit.height_range)) {
		/*smart_gesture_support*/
		if (ts->last_touch_major > SMART_GESTURE_THRESHOLD) {
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, ts->last_touch_major);

		} else {
			input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, SMART_GESTURE_LOW_VALUE);
		}

		/*pressure_report_support*/
		input_report_abs(ts->input_dev, ABS_MT_PRESSURE,
				 ts->last_touch_major);   /*add for fixing gripview tap no function issue*/
	}

	if (!CHK_BIT(ts->irq_slot, (1 << id))) {
		TP_INFO(ts->tp_index, "first touch point id %d [%4d %4d %4d %4d %4d %4d %4d]\n", id, points.x, points.y, points.z,
					points.rx_press, points.tx_press, points.rx_er, points.tx_er);
		if(id == 0 && tp_debug == 1 && ts->monitor_data.RATE_MIN) {
			tp_rate_calc(ts, TP_RATE_CLEAR);
		}
	}

	input_report_abs(ts->input_dev, ABS_MT_POSITION_X, points.x);
	input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, points.y);
	if(id == 0 && tp_debug == 1 && ts->monitor_data.RATE_MIN) {
		tp_rate_calc(ts, TP_RATE_CALC);
	}
	if (ts->last_x_y_point[id].x != points.x || ts->last_x_y_point[id].y != points.y) {
		cost_time = ktime_to_us(ktime_get()) - ktime_to_us(ts->monitor_data.irq_to_report_timer);
		input_report_abs(ts->input_dev, ABS_TOUCH_COST_TIME_KERNEL, (cost_time < MAX_TOUCH_COST_TIME) ? cost_time : MAX_TOUCH_COST_TIME);
	}
	ts->last_x_y_point[id].x = points.x;
	ts->last_x_y_point[id].y = points.y;

	TP_SPECIFIC_PRINT(ts->tp_index, ts->point_num, "Touchpanel id %d :Down[%4d %4d %4d %4d %4d %4d %4d] %d\n", id, points.x, points.y, points.z,
						points.rx_press, points.tx_press, points.rx_er, points.tx_er, cost_time);
}

static inline void tp_touch_up(struct touchpanel_data *ts)
{
	if (ts->input_dev == NULL) {
		return;
	}

	input_report_key(ts->input_dev, BTN_TOUCH, 0);
	input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
	ts->pen_mode_tp_state = DEFAULT;				/*need reset tp state, IC may report times palm, but common driver just report and clear once */
}

static inline void tp_touch_handle(struct touchpanel_data *ts)
{
	int i = 0;
	uint8_t finger_num = 0, touch_near_edge = 0, finger_num_center = 0;
	int obj_attention = 0;
	int retval = 0;
	struct point_info points[MAX_FINGER_NUM];

	if (((!ts->ts_ops->get_touch_points) && (!ts->enable_point_auto_change))
	    || ((!ts->ts_ops->get_touch_points_auto) && ts->enable_point_auto_change)) {
		TP_INFO(ts->tp_index, "not support ts->ts_ops->get_touch_points callback\n");
		return;
	}

	if(tp_debug == 1 && ts->monitor_data.RATE_MIN) {
		tp_rate_calc(ts, TP_RATE_START);
	}

	memset(points, 0, sizeof(points));

	if (!ts->enable_point_auto_change) {
		obj_attention = ts->ts_ops->get_touch_points(ts->chip_data, points, ts->max_num);
		if (obj_attention == -EINVAL) {
			TP_INFO(ts->tp_index, "Invalid points, ignore..\n");
			return;
		}
	} else {
		obj_attention = ts->ts_ops->get_touch_points_auto(ts->chip_data,
				points,
				ts->max_num,
				&ts->resolution_info);
		if (obj_attention == -EINVAL) {
			TP_INFO(ts->tp_index, "Invalid points, ignore..\n");
			return;
		}
	}

	mutex_lock(&ts->report_mutex);

	if (ts->major_rate_limit_support && !!(ts->noise_level)) {
		ts->tp_ic_touch_num = 0;
		for (i = 0; i < ts->max_num; i++) {
			if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01
					&& (points[i].status != 0)) {
				ts->tp_ic_touch_num++;
			}
		}
	}

	if ((obj_attention & TOUCH_BIT_CHECK) != 0) {
		ts->up_status = false;

		for (i = 0; i < ts->max_num; i++) {
			if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01
			    && (points[i].status == 0)) { /* buf[0] == 0 is wrong point, no process*/
				continue;
			}
			if (points[i].x > ts->resolution_info.max_x - 1
			    || points[i].y > ts->resolution_info.max_y - 1) { /* x y over max, no process*/
				continue;
			}

			if (((obj_attention & TOUCH_BIT_CHECK) >> i) & 0x01
			    && (points[i].status != 0)) {
				input_mt_slot(ts->input_dev, i);
				input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 1);
				ts->touch_report_num++;
				tp_touch_down(ts, points[i], ts->touch_report_num, i);
				SET_BIT(ts->irq_slot, (1 << i));
				finger_num++;

				if (ts->face_detect_support && ts->fd_enable && \
				    (points[i].y < ts->resolution_info.max_y / 2) && (points[i].x > 30)
				    && (points[i].x < ts->resolution_info.max_x - 30)) {
					finger_num_center++;
				}

				if (points[i].x > ts->resolution_info.max_x / 100
				    && points[i].x < ts->resolution_info.max_x * 99 / 100) {
					ts->view_area_touched = finger_num;

				} else {
					touch_near_edge++;
				}

				/*strore  the last point data*/
				retval = tp_memcpy(&ts->last_point, sizeof(ts->last_point), \
					  &points[i], sizeof(struct point_info), \
					  sizeof(struct point_info));
				if (retval < 0) {
					TPD_INFO("tp_memcpy failed.\n");
				}
			}
			else {
				input_mt_slot(ts->input_dev, i);
				input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
			}
		}

		ts->last_tp_ic_touch_num = ts->tp_ic_touch_num;
		if (touch_near_edge ==
		    finger_num) {        /*means all the touchpoint is near the edge*/
			ts->view_area_touched = 0;
		}

	} else {
		if (ts->up_status) {
			tp_touch_up(ts);
			mutex_unlock(&ts->report_mutex);
			return;
		}

		ts->total_operate_times++;
		finger_num = 0;
		finger_num_center = 0;
		ts->touch_report_num = 0;
		ts->tp_ic_touch_num = 0;
		ts->last_tp_ic_touch_num = 0;

		if (ts->is_pen_connected && ts->pen_mode_tp_state == CANCLE_TP) {
			ts->pen_mode_tp_state = DEFAULT;
			input_mt_slot(ts->input_dev, 0);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_PALM, 1);
			input_sync(ts->input_dev);
			TP_INFO(ts->tp_index,
			"detect touch up in pen mode,send touch cancle\n");
		}

		for (i = 0; i < ts->max_num; i++) {
			input_mt_slot(ts->input_dev, i);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
			ts->last_x_y_point[i].x = 0;
			ts->last_x_y_point[i].y = 0;
		}

		tp_touch_up(ts);
		ts->view_area_touched = 0;
		ts->irq_slot = 0;
		ts->up_status = true;
		TP_DETAIL(ts->tp_index, "all touch up,view_area_touched=%d finger_num=%d\n",
			  ts->view_area_touched, finger_num);
		TP_DETAIL(ts->tp_index, "last point x:%d y:%d\n", ts->last_point.x,
			  ts->last_point.y);
	}

	input_sync(ts->input_dev);
	ts->touch_count = (finger_num << 4) | (finger_num_center & 0x0F);
	mutex_unlock(&ts->report_mutex);
}

static void tp_face_detect_handle(struct touchpanel_data *ts)
{
	int ps_state = 0;

	if (!ts->ts_ops->get_face_state) {
		TP_INFO(ts->tp_index, "not support ts->ts_ops->get_face_state callback\n");
		return;
	}

	ps_state = ts->ts_ops->get_face_state(ts->chip_data);

	if (ps_state < 0) {
		return;
	}

	input_event(ts->ps_input_dev, EV_MSC, MSC_RAW, ps_state);
	input_sync(ts->ps_input_dev);
}

static void tp_fingerprint_handle(struct touchpanel_data *ts)
{
	struct fp_underscreen_info fp_tpinfo;

	if (((!ts->ts_ops->screenon_fingerprint_info) && (!ts->enable_point_auto_change))
	    || ((!ts->ts_ops->screenon_fingerprint_info_auto) && ts->enable_point_auto_change)) {
		TP_INFO(ts->tp_index, "not support screenon_fingerprint_info callback.\n");
		return;
	}

	if (!ts->enable_point_auto_change) {
		ts->ts_ops->screenon_fingerprint_info(ts->chip_data, &fp_tpinfo);
	} else {
		ts->ts_ops->screenon_fingerprint_info_auto(ts->chip_data,
							   &fp_tpinfo,
							   &ts->resolution_info);
	}
	ts->fp_info.area_rate = fp_tpinfo.area_rate;
	ts->fp_info.x = fp_tpinfo.x;
	ts->fp_info.y = fp_tpinfo.y;
	if (fp_tpinfo.touch_state == FINGERPRINT_DOWN_DETECT) {
		TP_INFO(ts->tp_index, "screen on down : (%d, %d)\n",
			ts->fp_info.x,
			ts->fp_info.y);

		ts->fp_info.touch_state = 1;
	} else if (fp_tpinfo.touch_state == FINGERPRINT_UP_DETECT) {
		TP_INFO(ts->tp_index, "screen on up : (%d, %d)\n", ts->fp_info.x, ts->fp_info.y);
		ts->fp_info.touch_state = 0;
	}
}

static void tp_geture_info_transform(struct gesture_info *gesture,
				     struct resolution_info *resolution_info)
{
	gesture->Point_start.x = gesture->Point_start.x *
				 resolution_info->LCD_WIDTH  / (resolution_info->max_x);
	gesture->Point_start.y = gesture->Point_start.y *
				 resolution_info->LCD_HEIGHT / (resolution_info->max_y);
	gesture->Point_end.x   = gesture->Point_end.x   *
				 resolution_info->LCD_WIDTH  / (resolution_info->max_x);
	gesture->Point_end.y   = gesture->Point_end.y   *
				 resolution_info->LCD_HEIGHT / (resolution_info->max_y);
	gesture->Point_1st.x   = gesture->Point_1st.x   *
				 resolution_info->LCD_WIDTH  / (resolution_info->max_x);
	gesture->Point_1st.y   = gesture->Point_1st.y   *
				 resolution_info->LCD_HEIGHT / (resolution_info->max_y);
	gesture->Point_2nd.x   = gesture->Point_2nd.x   *
				 resolution_info->LCD_WIDTH  / (resolution_info->max_x);
	gesture->Point_2nd.y   = gesture->Point_2nd.y   *
				 resolution_info->LCD_HEIGHT / (resolution_info->max_y);
	gesture->Point_3rd.x   = gesture->Point_3rd.x   *
				 resolution_info->LCD_WIDTH  / (resolution_info->max_x);
	gesture->Point_3rd.y   = gesture->Point_3rd.y   *
				 resolution_info->LCD_HEIGHT / (resolution_info->max_y);
	gesture->Point_4th.x   = gesture->Point_4th.x   *
				 resolution_info->LCD_WIDTH  / (resolution_info->max_x);
	gesture->Point_4th.y   = gesture->Point_4th.y   *
				 resolution_info->LCD_HEIGHT / (resolution_info->max_y);
}

static void tp_gesture_handle(struct touchpanel_data *ts)
{
	struct gesture_info gesture_info_temp;
	int retval = 0;
	int index = 0;
	struct touchpanel_data *g_ts = NULL;

	if (((!ts->ts_ops->get_gesture_info) && (!ts->enable_point_auto_change))
	    || ((!ts->ts_ops->get_gesture_info_auto) && ts->enable_point_auto_change)) {
		TP_INFO(ts->tp_index, "not support ts->ts_ops->get_gesture_info callback\n");
		return;
	}

	memset(&gesture_info_temp, 0, sizeof(struct gesture_info));
	if (!ts->enable_point_auto_change) {
		ts->ts_ops->get_gesture_info(ts->chip_data, &gesture_info_temp);
	} else {
		ts->ts_ops->get_gesture_info_auto(ts->chip_data, &gesture_info_temp, &ts->resolution_info);
	}
	tp_geture_info_transform(&gesture_info_temp, &ts->resolution_info);

	TP_INFO(ts->tp_index, "detect %s gesture\n",
		gesture_info_temp.gesture_type == DOU_TAP ? "double tap" :
		gesture_info_temp.gesture_type == UP_VEE ? "up vee" :
		gesture_info_temp.gesture_type == DOWN_VEE ? "down vee" :
		gesture_info_temp.gesture_type == LEFT_VEE ? "(>)" :
		gesture_info_temp.gesture_type == RIGHT_VEE ? "(<)" :
		gesture_info_temp.gesture_type == CIRCLE_GESTURE ? "circle" :
		gesture_info_temp.gesture_type == DOU_SWIP ? "(||)" :
		gesture_info_temp.gesture_type == LEFT2RIGHT_SWIP ? "(-->)" :
		gesture_info_temp.gesture_type == RIGHT2LEFT_SWIP ? "(<--)" :
		gesture_info_temp.gesture_type == UP2DOWN_SWIP ? "up to down |" :
		gesture_info_temp.gesture_type == DOWN2UP_SWIP ? "down to up |" :
		gesture_info_temp.gesture_type == M_GESTRUE ? "(M)" :
		gesture_info_temp.gesture_type == W_GESTURE ? "(W)" :
		gesture_info_temp.gesture_type == FINGER_PRINTDOWN ? "(fingerprintdown)" :
		gesture_info_temp.gesture_type == FRINGER_PRINTUP ? "(fingerprintup)" :
		gesture_info_temp.gesture_type == SINGLE_TAP ? "single tap" :
		gesture_info_temp.gesture_type == HEART ? "heart" :
		gesture_info_temp.gesture_type == PENDETECT ? "(pen detect)" :
		gesture_info_temp.gesture_type == S_GESTURE ? "(S)" : "unknown");

	if (gesture_info_temp.gesture_type != UNKOWN_GESTURE
	    && gesture_info_temp.gesture_type != FINGER_PRINTDOWN
	    && gesture_info_temp.gesture_type != FRINGER_PRINTUP) {
		retval = tp_memcpy(&ts->gesture, sizeof(ts->gesture), \
			  &gesture_info_temp, sizeof(struct gesture_info), \
			  sizeof(struct gesture_info));
		if (retval < 0) {
		    TPD_INFO("tp_memcpy failed.\n");
		}

		for (index = 0; index < TP_SUPPORT_MAX; index++) {
			g_ts = get_ts_data(index);
			if (g_ts) {
				g_ts->gesture.gesture_panel_id = ts->tp_index;
			}
		}

		input_report_key(ts->input_dev, KEY_F4, 1);
		input_sync(ts->input_dev);
		input_report_key(ts->input_dev, KEY_F4, 0);
		input_sync(ts->input_dev);

	} else if (gesture_info_temp.gesture_type == FINGER_PRINTDOWN) {
		ts->fp_info.touch_state = 1;
		ts->fp_info.x = gesture_info_temp.Point_start.x;
		ts->fp_info.y = gesture_info_temp.Point_start.y;
		TP_INFO(ts->tp_index, "screen off down : (%d, %d)\n", ts->fp_info.x, ts->fp_info.y);
	} else if (gesture_info_temp.gesture_type == FRINGER_PRINTUP) {
		ts->fp_info.touch_state = 0;
		ts->fp_info.x = gesture_info_temp.Point_start.x;
		ts->fp_info.y = gesture_info_temp.Point_start.y;
		TP_INFO(ts->tp_index, "screen off up : (%d, %d)\n", ts->fp_info.x, ts->fp_info.y);
	}
}

static void tp_exception_handle(struct touchpanel_data *ts)
{
	if (!ts->ts_ops->reset) {
		TP_INFO(ts->tp_index, "not support ts->ts_ops->reset callback\n");
		return;
	}

	ts->ts_ops->reset(
		ts->chip_data);    /* after reset, all registers set to default*/
	operate_mode_switch(ts);

	tp_btnkey_release(ts);
	tp_touch_release(ts);

	if (ts->fingerprint_underscreen_support) {
		ts->fp_info.touch_state = 0;
	}
}

static void tp_config_handle(struct touchpanel_data *ts)
{
	int ret = 0;

	if (!ts->ts_ops->fw_handle) {
		TP_INFO(ts->tp_index, "not support ts->ts_ops->fw_handle callback\n");
		return;
	}

	ret = ts->ts_ops->fw_handle(ts->chip_data);
}

void tp_fw_auto_reset_handle(struct touchpanel_data *ts)
{
	TP_INFO(ts->tp_index, "%s\n", __func__);

	operate_mode_switch(ts);

	tp_btnkey_release(ts);
	tp_touch_release(ts);
}

static inline void tp_work_func(struct touchpanel_data *ts)
{
	u32 cur_event = 0;

	if (!ts->ts_ops->trigger_reason) {
		TP_INFO(ts->tp_index, "not support ts_ops->trigger_reason callback\n");
		return;
	}

	/*
	 *  trigger_reason:this callback determine which trigger reason should be
	 *  The value returned has some policy!
	 *  1.IRQ_EXCEPTION /IRQ_GESTURE /IRQ_IGNORE /IRQ_FW_CONFIG --->should be only reported  individually
	 *  2.IRQ_TOUCH && IRQ_BTN_KEY --->should depends on real situation && set correspond bit on trigger_reason
	 */
	if (ts->ts_ops->trigger_reason) {
		cur_event = ts->ts_ops->trigger_reason(ts->chip_data, (ts->gesture_enable
						       || ts->fp_enable), ts->is_suspended);
	}

	if (CHK_BIT(cur_event, IRQ_TOUCH) || CHK_BIT(cur_event, IRQ_BTN_KEY)
		|| CHK_BIT(cur_event, IRQ_FW_HEALTH) || CHK_BIT(cur_event, IRQ_PEN) || \
		CHK_BIT(cur_event, IRQ_FACE_STATE) || CHK_BIT(cur_event, IRQ_FINGERPRINT)
		|| CHK_BIT(cur_event, IRQ_PALM) || CHK_BIT(cur_event, IRQ_PEN_REPORT)) {
		if (CHK_BIT(cur_event, IRQ_BTN_KEY)) {
			tp_btnkey_handle(ts);
		}

		if (CHK_BIT(cur_event, IRQ_PALM)
			&& ts->palm_to_sleep_enable && !ts->is_suspended) {
			tp_palm_to_sleep_inScreenLock(ts);
		}

		if (CHK_BIT(cur_event, IRQ_TOUCH)) {
			tp_touch_handle(ts);
		}

		if (CHK_BIT(cur_event, IRQ_FACE_STATE) && ts->fd_enable) {
			tp_face_detect_handle(ts);
		}

		if (CHK_BIT(cur_event, IRQ_FINGERPRINT) && ts->fp_enable) {
			tp_fingerprint_handle(ts);
		}

	} else if (CHK_BIT(cur_event, IRQ_GESTURE)) {
		tp_gesture_handle(ts);

	} else if (CHK_BIT(cur_event, IRQ_EXCEPTION)) {
		tp_exception_handle(ts);

	} else if (CHK_BIT(cur_event, IRQ_FW_CONFIG)) {
		tp_config_handle(ts);

	} else if (CHK_BIT(cur_event, IRQ_FW_AUTO_RESET)) {
		tp_fw_auto_reset_handle(ts);

	} else {
		TPD_DEBUG("unknown irq trigger reason\n");
	}
}

static irqreturn_t tp_irq_thread_fn(int irq, void *dev_id)
{
	struct touchpanel_data *ts = (struct touchpanel_data *)dev_id;
	ts->monitor_data.irq_to_report_timer = ktime_get();

	if (ts->ws) {
		__pm_stay_awake(ts->ws);
	}

	if (ts->ts_ops->tp_irq_throw_away) {
		if (ts->ts_ops->tp_irq_throw_away(ts->chip_data)) {
			goto exit;
		}
	}

	/*for stop system go to sleep*/
	/*wake_lock_timeout(&ts->wakelock, 1*HZ);*/

	/*for check bus i2c/spi is ready or not*/
	if (ts->bus_ready == false) {
		/*TP_INFO(ts->tp_index, "Wait device resume!");*/
		wait_event_interruptible_timeout(ts->wait,
						 ts->bus_ready,
						 msecs_to_jiffies(ts->irq_need_dev_resume_time));
		TP_INFO(ts->tp_index, "Device maybe resume!");
	}

	if (false == monitor_irq_bus_ready(ts)) {
		goto exit;
	}

	/*for some ic such as samsung ic*/
	if (ts->sec_long_low_trigger) {
		disable_irq_nosync(ts->irq);
	}

	/*for normal ic*/
	if (ts->int_mode == BANNABLE) {
		mutex_lock(&ts->mutex);
		/*Only if the following four conditions are met at the same time:
		 tp is suspend && gesture is disable && irq flag is edge-triggered && disable_suspend_irq_handler is set to 1,
		tp_work_func(ts)  will be ignored */
		if (!ts->is_suspended || (TP_ALL_GESTURE_SUPPORT && TP_ALL_GESTURE_ENABLE) ||
				!(ts->irq_flags & (IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING)) ||
				!(ts->disable_suspend_irq_handler)) {
			tp_work_func(ts);
		}
		mutex_unlock(&ts->mutex);

	} else { /*for some ic such as synaptic tcm need get data by interrupt*/
		tp_work_func(ts);
	}

	if (ts->sec_long_low_trigger) {
		enable_irq(ts->irq);
	}

exit:

        if (ts->ws) {
                __pm_relax(ts->ws);
        }

	return IRQ_HANDLED;
}

int tp_register_irq_func(struct touchpanel_data *ts)
{
	int ret = 0;

/* Avoid setting up hardware for TVM during probe */
#ifdef CONFIG_ARCH_QTI_VM
	if (!atomic_read(&ts->delayed_vm_probe_pending)) {
		atomic_set(&ts->delayed_vm_probe_pending, 1);
		return 0;
	}
	goto tvm_setup;
#endif

	if (gpio_is_valid(ts->hw_res.irq_gpio)) {
	TP_DEBUG(ts->tp_index, "%s, irq_gpio is %d, ts->irq is %d\n", __func__,
	ts->hw_res.irq_gpio, ts->irq);

		if (ts->irq_flags_cover) {
			ts->irq_flags = ts->irq_flags_cover;
			TP_BOOT_INFO(ts->tp_index, "%s irq_flags is covered by 0x%x\n", __func__,
				ts->irq_flags_cover);
		}

		if (ts->irq <= 0) {
			ts->irq = gpio_to_irq(ts->hw_res.irq_gpio);
			TP_BOOT_INFO(ts->tp_index, "%s ts->irq is %d\n", __func__, ts->irq);
		}

		snprintf(ts->irq_name, sizeof(ts->irq_name), "touch-%02d", ts->tp_index);
		ret = devm_request_threaded_irq(ts->dev, ts->irq, NULL,
						tp_irq_thread_fn,
						ts->irq_flags | IRQF_ONESHOT,
						ts->irq_name, ts);

		if (ret < 0) {
			TP_INFO(ts->tp_index, "%s request_threaded_irq ret is %d\n", __func__, ret);
		}

	} else {
		TP_INFO(ts->tp_index, "%s:no valid irq\n", __func__);
		ret = -1;
	}
	return ret;
#ifdef CONFIG_ARCH_QTI_VM
tvm_setup:
	TP_INFO(ts->tp_index, "%s ts->irq is %d\n", __func__, ts->irq);
	snprintf(ts->irq_name, sizeof(ts->irq_name), "touch-%02d", ts->irq);
	ret = devm_request_threaded_irq(ts->dev, ts->irq, NULL,
				tp_irq_thread_fn,
				ts->irq_tui_flags | IRQF_ONESHOT,
				ts->irq_name, ts);
	return 0;
#endif
}

/**
 * speedup_resume - speedup resume thread process
 * @work: work struct using for this thread
 *
 * do actully resume function
 * Do not care the result: Return void type
 */
static void speedup_resume(struct work_struct *work)
{
	int ret = 0;
	struct specific_resume_data specific_resume_data;
	struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
				     speed_up_work);

	TP_INFO(ts->tp_index, "%s is called\n", __func__);
	if (ts->tp_wakelock) {
		__pm_stay_awake(ts->tp_wakelock);
	}

	/*step1: get mutex for locking i2c acess flow*/
	mutex_lock(&ts->mutex);

	/*step2:before Resume clear All of touch/key event Reset some flag to default satus*/
	tp_btnkey_release(ts);
	tp_touch_release(ts);

	if (ts->force_bus_ready_support && (false == ts->bus_ready)) {
		TP_INFO(ts->tp_index, "%s force bus_ready to true\n", __func__);
		ts->bus_ready = true;
	}

	if (!(ts->tp_ic_type == TYPE_TDDI_TCM && ts->is_noflash_ic)) {
		if (ts->int_mode == UNBANNABLE) {
			tp_register_irq_func(ts);
		}
	}

	if (ts->ts_ops->speed_up_resume_prepare) {
		ts->ts_ops->speed_up_resume_prepare(ts->chip_data);
	}

	if (ts->ts_ops->specific_resume_operate && !ts->fp_info.touch_state) {
		specific_resume_data.suspend_state = ts->suspend_state;
		ret =  ts->ts_ops->specific_resume_operate(ts->chip_data,
				&specific_resume_data);

		if (ret < 0) {
			goto EXIT;
		}
	}

	/*when fingerprint quick start featrue on ,need keep fp_enable = 1 befor tp resume,now need turn off*/
	if(ts->fp_disable_after_resume && ts->fp_quick_start_data == 1) {
		ts->fp_quick_start_data = 0;
		ts->fp_enable = 0;
	}

	operate_mode_switch(ts);

	if (ts->esd_handle_support) {
		esd_handle_switch(&ts->esd_info, true);
	}

	/*step6:Request irq again*/
	if (!(ts->tp_ic_type == TYPE_TDDI_TCM && ts->is_noflash_ic)) {
		if (ts->int_mode == BANNABLE) {
			tp_register_irq_func(ts);
		}
	}

EXIT:
	ts->suspend_state = TP_SPEEDUP_RESUME_COMPLETE;

	/*step7:Unlock  && exit*/
	TP_INFO(ts->tp_index, "%s: end!\n", __func__);
	mutex_unlock(&ts->mutex);
	if (ts->tp_wakelock) {
		__pm_relax(ts->tp_wakelock);
	}
}

/**
 * touchpanel_ts_suspend - touchpanel suspend function
 * @dev: i2c_client->dev using to get touchpanel_data resource
 *
 * suspend function bind to LCD on/off status
 * Returning zero(sucess) or negative errno(failed)
 */

static void tp_suspend_direct(struct touchpanel_data *ts)
{
	int ret;

	TP_INFO(ts->tp_index, "tp_suspend ts->bus_ready =%d\n", ts->bus_ready);

	/*
	* block this process to wait for exiting tvm mode
	* and return to pvm successfully tp prevent crash dump
	* caused by accessing spi resources
	*/

	/*step1:detect whether we need to do suspend*/
	if (ts->input_dev == NULL) {
		TP_INFO(ts->tp_index, "input_dev  registration is not complete\n");
		goto EXIT;
	}

	if (ts->loading_fw) {
		TP_INFO(ts->tp_index, "FW is updating while suspending");
		goto EXIT;
	}

	/* release all complete first */
	if (!ts->skip_reinit_device_support) {
		if (ts->tp_ic_type == TYPE_TDDI_TCM) {
			if (ts->ts_ops->reinit_device) {
				if (ts->bus_type != TP_BUS_I2C) {
					if (!ts->is_suspended) {
						ts->ts_ops->reinit_device(ts->chip_data);
					}
				} else {
					ts->ts_ops->reinit_device(ts->chip_data);
				}
			}
		}
	}

	/*step2:get mutex && start process suspend flow*/
	mutex_lock(&ts->mutex);

	if (!ts->is_suspended) {
		ts->is_suspended = 1;
		ts->suspend_state = TP_SUSPEND_COMPLETE;

	} else {
		TP_INFO(ts->tp_index, "%s: do not suspend twice.\n", __func__);
		goto EXIT;
	}

	/*step3:Release key && touch event before suspend*/
	tp_btnkey_release(ts);
	tp_touch_release(ts);

	/*step4:cancel esd test*/
	if (ts->esd_handle_support) {
		esd_handle_switch(&ts->esd_info, false);
	}

	ts->rate_ctrl_level = 0;

	if (!ts->is_incell_panel || (ts->black_gesture_support
				     && ts->gesture_enable > 0)) {
		/*step5:gamde mode support*/
		if (ts->game_switch_support) {
			ts->ts_ops->mode_switch(ts->chip_data, MODE_GAME, false);
		}

		if (ts->report_rate_white_list_support && ts->ts_ops->rate_white_list_ctrl) {
			ts->ts_ops->rate_white_list_ctrl(ts->chip_data, 0);
		}

		if (ts->face_detect_support && ts->fd_enable) {
			ts->ts_ops->mode_switch(ts->chip_data, MODE_FACE_DETECT, false);
		}
	}

	/*step6:finger print support handle*/
	if (ts->fingerprint_underscreen_support) {
		operate_mode_switch(ts);
		if (!ts->fingerprint_not_report_in_suspend) {
			ts->fp_info.touch_state = 0;
		} else {
			TP_INFO(ts->tp_index, "%s, not report fp up for S3908 & S3910\n", __func__);
		}
		goto EXIT;
	}

	/*step for suspend_gesture_cfg when ps is near ts->gesture_enable == 2*/
	if (ts->suspend_gesture_cfg && ts->black_gesture_support
	    && ts->gesture_enable == 2) {
		ts->ts_ops->mode_switch(ts->chip_data, MODE_GESTURE, true);
		operate_mode_switch(ts);
		goto EXIT;
	}

	/*step8:skip suspend operate only when gesture_enable is 0*/
	if (ts->skip_suspend_operate && (!ts->gesture_enable)) {
		goto EXIT;
	}

	/*step9:switch mode to sleep*/
	ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_SLEEP, true);

	if (ret < 0) {
		TP_INFO(ts->tp_index, "%s, Touchpanel operate mode switch failed\n", __func__);
	}

EXIT:

	TP_INFO(ts->tp_index, "%s: end.\n", __func__);
	mutex_unlock(&ts->mutex);
}

static void tp_suspend_work(struct work_struct *work)
{
	struct touchpanel_data *ts = NULL;

	ts = container_of(work, struct touchpanel_data, suspend_work);
	TP_INFO(ts->tp_index, "%s: start.\n", __func__);

	tp_suspend_direct(ts);
}

static void tp_delta_read_triggered_by_key_handle(struct work_struct *work)
{
	struct debug_info_proc_operations *debug_info_ops;
	struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
				key_trigger_work);

	TPD_INFO("%s:tp_debug= %d\n", __func__, tp_debug);

	if (tp_debug != 2)
		return;

	if (!ts)
		return;

	debug_info_ops = (struct debug_info_proc_operations *)ts->debug_info_ops;

	if (!debug_info_ops)
		return;
	if (!debug_info_ops->key_trigger_delta_read) {
		return;
	}
	if (!debug_info_ops->delta_read) {
		return;
	}

	if (ts->int_mode == BANNABLE) {
		disable_irq_nosync(ts->irq);
	}
	mutex_lock(&ts->mutex);
	debug_info_ops->key_trigger_delta_read(ts->chip_data);
	mutex_unlock(&ts->mutex);
	if (ts->int_mode == BANNABLE) {
	enable_irq(ts->irq);
	}
	return;
}

static void esd_handle_func(struct work_struct *work)
{
	int ret = 0;
	int gpio_en, regulator_en, regulator_vol = 0;
	struct touchpanel_data *ts = container_of(work, struct touchpanel_data,
				     esd_info.esd_check_work.work);

	if (ts->loading_fw) {
		TP_INFO(ts->tp_index, "FW is updating, stop esd handle!\n");
		return;
	}

	mutex_lock(&ts->esd_info.esd_lock);

	if (!ts->esd_info.esd_running_flag) {
		TP_INFO(ts->tp_index, "Esd protector has stopped!\n");
		goto ESD_END;
	}

	if (ts->is_suspended == 1) {
		TP_INFO(ts->tp_index, "Touch panel has suspended!\n");
		goto ESD_END;
	}

	if (!ts->ts_ops->esd_handle) {
		TP_INFO(ts->tp_index, "not support ts_ops->esd_handle callback\n");
		goto ESD_END;
	}

	ret = ts->ts_ops->esd_handle(ts->chip_data);

	if (ts->monitor_data.health_simulate_trigger
		   || ret == -1) {    /*-1 means esd hanppened: handled in IC part, recovery the state here*/
		operate_mode_switch(ts);
	}

	if (!IS_ERR_OR_NULL(ts->hw_res.avdd) && (regulator_get_voltage(ts->hw_res.avdd) != -EINVAL)) {
		regulator_en = regulator_is_enabled(ts->hw_res.avdd);
		regulator_vol = regulator_get_voltage(ts->hw_res.avdd);
		if (!regulator_en || regulator_vol < AVDD_VOLTAGE_LIMIT_MIN || regulator_vol > AVDD_VOLTAGE_LIMIT_MAX
				   || ts->monitor_data.health_simulate_trigger) {
			TP_INFO(ts->tp_index, "avdd regulator enabled=%d, voltage=%d\n", regulator_en, regulator_vol);
			regulator_vol = regulator_en ? regulator_vol : VOLTAGE_STATE_REGULATOR_DISABLED;
		}
	}
	if (ts->hw_res.enable_avdd_gpio > 0) {
		gpio_en = gpio_get_value(ts->hw_res.enable_avdd_gpio);
		if (!gpio_en || ts->monitor_data.health_simulate_trigger) {
			TPD_INFO("avdd gpio is %d\n", gpio_en);
			regulator_vol = VOLTAGE_STATE_ENABLE_GPIO_LOW;
		}
	}

	if (!IS_ERR_OR_NULL(ts->hw_res.vddi) && (regulator_get_voltage(ts->hw_res.vddi) != -EINVAL)) {
		regulator_en = regulator_is_enabled(ts->hw_res.vddi);
		regulator_vol = regulator_get_voltage(ts->hw_res.vddi);
		if (!regulator_en || regulator_vol < VDDI_VOLTAGE_LIMIT_MIN || regulator_vol > VDDI_VOLTAGE_LIMIT_MAX
				   || ts->monitor_data.health_simulate_trigger) {
			TP_INFO(ts->tp_index, "vddi regulator enabled=%d, voltage=%d\n", regulator_en, regulator_vol);
			regulator_vol = regulator_en ? regulator_vol : VOLTAGE_STATE_REGULATOR_DISABLED;
		}
	}
	if (ts->hw_res.enable_vddi_gpio > 0) {
		gpio_en = gpio_get_value(ts->hw_res.enable_vddi_gpio);
		if (!gpio_en || ts->monitor_data.health_simulate_trigger) {
			TPD_INFO("vddi gpio is %d\n", gpio_en);
			regulator_vol = VOLTAGE_STATE_ENABLE_GPIO_LOW;
		}
	}

	if (ts->up_status && gpio_is_valid(ts->hw_res.irq_gpio)) {
		gpio_en = gpio_get_value(ts->hw_res.irq_gpio);
		if (!gpio_en || ts->monitor_data.health_simulate_trigger) {
			TPD_INFO("irq gpio is %d\n", gpio_en);
		}
	}

	if (ts->esd_info.esd_running_flag) {
		queue_delayed_work(ts->esd_info.esd_workqueue, &ts->esd_info.esd_check_work,
				   ts->esd_info.esd_work_time);

	} else {
		TP_INFO(ts->tp_index, "Esd protector suspended!");
	}

ESD_END:
	mutex_unlock(&ts->esd_info.esd_lock);
	return;
}

/**
 * register_common_touch_device - parse dts, get resource defined in Dts
 * @pdata: touchpanel_data, using for common driver
 *
 * entrance of common touch Driver
 * Returning zero(sucess) or negative errno(failed)
 */
int register_common_touch_device(struct touchpanel_data *pdata)
{
	struct touchpanel_data *ts = pdata;
	char name[TP_NAME_SIZE_MAX];
	int ret = -1;
	int i = 0;
	TPD_BOOT_INFO("%s  is called\n", __func__);

	if (!ts->dev) {
		return -1;
	}

	/*step1 : dts parse*/
	ret = init_parse_dts(ts->dev, ts);

	if (ret < 0) {
		TP_INFO(ts->tp_index, "%s: dts init failed.\n", __func__);
		return -1;
	}


	/*step3 : interfaces init*/
	init_touch_interfaces(ts->dev, ts->register_is_16bit);
	// lcd_tp_refresh_support support
	if (ts->lcd_tp_refresh_support) {
		ts->tp_refresh_wq = create_singlethread_workqueue("tp_refresh_wq");
		if (!ts->tp_refresh_wq) {
		return -1;
		}
		INIT_WORK(&ts->tp_refresh_work, lcd_tp_refresh_work);
	}

	/*step4 : mutex init*/
	mutex_init(&ts->mutex);
	mutex_init(&ts->report_mutex);
	init_completion(&ts->fw_complete);
	init_waitqueue_head(&ts->wait);
	ts->com_api_data.tp_irq_disable = 1;
	/*wake_lock_init(&ts->wakelock, WAKE_LOCK_SUSPEND, "tp_wakelock");*/
	/*step5 : power init*/
	if(strcmp(ts->touch_environment, "tvm") != 0) {
		ret = tp_power_init(ts);
		if (ret < 0) {
			return ret;
		}

		ret = oplus_gpio_resume(ts);
		if (ret < 0) {
			return ret;
		}

		/*step6 : I2C function check*/
		if ((!ts->is_noflash_ic) && (ts->bus_type == TP_BUS_I2C)) {
				if (!i2c_check_functionality(ts->client->adapter, I2C_FUNC_I2C)) {
					TP_INFO(ts->tp_index, "%s: need I2C_FUNC_I2C\n", __func__);
					ret = -ENODEV;
					goto err_check_functionality_failed;
				}
		}
	}
	/*step7 : touch input dev init*/
	ret = init_input_device(ts);

	if (ret < 0) {
		ret = -EINVAL;
		TP_INFO(ts->tp_index, "tp_input_init failed!\n");
		goto err_check_functionality_failed;
	}

	/*step8 : irq request setting*/
	if (ts->int_mode == UNBANNABLE) {
		ret = tp_register_irq_func(ts);

		if (ret < 0) {
			goto err_check_functionality_failed;
		}

		ts->bus_ready = true;
	}

	/*step9 : panel data init*/
	ret = tp_paneldata_init(ts);

	if (ret < 0) {
		goto err_check_functionality_failed;
	}

	if (strcmp(ts->touch_environment, "tvm") != 0) {
		/*step11:get chip info*/
		if (!ts->ts_ops->get_chip_info) {
			ret = -EINVAL;
			TP_INFO(ts->tp_index, "tp get_chip_info NULL!\n");
			goto err_check_functionality_failed;
		}

		ret = ts->ts_ops->get_chip_info(ts->chip_data);

		if (ret < 0) {
			ret = -EINVAL;
			TP_INFO(ts->tp_index, "tp get_chip_info failed!\n");
			goto err_check_functionality_failed;
		}

		/*step12 : touchpanel Fw check*/
		if (!ts->is_noflash_ic) {           /*noflash don't have firmware before fw update*/
			if (!ts->ts_ops->fw_check) {
				ret = -EINVAL;
				TP_INFO(ts->tp_index, "tp fw_check NULL!\n");
				goto err_check_functionality_failed;
			}

			ret = ts->ts_ops->fw_check(ts->chip_data, &ts->resolution_info,
						   &ts->panel_data);
			if (ret == FW_ABNORMAL) {
				ts->force_update = 1;
				TP_BOOT_INFO(ts->tp_index, "This FW need to be updated!\n");

			} else {
				ts->force_update = 0;
			}
		}

		/*step13 : enable touch ic irq output ability*/
		if (!ts->ts_ops->mode_switch) {
			ret = -EINVAL;
			TP_INFO(ts->tp_index, "tp mode_switch NULL!\n");
			goto err_check_functionality_failed;
		}

		ret = ts->ts_ops->mode_switch(ts->chip_data, MODE_NORMAL, true);

		if (ret < 0) {
			ret = -EINVAL;
			TP_INFO(ts->tp_index, "%s:modem switch failed!\n", __func__);
			/*goto err_check_functionality_failed;*/   /*mode swith fail don't goto end, still keep going upgrade*/
		}
	}
	/*step14 : irq request setting*/
	if (ts->int_mode == BANNABLE) {
		ret = tp_register_irq_func(ts);

		if (ret < 0) {
			goto err_check_functionality_failed;
		}
	}

	/*step15 : suspend && resume fuction register*/

	if (strcmp(ts->touch_environment, "tvm") != 0) {
#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
		if (ts->active_panel) {
			cookie = panel_event_notifier_register(GET_INDEX(ts->tp_index),
					GET_TOUCH(ts->tp_index), ts->active_panel,
					&ts_panel_notifier_callback, ts);

			if (!cookie) {
				TP_INFO(ts->tp_index, "Unable to register fb_notifier: %d\n", ret);
				goto err_check_functionality_failed;
			}
			ts->notifier_cookie = cookie;
		}
#endif

	}

	/*step16 : workqueue create(speedup_resume)*/
	snprintf(name, TP_NAME_SIZE_MAX, "sp_resume%d", ts->tp_index);
	ts->speedup_resume_wq = create_singlethread_workqueue(name);

	if (!ts->speedup_resume_wq) {
		ret = -ENOMEM;
	}

	INIT_WORK(&ts->speed_up_work, speedup_resume);

	if (ts->suspend_work_support) {
		snprintf(name, TP_NAME_SIZE_MAX, "suspend_wq%d", ts->tp_index);
		ts->suspend_wq = create_singlethread_workqueue(name);
		if (!ts->suspend_wq) {
			ret = -ENOMEM;
			goto error_speedup_resume_wq;
		}
		INIT_WORK(&ts->suspend_work, tp_suspend_work);
	}

	/*create workqueue for key trigger */
	snprintf(name, TP_NAME_SIZE_MAX, "volume_key_trigger%d", ts->tp_index);
	ts->key_trigger_wq = create_singlethread_workqueue(name);

	if (!ts->key_trigger_wq) {
		ret = -ENOMEM;
	}

	INIT_WORK(&ts->key_trigger_work, tp_delta_read_triggered_by_key_handle);

	/*step 18:esd recover support*/
	if (ts->esd_handle_support) {
		snprintf(name, TP_NAME_SIZE_MAX, "esd_workthread%d", ts->tp_index);
		ts->esd_info.esd_workqueue = create_singlethread_workqueue(name);

		if (!ts->esd_info.esd_workqueue) {
			ret = -ENOMEM;
			goto error_tp_fw_wq;
		}

		INIT_DELAYED_WORK(&ts->esd_info.esd_check_work, esd_handle_func);

		mutex_init(&ts->esd_info.esd_lock);

		ts->esd_info.esd_running_flag = 0;
		ts->esd_info.esd_work_time = 2 *
					     HZ; /* HZ: clock ticks in 1 second generated by system*/
		TP_BOOT_INFO(ts->tp_index, "Clock ticks for an esd cycle: %d\n",
			ts->esd_info.esd_work_time);

		esd_handle_switch(&ts->esd_info, true);
	}

	/*step 19:frequency hopping simulate support*/
	if (ts->freq_hop_simulate_support) {
		snprintf(name, TP_NAME_SIZE_MAX, "syna_tcm_freq_hop%d", ts->tp_index);
		ts->freq_hop_info.freq_hop_workqueue = create_singlethread_workqueue(name);

		if (!ts->freq_hop_info.freq_hop_workqueue) {
			ret = -ENOMEM;
			goto error_esd_wq;
		}

		ts->freq_hop_info.freq_hop_simulating = false;
		ts->freq_hop_info.freq_hop_freq = 0;
	}


	/*step 23 : Other*****/
        ts->ws = wakeup_source_register(ts->dev,dev_name(ts->dev));
	ts->tp_wakelock = wakeup_source_register(ts->dev, "tp_wakelock");

	ts->bus_ready = true;
	ts->loading_fw = false;
	ts->is_suspended = 0;
	ts->suspend_state = TP_SPEEDUP_RESUME_COMPLETE;
	ts->gesture_enable = 0;
	ts->fd_enable = 0;
	ts->fp_enable = 0;
	ts->fp_info.touch_state = 0;
	ts->palm_enable = 1;
	ts->touch_count = 0;
	ts->view_area_touched = 0;
	ts->tp_suspend_order = LCD_TP_SUSPEND;
	ts->tp_resume_order = TP_LCD_RESUME;
	ts->skip_suspend_operate = false;
	ts->skip_reset_in_resume = false;
	ts->irq_slot = 0;
	ts->firmware_update_type = 0;
	ts->major_rate_limit_times = 0;
	ts->palm_to_sleep_enable = 0;
	ts->tp_ic_touch_num = 0;
	ts->last_tp_ic_touch_num = 0;
	for (i = 0; i < MAX_FINGER_NUM; i++) {
		ts->last_x_y_point[i].x = 0;
		ts->last_x_y_point[i].y = 0;
	}

	if (ts->is_noflash_ic || ts->bus_type == TP_BUS_SPI) {
		ts->irq = ts->s_client->irq;
	} else {
		ts->irq = ts->client->irq;
	}


	mutex_lock(&tp_core_lock);
	g_tp[ts->tp_index] = ts;
	mutex_unlock(&tp_core_lock);
	TP_BOOT_INFO(ts->tp_index, "Touch panel probe : normal end\n");
	return 0;

error_esd_wq:

	if (ts->esd_handle_support) {
		destroy_workqueue(ts->esd_info.esd_workqueue);
	}

error_tp_fw_wq:


error_speedup_resume_wq:

	if (ts->speedup_resume_wq) {
		destroy_workqueue(ts->speedup_resume_wq);
	}


err_check_functionality_failed:
#ifndef CONFIG_ARCH_QTI_VM
	ts->ts_ops->power_control(ts->chip_data, false);
	if (gpio_is_valid(ts->hw_res.cs_gpio)) {
		gpio_free(ts->hw_res.cs_gpio);
	}
#endif
	/*wake_lock_destroy(&ts->wakelock);*/
	ret = -1;
	return ret;
}

void unregister_common_touch_device(struct touchpanel_data *pdata)
{
	struct touchpanel_data *ts = pdata;

	if (!pdata) {
		return;
	}

	/*step1 :free irq*/
	devm_free_irq(ts->dev, ts->irq, ts);

	/*step3 :free the hw resource*/
	pdata->ts_ops->power_control(ts->chip_data, false);

	/*lcd_tp_refresh_support*/
	if (ts->lcd_tp_refresh_support) {
		if (ts->tp_refresh_wq) {
			flush_workqueue(ts->tp_refresh_wq);
			destroy_workqueue(ts->tp_refresh_wq);
		}
	}

	/*step4:frequency hopping simulate support*/
	if (ts->freq_hop_simulate_support) {
		if (ts->freq_hop_info.freq_hop_workqueue) {
			cancel_delayed_work_sync(&ts->freq_hop_info.freq_hop_work);
			flush_workqueue(ts->freq_hop_info.freq_hop_workqueue);
			destroy_workqueue(ts->freq_hop_info.freq_hop_workqueue);
		}
	}

	/*step5:esd recover support*/
	if (ts->esd_handle_support) {
		esd_handle_switch(&ts->esd_info, false);

		if (ts->esd_info.esd_workqueue) {
			cancel_delayed_work_sync(&ts->esd_info.esd_check_work);
			flush_workqueue(ts->esd_info.esd_workqueue);
			destroy_workqueue(ts->esd_info.esd_workqueue);
		}

		mutex_destroy(&ts->esd_info.esd_lock);
	}

	/*step6 : workqueue create(speedup_resume)*/
	if (ts->speedup_resume_wq) {
		cancel_work_sync(&ts->speed_up_work);
		flush_workqueue(ts->speedup_resume_wq);
		destroy_workqueue(ts->speedup_resume_wq);
	}

	if (ts->suspend_work_support) {
		if (ts->suspend_wq) {
			cancel_work_sync(&ts->suspend_work);
			flush_workqueue(ts->suspend_wq);
			destroy_workqueue(ts->suspend_wq);
		}
	}

	if (ts->lcd_trigger_load_tp_fw_wq) {
		cancel_work_sync(&ts->lcd_trigger_load_tp_fw_work);
		flush_workqueue(ts->lcd_trigger_load_tp_fw_wq);
		destroy_workqueue(ts->lcd_trigger_load_tp_fw_wq);
	}

	/*step7 : suspend && resume fuction register*/
#if IS_ENABLED(CONFIG_QCOM_PANEL_EVENT_NOTIFIER)
	if (ts->active_panel && ts->notifier_cookie) {
		panel_event_notifier_unregister(ts->notifier_cookie);
	}
#endif/*CONFIG_FB*/

	/*free regulator*/
	if (!IS_ERR_OR_NULL(ts->hw_res.avdd)) {
		regulator_put(ts->hw_res.avdd);
		ts->hw_res.avdd = NULL;
	}

	if (!IS_ERR_OR_NULL(ts->hw_res.vddi)) {
		regulator_put(ts->hw_res.vddi);
		ts->hw_res.vddi = NULL;
	}

	/*free wakeup source*/
	if (ts->ws) {
		wakeup_source_unregister(ts->ws);
	}

	/*wake_lock_destroy(&ts->wakelock);*/
	/*step8 : mutex init*/
	mutex_destroy(&ts->mutex);
}

int common_touch_data_free(struct touchpanel_data *pdata)
{
	if (pdata) {
		if (g_tp[pdata->tp_index]) {
			g_tp[pdata->tp_index] = NULL;
		}
		kfree(pdata);
	}

	return 0;
}

int tp_powercontrol_vddi(struct hw_resource *hw_res, bool on)
{
	int ret = 0;

	if (on) { /* 1v8 power on*/
		if (!IS_ERR_OR_NULL(hw_res->vddi)) {
			TPD_BOOT_INFO("Enable the Regulator vddi.\n");
			ret = regulator_enable(hw_res->vddi);

			if (ret) {
				TPD_INFO("Regulator vcc_i2c enable failed ret = %d\n", ret);
				return ret;
			}
		}

		if (hw_res->enable_vddi_gpio > 0) {
			TPD_BOOT_INFO("Enable the vddi_gpio\n");
			ret = gpio_direction_output(hw_res->enable_vddi_gpio, 1);

			if (ret) {
				TPD_INFO("enable the enable_vddi_gpio failed.\n");
				return ret;
			}
		}

	} else { /* 1v8 power off*/
		if (!IS_ERR_OR_NULL(hw_res->vddi)) {
			TPD_BOOT_INFO("disable the vddi_gpio\n");
			ret = regulator_disable(hw_res->vddi);
			if (ret) {
				TPD_INFO("Regulator vcc_i2c enable failed rc = %d\n", ret);
				return ret;
			}
		}

		if (hw_res->enable_vddi_gpio > 0) {
			TPD_BOOT_INFO("disable the enable_vddi_gpio\n");
			ret = gpio_direction_output(hw_res->enable_vddi_gpio, 0);

			if (ret) {
				TPD_INFO("disable the enable_vddi_gpio failed.\n");
				return ret;
			}
		}
	}

	return 0;
}

int tp_powercontrol_avdd(struct hw_resource *hw_res, bool on)
{
	int ret = 0;

	if (on) { /* 2v8 power on*/
		if (!IS_ERR_OR_NULL(hw_res->avdd)) {
			TPD_BOOT_INFO("Enable the Regulator2v8.\n");
			ret = regulator_enable(hw_res->avdd);

			if (ret) {
				TPD_INFO("Regulator vdd enable failed ret = %d\n", ret);
				return ret;
			}
		}

		if (hw_res->enable_avdd_gpio > 0) {
			TPD_BOOT_INFO("Enable the enable_avdd_gpio, hw_res->enable2v8_gpio is %d\n",
				 hw_res->enable_avdd_gpio);
			ret = gpio_direction_output(hw_res->enable_avdd_gpio, 1);

			if (ret) {
				TPD_INFO("enable the enable_avdd_gpio failed.\n");
				return ret;
			}
		}

	} else { /* 2v8 power off*/
		if (!IS_ERR_OR_NULL(hw_res->avdd)) {
			ret = regulator_disable(hw_res->avdd);

			if (ret) {
				TPD_INFO("Regulator vdd disable failed rc = %d\n", ret);
				return ret;
			}
		}

		if (hw_res->enable_avdd_gpio > 0) {
			TPD_BOOT_INFO("disable the enable_avdd_gpio\n");
			ret = gpio_direction_output(hw_res->enable_avdd_gpio, 0);

			if (ret) {
				TPD_INFO("disable the enable_avdd_gpio failed.\n");
				return ret;
			}
		}
	}

	return ret;
}

/*
 * tp_pm_resume - touchpanel pm resume function
 * @ts: ts using to get touchpanel_data resource
 * resume function is called when system wake up
 * Returning void
 */
void tp_pm_resume(struct touchpanel_data *ts)
{
	if (!ts) {
		return;
	}
	if (TP_ALL_GESTURE_SUPPORT) {
		if (TP_ALL_GESTURE_ENABLE) {
			/*disable gpio wake system through intterrupt*/
			disable_irq_wake(ts->irq);
			goto OUT;
		}
	}

	enable_irq(ts->irq);

OUT:
	ts->bus_ready = true;

	if ((ts->black_gesture_support || ts->fingerprint_underscreen_support)) {
		if ((ts->gesture_enable == 1 || ts->fp_enable)) {
			wake_up_interruptible(&ts->wait);
		}
	}
}

/*
 * tp_pm_suspend - touchpanel pm suspend function
 * @ts: ts using to get touchpanel_data resource
 * suspend function is called when system go to sleep
 * Returning void
 */
void tp_pm_suspend(struct touchpanel_data *ts)
{
	if (!ts) {
		return;
	}

	ts->bus_ready = false;

	if (TP_ALL_GESTURE_SUPPORT) {
		if (TP_ALL_GESTURE_ENABLE) {
			/*enable gpio wake system through interrupt*/
			enable_irq_wake(ts->irq);
			return;
		}
	}

	disable_irq(ts->irq);
}

static int oplus_gpio_suspend(struct touchpanel_data *ts)
{
	int r = 0;

	if (ts->hw_res.pin_spi_mode_suspend != NULL) {
		r = pinctrl_select_state(ts->hw_res.pinctrl, ts->hw_res.pin_spi_mode_suspend);
		if (r < 0) {
			TPD_INFO("Failed to select pin_spi_mode_suspend, r:%d", r);
			goto err_suspend_pinctrl;
		}
		TPD_INFO("success set suspend for pin_spi_mode_suspend");
	}

	if (ts->hw_res.pin_int_sta_suspend != NULL) {
		r = pinctrl_select_state(ts->hw_res.pinctrl, ts->hw_res.pin_int_sta_suspend);
		if (r < 0) {
			TPD_INFO("Failed to select pin_int_sta_suspend, r:%d", r);
			goto err_suspend_pinctrl;
		}
		TPD_INFO("success set suspend for pin_int_sta_suspend");
	}

	if (ts->hw_res.pin_rst_sta_suspend != NULL) {
		r = pinctrl_select_state(ts->hw_res.pinctrl, ts->hw_res.pin_rst_sta_suspend);
		if (r < 0) {
			TPD_INFO("Failed to select pin_rst_sta_suspend, r:%d", r);
			goto err_suspend_pinctrl;
		}
		TPD_INFO("success set suspend for pin_rst_sta_suspend");
	}

err_suspend_pinctrl:
	return r;
}

/*
 * tp_shutdown - touchpanel shutdown function
 * @ts: ts using to get touchpanel_data resource
 * shutdown function is called when power off or reboot
 * Returning void
 */
void tp_shutdown(struct touchpanel_data *ts)
{
	if (!ts) {
		return;
	}


#ifdef CONFIG_QCOM_PANEL_EVENT_NOTIFIER
	TPD_INFO("qcom gki2.0 need to unregister notifier");
	if (ts->active_panel && ts->notifier_cookie) {
		panel_event_notifier_unregister(ts->notifier_cookie);
	}
#endif
	/*step0 :close esd*/
	if (ts->esd_handle_support) {
		esd_handle_switch(&ts->esd_info, false);
	}
	oplus_gpio_suspend(ts);
	/*step1 :free the hw resource*/
	ts->ts_ops->power_control(ts->chip_data, false);
}

struct touchpanel_data *common_touch_data_alloc(void)
{
	return tp_kzalloc(sizeof(struct touchpanel_data), GFP_KERNEL);
}

MODULE_DESCRIPTION("Touchscreen Synaptics Common Interface");
MODULE_LICENSE("GPL");
