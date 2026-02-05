#
# Makefile for the touchscreen drivers.
#


oplus_bsp_tp_comon-y := util_interface/touch_interfaces.o
oplus_bsp_tp_comon-y += touchpanel_exception.o

oplus_bsp_tp_comon-y += touchpanel_common_driver.o
oplus_bsp_tp_comon-y += touchpanel_proc.o
oplus_bsp_tp_comon-y += tp_ioctl.o
oplus_bsp_tp_comon-y += message_list.o

obj-$(CONFIG_TOUCHPANEL_OPLUS) += oplus_bsp_tp_comon.o

obj-$(CONFIG_TOUCHPANEL_SYNAPTICS) += Synaptics/
