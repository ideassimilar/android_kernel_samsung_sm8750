/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM usb
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH trace/hooks

#if !defined(_TRACE_HOOK_USB_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_HOOK_USB_H

#include <trace/hooks/vendor_hooks.h>

struct usb_device;

DECLARE_HOOK(android_vh_configfs_uevent_work,
		TP_PROTO(bool connected, bool disconnected, bool configured, bool uevent_sent),
		TP_ARGS(connected, disconnected, configured, uevent_sent));

/*
 * Following tracepoints are not exported in tracefs and provide a
 * mechanism for vendor modules to hook and extend functionality
 */
DECLARE_RESTRICTED_HOOK(android_rvh_usb_dev_suspend,
	TP_PROTO(struct usb_device *udev, pm_message_t msg, int *bypass),
	TP_ARGS(udev, msg, bypass), 1);

DECLARE_HOOK(android_vh_usb_dev_resume,
	TP_PROTO(struct usb_device *udev, pm_message_t msg, int *bypass),
	TP_ARGS(udev, msg, bypass));

DECLARE_HOOK(android_vh_usb_new_device_added,
	TP_PROTO(struct usb_device *udev, int *err),
	TP_ARGS(udev, err));

#endif /*  _TRACE_HOOK_USB_H */
/*  This part must be outside protection */
#include <trace/define_trace.h>
