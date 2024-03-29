/*
 * Wildcard TDM2400P TDM FXS/FXO Interface Driver for DAHDI Telephony interface
 *
 * Written by Mark Spencer <markster@digium.com>
 * Support for TDM800P and VPM150M by Matthew Fredrickson <creslin@digium.com>
 *
 * Copyright (C) 2005 - 2009 Digium, Inc.
 * All rights reserved.
 *
 * Sections for QRV cards written by Jim Dixon <jim@lambdatel.com>
 * Copyright (C) 2006, Jim Dixon and QRV Communications
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

/* For QRV DRI cards, gain is signed short, expressed in hundredths of
db (in reference to 1v Peak @ 1000Hz) , as follows:

Rx Gain: -11.99 to 15.52 db
Tx Gain - No Pre-Emphasis: -35.99 to 12.00 db
Tx Gain - W/Pre-Emphasis: -23.99 to 0.00 db
*/

#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/moduleparam.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,26)
#include <linux/semaphore.h>
#else
#include <asm/semaphore.h>
#endif

#include <dahdi/kernel.h>
#include <dahdi/wctdm_user.h>

#include "proslic.h"

#include "wctdm24xxp.h"

#include "adt_lec.h"

#include "voicebus/GpakCust.h"
#include "voicebus/GpakApi.h"

/*
  Experimental max loop current limit for the proslic
  Loop current limit is from 20 mA to 41 mA in steps of 3
  (according to datasheet)
  So set the value below to:
  0x00 : 20mA (default)
  0x01 : 23mA
  0x02 : 26mA
  0x03 : 29mA
  0x04 : 32mA
  0x05 : 35mA
  0x06 : 37mA
  0x07 : 41mA
*/
static int loopcurrent = 20;

/* Following define is a logical exclusive OR to determine if the polarity of an fxs line is to be reversed.
 * 	The items taken into account are:
 * 	overall polarity reversal for the module,
 * 	polarity reversal for the port,
 * 	and the state of the line reversal MWI indicator
 */
#define POLARITY_XOR(card) ( (reversepolarity != 0) ^ (wc->mods[(card)].fxs.reversepolarity != 0) ^ (wc->mods[(card)].fxs.vmwi_linereverse != 0) )
static int reversepolarity = 0;

static alpha  indirect_regs[] =
{
{0,255,"DTMF_ROW_0_PEAK",0x55C2},
{1,255,"DTMF_ROW_1_PEAK",0x51E6},
{2,255,"DTMF_ROW2_PEAK",0x4B85},
{3,255,"DTMF_ROW3_PEAK",0x4937},
{4,255,"DTMF_COL1_PEAK",0x3333},
{5,255,"DTMF_FWD_TWIST",0x0202},
{6,255,"DTMF_RVS_TWIST",0x0202},
{7,255,"DTMF_ROW_RATIO_TRES",0x0198},
{8,255,"DTMF_COL_RATIO_TRES",0x0198},
{9,255,"DTMF_ROW_2ND_ARM",0x0611},
{10,255,"DTMF_COL_2ND_ARM",0x0202},
{11,255,"DTMF_PWR_MIN_TRES",0x00E5},
{12,255,"DTMF_OT_LIM_TRES",0x0A1C},
{13,0,"OSC1_COEF",0x7B30},
{14,1,"OSC1X",0x0063},
{15,2,"OSC1Y",0x0000},
{16,3,"OSC2_COEF",0x7870},
{17,4,"OSC2X",0x007D},
{18,5,"OSC2Y",0x0000},
{19,6,"RING_V_OFF",0x0000},
{20,7,"RING_OSC",0x7EF0},
{21,8,"RING_X",0x0160},
{22,9,"RING_Y",0x0000},
{23,255,"PULSE_ENVEL",0x2000},
{24,255,"PULSE_X",0x2000},
{25,255,"PULSE_Y",0x0000},
//{26,13,"RECV_DIGITAL_GAIN",0x4000},	// playback volume set lower
{26,13,"RECV_DIGITAL_GAIN",0x2000},	// playback volume set lower
{27,14,"XMIT_DIGITAL_GAIN",0x4000},
//{27,14,"XMIT_DIGITAL_GAIN",0x2000},
{28,15,"LOOP_CLOSE_TRES",0x1000},
{29,16,"RING_TRIP_TRES",0x3600},
{30,17,"COMMON_MIN_TRES",0x1000},
{31,18,"COMMON_MAX_TRES",0x0200},
{32,19,"PWR_ALARM_Q1Q2",0x07C0},
{33,20,"PWR_ALARM_Q3Q4", 0x4C00 /* 0x2600 */},
{34,21,"PWR_ALARM_Q5Q6",0x1B80},
{35,22,"LOOP_CLOSURE_FILTER",0x8000},
{36,23,"RING_TRIP_FILTER",0x0320},
{37,24,"TERM_LP_POLE_Q1Q2",0x008C},
{38,25,"TERM_LP_POLE_Q3Q4",0x0100},
{39,26,"TERM_LP_POLE_Q5Q6",0x0010},
{40,27,"CM_BIAS_RINGING",0x0C00},
{41,64,"DCDC_MIN_V",0x0C00},
{42,255,"DCDC_XTRA",0x1000},
{43,66,"LOOP_CLOSE_TRES_LOW",0x1000},
};

#ifdef FANCY_ECHOCAN
static char ectab[] = {
0, 0, 0, 1, 2, 3, 4, 6, 8, 9, 11, 13, 16, 18, 20, 22, 24, 25, 27, 28, 29, 30, 31, 31, 32, 
32, 32, 32, 32, 32, 32, 32, 32, 32, 32 ,32 ,32, 32,
32, 32, 32, 32, 32, 32, 32, 32, 32, 32 ,32 ,32, 32,
32, 32, 32, 32, 32, 32, 32, 32, 32, 32 ,32 ,32, 32,
31, 31, 30, 29, 28, 27, 25, 23, 22, 20, 18, 16, 13, 11, 9, 8, 6, 4, 3, 2, 1, 0, 0, 
};
static int ectrans[4] = { 0, 1, 3, 2 };
#define EC_SIZE (sizeof(ectab))
#define EC_SIZE_Q (sizeof(ectab) / 4)
#endif

/* Undefine to enable Power alarm / Transistor debug -- note: do not
   enable for normal operation! */
/* #define PAQ_DEBUG */

#define DEBUG_CARD (1 << 0)
#define DEBUG_ECHOCAN (1 << 1)

#include "fxo_modes.h"

struct wctdm_desc {
	const char *name;
	const int flags;
	const int ports;
};

static const struct wctdm_desc wctdm2400 = { "Wildcard TDM2400P", 0, 24 };
static const struct wctdm_desc wctdm800 = { "Wildcard TDM800P", 0, 8 };
static const struct wctdm_desc wctdm410 = { "Wildcard TDM410P", 0, 4 };
static const struct wctdm_desc wcaex2400 = { "Wildcard AEX2400", FLAG_EXPRESS, 24 };
static const struct wctdm_desc wcaex800 = { "Wildcard AEX800", FLAG_EXPRESS, 8 };
static const struct wctdm_desc wcaex410 = { "Wildcard AEX410", FLAG_EXPRESS, 4 };

static int acim2tiss[16] = { 0x0, 0x1, 0x4, 0x5, 0x7, 0x0, 0x0, 0x6, 0x0, 0x0, 0x0, 0x2, 0x0, 0x3 };

struct wctdm *ifaces[WC_MAX_IFACES];
spinlock_t ifacelock = SPIN_LOCK_UNLOCKED;

static void wctdm_release(struct wctdm *wc);

static int fxovoltage = 0;
static unsigned int battdebounce;
static unsigned int battalarm;
static unsigned int battthresh;
static int debug = 0;
static int robust = 0;
static int lowpower = 0;
static int boostringer = 0;
static int fastringer = 0;
static int _opermode = 0;
static char *opermode = "FCC";
static int fxshonormode = 0;
static int alawoverride = 0;
static int fxo_addrs[4] = { 0x00, 0x08, 0x04, 0x0c };
static int fxotxgain = 0;
static int fxorxgain = 0;
static int fxstxgain = 0;
static int fxsrxgain = 0;
static int nativebridge = 0;
static int ringdebounce = DEFAULT_RING_DEBOUNCE;
static int fwringdetect = 0;
static int latency = VOICEBUS_DEFAULT_LATENCY;

#define MS_PER_HOOKCHECK	(1)
#define NEONMWI_ON_DEBOUNCE	(100/MS_PER_HOOKCHECK)
static int neonmwi_monitor = 0; 	/* Note: this causes use of full wave ring detect */
static int neonmwi_level = 75;		/* neon mwi trip voltage */
static int neonmwi_envelope = 10;
static int neonmwi_offlimit = 16000;  /* Time in milliseconds the monitor is checked before saying no message is waiting */
static int neonmwi_offlimit_cycles;  /* Time in milliseconds the monitor is checked before saying no message is waiting */

static int vpmsupport = 1;

static int vpmnlptype = DEFAULT_NLPTYPE;
static int vpmnlpthresh = DEFAULT_NLPTHRESH;
static int vpmnlpmaxsupp = DEFAULT_NLPMAXSUPP;

static int echocan_create(struct dahdi_chan *chan, struct dahdi_echocanparams *ecp,
			   struct dahdi_echocanparam *p, struct dahdi_echocan_state **ec);
static void echocan_free(struct dahdi_chan *chan, struct dahdi_echocan_state *ec);

static const struct dahdi_echocan_features vpm100m_ec_features = {
	.NLP_automatic = 1,
	.CED_tx_detect = 1,
	.CED_rx_detect = 1,
};

static const struct dahdi_echocan_features vpm150m_ec_features = {
	.NLP_automatic = 1,
	.CED_tx_detect = 1,
	.CED_rx_detect = 1,
};

static const struct dahdi_echocan_ops vpm100m_ec_ops = {
	.name = "VPM100M",
	.echocan_free = echocan_free,
};

static const struct dahdi_echocan_ops vpm150m_ec_ops = {
	.name = "VPM150M",
	.echocan_free = echocan_free,
};

static int wctdm_init_proslic(struct wctdm *wc, int card, int fast , int manual, int sane);

static inline int CMD_BYTE(int card, int bit, int altcs)
{
	/* Let's add some trickery to make the TDM410 work */
	if (altcs == 3) {
		if (card == 2) {
			card = 4;
			altcs = 0;
		} else if (card == 3) {
			card = 5;
			altcs = 2;
		}
	}

	return (((((card) & 0x3) * 3 + (bit)) * 7) \
			+ ((card) >> 2) + (altcs) + ((altcs) ? -21 : 0));
}

/* sleep in user space until woken up. Equivilant of tsleep() in BSD */
int schluffen(wait_queue_head_t *q)
{
	DECLARE_WAITQUEUE(wait, current);
	add_wait_queue(q, &wait);
	current->state = TASK_INTERRUPTIBLE;
	if (!signal_pending(current)) schedule();
	current->state = TASK_RUNNING;
	remove_wait_queue(q, &wait);
	if (signal_pending(current)) return -ERESTARTSYS;
	return(0);
}

static inline int empty_slot(struct wctdm *wc, int card)
{
	int x;
	for (x=0;x<USER_COMMANDS;x++) {
		if (!wc->cmdq[card].cmds[x])
			return x;
	}
	return -1;
}

void setchanconfig_from_state(struct vpmadt032 *vpm, int channel, GpakChannelConfig_t *chanconfig)
{
	const struct vpmadt032_options *options;
	GpakEcanParms_t *p;

	BUG_ON(!vpm);

	options = &vpm->options;

	chanconfig->PcmInPortA = 3;
	chanconfig->PcmInSlotA = channel;
	chanconfig->PcmOutPortA = SerialPortNull;
	chanconfig->PcmOutSlotA = channel;
	chanconfig->PcmInPortB = 2;
	chanconfig->PcmInSlotB = channel;
	chanconfig->PcmOutPortB = 3;
	chanconfig->PcmOutSlotB = channel;
	chanconfig->ToneTypesA = Null_tone;
	chanconfig->MuteToneA = Disabled;
	chanconfig->FaxCngDetA = Disabled;
	chanconfig->ToneTypesB = Null_tone;
	chanconfig->EcanEnableA = Enabled;
	chanconfig->EcanEnableB = Disabled;
	chanconfig->MuteToneB = Disabled;
	chanconfig->FaxCngDetB = Disabled;

	chanconfig->SoftwareCompand = (ADT_COMP_ALAW == vpm->companding) ?
						cmpPCMA : cmpPCMU;
	chanconfig->FrameRate = rate2ms;
	p = &chanconfig->EcanParametersA;

	vpmadt032_get_default_parameters(p);

	p->EcanNlpType = vpm->curecstate[channel].nlp_type;
	p->EcanNlpThreshold = vpm->curecstate[channel].nlp_threshold;
	p->EcanNlpMaxSuppress = vpm->curecstate[channel].nlp_max_suppress;

	memcpy(&chanconfig->EcanParametersB,
		&chanconfig->EcanParametersA,
		sizeof(chanconfig->EcanParametersB));
}

static int config_vpmadt032(struct vpmadt032 *vpm, struct wctdm *wc)
{
	int res, i;
	GpakPortConfig_t portconfig = {0};
	gpakConfigPortStatus_t configportstatus;
	GPAK_PortConfigStat_t pstatus;
	GpakChannelConfig_t chanconfig;
	GPAK_ChannelConfigStat_t cstatus;
	GPAK_AlgControlStat_t algstatus;

	/* First Serial Port config */
	portconfig.SlotsSelect1 = SlotCfgNone;
	portconfig.FirstBlockNum1 = 0;
	portconfig.FirstSlotMask1 = 0x0000;
	portconfig.SecBlockNum1 = 1;
	portconfig.SecSlotMask1 = 0x0000;
	portconfig.SerialWordSize1 = SerWordSize8;
	portconfig.CompandingMode1 = cmpNone;
	portconfig.TxFrameSyncPolarity1 = FrameSyncActHigh;
	portconfig.RxFrameSyncPolarity1 = FrameSyncActHigh;
	portconfig.TxClockPolarity1 = SerClockActHigh;
	portconfig.RxClockPolarity1 = SerClockActHigh;
	portconfig.TxDataDelay1 = DataDelay0;
	portconfig.RxDataDelay1 = DataDelay0;
	portconfig.DxDelay1 = Disabled;
	portconfig.ThirdSlotMask1 = 0x0000;
	portconfig.FouthSlotMask1 = 0x0000;
	portconfig.FifthSlotMask1 = 0x0000;
	portconfig.SixthSlotMask1 = 0x0000;
	portconfig.SevenSlotMask1 = 0x0000;
	portconfig.EightSlotMask1 = 0x0000;

	/* Second Serial Port config */
	portconfig.SlotsSelect2 = SlotCfg2Groups;
	portconfig.FirstBlockNum2 = 0;
	portconfig.FirstSlotMask2 = 0xffff;
	portconfig.SecBlockNum2 = 1;
	portconfig.SecSlotMask2 = 0xffff;
	portconfig.SerialWordSize2 = SerWordSize8;
	portconfig.CompandingMode2 = cmpNone;
	portconfig.TxFrameSyncPolarity2 = FrameSyncActHigh;
	portconfig.RxFrameSyncPolarity2 = FrameSyncActHigh;
	portconfig.TxClockPolarity2 = SerClockActHigh;
	portconfig.RxClockPolarity2 = SerClockActLow;
	portconfig.TxDataDelay2 = DataDelay0;
	portconfig.RxDataDelay2 = DataDelay0;
	portconfig.DxDelay2 = Disabled;
	portconfig.ThirdSlotMask2 = 0x0000;
	portconfig.FouthSlotMask2 = 0x0000;
	portconfig.FifthSlotMask2 = 0x0000;
	portconfig.SixthSlotMask2 = 0x0000;
	portconfig.SevenSlotMask2 = 0x0000;
	portconfig.EightSlotMask2 = 0x0000;

	/* Third Serial Port Config */
	portconfig.SlotsSelect3 = SlotCfg2Groups;
	portconfig.FirstBlockNum3 = 0;
	portconfig.FirstSlotMask3 = 0xffff;
	portconfig.SecBlockNum3 = 1;
	portconfig.SecSlotMask3 = 0xffff;
	portconfig.SerialWordSize3 = SerWordSize8;
	portconfig.CompandingMode3 = cmpNone;
	portconfig.TxFrameSyncPolarity3 = FrameSyncActHigh;
	portconfig.RxFrameSyncPolarity3 = FrameSyncActHigh;
	portconfig.TxClockPolarity3 = SerClockActHigh;
	portconfig.RxClockPolarity3 = SerClockActLow;
	portconfig.TxDataDelay3 = DataDelay0;
	portconfig.RxDataDelay3 = DataDelay0;
	portconfig.DxDelay3 = Disabled;
	portconfig.ThirdSlotMask3 = 0x0000;
	portconfig.FouthSlotMask3 = 0x0000;
	portconfig.FifthSlotMask3 = 0x0000;
	portconfig.SixthSlotMask3 = 0x0000;
	portconfig.SevenSlotMask3 = 0x0000;
	portconfig.EightSlotMask3 = 0x0000;

	if ((configportstatus = gpakConfigurePorts(vpm->dspid, &portconfig, &pstatus))) {
		printk(KERN_NOTICE "Configuration of ports failed (%d)!\n", configportstatus);
		return -1;
	} else {
		if (vpm->options.debug & DEBUG_ECHOCAN)
			printk(KERN_DEBUG "Configured McBSP ports successfully\n");
	}

	if ((res = gpakPingDsp(vpm->dspid, &vpm->version))) {
		printk(KERN_NOTICE "Error pinging DSP (%d)\n", res);
		return -1;
	}

	vpm->companding = (wc->span.deflaw == DAHDI_LAW_MULAW) ?
				ADT_COMP_ULAW : ADT_COMP_ALAW;

	for (i = 0; i < vpm->options.channels; ++i) {
		vpm->curecstate[i].tap_length = 0;
		vpm->curecstate[i].nlp_type = vpm->options.vpmnlptype;
		vpm->curecstate[i].nlp_threshold = vpm->options.vpmnlpthresh;
		vpm->curecstate[i].nlp_max_suppress = vpm->options.vpmnlpmaxsupp;
		vpm->curecstate[i].companding = (wc->span.deflaw == DAHDI_LAW_MULAW) ? ADT_COMP_ULAW : ADT_COMP_ALAW;

		/* set_vpmadt032_chanconfig_from_state(&vpm->curecstate[i], &vpm->options, i, &chanconfig); !!! */
		vpm->setchanconfig_from_state(vpm, i, &chanconfig);
		if ((res = gpakConfigureChannel(vpm->dspid, i, tdmToTdm, &chanconfig, &cstatus))) {
			printk(KERN_NOTICE "Unable to configure channel #%d (%d)", i, res);
			if (res == 1) {
				printk(", reason %d", cstatus);
			}
			printk("\n");
			return -1;
		}

		if ((res = gpakAlgControl(vpm->dspid, i, BypassEcanA, &algstatus))) {
			printk(KERN_NOTICE "Unable to disable echo can on channel %d (reason %d:%d)\n", i + 1, res, algstatus);
			return -1;
		}

		if ((res = gpakAlgControl(vpm->dspid, i, BypassSwCompanding, &algstatus))) {
			printk(KERN_NOTICE "Unable to disable echo can on channel %d (reason %d:%d)\n", i + 1, res, algstatus);
			return -1;
		}
	}

	if ((res = gpakPingDsp(vpm->dspid, &vpm->version))) {
		printk(KERN_NOTICE "Error pinging DSP (%d)\n", res);
		return -1;
	}

	set_bit(VPM150M_ACTIVE, &vpm->control);

	return 0;
}


static inline void cmd_dequeue_vpmadt032(struct wctdm *wc, u8 *writechunk, int whichframe)
{
	struct vpmadt032_cmd *curcmd = NULL;
	struct vpmadt032 *vpmadt032 = wc->vpmadt032;
	int x;
	unsigned char leds = ~((wc->intcount / 1000) % 8) & 0x7;

	/* Skip audio */
	writechunk += 24;

	if (test_bit(VPM150M_SPIRESET, &vpmadt032->control) || test_bit(VPM150M_HPIRESET, &vpmadt032->control)) {
		if (debug & DEBUG_ECHOCAN)
			printk(KERN_INFO "HW Resetting VPMADT032...\n");
		for (x = 24; x < 28; x++) {
			if (x == 24) {
				if (test_and_clear_bit(VPM150M_SPIRESET, &vpmadt032->control))
					writechunk[CMD_BYTE(x, 0, 0)] = 0x08;
				else if (test_and_clear_bit(VPM150M_HPIRESET, &vpmadt032->control))
					writechunk[CMD_BYTE(x, 0, 0)] = 0x0b;
			} else
				writechunk[CMD_BYTE(x, 0, 0)] = 0x00 | leds;
			writechunk[CMD_BYTE(x, 1, 0)] = 0;
			writechunk[CMD_BYTE(x, 2, 0)] = 0x00;
		}
		return;
	}

	if ((curcmd = vpmadt032_get_ready_cmd(vpmadt032))) {
		curcmd->txident = wc->txident;
#if 0
		// if (printk_ratelimit()) 
			printk(KERN_DEBUG "Transmitting txident = %d, desc = 0x%x, addr = 0x%x, data = 0x%x\n", curcmd->txident, curcmd->desc, curcmd->address, curcmd->data);
#endif
		if (curcmd->desc & __VPM150M_RWPAGE) {
			/* Set CTRL access to page*/
			writechunk[CMD_BYTE(24, 0, 0)] = (0x8 << 4);
			writechunk[CMD_BYTE(24, 1, 0)] = 0;
			writechunk[CMD_BYTE(24, 2, 0)] = 0x20;

			/* Do a page write */
			if (curcmd->desc & __VPM150M_WR)
				writechunk[CMD_BYTE(25, 0, 0)] = ((0x8 | 0x4) << 4);
			else
				writechunk[CMD_BYTE(25, 0, 0)] = ((0x8 | 0x4 | 0x1) << 4);
			writechunk[CMD_BYTE(25, 1, 0)] = 0;
			if (curcmd->desc & __VPM150M_WR)
				writechunk[CMD_BYTE(25, 2, 0)] = curcmd->data & 0xf;
			else
				writechunk[CMD_BYTE(25, 2, 0)] = 0;

			/* Clear XADD */
			writechunk[CMD_BYTE(26, 0, 0)] = (0x8 << 4);
			writechunk[CMD_BYTE(26, 1, 0)] = 0;
			writechunk[CMD_BYTE(26, 2, 0)] = 0;

			/* Fill in to buffer to size */
			writechunk[CMD_BYTE(27, 0, 0)] = 0;
			writechunk[CMD_BYTE(27, 1, 0)] = 0;
			writechunk[CMD_BYTE(27, 2, 0)] = 0;

		} else {
			/* Set address */
			writechunk[CMD_BYTE(24, 0, 0)] = ((0x8 | 0x4) << 4);
			writechunk[CMD_BYTE(24, 1, 0)] = (curcmd->address >> 8) & 0xff;
			writechunk[CMD_BYTE(24, 2, 0)] = curcmd->address & 0xff;

			/* Send/Get our data */
			writechunk[CMD_BYTE(25, 0, 0)] = (curcmd->desc & __VPM150M_WR) ?
				((0x8 | (0x3 << 1)) << 4) : ((0x8 | (0x3 << 1) | 0x1) << 4);
			writechunk[CMD_BYTE(25, 1, 0)] = (curcmd->data >> 8) & 0xff;
			writechunk[CMD_BYTE(25, 2, 0)] = curcmd->data & 0xff;
			
			writechunk[CMD_BYTE(26, 0, 0)] = 0;
			writechunk[CMD_BYTE(26, 1, 0)] = 0;
			writechunk[CMD_BYTE(26, 2, 0)] = 0;

			/* Fill in the rest */
			writechunk[CMD_BYTE(27, 0, 0)] = 0;
			writechunk[CMD_BYTE(27, 1, 0)] = 0;
			writechunk[CMD_BYTE(27, 2, 0)] = 0;
		}
	} else if (test_and_clear_bit(VPM150M_SWRESET, &vpmadt032->control)) {
		for (x = 24; x < 28; x++) {
			if (x == 24)
				writechunk[CMD_BYTE(x, 0, 0)] = (0x8 << 4);
			else
				writechunk[CMD_BYTE(x, 0, 0)] = 0x00;
			writechunk[CMD_BYTE(x, 1, 0)] = 0;
			if (x == 24)
				writechunk[CMD_BYTE(x, 2, 0)] = 0x01;
			else
				writechunk[CMD_BYTE(x, 2, 0)] = 0x00;
		}
	} else {
		for (x = 24; x < 28; x++) {
			writechunk[CMD_BYTE(x, 0, 0)] = 0x00;
			writechunk[CMD_BYTE(x, 1, 0)] = 0x00;
			writechunk[CMD_BYTE(x, 2, 0)] = 0x00;
		}
	}

	/* Add our leds in */
	for (x = 24; x < 28; x++) {
		writechunk[CMD_BYTE(x, 0, 0)] |= leds;
	}
}

static inline void cmd_dequeue(struct wctdm *wc, unsigned char *writechunk, int card, int pos)
{
	unsigned long flags;
	unsigned int curcmd=0;
	int x;
	int subaddr = card & 0x3;
#ifdef FANCY_ECHOCAN
	int ecval;
	ecval = wc->echocanpos;
	ecval += EC_SIZE_Q * ectrans[(card & 0x3)];
	ecval = ecval % EC_SIZE;
#endif

 	/* if a QRV card, map it to its first channel */  
 	if ((wc->modtype[card] ==  MOD_TYPE_QRV) && (card & 3))
 	{
 		return;
 	}
 	if (wc->altcs[card])
 		subaddr = 0;
 

 
	/* Skip audio */
	writechunk += 24;
	spin_lock_irqsave(&wc->reglock, flags);
	/* Search for something waiting to transmit */
	if (pos) {
		for (x=0;x<MAX_COMMANDS;x++) {
			if ((wc->cmdq[card].cmds[x] & (__CMD_RD | __CMD_WR)) && 
			   !(wc->cmdq[card].cmds[x] & (__CMD_TX | __CMD_FIN))) {
			   	curcmd = wc->cmdq[card].cmds[x];
#if 0
				printk(KERN_DEBUG "Transmitting command '%08x' in slot %d\n", wc->cmdq[card].cmds[x], wc->txident);
#endif			
				wc->cmdq[card].cmds[x] |= (wc->txident << 24) | __CMD_TX;
				break;
			}
		}
	}
	if (!curcmd) {
		/* If nothing else, use filler */
		if (wc->modtype[card] == MOD_TYPE_FXS)
			curcmd = CMD_RD(LINE_STATE);
		else if (wc->modtype[card] == MOD_TYPE_FXO)
			curcmd = CMD_RD(12);
		else if (wc->modtype[card] == MOD_TYPE_QRV)
			curcmd = CMD_RD(3);
		else if (wc->modtype[card] == MOD_TYPE_VPM) {
#ifdef FANCY_ECHOCAN
			if (wc->blinktimer >= 0xf) {
				curcmd = CMD_WR(0x1ab, 0x0f);
			} else if (wc->blinktimer == (ectab[ecval] >> 1)) {
				curcmd = CMD_WR(0x1ab, 0x00);
			} else
#endif
			curcmd = CMD_RD(0x1a0);
		}
	}
	if (wc->modtype[card] == MOD_TYPE_FXS) {
 		writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = (1 << (subaddr));
		if (curcmd & __CMD_WR)
 			writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = (curcmd >> 8) & 0x7f;
		else
 			writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x80 | ((curcmd >> 8) & 0x7f);
 		writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = curcmd & 0xff;
	} else if (wc->modtype[card] == MOD_TYPE_FXO) {
		if (curcmd & __CMD_WR)
 			writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x20 | fxo_addrs[subaddr];
		else
 			writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x60 | fxo_addrs[subaddr];
 		writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = (curcmd >> 8) & 0xff;
 		writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = curcmd & 0xff;
	} else if (wc->modtype[card] == MOD_TYPE_FXSINIT) {
		/* Special case, we initialize the FXS's into the three-byte command mode then
		   switch to the regular mode.  To send it into thee byte mode, treat the path as
		   6 two-byte commands and in the last one we initialize register 0 to 0x80. All modules
		   read this as the command to switch to daisy chain mode and we're done.  */
 		writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x00;
 		writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x00;
		if ((card & 0x1) == 0x1) 
 			writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = 0x80;
		else
 			writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = 0x00;
	} else if (wc->modtype[card] == MOD_TYPE_VPM) {
		if (curcmd & __CMD_WR)
 			writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = ((card & 0x3) << 4) | 0xc | ((curcmd >> 16) & 0x1);
		else
 			writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = ((card & 0x3) << 4) | 0xa | ((curcmd >> 16) & 0x1);
 		writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = (curcmd >> 8) & 0xff;
 		writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = curcmd & 0xff;
	} else if (wc->modtype[card] == MOD_TYPE_VPM150M) {
		;
 	} else if (wc->modtype[card] == MOD_TYPE_QRV) {
 
 		writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x00;
 		if (!curcmd)
 		{
 			writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x00;
 			writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = 0x00;
 		}
 		else
 		{
 			if (curcmd & __CMD_WR)
 				writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x40 | ((curcmd >> 8) & 0x3f);
 			else
 				writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0xc0 | ((curcmd >> 8) & 0x3f);
 			writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = curcmd & 0xff;
 		}
	} else if (wc->modtype[card] == MOD_TYPE_NONE) {
 		writechunk[CMD_BYTE(card, 0, wc->altcs[card])] = 0x00;
 		writechunk[CMD_BYTE(card, 1, wc->altcs[card])] = 0x00;
 		writechunk[CMD_BYTE(card, 2, wc->altcs[card])] = 0x00;
	}
#if 0
	/* XXX */
	if (cmddesc < 40)
		printk(KERN_DEBUG "Pass %d, card = %d (modtype=%d), pos = %d, CMD_BYTES = %d,%d,%d, (%02x,%02x,%02x) curcmd = %08x\n", cmddesc, card, wc->modtype[card], pos, CMD_BYTE(card, 0), CMD_BYTE(card, 1), CMD_BYTE(card, 2), writechunk[CMD_BYTE(card, 0)], writechunk[CMD_BYTE(card, 1)], writechunk[CMD_BYTE(card, 2)], curcmd);
#endif
	spin_unlock_irqrestore(&wc->reglock, flags);
#if 0
	/* XXX */
	cmddesc++;
#endif	
}

static inline void cmd_decipher_vpmadt032(struct wctdm *wc, unsigned char *readchunk)
{
	unsigned long flags;
	struct vpmadt032 *vpm = wc->vpmadt032;
	struct vpmadt032_cmd *cmd;

	BUG_ON(!vpm);

	/* If the hardware is not processing any commands currently, then
	 * there is nothing for us to do here. */
	if (list_empty(&vpm->active_cmds)) {
		return;
	}

	spin_lock_irqsave(&vpm->list_lock, flags);
	cmd = list_entry(vpm->active_cmds.next, struct vpmadt032_cmd, node);
	if (wc->rxident == cmd->txident) {
		list_del_init(&cmd->node);
	} else {
		cmd = NULL;
	}
	spin_unlock_irqrestore(&vpm->list_lock, flags);

	if (!cmd) {
		return;
	}

	/* Skip audio */
	readchunk += 24;
	/* Store result */
	cmd->data = (0xff & readchunk[CMD_BYTE(25, 1, 0)]) << 8;
	cmd->data |= readchunk[CMD_BYTE(25, 2, 0)];
	if (cmd->desc & __VPM150M_WR) {
		/* Writes do not need any acknowledgement */
		list_add_tail(&cmd->node, &vpm->free_cmds);
	} else {
		cmd->desc |= __VPM150M_FIN;
		complete(&cmd->complete);
	}
#if 0
	// if (printk_ratelimit()) 
		printk(KERN_DEBUG "Received txident = %d, desc = 0x%x, addr = 0x%x, data = 0x%x\n", cmd->txident, cmd->desc, cmd->address, cmd->data);
#endif
}

static inline void cmd_decipher(struct wctdm *wc, u8 *readchunk, int card)
{
	unsigned long flags;
	unsigned char ident;
	int x;

	/* if a QRV card, map it to its first channel */  
	if ((wc->modtype[card] ==  MOD_TYPE_QRV) && (card & 3))
	{
		return;
	}
	/* Skip audio */
	readchunk += 24;
	spin_lock_irqsave(&wc->reglock, flags);
	/* Search for any pending results */
	for (x=0;x<MAX_COMMANDS;x++) {
		if ((wc->cmdq[card].cmds[x] & (__CMD_RD | __CMD_WR)) && 
		    (wc->cmdq[card].cmds[x] & (__CMD_TX)) && 
		   !(wc->cmdq[card].cmds[x] & (__CMD_FIN))) {
		   	ident = (wc->cmdq[card].cmds[x] >> 24) & 0xff;
		   	if (ident == wc->rxident) {
				/* Store result */
				wc->cmdq[card].cmds[x] |= readchunk[CMD_BYTE(card, 2, wc->altcs[card])];
				wc->cmdq[card].cmds[x] |= __CMD_FIN;
				if (wc->cmdq[card].cmds[x] & __CMD_WR) {
					/* Go ahead and clear out writes since they need no acknowledgement */
					wc->cmdq[card].cmds[x] = 0x00000000;
				} else if (x >= USER_COMMANDS) {
					/* Clear out ISR reads */
					wc->cmdq[card].isrshadow[x - USER_COMMANDS] = wc->cmdq[card].cmds[x] & 0xff;
					wc->cmdq[card].cmds[x] = 0x00000000;
				}
				break;
			}
		}
	}
#if 0
	/* XXX */
	if (!pos && (cmddesc < 256))
		printk(KERN_DEBUG "Card %d: Command '%08x' => %02x\n",card,  wc->cmdq[card].lasttx[pos], wc->cmdq[card].lastrd[pos]);
#endif
	spin_unlock_irqrestore(&wc->reglock, flags);
}

static inline void cmd_checkisr(struct wctdm *wc, int card)
{
	if (!wc->cmdq[card].cmds[USER_COMMANDS + 0]) {
		if (wc->sethook[card]) {
			wc->cmdq[card].cmds[USER_COMMANDS + 0] = wc->sethook[card];
			wc->sethook[card] = 0;
		} else if (wc->modtype[card] == MOD_TYPE_FXS) {
			wc->cmdq[card].cmds[USER_COMMANDS + 0] = CMD_RD(68);	/* Hook state */
		} else if (wc->modtype[card] == MOD_TYPE_FXO) {
			wc->cmdq[card].cmds[USER_COMMANDS + 0] = CMD_RD(5);	/* Hook/Ring state */
		} else if (wc->modtype[card] == MOD_TYPE_QRV) {
			wc->cmdq[card & 0xfc].cmds[USER_COMMANDS + 0] = CMD_RD(3);	/* COR/CTCSS state */
#ifdef VPM_SUPPORT
		} else if (wc->modtype[card] == MOD_TYPE_VPM) {
			wc->cmdq[card].cmds[USER_COMMANDS + 0] = CMD_RD(0xb9); /* DTMF interrupt */
#endif
		}
	}
	if (!wc->cmdq[card].cmds[USER_COMMANDS + 1]) {
		if (wc->modtype[card] == MOD_TYPE_FXS) {
#ifdef PAQ_DEBUG
			wc->cmdq[card].cmds[USER_COMMANDS + 1] = CMD_RD(19);	/* Transistor interrupts */
#else
			wc->cmdq[card].cmds[USER_COMMANDS + 1] = CMD_RD(LINE_STATE);
#endif
		} else if (wc->modtype[card] == MOD_TYPE_FXO) {
			wc->cmdq[card].cmds[USER_COMMANDS + 1] = CMD_RD(29);	/* Battery */
		} else if (wc->modtype[card] == MOD_TYPE_QRV) {
			wc->cmdq[card & 0xfc].cmds[USER_COMMANDS + 1] = CMD_RD(3);	/* Battery */
#ifdef VPM_SUPPORT
		} else if (wc->modtype[card] == MOD_TYPE_VPM) {
			wc->cmdq[card].cmds[USER_COMMANDS + 1] = CMD_RD(0xbd); /* DTMF interrupt */
#endif
		}
	}
}

static inline void wctdm_transmitprep(struct wctdm *wc, unsigned char *writechunk)
{
	int x,y;

	/* Calculate Transmission */
	if (likely(wc->initialized)) {
		dahdi_transmit(&wc->span);
	}

	for (x=0;x<DAHDI_CHUNKSIZE;x++) {
		/* Send a sample, as a 32-bit word */
		for (y=0;y < wc->cards;y++) {
			if (!x) {
				cmd_checkisr(wc, y);
			}

			if (likely(wc->initialized)) {
				if (y < wc->desc->ports)
					writechunk[y] = wc->chans[y]->writechunk[x];
			}
			cmd_dequeue(wc, writechunk, y, x);
		}
		if (!x)
			wc->blinktimer++;
		if (wc->vpm100) {
			for (y=24;y<28;y++) {
				if (!x) {
					cmd_checkisr(wc, y);
				}
				cmd_dequeue(wc, writechunk, y, x);
			}
#ifdef FANCY_ECHOCAN
			if (wc->vpm100 && wc->blinktimer >= 0xf) {
				wc->blinktimer = -1;
				wc->echocanpos++;
			}
#endif			
		} else if (wc->vpmadt032) {
			cmd_dequeue_vpmadt032(wc, writechunk, x);
		}

		if (x < DAHDI_CHUNKSIZE - 1) {
			writechunk[EFRAME_SIZE] = wc->ctlreg;
			writechunk[EFRAME_SIZE + 1] = wc->txident++;

			if ((wc->desc->ports == 4) && ((wc->ctlreg & 0x10) || (wc->modtype[NUM_CARDS] == MOD_TYPE_NONE))) {
				writechunk[EFRAME_SIZE + 2] = 0;
				for (y = 0; y < 4; y++) {
					if (wc->modtype[y] == MOD_TYPE_NONE)
						writechunk[EFRAME_SIZE + 2] |= (1 << y);
				}
			} else
				writechunk[EFRAME_SIZE + 2] = 0xf;
		}
		writechunk += (EFRAME_SIZE + EFRAME_GAP);
	}
}

static inline int wctdm_setreg_full(struct wctdm *wc, int card, int addr, int val, int inisr)
{
	unsigned long flags;
	int hit=0;
	int ret;

	/* if a QRV card, use only its first channel */  
	if (wc->modtype[card] ==  MOD_TYPE_QRV)
	{
		if (card & 3) return(0);
	}
	do {
		spin_lock_irqsave(&wc->reglock, flags);
		hit = empty_slot(wc, card);
		if (hit > -1) {
			wc->cmdq[card].cmds[hit] = CMD_WR(addr, val);
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (inisr)
			break;
		if (hit < 0) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit < 0);
	return (hit > -1) ? 0 : -1;
}

static inline int wctdm_setreg_intr(struct wctdm *wc, int card, int addr, int val)
{
	return wctdm_setreg_full(wc, card, addr, val, 1);
}
static inline int wctdm_setreg(struct wctdm *wc, int card, int addr, int val)
{
	return wctdm_setreg_full(wc, card, addr, val, 0);
}

static inline int wctdm_getreg(struct wctdm *wc, int card, int addr)
{
	unsigned long flags;
	int hit;
	int ret=0;

	/* if a QRV card, use only its first channel */  
	if (wc->modtype[card] ==  MOD_TYPE_QRV)
	{
		if (card & 3) return(0);
	}
	do {
		spin_lock_irqsave(&wc->reglock, flags);
		hit = empty_slot(wc, card);
		if (hit > -1) {
			wc->cmdq[card].cmds[hit] = CMD_RD(addr);
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (hit < 0) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit < 0);
	do {
		spin_lock_irqsave(&wc->reglock, flags);
		if (wc->cmdq[card].cmds[hit] & __CMD_FIN) {
			ret = wc->cmdq[card].cmds[hit] & 0xff;
			wc->cmdq[card].cmds[hit] = 0x00000000;
			hit = -1;
		}
		spin_unlock_irqrestore(&wc->reglock, flags);
		if (hit > -1) {
			if ((ret = schluffen(&wc->regq)))
				return ret;
		}
	} while (hit > -1);
	return ret;
}

static inline unsigned char wctdm_vpm_in(struct wctdm *wc, int unit, const unsigned int addr)
{
	return wctdm_getreg(wc, unit + NUM_CARDS, addr);
}

static inline void wctdm_vpm_out(struct wctdm *wc, int unit, const unsigned int addr, const unsigned char val)
{
	wctdm_setreg(wc, unit + NUM_CARDS, addr, val);
}

/* TODO: this should go in the dahdi_voicebus module... */
static inline void cmd_vpmadt032_retransmit(struct wctdm *wc)
{
	unsigned long flags;
	struct vpmadt032 *vpmadt032 = wc->vpmadt032;
	struct vpmadt032_cmd *cmd, *temp;

	BUG_ON(!vpmadt032);

	/* By moving the commands back to the pending list, they will be
	 * transmitted when room is available */
	spin_lock_irqsave(&vpmadt032->list_lock, flags);
	list_for_each_entry_safe(cmd, temp, &vpmadt032->active_cmds, node) {
		cmd->desc &= ~(__VPM150M_TX);
		list_move_tail(&cmd->node, &vpmadt032->pending_cmds);
	}
	spin_unlock_irqrestore(&vpmadt032->list_lock, flags);
}

static inline void cmd_retransmit(struct wctdm *wc)
{
	int x,y;
	unsigned long flags;
	/* Force retransmissions */
	spin_lock_irqsave(&wc->reglock, flags);
	for (x=0;x<MAX_COMMANDS;x++) {
		for (y=0;y<wc->cards;y++) {
			if (!(wc->cmdq[y].cmds[x] & __CMD_FIN))
				wc->cmdq[y].cmds[x] &= ~(__CMD_TX | (0xff << 24));
		}
	}
	spin_unlock_irqrestore(&wc->reglock, flags);
#ifdef VPM_SUPPORT
	if (wc->vpmadt032)
		cmd_vpmadt032_retransmit(wc);
#endif
}

static inline void wctdm_receiveprep(struct wctdm *wc, unsigned char *readchunk)
{
	int x,y;
	unsigned char expected;

	BUG_ON(NULL == readchunk);

	for (x=0;x<DAHDI_CHUNKSIZE;x++) {
		if (x < DAHDI_CHUNKSIZE - 1) {
			expected = wc->rxident+1;
			wc->rxident = readchunk[EFRAME_SIZE + 1];
			if (wc->rxident != expected) {
				wc->span.irqmisses++;
				cmd_retransmit(wc);
			}
		}
		for (y=0;y < wc->cards;y++) {
			if (likely(wc->initialized) && (y < wc->desc->ports))
				wc->chans[y]->readchunk[x] = readchunk[y];
			cmd_decipher(wc, readchunk, y);
		}
		if (wc->vpm100) {
			for (y=NUM_CARDS;y < NUM_CARDS + NUM_EC; y++)
				cmd_decipher(wc, readchunk, y);
		} else if (wc->vpmadt032) {
			cmd_decipher_vpmadt032(wc, readchunk);
		}

		readchunk += (EFRAME_SIZE + EFRAME_GAP);
	}
	/* XXX We're wasting 8 taps.  We should get closer :( */
	if (likely(wc->initialized)) {
		for (x = 0; x < wc->desc->ports; x++) {
			if (wc->cardflag & (1 << x))
				dahdi_ec_chunk(wc->chans[x], wc->chans[x]->readchunk, wc->chans[x]->writechunk);
		}
		dahdi_receive(&wc->span);
	}
	/* Wake up anyone sleeping to read/write a new register */
	wake_up_interruptible(&wc->regq);
}

static int wait_access(struct wctdm *wc, int card)
{
    unsigned char data=0;
    long origjiffies;
    int count = 0;

    #define MAX 10 /* attempts */


    origjiffies = jiffies;
    /* Wait for indirect access */
    while (count++ < MAX)
	 {
		data = wctdm_getreg(wc, card, I_STATUS);

		if (!data)
			return 0;

	 }

    if(count > (MAX-1)) printk(KERN_NOTICE " ##### Loop error (%02x) #####\n", data);

	return 0;
}

static unsigned char translate_3215(unsigned char address)
{
	int x;
	for (x = 0; x < ARRAY_SIZE(indirect_regs); x++) {
		if (indirect_regs[x].address == address) {
			address = indirect_regs[x].altaddr;
			break;
		}
	}
	return address;
}

static int wctdm_proslic_setreg_indirect(struct wctdm *wc, int card, unsigned char address, unsigned short data)
{
	int res = -1;
	/* Translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215) {
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	if(!wait_access(wc, card)) {
		wctdm_setreg(wc, card, IDA_LO,(unsigned char)(data & 0xFF));
		wctdm_setreg(wc, card, IDA_HI,(unsigned char)((data & 0xFF00)>>8));
		wctdm_setreg(wc, card, IAA,address);
		res = 0;
	};
	return res;
}

static int wctdm_proslic_getreg_indirect(struct wctdm *wc, int card, unsigned char address)
{ 
	int res = -1;
	char *p=NULL;
	/* Translate 3215 addresses */
	if (wc->flags[card] & FLAG_3215) {
		address = translate_3215(address);
		if (address == 255)
			return 0;
	}
	if (!wait_access(wc, card)) {
		wctdm_setreg(wc, card, IAA, address);
		if (!wait_access(wc, card)) {
			unsigned char data1, data2;
			data1 = wctdm_getreg(wc, card, IDA_LO);
			data2 = wctdm_getreg(wc, card, IDA_HI);
			res = data1 | (data2 << 8);
		} else
			p = "Failed to wait inside\n";
	} else
		p = "failed to wait\n";
	if (p)
		printk(KERN_NOTICE "%s", p);
	return res;
}

static int wctdm_proslic_init_indirect_regs(struct wctdm *wc, int card)
{
	unsigned char i;

	for (i = 0; i < ARRAY_SIZE(indirect_regs); i++)
	{
		if(wctdm_proslic_setreg_indirect(wc, card, indirect_regs[i].address,indirect_regs[i].initial))
			return -1;
	}

	return 0;
}

static int wctdm_proslic_verify_indirect_regs(struct wctdm *wc, int card)
{ 
	int passed = 1;
	unsigned short i, initial;
	int j;

	for (i = 0; i < ARRAY_SIZE(indirect_regs); i++) 
	{
		if((j = wctdm_proslic_getreg_indirect(wc, card, (unsigned char) indirect_regs[i].address)) < 0) {
			printk(KERN_NOTICE "Failed to read indirect register %d\n", i);
			return -1;
		}
		initial= indirect_regs[i].initial;

		if ( j != initial && (!(wc->flags[card] & FLAG_3215) || (indirect_regs[i].altaddr != 255)))
		{
			 printk(KERN_NOTICE "!!!!!!! %s  iREG %X = %X  should be %X\n",
				indirect_regs[i].name,indirect_regs[i].address,j,initial );
			 passed = 0;
		}	
	}

    if (passed) {
		if (debug & DEBUG_CARD)
			printk(KERN_DEBUG "Init Indirect Registers completed successfully.\n");
    } else {
		printk(KERN_NOTICE " !!!!! Init Indirect Registers UNSUCCESSFULLY.\n");
		return -1;
    }
    return 0;
}

static inline void wctdm_proslic_recheck_sanity(struct wctdm *wc, int card)
{
	struct fxs *const fxs = &wc->mods[card].fxs;
	int res;
	unsigned long flags;
#ifdef PAQ_DEBUG
	res = wc->cmdq[card].isrshadow[1];
	res &= ~0x3;
	if (res) {
		wc->cmdq[card].isrshadow[1]=0;
		fxs->palarms++;
		if (fxs->palarms < MAX_ALARMS) {
			printk(KERN_NOTICE "Power alarm (%02x) on module %d, resetting!\n", res, card + 1);
			wc->sethook[card] = CMD_WR(19, res);
			/* Update shadow register to avoid extra power alarms until next read */
			wc->cmdq[card].isrshadow[1] = 0;
		} else {
			if (fxs->palarms == MAX_ALARMS)
				printk(KERN_NOTICE "Too many power alarms on card %d, NOT resetting!\n", card + 1);
		}
	}
#else
	spin_lock_irqsave(&fxs->lasttxhooklock, flags);
	res = wc->cmdq[card].isrshadow[1];
	/* This makes sure the lasthook was put in reg 64 the linefeed reg */
	if (((res & SLIC_LF_SETMASK) | SLIC_LF_OPPENDING) == fxs->lasttxhook)
		fxs->lasttxhook &= SLIC_LF_SETMASK;

	res = !res &&    /* reg 64 has to be zero at last isr read */
		!(fxs->lasttxhook & SLIC_LF_OPPENDING) && /* not a transition */
		fxs->lasttxhook; /* not an intended zero */
	spin_unlock_irqrestore(&fxs->lasttxhooklock, flags);
	
	if (res) {
		fxs->palarms++;
		if (fxs->palarms < MAX_ALARMS) {
			printk(KERN_NOTICE "Power alarm on module %d, resetting!\n", card + 1);
			spin_lock_irqsave(&fxs->lasttxhooklock, flags);
			if (fxs->lasttxhook == SLIC_LF_RINGING) {
				fxs->lasttxhook = POLARITY_XOR(card) ?
							SLIC_LF_ACTIVE_REV :
							SLIC_LF_ACTIVE_FWD;;
			}
			fxs->lasttxhook |= SLIC_LF_OPPENDING;
			wc->sethook[card] = CMD_WR(LINE_STATE, fxs->lasttxhook);
			spin_unlock_irqrestore(&fxs->lasttxhooklock, flags);

			/* Update shadow register to avoid extra power alarms until next read */
			wc->cmdq[card].isrshadow[1] = fxs->lasttxhook;
		} else {
			if (fxs->palarms == MAX_ALARMS)
				printk(KERN_NOTICE "Too many power alarms on card %d, NOT resetting!\n", card + 1);
		}
	}
#endif
}

static inline void wctdm_qrvdri_check_hook(struct wctdm *wc, int card)
{
	signed char b,b1;
	int qrvcard = card & 0xfc;

	
	if (wc->qrvdebtime[card] >= 2) wc->qrvdebtime[card]--;
	b = wc->cmdq[qrvcard].isrshadow[0];	/* Hook/Ring state */
	b &= 0xcc; /* use bits 3-4 and 6-7 only */

	if (wc->radmode[qrvcard] & RADMODE_IGNORECOR) b &= ~4;
	else if (!(wc->radmode[qrvcard] & RADMODE_INVERTCOR)) b ^= 4;
	if (wc->radmode[qrvcard + 1] | RADMODE_IGNORECOR) b &= ~0x40;
	else if (!(wc->radmode[qrvcard + 1] | RADMODE_INVERTCOR)) b ^= 0x40;

	if ((wc->radmode[qrvcard] & RADMODE_IGNORECT) || 
		(!(wc->radmode[qrvcard] & RADMODE_EXTTONE))) b &= ~8;
	else if (!(wc->radmode[qrvcard] & RADMODE_EXTINVERT)) b ^= 8;
	if ((wc->radmode[qrvcard + 1] & RADMODE_IGNORECT) || 
		(!(wc->radmode[qrvcard + 1] & RADMODE_EXTTONE))) b &= ~0x80;
	else if (!(wc->radmode[qrvcard + 1] & RADMODE_EXTINVERT)) b ^= 0x80;
	/* now b & MASK should be zero, if its active */
	/* check for change in chan 0 */
	if ((!(b & 0xc)) != wc->qrvhook[qrvcard + 2])
	{
		wc->qrvdebtime[qrvcard] = wc->debouncetime[qrvcard];
		wc->qrvhook[qrvcard + 2] = !(b & 0xc);
	} 
	/* if timed-out and ready */
	if (wc->qrvdebtime[qrvcard] == 1)
	{
		b1 = wc->qrvhook[qrvcard + 2];
if (debug) printk(KERN_DEBUG "QRV channel %d rx state changed to %d\n",qrvcard,wc->qrvhook[qrvcard + 2]);
		dahdi_hooksig(wc->chans[qrvcard], 
			(b1) ? DAHDI_RXSIG_OFFHOOK : DAHDI_RXSIG_ONHOOK);
		wc->qrvdebtime[card] = 0;
	}
	/* check for change in chan 1 */
	if ((!(b & 0xc0)) != wc->qrvhook[qrvcard + 3])
	{
		wc->qrvdebtime[qrvcard + 1] = QRV_DEBOUNCETIME;
		wc->qrvhook[qrvcard + 3] = !(b & 0xc0);
	}
	if (wc->qrvdebtime[qrvcard + 1] == 1)
	{
		b1 = wc->qrvhook[qrvcard + 3];
if (debug) printk(KERN_DEBUG "QRV channel %d rx state changed to %d\n",qrvcard + 1,wc->qrvhook[qrvcard + 3]);
		dahdi_hooksig(wc->chans[qrvcard + 1], 
			(b1) ? DAHDI_RXSIG_OFFHOOK : DAHDI_RXSIG_ONHOOK);
		wc->qrvdebtime[card] = 0;
	}
	return;
}

static inline void wctdm_voicedaa_check_hook(struct wctdm *wc, int card)
{
#define MS_PER_CHECK_HOOK 1

	unsigned char res;
	signed char b;
	unsigned int abs_voltage;
	struct fxo *fxo = &wc->mods[card].fxo;

	/* Try to track issues that plague slot one FXO's */
	b = wc->cmdq[card].isrshadow[0];	/* Hook/Ring state */
	b &= 0x9b;
	if (fxo->offhook) {
		if (b != 0x9)
			wctdm_setreg_intr(wc, card, 5, 0x9);
	} else {
		if (b != 0x8)
			wctdm_setreg_intr(wc, card, 5, 0x8);
	}
	if (!fxo->offhook) {
		if(fwringdetect || neonmwi_monitor) {
			/* Look for ring status bits (Ring Detect Signal Negative and
			* Ring Detect Signal Positive) to transition back and forth
			* some number of times to indicate that a ring is occurring.
			* Provide some number of samples to allow for the transitions
			* to occur before ginving up.
			* NOTE: neon mwi voltages will trigger one of these bits to go active
			* but not to have transitions between the two bits (i.e. no negative
			* to positive or positive to negative transversals )
			*/
			res =  wc->cmdq[card].isrshadow[0] & 0x60;
			if (0 == wc->mods[card].fxo.wasringing) {
				if (res) {
					/* Look for positive/negative crossings in ring status reg */
					fxo->wasringing = 2;
					fxo->ringdebounce = ringdebounce /16;
					fxo->lastrdtx = res;
					fxo->lastrdtx_count = 0;
				}
			} else if (2 == fxo->wasringing) {
				/* If ring detect signal has transversed */
				if (res && res != fxo->lastrdtx) {
					/* if there are at least 3 ring polarity transversals */
					if(++fxo->lastrdtx_count >= 2) {
						fxo->wasringing = 1;
						if (debug)
							printk("FW RING on %d/%d!\n", wc->span.spanno, card + 1);
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_RING);
						fxo->ringdebounce = ringdebounce / 16;
					} else {
						fxo->lastrdtx = res;
						fxo->ringdebounce = ringdebounce / 16;
					}
					/* ring indicator (positve or negative) has not transitioned, check debounce count */
				} else if (--fxo->ringdebounce == 0) {
					fxo->wasringing = 0;
				}
			} else {  /* I am in ring state */
				if (res) { /* If any ringdetect bits are still active */
					fxo->ringdebounce = ringdebounce / 16;
				} else if (--fxo->ringdebounce == 0) {
					fxo->wasringing = 0;
					if (debug)
						printk("FW NO RING on %d/%d!\n", wc->span.spanno, card + 1);
					dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
				}
			}
		} else {
			res =  wc->cmdq[card].isrshadow[0];
			if ((res & 0x60) && (fxo->battery == BATTERY_PRESENT)) {
				fxo->ringdebounce += (DAHDI_CHUNKSIZE * 16);
				if (fxo->ringdebounce >= DAHDI_CHUNKSIZE * ringdebounce) {
					if (!fxo->wasringing) {
						fxo->wasringing = 1;
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_RING);
						if (debug)
							printk(KERN_DEBUG "RING on %d/%d!\n", wc->span.spanno, card + 1);
					}
					fxo->ringdebounce = DAHDI_CHUNKSIZE * ringdebounce;
				}
			} else {
				fxo->ringdebounce -= DAHDI_CHUNKSIZE * 4;
				if (fxo->ringdebounce <= 0) {
					if (fxo->wasringing) {
						fxo->wasringing = 0;
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
						if (debug)
							printk(KERN_DEBUG "NO RING on %d/%d!\n", wc->span.spanno, card + 1);
					}
					fxo->ringdebounce = 0;
				}
					
			}
		}
	}

	b = wc->cmdq[card].isrshadow[1]; /* Voltage */
	abs_voltage = abs(b);

	if (fxovoltage) {
		if (!(wc->intcount % 100)) {
			printk(KERN_INFO "Port %d: Voltage: %d  Debounce %d\n", card + 1, 
			       b, fxo->battdebounce);
		}
	}

	if (unlikely(DAHDI_RXSIG_INITIAL == wc->chans[card]->rxhooksig)) {
		/*
		 * dahdi-base will set DAHDI_RXSIG_INITIAL after a
		 * DAHDI_STARTUP or DAHDI_CHANCONFIG ioctl so that new events
		 * will be queued on the channel with the current received
		 * hook state.  Channels that use robbed-bit signalling always
		 * report the current received state via the dahdi_rbsbits
		 * call. Since we only call dahdi_hooksig when we've detected
		 * a change to report, let's forget our current state in order
		 * to force us to report it again via dahdi_hooksig.
		 *
		 */
		fxo->battery = BATTERY_UNKNOWN;
	}

	if (abs_voltage < battthresh) {
		/* possible existing states:
		   battery lost, no debounce timer
		   battery lost, debounce timer (going to battery present)
		   battery present or unknown, no debounce timer
		   battery present or unknown, debounce timer (going to battery lost)
		*/

		if (fxo->battery == BATTERY_LOST) {
			if (fxo->battdebounce) {
				/* we were going to BATTERY_PRESENT, but battery was lost again,
				   so clear the debounce timer */
				fxo->battdebounce = 0;
			}
		} else {
			if (fxo->battdebounce) {
				/* going to BATTERY_LOST, see if we are there yet */
				if (--fxo->battdebounce == 0) {
					fxo->battery = BATTERY_LOST;
					if (debug)
						printk(KERN_DEBUG "NO BATTERY on %d/%d!\n", wc->span.spanno, card + 1);
#ifdef	JAPAN
					if (!wc->ohdebounce && wc->offhook) {
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_ONHOOK);
						if (debug)
							printk(KERN_DEBUG "Signalled On Hook\n");
#ifdef	ZERO_BATT_RING
						wc->onhook++;
#endif
					}
#else
					dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_ONHOOK);
					/* set the alarm timer, taking into account that part of its time
					   period has already passed while debouncing occurred */
					fxo->battalarm = (battalarm - battdebounce) / MS_PER_CHECK_HOOK;
#endif
				}
			} else {
				/* start the debounce timer to verify that battery has been lost */
				fxo->battdebounce = battdebounce / MS_PER_CHECK_HOOK;
			}
		}
	} else {
		/* possible existing states:
		   battery lost or unknown, no debounce timer
		   battery lost or unknown, debounce timer (going to battery present)
		   battery present, no debounce timer
		   battery present, debounce timer (going to battery lost)
		*/

		if (fxo->battery == BATTERY_PRESENT) {
			if (fxo->battdebounce) {
				/* we were going to BATTERY_LOST, but battery appeared again,
				   so clear the debounce timer */
				fxo->battdebounce = 0;
			}
		} else {
			if (fxo->battdebounce) {
				/* going to BATTERY_PRESENT, see if we are there yet */
				if (--fxo->battdebounce == 0) {
					fxo->battery = BATTERY_PRESENT;
					if (debug)
						printk(KERN_DEBUG "BATTERY on %d/%d (%s)!\n", wc->span.spanno, card + 1, 
						       (b < 0) ? "-" : "+");			    
#ifdef	ZERO_BATT_RING
					if (wc->onhook) {
						wc->onhook = 0;
						dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
						if (debug)
							printk(KERN_DEBUG "Signalled Off Hook\n");
					}
#else
					dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
#endif
					/* set the alarm timer, taking into account that part of its time
					   period has already passed while debouncing occurred */
					fxo->battalarm = (battalarm - battdebounce) / MS_PER_CHECK_HOOK;
				}
			} else {
				/* start the debounce timer to verify that battery has appeared */
				fxo->battdebounce = battdebounce / MS_PER_CHECK_HOOK;
			}
		}

		if (fxo->lastpol >= 0) {
			if (b < 0) {
				fxo->lastpol = -1;
				fxo->polaritydebounce = POLARITY_DEBOUNCE / MS_PER_CHECK_HOOK;
			}
		} 
		if (fxo->lastpol <= 0) {
			if (b > 0) {
				fxo->lastpol = 1;
				fxo->polaritydebounce = POLARITY_DEBOUNCE / MS_PER_CHECK_HOOK;
			}
		}
	}

	if (fxo->battalarm) {
		if (--fxo->battalarm == 0) {
			/* the alarm timer has expired, so update the battery alarm state
			   for this channel */
			dahdi_alarm_channel(wc->chans[card], fxo->battery == BATTERY_LOST ? DAHDI_ALARM_RED : DAHDI_ALARM_NONE);
		}
	}

	if (fxo->polaritydebounce) {
	        fxo->polaritydebounce--;
		if (fxo->polaritydebounce < 1) {
		    if (fxo->lastpol != fxo->polarity) {
			if (debug & DEBUG_CARD)
				printk(KERN_DEBUG "%lu Polarity reversed (%d -> %d)\n", jiffies, 
				       fxo->polarity, 
				       fxo->lastpol);
			if (fxo->polarity)
				dahdi_qevent_lock(wc->chans[card], DAHDI_EVENT_POLARITY);
			fxo->polarity = fxo->lastpol;
		    }
		}
	}
	/* Look for neon mwi pulse */
	if (neonmwi_monitor && !wc->mods[card].fxo.offhook) {
		/* Look for 4 consecutive voltage readings
		* where the voltage is over the neon limit but
		* does not vary greatly from the last reading
		*/
		if (fxo->battery == 1 &&
				  abs_voltage > neonmwi_level &&
				  (0 == fxo->neonmwi_last_voltage ||
				  (b >= fxo->neonmwi_last_voltage - neonmwi_envelope &&
				  b <= fxo->neonmwi_last_voltage + neonmwi_envelope ))) {
			fxo->neonmwi_last_voltage = b;
			if (NEONMWI_ON_DEBOUNCE == fxo->neonmwi_debounce) {
				fxo->neonmwi_offcounter = neonmwi_offlimit_cycles;
				if(0 == fxo->neonmwi_state) {
					dahdi_qevent_lock(wc->chans[card], DAHDI_EVENT_NEONMWI_ACTIVE);
					fxo->neonmwi_state = 1;
					if (debug)
						printk("NEON MWI active for card %d\n", card+1);
				}
				fxo->neonmwi_debounce++;  /* terminate the processing */
			} else if (NEONMWI_ON_DEBOUNCE > fxo->neonmwi_debounce) {
				fxo->neonmwi_debounce++;
			} else { /* Insure the count gets reset */
				fxo->neonmwi_offcounter = neonmwi_offlimit_cycles;
			}
		} else {
			fxo->neonmwi_debounce = 0;
			fxo->neonmwi_last_voltage = 0;
		}
		/* If no neon mwi pulse for given period of time, indicte no neon mwi state */
		if (fxo->neonmwi_state && 0 < fxo->neonmwi_offcounter ) {
			fxo->neonmwi_offcounter--;
			if (0 == fxo->neonmwi_offcounter) {
				dahdi_qevent_lock(wc->chans[card], DAHDI_EVENT_NEONMWI_INACTIVE);
				fxo->neonmwi_state = 0;
				if (debug)
					printk("NEON MWI cleared for card %d\n", card+1);
			}
		}
	}
#undef MS_PER_CHECK_HOOK
}

static void wctdm_fxs_off_hook(struct wctdm *wc, const int card)
{
	struct fxs *const fxs = &wc->mods[card].fxs;

	if (debug & DEBUG_CARD)
		printk(KERN_DEBUG "wctdm: Card %d Going off hook\n", card);
	switch (fxs->lasttxhook) {
	case SLIC_LF_RINGING:		/* Ringing */
	case SLIC_LF_OHTRAN_FWD:	/* Forward On Hook Transfer */
	case SLIC_LF_OHTRAN_REV:	/* Reverse On Hook Transfer */
		/* just detected OffHook, during Ringing or OnHookTransfer */
		fxs->idletxhookstate = POLARITY_XOR(card) ?
						SLIC_LF_ACTIVE_REV :
						SLIC_LF_ACTIVE_FWD;
		break;
	}
	dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_OFFHOOK);
	if (robust)
		wctdm_init_proslic(wc, card, 1, 0, 1);
	fxs->oldrxhook = 1;
}

static void wctdm_fxs_on_hook(struct wctdm *wc, const int card)
{
	struct fxs *const fxs = &wc->mods[card].fxs;
	if (debug & DEBUG_CARD)
		printk(KERN_DEBUG "wctdm: Card %d Going on hook\n", card);
	dahdi_hooksig(wc->chans[card], DAHDI_RXSIG_ONHOOK);
	fxs->oldrxhook = 0;
}

static inline void wctdm_proslic_check_hook(struct wctdm *wc, const int card)
{
	struct fxs *const fxs = &wc->mods[card].fxs;
	char res;
	int hook;

	/* For some reason we have to debounce the
	   hook detector.  */

	res = wc->cmdq[card].isrshadow[0];	/* Hook state */
	hook = (res & 1);
	
	if (hook != fxs->lastrxhook) {
		/* Reset the debounce (must be multiple of 4ms) */
		fxs->debounce = 8 * (4 * 8);
#if 0
		printk(KERN_DEBUG "Resetting debounce card %d hook %d, %d\n",
		       card, hook, fxs->debounce);
#endif
	} else {
		if (fxs->debounce > 0) {
			fxs->debounce -= 4 * DAHDI_CHUNKSIZE;
#if 0
			printk(KERN_DEBUG "Sustaining hook %d, %d\n",
			       hook, fxs->debounce);
#endif
			if (!fxs->debounce) {
#if 0
				printk(KERN_DEBUG "Counted down debounce, newhook: %d...\n", hook);
#endif
				fxs->debouncehook = hook;
			}

			if (!fxs->oldrxhook && fxs->debouncehook)
				wctdm_fxs_off_hook(wc, card);
			else if (fxs->oldrxhook && !fxs->debouncehook)
				wctdm_fxs_on_hook(wc, card);
		}
	}
	fxs->lastrxhook = hook;
}

static inline void wctdm_vpm_check(struct wctdm *wc, int x)
{
	if (wc->cmdq[x].isrshadow[0]) {
		if (debug & DEBUG_ECHOCAN)
			printk(KERN_DEBUG "VPM: Detected dtmf ON channel %02x on chip %d!\n", wc->cmdq[x].isrshadow[0], x - NUM_CARDS);
		wc->sethook[x] = CMD_WR(0xb9, wc->cmdq[x].isrshadow[0]);
		wc->cmdq[x].isrshadow[0] = 0;
		/* Cancel most recent lookup, if there is one */
		wc->cmdq[x].cmds[USER_COMMANDS+0] = 0x00000000; 
	} else if (wc->cmdq[x].isrshadow[1]) {
		if (debug & DEBUG_ECHOCAN)
			printk(KERN_DEBUG "VPM: Detected dtmf OFF channel %02x on chip %d!\n", wc->cmdq[x].isrshadow[1], x - NUM_CARDS);
		wc->sethook[x] = CMD_WR(0xbd, wc->cmdq[x].isrshadow[1]);
		wc->cmdq[x].isrshadow[1] = 0;
		/* Cancel most recent lookup, if there is one */
		wc->cmdq[x].cmds[USER_COMMANDS+1] = 0x00000000; 
	}
}

static int echocan_create(struct dahdi_chan *chan, struct dahdi_echocanparams *ecp,
			  struct dahdi_echocanparam *p, struct dahdi_echocan_state **ec)
{
	struct wctdm *wc = chan->pvt;
	const struct dahdi_echocan_ops *ops;
	const struct dahdi_echocan_features *features;

	if (!wc->vpm100 && !wc->vpmadt032)
		return -ENODEV;

	if (wc->vpmadt032) {
		ops = &vpm150m_ec_ops;
		features = &vpm150m_ec_features;
	} else {
		ops = &vpm100m_ec_ops;
		features = &vpm100m_ec_features;
	}

	if (wc->vpm100 && (ecp->param_count > 0)) {
		printk(KERN_WARNING "%s echo canceller does not support parameters; failing request\n", ops->name);
		return -EINVAL;
	}

	*ec = wc->ec[chan->chanpos - 1];
	(*ec)->ops = ops;
	(*ec)->features = *features;

	if (wc->vpm100) {
		int channel;
		int unit;

		channel = (chan->chanpos - 1);
		unit = (chan->chanpos - 1) & 0x3;
		if (wc->vpm100 < 2)
			channel >>= 2;
	
		if (debug & DEBUG_ECHOCAN)
			printk(KERN_DEBUG "echocan: Unit is %d, Channel is %d length %d\n", unit, channel, ecp->tap_length);

		wctdm_vpm_out(wc, unit, channel, 0x3e);
		return 0;
	} else if (wc->vpmadt032) {
		return vpmadt032_echocan_create(wc->vpmadt032,
			chan->chanpos-1, ecp, p);
	} else {
		return -ENODEV;
	}
}

static void echocan_free(struct dahdi_chan *chan, struct dahdi_echocan_state *ec)
{
	struct wctdm *wc = chan->pvt;

	memset(ec, 0, sizeof(*ec));
	if (wc->vpm100) {
		int channel;
		int unit;

		channel = (chan->chanpos - 1);
		unit = (chan->chanpos - 1) & 0x3;
		if (wc->vpm100 < 2)
			channel >>= 2;

		if (debug & DEBUG_ECHOCAN)
			printk(KERN_DEBUG "echocan: Unit is %d, Channel is %d length 0\n",
			       unit, channel);
		wctdm_vpm_out(wc, unit, channel, 0x01);
	} else if (wc->vpmadt032) {
		vpmadt032_echocan_free(wc->vpmadt032, chan->chanpos - 1, ec);
	}
}

static void wctdm_isr_misc_fxs(struct wctdm *wc, int card)
{
	struct fxs *const fxs = &wc->mods[card].fxs;
	unsigned long flags;

	if (!(wc->intcount % 10000)) {
		/* Accept an alarm once per 10 seconds */
		if (fxs->palarms)
			fxs->palarms--;
	}
	wctdm_proslic_check_hook(wc, card);
	if (!(wc->intcount & 0xfc))
		wctdm_proslic_recheck_sanity(wc, card);
	if (SLIC_LF_RINGING == fxs->lasttxhook) {
		/* RINGing, prepare for OHT */
		fxs->ohttimer = OHT_TIMER << 3;
		/* OHT mode when idle */
		fxs->idletxhookstate = POLARITY_XOR(card) ? SLIC_LF_OHTRAN_REV :
							    SLIC_LF_OHTRAN_FWD;
	} else if (fxs->ohttimer) {
		fxs->ohttimer -= DAHDI_CHUNKSIZE;
		if (fxs->ohttimer)
			return;

		/* Switch to active */
		fxs->idletxhookstate = POLARITY_XOR(card) ? SLIC_LF_ACTIVE_REV :
							    SLIC_LF_ACTIVE_FWD;
		spin_lock_irqsave(&fxs->lasttxhooklock, flags);
		if (SLIC_LF_OHTRAN_FWD == fxs->lasttxhook) {
			/* Apply the change if appropriate */
			fxs->lasttxhook = SLIC_LF_OPPENDING | SLIC_LF_ACTIVE_FWD;
			/* Data enqueued here */
			wc->sethook[card] = CMD_WR(LINE_STATE, fxs->lasttxhook);
		} else if (SLIC_LF_OHTRAN_REV == fxs->lasttxhook) {
			/* Apply the change if appropriate */
			fxs->lasttxhook = SLIC_LF_OPPENDING | SLIC_LF_ACTIVE_REV;
			/* Data enqueued here */
			wc->sethook[card] = CMD_WR(LINE_STATE, fxs->lasttxhook);
		}
		spin_unlock_irqrestore(&fxs->lasttxhooklock, flags);
	}
}

static inline void wctdm_isr_misc(struct wctdm *wc)
{
	int x;

	if (unlikely(!wc->initialized)) {
		return;
	}

	for (x=0;x<wc->cards;x++) {
		if (wc->cardflag & (1 << x)) {
			if (wc->modtype[x] == MOD_TYPE_FXS) {
				wctdm_isr_misc_fxs(wc, x);
			} else if (wc->modtype[x] == MOD_TYPE_FXO) {
				wctdm_voicedaa_check_hook(wc, x);
			} else if (wc->modtype[x] == MOD_TYPE_QRV) {
				wctdm_qrvdri_check_hook(wc, x);
			}
		}
	}
	if (wc->vpm100 > 0) {
		for (x=NUM_CARDS;x<NUM_CARDS+NUM_EC;x++) {
			wctdm_vpm_check(wc, x);
		}
	}
}

static void handle_receive(void* vbb, void* context)
{
	struct wctdm *wc = context;
	wc->rxints++;
	wctdm_receiveprep(wc, vbb);
}

static void handle_transmit(void* vbb, void* context)
{
	struct wctdm *wc = context;
	memset(vbb, 0, SFRAME_SIZE);
	wc->txints++;
	wctdm_transmitprep(wc, vbb);
	wctdm_isr_misc(wc);
	wc->intcount++;
	voicebus_transmit(wc->vb, vbb);
}

static int wctdm_voicedaa_insane(struct wctdm *wc, int card)
{
	int blah;
	blah = wctdm_getreg(wc, card, 2);
	if (blah != 0x3)
		return -2;
	blah = wctdm_getreg(wc, card, 11);
	if (debug & DEBUG_CARD)
		printk(KERN_DEBUG "VoiceDAA System: %02x\n", blah & 0xf);
	return 0;
}

static int wctdm_proslic_insane(struct wctdm *wc, int card)
{
	int blah, reg1, insane_report;
	insane_report=0;

	blah = wctdm_getreg(wc, card, 0);
	if (debug & DEBUG_CARD) 
		printk(KERN_DEBUG "ProSLIC on module %d, product %d, version %d\n", card, (blah & 0x30) >> 4, (blah & 0xf));

#if 0
	if ((blah & 0x30) >> 4) {
		printk(KERN_DEBUG "ProSLIC on module %d is not a 3210.\n", card);
		return -1;
	}
#endif
	if (((blah & 0xf) == 0) || ((blah & 0xf) == 0xf)) {
		/* SLIC not loaded */
		return -1;
	}

	/* let's be really sure this is an FXS before we continue */
	reg1 = wctdm_getreg(wc, card, 1);
	if ((0x80 != (blah & 0xf0)) || (0x88 != reg1)) {
		if (debug & DEBUG_CARD)
			printk("DEBUG: not FXS b/c reg0=%x or reg1 != 0x88 (%x).\n", blah, reg1);
		return -1;
	}

	if ((blah & 0xf) < 2) {
		printk(KERN_INFO "ProSLIC 3210 version %d is too old\n", blah & 0xf);
		return -1;
	}
	if (wctdm_getreg(wc, card, 1) & 0x80)
		/* ProSLIC 3215, not a 3210 */
		wc->flags[card] |= FLAG_3215;

	blah = wctdm_getreg(wc, card, 8);
	if (blah != 0x2) {
		printk(KERN_NOTICE "ProSLIC on module %d insane (1) %d should be 2\n", card, blah);
		return -1;
	} else if ( insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 8 Reads %d Expected is 0x2\n",card,blah);

	blah = wctdm_getreg(wc, card, 64);
	if (blah != 0x0) {
		printk(KERN_NOTICE "ProSLIC on module %d insane (2)\n", card);
		return -1;
	} else if ( insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 64 Reads %d Expected is 0x0\n",card,blah);

	blah = wctdm_getreg(wc, card, 11);
	if (blah != 0x33) {
		printk(KERN_NOTICE "ProSLIC on module %d insane (3)\n", card);
		return -1;
	} else if ( insane_report)
		printk(KERN_NOTICE "ProSLIC on module %d Reg 11 Reads %d Expected is 0x33\n",card,blah);

	/* Just be sure it's setup right. */
	wctdm_setreg(wc, card, 30, 0);

	if (debug & DEBUG_CARD) 
		printk(KERN_DEBUG "ProSLIC on module %d seems sane.\n", card);
	return 0;
}

static int wctdm_proslic_powerleak_test(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	unsigned char vbat;

	/* Turn off linefeed */
	wctdm_setreg(wc, card, LINE_STATE, 0);

	/* Power down */
	wctdm_setreg(wc, card, 14, 0x10);

	/* Wait for one second */
	origjiffies = jiffies;

	while((vbat = wctdm_getreg(wc, card, 82)) > 0x6) {
		if ((jiffies - origjiffies) >= (HZ/2))
			break;;
	}

	if (vbat < 0x06) {
		printk(KERN_NOTICE "Excessive leakage detected on module %d: %d volts (%02x) after %d ms\n", card,
		       376 * vbat / 1000, vbat, (int)((jiffies - origjiffies) * 1000 / HZ));
		return -1;
	} else if (debug & DEBUG_CARD) {
		printk(KERN_DEBUG "Post-leakage voltage: %d volts\n", 376 * vbat / 1000);
	}
	return 0;
}

static int wctdm_powerup_proslic(struct wctdm *wc, int card, int fast)
{
	unsigned char vbat;
	unsigned long origjiffies;
	int lim;

	/* Set period of DC-DC converter to 1/64 khz */
	wctdm_setreg(wc, card, 92, 0xc0 /* was 0xff */);

	/* Wait for VBat to powerup */
	origjiffies = jiffies;

	/* Disable powerdown */
	wctdm_setreg(wc, card, 14, 0);

	/* If fast, don't bother checking anymore */
	if (fast)
		return 0;

	while((vbat = wctdm_getreg(wc, card, 82)) < 0xc0) {
		/* Wait no more than 500ms */
		if ((jiffies - origjiffies) > HZ/2) {
			break;
		}
	}

	if (vbat < 0xc0) {
		printk(KERN_NOTICE "ProSLIC on module %d failed to powerup within %d ms (%d mV only)\n\n -- DID YOU REMEMBER TO PLUG IN THE HD POWER CABLE TO THE TDM CARD??\n",
		       card, (int)(((jiffies - origjiffies) * 1000 / HZ)),
			vbat * 375);
		return -1;
	} else if (debug & DEBUG_CARD) {
		printk(KERN_DEBUG "ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",
		       card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
	}

        /* Proslic max allowed loop current, reg 71 LOOP_I_LIMIT */
        /* If out of range, just set it to the default value     */
        lim = (loopcurrent - 20) / 3;
        if ( loopcurrent > 41 ) {
                lim = 0;
                if (debug & DEBUG_CARD)
                        printk(KERN_DEBUG "Loop current out of range! Setting to default 20mA!\n");
        }
        else if (debug & DEBUG_CARD)
                        printk(KERN_DEBUG "Loop current set to %dmA!\n",(lim*3)+20);
        wctdm_setreg(wc,card,LOOP_I_LIMIT,lim);

	/* Engage DC-DC converter */
	wctdm_setreg(wc, card, 93, 0x19 /* was 0x19 */);
#if 0
	origjiffies = jiffies;
	while(0x80 & wctdm_getreg(wc, card, 93)) {
		if ((jiffies - origjiffies) > 2 * HZ) {
			printk(KERN_DEBUG "Timeout waiting for DC-DC calibration on module %d\n", card);
			return -1;
		}
	}

#if 0
	/* Wait a full two seconds */
	while((jiffies - origjiffies) < 2 * HZ);

	/* Just check to be sure */
	vbat = wctdm_getreg(wc, card, 82);
	printk(KERN_DEBUG "ProSLIC on module %d powered up to -%d volts (%02x) in %d ms\n",
		       card, vbat * 376 / 1000, vbat, (int)(((jiffies - origjiffies) * 1000 / HZ)));
#endif
#endif
	return 0;

}

static int wctdm_proslic_manual_calibrate(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	unsigned char i;

	wctdm_setreg(wc, card, 21, 0);//(0)  Disable all interupts in DR21
	wctdm_setreg(wc, card, 22, 0);//(0)Disable all interupts in DR21
	wctdm_setreg(wc, card, 23, 0);//(0)Disable all interupts in DR21
	wctdm_setreg(wc, card, 64, 0);//(0)

	wctdm_setreg(wc, card, 97, 0x18); //(0x18)Calibrations without the ADC and DAC offset and without common mode calibration.
	wctdm_setreg(wc, card, 96, 0x47); //(0x47)	Calibrate common mode and differential DAC mode DAC + ILIM

	origjiffies=jiffies;
	while( wctdm_getreg(wc,card,96)!=0 ){
		if((jiffies-origjiffies)>80)
			return -1;
	}
//Initialized DR 98 and 99 to get consistant results.
// 98 and 99 are the results registers and the search should have same intial conditions.

/*******************************The following is the manual gain mismatch calibration****************************/
/*******************************This is also available as a function *******************************************/
	// Delay 10ms
	origjiffies=jiffies; 
	while((jiffies-origjiffies)<1);
	wctdm_proslic_setreg_indirect(wc, card, 88,0);
	wctdm_proslic_setreg_indirect(wc,card,89,0);
	wctdm_proslic_setreg_indirect(wc,card,90,0);
	wctdm_proslic_setreg_indirect(wc,card,91,0);
	wctdm_proslic_setreg_indirect(wc,card,92,0);
	wctdm_proslic_setreg_indirect(wc,card,93,0);

	wctdm_setreg(wc, card, 98,0x10); // This is necessary if the calibration occurs other than at reset time
	wctdm_setreg(wc, card, 99,0x10);

	for ( i=0x1f; i>0; i--)
	{
		wctdm_setreg(wc, card, 98,i);
		origjiffies=jiffies; 
		while((jiffies-origjiffies)<4);
		if((wctdm_getreg(wc,card,88)) == 0)
			break;
	} // for

	for ( i=0x1f; i>0; i--)
	{
		wctdm_setreg(wc, card, 99,i);
		origjiffies=jiffies; 
		while((jiffies-origjiffies)<4);
		if((wctdm_getreg(wc,card,89)) == 0)
			break;
	}//for

/*******************************The preceding is the manual gain mismatch calibration****************************/
/**********************************The following is the longitudinal Balance Cal***********************************/
	wctdm_setreg(wc,card,64,1);
	while((jiffies-origjiffies)<10); // Sleep 100?

	wctdm_setreg(wc, card, 64, 0);
	wctdm_setreg(wc, card, 23, 0x4);  // enable interrupt for the balance Cal
	wctdm_setreg(wc, card, 97, 0x1); // this is a singular calibration bit for longitudinal calibration
	wctdm_setreg(wc, card, 96,0x40);

	wctdm_getreg(wc,card,96); /* Read Reg 96 just cause */

	wctdm_setreg(wc, card, 21, 0xFF);
	wctdm_setreg(wc, card, 22, 0xFF);
	wctdm_setreg(wc, card, 23, 0xFF);

	/**The preceding is the longitudinal Balance Cal***/
	return(0);

}

static int wctdm_proslic_calibrate(struct wctdm *wc, int card)
{
	unsigned long origjiffies;
	int x;
	/* Perform all calibrations */
	wctdm_setreg(wc, card, 97, 0x1f);
	
	/* Begin, no speedup */
	wctdm_setreg(wc, card, 96, 0x5f);

	/* Wait for it to finish */
	origjiffies = jiffies;
	while(wctdm_getreg(wc, card, 96)) {
		if ((jiffies - origjiffies) > 2 * HZ) {
			printk(KERN_NOTICE "Timeout waiting for calibration of module %d\n", card);
			return -1;
		}
	}
	
	if (debug & DEBUG_CARD) {
		/* Print calibration parameters */
		printk(KERN_DEBUG "Calibration Vector Regs 98 - 107: \n");
		for (x=98;x<108;x++) {
			printk(KERN_DEBUG "%d: %02x\n", x, wctdm_getreg(wc, card, x));
		}
	}
	return 0;
}

static void wait_just_a_bit(int foo)
{
	long newjiffies;
	newjiffies = jiffies + foo;
	while(jiffies < newjiffies);
}

/*********************************************************************
 * Set the hwgain on the analog modules
 *
 * card = the card position for this module (0-23)
 * gain = gain in dB x10 (e.g. -3.5dB  would be gain=-35)
 * tx = (0 for rx; 1 for tx)
 *
 *******************************************************************/
static int wctdm_set_hwgain(struct wctdm *wc, int card, __s32 gain, __u32 tx)
{
	if (!(wc->modtype[card] == MOD_TYPE_FXO)) {
		printk(KERN_NOTICE "Cannot adjust gain.  Unsupported module type!\n");
		return -1;
	}
	if (tx) {
		if (debug)
			printk(KERN_DEBUG "setting FXO tx gain for card=%d to %d\n", card, gain);
		if (gain >=  -150 && gain <= 0) {
			wctdm_setreg(wc, card, 38, 16 + (gain/-10));
			wctdm_setreg(wc, card, 40, 16 + (-gain%10));
		} else if (gain <= 120 && gain > 0) {
			wctdm_setreg(wc, card, 38, gain/10);
			wctdm_setreg(wc, card, 40, (gain%10));
		} else {
			printk(KERN_NOTICE "FXO tx gain is out of range (%d)\n", gain);
			return -1;
		}
	} else { /* rx */
		if (debug)
			printk(KERN_DEBUG "setting FXO rx gain for card=%d to %d\n", card, gain);
		if (gain >=  -150 && gain <= 0) {
			wctdm_setreg(wc, card, 39, 16+ (gain/-10));
			wctdm_setreg(wc, card, 41, 16 + (-gain%10));
		} else if (gain <= 120 && gain > 0) {
			wctdm_setreg(wc, card, 39, gain/10);
			wctdm_setreg(wc, card, 41, (gain%10));
		} else {
			printk(KERN_NOTICE "FXO rx gain is out of range (%d)\n", gain);
			return -1;
		}
	}

	return 0;
}

static int set_lasttxhook_interruptible(struct fxs *fxs, unsigned newval, int * psethook)
{
	int res = 0;
	unsigned long flags;
	int timeout = 0;

	do {
		spin_lock_irqsave(&fxs->lasttxhooklock, flags);
		if (SLIC_LF_OPPENDING & fxs->lasttxhook) {
			spin_unlock_irqrestore(&fxs->lasttxhooklock, flags);
			if (timeout++ > 100)
				return -1;
			msleep(1);
		} else {
			fxs->lasttxhook = (newval & SLIC_LF_SETMASK) | SLIC_LF_OPPENDING;
			*psethook = CMD_WR(LINE_STATE, fxs->lasttxhook);
			spin_unlock_irqrestore(&fxs->lasttxhooklock, flags);
			break;
		}
	} while (1);

	return res;
}

/* Must be called from within an interruptible context */
static int set_vmwi(struct wctdm *wc, int chan_idx)
{
	int x;
	struct fxs *const fxs = &wc->mods[chan_idx].fxs;

	/* Presently only supports line reversal MWI */
	if ((fxs->vmwi_active_messages) &&
	    (fxs->vmwisetting.vmwi_type & DAHDI_VMWI_LREV))
		fxs->vmwi_linereverse = 1;
	else
		fxs->vmwi_linereverse = 0;

	/* Set line polarity for new VMWI state */
	if (POLARITY_XOR(chan_idx)) {
		fxs->idletxhookstate |= SLIC_LF_OPPENDING | SLIC_LF_REVMASK;
		/* Do not set while currently ringing or open */
		if (((fxs->lasttxhook & SLIC_LF_SETMASK) != SLIC_LF_RINGING)  &&
		    ((fxs->lasttxhook & SLIC_LF_SETMASK) != SLIC_LF_OPEN)) {
			x = fxs->lasttxhook;
			x |= SLIC_LF_REVMASK;
			set_lasttxhook_interruptible(fxs, x, &wc->sethook[chan_idx]);
		}
	} else {
		fxs->idletxhookstate &= ~SLIC_LF_REVMASK;
		/* Do not set while currently ringing or open */
		if (((fxs->lasttxhook & SLIC_LF_SETMASK) != SLIC_LF_RINGING) &&
		    ((fxs->lasttxhook & SLIC_LF_SETMASK) != SLIC_LF_OPEN)) {
			x = fxs->lasttxhook;
			x &= ~SLIC_LF_REVMASK;
			set_lasttxhook_interruptible(fxs, x, &wc->sethook[chan_idx]);
		}
	}
	if (debug) {
		printk(KERN_DEBUG
		       "Setting VMWI on channel %d, messages=%d, lrev=%d\n",
		       chan_idx, fxs->vmwi_active_messages,
		       fxs->vmwi_linereverse);
	}
	return 0;
}

static int wctdm_init_voicedaa(struct wctdm *wc, int card, int fast, int manual, int sane)
{
	unsigned char reg16=0, reg26=0, reg30=0, reg31=0;
	long newjiffies;

	if (wc->modtype[card & 0xfc] == MOD_TYPE_QRV) return -2;

	wc->modtype[card] = MOD_TYPE_NONE;
	/* Wait just a bit */
	wait_just_a_bit(HZ/10);

	wc->modtype[card] = MOD_TYPE_FXO;
	wait_just_a_bit(HZ/10);

	if (!sane && wctdm_voicedaa_insane(wc, card))
		return -2;

	/* Software reset */
	wctdm_setreg(wc, card, 1, 0x80);

	/* Wait just a bit */
	wait_just_a_bit(HZ/10);

	/* Enable PCM, ulaw */
	if (alawoverride) {
		wctdm_setreg(wc, card, 33, 0x20);
	} else {
		wctdm_setreg(wc, card, 33, 0x28);
	}

	/* Set On-hook speed, Ringer impedence, and ringer threshold */
	reg16 |= (fxo_modes[_opermode].ohs << 6);
	reg16 |= (fxo_modes[_opermode].rz << 1);
	reg16 |= (fxo_modes[_opermode].rt);
	wctdm_setreg(wc, card, 16, reg16);

	if(fwringdetect || neonmwi_monitor) {
		/* Enable ring detector full-wave rectifier mode */
		wctdm_setreg(wc, card, 18, 2);
		wctdm_setreg(wc, card, 24, 0);
	} else { 
		/* Set to the device defaults */
		wctdm_setreg(wc, card, 18, 0);
		wctdm_setreg(wc, card, 24, 0x19);
	}
	
	/* Enable ring detector full-wave rectifier mode */
	wctdm_setreg(wc, card, 18, 2);
	wctdm_setreg(wc, card, 24, 0);
	
	/* Set DC Termination:
	   Tip/Ring voltage adjust, minimum operational current, current limitation */
	reg26 |= (fxo_modes[_opermode].dcv << 6);
	reg26 |= (fxo_modes[_opermode].mini << 4);
	reg26 |= (fxo_modes[_opermode].ilim << 1);
	wctdm_setreg(wc, card, 26, reg26);

	/* Set AC Impedence */
	reg30 = (fxo_modes[_opermode].acim);
	wctdm_setreg(wc, card, 30, reg30);

	/* Misc. DAA parameters */
	reg31 = 0xa3;
	reg31 |= (fxo_modes[_opermode].ohs2 << 3);
	wctdm_setreg(wc, card, 31, reg31);

	/* Set Transmit/Receive timeslot */
	wctdm_setreg(wc, card, 34, (card * 8) & 0xff);
	wctdm_setreg(wc, card, 35, (card * 8) >> 8);
	wctdm_setreg(wc, card, 36, (card * 8) & 0xff);
	wctdm_setreg(wc, card, 37, (card * 8) >> 8);

	/* Enable ISO-Cap */
	wctdm_setreg(wc, card, 6, 0x00);

	/* Wait 1000ms for ISO-cap to come up */
	newjiffies = jiffies;
	newjiffies += 2 * HZ;
	while((jiffies < newjiffies) && !(wctdm_getreg(wc, card, 11) & 0xf0))
		wait_just_a_bit(HZ/10);

	if (!(wctdm_getreg(wc, card, 11) & 0xf0)) {
		printk(KERN_NOTICE "VoiceDAA did not bring up ISO link properly!\n");
		return -1;
	}
	if (debug & DEBUG_CARD)
		printk(KERN_DEBUG "ISO-Cap is now up, line side: %02x rev %02x\n", 
		       wctdm_getreg(wc, card, 11) >> 4,
		       (wctdm_getreg(wc, card, 13) >> 2) & 0xf);
	/* Enable on-hook line monitor */
	wctdm_setreg(wc, card, 5, 0x08);
	
	/* Take values for fxotxgain and fxorxgain and apply them to module */
	wctdm_set_hwgain(wc, card, fxotxgain, 1);
	wctdm_set_hwgain(wc, card, fxorxgain, 0);

	if(debug)
		printk(KERN_DEBUG "DEBUG fxotxgain:%i.%i fxorxgain:%i.%i\n", (wctdm_getreg(wc, card, 38)/16) ? -(wctdm_getreg(wc, card, 38) - 16) : wctdm_getreg(wc, card, 38), (wctdm_getreg(wc, card, 40)/16) ? -(wctdm_getreg(wc, card, 40) - 16) : wctdm_getreg(wc, card, 40), (wctdm_getreg(wc, card, 39)/16) ? -(wctdm_getreg(wc, card, 39) - 16): wctdm_getreg(wc, card, 39), (wctdm_getreg(wc, card, 41)/16)?-(wctdm_getreg(wc, card, 41) - 16) : wctdm_getreg(wc, card, 41));
	
	return 0;
		
}

static int wctdm_init_proslic(struct wctdm *wc, int card, int fast, int manual, int sane)
{

	unsigned short tmp[5];
	unsigned char r19,r9;
	int x;
	int fxsmode=0;

	if (wc->modtype[card & 0xfc] == MOD_TYPE_QRV) return -2;

	/* Sanity check the ProSLIC */
	if (!sane && wctdm_proslic_insane(wc, card))
		return -2;

	/* Initialize VMWI settings */
	memset(&(wc->mods[card].fxs.vmwisetting), 0, sizeof(wc->mods[card].fxs.vmwisetting));
	wc->mods[card].fxs.vmwi_linereverse = 0;

	/* By default, don't send on hook */
	if (!reversepolarity != !wc->mods[card].fxs.reversepolarity) {
		wc->mods[card].fxs.idletxhookstate = SLIC_LF_ACTIVE_REV;
	} else {
		wc->mods[card].fxs.idletxhookstate = SLIC_LF_ACTIVE_FWD;
	}

	if (sane) {
		/* Make sure we turn off the DC->DC converter to prevent anything from blowing up */
		wctdm_setreg(wc, card, 14, 0x10);
	}

	if (wctdm_proslic_init_indirect_regs(wc, card)) {
		printk(KERN_INFO "Indirect Registers failed to initialize on module %d.\n", card);
		return -1;
	}

	/* Clear scratch pad area */
	wctdm_proslic_setreg_indirect(wc, card, 97,0);

	/* Clear digital loopback */
	wctdm_setreg(wc, card, 8, 0);

	/* Revision C optimization */
	wctdm_setreg(wc, card, 108, 0xeb);

	/* Disable automatic VBat switching for safety to prevent
	   Q7 from accidently turning on and burning out. */
	wctdm_setreg(wc, card, 67, 0x07); /* If pulse dialing has trouble at high REN
					     loads change this to 0x17 */

	/* Turn off Q7 */
	wctdm_setreg(wc, card, 66, 1);

	/* Flush ProSLIC digital filters by setting to clear, while
	   saving old values */
	for (x=0;x<5;x++) {
		tmp[x] = wctdm_proslic_getreg_indirect(wc, card, x + 35);
		wctdm_proslic_setreg_indirect(wc, card, x + 35, 0x8000);
	}

	/* Power up the DC-DC converter */
	if (wctdm_powerup_proslic(wc, card, fast)) {
		printk(KERN_NOTICE "Unable to do INITIAL ProSLIC powerup on module %d\n", card);
		return -1;
	}

	if (!fast) {
		spin_lock_init(&wc->mods[card].fxs.lasttxhooklock);

		/* Check for power leaks */
		if (wctdm_proslic_powerleak_test(wc, card)) {
			printk(KERN_NOTICE "ProSLIC module %d failed leakage test.  Check for short circuit\n", card);
		}
		/* Power up again */
		if (wctdm_powerup_proslic(wc, card, fast)) {
			printk(KERN_NOTICE "Unable to do FINAL ProSLIC powerup on module %d\n", card);
			return -1;
		}
#ifndef NO_CALIBRATION
		/* Perform calibration */
		if(manual) {
			if (wctdm_proslic_manual_calibrate(wc, card)) {
				//printk(KERN_NOTICE "Proslic failed on Manual Calibration\n");
				if (wctdm_proslic_manual_calibrate(wc, card)) {
					printk(KERN_NOTICE "Proslic Failed on Second Attempt to Calibrate Manually. (Try -DNO_CALIBRATION in Makefile)\n");
					return -1;
				}
				printk(KERN_INFO "Proslic Passed Manual Calibration on Second Attempt\n");
			}
		}
		else {
			if(wctdm_proslic_calibrate(wc, card))  {
				//printk(KERN_NOTICE "ProSlic died on Auto Calibration.\n");
				if (wctdm_proslic_calibrate(wc, card)) {
					printk(KERN_NOTICE "Proslic Failed on Second Attempt to Auto Calibrate\n");
					return -1;
				}
				printk(KERN_INFO "Proslic Passed Auto Calibration on Second Attempt\n");
			}
		}
		/* Perform DC-DC calibration */
		wctdm_setreg(wc, card, 93, 0x99);
		r19 = wctdm_getreg(wc, card, 107);
		if ((r19 < 0x2) || (r19 > 0xd)) {
			printk(KERN_NOTICE "DC-DC cal has a surprising direct 107 of 0x%02x!\n", r19);
			wctdm_setreg(wc, card, 107, 0x8);
		}

		/* Save calibration vectors */
		for (x=0;x<NUM_CAL_REGS;x++)
			wc->mods[card].fxs.calregs.vals[x] = wctdm_getreg(wc, card, 96 + x);
#endif

	} else {
		/* Restore calibration registers */
		for (x=0;x<NUM_CAL_REGS;x++)
			wctdm_setreg(wc, card, 96 + x, wc->mods[card].fxs.calregs.vals[x]);
	}
	/* Calibration complete, restore original values */
	for (x=0;x<5;x++) {
		wctdm_proslic_setreg_indirect(wc, card, x + 35, tmp[x]);
	}

	if (wctdm_proslic_verify_indirect_regs(wc, card)) {
		printk(KERN_INFO "Indirect Registers failed verification.\n");
		return -1;
	}


#if 0
    /* Disable Auto Power Alarm Detect and other "features" */
    wctdm_setreg(wc, card, 67, 0x0e);
    blah = wctdm_getreg(wc, card, 67);
#endif

#if 0
    if (wctdm_proslic_setreg_indirect(wc, card, 97, 0x0)) { // Stanley: for the bad recording fix
		 printk(KERN_INFO "ProSlic IndirectReg Died.\n");
		 return -1;
	}
#endif

    if (alawoverride)
    	wctdm_setreg(wc, card, 1, 0x20);
    else
    	wctdm_setreg(wc, card, 1, 0x28);
 	// U-Law 8-bit interface
    wctdm_setreg(wc, card, 2, (card * 8) & 0xff);    // Tx Start count low byte  0
    wctdm_setreg(wc, card, 3, (card * 8) >> 8);    // Tx Start count high byte 0
    wctdm_setreg(wc, card, 4, (card * 8) & 0xff);    // Rx Start count low byte  0
    wctdm_setreg(wc, card, 5, (card * 8) >> 8);    // Rx Start count high byte 0
    wctdm_setreg(wc, card, 18, 0xff);     // clear all interrupt
    wctdm_setreg(wc, card, 19, 0xff);
    wctdm_setreg(wc, card, 20, 0xff);
    wctdm_setreg(wc, card, 22, 0xff);
    wctdm_setreg(wc, card, 73, 0x04);
	if (fxshonormode) {
		fxsmode = acim2tiss[fxo_modes[_opermode].acim];
		wctdm_setreg(wc, card, 10, 0x08 | fxsmode);
		if (fxo_modes[_opermode].ring_osc)
			wctdm_proslic_setreg_indirect(wc, card, 20, fxo_modes[_opermode].ring_osc);
		if (fxo_modes[_opermode].ring_x)
			wctdm_proslic_setreg_indirect(wc, card, 21, fxo_modes[_opermode].ring_x);
	}
    if (lowpower)
    	wctdm_setreg(wc, card, 72, 0x10);

#if 0
    wctdm_setreg(wc, card, 21, 0x00); 	// enable interrupt
    wctdm_setreg(wc, card, 22, 0x02); 	// Loop detection interrupt
    wctdm_setreg(wc, card, 23, 0x01); 	// DTMF detection interrupt
#endif

#if 0
    /* Enable loopback */
    wctdm_setreg(wc, card, 8, 0x2);
    wctdm_setreg(wc, card, 14, 0x0);
    wctdm_setreg(wc, card, 64, 0x0);
    wctdm_setreg(wc, card, 1, 0x08);
#endif

	if (fastringer) {
		/* Speed up Ringer */
		wctdm_proslic_setreg_indirect(wc, card, 20, 0x7e6d);
		wctdm_proslic_setreg_indirect(wc, card, 21, 0x01b9);
		/* Beef up Ringing voltage to 89V */
		if (boostringer) {
			wctdm_setreg(wc, card, 74, 0x3f);
			if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x247)) 
				return -1;
			printk(KERN_INFO "Boosting fast ringer on slot %d (89V peak)\n", card + 1);
		} else if (lowpower) {
			if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x14b)) 
				return -1;
			printk(KERN_INFO "Reducing fast ring power on slot %d (50V peak)\n", card + 1);
		} else
			printk(KERN_INFO "Speeding up ringer on slot %d (25Hz)\n", card + 1);
	} else {
		/* Beef up Ringing voltage to 89V */
		if (boostringer) {
			wctdm_setreg(wc, card, 74, 0x3f);
			if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x1d1)) 
				return -1;
			printk(KERN_INFO "Boosting ringer on slot %d (89V peak)\n", card + 1);
		} else if (lowpower) {
			if (wctdm_proslic_setreg_indirect(wc, card, 21, 0x108)) 
				return -1;
			printk(KERN_INFO "Reducing ring power on slot %d (50V peak)\n", card + 1);
		}
	}

	if (fxstxgain || fxsrxgain) {
		r9 = wctdm_getreg(wc, card, 9);
		switch (fxstxgain) {
		
			case 35:
				r9+=8;
				break;
			case -35:
				r9+=4;
				break;
			case 0: 
				break;
		}
	
		switch (fxsrxgain) {
			
			case 35:
				r9+=2;
				break;
			case -35:
				r9+=1;
				break;
			case 0:
				break;
		}
		wctdm_setreg(wc, card, 9, r9);
	}

	if (debug)
		printk(KERN_DEBUG "DEBUG: fxstxgain:%s fxsrxgain:%s\n",((wctdm_getreg(wc, card, 9)/8) == 1)?"3.5":(((wctdm_getreg(wc,card,9)/4) == 1)?"-3.5":"0.0"),((wctdm_getreg(wc, card, 9)/2) == 1)?"3.5":((wctdm_getreg(wc,card,9)%2)?"-3.5":"0.0"));

	wc->mods[card].fxs.lasttxhook = wc->mods[card].fxs.idletxhookstate;
	wctdm_setreg(wc, card, LINE_STATE, wc->mods[card].fxs.lasttxhook);
	return 0;
}

static int wctdm_init_qrvdri(struct wctdm *wc, int card)
{
	unsigned char x,y;
	unsigned long endjif;

	/* have to set this, at least for now */
	wc->modtype[card] = MOD_TYPE_QRV;
	if (!(card & 3)) /* if at base of card, reset and write it */
	{
		wctdm_setreg(wc,card,0,0x80); 
		wctdm_setreg(wc,card,0,0x55);
		wctdm_setreg(wc,card,1,0x69);
		wc->qrvhook[card] = wc->qrvhook[card + 1] = 0;
		wc->qrvhook[card + 2] = wc->qrvhook[card + 3] = 0xff;
		wc->debouncetime[card] = wc->debouncetime[card + 1] = QRV_DEBOUNCETIME;
		wc->qrvdebtime[card] = wc->qrvdebtime[card + 1] = 0;
		wc->radmode[card] = wc->radmode[card + 1] = 0;
		wc->txgain[card] = wc->txgain[card + 1] = 3599;
		wc->rxgain[card] = wc->rxgain[card + 1] = 1199;
	} else { /* channel is on same card as base, no need to test */
		if (wc->modtype[card & 0x7c] == MOD_TYPE_QRV) 
		{
			/* only lower 2 are valid */
			if (!(card & 2)) return 0;
		}
		wc->modtype[card] = MOD_TYPE_NONE;
		return 1;
	}
	x = wctdm_getreg(wc,card,0);
	y = wctdm_getreg(wc,card,1);
	/* if not a QRV card, return as such */
	if ((x != 0x55) || (y != 0x69))
	{
		wc->modtype[card] = MOD_TYPE_NONE;
		return 1;
	}
	for(x = 0; x < 0x30; x++)
	{
		if ((x >= 0x1c) && (x <= 0x1e)) wctdm_setreg(wc,card,x,0xff);
		else wctdm_setreg(wc,card,x,0);
	}
	wctdm_setreg(wc,card,0,0x80); 
	endjif = jiffies + (HZ/10);
	while(endjif > jiffies);
	wctdm_setreg(wc,card,0,0x10); 
	wctdm_setreg(wc,card,0,0x10); 
	endjif = jiffies + (HZ/10);
	while(endjif > jiffies);
	/* set up modes */
	wctdm_setreg(wc,card,0,0x1c); 
	/* set up I/O directions */
	wctdm_setreg(wc,card,1,0x33); 
	wctdm_setreg(wc,card,2,0x0f); 
	wctdm_setreg(wc,card,5,0x0f); 
	/* set up I/O to quiescent state */
	wctdm_setreg(wc,card,3,0x11);  /* D0-7 */
	wctdm_setreg(wc,card,4,0xa);  /* D8-11 */
	wctdm_setreg(wc,card,7,0);  /* CS outputs */
	/* set up timeslots */
	wctdm_setreg(wc,card,0x13,card + 0x80);  /* codec 2 tx, ts0 */
	wctdm_setreg(wc,card,0x17,card + 0x80);  /* codec 0 rx, ts0 */
	wctdm_setreg(wc,card,0x14,card + 0x81);  /* codec 1 tx, ts1 */
	wctdm_setreg(wc,card,0x18,card + 0x81);  /* codec 1 rx, ts1 */
	/* set up for max gains */
	wctdm_setreg(wc,card,0x26,0x24); 
	wctdm_setreg(wc,card,0x27,0x24); 
	wctdm_setreg(wc,card,0x0b,0x01);  /* "Transmit" gain codec 0 */
	wctdm_setreg(wc,card,0x0c,0x01);  /* "Transmit" gain codec 1 */
	wctdm_setreg(wc,card,0x0f,0xff);  /* "Receive" gain codec 0 */
	wctdm_setreg(wc,card,0x10,0xff);  /* "Receive" gain codec 1 */
	return 0;
}

static void qrv_dosetup(struct dahdi_chan *chan,struct wctdm *wc)
{
int qrvcard;
unsigned char r;
long l;

	/* actually do something with the values */
	qrvcard = (chan->chanpos - 1) & 0xfc;
	if (debug) printk(KERN_DEBUG "@@@@@ radmodes: %d,%d  rxgains: %d,%d   txgains: %d,%d\n",
	wc->radmode[qrvcard],wc->radmode[qrvcard + 1],
		wc->rxgain[qrvcard],wc->rxgain[qrvcard + 1],
			wc->txgain[qrvcard],wc->txgain[qrvcard + 1]);
	r = 0;
	if (wc->radmode[qrvcard] & RADMODE_DEEMP) r |= 4;		
	if (wc->radmode[qrvcard + 1] & RADMODE_DEEMP) r |= 8;		
	if (wc->rxgain[qrvcard] < 1200) r |= 1;
	if (wc->rxgain[qrvcard + 1] < 1200) r |= 2;
	wctdm_setreg(wc, qrvcard, 7, r);
	if (debug) printk(KERN_DEBUG "@@@@@ setting reg 7 to %02x hex\n",r);
	r = 0;
	if (wc->radmode[qrvcard] & RADMODE_PREEMP) r |= 3;
	else if (wc->txgain[qrvcard] >= 3600) r |= 1;
	else if (wc->txgain[qrvcard] >= 1200) r |= 2;
	if (wc->radmode[qrvcard + 1] & RADMODE_PREEMP) r |= 0xc;
	else if (wc->txgain[qrvcard + 1] >= 3600) r |= 4;
	else if (wc->txgain[qrvcard + 1] >= 1200) r |= 8;
	wctdm_setreg(wc, qrvcard, 4, r);
	if (debug) printk(KERN_DEBUG "@@@@@ setting reg 4 to %02x hex\n",r);
	r = 0;
	if (wc->rxgain[qrvcard] >= 2400) r |= 1; 
	if (wc->rxgain[qrvcard + 1] >= 2400) r |= 2; 
	wctdm_setreg(wc, qrvcard, 0x25, r);
	if (debug) printk(KERN_DEBUG "@@@@@ setting reg 0x25 to %02x hex\n",r);
	r = 0;
	if (wc->txgain[qrvcard] < 2400) r |= 1; else r |= 4;
	if (wc->txgain[qrvcard + 1] < 2400) r |= 8; else r |= 0x20;
	wctdm_setreg(wc, qrvcard, 0x26, r);
	if (debug) printk(KERN_DEBUG "@@@@@ setting reg 0x26 to %02x hex\n",r);
	l = ((long)(wc->rxgain[qrvcard] % 1200) * 10000) / 46875;
	if (l == 0) l = 1;
	if (wc->rxgain[qrvcard] >= 2400) l += 181;
	wctdm_setreg(wc, qrvcard, 0x0b, (unsigned char)l);
	if (debug) printk(KERN_DEBUG "@@@@@ setting reg 0x0b to %02x hex\n",(unsigned char)l);
	l = ((long)(wc->rxgain[qrvcard + 1] % 1200) * 10000) / 46875;
	if (l == 0) l = 1;
	if (wc->rxgain[qrvcard + 1] >= 2400) l += 181;
	wctdm_setreg(wc, qrvcard, 0x0c, (unsigned char)l);
	if (debug) printk(KERN_DEBUG "@@@@@ setting reg 0x0c to %02x hex\n",(unsigned char)l);
	l = ((long)(wc->txgain[qrvcard] % 1200) * 10000) / 46875;
	if (l == 0) l = 1;
	wctdm_setreg(wc, qrvcard, 0x0f, (unsigned char)l);
	if (debug) printk(KERN_DEBUG "@@@@@ setting reg 0x0f to %02x hex\n", (unsigned char)l);
	l = ((long)(wc->txgain[qrvcard + 1] % 1200) * 10000) / 46875;
	if (l == 0) l = 1;
	wctdm_setreg(wc, qrvcard, 0x10,(unsigned char)l);
	if (debug) printk(KERN_DEBUG "@@@@@ setting reg 0x10 to %02x hex\n",(unsigned char)l);
	return;
}

static int wctdm_ioctl(struct dahdi_chan *chan, unsigned int cmd, unsigned long data)
{
	struct wctdm_stats stats;
	struct wctdm_regs regs;
	struct wctdm_regop regop;
	struct wctdm_echo_coefs echoregs;
	struct dahdi_hwgain hwgain;
	struct wctdm *wc = chan->pvt;
	int x;
	union {
		struct dahdi_radio_stat s;
		struct dahdi_radio_param p;
	} stack;
	struct fxs *const fxs = &wc->mods[chan->chanpos - 1].fxs;

	switch (cmd) {
	case DAHDI_ONHOOKTRANSFER:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		fxs->ohttimer = x << 3;

		/* Active mode when idle */
		fxs->idletxhookstate = POLARITY_XOR(chan->chanpos - 1) ?
						SLIC_LF_ACTIVE_REV :
						SLIC_LF_ACTIVE_FWD;

		if (((fxs->lasttxhook & SLIC_LF_SETMASK) == SLIC_LF_ACTIVE_FWD) ||
		    ((fxs->lasttxhook & SLIC_LF_SETMASK) == SLIC_LF_ACTIVE_REV)) {

			set_lasttxhook_interruptible(fxs, POLARITY_XOR(chan->chanpos - 1)
									? SLIC_LF_OHTRAN_REV : SLIC_LF_OHTRAN_FWD ,
									&wc->sethook[chan->chanpos - 1]);
		}
		break;
	case DAHDI_VMWI_CONFIG:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (copy_from_user(&(fxs->vmwisetting),
				   (__user void *)data,
				   sizeof(fxs->vmwisetting)))
			return -EFAULT;
		set_vmwi(wc, chan->chanpos - 1);
		break;
	case DAHDI_VMWI:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		if (0 > x)
			return -EFAULT;
		fxs->vmwi_active_messages = x;
		set_vmwi(wc, chan->chanpos - 1);
		break;
	case WCTDM_GET_STATS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
			stats.tipvolt = wctdm_getreg(wc, chan->chanpos - 1, 80) * -376;
			stats.ringvolt = wctdm_getreg(wc, chan->chanpos - 1, 81) * -376;
			stats.batvolt = wctdm_getreg(wc, chan->chanpos - 1, 82) * -376;
		} else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
			stats.tipvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
			stats.ringvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
			stats.batvolt = (signed char)wctdm_getreg(wc, chan->chanpos - 1, 29) * 1000;
		} else 
			return -EINVAL;
		if (copy_to_user((__user void *) data, &stats, sizeof(stats)))
			return -EFAULT;
		break;
	case WCTDM_GET_REGS:
		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXS) {
			for (x=0;x<NUM_INDIRECT_REGS;x++)
				regs.indirect[x] = wctdm_proslic_getreg_indirect(wc, chan->chanpos -1, x);
			for (x=0;x<NUM_REGS;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		} else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_QRV) {
			memset(&regs, 0, sizeof(regs));
			for (x=0;x<0x32;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		} else {
			memset(&regs, 0, sizeof(regs));
			for (x=0;x<NUM_FXO_REGS;x++)
				regs.direct[x] = wctdm_getreg(wc, chan->chanpos - 1, x);
		}
		if (copy_to_user((__user void *)data, &regs, sizeof(regs)))
			return -EFAULT;
		break;
	case WCTDM_SET_REG:
		if (copy_from_user(&regop, (__user void *) data, sizeof(regop)))
			return -EFAULT;
		if (regop.indirect) {
			if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
				return -EINVAL;
			printk(KERN_INFO "Setting indirect %d to 0x%04x on %d\n", regop.reg, regop.val, chan->chanpos);
			wctdm_proslic_setreg_indirect(wc, chan->chanpos - 1, regop.reg, regop.val);
		} else {
			regop.val &= 0xff;
			if (regop.reg == LINE_STATE) {
				/* Set feedback register to indicate the new state that is being set */
				fxs->lasttxhook = (regop.val & 0x0f) |  SLIC_LF_OPPENDING;
			}
			printk(KERN_INFO "Setting direct %d to %04x on %d\n", regop.reg, regop.val, chan->chanpos);
			wctdm_setreg(wc, chan->chanpos - 1, regop.reg, regop.val);
		}
		break;
	case WCTDM_SET_ECHOTUNE:
		printk(KERN_INFO "-- Setting echo registers: \n");
		if (copy_from_user(&echoregs, (__user void *) data, sizeof(echoregs)))
			return -EFAULT;

		if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
			/* Set the ACIM register */
			wctdm_setreg(wc, chan->chanpos - 1, 30, echoregs.acim);

			/* Set the digital echo canceller registers */
			wctdm_setreg(wc, chan->chanpos - 1, 45, echoregs.coef1);
			wctdm_setreg(wc, chan->chanpos - 1, 46, echoregs.coef2);
			wctdm_setreg(wc, chan->chanpos - 1, 47, echoregs.coef3);
			wctdm_setreg(wc, chan->chanpos - 1, 48, echoregs.coef4);
			wctdm_setreg(wc, chan->chanpos - 1, 49, echoregs.coef5);
			wctdm_setreg(wc, chan->chanpos - 1, 50, echoregs.coef6);
			wctdm_setreg(wc, chan->chanpos - 1, 51, echoregs.coef7);
			wctdm_setreg(wc, chan->chanpos - 1, 52, echoregs.coef8);

			printk(KERN_INFO "-- Set echo registers successfully\n");

			break;
		} else {
			return -EINVAL;

		}
		break;
	case DAHDI_SET_HWGAIN:
		if (copy_from_user(&hwgain, (__user void *) data, sizeof(hwgain)))
			return -EFAULT;

		wctdm_set_hwgain(wc, chan->chanpos-1, hwgain.newgain, hwgain.tx);

		if (debug)
			printk(KERN_DEBUG "Setting hwgain on channel %d to %d for %s direction\n", 
				chan->chanpos-1, hwgain.newgain, hwgain.tx ? "tx" : "rx");
		break;
#ifdef VPM_SUPPORT
	case DAHDI_TONEDETECT:
		/* Hardware DTMF detection is not supported. */
		return -ENOSYS;
#endif
	case DAHDI_SETPOLARITY:
		if (get_user(x, (__user int *) data))
			return -EFAULT;
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_FXS)
			return -EINVAL;
		/* Can't change polarity while ringing or when open */
		if (((fxs->lasttxhook & SLIC_LF_SETMASK) == SLIC_LF_RINGING) ||
		    ((fxs->lasttxhook & SLIC_LF_SETMASK) == SLIC_LF_OPEN))
			return -EINVAL;

		fxs->reversepolarity = (x) ? 1 : 0;

		if (POLARITY_XOR(chan->chanpos -1)) {
			fxs->idletxhookstate |= SLIC_LF_REVMASK;
			x = fxs->lasttxhook;
			x |= SLIC_LF_REVMASK;
			set_lasttxhook_interruptible(fxs, x, &wc->sethook[chan->chanpos - 1]);
		} else {
			fxs->idletxhookstate &= ~SLIC_LF_REVMASK;
			x = fxs->lasttxhook;
			x &= ~SLIC_LF_REVMASK;
			set_lasttxhook_interruptible(fxs, x, &wc->sethook[chan->chanpos - 1]);
		}
		break;
	case DAHDI_RADIO_GETPARAM:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_QRV) 
			return -ENOTTY;
		if (copy_from_user(&stack.p, (__user void *) data, sizeof(stack.p)))
			return -EFAULT;
		stack.p.data = 0; /* start with 0 value in output */
		switch(stack.p.radpar) {
		case DAHDI_RADPAR_INVERTCOR:
			if (wc->radmode[chan->chanpos - 1] & RADMODE_INVERTCOR)
				stack.p.data = 1;
			break;
		case DAHDI_RADPAR_IGNORECOR:
			if (wc->radmode[chan->chanpos - 1] & RADMODE_IGNORECOR)
				stack.p.data = 1;
			break;
		case DAHDI_RADPAR_IGNORECT:
			if (wc->radmode[chan->chanpos - 1] & RADMODE_IGNORECT)
				stack.p.data = 1;
			break;
		case DAHDI_RADPAR_EXTRXTONE:
			stack.p.data = 0;
			if (wc->radmode[chan->chanpos - 1] & RADMODE_EXTTONE)
			{
				stack.p.data = 1;
				if (wc->radmode[chan->chanpos - 1] & RADMODE_EXTINVERT)
				{
					stack.p.data = 2;
				}
			}
			break;
		case DAHDI_RADPAR_DEBOUNCETIME:
			stack.p.data = wc->debouncetime[chan->chanpos - 1];
			break;
		case DAHDI_RADPAR_RXGAIN:
			stack.p.data = wc->rxgain[chan->chanpos - 1] - 1199;
			break;
		case DAHDI_RADPAR_TXGAIN:
			stack.p.data = wc->txgain[chan->chanpos - 1] - 3599;
			break;
		case DAHDI_RADPAR_DEEMP:
			stack.p.data = 0;
			if (wc->radmode[chan->chanpos - 1] & RADMODE_DEEMP)
			{
				stack.p.data = 1;
			}
			break;
		case DAHDI_RADPAR_PREEMP:
			stack.p.data = 0;
			if (wc->radmode[chan->chanpos - 1] & RADMODE_PREEMP)
			{
				stack.p.data = 1;
			}
			break;
		default:
			return -EINVAL;
		}
		if (copy_to_user((__user void *) data, &stack.p, sizeof(stack.p)))
		    return -EFAULT;
		break;
	case DAHDI_RADIO_SETPARAM:
		if (wc->modtype[chan->chanpos - 1] != MOD_TYPE_QRV) 
			return -ENOTTY;
		if (copy_from_user(&stack.p, (__user void *) data, sizeof(stack.p)))
			return -EFAULT;
		switch(stack.p.radpar) {
		case DAHDI_RADPAR_INVERTCOR:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_INVERTCOR;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_INVERTCOR;
			return 0;
		case DAHDI_RADPAR_IGNORECOR:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_IGNORECOR;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_IGNORECOR;
			return 0;
		case DAHDI_RADPAR_IGNORECT:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_IGNORECT;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_IGNORECT;
			return 0;
		case DAHDI_RADPAR_EXTRXTONE:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_EXTTONE;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_EXTTONE;
			if (stack.p.data > 1)
				wc->radmode[chan->chanpos - 1] |= RADMODE_EXTINVERT;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_EXTINVERT;
			return 0;
		case DAHDI_RADPAR_DEBOUNCETIME:
			wc->debouncetime[chan->chanpos - 1] = stack.p.data;
			return 0;
		case DAHDI_RADPAR_RXGAIN:
			/* if out of range */
			if ((stack.p.data <= -1200) || (stack.p.data > 1552))
			{
				return -EINVAL;
			}
			wc->rxgain[chan->chanpos - 1] = stack.p.data + 1199;
			break;
		case DAHDI_RADPAR_TXGAIN:
			/* if out of range */
			if (wc->radmode[chan->chanpos -1] & RADMODE_PREEMP)
			{
				if ((stack.p.data <= -2400) || (stack.p.data > 0))
				{
					return -EINVAL;
				}
			}
			else
			{
				if ((stack.p.data <= -3600) || (stack.p.data > 1200))
				{
					return -EINVAL;
				}
			}
			wc->txgain[chan->chanpos - 1] = stack.p.data + 3599;
			break;
		case DAHDI_RADPAR_DEEMP:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_DEEMP;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_DEEMP;
			wc->rxgain[chan->chanpos - 1] = 1199;
			break;
		case DAHDI_RADPAR_PREEMP:
			if (stack.p.data)
				wc->radmode[chan->chanpos - 1] |= RADMODE_PREEMP;
			else
				wc->radmode[chan->chanpos - 1] &= ~RADMODE_PREEMP;
			wc->txgain[chan->chanpos - 1] = 3599;
			break;
		default:
			return -EINVAL;
		}
		qrv_dosetup(chan,wc);
		return 0;				
	default:
		return -ENOTTY;
	}
	return 0;
}

static int wctdm_open(struct dahdi_chan *chan)
{
	struct wctdm *wc = chan->pvt;
	int channo = chan->chanpos - 1;
	unsigned long flags;

	if (!(wc->cardflag & (1 << (chan->chanpos - 1))))
		return -ENODEV;
	if (wc->dead)
		return -ENODEV;
	wc->usecount++;
	try_module_get(THIS_MODULE);
	
	/* Reset the mwi indicators */
	spin_lock_irqsave(&wc->reglock, flags);
	wc->mods[channo].fxo.neonmwi_debounce = 0;
	wc->mods[channo].fxo.neonmwi_offcounter = 0;
	wc->mods[channo].fxo.neonmwi_state = 0;
	spin_unlock_irqrestore(&wc->reglock, flags);

	return 0;
}

static int wctdm_watchdog(struct dahdi_span *span, int event)
{
	printk(KERN_INFO "TDM: Called watchdog\n");
	return 0;
}

static int wctdm_close(struct dahdi_chan *chan)
{
	struct wctdm *wc = chan->pvt;
	int x;
	signed char reg;
	wc->usecount--;
	module_put(THIS_MODULE);
	for (x=0;x<wc->cards;x++) {
		if (wc->modtype[x] == MOD_TYPE_FXS) {
			wc->mods[x].fxs.idletxhookstate =
				POLARITY_XOR(x) ? SLIC_LF_ACTIVE_REV :
						  SLIC_LF_ACTIVE_FWD;
		}
		if (wc->modtype[x] == MOD_TYPE_QRV)
		{
			int qrvcard = x & 0xfc;

			wc->qrvhook[x] = 0;
			wc->qrvhook[x + 2] = 0xff;
			wc->debouncetime[x] = QRV_DEBOUNCETIME;
			wc->qrvdebtime[x] = 0;
			wc->radmode[x] = 0;
			wc->txgain[x] = 3599;
			wc->rxgain[x] = 1199;
			reg = 0;
			if (!wc->qrvhook[qrvcard]) reg |= 1;
			if (!wc->qrvhook[qrvcard + 1]) reg |= 0x10;
			wc->sethook[qrvcard] = CMD_WR(3, reg);
			qrv_dosetup(chan,wc);
		}
	}
	/* If we're dead, release us now */
	if (!wc->usecount && wc->dead) 
		wctdm_release(wc);
	return 0;
}

static int wctdm_hooksig(struct dahdi_chan *chan, enum dahdi_txsig txsig)
{
	struct wctdm *wc = chan->pvt;

	int reg=0,qrvcard;
	if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_QRV) {
		qrvcard = (chan->chanpos - 1) & 0xfc;
		switch(txsig) {
		case DAHDI_TXSIG_START:
		case DAHDI_TXSIG_OFFHOOK:
			wc->qrvhook[chan->chanpos - 1] = 1;
			break;
		case DAHDI_TXSIG_ONHOOK:
			wc->qrvhook[chan->chanpos - 1] = 0;
			break;
		default:
			printk(KERN_NOTICE "wctdm24xxp: Can't set tx state to %d\n", txsig);
		}
		reg = 0;
		if (!wc->qrvhook[qrvcard]) reg |= 1;
		if (!wc->qrvhook[qrvcard + 1]) reg |= 0x10;
		wc->sethook[qrvcard] = CMD_WR(3, reg);
		/* wctdm_setreg(wc, qrvcard, 3, reg); */
	} else if (wc->modtype[chan->chanpos - 1] == MOD_TYPE_FXO) {
		switch(txsig) {
		case DAHDI_TXSIG_START:
		case DAHDI_TXSIG_OFFHOOK:
			wc->mods[chan->chanpos - 1].fxo.offhook = 1;
			wc->sethook[chan->chanpos - 1] = CMD_WR(5, 0x9);
			/* wctdm_setreg(wc, chan->chanpos - 1, 5, 0x9); */
			break;
		case DAHDI_TXSIG_ONHOOK:
			wc->mods[chan->chanpos - 1].fxo.offhook = 0;
			wc->sethook[chan->chanpos - 1] = CMD_WR(5, 0x8);
			/* wctdm_setreg(wc, chan->chanpos - 1, 5, 0x8); */
			break;
		default:
			printk(KERN_NOTICE "wctdm24xxp: Can't set tx state to %d\n", txsig);
		}
	} else {  /* Else this is an fxs port */
		unsigned long flags;
		struct fxs *const fxs = &wc->mods[chan->chanpos - 1].fxs;
		spin_lock_irqsave(&fxs->lasttxhooklock, flags);
		switch(txsig) {
		case DAHDI_TXSIG_ONHOOK:
			switch(chan->sig) {
			case DAHDI_SIG_EM:
			case DAHDI_SIG_FXOKS:
			case DAHDI_SIG_FXOLS:
				fxs->lasttxhook = SLIC_LF_OPPENDING |
					fxs->idletxhookstate;
				break;
			case DAHDI_SIG_FXOGS:
				if (POLARITY_XOR(chan->chanpos -1)) {
					fxs->lasttxhook = SLIC_LF_OPPENDING |
						SLIC_LF_RING_OPEN;
				} else {
					fxs->lasttxhook = SLIC_LF_OPPENDING |
						SLIC_LF_TIP_OPEN;
				}
				break;
			}
			break;
		case DAHDI_TXSIG_OFFHOOK:
			switch(chan->sig) {
			case DAHDI_SIG_EM:
				if (POLARITY_XOR(chan->chanpos -1)) {
					fxs->lasttxhook = SLIC_LF_OPPENDING |
						SLIC_LF_ACTIVE_FWD;
				} else {
					fxs->lasttxhook = SLIC_LF_OPPENDING |
						SLIC_LF_ACTIVE_REV;
				}
				break;
			default:
				fxs->lasttxhook = SLIC_LF_OPPENDING |
					fxs->idletxhookstate;
				break;
			}
			break;
		case DAHDI_TXSIG_START:
			fxs->lasttxhook = SLIC_LF_OPPENDING | SLIC_LF_RINGING;
			break;
		case DAHDI_TXSIG_KEWL:
			fxs->lasttxhook = SLIC_LF_OPPENDING | SLIC_LF_OPEN;
			break;
		default:
			printk(KERN_NOTICE "wctdm24xxp: Can't set tx state to %d\n", txsig);
		}
		wc->sethook[chan->chanpos - 1] = CMD_WR(LINE_STATE, fxs->lasttxhook);
		spin_unlock_irqrestore(&fxs->lasttxhooklock, flags);
		if (debug & DEBUG_CARD)
			printk(KERN_DEBUG "Setting FXS hook state to %d (%02x)\n", txsig, reg);
	}
	return 0;
}

static void wctdm_dacs_connect(struct wctdm *wc, int srccard, int dstcard)
{

	if (wc->dacssrc[dstcard] > - 1) {
		printk(KERN_NOTICE "wctdm_dacs_connect: Can't have double sourcing yet!\n");
		return;
	}
	if (!((wc->modtype[srccard] == MOD_TYPE_FXS)||(wc->modtype[srccard] == MOD_TYPE_FXO))){
		printk(KERN_NOTICE "wctdm_dacs_connect: Unsupported modtype for card %d\n", srccard);
		return;
	}
	if (!((wc->modtype[dstcard] == MOD_TYPE_FXS)||(wc->modtype[dstcard] == MOD_TYPE_FXO))){
		printk(KERN_NOTICE "wctdm_dacs_connect: Unsupported modtype for card %d\n", dstcard);
		return;
	}
	if (debug)
		printk(KERN_DEBUG "connect %d => %d\n", srccard, dstcard);
	wc->dacssrc[dstcard] = srccard;

	/* make srccard transmit to srccard+24 on the TDM bus */
	if (wc->modtype[srccard] == MOD_TYPE_FXS) {
		/* proslic */
		wctdm_setreg(wc, srccard, PCM_XMIT_START_COUNT_LSB, ((srccard+24) * 8) & 0xff); 
		wctdm_setreg(wc, srccard, PCM_XMIT_START_COUNT_MSB, ((srccard+24) * 8) >> 8);
	} else if(wc->modtype[srccard] == MOD_TYPE_FXO) { 
		/* daa */
		wctdm_setreg(wc, srccard, 34, ((srccard+24) * 8) & 0xff); /* TX */
		wctdm_setreg(wc, srccard, 35, ((srccard+24) * 8) >> 8);   /* TX */
	}

	/* have dstcard receive from srccard+24 on the TDM bus */
	if (wc->modtype[dstcard] == MOD_TYPE_FXS) {
		/* proslic */
    	wctdm_setreg(wc, dstcard, PCM_RCV_START_COUNT_LSB,  ((srccard+24) * 8) & 0xff);
		wctdm_setreg(wc, dstcard, PCM_RCV_START_COUNT_MSB,  ((srccard+24) * 8) >> 8);
	} else if(wc->modtype[dstcard] == MOD_TYPE_FXO) {
		/* daa */
		wctdm_setreg(wc, dstcard, 36, ((srccard+24) * 8) & 0xff); /* RX */
		wctdm_setreg(wc, dstcard, 37, ((srccard+24) * 8) >> 8);   /* RX */
	}

}

static void wctdm_dacs_disconnect(struct wctdm *wc, int card)
{
	if (wc->dacssrc[card] > -1) {
		if (debug)
			printk(KERN_DEBUG "wctdm_dacs_disconnect: restoring TX for %d and RX for %d\n",wc->dacssrc[card], card);

		/* restore TX (source card) */
		if(wc->modtype[wc->dacssrc[card]] == MOD_TYPE_FXS){
			wctdm_setreg(wc, wc->dacssrc[card], PCM_XMIT_START_COUNT_LSB, (wc->dacssrc[card] * 8) & 0xff);
			wctdm_setreg(wc, wc->dacssrc[card], PCM_XMIT_START_COUNT_MSB, (wc->dacssrc[card] * 8) >> 8);
		} else if(wc->modtype[wc->dacssrc[card]] == MOD_TYPE_FXO){
			wctdm_setreg(wc, card, 34, (card * 8) & 0xff);
			wctdm_setreg(wc, card, 35, (card * 8) >> 8);
		} else {
			printk(KERN_WARNING "WARNING: wctdm_dacs_disconnect() called on unsupported modtype\n");
		}

		/* restore RX (this card) */
		if(wc->modtype[card] == MOD_TYPE_FXS){
	   		wctdm_setreg(wc, card, PCM_RCV_START_COUNT_LSB, (card * 8) & 0xff);
	    	wctdm_setreg(wc, card, PCM_RCV_START_COUNT_MSB, (card * 8) >> 8);
		} else if(wc->modtype[card] == MOD_TYPE_FXO){
			wctdm_setreg(wc, card, 36, (card * 8) & 0xff);
			wctdm_setreg(wc, card, 37, (card * 8) >> 8);
		} else {
			printk(KERN_WARNING "WARNING: wctdm_dacs_disconnect() called on unsupported modtype\n");
		}

		wc->dacssrc[card] = -1;
	}
}

static int wctdm_dacs(struct dahdi_chan *dst, struct dahdi_chan *src)
{
	struct wctdm *wc;

	if(!nativebridge)
		return 0; /* should this return -1 since unsuccessful? */

	wc = dst->pvt;

	if(src) {
		wctdm_dacs_connect(wc, src->chanpos - 1, dst->chanpos - 1);
		if (debug)
			printk(KERN_DEBUG "dacs connecct: %d -> %d!\n\n", src->chanpos, dst->chanpos);
	} else {
		wctdm_dacs_disconnect(wc, dst->chanpos - 1);
		if (debug)
			printk(KERN_DEBUG "dacs disconnect: %d!\n", dst->chanpos);
	}
	return 0;
}

static int wctdm_initialize(struct wctdm *wc)
{
	int x;
	struct pci_dev *pdev = voicebus_get_pci_dev(wc->vb);

	/* DAHDI stuff */
	sprintf(wc->span.name, "WCTDM/%d", wc->pos);
	snprintf(wc->span.desc, sizeof(wc->span.desc) - 1,
		 "%s Board %d", wc->desc->name, wc->pos + 1);
	snprintf(wc->span.location, sizeof(wc->span.location) - 1,
		 "PCI%s Bus %02d Slot %02d", (wc->flags[0] & FLAG_EXPRESS) ? " Express" : "",
		 pdev->bus->number, PCI_SLOT(pdev->devfn) + 1);
	wc->span.manufacturer = "Digium";
	strncpy(wc->span.devicetype, wc->desc->name,
		sizeof(wc->span.devicetype) - 1);
	if (alawoverride) {
		printk(KERN_INFO "ALAW override parameter detected.  Device will be operating in ALAW\n");
		wc->span.deflaw = DAHDI_LAW_ALAW;
	} else {
		wc->span.deflaw = DAHDI_LAW_MULAW;
	}
	for (x=0;x<wc->cards;x++) {
		sprintf(wc->chans[x]->name, "WCTDM/%d/%d", wc->pos, x);
		wc->chans[x]->sigcap = DAHDI_SIG_FXOKS | DAHDI_SIG_FXOLS | DAHDI_SIG_FXOGS | DAHDI_SIG_SF | DAHDI_SIG_EM | DAHDI_SIG_CLEAR;
		wc->chans[x]->sigcap |= DAHDI_SIG_FXSKS | DAHDI_SIG_FXSLS | DAHDI_SIG_SF | DAHDI_SIG_CLEAR;
		wc->chans[x]->chanpos = x+1;
		wc->chans[x]->pvt = wc;
	}
	wc->span.chans = wc->chans;
	wc->span.channels = wc->desc->ports;
	wc->span.irq = pdev->irq;
	wc->span.hooksig = wctdm_hooksig;
	wc->span.open = wctdm_open;
	wc->span.close = wctdm_close;
	wc->span.flags = DAHDI_FLAG_RBS;
	wc->span.ioctl = wctdm_ioctl;
	wc->span.watchdog = wctdm_watchdog;
	wc->span.dacs= wctdm_dacs;
#ifdef VPM_SUPPORT
	if (vpmsupport)
		wc->span.echocan_create = echocan_create;
#endif	
	init_waitqueue_head(&wc->span.maintq);

	wc->span.pvt = wc;
	return 0;
}

static void wctdm_post_initialize(struct wctdm *wc)
{
	int x;

	/* Finalize signalling  */
	for (x = 0; x <wc->cards; x++) {
		if (wc->cardflag & (1 << x)) {
			if (wc->modtype[x] == MOD_TYPE_FXO)
				wc->chans[x]->sigcap = DAHDI_SIG_FXSKS | DAHDI_SIG_FXSLS | DAHDI_SIG_SF | DAHDI_SIG_CLEAR;
			else if (wc->modtype[x] == MOD_TYPE_FXS)
				wc->chans[x]->sigcap = DAHDI_SIG_FXOKS | DAHDI_SIG_FXOLS | DAHDI_SIG_FXOGS | DAHDI_SIG_SF | DAHDI_SIG_EM | DAHDI_SIG_CLEAR;
			else if (wc->modtype[x] == MOD_TYPE_QRV)
				wc->chans[x]->sigcap = DAHDI_SIG_SF | DAHDI_SIG_EM | DAHDI_SIG_CLEAR;
		} else if (!(wc->chans[x]->sigcap & DAHDI_SIG_BROKEN)) {
			wc->chans[x]->sigcap = 0;
		}
	}

	if (wc->vpm100) {
		strncat(wc->span.devicetype, " (VPM100M)", sizeof(wc->span.devicetype) - 1);
	} else if (wc->vpmadt032) {
		strncat(wc->span.devicetype, " (VPMADT032)", sizeof(wc->span.devicetype) - 1);
	}
}

static int wctdm_vpm_init(struct wctdm *wc)
{
	unsigned char reg;
	unsigned int mask;
	unsigned int ver;
	unsigned char vpmver=0;
	unsigned int i, x, y;

	for (x=0;x<NUM_EC;x++) {
		ver = wctdm_vpm_in(wc, x, 0x1a0); /* revision */
		if (debug & DEBUG_ECHOCAN)
			printk(KERN_DEBUG "VPM100: Chip %d: ver %02x\n", x, ver);
		if (ver != 0x33) {
			printk(KERN_DEBUG "VPM100: %s\n", x ? "Inoperable" : "Not Present");
			wc->vpm100 = 0;
			return -ENODEV;
		}	

		if (!x) {
			vpmver = wctdm_vpm_in(wc, x, 0x1a6) & 0xf;
			printk(KERN_INFO "VPM Revision: %02x\n", vpmver);
		}


		/* Setup GPIO's */
		for (y=0;y<4;y++) {
			wctdm_vpm_out(wc, x, 0x1a8 + y, 0x00); /* GPIO out */
			if (y == 3)
				wctdm_vpm_out(wc, x, 0x1ac + y, 0x00); /* GPIO dir */
			else
				wctdm_vpm_out(wc, x, 0x1ac + y, 0xff); /* GPIO dir */
			wctdm_vpm_out(wc, x, 0x1b0 + y, 0x00); /* GPIO sel */
		}

		/* Setup TDM path - sets fsync and tdm_clk as inputs */
		reg = wctdm_vpm_in(wc, x, 0x1a3); /* misc_con */
		wctdm_vpm_out(wc, x, 0x1a3, reg & ~2);

		/* Setup Echo length (256 taps) */
		wctdm_vpm_out(wc, x, 0x022, 0);

		/* Setup timeslots */
		if (vpmver == 0x01) {
			wctdm_vpm_out(wc, x, 0x02f, 0x00); 
			wctdm_vpm_out(wc, x, 0x023, 0xff);
			mask = 0x11111111 << x;
		} else {
			wctdm_vpm_out(wc, x, 0x02f, 0x20  | (x << 3)); 
			wctdm_vpm_out(wc, x, 0x023, 0x3f);
			mask = 0x0000003f;
		}

		/* Setup the tdm channel masks for all chips*/
		for (i = 0; i < 4; i++)
			wctdm_vpm_out(wc, x, 0x33 - i, (mask >> (i << 3)) & 0xff);

		/* Setup convergence rate */
		reg = wctdm_vpm_in(wc,x,0x20);
		reg &= 0xE0;
		if (alawoverride) {
			if (!x)
				printk(KERN_INFO "VPM: A-law mode\n");
			reg |= 0x01;
		} else {
			if (!x)
				printk(KERN_INFO "VPM: U-law mode\n");
			reg &= ~0x01;
		}
		wctdm_vpm_out(wc,x,0x20,(reg | 0x20));

		/* Initialize echo cans */
		for (i = 0 ; i < MAX_TDM_CHAN; i++) {
			if (mask & (0x00000001 << i))
				wctdm_vpm_out(wc,x,i,0x00);
		}

		for (i=0;i<30;i++) 
			schluffen(&wc->regq);

		/* Put in bypass mode */
		for (i = 0 ; i < MAX_TDM_CHAN ; i++) {
			if (mask & (0x00000001 << i)) {
				wctdm_vpm_out(wc,x,i,0x01);
			}
		}

		/* Enable bypass */
		for (i = 0 ; i < MAX_TDM_CHAN ; i++) {
			if (mask & (0x00000001 << i))
				wctdm_vpm_out(wc,x,0x78 + i,0x01);
		}
      
		/* Enable DTMF detectors (always DTMF detect all spans) */
		for (i = 0; i < 6; i++) {
			if (vpmver == 0x01) 
				wctdm_vpm_out(wc, x, 0x98 + i, 0x40 | (i << 2) | x);
			else
				wctdm_vpm_out(wc, x, 0x98 + i, 0x40 | i);
		}

		for (i = 0xB8; i < 0xC0; i++)
			wctdm_vpm_out(wc, x, i, 0xFF);
		for (i = 0xC0; i < 0xC4; i++)
			wctdm_vpm_out(wc, x, i, 0xff);

	} 
	
	/* TODO: What do the different values for vpm100 mean? */
	if (vpmver == 0x01) {
		wc->vpm100 = 2;
	} else {
		wc->vpm100 = 1;
	}

	printk(KERN_INFO "Enabling VPM100 gain adjustments on any FXO ports found\n");
	for (i = 0; i < wc->desc->ports; i++) {
		if (wc->modtype[i] == MOD_TYPE_FXO) {
			/* Apply negative Tx gain of 4.5db to DAA */
			wctdm_setreg(wc, i, 38, 0x14);	/* 4db */
			wctdm_setreg(wc, i, 40, 0x15);	/* 0.5db */

			/* Apply negative Rx gain of 4.5db to DAA */
			wctdm_setreg(wc, i, 39, 0x14);	/* 4db */
			wctdm_setreg(wc, i, 41, 0x15);	/* 0.5db */
		}
	}

	return 0;
}

static void get_default_portconfig(GpakPortConfig_t *portconfig)
{
	memset(portconfig, 0, sizeof(GpakPortConfig_t));

	/* First Serial Port config */
	portconfig->SlotsSelect1 = SlotCfgNone;
	portconfig->FirstBlockNum1 = 0;
	portconfig->FirstSlotMask1 = 0x0000;
	portconfig->SecBlockNum1 = 1;
	portconfig->SecSlotMask1 = 0x0000;
	portconfig->SerialWordSize1 = SerWordSize8;
	portconfig->CompandingMode1 = cmpNone;
	portconfig->TxFrameSyncPolarity1 = FrameSyncActHigh;
	portconfig->RxFrameSyncPolarity1 = FrameSyncActHigh;
	portconfig->TxClockPolarity1 = SerClockActHigh;
	portconfig->RxClockPolarity1 = SerClockActHigh;
	portconfig->TxDataDelay1 = DataDelay0;
	portconfig->RxDataDelay1 = DataDelay0;
	portconfig->DxDelay1 = Disabled;
	portconfig->ThirdSlotMask1 = 0x0000;
	portconfig->FouthSlotMask1 = 0x0000;
	portconfig->FifthSlotMask1 = 0x0000;
	portconfig->SixthSlotMask1 = 0x0000;
	portconfig->SevenSlotMask1 = 0x0000;
	portconfig->EightSlotMask1 = 0x0000;

	/* Second Serial Port config */
	portconfig->SlotsSelect2 = SlotCfg2Groups;
	portconfig->FirstBlockNum2 = 0;
	portconfig->FirstSlotMask2 = 0xffff;
	portconfig->SecBlockNum2 = 1;
	portconfig->SecSlotMask2 = 0xffff;
	portconfig->SerialWordSize2 = SerWordSize8;
	portconfig->CompandingMode2 = cmpNone;
	portconfig->TxFrameSyncPolarity2 = FrameSyncActHigh;
	portconfig->RxFrameSyncPolarity2 = FrameSyncActHigh;
	portconfig->TxClockPolarity2 = SerClockActHigh;
	portconfig->RxClockPolarity2 = SerClockActLow;
	portconfig->TxDataDelay2 = DataDelay0;
	portconfig->RxDataDelay2 = DataDelay0;
	portconfig->DxDelay2 = Disabled;
	portconfig->ThirdSlotMask2 = 0x0000;
	portconfig->FouthSlotMask2 = 0x0000;
	portconfig->FifthSlotMask2 = 0x0000;
	portconfig->SixthSlotMask2 = 0x0000;
	portconfig->SevenSlotMask2 = 0x0000;
	portconfig->EightSlotMask2 = 0x0000;

	/* Third Serial Port Config */
	portconfig->SlotsSelect3 = SlotCfg2Groups;
	portconfig->FirstBlockNum3 = 0;
	portconfig->FirstSlotMask3 = 0xffff;
	portconfig->SecBlockNum3 = 1;
	portconfig->SecSlotMask3 = 0xffff;
	portconfig->SerialWordSize3 = SerWordSize8;
	portconfig->CompandingMode3 = cmpNone;
	portconfig->TxFrameSyncPolarity3 = FrameSyncActHigh;
	portconfig->RxFrameSyncPolarity3 = FrameSyncActHigh;
	portconfig->TxClockPolarity3 = SerClockActHigh;
	portconfig->RxClockPolarity3 = SerClockActLow;
	portconfig->TxDataDelay3 = DataDelay0;
	portconfig->RxDataDelay3 = DataDelay0;
	portconfig->DxDelay3 = Disabled;
	portconfig->ThirdSlotMask3 = 0x0000;
	portconfig->FouthSlotMask3 = 0x0000;
	portconfig->FifthSlotMask3 = 0x0000;
	portconfig->SixthSlotMask3 = 0x0000;
	portconfig->SevenSlotMask3 = 0x0000;
	portconfig->EightSlotMask3 = 0x0000;
}

static int wctdm_locate_modules(struct wctdm *wc)
{
	int x;
	unsigned long flags;
	wc->ctlreg = 0x00;
	
	/* Make sure all units go into daisy chain mode */
	spin_lock_irqsave(&wc->reglock, flags);
	wc->span.irqmisses = 0;
	for (x=0;x<wc->cards;x++) 
		wc->modtype[x] = MOD_TYPE_FXSINIT;
	wc->vpm100 = -1;
	for (x = wc->cards; x < wc->cards+NUM_EC; x++)
		wc->modtype[x] = MOD_TYPE_VPM;
	spin_unlock_irqrestore(&wc->reglock, flags);
	/* Wait just a bit */
	for (x=0;x<10;x++) 
		schluffen(&wc->regq);
	spin_lock_irqsave(&wc->reglock, flags);
	for (x=0;x<wc->cards;x++) 
		wc->modtype[x] = MOD_TYPE_FXS;
	spin_unlock_irqrestore(&wc->reglock, flags);

#if 0
	/* XXX */
	cmddesc = 0;
#endif	
	/* Now that all the cards have been reset, we can stop checking them all if there aren't as many */
	spin_lock_irqsave(&wc->reglock, flags);
	wc->cards = wc->desc->ports;
	spin_unlock_irqrestore(&wc->reglock, flags);

	/* Reset modules */
	for (x=0;x<wc->cards;x++) {
		int sane=0,ret=0,readi=0;
retry:
		/* Init with Auto Calibration */
		if (!(ret = wctdm_init_proslic(wc, x, 0, 0, sane))) {
			wc->cardflag |= (1 << x);
			if (debug & DEBUG_CARD) {
				readi = wctdm_getreg(wc,x,LOOP_I_LIMIT);
				printk(KERN_DEBUG "Proslic module %d loop current is %dmA\n",x,
					((readi*3)+20));
			}
			printk(KERN_INFO "Port %d: Installed -- AUTO FXS/DPO\n", x + 1);
		} else {
			if(ret!=-2) {
				sane=1;
				/* Init with Manual Calibration */
				if (!wctdm_init_proslic(wc, x, 0, 1, sane)) {
					wc->cardflag |= (1 << x);
                                if (debug & DEBUG_CARD) {
                                        readi = wctdm_getreg(wc,x,LOOP_I_LIMIT);
                                        printk(KERN_DEBUG "Proslic module %d loop current is %dmA\n",x,
                                 	       ((readi*3)+20));
                                }
					printk(KERN_INFO "Port %d: Installed -- MANUAL FXS\n",x + 1);
				} else {
					printk(KERN_NOTICE "Port %d: FAILED FXS (%s)\n", x + 1, fxshonormode ? fxo_modes[_opermode].name : "FCC");
					wc->chans[x]->sigcap = DAHDI_SIG_BROKEN | __DAHDI_SIG_FXO;
				} 
			} else if (!(ret = wctdm_init_voicedaa(wc, x, 0, 0, sane))) {
				wc->cardflag |= (1 << x);
				printk(KERN_INFO "Port %d: Installed -- AUTO FXO (%s mode)\n",x + 1, fxo_modes[_opermode].name);
 			} else if (!wctdm_init_qrvdri(wc,x)) {
 				wc->cardflag |= 1 << x;
 				printk(KERN_INFO "Port %d: Installed -- QRV DRI card\n",x + 1);
			} else {
				if ((wc->desc->ports != 24) &&
				    ((x & 0x3) == 1) && !wc->altcs[x]) {
					spin_lock_irqsave(&wc->reglock, flags);
					wc->altcs[x] = 2;
					if (wc->desc->ports == 4) {
						wc->altcs[x+1] = 3;
						wc->altcs[x+2] = 3;
					}
 					wc->modtype[x] = MOD_TYPE_FXSINIT;
 					spin_unlock_irqrestore(&wc->reglock, flags);
				
 					schluffen(&wc->regq);
 					schluffen(&wc->regq);
 					spin_lock_irqsave(&wc->reglock, flags);
 					wc->modtype[x] = MOD_TYPE_FXS;
 					spin_unlock_irqrestore(&wc->reglock, flags);
 					if (debug & DEBUG_CARD)
 						printk(KERN_DEBUG "Trying port %d with alternate chip select\n", x + 1);
 					goto retry;
				} else {
 					printk(KERN_NOTICE "Port %d: Not installed\n", x + 1);
 					wc->modtype[x] = MOD_TYPE_NONE;
 					wc->cardflag |= (1 << x);
 				}
			}
		}
	}

	if (!vpmsupport) {
		printk(KERN_NOTICE "VPM: Support Disabled\n");
	} else if (!wctdm_vpm_init(wc)) {
		printk(KERN_INFO "VPM: Present and operational (Rev %c)\n", 'A' + wc->vpm100 - 1);
		wc->ctlreg |= 0x10;
	} else {
		int res;
		struct vpmadt032_options options;
		GpakPortConfig_t portconfig;
		
		spin_lock_irqsave(&wc->reglock, flags);
		for (x = NUM_CARDS; x < NUM_CARDS + NUM_EC; x++)
			wc->modtype[x] = MOD_TYPE_NONE;
		spin_unlock_irqrestore(&wc->reglock, flags);

		options.debug = debug;
		options.vpmnlptype = vpmnlptype;
		options.vpmnlpthresh = vpmnlpthresh;
		options.vpmnlpmaxsupp = vpmnlpmaxsupp;

		wc->vpmadt032 = vpmadt032_alloc(&options, wc->board_name);
		if (!wc->vpmadt032)
			return -ENOMEM;

		wc->vpmadt032->setchanconfig_from_state = setchanconfig_from_state;
		wc->vpmadt032->options.channels = wc->span.channels;
		get_default_portconfig(&portconfig);
		res = vpmadt032_init(wc->vpmadt032, wc->vb);
		if (res) {
			vpmadt032_free(wc->vpmadt032);
			wc->vpmadt032 = NULL;
			return res;
		} 

		/* Now we need to configure the VPMADT032 module for this
		 * particular board. */
		res = config_vpmadt032(wc->vpmadt032, wc);
		if (res) {
			vpmadt032_free(wc->vpmadt032);
			wc->vpmadt032 = NULL;
			return res;
		}

		printk(KERN_INFO "VPMADT032: Present and operational (Firmware version %x)\n", wc->vpmadt032->version);
		/* TODO what is 0x10 in this context? */
		wc->ctlreg |= 0x10;
	}

	return 0;
}

static struct pci_driver wctdm_driver;

static void free_wc(struct wctdm *wc)
{
	unsigned int x;

	for (x = 0; x < ARRAY_SIZE(wc->chans); x++) {
		if (wc->chans[x]) {
			kfree(wc->chans[x]);
		}
		if (wc->ec[x])
			kfree(wc->ec[x]);
	}
	kfree(wc);
}

static int __devinit wctdm_init_one(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct wctdm *wc;
	int i;
	int y;
	int ret;
	
	neonmwi_offlimit_cycles = neonmwi_offlimit /MS_PER_HOOKCHECK;

	if (!(wc = kmalloc(sizeof(*wc), GFP_KERNEL))) {
		return -ENOMEM;
	}

	memset(wc, 0, sizeof(*wc));
	wc->desc = (struct wctdm_desc *)ent->driver_data;
	spin_lock(&ifacelock);	
	/* \todo this is a candidate for removal... */
	for (i = 0; i < WC_MAX_IFACES; ++i) {
		if (!ifaces[i]) {
			ifaces[i] = wc;
			break;
		}
	}
	spin_unlock(&ifacelock);

	snprintf(wc->board_name, sizeof(wc->board_name)-1, "%s%d",
		 wctdm_driver.name, i);
	ret = voicebus_init(pdev, SFRAME_SIZE, wc->board_name,
		handle_receive, handle_transmit, wc, debug, &wc->vb);
	if (ret) {
		kfree(wc);
		return ret;
	}
	BUG_ON(!wc->vb);

	if (VOICEBUS_DEFAULT_LATENCY != latency) {
		voicebus_set_minlatency(wc->vb, latency);
	}

	spin_lock_init(&wc->reglock);
	wc->curcard = -1;
	wc->cards = NUM_CARDS;
	wc->pos = i;
	wc->txident = 1;
	for (y=0;y<NUM_CARDS;y++) {
		wc->flags[y] = wc->desc->flags;
		wc->dacssrc[y] = -1;
	}

	init_waitqueue_head(&wc->regq);

	for (i = 0; i < wc->cards; i++) {
		if (!(wc->chans[i] = kmalloc(sizeof(*wc->chans[i]), GFP_KERNEL))) {
			free_wc(wc);
			return -ENOMEM;
		}
		memset(wc->chans[i], 0, sizeof(*wc->chans[i]));
		if (!(wc->ec[i] = kmalloc(sizeof(*wc->ec[i]), GFP_KERNEL))) {
			free_wc(wc);
			return -ENOMEM;
		}
		memset(wc->ec[i], 0, sizeof(*wc->ec[i]));
	}


	if (wctdm_initialize(wc)) {
		voicebus_release(wc->vb);
		wc->vb = NULL;
		kfree(wc);
		return -EIO;
	}

	voicebus_lock_latency(wc->vb);

	if (voicebus_start(wc->vb)) {
		BUG_ON(1);
	}
	
	/* Now track down what modules are installed */
	wctdm_locate_modules(wc);
	
	/* Final initialization */
	wctdm_post_initialize(wc);
	
	/* We should be ready for DAHDI to come in now. */
	if (dahdi_register(&wc->span, 0)) {
		printk(KERN_NOTICE "Unable to register span with DAHDI\n");
		return -1;
	}

	wc->initialized = 1;

	printk(KERN_INFO "Found a Wildcard TDM: %s (%d modules)\n",
	       wc->desc->name, wc->desc->ports);
	
	voicebus_unlock_latency(wc->vb);
	return 0;
}

static void wctdm_release(struct wctdm *wc)
{
	int i;

	if (wc->initialized) {
		dahdi_unregister(&wc->span);
	}

	voicebus_release(wc->vb);
	wc->vb = NULL;

	spin_lock(&ifacelock);
	for (i = 0; i < WC_MAX_IFACES; i++)
		if (ifaces[i] == wc)
			break;
	ifaces[i] = NULL;
	spin_unlock(&ifacelock);
	
	free_wc(wc);
}

static void __devexit wctdm_remove_one(struct pci_dev *pdev)
{
	struct wctdm *wc = voicebus_pci_dev_to_context(pdev);
	struct vpmadt032 *vpm = wc->vpmadt032;

	if (wc) {
		if (vpm) {
			clear_bit(VPM150M_DTMFDETECT, &vpm->control);
			clear_bit(VPM150M_ACTIVE, &vpm->control);
			flush_scheduled_work();
		}

		voicebus_stop(wc->vb);

		if (vpm) {
			vpmadt032_free(wc->vpmadt032);
			wc->vpmadt032 = NULL;
		}

		/* Release span, possibly delayed */
		if (!wc->usecount) {
			wctdm_release(wc);
			printk(KERN_INFO "Freed a Wildcard\n");
		}
		else {
			wc->dead = 1;
		}
	}
}

static struct pci_device_id wctdm_pci_tbl[] = {
	{ 0xd161, 0x2400, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctdm2400 },
	{ 0xd161, 0x0800, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctdm800 },
	{ 0xd161, 0x8002, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wcaex800 },
	{ 0xd161, 0x8003, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wcaex2400 },
	{ 0xd161, 0x8005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wctdm410 },
	{ 0xd161, 0x8006, PCI_ANY_ID, PCI_ANY_ID, 0, 0, (unsigned long) &wcaex410 },
	{ 0 }
};

MODULE_DEVICE_TABLE(pci, wctdm_pci_tbl);

static struct pci_driver wctdm_driver = {
	.name = "wctdm24xxp",
	.probe = wctdm_init_one,
	.remove = __devexit_p(wctdm_remove_one),
	.id_table = wctdm_pci_tbl,
};

static int __init wctdm_init(void)
{
	int res;
	int x;

	for (x = 0; x < ARRAY_SIZE(fxo_modes); x++) {
		if (!strcmp(fxo_modes[x].name, opermode))
			break;
	}
	if (x < ARRAY_SIZE(fxo_modes)) {
		_opermode = x;
	} else {
		printk(KERN_NOTICE "Invalid/unknown operating mode '%s' specified.  Please choose one of:\n", opermode);
		for (x = 0; x < ARRAY_SIZE(fxo_modes); x++)
			printk(KERN_NOTICE "  %s\n", fxo_modes[x].name);
		printk(KERN_NOTICE "Note this option is CASE SENSITIVE!\n");
		return -ENODEV;
	}

	if (!strcmp(opermode, "AUSTRALIA")) {
		boostringer = 1;
		fxshonormode = 1;
	}

	/* for the voicedaa_check_hook defaults, if the user has not overridden
	   them by specifying them as module parameters, then get the values
	   from the selected operating mode
	*/
	if (battdebounce == 0) {
		battdebounce = fxo_modes[_opermode].battdebounce;
	}
	if (battalarm == 0) {
		battalarm = fxo_modes[_opermode].battalarm;
	}
	if (battthresh == 0) {
		battthresh = fxo_modes[_opermode].battthresh;
	}

	res = dahdi_pci_module(&wctdm_driver);
	if (res)
		return -ENODEV;
	return 0;
}

static void __exit wctdm_cleanup(void)
{
	pci_unregister_driver(&wctdm_driver);
}

module_param(debug, int, 0600);
module_param(fxovoltage, int, 0600);
module_param(loopcurrent, int, 0600);
module_param(reversepolarity, int, 0600);
module_param(robust, int, 0600);
module_param(opermode, charp, 0600);
module_param(lowpower, int, 0600);
module_param(boostringer, int, 0600);
module_param(fastringer, int, 0600);
module_param(fxshonormode, int, 0600);
module_param(battdebounce, uint, 0600);
module_param(battalarm, uint, 0600);
module_param(battthresh, uint, 0600);
module_param(alawoverride, int, 0600);
module_param(nativebridge, int, 0600);
module_param(fxotxgain, int, 0600);
module_param(fxorxgain, int, 0600);
module_param(fxstxgain, int, 0600);
module_param(fxsrxgain, int, 0600);
module_param(ringdebounce, int, 0600);
module_param(fwringdetect, int, 0600);
module_param(latency, int, 0600);
module_param(neonmwi_monitor, int, 0600);
module_param(neonmwi_level, int, 0600);
module_param(neonmwi_envelope, int, 0600);
module_param(neonmwi_offlimit, int, 0600);
#ifdef VPM_SUPPORT
module_param(vpmsupport, int, 0600);
module_param(vpmnlptype, int, 0600);
module_param(vpmnlpthresh, int, 0600);
module_param(vpmnlpmaxsupp, int, 0600);
#endif

MODULE_DESCRIPTION("Wildcard VoiceBus Analog Card Driver");
MODULE_AUTHOR("Digium Incorporated <support@digium.com>");
MODULE_ALIAS("wctdm8xxp");
MODULE_ALIAS("wctdm4xxp");
MODULE_ALIAS("wcaex24xx");
MODULE_ALIAS("wcaex8xx");
MODULE_ALIAS("wcaex8xx");
MODULE_LICENSE("GPL v2");

module_init(wctdm_init);
module_exit(wctdm_cleanup);
