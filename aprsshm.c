#if defined(USE_SHM) && defined(HAVE_SHMCTL)
/*
 * routines to collect up packets for 'keepfor' minutes, manage shared
 *  memory circular buffer, etc. 
 * See aprsmon.c for the main program.
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
 * Copyright (c) 1996,1997,1999 Alan Crosswell
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
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "aprsshm.h"

#define MAXPPM 10
#define MAXPKT 256

struct pkt {			/* we make a circular buffer of these */
  int len;			/* packet length */
  time_t recvd;			/* when received */
  unsigned char data[MAXPKT];	/* the packet. */
};

struct pktseg {			/* mapping of the shared segment */
  struct pkt *head, *tail;	/* relative addr */
  int keepfor;			/* might as well make this info available */
  int filled;			/* and fact that we did some early tosses */
  int segsize;			/* need this in the client */
  struct pkt pkt0;		/* first packet in seg */
};
  
static int seg = -1, segsize = 0, sem = -1, master = 0;
static struct pktseg *pktseg = NULL;
static char *infofile = NULL;

/* convert relative ptr to absolute & vice-versa */
#define RELABS(x) ((struct pkt *)(((intptr_t)(x))+(intptr_t)pktseg))
#define ABSREL(x) ((struct pkt *)(((intptr_t)(x))-(intptr_t)pktseg))
#define PKTSIZE (sizeof(struct pkt))

int
shm_master(k,ifile)		/* "collector" shared memory master */
int k;				/* keep for n minutes */
char *ifile;
{
  struct sembuf sops[1];
  FILE *info;

  master = 1;
  infofile = ifile;
  segsize = sizeof(struct pktseg)+(k*MAXPPM*PKTSIZE);
  if ((seg = shmget(IPC_PRIVATE,segsize,IPC_CREAT|0644)) < 0) {
    perror("shmget");
    return -1;
  }
  if ((pktseg = (struct pktseg *)shmat(seg,0,0)) < 0) {
    perror("shmat");
    return -1;
  }
  if ((sem = semget(IPC_PRIVATE,1,IPC_CREAT|0644)) < 0) {
    perror("semget");
    return -1;
  }
  if (semctl(sem,0,SETVAL,1) < 0) { /* initialize the semaphore to locked */
    perror("semctl");
    return -1;
  }
  if ((info = fopen(infofile,"w")) != NULL) {
    fprintf(info,"%d %d\n",seg,sem);
    fclose(info);
  } else {
    perror(infofile);
    return -1;
  }

  bzero(pktseg,segsize);	/* initialize the shared segment */
  pktseg->segsize = segsize;
  pktseg->keepfor = k;
  pktseg->head = ABSREL(&pktseg->pkt0);
  pktseg->tail = (struct pkt *)-1;

  sops[0].sem_num = 0;  
  sops[0].sem_op = -1;		/* release the lock */
  sops[0].sem_flg = 0;
  if (semop(sem,sops,1) < 0) {
    perror("semop");
    return -1;
  }
  return 0;
}

void
shm_cleanup(sig)		/* release segment and semaphore */
int sig;
{
  if (pktseg) {
    shmdt((char *)pktseg);
  }
  if (master) {
    if (infofile)
      unlink(infofile);
    if (seg >= 0)
      shmctl(seg,IPC_RMID,0);
    if (sem >= 0)
      semctl(sem,0,IPC_RMID,0);
  }
}

int
shm_slave(f,ifile)		/* "client" shared memory slave */
FILE *f;
char *ifile;
{
  struct sembuf sops[1];
  FILE *info;

  infofile = ifile;
  if ((info = fopen(infofile,"r"))) {
    if (fscanf(info,"%d%d",&seg,&sem) != 2) {
      fclose(info);
      fprintf(stderr,"#%s: didn't scan\n",infofile);
      return -1;
    }
    fclose(info);
  } else {			/* no collector running.  That's OK. */
    printf("# No saved data available.\r\n");
    return -1;
  }
    
  if ((pktseg = (struct pktseg *)shmat(seg,0,SHM_RDONLY)) < 0) {
    perror("shmat");
    return -1;
  }
  segsize = pktseg->segsize;

  sops[0].sem_num = 0;  
  sops[0].sem_op = 0;		/* wait for a read lock?? */
  sops[0].sem_flg = 0;
  if (semop(sem,sops,1) < 0) {
    perror("semop");
    return -1;
  }
  dump_saved(f);		/* print out the saved stuff */
  shmdt((char *)pktseg);
  return 0;
}

/* accumulate packets and age out stale ones */
void
collect(buf,len)
unsigned char *buf;
int len;
{
  time_t now = time(NULL);
  struct pkt *p;
  struct sembuf sops[1];

  if (len>MAXPKT) {
    fprintf(stderr,"buffer overrun (pkt len %d)\n",len);
    len = MAXPKT;
  }
  /* obtain the write lock */
  sops[0].sem_num = 0;  
  sops[0].sem_op = 1;		
  sops[0].sem_flg = 0;
  if (semop(sem,sops,1) < 0) {
    perror("semop");
    return;
  }
  /* next free packet goes right after tail packet */
  p = free_stale(now,1);	/* toss stale packets/make room */
  p->recvd = now;
  p->len = len;
  bcopy(buf,p->data,len);
  bzero(&p->data[len],MAXPKT-len); /* clean out any garbage */
  pktseg->tail = ABSREL(p);	/* new tail */

  /* release the write lock */
  sops[0].sem_num = 0;  
  sops[0].sem_op = -1;		
  sops[0].sem_flg = 0;
  if (semop(sem,sops,1) < 0) {
    perror("semop");
  }
}

/* get rid of stale (older than keepfor minutes) packets and more
   packets if the buffer is full and makespace is true.  Returns
   abs pointer to where there's room for next packet. */

struct pkt *
free_stale(now,makespace)	/* lose stale packets */
time_t now;
int makespace;
{
  struct pkt *head = RELABS(pktseg->head);
  struct pkt *tail = RELABS(pktseg->tail);
  struct pkt *segend = (struct pkt *)((char *)pktseg+segsize);
  struct pkt *nextp;
  int tossed = 0;
  
  if (pktseg->tail == (struct pkt *)-1) { /* nothing in buffer yet! */
    return head;
  }

  /* throw out any stale packets, moving head forward through list. */
  while((head->recvd < (now - pktseg->keepfor*60)) && head != tail) {
    ++tossed;
    if (++head >= segend) {	/* wrap around */
      head = &pktseg->pkt0;
    }
  }
  pktseg->head = ABSREL(head);

  /* find where the next packet will be stuffed */
  nextp = &tail[1];
  if (nextp >= segend) {	/* have to circle around */
    nextp = &pktseg->pkt0;
  }

  /* must flush oldest non-stale (head) packet */
  if (makespace && !tossed && head == nextp) {
    if (++head >= segend) 	/* wrap around */
      head = &pktseg->pkt0;
    pktseg->head = ABSREL(head);
    pktseg->filled++;
  }
  if (pktseg->filled && tossed)	/* full buffer remedied by stale-out */
    pktseg->filled = 0;
  return nextp;
}

void
dump_saved(f)			/* write saved pkts to file */
FILE *f;
{
  extern void dump(FILE *,unsigned char *, int,time_t);
  struct pkt *head = RELABS(pktseg->head);
  struct pkt *tail = RELABS(pktseg->tail);
  struct pkt *segend = (struct pkt *)((char *)pktseg+segsize);
  struct pkt *p;
  struct tm *t;

  if (pktseg->tail == (struct pkt *)-1)
    return;			/* no logged packets yet. */
  t = gmtime(&head->recvd);
  fprintf(f,"# Activity seen since %04d%02d%02d %02d%02dZ:\r\n",
	  t->tm_year+1900,
	  t->tm_mon+1,
	  t->tm_mday,
	  t->tm_hour,
	  t->tm_min);
  for (p = head; p != tail;) { /* then print the rest */
    dump(f,p->data,p->len,p->recvd);
    if (++p >= segend) { /* have to circle around */
      p = &pktseg->pkt0;
    }
  }
  if (p == tail) {
    dump(f,p->data,p->len,p->recvd);    
  }
  fprintf(f,"# end of recorded activity.\r\n");
}
#endif
