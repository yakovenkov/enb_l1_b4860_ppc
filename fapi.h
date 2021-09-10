/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef _FAPI_H_
#define _FAPI_H_

#include "core/block_queue.h"

#include "fapi_interface.h"
#include "fapi_scheduler.h"

int32_t fapi_init();
void fapi_stop();

block_queue_t* fapi_get_subframe_ind_queue();
block_queue_t* fapi_get_rach_ind_queue();
block_queue_t* fapi_get_rx_ind_queue();
block_queue_t* fapi_get_harq_ind_queue();
block_queue_t* fapi_get_sr_ind_queue();
block_queue_t* fapi_get_cqi_ind_queue();
block_queue_t* fapi_get_crc_ind_queue();
block_queue_t* fapi_get_p8_ind_queue();

int32_t fapi_p5_start_request();
int32_t fapi_p5_stop_request();
int32_t fapi_is_connected();
int32_t fapi_p5_config_request(fapi_config_t *config, fapi_config_response_t **resp);
int32_t fapi_p7_dl_config_request(scheduler_dl_config_request_t *dl_cfg_req);
int32_t fapi_p7_ul_config_request(scheduler_ul_config_request_t *ul_cfg_req);
int32_t fapi_p7_tx_request(scheduler_dl_config_request_t *dl_cfg_req);
int32_t fapi_p7_hi_dci0_request(scheduler_dl_config_request_t *dl_cfg_req);

#endif /* _FAPI_H_ */
