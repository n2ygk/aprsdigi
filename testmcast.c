/* test multicast */
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
#include <netax25/kernel_ax25.h>
#include <netax25/kernel_rose.h>
#include <netax25/ax25.h>
#include <netax25/axlib.h>
#include <netax25/axconfig.h>
#ifndef AX25_MTU
#define AX25_MTU 256
#endif
#include <netdb.h>
#include "libax25ext.h"

static void
die(char *p, char *s)
{
  fprintf(stderr,"%s: ",p);
  perror(s);
  exit(1);
}

main(int argc,char **argv)
{
  struct sockaddr_in sin;
  struct sockaddr tsa, rsa;
  struct hostent *hp;
  struct servent *sp;
  char *name = argv[1], *service = argv[2], *ttl = argv[3];
  const int one=1;
  int ttlval;
  int tsock, rsock;
  unsigned char buffer[AX25_MTU];
  int selfds = 0;
  fd_set select_mask;

  if (argc < 4 || (ttlval = atoi(ttl)) <= 0) {
    fprintf(stderr,"Usage: %s destination port ttl\n",argv[0]);
    exit(1);
  }

  fprintf(stderr,"opening UDP socket on %s/%s/%d\n", name, service, ttlval);

  bzero(&sin, sizeof(struct sockaddr_in));
  if (isdigit(*name)) {
    sin.sin_addr.s_addr = inet_addr(name);
  }
  else if (hp = gethostbyname(name)) {
    bcopy(hp->h_addr, (char *)&sin.sin_addr, hp->h_length);
  }
  else {
    fprintf(stderr,"Unrecognized host name: %s\n",name);
    exit(1);
  }

  if (isdigit(*service)) {
    sin.sin_port = htons(atoi(service));
  }
  else if (sp = getservbyname(service,"udp")) {
    sin.sin_port = sp->s_port;
  }
  else {
    fprintf(stderr,"Unrecognized service: %s\n",service);
    exit(1);
  }
  sin.sin_family = AF_INET;

  if (argc == 4) {
    if ((tsock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
      perror(name);
      exit(1);
    }
    if (setsockopt(tsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
      die("setsockopt SO_REUSEADDR",name);
    if (IN_MULTICAST(ntohl(sin.sin_addr.s_addr))) {
      if (setsockopt(tsock, SOL_IP, IP_MULTICAST_TTL, &ttlval, sizeof(ttlval)) < 0)
	die("setsockopt MULTICAST_TTL",name);
    } else {
      if (setsockopt(tsock, SOL_IP, IP_TTL, &ttlval, sizeof(ttlval)) < 0)
	die("setsockopt IP_TTL",name);
    }
    
    bzero(&tsa,sizeof(tsa));
    bcopy(&sin,&tsa,sizeof(sin)); /* fill sockaddr w/sockaddr_in */
    
  }
  if (argc == 4) {
    if (connect(tsock, &tsa, sizeof(struct sockaddr)) < 0) {
      die("connect(tsock)",name);
    }
  }

  /* now the rsock */

  if ((rsock = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
    die("rsock = socket",name);
  }

  if (setsockopt(rsock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0)
    die("rsock SO_REUSEADDR",name);

  /* if the UDP socket is a multicast group, then do IGMP join for rsock */

  if (!IN_MULTICAST(ntohl(sin.sin_addr.s_addr)))
    sin.sin_addr.s_addr = INADDR_ANY;

  bzero(&rsa,sizeof(rsa));
  bcopy(&sin,&rsa,sizeof(sin));
  
  if (bind(rsock, &rsa, sizeof(struct sockaddr)) < 0) {
    die("bind(rsock)",name);
  }

  if (IN_MULTICAST(ntohl(sin.sin_addr.s_addr))) {
    struct ip_mreq mreq;
    mreq.imr_multiaddr = sin.sin_addr;
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(rsock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
  }

  /* sockets set up, enter loop, receiving from stdin & rsock,
     transmitting what's read from stdin on tsock */

  /* set up the initial select mask */
  FD_ZERO(&select_mask);
  FD_SET(rsock, &select_mask);
  FD_SET(0, &select_mask);
  selfds = rsock+1;
  for (;;) {
    int len;
    fd_set rmask = select_mask;
    int size = sizeof(struct sockaddr);
    
    fprintf(stderr,"before select rsock=%d rmask=0x%0x\n",rsock,rmask);
    if (select(selfds,&rmask,NULL,NULL,NULL) < 0) {
      if (errno == EINTR)
	continue;
      perror("select");
      return 1;
    }
    fprintf(stderr,"after select rmask=0x%0x\n",rmask);    
    /* find which sockets have data */
    if (FD_ISSET(rsock,&rmask)) {
      fprintf(stderr,"rsock was set\n");
      if ((len = recvfrom(rsock,buffer,sizeof(buffer),0,&rsa,&size)) < 0) {
	if (errno == EINTR)
	  continue;
	perror(name);
	exit(1);
      }
      fprintf(stderr,"rsock rec'd %d: %s\n",len,buffer);
    }
    if (FD_ISSET(0,&rmask)) {
      int r;
      fprintf(stderr,"stdin was set\n");
      if ((len = read(0,buffer,sizeof(buffer))) < 0) {
	perror("stdin");
	exit(1);
      }
      fprintf(stderr,"stdin read %d: %s\n",len,buffer);
      /* now xmit it */
      if ((r = sendto(tsock,buffer,len,0,&tsa,sizeof(tsa))) < 0)
	die("sendto","xmit");
      fprintf(stderr,"transmitted %d\n",r);
    }
    
  } /* end of for(;;) */
  return 0;			/* NOTREACHED */
}
