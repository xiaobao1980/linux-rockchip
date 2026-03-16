# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the Linux Bluetooth HCI device drivers.
#
#ccflags-y += -Iseekwave_ea6621q/include/linux/platform_data/
ccflags-y += -I$(srctree)/include/linux/platform_data/

ifneq ($(skw_extra_flags),)
ccflags-y += $(skw_extra_flags)
endif

ifeq ($(CONFIG_SEEKWAVE_PLD_RELEASE),m)
    ccflags-y += -DCONFIG_SEEKWAVE_PLD_RELEASE
endif

ifeq ($(CONFIG_SEEKWAVE_BSP_DRIVERS_V20),y)
    ccflags-y += -DINCLUDE_NEW_VERSION=1
endif

obj-$(CONFIG_SKW_BT)        += skwbt.o
skwbt-y                     := skw_btdriver.o
skwbt-y                     += skw_btsnoop.o
skwbt-y                     += skw_log.o
skwbt-y                     += skw_common.o