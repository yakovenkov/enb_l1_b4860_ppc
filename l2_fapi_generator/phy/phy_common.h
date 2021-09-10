/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef ENB_NR_PHY_COMMON_H
#define ENB_NR_PHY_COMMON_H

#include <fapi.h>

typedef struct enb_4g_phy_s enb_4g_phy_t;

void phy_common_set_time(enb_4g_phy_t *phy, uint32_t frame, uint32_t subframe);
void phy_common_encode_bch(enb_4g_phy_t *phy, scheduler_dl_config_request_t *dl_cfg_req, uint32_t tti);

int32_t phy_common_encode_pdcch_dl(l2_fapi_generator_stim_item_t *stim, scheduler_dl_config_request_t *dl_cfg_req);
int32_t phy_common_encode_pdsch(l2_fapi_generator_t *l2_fapi_gen, l2_fapi_generator_stim_item_t *stim, scheduler_dl_config_request_t *dl_cfg_req);
void phy_common_decode_pusch(enb_4g_phy_t *phy, fapi_rx_indication_t* rx_ind, fapi_crc_indication_t *crc_ind);
void phy_common_decode_pucch(enb_4g_phy_t *phy, fapi_harq_indication_t *harq_ind_msg, fapi_sr_indication_t *sr_ind_msg, fapi_cqi_indication_t *cqi_ind_msg);
int32_t phy_common_encode_pusch(l2_fapi_generator_stim_item_t *stim, scheduler_ul_config_request_t *ul_cfg_req);


#endif //ENB_NR_PHY_COMMON_H
