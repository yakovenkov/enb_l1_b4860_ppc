/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include "l2_fapi_generator_defs.h"

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

#include "core/log.h"

#include "phy/phy.h"

#include "l2_fapi_generator/l2_fapi_generator.h"

/* Weak stub */
extern bool enodeb_du_l1_init(const char* l1_name, const char *l1_config_file) __attribute__ ((weak));

bool l2_fapi_generator_l1_init(const char* l1_name, const char *l1_config_file)
{
	if(enodeb_du_l1_init)
		return enodeb_du_l1_init(l1_name, l1_config_file);
	return false;
}

l2_fapi_generator_t* create_l2_fapi_generator()
{
	l2_fapi_generator_t* du = calloc(1, sizeof(l2_fapi_generator_t));
	bzero(du, sizeof(l2_fapi_generator_t));

	return du;
}

bool l2_fapi_generator_init(l2_fapi_generator_t* l2_fapi_gen)
{
	assert(l2_fapi_gen != NULL);

	fapi_init();

	if (!l2_fapi_generator_l1_init(l2_fapi_gen->config.l1_name, l2_fapi_gen->config.l1_config))
	{
		LOG_E(L2_FAPI_GEN, "Unsupported L1 %s\n", l2_fapi_gen->config.l1_name);
		return false;
	}

	// Init and start eNB processing
	phy_init(l2_fapi_gen, &l2_fapi_gen->config);

	return true;
}

void l2_fapi_generator_deinit(l2_fapi_generator_t* l2_fapi_generator)
{
	free(l2_fapi_generator);
}

void l2_fapi_generator_start(l2_fapi_generator_t* l2_fapi_generator)
{
	l2_fapi_generator->running = 1;
}

void l2_fapi_generator_stop(l2_fapi_generator_t* l2_fapi_generator)
{
	l2_fapi_generator->running = 0;

	phy_stop(l2_fapi_generator);
}