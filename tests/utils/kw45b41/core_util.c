/*
 * Copyright 2016-2023 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "rpmsg_lite.h"
#include "fsl_device_registers.h"
#include "fsl_common.h"
#include "unity.h"
#include "board.h"

#include "app.h"
#if defined(IMU_CPU_INDEX) && (IMU_CPU_INDEX == 1U)
#include "mcmgr.h"
#include "fsl_debug_console.h"
#endif

#if defined(SDK_OS_FREE_RTOS)
#include "FreeRTOS.h"
#include "task.h"
#endif

#define TEST_TASK_STACK_SIZE 400

extern void run_tests(void *unused);

void setUp(void)
{
}

void tearDown(void)
{
}

/* This function is used for the Corn test automation framework
   to breakpoint/stop the execution and to capture results
   from the memory. It must be ensured that it will never be inlined
   and optimized to allow proper address recognition and breakpoint 
   placement during the Corn execution. */
__attribute__((noinline)) void CornBreakpointFunc(void)
{
    volatile int i=0;
    i++;
}

void run_test_suite(void *unused)
{

    /* Initialize MCMGR before calling its API */
    MCMGR_Init();

#if defined(IMU_CPU_INDEX) && (IMU_CPU_INDEX == 1U)

    PRINTF("Starting Primary core.\r\n");
    MCMGR_StartCore(kMCMGR_Core1, (void *)(char *)CORE1_BOOT_ADDRESS, 0, kMCMGR_Start_Asynchronous);

    /* Wait for remote side to come up. This delay is arbitrary and may
    * need adjustment for different configuration of remote systems */
    env_sleep_msec(1000);
#endif

    UnityBegin();
    run_tests(NULL);
    UnityEnd();
#if defined(IMU_CPU_INDEX) && (IMU_CPU_INDEX == 1U)
#ifdef __COVERAGESCANNER__
#if defined(SQUISHCOCO_RESULT_DATA_SAVE_TO_FILE)
    /* Store the secondary core measurement data (saved temporarily in shared memory) into the file */
    FILE *fptr;
    /* Wait until all SQUISHCOCO RESULT DATA is stored in the shared memory and the SQUISHCOCO_SEED_FLAG is set */
    while(SQUISHCOCO_SEED_FLAG != (*(uint32_t *)(RPMSG_LITE_SHMEM_BASE + 0x4)));
        ;
    fptr = fopen("rpmsg_lite_test_sec_core.csexe","w");
    fwrite((const void *)(RPMSG_LITE_SHMEM_BASE + 0x10), sizeof(char), *(uint32_t *)RPMSG_LITE_SHMEM_BASE,fptr);
    fclose(fptr);
#elif defined(SQUISHCOCO_RESULT_DATA_SAVE_TO_CONSOLE)
    /* Printf the secondary core measurement data (saved temporarily in shared memory) into the console */
    char *s_ptr = (char *)(RPMSG_LITE_SHMEM_BASE + 0x10);
    for(int32_t i=0; i<(*(uint32_t *)RPMSG_LITE_SHMEM_BASE);i++)
        PRINTF("%c", s_ptr[i]);
#endif
#endif /*__COVERAGESCANNER__*/
#endif
    CornBreakpointFunc();
    while (1)
        ;
}

#if defined(SDK_OS_FREE_RTOS)
TaskHandle_t test_task_handle = NULL;
int main(void)
{
    BOARD_InitHardware();

    if (xTaskCreate(run_test_suite, "TEST_TASK", TEST_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &test_task_handle) != pdPASS)
    {
        for (;;)
        {
        }
    }

    vTaskStartScheduler();
    return 0;
}
#else
int main(void)
{
    BOARD_InitHardware();
    run_test_suite(NULL);
    return 0;
}
#endif
