/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "core/file.h"

#include <stdio.h>

__attribute__((visibility("default"))) int read_file(const char* file, uint8_t** buffer, int* buffer_size)
{
	FILE *f = fopen(file, "r");

	int num_bytes_readed = 0;

	if(f != NULL)
	{
		fseek(f, 0L, SEEK_END);
		int file_size = ftell(f);

		fseek(f, 0L, SEEK_SET);
		*buffer = calloc(1, file_size);
		*buffer_size = file_size;
		num_bytes_readed = fread(*buffer, 1, *buffer_size, f);
		fclose(f);
	}
	else
	{
		return -1;
	}

	return num_bytes_readed;
}
