/*
 * Digium, Inc.  Wildcard TE12xP T1/E1 card Driver
 *
 * Written by Michael Spiceland <mspiceland@digium.com>
 *
 * Adapted from the wctdm24xxp and wcte11xp drivers originally
 * written by Mark Spencer <markster@digium.com>
 *            Matthew Fredrickson <creslin@digium.com>
 *            William Meadows <wmeadows@digium.com>
 *
 * Copyright (C) 2007-2009, Digium, Inc.
 *
 * All rights reserved.
 *
 */

/*
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2 as published by the
 * Free Software Foundation. See the LICENSE file included with
 * this program for more details.
 */

#ifndef _WCTE12XP_H
#define _WCTE12XP_H

/* Comment to disable VPM support */
#define VPM_SUPPORT 1

#define WC_MAX_IFACES 8

#ifdef VPM_SUPPORT
#define MAX_TDM_CHAN 31
#endif

#define SDI_CLK		(0x00010000)
#define SDI_DOUT	(0x00020000)
#define SDI_DREAD	(0x00040000)
#define SDI_DIN		(0x00080000)

#define EFRAME_SIZE	108
#define ERING_SIZE	16		/* Maximum ring size */
#define EFRAME_GAP	20
#define SFRAME_SIZE	((EFRAME_SIZE * DAHDI_CHUNKSIZE) + (EFRAME_GAP * (DAHDI_CHUNKSIZE - 1)))

#define PCI_WINDOW_SIZE ((2 * 2 * 2 * SFRAME_SIZE) + (2 * ERING_SIZE * 4))

#define MAX_COMMANDS 16

#define NUM_EC 4

#define __CMD_VPM  (1 << 16)		/* flag for VPM action */
#define __CMD_PINS (1 << 18)		/* CPLD pin read */
#define __CMD_LEDS (1 << 19)		/* LED Operation */
#define __CMD_RD   (1 << 20)		/* Read Operation */
#define __CMD_WR   (1 << 21)		/* Write Operation */

#define __LED_ORANGE	(1<<3)
#define __LED_GREEN	(1<<2)
#define __LED_RED	(1<<1)

#define SET_LED_ORANGE(a) (a | __LED_ORANGE)
#define SET_LED_RED(a) ((a | __LED_RED) & ~__LED_GREEN)
#define SET_LED_GREEN(a) ((a | __LED_GREEN) & ~__LED_RED)

#define UNSET_LED_ORANGE(a) (a & ~__LED_ORANGE)
#define UNSET_LED_REDGREEN(a) (a | __LED_RED | __LED_GREEN)

#define CMD_WR(a,b) (((a) << 8) | (b) | __CMD_WR)
#define CMD_RD(a) (((a) << 8) | __CMD_RD)
#define CMD_LEDS(a) (((a) << 8) | __CMD_LEDS)
//#define CMD_BYTE(slot, a) (slot*6)+(a*2) /* only even slots */
#define CMD_BYTE(slot, a, is_vpm) (slot*6)+(a*2)+is_vpm /* only even slots */
//TODO: make a separate macro

#define TYPE_T1	1
#define TYPE_E1	2

#define NOT_VPM -1

#define module_printk(fmt, args...) printk(KERN_INFO "%s: " fmt, te12xp_driver.name, ## args)
#define debug_printk(level, fmt, args...) if (debug >= level) printk(KERN_DEBUG "%s (%s): " fmt, te12xp_driver.name, __FUNCTION__, ## args)
extern spinlock_t ifacelock;

struct command {
	u8 data;
	u8 ident;
	u8 cs_slot;
	u8 vpm_num; /* ignored for all but vpm commmands */
	u16 address;
	u32 flags;
	struct list_head node;
	struct completion complete;
};

struct vpm150m;

struct t1 {
	spinlock_t reglock;
	unsigned char txident;
	unsigned char rxident;
	unsigned char statreg; /* bit 0 = vpmadt032 int */
	int spantype;
	struct {
		unsigned int nmf:1;
		unsigned int sendingyellow:1;
	} flags;
	unsigned char txsigs[16];  /* Copy of tx sig registers */
	int alarmcount;			/* How much red alarm we've seen */
	int losalarmcount;
	int aisalarmcount;
	int yelalarmcount;
	const char *variety;
	char name[80];
	unsigned long blinktimer;
	int loopupcnt;
	int loopdowncnt;
#define INITIALIZED 1
#define SHUTDOWN    2
	unsigned long bit_flags;
	unsigned long alarmtimer;
	unsigned char ledstate;
	unsigned char ec_chunk1[32][DAHDI_CHUNKSIZE];
	unsigned char ec_chunk2[32][DAHDI_CHUNKSIZE];
	struct dahdi_span span;						/* Span */
	struct dahdi_chan *chans[32];					/* Channels */
	struct dahdi_echocan_state *ec[32];				/* Echocan state for channels */
	unsigned long ctlreg;
	struct voicebus* vb;
	atomic_t txints;
	int vpm100;
	struct vpmadt032 *vpmadt032;
	unsigned long dtmfactive;
	unsigned long dtmfmask;
	unsigned long dtmfmutemask;

	spinlock_t cmd_list_lock;
	struct list_head pending_cmds;
	struct list_head active_cmds;
	struct timer_list timer;
	struct work_struct timer_work;
};

#define t1_info(t1, format, arg...)         \
        dev_info(&voicebus_get_pci_dev(t1->vb)->dev , format , ## arg)

#endif
