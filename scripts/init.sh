#!/bin/bash

. load_modules.sh

CAT=/bin/cat
PROC=/proc/devices
HETMGR=het_mgr
IPC=het_ipc
MKNOD=/bin/mknod
DEVSHM=fsl_shm

MAJOR1=`$CAT $PROC |grep $HETMGR |awk '{print $1}'`

$MKNOD /dev/$HETMGR c $MAJOR1 0
if [ $? -eq 0 ]
then
	echo "Device file created for \"$HETMGR\""; echo""
else
	echo "Not able to create device file for \"$HETMGR\""; echo""
	exit
fi

MAJOR2=`$CAT $PROC |grep $IPC |awk '{print $1}'`

$MKNOD /dev/$IPC c $MAJOR2 0
if [ $? -eq 0 ]
then
	echo "Device file created for \"$IPC\""; echo""
else
	echo "Not able to create device file for \"IPC\""; echo""
fi

MAJOR4=`$CAT $PROC |grep $DEVSHM |awk '{print $1}'`

$MKNOD /dev/$DEVSHM c $MAJOR4 0
if [ $? -eq 0 ]
then
	echo "Device file created for \"$DEVSHM\""; echo""
else
	echo "Not able to create device file for \"DEVSHM\""; echo""
fi

$MKNOD /dev/fsl_l1d c 246 0
echo 0x10000000 > /proc/sys/kernel/shmmax

ip tuntap add dev tap10 mode tap
ip addr add 192.168.31.1/24 dev tap10
ip addr add 192.168.30.1/24 dev tap10
ifconfig tap10 up

mount -t tmpfs -o size=512M /dev/null /enb/logs-mem

ifconfig lo add 127.0.0.11 netmask 255.0.0.0
ifconfig lo add 127.0.0.22 netmask 255.0.0.0

ulimit -c unlimited
