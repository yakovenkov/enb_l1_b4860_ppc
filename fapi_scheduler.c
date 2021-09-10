/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdint.h>
#include <string.h>

#include "fapi.h"
#include "fapi_scheduler.h"

static scheduler_dl_config_request_t sched_dl_config_req[10];
static scheduler_ul_config_request_t sched_ul_config_req[10];

__attribute__((visibility("default"))) void fapi_scheduler_init()
{
	bzero(sched_dl_config_req, sizeof(scheduler_dl_config_request_t) * 10);
	bzero(sched_ul_config_req, sizeof(scheduler_ul_config_request_t) * 10);
}

__attribute__((visibility("default"))) scheduler_dl_config_request_t *fapi_scheduler_get_dl_config_request(
	int32_t frame, int32_t subframe)
{
	scheduler_dl_config_request_t *dl_config_req = &sched_dl_config_req[subframe];

	dl_config_req->frame = frame;
	dl_config_req->subframe = subframe;

	return dl_config_req;
}

__attribute__((visibility("default"))) scheduler_ul_config_request_t *fapi_scheduler_get_ul_config_request(
	int32_t frame, int32_t subframe)
{
	scheduler_ul_config_request_t *ul_config_req = &sched_ul_config_req[subframe];

	ul_config_req->frame = frame;
	ul_config_req->subframe = subframe;

	return ul_config_req;
}
