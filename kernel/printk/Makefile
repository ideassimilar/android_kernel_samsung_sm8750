# SPDX-License-Identifier: GPL-2.0-only
obj-y	= printk.o
obj-$(CONFIG_PRINTK)	+= printk_safe.o
obj-$(CONFIG_A11Y_BRAILLE_CONSOLE)	+= braille.o
obj-$(CONFIG_PRINTK_INDEX)	+= index.o

obj-$(CONFIG_PRINTK)                 += printk_support.o
printk_support-y	             := printk_ringbuffer.o
printk_support-$(CONFIG_SYSCTL)	     += sysctl.o
printk_support-y	     	+= cx_gdsc_debug.o