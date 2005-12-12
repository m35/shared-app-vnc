/*
 * auth.c - deal with authentication.
 *
 * This file implements the VNC authentication protocol when setting up an RFB
 * connection.
 */

/*
 *  Original Xvnc code Copyright (C) 1999 AT&T Laboratories Cambridge.  
 *  All Rights Reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include "rfb.h"

char *rfbAuthPasswdFile = NULL;

void rfbSecurityResultMessage(rfbClientPtr cl, BOOL result, char *errorString) {
	if (result) {
		CARD32 authResult = Swap32IfLE(rfbVncAuthOK);
		
		if (WriteExact(cl, (char *)&authResult, 4) < 0) {
			rfbLogPerror("rfbSecurityResultMessage: write");
			rfbCloseClient(cl);
		}		
	}
	else {
		int len=0;
		char buf[256]; // For Error Messages
		
		*(CARD32 *)&buf[len] = Swap32IfLE(rfbVncAuthFailed);
		len+=4;
		
		if ((cl->major == 3) && (cl->minor >= 8)) { // Return Error String
			int errorLength = strlen(errorString);
			*(CARD32 *)&buf[len] = Swap32IfLE(errorLength);
			len+=4;
			
			memcpy(&buf[len], errorString, errorLength);
			len+=errorLength;
		}
		
		rfbLog(errorString);
		if (WriteExact(cl, buf, len) < 0) {
			rfbLogPerror("rfbSecurityResultMessage: write");
			rfbCloseClient(cl);
		}
	}	
}

/*
 * rfbAuthNewClient is called when we reach the point of authenticating
 * a new client.  If authentication isn't being used then we simply send
 * rfbNoAuth.  Otherwise we send rfbVncAuth plus the challenge.
 */

void rfbAuthNewClient(rfbClientPtr cl) {
    char buf[4 + CHALLENGESIZE+256];// 256 for error messages
    int len = 0;
	
    if (cl->major== 3 && cl->minor >= 7) {
		len++;
	rfbLog("passwdfile %s", rfbAuthPasswdFile);
		/** JAMF AUTH **/
		if (0) {
			buf[len++] = rfbJAMF;
			cl->state = RFB_AUTH_VERSION;
		}
		else if (!cl->reverseConnection && rfbAuthPasswdFile) {
            buf[len++] = rfbVncAuth;
            cl->state = RFB_AUTH_VERSION;
        }
        else if (1) { // currently we don't disable no-auth
            buf[len++] = rfbNoAuth;
            cl->state = RFB_AUTH_VERSION; //RFB_INITIALISATION;
        }
		buf[0] = (len-1); // Record How Many Auth Types
		
		if (len == 0) { // if we disable no-auth, for example
			char *errorString = "No Supported Security Types";
			int errorLength = strlen(errorString);
			*(CARD32 *)&buf[len] = Swap32IfLE(errorLength);
			len+=4;
			
			memcpy(&buf[len], errorString, errorLength);
			len+=errorLength;
		}
		rfbLog(buf);
        if (WriteExact(cl, buf, len) < 0) {
            rfbLogPerror("rfbAuthNewClient: write");
            rfbCloseClient(cl);
            return;
        }
    }
    else {
        // If We have a password file specified - Send Challenge Request
		rfbLog("passwdfile %s", rfbAuthPasswdFile);
        if (!cl->reverseConnection && rfbAuthPasswdFile) {
            *(CARD32 *)buf = Swap32IfLE(rfbVncAuth);
            vncRandomBytes(cl->authChallenge);
			len+=4;
            memcpy(&buf[len], (char *)cl->authChallenge, CHALLENGESIZE);
            len+=CHALLENGESIZE;

            cl->state = RFB_AUTHENTICATION;
        }
        // Otherwise just send NO auth
        else {
            *(CARD32 *)buf = Swap32IfLE(rfbNoAuth);
            len = 4;

            cl->state = RFB_INITIALISATION;
        }

        if (WriteExact(cl, buf, len) < 0) {
            rfbLogPerror("rfbAuthNewClient: write");
            rfbCloseClient(cl);
            return;
        }
    }
}

void rfbProcessAuthVersion(rfbClientPtr cl) {
    int n;
    CARD8 securityType;

    if ((n = ReadExact(cl, &securityType, 1)) <= 0) {
        if (n != 0)
            rfbLogPerror("rfbProcessAuthVersion: read");
        rfbCloseClient(cl);
        return;
    }
	
    switch (securityType) {
        case rfbVncAuth: {
            char buf[CHALLENGESIZE];
            int len = 0;

            vncRandomBytes(cl->authChallenge);
            memcpy(buf, (char *)cl->authChallenge, CHALLENGESIZE);
            len = CHALLENGESIZE;

            if (WriteExact(cl, buf, len) < 0) {
                rfbLogPerror("rfbProcessAuthVersion: write");
                rfbCloseClient(cl);
                return;
            }

            cl->state = RFB_AUTHENTICATION;
            break;
        }
        case rfbNoAuth: {
			if (!cl->reverseConnection && rfbAuthPasswdFile) {
				rfbLog("rfbProcessAuthVersion: Invalid Authorization Type from %s\n", cl->host);
				rfbSecurityResultMessage(cl, 0, "Invalid Security Type");
				rfbCloseClient(cl);
				return;
			}
			else {
				if ((cl->major == 3) && (cl->minor >= 8))
					rfbSecurityResultMessage(cl, 1, NULL);
				
				cl->state = RFB_INITIALISATION;
			}
			
            break;
        }
        default:
			rfbLog("rfbProcessAuthVersion: Invalid Authorization Type from %s\n", cl->host);
			rfbSecurityResultMessage(cl, 0, "Invalid Security Type");
			rfbCloseClient(cl);
			return;
    }
}

/*
 * rfbAuthProcessClientMessage is called when the client sends its
 * authentication response.
 */

void rfbAuthProcessClientMessage(rfbClientPtr cl) {
    char *passwd;
    int i, n;
    CARD8 response[CHALLENGESIZE];

    if ((n = ReadExact(cl, (char *)response, CHALLENGESIZE)) <= 0) {
        if (n != 0)
            rfbLogPerror("rfbAuthProcessClientMessage: read");
        rfbCloseClient(cl);
        return;
    }
	rfbLog("passwdfile %s", rfbAuthPasswdFile);
    passwd = vncDecryptPasswdFromFile(rfbAuthPasswdFile);
    if (passwd == NULL) {
        rfbLog("rfbAuthProcessClientMessage: Could not access password from %s\n", rfbAuthPasswdFile);
		rfbSecurityResultMessage(cl, 0, "Could not access password file");
        rfbCloseClient(cl);
        return;
    }

    vncEncryptBytes(cl->authChallenge, passwd);

    /* Lose the password from memory */
    for (i = strlen(passwd); i >= 0; i--) {
        passwd[i] = '\0';
    }
    free((char *)passwd);

    if (memcmp(cl->authChallenge, response, CHALLENGESIZE) != 0) {
        rfbLog("rfbAuthProcessClientMessage: Authentication failed from %s\n", cl->host);
		rfbSecurityResultMessage(cl, 0, "Incorrect Password");
        rfbCloseClient(cl);
        return;
    }

	rfbSecurityResultMessage(cl, 1, NULL);

    cl->state = RFB_INITIALISATION;
}
