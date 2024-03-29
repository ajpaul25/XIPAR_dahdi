/*
 * Written by Mark Spencer <markster@digium.com>
 * Based on previous works, designs, and architectures conceived and
 * written by Jim Dixon <jim@lambdatel.com>.
 *
 * Copyright (C) 2001 Jim Dixon / Zapata Telephony.
 * Copyright (C) 2001-2008 Digium, Inc.
 *
 * All rights reserved.
 *
 * Primary Author: Mark Spencer <markster@digium.com>
 * Radio Support by Jim Dixon <jim@lambdatel.com>
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

#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <linux/types.h>
#include <linux/ppp_defs.h> 
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdlib.h>
#include "bittest.h"

#include <dahdi/user.h>
#include "dahdi_tools_version.h"

#define BLOCK_SIZE 2039

void print_packet(unsigned char *buf, int len)
{
	int x;
	printf("{ ");
	for (x=0;x<len;x++)
		printf("%02x ",buf[x]);
	printf("}\n");
}

int main(int argc, char *argv[])
{
	int fd;
	int res, x;
	struct dahdi_params tp;
	int bs = BLOCK_SIZE;
	unsigned char c=0;
	unsigned char outbuf[BLOCK_SIZE];
	int setup=0;
	int errors=0;
	int bytes=0;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <DAHDI device>\n", argv[0]);
		exit(1);
	}
	fd = open(argv[1], O_RDWR, 0600);
	if (fd < 0) {
		fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
		exit(1);
	}
	if (ioctl(fd, DAHDI_SET_BLOCKSIZE, &bs)) {
		fprintf(stderr, "Unable to set block size to %d: %s\n", bs, strerror(errno));
		exit(1);
	}
	if (ioctl(fd, DAHDI_GET_PARAMS, &tp)) {
		fprintf(stderr, "Unable to get channel parameters\n");
		exit(1);
	}
	ioctl(fd, DAHDI_GETEVENT);
	for(;;) {
		res = bs;
		res = read(fd, outbuf, res);
		if (res < bs) {
			int e;
			struct dahdi_spaninfo zi;
			res = ioctl(fd,DAHDI_GETEVENT,&e);
			if (res == -1)
			{
				perror("DAHDI_GETEVENT");
				exit(1);
			}
			if (e == DAHDI_EVENT_NOALARM)
				printf("ALARMS CLEARED\n");
			if (e == DAHDI_EVENT_ALARM)
			{
				zi.spanno = 0;
				res = ioctl(fd,DAHDI_SPANSTAT,&zi);
				if (res == -1)
				{
					perror("DAHDI_SPANSTAT");
					exit(1);
				}
				printf("Alarm mask %x hex\n",zi.alarms);
			}
			continue;
		}
		if (!setup) {
			c = outbuf[0];
			setup++;
		}
		for (x=0;x<bs;x++)  {
			if (outbuf[x] != c) {
				printf("(Error %d): Unexpected result, %d != %d, %d bytes since last error.\n", ++errors, outbuf[x], c, bytes); 
				c = outbuf[x];
				bytes=0;
			}
			c = bit_next(c);
			bytes++;
		}
#if 0
		printf("(%d) Wrote %d bytes\n", packets++, res);
#endif
	}
	
}
