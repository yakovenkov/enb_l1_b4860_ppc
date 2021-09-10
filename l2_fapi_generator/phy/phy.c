/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <core/log.h>
#include <core/thread_helper.h>
#include <core/block_queue.h>

#include <fapi.h>

#include "l2_fapi_generator_defs.h"

#include "phy.h"

static pthread_t phy_thread_h;
static int32_t l1_connected = 0;
static int32_t l1_running = 0;

static void phy_start_l1();
static void phy_configure_l1();
static void phy_stop_l1();

static void *phy_dispatcher_thread(void *params);

#define USE_SINGLE_PRACH
#define MAX_PRACH_OFFSET_US 50

/* Weak stubs */
__attribute__((weak)) void l2_fapi_generator_tti_begin(l2_fapi_generator_t *fapi_gen)
{
}

__attribute__((weak)) void l2_fapi_generator_tti_end(l2_fapi_generator_t *fapi_gen)
{
}

__attribute__((weak)) int32_t mac_new_rach(l2_fapi_generator_t *enb, uint32_t tti, uint32_t preamble, uint32_t ta,
                                           uint32_t power)
{
	return 0;
}

__attribute__((weak)) int32_t l2_fapi_schedule(l2_fapi_generator_t *fapi_gen)
{
	return 0;
}

static void phy_start_l1()
{
	if (l1_running)
	{
		phy_stop_l1();
	}
	/*
	 * Запуск L1
	 * По-идее, если не прошла конфигурация, то этого делать лучше не стоит...
	 */
	fapi_p5_start_request();
	l1_running = 1;
}

static void phy_configure_l1(enb_4g_phy_t *phy)
{
	fapi_config_response_t *fapi_config_resp;
	fapi_config_t fapi_config;

	memset(&fapi_config, 0, sizeof(fapi_config_t));

	/* Конфигурация L1 */
	memcpy(&fapi_config, &phy->config->fapi_config, sizeof(fapi_config_t));

	LOG_I(PHY, "Configuring L1 for %s duplex\n", (phy->config->fapi_config.subframe_config.duplex_mode == FAPI_DUPLEXING_MODE_TDD ? "TDD" : "FDD"));
	if (phy->config->fapi_config.subframe_config.duplex_mode == FAPI_DUPLEXING_MODE_TDD)
	{
		LOG_I(PHY, "TDD configuration %i, SSF configuration %i\n",
			  phy->config->fapi_config.tdd_frame_structure_config.subframe_assignment,
			  phy->config->fapi_config.tdd_frame_structure_config.special_subframe_patterns);
	}

	if (fapi_p5_config_request(&fapi_config, &fapi_config_resp) != 0)
	{
		LOG_E(PHY, "Error receiving CONFIG.response from L1!\n");
	}
	else
	{
		if (fapi_config_resp != NULL)
		{
			LOG_I(PHY, "Got configuration response code: %i\n", fapi_config_resp->error_code);
		}
	}
}

static void phy_stop_l1()
{
	fapi_p5_stop_request();
	usleep(100000);
	l1_running = 0;
}

static void *phy_dispatcher_thread(void *param)
{
	assert(param != NULL);

	l2_fapi_generator_t *l2_fapi_gen = (l2_fapi_generator_t *) param;
	block_queue_t *msg_q_sf_ind;
	
	enb_4g_phy_t *phy = &l2_fapi_gen->phy;
	enb_4g_phy_proc_t *proc = &phy->proc;

	bzero(proc->rx_ind_q, sizeof(proc->rx_ind_q));
	bzero(proc->harq_ind_q, sizeof(proc->harq_ind_q));
	bzero(proc->sr_ind_q, sizeof(proc->sr_ind_q));
	bzero(proc->cqi_ind_q, sizeof(proc->cqi_ind_q));

	scheduler_dl_config_request_t *dl_cfg_req;
	scheduler_ul_config_request_t *ul_cfg_req;

	block_queue_t *msg_q_rach_ind = fapi_get_rach_ind_queue();
	block_queue_t *msg_q_rx_ind = fapi_get_rx_ind_queue();
	block_queue_t *msg_q_harq_ind = fapi_get_harq_ind_queue();
	block_queue_t *msg_q_sr_ind = fapi_get_sr_ind_queue();
	block_queue_t *msg_q_cqi_ind = fapi_get_cqi_ind_queue();
	block_queue_t *msg_q_crc_ind = fapi_get_crc_ind_queue();


	thread_helper_thread_top_init("l2_phy_disp", THREAD_PRIO_L2_PHY_DISPATCHER, 0, 0, 0);

	// Set TTI so that first TX is at tti=0
	msg_q_sf_ind = fapi_get_subframe_ind_queue();

	phy_stop_l1();

	sleep(1);

	LOG_I(PHY, "Waiting for FAPI startup to complete...\n");

	// Main loop
	while (l2_fapi_gen->phy.running)
	{
		if (!l1_connected)
		{
			if (!fapi_is_connected())
			{
				usleep(10000);
				continue;
			}

			/* Start L1 */
			phy_stop_l1();
			phy_configure_l1(&l2_fapi_gen->phy);
			phy_start_l1();

			l1_connected = 1;
		}
		else
		{
			if (!fapi_is_connected())
			{
				l1_connected = 0;
				continue;
			}

			fapi_subframe_indication_t *msg = block_queue_pop_last(msg_q_sf_ind);

			if (msg == NULL)
			{
				continue;
			}

			LOG_D(PHY, "Got sf.ind for %i:%i\n", msg->frame, msg->subframe);

			l2_fapi_gen->abs_tti++;

			// Проверка, что tti не разбежались
			if((l2_fapi_gen->abs_tti % 10) != msg->subframe)
			{
				int32_t new_abs_tti = msg->frame * 10 + msg->subframe;
				
				LOG_W(L2_FAPI_GEN, "Correcting L2 FAPI Generator abs_tti from %i to %i\n", l2_fapi_gen->abs_tti, new_abs_tti);
				l2_fapi_gen->abs_tti = new_abs_tti;
			}

			phy_common_set_time(&l2_fapi_gen->phy, msg->frame, msg->subframe);

			log_set_tti(l2_fapi_gen->phy.proc.tti_tx);

		l2_fapi_generator_tti_begin(l2_fapi_gen);

		// Приём индикации от L1

		// Проверка RACH сообщений
		int32_t max_preamble_no = 0, max_preamble_val = 0;
		fapi_rach_indication_t *rach_ind_msg = NULL;

		while(block_queue_try_pop(msg_q_rach_ind, (void **)&rach_ind_msg))
		{
			uint32_t rach_tti_rx = rach_ind_msg->frame * 10 + rach_ind_msg->subframe;

			for(uint32_t i = 0; i < rach_ind_msg->rach_indication_body.number_of_preambles; i++)
			{
				LOG_I(PHY, "PRACH: %i/%i, preamble=%i TA=%i pwr=%i\n", i,
				      rach_ind_msg->rach_indication_body.number_of_preambles,
				      rach_ind_msg->rach_indication_body.preamble_list[i].r8.preamble,
				      rach_ind_msg->rach_indication_body.preamble_list[i].r8.timing_advance,
				      rach_ind_msg->rach_indication_body.preamble_list[i].r8.power);

#ifdef USE_SINGLE_PRACH
				if(max_preamble_val < rach_ind_msg->rach_indication_body.preamble_list[i].r8.power)
				{
					max_preamble_val = rach_ind_msg->rach_indication_body.preamble_list[i].r8.power;
					max_preamble_no = i;
				}
#else

				if(rach_ind_msg->rach_indication_body.preamble_list[i].r8.timing_advance < MAX_PRACH_OFFSET_US)
				{
					mac_new_rach(enb, tti_rx, msg->rach_indication_body.preamble_list[i].r8.preamble,
					             rach_ind_msg->rach_indication_body.preamble_list[i].r8.timing_advance / 16,
					             rach_ind_msg->rach_indication_body.preamble_list[i].r8.power);
				}
#endif
			}

#ifdef USE_SINGLE_PRACH
			mac_new_rach(l2_fapi_gen, rach_tti_rx,
			             rach_ind_msg->rach_indication_body.preamble_list[max_preamble_no].r8.preamble,
			             rach_ind_msg->rach_indication_body.preamble_list[max_preamble_no].r8.timing_advance / 16,
			             rach_ind_msg->rach_indication_body.preamble_list[max_preamble_no].r8.power);
#endif
		}

		//Результат демодуляции данных
		fapi_rx_indication_t *rx_ind_msg;
		while(block_queue_try_pop(msg_q_rx_ind, (void **)&rx_ind_msg))
		{
			proc->rx_ind_q[rx_ind_msg->subframe] = rx_ind_msg;
		}

		//Результат демодуляции подтверждений
		fapi_harq_indication_t *harq_ind_msg;
		while(block_queue_try_pop(msg_q_harq_ind, (void **)&harq_ind_msg))
		{
			if(harq_ind_msg->harq_indication_body.number_of_harqs > 0)
			{
				proc->harq_ind_q[harq_ind_msg->subframe] = harq_ind_msg;
			}
		}

		//Результат демодуляции Schedule Request
		fapi_sr_indication_t *sr_ind_msg;
		while(block_queue_try_pop(msg_q_sr_ind, (void **)&sr_ind_msg))
		{
			if(sr_ind_msg->sr_indication_body.number_of_srs > 0)
			{
				LOG_D(PHY, "Received SR.ind for %i:%i n_srs %i\n", sr_ind_msg->frame, sr_ind_msg->subframe,
				      sr_ind_msg->sr_indication_body.number_of_srs);
				proc->sr_ind_q[sr_ind_msg->subframe] = sr_ind_msg;
			}
		}

		//Результат демодуляции CQI
		fapi_cqi_indication_t *cqi_ind_msg;
		while(block_queue_try_pop(msg_q_cqi_ind, (void **)&cqi_ind_msg))
		{
			if(cqi_ind_msg->cqi_indication_body.number_of_cqis > 0)
			{
				LOG_D(PHY, "Received CQI.ind for %i:%i n_srs %i\n", cqi_ind_msg->frame, cqi_ind_msg->subframe,
				      cqi_ind_msg->cqi_indication_body.number_of_cqis);
				proc->cqi_ind_q[cqi_ind_msg->subframe] = cqi_ind_msg;
			}
		}

		//Результат CRC
		fapi_crc_indication_t *crc_ind_msg;
		while(block_queue_try_pop(msg_q_crc_ind, (void **)&crc_ind_msg))
		{
			if(crc_ind_msg->crc_indication_body.number_of_crcs > 0)
			{
				LOG_D(PHY, "Received CRC.ind for %i:%i n_srs %i\n", crc_ind_msg->frame, crc_ind_msg->subframe,
				      crc_ind_msg->crc_indication_body.number_of_crcs);
				proc->crc_ind_q[crc_ind_msg->subframe] = crc_ind_msg;
			}
		}

		rx_ind_msg = proc->rx_ind_q[phy->proc.sf_rx];
		crc_ind_msg = proc->crc_ind_q[phy->proc.sf_rx];

		if(rx_ind_msg != NULL)
		{
			phy_common_decode_pusch(phy, rx_ind_msg, crc_ind_msg);
			proc->rx_ind_q[phy->proc.sf_rx] = NULL;
		}
		
		proc->crc_ind_q[phy->proc.sf_rx] = NULL;

		harq_ind_msg = proc->harq_ind_q[phy->proc.sf_rx];
		sr_ind_msg = proc->sr_ind_q[phy->proc.sf_rx];
		cqi_ind_msg = proc->cqi_ind_q[phy->proc.sf_rx];

		if(harq_ind_msg != NULL || sr_ind_msg != NULL || cqi_ind_msg != NULL)
		{
			phy_common_decode_pucch(phy, harq_ind_msg, sr_ind_msg, cqi_ind_msg);

			proc->harq_ind_q[phy->proc.sf_rx] = NULL;
			proc->sr_ind_q[phy->proc.sf_rx] = NULL;
			proc->cqi_ind_q[phy->proc.sf_rx] = NULL;
		}

		if(l2_fapi_schedule(l2_fapi_gen) != 0)
		{
			LOG_E(PHY, "MAC error schedule\n");
			continue;
		}

		dl_cfg_req = fapi_scheduler_get_dl_config_request(phy->proc.frame_tx, phy->proc.sf_tx);
		ul_cfg_req = fapi_scheduler_get_ul_config_request(phy->proc.frame_tx, phy->proc.sf_tx);

		// Очистка всех счетчиков перед началом заполнения
		dl_cfg_req->number_pdu = 0;
		dl_cfg_req->number_dci = 0;
		dl_cfg_req->number_tx_req = 0;
		dl_cfg_req->number_dci0 = 0;
		dl_cfg_req->number_hi = 0;

		ul_cfg_req->number_pdu = 0;

		phy->proc.tx_pdu_index = 0;

		/* Сначала должна идти обработка сценария, т.к. при разборе файла сценария производится связывание PDU и TX_REQ
		 * Дополнительные PDU (BCH, SIBx, и т.д.) должны формировать после этого блока
		 */
		if(l2_fapi_gen->stim_list[l2_fapi_gen->stim_ptr].tti == (l2_fapi_gen->abs_tti % l2_fapi_gen->stim_len))
		{

			l2_fapi_generator_stim_item_t *stim_item = &l2_fapi_gen->stim_list[l2_fapi_gen->stim_ptr];

			//Формирование FAPI сообщений на основе полученных грантов от MAC
			phy_common_encode_pdcch_dl(stim_item, dl_cfg_req);
			phy_common_encode_pdsch(l2_fapi_gen, stim_item, dl_cfg_req);
			phy_common_encode_pusch(stim_item, ul_cfg_req);

			ul_cfg_req->srs_present = 0;

			/* DL_CONFIG.request отправляется в любом случае */
			dl_cfg_req->number_pdcch_ofdm_symbols = stim_item->dl_cfg_req_list.number_pdcch_ofdm_symbols;

			// dl_cfg_req->number_pdcch_ofdm_symbols = 1;

			l2_fapi_gen->stim_ptr++;
			if(l2_fapi_gen->stim_ptr >= l2_fapi_gen->stim_len)
				l2_fapi_gen->stim_ptr = 0;
		}

		if(l2_fapi_gen->config.auto_mib)
		{
			/* Формирование BCH */
			phy_common_encode_bch(phy, dl_cfg_req, phy->proc.tti_tx);
		}

		fapi_p7_dl_config_request(dl_cfg_req);

		/* Отправка UL_CONFIG.request при наличии */
		fapi_p7_ul_config_request(ul_cfg_req);

		ul_cfg_req->number_pdu = 0;

		/* TX.request оптравляется только при наличии данных */
		if(dl_cfg_req->number_tx_req > 0)
		{
			fapi_p7_tx_request(dl_cfg_req);
		}

		/* HI_DCI0.request оптарвляется только при наличии данных */
		if(dl_cfg_req->number_dci0 > 0 || dl_cfg_req->number_hi > 0)
		{
			fapi_p7_hi_dci0_request(dl_cfg_req);
		}

		dl_cfg_req->number_pdu = 0;
		dl_cfg_req->number_dci = 0;
		dl_cfg_req->number_tx_req = 0;
		dl_cfg_req->number_dci0 = 0;
		dl_cfg_req->number_hi = 0;
		}
	}

	l2_fapi_gen->phy.running = 0;
	phy_stop_l1();

	return NULL;
}

void phy_init(l2_fapi_generator_t *l2_fapi_gen, const l2_fapi_generator_config_t* config)
{
	assert(l2_fapi_gen != NULL);

	l2_fapi_gen->phy.running = 1;
	l2_fapi_gen->phy.config = config;

	pthread_create(&phy_thread_h, NULL, phy_dispatcher_thread, l2_fapi_gen);
}

void phy_stop(l2_fapi_generator_t *l2_fapi_gen)
{
	l2_fapi_gen->phy.running = 0;

	pthread_join(phy_thread_h, NULL);
}