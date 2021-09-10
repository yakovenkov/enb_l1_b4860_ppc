/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef _FILE_H
#define _FILE_H

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

int read_file(const char* file, uint8_t** buffer, int* buffer_size);

#endif
