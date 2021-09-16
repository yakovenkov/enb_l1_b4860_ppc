/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <assert.h>
#include <malloc.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#include "core/log.h"
#include "core/file.h"
#include "tbs_tables.h"

#include "l2_fapi_generator/l2_fapi_generator.h"
#include "l2_fapi_generator/l2_fapi_generator_defs.h"

#include <ASN1/L2-FAPI-GENERATOR-CONFIG.h>
#include <asn1/gser_decoder.h>
#include <asn1/per_encoder.h>


//#define DEBUG_ASN_STIM

#ifdef L2_FAPI_GENERATOR_APP_STANDALONE
#define FUNC_VISIBILITY static
#else
#define FUNC_VISIBILITY
#endif

static int is_running = 1;
static l2_fapi_generator_t *g_l2_fapi_gen = NULL;

/**
 * @brief
 *
 * @param path
 * @param cfg
 * @return true
 * @return false
 */
static bool prepare_l2_fapi_generator_configs(const char *stim_path, l2_fapi_generator_config_t *cfg)
{
	cfg->l1_name = "l1_b4860_v1";
	cfg->l1_config = "l1.conf";
	cfg->l2_stim_filename = stim_path;

	return true;
}

/**
 * @brief
 *
 * @param l2_fapi_gen
 * @param l2_fapi_gen_config
 * @return int32_t
 */
int32_t l2_fapi_generator_app_parse_general(l2_fapi_generator_t *l2_fapi_gen, L2_FAPI_GENERATOR_CONFIG_t *stim)
{
	l2_fapi_gen->config.use_sibs_for_p5 = stim->general.use_sibs_for_p5;
	l2_fapi_gen->config.auto_mib = stim->general.auto_mib;
	l2_fapi_gen->config.auto_sibs = stim->general.auto_sibs;
	l2_fapi_gen->config.stim_start_tti = stim->general.stim_start_tti;
	l2_fapi_gen->config.stim_duration = stim->general.stim_duration;
	l2_fapi_gen->config.stim_loop = stim->general.stim_loop;

	l2_fapi_gen->config.rru_active = stim->general.rru.active;
	l2_fapi_gen->config.rru_earfcn = stim->general.rru.earfcn;

	return 0;
}
int32_t l2_fapi_generator_app_parse_p5(l2_fapi_generator_t *l2_fapi_gen, L2_FAPI_GENERATOR_CONFIG_t *stim)
{
	// Fill P5 configuration
	L2_FAPI_GENERATOR_CONFIG_t *cfg = stim;
	fapi_config_t *fc = &l2_fapi_gen->config.fapi_config;

	fc->subframe_config.duplex_mode = cfg->p5.duplex;
	fc->subframe_config.dl_cyclic_prefix_type = cfg->p5.cyclic_prefix;
	fc->subframe_config.ul_cyclic_prefix_type = cfg->p5.cyclic_prefix;
	fc->subframe_config.pb = cfg->p5.p_b;
	fc->subframe_config.pcfich_power_offset = cfg->p5.pcfich_power_offset;

	fc->rf_config.dl_channel_bandwidth = cfg->p5.rf_config.nprb;
	fc->rf_config.ul_channel_bandwidth = cfg->p5.rf_config.nprb;
	fc->rf_config.tx_antenna_ports = cfg->p5.rf_config.tx_antenna_ports;
	fc->rf_config.rx_antenna_ports = cfg->p5.rf_config.rx_antenna_ports;
	fc->rf_config.reference_signal_power = cfg->p5.rf_config.rs_power;

	fc->phich_config.phich_duration = cfg->p5.phich_config.phich_duration;
	fc->phich_config.phich_power_offset = cfg->p5.phich_config.phich_power_offset;
	fc->phich_config.phich_resource = cfg->p5.phich_config.phich_resource;

	fc->sch_config.physical_cell_id = cfg->p5.sch_config.physical_cell_id;
	fc->sch_config.primary_synchronization_signal_epre_eprers = cfg->p5.sch_config.pss_epre;
	fc->sch_config.secondary_synchronization_signal_epre_eprers = cfg->p5.sch_config.sss_epre;

	fc->prach_config.configuration_index = cfg->p5.prach_config.configuration_index;
	fc->prach_config.frequency_offset = cfg->p5.prach_config.frequency_offset;
	fc->prach_config.high_speed_flag = cfg->p5.prach_config.high_speed_flag;
	fc->prach_config.root_sequence_index = cfg->p5.prach_config.root_sequence_index;
	fc->prach_config.zero_correlation_zone_configuration = cfg->p5.prach_config.zero_correlation_zone_configuration;

	fc->pusch_config.hopping_mode = cfg->p5.pusch_config.hopping_mode;
	fc->pusch_config.hopping_offset = cfg->p5.pusch_config.hopping_offset;
	fc->pusch_config.number_of_subbands = cfg->p5.pusch_config.number_of_subbands;

	fc->pucch_config.delta_pucch_shift = cfg->p5.pucch_config.delta_pucch_shift;
	fc->pucch_config.n1_pucch_an = cfg->p5.pucch_config.n1_pucch_an;
	fc->pucch_config.n_an_cs = cfg->p5.pucch_config.n_an_cs;
	fc->pucch_config.n_cqi_rb = cfg->p5.pucch_config.n_cqi_rb;

	fc->uplink_reference_signal_config.cyclic_shift_1_for_drms =
		cfg->p5.uplink_reference_signal_config.cyclic_shift_1_for_drms;
	fc->uplink_reference_signal_config.group_assignment = cfg->p5.uplink_reference_signal_config.group_assignment;
	fc->uplink_reference_signal_config.uplink_rs_hopping = cfg->p5.uplink_reference_signal_config.uplink_rs_hopping;

	return 0;
}

#define ASSIGN_OPTIONAL(x, d) ((x) ? (*x) : d)

static int32_t parse_p7_dl_cfg_dci(DL_CONFIG_PDU_TYPE_DCI_t *stim_dci, fapi_dl_config_dci_dl_pdu *fapi_dci)
{
	fapi_dci->dci_dl_pdu_rel8.dci_format = stim_dci->format;

	fapi_dci->dci_dl_pdu_rel8.cce_idx = stim_dci->cce;
	fapi_dci->dci_dl_pdu_rel8.aggregation_level = stim_dci->l;
	fapi_dci->dci_dl_pdu_rel8.rnti = stim_dci->rnti;
	fapi_dci->dci_dl_pdu_rel8.resource_allocation_type = stim_dci->ra_type;

	fapi_dci->dci_dl_pdu_rel8.virtual_resource_block_assignment_flag = ASSIGN_OPTIONAL(stim_dci->vrb_flag, 0);
	fapi_dci->dci_dl_pdu_rel8.resource_block_coding = stim_dci->rb_coding;
	fapi_dci->dci_dl_pdu_rel8.mcs_1 = stim_dci->mcs_1;
	fapi_dci->dci_dl_pdu_rel8.redundancy_version_1 = stim_dci->rv_1;
	fapi_dci->dci_dl_pdu_rel8.new_data_indicator_1 = stim_dci->ndi_1;
	fapi_dci->dci_dl_pdu_rel8.transport_block_to_codeword_swap_flag = ASSIGN_OPTIONAL(stim_dci->tb_swap, 0);
	fapi_dci->dci_dl_pdu_rel8.mcs_2 = ASSIGN_OPTIONAL(stim_dci->mcs_2, 0);
	fapi_dci->dci_dl_pdu_rel8.redundancy_version_2 = ASSIGN_OPTIONAL(stim_dci->rv_2, 0);
	fapi_dci->dci_dl_pdu_rel8.new_data_indicator_2 = ASSIGN_OPTIONAL(stim_dci->ndi_2, 0);
	fapi_dci->dci_dl_pdu_rel8.harq_process = stim_dci->harq;
	fapi_dci->dci_dl_pdu_rel8.tpmi = ASSIGN_OPTIONAL(stim_dci->tpmi, 0);
	fapi_dci->dci_dl_pdu_rel8.pmi = ASSIGN_OPTIONAL(stim_dci->pmi, 0);
	fapi_dci->dci_dl_pdu_rel8.precoding_information = ASSIGN_OPTIONAL(stim_dci->pi, 0);
	fapi_dci->dci_dl_pdu_rel8.tpc = ASSIGN_OPTIONAL(stim_dci->tpc, 0);
	fapi_dci->dci_dl_pdu_rel8.downlink_assignment_index = ASSIGN_OPTIONAL(stim_dci->downlink_index, 0);
	fapi_dci->dci_dl_pdu_rel8.ngap = ASSIGN_OPTIONAL(stim_dci->n_gap, 0);
	fapi_dci->dci_dl_pdu_rel8.transport_block_size_index = ASSIGN_OPTIONAL(stim_dci->tbs_index, 0);
	fapi_dci->dci_dl_pdu_rel8.downlink_power_offset = ASSIGN_OPTIONAL(stim_dci->power_offset, 0);
	fapi_dci->dci_dl_pdu_rel8.allocate_prach_flag = ASSIGN_OPTIONAL(stim_dci->prach_flag, 0);
	fapi_dci->dci_dl_pdu_rel8.preamble_index = ASSIGN_OPTIONAL(stim_dci->preamble_index, 0);
	fapi_dci->dci_dl_pdu_rel8.prach_mask_index = ASSIGN_OPTIONAL(stim_dci->prach_mask, 0);
	fapi_dci->dci_dl_pdu_rel8.rnti_type = stim_dci->rnti_type;
	fapi_dci->dci_dl_pdu_rel8.transmission_power = stim_dci->power;

	return 0;
}

static uint32_t conv_nprb(uint32_t ra_header, uint32_t rb_alloc, int32_t N_RB_DL)
{
	uint32_t nprb = 0, i;

	switch(N_RB_DL)
	{
		case 6:
			for(i = 0; i < 6; i++)
			{
				if((rb_alloc & (1 << i)) != 0)
					nprb += 1;
			}

			break;

		case 25:
			if(ra_header == 0)
			{ // Type 0 Allocation

				for(i = 12; i > 0; i--)
				{
					if((rb_alloc & (1 << i)) != 0)
						nprb += 2;
				}

				if((rb_alloc & 1) != 0)
					nprb += 1;
			}
			else if(ra_header == 2)
			{
				int rb_start = rb_alloc % N_RB_DL;
				nprb = (rb_alloc / N_RB_DL) + 1;
				if(rb_start + nprb > N_RB_DL)
				{
					nprb = N_RB_DL - rb_start;
				}
			}
			else
			{
				for(i = 0; i < 11; i++)
				{
					if((rb_alloc & (1 << (i + 2))) != 0)
						nprb += 1;
				}
			}

			break;

		case 50:
			if(ra_header == 0)
			{ // Type 0 Allocation

				for(i = 0; i < 16; i++)
				{
					if((rb_alloc & (1 << (16 - i))) != 0)
						nprb += 3;
				}

				if((rb_alloc & 1) != 0)
					nprb += 2;
			}
			else
			{
				for(i = 0; i < 17; i++)
				{
					if((rb_alloc & (1 << (i + 2))) != 0)
						nprb += 1;
				}
			}

			break;

		case 100:
			if(ra_header == 0)
			{ // Type 0 Allocation

				for(i = 0; i < 25; i++)
				{
					if((rb_alloc & (1 << (24 - i))) != 0)
						nprb += 4;
				}
			}
			else
			{
				for(i = 0; i < 25; i++)
				{
					if((rb_alloc & (1 << (i + 2))) != 0)
						nprb += 1;
				}
			}

			break;

		default:
			LOG_E(L2_FAPI_GEN, "Invalid N_RB_DL %d\n", N_RB_DL);
			break;
	}

	return (nprb);
}

static int32_t parse_p7_dl_cfg_dlsch(l2_fapi_generator_t *l2_fapi_gen, DL_CONFIG_PDU_TYPE_DLSCH_t *stim_dlsch, fapi_dl_config_dlsch_pdu *fapi_dlsch,
									 fapi_tx_request_pdu_t *fapi_tx_req)
{
	int32_t i;

	fapi_dlsch->dlsch_pdu_rel8.length = 0;
	fapi_dlsch->dlsch_pdu_rel8.pdu_index = 0;
	fapi_dlsch->dlsch_pdu_rel8.rnti = stim_dlsch->rnti;
	fapi_dlsch->dlsch_pdu_rel8.resource_allocation_type = stim_dlsch->ra_type;
	fapi_dlsch->dlsch_pdu_rel8.virtual_resource_block_assignment_flag = ASSIGN_OPTIONAL(stim_dlsch->vrb_flag, 0);
	fapi_dlsch->dlsch_pdu_rel8.resource_block_coding = stim_dlsch->rb_coding;
	fapi_dlsch->dlsch_pdu_rel8.modulation = stim_dlsch->modulation;
	fapi_dlsch->dlsch_pdu_rel8.redundancy_version = stim_dlsch->rv;
	fapi_dlsch->dlsch_pdu_rel8.transport_blocks = stim_dlsch->tb;
	fapi_dlsch->dlsch_pdu_rel8.transport_block_to_codeword_swap_flag = ASSIGN_OPTIONAL(stim_dlsch->tb_swap, 0);
	fapi_dlsch->dlsch_pdu_rel8.transmission_scheme = ASSIGN_OPTIONAL(stim_dlsch->tx_scheme, 0);
	fapi_dlsch->dlsch_pdu_rel8.number_of_layers = ASSIGN_OPTIONAL(stim_dlsch->n_layers, 1);
	fapi_dlsch->dlsch_pdu_rel8.number_of_subbands = ASSIGN_OPTIONAL(stim_dlsch->n_subbands, 0);
	// fapi_dlsch->dlsch_pdu_rel8.codebook_index[FAPI_MAX_NUM_SUBBANDS];
	fapi_dlsch->dlsch_pdu_rel8.ue_category_capacity = ASSIGN_OPTIONAL(stim_dlsch->ue_categiry, 0);
	fapi_dlsch->dlsch_pdu_rel8.pa = ASSIGN_OPTIONAL(stim_dlsch->p_a, 0);
	fapi_dlsch->dlsch_pdu_rel8.delta_power_offset_index = ASSIGN_OPTIONAL(stim_dlsch->delta_power, 0);
	fapi_dlsch->dlsch_pdu_rel8.ngap = ASSIGN_OPTIONAL(stim_dlsch->n_gap, 0);
	fapi_dlsch->dlsch_pdu_rel8.nprb = ASSIGN_OPTIONAL(stim_dlsch->n_prb, 0);
	fapi_dlsch->dlsch_pdu_rel8.transmission_mode = ASSIGN_OPTIONAL(stim_dlsch->tm, 0);

	// fapi_dlsch->dlsch_pdu_rel8.num_bf_prb_per_subband = 0;
	// fapi_dlsch->dlsch_pdu_rel8.num_bf_vector = 0;
	// fapi_dlsch->dlsch_pdu_rel8.bf_vector[FAPI_MAX_BF_VECTORS];

	fapi_tx_req->num_segments = 1;
	fapi_tx_req->tbs[0].tb_tag = 1;
	fapi_tx_req->tbs[0].tb_length = stim_dlsch->pdu.size;

	// Здесь делается коррекция TBS по MCS/RB, просто брать PDU из файла нельзя, т.к. может быть некорректная  длина
	int32_t nprb = conv_nprb(fapi_dlsch->dlsch_pdu_rel8.resource_allocation_type, fapi_dlsch->dlsch_pdu_rel8.resource_block_coding,
		l2_fapi_gen->config.fapi_config.rf_config.dl_channel_bandwidth);

	int32_t new_tbs = fapi_tx_req->tbs[0].tb_length;

	// Округление до ближайшего TBS в большую сторону
	if(nprb > 0 && nprb < 110)
	{
		extern const int tbs_table[27][110];
		for(i=0; i<27; i++)
		{
			if((fapi_tx_req->tbs[0].tb_length << 3) <= tbs_table[i][nprb - 1])
			{
				new_tbs = tbs_table[i][nprb - 1] / 8;
				break;
			}
		}

		if(new_tbs != fapi_tx_req->tbs[0].tb_length)
		{
			LOG_I(L2_FAPI_GEN, "Correcting TBS from %i to %i, mcs %i nprb %i\n", fapi_tx_req->tbs[0].tb_length, new_tbs, i, nprb);
			fapi_tx_req->tbs[0].tb_length = new_tbs;
		}

		//sleep(2);
		//exit(0);
	}
	else
	{
		LOG_E(L2_FAPI_GEN, "Invalid nprb %i\n", nprb);
	}

	fapi_tx_req->tbs[0].tb_data = calloc(fapi_tx_req->tbs[0].tb_length, 1);
	memcpy(fapi_tx_req->tbs[0].tb_data, stim_dlsch->pdu.buf, stim_dlsch->pdu.size);

	return 0;
}

static int32_t parse_p7_dl_cfg(l2_fapi_generator_t *l2_fapi_gen, L2_FAPI_GENERATOR_CONFIG__p7__Member *stim, scheduler_dl_config_request_t *fapi_dl_cfg)
{
	int32_t i;
	bzero(fapi_dl_cfg, sizeof(scheduler_dl_config_request_t));

	fapi_dl_cfg->number_pdcch_ofdm_symbols = stim->dl_config.cfi;
	fapi_dl_cfg->transmission_power_pcfich = stim->dl_config.cfi_power;

	for(i = 0; i < stim->dl_config.pdus->list.count; i++)
	{
		switch(stim->dl_config.pdus->list.array[i]->present)
		{
			case DL_CONFIG_PDU_TYPE_PR_dci:
				if(parse_p7_dl_cfg_dci(&stim->dl_config.pdus->list.array[i]->choice.dci,
									   &fapi_dl_cfg->dl_config_dci_list[fapi_dl_cfg->number_dci].dci_dl_pdu) == 0)
				{
					fapi_dl_cfg->dl_config_dci_list[fapi_dl_cfg->number_dci].pdu_type = FAPI_DL_CONFIG_DCI_DL_PDU_TYPE;
					fapi_dl_cfg->number_dci++;
				}
				break;

			case DL_CONFIG_PDU_TYPE_PR_bch:
				if(g_l2_fapi_gen->config.auto_mib)
				{
					LOG_W(L2_FAPI_GEN, "Skipping BCH PDU at %i due to auto_mib option is set\n", i);
					continue;
				}

				if(fapi_dl_cfg->number_pdu != 0)
				{
					LOG_W(L2_FAPI_GEN, "Item %i: stim BCH PDU should be the first, skipping\n", i);
					continue;
				}

				{
					fapi_dl_cfg->dl_config_pdu_list[fapi_dl_cfg->number_pdu].pdu_type = FAPI_DL_CONFIG_BCH_PDU_TYPE;
					fapi_dl_cfg->dl_config_pdu_list[fapi_dl_cfg->number_pdu].bch_pdu.bch_pdu_rel8.pdu_index =
						fapi_dl_cfg->number_tx_req;
					fapi_dl_cfg->dl_config_pdu_list[fapi_dl_cfg->number_pdu].bch_pdu.bch_pdu_rel8.transmission_power =
						stim->dl_config.pdus->list.array[i]->choice.bch.power;
					fapi_dl_cfg->number_pdu++;

					fapi_dl_cfg->tx_request_pdu_list[fapi_dl_cfg->number_tx_req].pdu_index = fapi_dl_cfg->number_tx_req;
					fapi_dl_cfg->tx_request_pdu_list[fapi_dl_cfg->number_tx_req].tbs[0].tb_tag = 1;

					fapi_dl_cfg->tx_request_pdu_list[fapi_dl_cfg->number_tx_req].tbs[0].tb_data =
						calloc(stim->dl_config.pdus->list.array[i]->choice.bch.pdu.size, 1);
					memcpy(fapi_dl_cfg->tx_request_pdu_list[fapi_dl_cfg->number_tx_req].tbs[0].tb_data,
						   stim->dl_config.pdus->list.array[i]->choice.bch.pdu.buf,
						   stim->dl_config.pdus->list.array[i]->choice.bch.pdu.size);

					fapi_dl_cfg->tx_request_pdu_list[fapi_dl_cfg->number_tx_req].tbs[0].tb_length =
						stim->dl_config.pdus->list.array[i]->choice.bch.pdu.size;

					fapi_dl_cfg->number_tx_req++;
				}
				break;

			case DL_CONFIG_PDU_TYPE_PR_dlsch:
				if(parse_p7_dl_cfg_dlsch(l2_fapi_gen, &stim->dl_config.pdus->list.array[i]->choice.dlsch,
										 &fapi_dl_cfg->dl_config_pdu_list[fapi_dl_cfg->number_pdu].dlsch_pdu,
										 &fapi_dl_cfg->tx_request_pdu_list[fapi_dl_cfg->number_tx_req]) == 0)
				{
					fapi_dl_cfg->dl_config_pdu_list[fapi_dl_cfg->number_pdu].pdu_type = FAPI_DL_CONFIG_DLSCH_PDU_TYPE;
					fapi_dl_cfg->dl_config_pdu_list[fapi_dl_cfg->number_pdu].dlsch_pdu.dlsch_pdu_rel8.pdu_index =
						fapi_dl_cfg->number_tx_req;
					fapi_dl_cfg->tx_request_pdu_list[fapi_dl_cfg->number_tx_req].pdu_index = fapi_dl_cfg->number_tx_req;

					fapi_dl_cfg->number_pdu++;
					fapi_dl_cfg->number_tx_req++;
				}
				break;

			default:
				LOG_I(L2_FAPI_GEN, "Unknown stim DL_CONFIG.req PDU item %i type %i\n", i,
					  stim->dl_config.pdus->list.array[i]->present);
		}
	}

	if(fapi_dl_cfg->number_pdu != fapi_dl_cfg->number_tx_req)
	{
		LOG_E(L2_FAPI_GEN, "DL_CONFIG.req->number_pdu (%i) != TX.req->number_pdu (%i)\n", fapi_dl_cfg->number_pdu,
			  fapi_dl_cfg->number_tx_req);
		return -1;
	}

	return 0;
}

static int32_t parse_p7_ul_cfg(l2_fapi_generator_t *l2_fapi_gen, L2_FAPI_GENERATOR_CONFIG__p7__Member *stim,
	scheduler_ul_config_request_t *fapi_ul_cfg)
{
	int32_t i;
	int32_t npdus = 0;
	int32_t pdu_offset = 0;
	bzero(fapi_ul_cfg, sizeof(scheduler_ul_config_request_t));
	
	//fapi_ul_cfg->number_pdu = stim->dl_config.cfi;

	return 0;
}

static int32_t l2_fapi_generator_app_parse_p7(l2_fapi_generator_t *l2_fapi_gen, L2_FAPI_GENERATOR_CONFIG_t *stim)
{
	int32_t i;
	l2_fapi_gen->stim_len = stim->p7.list.count;

	if(l2_fapi_gen->stim_len == 0)
	{
		return 0;
	}

	l2_fapi_gen->stim_list =
		(l2_fapi_generator_stim_item_t *)calloc(l2_fapi_gen->stim_len, sizeof(l2_fapi_generator_stim_item_t));

	for(i = 0; i < stim->p7.list.count; i++)
	{
		l2_fapi_gen->stim_list[i].tti = stim->p7.list.array[i]->tti;

		if(parse_p7_dl_cfg(l2_fapi_gen, stim->p7.list.array[i], &l2_fapi_gen->stim_list[i].dl_cfg_req_list) != 0)
		{
			LOG_E(L2_FAPI_GEN, "Error parsing DL_CONFIG.req for tti %i\n", stim->p7.list.array[i]->tti);
			return -1;
		}

		if(parse_p7_ul_cfg(l2_fapi_gen, stim->p7.list.array[i], &l2_fapi_gen->stim_list[i].ul_cfg_req_list))
		{
			LOG_E(L2_FAPI_GEN, "Error parsing UL_CONFIG.req for tti %i\n", stim->p7.list.array[i]->tti);
			return -1;
		}
	}

	return 0;
}

static int read_stim_config(l2_fapi_generator_t *l2_fapi_gen)
{
	uint8_t *asn_text_buffer = NULL;
	int asn_text_buffer_size = 0;
	asn_dec_rval_t dec_rval = {};

	int num_bytes_readed = read_file(l2_fapi_gen->config.l2_stim_filename, &asn_text_buffer, &asn_text_buffer_size);

	if(asn_text_buffer != NULL && num_bytes_readed > 0)
	{
		L2_FAPI_GENERATOR_CONFIG_t *stim = NULL;

		dec_rval =
			gser_decode(NULL, &asn_DEF_L2_FAPI_GENERATOR_CONFIG, (void **)&stim, asn_text_buffer, num_bytes_readed);

		free(asn_text_buffer);

		if(dec_rval.code == RC_OK && stim != NULL)
		{
#ifdef DEBUG_ASN_STIM
			asn_enc_rval_t enc_rval = {};
			byte_buffer_t *b = byte_buffer_pool_allocate();
			enc_rval = gser_encode(&asn_DEF_L2_FAPI_GENERATOR_CONFIG, stim, GSER_F_BASIC, consume_to_byte_buffer_cb, b);

			if(enc_rval.encoded > 0)
			{
				printf("%s\n", b->ptr);
			}

			byte_buffer_pool_deallocate(b);
#endif
			if(l2_fapi_generator_app_parse_general(l2_fapi_gen, stim) != 0)
			{
				LOG_E(L2_FAPI_GEN, "Error parsing GENERAL section of stim file %s\n",
					  l2_fapi_gen->config.l2_stim_filename);
				return -1;
			}

			if(l2_fapi_generator_app_parse_p5(l2_fapi_gen, stim) != 0)
			{
				LOG_E(L2_FAPI_GEN, "Error parsing P5 section of stim file %s\n", l2_fapi_gen->config.l2_stim_filename);
				return -1;
			}

			if(l2_fapi_generator_app_parse_p7(l2_fapi_gen, stim) != 0)
			{
				LOG_E(L2_FAPI_GEN, "Error parsing P7 section of stim file %s\n", l2_fapi_gen->config.l2_stim_filename);
				return -1;
			}
		}
		else
		{
			LOG_E(L2_FAPI_GEN, "Failed to parse stim file %s\n", l2_fapi_gen->config.l2_stim_filename);
			return -1;
		}
	}
	else
	{
		LOG_E(L2_FAPI_GEN, "Failed to read stim file %s\n", l2_fapi_gen->config.l2_stim_filename);
		return -1;
	}

	return 0;
}

FUNC_VISIBILITY
int l2_fapi_generator_app_entry_point(int argc, char **argv)
{
	g_l2_fapi_gen = create_l2_fapi_generator();

	g_l2_fapi_gen->no = 0;
	
	if(argc < 2)
	{
		LOG_E(L2_FAPI_GEN, "Usage: l2_fapi_generator_dl <stim_file>\n");
		return -1;
	}

	prepare_l2_fapi_generator_configs(argv[1], &g_l2_fapi_gen->config);

	// Read L2 FAPI JSON Stimuli file
	if(read_stim_config(g_l2_fapi_gen) != 0)
	{
		LOG_E(L2_FAPI_GEN, "Error reading L2 FAPI stim %s\n", g_l2_fapi_gen->config.l2_stim_filename);
		return -1;
	}

	if(!l2_fapi_generator_init(g_l2_fapi_gen))
	{
		LOG_E(L2_FAPI_GEN, "Failed to init L2 FAPI Generator\n");
		return -1;
	}

	l2_fapi_generator_start(g_l2_fapi_gen);

	return 0;
}

FUNC_VISIBILITY
void l2_fapi_generator_app_exit_point()
{
	// STUB
	l2_fapi_generator_stop(g_l2_fapi_gen);

	sleep(3);
}

static void sig_int_handler(int signo)
{
	is_running = 0;
}

int main(int argc, char **argv)
{
	signal(SIGINT, sig_int_handler);
	signal(SIGKILL, sig_int_handler);
	if (is_rt_priority_allowed())
	{
		LOG_I(L2_FAPI_GEN, "Lock memory for real-time\n");
		mlockall(MCL_CURRENT | MCL_FUTURE);
	}
	else
	{
		LOG_W(L2_FAPI_GEN, "Real-time priority is not allowed\n");
	}
	
	log_set_level(LOG_LEVEL_INFO);
	
	if(l2_fapi_generator_app_entry_point(argc, argv) < 0)
	{
		LOG_E(MAC, "Failed to init eNodeb DU\n");
		goto on_exit;
	}

	while(is_running)
	{
		sleep(1);
	}

	l2_fapi_generator_app_exit_point();

on_exit:
	return 0;
}