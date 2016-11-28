/*
 * time.h - ESP8266-specific header for SNTP
 * Copyright (c) 2015 Peter Dobler. All rights reserved.
 * This file is part of the esp8266 core for Arduino environment.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 */

#ifndef TIME_H
#define TIME_H

#ifndef size_t
#ifndef __SIZE_TYPE__
#define __SIZE_TYPE__ long
#endif
typedef __SIZE_TYPE__ size_t;
#endif

#ifndef time_t
#ifndef _TIME_T_
#define _TIME_T_ long
#endif
typedef _TIME_T_ time_t;
#endif

#ifndef __clockid_t_defined
#ifndef _CLOCKID_T_
#define _CLOCKID_T_ long
#endif
typedef _CLOCKID_T_ clockid_t;
#define __clockid_t_defined
#endif

#include "user_interface.h"

struct timeval
{
  time_t tv_sec;
  long   tv_usec;
};

#ifndef __timespec_defined
#define __timespec_defined
struct timespec {
  time_t tv_sec;   /* Seconds */
  long   tv_nsec;  /* Nanoseconds */
};
#endif

// time structure represents time A.D. (e.g. year: 2015)
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
};

time_t mktime(struct tm *t);
char*  ctime(const time_t *clock);
int    gettimeofday(void *tp, void *tzp);
int    clock_gettime(clockid_t clock_id, struct timespec *tp);

void   configTime(unsigned long timezone, int daylight);
time_t time(time_t *t);
int    compareTime(struct tm *t1, struct tm *t2);

#endif //TIME_H

