/*
 * mic_e: function to reformat APRS Mic Encoder compressed position report
 *  into a conventional (non-binary) tracker report.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License, version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * (See the file "COPYING" that is included with this source distribution.)
 * 
 * Copyright (c) 1997,1999 Alan Crosswell
 * Alan Crosswell, N2YGK
 * 144 Washburn Road
 * Briarcliff Manor, NY 10510, USA
 * n2ygk@weca.org
 */
#include <stdio.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include "mic_e.h"

static char *msgname[] = {
  "Off duty..",
  "Enroute...",
  "In Service",
  "Returning.",
  "Committed.",
  "Special...",
  "PRIORITY..",
  "EMERGENCY."
};

static unsigned int hex2i(u_char,u_char);

int
fmt_mic_e(const u_char *t,	/* tocall */
	  const u_char *i,	/* info */
	  const int l,		/* length of info */
	  u_char *buf1,		/* output buffer */
	  int *l1,		/* length of output buffer */
	  u_char *buf2,		/* 2nd output buffer */
	  int *l2,		/* length of 2nd output buffer */
	  time_t tick)		/* received timestamp of the packet */
{
  u_int msg,sp,dc,se,spd,cse;
  char north, west;
  int lon1,lonDD,lonMM,lonHH;
  char *bp;
  struct tm *gmt;
  enum {none=0, BETA, REV1} rev = none; /* mic_e revision level */
  int gps_valid = 0;
  char symtbl = '/', symbol = '$', etype = 'E';
  int buf2_n = 0;

  *l1 = *l2 = 0;
  
  switch (i[0]) {		/* possible valid MIC-E flags */
  case 0x60:
    gps_valid = 1;
    rev = REV1;
    break;
  case 0x27:
    gps_valid = 0;
    rev = REV1;
    break;
  case 0x1c:
    gps_valid = 1;
    rev = BETA;
    break;
  case 0x1d:
    gps_valid = 0;
    rev = BETA;
    break;
  default:
    gps_valid = 0;
    rev = none;
    break;
  }
  if (l >= 7 && (rev != none)) {
    msg = ((t[0]&0x40)?0:4) + ((t[1]&0x40)?0:2) + ((t[2]&0x40)?0:1);
    north = t[3]&0x40?'N':'S';
    west = t[5]&0x40?'W':'E';
    lon1 = t[4]&0x40;
    lonDD = i[1] - 28;
    lonMM = i[2] - 28;
    lonHH = i[3] - 28;
#ifdef DEBUG
    fprintf(stderr,"raw lon 0x%02x(%d) 0x%02x(%d) 0x%02x(%d) 0x%02x(%d)\n",
	    i[1],i[1],i[2],i[2],i[3],i[3],i[4],i[4]);
    fprintf(stderr,"before: lonDD=%d, lonMM=%d, lonHH=%d lon1=0x%0x\n",lonDD,lonMM,lonHH,lon1);
#endif
    if (rev >= REV1) {
      if (lon1)			/* do this first? */
	lonDD += 100;
      if (180 <= lonDD && lonDD <= 189)
	lonDD -= 80;
      if (190 <= lonDD && lonDD <= 199)
	lonDD -= 190;
      if (lonMM >= 60)
	lonMM -= 60;
    }
#ifdef DEBUG
    fprintf(stderr,"after: lonDD=%d, lonMM=%d, lonHH=%d lon1=0x%0x\n",lonDD,lonMM,lonHH,lon1);
#endif
    sp = i[4] - 28;
    dc = i[5] - 28;
    se = i[6] - 28;
    buf2_n = 6;			/* keep track of where we are */
#ifdef DEBUG
    fprintf(stderr,"sp=%02x dc==%02x se=%02x\n",sp,dc,se);
#endif
    spd = sp*10+dc/10;
    cse = (dc%10)*100+se;
    if (rev >= REV1) {
      if (spd >= 800)
	spd -= 800;
      if (cse >= 400)
	cse -= 400;
    }
    gmt = gmtime(&tick);
#ifdef DEBUG
    fprintf(stderr,"symbol=0x%02x, symtbl=0x%02x\n",i[7],i[8]);
#endif
    symtbl = (l >= 8 && rev >= REV1)? i[8] : '/';
    /* rev1 bug workaround:  sends null symbol/table bytes */
    if (symtbl != '/' && symtbl != '\\' 
	&& !('0' <= symtbl && symtbl <= '9')
	&& !('A' <= symtbl && symtbl <= 'J')
	&& symtbl != '*' && symtbl != '!')
      symtbl = '/';
    symbol = i[7];
    if (!isprint(symbol))
      symbol = '$';

    buf2_n = (rev == BETA)?8:9;
    if (l >= 10) {
      buf2_n = (rev == BETA)?8:9;
      switch (i[buf2_n]) {
      case '>':		/* Kenwood TH-D7: no telemetry */
	etype = 'T';
	buf2_n++;
	break;
      case 0x60:		/* two-channel telemetry */
	sprintf(buf2,"T#MIC%03d,%03d",
		hex2i(i[buf2_n+1],i[buf2_n+2]),
		hex2i(i[buf2_n+3],i[buf2_n+4]));
	buf2_n+=5;
	*l2 = strlen(buf2);
	break;
      case 0x27:		/* five-channel telemetry */
	sprintf(buf2,"T#MIC%03d,%03d,%03d,%03d,%03d",
		hex2i(i[buf2_n+1],i[buf2_n+2]),
		hex2i(i[buf2_n+3],i[buf2_n+4]),
		hex2i(i[buf2_n+5],i[buf2_n+6]),
		hex2i(i[buf2_n+7],i[buf2_n+8]),
		hex2i(i[buf2_n+9],i[buf2_n+10]));
	buf2_n+=11;
	*l2 = strlen(buf2);
	break;
      case 0x1d:		/* five-channel telemetry */
	sprintf(buf2,"T#MIC%03d,%03d,%03d,%03d,%03d",
		i[buf2_n+1],i[buf2_n+2],i[buf2_n+3],i[buf2_n+4],i[buf2_n+5]);
	buf2_n+=6;
	*l2 = strlen(buf2);
	break;
      default:
	break;			/* no telemetry */
      }
    }
    sprintf(buf1,"@%02d%02d%02dz%d%d%d%d.%d%d%c%c%03d%02d.%02d%c%c%03d/%03d/%c>mon/M%d/%s",
	    gmt->tm_mday,gmt->tm_hour,gmt->tm_min,
	    t[0]&0x0f,t[1]&0x0f,t[2]&0x0f,t[3]&0x0f,t[4]&0x0f,t[5]&0x0f,
	    north,symtbl,
	    lonDD,lonMM,lonHH,
	    west,
	    symbol,
	    cse,spd,
	    etype,
	    msg,msgname[msg]);
    bp = &buf1[(*l1 = strlen(buf1))];
    if (buf2_n < l) {		/* additional comments/btext */
      *bp++ = ' ';		/* add a space */
      (*l1)++;
      memcpy(bp,&i[buf2_n],l-buf2_n);
      *l1 += l-buf2_n;
    }
  }
  return ((*l1>0)+(*l2>0));
}

/*
 * here's another APRS device that get's mistreated by some APRS
 * implementations:  A TNC2 running TheNet X1J4.
 *
 *          1         2
 * 1234567890123456789012
 * TheNet X-1J4  (RELAY)
 */
static u_char x1j4prefix[] = "TheNet X-1J4";
#define MINX1J4LEN 14		/* min length w/TheNet X1J4 prefix */
#define MAXX1J4LEN 22		/* max length w/(alias) */
int
fmt_x1j4(const u_char *t,	/* tocall */
	 const u_char *i,	/* info */
	 const int l,		/* length of info */
	 u_char *buf1,		/* output buffer */
	 int *l1,		/* length of output buffer */
	 u_char *buf2,		/* 2nd output buffer */
	 int *l2,		/* length of 2nd output buffer */
	 time_t tick)		/* received timestamp of the packet */
{
  const u_char *cp;
  *l1 = *l2 = 0;
  if (l < MINX1J4LEN || memcmp(i,x1j4prefix,sizeof(x1j4prefix)-1) != 0)
    return 0;
  /* it's a TheNet beacon: either has ()'s w/alias name or no ()'s
     and the btext starts after some white space! */
  cp = memchr(&i[MINX1J4LEN],')',MAXX1J4LEN-MINX1J4LEN);
  if (cp == NULL)		/* no parens (no alias call) */
    cp = &i[MINX1J4LEN];
  else
    cp++;			/* skip over the ) */
  while (cp <= &i[l-1] && *cp == ' ') cp++; /* skip blanks */
  memcpy(buf1,cp,*l1=&i[l-1]-cp);
  return 1;
}

static unsigned int hex2i(u_char a,u_char b)
{
  unsigned int r = 0;

  if (a >= '0' && a <= '9') {
   r = (a-'0') << 4;
  } else if (a >= 'A' && a <= 'F') {
   r = (a-'A'+10) << 4;
  }
  if (b >= '0' && b <= '9') {
   r += (b-'0');
  } else if (b >= 'A' && b <= 'F') {
   r += (b-'A'+10);
  }
  return r;
}
