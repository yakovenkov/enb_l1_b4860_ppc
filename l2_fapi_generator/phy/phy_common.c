/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <core/log.h>
//#include <core/byte_buffer.h>

#include <fapi.h>

#include "l2_fapi_generator_defs.h"
#include "phy_common.h"

void phy_common_set_time(enb_4g_phy_t *phy, uint32_t frame, uint32_t subframe)
{
	/* Пришедшее значение TTI указывает на номер TTI DL
	 * UL был на 4 TTI раньше
	 * Следующий UL будет через 4 TTI после
	 */

	phy->proc.tti_tx = frame * 10 + subframe;
	phy->proc.frame_tx = frame;
	phy->proc.sf_tx = subframe;

	phy->proc.tti_rx = (phy->proc.tti_tx >= 4) ? (phy->proc.tti_tx - 4) : 10240 + phy->proc.tti_tx - 4;
	phy->proc.frame_rx = phy->proc.tti_rx / 10;
	phy->proc.sf_rx = phy->proc.tti_rx % 10;

	phy->proc.tti_sched_ul = (phy->proc.tti_tx + 4) % 10240;
	phy->proc.frame_sched_ul = phy->proc.tti_sched_ul / 10;
	phy->proc.sf_sched_ul = phy->proc.tti_sched_ul % 10;

	// Set helper TTIs
	if(phy->proc.abs_tti_tx % 10240 != phy->proc.tti_tx)
	{
		phy->proc.abs_tti_tx = phy->proc.tti_tx;
	}
	else
	{
		phy->proc.abs_tti_tx++;
	}

	if(phy->proc.abs_tti_rx % 10240 != phy->proc.tti_rx)
	{
		phy->proc.abs_tti_rx = phy->proc.tti_rx;
	}
	else
	{
		phy->proc.abs_tti_rx++;
	}

}

//Упаковка и отправка BCH
void phy_common_encode_bch(enb_4g_phy_t *phy, scheduler_dl_config_request_t *dl_cfg_req, uint32_t tti)
{
	if(phy->proc.sf_tx != 0)
	{
		return;
	}

	/* Заполнение BCH производится каждые 40мс (4 фрейма) */
	if((phy->proc.frame_tx & 3) == 0)
	{

		switch(phy->config->fapi_config.rf_config.dl_channel_bandwidth)
		{
			case 6:
				phy->proc.pbch_pdu[0] = (0 << 5);
				break;
			case 15:
				phy->proc.pbch_pdu[0] = (1 << 5);
				break;
			case 25:
				phy->proc.pbch_pdu[0] = (2 << 5);
				break;
			case 50:
				phy->proc.pbch_pdu[0] = (3 << 5);
				break;
			case 100:
				phy->proc.pbch_pdu[0] = (4 << 5);
				break;
			default:
				phy->proc.pbch_pdu[0] = (2 << 5);
				break;
		}

		phy->proc.pbch_pdu[0] |= ((phy->config->fapi_config.phich_config.phich_duration << 4) & 0x10);
		phy->proc.pbch_pdu[0] |= (phy->config->fapi_config.phich_config.phich_resource << 2);
		phy->proc.pbch_pdu[0] |= ((phy->proc.frame_tx >> 8) & 0x3);
		phy->proc.pbch_pdu[1] = phy->proc.frame_tx & 0xfc;
		phy->proc.pbch_pdu[2] = 0;

		fapi_dl_config_request_pdu_t *fapi_dlsch_req = &dl_cfg_req->dl_config_pdu_list[dl_cfg_req->number_pdu];
		fapi_tx_request_pdu_t *fapi_tx_req = &dl_cfg_req->tx_request_pdu_list[dl_cfg_req->number_tx_req];

		fapi_dl_config_bch_pdu *fapi_bch_pdu = &fapi_dlsch_req->bch_pdu;
		fapi_dlsch_req->pdu_type = FAPI_DL_CONFIG_BCH_PDU_TYPE;
		fapi_dlsch_req->pdu_size = sizeof(fapi_dl_config_bch_pdu);

		fapi_bch_pdu->bch_pdu_rel8.pdu_index = phy->proc.tx_pdu_index;
		fapi_bch_pdu->bch_pdu_rel8.length = 3;

		fapi_tx_req->pdu_index = phy->proc.tx_pdu_index;
		fapi_tx_req->pdu_length = 3;
		fapi_tx_req->num_segments = 1;
		fapi_tx_req->tbs[0].tb_length = 3;
		fapi_tx_req->tbs[0].tb_tag = 1;
		fapi_tx_req->tbs[0].tb_data = (void *)&phy->proc.pbch_pdu;

		dl_cfg_req->number_pdu++;
		dl_cfg_req->number_tx_req++;
		phy->proc.tx_pdu_index++;
	}
}

void phy_common_decode_pusch(enb_4g_phy_t *phy, fapi_rx_indication_t* rx_ind, fapi_crc_indication_t *crc_ind)
{
}

void phy_common_decode_pucch(enb_4g_phy_t *phy, fapi_harq_indication_t *harq_ind_msg, fapi_sr_indication_t *sr_ind_msg, fapi_cqi_indication_t *cqi_ind_msg)
{
}

int32_t phy_common_encode_pusch(l2_fapi_generator_stim_item_t *stim, scheduler_ul_config_request_t *ul_cfg_req)
{
	memcpy(&ul_cfg_req->ul_config_list, &stim->ul_cfg_req_list.ul_config_list, stim->ul_cfg_req_list.number_pdu * sizeof(fapi_ul_config_request_pdu_t));
	ul_cfg_req->number_pdu = stim->ul_cfg_req_list.number_pdu;

	return 0;
}

int phy_common_encode_pdcch_dl(l2_fapi_generator_stim_item_t *stim, scheduler_dl_config_request_t *dl_cfg_req)
{
	int32_t i;

	for(i = 0; i<stim->dl_cfg_req_list.number_dci; i++)
	{
		if(stim->dl_cfg_req_list.dl_config_dci_list[i].pdu_type == FAPI_DL_CONFIG_DCI_DL_PDU_TYPE)
		{
			memcpy(&dl_cfg_req->dl_config_dci_list[dl_cfg_req->number_dci], &stim->dl_cfg_req_list.dl_config_dci_list[i], sizeof(fapi_dl_config_request_pdu_t));
			dl_cfg_req->number_dci++;
		}
	}

	return 0;
}

int32_t phy_common_encode_pdsch(l2_fapi_generator_t *l2_fapi_gen, l2_fapi_generator_stim_item_t *stim, scheduler_dl_config_request_t *dl_cfg_req)
{
	int32_t i;

	for(i = 0; i<stim->dl_cfg_req_list.number_pdu; i++)
	{
		if(stim->dl_cfg_req_list.dl_config_pdu_list[i].pdu_type == FAPI_DL_CONFIG_DLSCH_PDU_TYPE)
		{
			memcpy(&dl_cfg_req->dl_config_pdu_list[dl_cfg_req->number_pdu], &stim->dl_cfg_req_list.dl_config_pdu_list[i], sizeof(fapi_dl_config_request_pdu_t));
			memcpy(&dl_cfg_req->tx_request_pdu_list[dl_cfg_req->number_tx_req], &stim->dl_cfg_req_list.tx_request_pdu_list[i], sizeof(fapi_tx_request_pdu_t));

			/* Fix pdu_index for DLSCH PDU and TX.req */
			dl_cfg_req->dl_config_pdu_list[dl_cfg_req->number_pdu].dlsch_pdu.dlsch_pdu_rel8.pdu_index = l2_fapi_gen->phy.proc.tx_pdu_index;
			dl_cfg_req->tx_request_pdu_list[dl_cfg_req->number_tx_req].pdu_index = l2_fapi_gen->phy.proc.tx_pdu_index;

			dl_cfg_req->number_pdu++;
			dl_cfg_req->number_tx_req++;
			l2_fapi_gen->phy.proc.tx_pdu_index++;
		}
	}

	return 0;
}
