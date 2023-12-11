/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_TIME_H
#define _LINUX_TIME_H

# include <linux/cache.h>
# include <linux/seqlock.h>
# include <linux/math64.h>
# include <linux/time64.h>

extern struct timezone sys_tz;

int get_timespec64(struct timespec64 *ts,
		const struct timespec __user *uts);
int put_timespec64(const struct timespec64 *ts,
		struct timespec __user *uts);
int get_itimerspec64(struct itimerspec64 *it,
			const struct itimerspec __user *uit);
int put_itimerspec64(const struct itimerspec64 *it,
			struct itimerspec __user *uit);

#define TIME_T_MAX	(time_t)((1UL << ((sizeof(time_t) << 3) - 1)) - 1)

static inline int timespec_equal(const struct timespec *a,
                                 const struct timespec *b)
{
	return (a->tv_sec == b->tv_sec) && (a->tv_nsec == b->tv_nsec);
}

/*
 * lhs < rhs:  return <0
 * lhs == rhs: return 0
 * lhs > rhs:  return >0
 */
static inline int timespec_compare(const struct timespec *lhs, const struct timespec *rhs)
{
	if (lhs->tv_sec < rhs->tv_sec)
		return -1;
	if (lhs->tv_sec > rhs->tv_sec)
		return 1;
	return lhs->tv_nsec - rhs->tv_nsec;
}

static inline int timeval_compare(const struct timeval *lhs, const struct timeval *rhs)
{
	if (lhs->tv_sec < rhs->tv_sec)
		return -1;
	if (lhs->tv_sec > rhs->tv_sec)
		return 1;
	return lhs->tv_usec - rhs->tv_usec;
}

/*
 * mktime64 - Converts date to seconds.
 * Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * A leap second can be indicated by calling this function with sec as
 * 60 (allowable under ISO 8601).  The leap second is treated the same
 * as the following second since they don't exist in UNIX time.
 *
 * An encoding of midnight at the end of the day as 24:00:00 - ie. midnight
 * tomorrow - (allowable under ISO 8601) is supported.
 */
static inline time64_t mktime64(const unsigned int year0, const unsigned int mon0,
		const unsigned int day, const unsigned int hour,
		const unsigned int min, const unsigned int sec)
{
	unsigned int mon = mon0, year = year0;

	/* 1..12 -> 11,12,1..10 */
	if (0 >= (int) (mon -= 2)) {
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}

	return ((((time64_t)
		  (year/4 - year/100 + year/400 + 367*mon/12 + day) +
		  year*365 - 719499
	    )*24 + hour /* now have hours - midnight tomorrow handled here */
	  )*60 + min /* now have minutes */
	)*60 + sec; /* finally seconds */
}

/**
 * Deprecated. Use mktime64().
 */
static inline unsigned long mktime(const unsigned int year,
			const unsigned int mon, const unsigned int day,
			const unsigned int hour, const unsigned int min,
			const unsigned int sec)
{
	return mktime64(year, mon, day, hour, min, sec);
}

/**
 * set_normalized_timespec - set timespec sec and nsec parts and normalize
 *
 * @ts:		pointer to timespec variable to be set
 * @sec:	seconds to set
 * @nsec:	nanoseconds to set
 *
 * Set seconds and nanoseconds field of a timespec variable and
 * normalize to the timespec storage format
 *
 * Note: The tv_nsec part is always in the range of
 *	0 <= tv_nsec < NSEC_PER_SEC
 * For negative values only the tv_sec field is negative !
 */
static inline void set_normalized_timespec(struct timespec *ts, time_t sec, s64 nsec)
{
	while (nsec >= NSEC_PER_SEC) {
		/*
		 * The following asm() prevents the compiler from
		 * optimising this loop into a modulo operation. See
		 * also __iter_div_u64_rem() in include/linux/time.h
		 */
		asm("" : "+rm"(nsec));
		nsec -= NSEC_PER_SEC;
		++sec;
	}
	while (nsec < 0) {
		asm("" : "+rm"(nsec));
		nsec += NSEC_PER_SEC;
		--sec;
	}
	ts->tv_sec = sec;
	ts->tv_nsec = nsec;
}

/*
 * timespec_add_safe assumes both values are positive and checks
 * for overflow. It will return TIME_T_MAX if the reutrn would be
 * smaller then either of the arguments.
 *
 * Add two timespec values and do a safety check for overflow.
 * It's assumed that both values are valid (>= 0)
 */
static inline struct timespec timespec_add_safe(const struct timespec lhs,
				  const struct timespec rhs)
{
	struct timespec res;

	set_normalized_timespec(&res, lhs.tv_sec + rhs.tv_sec,
				lhs.tv_nsec + rhs.tv_nsec);

	if (res.tv_sec < lhs.tv_sec || res.tv_sec < rhs.tv_sec)
		res.tv_sec = TIME_T_MAX;

	return res;
}


static inline struct timespec timespec_add(struct timespec lhs,
						struct timespec rhs)
{
	struct timespec ts_delta;
	set_normalized_timespec(&ts_delta, lhs.tv_sec + rhs.tv_sec,
				lhs.tv_nsec + rhs.tv_nsec);
	return ts_delta;
}

/*
 * sub = lhs - rhs, in normalized form
 */
static inline struct timespec timespec_sub(struct timespec lhs,
						struct timespec rhs)
{
	struct timespec ts_delta;
	set_normalized_timespec(&ts_delta, lhs.tv_sec - rhs.tv_sec,
				lhs.tv_nsec - rhs.tv_nsec);
	return ts_delta;
}

/*
 * Returns true if the timespec is norm, false if denorm:
 */
static inline bool timespec_valid(const struct timespec *ts)
{
	/* Dates before 1970 are bogus */
	if (ts->tv_sec < 0)
		return false;
	/* Can't have more nanoseconds then a second */
	if ((unsigned long)ts->tv_nsec >= NSEC_PER_SEC)
		return false;
	return true;
}

static inline bool timespec_valid_strict(const struct timespec *ts)
{
	if (!timespec_valid(ts))
		return false;
	/* Disallow values that could overflow ktime_t */
	if ((unsigned long long)ts->tv_sec >= KTIME_SEC_MAX)
		return false;
	return true;
}

static inline bool timeval_valid(const struct timeval *tv)
{
	/* Dates before 1970 are bogus */
	if (tv->tv_sec < 0)
		return false;

	/* Can't have more microseconds then a second */
	if (tv->tv_usec < 0 || tv->tv_usec >= USEC_PER_SEC)
		return false;

	return true;
}

/**
 * timespec_trunc - Truncate timespec to a granularity
 * @t: Timespec
 * @gran: Granularity in ns.
 *
 * Truncate a timespec to a granularity. Always rounds down. gran must
 * not be 0 nor greater than a second (NSEC_PER_SEC, or 10^9 ns).
 */
static inline struct timespec timespec_trunc(struct timespec t, unsigned gran)
{
	/* Avoid division in the common cases 1 ns and 1 s. */
	if (gran == 1) {
		/* nothing */
	} else if (gran == NSEC_PER_SEC) {
		t.tv_nsec = 0;
	} else if (gran > 1 && gran < NSEC_PER_SEC) {
		t.tv_nsec -= t.tv_nsec % gran;
	} else {
		WARN(1, "illegal file time granularity: %u", gran);
	}
	return t;
}

/*
 * Validates if a timespec/timeval used to inject a time offset is valid.
 * Offsets can be postive or negative. The value of the timeval/timespec
 * is the sum of its fields, but *NOTE*: the field tv_usec/tv_nsec must
 * always be non-negative.
 */
static inline bool timeval_inject_offset_valid(const struct timeval *tv)
{
	/* We don't check the tv_sec as it can be positive or negative */

	/* Can't have more microseconds then a second */
	if (tv->tv_usec < 0 || tv->tv_usec >= USEC_PER_SEC)
		return false;
	return true;
}

static inline bool timespec_inject_offset_valid(const struct timespec *ts)
{
	/* We don't check the tv_sec as it can be positive or negative */

	/* Can't have more nanoseconds then a second */
	if (ts->tv_nsec < 0 || ts->tv_nsec >= NSEC_PER_SEC)
		return false;
	return true;
}

/* Some architectures do not supply their own clocksource.
 * This is mainly the case in architectures that get their
 * inter-tick times by reading the counter on their interval
 * timer. Since these timers wrap every tick, they're not really
 * useful as clocksources. Wrapping them to act like one is possible
 * but not very efficient. So we provide a callout these arches
 * can implement for use with the jiffies clocksource to provide
 * finer then tick granular time.
 */
#ifdef CONFIG_ARCH_USES_GETTIMEOFFSET
extern u32 (*arch_gettimeoffset)(void);
#endif

struct itimerval;
extern int do_setitimer(int which, struct itimerval *value,
			struct itimerval *ovalue);
extern int do_getitimer(int which, struct itimerval *value);

extern long do_utimes(int dfd, const char __user *filename, struct timespec64 *times, int flags);

/*
 * Similar to the struct tm in userspace <time.h>, but it needs to be here so
 * that the kernel source is self contained.
 */
struct tm {
	/*
	 * the number of seconds after the minute, normally in the range
	 * 0 to 59, but can be up to 60 to allow for leap seconds
	 */
	int tm_sec;
	/* the number of minutes after the hour, in the range 0 to 59*/
	int tm_min;
	/* the number of hours past midnight, in the range 0 to 23 */
	int tm_hour;
	/* the day of the month, in the range 1 to 31 */
	int tm_mday;
	/* the number of months since January, in the range 0 to 11 */
	int tm_mon;
	/* the number of years since 1900 */
	long tm_year;
	/* the number of days since Sunday, in the range 0 to 6 */
	int tm_wday;
	/* the number of days since January 1, in the range 0 to 365 */
	int tm_yday;
};

void time64_to_tm(time64_t totalsecs, int offset, struct tm *result);

/**
 * time_to_tm - converts the calendar time to local broken-down time
 *
 * @totalsecs	the number of seconds elapsed since 00:00:00 on January 1, 1970,
 *		Coordinated Universal Time (UTC).
 * @offset	offset seconds adding to totalsecs.
 * @result	pointer to struct tm variable to receive broken-down time
 */
static inline void time_to_tm(time_t totalsecs, int offset, struct tm *result)
{
	time64_to_tm(totalsecs, offset, result);
}

/**
 * timespec_to_ns - Convert timespec to nanoseconds
 * @ts:		pointer to the timespec variable to be converted
 *
 * Returns the scalar nanosecond representation of the timespec
 * parameter.
 */
static inline s64 timespec_to_ns(const struct timespec *ts)
{
	return ((s64) ts->tv_sec * NSEC_PER_SEC) + ts->tv_nsec;
}

/**
 * timeval_to_ns - Convert timeval to nanoseconds
 * @ts:		pointer to the timeval variable to be converted
 *
 * Returns the scalar nanosecond representation of the timeval
 * parameter.
 */
static inline s64 timeval_to_ns(const struct timeval *tv)
{
	return ((s64) tv->tv_sec * NSEC_PER_SEC) +
		tv->tv_usec * NSEC_PER_USEC;
}

/**
 * ns_to_timespec - Convert nanoseconds to timespec
 * @nsec:       the nanoseconds value to be converted
 *
 * Returns the timespec representation of the nsec parameter.
 */
static inline struct timespec ns_to_timespec(const s64 nsec)
{
	struct timespec ts;
	s32 rem;

	if (!nsec)
		return (struct timespec) {0, 0};

	ts.tv_sec = div_s64_rem(nsec, NSEC_PER_SEC, &rem);
	if (unlikely(rem < 0)) {
		ts.tv_sec--;
		rem += NSEC_PER_SEC;
	}
	ts.tv_nsec = rem;

	return ts;
}

/**
 * ns_to_timeval - Convert nanoseconds to timeval
 * @nsec:       the nanoseconds value to be converted
 *
 * Returns the timeval representation of the nsec parameter.
 */
static inline struct timeval ns_to_timeval(const s64 nsec)
{
	struct timespec ts = ns_to_timespec(nsec);
	struct timeval tv;

	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = (suseconds_t) ts.tv_nsec / 1000;

	return tv;
}

/**
 * timespec_add_ns - Adds nanoseconds to a timespec
 * @a:		pointer to timespec to be incremented
 * @ns:		unsigned nanoseconds value to be added
 *
 * This must always be inlined because its used from the x86-64 vdso,
 * which cannot call other kernel functions.
 */
static __always_inline void timespec_add_ns(struct timespec *a, u64 ns)
{
	a->tv_sec += __iter_div_u64_rem(a->tv_nsec + ns, NSEC_PER_SEC, &ns);
	a->tv_nsec = ns;
}

static inline bool itimerspec64_valid(const struct itimerspec64 *its)
{
	if (!timespec64_valid(&(its->it_interval)) ||
		!timespec64_valid(&(its->it_value)))
		return false;

	return true;
}

/**
 * time_after32 - compare two 32-bit relative times
 * @a:	the time which may be after @b
 * @b:	the time which may be before @a
 *
 * time_after32(a, b) returns true if the time @a is after time @b.
 * time_before32(b, a) returns true if the time @b is before time @a.
 *
 * Similar to time_after(), compare two 32-bit timestamps for relative
 * times.  This is useful for comparing 32-bit seconds values that can't
 * be converted to 64-bit values (e.g. due to disk format or wire protocol
 * issues) when it is known that the times are less than 68 years apart.
 */
#define time_after32(a, b)	((s32)((u32)(b) - (u32)(a)) < 0)
#define time_before32(b, a)	time_after32(a, b)

/**
 * time_between32 - check if a 32-bit timestamp is within a given time range
 * @t:	the time which may be within [l,h]
 * @l:	the lower bound of the range
 * @h:	the higher bound of the range
 *
 * time_before32(t, l, h) returns true if @l <= @t <= @h. All operands are
 * treated as 32-bit integers.
 *
 * Equivalent to !(time_before32(@t, @l) || time_after32(@t, @h)).
 */
#define time_between32(t, l, h) ((u32)(h) - (u32)(l) >= (u32)(t) - (u32)(l))
#endif
