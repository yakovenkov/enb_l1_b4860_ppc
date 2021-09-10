/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef L2_FAPI_GENERATOR_H
#define L2_FAPI_GENERATOR_H

#include <stdbool.h>
#include <stdint.h>

#include <fapi.h>

#define N_MAX_SIB_PDU_LEN 256

typedef struct l2_fapi_generator_s l2_fapi_generator_t;

typedef struct l2_fapi_generator_config_s
{
	const char *l1_name;
	const char *l1_config;
	const char *l2_stim_filename;
	
	fapi_config_t fapi_config;

	bool use_sibs_for_p5;
	bool auto_mib;
	bool auto_sibs;
	int32_t stim_start_tti;
	int32_t stim_duration;
	bool stim_loop;
	bool rru_active;
	int32_t rru_earfcn;

} l2_fapi_generator_config_t;

l2_fapi_generator_t* create_l2_fapi_generator();
bool l2_fapi_generator_init(l2_fapi_generator_t* l2_fapi_generator);
void l2_fapi_generator_start(l2_fapi_generator_t* l2_fapi_generator);
void l2_fapi_generator_stop(l2_fapi_generator_t* l2_fapi_generator);
void l2_fapi_generator_deinit(l2_fapi_generator_t* l2_fapi_generator);

#endif /* L2_FAPI_GENERATOR_H */
