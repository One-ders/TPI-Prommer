/* $PromReader: main.c, v1.1 2014/04/07 21:44:00 anders Exp $ */

/*
 * Copyright (c) 2021, Anders Franzen.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * @(#)main.c
 */

#include "sys.h"
#include "sys_env.h"
#include "io.h"
#include "gpio_drv.h"

#include <string.h>
#include <ctype.h>

static int d_fd;
static int a_fd;
static int cs_fd;
static int oe_fd;

static int inited=0;

static int init_pins(int argc, char **argv, struct Env *env);
static int deinit_pins(int argc, char **argv, struct Env *env);
static int read_prom(int argc, char **argv, struct Env *env);
static int testA0A7(int argc, char **argv, struct Env *env);
static int testA6A13(int argc, char **argv, struct Env *env);

static struct cmd cmds[] = {
	{"help", generic_help_fnc},
	{"init_pins", init_pins},
	{"deinit_pins", deinit_pins},
	{"testA0A7", testA0A7},
	{"testA6A13", testA6A13},
	{"read_prom", read_prom},
	{0,0}
};

static struct cmd_node cmn = {
	"promrd",
	cmds,
};

static unsigned int set_address(unsigned int address) {
//	unsigned int pins=(address&0x3ff)|((address<<1)&7000);
//	gpioB->ODR=(gpioB->IDR&0xffff8800)|pins;
	return 0;
}

static unsigned char read_datapins() {
//		return ((gpioA->IDR>1)&0xff);
	return 0;
}

static int udelay(unsigned int usec) {
	unsigned int count=0;
	unsigned int utime=(120*usec/7);
	do {
		if (++count>utime) return 0;
	} while (1);

	return 0;
}

static char *toIhex(char *buf, int address, int len, unsigned char *data) {
	unsigned int ck=0;
	int offs=0;
	int i;
	ck=0x10+(address&0xff)+(address>>8);
	sprintf(buf,":%02x%04x%02x",len,address,0);
	offs=9;
	for(i=0;i<len;i++) {
		ck+=data[i];
		sprintf(&buf[offs+(i*2)],"%02x",data[i]);
	}
	ck=ck&0xff;
	ck=(~ck)+1;
	ck&=0xff;
	sprintf(&buf[offs+(2*len)],"%02x", ck);

	return buf;
}


static int read_byte(unsigned int addr) {
	unsigned int pin_addr=(addr&0x7ff)|((addr<<1)&0x7000);
	int rc;
	int zero=0;
	int one=1;
	unsigned int data=0;


	rc=io_control(a_fd, GPIO_BUS_WRITE_BUS, &pin_addr, sizeof(pin_addr));
	if (rc<0) {
		printf("read byte: failed to write bus\n");
		return -1;
	}

	udelay(150);
	rc=io_control(cs_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		printf("init pins: failed to activate cs pin\n");
		return -1;
	}

	rc=io_control(oe_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		printf("read byte: failed to activate oe pin\n");
		return -1;
	}

	udelay(50);
	rc=io_control(d_fd, GPIO_BUS_READ_BUS, &data, sizeof(data));
	if (rc<0) {
		printf("read byte: failed to read data\n");
		return -1;
	}

	data=data>>1; // adjust for bus pins


	rc=io_control(oe_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		printf("read byte: failed to activate oe pin\n");
		return -1;
	}

	rc=io_control(cs_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		printf("init pins: failed to set flags cs pin\n");
		return -1;
	}

	return data;
}

static int dump_prom(int argc, char **argv, struct Env *env) {
	return 0;
}


static int testA0A7(int argc, char **argv, struct Env *env) {
	int a;
	printf("testA0A7\n");
	printf("Connect a loop from A0-A7 to D0-D7\n");

	for(a=0;a<16384;a++) {
		unsigned int b=read_byte(a);
		if (b!=(a&0xff)) {
			fprintf(env->io_fd,"got bad byte at %x, got %x\n",a,b);
			return -1;
		}
//		fprintf(env->io_fd,"address:data (%x:%x)\n",a,b);
	}
	fprintf(env->io_fd,"A success\n");
	return 0;
}

static int testA6A13(int argc, char **argv, struct Env *env) {
	int a;
	printf("testA6A13\n");
	printf("Connect a loop from A6-A13 to D0-D7\n");

	for(a=0;a<16384;a++) {
		unsigned int b=read_byte(a);
		if (b!=((a>>6)&0xff)) {
			fprintf(env->io_fd,"got bad byte at %x, got %x\n",a,b);
			return -1;
		}
//		fprintf(env->io_fd,"address:data (%x:%x)\n",a,b);
	}
	fprintf(env->io_fd,"A success\n");
	return 0;
}

static int read_prom(int argc, char **argv, struct Env *env) {

	unsigned char b[16];
	int a;

	fprintf(env->io_fd, "read prom\n");
	fprintf(env->io_fd, "==================== CUT IHEX Start ==============\n");

	for(a=0;a<0x4000;a++) {
		b[a%16]=read_byte(a);
		if (!((a+1)%16)) { // if we read a line
			char buf[64];
			fprintf(env->io_fd, "%s\n", toIhex(buf,a-15,16,b));
		}
	}

	fprintf(env->io_fd, "==================== CUT IHEX Stop ==============\n");
	fprintf(env->io_fd, "A success\n");

	return 0;
}



static int init_pins(int argc, char **argv, struct Env *env) {
	unsigned int cs_pin;
	unsigned int oe_pin;
	struct pin_spec DBus;
	struct pin_spec ABus;

	unsigned int dflags;
	unsigned int oflags;
	unsigned int one=1;

	int rc;


	if (inited) {
		fprintf(env->io_fd, "init pins: already inited\n");
		return -1;
	}

	d_fd=io_open(GPIO_DRV);
	a_fd=io_open(GPIO_DRV);
	cs_fd=io_open(GPIO_DRV);
	oe_fd=io_open(GPIO_DRV);


	if ((d_fd<0) || (a_fd<0) ||
		(cs_fd<0) || (oe_fd<0)) {
		fprintf(env->io_fd,"init pins: failed to open gpiodrv\n");
		return -1;
	}

	oflags=GPIO_DIR(0,GPIO_OUTPUT);
	oflags=GPIO_DRIVE(oflags,GPIO_PUSHPULL);
	oflags=GPIO_SPEED(oflags,GPIO_SPEED_HIGH);

	//chip select
	cs_pin=GPIO_PIN(GPIO_PC,0xd);  // active lo, also led

	rc=io_control(cs_fd, GPIO_BIND_PIN, &cs_pin, sizeof(cs_pin));
	if (rc<0) {
		printf("init pins: failed to bind cs pin\n");
		return -1;
	}


	rc=io_control(cs_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		printf("init pins: failed to set flags cs pin\n");
		return -1;
	}

	rc=io_control(cs_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		printf("init pins: failed to set flags cs pin\n");
		return -1;
	}
	printf("cd now high (not active)\n");

	// output enable
	oe_pin=GPIO_PIN(GPIO_PA,0x0);

	rc=io_control(oe_fd, GPIO_BIND_PIN, &oe_pin, sizeof(oe_pin));
	if (rc<0) {
		printf("init pins: failed to bind oe pin\n");
		return -1;
	}

	rc=io_control(oe_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		printf("init pins: failed to set flags oe pin\n");
		return -1;
	}

	//data bus
	dflags=GPIO_DIR(0,GPIO_INPUT);
	dflags=GPIO_DRIVE(dflags,GPIO_FLOAT);
	dflags=GPIO_SPEED(dflags,GPIO_SPEED_HIGH);

	DBus.port=GPIO_PA;
	DBus.flags=dflags;
	DBus.pins=0;
	DBus.pins|=1<<1;
	DBus.pins|=1<<2;
	DBus.pins|=1<<3;
	DBus.pins|=1<<4;
	DBus.pins|=1<<5;
	DBus.pins|=1<<6;
	DBus.pins|=1<<7;
	DBus.pins|=1<<8;

	rc=io_control(d_fd, GPIO_BUS_ASSIGN_PINS, &DBus, sizeof(DBus));
	if (rc<0) {
		printf("init pins: failed to bind data bus\n");
		return -1;
	}
	ABus.port=GPIO_PB;
	ABus.flags=oflags;
	ABus.pins=0;
	ABus.pins|=1<<0;
	ABus.pins|=1<<1;
	ABus.pins|=1<<2;
	ABus.pins|=1<<3;
	ABus.pins|=1<<4;
	ABus.pins|=1<<5;
	ABus.pins|=1<<6;
	ABus.pins|=1<<7;
	ABus.pins|=1<<8;
	ABus.pins|=1<<9;
	ABus.pins|=1<<10;
	ABus.pins|=1<<12;
	ABus.pins|=1<<13;
	ABus.pins|=1<<14;

	rc=io_control(a_fd, GPIO_BUS_ASSIGN_PINS, &ABus, sizeof(ABus));
	if (rc<0) {
		printf("init pins: failed to bind address bus\n");
		return -1;
	}

	inited=1;
	return 0;
}

static int deinit_pins(int argc, char **argv, struct Env *env) {
	io_close(d_fd);
	io_close(a_fd);
	io_close(cs_fd);
	io_close(oe_fd);
	inited=0;
	return 0;
}

//int main(void) {
int init_pkg(void) {
	struct Env e;
	e.io_fd=0;
	init_pins(0,0,&e);
	install_cmd_node(&cmn, root_cmd_node);
	printf("back from thread create\n");
	return 0;
}
