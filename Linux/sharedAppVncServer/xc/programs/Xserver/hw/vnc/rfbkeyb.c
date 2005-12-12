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
#else
#include <keysym.h>
#include <dix.h>
#include <mipointer.h>
#endif


extern Bool noXkbExtension;
extern void rfbSendBell(void);
extern DeviceIntPtr kbdDevice;

static const char *DEFAULTS[] = {
    NULL
};

#include "keyboard.h"

#ifdef XKB
#include <X11/extensions/XKB.h>
#include <X11/extensions/XKBstr.h>
#include <X11/extensions/XKBsrv.h>

#if XFREE86VNC
    /* 
     * would like to use an XkbComponentNamesRec here but can't without
     * pulling in a bunch of header files. :-(
     */
static    char *		xkbkeymap;
static    char *		xkbkeycodes;
static    char *		xkbtypes;
static    char *		xkbcompat;
static    char *		xkbsymbols;
static    char *		xkbgeometry;
static    Bool		xkbcomponents_specified;
static    char *		xkbrules;
static    char *		xkbmodel;
static    char *		xkblayout;
static    char *		xkbvariant;
static    char *		xkboptions;
#endif
#endif

void
KbdDeviceInit(DeviceIntPtr pDevice, KeySymsPtr pKeySyms, CARD8 *pModMap)
{
    int i;

    kbdDevice = pDevice;

    for (i = 0; i < MAP_LENGTH; i++)
	pModMap[i] = NoSymbol;

    pModMap[CONTROL_L_KEY_CODE] = ControlMask;
    pModMap[CONTROL_R_KEY_CODE] = ControlMask;
    pModMap[SHIFT_L_KEY_CODE] = ShiftMask;
    pModMap[SHIFT_R_KEY_CODE] = ShiftMask;
    pModMap[META_L_KEY_CODE] = Mod1Mask;
    pModMap[META_R_KEY_CODE] = Mod1Mask;
    pModMap[ALT_L_KEY_CODE] = Mod1Mask;
    pModMap[ALT_R_KEY_CODE] = Mod1Mask;

    pKeySyms->minKeyCode = MIN_KEY_CODE;
    pKeySyms->maxKeyCode = MAX_KEY_CODE;
    pKeySyms->mapWidth = GLYPHS_PER_KEY;

    pKeySyms->map = (KeySym *)xalloc(sizeof(KeySym)
				     * MAP_LENGTH * GLYPHS_PER_KEY);

    if (!pKeySyms->map) {
	ErrorF("xalloc failed\n");
	exit(1);
    }

    for (i = 0; i < MAP_LENGTH * GLYPHS_PER_KEY; i++)
	pKeySyms->map[i] = NoSymbol;

    for (i = 0; i < N_PREDEFINED_KEYS * GLYPHS_PER_KEY; i++) {
	pKeySyms->map[i] = map[i];
    }
}

void
KbdDeviceOn()
{
}


void
KbdDeviceOff()
{
}

#if XFREE86VNC
static int
xf86rfbKeybControlProc(DeviceIntPtr device, int onoff)
{
    KeySymsRec		keySyms;
    CARD8 		modMap[MAP_LENGTH];
    DevicePtr pDev = (DevicePtr)device;

    switch (onoff)
    {
    case DEVICE_INIT: 
	KbdDeviceInit(device, &keySyms, modMap);
#ifdef XKB
	if (noXkbExtension) {
#endif
	    InitKeyboardDeviceStruct(pDev, &keySyms, modMap,
				 (BellProcPtr)rfbSendBell,
				 (KbdCtrlProcPtr)NoopDDA);
#ifdef XKB
	} else {
 	    XkbComponentNamesRec names;
	    if (XkbInitialMap) {
	    	if ((xkbkeymap = strchr(XkbInitialMap, '/')) != NULL)
		    xkbkeymap++;
	    	else
		    xkbkeymap = XkbInitialMap;
	    }
	    if (xkbkeymap) {
	    	names.keymap = xkbkeymap;
	    	names.keycodes = NULL;
	    	names.types = NULL;
	    	names.compat = NULL;
	    	names.symbols = NULL;
	    	names.geometry = NULL;
	    } else {
	    	names.keymap = NULL;
	    	names.keycodes = xkbkeycodes;
	    	names.types = xkbtypes;
	    	names.compat = xkbcompat;
	    	names.symbols = xkbsymbols;
	    	names.geometry = xkbgeometry;
	    }
	if ((xkbkeymap || xkbcomponents_specified)
	   && (xkbmodel == NULL || xkblayout == NULL)) {
		xkbrules = NULL;
	}
#if 0
	XkbSetRulesDflts(xkbrules, xkbmodel,
			 xkblayout, xkbvariant,
			 xkboptions);
#endif
	XkbInitKeyboardDeviceStruct(device, 
				    &names,
				    &keySyms, 
				    modMap, 
				    (BellProcPtr)rfbSendBell,
				    (KbdCtrlProcPtr)NoopDDA);
    }
#endif
	break;
    case DEVICE_ON: 
	pDev->on = TRUE;
	KbdDeviceOn();
	break;
    case DEVICE_OFF: 
	pDev->on = FALSE;
	KbdDeviceOff();
	break;
    case DEVICE_CLOSE:
	if (pDev->on)
	    KbdDeviceOff();
	break;
    }
    return Success;
}

static void
xf86rfbKeybUninit(InputDriverPtr	drv,
	       InputInfoPtr	pInfo,
	       int		flags)
{
    xf86rfbKeybControlProc(pInfo->dev, DEVICE_OFF);
}

static InputInfoPtr
xf86rfbKeybInit(InputDriverPtr	drv,
	     IDevPtr		dev,
	     int		flags)
{
    InputInfoPtr pInfo;
    char *s;
    Bool from;

    if (!(pInfo = xf86AllocateInput(drv, 0)))
	return NULL;

    /* Initialise the InputInfoRec. */
    pInfo->name = dev->identifier;
    pInfo->type_name = "rfbKeyb";
    pInfo->flags = XI86_KEYBOARD_CAPABLE;
    pInfo->device_control = xf86rfbKeybControlProc;
    pInfo->read_input = NULL;
    pInfo->motion_history_proc = NULL;
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

#ifdef XKB
  from = X_DEFAULT;
  if (noXkbExtension)
    from = X_CMDLINE;
  else if (xf86FindOption(dev->commonOptions, "XkbDisable")) {
    noXkbExtension =
	xf86SetBoolOption(dev->commonOptions, "XkbDisable", FALSE);
    from = X_CONFIG;
  }
  if (noXkbExtension)
    xf86Msg(from, "XKB: disabled\n");

#define NULL_IF_EMPTY(s) (s[0] ? s : (xfree(s), (char *)NULL))

  if (!noXkbExtension && !XkbInitialMap) {
    if ((s = xf86SetStrOption(dev->commonOptions, "XkbKeymap", NULL))) {
      xkbkeymap = NULL_IF_EMPTY(s);
      xf86Msg(X_CONFIG, "XKB: keymap: \"%s\" "
		"(overrides other XKB settings)\n", xkbkeymap);
    } else {
      if ((s = xf86SetStrOption(dev->commonOptions, "XkbCompat", NULL))) {
	xkbcompat = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: compat: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbTypes", NULL))) {
	xkbtypes = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: types: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbKeycodes", NULL))) {
	xkbkeycodes = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: keycodes: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbGeometry", NULL))) {
	xkbgeometry = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: geometry: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbSymbols", NULL))) {
	xkbsymbols = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: symbols: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbRules", NULL))) {
	xkbrules = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: rules: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbModel", NULL))) {
	xkbmodel = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: model: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbLayout", NULL))) {
	xkblayout = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: layout: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbVariant", NULL))) {
	xkbvariant = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: variant: \"%s\"\n", s);
      }

      if ((s = xf86SetStrOption(dev->commonOptions, "XkbOptions", NULL))) {
	xkboptions = NULL_IF_EMPTY(s);
	xkbcomponents_specified = TRUE;
	xf86Msg(X_CONFIG, "XKB: options: \"%s\"\n", s);
      }
    }
  }
#undef NULL_IF_EMPTY
#endif

    /* Return the configured device */
    return (pInfo);
}

#ifdef XFree86LOADER
static
#endif
InputDriverRec RFBKEYB = {
    1,				/* driver version */
    "rfbkeyb",			/* driver name */
    NULL,			/* identify */
    xf86rfbKeybInit,		/* pre-init */
    xf86rfbKeybUninit,		/* un-init */
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
xf86rfbKeybUnplug(pointer	p)
{
}

static pointer
xf86rfbKeybPlug(pointer	module,
	    pointer	options,
	    int		*errmaj,
	    int		*errmin)
{
    xf86AddInputDriver(&RFBKEYB, module, 0);

    return module;
}

void
vncInitKeyb(void)
{
    xf86AddInputDriver(&RFBKEYB, NULL, 0);
}

static XF86ModuleVersionInfo xf86rfbKeybVersionRec =
{
    "rfbkeyb",
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

XF86ModuleData rfbkeybModuleData = {&xf86rfbKeybVersionRec,
				  xf86rfbKeybPlug,
				  xf86rfbKeybUnplug};

#endif /* XFree86LOADER */
#endif
