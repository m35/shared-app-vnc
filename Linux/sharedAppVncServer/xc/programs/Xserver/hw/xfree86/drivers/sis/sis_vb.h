/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/sis/sis_vb.h,v 1.12 2003/10/30 18:53:43 twini Exp $ */
/*
 * Video bridge detection and configuration for 300, 315 and 330 series
 * Data and prototypes
 *
 * Copyright 2002, 2003 by Thomas Winischhofer, Vienna, Austria
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holder not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The copyright holder makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDER DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 * Author: 	Thomas Winischhofer <thomas@winischhofer.net>
 */

typedef struct _SiS_LCD_StStruct
{
	ULONG VBLCD_lcdflag;
	USHORT LCDwidth;
	USHORT LCDheight;
	USHORT LCDtype;
} SiS_LCD_StStruct;

void SISCRT1PreInit(ScrnInfoPtr pScrn);
void SISLCDPreInit(ScrnInfoPtr pScrn);
void SISTVPreInit(ScrnInfoPtr pScrn);
void SISCRT2PreInit(ScrnInfoPtr pScrn);

extern BOOLEAN SiS_GetPanelID(SiS_Private *SiS_Pr, PSIS_HW_INFO HwDeviceExtension);
extern USHORT SiS_SenseLCDDDC(SiS_Private *SiS_Pr, SISPtr pSiS);
extern USHORT SiS_SenseVGA2DDC(SiS_Private *SiS_Pr, SISPtr pSiS);
