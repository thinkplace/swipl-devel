/*  $Id$

    Part of SWI-Prolog

    Author:        Jan Wielemaker
    E-mail:        wielemak@science.uva.nl
    WWW:           http://www.swi-prolog.org
    Copyright (C): 1985-2006, University of Amsterdam

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "pl-incl.h"
#include "libtai/taia.h"
#include "libtai/caltime.h"
#include <math.h>
#include <stdio.h>

#define TAI_UTC_OFFSET 4611686018427387914LL

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
struct ftm is a `floating' version of the system struct tm.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#define		HAS_STAMP	0x0001
#define		HAS_WYDAY	0x0002

typedef struct ftm
{ struct	tm tm;			/* System time structure */
  double	sec;			/* float version of tm.tm_sec */
  int		utcoff;			/* offset to UTC (seconds) */
  atom_t	tzname;			/* Name of timezone */
  int		isdst;			/* Daylight saving time */
  double	stamp;			/* Time stamp (sec since 1970-1-1) */
  int		flags;			/* Filled fields */
} ftm;

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
tz_offset() returns the offset from UTC in seconds.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static void
do_tzset()
{ static int done = FALSE;

  if ( !done )
  { tzset();
    done = TRUE;
  }
}


static int
tz_offset()
{ do_tzset();
  return timezone;
}


static char *
tz_name(int dst)
{ dst = (dst != 0);

  do_tzset();
  return tzname[dst];
}


static atom_t
tz_name_as_atom(int dst)
{ static atom_t a[2];

  dst = (dst != 0);			/* 0 or 1 */

  if ( !a[dst] )
  { do_tzset();
    a[dst] = PL_new_atom(tzname[dst]);
  }

  return a[dst];
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
unify_taia(): Unify a TAIA date as a Prolog double using the POSIX 1970
origin;
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

/*
static int
unify_taia(term_t t, struct taia *taia)
{ double d = (double)((int64_t)taia->sec.x - TAI_UTC_OFFSET);
  
  d += taia->nano / 1e9;

  return PL_unify_float(t, d);
}
*/


static int
get_taia(term_t t, struct taia *taia, double *seconds)
{ double d;

  if ( PL_get_float(t, &d) )
  { double fp, ip;

    if ( seconds )
      *seconds = d;

    fp = modf(d, &ip);
    if ( fp < 0 )
    { fp += 1.0;
      ip -= 1.0;
    }

    taia->sec.x = (int64_t)ip + TAI_UTC_OFFSET;
    taia->nano  = (long)(fp*1e9);
    taia->atto  = 0L;

    return TRUE;
  }

  return FALSE;
}


static int
get_tz_arg(int i, term_t t, term_t a, atom_t *tz)
{ atom_t name;

  _PL_get_arg(i, t, a);
  if ( !PL_get_atom_ex(a, &name) )
    fail;
  if ( name != ATOM_minus )
    *tz = name;

  succeed;
}


static int
get_int_arg(int i, term_t t, term_t a, int *val)
{ _PL_get_arg(i, t, a);

  return PL_get_integer_ex(a, val);
}


static int
get_float_arg(int i, term_t t, term_t a, double *val)
{ _PL_get_arg(i, t, a);

  return PL_get_float_ex(a, val);
}


static int
get_bool_arg(int i, term_t t, term_t a, int *val)
{ _PL_get_arg(i, t, a);

  return PL_get_bool_ex(a, val);
}


static int
get_ftm(term_t t, ftm *ftm)
{ if ( PL_is_functor(t, FUNCTOR_date9) )
  { term_t tmp = PL_new_term_ref();

    memset(ftm, 0, sizeof(*ftm));

    if ( get_int_arg  (1, t, tmp, &ftm->tm.tm_year) &&
	 get_int_arg  (2, t, tmp, &ftm->tm.tm_mon)  &&
	 get_int_arg  (3, t, tmp, &ftm->tm.tm_mday) &&
	 get_int_arg  (4, t, tmp, &ftm->tm.tm_hour) &&
	 get_int_arg  (5, t, tmp, &ftm->tm.tm_min)  &&
	 get_float_arg(6, t, tmp, &ftm->sec)	    &&
	 get_int_arg  (7, t, tmp, &ftm->utcoff)     &&
	 get_tz_arg   (8, t, tmp, &ftm->tzname)     &&
	 get_bool_arg (9, t, tmp, &ftm->isdst) )
    { double fp, ip;

      fp = modf(ftm->sec, &ip);
      if ( fp < 0.0 )
      { fp += 1.0;
	ip -= 1.0;
      }

      ftm->tm.tm_sec = (int)ip;
      ftm->tm.tm_year -= 1900;		/* 1900 based */
      ftm->tm.tm_mon--;			/* 0-based */

      succeed;
    }
  }

  fail;
}


/** void cal_ftm(ftm *ftm, int required)
    compute missing fields from fmt
*/

void
cal_ftm(ftm *ftm, int required)
{ int missing = ftm->flags^required;

  if ( missing )			/* we need something, so we always */
  { struct caltime ct;			/* need the stamp */
    struct tai tai;

    ct.date.year  = ftm->tm.tm_year+1900;
    ct.date.month = ftm->tm.tm_mon+1;
    ct.date.day   = ftm->tm.tm_mday;
    ct.hour       = ftm->tm.tm_hour;
    ct.minute     = ftm->tm.tm_min;
    ct.second     = ftm->tm.tm_sec;
    ct.offset     = -ftm->utcoff / 60;	/* TBD: make libtai speak seconds */

    caltime_tai(&ct, &tai);
    ftm->stamp  = (double)((int64_t)tai.x - TAI_UTC_OFFSET);
    ftm->stamp -= (double)ct.second;
    ftm->stamp += ftm->sec;
    ftm->flags |= HAS_STAMP;

    if ( missing & HAS_WYDAY )
    { caltime_utc(&ct, &tai, &ftm->tm.tm_wday, &ftm->tm.tm_yday);
      ftm->flags |= HAS_WYDAY;
    }
  }
}


static
PRED_IMPL("stamp_date_time", 3, stamp_date_time, 0)
{ struct taia taia;
  term_t compound = A2;
  double argsec;

  if ( get_taia(A1, &taia, &argsec) )
  { struct caltime ct;
    int weekday, yearday;
    double sec;
    int utcoffset;
    int done = FALSE;
    atom_t alocal;
    atom_t tzatom = ATOM_minus;
    atom_t dstatom = ATOM_minus;

    if ( PL_get_atom(A3, &alocal) && alocal == ATOM_local )
    { time_t unixt;
      int64_t ut64;
      struct tm tm;

      utcoffset = tz_offset();

      ut64 = taia.sec.x - TAI_UTC_OFFSET;
      unixt = (time_t) ut64;

      if ( (int64_t)unixt == ut64 )
      { double ip;

	localtime_r(&unixt, &tm);
	sec = (double)tm.tm_sec + modf(argsec, &ip);
	ct.date.year  = tm.tm_year+1900;
	ct.date.month = tm.tm_mon+1;
	ct.date.day   = tm.tm_mday;
	ct.hour       = tm.tm_hour;
	ct.minute     = tm.tm_min;
	tzatom = tz_name_as_atom(tm.tm_isdst);
	if ( daylight )			/* from tzset() */
	{ if ( tm.tm_isdst )
	  { utcoffset -= 3600;
	    dstatom    = ATOM_true;
	  } else
	  { dstatom    = ATOM_false;
	  }
	}
	done = TRUE;
      }
    } else if ( !PL_get_integer_ex(A3, &utcoffset) )
    { fail;
    }

    if ( !done )
    { taia.sec.x -= utcoffset;
      caltime_utc(&ct, &taia.sec, &weekday, &yearday);
      sec = (double)ct.second+(double)taia.nano/1e9;
    }

    return PL_unify_term(compound,
			 PL_FUNCTOR, FUNCTOR_date9,
			   PL_INTEGER, ct.date.year,
			   PL_INTEGER, ct.date.month,
			   PL_INTEGER, ct.date.day,
			   PL_INTEGER, ct.hour,
			   PL_INTEGER, ct.minute,
			   PL_FLOAT,   sec,
			   PL_INTEGER, utcoffset,
			   PL_ATOM,    tzatom,
			   PL_ATOM,    dstatom);
  }

					/* time_stamp */
  return PL_error(NULL, 0, NULL, ERR_TYPE, ATOM_float, A1);
}


static
PRED_IMPL("date_time_stamp", 2, date_time_stamp, 0)
{ ftm ftm;

  if ( !get_ftm(A1, &ftm) )
    fail;
  cal_ftm(&ftm, HAS_STAMP);

  return PL_unify_float(A2, ftm.stamp);
}


		 /*******************************
		 *	  GLIBC FUNCTIONS	*
		 *******************************/

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
These functions support strftime() %g, %G and   %V.  Code is copied from
glibc 2.3.5. As Glibc is LGPL, there are no license issues.
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

#ifndef __isleap
/* Nonzero if YEAR is a leap year (every 4 years,
   except every 100th isn't, and every 400th is).  */
# define __isleap(year)	\
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))
#endif

/* The number of days from the first day of the first ISO week of this
   year to the year day YDAY with week day WDAY.  ISO weeks start on
   Monday; the first ISO week has the year's first Thursday.  YDAY may
   be as small as YDAY_MINIMUM.  */
#define ISO_WEEK_START_WDAY 1 /* Monday */
#define ISO_WEEK1_WDAY 4 /* Thursday */
#define YDAY_MINIMUM (-366)
static int iso_week_days (int, int) __THROW;
#ifdef __GNUC__
__inline__
#endif
static int
iso_week_days(int yday, int wday)
{ /* Add enough to the first operand of % to make it nonnegative.  */
  int big_enough_multiple_of_7 = (-YDAY_MINIMUM / 7 + 2) * 7;
  return (yday
	  - (yday - wday + ISO_WEEK1_WDAY + big_enough_multiple_of_7) % 7
	  + ISO_WEEK1_WDAY - ISO_WEEK_START_WDAY);
}


		 /*******************************
		 *	    FORMATTING		*
		 *******************************/

#define OUT1DIGIT(fd, val) \
	{ Sputcode('0'+(val)%10, fd); \
	}
#define OUT2DIGITS(fd, val) \
	{ Sputcode('0'+((val)/10)%10, fd); \
	  Sputcode('0'+(val)%10, fd); \
	}
#define OUT3DIGITS(fd, val) \
	{ Sputcode('0'+((val)/100)%10, fd); \
	  Sputcode('0'+((val)/10)%10, fd); \
	  Sputcode('0'+(val)%10, fd); \
	}
#define OUT2DIGITS_SPC(fd, val) \
	{ Sputcode(((val)/10 == 0 ? ' ' : '0'+((val)/10)%10), fd); \
	  Sputcode('0'+(val)%10, fd); \
	}
#define OUTNUMBER(fd, fmt, val) \
	{ Sfprintf(fd, fmt, val); \
	}
#define SUBFORMAT(f) \
	{ format_time(fd, f, ftm); \
	}
#define OUTCHR(fd, c) \
 	{ Sputcode(c, fd); \
	}
#define OUTSTR(str) \
	{ Sfputs(str, fd); \
	}
#define ERROR(e) \
	{ return EOF; \
	}

static int
format_time(IOSTREAM *fd, const wchar_t *format, ftm *ftm)
{ wint_t c;

  while((c = *format++))
  { switch(c)
    { case '%':
	switch((c = *format++))
	{ case 'a':			/* %a: abbreviated weekday */
	  case 'A':			/* %A: weekday */
	  case 'b':			/* %b: abbreviated month */
	  case 'B':			/* %B: month */
	  case 'c':			/* %c: default representation */
	  case 'p':			/* %p: AM/PM (locale) */
	  case 'P':			/* %P: am/pm (locale) */
	  case 'x':			/* %x: date in locale */
	  case 'X':			/* %X: time in locale */
	  case_b:
	  { char fmt[3];
	    char buf[256];
	    wchar_t wbuf[256];
	    size_t n;
  
	    fmt[0] = '%';
	    fmt[1] = c;
	    fmt[2] = EOS;

	    cal_ftm(ftm, HAS_STAMP|HAS_WYDAY);
					/* conversion is not thread-safe under locale switch */
	    n = strftime(buf, sizeof(buf), fmt, &ftm->tm);
	    if ( (n = mbstowcs(wbuf, buf, sizeof(wbuf)/sizeof(wchar_t))) != (size_t)-1 )
	    { wchar_t *p;

	      for(p=wbuf; n-- > 0; p++)
		Sputcode(*p, fd);
	    }
	    break;
	  }
	  case 'C':			/* (year/100) as a 2-digit int */
	  { if ( ftm->tm.tm_year >= 0 && ftm->tm.tm_year < 10000 )
	    { int century = (ftm->tm.tm_year+1900)/100;
	      OUT2DIGITS(fd, century);
	    } else
	    { ERROR("%C: year out of range");
	    }
	    break;
	  }
	  case 'd':			/* day of the month */
	    OUT2DIGITS(fd, ftm->tm.tm_mday);
	    break;
	  case 'D':			/* %m/%d/%y */
	    SUBFORMAT(L"%m/%d/%y");
	    break;
	  case 'e':			/* day of the month */
	    OUT2DIGITS_SPC(fd, ftm->tm.tm_mday);
	    break;
	  case 'E':			/* alternative format */
	    ERROR("%E: not supported");
	    break;
	  case 'F':			/* ISO 8601 date format */
	    SUBFORMAT(L"%Y-%m-%d");
	    break;
	  case 'G':
	  case 'g':
	  case 'V':
	  { int year, days;
	    
	    cal_ftm(ftm, HAS_STAMP|HAS_WYDAY);
	    year = ftm->tm.tm_year+1900;
	    days = iso_week_days(ftm->tm.tm_yday, ftm->tm.tm_wday);

	    if ( days < 0 )
	    { year--;
	      days = iso_week_days(ftm->tm.tm_yday + (365 + __isleap (year)),
				   ftm->tm.tm_wday);
	    } else
	    { int d = iso_week_days(ftm->tm.tm_yday - (365 + __isleap (year)),
				    ftm->tm.tm_wday);
	      if (0 <= d)
	      { /* This ISO week belongs to the next year.  */
		year++;
		days = d;
	      }
	    }
	    
	    switch(c)
	    { case 'g':
		OUT2DIGITS(fd, (year % 100 + 100) % 100);
		break;
	      case 'G':
		OUTNUMBER(fd, "%d", year);
		break;
	      case 'V':
		OUT2DIGITS(fd, days/7+1);
		break;
	    }
	    break;
	  }
	  case 'h':			/* Equivalent to %b. (SU) */
	    c = 'b';
	    goto case_b;
	  case 'H':			/* 0..23 hours */
	    OUT2DIGITS(fd, ftm->tm.tm_hour);
	    break;
	  case 'I':			/* 01..12 hours */
	    OUT2DIGITS(fd, (ftm->tm.tm_hour)%12);
	    break;
	  case 'j':			/* yday (001..366) */
	    cal_ftm(ftm, HAS_WYDAY);
	    OUT3DIGITS(fd, ftm->tm.tm_yday+1);
	    break;
	  case 'k':			/* 0..23 hours (leading space) */
	    OUT2DIGITS_SPC(fd, ftm->tm.tm_hour);
	    break;
	  case 'l':			/* 1..12 hours (leading space) */
	    OUT2DIGITS_SPC(fd, (ftm->tm.tm_hour)%12);
	    break;
	  case 'm':			/* 01..12 month  */
	    OUT2DIGITS(fd, ftm->tm.tm_mon+1);
	    break;
	  case 'M':			/* 00..59 minute  */
	    OUT2DIGITS(fd, ftm->tm.tm_min);
	    break;
	  case 'n':			/* newline */
	    OUTCHR(fd, '\n');
	    break;
	  case 'O':
	    ERROR("%O: not supported");
	    break;
	  case 'r':			/* The  time in a.m./p.m. notation */
	    SUBFORMAT(L"%I:%M:%S %p");	/* TBD: :-separator locale handling */
	    break;
	  case 'R':
	    SUBFORMAT(L"%H:%M");
	    break;
	  case 's':			/* Seconds since 1970 */
	    cal_ftm(ftm, HAS_STAMP);
	    OUTNUMBER(fd, "%.0f", ftm->stamp);
	    break;
	  case 'S':			/* Seconds */
	    OUT2DIGITS(fd, ftm->tm.tm_sec);
	    break;
	  case 't':			/* tab */
	    OUTCHR(fd, '\t');
	    break;
	  case 'T':
	    SUBFORMAT(L"%H:%M:%S");
	    break;
	  case 'u':			/* 1..7 weekday, mon=1 */
	  { int wday;

	    cal_ftm(ftm, HAS_WYDAY);
	    wday = (ftm->tm.tm_wday - 1 + 7) % 7 + 1;
	    OUT1DIGIT(fd, wday);
	    break;
	  }
	  case 'U':			/* 00..53 weeknumber */
	  { int wk;
  
	    cal_ftm(ftm, HAS_WYDAY);
	    wk = (ftm->tm.tm_yday - (ftm->tm.tm_yday - ftm->tm.tm_wday + 7) % 7 + 7) / 7;
	    OUT2DIGITS(fd, wk);
	    break;
	  }
	  case 'w':			/* 0..6 weekday */
	    cal_ftm(ftm, HAS_WYDAY);
	    OUT1DIGIT(fd, ftm->tm.tm_wday);
	    break;
	  case 'W':			/* 00..53 monday-based week number */
	  { int wk;
  
	    cal_ftm(ftm, HAS_WYDAY);
	    wk = (ftm->tm.tm_yday - (ftm->tm.tm_yday - ftm->tm.tm_wday + 8) % 7 + 7) / 7;
	    OUT2DIGITS(fd, wk);
	    break;
	  }
	  case 'y':			/* 00..99 (year) */
	    OUT2DIGITS(fd, (ftm->tm.tm_year+1900) % 100);
	    break;
	  case 'Y':			/* Year (decimal) */
	    OUTNUMBER(fd, "%d", ftm->tm.tm_year+1900);
	    break;
	  case 'z':			/* Time-zone as offset */
	  { int min = -ftm->utcoff/60;
	    if ( min > 0 )
	    { OUTCHR(fd, '+');
	    } else
	    { min = -min;
	      OUTCHR(fd, '-');
	    }
	    OUT2DIGITS(fd, min/60);
	    OUT2DIGITS(fd, min%60);
	    break;
	  }
	  case 'Z':			/* Time-zone as name */
	    if ( ftm->tzname )
	    { OUTSTR(PL_atom_chars(ftm->tzname));
	    } else
	    { OUTSTR(tz_name(ftm->tm.tm_isdst));
	    }
	    break;
	  case '+':
	    { char buf[26];

	      asctime_r(&ftm->tm, buf);
	      buf[24] = EOS;
	      OUTSTR(buf);
	    }
	    break;
	  case '%':
	    OUTCHR(fd, '%');
	    break;
	}
        break;
      default:
	OUTCHR(fd, c);
    }
  }

  return Sferror(fd) ? EOF : 0;
}


/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
format_time(+Spec, +Format, +Stamp)

Issues:
	* Localtime/DST
	* Year is an int (not so bad)
	* Portability
	* Sub-second times
- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

static
PRED_IMPL("format_time", 3, format_time, 0)
{ struct taia taia;
  struct caltime ct;
  struct ftm tb;
  int weekday, yearday;
  wchar_t *fmt;
  time_t unixt;
  int64_t ut64;
  unsigned fmtlen;
  redir_context ctx;
  int rc;

  if ( !PL_get_wchars(A2, &fmtlen, &fmt,
		      CVT_ATOM|CVT_STRING|CVT_LIST|CVT_EXCEPTION) )
    fail;
  if ( get_taia(A3, &taia, &tb.stamp) )
  { ut64 = taia.sec.x - TAI_UTC_OFFSET;
    unixt = (time_t) ut64;

    if ( (int64_t)unixt == ut64 )
    { localtime_r(&unixt, &tb.tm);
    } else
    { caltime_utc(&ct, &taia.sec, &weekday, &yearday);
      memset(&tb, 0, sizeof(tb));
      tb.tm.tm_sec  = ct.second;
      tb.tm.tm_min  = ct.minute;
      tb.tm.tm_hour = ct.hour;
      tb.tm.tm_mday = ct.date.day;
      tb.tm.tm_mon  = ct.date.month - 1;
      tb.tm.tm_year = ct.date.year - 1900;
      tb.tm.tm_wday = weekday;
      tb.tm.tm_yday = yearday;
    }
  } else if ( !get_ftm(A3, &tb) )
  { fail;
  }

  if ( !setupOutputRedirect(A1, &ctx, FALSE) )
    fail;
  if ( (rc = format_time(ctx.stream, fmt, &tb)) == 0 )
    return closeOutputRedirect(&ctx);

  discardOutputRedirect(&ctx);
  fail;
}



		 /*******************************
		 *      PUBLISH PREDICATES	*
		 *******************************/


BeginPredDefs(tai)
  PRED_DEF("stamp_date_time", 3, stamp_date_time, 0)
  PRED_DEF("date_time_stamp", 2, date_time_stamp, 0)
  PRED_DEF("format_time",     3, format_time,     0)
EndPredDefs
