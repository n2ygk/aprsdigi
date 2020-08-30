/* aprsdigi: APRS-style UI frame digipeater and node.
 *
 *  APRS has some peculiar digipeating ideas, so let's test them here:
 *
 *  1. Several digipeater aliases such as RELAY, WIDE, GATE.
 *  2. WIDEn-n flooding algorithm.
 *  3. SSID-based shorthand for digi path.
 *  
 *  See Bob Bruninga's (WB4APR) README/MIC-E.TXT for a description of
 *  methods 2 and 3.  #1 is conventional TNC digipeating with MYAlias, etc.
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
 * portions derived from ax25-utils/listen.c
 *
 */
static char copyr[] = "Copyright (c) 1996,1997,1999,2001,2002,2003,2004,2009,2012 Alan Crosswell, n2ygk@weca.org";
/*
 * Alan Crosswell, N2YGK
 * 144 Washburn Road
 * Briarcliff Manor, NY 10510, USA
 * n2ygk@weca.org
 *
 * TODO:
 *  see ./TODO
 */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
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
#ifndef AX25_MTU
#define AX25_MTU 256
#endif
#include <netdb.h>

#include "mic_e.h"
#include "libax25ext.h"
#include "netax25/axconfig.h"

#ifndef PACKAGE
#define PACKAGE "aprsdigi"
#endif
#ifndef VERSION
#define VERSION "$Revision$";
#endif

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


struct statistics {		/* counters */
  int rx;			/* packets received */
  int rx_ign;			/*  rx packets ignored */
  int rx_dup;			/*  dupe packets killed */
  int rx_loop;			/*  looping packets killed */
  int rx_mic;			/*  mic_e packets received */
  int rx_ssid;			/*  dest SSID packets received */
  int tx;			/* packets transmitted, sum of: */
  int digi;			/*  regular digipeats */
  int flood;			/*  flood-N digipeats */
  int ssid;			/*  dest SSID digipeats */
  int ids;			/*  id packets */
};

/* the per-interface information */
#define MAXALIASES 5
#define MAXINTF 10

struct budlist_entry {		/* budlist entry */
  struct budlist_entry *be_next;
  int be_permit;			/* permit/deny */
  struct sockaddr_storage be_addr, be_mask;
  int be_maskbits;
};

struct budlist {		/* a given budlist */
  struct budlist *bl_next;
  int bl_list_no;			/* list number */
  struct budlist_entry *bl_ent_head, *bl_ent_last;
};

static struct budlist *Bl_head = NULL, *Bl_last = NULL;

struct dupelist_entry {		/* dupelist entry */
  struct dupelist_entry *de_next;
  char *de_dev;			/* interface device name to dupe to */
};

struct dupelist {		/* a given dupelist */
  struct dupelist *dl_next;
  int dl_list_no;		/* list number */
  struct interface_list *dl_il;	/* list of interfaces to dupe to */
  struct dupelist_entry *dl_ent_head, *dl_ent_last;
};

static struct dupelist *Dl_head = NULL, *Dl_last=NULL;

static struct interface {
  char *port;			/* axports port name */
  char *dev;			/* kernel device name */
  char *devname;		/* user-friendly device name */
  char *tag;			/* optional tag text */
  int taglen;
  int idinterval;		/* seconds between IDs; 0 for none. */
  int i_flags;			/* status flags */
  enum {
    P_UDP=1,			/* interface is UDP socket */
    P_AX25=2,			/* interface is AX25 socket */
    P_UNIX=3,			/* interface is a unix file */
  } i_proto;
  ax25_address aliases[MAXALIASES]; /* intf call & aliases */
  int n_aliases;
  int rsock,tsock;		/* socket fds */
#ifdef IPV6
  struct sockaddr_storage rsa,tsa,rsafrom;	/* and their sockaddrs */
#else
  struct sockaddr rsa,tsa,rsafrom;	/* and their sockaddrs */
#endif /*IPV6*/
  int rsa_size,tsa_size;	/* sendto() bug w/sizeof(sockaddr_storage)? */
  time_t next_id;		/* next time we ID */
  u_char idbuf[AX25_MTU];	/* An ID packet for this intf */
  int idlen;
  struct statistics stats;	/* statistics */
  struct budlist *bud;		/* budlist */
  struct dupelist *dupe;	/* list of interfaces to dupe to */
} Intf[MAXINTF];
#define MYCALL(n) Intf[n].aliases[0]
#define I_MYCALL(i) i->aliases[0]
static int N_intf = 0;

struct interface_list {		/* a list of interfaces (duh) */
  struct interface_list *next;
  struct interface *i;
};

struct stuff {			/* maybe I should learn C++... */
  u_char *cp;			/* pointer into the packet */
  int len;			/* length of it */
  struct ax_calls in;		/* the received packet's calls, flags, etc. */
  struct ax_calls out;		/* the transmitted packet's calls, etc. */
  struct interface *i;		/* the interface received on */
}; 

/* some global stuff */

/* General options: */
static int Verbose = 0;
static int Testing = 0;
static int Maxhops = 0;	/* limit direct input packets to this many digipeats */
static int Digi_SSID = 0;
static int Kill_dupes = 0;	/* kill dupes even in conventional mode */
static int Kill_loops = 0;	/* kill loops */
static int Doing_dupes = 0;	/* dupelist was set on some interface */
static char *Logfile = NULL;
static ax25_address Aprscall;	/* replace mic-e encoded to-call with this */
static ax25_address Trace_dummy; /* dummy call for tracen-n substitution */
static ax25_address Widecall;	/* dummy call for WIDEn-n substitution */
static ax25_address Udpipcall;	/* dummy call for UDPIP 3rd party */
static int Have_digipath = 0;
#define DIGISIZE 7		/* one more than usual! */
static ax25_address Digipath[DIGISIZE]; /* for SSID-[1:7] w/o flooding */
static struct full_sockaddr_ax25 Path[4]; /* for SSID-[8:15] N S E W */
static char Dirs[5] = "NSEW";

/* per-interface default options: */
static char *Tag = NULL;	/* tag onto end of rx'd posit */
static int Taglen = 0;
static int Idinterval = (9*60)+30; /* default to 9:30 */
static int Keep = 28;		/* seconds to remember for dupe test */

static int I_flags = 0;		/* interface default flags */
#define I_NEED_ID    0x01	/* need to ID */
#define SUBST_MYCALL 0x02	/* replace digi alias w/mycall */
#define MICE_XLATE   0x04	/* translate mic-e to tracker format */
#define X1J4_XLATE   0x08	/* translate x1j4 to plain format */
#define I_COOKED     0x20	/* interface is cooked */
#define I_RX         0x40	/* interface is enabled to receive */
#define I_TX         0x80	/* interface is enabled to transmit */
#define I_TXSAME   0x0100	/* enable re-TX on received interface */
#define I_3RDP     0x0200	/* enable 3rd party tunneling */

/*
 * A table of recognized flooding alias prefixes.  In current practice, 
 * the only recognized flood aliases are WIDEn and TRACEn(where n is 0-7)
 * where the latter has truly odd behavior.  The flavors of WIDE and TRACE
 * are:
 *  1. WIDE:  A conventional alias, eligible for MYCALL substitution.
 *     Roughly equivalent to WIDE0-0.
 *  2. WIDEn-n: Flooding algorithm.  Decrement SSID and repeat the packet
 *     without setting the DIGIPEATED flag until it reaches zero at which
 *     point do set the DIGIPEATED flag and move on to the next callsign
 *     in the digipeat list.  Does not get MYCALL substituted (even WIDEn-0)
 *     so the user can see that the packet came in via n (anonymous) WIDE
 *     digis.
 *  3. TRACE: Like WIDE but MYCALL substitution is not required.
 *  4. TRACEn-n: Do the SSID substitution like WIDE, but also insert MYCALL
 *     into the digi list:
 *	RELAY*,TRACE3-3
 *	RELAY,N2YGK-7*,TRACE3-2
 *	RELAY,N2YGK-7,WB2ZII*,TRACE3-1
 *	RELAY,N2YGK-7,WB2ZII,N2MH-15*,TRACE3*
 *     (What happens when the digi list gets too long for the AX.25 protocol
 *     appears not to have been considered in this design.  I guess we'll
 *     just insert as many as we can.)
 */

/*
 * These flags are shared by the calltab and floods tables and by 
 * the floodtype of a specific received call.  That is, while there is
 * only one WIDE calltab entry, a received callsign can be WIDE-n or
 * WIDEn-n.
 */
#define C_IS_NOFLOOD 0		/* not a flood */
#define C_IS_FLOOD 1		/* this callsign is a flood (WIDE) */
#define C_IS_FLOODN 2		/* this is an N-N type flood (WIDEn-n) */
#define C_IS_TRACE 4		/* this is a trace-type flood (TRACE*) */

struct flood {
  ax25_address call;		/* the flooding callsign prefix */
  int len;			/* length */
  int flags;
} Floods[MAXALIASES];
static int N_floods = 0;
#define FLOODCALL Floods[0].call
#define FLOODLEN Floods[0].len

/* forward declarations */
static int unproto(struct full_sockaddr_ax25 *, char *);
static int parsecalls(ax25_address*, int, char *);
static void print_it(FILE *,struct ax_calls *,unsigned char *,int len);
static void add_text(u_char **,int *,u_char *,int,char *,int);
static int dupe_packet(struct ax_calls *,u_char *,int);
static int loop_packet(struct ax_calls *,u_char *,int);
static int xmit(struct stuff *s, struct interface_list *list);
static void set_id(void);
static void rx_packet(struct interface *i, u_char *buffer, int len);
static void fix_recvfrom(struct stuff *s);
static int floodcmp(ax25_address *a, int len, ax25_address *b);
static int floodtype(ax25_address *c, int i);
static void sked_id(struct interface *iface);
static void budlist_add(char *, int);
static int budlist_permit(struct stuff *);
static void budlist_print_all(FILE *f);
static void budlist_print(FILE *f,struct budlist *);
static void dupelist_add(char *);
static void dupelist_print_all(FILE *f);
static void dupelist_print(FILE *f,struct dupelist *);
static void dupelist_init(void);
static void printaddr(struct sockaddr_storage *s, char *buf, int buflen);
static void setmask(int, u_char *);
static int cmpmask(int, u_char *, u_char *, u_char *);
static void die(char *);
static void usage(void);
static void usage_udp(void);
static void usage_unix(void);
static void usage_ax25(void);
static void usage_budlist(void);
static void check_config();
static void config_print(FILE *f);
static void print_dupes(void);
static struct interface_list *intf_of(ax25_address *callsign);
static void to_3rdparty(struct ax_calls *calls);
static void drop_unused_digis(struct ax_calls *calls);
static void from_3rdparty(struct stuff *s,
	      int npkts,	/* dimension of the next two vectors */
	      u_char *pkt[],	/* output buffers for source path header */
	      int pktl[]);	/* output length of sph (AX25_MTU max) */
/* signal handlers */
static void cleanup(int),identify(int),identify_final(int),
  print_stats(int),reset_stats(int);

static void do_opts(int argc, char **argv);
static void do_ports(void);
static void do_port_ax25(struct interface *);
static void do_port_udp(struct interface *);
static void do_port_unix(struct interface *);
static void set_sig(void);
static int rx_loop(void);

int
main(int argc, char **argv)
{
  int r;

  bzero(Intf,sizeof(Intf));
  do_opts(argc,argv);
  do_ports();
  set_id();
  set_sig();
  check_config();
  r = rx_loop();
  exit(r);
}

/* Listen for received packets and farm out the work */
static int
rx_loop()
{
  unsigned char buffer[AX25_MTU];
  int n,selfds = 0;
  struct interface *i;
  fd_set select_mask;

  /* set up the initial select mask */
  FD_ZERO(&select_mask);
  for (n  = 0, i = Intf; n < N_intf; n++, i++) {
    if (i->rsock >= 0 && (i->i_flags & I_RX)) {
      FD_SET(i->rsock, &select_mask);
      selfds = (i->rsock>selfds)?i->rsock:selfds;
    }
  }
  ++selfds;
  for (;;) {
    int len;
    fd_set rmask = select_mask;

    if (select(selfds,&rmask,NULL,NULL,NULL) < 0) {
      if (errno == EINTR)
	continue;
      perror("select");
      return 1;
    }

    /* find which sockets have data */
    for (n = 0, i = Intf; n < N_intf; n++, i++) {
      if (i->rsock >= 0 && FD_ISSET(i->rsock,&rmask)) {
	int size = sizeof(i->rsafrom);
	if ((len = recvfrom(i->rsock,buffer,sizeof(buffer),0,(struct sockaddr *)&i->rsafrom,&size)) < 0) {
	  if (errno == EINTR)
	    continue;
	  perror(i->port);
	  return 1;
	}
	rx_packet(i,buffer,len); /* process the received packet */
      }
    }
  } /* end of for(;;) */
  return 0;			/* NOTREACHED */
}

/* Parse a received packet, convert and digipeat if necessary. */

static void rx_dupeit(struct stuff *s);
static int rx_nodigi(struct stuff *s,int r);
static int rx_dupe(struct stuff *s);
static int rx_to_me(struct stuff *s);
static int rx_bud_deny(struct stuff *s);
static int rx_from_me(struct stuff *s);
static int rx_ssid(struct stuff *s);
static int rx_limit(struct stuff *s);
static int rx_flood(struct stuff *s);
static int rx_digi(struct stuff *s);

static void
rx_packet(struct interface *i,	/* which interface received on */
	  u_char *buffer,	/* what was received */
	  int len)		/* length received */
{
  int r;
  struct stuff s;

  bzero(&s,sizeof(s));
  s.i = i;
  ++s.i->stats.rx;
  s.cp = buffer;
  s.len = len;


  /* parse the cooked or raw kiss frame */
  if (i->i_flags&I_COOKED)
    r = parse_cooked_ax25(&s.cp,&s.len,&s.in);
  else
    r = parse_raw_ax25(&s.cp,&s.len,&s.in);
  
  fix_recvfrom(&s);		/* workaround bug in AF_AX25 */

  if (Verbose) {
    char buf[20];
    time_t tick = time(0);
    strftime(buf,sizeof(buf),"%T",gmtime(&tick));
    fprintf(stderr,"%s %s: RX: ",buf,s.i->port);
    print_it(stderr,&s.in,s.cp,s.len);
  }

  /* go through a bunch of choices until we eat the packet or die:-) */

  if (Doing_dupes)
    rx_dupeit(&s);		/* blindly dupe it */
  if (rx_bud_deny(&s))		/* budlisted */
    return;
  else if (rx_from_me(&s))      /* don't digi my own repeated beacons! */
    return;
  else if (rx_ssid(&s))		/* dest SSID handling */
    return;
  else if (rx_nodigi(&s,r))	/* Nothing to digi? */
    return;
  else if (rx_dupe(&s))		/* Is it a killed dupe or loop? */
    return;
  else if (rx_to_me(&s))	/* Addressed to me? */
    return;
  else if (rx_limit(&s))	/* Will this packet exceed the digipeat limit */
    return;
  else if (rx_flood(&s))	/* flood special handling */
    return;
  else if (rx_digi(&s))		/* conventional digi */
    return;

  /* How'd we get here? */
  if (Testing) {
    fprintf(stderr,"%s: Not repeatable\n",s.i->port);
  }
}	  

/* XXX bug(?) in AF_AX25 for raw sockets puts the interface name in
   sax25_call instead of the sender's callsign:
   (gdb) p (struct sockaddr_ax25)i->rsafrom
   $3 = {sax25_family = 3, sax25_call = {ax25_call = "ax0\0\0\0"}, 
   sax25_ndigis = 0}
*/
static void
fix_recvfrom(struct stuff *s)
{
  struct sockaddr_ax25 *sax25 = (struct sockaddr_ax25 *)&s->i->rsafrom;

  if (sax25->sax25_family == AF_AX25) {
    bcopy(&s->in.ax_from_call,
	  sax25->sax25_call.ax25_call,sizeof(sax25->sax25_call.ax25_call));
  }
}

static void
rx_dupeit(struct stuff *s)
{
  int n;

  if (s->i->dupe && s->i->dupe->dl_il) {
    if (Verbose) {
      fprintf(stderr,"%s: duping to dupelist %d\n",s->i->devname,
	      s->i->dupe->dl_list_no);
    }
    s->out = s->in;
    if (xmit(s,s->i->dupe->dl_il) < 0) 
      perror("xmit");
    }
}

static int
rx_nodigi(struct stuff *s,int r)
{
  int result = 0;
  /* bail if invalid or not doing SSID & no unrepeated digis */
  if (r == PK_INVALID || r == PK_VALID || (r != PK_VALDIGI && !Digi_SSID)) {
    ++s->i->stats.rx_ign;
    result = 1;
  }
  if (Verbose) {
    if (result)
      fprintf(stderr,"packet is %s. (r=%d)\n",
	      (r == PK_INVALID)?"invalid":"not repeatable",r);
    else
      fprintf(stderr,"packet is repeatable. (r=%d)\n",r);
  }
  return result;
}

static int
rx_dupe(struct stuff *s)
{
  int result = 0;
  static char *lupedupe[] = {"dupe or loop","dupe","loop"};

  /* If packet was last transmitted by me, then don't digipeat it! */
  if (Kill_dupes && dupe_packet(&s->in,s->cp,s->len)) {
    ++s->i->stats.rx_dup;
    result = 1;
  } else if (Kill_loops && loop_packet(&s->in,s->cp,s->len)) {
    ++s->i->stats.rx_loop;
    result = 2;
  }
  if (Verbose) {
    fprintf(stderr,"packet is%sa %s\n",result?" ":" not ",lupedupe[result]);
  }
  return result;
}  


/*
 * find interfaces belonging to a callsign or alias.  Returns a pointer to
 * a list of interfaces or NULL if not found.
 */
static struct callsign_list {
  ax25_address *callsign;
  int flags;
  int floodlen;			/* length of flood call */
  struct interface_list *l;	/* list of interfaces having this callsign */
  struct interface_list *illast;
  struct callsign_list *next;
} *calltab = NULL, *ctlast = NULL;

static void calltab_init(void);

static struct callsign_list *
calltab_entry(ax25_address *callsign,int *flags)
{
  struct callsign_list *c;
  int flagdummy;

  if (flags == NULL)
    flags = &flagdummy;		/* dummy the flags if not needed */
  *flags = 0;
  for (c = calltab; c; c = c->next) {
    if ((c->flags&C_IS_FLOOD 
	 && (*flags = floodcmp(c->callsign,c->floodlen,callsign)))
	|| (ax25_cmp(c->callsign,callsign) == 0)) {
      *flags |= c->flags;	/* add stuff floodcmp doesn't know about */
      return c;
    }
  }
  return NULL;
}

static void 
calltab_init(void)
{
  struct interface *i;
  int n,m;
  
  /* iterate over all interfaces' callsigns */
  for (i = Intf, n = 0; n < N_intf; n++,i++) {
    for (m = 0; m < i->n_aliases; m++) {
      struct interface_list *new_il = 
	(struct interface_list *)calloc(1,sizeof(struct interface_list));
      struct callsign_list *c = (calltab)?calltab_entry(&i->aliases[m],0):NULL;

      if (!c) {			/* first time seeing this call */
	int f;
	c = (struct callsign_list *)calloc(1,sizeof(struct callsign_list));
	c->callsign = &i->aliases[m];
	/* see if this is a flood call */
	for (f = 0; f < N_floods; f++) {
	  if (floodcmp(&Floods[f].call,Floods[f].len,c->callsign)) {
	    c->flags =  Floods[f].flags;
	    c->floodlen = Floods[f].len;
	    break;
	  }
	}
	/* add the new callsign_list to calltab */
	if (ctlast) {
	  ctlast->next = c;
	  ctlast = c;
	} else {
	  calltab = ctlast = c;	/* very first entry */
	}
      }
      new_il->i = i;		/* initialize the interface list */
      if (c->illast) {		/* and link it in to the callsign's list */
	c->illast->next = new_il;
	c->illast = new_il;
      } else {
	c->l = c->illast = new_il;
      }
    }
  }
}

static struct interface_list *
intf_of(ax25_address *callsign)
{
  struct callsign_list *c;

  if (c = calltab_entry(callsign,0))
    return c->l;
  return NULL;
}

/* Does a packet appear to come directly to us from the rf originator */
static int
is_direct(struct stuff *s)
{
  struct callsign_list *c;
  int flag=0, digit, n1, n2;
  if (s->in.ax_next_digi != 0) return 0;
  c = calltab_entry(&s->in.ax_digi_call[0],&flag);
  if (!c) 	/* Nothing we recognize or will deal with, but prob. direct */
     return 1;
  if ((!flag&C_IS_FLOOD) || (!flag&C_IS_FLOODN)) /* alias, yes we are direct */
     return 1;
  digit = ((s->in.ax_digi_call[0].ax25_call[c->floodlen]) & 0xFE)>>1;
  if ((digit<'0') || digit > '9') return 0; /* Something is very wrong */
  n1 = digit - 0x30;
  n2 = (s->in.ax_digi_call[0].ax25_call[ALEN]&SSID)>>1;
  if (n2<n1) 	/* There's probably been a hop already */
     return 0;

  return 1;
}

static int
rx_to_me(struct stuff *s)
{
  int result = 0;
  struct interface_list *l = intf_of(&s->in.ax_to_call);

  if (l) {
    result = 1;
    ++s->i->stats.rx_ign;
  }
  if (Verbose) {
    fprintf(stderr,"packet is%saddressed to me.\n",result?" ":" not ");
  }
  return result;
}  

static int
rx_bud_deny(struct stuff *s)
{
  int permit = 0;

  permit = budlist_permit(s);
  if (Verbose) {
    fprintf(stderr,"packet is %s by budlist.\n",permit?"permitted":"denied");
  }
  return !permit;		/* true if denied */
}  


static int
rx_from_me(struct stuff *s)
{
  int n,result = 0;
  struct interface *iface;

  for (n = 0, iface = Intf; n < N_intf; n++, iface++) {
    if (ax25_cmp(&MYCALL(n),&s->in.ax_from_call) == 0) {
      result=1;
      ++s->i->stats.rx_ign;
    }
  }

  if (Verbose) {
    fprintf(stderr,"packet is%sfrom me.\n",result?" ":" not ");
  }
  return result;
}  

/* Does a direct inbound packet request more hops than the limit we set */
static int
rx_limit(struct stuff *s)
{
  int sum = 0, result = 0, flag=0;
  struct callsign_list *c;
  if (!Maxhops) return result;
  if (!(is_direct(s))) return result;
  c = calltab_entry(&s->in.ax_digi_call[0],&flag);
  if (!c) return result;
  if (Verbose) {
    fprintf(stderr, "Direct input packet with known via call\n");
  }

  for (int n=0 ; n < s->in.ax_n_digis; n++) {
    flag = 0;
    c = calltab_entry(&s->in.ax_digi_call[n],&flag);
    if (!c) {
        sum++;
	if (Verbose) {
	   fprintf(stderr, "Call, non flood running total %i\n", ax25_ntoa_pretty(&s->in.ax_digi_call[n]), sum);
	}
        continue;
    }
    if ((flag&C_IS_FLOOD) && (flag&C_IS_FLOODN)) {
        sum += (s->in.ax_digi_call[n].ax25_call[ALEN]&SSID)>>1;
	if (Verbose) {
	   fprintf(stderr, "Call %s, flag %i, flood running total %i\n", ax25_ntoa_pretty(&s->in.ax_digi_call[n]), flag, sum);
	}
    } else {
	sum++;
	if (Verbose) {
	   fprintf(stderr, "Call %s, flag %i, non floodn running total %i\n", ax25_ntoa_pretty(&s->in.ax_digi_call[n]), flag, sum);
	}
    }
  }

  if (Verbose) {
    fprintf(stderr,"Total hops expected for pkt being received directly %i limit %i\n",
       sum, Maxhops);
  }
  if (sum > Maxhops) {
     ++s->i->stats.rx_ign;
     result = 1;
  }

  if (Verbose) {
    fprintf(stderr,"Direct input packet%sdropped for too many digipeats requested\n",
	    result?" ":" not ");
  }

  return result;
}

/* 
 * SSID path selection only applies if:
 *  1. SSID digipeating is enabled.
 *  2. To-call's SSID must be non zero: this is the route.
 *  3. Digipeater path must be empty.
 * Count up mic_e packets while we are here.
 */
static int
rx_ssid(struct stuff *s)
{
  int ssid, mic_e, result = 0;

  mic_e = (*(s->cp) == 0x60 || *(s->cp) == 0x27
	   || *(s->cp) == 0x1c || *(s->cp) == 0x1d);
  if (mic_e)
    ++s->i->stats.rx_mic;
  if (Verbose) {
    fprintf(stderr,"%s mic_e...\n",mic_e?"is":"is not");
  }  
  if (Digi_SSID && s->in.ax_n_digis == 0 
      && (ssid = (s->in.ax_to_call.ax25_call[ALEN]&SSID)>>1)
      && s->len > 0) {
    if (Verbose) {
      fprintf(stderr,"Got an SSID route for path %d.\n",ssid);
    }
    ++s->i->stats.rx_ssid;
    s->out.ax_from_call = s->in.ax_from_call;
    s->out.ax_to_call = s->in.ax_to_call;
    s->out.ax_to_call.ax25_call[ALEN]&=~SSID; /* zero the SSID */
    s->out.ax_type = s->in.ax_type;
    s->out.ax_pid = s->in.ax_pid;
    if (ssid <= 7) {	/* omnidirectional */
      if (N_floods || ssid == 0) { /* in a flooding network? */
	s->out.ax_n_digis = 1;
	s->out.ax_digi_call[0] = FLOODCALL;
	s->out.ax_digi_call[0].ax25_call[FLOODLEN] = (ssid+'0') << 1;
	s->out.ax_digi_call[0].ax25_call[ALEN] |= SSID&(ssid<<1);
	if (Verbose) {
	  fprintf(stderr,"Flooding: setting path to %s\n",
		  ax25_ntoa_pretty(&s->out.ax_digi_call[0]));
	}
      } else {		/* not in a flooding network */
	int startat,i;
	if (ssid < 4) {		/* RTFM for why this is */
	  startat = 0;		/* starting point in digipath */
	  s->out.ax_n_digis = ssid; /* number of digis from there. */
	} else {
	  startat = 3;
	  s->out.ax_n_digis = ssid-3;
	}
	if (Verbose) {
	  fprintf(stderr,"Non-flooding: converting SSID WIDE-%d path to DIGI[%d:%d]\n",
		  ssid,startat,startat+s->out.ax_n_digis-1);
	}
	/* fill in the digipeater list */
	for (i = 0; i < s->out.ax_n_digis; i++,startat++) {
	  s->out.ax_digi_call[i] = Digipath[startat];
	}
      } /* flooding/non-flooding network */
    } else {			/* SSID > 7 is directional */
      int j;
      if (Verbose) {
	fprintf(stderr,"setting path to %c UNPROTO%s\n",
		Dirs[ssid&3],(ssid&4)?" + WIDE":"");
      }
      s->out.ax_n_digis = Path[ssid&3].fsa_ax25.sax25_ndigis;
      for (j = 0; j <= s->out.ax_n_digis; j++)
	s->out.ax_digi_call[j] = Path[ssid&3].fsa_digipeater[j];
      if (ssid&4) {	/* directional + wide call */
	s->out.ax_digi_call[s->out.ax_n_digis++] = Widecall;
      }
    }
    ++s->i->stats.ssid;
    if (xmit(s,NULL) < 0)
      perror("xmit");
    result = 1;
  }
  if (Verbose) {
    fprintf(stderr,"did%srequire destination SSID handling.\n",
	    result?" ":" not ");
  }
  return result;
}  

/* see if special WIDEn-n & TRACEn-n handling applies. */
static int
rx_flood(struct stuff *s)
{
  int i,wide,thisflags,result=0;
  struct callsign_list *c;

  /* FLOODn-n algorithm: next digi with non-zero ssid callsign */
  c = calltab_entry(&s->in.ax_digi_call[s->in.ax_next_digi],&thisflags);
  if (c && thisflags&C_IS_FLOOD
      && (wide = (s->in.ax_digi_call[s->in.ax_next_digi].ax25_call[ALEN]&SSID)>>1)) {

    if (Verbose) {
      fprintf(stderr,"Got a flooding %s route.\n",
	      ax25_ntoa_pretty(&s->in.ax_digi_call[s->in.ax_next_digi]));
    }

    /* flooding algorithm always kills dupes.  If kill_dupes
       option was not selected, then do the dupe checking here */
    if (!Kill_dupes && dupe_packet(&s->in,s->cp,s->len)) {
      if (Verbose) {
	fprintf(stderr,"flood packet is a dupe\n");
      }
      return 1;
    }
    /* decrement the flood counter (SSID) */
    s->out = s->in;		/* copy the input header */
    s->out.ax_digi_call[s->out.ax_next_digi].ax25_call[ALEN] &= ~SSID;
    s->out.ax_digi_call[s->out.ax_next_digi].ax25_call[ALEN] |= ((--wide) << 1)&SSID;
    /* Handle FLOOD-1 -> FLOOD-0 case */
    if (wide == 0)
      s->out.ax_digi_call[s->out.ax_next_digi].ax25_call[ALEN] |= REPEATED;
    /* TRACEn-n: insert dummy mycall in front: xmit will put the real one in */
    if ((thisflags&(C_IS_FLOODN|C_IS_TRACE)) == (C_IS_FLOODN|C_IS_TRACE)) {
      int n;
      if (s->out.ax_n_digis >= AX25_MAX_DIGIS) {
	fprintf(stderr,"%s: TRACEn-n list overflow. Last digi dropped.\n",
		s->i->port);
	s->out.ax_n_digis--;
      }
      /* shift remaining digis right one slot to make room */
      for (n = s->out.ax_n_digis; n >= s->out.ax_next_digi; n--) {
	s->out.ax_digi_call[n] = s->out.ax_digi_call[n-1];
      }
      /* then stuff in a dummy "TRACE" and mark it repeated */
      s->out.ax_digi_call[s->out.ax_next_digi] = Trace_dummy;
      s->out.ax_digi_call[s->out.ax_next_digi].ax25_call[ALEN] |= REPEATED;
      s->out.ax_n_digis++;
    }
    if (Verbose) {
      fprintf(stderr,"Rewriting it as %s:\n",
	      ax25_ntoa_pretty(&s->out.ax_digi_call[s->out.ax_next_digi]));
    }
    ++s->i->stats.flood;
    if (xmit(s,NULL) < 0) 
      perror("xmit");
    result = 1;
  }
  if (Verbose) {
    fprintf(stderr,"Did %s require special flooding handling.\n",
	    result?" ":" not ");
  }
  return result;
}

/* see if conventional digipeat handling applies. */
static int
rx_digi(struct stuff *s)
{
  int j, result = 0;
  struct interface_list *l;

  /* conventional: see if the next digipeater matches one of my calls */
  if (l = intf_of(&s->in.ax_digi_call[s->in.ax_next_digi])) {
    /* a packet for me to digipeat */
    if (Verbose) {
      fprintf(stderr,"Got a conventional digipeat.\n");
    }
      s->out = s->in;	/* copy input list to output unmodifed */
      s->out.ax_digi_call[s->out.ax_next_digi].ax25_call[ALEN] |= REPEATED;
      ++s->i->stats.digi;
      if (xmit(s,NULL) < 0) 
	perror("xmit");
      result  = 1;
  }
  if (Verbose) {
    fprintf(stderr,"Did%srequire conventional digipeat handling.\n",
	    result?" ":" not ");
  }
  return result;
}

static void
add_text(op,oleft,text,len,tag,taglen)
unsigned char **op;
int *oleft;
u_char *text;
int len;
char *tag;
int taglen;
{
  if ((*oleft -= len) > 0) {
    bcopy(text,*op,len);	/* copy the text */
    *op += len;
  }
  if (taglen && tag && (*oleft -= taglen) > 0) {
    bcopy(tag,*op,taglen); /* and tack on the tag */
    *op += taglen;
  }
}

/* watch out for overflow when adding mycall and/or wide */
static int
unproto(calls,str)		/* parse a via path into a calls struct */
struct full_sockaddr_ax25 *calls;
char *str;
{
  char buf[200];

  bzero(calls, sizeof(*calls));
  sprintf(buf,"dummy via %s",str);
  return ax25_aton(buf,calls);
}

static int
parsecalls(calls,ncalls,str)	/* parse a via path into a calls struct */
ax25_address *calls;
int ncalls;			/* max number */
char *str;
{
  char *cp;
  int i;

  bzero(calls,ncalls*sizeof(*calls));
  cp = strtok(str," \t\n,");
  for (i = 0; cp && i < ncalls; i++) {
    if (ax25_aton_entry(cp,calls[i].ax25_call) < 0)
      return -1;
    cp = strtok(NULL," \t\n,");
  }
  return i;
}

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
  strncpy(asc_to,ax25_ntoa_pretty(&calls->ax_to_call),sizeof(asc_to));
  strncpy(asc_from,ax25_ntoa_pretty(&calls->ax_from_call),sizeof(asc_from));
  fprintf(f,"%s>%s",asc_from,asc_to);
  for (j = 0; j < calls->ax_n_digis; j++) {
    fprintf(f,",%s%s",ax25_ntoa_pretty(&calls->ax_digi_call[j]),
	    (calls->ax_digi_call[j].ax25_call[ALEN]&REPEATED
		&& (j == calls->ax_next_digi-1))?"*":"");
  }
  fprintf(f,":%.*s\n",len,data);
}

/* 
 * packet reformatter
 *  depending on options flags on transmit interface, perform reformatting
 *  of the payload.  Current methods include mic_E expansion, X1J4
 *  stripping of the the "TheNet X1J4 (alias)" prefix. Future methods 
 *  to include GPS/APRS position compression, expansion, etc.
 */
static int
reformat(struct stuff *s,	/* "class data":-) */
	 struct interface *i,	/* this iteration's transmit interface */
	 u_char ***ovec,	/* returns ptr to array of u_char ptrs */
	 int **olen)		/* returns ptr to array of lengths */
{
  int r;
  time_t now;
  static u_char mic1[AX25_MTU], mic2[AX25_MTU];	/* MTU is *not* max payload! */
  static u_char pref1[AX25_MTU], pref2[AX25_MTU];
  static u_char *vecp[2];
  static int vecl[2];

  time(&now);

  *ovec = vecp;			/* returned ptrs */
  *olen = vecl;
  vecp[0] = mic1; vecl[0] = 0;
  vecp[1] = mic2; vecl[1] = 0;
  /* does xmit intf want mic-E translation? */
  if (i->i_flags&MICE_XLATE && 
      fmt_mic_e(ax25_ntoa_pretty(&s->out.ax_to_call),s->cp,s->len,
		mic1,&vecl[0],mic2,&vecl[1],now)) {
    s->out.ax_to_call = Aprscall; /* replace compressed lat w/"APRS" */
  } else if (i->i_flags&X1J4_XLATE &&   /* want x1j4 translation? */
	     fmt_x1j4(ax25_ntoa_pretty(&s->out.ax_to_call),s->cp,s->len,
		      mic1,&vecl[0],mic2,&vecl[1],now)) {
  } else {			/* no reformat; just pass thru */
    vecp[0] = s->cp;
    vecl[0] = s->len;
  }
  return (vecl[0]>0)+(vecl[1]>0);
}

/* 
 * xmit looks at the next digipeater call to see which interfaces the
 * packet has to go out.  By giving several interfaces the same callsign
 * or alias, multi-way fanout and/or gatewaying can be performed.
 *
 * An overriding interface list passed in is used by rx_dupeit().
 */
static int
xmit(struct stuff *s, struct interface_list *dupelist)
{
  struct callsign_list *c;
  struct interface_list *l;
  int r = 0, sent;
  int thisflags = 0;

  /* transmit the packet on the interface(s) of the next callsign */
  if (!dupelist) {
    if ((c = calltab_entry(&s->out.ax_digi_call[s->out.ax_next_digi],&thisflags)) == NULL) {
      fprintf(stderr,"xmit: Assertion failed: call %s not found\n",
	      ax25_ntoa_pretty(&s->out.ax_digi_call[s->out.ax_next_digi]));
      return -1;
    }
  }
  for(l = (dupelist)?dupelist:c->l; l; l = l->next) { /* loop over each tx interface */
    u_char obuf[AX25_MTU];
    u_char *op = obuf;
    int oleft = sizeof(obuf);
    int olen;
    int npkt,n;
    u_char **vecp;		/* output vector */
    int *vecl;			/* and lengths */

    if (!(l->i->i_flags & I_TX)) {
      if (Verbose)
	fprintf(stderr,"%s: interface will not xmit.\n",l->i->devname);
      continue;
    }
    if (!(l->i->i_flags & I_TXSAME) && l->i == s->i) {
      if (Verbose)
	fprintf(stderr,"%s: will not retransmit on same interface.\n",l->i->devname);
      continue;
    }

    /* do any required per-intf expansion/translation/compression of payload */
    npkt = reformat(s,l->i,&vecp,&vecl);

    if (s->i != l->i && l->i->i_flags&I_3RDP) {
      if (Verbose)
	fprintf(stderr,"3rd party de-tunnel: source port: %s  dest port: %s\n",
		s->i->port, l->i->port);
      from_3rdparty(s,npkt,vecp,vecl);
    }
          
    for (n = 0; n < npkt; n++) {
      struct ax_calls calls = s->out;
      /* 
       * Adjust the next_digi pointer only if REPEATED was set.
       * Only substitute MYCALL if marked REPEATED and the callsign
       * flags permit it.  Flags and their rules:
       * FLOOD  FLOODN  TRACE  Rule
       * -----  ------  -----  -----------------------------------
       *     0       X      X  only if SUBST_MYCALL substitute MYCALL
       *     1       0      X  always substitute MYCALL (WIDE/TRACE->MYCALL)
       *     1       1      0  never substitute MYCALL (WIDEn-n)
       *     1       1      1  always substitute MYCALL (???->MYCALL)
       * rx_flood will have inserted tracecall in front of TRACEn-n.
       * but MYCALL subst happens here since it is iface-specific.
       */
      if (!dupelist) {		/* if dupeing, ignore this crap */
	if (calls.ax_digi_call[calls.ax_next_digi].ax25_call[ALEN]&REPEATED) {
	  if ((!(thisflags&C_IS_FLOOD) && l->i->i_flags&SUBST_MYCALL)
	      || ((thisflags&C_IS_FLOOD) && !(thisflags&C_IS_FLOODN))
	      || ((thisflags&(C_IS_FLOOD|C_IS_FLOODN|C_IS_TRACE))
		  == (C_IS_FLOOD|C_IS_FLOODN|C_IS_TRACE))) {
	    calls.ax_digi_call[calls.ax_next_digi] = I_MYCALL(l->i);
	    calls.ax_digi_call[calls.ax_next_digi].ax25_call[ALEN] |= REPEATED;
	  }
	  ++calls.ax_next_digi;
	}
      }
      /* XXX - deal with detunneled 3rdparty (empty digi list) -- or is
	 some default digi list supposed to be used? */
      if (s->i != l->i && s->i->i_flags&I_3RDP) {
	if (Verbose)
	  fprintf(stderr,"3rd party tunnel: source port: %s  dest port: %s\n",
		  s->i->port, l->i->port);
	to_3rdparty(&calls);
      }

      if (l->i->i_flags&I_COOKED)
	gen_cooked_ax25(&op,&oleft,&calls); /* generate cooked header */
      else
	gen_raw_ax25(&op,&oleft,&calls); /* generate the raw kiss header */
      add_text(&op,&oleft,vecp[n],vecl[n],l->i->tag,l->i->taglen); /* fill in the info field */	

      olen = sizeof(obuf) - oleft;
      if (Logfile) {		/* XXX this gets duplicated crap for multiple intfs */
	FILE *of;
	if (Logfile[0] == '-' && Logfile[1] == '\0')
	  of = stdout;
	else
	  of = fopen(Logfile,"a");
	print_it(of,&calls,op-(vecl[n]+l->i->taglen),vecl[n]+l->i->taglen);
	if (of != stdout && of != stderr)
	  fclose(of);
      }
      if (Verbose) {
	char buf[20];
	time_t tick = time(0);
	strftime(buf,sizeof(buf),"%T",gmtime(&tick));
	fprintf(stderr,"%s %s: TX: ",buf,l->i->port);
	print_it(stderr,&calls,op-(vecl[n]+l->i->taglen),vecl[n]+l->i->taglen);
      }
      ++l->i->stats.tx;
      sked_id(l->i);
      if ((sent = sendto(l->i->tsock,obuf,olen,0,(struct sockaddr *)&l->i->tsa,
			 l->i->tsa_size)) < 0)
	perror(l->i->port);
      else
	r += sent;
    } /* end for n */
  } /* end for l  */
  return r;
}


/* 
 * packet dupe checking 
 *
 * Compare the to, from, and info.  Ignore the digipeater path.
 * If the packet matches one already received within the 'keep'
 * interval then it is a dupe.  Keep the list sorted in reverse
 * chronological order and throw out any stale packets.
 */
struct pkt {
  struct pkt *next,*prev;
  time_t t;			/* when recevied */
  ax25_address to;		/* destination */
  ax25_address fr;		/* source */
  int l;			/* length of text */
  u_char d[AX25_MTU];		/* the text */
};

static struct pkt *top;		/* stacked in reverse chronological order */

static int
dupe_packet(struct ax_calls *calls,u_char *cp,int len)
{
  struct pkt *p, *matched = NULL, *old = NULL;
  time_t now = time(0);
  time_t stale = now - Keep;

  if (Verbose) {
    print_dupes();
  }
  for (p = top; p; p = p->next) {
    if (p->t >= stale) {	/* packet is still fresh */
      if (p->l == len && bcmp(p->d,cp,len) == 0
	  && bcmp(&calls->ax_to_call,&p->to,sizeof(p->to)) == 0
	  && bcmp(&calls->ax_from_call,&p->fr,sizeof(p->fr)) == 0) {
	matched = p;
	break;
      } else
	continue;
    } else {			/* all following packets are stale */
      old = p;
      break;
    }
  }
  if (old) {			/* trim list of stale pkts */
    if (top == old)
      top = NULL;		/* entire list is stale */
    else {
      old->prev->next = NULL;
    }
    while (old) {
      p = old->next;
      free(old);
      old = p;
    }
  }
  if (matched) {		/* move matched packet to head of list? */
#ifdef DUPE_REFRESH_STAMP
    /* 
     * Should dupe-checking update the time stamp each time a dupe is
     * heard again or let it age out so that it can get digi'd
     * periodically?  For example, if a station beacons once every 15
     * seconds, then only the first packet received ever will be
     * digi'd if the "memory" is 28 sec.  Good example of where this
     * could be a problem is a MIC-E beaconing an emergency.  By not
     * refreshing the timestamp, the desired behavior of reducing channel
     * clutter is still achieved.
     */
    matched->t = now;		/* updated timestamp */
    if (matched == top)		/* already on top */
      return 1;
    /* unlink matched packet from list */
    if (matched->prev)
      matched->prev->next = matched->next;
    if (matched->next)
      matched->next->prev = matched->prev;
    matched->prev = matched->next = NULL;
    /* push matched packet on top of list */
    matched->next = top;
    if (top)
      top->prev = matched;
    top = matched;
#endif
    return 1;
  } else {			/* not matched: push new packet on top */
    if ((p = (struct pkt *)calloc(1,sizeof(struct pkt))) == NULL) {
      fprintf(stderr,"failed to calloc!\n");
      return 0;			
    }
    p->t = now;
    p->l = (len>AX25_MTU)?AX25_MTU:len;
    bcopy(cp,p->d,p->l);
    p->to = calls->ax_to_call;
    p->fr = calls->ax_from_call;
    /* push new packet on top of list */
    p->next = top;
    if (top)
      top->prev = p;
    top = p;
    return 0;
  }
}

static void
print_dupes(void) {
  struct pkt *p;

  for (p = top; p ; p = p->next) {
    fprintf(stderr,"dupe @ 0x%0x:  prev->0x%0x  next->0x%0x  time %d  len %d\n",
	    p,p->prev,p->next,p->t,p->l);
  }
}

/*
 * is packet looping?
 * e.g. FOO,mycall,WIDE*,mycall,somecall....
 * Only look for one of my already repeated real callsigns (e.g. MYCALL(n)).
 *
 * Search through the list of already-repeated digipeaters and compare
 * each to one of my interface callsigns (since these are the ones that
 * replace generic callsigns).
 */

static int
loop_packet(struct ax_calls *calls,u_char *cp,int len)
{
  int n, flags;
  int j;
  struct interface *iface;
  struct callsign_list *cl;

  for (n = 0; n < calls->ax_next_digi; n++) {
    if (calls->ax_digi_call[n].ax25_call[ALEN]&REPEATED) {
      for (j = 0, iface = Intf; j < N_intf; j++, iface++) {
	if (ax25_cmp(&MYCALL(j),&calls->ax_digi_call[n]) == 0) {
	  if (Verbose)
	    fprintf(stderr,"loop_packet: found my call %s at digi_call[%d]\n",
		    ax25_ntoa_pretty(&calls->ax_digi_call[n]),n);
	  return 1;
	}
      }
    } else
      return 0;			/* last repeated call */
  }
  return 0;
}

static void
cleanup(sig)
int sig;			/* dont't really care what it was */
{
  if (alarm(0) > 0)		/* an alarm was pending, so... */
    sked_id(NULL);		/*  ID one last time */
  identify_final(sig);
  print_stats(sig);		/* dump final stats */
  exit(0);			/* and exit */
}

static void
print_stats(sig)
int sig;
{
  FILE *of = stderr;
  struct interface *i;
  int n;

  if (Logfile) {
    if (Logfile[0] == '-' && Logfile[1] == '\0')
      of = stdout;
    else
      of = fopen(Logfile,"a");
  }
  if (of == NULL)
    return;
  for (i = Intf, n = 0; n < N_intf; n++,i++) {
    fprintf(of,"# %s: rx %d (ign %d dup %d loop %d mic %d ssid %d) tx %d (digi %d flood %d ssid %d ids %d)\n",
	    i->port,
	    i->stats.rx,i->stats.rx_ign,i->stats.rx_dup,i->stats.rx_loop,
	    i->stats.rx_mic,i->stats.rx_ssid,
	    i->stats.tx,i->stats.digi,i->stats.flood,i->stats.ssid,
	    i->stats.ids);
  }
  if (of != stdout && of != stderr)
    fclose(of);
  if (sig >= 0)
    signal(sig,print_stats);
}

static void
reset_stats(sig)
int sig;
{
  struct interface *i;
  int n;
  print_stats(-1);

  for (i = Intf, n = 0; n < N_intf; n++,i++) {
    bzero(&i->stats,sizeof(i->stats));
  }
  signal(sig,reset_stats);
}


/* setup a bare minimum legal ID (don't QRM the world with a digi path!) */
static void
set_id()
{
  struct ax_calls id;
  char idinfo[AX25_MTU];
  int i,n;
  struct interface *iface;

  for (n = 0, iface = Intf; n < N_intf; n++, iface++) {
    u_char *op = iface->idbuf;
    int oleft = sizeof(iface->idbuf);

    bzero(&id,sizeof(id));
    ax25_aton_entry("ID",id.ax_to_call.ax25_call);
    id.ax_to_call.ax25_call[ALEN] |= C; /* it's a command */
    id.ax_from_call = MYCALL(n);
    id.ax_type = UI;
    id.ax_pid = PID_NO_L3;
    id.ax_n_digis = 0;

    if (iface->i_flags&I_COOKED)
      gen_cooked_ax25(&op,&oleft,&id);	/* generate the kiss header */
    else
      gen_raw_ax25(&op,&oleft,&id);	/* generate the kiss header */
    *idinfo = '\0';
    for (i = 0; i < iface->n_aliases; i++) {
      strcat(idinfo,ax25_ntoa_pretty(&iface->aliases[i]));
      strcat(idinfo,"/R ");
    }
    for (i = 0; i < N_floods; i++) {
      strcat(idinfo,ax25_ntoa_pretty(&Floods[i].call));
      strcat(idinfo,"n-n/R ");
    }
    add_text(&op,&oleft,idinfo,strlen(idinfo),0,0);
    iface->idlen = sizeof(iface->idbuf) - oleft;
  }
}

static void
xmit_id(struct interface *i)
{
  if (Verbose) {
    fprintf(stderr,"%s: TX: ID\n",i->port);
  }
  if (sendto(i->tsock,i->idbuf,i->idlen,
	     0,(struct sockaddr *)&i->tsa,i->tsa_size) < 0) /* ship it off */
    perror(i->devname);
  i->i_flags &= ~I_NEED_ID;
  i->next_id = 0;
  ++i->stats.ids;
}

/*
 * sched alarm for next id.  Called with intf ptr if that interface
 * needs to schedule an ID, NULL during alarm sig handler.
 */
static struct interface *ID_next = NULL;
static void
sked_id(struct interface *iface)
{
  struct interface *i;
  int n;
  time_t now = time(0);
  time_t min = now+24*60*60;
  time_t when;

  if (iface && !(iface->i_flags&I_NEED_ID) && iface->idinterval) { 
    iface->next_id = now+iface->idinterval;
    iface->i_flags |= I_NEED_ID;
  }
  /* find minimum time among all interfaces 'til next needed ID. */
  for (i = Intf, n = 0, ID_next = NULL; n < N_intf; n++,i++) {
    if (i->i_flags&I_NEED_ID && i->idinterval && i->next_id > 0) {
      if ((when = i->next_id - now) <= 0) /* catch any already due */
	xmit_id(i);
      else if (i->next_id < min) {
	ID_next = i;		/* new minimum */
	min = ID_next->next_id;
      }    
    }
  }
  if (ID_next && ID_next->next_id && ID_next->next_id > 0) {
    alarm(when); /* next alarm in this many secs */
    if (Verbose) {
      fprintf(stderr,"next ID for %s in %d seconds\n",ID_next->port, when);
    }
  }
}

static void
identify(int sig)		/* wake up and ID! */
{
  if (ID_next && ID_next->i_flags&I_NEED_ID) {
    xmit_id(ID_next);
    ID_next = NULL;
    sked_id(NULL);		/* schedule next ID */
    signal(sig,identify);
  }
}

static void
identify_final(int sig)		/* wake up and ID! */
{
  struct interface *i;
  int n;

  for (i = Intf, n = 0; n < N_intf; n++,i++) {
    if (i->i_flags&I_NEED_ID && i->idinterval && i->next_id > 0) {
      xmit_id(i);
    }
  }
}

#include <getopt.h>
enum {NONE=no_argument,REQD=required_argument,OPT=optional_argument};
static struct option opts[] = {
  {"verbose",OPT,0,'v'},
  {"testing",NONE,0,'T'},
  {"interface",REQD,0,'p'},
  {"port",REQD,0,'p'},
  {"trace",REQD,0,'F'},
  {"flood",REQD,0,'f'},
  {"east",REQD,0,'e'},
  {"west",REQD,0,'w'},
  {"north",REQD,0,'n'},
  {"south",REQD,0,'s'},
  {"digipath",REQD,0,'d'},
  {"tag",REQD,0,'t'},
  {"keep",REQD,0,'k'},
  {"hops",REQD,0,'H'},
  {"logfile",REQD,0,'l'},
  {"idinterval",REQD,0,'i'},
  {"subst_mycall",NONE,0,'C'},
  {"nosubst_mycall",NONE,0,'c'},
  {"mice_xlate",NONE,0,'M'},
  {"nomice_xlate",NONE,0,'m'},
  {"x1j4_xlate",NONE,0,'X'},
  {"nox1j4_xlate",NONE,0,'x'},
  {"3rdparty",NONE,0,'3'},
  {"no3rdparty",NONE,0,'0'},
  {"kill_dupes",NONE,0,'D'},
  {"kill_loops",NONE,0,'L'},
  {"bud",REQD,0,'B'},
  {"nobud",REQD,0,'b'},
  {"version",NONE,0,'V'},
  {"help",NONE,0,'?'},
  {"opts",REQD,0,'o'},  /* ran out of letters */
#define O(x) ((int)(256+(int)x))
  {"rx",NONE,0,O('R')},
  {"norx",NONE,0,O('r')},
  {"tx",NONE,0,O('T')},
  {"notx",NONE,0,O('t')},
  {"txsame",NONE,0,O('S')},
  {"notxsame",NONE,0,O('s')},
  {"dupe",REQD,0,O('d')},
  {"duplicate",REQD,0,O('d')},
  {0,0,0,0}
};
static char *optstring = "CcMmXxF:f:n:s:e:w:t:k:H:l:i:d:p:DLVvT30o:B:b:";

static void
do_opts(int argc, char **argv)
{
  int s;
  int opt_index = 0;

  /* set interface defaults */
  I_flags = I_TX|I_RX|I_TXSAME;
  while ((s = getopt_long(argc, argv, optstring, opts, &opt_index)) != -1) {
    int p = -1;			/* used for NSEW hack, below */
    int fflags = 0;		/* use for fF hack */
    switch (s) {
    case 'v':
      ++Verbose;
      break;
    case 'T':
      ++Testing;
      break;
    case 'p':			/* -p port[:alias,alias,alias...] */
      if (N_intf >= MAXINTF) {
	fprintf(stderr,"too many interfaces!\n");
	exit(1);
      }
      {
	struct interface *i = &Intf[N_intf++];
	int a_i = 1;		/* default 0'th alias is MYCALL */
	char *op = strchr(optarg,':');
	/* fill in defaults */
	i->i_flags = I_flags;
	i->tag = Tag;
	i->taglen = Taglen;
	i->idinterval = Idinterval;
	i->bud = Bl_last;
	i->dupe = Dl_last;
	if (strncasecmp(optarg,"udp:",4) == 0) { /* IP/UDP port */
	  i->i_flags |= I_COOKED;
	  i->i_proto = P_UDP;
	  i->i_flags &= ~I_TXSAME; /* prevent stupid loops? */
	  a_i = 0;		/* get MYCALL here */
	  /* udp:foo.bar.net/aprs/ttl:call,alias... */
	  /* or udp:224.1.2.3/1234/ttl:call,alias... */
	  /* or udp:fe80::204:e2ff:fe3c:ca2a/1234/ttl:call,alias... */
	  if (!op || !*op) {	/* missing operand */
	    usage_udp();
	  }
	  i->port = ++op;	/* parse host/service/ttl later on */
	  op = strrchr(op,':'); /* skip backwards to the callsigns */
	} else if (strncasecmp(optarg,"ax25:",5) == 0) { /* new-style axlib */
	  i->i_proto = P_AX25;
	  if (!op || !*op) {	/* missing operand */
	    usage_ax25();
	  }
	  i->port = ++op;
	  op = strchr(op,':');
	  if (op)
	    *op = '\0';
	} else if (strncasecmp(optarg,"unix:",5) == 0) { /* Unix fifo */
	  i->i_flags |= I_COOKED;
	  i->i_proto = P_UNIX;
	  a_i = 0;		/* get MYCALL here */
	  if (!op || !*op) {	/* missing operand */
	    usage_unix();
	  }
	  i->port = ++op;
	  op = strrchr(op,':'); /* skip backwards to the callsigns */
	} else {		/* ax25lib port name (same as ax25:) */
	  fprintf(stderr,"Warning: deprecated usage. Use \"-p ax25:%s\"\n",optarg);
	  i->i_proto = P_AX25;
	  i->port = optarg;
	}
	if (op) {		/* additional aliases... */
	  *op++ = '\0';
	  if ((i->n_aliases=parsecalls(&i->aliases[a_i],MAXALIASES-1,op)) < 1) {
	    fprintf(stderr,"Don't understand port %s aliases %s\n",optarg,op);
	    exit(1);
	  }
	  if (i->i_proto == P_UDP || i->i_proto == P_UNIX)
	    i->n_aliases--;	/* alias 0 is MYCALL */
	}
      }
#ifdef notdef
      /* -p is an implicit -B - XXX why? */
      budlist_add("-",0);
#endif
      break;
    case 'F':			/* flooding trace alias */
      fflags = C_IS_TRACE;
    case 'f':			/* flooding wide alias */
      fflags |= C_IS_FLOOD;
      if (N_floods >= MAXALIASES) {
	fprintf(stderr,"too many flooding aliases!\n");
	exit(1);
      }
      if (ax25_aton_entry(optarg,Floods[N_floods].call.ax25_call) < 0) {
	fprintf(stderr,"Don't understand callsign %s\n",optarg);
	exit(1);
      }
      if ((Floods[N_floods].call.ax25_call[ALEN]&SSID) != 0) {
	fprintf(stderr,"-f %s: flooding callsign must have a zero SSID\n",
		optarg);
	exit(1);
      }
      Floods[N_floods].flags = fflags;
      Floods[N_floods++].len = strlen(optarg);
      break;
    case 'w':			/* directional unproto path */
      ++p;
    case 'e':
      ++p;
    case 's':
      ++p;
    case 'n':
      ++p;
      ++Digi_SSID;
      if (unproto(&Path[p],optarg) < 0) {
	fprintf(stderr,"Don't understand %c path %s\n",toupper(s),optarg);
	exit(1);
      }
      break;
    case 'd':			/* DIGI-style(non-SSID) unproto path */
      ++Have_digipath;
      if (parsecalls(Digipath,DIGISIZE,optarg)!=DIGISIZE) {
	fprintf(stderr,"Don't understand %c path %s\n",toupper(s),optarg);
	exit(1);
      }
      break;
    case 't':			/* tag to tack on to end of all packets */
      Tag = optarg;
      Taglen = strlen(optarg);
      if (*Tag == '-' && Taglen == 1) {
	Tag = NULL;
	Taglen = 0;
      }
      break;
    case 'k':
      if ((Keep = atoi(optarg)) <= 0)
	Keep = 28;		/* default keep is 28 */
      break;
    case 'H':
      Maxhops = atoi(optarg);
      if (Maxhops < 0) Maxhops = 0;
      break;
    case 'l':
      Logfile = optarg;		/* log digipeated packets */
      break;
    case 'i':
      Idinterval = atoi(optarg);
      break;
    case 'C':
      I_flags |= SUBST_MYCALL;
      break;
    case 'c':
      I_flags &= ~SUBST_MYCALL;
      break;
    case 'M':
      I_flags |= MICE_XLATE;
      break;
    case 'm':
      I_flags &= ~MICE_XLATE;
      break;
    case 'X':
      I_flags |= X1J4_XLATE;
      break;
    case 'x':
      I_flags &= ~X1J4_XLATE;
      break;
    case '3':
      I_flags |= I_3RDP;	/* enable 3rd party tunneling */
      break;
    case '0':
      I_flags &= ~I_3RDP;	/* disable 3rd party tunneling (transparent) */
      break;
    case 'o':			/* o for options cuz I'm out of letters! */
      switch (*optarg) {
      case 'R':			/* RX enable */
	I_flags |= I_RX;
	break;
      case 'r':			/* RX disable */
	I_flags &= ~I_RX;
	break;
      case 'T':
	I_flags |= I_TX;	/* TX enable */
	break;
      case 't':
	I_flags &= ~I_TX;	/* TX disable */
	break;
      case 'S':
	I_flags |= I_TXSAME;	/* re-TX on RX interface */
	break;
      case 's':
	I_flags &= ~I_TXSAME;	/* no re-TX on RX interface */
	break;
      default:
	usage();
      }
      break;
    case O('d'):		/* dupe to interface w/no modification */
      dupelist_add(optarg);
      break;
    case O('R'):
      I_flags |= I_RX;
      break;
    case O('r'):
      I_flags &= ~I_RX;
      break;
    case O('T'):
      I_flags |= I_TX;		/* TX enable */
      break;
    case O('t'):
      I_flags &= ~I_TX;	/* TX disable */
      break;
    case O('S'):
      I_flags |= I_TXSAME;	/* re-TX on RX interface */
      break;
    case O('s'):
      I_flags &= ~I_TXSAME;	/* no re-TX on RX interface */
      break;
    case 'D':
      Kill_dupes++;
      break;
    case 'L':
      Kill_loops++;
      break;
    case 'B':
      budlist_add(optarg,1);
      break;
    case 'b':
      budlist_add(optarg,0);
      break;
    case 'V':
      printf("aprsdigi: %s-%s\n",PACKAGE,VERSION);
      exit(0);
    case '?':
      usage();
    }
  }
  /* sanity-check args */
  if (N_intf <= 0) {
    fprintf(stderr,"aprsdigi: must specify at least one port with -p\n");
    exit(1);
  }
}

static void
die(char *s)
{
  perror(s);
  exit(1);
}

static void
usage()
{
  fprintf(stderr,"Usage: aprsdigi [-CcDLMXv] [-n|s|e|w path] [-fF call] [-t tag] [-k secs] [-l logfile] [-i interval] [-o r|R|t|T|s|S] [-p port:alias1,alias2...] [-B budlist] [-b budlist] ...\n");
  fprintf(stderr," general options:\n");
  fprintf(stderr," -v | --verbose     -- produce verbose debugging output\n");
  fprintf(stderr," -T | --testing     -- test mode: listen to my packets too\n");
  fprintf(stderr," -D | --kill_dupes  -- suppress Duplicate packets\n");
  fprintf(stderr," -L | --kill_loops  -- suppress Looping packets\n");
  fprintf(stderr," -V | --version     -- print program version and exit\n");
  fprintf(stderr," -n|s|e|w -- set North|South|East|West SSID directional path\n");
  fprintf(stderr," --north | --south | --east | --west\n");
  fprintf(stderr," -f | --flood       -- set Flooding callsign (usually WIDE)\n");
  fprintf(stderr," -F | --trace       -- set Flooding TRACE callsign (usually TRACE)\n");
  fprintf(stderr," -k | --keep        -- seconds of old dupes to remember\n");
  fprintf(stderr," -H | --hops        -- direct input maximum number of future hops allowed\n");
  fprintf(stderr," -l | --logfile     -- log digipeated packets here\n");
  fprintf(stderr," per-interface options (put *before* each -p as needed):\n");
  fprintf(stderr," -C (-c) | --[no]subst_mycall  -- do (not) perform Mycall substitution\n");
  fprintf(stderr," -M (-m) | --[no]mice_xlate    -- do (not) perform Mic-E translation\n");
  fprintf(stderr," -X (-x) | --[no]x1j4_xlate    -- do (not) perform X1J4 translation\n");
  fprintf(stderr," -3 | --3rdparty     -- perform 3rd party tunneling\n");
  fprintf(stderr," -0 | --no3rdparty   -- perform transparent tunneling (default)\n");
  fprintf(stderr," -o r (R) | --[no]rx           -- do (not) mark interface receive-only\n");
  fprintf(stderr," -o t (T) | --[no]tx           -- do (not) mark interface transmit-only\n");
  fprintf(stderr," -o s (S) | --[no]txsame       -- do (not) suppress retransmission on RX intf.\n");
  fprintf(stderr," -i | --idinterval   -- seconds between ID transmissions\n");
  fprintf(stderr,"             (Use \"-i 0\" to disable IDs)\n");
  fprintf(stderr," -t | --tag          -- tag text to add to received packets\n");
  fprintf(stderr,"             (Use \"-t -\" to reset to empty)\n");
  fprintf(stderr," -B | --bud call|addr -- accept traffic only from matching source.\n");
  fprintf(stderr," -b | --nobud call|addr -- deny traffic from matching source.\n");
  fprintf(stderr," --dupe interface     -- copy traffic from this interface.\n");
  fprintf(stderr,"\n");
  fprintf(stderr," -p | --port | --interface ax25:port:a1,a2,... -- port name and optional list of aliases\n");
  fprintf(stderr,"             port is from /etc/ax25/axports\n");
  fprintf(stderr,"             (primary callsign is obtained from the port)\n");
  fprintf(stderr," -p | --port | --interface udp:address/port/ttl:call,a1,a2... -- IP host,service,ttl,call and optional list of aliases\n");
  fprintf(stderr," -p | --port | --interface unix:path:call,a1,a2... -- FIFO filename, call and optional list of aliases\n");
  exit(1);
}

static void
usage_udp()
{
  fprintf(stderr,"aprsdigi: Use --int udp:host/service/ttl:callsign,a1,a2,...\n");
  fprintf(stderr," example: --int udp:224.1.2.3/1234/127:N2YGK-2,RELAY,WIDE\n");
  fprintf(stderr,"          --int udp:192.168.1.2/1234/16:N2YGK-2,RELAY,WIDE\n");
  fprintf(stderr,"          --int udp:n0clu.ampr.net/1234/16:N2YGK-2,RELAY,WIDE\n");
  fprintf(stderr,"          --int udp:2002:123:45::ab:cd/1234/16:N2YGK-2,RELAY,WIDE\n");
  exit(1);
}

static void
usage_ax25()
{
  fprintf(stderr,"aprsdigi: Use --int ax25:port:a1,a2,...\n");
  fprintf(stderr," example: --int ax25:1:N2YGK-7,RELAY,WIDE\n");
  exit(1);
}

static void
usage_unix()
{
  fprintf(stderr,"aprsdigi: Use --int unix:path:a1,a2,...\n");
  fprintf(stderr," example: --int unix:/tmp/fifo:N2YGK-7,RELAY,WIDE\n");
  exit(1);
}

static void
usage_budlist()
{
  fprintf(stderr,"aprsdigi: Use --[no]bud ax25:callsign[-SSID]|ip:host|udp:addr[/mask].\n");
  fprintf(stderr,"          \"--bud -\" resets the list to empty\n");
  fprintf(stderr,"          Use callsign-SSID to match specific station.\n");
  fprintf(stderr,"          Use callsign to match all SSIDs.\n");
  fprintf(stderr,"          Use host to match a given IP host.\n");
  fprintf(stderr,"          Use addr/mask to match a given IP range.\n");
  fprintf(stderr,"examples: --bud ax25:n0clu-14  -- matches only n0clu-14.\n");
  fprintf(stderr,"          --bud ax25:n0clu     -- matches all n0clu's\n");
  fprintf(stderr,"          --bud ip:n0clu.ampr.net  -- matches one host.\n");
  fprintf(stderr,"          --bud ip:192.168.5.3     -- matches one host.\n");
  fprintf(stderr,"          --bud ip:192.168.5.3/32  -- matches one host.\n");
  fprintf(stderr,"          --bud ip:192.168.5.0/25  -- matches subnet\n");
  fprintf(stderr,"          --bud ip:fe80::201:3ff:fe9a:38c6  -- matches one host.\n");
  fprintf(stderr,"          --bud ip:fe80::201:3ff:fe9a:38c6/128  -- matches one host.\n");
  fprintf(stderr,"          --bud ip:2002:905::/32  -- matches prefix.\n");
  exit(1);
}

static void
usage_dupelist()
{
  fprintf(stderr,"aprsdigi: Use --dupe ax25:interface|udp:addr|unix:file.\n");
  fprintf(stderr,"          \"--dupe -\" resets the list to empty\n");
  exit(1);
}

/*
 * do_ports: Initialize interfaces.
 */
static void
do_ports()
{
  struct interface *i;

  ax25_aton_entry("APRS",Aprscall.ax25_call);
  ax25_aton_entry("WIDE",Widecall.ax25_call);
  ax25_aton_entry("UDPIP",Udpipcall.ax25_call);
  ax25_aton_entry("trace",Trace_dummy.ax25_call);
  if (ax25_config_load_ports() == 0)
    fprintf(stderr, "aprsdigi: no AX.25 port data configured\n");
  
  for (i = Intf; i < &Intf[N_intf]; i++) {
    ++i->n_aliases;		/* count the zeroth element too! */
    switch (i->i_proto) {
    case P_AX25:
      do_port_ax25(i);
      break;
    case P_UNIX:
      do_port_unix(i);
      break;
    case P_UDP:
      do_port_udp(i);
      break;
    default:
      fprintf(stderr,"Unknown proto %d\n",i->i_proto);
      exit(1);
      break;
    }
  }
  calltab_init();
  dupelist_init();
}

static void
do_port_ax25(struct interface *i)
{
  struct ifreq ifr;
  int proto = (Testing)?ETH_P_ALL:ETH_P_AX25;

  if ((i->dev = ax25_config_get_dev(i->port)) == NULL) {
    fprintf(stderr, "aprsdigi: invalid ax25 port name - %s\n", i->port);
    exit(1);
  }
  i->devname = strdup(i->port);
  i->rsa_size = i->tsa_size = sizeof(struct sockaddr_ax25);

  if (!(i->i_flags & I_TX)) {
    i->tsock = -1;
  } else {			/* open transmit socket */
    if ((i->tsock = socket(AF_PACKET, SOCK_PACKET, htons(proto))) == -1) {
      die(i->port);
    }
    strcpy(((struct sockaddr*)&i->tsa)->sa_data, i->dev);
    ((struct sockaddr *)&i->tsa)->sa_family = AF_AX25;
  }

  if (!(i->i_flags & I_RX)) {
    i->rsock = -1;
  } else {			/* open receive socket */
    if ((i->rsock = socket(AF_PACKET, SOCK_PACKET, htons(proto))) == -1) {
      die(i->port);
    }
    strcpy(((struct sockaddr *)&i->rsa)->sa_data, i->dev);
    ((struct sockaddr *)&i->rsa)->sa_family = AF_AX25;
    if (bind(i->rsock, (struct sockaddr *)&i->rsa, i->rsa_size) < 0) {
      die(i->devname);
    }
  }

  /* set mycall (zeroth alias) to the port's */
  strcpy(ifr.ifr_name, i->dev); /* get this port's callsign */
  if (ioctl(i->rsock, SIOCGIFHWADDR, &ifr) < 0) {
    die(i->devname);
  }
  if (ifr.ifr_hwaddr.sa_family != AF_AX25) {
    fprintf(stderr,"%s: not AX25\n",i->port);
    exit(1);
  }
  bcopy(ifr.ifr_hwaddr.sa_data,I_MYCALL(i).ax25_call,sizeof(I_MYCALL(i).ax25_call));
  I_MYCALL(i).ax25_call[ALEN] |= ESSID_SPARE|SSSID_SPARE;
} 

static void
do_port_udp(struct interface *i)
{
  struct hostent *hp;
  struct servent *sp;
  char *name, *service, *ttl;
  const int one=1;
  int ttlval;
  struct addrinfo *ai, hints = {0, PF_UNSPEC, SOCK_DGRAM, 0};

  /* hostname/service(port)/ttl */
  i->dev = i->port;
  name = i->port;
  service = strchr(name,'/');
  if (!service || *service != '/')
    usage_udp();
  //  *service++ = '\0';
  ttl = strchr(&service[1],'/');
  if (!ttl || *ttl != '/')
    usage_udp();
  *ttl++ = '\0';
  i->devname = strdup(name);
  *service++ = '\0';
  if ((ttlval = atoi(ttl)) <= 0)
      usage_udp();

  if (Verbose)
    fprintf(stderr,"opening UDP socket on %s/%s/%d\n", name, service, ttlval);
  if (getaddrinfo(name,service,&hints,&ai) < 0)
    die(name);
  if (Verbose)
    fprintf(stderr,"UDP address info: family %d type %d proto %d next 0x%0x\n",
	    ai->ai_family,ai->ai_socktype,ai->ai_protocol,ai->ai_next);
  if (ai->ai_socktype != SOCK_DGRAM) {
    die("getaddrinfo returned non SOCK_DGRAM!\n");
  }
  if (!(i->i_flags & I_TX)) {
    i->tsock = -1;
  } else {
    if ((i->tsock = socket(ai->ai_family, SOCK_DGRAM, 0)) == -1)
      die(name);
    if (setsockopt(i->tsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
      die(i->port);
    if (ai->ai_family == AF_INET) { /* IPv4 */
      i->tsa_size = sizeof(struct sockaddr);
      if (IN_MULTICAST(ntohl(((struct sockaddr_in*)ai->ai_addr)->sin_addr.s_addr))) {
	if (setsockopt(i->tsock, SOL_IP, IP_MULTICAST_TTL, &ttlval, sizeof(ttlval)) < 0)
	  die(i->port);
      } else {
	if (setsockopt(i->tsock, SOL_IP, IP_TTL, &ttlval, sizeof(ttlval)) < 0)
	  die(i->port);
      }
#ifdef IPV6
    } else if (ai->ai_family == AF_INET6) { /* IPv6 */
      i->tsa_size = sizeof(struct sockaddr_storage);
      /* XXX ??? */
#endif /* IPV6 */
    } else {
      fprintf(stderr,"%s: unsupported protocol family\n",i->port);
      exit(1);
    }
    bzero(&i->tsa,sizeof(i->tsa));
    bcopy((struct sockaddr*)ai->ai_addr,&i->tsa,sizeof(i->tsa)); /* fill sockaddr w/sockaddr_in */
    if (connect(i->tsock, (struct sockaddr *)&i->tsa, i->tsa_size) < 0) {
      die(i->devname);
    }
  }

  /* now the receive socket */
  if (!(i->i_flags & I_RX)) {
    i->rsock = -1;
  } else {
    if ((i->rsock = socket(ai->ai_family, SOCK_DGRAM, 0)) == -1) {
      die(i->port);
    }
    if (setsockopt(i->rsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
      die(i->port);
    
    if (ai->ai_family == AF_INET && !IN_MULTICAST(ntohl(((struct sockaddr_in*)ai->ai_addr)->sin_addr.s_addr))) {
      ((struct sockaddr_in *)ai->ai_addr)->sin_addr.s_addr = INADDR_ANY; /* not mcast so wildcard listen */
      i->rsa_size = sizeof(struct sockaddr);
#ifdef IPV6
    } else if (ai->ai_family == AF_INET6) { /* IPv6 */
      /* XXX ??? */
#endif /* IPV6 */
    }
    i->rsa_size = sizeof(struct sockaddr_storage);
    bzero(&i->rsa,sizeof(i->rsa));
    bcopy((struct sockaddr*)ai->ai_addr,&i->rsa,sizeof(i->rsa));
    if (bind(i->rsock, (struct sockaddr *)&i->rsa, i->rsa_size) < 0) {
      die(i->devname);
    }
    /* if the UDP socket is a multicast group, then do IGMP join for rsock */
    
    if (ai->ai_family == AF_INET && IN_MULTICAST(ntohl(((struct sockaddr_in*)ai->ai_addr)->sin_addr.s_addr))) {
      struct ip_mreq mreq;
      mreq.imr_multiaddr = ((struct sockaddr_in*)ai->ai_addr)->sin_addr;
      mreq.imr_interface.s_addr = INADDR_ANY;
      if (setsockopt(i->rsock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
	die(i->port);
#ifdef IPV6
    } else if (ai->ai_family == AF_INET6) { /* IPv6 */
      /* XXX ??? */
#endif /* IPV6 */
    }
  }
}

static void
do_port_unix(struct interface *i)
{
  const int one=1;
  struct sockaddr_un *sun;
  char name[sizeof(sun->sun_path)];
  
  i->dev = i->port;
  i->devname = strdup(i->dev);
  strncpy(name,i->dev,sizeof(name));
  i->rsa_size = i->tsa_size = sizeof(struct sockaddr_un);
  /* the receive socket */
  if (!(i->i_flags & I_RX)) {
    i->rsock = -1;
  } else {
    if (Verbose)
      fprintf(stderr,"opening RX FIFO socket on %s\n", name);
    if ((i->rsock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
      die(name);
    }
    bzero(&i->rsa,sizeof(i->rsa));
    sun = (struct sockaddr_un *)&i->rsa;
    sun->sun_family = AF_UNIX;
    strncpy(sun->sun_path,name,sizeof(sun->sun_path));    
    /* create the fifo if it doesn't already exist */
    if (bind(i->rsock, (struct sockaddr *)&i->rsa, i->rsa_size) < 0
	&& errno != EADDRINUSE) {
      die(name);
    }
  }

  /* the transmit socket */
  if (!(i->i_flags & I_TX)) {
    i->tsock = -1;
  } else {
    if (Verbose)
      fprintf(stderr,"opening TX FIFO socket on %s\n", name);
    if ((i->tsock = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
      die(name);
    }
    bzero(&i->tsa,sizeof(i->tsa));
    sun = (struct sockaddr_un *)&i->tsa;
    sun->sun_family = AF_UNIX;
    strncpy(sun->sun_path,name,sizeof(sun->sun_path));    
    if (setsockopt(i->tsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
      die(i->port);
  }
}

static void
set_sig()
{
  signal(SIGHUP,cleanup);
  signal(SIGINT,cleanup);
  signal(SIGQUIT,cleanup);
  signal(SIGTERM,cleanup);
  signal(SIGPWR,cleanup);
  signal(SIGPIPE,cleanup);
  signal(SIGUSR1,print_stats);
  signal(SIGUSR2,reset_stats);
  signal(SIGALRM,identify);
}

/*
 * compare two ax.25 "flooding" addresses.
 * returns: floodtype
 * args: f is the known flood call prefix, len is its len.
 *       c is what we are testing against it to see if it is
 *       flood, floodN, or none.
 */
static int
floodcmp(ax25_address *f, int len, ax25_address *c)
{
  int i;

  for (i = 0; i < len; i++) 
    if ((f->ax25_call[i] & 0xFE) != (c->ax25_call[i] & 0xFE))
      return C_IS_NOFLOOD;
  return floodtype(c,i);
}

/*
 * floodtype: Is it a flood, or floodN, or not a flood at all?
 * The call should have blanks in the remainder
 * (e.g. WIDE-3), or a single digit followed by blanks
 * (WIDE3-3).  If it has other junk then is is not a flood (WIDENED).
 */
static int
floodtype(ax25_address *c, int i)
{
  int dig = (c->ax25_call[i]&0xFE)>>1;
  if (dig == ' ')
    return C_IS_FLOOD;
  else if (dig >= '0' && dig <= '9' 
	   && ((i>=5) || (c->ax25_call[i+1] & 0xFE)>>1 == ' '))
    return C_IS_FLOOD|C_IS_FLOODN;
  else
    return C_IS_NOFLOOD;
}

#define DEVTYPE(i) ((i)->i_proto == P_UDP)?"udp":\
	    ((i)->i_proto == P_AX25)?"ax25":\
	    ((i)->i_proto == P_UNIX)?"unix":"???"


static void
check_config()
{
  int n, j;
  struct callsign_list *cl;
  
  printf("Linux APRS(tm) digipeater\n%s\n",copyr);
  printf("Version: aprsdigi %s-%s\n",PACKAGE,VERSION);
  printf("This is free software covered under the GNU General Public License.\n");
  printf("There is no warranty.  See the file COPYING for details.\n\n");

  config_print(stdout);
  printf("My callsigns and aliases (routing table):\n");
  printf("Callsign  Interfaces...\n");
  for (cl = calltab; cl; cl = cl->next) {
    struct interface_list *l;
    char t[10];

    strcpy(t,ax25_ntoa_pretty(cl->callsign));
    if (cl->flags&C_IS_FLOOD)
      strcat(t,"n-n");		/* indicate a WIDEn-n */
    printf("%-9.9s",t);
    for (l = cl->l; l; l = l->next) {
      printf(" %s:%s",DEVTYPE(l->i),l->i->devname);
    }
    printf("\n");
  }
  if (N_floods == 0)
    printf("No flooding callsigns\n");
  if (Have_digipath) {
    printf("SSID-based omnidirectional routing:\n");
    if (N_floods) {		/* doing WIDEn-n so this won't get used */
      fprintf(stderr,"ERROR: -d is inconsistent with WIDEn-n flooding\n");
      exit(1);
    }
    if (intf_of(&Digipath[0]) == NULL) {
      fprintf(stderr,"ERROR: %s: first callsign in omni route is not one of my callsigns.\n",
	      ax25_ntoa_pretty(&Path[n].fsa_digipeater[0]));
      exit(1);
    }
    for (j = 0; j < DIGISIZE; j++) {
      printf("%-9.9s ",ax25_ntoa_pretty(&Digipath[j]));
    }
    printf("\n");
  }
  if (Digi_SSID) {
    printf("SSID-based directional routing:\n");
    for (n = 0; n < 4; n++) {
      if (intf_of(&Path[n].fsa_digipeater[0]) == NULL) {
	fprintf(stderr,"ERROR: %s: first callsign in %c route is not one of my callsigns.\n",
		ax25_ntoa_pretty(&Path[n].fsa_digipeater[0]),Dirs[n]);
	exit(1);
      }
      printf("\n%c:        ",Dirs[n]);
      for (j = 0; j < Path[n].fsa_ax25.sax25_ndigis; j++)
	printf("%-9.9s ",ax25_ntoa_pretty(&Path[n].fsa_digipeater[j]));
    }
    printf("\n");
  } else {
    printf("SSID-based routing: (none)\n");
  }
  /* flags */
  printf("keep dupes for: %d seconds\n",Keep);
  if (Maxhops) {
     printf("maximum number of hops allowed is %d for packets input directly to us\n",Maxhops);
  }
  printf("log file: %s\n",(Logfile)?Logfile:"(none)");
#define onoff(x) (x)?"ON":"OFF"
  printf("kill dupes: %s loops: %s  testing: %s\n",
	 onoff(Kill_dupes), onoff(Kill_loops), 
	 onoff(Testing));
#undef onoff
  fflush(stdout);
}

static void
config_print(FILE *f)
{
  struct interface *i;
  int n, j;

  fprintf(f,"# configuration:\n");
  budlist_print_all(f);
  dupelist_print_all(f);
  for (i = Intf, n = 0; n < N_intf; i++,n++) {
    fprintf(f,"interface %s:%s\n", DEVTYPE(i),i->devname);
    fprintf(f," callsign %s\n",ax25_ntoa_pretty(&i->aliases[0]));
    for (j = 1; j < i->n_aliases; j++) {
      fprintf(f," alias %s\n",ax25_ntoa_pretty(&i->aliases[j]));
    }
#define onoff(opt) fprintf(f," option %s %s\n",(#opt),((i->i_flags&(opt))?("on"):("off")))
    onoff(SUBST_MYCALL);
    onoff(MICE_XLATE);
    onoff(X1J4_XLATE);
    onoff(I_TX);
    onoff(I_RX);
    onoff(I_TXSAME);
    onoff(I_3RDP);
#undef onoff
    fprintf(f," option idinterval %d #(%02d:%02d)\n",i->idinterval, i->idinterval/60,i->idinterval%60);
    fprintf(f," option tag %s\n",(i->taglen)?i->tag:"#(none)");
    if (i->bud)
      fprintf(f," budlist %d\n",i->bud->bl_list_no);
    if (i->dupe && i->dupe->dl_ent_head)
      fprintf(f," dupelist %d\n",i->dupe->dl_list_no);
  }
  fprintf(f,"# end of configuration\n\n");
}

/* 
 * budlist's can be "global" or per-interface.  Basically, the
 * budlist is built up with -B|-b's prior to the interface's -p
 * and then these get glued to the per-interface budlist. To
 * reset the budlist between ports, use "-B -".
 */

static void
budlist_add(char *item,int permit)
{
  struct sockaddr_in *sin, *sinmask;
  struct sockaddr_in6 *sin6, *sin6mask;
  struct sockaddr_ax25 *sinax25, *sinax25mask;
  struct budlist_entry *e;

  if (!item || !*item)
    usage_budlist();
  if (!Bl_head) {		/* this is first invocation */
    Bl_head = Bl_last = (struct budlist *)calloc(1,sizeof(struct budlist));
    if (!Bl_head) 
      die("malloc");
    Bl_head->bl_list_no = 1;
  }
  if (item[0] == '-' && item[1] == '\0') { /* start a new list */
    if (Bl_last->bl_ent_last)	{ /* if the list has entries... */
      /* ... start a new list */
      Bl_last->bl_next = (struct budlist *)calloc(1,sizeof(struct budlist));
      if (!Bl_last->bl_next)
	die("malloc");
      Bl_last->bl_next->bl_list_no = Bl_last->bl_list_no+1;
      Bl_last = Bl_last->bl_next;
    }
    return;
  }

  if (!Bl_last->bl_ent_head) {	/* list has no elements yet */
    Bl_last->bl_ent_head = Bl_last->bl_ent_last = 
      (struct budlist_entry *)calloc(1,sizeof(struct budlist_entry));
    if (!Bl_last->bl_ent_head)
      die("malloc");
  } else if (!Bl_last->bl_ent_last->be_next) { /* alloc a new entry */
    Bl_last->bl_ent_last->be_next = 
      (struct budlist_entry *)calloc(1,sizeof(struct budlist_entry));
    if (!Bl_last->bl_ent_last->be_next)
      die("malloc");
    Bl_last->bl_ent_last = Bl_last->bl_ent_last->be_next;
  }
  /* done with all that allocation foolishness */
  e = Bl_last->bl_ent_last;
  e->be_permit = permit;
  if (!strcasecmp(item,"any")) {
    bzero(&e->be_addr,sizeof(e->be_addr));
    setmask(8*sizeof(e->be_mask), (u_char *)&e->be_mask);
    e->be_maskbits = -1;
  } else if (strncasecmp(item,"ax25:",5) == 0) {
    char *name;
    struct sockaddr_ax25 *call = (struct sockaddr_ax25 *)&e->be_addr;
    struct sockaddr_ax25 *callmask = (struct sockaddr_ax25 *)&e->be_mask;

    name = &item[5];
    bzero(&e->be_addr,sizeof(e->be_addr));
    if (ax25_aton_entry(name,call->sax25_call.ax25_call) < 0)
      die(item);
    call->sax25_family = AF_AX25;
    call->sax25_ndigis = 0;

    /* set maskbits as follows:
     * if callsign then mask the 6 bytes containing the call.
     * if callsign-SSID then mask all 7.
     */
    bzero(&e->be_mask,sizeof(e->be_mask));
    e->be_maskbits = (strchr(name,'-'))? 7*8 : 6*8;
    setmask(8*sizeof(call->sax25_family), (u_char *)&callmask->sax25_family);
    setmask(e->be_maskbits, (u_char *)callmask->sax25_call.ax25_call);
  } else if (strncasecmp(item,"ip:",3) == 0) {
    struct addrinfo *ai, hints = {0, PF_UNSPEC, SOCK_DGRAM, 0};
    char *name, *mask;

    e->be_maskbits = 0;
    name = &item[3];
    if (mask = strrchr(name,'/')) {
      *mask++ = '\0';
      e->be_maskbits = atoi(mask);
    }
    if (getaddrinfo(name,NULL,&hints,&ai) < 0)
      die(item);
    
    bzero(&e->be_mask,sizeof(e->be_mask));
    bcopy(ai->ai_addr,&e->be_addr,ai->ai_addrlen);
    switch (ai->ai_family) {
    case AF_INET:		/* IPv4 */
      sin = (struct sockaddr_in *)&e->be_addr;
      sinmask = (struct sockaddr_in *)&e->be_mask;
      if (e->be_maskbits == 0)
	e->be_maskbits = 32;
      /* sockaddr_in bitmask: sin_family all ones; sin_port all zeros; 
	 sin_addr specified number of ones */
      setmask(8*sizeof(sin->sin_family), (u_char *)&sinmask->sin_family);
      setmask(e->be_maskbits, (u_char *)&sinmask->sin_addr);
      break;
    case AF_INET6:		/* IPv6 */
      sin6 = (struct sockaddr_in6 *)&e->be_addr;
      sin6mask = (struct sockaddr_in6 *)&e->be_mask;
      if (e->be_maskbits == 0)
	e->be_maskbits = 128;
      /* sockaddr_in6 bitmask: sin6_family ones; sin6_port zeros; 
	 sin6_flowinfo zeros; sin6_addr specified; sin6_scope_id zeros */
      setmask(8*sizeof(sin6->sin6_family), (u_char *)&sin6mask->sin6_family),
      setmask(e->be_maskbits,(u_char *)&sin6mask->sin6_addr);
      break;
    default:
      fprintf(stderr,"%s: Unsupported protocol family (%d)\n",name,ai->ai_family);
      exit(1);
    }
    freeaddrinfo(ai);
  } else
    usage_budlist();
}

static void
budlist_print_all(FILE *f)
{
  struct budlist *l;

  for (l = Bl_head; l; l=l->bl_next)
    budlist_print(f,l);
}
  

static void
budlist_print(FILE *f, struct budlist *bud)
{
  char host[1000];
  struct sockaddr_in *sin;
  struct sockaddr_in6 *sin6;
  struct sockaddr_ax25 *sinax25;
  struct budlist_entry *b;

  if (!bud || !bud->bl_ent_head) 
    return;
  for (b = bud->bl_ent_head; b; b = b->be_next) {
    printaddr(&b->be_addr,host,sizeof(host));
    fprintf(f," budlist %d %s %s",bud->bl_list_no,
	   (b->be_permit)?"permit":"deny",host);
    if (b->be_maskbits>=0)
      fprintf(f,"/%d\n",b->be_maskbits);
    else
      fprintf(f,"\n");
  }
}

static void
printaddr(struct sockaddr_storage *s, char *buf, int buflen)
{
  struct sockaddr_in *sin = (struct sockaddr_in *)s;
  struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)s;
  struct sockaddr_ax25 *sinax25 = (struct sockaddr_ax25 *)s;

  switch (sin->sin_family) {
  case AF_INET:
    inet_ntop(sin->sin_family,&sin->sin_addr, buf, buflen);
    break;
  case AF_INET6:
    inet_ntop(sin6->sin6_family,&sin6->sin6_addr, buf, buflen);
    break;
  case AF_AX25:
    strncpy(buf,ax25_ntoa_pretty(&sinax25->sax25_call),buflen);
    break;
  case 0:
    strncpy(buf,"any",buflen);      
    break;
  default:
    strncpy(buf,"???",buflen);
    break;
  }
}

/*
 * Is this packet permitted by budlist?
 *  1. Check (possibly IP) source address
 *  2. Check AX25 source callsign
 *  3. Check intermediate repeated digis
 *  First match wins.
 */

int
budlist_permit(struct stuff *s)
{
  struct budlist_entry *be;
  struct interface *i = s->i;
  char testhost[200],host[200];
  int j;

  if (!s->i->bud || !s->i->bud->bl_ent_head) /* no budlist */
    return 1;			/* permitted */
  for (be = s->i->bud->bl_ent_head; be; be = be->be_next) {
    struct sockaddr_ax25 *be25  = (struct sockaddr_ax25 *)&be->be_addr;
    struct sockaddr_ax25 *bemask  = (struct sockaddr_ax25 *)&be->be_mask;
    /* 1: check source address */
    if (Verbose) {
      printaddr(&be->be_addr,testhost,sizeof(testhost));
      printaddr(&i->rsafrom,host,sizeof(host));
      fprintf(stderr,"budlist_permit: budlist %d %s %s/%d vs. net source %s\n",
	      s->i->bud->bl_list_no, (be->be_permit)?"permit":"deny",
	      testhost, be->be_maskbits, host);
    }
    if (cmpmask(8*sizeof(be->be_addr), (u_char *)&be->be_addr, 
		(u_char *)&be->be_mask, (u_char *)&i->rsafrom) == 0)
      return be->be_permit;

    /* 2: check ax25 from call. */
    if (be25->sax25_family != AF_AX25)
      continue;			/* no need to compare non-ax25 buds */
    if (Verbose) {
      fprintf(stderr,"budlist_permit: budlist %d %s %s/%d vs. ax25 source %s\n",
	      s->i->bud->bl_list_no, (be->be_permit)?"permit":"deny",
	      testhost, be->be_maskbits, ax25_ntoa_pretty(&s->in.ax_from_call));
    }
    if (cmpmask(be->be_maskbits,
		(u_char *)&be25->sax25_call.ax25_call,
		(u_char *)&bemask->sax25_call.ax25_call, 
		(u_char *)&s->in.ax_from_call) == 0)
      return be->be_permit;
    /* 3. check ax25 digis. */ 
    for (j = 0; j < s->in.ax_next_digi-1; j++) {
      if (Verbose) {
	fprintf(stderr,"budlist_permit: budlist %d %s %s/%d vs. ax25 digi[%d] %s\n",
		s->i->bud->bl_list_no, (be->be_permit)?"permit":"deny",
		testhost, be->be_maskbits, j,
		ax25_ntoa_pretty(&s->in.ax_digi_call[j]));
      }
    if (cmpmask(be->be_maskbits,
		(u_char *)&be25->sax25_call.ax25_call,
		(u_char *)&bemask->sax25_call.ax25_call, 
		(u_char *)&s->in.ax_digi_call[j]) == 0)
      return be->be_permit;
    }
  }
  return 1;			/* permit */
}

static void
setmask(int bits, u_char *mask)
{
  int B,b,rem;

  for (B = 0; B < bits/8; B++) {
    mask[B] |= 0xff;
  }
  for (b = 0, rem = 0; b < bits%8; b++) {
    rem = rem >> 1;
    rem |= 0x80;
  }
  mask[B] = rem;
}

static int
cmpmask(int bits, u_char *val1, u_char *mask, u_char *val2) 
{
  int B,bytes;

  bytes = bits/8;
  if (bits%8)
    ++bytes;
  
  for (B = 0; B < bytes; B++) {
    if ((val1[B] & mask[B]) != (val2[B] & mask[B]))
      return val1[B] - val2[B];
  }
  
  return 0;
}

/* gee, this looks like a copy of budlist.  I should make a class. */

static void
dupelist_add(char *item)
{
  struct dupelist_entry *e;

  if (!item || !*item)
    usage_dupelist();
  ++Doing_dupes;
  if (!Dl_head) {		/* this is first invocation */
    Dl_head = Dl_last = (struct dupelist *)calloc(1,sizeof(struct dupelist));
    if (!Dl_head) 
      die("malloc");
    Dl_head->dl_list_no = 1;
  }
  if (item[0] == '-' && item[1] == '\0') { /* start a new list */
    if (Dl_last->dl_ent_last)	{ /* if the list has entries... */
      /* ... start a new list */
      Dl_last->dl_next = (struct dupelist *)calloc(1,sizeof(struct dupelist));
      if (!Dl_last->dl_next)
	die("malloc");
      Dl_last->dl_next->dl_list_no = Dl_last->dl_list_no+1;
      Dl_last = Dl_last->dl_next;
    }
    return;
  }

  if (!Dl_last->dl_ent_head) {	/* list has no elements yet */
    Dl_last->dl_ent_head = Dl_last->dl_ent_last = 
      (struct dupelist_entry *)calloc(1,sizeof(struct dupelist_entry));
    if (!Dl_last->dl_ent_head)
      die("malloc");
  } else if (!Dl_last->dl_ent_last->de_next) { /* alloc a new entry */
    Dl_last->dl_ent_last->de_next = 
      (struct dupelist_entry *)calloc(1,sizeof(struct dupelist_entry));
    if (!Dl_last->dl_ent_last->de_next)
      die("malloc");
    Dl_last->dl_ent_last = Dl_last->dl_ent_last->de_next;
  }
  /* done with all that allocation foolishness */
  e = Dl_last->dl_ent_last;
  e->de_dev = item;
}

static void
dupelist_print_all(FILE *f)
{
  struct dupelist *l;

  for (l = Dl_head; l; l=l->dl_next)
    dupelist_print(f,l);
}
  

static void
dupelist_print(FILE *f, struct dupelist *dupe)
{
  char host[1000];
  struct dupelist_entry *l;

  if (!dupe || !dupe->dl_ent_head) 
    return;
  for (l = dupe->dl_ent_head; l; l = l->de_next) {
    fprintf(f," dupelist %d %s\n",dupe->dl_list_no,l->de_dev);
  }
}

static void add_intflist(struct interface_list **il, struct interface *i);

/*
 * dupelist_init:  Take dupelist from i->dupe, search for interfaces
 * with matching names and add them to i->dupe->dl_il.  Do not add own
 * interface.
 */
static void
dupelist_init() 
{
  struct dupelist_entry *de;
  struct dupelist *dl;
  struct interface *i,*j;
  int n;

  for (i = Intf; i < &Intf[N_intf]; i++) {
    if (i->dupe) {    /* walk all entries in this list */
      for (de = i->dupe->dl_ent_head; de; de = de->de_next) { 
	char devname[100];
	/* search for an interface name that matches */
	for (j = Intf, n = 0; n < N_intf; j++,n++) {
	  sprintf(devname,"%s:%s",DEVTYPE(j),j->devname);
	  if (Verbose)
	    fprintf(stderr,"dupelist_init: comparing %s to %s...\n",
		    de->de_dev,devname);
	  if ((strcasecmp(devname,de->de_dev) == 0) && j != i) {
	    if (Verbose)
	      fprintf(stderr," ... matched.  Adding to interface list\n");
	    add_intflist(&i->dupe->dl_il,j); /* add j to dupe interface list */
	    break;
	  }
	}
	if (n >= N_intf) {		/* search fell through */
	  fprintf(stderr,"warning: dupelist %d interface %s not found.\n",
		  i->dupe->dl_list_no,de->de_dev);
	}
      }
    }
  }
}

/*
 * add_intflist: Add an interface to an interface list.
 */
static void
add_intflist(struct interface_list **il, struct interface *i)
{
  struct interface_list *n = calloc(1,sizeof(struct interface_list));

  if (n == NULL)
    die("malloc");
  n->i = i;
  if (*il == NULL)
    *il = n;
  else {
    for (; (*il)->next; (*il) = (*il)->next) ;
    (*il)->next = n;
  }
}

/* 
 * 3rd party tunneling per APRS spec 1.01 Ch. 17
 *
 * What I implement is what I will call "transparent" tunneling. 
 * For APRS 1.0 compatibility, need to implement "3rd party" tunneling:
 *  0. 3rd party only applies when input and output intfs are different.
 *  1. Make 3rdparty an option so I can do transparent tunneling too.
 *  2. For transmit to a 3rdparty intf: just drop unusused digipeaters.
 *  3. For transmit to any intf when pkt is received from a 3rdparty intf:
 *     a. drop unused digipeaters (in case the sender didn't)
 *     b. append Third-Party Network ID (Use UDPIP) followed by the xmit
 *        intf callsign (MYCALL(i)) to digi list, marked as repeated.
 *     c. Spit out "}" and then gen_cooked_ax25() (the Source Path
 *        Header) into the payload, followed by the original payload.
 *     d. Rewrite the raw ax25 header with a from_call of MYCALL, and no
 *        digipeat path(!).
 *     e. xmit on the ax25 interface(s).
 * Editorial note: Tunneling was implemented this way because the APRS
 * authors were using hardware TNC-2's in CONVerse mode and were unable to
 * modify the real AX.25 headers the way I can.
 */

/* removed non-repeated digipeaters */

static void 
drop_unused_digis(struct ax_calls *calls) 
{
  if (!calls->ax_n_digis || calls->ax_next_digi >= calls->ax_n_digis)
    return;			/* all digis are used up */
  if ((calls->ax_n_digis = calls->ax_next_digi) > 0) { /* truncate the list */
    calls->ax_digi_call[calls->ax_next_digi-1].ax25_call[ALEN] |= HDLCAEB;
  } else {			/* no digis left */
    calls->ax_from_call.ax25_call[ALEN] |= HDLCAEB;
  }
  if (Verbose)
    fprintf(stderr,"3rd party: Unused digis have been dropped.\n");
}

/*
 * from_3rdparty: generates the source path header and insert it into
 * the list of packet payloads in pkt[].
 * clobbers the s->out digipeater list as well.
 */
static void 
from_3rdparty(struct stuff *s,
	      int npkts,	/* dimension of the next two vectors */
	      u_char *pkt[],	/* output buffers for source path header */
	      int pktl[])	/* output length of sph (AX25_MTU max) */
{
  u_char pref[AX25_MTU], *pp;
  int j, plen, rem;

  /* a. drop unused digipeaters (in case the sender didn't) */
  drop_unused_digis(&s->out);
  /* b. append Third-Party Network ID (Use UDPIP) ... */
  /* XXX fencepost check */
  if (s->out.ax_n_digis >= AX25_MAX_DIGIS-1) {
    if (Verbose)
      fprintf(stderr,"%s: 3rd party digi list overflow. First 2 digis dropped.\n",
	      s->i->port);
    for (j = 2; j < s->out.ax_n_digis; j++)
      s->out.ax_digi_call[j - 2] = s->out.ax_digi_call[j];
    s->out.ax_n_digis -= 2;
    s->out.ax_next_digi -= 2;
  } 
  s->out.ax_digi_call[s->out.ax_n_digis] = Udpipcall;
  s->out.ax_digi_call[s->out.ax_n_digis].ax25_call[ALEN] |= REPEATED;
  /* ... followed by the xmit intf callsign (MYCALL(i)) ... */
  s->out.ax_digi_call[s->out.ax_n_digis+1] = I_MYCALL(s->i);
  s->out.ax_digi_call[s->out.ax_n_digis+1].ax25_call[ALEN] |= REPEATED|HDLCAEB;
  s->out.ax_n_digis += 2;
  s->out.ax_next_digi += 2;
  /* c. Spit out "}" and then gen_cooked_ax25() (the Source Path
     Header) into the payload, followed by the original payload. */
  plen = 0;
  pref[plen++] = '}';
  rem = AX25_MTU - plen;
  pp = &pref[1];
  gen_cooked_ax25(&pp,&rem,&s->out); /* generate the source path header */
  plen = AX25_MTU - rem + 1;	/* XXX??? */
  /* for each payload, insert the prefix in front of it! */
  for (j = 0; j < npkts; j++) {	/* may have to repeat for multiple pkts */
    u_char *oldp = pkt[j];
    if (pktl[j]) {
      if (pktl[j] + plen > AX25_MTU) {
	pktl[j] -= AX25_MTU-pktl[j]-plen;
	if (Verbose)
	  fprintf(stderr,"%s: Oversized 3rd party detunneled packet truncated.\n",
		  s->i->port);
      }
      bcopy(pkt[j],&pref[plen-1],pktl[j]); /* tag vecp onto end of pref. */
      bcopy(pref,pkt[j],pktl[j]+=plen-1); /* not sure why this is -1 */
      pref[plen] = '\0';
    }
  }
  /* d. Rewrite the raw ax25 header with a from_call of MYCALL, and no
   *    digipeat path(!).  */
  s->out.ax_n_digis = s->out.ax_next_digi = 0;
  s->out.ax_from_call.ax25_call[ALEN] |= HDLCAEB;
  s->out.ax_from_call = I_MYCALL(s->i);
}

static void
to_3rdparty(struct ax_calls *calls)
{
  drop_unused_digis(calls);		/* just drop unused digipeaters */
}

