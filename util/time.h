#ifndef TIME_H
#define TIME_H

typedef long size_t;
typedef long time_t;
typedef long clockid_t;

#include "user_interface.h"

struct timeval
{
  time_t tv_sec;
  long   tv_usec;
};

struct timespec {
  time_t tv_sec;   /* Seconds */
  long   tv_nsec;  /* Nanoseconds */
};

//*******************************************************************

// time structure represents time A.D. (e.g. year: 2015)
struct tm_t {
  int tm_sec;  /* seconds,          range 0 to 59  */
  int tm_min;  /* minutes,          range 0 to 59  */
  int tm_hour; /* hours,            range 0 to 23  */
  int tm_mday; /* day of the month, range 1 to 31  */
  int tm_mon;  /* month,            range 0 to 11  */
  int tm_year; /* number of years   since 1900     */
  int tm_wday; /* day of the week,  range 0 to 6   */ //sunday = 0
  int tm_yday; /* day in the year,  range 0 to 365 */
  int tm_isdst;/* daylight saving time             */ //no=0, yes>0
} tm_t;

struct tm {
  int tm_sec;  /* seconds,          range 0 to 59  */
  int tm_min;  /* minutes,          range 0 to 59  */
  int tm_hour; /* hours,            range 0 to 23  */
  int tm_mday; /* day of the month, range 1 to 31  */
  int tm_mon;  /* month,            range 0 to 11  */
  int tm_year; /* number of years   since 1900     */
  int tm_wday; /* day of the week,  range 0 to 6   */ //sunday = 0
  int tm_yday; /* day in the year,  range 0 to 365 */
  int tm_isdst;/* daylight saving time             */ //no=0, yes>0
} tm;

time_t mktime(struct tm_t *tm);
char*  ctime(const time_t *clock);
int    gettimeofday(void *tp, void *tzp);
int    clock_gettime(clockid_t clock_id, struct timespec *tp);

void   configTime(unsigned long timezone, int daylight);
time_t time(time_t * t);
int    compareTime(struct tm_t *tm1, struct tm_t *tm2);

#endif //TIME_H

