#
# Makefile for the touchscreen drivers.
#
subdir-ccflags-$(CONFIG_TOUCHPANEL_MTK_PLATFORM) += -I$(srctree)/drivers/misc/mediatek/include/
subdir-ccflags-$(CONFIG_OPLUS_MTK_DRM_GKI_NOTIFY) += -I$(srctree)/drivers/misc/mediatek/include/


obj-y := util_interface/touch_interfaces.o
obj-y += touch_comon_api/touch_comon_api.o
obj-y += touchpanel_healthinfo/touchpanel_healthinfo.o
obj-y += touchpanel_healthinfo/touchpanel_exception.o
obj-y += touchpanel_autotest/touchpanel_autotest.o
obj-y += touchpanel_prevention/touchpanel_prevention.o

obj-y += touchpanel_common_driver.o
obj-y += touchpanel_proc.o
obj-y += tp_ioctl.o
obj-y += message_list.o
obj-y += touch_custom/touch.o
obj-y += touch_pen/touch_pen_core.o
oplus_bsp_tp_comon-y += touch_pen/touch_pen_algo.o

obj-$(CONFIG_TOUCHPANEL_OPLUS) += oplus_bsp_tp_comon.o

oplus_bsp_tp_notify-y += touchpanel_notify/touchpanel_event_notify.o
obj-y += touchpanel_tui_support/touchpanel_tui_support.o  gh_irq_lend.o
obj-$(CONFIG_TOUCHPANEL_NOTIFY) += oplus_bsp_tp_notify.o

obj-$(CONFIG_TOUCHPANEL_SYNAPTICS) += Synaptics/
obj-$(CONFIG_TOUCHPANEL_FOCAL) += Focal/
obj-$(CONFIG_TOUCHPANEL_GOODIX) += Goodix/
obj-$(CONFIG_TOUCHPANEL_NOVA) += Novatek/
obj-$(CONFIG_TOUCHPANEL_HIMAX) += Himax/
obj-$(CONFIG_TOUCHPANEL_SAMSUNG) += Samsung/
obj-$(CONFIG_TOUCHPANEL_ILITEK) += ilitek/
obj-$(CONFIG_TOUCHPANEL_ST) += St/
