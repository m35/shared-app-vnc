/*
 *  Copyright (C) 2003 Alan Hourihane.  All Rights Reserved.
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
 *
 *  Sample application to demonstrate the features of the 'useraccept'
 *  option in conjunction with xf4vnc's unique VNC extension to deliver
 *  events when someone is connecting or disconnecting.
 *
 *  Ensure your running with xf4vnc v4.3.0.4 which includes the new
 *  VncExt v2.0 library.
 *
 *  Compilation as follows:-
 *	gcc -o vncsrv vncsrv.c -L/usr/X11R6/lib -lVncExt -lXext -lX11
 */

#include <X11/Xmd.h>
#include <X11/Xlib.h>
#include <X11/extensions/vnc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

Display *dpy = NULL;

static void
do_VncConn (dpy, eventp)
    Display *dpy;
    XEvent *eventp;
{
    XVncConnectedEvent *e = (XVncConnectedEvent *) eventp;
    Window w, wy, wn;
    unsigned long mask = 0;
    GC gcw, gc;
    XGCValues xgc;
    XFontStruct *font;
    XSetWindowAttributes attr;
    char *string = "User connected";

    /* Create main window with two button windows */
    w = XCreateWindow (dpy, RootWindow (dpy, DefaultScreen(dpy)), 
			   0, 0, 300, 125, 1, 0,
			   InputOutput, (Visual *)CopyFromParent,
			   mask, &attr);

    wy = XCreateWindow (dpy, w, 
			   25, 75, 100, 25, 1, 0,
			   InputOutput, (Visual *)CopyFromParent,
			   mask, &attr);

    wn = XCreateWindow (dpy, w, 
			   150, 75, 100, 25, 1, 0,
			   InputOutput, (Visual *)CopyFromParent,
			   mask, &attr);

    XSelectInput(dpy, wy, ExposureMask |
	     ButtonPressMask |
	     StructureNotifyMask);
    XSelectInput(dpy, wn, ExposureMask |
	     ButtonPressMask |
	     StructureNotifyMask);

    /* grab the '9x15' font */
    font = XLoadQueryFont(dpy, "9x15");

    gc = XCreateGC(dpy, w, 0, &xgc);
    gcw = XCreateGC(dpy, w, 0, &xgc);
    XSetFont(dpy, gc, font->fid);

    XSetForeground(dpy, gc, BlackPixel(dpy,DefaultScreen(dpy)));
    XSetForeground(dpy, gcw, WhitePixel(dpy,DefaultScreen(dpy)));

    XMapWindow(dpy, w);
    XMapWindow(dpy, wy);
    XMapWindow(dpy, wn);
    XSync(dpy, 0);

    /* Wait until we've hit one of our two buttons */
    while (1) {
	XEvent event;
	while (XPending(dpy)) {
  	    XNextEvent(dpy, &event);
  	    switch (event.type)
    	    {
     	    case Expose:
    		XFillRectangle(dpy, w, gcw, 0, 0, 300, 125);
    		XDrawString(dpy, w, gc, 100, 20, string, strlen(string));
    		XFillRectangle(dpy, wy, gcw, 0, 0, 100, 25);
    		XDrawString(dpy, wy, gc, 0, 20, "Accept", 6);
    		XFillRectangle(dpy, wn, gcw, 0, 0, 100, 25);
    		XDrawString(dpy, wn, gc, 0, 20, "Disconnect", 10);
    		break;
	    case ButtonPress:
		{
		    XButtonEvent *xb = (XButtonEvent *)&event;
		    if (xb->window == wn) {
    			XVncConnection(dpy, e->connected, VNC_USER_DISCONNECT);
			goto done;
		    }
		    if (xb->window == wy) {
    			XVncConnection(dpy, e->connected, VNC_USER_CONNECT);
			goto done;
		    }
		}
		break;
     	    default:
    		break;
    	    }
	}
    }

done:
    XDestroyWindow(dpy, wy);
    XDestroyWindow(dpy, wn);
    XDestroyWindow(dpy, w);
    XSync(dpy, 0);
}


int
main (argc, argv)
    int argc;
    char **argv;
{
    int VncEventBase, VncConn;
    int maj, min;

    dpy = XOpenDisplay ( NULL );
    if (!dpy) {
	printf ("unable to open display.\n");
	exit (1);
    }

    /* NOTE: should probably check the major/minor version too!! */
    if (!XVncQueryExtension(dpy, &maj, &min)) {
  	printf("VNC extension is not available\n");
	exit (1);
    }

    /* retrieve the base of our vnc extension events */
    VncEventBase = XVncGetEventBase(dpy);
    VncConn = VncEventBase + XVncConnected;

    /* Ask the VNC extension to deliver events to us */
    XVncSelectNotify(dpy, 1);

    /* our main loop, spinning waiting for VNC connections */
    while ( 1 ) {
	XEvent event;

	while (XPending(dpy)) {
	    XNextEvent (dpy, &event);

	    if (event.type == VncConn) {
	    	do_VncConn (dpy, &event);
	    }
	}
    }

    XCloseDisplay (dpy);
    exit (0);
}
