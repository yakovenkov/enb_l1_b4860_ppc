/*
 * Copyright (c) 2013-2021 Artem Shatalov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef INCLUDE_ENB_SCHEDULER_H_
#define INCLUDE_ENB_SCHEDULER_H_

//#include <enodeb/enodeb.h>
//#include <enb_types.h>
#include <stdint.h>

#include "fapi_interface.h"

#define FAPI_DL_CONFIG_MAX_PDU 64
#define FAPI_UL_CONFIG_MAX_PDU 64

typedef struct scheduler_dl_config_request_s //__attribute__((packed))
{
	uint32_t frame;
	uint32_t subframe;

	uint32_t number_pdcch_ofdm_symbols;
	uint32_t number_dci;
	uint32_t number_pdu;
	uint32_t number_tx_req;
	uint32_t number_hi;
	uint32_t number_dci0;
	uint32_t number_pdsch_rnti;
	uint32_t transmission_power_pcfich;

	//enodeb_schedule_request_t *schedule_request[FAPI_DL_CONFIG_MAX_PDU];
	fapi_dl_config_request_pdu_t dl_config_dci_list[FAPI_DL_CONFIG_MAX_PDU];
	fapi_dl_config_request_pdu_t dl_config_pdu_list[FAPI_DL_CONFIG_MAX_PDU];
	fapi_tx_request_pdu_t tx_request_pdu_list[FAPI_DL_CONFIG_MAX_PDU];
	fapi_hi_dci0_request_pdu_t dci0_list[FAPI_DL_CONFIG_MAX_PDU];
	fapi_hi_dci0_request_pdu_t hi_list[FAPI_DL_CONFIG_MAX_PDU];

} scheduler_dl_config_request_t;

typedef struct scheduler_ul_config_request_s //__attribute__((packed))
{
	uint32_t frame;
	uint32_t subframe;

	uint32_t number_pdu;
	uint32_t rach_prach_frequency_resources;
	uint32_t srs_present;

	fapi_ul_config_request_pdu_t ul_config_list[FAPI_UL_CONFIG_MAX_PDU];
} scheduler_ul_config_request_t;

#define NUM_DL_CONFIG_REQ_FRAMES 16
#define NUM_UL_CONFIG_REQ_FRAMES 16

void fapi_scheduler_init();
scheduler_dl_config_request_t *fapi_scheduler_get_dl_config_request(int32_t frame, int32_t subframe);
scheduler_ul_config_request_t *fapi_scheduler_get_ul_config_request(int32_t frame, int32_t subframe);

#endif /* INCLUDE_ENB_SCHEDULER_H_ */
