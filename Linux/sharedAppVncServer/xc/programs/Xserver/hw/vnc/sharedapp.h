/*
 * sharedapp.h
 *
 * Copyright (C) 2005 Grant Wallace.  All Rights Reserved.
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

#ifndef _SHAREDAPP_H_
#define _SHAREDAPP_H_

#include "list.h"

typedef struct _SharedAppVnc
{ 
  Bool bEnabled;
  Bool bOn;
  Bool bIncludeDialogWindows;
  List sharedAppList;
  char* reverseConnectionHost;
} SharedAppVnc, *SharedAppVncPtr;

/* These are defined in rfb.h
 * void sharedapp_Init(SharedAppVncPtr shapp);
 * void sharedapp_HandleRequest(rfbClientPtr cl, unsigned int command, unsigned int id);
 * Bool sharedapp_RfbSendUpdates(ScreenPtr pScreen, rfbClientPtr cl);
*/

#endif
