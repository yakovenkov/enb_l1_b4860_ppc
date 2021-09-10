/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <signal.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sched.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdbool.h>
#include <time.h>

#include <core/block_queue.h>
#include <core/log.h>
#include <core/thread_helper.h>

#include "fapi_b4860.h"
#include "fapi_interface.h"

#include "libb4860/ipc/include/fsl_bsc913x_ipc.h"
#include "libb4860/ipc/include/fsl_ipc_errorcodes.h"
#include "libb4860/ipc/include/fsl_usmmgr.h"

#include "fapi.h"
#include "fapi_scheduler.h"
//#include "ran/coreset.h"

#include "fapi_b4860_cpri_eth.h"

#include "fapi_b4860_cfg.h"

#include "p8.h"

// Init CPRI RRU at IPC initialization early level
//#define CPRI_RRU_INIT_EARLY

//#define INIT_IPC_IN_FSL_THREAD
//#define FAPI_RT_SIGNAL

#include "libb4860/ipc/include/fsl_usmmgr.h"
#include "libb4860/ipc/include/fsl_bsc913x_ipc.h"
#include "libb4860/ipc/include/fsl_ipc_errorcodes.h"
#include <fapi_b4860_v1_version.h>

/*
 * Memory barrier.
 * The sync instruction guarantees that all memory accesses initiated
 * by this processor have been performed (with respect to all other
 * mechanisms that access memory).  The eieio instruction is a barrier
 * providing an ordering (separately) for (a) cacheable stores and (b)
 * loads and stores to non-cacheable memory (e.g. I/O devices).
 *
 * mb() prevents loads and stores being reordered across this point.
 * rmb() prevents loads being reordered across this point.
 * wmb() prevents stores being reordered across this point.
 * read_barrier_depends() prevents data-dependent loads being reordered
 *      across this point (nop on PPC).
 *
 * *mb() variants without smp_ prefix must order all types of memory
 * operations with one another. sync is the only instruction sufficient
 * to do this.
 *
 * For the smp_ barriers, ordering is for cacheable memory operations
 * only. We have to use the sync instruction for smp_mb(), since lwsync
 * doesn't order loads with respect to previous stores.  Lwsync can be
 * used for smp_rmb() and smp_wmb().
 *
 * However, on CPUs that don't support lwsync, lwsync actually maps to a
 * heavy-weight sync, so smp_wmb() can be a lighter-weight eieio.
 */
#define mb()   __asm__ __volatile__ ("sync" : : : "memory")
#define rmb()  __asm__ __volatile__ ("sync" : : : "memory")
#define wmb()  __asm__ __volatile__ ("sync" : : : "memory")

#define lwsync() \
        do { \
                asm volatile ("lwsync" : : : "memory"); \
        } while (0)

#define dcbf(p) \
        do { \
               asm volatile ("dcbf 0,%0" : : "r" (p)); \
       } while (0)

#define L1_CACHE_SHIFT          6
#define MAX_COPY_PREFETCH       4
#define L1_CACHE_BYTES          (1 << L1_CACHE_SHIFT)
/*
 * Write any modified data cache blocks out to memory and invalidate them.
 * Does not invalidate the corresponding instruction cache blocks.
 */
static inline void flush_dcache_range(void *start, unsigned long size)
{
#if 0	
	void *addr = (void *)((uint32_t)start & ~(L1_CACHE_BYTES - 1));
	uint32_t len = size + (L1_CACHE_BYTES - 1);
 	uint32_t i;
 
	for (i = 0; i < len >> L1_CACHE_SHIFT; i++, addr += L1_CACHE_BYTES)
 		dcbf(addr);
 	mb();	/* sync */
#endif
}

#include "fapi_interface.h"

__attribute__((visibility("default"))) const char *fapi_b4860_git_version = FAPI_LIB_GIT_REVISION;
__attribute__((visibility("default"))) const char *fapi_b4860_build_timestamp = FAPI_LIB_BUILD_TIMESTAMP;

static int32_t running;
static pthread_mutex_t fapi_fsl_b4860_ipc_send_mutex;
static pthread_mutex_t fapi_fsl_b4860_ipc_recv_mutex;
static pthread_cond_t fapi_fsl_b4860_ipc_recv_cond;
static pthread_mutex_t fapi_alloc_mutex;
static pthread_mutex_t ipc_send_mutex;
static pthread_mutex_t ipc_recv_mutex;
static pthread_cond_t ipc_recv_cond;

static block_queue_t *fapi_p5_ipc_queue;
static block_queue_t *fapi_p7_ipc_queue;
static block_queue_t *fapi_p7_subframe_ind_queue;
static block_queue_t *fapi_p7_rach_ind_queue;
static block_queue_t *fapi_p7_rx_ind_queue;
static block_queue_t *fapi_p7_harq_ind_queue;
static block_queue_t *fapi_p7_sr_ind_queue;
static block_queue_t *fapi_p7_cqi_ind_queue;
static block_queue_t *fapi_p7_crc_ind_queue;
static block_queue_t *fapi_p8_ind_queue;

static pthread_t fapi_thread_h;
static void* fapi_thread(void * param);

static fsl_usmmgr_t usmmgr;
static fsl_ipc_t ipc;

static mem_range_t fapi_tx_req_data_bufs[FAPI_TX_REQ_BUF_NUM];
static uint32_t fapi_tx_req_data_buf_ptr;
static mem_range_t fapi_cpri_eth_bufs[FAPI_CPRI_ETH_BUF_NUM];
static uint32_t fapi_cpri_eth_buf_ptr;

static phys_addr_t ipc_v2p(void *addr);
static int32_t ipc_send_msg(fapi_ipc_msg_t *msg);
static void fapi_b4860_process_p7_message(fapi_l1_message_header_t *msg);
static void fapi_b4860_log_fsl(char * log_msg);
static void fapi_b4860_log_fsl_direct(void *e4860);
static int32_t ipc_l1_start();
static int32_t fapi_b4860_ipc_init();

fapi_config_t g_fapi_cfg_req;
FAPI_B4860_CONFIG_t *g_fapi_b4860_cfg;

#define UIO_INTERFACE "/dev/uio0"

__attribute__((visibility("default"))) block_queue_t *fapi_get_subframe_ind_queue()
{
        return fapi_p7_subframe_ind_queue;
}

__attribute__((visibility("default"))) block_queue_t *fapi_get_rach_ind_queue()
{
        return fapi_p7_rach_ind_queue;
}

__attribute__((visibility("default"))) block_queue_t *fapi_get_rx_ind_queue()
{
        return fapi_p7_rx_ind_queue;
}

__attribute__((visibility("default"))) block_queue_t *fapi_get_harq_ind_queue()
{
        return fapi_p7_harq_ind_queue;
}

__attribute__((visibility("default"))) block_queue_t *fapi_get_sr_ind_queue()
{
        return fapi_p7_sr_ind_queue;
}

__attribute__((visibility("default"))) block_queue_t *fapi_get_cqi_ind_queue()
{
        return fapi_p7_cqi_ind_queue;
}

__attribute__((visibility("default"))) block_queue_t *fapi_get_crc_ind_queue()
{
        return fapi_p7_crc_ind_queue;
}

__attribute__((visibility("default"))) block_queue_t *fapi_get_p8_ind_queue()
{
        return fapi_p8_ind_queue;
}

__attribute__((visibility("default"))) int32_t fapi_is_connected()
{
        return 1;
}

/* Обработка параметров L1 */
static int32_t fapi_init_late();

__attribute__((visibility("default"))) bool enodeb_du_l1_init(const char* l1_name, const char *l1_config_file)
{
	bool res = false;

	if(strcmp(l1_name, "l1_b4860_v1") == 0)
	{
		//bzero(&fapi_b4860_cfg, sizeof(fapi_b4860_cfg_t));

		g_fapi_b4860_cfg = fapi_b4860_parse_config(l1_config_file);
		
		if(g_fapi_b4860_cfg != NULL)
		{
			/* Проверка наличия лицензии */
			if(g_fapi_b4860_cfg->license == NULL || g_fapi_b4860_cfg->license->size != 64)
			{
				LOG_W(FAPI, "No valid L1 license found!\n");
			}
			
			res = true;
		}
		else
		{
			LOG_E(FAPI, "Error configuring l1_b4860_v1: %s\n", gser_decoder_get_error_str());
			return false;
		}
	}

	if(res)
	{
		if(fapi_init_late() != 0)
			res = false;
	}
	return res;
}

__attribute__((visibility("default"))) void enodeb_du_l1_deinit()
{
	//TODO: добавить код для правильного заверешния L1
}

fapi_ipc_msg_t *fapi_alloc_send_msg(uint32_t id, void **body_addr)
{
	mem_range_t *r;
	fapi_ipc_msg_t *msg;

	// Lock here
	pthread_mutex_lock(&fapi_alloc_mutex);
	r = &fapi_tx_req_data_bufs[fapi_tx_req_data_buf_ptr];

	fapi_tx_req_data_buf_ptr++;

	if (fapi_tx_req_data_buf_ptr == FAPI_TX_REQ_BUF_NUM)
		fapi_tx_req_data_buf_ptr = 0;

	// Unlock here
	pthread_mutex_unlock(&fapi_alloc_mutex);

	msg = (fapi_ipc_msg_t *) r->vaddr;
	msg->channel_id = id;
	msg->phys_addr = r->phys_addr;
	msg->length = 0;

	if (body_addr)
		*body_addr = ((uint8_t *) msg + sizeof(fapi_ipc_msg_t));

	return (fapi_ipc_msg_t *) msg;
}

static fapi_ipc_msg_t *fapi_alloc_cpri_eth_msg(uint32_t id, void **body_addr)
{
	mem_range_t *r;
	fapi_ipc_msg_t *msg;

	// Lock here
	pthread_mutex_lock(&fapi_alloc_mutex);
	r = &fapi_cpri_eth_bufs[fapi_cpri_eth_buf_ptr];

	fapi_cpri_eth_buf_ptr++;

	if (fapi_cpri_eth_buf_ptr == FAPI_CPRI_ETH_BUF_NUM)
		fapi_cpri_eth_buf_ptr = 0;

	// Unlock here
	pthread_mutex_unlock(&fapi_alloc_mutex);

	msg = (fapi_ipc_msg_t *) r->vaddr;
	msg->channel_id = id;
	msg->phys_addr = r->phys_addr;
	msg->length = 0;

	if (body_addr)
		*body_addr = ((uint8_t *) msg + sizeof(fapi_ipc_msg_t));

	return (fapi_ipc_msg_t *) msg;
}

fapi_l1_message_header_t *fapi_p5_recv_blocking(int32_t timeout)
{
	fapi_l1_message_header_t *msg = NULL;

	msg = (fapi_l1_message_header_t *) block_queue_pop(fapi_p5_ipc_queue);

	return msg;
}

fapi_l1_message_header_t * fapi_p7_recv_blocking(int32_t timeout)
{
	fapi_l1_message_header_t *msg = NULL;

	msg = (fapi_l1_message_header_t *) block_queue_pop(fapi_p7_ipc_queue);

	return msg;
}

__attribute__((visibility("default"))) int32_t fapi_init()
{
	return 0;
}

static int32_t fapi_init_late()
{
	uint32_t len;
	int32_t ret;
	int32_t i;

	LOG_I(FAPI, "B4860 IPC initalization\n");

	fapi_scheduler_init();

	fapi_p5_ipc_queue = block_queue_init(64);
	fapi_p7_ipc_queue = block_queue_init(64);

	fapi_p7_rach_ind_queue = block_queue_init(64);
	fapi_p7_subframe_ind_queue = block_queue_init(64);
	fapi_p7_rx_ind_queue = block_queue_init(64);
	fapi_p7_harq_ind_queue = block_queue_init(64);
	fapi_p7_sr_ind_queue = block_queue_init(64);
	fapi_p7_cqi_ind_queue = block_queue_init(64);
	fapi_p7_crc_ind_queue = block_queue_init(64);
	fapi_p8_ind_queue = block_queue_init(64);

	thread_helper_mutex_init_pi(&ipc_send_mutex);
	thread_helper_mutex_init_pi(&ipc_recv_mutex);
	
#ifndef INIT_IPC_IN_FSL_THREAD
	if(fapi_b4860_ipc_init() != 0)
	{
		LOG_E(FAPI, "Error initializing B4860 IPC, exiting!\n");
		return -1;
	}

	/* Инициализация канала TX.req */
	fapi_tx_req_data_buf_ptr = 0;

	for (i = 0; i < FAPI_TX_REQ_BUF_NUM; i++)
	{
		fapi_tx_req_data_bufs[i].size = FAPI_TX_REQ_BUF_SIZE;

		ret = fsl_usmmgr_alloc(&fapi_tx_req_data_bufs[i], usmmgr);
		if (ret)
		{
			LOG_E(FAPI, "Unable to allocate memory from shm_alloc \n");
			return -1;
		}
	}

	/* Инициализация буферов CPRI Ethernet */
	fapi_cpri_eth_buf_ptr = 0;

	for (i = 0; i < FAPI_CPRI_ETH_BUF_NUM; i++)
	{
		fapi_cpri_eth_bufs[i].size = FAPI_CPRI_ETH_BUF_SIZE;

		ret = fsl_usmmgr_alloc(&fapi_cpri_eth_bufs[i], usmmgr);
		if (ret)
		{
			LOG_E(FAPI, "Unable to allocate memory from shm_alloc \n");
			return -1;
		}
	}

	thread_helper_mutex_init_pi(&fapi_alloc_mutex);

#endif

	// TODO: Move to L1 initialization
	//cpri_eth_init();

	pthread_mutex_init(&fapi_alloc_mutex, NULL);
	pthread_create(&fapi_thread_h, NULL, fapi_thread, NULL);
	
	if(g_fapi_b4860_cfg->p8 && g_fapi_b4860_cfg->p8->enable)
		p8_start(g_fapi_b4860_cfg->p8, fapi_get_p8_ind_queue());

#ifdef CPRI_RRU_INIT_EARLY
	cpri_eth_start();

	LOG_I(FAPI,"Waiting for RRU initialization\n");
	
	while(!cpri_eth_connected())
	{
		usleep(100000);
	}

	LOG_I(FAPI,"RRU initialized\n");
#endif

	return 0;
}

void fapi_stop()
{
	if (running)
	{
		running = 0;
		pthread_cancel(fapi_thread_h);
		pthread_join(fapi_thread_h, NULL);
	}

	p8_stop();

	pthread_mutex_destroy(&fapi_alloc_mutex);

	pthread_mutex_destroy(&ipc_send_mutex);
	pthread_mutex_destroy(&ipc_recv_mutex);
}

int32_t fapi_is_timeout_ms(struct timespec *tp_start, int32_t timeout_ms)
{
	struct timespec tp;

	clock_gettime(CLOCK_MONOTONIC, &tp);

	if (tp.tv_nsec < tp_start->tv_nsec)
		tp.tv_nsec += 1000000000;

	return ((tp.tv_nsec - tp_start->tv_nsec) > timeout_ms * 1000000);
}

__attribute__((visibility("default"))) int32_t fapi_p5_config_request(fapi_config_t *config, fapi_config_response_t **resp)
{
	fapi_ipc_msg_t *ipc_msg;
	fapi_config_t *cfg_req;
	phys_addr_t phys_ptr;
	uint32_t len;
	int ret;
	fapi_l1_message_header_t *l1_ans_msg;

	ipc_msg = fapi_alloc_send_msg(FAPI_CHANNEL_P5_REQ, (void **)&cfg_req);

	memcpy(cfg_req, config, sizeof(fapi_config_t));
	memcpy(&g_fapi_cfg_req, config, sizeof(fapi_config_t));

	cfg_req->header.message_id = FAPI_CONFIG_REQUEST;
	cfg_req->header.message_length = sizeof(fapi_config_t);

	FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, cfg_req);

	ret = ipc_send_msg(ipc_msg);
	if (ret)
	{
		LOG_E(FAPI, "Error sending p5 CONFIG.request %d\n", ret);
		return -1;
	}

	/* Ожидание ответа CONFIG.response */
	//clock_gettime(CLOCK_MONOTONIC, &tp_start);
	l1_ans_msg = fapi_p5_recv_blocking(20000000);

	if (l1_ans_msg == NULL)
	{
		LOG_E(FAPI, "Error receiving CONFIG.response in 2000ms, ret=%x\n", ret);

		*resp = NULL;
		ret = 1;
	}

	*resp = (fapi_config_response_t *) l1_ans_msg;

	return ret;
}

__attribute__((visibility("default"))) int32_t fapi_p5_start_request()
{
	int ret;
	fapi_ipc_msg_t *ipc_msg;
	fapi_start_request_t *msg;

	ipc_msg = fapi_alloc_send_msg(FAPI_CHANNEL_P5_REQ, (void **)&msg);

	msg->header.message_id = FAPI_START_REQUEST;
	msg->header.message_length = sizeof(fapi_start_request_t);

	FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, msg);

	ret = ipc_send_msg(ipc_msg);

	if (ret)
	{
		LOG_E(FAPI, "Error sending p5 START.request %d\n", ret);
		return -1;
	}

#ifndef CPRI_RRU_INIT_EARLY
	// Init CPRI ethernet subsystem

	LOG_I(FAPI, "Waiting for RRU initialization\n");

	cpri_eth_init();
	cpri_eth_start();		

	LOG_I(FAPI, "RRU initialized\n");
#endif

	return 0;
}

int32_t fapi_b4860_fapi_cpri_eth_send(void *data, int32_t len)
{
	int ret;
	fapi_ipc_msg_t *ipc_msg;
	void *databody;

	ipc_msg = fapi_alloc_cpri_eth_msg(FAPI_CHANNEL_CPRI_ETH, (void **)&databody);

	memcpy(databody, data, len);

	ipc_msg->length = sizeof(fapi_ipc_msg_t) + len;

	ret = ipc_send_msg(ipc_msg);
	if (ret)
		LOG_E(FAPI, "Error sending CPRI ethernet msg %d\n", ret);

	return 0;
}

__attribute__((visibility("default"))) int32_t fapi_p5_stop_request()
{
	int ret;
	fapi_ipc_msg_t *ipc_msg;
	fapi_stop_request_t *msg;

	ipc_msg = fapi_alloc_send_msg(FAPI_CHANNEL_P5_REQ, (void **)&msg);

	msg->header.message_id = FAPI_STOP_REQUEST;
	msg->header.message_length = sizeof(fapi_stop_request_t);

	FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, msg);

	ret = ipc_send_msg(ipc_msg);
	if (ret)
		LOG_E(FAPI, "Error sending p5 STOP.request %d\n", ret);

	//cpri_eth_stop();

	return 0;
}

/* DL_CONFIG.request */
__attribute__((visibility("default"))) int32_t fapi_p7_dl_config_request(scheduler_dl_config_request_t *dl_cfg_req)
{
	fapi_dl_config_request_t *msg; // = (fapi_dl_config_request_t *)dl_config_buf;
	int32_t ret;
	fapi_ipc_msg_t *ipc_msg;

	ipc_msg = fapi_alloc_send_msg(FAPI_CHANNEL_P7_REQ, (void **)&msg);

	msg->header.message_id = FAPI_DL_CONFIG_REQUEST;

	msg->sfn_sf = (dl_cfg_req->frame << 4) | (dl_cfg_req->subframe);
	msg->dl_config_request_body.number_dci = dl_cfg_req->number_dci;
	msg->dl_config_request_body.number_pdu = dl_cfg_req->number_pdu;
	msg->dl_config_request_body.number_pdcch_ofdm_symbols = dl_cfg_req->number_pdcch_ofdm_symbols;
	msg->dl_config_request_body.number_pdsch_rnti = dl_cfg_req->number_pdsch_rnti;

	memcpy(&msg->dl_config_request_body.dl_config_pdu_list, &dl_cfg_req->dl_config_dci_list, dl_cfg_req->number_dci * sizeof(fapi_dl_config_request_pdu_t));

	memcpy(&msg->dl_config_request_body.dl_config_pdu_list[dl_cfg_req->number_dci], &dl_cfg_req->dl_config_pdu_list,
			dl_cfg_req->number_pdu * sizeof(fapi_dl_config_request_pdu_t));
	
	msg->header.message_length = FAPI_GET_MSG_PTR_VAR_SIZE(msg, dl_config_request_body.dl_config_pdu_list,
			msg->dl_config_request_body.number_dci + msg->dl_config_request_body.number_pdu);

	//if((dl_cfg_req->frame % 8) == 0 && (dl_cfg_req->subframe == 1 || dl_cfg_req->subframe == 2))
	LOG_D(FAPI, "DL_CONFIG.req for %i:%i len=%i nDCI=%i nPDU=%i nRNTI=%i\n", dl_cfg_req->frame, dl_cfg_req->subframe, msg->header.message_length,
		msg->dl_config_request_body.number_dci, msg->dl_config_request_body.number_pdu, msg->dl_config_request_body.number_pdsch_rnti);

	FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, msg);

	flush_dcache_range(ipc_msg, ipc_msg->length);
	ret = ipc_send_msg(ipc_msg);

	if (ret)
		LOG_E(FAPI, "Error sending p7 DL_CONFIG.request %d\n", ret);

	return 0;
}

/* UL_CONFIG.request */
__attribute__((visibility("default"))) int32_t fapi_p7_ul_config_request(scheduler_ul_config_request_t *ul_cfg_req)
{
	fapi_ul_config_request_t *msg;
	int32_t ret;
	fapi_ipc_msg_t *ipc_msg;

	ipc_msg = fapi_alloc_send_msg(FAPI_CHANNEL_P7_REQ, (void **)&msg);

	msg->header.message_id = FAPI_UL_CONFIG_REQUEST;

	msg->sfn_sf = (ul_cfg_req->frame << 4) | (ul_cfg_req->subframe);
	msg->ul_config_request_body.number_of_pdus = ul_cfg_req->number_pdu;
	msg->ul_config_request_body.rach_prach_frequency_resources = ul_cfg_req->rach_prach_frequency_resources;
	msg->ul_config_request_body.srs_present = ul_cfg_req->srs_present;

	memcpy(&msg->ul_config_request_body.ul_config_pdu_list, &ul_cfg_req->ul_config_list, ul_cfg_req->number_pdu * sizeof(fapi_ul_config_request_pdu_t));

	msg->header.message_length = FAPI_GET_MSG_PTR_VAR_SIZE(msg, ul_config_request_body.ul_config_pdu_list, msg->ul_config_request_body.number_of_pdus);

	if (ul_cfg_req->number_pdu > 0)
	{
		LOG_D(FAPI, "UL_CONFIG.req for %i:%i len=%i nPDU=%i phys_addr=0x%08x\n", ul_cfg_req->frame, ul_cfg_req->subframe, msg->header.message_length,
				msg->ul_config_request_body.number_of_pdus, ipc_msg->phys_addr);
	}

	FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, msg);

	flush_dcache_range(ipc_msg, ipc_msg->length);

	ret = ipc_send_msg(ipc_msg);

	if (ret)
		LOG_E(FAPI, "Error sending p7 UL_CONFIG.request %d\n", ret);

	return 0;
}

/* HI_DCI0.request */
__attribute__((visibility("default"))) int32_t fapi_p7_hi_dci0_request(scheduler_dl_config_request_t *dl_cfg_req)
{
	fapi_hi_dci0_request_t *msg;
	int32_t ret;
	fapi_ipc_msg_t *ipc_msg;

	ipc_msg = fapi_alloc_send_msg(FAPI_CHANNEL_P7_REQ, (void **)&msg);

	msg->header.message_id = FAPI_HI_DCI0_REQUEST;

	msg->sfn_sf = (dl_cfg_req->frame << 4) | (dl_cfg_req->subframe);
	msg->hi_dci0_request_body.number_of_dci = dl_cfg_req->number_dci0;
	msg->hi_dci0_request_body.number_of_hi = dl_cfg_req->number_hi;

	memcpy(&msg->hi_dci0_request_body.hi_dci0_pdu_list, &dl_cfg_req->dci0_list, dl_cfg_req->number_dci0 * sizeof(fapi_hi_dci0_request_pdu_t));

	memcpy(&msg->hi_dci0_request_body.hi_dci0_pdu_list[dl_cfg_req->number_dci0], &dl_cfg_req->hi_list,
			dl_cfg_req->number_hi * sizeof(fapi_hi_dci0_request_pdu_t));

	msg->header.message_length = FAPI_GET_MSG_PTR_VAR_SIZE(msg, hi_dci0_request_body.hi_dci0_pdu_list,
			msg->hi_dci0_request_body.number_of_dci + msg->hi_dci0_request_body.number_of_hi);

	LOG_D(FAPI, "HI_DCI0.req for %i:%i len=%i nDCI0=%i nHI=%i\n", dl_cfg_req->frame, dl_cfg_req->subframe, msg->header.message_length,
			msg->hi_dci0_request_body.number_of_dci, msg->hi_dci0_request_body.number_of_hi);

	FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, msg);

	flush_dcache_range(ipc_msg, ipc_msg->length);
	ret = ipc_send_msg(ipc_msg);

	if (ret)
		LOG_E(FAPI, "Error sending p7 HI_DCI0.request %d\n", ret);

	return 0;
}

/* TX.request via PTR channel */
__attribute__((visibility("default"))) int32_t fapi_p7_tx_request(scheduler_dl_config_request_t *dl_cfg_req)
{
	fapi_tx_request_t *msg; // = (fapi_tx_request_t *)tx_reQ_msg_buf;
	uint8_t *tx_req_data_buf;
	int32_t i;
	int32_t ret;
	int32_t tbs_i;
	int32_t tx_req_data_ptr = 0;
	fapi_ipc_msg_t *ipc_msg;
	int32_t msg_len_aligned;

	ipc_msg = fapi_alloc_send_msg(FAPI_CHANNEL_P7_REQ, (void **)&msg);

	/* Берем первый буфер из кольца */

	msg->header.message_id = FAPI_TX_REQUEST;

	msg->sfn_sf = (dl_cfg_req->frame << 4) | (dl_cfg_req->subframe);
	msg->tx_request_body.number_of_pdus = dl_cfg_req->number_tx_req;

	/* Смещение буфера TX.req
	 * Выровнен по границе 64 байт
	 */

	msg_len_aligned = ((FAPI_GET_MSG_PTR_VAR_SIZE(msg, tx_request_body.tx_pdu_list, msg->tx_request_body.number_of_pdus) + 63) & 0xffffffc0);
	tx_req_data_ptr = 0;
	tx_req_data_buf = (uint8_t *) msg + msg_len_aligned;

	for (i = 0; i < dl_cfg_req->number_tx_req; i++)
	{
		msg->tx_request_body.tx_pdu_list[i].num_segments = dl_cfg_req->tx_request_pdu_list[i].num_segments;
		msg->tx_request_body.tx_pdu_list[i].pdu_index = dl_cfg_req->tx_request_pdu_list[i].pdu_index;
		msg->tx_request_body.tx_pdu_list[i].pdu_length = dl_cfg_req->tx_request_pdu_list[i].pdu_length;

		for (tbs_i = 0; tbs_i < dl_cfg_req->tx_request_pdu_list[i].num_segments; tbs_i++)
		{
			msg->tx_request_body.tx_pdu_list[i].tbs[tbs_i].tb_tag = dl_cfg_req->tx_request_pdu_list[i].tbs[tbs_i].tb_tag;
			msg->tx_request_body.tx_pdu_list[i].tbs[tbs_i].tb_length = dl_cfg_req->tx_request_pdu_list[i].tbs[tbs_i].tb_length;
			if (dl_cfg_req->tx_request_pdu_list[i].tbs[tbs_i].tb_tag)
			{
				/* В поле tb_data передается адрес на блок данных
				 * Его необходимо скопировать в шаред мемори, чтобы L1 смог его прочитать
				 */
				memcpy(&tx_req_data_buf[tx_req_data_ptr], (void *) dl_cfg_req->tx_request_pdu_list[i].tbs[tbs_i].tb_data,
						dl_cfg_req->tx_request_pdu_list[i].tbs[tbs_i].tb_length);

				/* Указатель на данные в буфере */
				msg->tx_request_body.tx_pdu_list[i].tbs[tbs_i].tb_data = (void*)ipc_v2p(&tx_req_data_buf[tx_req_data_ptr]);
				//r->phys_addr + tx_req_data_ptr;

				/* Увеличение указателя данных с выравниванием по 8 байтам */
				tx_req_data_ptr += (dl_cfg_req->tx_request_pdu_list[i].tbs[tbs_i].tb_length + 7) & 0xfffffff8;
			}
			else
			{
				msg->tx_request_body.tx_pdu_list[i].tbs[tbs_i].tb_data = dl_cfg_req->tx_request_pdu_list[i].tbs[tbs_i].tb_data;
			}
		}
	}

	msg->header.message_length = msg_len_aligned + tx_req_data_ptr;

	LOG_D(FAPI, "Sending TX_REQ MSG len=%i nPDU=%i P=0x%08x Ptx=0x%08x S=0x%08x\n", msg->header.message_length, msg->tx_request_body.number_of_pdus,
			ipc_msg->phys_addr, ipc_msg->phys_addr + (tx_req_data_buf - (uint8_t * ) msg), msg->header.message_length);

	FAPI_IPC_MSG_SET_LENGTH_FROM_L1(ipc_msg, msg);
	
	flush_dcache_range(ipc_msg, ipc_msg->length);
	ret = ipc_send_msg(ipc_msg);

	if (ret)
		LOG_E(FAPI, "Error sending p7 TX.request %d\n", ret);

	return 0;
}

static void *ipc_p2v(phys_addr_t phys_addr)
{
	return fsl_usmmgr_p2v(phys_addr, usmmgr);
}

static phys_addr_t ipc_v2p(void *addr)
{
	return fsl_usmmgr_v2p(addr, usmmgr);
}

static int32_t is_dsp_up()
{
	int32_t i;
	uint64_t bmask;

	for (i = 0; i < 32; i++)
	{
		fsl_ipc_chk_recv_status(&bmask, ipc);
		if ((bmask >> (63 - 0)) & 0x1)
		{
			break;
		}

		usleep(10000);
	}

	return i < 32 ? 1 : 0;
}

static int32_t ipc_send_l1_msg(int32_t id, mem_range_t *r, fapi_l1_message_header_t *hdr)
{
	int32_t ret;
	fapi_ipc_msg_t ipc_msg;

	ipc_msg.channel_id = id;
	ipc_msg.body_addr = r->phys_addr;
	ipc_msg.length = hdr->message_length;

	ret = fsl_ipc_send_msg(IPC_PA2SC_CH_ID, &ipc_msg, sizeof(fapi_ipc_msg_t), ipc);
	if (ret)
	{
		LOG_E(FAPI, "Error sending IPC message: %i\n", ret);
	}

	return ret;
}

static int32_t ipc_send_msg(fapi_ipc_msg_t *msg)
{
	int32_t ret;
	fapi_ipc_msg_t ipc_msg;

	msg->body_addr = msg->phys_addr + sizeof(fapi_ipc_msg_t);

	ipc_msg.channel_id = msg->channel_id;
	ipc_msg.body_addr = msg->phys_addr;
	ipc_msg.length = msg->length;

	if (ipc_msg.length <= 0)
	{
		LOG_E(FAPI, "Attempting to send invalid message: id %i addr 0x%08x len 0x%08x\n", ipc_msg.channel_id, ipc_msg.body_addr, ipc_msg.length);
		return -1;
	}

	pthread_mutex_lock(&ipc_send_mutex);
	ret = fsl_ipc_send_msg(IPC_PA2SC_CH_ID, &ipc_msg, sizeof(fapi_ipc_msg_t), ipc);
	if (ret)
	{
		LOG_E(FAPI, "Error sending IPC message: %i\n", ret);
	}
	pthread_mutex_unlock(&ipc_send_mutex);

	return ret;
}

static int32_t fapi_b4860_ipc_init()
{

	int32_t ret = 0;
	mem_range_t r;
	phys_addr_t phys_ptr;
	int32_t depth = 16;
	uint32_t len;

	mem_range_t sh_ctrl;
	mem_range_t dsp_ccsr;
	mem_range_t pa_ccsr;

	LOG_I(FAPI, "FSL IPC initalization\n");

	usmmgr = fsl_usmmgr_init();
	if (!usmmgr)
	{
		LOG_E(FAPI, "Error in Initializing User Space Memory Manager\n");
		return -1;
	}

	/* get values from mmgr */
	ret = get_pa_ccsr_area(&pa_ccsr, usmmgr);
	if (ret)
	{
		LOG_E(FAPI, "Error in obtaining PA CCSR Area information: %i\n", ret);
		return -1;
	}

	ret = get_dsp_ccsr_area(&dsp_ccsr, usmmgr);
	if (ret)
	{
		LOG_E(FAPI, "Error in obtaining DSP CCSR Area information\n");
		return -1;
	}

	ret = get_shared_ctrl_area(&sh_ctrl, usmmgr);
	if (ret)
	{
		LOG_E(FAPI, "Error in obtaining Shared Control Area information\n");
		return -1;
	}

	ipc = fsl_ipc_init_rat(0, ipc_p2v, sh_ctrl, dsp_ccsr, pa_ccsr, UIO_INTERFACE);

	if (!ipc)
	{
		LOG_E(FAPI, "Issue with fsl_ipc_init %d\n", ret);
		return -1;
	}

	if (!is_dsp_up())
	{
		LOG_E(FAPI, "L1 seems to be uninitialized\n");
		return -1;
	}

	thread_helper_mutex_init_pi(&ipc_send_mutex);
	thread_helper_mutex_init_pi(&ipc_recv_mutex);

	LOG_I(FAPI, "Starting IPC dispatcher thread\n");

	fsl_ipc_open_prod_ch(IPC_MSG_CH_ID, ipc);
	fsl_ipc_open_prod_ch(IPC_PA2SC_CH_ID, ipc);

#ifdef FAPI_RT_SIGNAL
	ret = fsl_ipc_configure_channel_int(IPC_SC2PA_CH_ID, 16, IPC_PTR_CH, 0, 0, (void *)ipc_cbfunc, ipc);
#else
	ret = fsl_ipc_configure_channel_int(IPC_SC2PA_CH_ID, 16, IPC_PTR_CH, 0, 0, 0, ipc);
#endif

	if (ret)
	{
		LOG_E(FAPI, "Error configuring PHY log channel (%i)\n", ret);
		return -1;
	}

	/* Вычитывание старых сообщений из каналов P7 и P5 */
	do
	{
		ret = fsl_ipc_recv_ptr(IPC_SC2PA_CH_ID, &phys_ptr, &len, ipc);
	}
	while (ret == 0);

	ipc_l1_start();

	return 0;
}

int32_t ipc_reset_channel(int32_t ch_id)
{
	phys_addr_t phys_ptr;
	uint32_t len;
	int32_t ret;

	/* Вычитывание старых сообщений из каналов P7 и P5 */
	do
	{
		ret = fsl_ipc_recv_ptr(IPC_SC2PA_CH_ID, &phys_ptr, &len, ipc);
	}
	while (ret == 0);

	return (ret == -ERR_CHANNEL_EMPTY) ? 0 : ret;
}

static int32_t ipc_l1_start()
{
	int32_t ret;
	fapi_ipc_cfg_t fapi_ipc_cfg;
	int32_t nrru;

	bzero(&fapi_ipc_cfg, sizeof(fapi_ipc_cfg));

	fapi_ipc_cfg.magic = FAPI_IPC_CFG_MAGIC;
	fapi_ipc_cfg.version = 1;

	if(g_fapi_b4860_cfg->license != NULL && g_fapi_b4860_cfg->license->size == 64)
	{
		memcpy(&fapi_ipc_cfg.cfg_v1.license, g_fapi_b4860_cfg->license->buf, 64);
	}

	if(g_fapi_b4860_cfg->loglevel)
		fapi_ipc_cfg.cfg_v1.loglevel = *g_fapi_b4860_cfg->loglevel;
	else
		fapi_ipc_cfg.cfg_v1.loglevel = 255;

	fapi_ipc_cfg.cfg_v1.n_rrus = g_fapi_b4860_cfg->rrus.list.count;

	for(nrru=0; nrru<fapi_ipc_cfg.cfg_v1.n_rrus; nrru++)
	{
		if(g_fapi_b4860_cfg->rrus.list.array[nrru])
		{
			fapi_ipc_cfg.cfg_v1.rrus[nrru].cpri_port = g_fapi_b4860_cfg->rrus.list.array[nrru]->cpri_port;
			fapi_ipc_cfg.cfg_v1.rrus[nrru].speed = g_fapi_b4860_cfg->rrus.list.array[nrru]->speed;
		}
	}

	/* Флаги дампа P8 */
	if(g_fapi_b4860_cfg->p8 && g_fapi_b4860_cfg->p8->enable)
	{
		if(g_fapi_b4860_cfg->p8->dump_ul)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL;

		if(g_fapi_b4860_cfg->p8->dump_ul_bad_crc)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_ON_BAD_CRC;
			
		if(g_fapi_b4860_cfg->p8->dump_ul_pucch)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUCCH;

		if(g_fapi_b4860_cfg->p8->dump_ul_pucch_ack)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUCCH_ACK;

		if(g_fapi_b4860_cfg->p8->dump_ul_pucch_nack)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUCCH_NACK;
			
		if(g_fapi_b4860_cfg->p8->dump_ul_pucch_not)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUCCH_NOT;
			
		if(g_fapi_b4860_cfg->p8->dump_ul_pucch_sr)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUCCH_SR;
			
		if(g_fapi_b4860_cfg->p8->dump_ul_pucch_sr_all)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUCCH_SR_ALL;

		if(g_fapi_b4860_cfg->p8->dump_ul_pucch_cqi)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUCCH_CQI;

		if(g_fapi_b4860_cfg->p8->dump_ul_pusch_ack)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUSCH_ACK;

		if(g_fapi_b4860_cfg->p8->dump_ul_pusch_nack)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUSCH_NACK;

		if(g_fapi_b4860_cfg->p8->dump_ul_pusch_cqi)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_PUSCH_CQI;
			
		if(g_fapi_b4860_cfg->p8->dump_ul_llr_check)
			fapi_ipc_cfg.cfg_v1.p8_dump_flags |= FAPI_P8_DUMP_FLAG_UL_LLR_CHECK;
			
		fapi_ipc_cfg.cfg_v1.p8_dump_limit = g_fapi_b4860_cfg->p8->limit;
	}
	
	//const char *buf = "Starting DSP";

	LOG_I(FAPI, "IPC initialization complete, firing up L1\n");
	
	ret = fsl_ipc_send_msg(IPC_MSG_CH_ID, &fapi_ipc_cfg, sizeof(fapi_ipc_cfg), ipc);
	
	if (ret)
		LOG_E(FAPI, "Issue with fsl_ipc_send_msg %d\n", ret);

	return ret;
}

static void fapi_cpri_eth_process(fapi_ipc_msg_t *msg, int32_t msglen)
{
	cpri_eth_process_cpri_msg(msg);
}

static void* fapi_thread(void * param)
{
	int32_t ret;
	phys_addr_t msg_addr;
	uint32_t msg_len;
	fapi_ipc_msg_t *msg;
	int32_t i;
	int32_t p7_msg_num = 0;

	LOG_I(FAPI, "Starting B4860 dispatcher thread\n");
	thread_helper_thread_top_init("l2_fapi", THREAD_PRIO_FAPI, 0, 0, 0);

#ifdef INIT_IPC_IN_FSL_THREAD
	ipc_init();

	/* Инициализация канала TX.req */
	fapi_tx_req_data_buf_ptr = 0;

	for (i = 0; i < FAPI_TX_REQ_BUF_NUM; i++)
	{
		fapi_tx_req_data_bufs[i].size = FAPI_TX_REQ_BUF_SIZE;

		ret = fsl_usmmgr_alloc(&fapi_tx_req_data_bufs[i], usmmgr);
		if (ret)
		{
			Error("Unable to allocate memory from shm_alloc \n");
			return;
		}
	}

	mutex_init_pi(&fapi_alloc_mutex);
#endif
	running = 1;
	/* Сброс каналов передачи IPC */
	LOG_I(FAPI, "Resetting IPC channels\n");

	if((ret = ipc_reset_channel(IPC_SC2PA_CH_ID)) != 0)
	{
		LOG_W(FAPI, "Reset IPC ret=%i\n", ret);
	}

	while (running)
	{
#ifndef FAPI_RT_SIGNAL

		ret = fsl_ipc_recv_ptr_block(IPC_SC2PA_CH_ID, &msg_addr, &msg_len, ipc);

		if (ret == -ERR_CHANNEL_EMPTY)
		{
			continue;
		}

		if (ret != 0)
		{
			LOG_E(FAPI, "Error receiving message: %i\n", ret);
			continue;
		}
#else
		msg_addr = (phys_addr_t)ipc_recv_queue.wait_pop();
#endif
		if(msg_addr == 0)
		{
			continue;
		}

		msg = (fapi_ipc_msg_t *) ipc_p2v(msg_addr);

		if (msg == NULL)
		{
			LOG_E(FAPI, "NULL message received\n");
			continue;
		}

		if (msg->channel_id < 0 || msg->channel_id >= FAPI_CHANNELS_NUM)
		{
			LOG_E(FAPI, "Invalid channel id receiver: %i\n", msg->channel_id);
			continue;
		}

		switch (msg->channel_id)
		{
			case FAPI_CHANNEL_CPRI_ETH:
				msg->body_addr = (uint32_t)ipc_p2v(msg->body_addr);
				fapi_cpri_eth_process(msg, msg->length);
				break;

			case FAPI_CHANNEL_P5_IND:
				block_queue_push(fapi_p5_ipc_queue, (fapi_l1_message_header_t *) ipc_p2v(msg->body_addr), sizeof(fapi_l1_message_header_t));
				break;

			case FAPI_CHANNEL_P7_IND:
				fapi_b4860_process_p7_message((fapi_l1_message_header_t *) ipc_p2v(msg->body_addr));
				
				// Reconfigure RRU on 20 subframe
				if(p7_msg_num < 20)
				{
					p7_msg_num++;
				}
				else if(p7_msg_num == 20)
				{
					// Not used now
					//cpri_eth_rrus_reconfigure();
				}

				break;
			
			case FAPI_CHANNEL_P8_IND:
				block_queue_try_push(fapi_p8_ind_queue, (fapi_l1_message_header_t *) ipc_p2v(msg->body_addr), sizeof(fapi_l1_message_header_t));
				break;

			case FAPI_CHANNEL_LOG:
				{
					uint8_t *body = (uint8_t *)ipc_p2v(msg->body_addr);
					if(body[0] == 0)
						fapi_b4860_log_fsl_direct(body);
					else
						fapi_b4860_log_fsl(body);
				}
				break;
		}
	}

	return NULL;
}

typedef enum
{
	DTRX = 0, DCONFIG, DRACH, DFAPI, DLASTCOMP
} log_comp_id_t;

typedef enum
{
	LOGL_DEBUG = 0, LOGL_INFO, LOGL_WARN, LOGL_ERROR, LOGL_NONE
} log_level_t;
typedef struct
{
	uint32_t flag;
	uint32_t tick;
	log_comp_id_t comp;
	log_level_t level;
	uint32_t frame;
	uint32_t subframe;
	//const char *file;
	//const char *func;
	int line;
	uint8_t msg[256];
	int32_t nargs;
	int32_t args[64];
} dentry_ipc_t;

static void fapi_b4860_log_fsl(char * log_msg)
{
	/* Получено новое сообщение */
	switch (log_msg[1])
	{
		case 'E':
			LOG_E(L1, log_msg);
			break;

		case 'W':
			LOG_W(L1, log_msg);
			break;

		case 'I':
			LOG_I(L1, log_msg);
			break;

		case 'D':
			LOG_D(L1, log_msg);
			break;

		default:
			LOG_I(L1, log_msg);
			break;
	}
}

static void fapi_b4860_log_fsl_direct(void *p)
{
#ifdef ENABLE_B4860_DIRECT_LOG
	dentry_ipc_t *e4860 = (dentry_ipc_t *)p;
	log_level_e l_b4860_to_l_ppc[] = {LOG_LEVEL_DEBUG, LOG_LEVEL_INFO, LOG_LEVEL_WARNING, LOG_LEVEL_ERROR, LOG_LEVEL_NONE};

	log_line_internal_array(L1, l_b4860_to_l_ppc[e4860->level], e4860->nargs, "B4860 L1",
	                             e4860->line, e4860->msg, e4860->args);
#endif
}

static void fapi_b4860_process_p7_message(fapi_l1_message_header_t *msg)
{
	switch (msg->message_id)
	{
		case FAPI_SUBFRAME_INDICATION:
			block_queue_push(fapi_p7_subframe_ind_queue, msg, sizeof(fapi_l1_message_header_t));
			break;

		case FAPI_RACH_INDICATION:
			block_queue_push(fapi_p7_rach_ind_queue, msg, sizeof(fapi_l1_message_header_t));
			break;

		case FAPI_RX_ULSCH_INDICATION:
		{
			fapi_rx_indication_t *rx_ind_msg = (fapi_rx_indication_t *) msg;
			/* Преобразование физических адресов в виртуальные */
			for (uint32_t i = 0; i < rx_ind_msg->rx_indication_body.number_of_pdus; i++)
			{
				rx_ind_msg->rx_indication_body.rx_pdu_list[i].data_ptr = (void*) ipc_p2v((phys_addr_t)rx_ind_msg->rx_indication_body.rx_pdu_list[i].data_ptr);
			}
			//LOG_D(FAPI, "Got RX.ind for %i:%i\n", rx_ind_msg->frame, rx_ind_msg->subframe);
			 block_queue_push(fapi_p7_rx_ind_queue, msg, sizeof(fapi_l1_message_header_t));
		}
			break;

		case FAPI_RX_SR_INDICATION:
		{
			block_queue_push(fapi_p7_sr_ind_queue, msg, sizeof(fapi_l1_message_header_t));
		}
			break;

		case FAPI_HARQ_INDICATION:
		{
			block_queue_push(fapi_p7_harq_ind_queue, msg, sizeof(fapi_l1_message_header_t));
		}
			break;

		case FAPI_RX_CQI_INDICATION:
		{
			block_queue_push(fapi_p7_cqi_ind_queue, msg, sizeof(fapi_l1_message_header_t));
		}
			break;
		
		case FAPI_CRC_INDICATION:
		{
			block_queue_push(fapi_p7_crc_ind_queue, msg, sizeof(fapi_l1_message_header_t));
		}
			break;

		default:
			block_queue_push(fapi_p7_ipc_queue, msg, sizeof(fapi_l1_message_header_t));
			break;
	}
}
