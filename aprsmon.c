/* aprsmon: An incredible simulation of a non-KISS TNC with MONitor ON
 *  (for UI frames only) on a Linux AX.25 interface.  Why?  So it can
 *  be run under inetd and connected to by programs like javAPRS that
 *  want to see a TNC datastream.
 *  (e.g. FROM>TO,DIGI,DIGI...DIGI:text)
 *
 * Runs in two modes:
 *  1. Just spits out received data to stdout.
 *  2. Buffers up some number of minutes worth of received packets into
 *     a segment.  #1 mode dumps contents of this segment before going "live."
 *
 * A socket opened in SOCK_PACKET mode returns frames that are exactly
 * what a KISS TNC sends: a zero, indicating data, followed by the
 * contents of the AX.25 packet.  This program has to decode all that
 * AX.25 junk all over again just to print it out in generic TNC MONitor
 * format.
 *
 * This program is derived from AX.25 utils 2.0.12 listen(8) written by
 * (according to the lsm):
 * 
 *      jsn@cs.nott.ac.uk (Jonathan Naylor)
 *  	tpmannin@cc.hut.fi (Tomi Manninen)
 *      A.Cox@swansea.ac.uk (Alan Cox)
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
 * portions Copyright (c) 1996,1997,1998,1999 Alan Crosswell
 * Alan Crosswell, N2YGK
 * 144 Washburn Road
 * Briarcliff Manor, NY 10510, USA
 * n2ygk@weca.org
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>

#include <linux/ax25.h>
#include "netax25/axconfig.h"

#ifndef HAVE_LIBAX25_EXTENSIONS
#include "libax25ext.h"
#endif
#ifdef USE_SHM
#include "aprsshm.h"
#endif
#include "mic_e.h"

#define	ALEN		6
#define	AXLEN		7
/* SSID mask and various flags that are stuffed into the SSID byte */
#define	SSID		0x1E
#define	HDLCAEB		0x01
#define	REPEATED	0x80

#define	UI		0x03	/* unnumbered information (unproto) */
#define	PID_NO_L3	0xF0	/* no level-3 (text) */

void dump(FILE *f,unsigned char *, int,time_t);
void fmt(const unsigned char *, int, unsigned char **, unsigned char **,time_t);
char *pax25(char *, const unsigned char *);
void cleanup(int);

int master = 0;			/* data collector */
int keepfor = 30;		/* minutes to keep collected data.  */
int raw = 0;			/* print raw data */
char *infofile = "/var/ax25/aprsmon.info";
#ifndef PACKAGE
#define PACKAGE "aprsmon"
#endif

#ifndef VERSION
#define VERSION "$Revision$"
#endif

char *title = "Live data from Linux";

int main(int argc, char **argv)
{
  unsigned char buffer[1500];
  int size;
  int s;
  char *port = NULL, *dev = NULL;
  struct sockaddr sa;
  struct ifreq ifr;
  int proto = ETH_P_AX25;
  time_t now;

#ifdef USE_SHM
#define OPTS "p:amk:i:t:rv"
#else
#define OPTS "p:at:rv"
#endif
  while ((s = getopt(argc, argv, OPTS)) != -1) {
    switch (s) {
#ifdef USE_SHM
    case 'm':
      master++;
      break;
    case 'i':
      infofile = optarg;	/* file name for passing seg/sem info */
      break;
    case 'k':			/* keep for optarg minutes */
      keepfor = atoi(optarg);
      break;
#endif
    case 'p':			/* entry in axports */
      port = optarg;
      break;
    case 'a': /* allows 'hearing' my transmitted packets */
      proto = ETH_P_ALL;
      break;
    case 'r':			/* raw data printing */
      raw++;
      break;
    case 't':
      title = optarg;
      break;
    case 'v':
      printf("aprsmon: %s-%s\n",PACKAGE,VERSION);
      exit(0);
    case '?':
#ifdef USE_SHM
      fprintf(stderr, "Usage: aprsmon [-ar] [-p port] [-m [-k minutes]] [-i infofile] [-t title]\n");
#else
      fprintf(stderr, "Usage: aprsmon [-ar] [-p port] [-t title]\n");
#endif
      return 1;
    }
  }

  if (ax25_config_load_ports() == 0)
    fprintf(stderr, "aprsmon: no AX.25 port data configured\n");
  
  if (port != NULL) {
    if ((dev = ax25_config_get_dev(port)) == NULL) {
      fprintf(stderr, "aprsmon: invalid port name - %s\n", port);
      return 1;
    }
  }
  
  if ((s = socket(AF_PACKET, SOCK_PACKET, htons(proto))) == -1) {
    perror("socket");
    return 1;
  }
  signal(SIGHUP,cleanup);
  signal(SIGINT,cleanup);
  signal(SIGQUIT,cleanup);
  signal(SIGPWR,cleanup);
  signal(SIGPIPE,cleanup);
  
#ifdef USE_SHM
  if (master) {
    if (shm_master(keepfor,infofile))
      return 1;
  } else 
#endif
    {
    struct utsname me;

    uname(&me);
    printf("aprsmon>JAVA:javaMSG  :Linux APRS server (%s-%s by Alan Crosswell, N2YGK) on %s running %s %s\r\n",
	   PACKAGE, VERSION, me.nodename, me.sysname, me.release);
    printf("# %s\r\n",title);
#ifdef USE_SHM
    (void) shm_slave(stdout,infofile);
#endif
    printf("# Live data follows:\r\n");
    printf("aprsmon>JAVA:javaTITLE:%s\r\n",title);
    fflush(stdout);
  }
  for (;;) {
    int asize;

    asize = sizeof(sa);
    if ((size = recvfrom(s, buffer, sizeof(buffer), 0, &sa, &asize)) == -1) {
      perror("recv");
      return 1;
    }
    if (dev != NULL && strcmp(dev, sa.sa_data) != 0)
      continue; 
    
    time(&now);
    if (proto == ETH_P_ALL) {	/* promiscuous mode? */
      strcpy(ifr.ifr_name, sa.sa_data);
      if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0
	  || ifr.ifr_hwaddr.sa_family != AF_AX25)
	continue;		/* not AX25 so ignore this packet */
    }
#ifdef USE_SHM
    if (master)
      collect(buffer,size);
    else
#endif
      dump(stdout,buffer,size,now);
  }
}

void
dump(FILE *f,
     unsigned char *buf,
     int len,
     time_t now)
{
  unsigned char *cp1,*cp2;

  fmt(buf,len,&cp1,&cp2,now);
  if (*cp1) {
    fprintf(f,"%s\r\n",cp1);
  }
  if (*cp2) {
    fprintf(f,"%s\r\n",cp2);
  }
  fflush(f);
}

void
fmt(const unsigned char *buf,
    int len,unsigned char **cp1, 
    unsigned char **cp2,
    time_t now)
{
  static unsigned char buf1[1000],buf2[100];
  char mic1[200],mic2[200];
  int l1,l2;
  char from[10],to[10],digis[100];
  int i,hadlast,l;
  char tmp[15];

  *buf1 = *buf2 = '\0';
  *cp1 = buf1;
  *cp2 = buf2;
  if ((buf[0] & 0xf) != 0)	/* not a kiss data frame */
    return;
  ++buf;
  --len;
  if (len < (AXLEN+ALEN+1))	/* too short */
    return;

  pax25(to,buf);		/* to call */
  pax25(from,&buf[AXLEN]);	/* from call */

  buf+=AXLEN;			/* skip over the from call now... */
  len-=AXLEN;
  *digis = '\0';
  if (!(buf[ALEN] & HDLCAEB)) {	/* is there a digipeater path? */
    for (l=0, buf+=AXLEN, len-=AXLEN, i = 0;
	 i < 6 && len >= 0;
	 i++, len-=AXLEN, buf+=AXLEN) {
      int nextrept = buf[AXLEN+ALEN]&REPEATED;
      if (buf[ALEN]&HDLCAEB)
	nextrept = 0;
      pax25(tmp,buf);
      
      if (*tmp) {
	sprintf(&digis[l],",%s%s",tmp,(buf[ALEN]&REPEATED&&!nextrept)?"*":"");
	++hadlast;
	l += strlen(&digis[l]);
      }
      if (buf[ALEN] & HDLCAEB)
	break;
    }
  }
  buf += AXLEN;
  len -= AXLEN;
  if (len <= 0)
    return;			/* no data after callsigns */
  if (*buf++ == UI && *buf++ == PID_NO_L3) {
    len -= 2;
  } else {
    return;			/* must have UI text to be useful */
  }
  /* now check to see if it's a mic-e frame & rewrite it */
  if (fmt_mic_e(to,buf,len,mic1,&l1,mic2,&l2,now)) {
    /* it was a MIC-E */
    mic1[l1] = mic2[l2] = '\0';
    if (l1)
      sprintf(buf1,"%s>%s%s:%s",from,"APRS",digis,mic1);
    if (l2)
      sprintf(buf2,"%s>%s%s:%s",from,"APRS",digis,mic2);
  } else if (fmt_x1j4(to,buf,len,mic1,&l1,mic2,&l2,now)) {
    /* it was an X1J4 digi */
    mic1[l1] = mic2[l2] = '\0';
    if (l1)
      sprintf(buf1,"%s>%s%s:%s",from,to,digis,mic1);
  } else {
    /* not mic-e/x1j4, so just print the packet without editing */
    sprintf(buf1,"%s>%s%s:",from,to,digis);
    l = strlen(buf1);
    for (i = 0; i < len; i++,l++) {
      buf1[l] = (raw||isprint(buf[i]))?buf[i]:' '; /* keep it clean */
    }
    buf1[l] = '\0';
  }
  return;
}

char *pax25(char *buf, const unsigned char *data)
{
  int i, ssid;
  char *s;
  char c;
  
  s = buf;
  
  for (i = 0; i < ALEN; i++) {
    c = (data[i] >> 1) & 0x7F;
    
    if (!isalnum(c) && c != ' ') {
      strcpy(buf, "[invalid]");
      return buf;
    }
    
    if (c != ' ') *s++ = c;
  }	
  
  if ((ssid = (data[ALEN] & SSID)>>1) != 0)
    sprintf(s, "-%d", ssid);
  else
    *s = '\0';
  
  return(buf);
}

void
cleanup(sig)
int sig;
{
#ifdef USE_SHM
  shm_cleanup(sig);		/* clean up shared memory, etc. */
#endif
  exit(0);
}
