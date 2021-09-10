/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>

#include "core/block_queue.h"
#include "core/log.h"
#include "fapi_interface.h"
#include <core/thread_helper.h>

#include "p8.h"

#include "fapi_b4860_cfg.h"

static bool p8_running;
static pthread_t p8_thread_h;
static void *p8_thread(void *param);
static block_queue_t *g_p8_q;
static struct P8_CONFIG *g_p8_cfg;

int32_t p8_start(struct P8_CONFIG *p8_cfg, block_queue_t *p8_q)
{
	if(p8_q == NULL || p8_cfg == NULL)
		return 0;

	if(!p8_cfg->enable)
		return 0;

	p8_running = true;
	g_p8_cfg = p8_cfg;
	g_p8_q = p8_q;

	pthread_create(&p8_thread_h, NULL, p8_thread, NULL);

	return 0;
}

void p8_stop()
{
	if(p8_running)
	{
		p8_running = false;
		block_queue_break(g_p8_q);
		pthread_join(p8_thread_h, NULL);
	}
}

void *p8_thread(void *param)
{
	char p8_filename_template[PATH_MAX + 1];
	char p8_date[PATH_MAX + 1];
	char p8_filename[PATH_MAX + 1];
	char p8_dir[PATH_MAX + 1];
	int32_t msg_no = 0;
	int32_t i;

	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);
	timeinfo = localtime(&rawtime);

	strftime(p8_date, PATH_MAX, "%Y%m%d-%H%M%S", timeinfo);
	snprintf(p8_dir, PATH_MAX, "%s/%s", g_p8_cfg->dump_path.buf, p8_date);

	if(mkdir(p8_dir, 0755) != 0)
	{
		LOG_E(FAPI, "Error creating P8 dir\n");
		return NULL;
	}

	snprintf(p8_filename_template, PATH_MAX, "%s/%%06i_%%04i_%%02i_%%02i_%%08x.raw", p8_dir);

	thread_helper_thread_top_init("p8_handler", THREAD_PRIO_LOG, 0, 0, 0);

	while(p8_running)
	{
		fapi_p8_indication_t *p8_ind = block_queue_pop(g_p8_q);

		if(p8_ind == NULL)
			continue;

		if(!g_p8_cfg->dump_path.buf)
			continue;

		if(g_p8_cfg->limit > 0 && msg_no >= g_p8_cfg->limit)
			continue;

		for(i = 0; i < p8_ind->number_of_pdus; i++)
		{
			fapi_p8_ind_pdu_t *pdu = &p8_ind->pdus[i];
			if((g_p8_cfg->dump_ul &&
			    (pdu->type & FAPI_P8_DUMP_FLAG_UL || pdu->type & FAPI_P8_DUMP_FLAG_UL_ON_BAD_CRC)) ||
			   (g_p8_cfg->dump_ul_bad_crc && (pdu->type & FAPI_P8_DUMP_FLAG_UL_ON_BAD_CRC)) ||
			   (g_p8_cfg->dump_ul_pusch_ack && (pdu->type & FAPI_P8_DUMP_FLAG_UL_PUSCH_ACK)) ||
			   (g_p8_cfg->dump_ul_pusch_nack && (pdu->type & FAPI_P8_DUMP_FLAG_UL_PUSCH_NACK)) ||
			   (g_p8_cfg->dump_ul_pusch_cqi && (pdu->type & FAPI_P8_DUMP_FLAG_UL_PUSCH_CQI)) ||
			   (g_p8_cfg->dump_ul_pucch && (pdu->type & FAPI_P8_DUMP_FLAG_UL_PUCCH)))
			{
				uint8_t *pdu_data = &p8_ind->pdu_data[0] + pdu->offset;

				snprintf(p8_filename, PATH_MAX, p8_filename_template, msg_no, p8_ind->frame, p8_ind->subframe, i,
				         pdu->type);

				int fd = open(p8_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
				if(fd > 0)
				{
					write(fd, pdu_data, pdu->length);
					close(fd);
				}

				msg_no++;
			}
		}
	}

	return NULL;
}
