/*
 * libax25ext.h: Some added functions for libax25.  Conditionally
 *  compiled only if the installed libax25 has not integrated them.
 *  Copyright (c) 1999 E. Alan Crosswell, N2YGK
 */

#ifndef HAVE_LIBAX25_EXTENSIONS
  
struct ax_calls {
  ax25_address ax_to_call;
  ax25_address ax_from_call;
  int ax_n_digis;		/* number of digis to follow */
  ax25_address ax_digi_call[AX25_MAX_DIGIS];
  int ax_next_digi;		/* index of next digi (1st non-repeated) */
  unsigned char ax_type;	/* frame type */
  unsigned char ax_pid;		/* protocol ID */
};

/*
 * parse a received kiss data frame into ax_calls and update data pointer
 *  and length next byte of frame.  Returns -1 on invalid frame, 0
 *  on valid frame, 1 on valid frame that still has a non-repeated digi.
 */
typedef enum {PK_INVALID=-1,PK_VALID=0,PK_VALDIGI=1} pk_val;
extern pk_val parse_raw_ax25(unsigned char **, int *, struct ax_calls *);
/* parse a received cooked TNC-2 or AEA format data frame */
extern pk_val parse_cooked_ax25(unsigned char **, int *, struct ax_calls *);
/* generate a raw kiss data frame header (converse of parse_raw_ax25) */
extern int gen_raw_ax25(unsigned char **, int *, struct ax_calls *);
/* generate a cooked TNC-2 format data frame header (converse of parse_cooked_ax25) */
extern int gen_cooked_ax25(unsigned char **, int *, struct ax_calls *);

/* pretty version of stand libax25's ax25_ntoa */
extern char *ax25_ntoa_pretty(const ax25_address *a);

#endif
