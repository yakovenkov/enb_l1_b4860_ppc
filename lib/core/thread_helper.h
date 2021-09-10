#ifndef THREAD_HELPER_H_
#define THREAD_HELPER_H_

#include <time.h>
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <syscall.h>
#include <sched.h>
#include <sched.h>
#include <linux/sched.h>
#include <sys/sysinfo.h>

void set_latency_target(void);
void check_clock(void);

#define THREAD_PRIO_RX 97
#define THREAD_PRIO_RX_DFT 97
#define THREAD_PRIO_TX 96
#define THREAD_PRIO_TX_IDFT 96
#define THREAD_PRIO_FAPI 98
#define THREAD_PRIO_RACH 90
#define THREAD_PRIO_LOG 0
#define THREAD_PRIO_PCAP 0

#define THREAD_PRIO_L2_PHY_DISPATCHER	90
#define THREAD_PRIO_L2_PHY	89
#define THREAD_PRIO_L2_RACH 88

#define THREAD_PRIO_L2_MAC 50
#define THREAD_PRIO_L2_EVENT_DISPATCHER 50



void thread_helper_thread_top_init(char *thread_name,
		     int priority,
		     uint64_t runtime,
		     uint64_t deadline,
		     uint64_t period);

void thread_helper_mutex_init_pi(pthread_mutex_t *mutex);

int is_rt_priority_allowed();

#endif
