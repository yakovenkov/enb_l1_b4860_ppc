/* 
 * Copyright (c) 2013-2021 Valentin Yakovenkov
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <lib/core/block_queue.h>
#include <lib/core/log.h>
#include <lib/core/thread_helper.h>

#include "fapi_interface.h"

#include "libb4860/ipc/include/fsl_bsc913x_ipc.h"
#include "libb4860/ipc/include/fsl_ipc_errorcodes.h"
#include "libb4860/ipc/include/fsl_usmmgr.h"

#include "fapi.h"
#include "fapi_scheduler.h"
#include "fapi_b4860.h"
#include "fapi_b4860_cfg.h"
#include "fapi_b4860_cpri_eth.h"

/*
#define USE_3MHZ
#define USE_2T2R
*/

static int32_t initialized;
static char devname[16];
static bool running;
static int32_t tap_fd;

static int32_t cfg_fd;
static uint32_t bbu_ip;
static uint32_t rru_ip;
static uint32_t netmask;
static int32_t rru_need_reset;

static int32_t tx_pipe_write, tx_pipe_read;
static pthread_t cpri_rru_manager_h;

#define MAX_RRU_CLIENTS 10
#define RRU_INACTIVITY_TIMEOUT_SECS 10
static int32_t control_fd;
static rru_t rrus[MAX_RRU_CLIENTS];

#define POLL_FD_CFG (MAX_RRU_CLIENTS + 0)
#define POLL_FD_CONTROL (MAX_RRU_CLIENTS + 1)
#define POLL_FD_TX_READ (MAX_RRU_CLIENTS + 2)
#define POLL_FD_TAP (MAX_RRU_CLIENTS + 3)
#define MAX_POLL_FDS (MAX_RRU_CLIENTS + 4)

#define THREAD_PRIO_CPRI_ETH 60

static int cpri_eth_tun_alloc(char *dev, int flags);

static int32_t cpri_rru_manager_init(char *ifname);
static rru_t *cpri_rru_manager_find_rru_by_mac(ir_cfg_req_t *cfg_req);
static rru_t *cpri_rru_manager_add_rru_by_mac(ir_cfg_req_t *cfg_req);
static rru_t *cpri_rru_manager_find_rru_by_ip(uint32_t ip);
static void cpri_rru_manager_rrus_reconfigure();
static void cpri_rru_manager_start();
static void cpri_rru_manager_stop();
static void *cpri_rru_manager_run_thread(void *);

static void cpri_rru_manager_send_bbu_heartbeat(rru_t *rru);
static void cpri_rru_manager_send_channel_establishment_config(rru_t *rru);
static void cpri_rru_manager_send_rru_reset_req(rru_t *rru);
static void cpri_rru_manager_send_parameter_configuration(rru_t *rru);
static void cpri_rru_manager_send_cell_clear_fdd(rru_t *rru);
static void cpri_rru_manager_send_cell_configuration_fdd(rru_t *rru);
static void cpri_rru_manager_send_cell_clear_tdd(rru_t *rru);
static void cpri_rru_manager_send_cell_configuration_tdd(rru_t *rru);
static void cpri_rru_manager_send_delay_measure(rru_t *rru);
static void cpri_rru_manager_send_delay_configuration(rru_t *rru);
static void cpri_rru_manager_process_rru_message(rru_t *rru, uint8_t *buf, int32_t len);
static int32_t cpri_rru_manager_connected();

extern FAPI_B4860_CONFIG_t *g_fapi_b4860_cfg;
extern fapi_config_t g_fapi_cfg_req;

void cpri_eth_init()
{
	int32_t pipefd[2];

	bzero(devname, sizeof(devname));

	strcpy(devname, "tap10");

	tap_fd = cpri_eth_tun_alloc(devname, IFF_TAP | IFF_NO_PI);

	if(tap_fd > 0)
	{
		initialized = 1;

		if(pipe2(pipefd, O_DIRECT) != 0)
		{
			LOG_E(HW, "Error creating CPRI Ethernet TX pipe!\n");
			return;
		}

		tx_pipe_read = pipefd[0];
		tx_pipe_write = pipefd[1];

		cpri_rru_manager_init(devname);
	}
}

void cpri_eth_start()
{
	cpri_rru_manager_start();

	running = 1;

	LOG_I(HW, "Using CPRI ethernet interface %s\n", devname);
}

void cpri_eth_stop()
{
	cpri_rru_manager_stop();
}

void cpri_eth_process_cpri_msg(fapi_ipc_msg_t *msg)
{
	// write(tx_pipe_write, msg, sizeof(msg));
	int32_t len = msg->length - sizeof(fapi_ipc_msg_t);
	if(msg->body_addr != 0 && len > 0)
	{
		write(tap_fd, (void *)msg->body_addr, len);
	}
}

static int cpri_eth_tun_alloc(char *dev, int flags)
{

	struct ifreq ifr;
	int fd, err;
	char *clonedev = "/dev/net/tun";

	/* Arguments taken by the function:
	 *
	 * char *dev: the name of an interface (or '\0'). MUST have enough
	 *   space to hold the interface name if '\0' is passed
	 * int flags: interface flags (eg, IFF_TUN etc.)
	 */

	/* open the clone device */
	if((fd = open(clonedev, O_RDWR)) < 0)
	{
		return fd;
	}

	/* preparation of the struct ifr, of type "struct ifreq" */
	memset(&ifr, 0, sizeof(ifr));

	ifr.ifr_flags = flags; /* IFF_TUN or IFF_TAP, plus maybe IFF_NO_PI */

	if(*dev)
	{
		/* if a device name was specified, put it in the structure; otherwise,
		 * the kernel will try to allocate the "next" device of the
		 * specified type */
		strncpy(ifr.ifr_name, dev, IFNAMSIZ);
	}

	/* try to create the device */
	if((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0)
	{
		close(fd);
		return err;
	}

	/* if the operation was successful, write back the name of the
	 * interface to the variable "dev", so the caller can know
	 * it. Note that the caller MUST reserve space in *dev (see calling
	 * code below) */
	strcpy(dev, ifr.ifr_name);

	/* this is the special file descriptor that the caller will use to talk
	 * with the virtual interface */
	return fd;
}

static int32_t cpri_eth_connected()
{
	return cpri_rru_manager_connected();
}

static void cpri_eth_rrus_reconfigure()
{
	cpri_rru_manager_rrus_reconfigure();
}

static int32_t cpri_rx_thr_send_to_cpri(void *buf, int32_t buflen)
{
	return fapi_b4860_fapi_cpri_eth_send(buf, buflen);
}

static int32_t cpri_rru_manager_init(char *iface)
{
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);

	bbu_ip = htonl(0xc0a81f01);
	rru_ip = htonl(0xc0a81f41);
	netmask = htonl(0xffffff00);

	int enable = 1;

	// сокет для приёма
	cfg_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(cfg_fd < 0)
	{
		return -1;
	}

#if defined(SO_REUSEADDR)
	if(setsockopt(cfg_fd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0)
	{
	}
#endif
#if defined(SO_REUSEPORT)
	if(setsockopt(cfg_fd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0)
	{
	}
#endif

#if defined(SO_BROADCAST)
	if(setsockopt(cfg_fd, SOL_SOCKET, SO_BROADCAST, &enable, sizeof(int)) < 0)
	{
	}
#endif

	if(setsockopt(cfg_fd, SOL_SOCKET, SO_BINDTODEVICE, iface, strlen(iface) + 1) < 0)
	{
		return -1;
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	address.sin_port = htons(33333);

	if(bind(cfg_fd, (struct sockaddr *)&address, addrlen))
	{
		return -1;
	}

	// RRU control socket tcp/30000
	if((control_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
	{
		return -1;
	}

	if(setsockopt(control_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt)))
	{
		return -1;
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(30000);

	if(bind(control_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		return -1;
	}
	if(listen(control_fd, 10) < 0)
	{
		return -1;
	}

	return 0;
}

static void cpri_rru_manager_start()
{
	running = 1;

	pthread_create(&cpri_rru_manager_h, NULL, cpri_rru_manager_run_thread, NULL);
	// thread::start(CPRI_THREAD_PRIO);
}

static void cpri_rru_manager_stop()
{
	close(cfg_fd);
	close(control_fd);
	close(tx_pipe_read);
	close(tx_pipe_write);

	if(running)
	{
		running = 0;
		pthread_cancel(cpri_rru_manager_h);
		pthread_join(cpri_rru_manager_h, NULL);
	}
}

static void cpri_rru_manager_rrus_reconfigure()
{
	int32_t i;
	for(i = 0; i < MAX_RRU_CLIENTS; i++)
	{
		if(rrus[i].state == RRU_STATE_READY)
		{
			rrus[i].state = RRU_STATE_NEED_PARAMS_CONFIG;
			rrus[i].timer = 0;
		}
	}
}

static rru_t *cpri_rru_manager_find_rru_by_mac(ir_cfg_req_t *cfg_req)
{
	int32_t i;

	for(i = 0; i < MAX_RRU_CLIENTS; i++)
	{
		if(rrus[i].mac[0] == cfg_req->rru_mac[0] && rrus[i].mac[1] == cfg_req->rru_mac[1] &&
		   rrus[i].mac[2] == cfg_req->rru_mac[2] && rrus[i].mac[3] == cfg_req->rru_mac[3] &&
		   rrus[i].mac[4] == cfg_req->rru_mac[4] && rrus[i].mac[5] == cfg_req->rru_mac[5])
		{
			return &rrus[i];
		}
	}

	return NULL;
}

static rru_t *cpri_rru_manager_find_rru_by_fd(uint32_t fd)
{
	int32_t i;

	for(i = 0; i < MAX_RRU_CLIENTS; i++)
	{
		if(rrus[i].fd == fd)
		{
			return &rrus[i];
		}
	}

	return NULL;
}

static rru_t *cpri_rru_manager_find_rru_by_ip(uint32_t ip)
{
	int32_t i;

	for(i = 0; i < MAX_RRU_CLIENTS; i++)
	{
		if(rrus[i].ip == ip)
		{
			return &rrus[i];
		}
	}

	return NULL;
}

static rru_t *cpri_rru_manager_add_rru_by_mac(ir_cfg_req_t *cfg_req)
{
	int32_t i;

	for(i = 0; i < MAX_RRU_CLIENTS; i++)
	{
		// if position is empty
		if(rrus[i].state == RRU_STATE_NONE)
		{
			rrus[i].mac[0] = cfg_req->rru_mac[0];
			rrus[i].mac[1] = cfg_req->rru_mac[1];
			rrus[i].mac[2] = cfg_req->rru_mac[2];
			rrus[i].mac[3] = cfg_req->rru_mac[3];
			rrus[i].mac[4] = cfg_req->rru_mac[4];
			rrus[i].mac[5] = cfg_req->rru_mac[5];
			rrus[i].timeout = 0;
			rrus[i].need_reset = 1;

			return &rrus[i];
		}
	}

	return NULL;
}

static void *cpri_rru_manager_run_thread(void *param)
{
	struct sockaddr_in rruaddr;
	socklen_t len;
	uint8_t bbu_id = 0;
	struct sockaddr_in address;
	int addrlen = sizeof(address);
	int new_socket, activity, i, fd;
	uint8_t recv_buf[4096];
	uint32_t rru_count = 0;
	struct pollfd pollfds[MAX_POLL_FDS];
	uint8_t pkt_buf[8192];

	LOG_I(FAPI, "Starting B4860 CPRI RRU manager thread\n");
	thread_helper_thread_top_init("l2_cpri_mgr", THREAD_PRIO_CPRI_ETH, 0, 0, 0);

	// initialise all client_socket[] to 0 so not checked
	for(i = 0; i < MAX_RRU_CLIENTS; i++)
	{
		memset(&rrus[i], 0, sizeof(rru_t));
	}

	for(i = 0; i < MAX_POLL_FDS; i++)
	{
		pollfds[i].fd = -1;
		pollfds[i].events = 0;
	}

	rru_need_reset = 1;

	while(running)
	{
		int32_t recv_len, send_len;
		uint8_t rx_buf[128], tx_buf[128];

		// Wait for RRU configuration request
		memset(&rruaddr, 0, sizeof(rruaddr));
		len = sizeof(rruaddr);

		pollfds[POLL_FD_CONTROL].fd = control_fd;
		pollfds[POLL_FD_CONTROL].events = POLLIN;

		pollfds[POLL_FD_CFG].fd = cfg_fd;
		pollfds[POLL_FD_CFG].events = POLLIN;

		pollfds[POLL_FD_TAP].fd = tap_fd;
		pollfds[POLL_FD_TAP].events = POLLIN;

		// add RRU sockets to set
		for(i = 0; i < MAX_RRU_CLIENTS; i++)
		{
			if(rrus[i].state == RRU_STATE_NONE)
			{
				pollfds[i].fd = -1;
				continue;
			}

			// socket descriptor
			fd = rrus[i].fd;

			// if valid socket descriptor then add to read list
			if(fd > 0)
			{
				pollfds[i].fd = fd;
				pollfds[i].events = POLLIN;
			}
		}

		/* Ожидание событий чтения на сокетах 10 секунд */
		activity = poll(pollfds, MAX_POLL_FDS, 1000);

		if((activity < 0) && (errno != EINTR))
		{
			LOG_E(HW, "CPRI manager: fd poll error");
			// TODO: what to do???
			continue;
		}

#if 1
		/* Обработка состояния PARAMS_CONFIG RRU */
		if(activity == 0)
		{
			// Send BBU hearbeat to active RRUs
			for(i = 0; i < MAX_RRU_CLIENTS; i++)
			{
				if(rrus[i].state == RRU_STATE_NEED_PARAMS_CONFIG)
				{
					if(rrus[i].timer >= 10)
					{
						cpri_rru_manager_send_parameter_configuration(&rrus[i]);
						rrus[i].state = RRU_STATE_WAIT;
					}
					else
					{
						rrus[i].timer++;
					}
				}
			}
		}
#endif

		if(activity == 0)
		{
			// Timeout, check active RRUs and close inactive ones
			// add RRU socket to array of sockets
			for(i = 0; i < MAX_RRU_CLIENTS; i++)
			{
				// if position is empty
				if(rrus[i].state != RRU_STATE_NONE)
				{
					if(rrus[i].timeout++ > RRU_INACTIVITY_TIMEOUT_SECS)
					{
						fd = rrus[i].fd;

						// RRU disconnected , get his details and print
						getpeername(fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
						LOG_I(HW, "RRU disconnected by timeout, ip %s, port %d \n", inet_ntoa(address.sin_addr),
							  ntohs(address.sin_port));
						// Close the socket and mark as 0 in list for reuse
						close(fd);

						rrus[i].state = RRU_STATE_NONE;
						rrus[i].timeout = 0;

						rru_need_reset = 1;
					}
				}
			}

			// There was no activity, so loop again
			continue;
		}

		// Обработка cfg_fd
		if(pollfds[POLL_FD_CFG].revents & POLLIN)
		{
			recv_len = recvfrom(cfg_fd, rx_buf, sizeof(rx_buf), 0, (struct sockaddr *)&rruaddr, &len);

			if(recv_len == -1)
			{
			}
			else
			{
				ir_cfg_req_t *cfg_req = (ir_cfg_req_t *)rx_buf;
				ir_cfg_resp_t *cfg_resp = (ir_cfg_resp_t *)tx_buf;
				int32_t bbu_port = cfg_req->bbu_port;

				memset(tx_buf, 0, sizeof(tx_buf));

				rru_t *rru = cpri_rru_manager_find_rru_by_mac(cfg_req);
				if(rru == NULL)
				{
					// New RRU
					rru = cpri_rru_manager_add_rru_by_mac(cfg_req);
				}

				rru->state = RRU_STATE_INIT;
				rru->timeout = 0;

				rru->ip = 0;

				if(bbu_port < g_fapi_b4860_cfg->rrus.list.count)
				{
					struct in_addr ia;
					if(inet_aton(g_fapi_b4860_cfg->rrus.list.array[cfg_req->bbu_port]->ip.buf, &ia))
					{
						rru->ip = ia.s_addr;
						rru->cfg = g_fapi_b4860_cfg->rrus.list.array[cfg_req->bbu_port];
					}
				}
				else
				{
					LOG_E(HW, "Invalid BBU port received from RRU: %i\n", bbu_port);
				}
				
				if(rru->ip == 0)
				{
					// Базовый адрес 192.168.30.65
					rru->ip = 0xc0a81e41 + (rru_count & 0xff);

					// Проверка на переполнение адресов IP при прибавлении счетчика RRU к базовому адресу
					rru_count++;

					if(rru_count > 0xfe - 0x41)
						rru_count = 0;
				}

				struct in_addr ia;
				ia.s_addr = rru->ip;

				LOG_I(HW, "Adding new RRU in chain, ip %s\n", inet_ntoa(ia));

				cfg_resp->bbu_port = cfg_req->bbu_port;
				cfg_resp->rru_id = cfg_req->rru_id;
				cfg_resp->bbu_id = bbu_id;
				bbu_id += 0x10;
				cfg_resp->rru_mac[0] = cfg_req->rru_mac[0];
				cfg_resp->rru_mac[1] = cfg_req->rru_mac[1];
				cfg_resp->rru_mac[2] = cfg_req->rru_mac[2];
				cfg_resp->rru_mac[3] = cfg_req->rru_mac[3];
				cfg_resp->rru_mac[4] = cfg_req->rru_mac[4];
				cfg_resp->rru_mac[5] = cfg_req->rru_mac[5];
				cfg_resp->bbu_ip = bbu_ip;
				cfg_resp->rru_ip = rru_ip;
				cfg_resp->netmask = netmask;

				cfg_resp->bbu_ip = htonl(0xc0a81e01);
				cfg_resp->rru_ip = rru->ip; // htonl(0xc0a81e41);
				cfg_resp->netmask = htonl(0xffffff00);

				// Configuration response sent to port 33334
				rruaddr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
				rruaddr.sin_port = htons(33334);

				// Prepare RRU configuration answer
				send_len = sendto(cfg_fd, (const char *)tx_buf, sizeof(ir_cfg_resp_t), 0,
								  (const struct sockaddr *)&rruaddr, len);
			}
		}

		// Прием соединения на control_fd
		if(pollfds[POLL_FD_CONTROL].revents & POLLIN)
		{
			if((new_socket = accept(control_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
			{
				// pLOG_E(HW, "accept");
				// exit(EXIT_FAILURE);
			}

			LOG_I(HW, "New RRU, fd %d, ip %s, port %d \n", new_socket, inet_ntoa(address.sin_addr),
				  ntohs(address.sin_port));

			rru_t *rru = cpri_rru_manager_find_rru_by_ip(address.sin_addr.s_addr);

			if(rru != NULL)
			{

				// add RRU socket to array of sockets
				// if position is empty
				if(rru->state == RRU_STATE_INIT)
				{
					rru->fd = new_socket;
					rru->state = RRU_STATE_CONNECTED;
					rru->timeout = 0;
					// printf("Adding to list of sockets as %d\n" , i);
					// break;
				}
			}
		}

		/* Обработка TAP */
		if(pollfds[POLL_FD_TAP].revents & POLLIN)
		{
			int32_t n_read = read(tap_fd, pkt_buf, sizeof(pkt_buf));

			if(n_read < 0)
				break;

			cpri_rx_thr_send_to_cpri(pkt_buf, n_read);
		}

		// else its some IO operation on some other socket
		for(i = 0; i < MAX_RRU_CLIENTS; i++)
		{
			if(rrus[i].state == RRU_STATE_NONE)
				continue;

			fd = rrus[i].fd;

			if(pollfds[i].revents & POLLIN)
			{
				// Check if it was for closing , and also read the
				// incoming message
				if((recv_len = read(fd, recv_buf, 4096)) == 0)
				{
					// RRU disconnected , get his details and print
					getpeername(fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);

					LOG_I(HW, "RRU disconnected, ip %s, port %d \n", inet_ntoa(address.sin_addr),
						  ntohs(address.sin_port));

					rrus[i].state = RRU_STATE_NONE;

					// Close the socket and mark as 0 in list for reuse
					close(fd);

					// memset(&rrus[i], 0, sizeof(rru_t));
					/*
					rrus[i].fd = 0;
					rrus[i].state = RRU_STATE_NONE;
					rrus[i].timeout = 0;
					*/
				}
				else
				{
					// Process RRU message
					cpri_rru_manager_process_rru_message(&rrus[i], recv_buf, recv_len);

					// Reset timeout
					rrus[i].timeout = 0;
					/*
										if(rrus[i].state == RRU_STATE_NEED_PARAMS_CONFIG)
										{
											send_parameter_configuration(&rrus[i]);
											rrus[i].state = RRU_STATE_WAIT;
										}
					*/
				}
			}
		}
	}

	return NULL;
}

static void cpri_rru_manager_process_channel_establishment_req(rru_t *rru, uint8_t *buf, int32_t len)
{
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	ir_ie_header_t *ie_hdr;
	int32_t off;

	off = sizeof(ir_msg_header_t);

	rru->id = hdr->rru_id;
	rru->bbu_id = hdr->bbu_id;
	rru->port = hdr->port;

	// First IE is after msg header
	while(off < len)
	{
		ie_hdr = (ir_ie_header_t *)(buf + off);
		// TODO: implement length checks

		off += ie_hdr->ie_len;

		switch(ie_hdr->ie_no)
		{
			case 1: {
				ir_ie_1_t *ie_1 = (ir_ie_1_t *)ie_hdr;
				if(ie_1->ie_len != 100)
				{
					LOG_E(HW, "invalid IE 1 length %i (should be 100)", ie_1->ie_len);
				}
				else
				{
					memcpy(&rru->ie_1, ie_1, sizeof(ir_ie_1_t));

					LOG_I(HW, "RRU manufacturer: %s\n", rru->ie_1.rru_manufacturer);
					LOG_I(HW, "RRU vendor: %s\n", rru->ie_1.rru_vendor);
					LOG_I(HW, "RRU serial: %s\n", rru->ie_1.serial);
					LOG_I(HW, "RRU production date: %s\n", rru->ie_1.date_prod);

					if(rru->ie_1.date_service[0] != 0x00)
						LOG_I(HW, "RRU last service date: %s\n", rru->ie_1.date_service);
				}
			}
			break;
			case 2:
				break;
			case 3:
				break;
		}
	}

	// Send channel configuration
	cpri_rru_manager_send_channel_establishment_config(rru);
}

static void cpri_rru_manager_send_channel_establishment_config(rru_t *rru)
{
	uint8_t buf[2048];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	int32_t off = 0;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = 2;

	off += sizeof(ir_msg_header_t);

	ir_ie_11_t *ie_11 = (ir_ie_11_t *)(buf + off);

	ie_11->ie_no = 11;
	ie_11->ie_len = sizeof(ir_ie_11_t);

	ie_11->hour = tm.tm_hour;
	ie_11->minute = tm.tm_min;
	ie_11->second = tm.tm_sec;

	ie_11->day = tm.tm_mday;
	ie_11->month = tm.tm_mon + 1;
	ie_11->year = tm.tm_year + 1900;

	off += sizeof(ir_ie_11_t);

	ir_ie_12_t *ie_12 = (ir_ie_12_t *)(buf + off);
	ie_12->ie_no = 12;
	ie_12->ie_len = sizeof(ir_ie_12_t);
	ie_12->bbu_ftp_addr[0] = (bbu_ip & 0xff000000) >> 24;
	ie_12->bbu_ftp_addr[1] = (bbu_ip & 0x00ff0000) >> 16;
	ie_12->bbu_ftp_addr[2] = (bbu_ip & 0x0000ff00) >> 8;
	ie_12->bbu_ftp_addr[3] = (bbu_ip & 0x000000ff) >> 0;

	off += sizeof(ir_ie_12_t);

	ir_ie_13_t *ie_13 = (ir_ie_13_t *)(buf + off);
	ie_13->ie_no = 13;
	ie_13->ie_len = sizeof(ir_ie_13_t);
	ie_13->status = 0; // RRU normal operation

	off += sizeof(ir_ie_13_t);

	// Software version check result OK
	ir_ie_14_t *ie_14 = (ir_ie_14_t *)(buf + off);
	ie_14->ie_no = 14;
	ie_14->ie_len = sizeof(ir_ie_14_t);
	ie_14->software_type = 0;
	ie_14->result = 0;

	off += sizeof(ir_ie_14_t);
#if 0
	// Firmware version check result OK
	ie_14 = (ir_ie_14_t *)(buf + off);
	ie_14->ie_no = 14;
	ie_14->ie_len = sizeof(ir_ie_14_t);
	ie_14->software_type = 1;
	ie_14->result = 0;

	off += sizeof(ir_ie_14_t);
#endif
	// Ir port working mode = 1 (normal mode)
	ir_ie_504_t *ie_15 = (ir_ie_504_t *)(buf + off);
	ie_15->ie_no = 504;
	ie_15->ie_len = sizeof(ir_ie_504_t);
	ie_15->mode = 1;

	off += sizeof(ir_ie_504_t);

	hdr->msg_len = off;

	if(send(rru->fd, buf, off, 0) != off)
	{
		LOG_E(HW, "Error sending channel configuration!\n");
	}

	rru->state = RRU_STATE_CONN_SETUP_SENT;
	rru->timeout = 0;
}

static void cpri_rru_manager_process_channel_establishment_resp(rru_t *rru, uint8_t *buf, int32_t len)
{
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	ir_ie_21_t *ie_21;
	int32_t off;

	off = sizeof(ir_msg_header_t);
	ie_21 = (ir_ie_21_t *)(buf + off);

	if(ie_21->ie_no != 21)
	{
		LOG_E(HW, "Invalid IE %i in channel configuration response!\n", ie_21->ie_no);
		return;
	}

	LOG_I(HW, "Channel establishment response %i\n", ie_21->result);
	rru->state = RRU_STATE_CONN_SETUP_ACK;
	rru->timeout = 0;

	if(rru->need_reset)
	{
		cpri_rru_manager_send_rru_reset_req(rru);
		rru->need_reset = 0;
	}
	else
	{
		cpri_rru_manager_send_delay_measure(rru);
	}
}

static void cpri_rru_manager_process_rru_init_calibration_report(rru_t *rru, uint8_t *buf, int32_t len)
{
	// Send CELL clear message
#ifdef ENABLE_CPRI_ETH_FDD
	cpri_rru_manager_send_cell_clear_fdd(rru);
#else
	cpri_rru_manager_send_cell_clear_tdd(rru);
#endif
}

static void cpri_rru_manager_process_delay_measure_resp(rru_t *rru, uint8_t *buf, int32_t len)
{
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	ir_ie_911_t *ie_911;
	int32_t off;

	off = sizeof(ir_msg_header_t);
	ie_911 = (ir_ie_911_t *)(buf + off);

	if(ie_911->ie_no != 911)
	{
		LOG_E(HW, "Invalid IE %i in channel configuration response!\n", ie_911->ie_no);
		return;
	}

	LOG_I(HW, "t_offset=%i tb_delay_dl=%i tb_delay_ul=%i t2a=%i ta3=%i N=%i\n", ie_911->t_offset, ie_911->tb_delay_dl,
		  ie_911->tb_delay_ul, ie_911->t2a, ie_911->ta3, ie_911->N);

	rru->state = RRU_STATE_CONN_SETUP_ACK;
	rru->timeout = 0;

	cpri_rru_manager_send_delay_configuration(rru);
}

static void cpri_rru_manager_process_delay_configuration_resp(rru_t *rru, uint8_t *buf, int32_t len)
{
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	ir_ie_931_t *ie_931;
	int32_t off;

	off = sizeof(ir_msg_header_t);
	ie_931 = (ir_ie_931_t *)(buf + off);

	if(ie_931->ie_no != 931)
	{
		LOG_E(HW, "Invalid IE %i in delay configuration response!\n", ie_931->ie_no);
		return;
	}

	LOG_I(HW, "Delay configuration for port %i result %i\n", ie_931->port, ie_931->result);

	// rru->state = RRU_STATE_READY;
	rru->timeout = 0;

	cpri_rru_manager_send_parameter_configuration(rru);
}

static void cpri_rru_manager_process_rru_heartbeat(rru_t *rru, uint8_t *buf, int32_t len)
{
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;

	LOG_D(HW, "RRU heartbeat\n");

	cpri_rru_manager_send_bbu_heartbeat(rru); //, hdr->flow);

	rru->timeout = 0;

	// Switch to WAIT state after 3 heartbeats received
	// if(rru->heartbeat_counter++ == 3)
	//{
	//		rru->state = RRU_STATE_NEED_PARAMS_CONFIG;
	//}
}

static void cpri_rru_manager_process_rru_reset_event(rru_t *rru, uint8_t *buf, int32_t len)
{
	// send_rru_reset_req(rru);

	rru->timeout = 0;
}

#if 0
void cpri_rru_manager_send_bbu_heartbeat(rru_t *rru, uint32_t id)
{
	uint8_t buf[128];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	//hdr->flow = rru->serial_out++;
	hdr->flow = id;

	hdr->msg_no = 181;
	hdr->msg_len = sizeof(ir_msg_header_t);

	send(rru->fd, buf, hdr->msg_len, 0);
}
#endif

static void cpri_rru_manager_send_bbu_heartbeat(rru_t *rru)
{
	uint8_t buf[128];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = 181;
	hdr->msg_len = sizeof(ir_msg_header_t);

	send(rru->fd, buf, hdr->msg_len, 0);
}

static void cpri_rru_manager_send_rru_reset_req(rru_t *rru)
{
	uint8_t buf[128];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	int32_t off = 0;

	LOG_I(HW, "Resetting RRU\n");

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = IR_MSG_RRU_RESET_REQ;
	off += sizeof(ir_msg_header_t);

	ir_ie_1301_t *ie_1301 = (ir_ie_1301_t *)(buf + off);
	ie_1301->ie_no = 1301;
	ie_1301->ie_len = sizeof(ir_ie_1301_t);
	ie_1301->reset_type = 0;
	off += sizeof(ir_ie_1301_t);

	hdr->msg_len = off;

	send(rru->fd, buf, hdr->msg_len, 0);
}

static void cpri_rru_manager_send_parameter_configuration(rru_t *rru)
{
	uint8_t buf[2048];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	time_t t = time(NULL);
	int32_t off = 0;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = IR_MSG_RRU_PARAMETERS_CONFIGURATION_REQ;

	off += sizeof(ir_msg_header_t);

#if 1
	ir_ie_501_t *ie_501;
	/*
	// Clear IQ configuration, antenna 1
	ie_501 = (ir_ie_501_t *)(buf + off);
	ie_501->ie_no = 501;
	ie_501->ie_len = sizeof(ir_ie_501_t);
	ie_501->carrier = 1;
	ie_501->antenna = 1;
	ie_501->axc = 254;
	ie_501->fiber = 0;
	off += sizeof(ir_ie_501_t);

	// Clear IQ configuration, antenna 2
	ie_501 = (ir_ie_501_t *)(buf + off);
	ie_501->ie_no = 501;
	ie_501->ie_len = sizeof(ir_ie_501_t);
	ie_501->carrier = 1;
	ie_501->antenna = 2;
	ie_501->axc = 254;
	ie_501->fiber = 0;
	off += sizeof(ir_ie_501_t);
	*/

	// Antenna 1, AxC 0&1
	ie_501 = (ir_ie_501_t *)(buf + off);
	ie_501->ie_no = 501;
	ie_501->ie_len = sizeof(ir_ie_501_t);
	ie_501->carrier = 1;
	ie_501->antenna = 1;
	ie_501->axc = 0;
	ie_501->fiber = 0;
	off += sizeof(ir_ie_501_t);

	if(g_fapi_cfg_req.rf_config.dl_channel_bandwidth == 25)
	{
		ie_501 = (ir_ie_501_t *)(buf + off);
		ie_501->ie_no = 501;
		ie_501->ie_len = sizeof(ir_ie_501_t);
		ie_501->carrier = 1;
		ie_501->antenna = 1;
		ie_501->axc = 1;
		ie_501->fiber = 0;
		off += sizeof(ir_ie_501_t);
	}

	if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 2)
	{
		// Antenna 2
		ie_501 = (ir_ie_501_t *)(buf + off);
		ie_501->ie_no = 501;
		ie_501->ie_len = sizeof(ir_ie_501_t);
		ie_501->carrier = 1;
		ie_501->antenna = 2;
		ie_501->axc = 2;
		ie_501->fiber = 0;
		off += sizeof(ir_ie_501_t);

		if(g_fapi_cfg_req.rf_config.dl_channel_bandwidth == 25)
		{
			ie_501 = (ir_ie_501_t *)(buf + off);
			ie_501->ie_no = 501;
			ie_501->ie_len = sizeof(ir_ie_501_t);
			ie_501->carrier = 1;
			ie_501->antenna = 2;
			ie_501->axc = 3;
			ie_501->fiber = 0;
			off += sizeof(ir_ie_501_t);
		}
	}

#if 0
	// Antenna 3
	ie_501 = (ir_ie_501_t *)(buf + off);
	ie_501->ie_len = sizeof(ir_ie_501_t);
	ie_501->carrier = 3;
	ie_501->antenna = 3;
	ie_501->axc = 2;
	ie_501->fiber = 0;
	off += sizeof(ir_ie_501_t);

	// Antenna 3
	ie_501 = (ir_ie_501_t *)(buf + off);
	ie_501->ie_len = sizeof(ir_ie_501_t);
	ie_501->carrier = 4;
	ie_501->antenna = 4;
	ie_501->axc = 3;
	ie_501->fiber = 0;
	off += sizeof(ir_ie_501_t);
#endif
#endif

	// Antenna group configuration
	ir_ie_507_t *ie_507 = (ir_ie_507_t *)(buf + off);
	ie_507->ie_no = 507;
	ie_507->ie_len = sizeof(ir_ie_507_t);
	ie_507->mode = 1;
	ie_507->group = 1;

	if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 2)
	{
		// 2 antennas
		ie_507->uplink = 0xfc;
		ie_507->downlink = 0xfc;
	}
	else if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 1)
	{
		// 1 antenna
		ie_507->uplink = 0xfe;
		ie_507->downlink = 0xfe;
	}

	off += sizeof(ir_ie_507_t);

	hdr->msg_len = off;

	if(send(rru->fd, buf, off, 0) != off)
	{
		LOG_E(HW, "Error sending channel configuration!\n");
	}

	rru->state = RRU_STATE_WAIT_PARAMS_CONFIG_RESP;
	rru->timeout = 0;
}

static void cpri_rru_manager_process_parameter_configuration_resp(rru_t *rru, uint8_t *buf, int32_t len)
{
	LOG_I(HW, "RRU parameters configuration response\n");

	// Теперь необходимо дождаться сообщения INIT_CALIBRATION_RESULT

	// Send CELL clear message
	// send_cell_clear(rru);
}

static void cpri_rru_manager_process_cell_clear_resp(rru_t *rru, uint8_t *buf, int32_t len)
{
	LOG_I(HW, "RRU cell clear response\n");

	// Send CELL configuration message
#ifdef ENABLE_CPRI_ETH_FDD
	cpri_rru_manager_send_cell_configuration_fdd(rru);
#else
	cpri_rru_manager_send_cell_configuration_tdd(rru);
#endif
}

static void cpri_rru_manager_send_cell_clear_tdd(rru_t *rru)
{
	uint8_t buf[2048];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	time_t t = time(NULL);
	int32_t off = 0;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = IR_MSG_RRU_CELL_CONFIGURATION_REQ;

	off += sizeof(ir_msg_header_t);

	// Cell 1 RX clear
	ir_ie_1501_t *ie_1501 = (ir_ie_1501_t *)(buf + off);
	ie_1501->ie_no = 1501;
	ie_1501->ie_len = sizeof(ir_ie_1501_t);
	ie_1501->reason = 1; // delete
	ie_1501->cell_id = 0x00000001;
	ie_1501->power = 20;
	ie_1501->ant_group = 1;

	/*
	if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 2)
	{
		ie_1501->n_freqs = 2;
	}
	else if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 1)
	{
		ie_1501->n_freqs = 1;
	}
	*/

	ie_1501->n_freqs = 1;
	off += sizeof(ir_ie_1501_t);

	// Cell 1 Frequency clear
	ir_ie_1502_t *ie_1502 = (ir_ie_1502_t *)(buf + off);
	ie_1502->ie_no = 1502;
	ie_1502->ie_len = sizeof(ir_ie_1502_t);
	ie_1502->reason = 1; // delete
	ie_1502->cell_id = 0x00000001;
	ie_1502->carrier = 1;
	ie_1502->center = 23500;
	ie_1502->type = 0;
	ie_1502->bandwidth = 20;
	ie_1502->cp_length = 0;
	off += sizeof(ir_ie_1502_t);

	hdr->msg_len = off;

	if(send(rru->fd, buf, off, 0) != off)
	{
		LOG_E(HW, "Error sending cell clear!\n");
	}

	rru->state = RRU_STATE_WAIT_CELL_CLEAR_RESP;
	rru->timeout = 0;
}

static void cpri_rru_manager_send_cell_configuration_tdd(rru_t *rru)
{
	uint8_t buf[2048];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	time_t t = time(NULL);
	int32_t off = 0;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = IR_MSG_RRU_CELL_CONFIGURATION_REQ;

	off += sizeof(ir_msg_header_t);

	// Cell 1 configuration
	// Cell 1 RX
	ir_ie_1501_t *ie_1501 = (ir_ie_1501_t *)(buf + off);
	ie_1501->ie_no = 1501;
	ie_1501->ie_len = sizeof(ir_ie_1501_t);
	ie_1501->reason = 0; // establish
	ie_1501->cell_id = 0x00000001;
	//ie_1501->power = 20 * 256; // 43*256;
	ie_1501->power = rru->cfg->power_dbm * 256;
	ie_1501->ant_group = 1;
/*
	if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 2)
	{
		ie_1501->n_freqs = 2;
	}
	else if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 1)
	{
		ie_1501->n_freqs = 1;
	}
	*/
	ie_1501->n_freqs = 1;

	off += sizeof(ir_ie_1501_t);

	// Cell 1
	ir_ie_1502_t *ie_1502 = (ir_ie_1502_t *)(buf + off);
	ie_1502->ie_no = 1502;
	ie_1502->ie_len = sizeof(ir_ie_1502_t);
	ie_1502->reason = 2; // delete
	ie_1502->cell_id = 0x00000001;
	ie_1502->carrier = 1;
	ie_1502->center = rru->cfg->freq_dl;
	ie_1502->type = 0;

	switch(g_fapi_cfg_req.rf_config.dl_channel_bandwidth)
	{
		case 15:
			ie_1502->bandwidth = 3;
			break;
			
		case 25:
			ie_1502->bandwidth = 5;
			break;
			
		case 50:
			ie_1502->bandwidth = 10;
			break;

		case 100:
			ie_1502->bandwidth = 20;
			break;
	}
	
	ie_1502->cp_length = 0;
	off += sizeof(ir_ie_1502_t);

	hdr->msg_len = off;

	if(send(rru->fd, buf, off, 0) != off)
	{
		LOG_E(HW, "Error sending channel configuration!\n");
	}

	rru->state = RRU_STATE_WAIT_CELL_CONFIG_RESP;
	rru->timeout = 0;
}

static void cpri_rru_manager_send_cell_clear_fdd(rru_t *rru)
{
	uint8_t buf[2048];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	time_t t = time(NULL);
	int32_t off = 0;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = IR_MSG_RRU_CELL_CONFIGURATION_REQ;

	off += sizeof(ir_msg_header_t);

	// Cell 1 RX clear
	ir_ie_1501_t *ie_1501 = (ir_ie_1501_t *)(buf + off);
	ie_1501->ie_no = 1501;
	ie_1501->ie_len = sizeof(ir_ie_1501_t);
	ie_1501->reason = 2; // delete
	ie_1501->cell_id = 0x00000001;
	ie_1501->power = 20;
	ie_1501->ant_group = 1;

	/*
	if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 2)
	{
		ie_1501->n_freqs = 2;
	}
	else if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 1)
	{
		ie_1501->n_freqs = 1;
	}
	*/

	ie_1501->n_freqs = 1;

	off += sizeof(ir_ie_1501_t);

	// Cell 1 Frequency clear
	ir_ie_1503_t *ie_1503 = (ir_ie_1503_t *)(buf + off);
	ie_1503->ie_no = 1503;
	ie_1503->ie_len = sizeof(ir_ie_1503_t);
	ie_1503->reason = 2; // delete
	ie_1503->cell_id = 0x00000001;
	ie_1503->carrier = 1;
	ie_1503->dl_mid_freq = 4650;
	ie_1503->ul_mid_freq = 4550;
	ie_1503->type = 0;
	ie_1503->dl_bandwidth = 5;
	ie_1503->ul_bandwidth = 5;
	ie_1503->cp_length = 0;
	off += sizeof(ir_ie_1503_t);

#ifdef NOUSE_2T2R
	ie_1503 = (ir_ie_1503_t *)(buf + off);
	ie_1503->ie_no = 1503;
	ie_1503->ie_len = sizeof(ir_ie_1503_t);
	ie_1503->reason = 2; // delete
	ie_1503->cell_id = 0x00000001;
	ie_1503->carrier = 1;
	ie_1503->dl_mid_freq = 4650;
	ie_1503->ul_mid_freq = 4550;
	ie_1503->type = 0;
	ie_1503->dl_bandwidth = 5;
	ie_1503->ul_bandwidth = 5;
	ie_1503->cp_length = 0;
	off += sizeof(ir_ie_1503_t);
#endif
	hdr->msg_len = off;

	if(send(rru->fd, buf, off, 0) != off)
	{
		LOG_E(HW, "Error sending cell clear!\n");
	}

	rru->state = RRU_STATE_WAIT_CELL_CLEAR_RESP;
	rru->timeout = 0;
}

static void cpri_rru_manager_send_cell_configuration_fdd(rru_t *rru)
{
	uint8_t buf[2048];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	time_t t = time(NULL);
	int32_t off = 0;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = IR_MSG_RRU_CELL_CONFIGURATION_REQ;

	off += sizeof(ir_msg_header_t);

	// Cell 1 configuration
	// Cell 1 RX
	ir_ie_1501_t *ie_1501 = (ir_ie_1501_t *)(buf + off);
	ie_1501->ie_no = 1501;
	ie_1501->ie_len = sizeof(ir_ie_1501_t);
	ie_1501->reason = 0; // establish
	ie_1501->cell_id = 0x00000001;
	//ie_1501->power = 20 * 256; // 43*256;
	ie_1501->power = rru->cfg->power_dbm * 256;
	ie_1501->ant_group = 1;
/*
	if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 2)
	{
		ie_1501->n_freqs = 2;
	}
	else if(g_fapi_cfg_req.rf_config.tx_antenna_ports == 1)
	{
		ie_1501->n_freqs = 1;
	}
	*/
	ie_1501->n_freqs = 1;

	off += sizeof(ir_ie_1501_t);

	// Cell 1 Frequency 455
	ir_ie_1503_t *ie_1503 = (ir_ie_1503_t *)(buf + off);
	ie_1503->ie_no = 1503;
	ie_1503->ie_len = sizeof(ir_ie_1503_t);
	ie_1503->reason = 0;
	ie_1503->cell_id = 0x00000001;
	ie_1503->carrier = 1;
	ie_1503->type = 0;

	ie_1503->dl_mid_freq = rru->cfg->freq_dl / 100000;// 4650;
	ie_1503->ul_mid_freq = rru->cfg->freq_ul / 100000; // 4550;

	if(g_fapi_cfg_req.rf_config.dl_channel_bandwidth == 15)
	{
		ie_1503->dl_bandwidth = 3;
		ie_1503->ul_bandwidth = 3;
	}
	else if(g_fapi_cfg_req.rf_config.dl_channel_bandwidth == 25)
	{
		ie_1503->dl_bandwidth = 5;
		ie_1503->ul_bandwidth = 5;
	}

	//ie_1503->dl_bandwidth = 5;
	//ie_1503->ul_bandwidth = 5;
	
	ie_1503->cp_length = 0;
	off += sizeof(ir_ie_1503_t);

#ifdef NOUSE_2T2R
	ie_1503 = (ir_ie_1503_t *)(buf + off);
	ie_1503->ie_no = 1503;
	ie_1503->ie_len = sizeof(ir_ie_1503_t);
	ie_1503->reason = 0;
	ie_1503->cell_id = 0x00000001;
	ie_1503->carrier = 1;
	ie_1503->type = 0;
#ifdef USE_3MHZ
	ie_1503->dl_mid_freq = 4660;
	ie_1503->ul_mid_freq = 4560;

	ie_1503->dl_bandwidth = 3;
	ie_1503->ul_bandwidth = 3;
#else
	ie_1503->dl_mid_freq = 4650;
	ie_1503->ul_mid_freq = 4550;

	ie_1503->dl_bandwidth = 5;
	ie_1503->ul_bandwidth = 5;
#endif
	ie_1503->cp_length = 0;
	off += sizeof(ir_ie_1503_t);
#endif

	hdr->msg_len = off;

	if(send(rru->fd, buf, off, 0) != off)
	{
		LOG_E(HW, "Error sending channel configuration!\n");
	}

	rru->state = RRU_STATE_WAIT_CELL_CONFIG_RESP;
	rru->timeout = 0;
}

static void cpri_rru_manager_send_delay_measure(rru_t *rru)
{
	uint8_t buf[2048];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	time_t t = time(NULL);
	int32_t off = 0;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = IR_MSG_RRU_DELAY_MEASURE_REQ;

	off += sizeof(ir_msg_header_t);

	// Delay measure request
	ir_ie_901_t *ie_901 = (ir_ie_901_t *)(buf + off);
	ie_901->ie_no = 901;
	ie_901->ie_len = sizeof(ir_ie_901_t);
	ie_901->port = 0;
	off += sizeof(ir_ie_901_t);

	hdr->msg_len = off;

	if(send(rru->fd, buf, off, 0) != off)
	{
		LOG_E(HW, "Error sending delay measure request!\n");
	}

	rru->state = RRU_STATE_WAIT_CELL_CONFIG_RESP;
	rru->timeout = 0;
}

static void cpri_rru_manager_send_delay_configuration(rru_t *rru)
{
	uint8_t buf[2048];
	ir_msg_header_t *hdr = (ir_msg_header_t *)buf;
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	int32_t off = 0;

	memset(buf, 0, sizeof(buf));

	hdr->rru_id = rru->id;
	hdr->bbu_id = rru->bbu_id;
	hdr->port = rru->port;
	hdr->flow = rru->serial_out++;

	hdr->msg_no = IR_MSG_RRU_DELAY_CONFIG_REQ;

	off += sizeof(ir_msg_header_t);

	// Delay measure request
	ir_ie_921_t *ie_921 = (ir_ie_921_t *)(buf + off);
	ie_921->ie_no = 921;
	ie_921->ie_len = sizeof(ir_ie_921_t);
	ie_921->port = 0;
	ie_921->t12 = 943 + 2573;
	ie_921->t32 = 943 + 2426;
	ie_921->dl_offset = 256;

	ie_921->dl_cal_rru = 24240;
	ie_921->ul_cal_rru = 40227;

	// ie_921->dl_cal_rru = 0;
	// ie_921->ul_cal_rru = 15987;

	// ie_921->dl_cal_rru = 90907;
	// ie_921->ul_cal_rru = 106894;
	// ie_921->dl_cal_rru = 0;
	// ie_921->ul_cal_rru = 0;

	off += sizeof(ir_ie_921_t);

	hdr->msg_len = off;

	if(send(rru->fd, buf, off, 0) != off)
	{
		LOG_E(HW, "Error sending delay measure request!\n");
	}

	rru->state = RRU_STATE_WAIT_CELL_CONFIG_RESP;
	rru->timeout = 0;
}

static void cpri_rru_manager_process_cell_configuration_resp(rru_t *rru, uint8_t *buf, int32_t len)
{
	LOG_I(HW, "RRU cell configuration response\n");

	rru->state = RRU_STATE_READY;
	rru->timeout = 0;
}

static void cpri_rru_manager_process_rru_message(rru_t *rru, uint8_t *buf, int32_t len)
{
	ir_msg_header_t *hdr;
	ir_ie_header_t *ie_hdr;
	int32_t off;

	if(len < sizeof(ir_msg_header_t))
	{
		// Error message
		return;
	}

	hdr = (ir_msg_header_t *)buf;

	// LOG_I(HW, "Rx RRU msg %i\n", hdr->msg_no);

	switch(hdr->msg_no)
	{
		case 1:
			// Channel establishment request
			cpri_rru_manager_process_channel_establishment_req(rru, buf, len);
			break;

		case 3:
			cpri_rru_manager_process_channel_establishment_resp(rru, buf, len);
			// Channel configuration response
			break;

		case IR_MSG_RRU_PARAMETERS_CONFIGURATION_RESP:
			cpri_rru_manager_process_parameter_configuration_resp(rru, buf, len);
			break;

		case IR_MSG_RRU_CELL_CONFIGURATION_RESP:
			if(rru->state == RRU_STATE_WAIT_CELL_CLEAR_RESP)
			{
				cpri_rru_manager_process_cell_clear_resp(rru, buf, len);
			}
			else
			{
				cpri_rru_manager_process_cell_configuration_resp(rru, buf, len);
			}
			break;

		case IR_MSG_RRU_DELAY_MEASURE_RESP:
			cpri_rru_manager_process_delay_measure_resp(rru, buf, len);
			break;

		case IR_MSG_RRU_DELAY_CONFIG_RESP:
			cpri_rru_manager_process_delay_configuration_resp(rru, buf, len);
			break;

		case 171:
			cpri_rru_manager_process_rru_heartbeat(rru, buf, len);
			// RRU heartbeat
			break;

		case IR_MSG_RRU_ALARM_REPORT:
			cpri_rru_manager_process_rru_reset_event(rru, buf, len);
			break;

		case IR_MSG_RRU_INIT_CALIBRATION_REPORT:
		case IR_MSG_RRU_RESET_EVENT:
			if(rru->state != RRU_STATE_READY)
			{
				cpri_rru_manager_process_rru_init_calibration_report(rru, buf, len);
			}
			break;

		default:
			LOG_I(HW, "RRU msg_no %i not known\n", hdr->msg_no);
	}
}

static int32_t cpri_rru_manager_connected()
{
	int32_t i;

	for(i = 0; i < MAX_RRU_CLIENTS; i++)
	{
		if(rrus[i].state == RRU_STATE_READY)
			break;
	}

	return (i < MAX_RRU_CLIENTS);
}
