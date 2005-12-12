/*
 *  Copyright (C) 2002 Alan Hourihane.  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 *
 *  Author: Alan Hourihane <alanh@fairlite.demon.co.uk>
 */

#if XFREE86VNC
#ifndef XFree86LOADER
#include <unistd.h>
#include <errno.h>
#endif

#include <misc.h>
#include <xf86.h>
#define NEED_XF86_TYPES
#if !defined(DGUX)
#include <xf86_ansic.h>
#include <xisb.h>
#endif
#include <xf86_OSproc.h>
#include <xf86Xinput.h>
#include <exevents.h>		/* Needed for InitValuator/Proximity stuff */
#include <keysym.h>
#include <mipointer.h>

#ifdef XFree86LOADER
#include <xf86Module.h>
#endif

static const char *DEFAULTS[] = {
    NULL
};
#else
#include <keysym.h>
#include <dix.h>
#include <mipointer.h>
#endif


unsigned char ptrAcceleration = 50;

void
PtrDeviceOn(DeviceIntPtr pDev)
{
#if 0
    ptrAcceleration = (unsigned char)pDev->ptrfeed->ctrl.num;
#endif
}

void
PtrDeviceInit()
{
}

void
PtrDeviceOff()
{
}


void
PtrDeviceControl(DeviceIntPtr dev, PtrCtrl *ctrl)
{
#if 0
    ptrAcceleration = (char)ctrl->num;

    if (udpSockConnected) {
	if (write(udpSock, &ptrAcceleration, 1) <= 0) {
	    ErrorF("PtrDeviceControl: UDP input: write");
	    rfbDisconnectUDPSock();
	}
    }
#endif
}

#if XFREE86VNC
static int
xf86rfbMouseControlProc(DeviceIntPtr device, int onoff)
{
    BYTE map[6];
    DevicePtr pDev = (DevicePtr)device;

    switch (onoff)
    {
    case DEVICE_INIT:
	PtrDeviceInit();
	map[1] = 1;
	map[2] = 2;
	map[3] = 3;
	map[4] = 4;
	map[5] = 5;
	InitPointerDeviceStruct(pDev, map, 5, miPointerGetMotionEvents,
				PtrDeviceControl,
				miPointerGetMotionBufferSize());
	break;

    case DEVICE_ON:
	pDev->on = TRUE;
	PtrDeviceOn(device);
        break;

    case DEVICE_OFF:
	pDev->on = FALSE;
	PtrDeviceOff();
	break;

    case DEVICE_CLOSE:
	if (pDev->on)
	    PtrDeviceOff();
	break;
    }
    return Success;
}

static void
xf86rfbMouseUninit(InputDriverPtr	drv,
	       InputInfoPtr	pInfo,
	       int		flags)
{
    xf86rfbMouseControlProc(pInfo->dev, DEVICE_OFF);
}

static InputInfoPtr
xf86rfbMouseInit(InputDriverPtr	drv,
	     IDevPtr		dev,
	     int		flags)
{
    InputInfoPtr pInfo;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
	return NULL;

    /* Initialise the InputInfoRec. */
    pInfo->name = dev->identifier;
    pInfo->type_name = "rfbMouse";
    pInfo->flags = XI86_POINTER_CAPABLE | XI86_SEND_DRAG_EVENTS;
    pInfo->device_control = xf86rfbMouseControlProc;
    pInfo->read_input = NULL;
    pInfo->motion_history_proc = xf86GetMotionEvents;
    pInfo->history_size = 0;
    pInfo->control_proc = NULL;
    pInfo->close_proc = NULL;
    pInfo->switch_mode = NULL;
    pInfo->conversion_proc = NULL;
    pInfo->reverse_conversion_proc = NULL;
    pInfo->fd = -1;
    pInfo->dev = NULL;
    pInfo->private_flags = 0;
    pInfo->always_core_feedback = 0;
    pInfo->conf_idev = dev;

    /* Collect the options, and process the common options. */
    xf86CollectInputOptions(pInfo, DEFAULTS, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);
    
    /* Mark the device configured */
    pInfo->flags |= XI86_CONFIGURED;

    /* Return the configured device */
    return (pInfo);
}

#ifdef XFree86LOADER
static
#endif
InputDriverRec RFBMOUSE = {
    1,				/* driver version */
    "rfbmouse",			/* driver name */
    NULL,			/* identify */
    xf86rfbMouseInit,		/* pre-init */
    xf86rfbMouseUninit,		/* un-init */
    NULL,			/* module */
    0				/* ref count */
};

/*
 ***************************************************************************
 *
 * Dynamic loading functions
 *
 ***************************************************************************
 */
#ifdef XFree86LOADER
static void
xf86rfbMouseUnplug(pointer	p)
{
}

static pointer
xf86rfbMousePlug(pointer	module,
	    pointer	options,
	    int		*errmaj,
	    int		*errmin)
{
    xf86AddInputDriver(&RFBMOUSE, module, 0);

    return module;
}

void
vncInitMouse(void)
{
    xf86AddInputDriver(&RFBMOUSE, NULL, 0);
}

static XF86ModuleVersionInfo xf86rfbMouseVersionRec =
{
    "rfbmouse",
    "xf4vnc Project, see http://xf4vnc.sf.net",
    MODINFOSTRING1,
    MODINFOSTRING2,
    XF86_VERSION_CURRENT,
    1, 0, 0,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}		/* signature, to be patched into the file by */
				/* a tool */
};

XF86ModuleData rfbmouseModuleData = {&xf86rfbMouseVersionRec,
				  xf86rfbMousePlug,
				  xf86rfbMouseUnplug};

#endif /* XFree86LOADER */
#endif
