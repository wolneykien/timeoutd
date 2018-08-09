#include    <stdio.h>
#include    <stdlib.h>
#include    <utmp.h>
#include    <time.h>
#include    <string.h>

int main()
{
    FILE	*fp;
    struct 	utmp ut;
    char	user[9];
    char	host[17];
    char	line[13];
    char	buf[30];
    int		pid;
    /* struct 	tm *tm; */

    if ((fp = fopen(UTMP_FILE, "r")) == NULL)
    {
      printf("Could not open wtmp file!");
      exit(1);
    }

    /* Go to end of file minus one structure */
    fseek(fp, -1L * sizeof(struct utmp), SEEK_END);

    while (fread(&ut, sizeof(struct utmp), 1, fp) == 1)
    {
      /* time_t log_time = ut.ut_tv.tv_sec; */
      time_t log_time = ut.ut_time;

/*  Original outdated code
 *
 *     tm = localtime(&log_time);
 *
 *     * if (tm->tm_year != now.tm_year || tm->tm_yday != now.tm_yday)
 *     *  break;
 *
 *    printf("%02d:%02d type=", tm->tm_hour,tm->tm_min);
*/

      struct tm format_tm = *localtime(&log_time);
      strftime(buf, sizeof(buf), "%a %d %b %Y - %H:%M:%S", &format_tm);
      printf("%s type=", buf);

      switch (ut.ut_type)
      {
#ifndef SUNOS
     case RUN_LVL: printf("RUN_LVL");
     break;
     case BOOT_TIME: printf("BOOT_TIME");
     break;
     case NEW_TIME: printf("NEW_TIME");
     break;
     case OLD_TIME: printf("OLD_TIME");
     break;
     case INIT_PROCESS: printf("INIT_PROCESS");
     break;
     case LOGIN_PROCESS: printf("LOGIN_PROCESS");
     break;
     case USER_PROCESS: printf("USER_PROCESS");
     break;
     case DEAD_PROCESS: printf("DEAD_PROCESS");
     break;
#endif
     default: printf("UNKNOWN!(type=%d)", ut.ut_type);
     }
     strncpy(user, ut.ut_user, 8);
     user[8] = 0;
     strncpy(host, ut.ut_host, 16);
     host[16] = 0;
     strncpy(line, ut.ut_line, 12);
     line[12] = 0;
     pid = ut.ut_pid;
     printf(" line=%s host=%s user=%s pid=%d\n", line, host, user, pid);


      /* Position the file pointer 2 structures back */
      if (fseek(fp, -2 * sizeof(struct utmp), SEEK_CUR) < 0) break;
    }
    fclose(fp);
}
