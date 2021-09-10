/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#define BLOCK_QUEUE_INTERNAL

#include <assert.h>
#include <malloc.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>

#include <sys/time.h>
#include <time.h>

#include "core/block_queue.h"
#include "core/thread_helper.h"
#include "core/time.h"

static int power_of_two_round_up(int n)
{
	n = n - 1;

	while(n & (n - 1))
		n = n & (n - 1);

	return n << 1;
}

__attribute__((visibility("default"))) block_queue_t *block_queue_init(int32_t min_queue_len)
{
	block_queue_t *q;

	q = calloc(1, sizeof(block_queue_t));
	assert(q != NULL);

	q->len = power_of_two_round_up(min_queue_len);
	q->queue = calloc(q->len, sizeof(queue_item_t));

	q->pop_break_flag = false;
	q->push_break_flag = false;

	q->accumulated_items_len = 0;
	q->num_items = 0;

	assert(q->queue != NULL);

	thread_helper_mutex_init_pi(&q->mutex);
	pthread_cond_init(&q->not_empty_cond, NULL);
	pthread_cond_init(&q->not_full_cond, NULL);

	return q;
}

__attribute__((visibility("default"))) bool block_queue_push(block_queue_t *q, void *item, int32_t item_len)
{
	pthread_mutex_lock(&q->mutex);

	while((q->num_items == q->len) && (!q->push_break_flag))
	{
		pthread_cond_wait(&q->not_full_cond, &q->mutex);
		if(q->push_break_flag)
			break;
	}

	if(!q->push_break_flag)
	{
		q->queue[q->head].ptr = item;
		q->queue[q->head].len = item_len;
		q->head = (q->head + 1) & (q->len - 1);
		q->accumulated_items_len += item_len;
		q->num_items++;
		pthread_cond_broadcast(&q->not_empty_cond);
	}

	pthread_mutex_unlock(&q->mutex);
	return !q->push_break_flag;
}

__attribute__((visibility("default"))) bool block_queue_try_push(block_queue_t *q, void *item, int32_t item_len)
{
	pthread_mutex_lock(&q->mutex);

	if((q->num_items == q->len))
	{
		pthread_mutex_unlock(&q->mutex);
		return false;
	}

	q->queue[q->head].ptr = item;
	q->queue[q->head].len = item_len;
	q->head = (q->head + 1) & (q->len - 1);
	q->accumulated_items_len += item_len;
	q->num_items++;

	pthread_cond_broadcast(&q->not_empty_cond);

	pthread_mutex_unlock(&q->mutex);
	return true;
}

__attribute__((visibility("default"))) bool block_queue_try_pop(block_queue_t *q, void **item)
{
	pthread_mutex_lock(&q->mutex);

	if(q->num_items == 0)
	{
		pthread_mutex_unlock(&q->mutex);
		return false;
	}

	*item = q->queue[q->tail].ptr;
	q->accumulated_items_len -= q->queue[q->tail].len;
	q->num_items--;
	q->tail = (q->tail + 1) & (q->len - 1);
	pthread_cond_broadcast(&q->not_full_cond);

	pthread_mutex_unlock(&q->mutex);
	return true;
}

__attribute__((visibility("default"))) bool block_queue_try_pop_timeout(block_queue_t *q, void **item,
                                                                        uint64_t timeout_us)
{
	int rv = 0;

	struct timeval timeout_tv;
	timeval_from_monotone_us(timeout_us, &timeout_tv);

	struct timeval now_tv;
	struct timeval next_tv;

	gettimeofday(&now_tv, NULL);
	timeval_add(&now_tv, &timeout_tv, &next_tv);

	struct timespec ts = {next_tv.tv_sec, next_tv.tv_usec * 1000};

	pthread_mutex_lock(&q->mutex);

	if((q->num_items == 0) && (!q->pop_break_flag))
	{
		rv = pthread_cond_timedwait(&q->not_empty_cond, &q->mutex, &ts);
	}

	if(rv != 0 || q->pop_break_flag || (q->num_items == 0))
	{
		pthread_mutex_unlock(&q->mutex);
		return false;
	}

	*item = q->queue[q->tail].ptr;
	q->accumulated_items_len -= q->queue[q->tail].len;
	q->num_items--;
	q->tail = (q->tail + 1) & (q->len - 1);

	pthread_cond_broadcast(&q->not_full_cond);
	pthread_mutex_unlock(&q->mutex);

	return true;
}

__attribute__((visibility("default"))) void *block_queue_pop(block_queue_t *q)
{
	void *item = NULL;

	pthread_mutex_lock(&q->mutex);

	while((q->num_items == 0) && (!q->pop_break_flag))
	{
		pthread_cond_wait(&q->not_empty_cond, &q->mutex);
		if(q->pop_break_flag)
			break;
	}

	item = (q->pop_break_flag) ? NULL : q->queue[q->tail].ptr;
	q->accumulated_items_len -= q->queue[q->tail].len;
	q->num_items--;

	q->pop_break_flag = false;
	q->tail = (q->tail + 1) & (q->len - 1);

	pthread_cond_broadcast(&q->not_full_cond);
	pthread_mutex_unlock(&q->mutex);

	return item;
}

__attribute__((visibility("default"))) int32_t block_queue_is_empty(block_queue_t *q)
{
	int32_t ret = 0;

	// queue is empty?
	pthread_mutex_lock(&q->mutex);

	ret = (q->head == q->tail);

	pthread_mutex_unlock(&q->mutex);

	return ret;
}

__attribute__((visibility("default"))) void block_queue_break_pop(block_queue_t *q)
{
	pthread_mutex_lock(&q->mutex);
	q->pop_break_flag = true;
	pthread_cond_broadcast(&q->not_empty_cond);
	pthread_mutex_unlock(&q->mutex);
}

__attribute__((visibility("default"))) void block_queue_break_push(block_queue_t *q)
{
	pthread_mutex_lock(&q->mutex);
	q->push_break_flag = true;
	pthread_cond_broadcast(&q->not_full_cond);
	pthread_mutex_unlock(&q->mutex);
}

__attribute__((visibility("default"))) void block_queue_break(block_queue_t *q)
{
	block_queue_break_push(q);
	block_queue_break_pop(q);
}

__attribute__((visibility("default"))) void block_queue_clear(block_queue_t *q)
{
	pthread_mutex_lock(&q->mutex);

	q->pop_break_flag = true;
	q->push_break_flag = true;
	q->head = 0;
	q->tail = 0;
	q->num_items = 0;
	q->accumulated_items_len = 0;
	pthread_cond_broadcast(&q->not_empty_cond);
	pthread_cond_broadcast(&q->not_full_cond);

	pthread_mutex_unlock(&q->mutex);
}

__attribute__((visibility("default"))) void *block_queue_pop_last(block_queue_t *q)
{
	void *item = NULL;

	pthread_mutex_lock(&q->mutex);

	while((q->num_items == 0) && (!q->pop_break_flag))
	{
		pthread_cond_wait(&q->not_empty_cond, &q->mutex);
		if(q->pop_break_flag)
			break;
	}

	q->tail = q->head > 0 ? q->head - 1 : q->len - 1;
	item = q->pop_break_flag ? NULL : q->queue[q->tail].ptr;
	q->accumulated_items_len -= q->queue[q->tail].len;
	q->num_items--;

	q->pop_break_flag = false;
	q->tail = (q->tail + 1) & (q->len - 1);
	pthread_cond_broadcast(&q->not_full_cond);

	pthread_mutex_unlock(&q->mutex);

	return item;
}

__attribute__((visibility("default"))) int32_t block_queue_num_items(const block_queue_t *q)
{
	return q->num_items;
}

__attribute__((visibility("default"))) size_t block_queue_accumulated_items_len(const block_queue_t *q)
{
	return q->accumulated_items_len;
}