/* test parse_cooked_ax25 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <linux/ax25.h>
#include <netdb.h>
#include "libax25ext.h"
static void print_it(FILE *f,
	 struct ax_calls *calls,
	 u_char *data,
	      int len);
static void drop_unused_digis(struct ax_calls *);

main()
{
  unsigned char buf[2048],*bp;
  int buflen, *lp = &buflen;

  while (fgets(buf,sizeof(buf),stdin)) {
    unsigned char obuf[2048], *op;
    int r,olen = sizeof(obuf), *olp = &olen;
    struct ax_calls calls;

    bp = buf;
    buflen = strlen(buf);
    if (buf[buflen-1] == '\n')
      buf[--buflen] = '\0';
    op = obuf;
    olen = sizeof(obuf);
    bzero(obuf,sizeof(obuf));
    fprintf(stderr,"================================\ninput: %s\n",buf);
    switch(r=parse_cooked_ax25(&bp,lp,&calls)) {
    case PK_VALID:
    case PK_VALDIGI:
      fprintf(stderr,"valid%s packet.  n_digis: %d next_digi: %d  type: %d  pid: %d\n",
	      (r==PK_VALDIGI)?" repeatable":"",
	      calls.ax_n_digis, calls.ax_next_digi, calls.ax_type, calls.ax_pid);
      fprintf(stderr,"data @0x%0x (len %d): %s\n",bp,buflen,bp);
      fprintf(stderr,"---- print_it ----\n");
      print_it(stderr,&calls,bp,buflen);
      fprintf(stderr,"---- gen_cooked thing: ----\n");
      fprintf(stderr,"gen_cooked rc: %d\n",gen_cooked_ax25(&op,olp,&calls));
      fprintf(stderr,"cooked@0x%0x (len %d): %s\n",obuf,olen,obuf);/* ?? */
      fprintf(stderr,"data @0x%0x (len %d): %s\n", bp, buflen, bp);
      strncpy(&obuf[sizeof(obuf)-olen],bp,buflen);
      printf("%s\n",obuf);
      fprintf(stderr,"after dropping unused digis:\n");
      drop_unused_digis(&calls);
      fprintf(stderr,"  n_digis: %d next_digi: %d  type: %d  pid: %d\n",
	      calls.ax_n_digis, calls.ax_next_digi, calls.ax_type, calls.ax_pid);
      print_it(stderr,&calls,bp,buflen);
      break;
    case PK_INVALID:
      fprintf(stderr,"failed to parse\n");
      break;
    default:
      fprintf(stderr,"bogus return value\n");
      break;
    }
  }
}

/* some defines that really belong in a header file! */
#define	ALEN		6	/* index to where the SSID and flags go */
#define	AXLEN		7	/* length of a callsign */
/* SSID mask and various flags that are stuffed into the SSID byte */
#define	SSID		0x1E
#define	HDLCAEB		0x01
#define	REPEATED	0x80

#define	UI		0x03	/* unnumbered information (unproto) */
#define	PID_NO_L3	0xF0	/* no level-3 (text) */
#define	C		0x80
#define	SSSID_SPARE	0x40	/* must be set if not used */
#define	ESSID_SPARE	0x20

static void
print_it(FILE *f,
	 struct ax_calls *calls,
	 u_char *data,
	 int len)
{
  int j;
  char asc_from[12],asc_to[12];

  if (f == NULL)
    return;
  fprintf(stderr,"print_it: n_digis %d next_digi %d\n",
	  calls->ax_n_digis,calls->ax_next_digi);
  strncpy(asc_to,ax25_ntoa_pretty(&calls->ax_to_call),sizeof(asc_to));
  strncpy(asc_from,ax25_ntoa_pretty(&calls->ax_from_call),sizeof(asc_from));
  fprintf(f,"%s>%s",asc_from,asc_to);
  for (j = 0; j < calls->ax_n_digis; j++) {
    fprintf(stderr,"\nprint_it: digi[%d]: %s %s\n",j,
	    (calls->ax_digi_call[j].ax25_call[ALEN]&REPEATED)?"REPEATED":"",
	    (calls->ax_digi_call[j].ax25_call[ALEN]&HDLCAEB)?"HDLCAEB":"");
    fprintf(f,",%s%s",ax25_ntoa_pretty(&calls->ax_digi_call[j]),
	    (calls->ax_digi_call[j].ax25_call[ALEN]&REPEATED
		&& (j == calls->ax_next_digi-1))?"*":"");
  }
  fprintf(f,":%.*s\n",len,data);
}

static void 
drop_unused_digis(struct ax_calls *calls) 
{
  if (!calls->ax_n_digis || calls->ax_next_digi >= calls->ax_n_digis)
    return;			/* all digis are used up */
  calls->ax_n_digis = calls->ax_next_digi; /* truncate the list */
  calls->ax_digi_call[calls->ax_next_digi-1].ax25_call[ALEN] |= HDLCAEB;
}
