/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __P8_H
#define __P8_H

#include "core/block_queue.h"
#include "fapi_b4860_cfg.h"

int32_t p8_start(struct P8_CONFIG *p8_cfg, block_queue_t *p8_q);
void p8_stop();

#endif /* __P8_H */
