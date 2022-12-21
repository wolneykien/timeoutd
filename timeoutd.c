/*
		"@(#) timeoutd.c 1.5.1 by Shane Alderton"
			based on:
		"@(#) autologout.c by David Dickson" 
                        updatede by
		"@(#) Dennis Stampfer and Paul Wolneykien"

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

    Thanks to:
    	David Dickson for writing the original autologout.c
    	programme upon which this programme was based.

*/

#define VERSION "1.5.1"

#include    <unistd.h>
#include    <stdlib.h>
#include    <sys/ioctl.h>
#include    <fcntl.h>
#include    <stdio.h>
#include    <signal.h>
#include    <string.h>
#include    <sys/types.h>
#include    <sys/wait.h>
#include    <sys/stat.h>
#include    <utmp.h>
#include    <pwd.h>
#include    <grp.h>
#include    <sys/syslog.h>
#include    <time.h>
#include    <ctype.h>
#include    <termios.h>
#include    <fcntl.h>
#include    <dirent.h>
#include    <getopt.h>
#include    <stdarg.h>

#ifdef WITH_SYSTEMD
#include <systemd/sd-daemon.h>
#endif

#ifdef TIMEOUTDX11
#include <netdb.h>
#include <X11/Xlib.h>
#include <X11/extensions/scrnsaver.h>

#define TIMEOUTD_XSESSION_NONE 0
#define TIMEOUTD_XSESSION_LOCAL 1
#define TIMEOUTD_XSESSION_REMOTE 2
#endif

#define OPENLOG_FLAGS	LOG_CONS|LOG_PID

/* For those systems (SUNOS) which don't define this: */
#ifndef WTMP_FILE
#define WTMP_FILE "/var/log/wtmp"
#endif

#ifdef SUNOS
#define ut_pid ut_time
#define ut_user ut_name
#define SEEK_CUR 1
#define SEEK_END 2
#define SURE_KILL 1

FILE	*utfile = NULL;
#define NEED_UTMP_UTILS
#define NEED_STRSEP
#endif

int opt_foreground = 0;
int opt_verbose = 0;
int opt_debug = 0;

static void print_loglevel(int priority)
{
#ifdef WITH_SYSTEMD
	const char *level = NULL;

	switch (priority)
	{
	case LOG_EMERG:
		level = SD_EMERG;
		break;
	case LOG_ALERT:
		level = SD_ALERT;
		break;
	case LOG_CRIT:
		level = SD_CRIT;
		break;
	case LOG_ERR:
		level = SD_ERR;
		break;
	case LOG_WARNING:
		level = SD_WARNING;
		break;
	case LOG_NOTICE:
		level = SD_NOTICE;
		break;
	case LOG_INFO:
		level = SD_INFO;
		break;
	case LOG_DEBUG:
		level = SD_DEBUG;
		break;
	}

	if (level)
	{
		fprintf(stderr, level);
	}
#endif
}

static void vprintlog(int priority, const char *format, va_list ap)
{
	if (opt_foreground) {
		// TODO: Print with priority.
		if (priority < LOG_NOTICE ||			\
		    opt_verbose && priority < LOG_DEBUG ||	\
		    opt_debug)
		{
			print_loglevel(priority);
			vfprintf(stderr, format, ap);
			fprintf(stderr, "\n");
		}
	} else {
		vsyslog(priority, format, ap);
	}
}

static void printlog(int priority, const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	vprintlog(priority, format, ap);
	va_end(ap);
}

#ifdef NEED_UTMP_UTILS
void setutent()
{
	if (utfile == NULL)
	{
		if ((utfile = fopen("/etc/utmp", "r")) == NULL)
		{
			bailout(1, "Could not open /etc/utmp");
		}
	}
	else fseek(utfile, 0L, 0);
}

struct utmp *getutent()    /* returns next utmp file entry */
{
	static struct utmp	uent;

	while (fread(&uent, sizeof(struct utmp), 1, utfile) == 1)
	{
		if (uent.ut_line[0] != 0 && uent.ut_name[0] != 0) 
			return &uent;
	}
	return (struct utmp *) NULL;
}
#endif

#ifndef linux
#define N_TTY 1
#define N_SLIP 2
#endif

#ifndef CONFIG
#define CONFIG "/etc/timeoutd/timeouts"
#endif

const char* config_filename = CONFIG;

#define MAXLINES 512
#define max(a,b) ((a)>(b)?(a):(b))

#define ACTIVE		1
#define IDLEMAX		2
#define SESSMAX		3
#define DAYMAX		4
#define NOLOGIN		5
#define LOCKOUT		6
/*#define XSESSION	7*/
#define	IDLEMSG		0
#define	SESSMSG		1
#define	DAYMSG		2
#define	LOCKOUTMSG	3
#define	NOLOGINMSG	4

#define KWAIT		5  /* Time to wait after sending a kill signal */

char	*limit_names[] = {"idle", "session", "daily", "nologin"};

char	*daynames[] = {"SU", "MO", "TU", "WE", "TH", "FR", "SA", "WK", "AL", NULL};
char	daynums[] = {   1  ,  2  ,  4  ,  8  ,  16 ,  32 ,  64 ,  62 ,  127, 0};

struct utmp *utmpp;         /* pointer to utmp file entry */
char        *ctime();       /* returns pointer to time string */
struct utmp *getutent();    /* returns next utmp file entry */
void	    shut_down();
void	    read_config();
void	    reread_config();
void        reapchild();
void        free_wtmp();
void        check_idle();
void        read_wtmp();
void        bailout(int status, const char *message, ...);
char	    chk_timeout();
void	    logoff_msg();
void	    killit();
int	    getdisc();
void	    get_day_time(char*);
int	    get_rest_time(char*,int);
int 	    chk_ssh(pid_t pid); /* seppy: check if user is logged in via ssh (we have to
handle that different... ;( */
char	    *getusr(pid_t pid); /*seppy: get the owner of a running process */
void	    segfault(); /* seppy: catch segfault and log them */
int	    chk_xterm(); /* seppy: is it a xterm? */
pid_t	    getcpid(); /* seppy: get the child's pid. Needed for ssh */

#ifdef TIMEOUTDX11
Time	    get_xidle(); /* seppy: how long is user idle? (user,display)*/
int	    chk_xsession(); /* seppy: is it a X-Session? */
void	    killit_xsession(); /* seppy: kill the X-Session*/
#endif


struct ut_list {
	struct utmp	elem;
	struct ut_list	*next;
};

struct ut_list	*wtmplist = (struct ut_list *) NULL;
struct ut_list	*ut_list_p;

struct time_ent {
	int	days;
	int	starttime;
	int	endtime;
};

struct config_ent {
	struct time_ent	*times;
	char	*ttys;
	char	*users;
	char	*groups;
	char	login_allowed;
	int	idlemax;
	int	sessmax;
	int	daymax;
	int	warntime;
	int     lockout;
	char	*messages[NOLOGINMSG + 1];
};

struct config_ent	*config[MAXLINES + 1];
char	errmsg[256];
char	dev[sizeof(utmpp->ut_line)];
unsigned char	limit_type;
int	configline = 0;
int	pending_reread = 0;
int	allow_reread = 0;
time_t	time_now;
struct tm	now;
int		now_hhmm;
int	daytime = 0;	/* Amount of time a user has been on in current day */
char path[255]; /*seppy*/
FILE *proc_file;/*seppy*/
char comm[16]; /*seppy; to save the command of a pid*/

#ifdef NEED_STRCASECMP
int strcasecmp(char *s1, char *s2)
{
	while (*s1 && *s2)
	{
	  if (tolower(*s1) < tolower(*s2))
	  	return -1;
	  else if (tolower(*s1) > tolower(*s2))
	  	return 1;
	  s1++;
	  s2++;
	}
	if (*s1)
		return -1;
	if (*s2)
		return 1;
	return 0;
}
#endif

#ifdef NEED_STRSEP
char *strsep (stringp, delim)
char **stringp;
char *delim;
{
	char	*retp = *stringp;
	char	*p;

	if (!**stringp) return NULL;

	while (**stringp)
	{
		p = delim;
		while (*p)
		{
		  if (*p == **stringp)
		  {
		    **stringp = '\0';
		    (*stringp)++;
		    return retp;
		  }
		  p++;
	  	}
	  	(*stringp)++;
	}
	return retp;
}
#endif

static void print_usage()
{

  fprintf(stderr, "timeoutd [options] [USER TTY]\n");
}

int main(argc, argv)
int	argc;
char	*argv[];
{
    signal(SIGTERM, shut_down);
    signal(SIGHUP, reread_config);
    signal(SIGCHLD, reapchild);
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    signal(SIGSEGV, segfault);


    static const struct option opts[] = {
        {"foreground", no_argument, NULL, 'f'},
        {"verbose", no_argument, NULL, 'v'},
        {"debug", no_argument, NULL, 'd'},
        {"version", no_argument, NULL, 'V'},
        {"config", required_argument, NULL, 'c'},
	{"help",  no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    extern char *optarg;
    extern int optind;
    char c;

    while ((c = getopt_long(argc, argv, "fvdVc:h", opts, NULL)) != -1) {
      switch (c) {
      case 'f':
	opt_foreground = 1;
	break;
      case 'v':
	opt_verbose = 1;
	break;
      case 'd':
	opt_debug = 1;
	break;
      case 'V':
	fprintf(stdout, "Timeoutd v" VERSION "\n\n");
	fprintf(stdout, "timeoutd is a programm which allows you to automatically logout users by idle timeouts. timeoutd is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.\n\n");
	fprintf(stdout, "Originally written by Shane Alderton <shanea@bigpond.net.au>, updated by Dennis Stampfer <seppy@debian.org> and Paul Wolneykien <manowar@altlinux.org>.\n");
	exit(0);
	break;
      case 'c':
	config_filename = optarg;
	break;
      case 'h':
	fprintf(stdout, "timeoutd [options] [USER TTY]\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Options:\n\n");
	fprintf(stdout, "    -f, --foreground    do not fork;\n");
	fprintf(stdout, "    -v, --verbose       be verbose;\n");
	fprintf(stdout, "    -d, --debug         print even debug messages;\n");
	fprintf(stdout, "    -V, --version       print version and copyright information and exit;\n");
	fprintf(stdout, "    -c CONF, --config=CONF    use the specified configuration file;\n");
	fprintf(stdout, "    -h, --help          print this page and exit.\n");
	exit(0);
      default:
	print_usage();
	exit(1);
      }
    }

    /* check for trailing command line following options */
    if ((argc - optind) != 0 && (argc - optind) != 2) {
      print_usage();
      exit(5);
    }

    /* read config file into memory */
    read_config();

/* Change into the root filesystem to avoid "device busy" errors when the
 * filesystem from which we were started is unmounted.  /dev is convenient as
 * ut_line fields are relative to it.
 */
    if (chdir("/dev"))
    {
	bailout(1, "Could not change working directory to /dev!");
    }

    /* Handle the "timeoutd user tty" invocation */
    if (optind < argc)
    {
	printlog(LOG_DEBUG, "Running in user check mode. Checking user %s on %s.", argv[1], argv[2]);

    	strncpy(dev, argv[2], sizeof(dev) - 1);
    	dev[sizeof(dev) - 1] = '\0';
        time_now = time((time_t *)0);  /* get current time */
        now = *(localtime(&time_now));  /* Break it into bits */
        now_hhmm = now.tm_hour * 100 + now.tm_min;
        allow_reread = 0;
        read_wtmp(); /* Read in today's wtmp entries */
        switch(chk_timeout(argv[1], dev, "",  0, 0))
        {
            case DAYMAX:
		printlog(LOG_NOTICE,
		       "User %s on %s exceeded maximum daily limit (%d minutes). Login check failed.",
		       argv[1], argv[2], config[configline]->daymax);
    		/*
    		printf("\r\nLogin not permitted.  You have exceeded your maximum daily limit.\r\n");
    		printf("Please try again tomorrow.\r\n");
    		*/
    		logoff_msg(1);
    		exit(10);
            case NOLOGIN:
		printlog(LOG_NOTICE,
		       "User %s not allowed to login on %s at this time. Login check failed.",
		       argv[1], argv[2]);
    		/*
    		printf("\r\nLogin not permitted at this time.  Please try again later.\r\n");
    		*/
    		logoff_msg(1);
    		exit(20);
	    case LOCKOUT:
		printlog(LOG_NOTICE,
		       "User %s has not had a long enough rest to login on %s at this time. Login check failed.",
		       argv[1], argv[2]);
		logoff_msg(1);
    		exit(20);

            case ACTIVE:
		printlog(LOG_DEBUG, "User %s on %s passed login check.", argv[1], argv[2]);
		free_wtmp();
		exit(0);
            default:
		printlog(LOG_ERR, "Internal error while checking user %s on %s --- unexpected return from chk_timeout().",
			argv[1], argv[2]);
    		exit(30);
        }
    }

    /* If running in daemon mode (no parameters) */
    if (!opt_foreground)
    {
	    if (fork())             /* the parent process */
		    exit(0);        /* exits */

	    close(0);
	    close(1);
	    close(2);
	    setsid();

	    printlog(LOG_NOTICE, "Daemon started.");
    } else {
	    printlog(LOG_NOTICE, "Started.");
    }

    /* the child processes all utmp file entries: */
    while (1)
    {
        /* Record time at which we woke up & started checking */
        time_now = time((time_t *)0);  /* get current time */
        now = *(localtime(&time_now));  /* Break it into bits */
        now_hhmm = now.tm_hour * 100 + now.tm_min;
        allow_reread = 0;
        read_wtmp(); /* Read in today's wtmp entries */
    	setutent();

    	printlog(LOG_DEBUG, "Time to check wtmp for exceeded limits.");

        while ((utmpp = getutent()) != (struct utmp *) NULL)
            check_idle();
        free_wtmp();  /* Free up memory used by today's wtmp entries */
        allow_reread = 1;
        if (pending_reread)
           reread_config(SIGHUP);

        printlog(LOG_DEBUG, "Finished checking wtmp... sleeping for 1 minute.");
        sleep(60);
    }
}

/* Read in today's wtmp entries */

void read_wtmp()
{
    FILE	*fp;
    struct utmp ut;
    struct tm	*tm;

    printlog(LOG_DEBUG, "Reading today's wtmp entries.");

    if ((fp = fopen(WTMP_FILE, "r")) == NULL)
      bailout(1, "Could not open wtmp file %s", WTMP_FILE);

    printlog(LOG_DEBUG, "Seek to end of wtmp.");

    /* Go to end of file minus one structure */
    fseek(fp, -1L * sizeof(struct utmp), SEEK_END);

    while (fread(&ut, sizeof(struct utmp), 1, fp) == 1)
    {
      /* tm = localtime(&ut.ut_time); */
      time_t log_time = ut.ut_time;
      tm = localtime(&log_time);

      if (tm->tm_year != now.tm_year || tm->tm_yday != now.tm_yday)
        break;

#ifndef SUNOS
      if (ut.ut_type == USER_PROCESS ||
          ut.ut_type == DEAD_PROCESS ||
          ut.ut_type == UT_UNKNOWN ||	/* SA 19940703 */
	  ut.ut_type == LOGIN_PROCESS ||
          ut.ut_type == BOOT_TIME)
#endif
      {
        if ((ut_list_p = (struct ut_list *) malloc(sizeof(struct ut_list))) == NULL)
	{
	    bailout(1, "Out of memory in read_wtmp");
	}

        ut_list_p->elem = ut;
        ut_list_p->next = wtmplist;
        wtmplist = ut_list_p;
      }

      /* Position the file pointer 2 structures back */
      if (fseek(fp, -2 * sizeof(struct utmp), SEEK_CUR) < 0) break;
    }

    fclose(fp);
    printlog(LOG_DEBUG, "Finished reading today's wtmp entries.");
}

/* Free up memory used by today's wtmp entries */

void free_wtmp()
{
    printlog(LOG_DEBUG, "Freeing list of today's wtmp entries...");

    while (wtmplist)
    {
	struct tm	*tm;
	tm = localtime(&(wtmplist->elem.ut_time));
	printlog(LOG_DEBUG, "%d:%d %s %s %s",
		 tm->tm_hour,tm->tm_min, wtmplist->elem.ut_line,
		 wtmplist->elem.ut_user,
#ifndef SUNOS
		 wtmplist->elem.ut_type == LOGIN_PROCESS?"login":wtmplist->elem.ut_type == BOOT_TIME?"reboot":"logoff"
#else
		 ""
#endif
	    );

        ut_list_p = wtmplist;
        wtmplist = wtmplist->next;
        free(ut_list_p);
    }

    printlog(LOG_DEBUG, "Finished freeing list of today's wtmp entries.");
}

void	store_times(t, time_str)
struct time_ent **t;
char *time_str;
{
    int	i = 0;
    int	ar_size = 2;
    char	*p;
    struct time_ent	*te;

    while (time_str[i])
      if (time_str[i++] == ',')
        ar_size++;

    if ((*t = (struct time_ent *) malloc (ar_size * sizeof(struct time_ent))) == NULL)
      bailout(1, "Out of memory");
    te = *t;

    p = strtok(time_str, ",");
/* For each day/timerange set, */
    while (p)
    {
/* Store valid days */
      te->days = 0;
      while (isalpha(*p))
      {
        if (!p[1] || !isalpha(p[1]))
        {
          printlog(LOG_ERR, "Malformed day name (%c%c) in time field of config file (%s).  Entry ignored.", p[0], p[1], config_filename);
          (*t)->days = 0;
          return;
        }
        *p = toupper(*p);
        p[1] = toupper(p[1]);

      	i = 0;
      	while (daynames[i])
      	{
      	  if (!strncmp(daynames[i], p, 2))
      	  {
      	    te->days |= daynums[i];
      	    break;
      	  }
      	  i++;
      	}
      	if (!daynames[i])
      	{
          printlog(LOG_ERR, "Malformed day name (%c%c) in time field of config file (%s).  Entry ignored.", p[0], p[1], config_filename);
          (*t)->days = 0;
          return;
      	}
      	p += 2;
      }

/* Store start and end times */
      if (*p)
      {
        if (strlen(p) != 9 || p[4] != '-')
        {
          printlog(LOG_ERR, "Malformed time (%s) in time field of config file (%s).  Entry ignored.", p, config_filename);
          (*t)->days = 0;
          return;
        }
        te->starttime = atoi(p);
        te->endtime = atoi(p+5);
        if ((te->starttime == 0 && strncmp(p, "0000-", 5)) || (te->endtime == 0 && strcmp(p+5, "0000")))
        {
          printlog(LOG_ERR, "Invalid range (%s) in time field of config file (%s).  Entry ignored.", p, config_filename);
          (*t)->days = 0;
          return;
        }
      }
      else
      {
      	te->starttime = 0;
      	te->endtime = 2359;
      }
      p = strtok(NULL, ",");
      te++;
    }
    te->days = 0;
}

void alloc_cp(a, b)
char **a;
char *b;
{
	if ((*a = (char *) malloc(strlen(b)+1)) == NULL)
		bailout(1, "Out of memory");
	else	strcpy(*a, b);
}

void read_config()
{
    FILE	*config_file;
    char	*p;
    char	*lstart;
    int		i = 0;
    int		j = 0;
    char	line[256];
    char	*tok;
    int		linenum = 0;

    if ((config_file = fopen(config_filename, "r")) == NULL)
	    bailout(1, "Cannot open configuration file %s",	\
		    config_filename);

    printlog(LOG_NOTICE, "Using configuration file %s", config_filename);

    while (fgets(line, 256, config_file) != NULL)
    {
      linenum++;
      p = line;
      while  (*p && (*p == ' ' || *p == '\t'))
        p++;
      lstart = p;
      while (*p && *p != '#' && *p != '\n')
        p++;
      *p = '\0';
      if (*lstart)
      {
      	if (i == MAXLINES)
		bailout(1, "Too many lines in timeouts config file");
        if ((config[i] = (struct config_ent *) malloc(sizeof(struct config_ent))) == NULL)
		bailout(1, "Out of memory");
  	config[i]->times = NULL;
  	config[i]->ttys = NULL;
  	config[i]->users = NULL;
  	config[i]->groups = NULL;
  	config[i]->login_allowed = 1;
  	config[i]->idlemax = -1;
  	config[i]->sessmax = -1;
  	config[i]->daymax = -1;
  	config[i]->warntime = 5;
	config[i]->lockout = -1;
  	config[i]->messages[IDLEMSG] = NULL;
  	config[i]->messages[SESSMSG] = NULL;
  	config[i]->messages[DAYMSG] = NULL;
  	config[i]->messages[LOCKOUTMSG] = NULL;
  	config[i]->messages[NOLOGINMSG] = NULL;
	if ((tok = strsep(&lstart, ":")) != NULL) store_times(&config[i]->times, tok);
	if ((tok = strsep(&lstart, ":")) != NULL) alloc_cp(&config[i]->ttys, tok);
	if ((tok = strsep(&lstart, ":")) != NULL) alloc_cp(&config[i]->users, tok);
	if ((tok = strsep(&lstart, ":")) != NULL) alloc_cp(&config[i]->groups, tok);
	tok = strsep(&lstart, ":");
	if (tok != NULL && !strncasecmp(tok, "NOLOGIN", 7))
	{
		config[i]->login_allowed=0;
		if (tok[7] == ';') alloc_cp(&config[i]->messages[NOLOGINMSG], tok+8);
		else if ((tok = strsep(&lstart, ":")) != NULL) alloc_cp(&config[i]->messages[NOLOGINMSG], tok);
	}
	else
	if (tok != NULL && !strcasecmp(tok, "LOGIN")) config[i]->login_allowed=1;
	else
	{
		if (tok != NULL)
		{
		    config[i]->idlemax = atoi(tok);
		    if ((p = strchr(tok, ';')) != NULL) alloc_cp(&config[i]->messages[IDLEMSG], p+1);
		}
		if ((tok = strsep(&lstart, ":")) != NULL)
		{
		    config[i]->sessmax = atoi(tok);
		    if ((p = strchr(tok, ';')) != NULL) alloc_cp(&config[i]->messages[SESSMSG], p+1);
	    	}
		if ((tok = strsep(&lstart, ":")) != NULL)
		{
		    config[i]->daymax = atoi(tok);
		    if ((p = strchr(tok, ';')) != NULL) alloc_cp(&config[i]->messages[DAYMSG], p+1);
		}
		if ((tok = strsep(&lstart, ":")) != NULL)
		{
		    config[i]->warntime = atoi(tok);
		}
		if ((tok = strsep(&lstart, ":")) != NULL)
		{
		    config[i]->lockout = atoi(tok);
		    if ((p = strchr(tok, ';')) != NULL) alloc_cp(&config[i]->messages[LOCKOUTMSG], p+1);
		}
	}
	if (!config[i]->times || !config[i]->ttys  ||
	    !config[i]->users || !config[i]->groups)
        {
        	printlog(LOG_ERR,
        		"Error on line %d of config file (%s).  Line ignored.",
        		linenum, config_filename);
	}
	else i++;
      }
    }
    config[i] = NULL;

    if (fclose(config_file) == EOF)
	    bailout(1, "Cannot close the config file");

    printlog(LOG_DEBUG, "Current configuration:");

    i = 0;
    while (config[i])
    {
	    printlog(LOG_DEBUG, "line %d: ", i);
	    j = 0;
	    while (config[i]->times[j].days)
		    printlog(LOG_DEBUG, "%d(%d-%d):",
			     config[i]->times[j].days,
			     config[i]->times[j].starttime,
			     config[i]->times[j].endtime),j++;
	    printlog(LOG_DEBUG, "%s:%s:%s:%s:%d;%s:%d;%s:%d;%s:%d:%d;%s",
		     config[i]->ttys,
		     config[i]->users,
		     config[i]->groups,
		     config[i]->login_allowed?"LOGIN":"NOLOGIN",
		     config[i]->idlemax,
		     config[i]->messages[IDLEMSG] == NULL?"builtin":config[i]->messages[IDLEMSG],
		     config[i]->sessmax,
		     config[i]->messages[SESSMSG] == NULL?"builtin":config[i]->messages[SESSMSG],
		     config[i]->daymax,
		     config[i]->messages[DAYMSG] == NULL?"builtin":config[i]->messages[DAYMSG],
		     config[i]->warntime,
		     config[i]->lockout,
		     config[i]->messages[LOCKOUTMSG] == NULL?"builtin":config[i]->messages[LOCKOUTMSG]
		    ), i++;
    }
}

char chktimes(te)
struct time_ent *te;
{
    while (te->days)
    {
        if (daynums[now.tm_wday] & te->days &&	/* Date within range */
              ((te->starttime <= te->endtime &&	/* Time within range */
               now_hhmm >= te->starttime &&	
               now_hhmm <= te->endtime)
               ||
               (te->starttime > te->endtime &&
               (now_hhmm >= te->starttime ||
                now_hhmm <= te->endtime))
              )
           )
               return 1;
        te++;
    }
    return 0;
}

char chkmatch(element, in_set)
char *element;
char *in_set;
{
    char	*t;
    char	*set = (char *) malloc(strlen(in_set) + 1);

    if (set == NULL) bailout(1, "Out of memory");
    else strcpy(set, in_set);

    t = strtok(set, " ,");
    while (t)
    {
        if (t[strlen(t)-1] == '*')
        {
            if (!strncmp(t, element, strlen(t) - 1))
            {
              free(set);
              return 1;
	    }
	}
        else if (!strcmp(t, element))
        {
          free(set);
          return 1;
	}
        t = strtok(NULL, " ,");
    }
    free(set);
    return 0;
}

/* Return the number of minutes which user has been logged in for on
 * any of the ttys specified in config[configline] during the current day.
 */

void get_day_time(user)
char *user;
{
    struct ut_list	*login_p;
    struct ut_list	*logout_p;
    struct ut_list	*prev_p;

    daytime = 0;
    login_p = wtmplist;
    while (login_p)
    {
        /* For each login on a matching tty find its logout */
        if (
#ifndef SUNOS
	    login_p->elem.ut_type == USER_PROCESS &&
#endif
            !strncmp(login_p->elem.ut_user, user, 8) &&
            chkmatch(login_p->elem.ut_line, config[configline]->ttys))
        {

	    struct tm	*tm;
	    tm = localtime(&(login_p->elem.ut_time));
	    printlog(LOG_DEBUG, "%d:%d %s %s %s",
		     tm->tm_hour,tm->tm_min, login_p->elem.ut_line,
		     login_p->elem.ut_user,
		     "login");

	    prev_p = logout_p = login_p->next;
	    while (logout_p)
	    {
/*
 * SA19931128
 * If there has been a crash, then be reasonably fair and use the
 * last recorded login/logout as the user's logout time.  This will
 * potentially allow them slightly more online time than usual,
 * but is better than marking them as logged in for the time the machine
 * was down.
 */
#ifndef SUNOS
                if (logout_p->elem.ut_type == BOOT_TIME)
                {
                      logout_p = prev_p;
                      break;
	        }
#endif
                if (/*logout_p->elem.ut_type == DEAD_PROCESS &&*/
                  !strcmp(login_p->elem.ut_line, logout_p->elem.ut_line))
                      break;
                prev_p = logout_p;
                logout_p = logout_p->next;
	    }

	    if (logout_p)
	    {
		tm = localtime(&(logout_p->elem.ut_time));
		printlog(LOG_DEBUG, "%d:%d %s %s %s",
			 tm->tm_hour,tm->tm_min, logout_p->elem.ut_line,
			 logout_p->elem.ut_user, "logout");
		printlog(LOG_DEBUG, "%s %d minutes", user, ((logout_p?logout_p->elem.ut_time:time_now) - login_p->elem.ut_time)/60);
	    }

	    daytime += (logout_p?logout_p->elem.ut_time:time_now) - login_p->elem.ut_time;
        }
        login_p = login_p->next;
    }
    daytime /= 60;

    printlog(LOG_DEBUG, "%s has been logged in for %d minutes today.", user, daytime);
    return;
}

/* Return the number of minutes since user has logged out of a session of length min
 * on any of the ttys specified in config[configline] during the current day.
 */

int get_rest_time(user,min)
char *user;
int min;
{
    struct ut_list	*login_p = NULL;
    struct ut_list	*logout_p = NULL;
    struct ut_list	*prev_p = NULL;
    struct ut_list      *test_login_p, *test_logout_p;

    test_login_p = wtmplist;
    while (test_login_p)
      { 
        /* Find most recent login on a matching tty */
        if (
#ifndef SUNOS
	    test_login_p->elem.ut_type == USER_PROCESS &&
#endif
            !strncmp(test_login_p->elem.ut_user, user, 8) &&
            chkmatch(test_login_p->elem.ut_line, config[configline]->ttys) &&
	    (login_p == NULL || test_login_p->elem.ut_time >= login_p->elem.ut_time))
	  {
	    prev_p =  test_login_p;
	    test_logout_p=test_login_p->next;
	    /* Search from next to find the matching logout */
	    while (test_logout_p)
	      {
		/* Ignore sessions that are less than the given length.*/
		if ( (test_logout_p->elem.ut_time - test_login_p->elem.ut_time)/60 < min)
		  break;
#ifndef SUNOS
		if (test_logout_p->elem.ut_type == BOOT_TIME)
		  {
		    logout_p = prev_p;
		    break;
		  }
#endif
		if (/*test_logout_p->elem.ut_type == DEAD_PROCESS &&*/
		    !strcmp(test_login_p->elem.ut_line, test_logout_p->elem.ut_line)) /* match */
		  {
	    	    login_p =  test_login_p;
		    logout_p = test_logout_p;
		    break;
		  } else { /* no match */
		  prev_p = test_logout_p;
		  test_logout_p = test_logout_p->next;
		}
	      }
	  }
	test_login_p = test_login_p->next;
      }
    if (logout_p)
      return (time_now - logout_p->elem.ut_time)/60;
    else
      return -1;
}

void warnpending(tty, time_remaining, user, host)
char *tty;
int time_remaining;
char *user;
char *host;
{
    int		fd;
    FILE	*ttyf;
#ifdef TIMEOUTDX11
    char 	cmdbuf[1024];
#endif

    printlog(LOG_DEBUG, "Warning %s@%s on %s of pending logoff in %d minutes.",
	     user, host, tty, time_remaining);

#ifdef TIMEOUTDX11
    if(chk_xsession(tty, host)) {
    	printlog(LOG_DEBUG, "Warning %s running X on %s for pending logout! (%d min%s left)", user, tty, time_remaining, time_remaining==1?"":"s");

  	/* then send the message using xmessage */
  	/* well, this is not really clean: */
	sprintf(cmdbuf, "su %s -c \"xmessage -default okay -display %s -center 'WARNING: You will be logged out in %d minute%s when your %s limit expires.'&\"", user, host, time_remaining, time_remaining==1?"":"s", limit_names[limit_type]);
	system(cmdbuf);

	printlog(LOG_DEBUG, "cmdbuf=%s", cmdbuf);

	sleep(KWAIT); /* and give the user some time to read the message ;) */
	return;
    }
#endif

    if ((fd = open(tty, O_WRONLY|O_NOCTTY|O_NONBLOCK)) < 0 ||
	(ttyf = fdopen(fd, "w")) == NULL)
    {
        printlog(LOG_ERR, "Could not open %s to warn of impending logoff.",tty);
        return;
    }
    fprintf(ttyf, "\r\nWARNING:\r\nYou will be logged out in %d minute%s when your %s limit expires.\r\n",
    	time_remaining, time_remaining==1?"":"s", limit_names[limit_type]);
    fclose(ttyf);
}

char chk_timeout(user, dev, host, idle, session)
char *user;
char *dev;
char *host;
int idle;
int session;
{
    struct passwd	*pw;
    struct group	*gr;
    struct group	*secgr;
    char	timematch = 0;
    char	ttymatch = 0;
    char	usermatch = 0;
    char	groupmatch = 0;
    char	*tty = dev;
    char	**p;
    int		disc;

    configline = 0;

/* Find primary group for specified user */
    if ((pw = getpwnam(user)) == NULL)
    {
      printlog(LOG_ERR, "Could not get password entry for %s.", user);
      return 0;
    }
    if ((gr = getgrgid(pw->pw_gid)) == NULL)
    {
      printlog(LOG_ERR, "Could not get group name for %s.", user);
      return 0;
    }

    printlog(LOG_DEBUG, "Checking user %s group %s tty %s.", user, gr->gr_name, tty);

/* Check to see if current user matches any entry based on tty/user/group */
    while (config[configline])
    {
    	timematch = chktimes(config[configline]->times);
        ttymatch = chkmatch(tty, config[configline]->ttys);
        usermatch = chkmatch(user, config[configline]->users);
        groupmatch = chkmatch(gr->gr_name, config[configline]->groups);
/* If the primary group doesn't match this entry, check secondaries */
	setgrent();
	while (!groupmatch && (secgr = getgrent()) != NULL)
	{
	    p = secgr->gr_mem;
	    while (*p && !groupmatch)
	    {
/*
printf("Group %s member %s\n", secgr->gr_name, *p);
*/
	    	if (!strcmp(*p, user))
	    	    groupmatch = chkmatch(secgr->gr_name, config[configline]->groups);
	    	p++;
	    }
/*
	    free(gr);
*/
	}
/*
	endgrent();
*/
/* If so, then check their idle, daily and session times in turn */
        if (timematch && ttymatch && usermatch && groupmatch)
        {
          get_day_time(user);

	  printlog(LOG_DEBUG, "Matched entry %d", configline);
	  printlog(LOG_DEBUG, "Idle=%d (max=%d) Sess=%d (max=%d) Daily=%d (max=%d) warntime=%d", idle, config[configline]->idlemax, session, config[configline]->sessmax, daytime, config[configline]->daymax, config[configline]->warntime);

	  disc = getdisc(dev, host);

	  limit_type = NOLOGINMSG;
	  if (!config[configline]->login_allowed)
	  	return NOLOGIN;

	  limit_type = IDLEMSG;
	  if (disc == N_TTY && config[configline]->idlemax > 0 && idle >= config[configline]->idlemax)
	  	return IDLEMAX;

	  limit_type = SESSMSG;
	  if (config[configline]->sessmax > 0 && session >= config[configline]->sessmax)
	  	return SESSMAX;

	  limit_type = DAYMSG;
	  if (config[configline]->daymax > 0 && daytime >= config[configline]->daymax)
	  	return DAYMAX;

	  limit_type = LOCKOUTMSG;
	  if (config[configline]->lockout > 0 && config[configline]->sessmax > 0)
	    {
	      /* Treat sessions that logout during the warntime period as full length.
	       * This might be a bit tough, but it picks up people who logout just before the
	       * session expires in the hope they can login immediately
	       */
	      int rested = get_rest_time(user,config[configline]->sessmax - config[configline]->warntime);
	      if (rested > -1 && rested < config[configline]->lockout)
		return LOCKOUT;
	    }

/* If none of those have been exceeded, then warn users of upcoming logouts */
	  limit_type = DAYMSG;
	  if (config[configline]->daymax > 0 && daytime >= config[configline]->daymax - config[configline]->warntime)
	  	warnpending(dev, config[configline]->daymax - daytime, user, host);
	  else
	  {
	    limit_type = SESSMSG;
	    if (config[configline]->sessmax > 0 && session >= config[configline]->sessmax - config[configline]->warntime)
		warnpending(dev, config[configline]->sessmax - session, user, host);
	  }

/* Otherwise, leave the poor net addict alone */
          return ACTIVE;
        }
        configline++;
    }

/* If they do not match any entries, then they can stay on forever */
    return ACTIVE;
}

void check_idle()    /* Check for exceeded time limits & logoff exceeders */
{
    char        user[sizeof(utmpp->ut_user)];
    char	host[sizeof(utmpp->ut_host)];
    struct stat status, *pstat;
    time_t	idle, sesstime, time();
    short aktconfigline = -1; /* -1 if user is in config; >0 if he's not in config, * is handled in an other way */

    pstat = &status;    /* point to status structure */
#ifndef SUNOS
    sprintf(path, "/proc/%d", utmpp->ut_pid);
    if (utmpp->ut_type != USER_PROCESS || !utmpp->ut_user[0] || /* if not user process */
	stat(path, pstat))					/* or if proc doesn't exist */
        return;                      /* skip the utmp entry */
#endif
    strncpy(user, utmpp->ut_user, sizeof(user) - 1);   /* get user name */
    user[sizeof(user) - 1] = '\0';   /* null terminate user name string */


    /* Only check user if he is mentioned in the config */
    
    if(!config[0])
    	return; /* no entries in config */
    while(config[++aktconfigline] && aktconfigline >= 0)
    	if(strcmp(config[aktconfigline]->users, user) == 0 || config[aktconfigline]->users[0] == '*') {
    		aktconfigline = -2; /* we found user or * in config, so he/they has/have restrictions */
		break;
	}
	
    if(aktconfigline > 0) { /* > 0 if user is not in config */
	printlog(LOG_DEBUG, "User %s or * not in config -> No restrictions. Not checking %s on %s", user, user, dev);
	return; /* now, we return because the user beeing checked is not in config, so he has no restrictions */
    }

    strncpy(host, utmpp->ut_host, sizeof(host) - 1);	/* get host name */
    host[sizeof(host) - 1] = '\0';
    strncpy(dev, utmpp->ut_line, sizeof(dev) - 1);    /* get device name */
    dev[sizeof(dev) - 1] = '\0';
    sprintf(path, "/dev/%s", dev);

    if (stat(dev, pstat) /* if can't get status for port */
#ifdef TIMEOUTDX11
        && !(chk_xsession(dev, host) == TIMEOUTD_XSESSION_LOCAL)    /* && if it's not a local Xsession */
#endif
    )
    {
        sprintf(errmsg, "Can't get status of user %s's terminal (%s)\n",
        	user, dev);
	/* bailout(1, errmsg); MOH: is there a reason to exit here? */
	return; 
    }
    /* idle time is the lesser of:
     * current time less last access time OR
     * current time less last modified time
     */
#ifdef TIMEOUTDX11	
    if(chk_xsession(dev, host) && !chk_xterm(dev, host)) { /* check idle for Xsession, but not for xterm */
    	idle = get_xidle(user, host) / 1000 / 60; /* get_xidle returns millisecs, we need mins */
	printlog(LOG_DEBUG, "get_xidle(%s,%s) returned %d mins idle for %s.", dev, host, (int)idle, user);
    }
    else if (chk_xterm(dev, host)) return;
    else
#endif
    	idle = (time_now - max(pstat->st_atime, pstat->st_mtime)) / 60;

    sesstime = (time_now - utmpp->ut_time) / 60;
    switch(chk_timeout(user, dev, host, idle, sesstime))
    {
    	case ACTIVE:
		printlog(LOG_DEBUG, "User %s is active.", user);
    		break;
    	case IDLEMAX:
    		printlog(LOG_NOTICE,
    		       "User %s exceeded idle limit (idle for %ld minutes, max=%d).\n",
    		       user, idle, config[configline]->idlemax);
		killit(utmpp->ut_pid, user, dev, host);
    		break;
    	case SESSMAX:
		printlog(LOG_NOTICE,
		       "User %s exceeded maximum session limit at %s (on for %ld minutes, max=%d).\n",
		       user, dev, sesstime, config[configline]->sessmax);
		killit(utmpp->ut_pid, user, dev, host);
    		break;
    	case DAYMAX:
		printlog(LOG_NOTICE,
		       "User %s exceeded maximum daily limit (on for %d minutes, max=%d).\n",
		       user, daytime, config[configline]->daymax);
		killit(utmpp->ut_pid, user, dev, host);
    		break;
	case NOLOGIN:
		printlog(LOG_NOTICE, "NOLOGIN period reached for user %s@%s. (PID %d)", user, host, utmpp->ut_pid);
		killit(utmpp->ut_pid, user, dev, host);
		break;
        case LOCKOUT:
		printlog(LOG_NOTICE, "User %s@%s logged in during LOCKOUT period. (PID %d)", user, host, utmpp->ut_pid);
		killit(utmpp->ut_pid, user, dev, host);
		break;
	default:
		printlog(LOG_ERR, "Internal error - unexpected return from chk_timeout");
    }
}

/* display error message and exit */
void bailout(int status, const char *message, ...)
{
	va_list ap;

	va_start(ap, message);
	vprintlog(LOG_ERR, message, ap);
	printlog(LOG_ERR, "Exiting...");
	va_end(ap);
	exit(status);
}

void shut_down(signum)
int signum;
{
    printlog(LOG_NOTICE, "Received SIGTERM.. exiting.");
    exit(0);
}

void segfault(signum)
int signum;
{
    printlog(LOG_NOTICE, "Received SIGSEGV.. Something went wrong! Exiting!");
    exit(100);
}

void logoff_msg(tty)
int tty;
{
    FILE	*msgfile = NULL;
    char	msgbuf[1024];
    int		cnt;

    if (config[configline]->messages[limit_type])
      {
    	msgfile = fopen(config[configline]->messages[limit_type], "r");
	if (msgfile)
	  {
	    while ((cnt = read(fileno(msgfile), msgbuf, sizeof(msgbuf))) > 0)
	      write(tty, msgbuf, cnt);
	    fclose(msgfile);
	    return;
	  } else {
	  snprintf(msgbuf, sizeof(msgbuf), "\r\n%s\r\n", config[configline]->messages[limit_type]);
	}
      }	else {
      switch (limit_type)
	{
	case NOLOGINMSG:
	  sprintf(msgbuf, "\r\n\r\nLogins not allowed at this time.  Please try again later.\r\n");
	  break;
	case LOCKOUTMSG:
	  sprintf(msgbuf, "\r\n\r\nYou have logged in during your lockout time. Logging you off now.\r\n\r\n");
	  break;
    	default:
	  sprintf(msgbuf, "\r\n\r\nYou have exceeded your %s time limit.  Logging you off now.\r\n\r\n", limit_names[limit_type]);
	}
    }
    write(tty, msgbuf, strlen(msgbuf));
}

/* terminate process using SIGHUP, then SIGKILL */
void killit(pid, user, dev, host)
int pid;
char *user;
char *dev;
char *host;
{
    printlog(LOG_NOTICE, "Attempt to kill session for user %s@%s@%s (PID: %d)",
	     user, host, dev, pid);

    int	tty;
    pid_t cpid;
#ifdef SUNOS
   struct passwd	*pw;
#endif
#ifdef TIMEOUTDX11
    if(chk_xsession(dev, host) && !chk_xterm(dev, host)) {
	killit_xsession(utmpp->ut_pid, user, host);
    	return;
    }
#endif
/* Tell user which limit they have exceeded and that they will be logged off */
    if ((tty = open(dev, O_WRONLY|O_NOCTTY|O_NONBLOCK)) < 0)
    {
        printlog(LOG_ERR, "Could not write logoff message to %s.", dev);
	return;
    }

	/* check if the pid is sshd. If so, get PID of the child process (another ssh, owned by the user).
	   Test reverse if this child process is also ssh and owned by the user we want to log out.
	   (because we don't want to slay another user ;) */
	cpid = getcpid(pid);

	printlog(LOG_DEBUG, "Killing pid=%d user=%s child=%d...", pid, user, cpid);

	if(chk_ssh(pid) && chk_ssh(cpid) && !strcmp(getusr(cpid), user)) {
	    printlog(LOG_DEBUG, "User %s (PID: %d, CPID: %d) is logged in via SSH from %s.", user, pid, cpid, host);
	    pid = cpid;
	}

    logoff_msg(tty);
    sleep (KWAIT); /*make sure msg does not get lost, again (esp. ssh)*/
    close(tty);

    if (fork())             /* the parent process */
        return;            /* returns */

/* Wait a little while in case the above message gets lost during logout */
#ifdef SURE_KILL
    signal(SIGHUP, SIG_IGN);
    if ((pw = getpwnam(user)) == NULL)
    {
        printlog(LOG_ERR, "Could not log user %s off line %s --- unable to determine uid.", user, dev);
    }
    if (setuid(pw->pw_uid))
    {
        printlog(LOG_ERR, "Could not log user %s off line %s --- unable to setuid(%d).", user, dev, pw->pw_uid);
    }

    printlog(LOG_DEBUG, "Sending SIGHUP to ALL user processes..."),
	kill(-1, SIGHUP);
    sleep(KWAIT);

    printlog(LOG_DEBUG, "Sending SIGKILL to ALL user processes..."),
	kill(-1, SIGKILL);
#else
    printlog(LOG_DEBUG, "Sending SIGHUP to %d..."),
	kill(pid, SIGHUP);  /* first send "hangup" signal */
    sleep(KWAIT);

    if (!kill(pid, 0)) {    /* SIGHUP might be ignored */
	printlog(LOG_DEBUG, "Sending SIGKILL to %d..."),
	    kill(pid, SIGKILL); /* then send sure "kill" signal */
        sleep(KWAIT);
        if (!kill(pid, 0))
        {
            printlog(LOG_ERR, "Could not log user %s off line %s.", user, dev);
        }
    }
#endif
    exit(0);
}

void reread_config(signum)
int signum;
{
    int i = 0;

    if (!allow_reread)
        pending_reread = 1;
    else
    {
        pending_reread = 0;
        printlog(LOG_NOTICE, "Re-reading configuration file.");
        while (config[i])
        {
            free(config[i]->times);
            free(config[i]->ttys);
            free(config[i]->users);
            free(config[i]->groups);
            if (config[i]->messages[IDLEMSG]) free(config[i]->messages[IDLEMSG]);
            if (config[i]->messages[DAYMSG]) free(config[i]->messages[DAYMSG]);
            if (config[i]->messages[SESSMSG]) free(config[i]->messages[SESSMSG]);
            if (config[i]->messages[LOCKOUTMSG]) free(config[i]->messages[LOCKOUTMSG]);
            if (config[i]->messages[NOLOGINMSG]) free(config[i]->messages[NOLOGINMSG]);
            free(config[i]);
            i++;
        }
        read_config();
    }
    signal(SIGHUP, reread_config);
}

void reapchild(signum)
int signum;
{
    int st;

    wait(&st);
    signal(SIGCHLD, reapchild);
}

int getdisc(d, host)
char *d;
char *host;
{
    int	fd;
    int	disc;

#ifdef linux
    if(
#ifdef TIMEOUTDX11
       chk_xsession(d, host) || 
#endif
       chk_xterm(d, host))
    	return N_TTY;
	
    if ((fd = open(d, O_RDONLY|O_NONBLOCK|O_NOCTTY)) < 0)
    {
      printlog(LOG_WARNING, "Could not open %s for checking line discipline - idle limits will be enforced.", d);
      return N_TTY;
    }

    if (ioctl(fd, TIOCGETD, &disc) < 0)
    {
      close(fd);
      printlog(LOG_WARNING, "Could not get line discipline for %s - idle limits will be enforced.", d);
      return N_TTY;
    }

    close(fd);

    printlog(LOG_DEBUG, "TTY %s: Discipline=%s.",d,disc==N_SLIP?"SLIP":disc==N_TTY?"TTY":disc==N_PPP?"PPP":disc==N_MOUSE?"MOUSE":"UNKNOWN");

    return disc;
#else
    return N_TTY;
#endif
}

#ifdef TIMEOUTDX11
int chk_xsession(dev, host) /* returns TIMEOUTD_XSESSION_{REMOTE,LOCAL,NONE} when dev and host seem to be a xSession. */
char *dev,*host;
{
    if( strncmp(host, ":0", 1) == 0 ) {
      /* Look here, how we check if it's a Xsession but no telnet or whatever.
       * The problem is that a xterm running on :0 has the device pts/?.  But if we ignore
       * all pts/?, ssh users won't be restricted.  
       * So, if (tty="pts/?" OR tty=":*") AND host = ":*", we have a Xsession:
       * 
       * seppy@schleptop:~$ w
       * 20:06:33 up 18 min,  6 users,  load average: 0.14, 0.16, 0.12
       * USER     TTY      FROM             LOGIN@   IDLE   JCPU   PCPU  WHAT
       * dennis   :0       -                19:48   ?xdm?   0.00s   ?     -
       * dennis   pts/1    :0.0             20:00    4:12   0.03s  0.03s  bash 
       * dennis   pts/2    :0.0             20:01    0.00s  0.18s  0.16s  ssh localhost 
       * dennis   pts/3    localhost        20:01    0.00s  0.01s  0.00s  w
       */       
	printlog(LOG_DEBUG, "A LOCAL X-session detected for device=%s host=%s", dev, host);
	return TIMEOUTD_XSESSION_LOCAL;
    }
    else if (strstr(dev, ":") && strlen(host) > 1 && gethostbyname(host)) {
      /* What about remote XDMCP sessions?
       * USER     TTY      FROM              LOGIN@   IDLE   JCPU   PCPU WHAT
       * mark     pts/3    mercury           Sat11    0.00s 10.99s  0.04s w
       * rebecca  ap:10    ap                10:32    0.00s  0.00s  1.28s x-session-manager
       */

	printlog(LOG_DEBUG, "A REMOTE X-session detected for device=%s host=%s", dev, host);
	return TIMEOUTD_XSESSION_REMOTE;
    }
    else {
	printlog(LOG_DEBUG, "No X-session detected for device=%s host=%s", dev, host);
	return TIMEOUTD_XSESSION_NONE;
    }
}
#endif

/* We have to handle Xterms(pts/?) and Xsessions (:0) different:
   - Check Xsession for idle, but not a XTERM
   - Send message for pending logoff to X, but not to XTERM
     -> Don't check XTERM at all
   - but: check ssh (pts/?) but no XTERM (again)
*/
int chk_xterm(dev, host) /* returns 1 when dev and host seem to be a xTERM. */
char *dev,*host;
{
    if(strncmp(dev, "pts/0", 3) == 0 && strncmp(host, ":0", 1) == 0 ) {
	printlog(LOG_DEBUG, "XTERM detected. device=%s host=%s Ignoring.", dev, host);
    	return 1;
    }
    else
    	return 0;
} /* chk_xterm(dev,host) */

#ifdef TIMEOUTDX11
void killit_xsession(pid, user, host) /* returns 1 when host seems to be a xSession. */
int pid;
char *host, *user;
{
    char	msgbuf[1024], cmdbuf[1024];
  /* first, get the message into msgbuf */
    	if (limit_type == NOLOGINMSG) {
    	    sprintf(msgbuf, "Logins not allowed at this time.  Please try again later.");
	} else {
    	    sprintf(msgbuf, "You have exceeded your %s time limit.  Logging you off now.", limit_names[limit_type]);
        }

  /* then send the message using xmessage */
  /* well, this is not really clean: */
  sprintf(cmdbuf, "su %s -c \"xmessage -display %s -center '%s'&\"", user, host, msgbuf);
  printlog(LOG_DEBUG, "Executing: %s", cmdbuf);
  system(cmdbuf);

  sleep(KWAIT); /* and give the user some time to read the message ;) */

  /* kill pid here */
  printlog(LOG_DEBUG, "Sending SIGTERM to %d...", pid);
  kill(pid, SIGTERM);  /* otherwise, X crashes */
  sleep(KWAIT);

  if (!kill(pid, 0)) {    /* SIGHUP might be ignored */
      printlog(LOG_DEBUG, "Sending SIGKILL to %d...", pid);
      kill(pid, SIGKILL); /* then send sure "kill" signal */
      sleep(KWAIT);
      if (!kill(pid, 0))
      {
	  printlog(LOG_ERR, "Could not log user %s off line %s (an X-session).", user, host);
      }
  }
}
#endif

int chk_ssh(pid)/* seppy; returns true if pid is sshd, otherwise it returns false */
pid_t pid;
{
	sprintf(path, "/proc/%d/stat", pid);
	proc_file = fopen(path, "r");
	if(!proc_file) {
        	printlog(LOG_WARNING, "chk_ssh(): PID %d does not exist. Something went wrong. Ignoring.", pid);
		return 0;
	}

	fscanf (proc_file, "%*d (%[^)]", comm);
	fclose(proc_file);
	
	if(!strcmp(comm, "sshd"))
		return 1;
	else
		return 0;
}

char *getusr(pid) /*seppy; returns the name of the user owning process with the Process ID pid */
pid_t pid;
{
	char uid[99];
	sprintf(path, "/proc/%d/status", pid);
	proc_file = fopen(path, "r");
	if(!proc_file) {
        	printlog(LOG_NOTICE, "getusr(): PID %d does not exist. Ignoring.", pid);
		return "unknown";
	}
	while(!fscanf(proc_file, "Uid:    %s", uid))
                fgets(uid, 98, proc_file);
	fclose(proc_file);
	return getpwuid(atoi(uid))->pw_name;
}

#ifdef TIMEOUTDX11
Time get_xidle(user, display) /*seppy; returns millicecs since last input event */
char *user;
char *display;
{
	Display* dpy;
	static XScreenSaverInfo* mitInfo = 0; 
	struct passwd *pwEntry;
	char homedir[50]; /*50 should be enough*/
	char oldhomedir[50];
	uid_t oldeuid;
	Time retval = 0;

	pwEntry = getpwnam(user);
	if(!pwEntry) {
		printlog(LOG_ERR, "Could not get passwd-entry for user %s", user);
		return retval;
	}

	printlog(LOG_DEBUG, "Changing to user %s(%d) and connecting to X in order to get the idle time...", user, pwEntry->pw_uid);

	/*change into the user running x. we need that to connect to X*/
	/*setregid(1000, 1000); We don't need this*/

	/*save old, to come back*/
	oldeuid = geteuid();
	sprintf(oldhomedir, "HOME=%s", getenv("HOME"));

	/*become user*/
	if(seteuid(pwEntry->pw_uid) == -1) {
	    printlog(LOG_ERR, "Could not seteuid(%d).", pwEntry->pw_uid);
	    return retval;
	}

	sprintf(homedir, "HOME=%s", pwEntry->pw_dir);
	putenv(homedir);

	/* First, check if there is a xserver.. */ 
	if ((dpy = XOpenDisplay (display)) == NULL) { /* = intended */
		printlog(LOG_NOTICE, "Could not connect to %s to query idle-time for %s. Ignoring.", display, user);
	} else {
	    if (!mitInfo)
		mitInfo = XScreenSaverAllocInfo ();
	    XScreenSaverQueryInfo (dpy, DefaultRootWindow (dpy), mitInfo);
	    retval = mitInfo->idle;
	    XCloseDisplay(dpy);
	}

	/*go back again*/
	putenv(oldhomedir);
	if(seteuid(oldeuid) == -1) {
	    printlog(LOG_EMERG, "Could not restore UID with seteuid(%d)!", oldeuid);
	}

	printlog(LOG_DEBUG, "Got %d mins idle for user %s(%d).",
		 (int) retval, user, pwEntry->pw_uid);
    	return retval;
} /* get_xidle(user) */
#endif

/* seppy; getchild()
          returns the pid of the first child-process found. 
          - 1 if a error occured, 
	  - 0 if none found
	  
	  We need this because utmp returns a process owned by 
	  root when a user is connected via ssh. If we kill its
	  child (owned by the user) he/she gets logged off */
pid_t getcpid(ppid) 
pid_t ppid;
{
	DIR *proc;
	FILE *proc_file;
	struct dirent *cont;
	char akt_pid[99];
	char path[256];
	
	proc = opendir("/proc/");
	if(proc == NULL) {
	    printlog(LOG_ERR,"Error opening /proc.");
	    return -1; /* error */
	}

	while((cont = readdir(proc)) != NULL)
		if(cont->d_type == 4 && isdigit(cont->d_name[0])) { /* check only PIDs */						
			sprintf(path, "/proc/%s/status", cont->d_name);
			proc_file = fopen(path, "r");
			if(!proc_file) {
			    printlog(LOG_ERR, "Error opening a proc status file %s.", path);
			    continue;
			}

			while(!fscanf(proc_file, "PPid:    %s", akt_pid))
                		fgets(akt_pid, 10, proc_file);

			if(atoi(akt_pid) == ppid)
				return (pid_t)atoi(cont->d_name); /* return pid of child */
		} /* if(cont->d_type == 4) */

	printlog(LOG_DEBUG, "No child process found for PID=%d", ppid);
	return 0; /* no child found */
} /* getchild(ppid) */

#ifdef TESTING
int system_user(user, cmd)
char *user;
char *cmd;
{
    uid_t	oldeuid;
    char	homedir[50];
    char	oldhomedir[50];
    struct passwd *pwEntry;
    int retval;

  	/* save to restore */
	oldeuid=getuid();
	sprintf(oldhomedir, "HOME=%s", getenv("HOME"));
	/*become user*/
        pwEntry = getpwnam(user);
	if(!pwEntry) {
	  printlog(LOG_ERR, "Could not get passwd-entry for user %s", user);
	}
	if(seteuid(pwEntry->pw_uid) == -1) {
	  printlog(LOG_ERR, "Could not seteuid(%d).", pwEntry->pw_uid);
	}
	sprintf(homedir, "HOME=%s", pwEntry->pw_dir);
	putenv(homedir);

	retval = system(cmd);

	putenv(oldhomedir);
	setuid(oldeuid);

	printlog(LOG_DEBUG, "cmd=%s", cmd);

	return retval;
}
#endif
