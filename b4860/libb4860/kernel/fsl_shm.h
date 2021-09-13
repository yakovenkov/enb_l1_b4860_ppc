/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *	Author: Naveen Burmi <naveenburmi@freescale.com>
 */

#ifndef __FSL_MM_IPC_H
#define __FSL_MM_IPC_H

#include <stdint.h>
#include <linux/ioctl.h>

/* shm segment type, stores PA and VA*/
typedef struct shm_seg {
	__u64 vaddr;
	__u64 paddr;
	__u64 size;
} shm_seg_t;

/* store physical address, can be un-aligned */
typedef struct alloc_req {
	__u64 paddr;
	__u64 size;
} alloc_req_t;

/* stores aligned physical address */
typedef struct memalign_req {
	__u64 paddr;
	__u64 align;
	__u64 size;
} memalign_req_t;

#define FSL_MM_MAGIC	'F'
/* Initializses HUGEPAGE Shared Memory */
#define IOCTL_FSL_SHM_INIT	_IOWR(FSL_MM_MAGIC, 1, uint64_t)
/* Used for shared memory allocation */
#define IOCTL_FSL_SHM_ALLOC	_IOWR(FSL_MM_MAGIC, 2, uint64_t)
/* Used for address aligned shared memory allocation */
#define IOCTL_FSL_SHM_MEMALIGN  _IOWR(FSL_MM_MAGIC, 3, uint64_t)
/* Frees shared memory allocated */
#define IOCTL_FSL_SHM_FREE	_IOR(FSL_MM_MAGIC, 4, uint64_t)
/* Force Initializses HUGEPAGE Shared Memory */
#define IOCTL_FSL_SHM_FORCE_INIT	_IOWR(FSL_MM_MAGIC, 5, uint64_t)

#endif
