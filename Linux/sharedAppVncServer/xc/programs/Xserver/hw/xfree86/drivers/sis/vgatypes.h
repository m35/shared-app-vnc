/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/sis/vgatypes.h,v 1.18 2003/12/16 17:45:20 twini Exp $ */
/*
 * General type definitions for universal mode switching modules
 *
 * Copyright 2002, 2003 by Thomas Winischhofer, Vienna, Austria
 *
 * If distributed as part of the linux kernel, the contents of this file
 * is entirely covered by the GPL.
 *
 * Otherwise, the following terms apply:
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
 * Authors: 	Thomas Winischhofer <thomas@winischhofer.net>
 *		Silicon Integrated Systems
 *
 */
#ifndef _VGATYPES_
#define _VGATYPES_

#ifdef LINUX_XF86
#include "xf86Version.h"
#include "xf86Pci.h"
#endif

#ifdef LINUX_KERNEL  /* We don't want the X driver to depend on kernel source */
#include <linux/ioctl.h>
#endif

#ifndef FALSE
#define FALSE   0
#endif

#ifndef TRUE
#define TRUE    1
#endif

#ifndef NULL
#define NULL    0
#endif

#ifndef CHAR
typedef char CHAR;
#endif

#ifndef SHORT
typedef short SHORT;
#endif

#ifndef LONG
typedef long  LONG;
#endif

#ifndef UCHAR
typedef unsigned char UCHAR;
#endif

#ifndef USHORT
typedef unsigned short USHORT;
#endif

#ifndef ULONG
typedef unsigned long ULONG;
#endif

#ifndef BOOLEAN
typedef UCHAR BOOLEAN;
#endif

#ifndef bool
typedef UCHAR bool;
#endif

#ifdef LINUX_KERNEL
typedef unsigned long SISIOADDRESS;
#endif

#ifdef LINUX_XF86
#if XF86_VERSION_CURRENT < XF86_VERSION_NUMERIC(4,2,0,0,0)
typedef unsigned long IOADDRESS;
typedef unsigned long SISIOADDRESS;
#else
typedef IOADDRESS SISIOADDRESS;
#endif
#endif

#ifndef LINUX_KERNEL   /* For the linux kernel, this is defined in sisfb.h */
#ifndef SIS_CHIP_TYPE
typedef enum _SIS_CHIP_TYPE {
    SIS_VGALegacy = 0,
#ifdef LINUX_XF86
    SIS_530,
    SIS_OLD,
#endif
    SIS_300,
    SIS_630,
    SIS_730,
    SIS_540,
    SIS_315H,   /* SiS 310 */
    SIS_315,
    SIS_315PRO, /* SiS 325 */
    SIS_550,
    SIS_650,
    SIS_740,
    SIS_330,
    SIS_661,
    SIS_741,
    SIS_660,
    SIS_760,
    MAX_SIS_CHIP
} SIS_CHIP_TYPE;
#endif
#endif

#ifndef SIS_VB_CHIP_TYPE
typedef enum _SIS_VB_CHIP_TYPE {
    VB_CHIP_Legacy = 0,
    VB_CHIP_301,
    VB_CHIP_301B,
    VB_CHIP_301LV,
    VB_CHIP_302,
    VB_CHIP_302B,
    VB_CHIP_302LV,
    VB_CHIP_301C,
    VB_CHIP_302ELV,
    VB_CHIP_UNKNOWN, /* other video bridge or no video bridge */
    MAX_VB_CHIP
} SIS_VB_CHIP_TYPE;
#endif

#ifndef SIS_LCD_TYPE
typedef enum _SIS_LCD_TYPE {
    LCD_INVALID = 0,
    LCD_800x600,
    LCD_1024x768,
    LCD_1280x1024,
    LCD_1280x960,
    LCD_640x480,
    LCD_1600x1200,
    LCD_1920x1440,
    LCD_2048x1536,
    LCD_320x480,       /* FSTN, DSTN */
    LCD_1400x1050,
    LCD_1152x864,
    LCD_1152x768,
    LCD_1280x768,
    LCD_1024x600,
    LCD_640x480_2,     /* FSTN, DSTN */
    LCD_640x480_3,     /* FSTN, DSTN */
    LCD_848x480,
    LCD_CUSTOM,
    LCD_UNKNOWN
} SIS_LCD_TYPE;
#endif

#ifndef PSIS_DSReg
typedef struct _SIS_DSReg
{
  UCHAR  jIdx;
  UCHAR  jVal;
} SIS_DSReg, *PSIS_DSReg;
#endif

#ifndef SIS_HW_INFO

typedef struct _SIS_HW_INFO  SIS_HW_INFO, *PSIS_HW_INFO;

typedef BOOLEAN (*PSIS_QUERYSPACE)   (PSIS_HW_INFO, ULONG, ULONG, ULONG *);

struct _SIS_HW_INFO
{
#ifdef LINUX_XF86
    PCITAG PciTag;		 /* PCI Tag */
#endif

    UCHAR  *pjVirtualRomBase;    /* ROM image */

    BOOLEAN UseROM;		 /* Use the ROM image if provided */

    UCHAR  *pjVideoMemoryAddress;/* base virtual memory address */
                                 /* of Linear VGA memory */

    ULONG  ulVideoMemorySize;    /* size, in bytes, of the memory on the board */
    SISIOADDRESS ulIOAddress;    /* base I/O address of VGA ports (0x3B0) */
    UCHAR  jChipType;            /* Used to Identify SiS Graphics Chip */
                                 /* defined in the data structure type  */
                                 /* "SIS_CHIP_TYPE" */

    UCHAR  jChipRevision;        /* Used to Identify SiS Graphics Chip Revision */
    UCHAR  ujVBChipID;           /* the ID of video bridge */
                                 /* defined in the data structure type */
                                 /* "SIS_VB_CHIP_TYPE" */
#ifdef LINUX_KERNEL
    BOOLEAN Is301BDH;
#endif

    USHORT usExternalChip;       /* NO VB or other video bridge (other than  */
                                 /* SiS video bridge) */

    ULONG  ulCRT2LCDType;        /* defined in the data structure type */
                                 /* "SIS_LCD_TYPE" */
                                     
    BOOLEAN bIntegratedMMEnabled;/* supporting integration MM enable */
                                      
    BOOLEAN bSkipDramSizing;     /* True: Skip video memory sizing. */

#ifdef LINUX_KERNEL
    PSIS_DSReg  pSR;             /* restore SR registers in initial function. */
                                 /* end data :(idx, val) =  (FF, FF). */
                                 /* Note : restore SR registers if  */
                                 /* bSkipDramSizing = TRUE */

    PSIS_DSReg  pCR;             /* restore CR registers in initial function. */
                                 /* end data :(idx, val) =  (FF, FF) */
                                 /* Note : restore cR registers if  */
                                 /* bSkipDramSizing = TRUE */
#endif

    PSIS_QUERYSPACE  pQueryVGAConfigSpace; /* Get/Set VGA Configuration  */
                                           /* space */
 
    PSIS_QUERYSPACE  pQueryNorthBridgeSpace;/* Get/Set North Bridge  */
                                            /* space  */
};
#endif

/* Addtional IOCTL for communication sisfb <> X driver        */
/* If changing this, sisfb.h must also be changed (for sisfb) */

#ifdef LINUX_XF86  /* We don't want the X driver to depend on the kernel source */

/* ioctl for identifying and giving some info (esp. memory heap start) */
#define SISFB_GET_INFO    0x80046ef8  /* Wow, what a terrible hack... */

/* Structure argument for SISFB_GET_INFO ioctl  */
typedef struct _SISFB_INFO sisfb_info, *psisfb_info;

struct _SISFB_INFO {
	unsigned long sisfb_id;         /* for identifying sisfb */
#ifndef SISFB_ID
#define SISFB_ID	  0x53495346    /* Identify myself with 'SISF' */
#endif
 	int    chip_id;			/* PCI ID of detected chip */
	int    memory;			/* video memory in KB which sisfb manages */
	int    heapstart;               /* heap start (= sisfb "mem" argument) in KB */
	unsigned char fbvidmode;	/* current sisfb mode */

	unsigned char sisfb_version;
	unsigned char sisfb_revision;
	unsigned char sisfb_patchlevel;

	unsigned char sisfb_caps;	/* sisfb's capabilities */

	int    sisfb_tqlen;		/* turbo queue length (in KB) */

	unsigned int sisfb_pcibus;      /* The card's PCI ID */
	unsigned int sisfb_pcislot;
	unsigned int sisfb_pcifunc;

	unsigned char sisfb_lcdpdc;
	
	unsigned char sisfb_lcda;

	unsigned long sisfb_vbflags;
	unsigned long sisfb_currentvbflags;

	int sisfb_scalelcd;
	unsigned long sisfb_specialtiming;

	unsigned char sisfb_haveemi;
	unsigned char sisfb_emi30,sisfb_emi31,sisfb_emi32,sisfb_emi33;
	unsigned char sisfb_haveemilcd;

	char reserved[213]; 		/* for future use */
};
#endif

#endif

