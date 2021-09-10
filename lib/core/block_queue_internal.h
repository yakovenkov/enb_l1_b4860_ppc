/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef BLOCK_QUEUE_INTERNAL_H
#define BLOCK_QUEUE_INTERNAL_H

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef BLOCK_QUEUE_INTERNAL
/* Internal use only */

typedef struct queue_item_s
{
	void *ptr;
	size_t len;
} queue_item_t;

typedef struct block_queue_s
{
	pthread_mutex_t mutex;
	pthread_cond_t not_empty_cond;
	pthread_cond_t not_full_cond;
	int32_t head, tail;
	int32_t len;
	queue_item_t *queue;

	bool pop_break_flag;
	bool push_break_flag;

	int32_t num_items;            //текущее количество элементов в очереди
	size_t accumulated_items_len; //общий размер элементов в очереди
} block_queue_t;
#endif

#endif