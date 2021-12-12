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

#define PROM_27C128	1
#define PROM_27SF512	2

static int d_fd;
static int a_fd;
static int cs_fd;
static int oe_fd;
static int vpp_fd;
static int pgm_fd;
static int pa9_12v_fd;
static int oe_12v_fd;
static int pa15_fd;

static int configured=0;
static int prom_size=0;

static int config(int argc, char **argv, struct Env *env);
static int unconfig(int argc, char **argv, struct Env *env);
static int read_prom(int argc, char **argv, struct Env *env);
static int testA0A7(int argc, char **argv, struct Env *env);
static int testA6A13(int argc, char **argv, struct Env *env);
static int erase_prom(int argc, char **argv, struct Env *env);
static int burn_prom(int argc, char **argv, struct Env *env);

static struct cmd cmds[] = {
	{"help", generic_help_fnc},
	{"config", config},
	{"unconfig", unconfig},
	{"testA0A7", testA0A7},
	{"testA6A13", testA6A13},
	{"read", read_prom},
	{"erase", erase_prom},
	{"burn", burn_prom},
	{0,0}
};

static struct cmd_node cmn = {
	"prom",
	cmds,
};

static int udelay(unsigned int usec) {
	volatile unsigned int count=0;
	volatile unsigned int utime=(120*usec/7);
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
	unsigned int pin_addr=(addr&0x7ff)|((addr<<1)&0xf000);
	int rc;
	int zero=0;
	int one=1;
	int pa15=0;
	unsigned int data=0;

	if (addr&0x8000) {
		pa15=1;
	} else {
		pa15=0;
	}
	rc=io_control(pa15_fd, GPIO_SET_PIN, &pa15, sizeof(pa15));
	if (rc<0) {
		printf("read byte: failed to set pa15\n");
		return -1;
	}


	rc=io_control(a_fd, GPIO_BUS_WRITE_BUS, &pin_addr, sizeof(pin_addr));
	if (rc<0) {
		printf("read byte: failed to write bus\n");
		return -1;
	}

	udelay(5);
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

	udelay(10);
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

static int set_read_mode_databus(int fd);
static int set_write_mode_databus(int fd);

static int write_byte(unsigned int addr, unsigned int val) {
	unsigned int pin_addr=(addr&0x7ff)|((addr<<1)&0xf000);
	unsigned int pin_data=val<<1;
	int rc;
	int zero=0;
	int one=1;
	int pa15=0;

	if (addr&0x8000) {
		pa15=1;
	} else {
		pa15=0;
	}
	rc=io_control(pa15_fd, GPIO_SET_PIN, &pa15, sizeof(pa15));
	if (rc<0) {
		printf("write byte: failed to set pa15\n");
		return -1;
	}


	rc=io_control(a_fd, GPIO_BUS_WRITE_BUS, &pin_addr, sizeof(pin_addr));
	if (rc<0) {
		printf("write byte: failed to write bus\n");
		return -1;
	}

	rc=io_control(d_fd, GPIO_BUS_WRITE_BUS, &pin_data, sizeof(pin_data));
	if (rc<0) {
		printf("write byte: failed to write bus\n");
		return -1;
	}

	rc=io_control(vpp_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		printf("read byte: failed to activate vpp pin\n");
		return -1;
	}

	udelay(50);
	rc=io_control(cs_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		printf("init pins: failed to activate cs pin\n");
		return -1;
	}

	udelay(100);

	rc=io_control(cs_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		printf("init pins: failed to set flags cs pin\n");
		return -1;
	}

	rc=io_control(vpp_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		printf("read byte: failed to inactivate vpp pin\n");
		return -1;
	}

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


static char *strstr(const char *haystack, const char *needle) {
	const char *p=haystack;
	int needle_size=strlen(needle);
	int mcnt=0;
	const char *r=0;

	while(*p!=0) {
		if (*p==needle[mcnt]) {
			if (!mcnt) r=p;
			mcnt++;
			if (mcnt>=needle_size) return (char *)r;
			p++;
		} else {
			if (mcnt) {
				mcnt=0;
				r=0;
			} else {
				p++;
			}
		}
	}
	return 0;
}

static unsigned int char2num(unsigned char c) {
	switch(c) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			return c-'0';
			break;
		case 'a':
		case 'b':
		case 'c':
		case 'd':
		case 'e':
		case 'f':
			return c-'a'+10;
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'E':
		case 'F':
			return c-'A'+10;
			break;
	}
	return 0;
}

static unsigned int decode_dbyte(char *p) {
	unsigned int bval=0;
	int i;

	for(i=0;i<2;i++) {
		bval=(bval<<4)|char2num(p[i]);
	}
	return bval;
}


unsigned char cmp_buf[256];
static int burn_line(unsigned char *line, int len, unsigned int start_address, int io_fd) {
	int i;
	unsigned int address=start_address;

	set_write_mode_databus(io_fd);
	for(i=0;i<len;i++) {
		write_byte(address, line[i]);
		address++;
	}

	set_read_mode_databus(io_fd);

	address=start_address;
	for(i=0;i<len;i++) {
		unsigned int a=read_byte(address);
		if (a!=line[i]) {
			fprintf(io_fd, "missmatch at addr %04x, want %02x got %02x\n", address, line[i], a);
			return -1;
		}
		address++;
	}
	return 0;
}

unsigned char bin_buf[256];

static int parse_ihex(char *line, int size, int offset, int io_fd) {
	int i=0;
	int j;
	int datalen=0;
	unsigned int address=0;
	int rtype=0;
	int cksum=0;
	int checksum;
	int val;

	while(i<size) {
		if (line[i++]==':') {
			break;
		}
	}
//	printf("new record, ");

//========== Read len
	datalen=val=decode_dbyte(&line[i]);
	i+=2;
//	printf("len=%d, ", datalen);
	cksum=val;

//========== Read address
	address=val=decode_dbyte(&line[i]);
	i+=2;
	cksum+=val;

	val=decode_dbyte(&line[i]);
	i+=2;
	address=(address<<8)|val;
	cksum+=val;
//	printf("address=%04x, data:", address);

//========== Read record type
	rtype=val=decode_dbyte(&line[i]);
	i+=2;
	cksum+=val;
	if (rtype!=0) {
		fprintf(io_fd, "unhandled record type %x\n", rtype);
	}
//	printf("rt=%02x, ", rtype);

//========== Read data
	for(j=0;j<datalen;j++) {
		bin_buf[j]=val=decode_dbyte(&line[i]);
		i+=2;
		cksum+=val;
//		printf("%02x,", bin_buf[j]);
	}

//========== Read check sum
	checksum=decode_dbyte(&line[i]);
	cksum+=checksum;
	cksum&=0xff;

	if (cksum) {
		fprintf(io_fd, "checksum error for line with address %04x\n", address);
		return -1;
	}

	burn_line(bin_buf, datalen, address+offset,io_fd);

	return 0;
}

static int readfln(int fd, char *buf, int bsize) {
	unsigned char ch;
	int i=0;
	int rc;
	int state=0;   // 1 means seen CR waiting for LF

	while(1) {
		rc=io_read(fd,&ch,1);
		if (rc<0) return 0;
		switch(state) {
			case 0:
				if (ch==0xd) {
					state=1;
				}
				buf[i]=ch;
				i++;
				if (i>=bsize) return i;
				break;
			case 1:
				if (ch==0xa) {
					return i;
				} else if (ch!=0xd) {
					state=0;
				}
				break;
		}
	}
}

static int erase_prom(int argc, char **argv, struct Env *env) {
	unsigned int b;
	int a;
	int rc;
	int zero=0;
	int one=1;

	if (configured!=PROM_27SF512) {
		fprintf(env->io_fd, "can only erase 27SF512\n");
		return -1;
	}

	fprintf(env->io_fd, "erase prom\n");
	fprintf(env->io_fd, "hit return to start\n");

	rc=io_control(vpp_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		printf("init pins: failed to activate vpp pin\n");
		return -1;
	}

	rc=io_control(pa9_12v_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		printf("read byte: failed to activate pa9_12v pin\n");
		return -1;
	}

	rc=io_control(cs_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		printf("read byte: failed to activate cs pin\n");
		return -1;
	}


	sleep(900);

	rc=io_control(cs_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		printf("read byte: failed to activate ce pin\n");
		return -1;
	}

	rc=io_control(vpp_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		printf("init pins: failed to deactivate vpp pin\n");
		return -1;
	}

	rc=io_control(pa9_12v_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		printf("read byte: failed to deactivate pa9_12v pin\n");
		return -1;
	}



	for(a=0;a<prom_size;a++) {
		b=read_byte(a);
		if (b!=0xff) { //
			fprintf(env->io_fd, "failed to erase at %d, val %x\n", a, b);
			rc=-1;
		}
	}

	if (rc==0) {
		fprintf(env->io_fd, "erase A success\n");
	}

	return rc;
}

static int read_prom(int argc, char **argv, struct Env *env) {
	struct getopt_data gd;
	int offset=0;
	int opt;
	unsigned char b[16];
	int a;

	getopt_data_init(&gd);

	while ((opt=getopt_r(argc,argv,"o:", &gd))!=-1) {
		switch(opt) {
		case 'o':
			offset=strtoul(gd.optarg,0,0);
			fprintf(env->io_fd, "from offset of %x\n", offset);
		}
	}


	fprintf(env->io_fd, "read prom\n");
	fprintf(env->io_fd, "==================== CUT IHEX Start ==============\n");

	for(a=offset;a<prom_size;a++) {
		b[a%16]=read_byte(a);
		if (!((a+1)%16)) { // if we read a line
			char buf[64];
			unsigned int la=a-offset;
			fprintf(env->io_fd, "%s\n", toIhex(buf,la-15,16,b));
		}
	}

	fprintf(env->io_fd, "==================== CUT IHEX Stop ==============\n");
	fprintf(env->io_fd, "A success\n");

	return 0;
}


static char pi_buf[512];
static int burn_prom(int argc, char **argv, struct Env *env) {

	struct getopt_data gd;
	int offset=0;
	int opt;
	int rc;

	if (configured!=PROM_27SF512) {
		fprintf(env->io_fd, "can only erase 27SF512\n");
		return -1;
	}

	getopt_data_init(&gd);

	while ((opt=getopt_r(argc,argv,"o:", &gd))!=-1) {
		switch(opt) {
		case 'o':
			offset=strtoul(gd.optarg,0,0);
			fprintf(env->io_fd, "load with offset of %x\n", offset);
		}
	}

	fprintf(env->io_fd, "burn prom\n");
	fprintf(env->io_fd, "send ihex file from terminal emulator\n");


	while (1) {
		rc=readfln(env->io_fd, pi_buf, 512);
		if (rc<=0) {
			fprintf(env->io_fd,"read error\n");
			break;
		}
//		if (strstr(pi_buf, "IHEX")==0) continue;
		pi_buf[rc]=0;
		parse_ihex(pi_buf,rc,offset,env->io_fd);
		if (strstr(pi_buf, "End IHEX")!=0) break;
	}

	sleep(1000);
	fprintf(env->io_fd, "\n\nburn prom done\n");
	return rc;
}

static int set_write_mode_databus(int io_fd) {
	unsigned int oflags;
	int rc;

	oflags=GPIO_DIR(0,GPIO_OUTPUT);
	oflags=GPIO_DRIVE(oflags,GPIO_PUSHPULL);
	oflags=GPIO_SPEED(oflags,GPIO_SPEED_HIGH);

	rc=io_control(d_fd, GPIO_BUS_UPDATE_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "databus write mode: failed to update flags\n");
		return -1;
	}
	return 0;
}

static int set_read_mode_databus(int io_fd) {
	unsigned int dflags;
	int rc;

	//data bus
	dflags=GPIO_DIR(0,GPIO_INPUT);
	dflags=GPIO_DRIVE(dflags,GPIO_FLOAT);
	dflags=GPIO_SPEED(dflags,GPIO_SPEED_HIGH);

	rc=io_control(d_fd, GPIO_BUS_UPDATE_FLAGS, &dflags, sizeof(dflags));
	if (rc<0) {
		fprintf(io_fd, "databus read mode: failed to update flags\n");
		return -1;
	}
	return 0;
}



static int config_27SF512(int io_fd) {
	unsigned int cs_pin;     // PC13
	unsigned int oe_pin;     // PA0
	unsigned int vpp_pin;    // PA10
	unsigned int pa9_12v_pin;// PA15
	struct pin_spec DBus;    // PA1-PA8
	unsigned int pa15_pin;   // PA9
	struct pin_spec ABus;	 // PB0-PB10,PB12-PB15

	unsigned int dflags;
	unsigned int oflags;
	unsigned int one=1;
	unsigned int zero=0;

	int rc;


	if (configured) {
		fprintf(io_fd, "init pins: already inited\n");
		return -1;
	}

	d_fd=io_open(GPIO_DRV);
	a_fd=io_open(GPIO_DRV);
	cs_fd=io_open(GPIO_DRV);
	oe_fd=io_open(GPIO_DRV);
	vpp_fd=io_open(GPIO_DRV);
	pa9_12v_fd=io_open(GPIO_DRV); // PA15
	pa15_fd=io_open(GPIO_DRV);  // PA9


	if ((d_fd<0) || (a_fd<0) ||
		(cs_fd<0) || (oe_fd<0)) {
		fprintf(io_fd,"init pins: failed to open gpiodrv\n");
		return -1;
	}

	oflags=GPIO_DIR(0,GPIO_OUTPUT);
	oflags=GPIO_DRIVE(oflags,GPIO_PUSHPULL);
	oflags=GPIO_SPEED(oflags,GPIO_SPEED_HIGH);

	//chip select
	cs_pin=GPIO_PIN(GPIO_PC,0xd);  // active lo, also led

	rc=io_control(cs_fd, GPIO_BIND_PIN, &cs_pin, sizeof(cs_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind cs pin\n");
		return -1;
	}


	rc=io_control(cs_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags cs pin\n");
		return -1;
	}

	rc=io_control(cs_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags cs pin\n");
		return -1;
	}
	fprintf(io_fd, "cd now high (not active)\n");

	// output enable
	oe_pin=GPIO_PIN(GPIO_PA,0x0);

	rc=io_control(oe_fd, GPIO_BIND_PIN, &oe_pin, sizeof(oe_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind oe pin\n");
		return -1;
	}

	rc=io_control(oe_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags oe pin\n");
		return -1;
	}

	rc=io_control(oe_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags oe pin\n");
		return -1;
	}

	// vpp
	vpp_pin=GPIO_PIN(GPIO_PA,0xA);

	rc=io_control(vpp_fd, GPIO_BIND_PIN, &vpp_pin, sizeof(vpp_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind vpp pin\n");
		return -1;
	}

	rc=io_control(vpp_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags vpp pin\n");
		return -1;
	}

	rc=io_control(vpp_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags vpp pin\n");
		return -1;
	}

	// pa9_12v
	pa9_12v_pin=GPIO_PIN(GPIO_PA,0xF);

	rc=io_control(pa9_12v_fd, GPIO_BIND_PIN, &pa9_12v_pin, sizeof(pa9_12v_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind pa9_12v pin\n");
		return -1;
	}

	rc=io_control(pa9_12v_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags pa9_12v pin\n");
		return -1;
	}

	rc=io_control(pa9_12v_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags pa9_12v pin\n");
		return -1;
	}

	// pa_15
	pa15_pin=GPIO_PIN(GPIO_PA,0x9);

	rc=io_control(pa15_fd, GPIO_BIND_PIN, &pa15_pin, sizeof(pa15_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind pa15 pin\n");
		return -1;
	}

	rc=io_control(pa15_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags pa15 pin\n");
		return -1;
	}

	rc=io_control(pa15_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags pa15 pin\n");
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
		fprintf(io_fd, "init pins: failed to bind data bus\n");
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
	ABus.pins|=1<<15;

	rc=io_control(a_fd, GPIO_BUS_ASSIGN_PINS, &ABus, sizeof(ABus));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind address bus\n");
		return -1;
	}

	configured=PROM_27SF512;
	prom_size=(512*1024)/8;
	return 0;
}


static int config_27C128(int io_fd) {
	unsigned int cs_pin;     // PC13
	unsigned int oe_pin;     // PA0
	unsigned int vpp_pin;    // PA9
	unsigned int pgm_pin;    // PB15
	unsigned int pa9_12v_pin;// PA15
	unsigned int oe_12v_pin; // PA10
	struct pin_spec DBus;    // PA1-PA8
	struct pin_spec ABus;	 // PB0-PB10,PB12-PB14

	unsigned int dflags;
	unsigned int oflags;
	unsigned int one=1;
	unsigned int zero=0;

	int rc;


	if (configured) {
		fprintf(io_fd, "init pins: already inited\n");
		return -1;
	}

	d_fd=io_open(GPIO_DRV);
	a_fd=io_open(GPIO_DRV);
	cs_fd=io_open(GPIO_DRV);
	oe_fd=io_open(GPIO_DRV);
	vpp_fd=io_open(GPIO_DRV);
	pgm_fd=io_open(GPIO_DRV);
	pa9_12v_fd=io_open(GPIO_DRV); // PA15
	oe_12v_fd=io_open(GPIO_DRV);  // PA10


	if ((d_fd<0) || (a_fd<0) ||
		(cs_fd<0) || (oe_fd<0) ||
		(vpp_fd<0) || (pgm_fd<0) ||
		(pa9_12v_fd<0) || (oe_12v_fd<0)) {
		fprintf(io_fd,"init pins: failed to open gpiodrv\n");
		return -1;
	}

	oflags=GPIO_DIR(0,GPIO_OUTPUT);
	oflags=GPIO_DRIVE(oflags,GPIO_PUSHPULL);
	oflags=GPIO_SPEED(oflags,GPIO_SPEED_HIGH);

	//chip select
	cs_pin=GPIO_PIN(GPIO_PC,0xd);  // active lo, also led

	rc=io_control(cs_fd, GPIO_BIND_PIN, &cs_pin, sizeof(cs_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind cs pin\n");
		return -1;
	}


	rc=io_control(cs_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags cs pin\n");
		return -1;
	}

	rc=io_control(cs_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags cs pin\n");
		return -1;
	}
	fprintf(io_fd,"cs now high (not active)\n");

	// output enable
	oe_pin=GPIO_PIN(GPIO_PA,0x0);

	rc=io_control(oe_fd, GPIO_BIND_PIN, &oe_pin, sizeof(oe_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind oe pin\n");
		return -1;
	}

	rc=io_control(oe_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags oe pin\n");
		return -1;
	}

	rc=io_control(oe_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set oe pin\n");
		return -1;
	}

	// vpp
	vpp_pin=GPIO_PIN(GPIO_PA,0x9);

	rc=io_control(vpp_fd, GPIO_BIND_PIN, &vpp_pin, sizeof(vpp_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind vpp pin\n");
		return -1;
	}

	rc=io_control(vpp_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags vpp pin\n");
		return -1;
	}

	rc=io_control(vpp_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags vpp pin\n");
		return -1;
	}

	// pgm
	pgm_pin=GPIO_PIN(GPIO_PB,0xF);

	rc=io_control(pgm_fd, GPIO_BIND_PIN, &pgm_pin, sizeof(pgm_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind pgm pin\n");
		return -1;
	}

	rc=io_control(pgm_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags pgm pin\n");
		return -1;
	}

	rc=io_control(pgm_fd, GPIO_SET_PIN, &one, sizeof(one));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags vpp pin\n");
		return -1;
	}

	// a912v
	pa9_12v_pin=GPIO_PIN(GPIO_PA,0xF);

	rc=io_control(pa9_12v_fd, GPIO_BIND_PIN, &pa9_12v_pin, sizeof(pa9_12v_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind pa9_12v_pin pin\n");
		return -1;
	}

	rc=io_control(pa9_12v_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags pa9_12_v pin\n");
		return -1;
	}

	rc=io_control(pa9_12v_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags pa9_12v pin\n");
		return -1;
	}

	// oe12v
	oe_12v_pin=GPIO_PIN(GPIO_PA,0x0a);

	rc=io_control(oe_12v_fd, GPIO_BIND_PIN, &oe_12v_pin, sizeof(oe_12v_pin));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to bind oe_12v_pin pin\n");
		return -1;
	}

	rc=io_control(oe_12v_fd, GPIO_SET_FLAGS, &oflags, sizeof(oflags));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags oe_12_v pin\n");
		return -1;
	}

	rc=io_control(oe_12v_fd, GPIO_SET_PIN, &zero, sizeof(zero));
	if (rc<0) {
		fprintf(io_fd, "init pins: failed to set flags oe_12v pin\n");
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
		fprintf(io_fd, "init pins: failed to bind data bus\n");
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
		fprintf(io_fd, "init pins: failed to bind address bus\n");
		return -1;
	}

	configured=PROM_27C128;
	prom_size=(128*1024)/8;
	return 0;
}

static int config(int argc, char **argv, struct Env *env) {

	if (argc<2) {
		fprintf(env->io_fd, "need chip ID (27C128 or 27SF512) as an argument\n");
		return -1;
	}
	if (strcmp(argv[1], "27C128")==0) {
		config_27C128(env->io_fd);
	} else if (strcmp(argv[1],"27SF512")==0) {
		config_27SF512(env->io_fd);
	} else {
		fprintf(env->io_fd, "Dont know how to configure %s\n", argv[1]);
	}
	return 0;
}

static int unconfig(int argc, char **argv, struct Env *env) {
	if (d_fd) { io_close(d_fd); d_fd=0;}
	if (a_fd) { io_close(a_fd); a_fd=0;}
	if (cs_fd) { io_close(cs_fd); cs_fd=0;}
	if (oe_fd) { io_close(oe_fd); oe_fd=0;}
	if (vpp_fd) {io_close(vpp_fd); vpp_fd=0;}
	if (pgm_fd) {io_close(pgm_fd); pgm_fd=0;}
	if (pa9_12v_fd) { io_close(pa9_12v_fd); pa9_12v_fd=0;}
	if (oe_12v_fd) { io_close(oe_12v_fd); oe_12v_fd=0;}
	if (pa15_fd) {io_close(pa15_fd); pa15_fd=0;}

	configured=0;
	prom_size=0;
	return 0;
}

extern int init_pin_test();

//int main(void) {
int init_pkg(void) {
//	struct Env e;
//	e.io_fd=0;
//	init_pins(0,0,&e);
	install_cmd_node(&cmn, root_cmd_node);
	init_pin_test();
	printf("back from thread create\n");
	return 0;
}
