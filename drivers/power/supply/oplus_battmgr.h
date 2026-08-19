#ifndef _OPLUS_CHARGER_H_
#define _OPLUS_CHARGER_H_

#define OEM_OPCODE_READ_BUFFER    0x10000
#define BCC_OPCODE_READ_BUFFER    0x10003
#define PPS_OPCODE_READ_BUFFER    0x10004
#define AP_OPCODE_UFCS_BUFFER     0x10005
#define AP_OPCODE_READ_BUFFER     0x10006
#define AP_OPCODE_WRITE_BUFFER    0x10008
#define OPLUS_OPCODE_GET_SINK_MSG 0x10009
#define OEM_READ_WAIT_TIME_MS    500
#define MAX_OEM_PROPERTY_DATA_SIZE 128
#define AP_READ_WAIT_TIME_MS      1000
#define MAX_AP_PROPERTY_DATA_SIZE 512
#define AP_UFCS_WAIT_TIME_MS      500
#define MAX_UFCS_CAPS_ITEM        16
#define MAX_REVERSE_CHG_MSG_ITEM  16

enum oplus_opcodes {
	/* opcode for battery charger */
	BC_SET_NOTIFY_REQ 	= 0x4,
	BC_NOTIFY_IND	  	= 0x7,
	BC_BATTERY_STATUS_GET 	= 0x30,
	BC_BATTERY_STATUS_SET,
	BC_USB_STATUS_GET,
	BC_USB_STATUS_SET,
	BC_WLS_STATUS_GET,
	BC_WLS_STATUS_SET,
	BC_SHIP_MODE_REQ_SET,
	BC_WLS_FW_CHECK_UPDATE 	= 0x40,
	BC_WLS_FW_PUSH_BUF_REQ,
	BC_WLS_FW_UPDATE_STATUS_RESP,
	BC_WLS_FW_PUSH_BUF_RESP,
	BC_WLS_FW_GET_VERSION,
	BC_SHUTDOWN_NOTIFY	= 0x47,
	BC_GENERIC_NOTIFY 	= 0x80,
};

enum oplus_notifications {
	BC_VOOC_STATUS_GET	= 0x48,
	BC_VOOC_STATUS_SET,
	BC_OTG_ENABLE	= 0x50,
	BC_OTG_DISABLE,
	BC_VOOC_VBUS_ADC_ENABLE,
	BC_CID_DETECT,
	BC_QC_DETECT,
	BC_TYPEC_STATE_CHANGE,
	BC_PD_SVOOC,
	BC_PLUGIN_IRQ,
	BC_APSD_DONE,
	BC_CHG_STATUS_GET,
	BC_PD_SOFT_RESET,
	BC_CHG_STATUS_SET = 0x60,
	BC_ADSP_NOTIFY_AP_SUSPEND_CHG,
	BC_ADSP_NOTIFY_AP_CP_BYPASS_INIT,
	BC_ADSP_NOTIFY_AP_CP_MOS_ENABLE,
	BC_ADSP_NOTIFY_AP_CP_MOS_DISABLE,
	BC_PPS_OPLUS,
	BC_ADSP_NOTIFY_TRACK,
	BC_ABNORMAL_PD_SVOOC_ADAPTER,
	BC_UFCS_TEST_MODE_TRUE,
	BC_UFCS_TEST_MODE_FALSE,
	BC_UFCS_POWER_READY,
	BC_UFCS_HANDSHAKE_OK,
	BC_VOOC_GAN_MOS_ERROR,
	BC_UFCS_DISABLE_MOS,
	BC_UFCS_PDO_READY,
	BC_UFCS_VERIFY_AUTH_READY,
	BC_UFCS_VDM_EMARK_READY,
	BC_UFCS_PWR_INFO_READY,
	BC_BATTERY_RESET_START,
	PD_SOURCECAP_DONE,
	REQUEST_QOS,
	RELEASE_QOS,
	HMAC_UPDATE,
	BC_POWER_ROLE_STATUS,
	UFCS_EXIT_MODE_NOTIFY,
	PD_CONNECT_HARD_RESET,
	GAUGE_INITED = 0X90,
};

#define MAX_STR_LEN			128
#define BC_WAIT_TIME_MS			2500/* sjc 1K->2K */
#define WLS_FW_PREPARE_TIME_MS		300
#define WLS_FW_WAIT_TIME_MS		500
#define WLS_FW_BUF_SIZE			128
#define DEFAULT_RESTRICT_FCC_UA		1000000

struct oem_read_buffer_req_msg {
	struct pmic_glink_hdr hdr;
	u32 data_size;
};

struct oem_read_buffer_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 data_buffer[MAX_OEM_PROPERTY_DATA_SIZE];
	u32 data_size;
};

struct oplus_ap_read_ufcs_req_msg {
	struct pmic_glink_hdr hdr;
	u32 data_size;
	u32 msg_id;
};

struct oplus_ap_read_ufcs_resp_msg {
	struct pmic_glink_hdr hdr;
	u64 data_buffer[MAX_UFCS_CAPS_ITEM];
	u32 data_size;
	u32 msg_id;
};

struct oplus_ap_read_req_msg {
	struct pmic_glink_hdr hdr;
	u32 message_id;
	u32 value;
};

struct oplus_ap_read_buffer_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 message_id;
	u8 data_buffer[MAX_AP_PROPERTY_DATA_SIZE];
	u32 data_size;
};

struct oplus_chip {
	bool check_pd_svooc_complete;
	int pd_curr_max;
	int pd_svooc;
	int pd_chging;
	int pd_volt;
	int pps_to_pd_chging;

	int charger_type;

	bool is_abnormal_adapter;

	int voocphy_support;
};

struct vooc_chip {
	int pcb_version;
	bool allow_reading;
	bool fastchg_started;
	bool fastchg_ing;
	bool fastchg_allow;
	bool fastchg_to_normal;
	bool fastchg_to_warm;
	bool fastchg_to_warm_full;
	bool fastchg_low_temp_full;
	bool btb_temp_over;
	bool fastchg_dummy_started;
	bool need_to_up;
	bool have_updated;
	bool mcu_update_ing;
	bool mcu_update_ing_fix;
	bool mcu_boot_by_gpio;
	const unsigned char *firmware_data;
	unsigned int fw_data_count;
	int fw_mcu_version;
	int fw_data_version;
	int adapter_update_real;
	int adapter_update_report;
	int dpdm_switch_mode;
	bool support_vooc_by_normal_charger_path;
};

struct voocphy_manager {
	bool voocphy_dual_cp_support;
	bool voocphy_bidirect_cp_support;
	bool external_gauge_support;
	bool version_judge_support;
	bool impedance_calculation_newmethod;
	bool record_fastchg_end_soc;

	int batt_temp_plugin; //batt_temp at plugin
	int batt_soc_plugin; //batt_soc at plugin
	bool adapter_rand_start; //adapter checksum need start;
	bool adapter_check_ok;
	bool fastchg_allow;
	bool start_vaild_frame;
	bool ask_bat_model_finished;
	bool reply_bat_model_end;
	bool ask_vol_again;
	bool ignore_first_frame;
	bool ask_vooc3_detect;
	bool force_2a;
	bool force_2a_flag;
	bool force_3a;
	bool force_3a_flag;
	bool btb_temp_over;
	bool btb_err_first;
	bool vbatt_ovp_status;
	bool usb_bad_connect;
	bool fastchg_ing;
	bool fastchg_dummy_start;
	bool fastchg_start;
	bool fastchg_to_normal;
	bool fastchg_to_warm;
	bool adspvoocphy_fastchg_start;
	bool fastchg_to_warm_full;
	int fast_chg_type;
	int last_fast_chg_type;
	bool fastchg_err_commu;
	bool fastchg_reactive;
	bool fastchg_real_allow;
	bool fastchg_commu_stop;
	bool fastchg_check_stop;
	bool fastchg_monitor_stop;
	bool fastchg_commu_ing;
	bool vooc_move_head;
	bool copycat_vooc_adapter;
	bool user_exit_fastchg;
	unsigned char fastchg_stage;
	bool fastchg_need_reset;
	bool fastchg_recovering;
	unsigned int fastchg_recover_cnt;

	int vooc_vbus_status;
	int vbus_vbatt;
	int adapter_type;
	unsigned int fastchg_notify_status;
};


enum oplus_ap_message_id {
	AP_MESSAGE_ACK,
	AP_MESSAGE_GET_GAUGE_REG_INFO,
	AP_MESSAGE_GET_GAUGE_CALIB_TIME,
	AP_MESSAGE_GET_GAUGE_BATTINFO,
	AP_MESSAGE_GET_LPD_INFO,
	AP_MESSAGE_GET_GAUGE_LIFETIME_STATUS,
	AP_MESSAGE_GET_GAUGE_LIFETIME_INFO,
	AP_MESSAGE_GET_GAUGE_R_INFO,
	AP_MESSAGE_GET_GAUGE_THREE_LEVEL_TERM_VOLT,
	AP_MESSAGE_MAX_SIZE = 32,
};

struct oplus_ap_write_req_msg {
	struct pmic_glink_hdr hdr;
	u32 message_id;
	u8 data_buffer[MAX_AP_PROPERTY_DATA_SIZE];
	u32 data_size;
};

struct oplus_ap_write_buffer_resp_msg {
	struct pmic_glink_hdr hdr;
	u32 message_id;
	int ret;
};

enum oplus_ap_write_message_id {
	AP_MESSAGE_WRITE_CALIB_TIME,
	AP_MESSAGE_WRITE_THREE_LEVEL_TERM_VOLT,
};

enum lcm_en_status {
	LCM_EN_DEAFULT = 1,
	LCM_EN_ENABLE,
	LCM_EN_DISABLE,
};

enum psy_type {
	PSY_TYPE_BATTERY,
	PSY_TYPE_USB,
	PSY_TYPE_WLS,
	PSY_TYPE_MAX,
};

enum ship_mode_type {
	SHIP_MODE_PMIC,
	SHIP_MODE_PACK_SIDE,
};

typedef enum {
	DOUBLE_SERIES_WOUND_CELLS = 0,
	SINGLE_CELL,
	DOUBLE_PARALLEL_WOUND_CELLS,
} SCC_CELL_TYPE;

typedef enum {
	TI_GAUGE = 0,
	SW_GAUGE,
	UNKNOWN_GAUGE_TYPE,
} SCC_GAUGE_TYPE;

enum oplus_power_supply_usb_type {
	POWER_SUPPLY_USB_TYPE_PD_SDP = 17,		/* USB With PD Port*/
};

/* property ids */
enum battery_property_id {
	BATT_STATUS,
	BATT_HEALTH,
	BATT_PRESENT,
	BATT_CHG_TYPE,
	BATT_CAPACITY,
	BATT_SOH,
	BATT_VOLT_OCV,
	BATT_VOLT_NOW,
	BATT_VOLT_MAX,
	BATT_CURR_NOW,
	BATT_CHG_CTRL_LIM,
	BATT_CHG_CTRL_LIM_MAX,
	BATT_TEMP,
	BATT_TECHNOLOGY,
	BATT_CHG_COUNTER,
	BATT_CYCLE_COUNT,
	BATT_CHG_FULL_DESIGN,
	BATT_CHG_FULL,
	BATT_MODEL_NAME,
	BATT_TTF_AVG,
	BATT_TTE_AVG,
	BATT_RESISTANCE,
	BATT_POWER_NOW,
	BATT_POWER_AVG,
	BATT_CHG_EN,/* sjc add */
	BATT_SET_PDO,/* sjc add */
	BATT_SET_QC,/* sjc add */
	BATT_SET_SHIP_MODE,/*sjc add*/
	BATT_SET_COOL_DOWN,/*lzj add*/
	BATT_SET_MATCH_TEMP,/*lzj add*/
	BATT_BATTERY_AUTH,/*lzj add*/
	BATT_RTC_SOC,/*lzj add*/
	BATT_UEFI_INPUT_CURRENT,
	BATT_UEFI_PRE_CHG_CURRENT,
	BATT_UEFI_UEFI_CHG_EN,
	BATT_UEFI_LOAD_ADSP,
	BATT_UEFI_SET_VSYSTEM_MIN,
	BATT_SEND_CHG_STATUS,
	BATT_ADSP_GAUGE_INIT,
	BATT_UPDATE_SOC_SMOOTH_PARAM,
	BATT_BATTERY_HMAC,
	BATT_SET_BCC_CURRENT,
	BATT_ZY0603_CHECK_RC_SFR,
	BATT_ZY0603_SOFT_RESET,
	BATT_AFI_UPDATE_DONE,
	BATT_UI_SOC,
	BATT_AP_FASTCHG_ALLOW,
	BATT_SET_VOOC_CURVE_NUM,
	BATT_BAT_FULL_CURR_SET,
	BATT_DEEP_DISCHG_COUNT,
	BATT_DEEP_TERM_VOLT,
	BATT_SET_FIRST_USAGE_DATE,
	BATT_SET_UI_CYCLE_COUNT,
	BATT_SET_UI_SOH,
	BATT_SET_USED_FLAG,
	BATT_DEEP_DISCHG_LAST_CC,
	BATT_GET_UFCS_RUNNING_STATE,
	BATT_VOLT_MIN,
	BATT_SET_CHG_PATH,
	BATT_GET_CHG_PATH_STATUS,
	BATT_GET_CAR_C,
	BATT_SET_CAR_C_CLEAR,
	BATT_GET_VCT,
	BATT_SET_VCT,
	BATT_SET_BATT_FULL,
	BATT_SET_CUV_STATE,
	BATT_GET_CUV_STATE,
	BATT_ITERM_CHECK_STAT,
	BATT_ITERM_TIMEOUT,
	BATT_SET_TRUE_FCC,
	BATT_PROP_MAX,
};

enum usb_property_id {
	USB_ONLINE,
	USB_VOLT_NOW,
	USB_VOLT_MAX,
	USB_CURR_NOW,
	USB_CURR_MAX,
	USB_INPUT_CURR_LIMIT,
	USB_TYPE,
	USB_ADAP_TYPE,
	USB_MOISTURE_DET_EN,
	USB_MOISTURE_DET_STS,
	USB_TEMP,
	USB_REAL_TYPE,
	USB_TYPEC_COMPLIANT,
	USB_ADAP_SUBTYPE,
	USB_VBUS_COLLAPSE_STATUS,
	USB_VOOCPHY_STATUS,
	USB_VOOCPHY_ENABLE,
	USB_OTG_AP_ENABLE,
	USB_OTG_SWITCH,
	USB_POWER_SUPPLY_RELEASE_FIXED_FREQUENCE,
	USB_TYPEC_CC_ORIENTATION,
	USB_CID_STATUS,
	USB_TYPEC_MODE,
	USB_TYPEC_SINKONLY,
	USB_OTG_VBUS_REGULATOR_ENABLE,
	USB_VOOC_CHG_PARAM_INFO,
	USB_VOOC_FAST_CHG_TYPE,
	USB_DEBUG_REG,
	USB_VOOCPHY_RESET_AGAIN,
	USB_SUSPEND_PMIC,
	USB_OEM_MISC_CTL,
	USB_CCDETECT_HAPPENED,
	USB_GET_PPS_TYPE,
	USB_GET_PPS_STATUS,
	USB_SET_PPS_VOLT,
	USB_SET_PPS_CURR,
	USB_GET_PPS_MAX_CURR,
	USB_PPS_READ_VBAT0_VOLT,
	USB_PPS_CHECK_BTB_TEMP,
	USB_PPS_MOS_CTRL,
	USB_PPS_CP_MODE_INIT,
	USB_PPS_CHECK_AUTHENTICATE,
	USB_PPS_GET_AUTHENTICATE,
	USB_PPS_GET_CP_VBUS,
	USB_PPS_GET_CP_MASTER_IBUS,
	USB_PPS_GET_CP_SLAVE_IBUS,
	USB_PPS_MOS_SLAVE_CTRL,
	USB_PPS_GET_R_COOL_DOWN,
	USB_PPS_GET_DISCONNECT_STATUS,
	USB_PPS_VOOCPHY_ENABLE,
	USB_IN_STATUS,
	USB_GET_BATT_CURR,
	USB_PPS_FORCE_SVOOC,
	USB_SET_OVP_CFG,
	USB_SET_UFCS_START,
	USB_SET_UFCS_VOLT,
	USB_SET_UFCS_CURRENT,
	USB_GET_UFCS_STATUS,
	USB_GET_DEV_INFO_L,
	USB_GET_DEV_INFO_H,
	USB_SET_WD_TIME,
	USB_SET_EXIT,
	USB_GET_SRC_INFO_L,
	USB_GET_SRC_INFO_H,
	USB_OTG_BOOST_CURRENT,
	USB_SNS_STATUS,
	USB_SET_UFCS_SM_PERIOD,
	USB_SET_RERUN_AICL,
	USB_SET_AICL_VOL,
	USB_GET_AICL_VOL,
	USB_SET_PLC_STATUS,
	USB_GET_POWER_ROLE,
	USB_REVERSE_CHG_SET_VOLT,
	USB_REVERSE_CHG_SET_CURRENT,
	USB_RVS_HIGH_MODE_EN,
	USB_PROP_MAX,
};

enum wireless_property_id {
	WLS_ONLINE,
	WLS_VOLT_NOW,
	WLS_VOLT_MAX,
	WLS_CURR_NOW,
	WLS_CURR_MAX,
	WLS_TYPE,
	WLS_BOOST_EN,
	WLS_HBOOST_VMAX,
	WLS_INPUT_CURR_LIMIT,
	WLS_ADAP_TYPE,
	WLS_CONN_TEMP,
	WLS_BOOST_VOLT = 12,
	WLS_BOOST_AICL_ENABLE,
	WLS_BOOST_AICL_RERUN,
	WLS_BOOST_CURRENT,
	WLS_PROP_MAX,
};

typedef enum {
	NO_VOOCPHY = 0,
	ADSP_VOOCPHY,
	AP_SINGLE_CP_VOOCPHY,
	AP_DUAL_CP_VOOCPHY,
	INVALID_VOOCPHY,
} OPLUS_VOOCPHY_TYPE;

typedef enum {
	NO_VOOC = 0,
	VOOC,
	DUAL_BATT_50W,
	DUAL_BATT_65W,
	SINGLE_BATT_50W,
	VOOCPHY_33W = 5,
	VOOCPHY_60W,
	DUAL_BATT_80W,
	DUAL_BATT_100W = 8,
	DUAL_BATT_150W,
	POWER_BANK_66W = 12,
	POWER_BANK_67W = 13,
	POWER_BANK_120W = 14,
	POWER_BANK_44W = 15,
	DUAL_BATT_240W = 16,
	POWER_BANK_200W = 17,
	POWER_BANK_88W = 18,
	POWER_BANK_55W = 19,
	POWER_BANK_125W = 20,
	POWER_BANK_45W = 21,
	INVALID_VOOC_PROJECT,
} OPLUS_VOOC_PROJECT_TYPE;

enum {
	FASTCHG_CHARGER_TYPE_UNKOWN,
	PORTABLE_PIKAQIU_1 = 0x31,
	PORTABLE_PIKAQIU_2 = 0x32,
	PORTABLE_50W = 0x33,
	PORTABLE_20W_1 = 0X34,
	PORTABLE_20W_2 = 0x35,
	PORTABLE_20W_3 = 0x36,
};


#endif /* _OPLUS_CHARGER_H_ */
