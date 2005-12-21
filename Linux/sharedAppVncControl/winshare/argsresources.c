/*
 *  Copyright (C) 2005 Grant Wallace, Princeton University.  All Rights Reserved.
 *  Copyright (C) 2004 UCHINO Satoshi.  All Rights Reserved.
 *  Copyright (C) 2002-2003 Constantin Kaplinsky.  All Rights Reserved.
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

/*
 * argsresources.c - deal with command-line args and resources.
 */
#include <X11/Xmu/WinUtil.h>

#include "windowshare.h"
#include "dsimple.h"

/*
 * vncServerHost and vncServerPort are set either from the command line or
 * from a dialog box.
 */

char vncServerHost[256];
char *reverseConnectHost;
int vncServerPort = 0;
int windowCommand = 0;
int windowId = 0;


/*
 * appData is our application-specific data which can be set by the user with
 * application resource specs.  The AppData structure is defined in the header
 * file.
 */

AppData appData;

/*
 * usage() prints out the usage message.
 */

void
usage(void)
{
  fprintf(stderr,
	  "Window Sharing Control version 0.1\n"
	  "\n"
	  "Usage: %s [-user <USER>] [-passwd <PWDFILE>] [-dispay <VNCSERVER>] -command <WINSHARE_CMD>\n"
	  "\n"
    "<WINSHARE_CMD>\n"
	  "        enable\n"
	  "        disable\n"
	  "        on\n"
	  "        off\n"
	  "        share [-id <WIN_ID> || -name <WIN_NAME>] <-frame>\n "
	  "        hide [-id <WIN_ID> || -name <WIN_NAME>]\n"
	  "        hideall\n"
	  "        includeDialogs\n"
	  "        excludeDialogs\n"
    "(Note: if no -id or -name specified for share/hide, user to select window with mouse)\n"
	  "\n", programName, programName);
  exit(1);
}


/*
 * GetArgsAndResources() deals with resources and any command-line arguments
 * not already processed by XtVaAppInitialize().  It sets vncServerHost and
 * vncServerPort and all the fields in appData.
 */

void
GetArgsAndResources(int argc, char **argv)
{
  int i, portOffset;
  char *displayName = NULL; 
  char *commandStr, *colonPos;

  /* clear */
  memset(&appData, 0, sizeof(appData));

  displayName = Get_Display_Name(&argc, argv);
  if (!displayName) { displayName=getenv("DISPLAY"); }
  if (!displayName) { fprintf(stderr, "no display name set\n"); usage(); }
  dpy = Open_Display(displayName);
  screen = DefaultScreen(dpy);
  windowId = Select_Window_Args(&argc, argv);

  /* parameters */
  for (i = 1; i < argc; i++) {
    if (argv[i][0] == '-') {
      if (strcmp(argv[i], "-user") == 0 && i + 1 <argc) {
        appData.userLogin    = argv[++i];
      } else if (strcmp(argv[i], "-passwd") == 0 && i + 1 <argc) {
        appData.passwordFile = argv[++i];
      } else if (strcmp(argv[i], "-command") == 0 && i + 1 <argc) {
        commandStr = strdup(argv[++i]);
        if (strcmp(commandStr, "enable")==0) windowCommand = rfbSharedAppRequestEnable;
        else if (strcmp(commandStr, "disable")==0) windowCommand = rfbSharedAppRequestDisable;
        else if (strcmp(commandStr, "on")==0) windowCommand = rfbSharedAppRequestOn;
        else if (strcmp(commandStr, "off")==0) windowCommand = rfbSharedAppRequestOff;
        else if (strcmp(commandStr, "share")==0) windowCommand = rfbSharedAppRequestShow;
        else if (strcmp(commandStr, "hide")==0) windowCommand = rfbSharedAppRequestHide;
        else if (strcmp(commandStr, "hideall")==0) windowCommand = rfbSharedAppRequestHideAll;
        else if (strcmp(commandStr, "includeDialogs")==0) windowCommand = rfbSharedAppRequestIncludeDialogs;
        else if (strcmp(commandStr, "excludeDialogs")==0) windowCommand = rfbSharedAppRequestExcludeDialogs;
        else if (strcmp(commandStr, "connect")==0) 
        {
          windowCommand = rfbSharedAppReverseConnection;
          if (i + 1 <argc) reverseConnectHost = argv[++i];

        }
        else usage();
      } else if (strcmp(argv[i], "-frame") == 0) {
        appData.frame = TRUE;
      } else if (strcmp(argv[i], "-debug") == 0) {
        appData.debug = TRUE;
      } else {
        usage(); /* never returns */
      }
    } else {
      usage();
    }
  }

  if (!windowCommand) 
  {
    fprintf(stderr, "No command specified.\n\n");
    usage();
  }
  else if (windowCommand == rfbSharedAppRequestShow || windowCommand == rfbSharedAppRequestHide)
  {
    if (!windowId)
    {
      fprintf(stderr,"\n");
      fprintf(stderr,"winshare: Please use the mouse to click "
             "on the window you would like to %s\n", commandStr);
      windowId = Select_Window(dpy);
      if (windowId && !appData.frame) {
        Window root;
        int dummyi;
        unsigned int dummy;
        
        if (XGetGeometry (dpy, windowId, &root, &dummyi, &dummyi,
                          &dummy, &dummy, &dummy, &dummy) && 
            windowId != root)
        {
          windowId = XmuClientWindow (dpy, windowId);
        }
      }
    }
  }
  else 
  {
      if (windowId)
      {
        fprintf(stderr,"Window Id not used for %s command\n", commandStr);
      }
  }

  /* set up host name */
  DEBUG_PRINTF(("display name = %s\n", displayName));
  colonPos = strchr(displayName, ':');
  if (colonPos == NULL) {
    /* No colon -- use default port number */
    strcpy(vncServerHost, displayName);
    vncServerPort = SERVER_PORT_OFFSET;
  } else {
    if (colonPos > displayName) {
      memcpy(vncServerHost, displayName, colonPos - displayName);
      vncServerHost[colonPos - displayName] = '\0';
    } else {
      strcpy(vncServerHost, "localhost");
    }
    portOffset = SERVER_PORT_OFFSET;
    vncServerPort = atoi(colonPos + 1) + portOffset;
  }
  DEBUG_PRINTF(("vncServerHost = %s vncServerPort = %d\n", vncServerHost, vncServerPort));
}
