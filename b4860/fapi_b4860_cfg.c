/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <core/log.h>
#include <core/file.h>

#include "fapi_b4860_cfg.h"

FAPI_B4860_CONFIG_t *fapi_b4860_parse_config(const char *cfg_filename)
{
	uint8_t *asn_text_buffer = NULL;
	int asn_text_buffer_size = 0;
	asn_dec_rval_t dec_rval = {};
	int32_t i;

	FAPI_B4860_CONFIG_t *b4860_cfg = NULL;

	int num_bytes_readed = read_file(cfg_filename, &asn_text_buffer, &asn_text_buffer_size);

	if(asn_text_buffer != NULL && num_bytes_readed > 0)
	{
		dec_rval = gser_decode(NULL, &asn_DEF_FAPI_B4860_CONFIG, (void **)&b4860_cfg, asn_text_buffer, num_bytes_readed);

		free(asn_text_buffer);

		if(dec_rval.code == RC_OK && b4860_cfg != NULL)
		{
			// Проверка IP-адреса RRU
			struct in_addr ia;
			for(i=0; i<b4860_cfg->rrus.list.count; i++)
			{
				if(!inet_aton(b4860_cfg->rrus.list.array[i]->ip.buf, &ia))
				{
					LOG_E(FAPI, "Invalid RRU %i address: %s\n", i, b4860_cfg->rrus.list.array[i]->ip.buf);
					return NULL;
				}
			}
		}
		else
		{
			LOG_E(FAPI, "Failed to parse L1 config file %s\n", cfg_filename);
			return NULL;
		}
	}
	else
	{
		LOG_E(FAPI, "Failed to read L1 config file %s\n", cfg_filename);
		return NULL;
	}

	return b4860_cfg;
}
