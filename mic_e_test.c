#include <stdio.h>
#include <time.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <getopt.h>
#include "mic_e.h"

void
main(argc,argv)			/* test this formatter */
int argc;
char **argv;
{
  char b[100];
  char ob1[512],ob2[512];
  int ol1,ol2;

  bzero(b,sizeof(b));
  while (fgets(b,sizeof(b),stdin)) {
    int tl = strlen(b);
    u_char *to = strchr(b,'>');
    u_char *i = strchr(b,':');
    time_t tick;

    if (tl < sizeof(b) && b[tl-1] == '\n')
      b[tl-1] = '\0';
    if (!to || !i) {
      putchar('?');
      puts(b);
      continue;
    }
    ++to;
    ++i;
    time(&tick);
    
    if (fmt_mic_e(to,i,strlen(i),ob1,&ol1,ob2,&ol2,tick)
	|| fmt_x1j4(to,i,strlen(i),ob1,&ol1,ob2,&ol2,tick)) {
      if (ol2) {
	fwrite(ob2,ol2,1,stdout);	/* telemetry */
	putchar('\n');
      }
      if (ol1) {
	fwrite(ob1,ol1,1,stdout);	/* posit */
	putchar('\n');
      }
    } else {
      putchar('#');
      puts(b);
    }
    bzero(b,sizeof(b));
  }
}

