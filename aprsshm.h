extern int shm_master(int,char *);
extern int shm_slave(FILE *,char *);
extern void collect(unsigned char *, int);
extern struct pkt *free_stale(time_t,int);
extern void dump_saved(FILE *);
extern void shm_cleanup(int);
