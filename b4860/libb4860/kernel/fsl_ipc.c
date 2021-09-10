/*
 *  @ fsl_bsc913x_ipc_kern_mod.c
 *
 * Copyright (c) 2011-2013, Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Freescale Semiconductor Inc nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      Author: Manish Jaggi <manish.jaggi@freescale.com>
 *      Author: Ashish Kumar <ashish.kumar@freescale.com>
 */
#include "fsl_ipc_types.h"
#include "fsl_het_mgr.h"
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
//#include <asm/fsl_msg.h>
#include "fsl_heterogeneous.h"
#include "fsl_heterogeneous_ipc.h"
#include "fsl_ipc_kmod.h"
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/sched.h>
#include <linux/fcntl.h>
#include <linux/list.h>
#include <linux/wait-simple.h>
#include <asm/atomic.h>
//#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mpic.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>

#define MAX_MSG_IRQ	4
#define FSL_NUM_MPIC_MSGS 4
struct fsl_msg_unit {
	unsigned int irq;
	unsigned int msg_num;

	struct fsl_msg *fsl_msg;
	bool requested;
	u32 msg_group_addr_offset;

	u32 __iomem *msg_addr;
	u32 __iomem *mer;
	u32 __iomem *msr;
};
/*
Data Structure to store the callback handlers for channels
*/
typedef struct {
	int	used;
	int 		ch_id;
	struct fsl_msg_unit *ipc_msg_intr;
	struct	pid	*pid;
	struct	siginfo siginfo;
} user_irq_t;

/* IRQ - MSG related */
//int msg_irq_cnt=0;
spinlock_t	uirq_lock;
user_irq_t	uirq[MAX_MSG_IRQ];
//user_irq_t user_irq_arr[MAX_SC_PA_CHANNELS];


int fslipc_devno;
dev_t fslipc_dev;
struct cdev fslipc_cdev;
uint32_t het_ipc_major;
uint32_t het_ipc_minor;

/*IPC */
os_het_ipc_channel_t *ch;
os_het_ipc_t *ipc;

/* From bootargs */
int num_channels, channel_depth;
static char *max_num_ipc_channels;
static char *max_channel_depth;

#define FIFO_SIZE	128
uint32_t msg_channels_mask;
atomic_t msg_ready_mask;
struct swait_head msg_wait_q;

#ifdef CONFIG_MULTI_RAT
int get_hetmgr_ipc_addr(mem_range_t *, int inst_id);
#else
int get_hetmgr_ipc_addr(mem_range_t *);
#endif
int get_hetmgr_mem(mem_range_t *);

void open_channel_zero(os_het_ipc_channel_t *);
extern struct fsl_msg_unit *fsl_get_msg_unit(void);
extern uint32_t get_hetmgr_rat_instances(void);

struct fsl_msg_unit *fsl_get_msg_unit(void);
void fsl_release_msg_unit(struct fsl_msg_unit *msg);
void fsl_clear_msg(struct fsl_msg_unit *msg);
void fsl_enable_msg(struct fsl_msg_unit *msg);
void fsl_disable_msg(struct fsl_msg_unit *msg);
//void fsl_msg_route_int_to_irqout(struct fsl_msg_unit *msg);
//void fsl_send_msg(struct fsl_msg_unit *msg, u32 message);
void fsl_read_msg(struct fsl_msg_unit *msg, u32 *message);


struct msg_addr {
	u32 addr;
	u8 res[12];
};

struct fsl_msg_regs {
	struct msg_addr unit[FSL_NUM_MPIC_MSGS];
	u8 	res0[192];
	u32	mer;
	u8	res1[12];
	u32	msr;
};

struct fsl_msg {
	struct fsl_msg_regs __iomem *regs;
	struct fsl_msg_unit messages[FSL_NUM_MPIC_MSGS];
	spinlock_t lock;
	struct list_head list_node;
};

static LIST_HEAD(fsl_msgs);

struct fsl_msg_unit *fsl_get_msg_unit(void)
{
	struct fsl_msg *fsl_msg;
	unsigned long flags;
	int i;

	list_for_each_entry(fsl_msg, &fsl_msgs, list_node) {
		spin_lock_irqsave(&fsl_msg->lock, flags);

		for (i = 0; i < ARRAY_SIZE(fsl_msg->messages); i++) {
			if (!fsl_msg->messages[i].requested) {
				fsl_msg->messages[i].requested = true;
				spin_unlock_irqrestore(&fsl_msg->lock, flags);
				return &fsl_msg->messages[i];
			}
		}

		spin_unlock_irqrestore(&fsl_msg->lock, flags);
	}

	if (fsl_msg)
		return ERR_PTR(-EBUSY);

	return ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL(fsl_get_msg_unit);

void fsl_release_msg_unit(struct fsl_msg_unit *msg)
{
	struct fsl_msg *fsl_msg = msg->fsl_msg;
	unsigned long flags;

	spin_lock_irqsave(&fsl_msg->lock, flags);
	msg->requested = false;
	spin_unlock_irqrestore(&fsl_msg->lock, flags);
}
EXPORT_SYMBOL(fsl_release_msg_unit);

void fsl_clear_msg(struct fsl_msg_unit *msg)
{
	u32 tmp32;

	/* clear the interrupt by reading the message */
	fsl_read_msg(msg, &tmp32);
}
EXPORT_SYMBOL(fsl_clear_msg);

void fsl_enable_msg(struct fsl_msg_unit *msg)
{
	u32 tmp32;

	/* Set the mer bit */
	tmp32 = in_be32(msg->mer);
	out_be32(msg->mer, tmp32 | (1 << msg->msg_num));
}
EXPORT_SYMBOL(fsl_enable_msg);

void fsl_disable_msg(struct fsl_msg_unit *msg)
{
	u32 tmp32;

	/* Set the mer bit */
	tmp32 = in_be32(msg->mer);
	out_be32(msg->mer, tmp32 & ~(1 << msg->msg_num));
}
EXPORT_SYMBOL(fsl_disable_msg);
/*
 * Sometimes, we need to set the EIDR[EP] bit for the message interrupt
 * to route it to IRQ_OUT, Most of the times, if the interrupt was
 * routed out. there's no chance to unmask it. so we'll unmask it here.
 */
#if 0
void fsl_msg_route_int_to_irqout(struct fsl_msg_unit *msg)
{
	mpic_unmask_irq(msg->irq);
	mpic_irq_set_ep(msg->irq, 1);
}
EXPORT_SYMBOL(fsl_msg_route_int_to_irqout);
#endif

#if 0
void fsl_send_msg(struct fsl_msg_unit *msg, u32 message)
{
	out_be32(msg->msg_addr, message);
}
EXPORT_SYMBOL(fsl_send_msg);
#endif

void fsl_read_msg(struct fsl_msg_unit *msg, u32 *message)
{
	*message = in_be32(msg->msg_addr);
}
EXPORT_SYMBOL(fsl_read_msg);

#if 1
static int fsl_init_msg(void)
{
	struct device_node *np;
	struct resource rsrc;

	for_each_compatible_node(np, NULL, "fsl,mpic-msg") {
		int i;
		struct fsl_msg *fsl_msg;

		pr_err("fsl,mpic-msg: found\n");
		fsl_msg = kzalloc(sizeof(*fsl_msg), GFP_KERNEL);
		if (!fsl_msg) {
			pr_err("%s: unable to allocate memory\n",
				np->full_name);
			continue;
		}

		of_address_to_resource(np, 0, &rsrc);
		fsl_msg->regs = ioremap(rsrc.start, rsrc.end - rsrc.start);
		if (!fsl_msg->regs) {
			pr_err("%s: unable to iomap registers\n",
			       np->full_name);
			goto err;
		}

		for (i = 0; i < ARRAY_SIZE(fsl_msg->messages); i++) {
			int ret;
			struct resource irq;

			ret = of_irq_to_resource(np, i, &irq);
			if (ret == NO_IRQ) {
				pr_err("%s: not enough interrupts specified\n",
				       np->full_name);
				goto err;
			}
			fsl_msg->messages[i].msg_group_addr_offset =
				rsrc.start & 0xfffff;
			fsl_msg->messages[i].irq = irq.start;
			fsl_msg->messages[i].fsl_msg = fsl_msg;
			fsl_msg->messages[i].msg_num = i;
			fsl_msg->messages[i].mer = &fsl_msg->regs->mer;
			fsl_msg->messages[i].msr = &fsl_msg->regs->msr;
			fsl_msg->messages[i].requested = false;
			fsl_msg->messages[i].msg_addr =
					&fsl_msg->regs->unit[i].addr;
			pr_err("fsl,mpic-msg: added %msg no %i\n", i);
		}
		list_add(&fsl_msg->list_node, &fsl_msgs);

		/* We don't want to lose the node and its ->data */
		np->data = fsl_msg;
		of_node_get(np);

		continue;
err:
		kfree(fsl_msg);
	}
	return 0;
}
//arch_initcall(fsl_init_msg);
#endif

static long get_max_num_ipc_channels(void)
{
	unsigned long val;
	if (!max_num_ipc_channels || (kstrtoul(max_num_ipc_channels,
				0, &val) < 0))
		return -EINVAL;

	return val;
}

static long get_max_channel_depth(void)
{
	unsigned long val;

	if (!max_channel_depth || (kstrtoul(max_channel_depth,
				0, &val) < 0))
		return -EINVAL;

	return val;
}

static int __init get_max_num_ipc_channels_from_cmdline(char *str)
{
	if (!str)
		return 0;
	pr_err("%s\n", str);
	max_num_ipc_channels = str;

	return 1;
}

static int __init get_max_channel_depth_from_cmdline(char *str)
{
	if (!str)
		return 0;

	pr_err("%s\n", str);
	max_channel_depth = str;

	return 1;
}

__setup("max_num_ipc_channels=", get_max_num_ipc_channels_from_cmdline);
__setup("max_channel_depth=", get_max_channel_depth_from_cmdline);

/*******************************
 * Interrupt Handlers
 ******************************/
static irqreturn_t msg_intr(int irq, void *dev_id)
{
	int i;
	int old_mask, new_mask;
	struct task_struct *dst_tsk;
#ifdef DEBUG_READ_TS
	struct timespec ts;
#endif

	for (i = 0; i < MAX_MSG_IRQ; i++) 
	{
		if (uirq[i].ipc_msg_intr->irq == irq) 
		{
			fsl_clear_msg(uirq[i].ipc_msg_intr);
			if(uirq[i].ch_id > 0 && uirq[i].ch_id < 32)
			{
				do
				{
					//pr_info("Recvd %i\n", i);
					old_mask = atomic_read(&msg_ready_mask);
					new_mask = old_mask | (1 << uirq[i].ch_id);
				} while(atomic_cmpxchg(&msg_ready_mask, old_mask, new_mask) != old_mask);

#ifdef DEBUG_READ_TX
				getnstimeofday(&ts);
				pr_info("Recvd %i at %i.%i\n", new_mask,ts.tv_sec, ts.tv_nsec);
#endif
				if(uirq[i].siginfo.si_signo != 0)
				{
					/* push the signal */
					spin_lock(&uirq_lock);
					rcu_read_lock();
					dst_tsk = pid_task(uirq[i].pid, PIDTYPE_PID);
					rcu_read_unlock();
					spin_unlock(&uirq_lock);

					if (dst_tsk)
					{
						uirq[i].siginfo.si_int = atomic_xchg(&msg_ready_mask, 0);

						send_sig_info(uirq[i].siginfo.si_signo,
							&uirq[i].siginfo, dst_tsk);

						preempt_schedule();

//						pr_info("Sent sig %i val %i at %i.%i\n", uirq[i].siginfo.si_signo,
//								uirq[i].siginfo.si_int, ts.tv_sec, ts.tv_nsec);



					}
				}
				else
				{
					swait_wake_all(&msg_wait_q);
				}
			}
		}
	}


	return IRQ_HANDLED;
}

int register_msg_intr(void)
{
	int err, i;
	struct fsl_msg_unit *msg;

	for (i = 0; i < MAX_MSG_IRQ; i++) {
		msg = fsl_get_msg_unit();
		if (IS_ERR(msg)) {
			printk(KERN_ERR"Error in fsl_get_msg_uint\n");
			return (int)msg;
		}
		pr_info("msg irq %x msg num %x\n", msg->irq,
			msg->msg_num);
		fsl_enable_msg(msg);
		uirq[i].ipc_msg_intr = msg;
		err = request_irq(msg->irq, msg_intr, IRQF_NO_SOFTIRQ_CALL,
			"IPC_MSG", NULL);
		pr_info("request_irq err = %x\n", err);
	}
	return 0;
}

/*
 * @fsl_913xipc_init
 *
 *This method is called by het_ipc to initialize ipc ptr channels
 *It is assumed that the caller (het_ipc) has mapped ipc structre
 *already at this address
*/
#ifndef CONFIG_MULTI_RAT
int fsl_913xipc_init(void)
{
	int ret = 0;
	int i = 0;
	mem_range_t r, r1;
	phys_addr_t phys_addr, phys_addr1;

	ret = get_hetmgr_ipc_addr(&r);
	if (ret)
		goto end;
	ipc = r.vaddr;
	pr_err("os_het_ipc_t phys=%llx vaddr=%p \n", r.phys_addr,
			r.vaddr);
	num_channels = get_max_num_ipc_channels();
	if (num_channels < 0 || num_channels > 64) {
		num_channels = MAX_IPC_CHANNELS;
		printk(KERN_ERR"warning: max_num_ipc_channels not set properly,\
		setting default value = %d\n", num_channels);
	}

	channel_depth = get_max_channel_depth();
	if (channel_depth < 0) {
		channel_depth = DEFAULT_CHANNEL_DEPTH;
		printk(KERN_ERR"warning: max_channel_depth not set\
		setting default value = %d\n", channel_depth);
	}

	r1.size = sizeof(os_het_ipc_channel_t)*num_channels +
		/* array to hold channel pointers */
		sizeof(os_het_ipc_bd_t)*num_channels*channel_depth;
		/* ptr channel ring buffer */

	ret = get_hetmgr_mem(&r1);
	if (ret)
		goto end;

	memcpy(&ipc->ipc_channels, &r1.phys_addr, sizeof(phys_addr_t));
	ipc->num_ipc_channels = num_channels;
	ipc->ipc_max_bd_size = channel_depth;
	phys_addr = r1.phys_addr;
	ch = (os_het_ipc_channel_t *)(r1.vaddr);
	phys_addr1 = phys_addr + sizeof(os_het_ipc_channel_t)*num_channels;

//	mutex_init(&ipc_priv->lock);
	init_swait_head(&msg_wait_q);
	

//        INIT_LIST_HEAD(&rf_dev->event_handler_list);
//        spin_lock_init(&rf_dev->event_handler_lock);
	/*
		In a loop of num_channels, set the ptr of channel structures
		in ipc->channels
	*/

	for (i = 0; i < num_channels; i++) {
		ch[i].ipc_ind = OS_HET_NO_INT;
		ch[i].id = i;
		memcpy(&ch[i].bd_base, &phys_addr1, sizeof(phys_addr_t));
		phys_addr1 += sizeof(os_het_ipc_bd_t)*channel_depth;
	}
	ret = register_msg_intr();
	if (ret)
		goto end;
	open_channel_zero(ch);
end:
	return ret;
}
#else
int fsl_913xipc_init(void)
{
	int ret = 0;
	int i = 0;
	int j = 0;
	uint32_t rat_inst = 0;
	mem_range_t r, r1;
	phys_addr_t phys_addr, phys_addr1;

	num_channels = get_max_num_ipc_channels();
	if (num_channels < 0 || num_channels > 64) {
		num_channels = MAX_IPC_CHANNELS;
		printk(KERN_ERR"warning: max_num_ipc_channels not set properly,\
		setting default value = %d\n", num_channels);
	}

	channel_depth = get_max_channel_depth();
	if (channel_depth < 0) {
		channel_depth = DEFAULT_CHANNEL_DEPTH;
		printk(KERN_ERR"warning: max_channel_depth not set\
		setting default value = %d\n", channel_depth);
	}

	rat_inst = get_hetmgr_rat_instances();
	if (rat_inst <= 1) {
		rat_inst = 2;
		printk(KERN_ERR"warning: max_rat_inst not set\
		setting default value = %d\n", rat_inst);
	}



	for (j = 0; j < rat_inst; j++) {

		ret = get_hetmgr_ipc_addr(&r, j);
		if (ret)
			goto end;

		ipc = r.vaddr;

		r1.size = sizeof(os_het_ipc_channel_t)*num_channels +
				/* array to hold channel pointers */
				sizeof(os_het_ipc_bd_t)*
				num_channels*
				channel_depth;

		/* ptr channel ring buffer */
		ret = get_hetmgr_mem(&r1);
		if (ret)
			goto end;
		memcpy(&ipc->ipc_channels, &r1.phys_addr,
				sizeof(phys_addr_t));
		ipc->num_ipc_channels = num_channels;
		ipc->ipc_max_bd_size = channel_depth;

		phys_addr = r1.phys_addr;
		ch = (os_het_ipc_channel_t *)(r1.vaddr);
		phys_addr1 = phys_addr +
				sizeof(os_het_ipc_channel_t)*num_channels;

		/*
			In a loop of num_channels, set the ptr of channel
			structures
			in ipc->channels
		*/

		for (i = 0; i < num_channels; i++) {
			ch[i].ipc_ind = OS_HET_NO_INT;
			ch[i].id = i;
			memcpy(&ch[i].bd_base, &phys_addr1,
				sizeof(phys_addr_t));
			phys_addr1 += sizeof(os_het_ipc_bd_t)*channel_depth;
		}

		open_channel_zero(ch);
	}
end:
	return ret;
}
#endif

void open_channel_zero(os_het_ipc_channel_t *ch)
{
	ch[0].consumer_initialized = OS_HET_INITIALIZED;
	ch[0].id = 0;
	ch[0].bd_ring_size = channel_depth;
	ch[0].ch_type = OS_HET_IPC_POINTER_CH;
}


#if 0
int het_ipc_ioctl(struct inode *inode, struct file *filp,
		unsigned int cmd, unsigned long arg)
#endif
#ifdef HAVE_UNLOCKED_IOCTL
static long het_ipc_ioctl(struct file *filp, unsigned int cmd,
		                unsigned long arg)
#else
static int het_ipc_ioctl(struct inode *inode, struct file *filp,
		                unsigned int cmd, unsigned long arg)
#endif
{
	unsigned long flags;
	int ret = 0;
	ipc_rc_t r;
	int i;

	pr_info("IPC IOCTL cmd %x (%x) arg %llx\n", cmd, IOCTL_IPC_REGISTER_SIGNAL, arg);

	switch (cmd) {

	case IOCTL_IPC_REGISTER_SIGNAL:
		copy_from_user(&r, (void *)arg, sizeof(ipc_rc_t));
//		if (msg_irq_cnt < MAX_MSG_IRQ) {

			spin_lock_irqsave(&uirq_lock, flags);
			for(i=0; i<MAX_MSG_IRQ; i++)
			{
				if(uirq[i].used == 0)
					break;
			}

			if(i < MAX_MSG_IRQ)
			{
				if(r.signal != 0)
				{
					uirq[i].pid = task_pid(current);
					uirq[i].siginfo.si_signo = r.signal;
					uirq[i].siginfo.si_errno = 0;
					uirq[i].siginfo.si_code = SI_QUEUE;
				}

				uirq[i].used = 1;
				uirq[i].ch_id = r.channel_id;

				/* update channel structure with msg */
				ch[r.channel_id].ipc_ind = OS_HET_PA_MPIC;
				ch[r.channel_id].ind_offset = 0x1400 + (i * 16);
				ch[r.channel_id].ind_value = 0x12345678;
//				msg_irq_cnt++;
			}
			spin_unlock_irqrestore(&uirq_lock, flags);
//		}
		break;

		default:  /* redundant, as cmd was checked against MAXNR */
			return -ENOTTY;
	}

	return ret;
}

static int het_ipc_open(struct inode *inode, struct file *filep)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&uirq_lock, flags);
	for(i=0; i < MAX_MSG_IRQ; i++)
	{
		uirq[i].used = 0;
		uirq[i].ch_id = -1;
	}
	spin_unlock_irqrestore(&uirq_lock, flags);

	atomic_set(&msg_ready_mask, 0);

	return 0;
}

static int het_ipc_release(struct inode *inode, struct file *filep)
{
	int i;
	unsigned long flags;

	spin_lock_irqsave(&uirq_lock, flags);
	for(i=0; i < MAX_MSG_IRQ; i++)
	{
		uirq[i].used = 0;
		uirq[i].ch_id = -1;
		uirq[i].siginfo.si_signo = 0;
	}
	spin_unlock_irqrestore(&uirq_lock, flags);

	return 0;
}


static int atomic_check_new_msgs(atomic_t *v, int newval, int *oldval)
{
	*oldval = atomic_xchg(v, newval);
	return *oldval != 0;
}

static ssize_t het_ipc_read(struct file *filep, char __user *buf, size_t size,
		loff_t *offset)
{
	int ready_mask = 0;
	DEFINE_SWAITER(wait);
	int rc;

#if 0
	if(swait_event_timeout(msg_wait_q, atomic_check_new_msgs(&msg_ready_mask, 0, &ready_mask), 2) == 0)
		return -EAGAIN;
#else
	swait_prepare(&msg_wait_q, &wait, TASK_INTERRUPTIBLE);

	/*Now wait here, tti notificaion will wake us up*/
	schedule();

	swait_finish(&msg_wait_q, &wait);
	atomic_check_new_msgs(&msg_ready_mask, 0, &ready_mask);
#endif

	rc = put_user(ready_mask, (int *)buf);
	if (!rc)
		rc = sizeof(ready_mask);

	return rc;
}

/*
file operations data structure
*/
static const struct file_operations het_ipc_fops = {
	.owner	= THIS_MODULE,
	.open = 	het_ipc_open,
	.read = het_ipc_read,
	.release =	het_ipc_release,
#ifdef HAVE_UNLOCKED_IOCTL
        .unlocked_ioctl = het_ipc_ioctl,
#else
        .ioctl = het_ipc_ioctl,
#endif

#ifdef CONFIG_COMPAT
        .compat_ioctl = het_ipc_ioctl,
#endif	
};


int ipc_driver_init(void)
{
	int ret;
	int i;
	het_ipc_major = 0;
	het_ipc_minor = 0;
	fslipc_dev = 0;
	/*register /dev/het_ipc character driver */
	if (het_ipc_major) {
		fslipc_dev = MKDEV(het_ipc_major, het_ipc_minor);
		ret = register_chrdev_region(fslipc_dev, 1, "/dev/het_ipc");

	} else {
		ret = alloc_chrdev_region(&fslipc_dev, het_ipc_minor, 1,
				"/dev/het_ipc");
		het_ipc_major = MAJOR(fslipc_dev);
	}

	if (ret < 0) {
		pr_err("het_ipc_dev: can't get major %d\n", het_ipc_major);
		return ret;
	}

	fslipc_devno = MKDEV(het_ipc_major, het_ipc_minor);
	pr_info("Het Ipc %d %d\n", het_ipc_major, het_ipc_minor);
	cdev_init(&fslipc_cdev, &het_ipc_fops);
	fslipc_cdev.owner = THIS_MODULE;
	fslipc_cdev.ops = &het_ipc_fops;
	ret = cdev_add(&fslipc_cdev, fslipc_devno, 1);

	/* Fail gracefully if need be */
	if (ret)
		pr_err("Error %d adding Heterogeneous System Manager", ret);

	for(i=0; i<MAX_MSG_IRQ; i++)
	{
		uirq[i].used = 0;
	}

	fsl_init_msg();
	fsl_913xipc_init();

	return 0;
}

void ipc_driver_exit(void)
{
	cdev_del(&fslipc_cdev);
	unregister_chrdev_region(fslipc_dev, 1);
}

MODULE_AUTHOR("manish.jaggi@freescale.com");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("IPC kernel mode helper driver");
module_init(ipc_driver_init);
module_exit(ipc_driver_exit);
