/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef ENB_NR_PHY_H
#define ENB_NR_PHY_H

#include "phy_common.h"

typedef struct l2_fapi_generator_s l2_fapi_generator_t;
typedef struct l2_fapi_generator_config_s l2_fapi_generator_config_t;

void phy_init(l2_fapi_generator_t *fapi_gen, const l2_fapi_generator_config_t* config);
void phy_stop(l2_fapi_generator_t *l2_fapi_gen);

#endif //ENB_NR_PHY_H
