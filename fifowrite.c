/* fifowrite: write to the aprsdigi fifo */
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

int Verbose = 1;


static void die(char *s);

main(int argc, char **argv)
{
  struct sockaddr_un mysun;
  int sock;
  u_char buf[1024];
  int n;
 
  if (argc != 2) {
    fprintf(stderr,"Usage: %s file\n", argv[0]);
    exit(1);
  }
  bzero(&mysun,sizeof(mysun));
  mysun.sun_family = AF_UNIX;
  strncpy(mysun.sun_path,argv[1],sizeof(mysun.sun_path));
  if ((sock = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
    die("socket");
  }

  while (fgets(buf,sizeof(buf),stdin)) {
    int sent;
    if ((sent = sendto(sock,buf,strlen(buf),0,(struct sockaddr *)&mysun,
		       sizeof(mysun))) < 0)
      die("sendto");
  }
}

static void
die(char *s)
{
  perror(s);
  exit(1);
}

