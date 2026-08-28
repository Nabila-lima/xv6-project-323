struct stat;
struct rtcdate;

struct pstats {
  uint cpu_ticks;
  uint sleep_ticks;
  uint runnable_ticks;
  uint ctx_switches;
  uint preemptions;
};

struct procinfo {
  int pid;
  char name[16];
  int state;
  int timeslice;

  uint cpu_ticks;
  uint sleep_ticks;
  uint ctx_switches;
  uint preemptions;
};


// ---------------------------------------------------------
// System calls
// ---------------------------------------------------------

int fork(void);
int exit(void) __attribute__((noreturn));
int wait(void);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
char* sbrk(int);
int sleep(int);
int uptime(void);
int getyear(void);


// ---------------------------------------------------------
// Round Robin scheduling system calls
// ---------------------------------------------------------

int setquantum_pid(int pid, int quantum);
int gettimeslice(int pid);
int getpstats(int pid, struct pstats *ps);
int getprocinfo(int pid, struct procinfo *pi);


// ---------------------------------------------------------
// Semaphore system calls
// ---------------------------------------------------------

int sem_init(int value);
int sem_wait(int id);
int sem_signal(int id);


// ---------------------------------------------------------
// ulib.c
// ---------------------------------------------------------

int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
void printf(int, const char*, ...);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
void* malloc(uint);
void free(void*);
int atoi(const char*);
