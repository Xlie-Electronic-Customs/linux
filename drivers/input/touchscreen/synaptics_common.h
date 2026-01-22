/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

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

// #include "syna_tcm2.h"
// #include "touch_comon_api/touch_comon_api.h"
// #include "touchpanel_autotest/touchpanel_autotest.h"

/*********PART2:Define Area**********************/
#define SYNAPTICS_RMI4_PRODUCT_ID_SIZE 10
#define SYNAPTICS_RMI4_PRODUCT_INFO_SIZE 2

#define DIAGONAL_UPPER_LIMIT  1100
#define DIAGONAL_LOWER_LIMIT  900

#define MAX_RESERVE_SIZE 4
#define MAX_LIMIT_NAME_SIZE 16

/*********PART3:Struct Area**********************/
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

enum test_item_bit {
	TYPE_TRX_SHORT          = 1,
	TYPE_TRX_OPEN           = 2,
	TYPE_TRXGND_SHORT       = 3,
	TYPE_FULLRAW_CAP        = 5,
	TYPE_DELTA_NOISE        = 10,
	TYPE_HYBRIDRAW_CAP      = 18,
	TYPE_RAW_CAP            = 22,
	TYPE_TREXSHORT_CUSTOM   = 25,
	TYPE_HYBRIDABS_DIFF_CBC = 26,
	TYPE_HYBRIDABS_NOSIE    = 29,
	TYPE_HYBRIDRAW_CAP_WITH_AD  = 47,
};

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

struct limit_block {
	char name[MAX_LIMIT_NAME_SIZE];
	int mode;
	int reserve[MAX_RESERVE_SIZE]; /*16*/
	int size;
	int16_t data;
};

/*test item for syna oncell ic*/
enum {
	TYPE_ERROR          = 0x00,
	TYPE_TEST1            = 0x01,/*no cbc*/
	TYPE_TEST2            = 0x02,/*with cbc*/
	TYPE_TEST3            = 0x03,
	TYPE_TEST4            = 0x04,
	TYPE_TEST5            = 0x05,
	TYPE_TEST6            = 0x06,
	TYPE_TEST7            = 0x07,
	TYPE_TEST8            = 0x08,
	TYPE_TEST9            = 0x09,
	TYPE_TEST10          = 0x0a,
	TYPE_TEST11          = 0x0b,
	TYPE_TEST12          = 0x0c,
	TYPE_RT_MAX           = 0xFF,
};

struct synaptics_proc_operations {
	void (*set_touchfilter_state)(void *chip_data, uint8_t range_size);
	uint8_t (*get_touchfilter_state)(void *chip_data);
};

struct auto_test_header {
	uint32_t magic1;
	uint32_t magic2;
	uint64_t test_item;
};

struct auto_testdata {
	int tx_num;
	int rx_num;
	/*auto test*/
	void *fp;
	size_t length;
	ssize_t *pos;
	/*black screen*/
	void *bs_fp;
	size_t bs_length;
	ssize_t *bs_pos;
	int irq_gpio;
	int key_tx;
	int key_rx;
	uint64_t  tp_fw;
	uint64_t  dev_tp_fw;
	const struct firmware *fw;
	const struct firmware *fw_test;

	uint64_t test_item;
	char *test_list_log;
	int list_write_count;
};

struct auto_test_item_header {
	uint32_t    item_magic;
	uint32_t    item_size;
	uint16_t    item_bit;
	uint16_t    item_limit_type;
	uint32_t    top_limit_offset;
	uint32_t    floor_limit_offset;
	uint32_t    para_num;
};

/*get item information*/
struct test_item_info {
	uint32_t    item_magic;
	uint32_t    item_size;
	uint16_t    item_bit;
	uint16_t    item_limit_type;
	uint32_t    top_limit_offset;
	uint32_t    floor_limit_offset;
	uint32_t    para_num;                /*number of parameter*/
	int32_t     *p_buffer;               /*pointer to item parameter buffer*/
	uint32_t     item_offset;            /*item offset*/
};

struct syna_auto_test_operations {
	int (*test1)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test2)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test3)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test4)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test5)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test6)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test7)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test8)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test9)(struct seq_file *s, void *chip_data,
		     struct auto_testdata *syna_testdata,
		     struct test_item_info *p_test_item_info);
	int (*test10)(struct seq_file *s, void *chip_data,
		      struct auto_testdata *syna_testdata,
		      struct test_item_info *p_test_item_info);
	int (*test11)(struct seq_file *s, void *chip_data,
		      struct auto_testdata *syna_testdata,
		      struct test_item_info *p_test_item_info);
	int (*syna_auto_test_enable_irq)(void *chip_data, bool enable);
	int (*syna_auto_test_preoperation)(struct seq_file *s, void *chip_data,
					   struct auto_testdata *syna_testdata,
					   struct test_item_info *p_test_item_info);
	int (*syna_auto_test_endoperation)(struct seq_file *s, void *chip_data,
					   struct auto_testdata *syna_testdata,
					   struct test_item_info *p_test_item_info);
};


int syna_trx_short_test(struct seq_file *s, void *chip_data,
			       struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_trx_open_test(struct seq_file *s, void *chip_data,
			      struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_trx_gndshort_test(struct seq_file *s, void *chip_data,
				  struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_full_rawcap_test(struct seq_file *s, void *chip_data,
				 struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_delta_noise_test(struct seq_file *s, void *chip_data,
				 struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_hybrid_rawcap_test(struct seq_file *s, void *chip_data,
				   struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_rawcap_test(struct seq_file *s, void *chip_data,
			    struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_trex_shortcustom_test(struct seq_file *s, void *chip_data,
				      struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_hybrid_diffcbc_test(struct seq_file *s, void *chip_data,
				    struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);

int syna_hybrid_absnoise_test(struct seq_file *s, void *chip_data,
				     struct auto_testdata *syna_testdata, struct test_item_info *p_test_item_info);


int synaptics_auto_test(struct seq_file *s,  struct device *dev);

