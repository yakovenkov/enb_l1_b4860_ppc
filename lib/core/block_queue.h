/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef BLOCK_QUEUE_INTERNAL
typedef struct block_queue_s block_queue_t;
#else
#include "core/block_queue_internal.h"
#endif

block_queue_t *block_queue_init(int32_t min_queue_len);

bool block_queue_push(block_queue_t *q, void *item, int32_t item_size);
bool block_queue_try_push(block_queue_t *q, void *item, int32_t item_size);

void *block_queue_pop_last(block_queue_t *q);
bool block_queue_try_pop(block_queue_t *q, void **item);
bool block_queue_try_pop_timeout(block_queue_t *q, void **item, uint64_t timeout_us);
void *block_queue_pop(block_queue_t *q);

void block_queue_break_pop(block_queue_t *q);
void block_queue_break_push(block_queue_t *q);
void block_queue_break(block_queue_t *q);
int32_t block_queue_is_empty(block_queue_t *q);
void block_queue_clear(block_queue_t *q);

int32_t block_queue_num_items(const block_queue_t *q);
size_t block_queue_accumulated_items_len(const block_queue_t *q);

#endif