CC=$(CROSS_COMPILE)gcc
KERNEL_DIR1=$(PWD)/kernel
HET_DIR=$(KERNEL_DIR1)
FSL_SHM_DIR=../fsl_shm/include
SOURCES= ./dsp_boot.c
SOURCES_FSL_SHM= ../fsl_shm/lib/lg_shm.c
CFLAGS=-O2 -fPIC -I $(HET_DIR) -I $(FSL_SHM_DIR) -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -Wall

ifeq ($(B4860), 1)
  CFLAGS+=-DB4860
ifeq ($(PROD_REL), 1)
  CFLAGS+=-DPRODUCTION_REL
endif
endif

ifeq ($(B913x), 1)
  CFLAGS+=-DB913x
endif

all: fsl_shm lib_dsp_boot dsp_bt
lib_dsp_boot:
	$(CC) -c ./libdspboot.c $(CFLAGS) -o ./libdspboot.o
	$(CC) -shared -fPIC ./libdspboot.o -o libdspboot.so
	$(AR) rcs libdspboot.a ./libdspboot.o ../fsl_shm/lib/lg_shm.o
fsl_shm:
	$(CC) -c $(CFLAGS) $(SOURCES_FSL_SHM)
dsp_bt:
	$(CC) $(CFLAGS) $(SOURCES) $(SOURCES_FSL_SHM) libdspboot.c -lpthread ../ipc/libipc.a -o $@ 
	cp libdspboot.so  ../ipc/ -v
clean:
	rm -rf *.o dsp_bt libdspboot.so *.a
