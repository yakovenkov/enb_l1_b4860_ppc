/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef LOG_H
#define LOG_H

#include <core/narg.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MAX_LOG_FILES 4
#define LOG_MAX_TTI_DIMENSIONS 2
typedef enum
{
	LOG_LEVEL_NONE = 0,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_WARNING,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_N_ITEMS
} log_level_e;

typedef enum
{
	HW,
	FAPI,
	L1,
	PHY,
	MAC,
	CORE,
	ASN1,
	L2_FAPI_GEN,
	TEST,
	LOG_COMP_MAX
} log_comp_e;

void log_set_tti(uint32_t tti);
void log_set_level(log_level_e level);
void log_line_internal(log_comp_e comp, log_level_e level, int nargs, ...);
void log_line_internal_array(log_comp_e comp, log_level_e level, int nargs, const char *file, int32_t line,
                             const char *msg, void *args);

#ifndef __FILENAME__
#define __FILENAME__ (__builtin_strrchr(__FILE__, '/') ? __builtin_strrchr(__FILE__, '/') + 1 : __FILE__)
#endif

#define LOG_LINE_COMPONENT_LEVEL(component, level, ...)                                                                \
	log_line_internal(component, level, PP_NARG(__VA_ARGS__) + 2, __FILENAME__, __LINE__, __VA_ARGS__)
#define LOG_HEX_COMPONENT_LEVEL(component, level, ...)                                                                 \
	log_hex_internal(component, level, PP_NARG(__VA_ARGS__) + 2, __FILENAME__, __LINE__, __VA_ARGS__)

#ifndef LOG_DISABLE

#define LOG_E(c, x...) LOG_LINE_COMPONENT_LEVEL(c, LOG_LEVEL_ERROR, x)
#define LOG_W(c, x...) LOG_LINE_COMPONENT_LEVEL(c, LOG_LEVEL_WARNING, x)
#define LOG_I(c, x...) LOG_LINE_COMPONENT_LEVEL(c, LOG_LEVEL_INFO, x)
#define LOG_D(c, x...) LOG_LINE_COMPONENT_LEVEL(c, LOG_LEVEL_DEBUG, x)

#define LOG_BY_NAME_I(ctx, comp, fmt, ...) LOG_I(comp, "[%s] " fmt, ctx->name, ##__VA_ARGS__)
#define LOG_BY_NAME_D(ctx, comp, fmt, ...) LOG_D(comp, "[%s] " fmt, ctx->name, ##__VA_ARGS__)
#define LOG_BY_NAME_E(ctx, comp, fmt, ...) LOG_E(comp, "[%s] " fmt, ctx->name, ##__VA_ARGS__)
#define LOG_BY_NAME_W(ctx, comp, fmt, ...) LOG_W(comp, "[%s] " fmt, ctx->name, ##__VA_ARGS__)

#define TRACE_E(VALUE)                                                                                                 \
	do                                                                                                                 \
	{                                                                                                                  \
		LOG_E(CORE, #VALUE ": %i\n", VALUE);                                                                           \
	} while(0)
#define TRACE_W(VALUE)                                                                                                 \
	do                                                                                                                 \
	{                                                                                                                  \
		LOG_W(CORE, #VALUE ": %i\n", VALUE);                                                                           \
	} while(0)
#define TRACE_I(VALUE)                                                                                                 \
	do                                                                                                                 \
	{                                                                                                                  \
		LOG_I(CORE, #VALUE ": %i\n", VALUE);                                                                           \
	} while(0)
#define TRACE_D(VALUE)                                                                                                 \
	do                                                                                                                 \
	{                                                                                                                  \
		LOG_D(CORE, #VALUE ": %i\n", VALUE);                                                                           \
	} while(0)

#else

#define LOG_E(c, x...)
#define LOG_W(c, x...)
#define LOG_I(c, x...)
#define LOG_D(c, x...)

#define LOG_BY_NAME_E(c, x...)
#define LOG_BY_NAME_W(c, x...)
#define LOG_BY_NAME_I(c, x...)
#define LOG_BY_NAME_D(c, x...)

#define TRACE_E(...)
#define TRACE_W(...)
#define TRACE_I(...)
#define TRACE_D(...)

#endif

#endif // LOG_H
