/*
 * tsh - A tiny shell program with job control
 *
 * Name: Wang Jiarui
 * ID: 520021911165
 */

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* Misc manifest constants */
#define MAXLINE 1024   /* max line size */
#define MAXARGS 128    /* max args on a command line */
#define MAXJOBS 16     /* max jobs at any point in time */
#define MAXJID 1 << 16 /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/*
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;   /* defined in libc */
char prompt[] = "tsh> "; /* command line prompt (DO NOT CHANGE) */
int verbose = 0;         /* if true, print additional output */
int nextjid = 1;         /* next job ID to allocate */
struct job_t {           /* The job struct */
  pid_t pid;             /* job PID */
  int jid;               /* job ID [1, 2, ...] */
  int state;             /* UNDEF, BG, FG, or ST */
  char cmdline[MAXLINE]; /* command line */
};
struct job_t JOBS[MAXJOBS]; /* The job list */

/* End global variables */

/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv);
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs);
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid);
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid);
int pid2jid(pid_t pid);
void listjobs(struct job_t *jobs);

void usage(void);
void unix_error(char *msg);
void app_error(char *msg);
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);
/*
 * Tool Functions
 */
size_t slen(const char *str);
char *itoa(int value, char *result, int base);
/*
 * Wrapped Systemcall, they will EXIT when encountered error
 */
/*
 * functions used in tsh.c and will set errno:
 *  sigemptyset, sigfillset, sigprocmask, sigaction, setpgid, write, sigsuspend,
 * waitpid, strtol, kill.
 *  strtol can be handled and errno is used for further report, so is not
 * wrapped.
 *  sigsupend, kill and waitpid may generate uncritical errno, they can
 * continue, IGNORE them ERROR in other function is intolerable, Abort when
 * errno is emitted
 */
void Write(int fd, const void *buf, size_t n);
void Sigemptyset(sigset_t *sigset);
void Sigfillset(sigset_t *sigset);
void Sigprocmask(int how, const sigset_t *restrict sigset,
                 sigset_t *restrict oldset);
void Setpgid(pid_t pid, pid_t pgid);
void Sigsuspend(sigset_t *sigset);
void Kill(pid_t pid, int sig);
pid_t Waitpid(pid_t pid, int *wstatus, int options);
/*
 * main - The shell's main routine
 */
int main(int argc, char **argv) {
  char c;
  char cmdline[MAXLINE];
  int emit_prompt = 1; /* emit prompt (default) */

  /* Redirect stderr to stdout (so that driver will get all output
   * on the pipe connected to stdout) */
  dup2(1, 2);

  /* Parse the command line */
  while ((c = getopt(argc, argv, "hvp")) != EOF) {
    switch (c) {
    case 'h': /* print help message */
      usage();
      break;
    case 'v': /* emit additional diagnostic info */
      verbose = 1;
      break;
    case 'p':          /* don't print a prompt */
      emit_prompt = 0; /* handy for automatic testing */
      break;
    default:
      usage();
    }
  }

  /* Install the signal handlers */

  /* These are the ones you will need to implement */
  Signal(SIGINT, sigint_handler);   /* ctrl-c */
  Signal(SIGTSTP, sigtstp_handler); /* ctrl-z */
  Signal(SIGCHLD, sigchld_handler); /* Terminated or stopped child */

  /* This one provides a clean way to kill the shell */
  Signal(SIGQUIT, sigquit_handler);

  /* Initialize the job list */
  initjobs(JOBS);

  /* Execute the shell's read/eval loop */
  while (1) {

    /* Read command line */
    if (emit_prompt) {
      printf("%s", prompt);
      fflush(stdout);
    }
    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
      app_error("fgets error");
    if (feof(stdin)) { /* End of file (ctrl-d) */
      fflush(stdout);
      exit(0);
    }

    /* Evaluate the command line */
    eval(cmdline);
    fflush(stdout);
    fflush(stdout);
  }

  exit(0); /* control never reaches here */
}

/*
 * eval - Evaluate the command line that the user has just typed in
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */
void eval(char *cmdline) {

  static char *argv[MAXARGS];
  int state = parseline(cmdline, argv);
  ++state; // 0,1->BG,FG

  // BuiltIn Command
  if (builtin_cmd(argv)) {
    return;
  }

  sigset_t mask, prev_mask;
  Sigfillset(&mask);
  Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
  pid_t pid = fork();
  if (pid == 0) {
    Setpgid(0, 0);
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    if (execve(argv[0], argv, environ)) {
      printf("%s:Command not found\n", argv[0]);
    }
    exit(0);
  }
  addjob(JOBS, pid, state, cmdline);

  if (state == BG) {
    printf("[%d] (%d) %s", getjobpid(JOBS, pid)->jid, pid, cmdline);
    fflush(stdout);
  }

  if (state == FG) {
    waitfg(pid);
  }
  Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

/*
 * parseline - Parse the command line and build the argv array.
 *
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.
 */
int parseline(const char *cmdline, char **argv) {
  static char array[MAXLINE]; /* holds local copy of command line */
  char *buf = array;          /* ptr that traverses command line */
  char *delim;                /* points to first space delimiter */
  int argc;                   /* number of args */
  int bg;                     /* background job? */

  strcpy(buf, cmdline);
  buf[strlen(buf) - 1] = ' ';   /* replace trailing '\n' with space */
  while (*buf && (*buf == ' ')) /* ignore leading spaces */
    buf++;

  /* Build the argv list */
  argc = 0;
  if (*buf == '\'') {
    buf++;
    delim = strchr(buf, '\'');
  } else {
    delim = strchr(buf, ' ');
  }

  while (delim) {
    argv[argc++] = buf;
    *delim = '\0';
    buf = delim + 1;
    while (*buf && (*buf == ' ')) /* ignore spaces */
      buf++;

    if (*buf == '\'') {
      buf++;
      delim = strchr(buf, '\'');
    } else {
      delim = strchr(buf, ' ');
    }
  }
  argv[argc] = NULL;

  if (argc == 0) /* ignore blank line */
    return 1;

  /* should the job run in the background? */
  if ((bg = (*argv[argc - 1] == '&')) != 0) {
    argv[--argc] = NULL;
  }
  return bg;
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.
 */
int builtin_cmd(char **argv) {
  int argc = 0;
  while (argv[argc] != NULL) {
    ++argc;
  }
  if (argc == 0) {
    return 1;
  }

  if (strcmp(argv[0], "quit") == 0) {
    exit(0);
  } else if (strcmp(argv[0], "fg") == 0 || strcmp(argv[0], "bg") == 0) {
    if (argc >= 2) {
      do_bgfg(argv);
    } else {
      printf("%s command requires PID or %%job id argument\n",
             strcmp(argv[0], "fg") == 0 ? "fg" : "bg");
      fflush(stdout);
    }
    return 1;
  } else if (strcmp(argv[0], "jobs") == 0) {
    sigset_t mask, prev_mask;
    Sigfillset(&mask);
    Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
    listjobs(JOBS);
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    return 1;
  }

  return 0; /* not a builtin command */
}

/*
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) {
  sigset_t mask, prev_mask;
  Sigfillset(&mask);
  Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
  struct job_t *job;
  char *str = argv[1];
  int is_jid = 0;
  if (str[0] == '%') {
    ++str;
    is_jid = 1;
  }

  int errsv = errno;
  errno = 0;
  char *endptr = str;
  long lid = strtol(str, &endptr, 10);
  int id;
  if (errno != 0 || lid > INT_MAX || lid < INT_MIN) {
    perror("strtol");
    errno = errsv;
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    return;
  }
  id = (int)lid;
  if (endptr == str) {
    printf("%s: argument must be a PID or %%job id\n",
           strcmp(argv[0], "fg") == 0 ? "fg" : "bg");
    errno = errsv;
    Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
    return;
  }
  errno = errsv;
  if (is_jid) {
    job = getjobjid(JOBS, id);
    if (job == NULL) {
      printf("%%%d: No such job\n", id);
      Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
      return;
    }
  } else {
    job = getjobpid(JOBS, id);
    if (job == NULL) {
      printf("(%d): No such process\n", id);
      Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
      return;
    }
  }

  if (strcmp(argv[0], "bg") == 0) {
    job->state = BG;
    printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
    int errsv1 = errno;
    Kill(-job->pid, SIGCONT);
    errno = errsv1;
  } else if (strcmp(argv[0], "fg") == 0) {
    if (job->state != UNDEF) {
      job->state = FG;
      int errsv1 = errno;
      Kill(-job->pid, SIGCONT);
      errno = errsv1;
      int pid = job->pid;

      waitfg(pid);
    }
  }
  Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

/*
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid) {
  // Everytime,entered waitfg,all signals are blocked, so sigsuspend with empty
  // mask can correctly recevive the signal
  sigset_t mask;
  Sigemptyset(&mask);
  struct job_t *job = getjobpid(JOBS, pid);
  while (1) {

    Sigsuspend(&mask);

    if (!job || job->state != FG) {
      break;
    }
  }
}

/*****************
 * Signal handlers
 *****************/

/*
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.
 */
void sigchld_handler(int sig) {
#define writes(str) (Write(STDOUT_FILENO, str, sizeof(str) - 1))
  sigset_t mask, prev_mask;
  Sigfillset(&mask);
  Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
  static char buffer[32] = {0};
  struct job_t *job;
  int exitvalue;
  pid_t pid;
  for (pid = Waitpid(-1, &exitvalue, (WNOHANG | WUNTRACED)); pid > 0;
       pid = Waitpid(-(job->pid), &exitvalue, (WNOHANG | WUNTRACED))) {
    job = getjobpid(JOBS, pid);
    if (WIFEXITED(exitvalue)) {
      deletejob(JOBS, job->pid);
    }
    if (WIFSTOPPED(exitvalue)) {
      job->state = ST;

      char *s;
      writes("Job [");
      s = itoa(job->jid, buffer, 10);
      Write(STDOUT_FILENO, s, slen(s));
      writes("] (");
      s = itoa(job->pid, buffer, 10);
      Write(STDOUT_FILENO, s, slen(s));
      writes(") stopped by signal 20\n");
    }
    if (WIFSIGNALED(exitvalue)) {
      char *s;
      writes("Job [");
      s = itoa(job->jid, buffer, 10);
      Write(STDOUT_FILENO, s, slen(s));
      writes("] (");
      s = itoa(job->pid, buffer, 10);
      Write(STDOUT_FILENO, s, slen(s));
      writes(") terminated by signal 2\n");
      deletejob(JOBS, job->pid);
    }
  }

  Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
#undef writes
}

/*
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.
 */
void sigint_handler(int sig) {
  sigset_t mask, prev_mask;
  Sigfillset(&mask);
  Sigprocmask(SIG_BLOCK, &mask, &prev_mask);
  pid_t pid = fgpid(JOBS);

  if (pid) {
    Kill(-pid, SIGINT);
  }
  Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.
 */
void sigtstp_handler(int sig) {
  sigset_t mask, prev_mask;
  Sigfillset(&mask);
  Sigprocmask(SIG_BLOCK, &mask, &prev_mask);

  pid_t pid = fgpid(JOBS);
  if (pid) {
    Kill(-pid, SIGTSTP);
  }
  Sigprocmask(SIG_SETMASK, &prev_mask, NULL);
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
  job->pid = 0;
  job->jid = 0;
  job->state = UNDEF;
  job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) {
  int i, max = 0;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid > max)
      max = jobs[i].jid;
  return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) {
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == 0) {
      jobs[i].pid = pid;
      jobs[i].state = state;
      jobs[i].jid = nextjid++;
      if (nextjid > MAXJOBS)
        nextjid = 1;
      strcpy(jobs[i].cmdline, cmdline);
      if (verbose) {
        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid,
               jobs[i].cmdline);
      }
      return 1;
    }
  }
  printf("Tried to create too many jobs\n");
  return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) {
  int i;

  if (pid < 1)
    return 0;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid == pid) {
      clearjob(&jobs[i]);
      nextjid = maxjid(jobs) + 1;
      return 1;
    }
  }
  return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].state == FG)
      return jobs[i].pid;
  return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
  int i;

  if (pid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].pid == pid)
      return &jobs[i];
  return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) {
  int i;

  if (jid < 1)
    return NULL;
  for (i = 0; i < MAXJOBS; i++)
    if (jobs[i].jid == jid)
      return &jobs[i];
  return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) {
  int i;

  if (pid < 1)
    return 0;
  for (i = 0; i < MAXJOBS; i++)
    if (JOBS[i].pid == pid) {
      return JOBS[i].jid;
    }
  return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) {
  int i;

  for (i = 0; i < MAXJOBS; i++) {
    if (jobs[i].pid != 0) {
      printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
      switch (jobs[i].state) {
      case BG:
        printf("Running ");
        break;
      case FG:
        printf("Foreground ");
        break;
      case ST:
        printf("Stopped ");
        break;
      default:
        printf("listjobs: Internal error: job[%d].state=%d ", i, jobs[i].state);
      }
      printf("%s", jobs[i].cmdline);
    }
  }
}
/******************************
 * end job list helper routines
 ******************************/

/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) {
  printf("Usage: shell [-hvp]\n");
  printf("   -h   print this message\n");
  printf("   -v   print additional diagnostic information\n");
  printf("   -p   do not emit a command prompt\n");
  exit(1);
}

/*
 * unix_error - unix-style error routine
 */
void unix_error(char *msg) {
  fprintf(stdout, "%s: %s\n", msg, strerror(errno));
  exit(1);
}

/*
 * app_error - application-style error routine
 */
void app_error(char *msg) {
  fprintf(stdout, "%s\n", msg);
  exit(1);
}

/*
 * Signal - wrapper for the sigaction function
 */
handler_t *Signal(int signum, handler_t *handler) {
  struct sigaction action, old_action;

  action.sa_handler = handler;
  Sigemptyset(&action.sa_mask); /* block sigs of type being handled */
  action.sa_flags = SA_RESTART; /* restart syscalls if possible */

  if (sigaction(signum, &action, &old_action) < 0)
    unix_error("Signal error");
  return (old_action.sa_handler);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) {
  printf("Terminating after receipt of SIGQUIT signal\n");
  exit(1);
}
/*
 * Tool Functions
 */
/*
 * slen return the size of s
 */
size_t slen(const char *s) {
  size_t i = 0;
  for (; s[i]; ++i) {
  }
  return i;
}
/*
 * itoa transform int value to string
 */
char *itoa(int value, char *result, int base) {
  // check that the base if valid
  if (base < 2 || base > 36) {
    *result = '\0';
    return result;
  }

  char *ptr = result, *ptr1 = result, tmp_char;
  int tmp_value;

  do {
    tmp_value = value;
    value /= base;
    *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrst"
             "uvwxyz"[35 + (tmp_value - value * base)];
  } while (value);

  // Apply negative sign
  if (tmp_value < 0)
    *ptr++ = '-';
  *ptr-- = '\0';
  while (ptr1 < ptr) {
    tmp_char = *ptr;
    *ptr-- = *ptr1;
    *ptr1++ = tmp_char;
  }
  return result;
}
void Write(int fd, const void *buf, size_t n) {
  int errsv = errno;
  errno = 0;
  write(fd, buf, n);
  if (errno) {
    unix_error("write");
  }
  errno = errsv;
}
void Sigemptyset(sigset_t *sigset) {
  int errsv = errno;
  errno = 0;
  sigemptyset(sigset);
  if (errno) {
    unix_error("sigemptyset");
  }
  errno = errsv;
}
void Sigfillset(sigset_t *sigset) {
  int errsv = errno;
  errno = 0;
  sigfillset(sigset);
  if (errno) {
    unix_error("sigfillset");
  }
  errno = errsv;
}
void Sigprocmask(int how, const sigset_t *sigset, sigset_t *oldset) {
  int errsv = errno;
  errno = 0;
  sigprocmask(how, sigset, oldset);
  if (errno) {
    unix_error("sigprocmask");
  }
  errno = errsv;
}
void Setpgid(pid_t pid, pid_t pgid) {
  int errsv = errno;
  errno = 0;
  setpgid(pid, pgid);
  if (errno) {
    unix_error("setpgid");
  }
  errno = errsv;
}

void Sigsuspend(sigset_t *sigset) {
  int errsv = errno;
  sigsuspend(sigset);
  errno = errsv;
}
pid_t Waitpid(pid_t pid, int *wstatus, int options) {
  int errsv = errno;
  pid_t ret = waitpid(pid, wstatus, options);
  errno = errsv;
  return ret;
}
void Kill(pid_t pid, int sig) {
  int errsv = errno;
  kill(pid, sig);
  errno = errsv;
}
