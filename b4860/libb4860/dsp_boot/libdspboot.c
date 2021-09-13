/*
 * @file: libdspboot.c
 *
 * Copyright 2013 Freescale Semiconductor, Inc.
 *
 * Author: Ashish Kumar <ashish.kumar@freescale.com>
 */

#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdarg.h>

#include <stdint.h>
#include <time.h>
#include <errno.h>
#include "fsl_het_mgr.h"
#include "fsl_ipc_types.h"
#include "fsl_ipc_shm.h"
#include "dsp_boot.h"
#include "dsp_compact.h"
#include "fsl_heterogeneous_l1_defense.h"
#include "fsl_heterogeneous.h"

/* defines */
#define MAX_ENTRIES	10
/* PA CCSR */
#define	DSPSR	0xE00D8
#define DSPSR_DSP1_READY 0x80000000
#define DSPSR_DSP2_READY 0x40000000

/* DSP CCSR */
#define DSP_GCR	0x18000

#ifdef B4860
#define B4860QDS_MODEL_STR	"fsl,B4860QDS"
#define BSTAR_VAL 0x8000000C
#define DDRC_TRG_ID(val) (BSTAR_VAL | (val << 20))
static int DCFG_BRR_mask;
static int GIC_VIGR_VALUE;
static int GIC_VIGR_VALUE_ARR[6] = {0x302, 0x303, 0x304, 0x305, 0x306, 0x307};
#define SH_CTRL_VADDR_DSPBT(A, B) \
		(void *)((unsigned long)(A) \
		- ((dsp_bt_t *)(B))->sh_ctrl_area.phys_addr \
		+ ((dsp_bt_t *)(B))->sh_ctrl_area.vaddr)

#endif
#define PASTATE	0x104
#define DSPSTATE 0x100
#define BOOT_JMP_ADDR	0x108
#define PASTATE_PAREADY	0x1
#define RESET	0x0

#define MB_256		0x10000000
#define IPC_SHMEM	MB_256

#if PRODUCTION_REL
#define D_DSP0_BASE	(0x120000/4)
#define D_DSP1_BASE	(0x128000/4)
#define D_DSP2_BASE	(0x130000/4)
#define D_DSP3_BASE	(0x138000/4)
#define D_DSP4_BASE	(0x140000/4)
#define D_DSP5_BASE	(0x148000/4)

#define DTU_DMCSR 	(0x4080/4)
#define DMCSR_DBG_DISABLE 0x0FFFFFFF
static int DTU_DMCSR_ADDR[6] = {
D_DSP0_BASE + DTU_DMCSR,
D_DSP1_BASE + DTU_DMCSR,
D_DSP2_BASE + DTU_DMCSR,
D_DSP3_BASE + DTU_DMCSR,
D_DSP4_BASE + DTU_DMCSR,
D_DSP5_BASE + DTU_DMCSR
};
#endif

static int init_hetmgr();
static int init_devmem();
static int assign_memory_areas(void *);
static int init_hugetlb(void *);
static int dsp_ready_check(void *);
static int load_dsp_image(char *, void *);
static int set_sh_ctrl_pa_init(int);
static int set_ppc_ready(void *);
static int reset_ppc_ready(void *);
static int check_dsp_boot(void *);
static void cleanup(int, int);

void func_add2map(void *dsp_bt, uint64_t phys, uint32_t sz)
{

	int i = ((dsp_bt_t *)dsp_bt)->map_id;

	((dsp_bt_t *)dsp_bt)->map_d[i].phys_addr = phys;
	((dsp_bt_t *)dsp_bt)->map_d[i].vaddr = NULL;
	((dsp_bt_t *)dsp_bt)->map_d[i].size = sz;

	((dsp_bt_t *)dsp_bt)->map_id += 1;

	return;
}

void dump_sys_map(sys_map_t het_sys_map)
{
	printf("SYSTEM MAP\n");
	printf("DSP PrivArea: Addr=%llx Size=%x\n",
		het_sys_map.smart_dsp_os_priv_area.phys_addr,
		het_sys_map.smart_dsp_os_priv_area.size);

	printf("Shared CtrlArea: Addr=%llx Size=%x\n",
		het_sys_map.sh_ctrl_area.phys_addr,
		het_sys_map.sh_ctrl_area.size);

	printf("DSP Core0 M2: Addr=%llx Size=%x\n",
		het_sys_map.dsp_core0_m2.phys_addr,
		het_sys_map.dsp_core0_m2.size);

	printf("DSP Core1 M2: Addr=%llx Size=%x\n",
		het_sys_map.dsp_core1_m2.phys_addr,
		het_sys_map.dsp_core1_m2.size);

	printf("DSP M3: Addr=%llx Size=%x\n",
		het_sys_map.dsp_m3.phys_addr,
		het_sys_map.dsp_m3.size);

	printf("PA CCSRBAR: Addr =%llx Size=%x\n",
		het_sys_map.pa_ccsrbar.phys_addr,
		het_sys_map.pa_ccsrbar.size);

	printf("DSP CCSRBAR: Addr =%llx Size=%x\n",
		het_sys_map.dsp_ccsrbar.phys_addr,
		het_sys_map.dsp_ccsrbar.size);
}

void *map_area(uint64_t phys_addr, uint32_t  *sz, void *dsp_bt)
{
	int i;
	void *vaddr = NULL;;
	uint64_t diff;
	int size = *sz;
	uint64_t nphys_addr;
	int dev_mem = ((dsp_bt_t *)dsp_bt)->dev_mem;
	int mapidx = ((dsp_bt_t *)dsp_bt)->map_id;

	nphys_addr = phys_addr & MAP_AREA_MASK;
	if (phys_addr + size > nphys_addr + 0x1000)
		size = (size + 0x1000) & MAP_AREA_MASK;

	size += 0x1000 - size % 0x1000;
	diff = phys_addr - nphys_addr;

	for (i = 0; i < mapidx; i++)
		if (phys_addr >= ((dsp_bt_t *)dsp_bt)->map_d[i].phys_addr &&
		    phys_addr < ((dsp_bt_t *)dsp_bt)->map_d[i].phys_addr +
		    ((dsp_bt_t *)dsp_bt)->map_d[i].size) {
			vaddr = mmap(0, size, (PROT_READ | \
				PROT_WRITE), MAP_SHARED, \
					dev_mem, nphys_addr);
			break;
		}

	if (vaddr) {
		*sz = size;
		vaddr += diff;
		return vaddr;
	}

	return NULL;

}

void *map_area64(uint64_t phys_addr, uint64_t  *sz, void *dsp_bt)
{
	int i;
	void *vaddr = NULL;;
	uint64_t diff;
	int size = *sz;
	uint64_t nphys_addr;
	int dev_mem = ((dsp_bt_t *)dsp_bt)->dev_mem;
	int mapidx = ((dsp_bt_t *)dsp_bt)->map_id;

	nphys_addr = phys_addr & MAP_AREA_MASK;
	if (phys_addr + size > nphys_addr + 0x1000)
		size = (size + 0x1000) & MAP_AREA_MASK;

	size += 0x1000 - size % 0x1000;
	diff = phys_addr - nphys_addr;

	for (i = 0; i < mapidx; i++)
		if (phys_addr >= ((dsp_bt_t *)dsp_bt)->map_d[i].phys_addr &&
		    phys_addr < ((dsp_bt_t *)dsp_bt)->map_d[i].phys_addr +
		    ((dsp_bt_t *)dsp_bt)->map_d[i].size) {
			vaddr = mmap(0, size, (PROT_READ | \
				PROT_WRITE), MAP_SHARED, \
					dev_mem, nphys_addr);
			break;
		}

	if (vaddr) {
		*sz = size;
		vaddr += diff;
		return vaddr;
	}

	return NULL;

}

void *map_area64_cache(uint64_t phys_addr, uint64_t  *sz, void *dsp_bt)
{
	int i;
	void *vaddr = NULL;;
	uint64_t diff;
	int size = *sz;
	uint64_t nphys_addr;

	int mapidx = ((dsp_bt_t *)dsp_bt)->map_id;
	int dev_fsl_l1d = ((dsp_bt_t *)dsp_bt)->fsl_l1d;

	nphys_addr = phys_addr & MAP_AREA_MASK;
	if (phys_addr + size > nphys_addr + 0x1000)
		size = (size + 0x1000) & MAP_AREA_MASK;

	size += 0x1000 - size % 0x1000;
	diff = phys_addr - nphys_addr;

	for (i = 0; i < mapidx; i++)
		if (phys_addr >= ((dsp_bt_t *)dsp_bt)->map_d[i].phys_addr &&
		    phys_addr < ((dsp_bt_t *)dsp_bt)->map_d[i].phys_addr +
		    ((dsp_bt_t *)dsp_bt)->map_d[i].size) {
			vaddr = mmap(0, size, (PROT_READ | \
				PROT_WRITE), MAP_SHARED, \
					dev_fsl_l1d, nphys_addr);
			break;
		}

	if (vaddr) {
		*sz = size;
		vaddr += diff;
		return vaddr;
	}

	return NULL;

}

void unmap_area(void *vaddr, uint64_t size)
{
	munmap((void *)((uint32_t)vaddr & VIR_ADDR32_MASK), size);
}

/*
 * @get_hw_sem_value
 *
 */
static inline int get_hw_sem_value(uint32_t *val, int dev_het_mgr)
{
	int ret = 0;
	hw_sem_t hsem;
	hsem.sem_no = 1;
	ret = ioctl(dev_het_mgr, IOCTL_HW_SEM_GET_VALUE, &hsem);
	if (ret)
		return ret;

	*val = hsem.value;
	return ret;
}

/*
 * @set_hw_sem_value
 *
 */
static inline int set_hw_sem_value(uint32_t val, int dev_het_mgr)
{
	hw_sem_t hsem;
	hsem.sem_no = 1;
	hsem.value = val;
	return ioctl(dev_het_mgr, IOCTL_HW_SEM_SET_VALUE, &hsem);
}

/*
 * @get_dsp_uniq_sem_value
 *
 */
static inline int get_uniq_hw_sem_value(hw_sem_info_t *hseminfo,
		int dev_het_mgr)
{
	return ioctl(dev_het_mgr, IOCTL_HW_SEM_GET_UVALUE, hseminfo);
}

static int check_dsp_boot(void *dsp_bt)
{
	uint32_t val;
	int ret;
	int dev_het_mgr = ((dsp_bt_t *)dsp_bt)->het_mgr;

	hw_sem_info_t hwu;

	ret = get_uniq_hw_sem_value(&hwu, dev_het_mgr);
	if (ret)
		goto end;
	ret = get_hw_sem_value(&val, dev_het_mgr);
	if (ret)
		goto end;
	reload_print("val=%d hwu.dsp_uniq_val=%d\n", val, hwu.dsp_uniq_val);

	if (val == hwu.dsp_uniq_val) {
		val = 0;
		set_hw_sem_value(val, dev_het_mgr);
		printf("\n == DSP Booted up == \n");
	} else {
		printf("\n DSP HW Sem1 value not correctly set, value=%x\n",
		       val);
		printf("\n DSP Boot Failed, Please reset\n");
	}
end:
	/* in the end call this */
	if (ret) {
		printf("%s: Error in loading DSP\n", __func__);
		return -1;
	} else
		return DSP_BOOT_SUCCESS;
}

/*
 * @copy_file_part
 *
 * simple byte copy from file
 */
static int copy_file_part(void *virt_addr, uint32_t size, FILE * fp)
{
	int i, sz;
	unsigned char ch;
	unsigned char *vaddr = virt_addr;
	for (i = 0; i < size; i++) {
		sz = fread(&ch, 1, 1, fp);
		if (!sz) {
			printf("Error reading from file \n");
			return -1;
		}
		vaddr[i] = ch;
	}
	return 0;
}

static int init_devmem()
{
	reload_print("%s\n", __func__);
	int dev_mem = open("/dev/mem", O_RDWR);
	if (dev_mem == -1) {
		printf("Error: Cannot open /dev/mem.\n");
		return -1;
	}
	reload_print("dev_mem =%d\n", dev_mem);
	return dev_mem;
}

static int init_hetmgr()
{
	/* query ranges from /dev/het_mgr */
	reload_print("%s\n", __func__);
	int dev_het_mgr = open("/dev/het_mgr", O_RDWR);
	if (dev_het_mgr == -1) {
		printf("Error: Cannot open /dev/het_mgr\n");
		return  -1;
	}
	return dev_het_mgr;
}

static int init_fsl_l1d()
{
	int fsl_l1d = open("/dev/fsl_l1d", O_RDWR);
	if (fsl_l1d == -1) {
		printf("Error: Cannot open /dev/fsl_l1d\n");
		return -1;
	}
	return fsl_l1d;
}

static int assign_memory_areas(void *dsp_bt)
{
	reload_print("%s\n", __func__);
	int ret = 0;
	/* open /dev/mem
	 * map dsp m2/m3/ddr
	 * sys_map_t *het_sys_map, int dev_het_mgr)
	 */
	sys_map_t het_sys_map = ((dsp_bt_t *)dsp_bt)->het_sys_map;

	dump_sys_map(het_sys_map);

	if ((het_sys_map).smart_dsp_os_priv_area.phys_addr == 0xffffffff ||
	    (het_sys_map).dsp_core0_m2.phys_addr == 0xffffffff ||
	    (het_sys_map).dsp_core1_m2.phys_addr == 0xffffffff ||
	    (het_sys_map).dsp_m3.phys_addr == 0xffffffff ||
	    (het_sys_map).dsp_shared_size == 0xffffffff) {
		printf("Incorrect params\n");
		return -1;
	}

	((dsp_bt_t *)dsp_bt)->map_id = 0;
	func_add2map(dsp_bt, (het_sys_map).smart_dsp_os_priv_area.phys_addr,
		(het_sys_map).smart_dsp_os_priv_area.size);
	func_add2map(dsp_bt, (het_sys_map).dsp_core0_m2.phys_addr,
		(het_sys_map).dsp_core0_m2.size);
	func_add2map(dsp_bt, (het_sys_map).dsp_core1_m2.phys_addr,
		(het_sys_map).dsp_core1_m2.size);
	func_add2map(dsp_bt, (het_sys_map).dsp_m3.phys_addr,
		(het_sys_map).dsp_m3.size);
	func_add2map(dsp_bt, (het_sys_map).pa_ccsrbar.phys_addr,
		(het_sys_map).pa_ccsrbar.size);
	func_add2map(dsp_bt, (het_sys_map).dsp_ccsrbar.phys_addr,
		(het_sys_map).dsp_ccsrbar.size);
	func_add2map(dsp_bt, DCSR_BASE, (SIZE_1MB * 32));

	return ret;
}

static int load_dsp_image(char *fname, void *dsp_bt)
{
	/* Read First 4 bytes */
	/* Read phys address and size */
	/* get vaddr from phys addr */
	/* call copy part */
	FILE *dspbin;
	uint8_t endian;
	uint64_t addr;
	void *vaddr;
	int ret = 0;
	uint64_t tsize = 0, size;
	uint32_t reg_val32 = 0;
	dspbin = fopen(fname, "rb");
	if (!dspbin) {
		printf("%s File not found, exiting", fname);
		ret = -1;
		goto end;
	}

	/*Read first Byte */
	ret = fread(&endian, 1, 1, dspbin);
	while (!feof(dspbin)) {

		/*Read addr and size */
		ret = fread(&addr, ADDR_SIZE, 1, dspbin);
		if (!ret) {
			if (ferror(dspbin))
				printf("%s: File read error - %d\n",
				       fname, errno);
			break;
		}

		ret = fread(&size, ADDR_SIZE, 1, dspbin);
		if (!ret) {
			if (ferror(dspbin))
				printf("%s: File read error - %d\n",
				       fname, errno);
			break;
		}

		if (!size)
			continue;

		tsize = size;
		/* Read the intvec_addr and skip its mmap*/
		if (addr == 0xffffffffffffffff && size == 8) {
			ret = fread(&((dsp_bt_t *)dsp_bt)->intvec_addr,
				ADDR_SIZE, 1, dspbin);
			printf("intvec_addr =%llx in L1 Binary\n",
				((dsp_bt_t *)dsp_bt)->intvec_addr);
			if (!ret) {
				if (ferror(dspbin))
					printf("%s: File read error - %d\n",
				       fname, errno);
				break;
			}
			continue;
		}
#ifdef B4860
		/* Read the dsp MMU addr and store in dsp_mmu_reg
		 * Bitwise add with ccsbar 0xFFE000000 to verify the same
		 */
		if ((addr &
		     ((dsp_bt_t *)dsp_bt)->het_sys_map.pa_ccsrbar.phys_addr) ==
		     ((dsp_bt_t *)dsp_bt)->het_sys_map.pa_ccsrbar.phys_addr) {

			/* Values written in CCSR space should have
			   non-cacheable attribute
			 * It should have peripheral attribute
			 */
			vaddr = map_area64(addr, &tsize, dsp_bt);
			if (!vaddr) {
				ret = -1;
				printf("\n Error in mapping physical address"
				" %llx to virtual address in %s\n",
				(long long unsigned int)addr, __func__);
				goto end_close_file;
			}

			ret = fread(&reg_val32, size, 1, dspbin);
			if (!ret) {
				if (ferror(dspbin))
					printf("%s: File read error - %d\n",
				       fname, errno);
				unmap_area(vaddr, tsize);
				break;
			} else {
				*((uint32_t *)vaddr) = reg_val32;
				asm("lwsync");
				l1d_printf("\n\nDSP MMU addr=%#llx in %s"
					" reg_val32=%#x\n",
					addr, fname, reg_val32);
				l1d_printf("Read back DSP MMU addr=%#llx in %s"
					" reg_val32=%#x\n",
					addr, fname, *((uint32_t *)vaddr));
				unmap_area(vaddr, tsize);
				continue;
			}
		}
#endif

#ifdef B913x
			vaddr = map_area64(addr, &tsize, dsp_bt);
#endif
#ifdef B4860
			vaddr = map_area64_cache(addr, &tsize, dsp_bt);
#endif
		if (!vaddr) {
			ret = -1;
			printf("\n Error in mapping physical address"
			" %llx to virtual address in %s\n",
			(long long unsigned int)addr, __func__);
			goto end_close_file;
		}
		printf("\n Copy Part %llx %llx\n", (long long unsigned int)addr,
			       (long long unsigned int)size);
		ret = copy_file_part(vaddr, size, dspbin);
		unmap_area(vaddr, tsize);

		reload_print("%s: ret =%d\n", __func__, ret);

		if (ret)
			goto end_close_file;
	}

end_close_file:
	fclose(dspbin);
end:
	return ret;
}

static int dsp_ready_check(void *dsp_bt)
{
	reload_print("%s\n", __func__);
	void *vaddr;
	int ret;
	uint32_t val;
	ret = 0;
	uint32_t tsize = 4;
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;

	uint64_t phys_addr = (*het_sys_map).pa_ccsrbar.phys_addr + DSPSR;
	vaddr = map_area(phys_addr, &tsize, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address\n", phys_addr);
		return -1;
	}
	reload_print("%s physical address %llx to virtual address %lx\n",
			__func__, phys_addr, (long)vaddr);
	val = *((volatile uint32_t *)vaddr);
	unmap_area((void *)vaddr, tsize);
	reload_print("dsp_SR val =%x (%i)\n", val, val);

	if (val & DSPSR_DSP1_READY) {
		printf("DSP READY SET\n");
		return ret;
	}
	return -1;
}

static int set_sh_ctrl_pa_init(int dev_het_mgr)
{
	return ioctl(dev_het_mgr, IOCTL_HET_MGR_SET_INITIALIZED, 0);
}

static int set_ppc_ready(void *dsp_bt)
{
	reload_print("%s\n", __func__);
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	volatile unsigned long *vaddr;
	uint32_t tsize = 4;
	/* write to ppc ready */
	uint64_t phys_addr = (*het_sys_map).dsp_ccsrbar.phys_addr +
		DSP_GCR + PASTATE;
	vaddr = map_area(phys_addr, &tsize, dsp_bt);
	reload_print("%s: physical address %lx to virtual address\n",
		__func__ , (*het_sys_map).dsp_ccsrbar.phys_addr
		+ DSP_GCR + PASTATE);
	if (!vaddr) {
		printf("\nError in physical address %llx to virtual"
		       "address\n",
		       (*het_sys_map).dsp_ccsrbar.phys_addr +
		       DSP_GCR + PASTATE);
		return -1;
	}

	*vaddr |= PASTATE_PAREADY;
	unmap_area((void *)vaddr, tsize);
	return 0;
}

static int reset_ppc_ready(void *dsp_bt)
{
	reload_print("Entering func %s\n", __func__);
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	int dev_mem = ((dsp_bt_t *)dsp_bt)->dev_mem;
	volatile void *vaddr, *vaddr_pastate, *vaddr_boot_jmp_addr,
		 *vaddr_dspstate;
	int size = 0x1000;

	unsigned long phys_addr = (*het_sys_map).dsp_ccsrbar.phys_addr +
		DSP_GCR;
	reload_print("het_sys_map.dsp_ccsrbar.phys_addr %x DSP_GCR%x\n",
		(uint32_t)(*het_sys_map).dsp_ccsrbar.phys_addr,
		DSP_GCR);

	vaddr = mmap(0, size, (PROT_READ | PROT_WRITE),
			MAP_SHARED, dev_mem, phys_addr);
	if (!vaddr) {
		printf("\nError in physical address %llx to virtual"
		       "address\n",
		       (*het_sys_map).dsp_ccsrbar.phys_addr + DSP_GCR);
		return -1;
	}

	/* write to ppc ready */
	puts("Clearing PA_STATE");
	vaddr_pastate = vaddr + PASTATE;
	reload_print("before PASTATE= %d\n", *(int *)vaddr_pastate);
	*(int *)vaddr_pastate = RESET;
	reload_print("after PASTATE= %d\n", *(int *)vaddr_pastate);

	/* clear DSP STATE register */
	puts("Clearing DSP_STATE");

	vaddr_dspstate = vaddr + DSPSTATE;

	reload_print("before vaddr_dspstate= %d\n", *(int *)vaddr_dspstate);
	*(int *)vaddr_dspstate = RESET;
	reload_print("after vaddr_dspstate= %d\n", *(int *)vaddr_dspstate);

	/* clear BOOT_JMP_ADDR register */
	puts("Clearing BOOT_JMP_ADDR");

	vaddr_boot_jmp_addr = vaddr + BOOT_JMP_ADDR;

	reload_print("before BOOT_JMP_ADDR= %d\n", *(int *)vaddr_boot_jmp_addr);
	*(int *)vaddr_boot_jmp_addr = RESET;
	reload_print("after BOOT_JMP_ADDR= %d\n", *(int *)vaddr_boot_jmp_addr);
	unmap_area((void *)vaddr, size);
	return 0;
}

int send_vnmi_func(void *dsp_bt)
{
	l1d_printf("Entering func %s\n", __func__);
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	int dev_mem = ((dsp_bt_t *)dsp_bt)->dev_mem;
	volatile uint32_t *vaddr;
	int size = 0x100;
	uint64_t phys_addr = (*het_sys_map).dsp_ccsrbar.phys_addr +
		GIC_VIGR;
	static int32_t need_clear = 1;

	reload_print("het_sys_map.dsp_ccsrbar.phys_addr %x GIC_VIGR%x\n",
		(uint32_t)(*het_sys_map).dsp_ccsrbar.phys_addr,
		GIC_VIGR);

	vaddr = mmap(0, size, (PROT_READ | PROT_WRITE),
			MAP_SHARED, dev_mem, phys_addr);

	if (!vaddr) {
		printf("\nError in physical address %llx to virtual"
		       "address\n",
		       (*het_sys_map).dsp_ccsrbar.phys_addr + GIC_VIGR);
		return -1;
	}
	if(need_clear)
	{
		/* Очистка статуса прерыавний */
		*(vaddr + 2) = 0xffffffff;
		need_clear = 0;
	}

	(*vaddr) |= GIC_VIGR_VALUE;

	unmap_area((void *)vaddr, size);

	return 0;

}

static int init_hugetlb(void *dsp_bt)
{
	reload_print("%s\n", __func__);
	int ret;
	void *pa_p;
	shared_area_t shared_area;
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	int dev_het_mgr = ((dsp_bt_t *)dsp_bt)->het_mgr;
	pa_p = (void *)fsl_shm_init((*het_sys_map).dsp_shared_size, 1);
	if (!pa_p) {
		ret = -1;
		goto end;
	}

	(shared_area).pa_ipc_shared.phys_addr = (unsigned long)pa_p;
	(shared_area).pa_ipc_shared.size = MB_256
				- (*het_sys_map).dsp_shared_size;
	(shared_area).dsp_ipc_shared.phys_addr = (unsigned long)pa_p + MB_256
					- (*het_sys_map).dsp_shared_size;
	(shared_area).dsp_ipc_shared.size = (*het_sys_map).dsp_shared_size;

	printf("PA Shared Area: Addr=%llx Size=%x\n",
		(shared_area).pa_ipc_shared.phys_addr,
		(shared_area).pa_ipc_shared.size);

	printf("DSP Shared Area: Addr=%llx Size=%x\n",
		(shared_area).dsp_ipc_shared.phys_addr,
		(shared_area).dsp_ipc_shared.size);

	/* attach the physical address with shared mem */
	ret = ioctl(dev_het_mgr, IOCTL_HET_MGR_SET_SHARED_AREA, &shared_area);
	if (ret) {
		perror("ioctl IOCTL_HET_MGR_SET_SHARED failed:\n");
		ret = -1;
		goto end;
	}

end:
	return ret;
}

static void cleanup(int dev_mem, int dev_het_mgr)
{
	reload_print("%s\n", __func__);
	close(dev_mem);
	close(dev_het_mgr);
}

int fsl_send_vnmi(void *dsp_bt)
{
	reload_print("calling %s\n", __func__);
	int ret = 0;

	((dsp_bt_t *)dsp_bt)->het_mgr = init_hetmgr();
	if (((dsp_bt_t *)dsp_bt)->het_mgr == -1) {
		printf("error in init_hetmgr frm %s", __func__);
		return -1;
	}

	((dsp_bt_t *)dsp_bt)->dev_mem = init_devmem();
	if (((dsp_bt_t *)dsp_bt)->dev_mem == -1) {
		printf("error in dev_mem frm %s", __func__);
		return -1;
	}

	ret = ioctl(((dsp_bt_t *)dsp_bt)->het_mgr,
			IOCTL_HET_MGR_GET_SYS_MAP,
			&((dsp_bt_t *)dsp_bt)->het_sys_map);
	if (ret) {
		perror("IOCTL_HET_MGR_GET_SYS_MAP:");
		printf("frm %s\n", __func__);
		return -1;
	}

	if (reset_ppc_ready(dsp_bt) < 0) {
		printf("error in init_hetmgr frn %s", __func__);
		return -1;
	}

	/* send vnmi */
	if (send_vnmi_func(dsp_bt) < 0) {
		printf("error in send_vnmi_func frm %s", __func__);
		return -1;
	}

	return ret;
}

int pre_load_913x(int count, ...)
{
	int ret = 0;
	va_list arg_l;
	va_start(arg_l, count);
	void *dsp_bt = va_arg(arg_l, void *);
	((dsp_bt_t *)dsp_bt)->het_mgr = init_hetmgr();
	if (((dsp_bt_t *)dsp_bt)->het_mgr == -1) {
		printf("error in het_mgr from %s\n", __func__);
		return -1;
	}

	((dsp_bt_t *)dsp_bt)->dev_mem = init_devmem();
	if (((dsp_bt_t *)dsp_bt)->dev_mem == -1) {
		printf("error in dev_mem from %s\n", __func__);
		return -1;
	}

	ret = ioctl(((dsp_bt_t *)dsp_bt)->het_mgr,
			IOCTL_HET_MGR_GET_SYS_MAP,
		    &((dsp_bt_t *)dsp_bt)->het_sys_map);
	if (ret) {
		perror("IOCTL_HET_MGR_GET_SYS_MAP:");
		return -1;
	}

	if (assign_memory_areas(dsp_bt)) {
		printf("error in assign_memory_ar frm %s\n", __func__);
		return -1;
	}

	if (init_hugetlb(dsp_bt)) {
		printf("error in init_hugetlb frm %s\n", __func__);
		return -1;
	}

	if (fsl_913x_ipc_init(dsp_bt)) {
		printf("error in fsl_913x_ipc_init frm %s\n", __func__);
		return -1;
	}

	if (dsp_ready_check(dsp_bt)) {
		printf("error in dsp_ready_check frm %s\n", __func__);
		return -1;
	}
	va_end(arg_l);
	return ret;
}

int load_913x(char *fname, void *dsp_bt)
{
	int ret = 0;

	ret = load_dsp_image(fname, dsp_bt);
	if (ret < 0) {
		printf("error in load_dsp_image frm %s\n", __func__);
		return -1;
	}
	printf("DSP image copied\n");
	return ret;
}

int post_load_913x(void *dsp_bt)
{
	int dev_het_mgr = ((dsp_bt_t *)dsp_bt)->het_mgr;
	int ret = 0;

	if (set_sh_ctrl_pa_init(dev_het_mgr)) {
		printf("error in set_sh_ctrl_pa_init frm %s\n",
			__func__);
		return -1;
	}

	if (set_ppc_ready(dsp_bt) < 0) {
		printf("error in set_ppc_ready frm %s\n", __func__);
		return -1;
	}
	/* sleep for 4 sec */
	puts("sleep 4 sec for dsp to be ready");
	sleep(4);

	if (DSP_BOOT_SUCCESS != check_dsp_boot(dsp_bt)) {
		printf("error in check_dsp_boot frm %s\n", __func__);
		return -1;
	}
	return ret;
}

int b913x_load_dsp_image(char *fname)
{
	void *dsp_bt;
	dsp_bt = (void *)malloc(sizeof(dsp_bt_t));

	((dsp_bt_t *)dsp_bt)->het_mgr = -1;
	((dsp_bt_t *)dsp_bt)->dev_mem = -1;

	/* Assign func Pointers*/
	((dsp_bt_t *)dsp_bt)->pre_load = &pre_load_913x;
	((dsp_bt_t *)dsp_bt)->load_image = &load_913x;
	((dsp_bt_t *)dsp_bt)->post_load = &post_load_913x;


	/* Call func Pointers*/
	if (((dsp_bt_t *)dsp_bt)->pre_load(1, dsp_bt) < 0)
		goto end_b913x_load_dsp_image;

	if (((dsp_bt_t *)dsp_bt)->load_image(fname, dsp_bt) < 0)
		goto end_b913x_load_dsp_image;

	if (((dsp_bt_t *)dsp_bt)->post_load(dsp_bt) < 0)
		goto end_b913x_load_dsp_image;
	else {
		cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
			((dsp_bt_t *)dsp_bt)->het_mgr);
		free(dsp_bt);
		return 0;
	}

end_b913x_load_dsp_image:
	cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
		((dsp_bt_t *)dsp_bt)->het_mgr);
	return -1;
}

int pre_reload_913x(int count, ...)
{
	va_list arg_r;
	va_start(arg_r, count);
	void *dsp_bt = va_arg(arg_r, void *);
	void *ipc = va_arg(arg_r, void *);

	if (fsl_ipc_reinit(ipc) < 0) {
		printf("Error in fsl_ipc_reinit\n");
		return -1;
	}

	if (fsl_send_vnmi(dsp_bt) < 0) {
		printf("VNMI failed\n");
		printf("Error in fsl_send_vnmi\n");
		return -1;
	}
	puts("sleep 10 sec");
	sleep(10);

	if (assign_memory_areas(dsp_bt)) {
		printf("error in assign_memory_ar frm %s\n", __func__);
		return -1;
	}


	if (dsp_ready_check(dsp_bt)) {
		printf("error in dsp_ready_check frm %s\n", __func__);
		return -1;
	}

	va_end(arg_r);
	return 0;
}

int fsl_restart_L1(fsl_ipc_t ipc, char *fname)
{
	void *dsp_bt;
	dsp_bt = (void *)malloc(sizeof(dsp_bt_t));

	((dsp_bt_t *)dsp_bt)->het_mgr = -1;
	((dsp_bt_t *)dsp_bt)->dev_mem = -1;

	/* Assign func Pointers*/
	((dsp_bt_t *)dsp_bt)->pre_load = &pre_reload_913x;
	((dsp_bt_t *)dsp_bt)->load_image = &load_913x;
	((dsp_bt_t *)dsp_bt)->post_load = &post_load_913x;


	/* Call func Pointers*/
	if (((dsp_bt_t *)dsp_bt)->pre_load(2, dsp_bt, ipc) < 0)
		goto end_fsl_restart_L1;


	if (((dsp_bt_t *)dsp_bt)->load_image(fname, dsp_bt) < 0)
		goto end_fsl_restart_L1;

	if (((dsp_bt_t *)dsp_bt)->post_load(dsp_bt) < 0)
		goto end_fsl_restart_L1;
	else {
		cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
			((dsp_bt_t *)dsp_bt)->het_mgr);
		return 0;
	}

end_fsl_restart_L1:
	cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
		((dsp_bt_t *)dsp_bt)->het_mgr);
	((dsp_bt_t *)dsp_bt)->het_mgr = -1;
	((dsp_bt_t *)dsp_bt)->dev_mem = -1;
	return -1;

}

/*dsp_bt code for B4860*/
#ifdef B4860
static int release_starcore_B4(void *dsp_bt)
{
	uint64_t intvec_addr = ((dsp_bt_t *)dsp_bt)->intvec_addr;
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;

	/*map to this value size = 0x1000000*/
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address\n", (long long unsigned int)phys_addr);
		return -1;
	}

	printf("Before StarCore release ========\n");
	printf("LCC_BSTRH=0x%x\n", *(vaddr + LCC_BSTRH));
	printf("LCC_BSTRL=0x%x\n", *(vaddr + LCC_BSTRL));
	printf("LCC_BSTAR=0x%x\n", *(vaddr + LCC_BSTAR));
	printf("GCR_CDCER0=0x%x\n", *(vaddr + GCR_CDCER0));
	printf("GCR_CHMER0=0x%x\n", *(vaddr + GCR_CHMER0));
	printf("DCFG_BRR=0x%x\n", *(vaddr + DCFG_BRR));

	*(vaddr + LCC_BSTRH) =  ((intvec_addr >> 32) & 0xF);
	asm("lwsync");
	*(vaddr + LCC_BSTRL) =  (intvec_addr & U32_T_MASK);
	asm("lwsync");
	*(vaddr + LCC_BSTAR) =  DDRC_TRG_ID(((dsp_bt_t *)dsp_bt)->DDRC_trg_id);
	asm("lwsync");
	*(vaddr + GCR_CDCER0) =  0x000003f0;
	asm("lwsync");
	*(vaddr + GCR_CHMER0) =  0x00003f00;
	asm("lwsync");

	*(vaddr + DCFG_BRR) |=  DCFG_BRR_mask;
	asm("lwsync");

	printf(" After StarCore release ========\n");
	printf("LCC_BSTRH=0x%x\n", *(vaddr + LCC_BSTRH));
	printf("LCC_BSTRL=0x%x\n", *(vaddr + LCC_BSTRL));
	printf("LCC_BSTAR=0x%x\n", *(vaddr + LCC_BSTAR));
	printf("GCR_CDCER0=0x%x\n", *(vaddr + GCR_CDCER0));
	printf("GCR_CHMER0=0x%x\n", *(vaddr + GCR_CHMER0));
	printf("DCFG_BRR=0x%x\n", *(vaddr + DCFG_BRR));

	unmap_area((void *)vaddr, size);

	printf("BSTRL,BSTAR,CDCERO,CHMERO n DCFG_BRR set now\n");
	return 0;
}

static int l1d_hold_starcore_B4(dsp_core_info *DspCoreInfo, void *dsp_bt)
{
	l1d_printf("Enter func %s \n", __func__);
	uint32_t i = 0;
	uint32_t DCFG_BRR_mask = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		DCFG_BRR_mask |=
		 ((DspCoreInfo->reDspCoreInfo[i].reset_core_flag) << (i + 4));
		i++;
	}

	l1d_printf("DCFG_BRR_mask = %#x in func %s\n", DCFG_BRR_mask, __func__);
	*(vaddr + DCFG_BRR) &= ~(DCFG_BRR_mask);
	asm("lwsync");
	puts("Hold");

	l1d_printf("DCFG_BRR=0x%x\n", *(vaddr + DCFG_BRR));

	unmap_area((void *)vaddr, size);
	return 0;
}

static int l1d_hold_starcore_B4_simple(void *dsp_bt)
{
	l1d_printf("Enter func %s \n", __func__);
	uint32_t i = 0;
	uint32_t DCFG_BRR_mask = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		DCFG_BRR_mask |= (1 << (i + 4));
		i++;
	}

	l1d_printf("DCFG_BRR_mask = %#x in func %s\n", DCFG_BRR_mask, __func__);
	*(vaddr + DCFG_BRR) &= ~(DCFG_BRR_mask);
	asm("lwsync");
	puts("Hold");

	l1d_printf("DCFG_BRR=0x%x\n", *(vaddr + DCFG_BRR));

	unmap_area((void *)vaddr, size);
	return 0;
}

static int l1d_release_starcore_B4(dsp_core_info *DspCoreInfo, void *dsp_bt)
{
	uint32_t i = 0;
	uint32_t DCFG_BRR_mask = 0;
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;

	/*map to this value size = 0x1000000*/
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address\n", (long long unsigned int)phys_addr);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		DCFG_BRR_mask |=
		 ((DspCoreInfo->reDspCoreInfo[i].reset_core_flag) << (i + 4));
		i++;
	}

	*(vaddr + DCFG_BRR) |=  DCFG_BRR_mask;
	asm("lwsync");

	printf("Release\n");
	l1d_printf("DCFG_BRR=0x%x\n", *(vaddr + DCFG_BRR));

	unmap_area((void *)vaddr, size);

	return 0;
}

int check_dsp_boot_B4(void *dsp_bt)
{

	int hw_handshake_sem_no = ((dsp_bt_t *)dsp_bt)->semaphore_num;
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = (*het_sys_map).dsp_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = (*het_sys_map).dsp_ccsrbar.phys_addr;
	volatile int sem_val = 0;
	uint32_t hw_sem_offset;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %#llx to virtual"
		       "address %p\n", phys_addr, vaddr);
		return -1;
	}

	if (hw_handshake_sem_no <= 7 && hw_handshake_sem_no >= 1) {
		hw_sem_offset = SC_BOOT_HW_SEMAPHORE0 + 2 * hw_handshake_sem_no;
		while (!sem_val) {
			sem_val = *(vaddr + hw_sem_offset);
			/* sleep for 1 sec*/
			puts("sleep 1, waiting for hw sem");
			sleep(1);
		}

		printf("HS_MPR[%d]=0x%x\n", hw_handshake_sem_no, sem_val);
		if (sem_val == OS_HET_SC_SEMAPHORE_VAL) {
			*(vaddr + hw_sem_offset) = 0x00000000;
			asm("lwsync");
			printf("HS_MPR[%d]=0x%x\n",\
				hw_handshake_sem_no, *(vaddr + hw_sem_offset));
			printf("\n \n == DSP Booted up ==\n");
		}
	}

	unmap_area((void *)vaddr, size);
	return 0;
}

int check_vnmi_ack_B4(void *dsp_bt)
{

	l1d_printf("enter func %s\n", __func__);
	int core_id = ((dsp_bt_t *)dsp_bt)->core_id;
	os_het_control_t *ctrl = ((dsp_bt_t *)dsp_bt)->sh_ctrl_area.vaddr;
	os_het_l1d_t *l1_defense = SH_CTRL_VADDR_DSPBT(ctrl->l1d, dsp_bt);

	/*sleep 100ms for dsp to set ack */
	usleep(10000);

	if (l1_defense->reset_status[core_id] !=
	    OS_HET_INFO_L1D_READY_FOR_RESET) {
		printf("\nCore %d not responding to VNMI.\n",
			core_id);
		return -1;
	} else
		return 0;
}

static int set_soc_strobe_B4(dsp_core_info *DspCoreInfo, void * dsp_bt)
{
	l1d_printf("Enter func %s \n", __func__);
	uint32_t i = 0;
	uint32_t crsmcr0_mask = 0;

	volatile void *vaddr = NULL;

	uint64_t phys_addr = DCSR_BASE + RCPM_OFFSET;
	uint64_t size = SIZE_1MB;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n", phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		crsmcr0_mask |=
		 ((DspCoreInfo->reDspCoreInfo[i].reset_core_flag) << (i + 8));
		i++;
	}

	l1d_printf("crsmcr0_mask = %#x in func %s\n", crsmcr0_mask, __func__);
	l1d_printf("crsmcr0(p)=%llx v=b=%p\n", phys_addr, vaddr);

	*((uint32_t *)(vaddr + CRSMCR0_OFFSET)) |= crsmcr0_mask;
	asm("lwsync");

	l1d_printf("crsmcr0=%#x\n", *((uint32_t *)(vaddr + CRSMCR0_OFFSET)));
	printf("\nSoC strobe set via crsmcr0\n");

	unmap_area((void *)vaddr, size);
	return 0;
}

static int set_safe_addr_DTU_B4(dsp_core_info *DspCoreInfo, void *dsp_bt, int i)
{

	l1d_printf("In Func %s\n", __func__);

	volatile void *dcsr_dtu_vaddr = NULL;
	uint64_t temp = 0;
	int mem_fd = ((dsp_bt_t *)dsp_bt)->dev_mem;

	uint16_t clusterIndex = 0;
	uint16_t coreInCluster = 0;
	uint64_t dcsrDtuAddress = DCSR_BASE + DCSR_CLUSTER0_OFFSET;
	uint64_t phys_addr = 0;


	clusterIndex = i / 2;
	coreInCluster = i % 2;
	temp = 0;
	temp = (clusterIndex + 1) * DCSR_CLUSTER_OFFSET +
		coreInCluster * CORE_OFFSET + DTU_OFFSET;
	l1d_printf("i=%d, temp =%#llx\n", i, dcsrDtuAddress+temp);
	phys_addr = dcsrDtuAddress + temp;


	dcsr_dtu_vaddr = mmap(NULL, WPT_REGS_SIZE,
			   (PROT_READ | PROT_WRITE),
			   MAP_SHARED, mem_fd,
			   phys_addr);
	if (dcsr_dtu_vaddr == MAP_FAILED) {
		perror("Error in mmap:");
		printf("In func:%s: i=%d, p =%#llx\n", __func__, i, phys_addr);
		return -1;
	}

	if (SASR_DM_MASK &
	    (*((uint64_t *)(dcsr_dtu_vaddr + SASR_OFFSET)))) {


		/* PC_NEXT*/
		*((uint32_t *)(dcsr_dtu_vaddr + PC_NEXT_OFFSET)) =
			DspCoreInfo->reDspCoreInfo[i].dsp_safe_addr;
		asm("lwsync");
		l1d_printf("value %#x\n",
			DspCoreInfo->reDspCoreInfo[i].dsp_safe_addr);
		/* RCR  */
		*((uint32_t *)(dcsr_dtu_vaddr + RCR_OFFSET)) =
			RCR_VALUE;
		asm("lwsync");
		printf("Safe Address set for Core ID(%d)\n", i);
	} else {
		printf("Neither in debug nor responding to VNMI!\n");
		return -1;
	}
	l1d_printf("CoreID = %d: RCR p=%#llx v=%p\n", i,
		dcsrDtuAddress+temp+RCR_OFFSET,
		dcsr_dtu_vaddr+RCR_OFFSET);
	l1d_printf("CoreId = %d: PC_NEXT p=%#llx v=%p\n", i,
		dcsrDtuAddress+temp+PC_NEXT_OFFSET,
		dcsr_dtu_vaddr+PC_NEXT_OFFSET);

	munmap((void *)dcsr_dtu_vaddr, WPT_REGS_SIZE);

	return 0;
}

static int dsp_L2_cache_invalidate_B4(dsp_core_info *DspCoreInfo, void *dsp_bt)
{
	l1d_printf("enter func %s\n", __func__);

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;
	int ret = 0;
	FILE *fp;
	char model_str[16] = {0};
	int bytes =  strlen(B4860QDS_MODEL_STR);

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	/* open /proc/device-tree/model to find
	b4860/b4420
	*/
	fp = fopen("/proc/device-tree/model", "rb");
	if (!fp) {
		printf("Unable to open /proc/device-tree/model\n");
		ret = -1;
		goto end_L2_INVALIDATE;
	}

	ret = fread(model_str, bytes, 1, fp);
	fclose(fp);

	if (!ret)
		goto end_L2_INVALIDATE;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	l1d_printf("%s: before L2_CACHE_2=0x%x\n", __func__,
			*(vaddr + L2_CACHE_2));
	l1d_printf("L2_CACHE_INVALIDATE_MASK %x\n", L2FI);
	l1d_printf("L2_CACHE_FLUSH_MASK %x\n", L2FL);

	if (DspCoreInfo->reset_mode == MODE_3_ACTIVE) {
		*(vaddr + L2_CACHE_2) |= L2FI;
		asm("lwsync");

		puts("L2 cache cluster2 invalidated");

		if (!memcmp(model_str, B4860QDS_MODEL_STR, bytes)) {
			*(vaddr + L2_CACHE_3) |= L2FI;
			asm("lwsync");
			*(vaddr + L2_CACHE_4) |= L2FI;
			asm("lwsync");
			puts("L2 cache cluster3&4 invalidated");
		}

	}

	sleep(1);
	l1d_printf("%s: L2_CACHE_2=0x%x\n", __func__, *(vaddr + L2_CACHE_2));
	if (!memcmp(model_str, B4860QDS_MODEL_STR, bytes)) {
		l1d_printf("%s: L2_CACHE_3=0x%x\n", __func__,
				*(vaddr + L2_CACHE_3));
		l1d_printf("%s: L2_CACHE_4=0x%x\n", __func__,
				*(vaddr + L2_CACHE_4));
	}

	unmap_area((void *)vaddr, size);
end_L2_INVALIDATE:

	return ret;
}

static int dsp_L2_cache_invalidate_B4_dspbt(void *dsp_bt)
{
	l1d_printf("enter func %s\n", __func__);

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;
	int ret = 0;
	FILE *fp;
	char model_str[16] = {0};
	int bytes =  strlen(B4860QDS_MODEL_STR);

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	/* open /proc/device-tree/model to find
	b4860/b4420
	*/
	fp = fopen("/proc/device-tree/model", "rb");
	if (!fp) {
		printf("Unable to open /proc/device-tree/model\n");
		ret = -1;
		goto end_L2_INVALIDATE;
	}

	ret = fread(model_str, bytes, 1, fp);
	fclose(fp);

	if (!ret)
		goto end_L2_INVALIDATE;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	l1d_printf("%s: before L2_CACHE_2=0x%x\n", __func__,
			*(vaddr + L2_CACHE_2));
	l1d_printf("L2_CACHE_INVALIDATE_MASK %x\n", L2FI);
	l1d_printf("L2_CACHE_FLUSH_MASK %x\n", L2FL);

	/*
	*(vaddr + L2_CACHE_1) |= L2FI;
	asm("lwsync");

	puts("L2 cache cluster1 invalidated");
	*/

	*(vaddr + L2_CACHE_2) |= L2FI;
	asm("lwsync");

	puts("L2 cache cluster2 invalidated");

	if (!memcmp(model_str, B4860QDS_MODEL_STR, bytes)) {
		*(vaddr + L2_CACHE_3) |= L2FI;
		asm("lwsync");
		*(vaddr + L2_CACHE_4) |= L2FI;
		asm("lwsync");
		puts("L2 cache cluster3&4 invalidated");
	}

	sleep(1);
	l1d_printf("%s: L2_CACHE_2=0x%x\n", __func__, *(vaddr + L2_CACHE_2));
	if (!memcmp(model_str, B4860QDS_MODEL_STR, bytes)) {
		l1d_printf("%s: L2_CACHE_3=0x%x\n", __func__,
				*(vaddr + L2_CACHE_3));
		l1d_printf("%s: L2_CACHE_4=0x%x\n", __func__,
				*(vaddr + L2_CACHE_4));
	}

	unmap_area((void *)vaddr, size);
end_L2_INVALIDATE:

	return ret;
}

#if PRODUCTION_REL
static int set_DMCSR_on_dspcore_B4(void *dsp_bt)
{
	l1d_printf("enter func %s\n", __func__);
	uint32_t i = 0;

	volatile uint32_t *vaddr = NULL;

	uint64_t phys_addr = DCSR_BASE;
	uint64_t size = 0x01050000;

	int	mem_fd;
	mem_fd = open("/dev/mem", O_RDWR);
	if (mem_fd == -1) {
		fprintf(stderr, "dsp: Error: Cannot open /dev/mem\n");
		return -1;
	}

	vaddr = (uint32_t *) mmap(NULL, size,
		(PROT_READ | PROT_WRITE),
		MAP_SHARED, mem_fd, phys_addr);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	puts("Set DMCSR to disable debugm");
	while (i < NR_DSP_CORE) {
		l1d_printf("%s: DMCSR[%d]=0x%x\n", __func__, i,
				*(vaddr + DTU_DMCSR_ADDR[i]));
		*(vaddr + DTU_DMCSR_ADDR[i]) &= DMCSR_DBG_DISABLE;
		asm("lwsync");
		l1d_printf("%s: DMCSR[%d]=0x%x\n", __func__, i,
				*(vaddr + DTU_DMCSR_ADDR[i]));
		i++;
	}

	unmap_area((void *)vaddr, size);
	close(mem_fd);
	return 0;
}
#endif /* PROD_REL */

static int dsp_L2_cache_init_B4(void *dsp_bt, int dsp_cluster_count)
{
	L2I_printf("enter func %s %d\n", __func__, dsp_cluster_count);

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;
	int ret = 0, i = 0;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (dsp_cluster_count > 0) {

		if ((*(vaddr + L2_CACHE_2 + i * OFF_L2_CACHE_X) & L2E)) {
			printf("DSP L2 cache Cluster_%d already enabled\n",
				(2 + i));
			i++;
			dsp_cluster_count -= 1;
			continue;
		}
		L2I_printf("%s: Begin L2_CACHE_%d=0x%x\n", __func__, (2 + i),
			*(vaddr + L2_CACHE_2 + i * OFF_L2_CACHE_X));


		*(vaddr + L2_CACHE_2 + i * OFF_L2_CACHE_X) = L2FI | L2LFC;
		asm("lwsync");
		while ((*(vaddr + L2_CACHE_2 + i * OFF_L2_CACHE_X) &
			(L2FI|L2LFC))) {
				printf("Waiting for FI\n");
				L2I_printf("%s: before L2_CACHE_%d=0x%x\n",
						__func__, (2 + i),
				*(vaddr + L2_CACHE_2 + i * OFF_L2_CACHE_X));
			}

		L2I_printf("%s: After L2_CACHE_%d=0x%x\n", __func__, (2 + i),
			*(vaddr + L2_CACHE_2 + i * OFF_L2_CACHE_X));
		printf("L2 cache cluster%d init\n", (2+i));
		L2I_printf("vaddr %p\n", vaddr);

		L2I_printf("p1 = %p\n", (vaddr + L2_CACHE_2 +
				(i * OFF_L2_CACHE_X) + OFF_L2_CACHE_X_CSR1));
		*(vaddr + L2_CACHE_2 + (i * OFF_L2_CACHE_X) +
				OFF_L2_CACHE_X_CSR1) = (34 + (i * 2) + 1);
		asm("lwsync");

		*(vaddr + L2_CACHE_2 + (i * OFF_L2_CACHE_X)) = L2E | L2PE;
		asm("lwsync");

		L2I_printf("L2E p=%p %#x\n",
			(vaddr + L2_CACHE_2 + (i * OFF_L2_CACHE_X)),
			*(vaddr + L2_CACHE_2 + (i * OFF_L2_CACHE_X)));
		while (!(*(vaddr + L2_CACHE_2 + i * OFF_L2_CACHE_X) & L2E))
				printf("Waiting for L2E\n");

		L2I_printf("%s: After L2_CACHE_%d=0x%x\n",  __func__, (2 + i),
			*(vaddr + L2_CACHE_2 + (i * OFF_L2_CACHE_X)));

		dsp_cluster_count -= 1;
		i++;
	}
	unmap_area((void *)vaddr, size);

	return ret;
}

static int set_PH15_on_dspcore_B4(dsp_core_info *DspCoreInfo, void *dsp_bt)
{
	l1d_printf("enter func %s\n", __func__);
	uint32_t PH15_mask = 0, i = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		PH15_mask |=
		 ((DspCoreInfo->reDspCoreInfo[i].reset_core_flag) << (i + 4));
		i++;
	}

	l1d_printf("PH15_mask = %#x in func %s\n", PH15_mask, __func__);

	*(vaddr + PCPH15SETR) |= PH15_mask;
	asm("lwsync");
	int ntries = 0;
	while (PH15_mask != (*(vaddr + PCPH15SR) & PH15_mask) && ntries++ < 10)
		printf("Waiting for PCPH15SR\n");

	puts("Set PH15");

	l1d_printf("%s: PCPH15SR=0x%x\n", __func__, *(vaddr + PCPH15SR));
	l1d_printf("%s: PCPH15CLRR=0x%x\n", __func__, *(vaddr + PCPH15CLRR));
	unmap_area((void *)vaddr, size);
	return 0;
}

static int set_PH15_on_dspcore_B4_simple(void *dsp_bt)
{
	l1d_printf("enter func %s\n", __func__);
	uint32_t PH15_mask = 0, i = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		PH15_mask |= (1 << (i + 4));
		i++;
	}

	l1d_printf("PH15_mask = %#x in func %s\n", PH15_mask, __func__);

	*(vaddr + PCPH15SETR) |= PH15_mask;
	asm("lwsync");
	asm("sync");

	*(vaddr + PCPH15SETR) |= PH15_mask;
	asm("lwsync");
	asm("sync");

	*(vaddr + PCPH15SETR) |= PH15_mask;
	asm("lwsync");
	asm("sync");

	uint32_t ph15 = 0;
	uint32_t rc = 0;
	
	do
	{
		usleep(1000);
		
		asm("lwsync");

		ph15 = (*(vaddr + PCPH15SR) & PH15_mask);
		printf("Read PCPH15SR = 0x%08x mask 0x%08x\n", ph15, PH15_mask);
		rc++;

	} while (PH15_mask != ph15 && rc < 10);

//	if(PH15_mask != ph15)
//		return -1;
	
	//while (PH15_mask != (*(vaddr + PCPH15SR) & PH15_mask))
	//	printf("Waiting for PCPH15SR\n");

	puts("Set PH15");

	l1d_printf("%s: PCPH15SR=0x%x\n", __func__, *(vaddr + PCPH15SR));
	l1d_printf("%s: PCPH15CLRR=0x%x\n", __func__, *(vaddr + PCPH15CLRR));
	unmap_area((void *)vaddr, size);
	return 0;
}

static int set_PIR_on_dspcore_B4(dsp_core_info *DspCoreInfo, void *dsp_bt)
{
	uint32_t i = 0;
	uint32_t PIR_mask = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		PIR_mask |=
		 (DspCoreInfo->reDspCoreInfo[i].reset_core_flag) << (i + 8);
		i++;
	}

	l1d_printf("PIR_mask = %#x in func %s\n", PIR_mask, __func__);


	*(vaddr + PIR) |= PIR_mask;
	asm("lwsync");

	while (PIR_mask != (*(vaddr + PIR) & PIR_mask))
		printf("Waiting for PIR set\n");

	puts("Set PIR");
	printf("PIR=0x%x\n", *(vaddr + PIR));

	l1d_printf("PCPH15PSR=0x%x\n", *(vaddr + PCPH15PSR));

	l1d_printf("PCPH15CLRR=0x%x\n", *(vaddr + PCPH15CLRR));
	l1d_printf("PCPH15SR=0x%x\n", *(vaddr + PCPH15SR));
	l1d_printf("PCPH15PSR=0x%x\n", *(vaddr + PCPH15PSR));

	unmap_area((void *)vaddr, size);
	return 0;
}

static int set_PIR_on_dspcore_B4_simple(void *dsp_bt)
{
	uint32_t i = 0;
	uint32_t PIR_mask = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		PIR_mask |= (1 << (i + 8));
		i++;
	}

	l1d_printf("PIR_mask = %#x in func %s\n", PIR_mask, __func__);

	*(vaddr + PIR) |= PIR_mask;
	asm("lwsync");

	while (PIR_mask != (*(vaddr + PIR) & PIR_mask))
		printf("Waiting for PIR set\n");

	puts("Set PIR");
	printf("PIR=0x%x\n", *(vaddr + PIR));

	l1d_printf("PCPH15PSR=0x%x\n", *(vaddr + PCPH15PSR));

	l1d_printf("PCPH15CLRR=0x%x\n", *(vaddr + PCPH15CLRR));
	l1d_printf("PCPH15SR=0x%x\n", *(vaddr + PCPH15SR));
	l1d_printf("PCPH15PSR=0x%x\n", *(vaddr + PCPH15PSR));

	unmap_area((void *)vaddr, size);
	return 0;
}

static int clear_STMR_B4_simple(void *dsp_bt)
{
	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	*(vaddr + 0x8F3000/4) = 0x00000001;
	*(vaddr + 0x8F3004/4) = 0x00000001;
	*(vaddr + 0x8F3008/4) = 0x00000001;
	*(vaddr + 0x8F300C/4) = 0x00000001;
	*(vaddr + 0x8F3010/4) = 0x00000001;
	*(vaddr + 0x8F3014/4) = 0x00000001;
	*(vaddr + 0x8F3018/4) = 0x00000001;
	*(vaddr + 0x8F301C/4) = 0x00000001;

	asm("lwsync");

	printf("Clear STMR\n");

	unmap_area((void *)vaddr, size);
	return 0;
}

static int Reset_PIR_PH15_on_dspcore_B4(dsp_core_info *DspCoreInfo,
		void *dsp_bt)
{
	uint32_t i = 0;
	uint32_t PH15CLRR_mask = 0;
	uint32_t PIR_mask = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	l1d_printf("Enter func %s \n", __func__);
	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		PH15CLRR_mask |=
		 ((DspCoreInfo->reDspCoreInfo[i].reset_core_flag) << (i + 4));
		PIR_mask |=
		 (DspCoreInfo->reDspCoreInfo[i].reset_core_flag) << (i + 8);
		i++;
	}

	l1d_printf("PH15CLRR_mask = %#x in func %s\n", PH15CLRR_mask, __func__);
	l1d_printf("PIR_mask = %#x in func %s\n", PIR_mask, __func__);


	l1d_printf("%s: PIR=0x%x\n", __func__, *(vaddr + PIR));
	l1d_printf("PCPH15PSR=0x%x\n", *(vaddr + PCPH15PSR));

	puts("clear PIR");
	*(vaddr + PIR) &= ~PIR_mask;
	asm("lwsync");
	while ((*(vaddr + PIR) & PIR_mask))
		printf("Waiting for PIR clear\n");

	l1d_printf("%s: PIR=0x%x\n", __func__, *(vaddr + PIR));

	puts("clear PH15");
	*(vaddr + PCPH15CLRR) = PH15CLRR_mask;
	asm("lwsync");
	while (*(vaddr + PCPH15SR) & PH15CLRR_mask)
		printf("Waiting for PCPH15SR clear\n");

	l1d_printf("PCPH15CLRR=0x%x\n", *(vaddr + PCPH15CLRR));
	l1d_printf("PCPH15SR=0x%x\n", *(vaddr + PCPH15SR));
	printf("PCPH15PSR=0x%x\n", *(vaddr + PCPH15PSR));

	unmap_area((void *)vaddr, size);
	return 0;
}

static int Reset_PIR_PH15_on_dspcore_B4_simple(void *dsp_bt)
{
	uint32_t i = 0;
	uint32_t PH15CLRR_mask = 0;
	uint32_t PIR_mask = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	l1d_printf("Enter func %s \n", __func__);
	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}

	while (i < NR_DSP_CORE) {
		PH15CLRR_mask |= (1 << (i + 4));
		PIR_mask |= (1 << (i + 8));
		i++;
	}

	l1d_printf("PH15CLRR_mask = %#x in func %s\n", PH15CLRR_mask, __func__);
	l1d_printf("PIR_mask = %#x in func %s\n", PIR_mask, __func__);


	l1d_printf("%s: PIR=0x%x\n", __func__, *(vaddr + PIR));
	l1d_printf("PCPH15PSR=0x%x\n", *(vaddr + PCPH15PSR));

	puts("clear PIR");
	*(vaddr + PIR) &= ~PIR_mask;
	asm("lwsync");
	while ((*(vaddr + PIR) & PIR_mask))
		printf("Waiting for PIR clear\n");

	l1d_printf("%s: PIR=0x%x\n", __func__, *(vaddr + PIR));

	
	puts("clear PH15");
	*(vaddr + PCPH15CLRR) = PH15CLRR_mask;
	asm("lwsync");
	while (*(vaddr + PCPH15SR) & PH15CLRR_mask)
		printf("Waiting for PCPH15SR clear\n");
	
	l1d_printf("PCPH15CLRR=0x%x\n", *(vaddr + PCPH15CLRR));
	l1d_printf("PCPH15SR=0x%x\n", *(vaddr + PCPH15SR));
	printf("PCPH15PSR=0x%x\n", *(vaddr + PCPH15PSR));

	unmap_area((void *)vaddr, size);
	return 0;
}

static int check_dsp_ready_CRSTSR_B4(dsp_core_info *DspCoreInfo, void *dsp_bt)
{
	l1d_printf("enter func %s\n", __func__);
	uint32_t i = 0, j  = 0;

	sys_map_t *het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	uint64_t size = het_sys_map->pa_ccsrbar.size;
	volatile uint32_t *vaddr = NULL;

	/*map to this value phys_addr = 0xffe000000*/
	uint64_t phys_addr = het_sys_map->pa_ccsrbar.phys_addr;

	vaddr = map_area64(phys_addr, &size, dsp_bt);
	if (!vaddr) {
		printf("\nError in mapping physical address %llx to virtual"
		       "address from func %s\n",
		       (long long unsigned int)phys_addr, __func__);
		return -1;
	}
	sleep(3);
	while (i < NR_DSP_CORE) {
		if (DspCoreInfo->reDspCoreInfo[i].reset_core_flag) {
			l1d_printf("%s: ready=%#x\n", __func__,
				   (*(vaddr + DCFG_CRSTSR + i)));

			while (j++ < 5) {
				if (0x4 & (*(vaddr + DCFG_CRSTSR + i))) {
					printf("Core_id = %#x is ready now\n",
						(i + 4));
					j = 0;
					break;
				} else {
					puts("sleep 1 sec");
					sleep(1);
				}
			}

			if ((0x4 & (*(vaddr + DCFG_CRSTSR + i))) != 0x4)
				return -1;

		}

		i++;
	}
	unmap_area((void *)vaddr, size);
	return 0;
}

static void set_reset_modes_B4(dsp_core_info *DspCoreInfo, void *dsp_bt)
{
	l1d_printf("enter func %s\n", __func__);
	os_het_control_t *ctrl = ((dsp_bt_t *)dsp_bt)->sh_ctrl_area.vaddr;
	os_het_l1d_t *l1_defense =
		SH_CTRL_VADDR_DSPBT(ctrl->l1d, dsp_bt);

	l1_defense->warm_reset_mode = DspCoreInfo->reset_mode;
	l1_defense->reset_maple = DspCoreInfo->maple_reset_mode;
	return;
}

static int reset_het_structures_B4(void *dsp_bt)
{
	l1d_printf("enter func %s\n", __func__);
	int dev_het_mgr = ((dsp_bt_t *)dsp_bt)->het_mgr;
	return ioctl(dev_het_mgr, IOCTL_HET_MGR_RESET_STRUCTURES, 0);
}

void flush_cnpc()
{
	l1d_printf("Enter func %s\n", __func__);
	volatile uint32_t *cnpc_dcsr = NULL;
	int	mem_fd;
	mem_fd = open("/dev/mem", O_RDWR);
	if (mem_fd == -1) {
		fprintf(stderr, "dsp: Error: Cannot open /dev/mem\n");
		return;
	}

	/* mmap64 used in original code */
	cnpc_dcsr = (uint32_t *) mmap(NULL, NPC_REGS_SIZE,
		(PROT_READ | PROT_WRITE),
		MAP_SHARED, mem_fd, CNPC_PHYSICAL_ADDR);

	if (cnpc_dcsr == MAP_FAILED) {
		printf("error in mmap (dcsr) frm %s\n", __func__);
		close(mem_fd);
		return;
	}

	/* flush Central Nexus Port Controller by setting C-OQCR[AFA]*/
	cnpc_dcsr[DCSR_CNPC_OQCR_OFFSET] |= DCSR_CNPC_OQCR_AFA_MASK;

	munmap((void *)cnpc_dcsr, NPC_REGS_SIZE);
	close(mem_fd);

	return;
}

int set_Hw_watchpoint(dsp_core_info *DspCoreInfo, void *dsp_bt)
{
	printf("Set Hw_watchpoint \n");

	volatile void *dtu_offset_vaddr = NULL;
	uint64_t temp = 0;
	int mem_fd = ((dsp_bt_t *)dsp_bt)->dev_mem;
	int i = 0;
	uint16_t clusterIndex = 0;
	uint16_t coreInCluster = 0;
	uint64_t dcsrDtuAddress = DCSR_BASE + DCSR_CLUSTER0_OFFSET;

	while (i < NR_DSP_CORE) {
		if ((DspCoreInfo->reDspCoreInfo[i].reset_core_flag) &&
		   ((DspCoreInfo->reDspCoreInfo[i].wpt_type == 'b')
		     || (DspCoreInfo->reDspCoreInfo[i].wpt_type == 'w')
		     || (DspCoreInfo->reDspCoreInfo[i].wpt_type == 'r'))) {

			clusterIndex = i / 2;
			coreInCluster = i % 2;
			temp = 0;
			temp = (clusterIndex + 1) * DCSR_CLUSTER_OFFSET +
				coreInCluster * CORE_OFFSET + DTU_OFFSET;
			dtu_offset_vaddr = mmap(NULL, WPT_REGS_SIZE,
					   (PROT_READ | PROT_WRITE),
					   MAP_SHARED, mem_fd,
					   (dcsrDtuAddress + temp));
			printf("hw temp=%#llx\n", dcsrDtuAddress + temp);
			if (dtu_offset_vaddr == MAP_FAILED) {
				printf("error in mmap dcsrDtuAddress"
					" frm %s\n", __func__);
				return -1;
			}

			/* *regDHRRR */
			*((uint32_t *)(dtu_offset_vaddr + DHRRR_OFFSET)) =
				0x100;
			asm("lwsync");
			/* *regDMEER */
			*((uint32_t *)(dtu_offset_vaddr + DMEER_OFFSET)) =
				0x100;
			asm("lwsync");

			l1d_printf("CoreId=%d dhrrr=%#x dmeer=%#x\n", i,
			   *((uint32_t *)(dtu_offset_vaddr + DHRRR_OFFSET)),
			   *((uint32_t *)(dtu_offset_vaddr + DMEER_OFFSET)));

			/* *regARDCR0 */
			if (DspCoreInfo->reDspCoreInfo[i].wpt_type == 'b')
				*((uint32_t *)(dtu_offset_vaddr +\
				 ARDCR0_OFFSET)) = WPT_TYPE_WR;
			else if (DspCoreInfo->reDspCoreInfo[i].wpt_type == 'w')
				*((uint32_t *)(dtu_offset_vaddr +\
				ARDCR0_OFFSET)) = WPT_TYPE_W;
			else  /* read */
				*((uint32_t *)(dtu_offset_vaddr +\
				ARDCR0_OFFSET)) = WPT_TYPE_R;
			asm("lwsync");

			/* *regPADRRA0 */
			*((uint32_t *)(dtu_offset_vaddr + PADRRA0_OFFSET)) =
				DspCoreInfo->reDspCoreInfo[i].wpt_begin_addr;
			asm("lwsync");
			/* *regPADRRB0 */
			*((uint32_t *)(dtu_offset_vaddr + PADRRB0_OFFSET)) =
				DspCoreInfo->reDspCoreInfo[i].wpt_end_addr;
			asm("lwsync");

			l1d_printf("CoreID = %d: DHRRR p=%#llx v=%p\n", i,
				dcsrDtuAddress+temp+DHRRR_OFFSET,
				dtu_offset_vaddr+DHRRR_OFFSET);
			l1d_printf("CoreId = %d: ARDCR0 p=%#llx v=%p\n", i,
				dcsrDtuAddress+temp+ARDCR0_OFFSET,
				dtu_offset_vaddr+ARDCR0_OFFSET);

			munmap((void *)dtu_offset_vaddr, WPT_REGS_SIZE);
			}
		i++;
	}

	return 0;
}

int pre_load_B4(int count, ...)
{
	int ret = 0;
	va_list arg_b;
	va_start(arg_b, count);
	void *dsp_bt = va_arg(arg_b, void*);
	int dspClusterCount = va_arg(arg_b, int);

	((dsp_bt_t *)dsp_bt)->het_mgr = init_hetmgr();
	if (((dsp_bt_t *)dsp_bt)->het_mgr == -1) {
		printf("error in het_mgr frm %s\n", __func__);
		return -1;
	}

	((dsp_bt_t *)dsp_bt)->dev_mem = init_devmem();
	if (((dsp_bt_t *)dsp_bt)->dev_mem == -1) {
		printf("error in dev_mem frm %s\n", __func__);
		return -1;
	}

	((dsp_bt_t *)dsp_bt)->fsl_l1d = init_fsl_l1d();
	if (((dsp_bt_t *)dsp_bt)->fsl_l1d == -1) {
		printf("Error in open /dev/fsl_l1d\n");
		printf("frm %s\n", __func__);
		return -1;
	}

	ret = ioctl(((dsp_bt_t *)dsp_bt)->het_mgr,
		    IOCTL_HET_MGR_GET_SYS_MAP,
		    &((dsp_bt_t *)dsp_bt)->het_sys_map);
	if (ret) {
		perror("IOCTL_HET_MGR_GET_SYS_MAP:");
		printf("frm %s\n", __func__);
		return -1;
	}

	if (assign_memory_areas(dsp_bt)) {
		printf("error in assign_memory_areas frm %s\n", __func__);
		return -1;
	}

	/* get PA shared area and DSP shared AREA*/
	if (init_hugetlb(dsp_bt)) {
		printf("error in init_hugetlb frm %s\n", __func__);
		return -1;
	}

	if (fsl_B4_ipc_init(dsp_bt)) {
		printf("error in fsl_B4_ipc_init frm %s\n", __func__);
		return -1;
	}

	if (dsp_L2_cache_init_B4(dsp_bt, dspClusterCount)) {
		printf("error in dsp_L2_cache_init frm %s\n", __func__);
		return -1;
	}

	va_end(arg_b);
	return 0;
}

int pre_Reload_B4(int count, ...)
{
	int ret = 0, j = 0, flag_crsmcr0 = 0;
	va_list arg_b;
	va_start(arg_b, count);
	void *dsp_bt = va_arg(arg_b, void*);
	void *ipc = va_arg(arg_b, void*);
	dsp_core_info *DspCoreInfo = (dsp_core_info *)va_arg(arg_b, void*);
	sys_map_t *het_sys_map = NULL;
	uint64_t size = 0;
	/*map to this value phys_addr = 0xfff00000*/
	uint64_t phys_addr = 0;
	uint32_t *ctrl = NULL;


	((dsp_bt_t *)dsp_bt)->het_mgr = init_hetmgr();
	if (((dsp_bt_t *)dsp_bt)->het_mgr == -1) {
		printf("error in het_mgr frm %s\n", __func__);
		return -1;
	}

	((dsp_bt_t *)dsp_bt)->dev_mem = init_devmem();
	if (((dsp_bt_t *)dsp_bt)->dev_mem == -1) {
		printf("error in dev_mem frm %s\n", __func__);
		return -1;
	}

	((dsp_bt_t *)dsp_bt)->fsl_l1d = init_fsl_l1d();
	if (((dsp_bt_t *)dsp_bt)->fsl_l1d == -1) {
		printf("Error in open /dev/fsl_l1d\n");
		printf("frm %s\n", __func__);
		return -1;
	}

	ret = ioctl(((dsp_bt_t *)dsp_bt)->het_mgr,
		    IOCTL_HET_MGR_GET_SYS_MAP,
		    &((dsp_bt_t *)dsp_bt)->het_sys_map);
	if (ret) {
		perror("IOCTL_HET_MGR_GET_SYS_MAP:");
		printf("frm %s\n", __func__);
		return -1;
	}

	if (assign_memory_areas(dsp_bt)) {
		printf("error in assign_memory_areas frm %s\n", __func__);
		return -1;
	}

	het_sys_map = &((dsp_bt_t *)dsp_bt)->het_sys_map;
	size = het_sys_map->sh_ctrl_area.size;
	/*map to this value phys_addr = 0xfff00000*/
	phys_addr = het_sys_map->sh_ctrl_area.phys_addr;
	/* get os_het_control_t */
	ctrl = mmap(0, size, (PROT_READ | PROT_WRITE),
			MAP_SHARED, ((dsp_bt_t *)dsp_bt)->dev_mem, phys_addr);

	if (!ctrl) {
		printf("\nError in physical address %llx to virtual"
		       "address\n", phys_addr);
		return -1;
	}

	/*fill dsp_bt with sh_ctrl_area values*/
	((dsp_bt_t *)dsp_bt)->sh_ctrl_area.vaddr = (void *)ctrl;
	((dsp_bt_t *)dsp_bt)->sh_ctrl_area.phys_addr = phys_addr;
	((dsp_bt_t *)dsp_bt)->sh_ctrl_area.size = size;
	l1d_printf("%s: vadr=%p ph=%llx sz=%llx\n",
		   __func__, ctrl, phys_addr, size);

	l1d_printf("DspCoreInfo->reDspCoreInfo[j].reset_core_flag = %x\n",
			DspCoreInfo->reDspCoreInfo[j].reset_core_flag);
	for (j = 0; j < NR_DSP_CORE; j++) {
		((dsp_bt_t *)dsp_bt)->core_id = j;
		if (DspCoreInfo->reDspCoreInfo[j].reset_core_flag) {
			GIC_VIGR_VALUE = GIC_VIGR_VALUE_ARR[j];
			if (send_vnmi_func(dsp_bt)) {
				printf("Error in send_vnmi_func frm %s\n",
					__func__);
				return -1;
			}
			if (check_vnmi_ack_B4(dsp_bt)) {
				/* Waited for 100us, Go for the Kill */
				usleep(100);
				if (set_safe_addr_DTU_B4(DspCoreInfo, dsp_bt,
					j)) {
					printf("Error in set_safe_addr_DTU_B4"
						" from %s\n", __func__);
					return -1;
				} else
					flag_crsmcr0 = 1;
			}
		}
	}

	if (flag_crsmcr0) {
		if (set_soc_strobe_B4(DspCoreInfo, dsp_bt)) {
			printf("Error in set_soc_strobe_B4 %s\n", __func__);
			return -1;
		} else {
			for (j = 0; j < NR_DSP_CORE; j++) {
				((dsp_bt_t *)dsp_bt)->core_id = j;
				if (DspCoreInfo->reDspCoreInfo[j].\
						reset_core_flag) {
					if (check_vnmi_ack_B4(dsp_bt)) {
						printf("Error in "
						"check_vnmi_ack_B4"
						" from %s\n", __func__);
						return -1;
					}
				}
			}

			flag_crsmcr0 = 0;
		}
	}

	if (set_PH15_on_dspcore_B4(DspCoreInfo, dsp_bt) < 0) {
		printf("Error in set_PH15_on_dspcore_B4 %s\n", __func__);
		return -1;
	}

	if (dsp_L2_cache_invalidate_B4(DspCoreInfo, dsp_bt) < 0) {
		printf("Error in dsp_L2_cache_invalidate_B4 %s\n", __func__);
		return -1;
	}

	if (DspCoreInfo->reset_mode == MODE_3_ACTIVE) {
		/*
		if (reset_het_structures_B4(dsp_bt) < 0) {
			printf("Error in reset_het_structures frm %s\n",
				__func__);
			return -1;
		} else if (fsl_B4_ipc_reinit(ipc, dsp_bt) < 0) {
			printf("Error in fsl_ipc_reinit frm %s\n", __func__);
			return -1;
		}
		*/
		if (fsl_B4_ipc_reinit(ipc, dsp_bt) < 0) {
			printf("Error in fsl_ipc_reinit frm %s\n", __func__);
			return -1;
		}
	} else {
		ret = check_validation_fields(dsp_bt);
		if (ret != 0) {
			printf("Error in check_validation_fields frm"
			       " %s ret=(%i)\n", __func__, ret);
			return ret;
		}
	}

	set_reset_modes_B4(DspCoreInfo, dsp_bt);

	/* flush VTB
	 * Check for which mode this flush is needed
	 */
	if (DspCoreInfo->debug_print != 0)
		flush_cnpc();

	if (set_PIR_on_dspcore_B4(DspCoreInfo, dsp_bt) < 0) {
		printf("Error in set_PIR_on_dspcore_B4 frm %s\n", __func__);
		return -1;
	}

	if (l1d_hold_starcore_B4(DspCoreInfo, dsp_bt) < 0) {
		printf("Error in l1d_hold_starcore_B4 frm %s\n", __func__);
		return -1;
	}

	va_end(arg_b);
	return 0;
}

int load_B4(char *fname, void *dsp_bt)
{
	static int j;
	printf("Loading Dsp image %s\n", fname);
	int core_id = ((dsp_bt_t *)dsp_bt)->core_id;
	if (load_dsp_image(fname, dsp_bt)) {
		printf("Error in loading Dsp image StarCore\n");
		return -1;
	}

	if (j == 0) {
		if (set_sh_ctrl_pa_init(((dsp_bt_t *)dsp_bt)->het_mgr)) {
			printf("Error initialising PA Share"
				" CTRL StarCore\n");
			return -1;
		} else
			j++;
	}

	core_id += 4;
	DCFG_BRR_mask |=  1 << core_id;
	return 0;
}

int Reload_B4(char *fname, void *dsp_bt)
{
	static int j;
	printf("Reloading image %s\n", fname);
	if (load_dsp_image(fname, dsp_bt)) {
		printf("Error in loading Dsp image StarCore\n");
		return -1;
	}

	sleep(2);
	if (((dsp_bt_t *)dsp_bt)->core_id == 0) {
		if (set_sh_ctrl_pa_init(((dsp_bt_t *)dsp_bt)->het_mgr)) {
			printf("Error initialising PA Share"
				" CTRL StarCore\n");
			return -1;
		} else
			j++;
	}

	return 0;
}

int post_load_B4(void *dsp_bt)
{
	return check_dsp_boot_B4(dsp_bt);
}

int dsp_cluster_count_f(dspbt_core_info CI)
{
	return CI.core_id/2 + 1;
}

int b4860_load_dsp_image(int argc, dspbt_core_info CoreInfo[])
{

	int i = 0;
	/* argc -1 since array starts from 0 */
	int dspClusterCount = dsp_cluster_count_f(CoreInfo[(argc - 1)]);
	L2I_printf("dspClusterCount=%d\n", dspClusterCount);

	void *dsp_bt;
	dsp_bt = (void *)malloc(sizeof(dsp_bt_t));

	((dsp_bt_t *)dsp_bt)->het_mgr = -1;
	((dsp_bt_t *)dsp_bt)->dev_mem = -1;
	((dsp_bt_t *)dsp_bt)->fsl_l1d = -1;

	/* Assign func Pointers*/
	((dsp_bt_t *)dsp_bt)->pre_load = &pre_load_B4;
	((dsp_bt_t *)dsp_bt)->load_image = &load_B4;
	((dsp_bt_t *)dsp_bt)->post_load = &post_load_B4;


	/* Call func Pointers*/
	if (((dsp_bt_t *)dsp_bt)->pre_load(2, dsp_bt, dspClusterCount) < 0)
		goto end_b4860_load_dsp_image;

	/* Load image StarCore */
	argc -= 1 ;
	i += 1;

	while (argc > 0) {
		((dsp_bt_t *)dsp_bt)->core_id = CoreInfo[i].core_id;

		if (((dsp_bt_t *)dsp_bt)->load_image(CoreInfo[i].image_name,
		dsp_bt) < 0)
			goto end_b4860_load_dsp_image;

		argc -= 1;
		i += 1;
	}

#if PRODUCTION_REL
	if (set_DMCSR_on_dspcore_B4(dsp_bt) < 0) {
		printf("Error in set_DMCSR_on_dspcore_B4 frm %s\n", __func__);
		goto end_b4860_load_dsp_image;
	}
#endif

	/* release StarCore */
	((dsp_bt_t *)dsp_bt)->DDRC_trg_id = CoreInfo[0].DDRC_trg_id;
	if (release_starcore_B4(dsp_bt)) {
		printf("Error in releasing StarCore\n");
		goto end_b4860_load_dsp_image;
	} else
		DCFG_BRR_mask = 0;

	/* copy semaphore number*/
	((dsp_bt_t *)dsp_bt)->semaphore_num = CoreInfo[0].core_id;
	if (((dsp_bt_t *)dsp_bt)->post_load(dsp_bt) < 0)
		goto end_b4860_load_dsp_image;
	else {
		cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
			((dsp_bt_t *)dsp_bt)->het_mgr);
		close(((dsp_bt_t *)dsp_bt)->fsl_l1d);
		free(dsp_bt);
		return 0;
	}

end_b4860_load_dsp_image:
	cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
		((dsp_bt_t *)dsp_bt)->het_mgr);
	close(((dsp_bt_t *)dsp_bt)->fsl_l1d);
	return -1;
}

int b4860_load_dsp_image_new(int argc, dspbt_core_info CoreInfo[])
{

	int i = 0;
	/* argc -1 since array starts from 0 */
	int dspClusterCount = dsp_cluster_count_f(CoreInfo[(argc - 1)]);
	L2I_printf("dspClusterCount=%d\n", dspClusterCount);

	void *dsp_bt;
	dsp_bt = (void *)malloc(sizeof(dsp_bt_t));

	((dsp_bt_t *)dsp_bt)->het_mgr = -1;
	((dsp_bt_t *)dsp_bt)->dev_mem = -1;
	((dsp_bt_t *)dsp_bt)->fsl_l1d = -1;

	/* Assign func Pointers*/
	((dsp_bt_t *)dsp_bt)->pre_load = &pre_load_B4;
	((dsp_bt_t *)dsp_bt)->load_image = &load_B4;
	((dsp_bt_t *)dsp_bt)->post_load = &post_load_B4;


	/* Call func Pointers*/
	if (((dsp_bt_t *)dsp_bt)->pre_load(2, dsp_bt, dspClusterCount) < 0)
		goto end_b4860_load_dsp_image;

	//fsl_send_vnmi(dsp_bt);
#if 1
	/* Assert HRESET on DSP cores */
	if (set_PH15_on_dspcore_B4_simple(dsp_bt) < 0) {
		printf("Error in set_PH15_on_dspcore_B4_simple frm %s\n", __func__);
		return -1;
	}
#endif

	if (dsp_L2_cache_invalidate_B4_dspbt(dsp_bt) < 0) {
		printf("Error in dsp_L2_cache_invalidate_B4_dspbt %s\n", __func__);
		return -1;
	}


#if 1
	if (set_PIR_on_dspcore_B4_simple(dsp_bt) < 0) {
		printf("Error in set_PIR_on_dspcore_B4_simple frm %s\n", __func__);
		return -1;
	}
#endif 

	/* Hold DSP cores in boot state */
	if (l1d_hold_starcore_B4_simple(dsp_bt) < 0) {
		printf("Error in l1d_hold_starcore_B4_simple frm %s\n", __func__);
		return -1;
	}

#if 1
	if (Reset_PIR_PH15_on_dspcore_B4_simple(dsp_bt) < 0) {
		printf("Error in Reset_PIR_PH15_on_dspcore_B4_simple frm %s\n", __func__);
		return -1;
	}
#endif

	/* Clear STMR registers */
	if(clear_STMR_B4_simple(dsp_bt) < 0) {
		printf("Error in clear_STMR_B4_simple frm %s\n", __func__);
		return -1;
	}

	/* Load image StarCore */
	argc -= 1 ;
	i += 1;

	while (argc > 0) {
		((dsp_bt_t *)dsp_bt)->core_id = CoreInfo[i].core_id;

		if (((dsp_bt_t *)dsp_bt)->load_image(CoreInfo[i].image_name,
		dsp_bt) < 0)
			goto end_b4860_load_dsp_image;

		argc -= 1;
		i += 1;
	}

#if 0
	if (Reset_PIR_PH15_on_dspcore_B4_simple(dsp_bt) < 0) {
		printf("Error in Reset_PIR_PH15_on_dspcore_B4_simple frm %s\n", __func__);
		return -1;
	}
#endif

#if PRODUCTION_REL
	if (set_DMCSR_on_dspcore_B4(dsp_bt) < 0) {
		printf("Error in set_DMCSR_on_dspcore_B4 frm %s\n", __func__);
		goto end_b4860_load_dsp_image;
	}
#endif

	/* release StarCore */
	((dsp_bt_t *)dsp_bt)->DDRC_trg_id = CoreInfo[0].DDRC_trg_id;
	if (release_starcore_B4(dsp_bt)) {
		printf("Error in releasing StarCore\n");
		goto end_b4860_load_dsp_image;
	} else
		DCFG_BRR_mask = 0;

	/* copy semaphore number*/
	((dsp_bt_t *)dsp_bt)->semaphore_num = CoreInfo[0].core_id;
	if (((dsp_bt_t *)dsp_bt)->post_load(dsp_bt) < 0)
		goto end_b4860_load_dsp_image;
	else {
		cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
			((dsp_bt_t *)dsp_bt)->het_mgr);
		close(((dsp_bt_t *)dsp_bt)->fsl_l1d);
		free(dsp_bt);
		return 0;
	}

end_b4860_load_dsp_image:
	cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
		((dsp_bt_t *)dsp_bt)->het_mgr);
	close(((dsp_bt_t *)dsp_bt)->fsl_l1d);
	return -1;
}

int fsl_start_L1_defense(fsl_ipc_t ipc, dsp_core_info *DspCoreInfo)
{
	int i = 0, ret = 0;
	void *dsp_bt;
	os_het_control_t *ctrl = NULL;
	os_het_l1d_t *l1_d = NULL;
	dsp_bt = (void *)malloc(sizeof(dsp_bt_t));

	((dsp_bt_t *)dsp_bt)->het_mgr = -1;
	((dsp_bt_t *)dsp_bt)->dev_mem = -1;
	((dsp_bt_t *)dsp_bt)->fsl_l1d = -1;

	/* Assign func Pointers*/
	((dsp_bt_t *)dsp_bt)->pre_load = &pre_Reload_B4;
	((dsp_bt_t *)dsp_bt)->load_image = &Reload_B4;
	((dsp_bt_t *)dsp_bt)->post_load = &post_load_B4;


	/* Call func Pointers*/

	if (!ipc) {
		ret = (((dsp_bt_t *)dsp_bt)->pre_load(3, dsp_bt,
			NULL, (void *)DspCoreInfo) < 0);
		if (ret != 0)
			goto end_L1_defense;
	} else {
		ret = (((dsp_bt_t *)dsp_bt)->pre_load(3, dsp_bt, ipc,
				(void *)DspCoreInfo) < 0);
		if (ret != 0)
			goto end_L1_defense;

	}

	/* Now in between Preload and load*/
	if (Reset_PIR_PH15_on_dspcore_B4(DspCoreInfo, dsp_bt) < 0) {
		printf("Error in Reset_PIR_PH15_on_dspcore_B4 frm %s\n",
				__func__);
		goto end_L1_defense;
	}
	/*shared images loading only for mode 3*/
	while (i < NR_DSP_CORE) {
			if ((DspCoreInfo->reset_mode == MODE_3_ACTIVE) &&
			    DspCoreInfo->shDspCoreInfo[i].reset_core_flag) {
				((dsp_bt_t *)dsp_bt)->core_id = -1;
				if (((dsp_bt_t *)dsp_bt)->load_image(\
				   DspCoreInfo->shDspCoreInfo[i].dsp_filename,\
				   dsp_bt) < 0)
					goto end_L1_defense;
				else
					ret = 0;
				}

			i++;
		}

	/* Load image on StarCore */
	i = 0;
	while (i < NR_DSP_CORE) {
			if (DspCoreInfo->reDspCoreInfo[i].reset_core_flag) {
				((dsp_bt_t *)dsp_bt)->core_id = i;
				if (((dsp_bt_t *)dsp_bt)->load_image(\
				 DspCoreInfo->reDspCoreInfo[i].dsp_filename,\
				 dsp_bt) < 0)
					goto end_L1_defense;
				else
					ret = 0;
			}

			i++;
		}



#if PRODUCTION_REL
	if (set_DMCSR_on_dspcore_B4(dsp_bt) < 0) {
		printf("Error in set_DMCSR_on_dspcore_B4 frm %s\n", __func__);
		goto end_L1_defense;
	}
#endif

	if (l1d_release_starcore_B4(DspCoreInfo, dsp_bt) < 0) {
		printf("Error in l1d_release_starcore_B4 frm %s\n", __func__);
		goto end_L1_defense;
	}
	if (check_dsp_ready_CRSTSR_B4(DspCoreInfo, dsp_bt) < 0) {
		printf("Error in check_dsp_ready_CRSTSR_B4 frm %s\n", __func__);
		goto end_L1_defense;
	}

	/*
	 * Set Hw Watchpoint
	 */
	if (DspCoreInfo->cfg_wpt == 1)
		if (set_Hw_watchpoint(DspCoreInfo, dsp_bt) < 0) {
			printf("Error in set_Hw_watchpoint frm %s\n", __func__);
			return -1;
		}

	if (DspCoreInfo->reset_mode == MODE_3_ACTIVE ||
	    DspCoreInfo->reset_mode == MODE_2_ACTIVE) {
		((dsp_bt_t *)dsp_bt)->semaphore_num = DspCoreInfo->hw_sem_num;
		if (((dsp_bt_t *)dsp_bt)->post_load(dsp_bt) < 0)
			goto end_L1_defense;
	}

	/*check error status in reset_status[]*/
	ctrl = ((dsp_bt_t *)dsp_bt)->sh_ctrl_area.vaddr;
	l1_d = SH_CTRL_VADDR_DSPBT(ctrl->l1d, dsp_bt);
	i = 0;
	while (i < NR_DSP_CORE) {
		if (DspCoreInfo->reDspCoreInfo[i].reset_core_flag) {
			if (l1_d->reset_status[i] == WARM_RESET_SUCCESS) {
				printf("Warm reset success on"
					" core_id = %#x\n", i);
				i++;
				continue;
			} else {
				if (l1_d->reset_status[i] ==
				    BEGIN_WARM_RESET_OS_INIT  ||
				    l1_d->reset_status[i] ==
				    BEGIN_WARM_RESET_APP_INIT) {
					puts("Still at BEGIN_WARM_RESET_OS_INIT"
					" or BEGIN_WARM_RESET_APP_INIT"
					" sleep 2 sec");
					sleep(2);
				}

				if (l1_d->reset_status[i] !=
				    WARM_RESET_SUCCESS) {
					ret = l1_d->reset_status[i];
					printf("warm reset status = %#x\n",
						ret);
				}
			}
		}
		i++;
	}

goto L1_defense_success;
end_L1_defense:
	ret = -ERR_L1_DEFENSE_API_FAIL;
L1_defense_success:
	unmap_area(((dsp_bt_t *)dsp_bt)->sh_ctrl_area.vaddr,
		   ((dsp_bt_t *)dsp_bt)->sh_ctrl_area.size);
	cleanup(((dsp_bt_t *)dsp_bt)->dev_mem,
		((dsp_bt_t *)dsp_bt)->het_mgr);
	close(((dsp_bt_t *)dsp_bt)->fsl_l1d);
	free(dsp_bt);

	return ret;

}
#endif
