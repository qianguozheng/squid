
/*
 * $Id: rfc1123.c,v 1.12 1998/01/05 00:57:53 wessels Exp $
 *
 * DEBUG: 
 * AUTHOR: Harvest Derived
 *
 * SQUID Internet Object Cache  http://squid.nlanr.net/Squid/
 * --------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from the
 *  Internet community.  Development is led by Duane Wessels of the
 *  National Laboratory for Applied Network Research and funded by
 *  the National Science Foundation.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *  
 */

/*
 * Copyright (c) 1994, 1995.  All rights reserved.
 *  
 *   The Harvest software was developed by the Internet Research Task
 *   Force Research Group on Resource Discovery (IRTF-RD):
 *  
 *         Mic Bowman of Transarc Corporation.
 *         Peter Danzig of the University of Southern California.
 *         Darren R. Hardy of the University of Colorado at Boulder.
 *         Udi Manber of the University of Arizona.
 *         Michael F. Schwartz of the University of Colorado at Boulder.
 *         Duane Wessels of the University of Colorado at Boulder.
 *  
 *   This copyright notice applies to software in the Harvest
 *   ``src/'' directory only.  Users should consult the individual
 *   copyright notices in the ``components/'' subdirectories for
 *   copyright information about other software bundled with the
 *   Harvest source code distribution.
 *  
 * TERMS OF USE
 *   
 *   The Harvest software may be used and re-distributed without
 *   charge, provided that the software origin and research team are
 *   cited in any use of the system.  Most commonly this is
 *   accomplished by including a link to the Harvest Home Page
 *   (http://harvest.cs.colorado.edu/) from the query page of any
 *   Broker you deploy, as well as in the query result pages.  These
 *   links are generated automatically by the standard Broker
 *   software distribution.
 *   
 *   The Harvest software is provided ``as is'', without express or
 *   implied warranty, and with no support nor obligation to assist
 *   in its use, correction, modification or enhancement.  We assume
 *   no liability with respect to the infringement of copyrights,
 *   trade secrets, or any patents, and are not responsible for
 *   consequential damages.  Proper use of the Harvest software is
 *   entirely the responsibility of the user.
 *  
 * DERIVATIVE WORKS
 *  
 *   Users may make derivative works from the Harvest software, subject 
 *   to the following constraints:
 *  
 *     - You must include the above copyright notice and these 
 *       accompanying paragraphs in all forms of derivative works, 
 *       and any documentation and other materials related to such 
 *       distribution and use acknowledge that the software was 
 *       developed at the above institutions.
 *  
 *     - You must notify IRTF-RD regarding your distribution of 
 *       the derivative work.
 *  
 *     - You must clearly notify users that your are distributing 
 *       a modified version and not the original Harvest software.
 *  
 *     - Any derivative product is also subject to these copyright 
 *       and use restrictions.
 *  
 *   Note that the Harvest software is NOT in the public domain.  We
 *   retain copyright, as specified above.
 *  
 * HISTORY OF FREE SOFTWARE STATUS
 *  
 *   Originally we required sites to license the software in cases
 *   where they were going to build commercial products/services
 *   around Harvest.  In June 1995 we changed this policy.  We now
 *   allow people to use the core Harvest software (the code found in
 *   the Harvest ``src/'' directory) for free.  We made this change
 *   in the interest of encouraging the widest possible deployment of
 *   the technology.  The Harvest software is really a reference
 *   implementation of a set of protocols and formats, some of which
 *   we intend to standardize.  We encourage commercial
 *   re-implementations of code complying to this set of standards.  
 */

#include "config.h"


/*
 *  Adapted from HTSUtils.c in CERN httpd 3.0 (http://info.cern.ch/httpd/)
 *  by Darren Hardy <hardy@cs.colorado.edu>, November 1994.
 */
#if HAVE_STDIO_H
#include <stdio.h>
#endif
#if HAVE_STRING_H
#include <string.h>
#endif
#if HAVE_CTYPE_H
#include <ctype.h>
#endif
#if HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif
#if HAVE_TIME_H
#include <time.h>
#endif
#if HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#include "util.h"
#include "snprintf.h"

#define RFC850_STRFTIME "%A, %d-%b-%y %H:%M:%S GMT"
#define RFC1123_STRFTIME "%a, %d %b %Y %H:%M:%S GMT"

static int make_month(const char *s);
static int make_num(const char *s);

static char *month_names[12] =
{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};


static int
make_num(const char *s)
{
    if (*s >= '0' && *s <= '9')
	return 10 * (*s - '0') + *(s + 1) - '0';
    else
	return *(s + 1) - '0';
}

static int
make_month(const char *s)
{
    int i;
    char month[3];

    month[0] = toupper(*s);
    month[1] = tolower(*(s + 1));
    month[2] = tolower(*(s + 2));

    for (i = 0; i < 12; i++)
	if (!strncmp(month_names[i], month, 3))
	    return i;
    return 0;
}


time_t
parse_rfc1123(const char *str)
{
    const char *s;
    struct tm tm;
    time_t t;

    if (!str)
	return -1;

    memset(&tm, '\0', sizeof(struct tm));
    if ((s = strchr(str, ','))) {	/* Thursday, 10-Jun-93 01:29:59 GMT */
	s++;			/* or: Thu, 10 Jan 1993 01:29:59 GMT */
	while (*s && *s == ' ')
	    s++;
	if (strchr(s, '-')) {	/* First format */
	    if ((int) strlen(s) < 18)
		return -1;
	    tm.tm_mday = make_num(s);
	    tm.tm_mon = make_month(s + 3);
	    tm.tm_year = make_num(s + 7);
	    tm.tm_hour = make_num(s + 10);
	    tm.tm_min = make_num(s + 13);
	    tm.tm_sec = make_num(s + 16);
	} else {		/* Second format */
	    if ((int) strlen(s) < 20)
		return -1;
	    tm.tm_mday = make_num(s);
	    tm.tm_mon = make_month(s + 3);
	    tm.tm_year = (100 * make_num(s + 7) - 1900) + make_num(s + 9);
	    tm.tm_hour = make_num(s + 12);
	    tm.tm_min = make_num(s + 15);
	    tm.tm_sec = make_num(s + 18);

	}
    } else {			/* Try the other format:        */
	s = str;		/* Wed Jun  9 01:29:59 1993 GMT */
	while (*s && *s == ' ')
	    s++;
	if ((int) strlen(s) < 24)
	    return -1;
	tm.tm_mday = make_num(s + 8);
	tm.tm_mon = make_month(s + 4);
	tm.tm_year = make_num(s + 22);
	tm.tm_hour = make_num(s + 11);
	tm.tm_min = make_num(s + 14);
	tm.tm_sec = make_num(s + 17);
    }
    if (tm.tm_sec < 0 || tm.tm_sec > 59 ||
	tm.tm_min < 0 || tm.tm_min > 59 ||
	tm.tm_hour < 0 || tm.tm_hour > 23 ||
	tm.tm_mday < 1 || tm.tm_mday > 31 ||
	tm.tm_mon < 0 || tm.tm_mon > 11 ||
	tm.tm_year < 70 || tm.tm_year > 120) {
	return -1;
    }
    tm.tm_isdst = -1;

#ifdef HAVE_TIMEGM
    t = timegm(&tm);
#elif HAVE_TM_GMTOFF
    t = mktime(&tm);
    {
	time_t cur_t = time(NULL);
	struct tm *local = localtime(&cur_t);
	t += local->tm_gmtoff;
	/*
	 * The following assumes a fixed DST offset of 1 hour,
	 * which is probably wrong.
	 */
	if (tm.tm_isdst > 0)
	    t += 3600;
    }
#else
    /* some systems do not have tm_gmtoff so we fake it */
    t = mktime(&tm);
    {
	time_t dst = 0;
#ifndef _TIMEZONE
	extern time_t timezone;
#endif /* _TIMEZONE */
	/*
	 * The following assumes a fixed DST offset of 1 hour,
	 * which is probably wrong.
	 */
	if (tm.tm_isdst > 0)
	    dst = -3600;
	t -= (timezone + dst);
    }
#endif
    return t;
}

const char *
mkrfc1123(time_t t)
{
    static char buf[128];

    struct tm *gmt = gmtime(&t);

    buf[0] = '\0';
    strftime(buf, 127, RFC1123_STRFTIME, gmt);
    return buf;
}

const char *
mkhttpdlogtime(const time_t * t)
{
    static char buf[128];

    struct tm *gmt = gmtime(t);

#ifndef USE_GMT
    int gmt_min, gmt_hour, gmt_yday, day_offset;
    size_t len;
    struct tm *lt;
    int min_offset;

    /* localtime & gmtime may use the same static data */
    gmt_min = gmt->tm_min;
    gmt_hour = gmt->tm_hour;
    gmt_yday = gmt->tm_yday;

    lt = localtime(t);
    day_offset = lt->tm_yday - gmt_yday;
    min_offset = day_offset * 1440 + (lt->tm_hour - gmt_hour) * 60
	+ (lt->tm_min - gmt_min);

    /* wrap round on end of year */
    if (day_offset > 1)
	day_offset = -1;
    else if (day_offset < -1)
	day_offset = 1;

    len = strftime(buf, 127 - 5, "%d/%b/%Y:%H:%M:%S ", lt);
    snprintf(buf + len, 128-len, "%+03d%02d",
	(min_offset / 60) % 24,
	min_offset % 60);
#else /* USE_GMT */
    buf[0] = '\0';
    strftime(buf, 127, "%d/%b/%Y:%H:%M:%S -000", gmt);
#endif /* USE_GMT */

    return buf;
}

#if 0
int
main()
{
    char *x;
    time_t t, pt;

    t = time(NULL);
    x = mkrfc1123(t);
    printf("HTTP Time: %s\n", x);

    pt = parse_rfc1123(x);
    printf("Parsed: %d vs. %d\n", pt, t);
}

#endif
