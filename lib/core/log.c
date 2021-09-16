/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <core/log.h>
#include <stdio.h>
#include <stdarg.h>

static uint32_t log_tti;
static log_level_e g_log_level = LOG_LEVEL_DEBUG;

#define BOUND_ARG(b)                                                                                                   \
	b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15], b[16],       \
		b[17], b[18], b[19]

__attribute__((visibility("default"))) void log_set_tti(uint32_t tti)
{
	log_tti = tti;
}

__attribute__((visibility("default"))) void log_set_level(log_level_e level)
{
	g_log_level = level;
}

__attribute__((visibility("default"))) void log_line_internal(log_comp_e comp, log_level_e level, int nargs, ...)
{
	va_list args;
	va_start(args, nargs);

	char file = va_arg(args, char *);
	int line = va_arg(args, int);

	char *msg = va_arg(args, char *);
	
	if(level <= g_log_level)
	{
		vprintf(msg, args);
	}

	va_end(args);
}

__attribute__((visibility("default"))) void log_line_internal_array(log_comp_e comp, log_level_e level, int nargs,
                                                                    const char *file, int32_t line, const char *msg,
                                                                    void *args)
{
	uint64_t *args64 = (uint64_t *)args;
	
	if(level <= g_log_level)
	{
		printf(msg, BOUND_ARG(args64));
	}
}
