/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef CORE_TIME_H
#define CORE_TIME_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <sys/time.h>

void timeval_add(const struct timeval* a, const struct timeval* b, struct timeval* out);
bool timeval_sub(const struct timeval* a, const struct timeval* b, struct timeval* out);
uint64_t timeval_get_monotone(const struct timeval* a);
void timeval_from_monotone_us(uint64_t monotone_us, struct timeval* out);

void set_current_date_to_str(char* buf, int buf_size, const char* format);
void set_current_time_to_str(char* buf, int buf_size);

#endif /* CORE_TIME_H */
