/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef FAPI_B4860_CFG_H
#define FAPI_B4860_CFG_H

#include <stdbool.h>
#include <stddef.h>

#include <FAPI-B4860-CONFIG.h>

FAPI_B4860_CONFIG_t *fapi_b4860_parse_config(const char *cfg_filename);

#endif /* ENODEB_DU_APP_CONFIG_H */
