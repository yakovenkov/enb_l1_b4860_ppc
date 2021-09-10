/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef _FAPI_B4860_CPRI_ETH_H_
#define _FAPI_B4860_CPRI_ETH_H_

#include "fapi.h"
#include "fapi_b4860.h"
#include "fapi_scheduler.h"
#include "fapi_b4860_ir.h"

typedef struct ir_cfg_req_s
{
	uint8_t bbu_port;
	uint8_t rru_id;
	uint8_t rru_mac[6];
} __attribute__((__packed__)) ir_cfg_req_t;

typedef struct ir_cfq_resp_s
{
	uint8_t bbu_port;
	uint8_t rru_id;
	uint8_t bbu_id;
	uint8_t rru_mac[6];
	uint32_t rru_ip;
	uint32_t bbu_ip;
	uint32_t netmask;
} __attribute__((__packed__)) ir_cfg_resp_t;

typedef struct cpri_pkt_s
{
	void *data;
	int32_t len;
} cpri_pkt_t;

typedef enum 
{
	RRU_STATE_NONE=0,
	RRU_STATE_INIT,
	RRU_STATE_CONNECTED,
	RRU_STATE_CONN_EST_RECV,
	RRU_STATE_CONN_SETUP_SENT,
	RRU_STATE_CONN_SETUP_ACK,
	RRU_STATE_NEED_PARAMS_CONFIG,
	RRU_STATE_WAIT_PARAMS_CONFIG_RESP,
	RRU_STATE_WAIT_CELL_CONFIG_RESP,
	RRU_STATE_WAIT_CELL_CLEAR_RESP,
	RRU_STATE_WAIT,
	RRU_STATE_READY
} rru_state_e;

// RRU structure
typedef struct rru_s
{
	int32_t id; // RRU id
	int32_t bbu_id; // ???
	int32_t port; // BBU port
	int32_t fd; // socket fd

	rru_state_e state;

	int32_t timer;

	uint8_t mac[8];
	uint32_t ip;

	struct RRU *cfg;

	int32_t timeout;

	uint32_t serial_out;
	uint32_t serial_in;

	uint32_t heartbeat_counter;
	uint32_t need_reset;

	ir_ie_1_t ie_1;
} rru_t;

void cpri_eth_init();
void cpri_eth_start();
void cpri_eth_stop();
void cpri_eth_process_cpri_msg(fapi_ipc_msg_t *msg);

#endif