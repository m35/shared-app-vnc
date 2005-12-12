/*
 * kbdptr.c - deal with keyboard and pointer device over TCP & UDP.
 *
 * Modified for XFree86 4.x by Alan Hourihane <alanh@fairlite.demon.co.uk>
 *
 */

/*
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
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
 */

#include "rfb.h"
#include "X11/X.h"
#define NEED_EVENTS
#include "X11/Xproto.h"
#include "inputstr.h"
#define XK_CYRILLIC
#include <X11/keysym.h>
#include <Xatom.h>
#include "mi.h"
#include "mipointer.h"

#if XFREE86VNC
#ifdef XINPUT
#include "xf86Xinput.h"
#define Enqueue(ev) xf86eqEnqueue(ev)
#else
#define Enqueue(ev) mieqEnqueue(ev)
#endif
#else
#define Enqueue(ev) mieqEnqueue(ev)
#endif

#define KEY_IS_PRESSED(keycode) \
    (kbdDevice->key->down[(keycode) >> 3] & (1 << ((keycode) & 7)))

static void XConvertCase(KeySym sym, KeySym *lower, KeySym *upper);

DeviceIntPtr kbdDevice;

#include "keyboard.h"

void
KbdAddEvent(Bool down, KeySym keySym, rfbClientPtr cl)
{
    xEvent ev, fake;
    KeySymsPtr keySyms;
    int i;
    int keyCode = 0;
    int freeIndex = -1;
    unsigned long time;
    Bool fakeShiftPress = FALSE;
    Bool fakeShiftLRelease = FALSE;
    Bool fakeShiftRRelease = FALSE;
    Bool shiftMustBeReleased = FALSE;
    Bool shiftMustBePressed = FALSE;

    if (!kbdDevice) return;

    keySyms = &kbdDevice->key->curKeySyms;

#ifdef CORBA
    if (cl) {
        CARD32 clientId = cl->sock;
        ChangeWindowProperty(WindowTable[0], VNC_LAST_CLIENT_ID, XA_INTEGER,
                             32, PropModeReplace, 1, (pointer)&clientId, TRUE);
    }
#endif

    if (down) {
        ev.u.u.type = KeyPress;
    } else {
        ev.u.u.type = KeyRelease;
    }
    ev.u.keyButtonPointer.state = cl->multicursor << 13L;
    fake.u.keyButtonPointer.state = cl->multicursor << 13L;

    /* NOTE: This is where it gets hairy for XFree86 servers.
     * I think the best way to deal with this is invent a new protocol
     * to send over the wire the remote keyboard type and correctly
     * handle that as XINPUT extension device (we're part of the way
     * there for that.
     *
     * Alan.
     */
#if !XFREE86VNC
    /* First check if it's one of our predefined keys.  If so then we can make
       some attempt at allowing an xmodmap inside a VNC desktop behave
       something like you'd expect - e.g. if keys A & B are swapped over and
       the VNC client sends an A, then map it to a B when generating the X
       event.  We don't attempt to do this for keycodes which we make up on the
       fly because it's too hard... */

    for (i = 0; i < N_PREDEFINED_KEYS * GLYPHS_PER_KEY; i++) {
        if (keySym == map[i]) {
            keyCode = MIN_KEY_CODE + i / GLYPHS_PER_KEY;

            if (map[(i/GLYPHS_PER_KEY) * GLYPHS_PER_KEY + 1] != NoSymbol) {

                /* this keycode has more than one symbol associated with it,
                   so shift state is important */

                if ((i % GLYPHS_PER_KEY) == 0)
                    shiftMustBeReleased = TRUE;
                else
                    shiftMustBePressed = TRUE;
            }
            break;
        }
    }
#endif

    if (!keyCode) {

        /* not one of our predefined keys - see if it's in the current keyboard
           mapping (i.e. we've already allocated an extra keycode for it) */

        if (keySyms->mapWidth < 2) {
            ErrorF("KbdAddEvent: Sanity check failed - Keyboard mapping has "
                   "less than 2 keysyms per keycode (KeySym 0x%x)\n", keySym);
            return;
        }

        for (i = 0; i < NO_OF_KEYS * keySyms->mapWidth; i++) {
            if (keySym == keySyms->map[i]) {
                keyCode = MIN_KEY_CODE + i / keySyms->mapWidth;

                if (keySyms->map[(i / keySyms->mapWidth)
                                        * keySyms->mapWidth + 1] != NoSymbol) {

                    /* this keycode has more than one symbol associated with
                       it, so shift state is important */

                    if ((i % keySyms->mapWidth) == 0)
                        shiftMustBeReleased = TRUE;
                    else
                        shiftMustBePressed = TRUE;
                }
                break;
            }
            if ((freeIndex == -1) && (keySyms->map[i] == NoSymbol)
                && (i % keySyms->mapWidth) == 0)
            {
                freeIndex = i;
            }
        }
    }

    if (!keyCode) {
        KeySym lower, upper;

        /* we don't have an existing keycode - make one up on the fly and add
           it to the keyboard mapping.  Thanks to Vlad Harchev for pointing
           out problems with non-ascii capitalisation. */

        if (freeIndex == -1) {
            ErrorF("KbdAddEvent: ignoring KeySym 0x%x - no free KeyCodes\n",
                   keySym);
            return;
        }

        keyCode = MIN_KEY_CODE + freeIndex / keySyms->mapWidth;

        XConvertCase(keySym, &lower, &upper);

        if (lower == upper) {
            keySyms->map[freeIndex] = keySym;

        } else {
            keySyms->map[freeIndex] = lower;
            keySyms->map[freeIndex+1] = upper;

            if (keySym == lower)
                shiftMustBeReleased = TRUE;
            else
                shiftMustBePressed = TRUE;
        }

        SendMappingNotify(MappingKeyboard, keyCode, 1, serverClient);

        ErrorF("KbdAddEvent: unknown KeySym 0x%x - allocating KeyCode %d\n",
               keySym, keyCode);
    }

    time = GetTimeInMillis();

    if (down) {
        if (shiftMustBePressed && !(kbdDevice->key->state & ShiftMask)) {
            fakeShiftPress = TRUE;
            fake.u.u.type = KeyPress;
            fake.u.u.detail = SHIFT_L_KEY_CODE;
            fake.u.keyButtonPointer.time = time;
            Enqueue(&fake);
        }
        if (shiftMustBeReleased && (kbdDevice->key->state & ShiftMask)) {
            if (KEY_IS_PRESSED(SHIFT_L_KEY_CODE)) {
                fakeShiftLRelease = TRUE;
                fake.u.u.type = KeyRelease;
                fake.u.u.detail = SHIFT_L_KEY_CODE;
                fake.u.keyButtonPointer.time = time;
                Enqueue(&fake);
            }
            if (KEY_IS_PRESSED(SHIFT_R_KEY_CODE)) {
                fakeShiftRRelease = TRUE;
                fake.u.u.type = KeyRelease;
                fake.u.u.detail = SHIFT_R_KEY_CODE;
                fake.u.keyButtonPointer.time = time;
                Enqueue(&fake);
            }
        }
    }

    ev.u.u.detail = keyCode;
    ev.u.keyButtonPointer.time = time;
    Enqueue(&ev);

    if (fakeShiftPress) {
        fake.u.u.type = KeyRelease;
        fake.u.u.detail = SHIFT_L_KEY_CODE;
        fake.u.keyButtonPointer.time = time;
        Enqueue(&fake);
    }
    if (fakeShiftLRelease) {
        fake.u.u.type = KeyPress;
        fake.u.u.detail = SHIFT_L_KEY_CODE;
        fake.u.keyButtonPointer.time = time;
        Enqueue(&fake);
    }
    if (fakeShiftRRelease) {
        fake.u.u.type = KeyPress;
        fake.u.u.detail = SHIFT_R_KEY_CODE;
        fake.u.keyButtonPointer.time = time;
        Enqueue(&fake);
    }
}

void
PtrAddEvent(buttonMask, x, y, cl)
    int buttonMask;
    int x;
    int y;
    rfbClientPtr cl;
{
    xEvent ev;
    int i;
    unsigned long time;
    static int oldButtonMask = 0;

#ifdef CORBA
    if (cl) {
        CARD32 clientId = cl->sock;
        ChangeWindowProperty(WindowTable[0], VNC_LAST_CLIENT_ID, XA_INTEGER,
                             32, PropModeReplace, 1, (pointer)&clientId, TRUE);
    }
#endif

    ev.u.keyButtonPointer.state = cl->multicursor << 13L;

    time = GetTimeInMillis();

    if (cl->multicursor)
    {
      #if 0
      rfbLog("MC %d move %d %d\n", cl->multicursor, x, y);
      ev.u.u.type = MotionNotify;
      ev.u.keyButtonPointer.rootX = x;
      ev.u.keyButtonPointer.rootY = y;
      ev.u.keyButtonPointer.time = time;
      ev.u.keyButtonPointer.state = cl->multicursor << 13L; 
      Enqueue(&ev);
      #else
      miPointerAbsoluteCursor(x, y, time); 
      #endif
    }
    else
    {
      miPointerAbsoluteCursor(x, y, time);
    }

    for (i = 0; i < 5; i++) {
        if ((buttonMask ^ oldButtonMask) & (1<<i)) {
            if (buttonMask & (1<<i)) {
                ev.u.u.type = ButtonPress;
                ev.u.u.detail = i + 1;
                ev.u.keyButtonPointer.time = time;
            } else {
                ev.u.u.type = ButtonRelease;
                ev.u.u.detail = i + 1;
                ev.u.keyButtonPointer.time = time;
            }
             Enqueue(&ev);
        }
    }

    oldButtonMask = buttonMask;
}

void
KbdReleaseAllKeys()
{
    int i, j;
    xEvent ev;
    unsigned long time = GetTimeInMillis();

    if (!kbdDevice) 
        return;

    for (i = 0; i < DOWN_LENGTH; i++) {
        if (kbdDevice->key->down[i] != 0) {
            for (j = 0; j < 8; j++) {
                if (kbdDevice->key->down[i] & (1 << j)) {
                    ev.u.u.type = KeyRelease;
                    ev.u.u.detail = (i << 3) | j;
                    ev.u.keyButtonPointer.time = time;
                    Enqueue(&ev);
                }
            }
        }
    }
}


/* copied from Xlib source */

static void XConvertCase(KeySym sym, KeySym *lower, KeySym *upper)
{
    *lower = sym;
    *upper = sym;
    switch(sym >> 8) {
    case 0: /* Latin 1 */
        if ((sym >= XK_A) && (sym <= XK_Z))
            *lower += (XK_a - XK_A);
        else if ((sym >= XK_a) && (sym <= XK_z))
            *upper -= (XK_a - XK_A);
        else if ((sym >= XK_Agrave) && (sym <= XK_Odiaeresis))
            *lower += (XK_agrave - XK_Agrave);
        else if ((sym >= XK_agrave) && (sym <= XK_odiaeresis))
            *upper -= (XK_agrave - XK_Agrave);
        else if ((sym >= XK_Ooblique) && (sym <= XK_Thorn))
            *lower += (XK_oslash - XK_Ooblique);
        else if ((sym >= XK_oslash) && (sym <= XK_thorn))
            *upper -= (XK_oslash - XK_Ooblique);
        break;
    case 1: /* Latin 2 */
        /* Assume the KeySym is a legal value (ignore discontinuities) */
        if (sym == XK_Aogonek)
            *lower = XK_aogonek;
        else if (sym >= XK_Lstroke && sym <= XK_Sacute)
            *lower += (XK_lstroke - XK_Lstroke);
        else if (sym >= XK_Scaron && sym <= XK_Zacute)
            *lower += (XK_scaron - XK_Scaron);
        else if (sym >= XK_Zcaron && sym <= XK_Zabovedot)
            *lower += (XK_zcaron - XK_Zcaron);
        else if (sym == XK_aogonek)
            *upper = XK_Aogonek;
        else if (sym >= XK_lstroke && sym <= XK_sacute)
            *upper -= (XK_lstroke - XK_Lstroke);
        else if (sym >= XK_scaron && sym <= XK_zacute)
            *upper -= (XK_scaron - XK_Scaron);
        else if (sym >= XK_zcaron && sym <= XK_zabovedot)
            *upper -= (XK_zcaron - XK_Zcaron);
        else if (sym >= XK_Racute && sym <= XK_Tcedilla)
            *lower += (XK_racute - XK_Racute);
        else if (sym >= XK_racute && sym <= XK_tcedilla)
            *upper -= (XK_racute - XK_Racute);
        break;
    case 2: /* Latin 3 */
        /* Assume the KeySym is a legal value (ignore discontinuities) */
        if (sym >= XK_Hstroke && sym <= XK_Hcircumflex)
            *lower += (XK_hstroke - XK_Hstroke);
        else if (sym >= XK_Gbreve && sym <= XK_Jcircumflex)
            *lower += (XK_gbreve - XK_Gbreve);
        else if (sym >= XK_hstroke && sym <= XK_hcircumflex)
            *upper -= (XK_hstroke - XK_Hstroke);
        else if (sym >= XK_gbreve && sym <= XK_jcircumflex)
            *upper -= (XK_gbreve - XK_Gbreve);
        else if (sym >= XK_Cabovedot && sym <= XK_Scircumflex)
            *lower += (XK_cabovedot - XK_Cabovedot);
        else if (sym >= XK_cabovedot && sym <= XK_scircumflex)
            *upper -= (XK_cabovedot - XK_Cabovedot);
        break;
    case 3: /* Latin 4 */
        /* Assume the KeySym is a legal value (ignore discontinuities) */
        if (sym >= XK_Rcedilla && sym <= XK_Tslash)
            *lower += (XK_rcedilla - XK_Rcedilla);
        else if (sym >= XK_rcedilla && sym <= XK_tslash)
            *upper -= (XK_rcedilla - XK_Rcedilla);
        else if (sym == XK_ENG)
            *lower = XK_eng;
        else if (sym == XK_eng)
            *upper = XK_ENG;
        else if (sym >= XK_Amacron && sym <= XK_Umacron)
            *lower += (XK_amacron - XK_Amacron);
        else if (sym >= XK_amacron && sym <= XK_umacron)
            *upper -= (XK_amacron - XK_Amacron);
        break;
    case 6: /* Cyrillic */
        /* Assume the KeySym is a legal value (ignore discontinuities) */
        if (sym >= XK_Serbian_DJE && sym <= XK_Serbian_DZE)
            *lower -= (XK_Serbian_DJE - XK_Serbian_dje);
        else if (sym >= XK_Serbian_dje && sym <= XK_Serbian_dze)
            *upper += (XK_Serbian_DJE - XK_Serbian_dje);
        else if (sym >= XK_Cyrillic_YU && sym <= XK_Cyrillic_HARDSIGN)
            *lower -= (XK_Cyrillic_YU - XK_Cyrillic_yu);
        else if (sym >= XK_Cyrillic_yu && sym <= XK_Cyrillic_hardsign)
            *upper += (XK_Cyrillic_YU - XK_Cyrillic_yu);
        break;
    case 7: /* Greek */
        /* Assume the KeySym is a legal value (ignore discontinuities) */
        if (sym >= XK_Greek_ALPHAaccent && sym <= XK_Greek_OMEGAaccent)
            *lower += (XK_Greek_alphaaccent - XK_Greek_ALPHAaccent);
        else if (sym >= XK_Greek_alphaaccent && sym <= XK_Greek_omegaaccent &&
                 sym != XK_Greek_iotaaccentdieresis &&
                 sym != XK_Greek_upsilonaccentdieresis)
            *upper -= (XK_Greek_alphaaccent - XK_Greek_ALPHAaccent);
        else if (sym >= XK_Greek_ALPHA && sym <= XK_Greek_OMEGA)
            *lower += (XK_Greek_alpha - XK_Greek_ALPHA);
        else if (sym >= XK_Greek_alpha && sym <= XK_Greek_omega &&
                 sym != XK_Greek_finalsmallsigma)
            *upper -= (XK_Greek_alpha - XK_Greek_ALPHA);
        break;
    }
}


