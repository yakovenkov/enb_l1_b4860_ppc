/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "core/time.h"

#include <stdbool.h>
#include <time.h>
#include <sys/time.h>

void timeval_add(const struct timeval* a, const struct timeval* b, struct timeval* out)
{
    out->tv_sec = a->tv_sec + b->tv_sec;
    out->tv_usec = a->tv_usec + b->tv_usec;
    if (out->tv_usec >= 1000000)
    {
        out->tv_sec++;
        out->tv_usec -= 1000000;
    }
}

bool timeval_sub(const struct timeval* a, const struct timeval* b, struct timeval* out)
{
    bool is_negative;
    if (a->tv_sec > b->tv_sec)
    {
        is_negative = false;
        if (a->tv_usec >= b->tv_usec)
        {
            out->tv_sec = a->tv_sec - b->tv_sec;
            out->tv_usec = a->tv_usec - b->tv_usec;
        }
        else
        {
            out->tv_sec = a->tv_sec - b->tv_sec - 1;
            out->tv_usec = 1000000 - b->tv_usec + a->tv_usec;
        }
    }
    else if (a->tv_sec < b->tv_sec)
    {
        is_negative = true;
        if (a->tv_usec > b->tv_usec)
        {
            out->tv_sec = b->tv_sec - a->tv_sec - 1;
            out->tv_usec = 1000000 - a->tv_usec + b->tv_usec;
        }
        else
        {
            out->tv_sec = b->tv_sec - a->tv_sec;
            out->tv_usec = b->tv_usec - a->tv_usec;
        }
    }
    else
    {
        out->tv_sec = 0;
        if (a->tv_usec >= b->tv_usec)
        {
            is_negative = false;
            out->tv_usec = a->tv_usec - b->tv_usec;
        }
        else
        {
            is_negative = true;
            out->tv_usec = b->tv_usec - a->tv_usec;
        }
    }
    return is_negative;
}

uint64_t timeval_get_monotone(const struct timeval* a)
{
	return (a->tv_sec*1000000 + a->tv_usec);
}

void timeval_from_monotone_us(uint64_t monotone_us, struct timeval* out)
{
    out->tv_sec = monotone_us / 1000000;
    out->tv_usec = monotone_us % 1000000;
}

void set_current_date_to_str(char* buf, int buf_size, const char* format)
{
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);
	strftime(buf, buf_size, format, timeinfo);
}

void set_current_time_to_str(char* buf, int buf_size)
{
	time_t rawtime;
	struct tm *timeinfo;

	time(&rawtime);
	timeinfo = localtime(&rawtime);
	
	strftime(buf, buf_size, "%H:%M:%S", timeinfo);	
}