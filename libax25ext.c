/*
 * libax25ext.c: Some added functions for libax25.  Conditionally
 *  compiled only if the installed libax25 has not integrated them.
 *  which they haven't :-(
 *  Copyright (c) 1999,2001,2002 E. Alan Crosswell, N2YGK
 */

#ifndef HAVE_LIBAX25_EXTENSIONS

#include <netax25/ax25.h>
#include <netrose/rose.h>
#include "libax25ext.h"
#include <string.h>

#define	ALEN		6
#define	AXLEN		7
#define	HDLCAEB		0x01
#define	REPEATED	0x80
#define	UI		0x03	/* unnumbered information (unproto) */
#define	PID_NO_L3	0xF0	/* no level-3 */

/*
 * parse a received kiss data frame into ax_calls and update data pointer
 *  and length next byte of frame.  Returns -1 on invalid or non-UI frame, 0
 *  on valid frame, 1 on valid frame that still has a non-repeated digi.
 */

pk_val
parse_raw_ax25(unsigned char **frame, int *len, struct ax_calls *calls)
{
  if ((**frame & 0xf) != 0 || *len < 1+(2*AXLEN))
    return PK_INVALID;		/* not a valid kiss data frame */
  ++*frame;
  --*len;
  bzero(calls,sizeof(*calls));
  calls->ax_next_digi = 0;
  bcopy(*frame,calls->ax_to_call.ax25_call,AXLEN);
  *frame += AXLEN;
  *len -= AXLEN;
  bcopy(*frame,calls->ax_from_call.ax25_call,AXLEN);
  *frame += AXLEN;
  *len -= AXLEN;
  if (*len <= 0)
    return PK_VALID;
  if (!(calls->ax_from_call.ax25_call[ALEN] & HDLCAEB))	{ /* digis listed */
    for (calls->ax_n_digis = 0; 
	 calls->ax_n_digis < AX25_MAX_DIGIS && *len > 0; 
	 calls->ax_n_digis++, *len-=AXLEN,*frame+=AXLEN) {
      bcopy(*frame,calls->ax_digi_call[calls->ax_n_digis].ax25_call,AXLEN);
      if ((*frame)[ALEN]&REPEATED)
	calls->ax_next_digi = calls->ax_n_digis+1;
      if ((*frame)[ALEN]&HDLCAEB) { /* last digi */
	*frame += AXLEN;
	*len -= AXLEN;
	calls->ax_n_digis++;
	break;
      }
    }
  }
  if (--*len >= 0)
    calls->ax_type = *(*frame)++;
  if (--*len >= 0)
    calls->ax_pid = *(*frame)++;
  if (!(calls->ax_type == UI && calls->ax_pid == PID_NO_L3))
    return PK_INVALID;		/* not a UI text frame */
  if (calls->ax_n_digis == 0
      || calls->ax_next_digi >= calls->ax_n_digis) /* all digis used up */
    return PK_VALID;
  else
    return PK_VALDIGI;
}

/* 
 * like parse_raw_ax25 but the frame header is in TNC-2 or AEA format
 * per APRS spec 1.01 p. 84 
 * TNC-2: source>dest,digi1,digi2*,...,digi8:
 * AEA: source*>digi1>digi2*>...,digi8>dest:
 *
 * See parse_cooked_ax25.(fig,pdf) for a state diagram for the parser (uggh).
 */

pk_val
parse_cooked_ax25(unsigned char **frame, int *len, struct ax_calls *calls)
{
  unsigned char *tok;
  int state,next_state = 0;

  while (next_state <= 99) {
    int switchtok, i;
    state = next_state;
    switch(state) {
    case 0:
      /* 0: Initial state */
      bzero(calls,sizeof(*calls));
      calls->ax_n_digis = 0;	/* this is "n = 0" in the diagram */
      
      if ((tok = strpbrk(*frame,"*>")) == NULL) /* source callsign */
	return PK_INVALID;
      switchtok = *tok;
      *tok++ = '\0';
      ax25_aton_entry(*frame,calls->ax_from_call.ax25_call);
      *len -= tok - *frame;
      *frame = tok;
      switch (switchtok) {
      case '*':
	calls->ax_from_call.ax25_call[ALEN] |= REPEATED;
	next_state = 1;
	break;
      case '>':
	next_state= 2;
	break;
      }
      break;
    case 1:
      if (*tok++ == '>') {	/* 1->3: AEA */
	next_state = 3;
	*len -= tok - *frame;
	*frame = tok;
      } else
	return PK_INVALID;
      break;
    case 11:			/* 11->3: AEA */
      if (*tok++ != '>')
	return PK_INVALID;
      *frame = tok;		/* ? */
      (*len)--;
      next_state = 3;
      break;
    case 3:			/* 3->5: AEA */
      if ((tok = strpbrk(*frame,">*:")) == NULL) /* callsign */
	return PK_INVALID;
      switchtok = *tok;
      *tok++ = '\0';
      switch (switchtok) {
      case '>':			/* 5->3: digi */
      case '*':
	if (calls->ax_n_digis >= AX25_MAX_DIGIS)
	  return PK_INVALID;
	ax25_aton_entry(*frame,calls->ax_digi_call[calls->ax_n_digis].ax25_call);
	*len -= tok - *frame;
	*frame = tok;
	calls->ax_n_digis++;
	if (switchtok == '>') {
	  next_state = 3;
	} else {
	  /* set REPEATED flag for all digis up to and including this */
	  for (i = 0; i < calls->ax_n_digis; i++)
	    calls->ax_digi_call[i].ax25_call[ALEN] |= REPEATED; 
	  calls->ax_next_digi = calls->ax_n_digis;
	  next_state = 7;
	}
	break;
      case ':':
	ax25_aton_entry(*frame,calls->ax_to_call.ax25_call);
	*len -= tok - *frame;
	*frame = tok;
	next_state = 99;	
	break;
      default:
	return PK_INVALID;
      }
      break;
    case 5:			/* not really a state */
      return PK_INVALID;
      break;
    case 7:
      if (*tok++ != '>')
	return PK_INVALID;
      (*len)--;
      *frame = tok;
      next_state = 3;
      break;
    case 2:			/* 2->4: TNC2, unless it's an AEA:-) */
      if ((tok = strpbrk(*frame,">*:,")) == NULL) /* callsign */
	return PK_INVALID;
      switchtok = *tok;
      switch (switchtok) {
      case '>':			/* 4->3: AEA */
	*tok++ = '\0';
	if (calls->ax_n_digis >= AX25_MAX_DIGIS)
	  return PK_INVALID;
	ax25_aton_entry(*frame,calls->ax_digi_call[calls->ax_n_digis].ax25_call);
	*len -= tok - *frame;
	*frame = tok;
	calls->ax_n_digis++;
	next_state = 3;
	break;
      case '*':			/* 4->3: AEA, heard direct */
	*tok++ = '\0';
	if (calls->ax_n_digis >= AX25_MAX_DIGIS)
	  return PK_INVALID;
	ax25_aton_entry(*frame,calls->ax_digi_call[calls->ax_n_digis].ax25_call);
	*len -= tok - *frame;
	*frame = tok;
	calls->ax_n_digis++;
	/* set repeated AEB flags for all digis up to and including this */
	for (i = 0; i < calls->ax_n_digis; i++)
	  calls->ax_digi_call[i].ax25_call[ALEN] |= REPEATED; /* +HDLCAEB? */
	calls->ax_next_digi = calls->ax_n_digis;
	next_state = 11;
	break;
      case ':':
	*tok++ = '\0';
	ax25_aton_entry(*frame,calls->ax_to_call.ax25_call);
	*len -= tok - *frame;
	*frame = tok;
	next_state = 99;	
	break;
      case ',':
	*tok++ = '\0';
	ax25_aton_entry(*frame,calls->ax_to_call.ax25_call);
	*len -= tok - *frame;
	*frame = tok;
	next_state = 6;
	break;
      }
      break;
    case 4:			/* not really a state */
      return PK_INVALID;
      break;
    case 6:			/* 6->8 */
      if ((tok = strpbrk(*frame,":*,")) == NULL) /* callsign */
	return PK_INVALID;
      switchtok = *tok;
      *tok++ = '\0';
      if (calls->ax_n_digis >= AX25_MAX_DIGIS)
	return PK_INVALID;
      ax25_aton_entry(*frame,calls->ax_digi_call[calls->ax_n_digis].ax25_call);
      *len -= tok - *frame;
      *frame = tok;
      calls->ax_n_digis++;
      switch (switchtok) {
      case ':':			/* 8->T */
	next_state = 99;
	break;
      case '*':			/* 8->10 */
	/* set repeated AEB flags for all digis up to and including this */
	for (i = 0; i < calls->ax_n_digis; i++)
	  calls->ax_digi_call[i].ax25_call[ALEN] |= REPEATED; /* +HDLCAEB? */
	calls->ax_next_digi = calls->ax_n_digis;
	next_state = 10;
	break;
      case ',':			/* 8->6 */
	next_state = 6;
	break;
      }
      break;
    case 8:
      return PK_INVALID;	/* not really a state */
      break;
    case 10:
      switch(*tok) {
      case ',':
	next_state = 6;
	break;
      case ':':
	next_state = 99;
	break;
      default:
	return PK_INVALID;
      }
      *tok++ = '\0';
      (*len)--;
      *frame = tok;
      break;
  case 99:			/* terminal */
    calls->ax_type = UI;
    calls->ax_pid = PID_NO_L3;
    if (calls->ax_n_digis == 0)
      calls->ax_from_call.ax25_call[ALEN] |= HDLCAEB;
    else
      calls->ax_digi_call[calls->ax_n_digis-1].ax25_call[ALEN] |= HDLCAEB; 

    if (calls->ax_n_digis == 0
	|| calls->ax_next_digi >= calls->ax_n_digis) /* all digis used up */
      return PK_VALID;
    else
      return PK_VALDIGI;
    break;
  default:
    return PK_INVALID;
    }

  }
  /* NOTREACHED */
}


/* reverse of parse_raw_ax25: generates a kiss frame */
int
gen_raw_ax25(unsigned char **frame, int *len, struct ax_calls *calls)
{
  int minsize = 1+AXLEN+AXLEN+(calls->ax_n_digis*AXLEN)+2;
  int i;
  
  if (*len < minsize)
    return -1;
  *len -= minsize;
  if (calls->ax_n_digis == 0)
    calls->ax_from_call.ax25_call[ALEN] |= HDLCAEB;
  else
    calls->ax_from_call.ax25_call[ALEN] &= ~HDLCAEB;

  *(*frame)++ = '\0';		/* KISS type 0 */
  bcopy(calls->ax_to_call.ax25_call,*frame,AXLEN);
  *frame += AXLEN;
  bcopy(calls->ax_from_call.ax25_call,*frame,AXLEN);
  *frame += AXLEN;
  for (i = 0; i < calls->ax_n_digis; i++,*frame += AXLEN) {
    calls->ax_digi_call[i].ax25_call[ALEN] &= ~HDLCAEB;
    bcopy(calls->ax_digi_call[i].ax25_call,*frame,AXLEN);
  }    
  if (i > 0)
    *(*frame-AXLEN+ALEN) |= HDLCAEB;
  *(*frame)++ = calls->ax_type;
  *(*frame)++ = calls->ax_pid;
  return 0;
}

/* Like gen_raw_ax25 but in "cooked" TNC-2 format per APRS spec 1.01 p. 83 */
/* source_call>dest_call,digi1,digi2*,...,digi8: */
int
gen_cooked_ax25(unsigned char **frame, int *len, struct ax_calls *calls)
{
  int minsize = AXLEN+3+1+AXLEN+3+1+(calls->ax_n_digis*(AXLEN+3+1))+2;
  int i,l;
  
  if (*len < minsize)
    return -1;

  strncpy(*frame,ax25_ntoa_pretty(&calls->ax_from_call),AXLEN+3);
  l = strlen(*frame);
  *frame += l;
  *(*frame)++ = '>'; 
  *len -= (l+1);
  strncpy(*frame,ax25_ntoa_pretty(&calls->ax_to_call),AXLEN+3);
  l = strlen(*frame);
  *frame += l; 
  *len -= l;
  for (i = 0; i < calls->ax_n_digis; i++) {
    *(*frame)++ = ',';
    (*len)--;
    strncpy(*frame,ax25_ntoa_pretty(&calls->ax_digi_call[i]),AXLEN+3);
    l = strlen(*frame);
    *frame += l;    
    *len -= l;
    /* only the last repeated digi gets * */
    if (calls->ax_n_digis && calls->ax_next_digi-1 == i) {
      *(*frame)++ = '*';
      (*len)--;
    }
  }
  *(*frame)++ = ':';
  (*len)--;
  return 0;
}

/*
 *  ax25 -> ascii conversion, pretty version
 *
 *  lifted from libax25/axutils.c and fixed so that SSID 0 comes out
 *  as MYCALL instead of MYCALL-0, just like most people expect!
 *  The libax25 developers didn't accept my patch submission so I have
 *  to drag this code around forever.  Oh well.
 */
char *ax25_ntoa_pretty(const ax25_address *a)
{
  static char buf[11];
  char c, *s;
  int n;
  
  for (n = 0, s = buf; n < 6; n++) {
    c = (a->ax25_call[n] >> 1) & 0x7F;
    
    if (c != ' ') *s++ = c;
  }
  
  if ((n = ((a->ax25_call[6] >> 1) & 0x0F))) { /* not SSID=0 */
    *s++ = '-';
    
    if (n > 9) {
      *s++ = '1';
      n -= 10;
    }
    
    *s++ = n + '0';
  }
  
  *s++ = '\0';
  
  return buf;
}

#endif


