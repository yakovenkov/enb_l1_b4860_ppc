/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef FAPI_IPC_H_
#define FAPI_IPC_H_

#define FAPI_IPC_PA2SC_BD_RING_SIZE	16
#define FAPI_IPC_MAX_SC2PA_MSG_SIZE	16384
#define FAPI_IPC_MAX_PA2SC_MSG_SIZE	1020
#define FAPI_IPC_MAX_PA2SC_PTR_SIZE	65536

#define FAPI_IPC_MAX_SC2PA_MSGS_NUM	64
#define FAPI_IPC_MAX_PA2SC_MSGS_NUM	256
#define FAPI_IPC_MAX_PA2SC_PTR_NUM	16

#define FAPI_IPC_RX_IND_SC2PA_MSGS_NUM	16
#define FAPI_IPC_RX_IND_SC2PA_MSG_SIZE (128 * 1024)	

typedef enum
{
    IPC_DEDICATED_CH_ID = 0,
    IPC_RESERVED1,
    IPC_MSG_CH_ID,
    IPC_RESERVED3,
    IPC_PA2SC_CH_ID,
    IPC_SC2PA_CH_ID,
    NUM_IPC_CH
} ipc_channels_e;

typedef enum
{
	FAPI_CHANNEL_P5_REQ = 0,
	FAPI_CHANNEL_P5_IND,
	FAPI_CHANNEL_P7_REQ,
	FAPI_CHANNEL_P7_IND,
	FAPI_CHANNEL_P7_TX_REQ,
	FAPI_CHANNEL_LOG,

	FAPI_CHANNELS_NUM
} fapi_channels_e;

typedef struct
{
	uint32_t channel_id;
	uint32_t length;
	uint32_t body_addr;
	union
	{
		uint32_t reserved;
		uint32_t phys_addr;
	};
} fapi_ipc_msg_t;

#if 0
#ifdef __powerpc__
/*
 * Со стороны PA сообщения fapi_ipc_msg_t отправляются по каналу MSG
 * Следовательно, в буфере не надо учитывать их длину
 */
#define FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, l1_msg) { ipc_msg->length = l1_msg->header.message_length; }
#else
/*
 * Со стороны SC сообщения fapi_ipc_msg_t отправляются через общий буфер
 * Следовательно, их длина должна учитываться
 */
#define FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, l1_msg) { ipc_msg->length = sizeof(fapi_ipc_msg_t) + l1_msg->header.message_length; }
#endif
#else
#define FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, l1_msg) { ipc_msg->length = sizeof(fapi_ipc_msg_t) + l1_msg->header.message_length; }
#endif
#endif /* FAPI_IPC_H_ */
