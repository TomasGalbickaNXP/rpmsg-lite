/*
 * Copyright 2016-2022 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "rpmsg_lite.h"
#include "mcmgr.h"
#include "fsl_device_registers.h"
#include "fsl_common.h"
#include "unity.h"
#include "board.h"

#include "app.h"
#if defined(FSL_FEATURE_MAILBOX_SIDE_A)
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
    BOARD_InitHardware();

#ifdef CORE1_IMAGE_COPY_TO_RAM
    /* Calculate size of the image - not required on LPCExpresso. LPCExpresso copies image to RAM during startup
     * automatically */
    uint32_t core1_image_size;
    core1_image_size = get_core1_image_size();
    PRINTF("Copy Secondary core image to address: 0x%x, size: %d\r\n", (void *)(char *)CORE1_BOOT_ADDRESS, core1_image_size);

    /* Copy Secondary core application from FLASH to RAM. Primary core code is executed from FLASH, Secondary from RAM
     * for maximal effectivity.*/
    memcpy((void *)(char *)CORE1_BOOT_ADDRESS, (void *)CORE1_IMAGE_START, core1_image_size);
    
#ifdef APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY
    invalidate_cache_for_core1_image_memory(CORE1_BOOT_ADDRESS, core1_image_size);
#endif /* APP_INVALIDATE_CACHE_FOR_SECONDARY_CORE_IMAGE_MEMORY */
#endif /* CORE1_IMAGE_COPY_TO_RAM */

    /* Initialize MCMGR before calling its API */
    MCMGR_Init();

#if !defined(FSL_FEATURE_MU_SIDE_B)

    PRINTF("Starting Primary core.\r\n");
    MCMGR_StartCore(kMCMGR_Core1, (void *)(char *)CORE1_BOOT_ADDRESS, 0, kMCMGR_Start_Asynchronous);

    /* Wait for remote side to come up. This delay is arbitrary and may
    * need adjustment for different configuration of remote systems */
    env_sleep_msec(1000);
#endif

    UnityBegin();
    run_tests(NULL);
    UnityEnd();
    CornBreakpointFunc();
    while (1)
        ;
}

#if defined(SDK_OS_FREE_RTOS)
TaskHandle_t test_task_handle = NULL;
int main(void)
{
    if (xTaskCreate(run_test_suite, "TEST_TASK", TEST_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &test_task_handle) != pdPASS)
    {
        (void)PRINTF("\r\nFailed to create application task\r\n");
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
    run_test_suite(NULL);
    return 0;
}
#endif
