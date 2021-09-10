#include <assert.h>
#include <linux/sched.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <unistd.h>

#include "core/log.h"
#include "core/thread_helper.h"

static int latency_target_fd = -1;
static int32_t latency_target_value = 0;

#define CPU_AFFINITY

/* Latency trick - taken from cyclictest.c
 * if the file /dev/cpu_dma_latency exists,
 * open it and write a zero into it. This will tell
 * the power management system not to transition to
 * a high cstate (in fact, the system acts like idle=poll)
 * When the fd to /dev/cpu_dma_latency is closed, the behavior
 * goes back to the system default.
 *
 * Documentation/power/pm_qos_interface.txt
 */
void thread_helper_set_latency_target(void)
{
	struct stat s;
	int ret;

	if(stat("/dev/cpu_dma_latency", &s) == 0)
	{
		latency_target_fd = open("/dev/cpu_dma_latency", O_RDWR);

		if(latency_target_fd == -1)
		{
			return;
		}

		ret = write(latency_target_fd, &latency_target_value, 4);

		if(ret == 0)
		{
			LOG_E(CORE, "Error setting cpu_dma_latency to %d!: %s\n", latency_target_value, strerror(errno));
			close(latency_target_fd);
			return;
		}

		LOG_I(CORE, "/dev/cpu_dma_latency set to %dus\n", latency_target_value);
	}
}

__attribute__((visibility("default"))) void thread_helper_thread_top_init(char *thread_name, int priority,
                                                                          uint64_t runtime, uint64_t deadline,
                                                                          uint64_t period)
{

	int policy, s, j;
	struct sched_param sparam;
	char cpu_affinity[1024];
	cpu_set_t cpuset;

	/* Set affinity mask to include CPUs 1 to MAX_CPUS */
	/* CPU 0 is reserved for UHD threads */
	/* CPU 1 is reserved for all RX_TX threads */
	/* Enable CPU Affinity only if number of CPUs >2 */
	CPU_ZERO(&cpuset);

#ifdef CPU_AFFINITY
	static int cpu_aff = 1;

	if(get_nprocs() > 2)
	{
		if(priority == 0)
			CPU_SET(0, &cpuset);
		else
		{
			CPU_SET(cpu_aff, &cpuset);
			cpu_aff = (cpu_aff + 1) % get_nprocs();
		}
		s = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
		if(s != 0)
		{
			LOG_E(CORE, "pthread_setaffinity_np: %s\n", strerror(errno));
		}
	}
#endif // CPU_AFFINITY

	pthread_setname_np(pthread_self(), thread_name);

	/* Check the actual affinity mask assigned to the thread */
	s = pthread_getaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
	if(s != 0)
	{
		LOG_E(CORE, "pthread_getaffinity_np: %s\n", strerror(errno));
		// LOG_E(CORE, "Error getting processor affinity: %s\n", strerror(errno));
	}
	memset(cpu_affinity, 0, sizeof(cpu_affinity));
	for(j = 0; j < 1024; j++)
		if(CPU_ISSET(j, &cpuset))
		{
			char temp[1024];
			sprintf(temp, " CPU_%d", j);
			strcat(cpu_affinity, temp);
		}

	if(priority > 0)
	{
		memset(&sparam, 0, sizeof(sparam));
		sparam.sched_priority = priority; // sched_get_priority_max(SCHED_FIFO);
		policy = SCHED_FIFO;

		s = pthread_setschedparam(pthread_self(), policy, &sparam);
		if(s != 0)
		{
			LOG_W(HW, "Set thread parameters failed : %s\n", strerror(s));
		}

		s = pthread_getschedparam(pthread_self(), &policy, &sparam);
		if(s != 0)
		{
			LOG_E(CORE, "pthread_getschedparam: %s\n", strerror(errno));
		}

		LOG_I(CORE, "Thread %s started on CPU %d, sched_policy = %s , priority = %d\n", thread_name, sched_getcpu(),
		      (policy == SCHED_FIFO)    ? "SCHED_FIFO"
		      : (policy == SCHED_RR)    ? "SCHED_RR"
		      : (policy == SCHED_OTHER) ? "SCHED_OTHER"
		                                : "???",
		      sparam.sched_priority);
	}
}

__attribute__((visibility("default"))) void thread_helper_mutex_init_pi(pthread_mutex_t *mutex)
{
	pthread_mutexattr_t attr;

	if(!mutex)
	{
		return;
	}

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

	pthread_mutex_init(mutex, &attr);
}

__attribute__((visibility("default"))) void mutex_init_pi(pthread_mutex_t *mutex)
{
	pthread_mutexattr_t attr;

	if(!mutex)
	{
		return;
	}

	pthread_mutexattr_init(&attr);
	pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
	pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

	pthread_mutex_init(mutex, &attr);
}

__attribute__((visibility("default"))) int is_rt_priority_allowed()
{
	int rv = 0;
	int s = 0;

	int old_policy = 0;
	struct sched_param old_sparam = {0};

	int desired_policy = SCHED_FIFO;
	struct sched_param desired_sparam = {0};
	int max_priority = sched_get_priority_max(desired_policy);

	s = pthread_getschedparam(pthread_self(), &old_policy, &old_sparam);
	if(s != 0)
	{
		return 0;
	}

	desired_sparam.sched_priority = max_priority;

	s = pthread_setschedparam(pthread_self(), desired_policy, &desired_sparam);
	if(s != 0)
	{
		rv = 0;
	}
	else
	{
		rv = 1;
	}

	s = pthread_setschedparam(pthread_self(), old_policy, &old_sparam);
	if(s != 0)
	{
		return 0;
	}

	return rv;
}