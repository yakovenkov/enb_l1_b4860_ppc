/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef L2_FAPI_GENERATOR_DEFS_H
#define L2_FAPI_GENERATOR_DEFS_H

#include <pthread.h>
#include <stdint.h>

#include "l2_fapi_generator/l2_fapi_generator.h"

#include <core/thread_helper.h>

#include <fapi.h>

//#define N_ENB_PHY_MAX	32
#define N_MAC_PENDING_RACH_MAX 8
#define N_MAX_LC 6
#define N_SCHED_RESULT_MAX 128
#define N_MAX_DATA_LIST 32
#define N_MAX_BC_LIST 8
#define N_MAX_RLC_PDU_LIST 8
#define N_MAX_PHICH_LIST 8
#define N_MAX_BCCH_PDU 32
#define N_MAX_BCCH_PDU_LEN 1024
#define N_MAX_RAR_PDU 8
#define N_MAX_RAR_PDU_LEN 16
#define N_MAX_PENDING_ULSCH_REQUESTS 32
#define MAX_P8_IND_DATA_MAX_LEN (1024 * 1024)

#define TTI_INTERVAL(FAR, NEAR) (FAR > NEAR ? (FAR - NEAR) : (10240 - NEAR + FAR))

typedef struct enb_4g_phy_proc_s
{
	uint32_t tti_tx;
	uint32_t tti_rx;
	uint32_t tti_sched_ul;

	uint32_t frame_rx;
	uint32_t frame_tx;
	uint32_t frame_sched_ul;

	uint32_t sf_rx;
	uint32_t sf_tx;
	uint32_t sf_sched_ul;

	// Helper TTIs
	uint64_t abs_tti_tx;
	uint64_t abs_tti_rx;

	fapi_rx_indication_t *rx_ind_q[10];
	fapi_harq_indication_t *harq_ind_q[10];
	fapi_sr_indication_t *sr_ind_q[10];
	fapi_cqi_indication_t *cqi_ind_q[10];
	fapi_crc_indication_t *crc_ind_q[10];

	uint8_t pbch_pdu[4];
	
	int32_t tx_pdu_index;

} enb_4g_phy_proc_t;

typedef struct enb_4g_phy_s
{

	int32_t running;
	const l2_fapi_generator_config_t* config;
	enb_4g_phy_proc_t proc;
} enb_4g_phy_t;

typedef struct l2_fapi_generator_sched_result_s
{
	int32_t cfi;

	int32_t n_dci;
	int32_t n_dlsch;
	int32_t n_txreq;
	int32_t n_ulsch;

	fapi_ul_config_ulsch_pdu ulsch_pdu[N_SCHED_RESULT_MAX];
	fapi_dl_config_dci_dl_pdu dci_pdu[N_SCHED_RESULT_MAX];
	fapi_dl_config_dlsch_pdu dlsch_pdu[N_SCHED_RESULT_MAX];
	fapi_tx_request_pdu_t txreq_pdu[N_SCHED_RESULT_MAX];

	// BCCH PDUs buffers
	int32_t n_bcch_pdus;
	uint8_t bcch_pdu[N_MAX_BCCH_PDU][N_MAX_BCCH_PDU_LEN];

} l2_fapi_generator_sched_result_t;

typedef struct l2_fapi_generator_stim_item_s
{
	uint32_t tti;
	scheduler_dl_config_request_t dl_cfg_req_list;
	scheduler_ul_config_request_t ul_cfg_req_list;
} l2_fapi_generator_stim_item_t;

typedef struct l2_fapi_generator_s
{
	int32_t no;
	int32_t running;

	// Счетчик абсолютных tti
	uint32_t abs_tti;

	l2_fapi_generator_config_t config;
	enb_4g_phy_t phy;

	bool event_dispatcher_running;

	int32_t stim_ptr;
	int32_t stim_len;
	l2_fapi_generator_stim_item_t *stim_list;

} l2_fapi_generator_t;

#endif // L2_FAPI_GENERATOR_DEFS_H