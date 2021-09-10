set (B4860_IPC_SOURCES
		ipc/lib/fsl_ipc_channel.c
		ipc/lib/fsl_ipc_lock.c
		ipc/lib/fsl_user_dma.c
		ipc/lib/fsl_usmmgr.c
		fsl_shm/lib/lg_shm.c
		dsp_boot/libdspboot.c
		)

include_directories(ipc/include)
include_directories(fsl_shm/include)
include_directories(kernel)
include_directories(fsl_shm/lib)
include_directories(dsp_boot)

add_library(b4860ipc OBJECT ${B4860_IPC_SOURCES})

#add_executable(dsp_bt dsp_boot/dsp_boot.c dsp_boot/libdspboot.c)
add_executable(dsp_bt dsp_boot/dsp_boot.c)
target_link_libraries(dsp_bt b4860ipc "-lpthread")
#target_compile_definitions(dsp_bt PRIVATE -DDBG -DPRODUCTION_REL)
target_compile_definitions(dsp_bt PRIVATE -DDBG -DDEBUG_L1D)

add_custom_command(TARGET dsp_bt POST_BUILD
    COMMAND cp -p ${CMAKE_CURRENT_BINARY_DIR}/dsp_bt /tftpboot/b4860/enb/
    )

add_executable(l1_defense_app ipc/test/l1_defense_app.c)
target_link_libraries(l1_defense_app b4860ipc "-lpthread")
#target_compile_definitions(dsp_bt PRIVATE -DDBG -DPRODUCTION_REL)
target_compile_definitions(l1_defense_app PRIVATE -DDBG -DPRODUCTION_REL)

add_executable(dump_mem ipc/test/fsl_dump_memory.c)
target_link_libraries(dump_mem b4860ipc "-lpthread")
target_compile_definitions(dump_mem PRIVATE -DDBG)

