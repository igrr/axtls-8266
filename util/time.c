/*
 * time.c - ESP8266-specific header for SNTP
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

#include "time.h"
#include "sntp.h"

// time gap in seconds from 01.01.1900 (NTP time) to 01.01.1970 (UNIX time)
#define DIFF1900TO1970 2208988800UL

// set locals...
static unsigned long confTimeZone;
static int confDayLight;

void configTime(unsigned long timezone, int daylight)
{
  confTimeZone = timezone;
  // daylight actual not used
  confDayLight = daylight;
  sntp_init();
  sntp_setservername(0, (char*)"time.nist.gov");
  sntp_setservername(1, (char*)"time.windows.com");
  sntp_setservername(2, (char*)"de.pool.ntp.org");
  sntp_set_timezone(timezone/3600);
  delay(1000);
  // we have to wait for sntp
  unsigned long m_secsSince1900=0;
  while (!m_secsSince1900)
  {
    m_secsSince1900 = sntp_get_current_timestamp();
    delay(200);
  }
  delay(1000);

}

// seconds since 1970
time_t mktime(struct tm_t *tm) {
  // system_mktime expects month in range 1..12
  #define START_MONTH 1
  return DIFF1900TO1970 + system_mktime(tm->tm_year, tm->tm_mon + START_MONTH, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

time_t time(time_t * t)
{
  time_t seconds = sntp_get_current_timestamp();
  if (seconds)
    *t = seconds;
  return seconds ;
}

// The gettimeofday() function is marked obsolescent.
// Applications should use the clock_gettime() function instead
// see: http://pubs.opengroup.org/onlinepubs/9699919799/
int gettimeofday(void *tp, void *tzp)
{
  struct timeval tv;
  tv.tv_sec  = millis() / 1000;
  tv.tv_usec = micros();
  ((struct timeval*)tp)->tv_sec  = tv.tv_sec;
  ((struct timeval*)tp)->tv_usec = tv.tv_usec;
  return 0;
}

int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
  struct timespec tv;
  tv.tv_sec  = millis() / 1000;
  tv.tv_nsec = micros() * 1000L;
  ((struct timespec*)tp)->tv_sec  = tv.tv_sec;
  ((struct timespec*)tp)->tv_nsec = tv.tv_nsec;
  return 0;
}

char * ctime(const time_t *clock)
{
  return sntp_get_real_time(*clock);
}

int compareTime(struct tm_t *tm1, struct tm_t *tm2)
{
  unsigned long sec1 = mktime(tm1);
  unsigned long sec2 = mktime(tm2);
  return (sec1 - sec2);
}

