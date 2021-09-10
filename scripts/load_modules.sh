#!/bin/sh
insmod /usr/driver/IPC/single_rat/hetmgr.ko dsp_shared_size=67108864
insmod /usr/driver/IPC/single_rat/shm.ko
insmod /usr/driver/IPC/single_rat/ipc.ko
insmod /usr/driver/IPC/single_rat/l1d.ko
