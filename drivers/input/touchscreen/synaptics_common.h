/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef SYNAPTICS_H
#define SYNAPTICS_H
#define CONFIG_SYNAPTIC_RED

/*********PART1:Head files**********************/
#include <linux/firmware.h>
#include <linux/rtc.h>
#include <linux/syscalls.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/proc_fs.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/seq_file.h>

extern unsigned int tp_debug;

/*********PART2:Define Area**********************/
#define SYNAPTICS_RMI4_PRODUCT_ID_SIZE 10
#define SYNAPTICS_RMI4_PRODUCT_INFO_SIZE 2

#define DIAGONAL_UPPER_LIMIT  1100
#define DIAGONAL_LOWER_LIMIT  900

#define MAX_RESERVE_SIZE 4
#define MAX_LIMIT_NAME_SIZE 16

#define IMAGE_FILE_MAGIC_VALUE 0x4818472b
#define FLASH_AREA_MAGIC_VALUE 0x7c05e516

#define BOOT_CONFIG_ID "BOOT_CONFIG"
#define APP_CODE_ID "APP_CODE"
#define APP_CONFIG_ID "APP_CONFIG"
#define DISP_CONFIG_ID "DISPLAY"

#define TPD_DEVICE "touchpanel"

#define TPD_BOOT_INFO(a, arg...)  pr_info("[TP]"TPD_DEVICE ": " a, ##arg)
#define TP_BOOT_INFO(index, a, arg...)  pr_info("[TP""%x""]"TPD_DEVICE": " a, index, ##arg)

#define TPD_INFO(a, arg...)  pr_err("[TP]"TPD_DEVICE ": " a, ##arg)
#define TP_INFO(index, a, arg...)  pr_err("[TP""%x""]"TPD_DEVICE": " a, index, ##arg)

#define TPD_DEBUG(a, arg...)\
do{\
	if (LEVEL_DEBUG == tp_debug)\
		pr_err("[TP]"TPD_DEVICE ": " a, ##arg);\
}while(0)

#define TP_DETAIL(index, a, arg...)\
do{\
	if (LEVEL_BASIC != tp_debug)\
		pr_err("[TP""%x""]"TPD_DEVICE": " a, index, ##arg);\
}while(0)

#define TP_DEBUG(index, a, arg...)\
do{\
	if (LEVEL_DEBUG == tp_debug)\
		pr_err("[TP""%x""]"TPD_DEVICE": " a, index, ##arg);\
}while(0)

#define TP_SPECIFIC_PRINT(index, count, a, arg...)\
do{\
	if (count++ == 150 || LEVEL_DEBUG == tp_debug) {\
		TPD_INFO(TPD_DEVICE"%x"": " a, index, ##arg);\
		count = 0;\
	}\
}while(0)


/*********PART3:Struct Area**********************/
#define SIMULATE_DEBUG_INFO 0xff
typedef enum {
	BASE_NEGATIVE_FINGER = 0x02,
	BASE_MUTUAL_SELF_CAP = 0x04,
	BASE_ENERGY_RATIO = 0x08,
	BASE_RXABS_BASELINE = 0x10,
	BASE_TXABS_BASELINE = 0x20,
} BASELINE_ERR;

typedef enum {
	BASE_V2_NO_ERROR = 0x00,
	BASE_V2_CLASSIFIER_BL = 0x01,
	BASE_V2_ABS_POSITIVITY_TX = 0x02,
	BASE_V2_ABS_POSITIVITY_RX = 0x03,
	BASE_V2_ENERGY_RATIO = 0x04,
	BASE_V2_BUMPINESS = 0x05,
	BASE_V2_NEGTIVE_FINGER = 0x06,
	BASE_V2_STD_ERROR = 0x07,
	BASE_V2_CRITI_ERROR = 0x08,
	BASE_V2_STD_CRITI = 0x09,
	BASE_V2_METAL_PLATE = 0x0A,
	BASE_V2_WATER_DROP = 0x0B,
	BASE_V2_BIG_ABS_SHIFT = 0x0C,
} BASELINE_ERR_V2; /* used by S3910 */

typedef enum {
	SHIELD_PALM = 0x01,
	SHIELD_GRIP = 0x02,
	SHIELD_METAL = 0x04,
	SHIELD_MOISTURE = 0x08,
	SHIELD_ESD = 0x10,
} SHIELD_MODE;

typedef enum {
	RST_HARD = 0x01,
	RST_INST = 0x02,
	RST_PARITY = 0x04,
	RST_WD = 0x08,
	RST_OTHER = 0x10,
} RESET_REASON;

struct health_info {
	uint16_t grip_count;
	uint16_t grip_x;
	uint16_t grip_y;
	uint16_t freq_scan_count;
	uint16_t baseline_err;
	uint16_t curr_freq;
	uint16_t noise_state;
	uint16_t cid_im;
	uint16_t shield_mode;
	uint16_t reset_reason;
};

struct excep_count {
	uint16_t grip_count;
	/*baseline error type*/
	uint16_t neg_finger_count;
	uint16_t cap_incons_count;
	uint16_t energy_ratio_count;
	uint16_t rx_baseline_count;
	uint16_t tx_baseline_count;
	/*noise status*/
	uint16_t noise_count;
	/*shield report fingers*/
	uint16_t shield_palm_count;
	uint16_t shield_edge_count;
	uint16_t shield_metal_count;
	uint16_t shield_water_count;
	uint16_t shield_esd_count;
	/*exception reset count*/
	uint16_t hard_rst_count;
	uint16_t inst_rst_count;
	uint16_t parity_rst_count;
	uint16_t wd_rst_count;
	uint16_t other_rst_count;
};

struct image_header {
	/* 0x00 - 0x0f */
	unsigned char checksum[4];
	unsigned char reserved_04;
	unsigned char reserved_05;
	unsigned char options_firmware_id: 1;
	unsigned char options_contain_bootloader: 1;
	/* only available in s4322 , reserved in other, begin*/
	unsigned char options_guest_code: 1;
	unsigned char options_tddi: 1;
	unsigned char options_reserved: 4;
	/* only available in s4322 , reserved in other ,  end*/
	unsigned char bootloader_version;
	unsigned char firmware_size[4];
	unsigned char config_size[4];
	/* 0x10 - 0x1f */
	unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE];
	unsigned char package_id[2];
	unsigned char package_id_revision[2];
	unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
	/* 0x20 - 0x2f */
	/* only available in s4322 , reserved in other, begin*/
	unsigned char bootloader_addr[4];
	unsigned char bootloader_size[4];
	unsigned char ui_addr[4];
	unsigned char ui_size[4];
	/* only available in s4322 , reserved in other ,  end*/
	/* 0x30 - 0x3f */
	unsigned char ds_id[16];
	/* 0x40 - 0x4f */
	/* only available in s4322 , reserved in other, begin*/
	union {
		struct {
			unsigned char dsp_cfg_addr[4];
			unsigned char dsp_cfg_size[4];
			unsigned char reserved_48_4f[8];
		};
	};
	/* only available in s4322 , reserved in other ,  end*/
	/* 0x50 - 0x53 */
	unsigned char firmware_id[4];
};

struct image_header_data {
	bool contains_firmware_id;
	unsigned int firmware_id;
	unsigned int checksum;
	unsigned int firmware_size;
	unsigned int config_size;
	/* only available in s4322 , reserved in other, begin*/
	unsigned int disp_config_offset;
	unsigned int disp_config_size;
	unsigned int bootloader_offset;
	unsigned int bootloader_size;
	/* only available in s4322 , reserved in other ,  end*/
	unsigned char bootloader_version;
	unsigned char product_id[SYNAPTICS_RMI4_PRODUCT_ID_SIZE + 1];
	unsigned char product_info[SYNAPTICS_RMI4_PRODUCT_INFO_SIZE];
};

struct limit_block {
	char name[MAX_LIMIT_NAME_SIZE];
	int mode;
	int reserve[MAX_RESERVE_SIZE]; /*16*/
	int size;
	int16_t data;
};

struct area_descriptor {
	unsigned char magic_value[4];
	unsigned char id_string[16];
	unsigned char flags[4];
	unsigned char flash_addr_words[4];
	unsigned char length[4];
	unsigned char checksum[4];
};

struct block_data_v2 {
	const unsigned char *data;
	unsigned int size;
	unsigned int flash_addr;
};

struct image_info {
	unsigned int packrat_number;
	struct block_data_v2 boot_config;
	struct block_data_v2 app_firmware;
	struct block_data_v2 app_config;
	struct block_data_v2 disp_config;
};

struct image_header_v2 {
	unsigned char magic_value[4];
	unsigned char num_of_areas[4];
};

static inline unsigned int le2_to_uint(const unsigned char *src)
{
	return (unsigned int)src[0] +
	       (unsigned int)src[1] * 0x100;
}

static inline unsigned int le4_to_uint(const unsigned char *src)
{
	return (unsigned int)src[0] +
	       (unsigned int)src[1] * 0x100 +
	       (unsigned int)src[2] * 0x10000 +
	       (unsigned int)src[3] * 0x1000000;
}

void synaptics_parse_header(struct image_header_data *header,
			    const unsigned char *fw_image);
int synaptics_parse_header_v2(struct image_info *image_info,
			      const unsigned char *fw_image);

#endif  /*SYNAPTICS_H*/
