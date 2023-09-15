/*
 * Copyright 2021-2023 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "rpmsg_lite.h"
#include "fsl_device_registers.h"
#include "fsl_common.h"
#include "unity.h"
#include "fsl_mu.h"
#if defined(MU1_MUA)
#include "board.h"
#elif defined(MU1_MUB)
#include "board_fusionf1.h"
#include <xtensa/config/core.h>
#endif

#include "app.h"
#if defined(MU1_MUA)
#include "dsp_support.h"
#include "fsl_debug_console.h"
#endif

#if defined(SDK_OS_FREE_RTOS)
#include "FreeRTOS.h"
#include "task.h"
#elif defined(FSL_RTOS_XOS)
#include <xtensa/xos.h>
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
    volatile int i = 0;
    i++;
}

void run_test_suite(void *unused)
{

#if defined(MU1_MUA)

    /* MUA init - must be called before BOARD_DSP_Init() otherwise the MUB on the DSP core is not enabled
     and the MU interrupt is not registerred correctly when the DSP core runs (writing to MUB registers
     is not possible when the MUA is not initialized before). */
    MU_Init(MU1_MUA);

    PRINTF("Starting Secondary core.\r\n");

    /* Start dsp firmware */
    BOARD_DSP_Init();

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
    BOARD_InitHardware();

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    if (xTaskCreate(run_test_suite, "TEST_TASK", TEST_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &test_task_handle) != pdPASS)
    {
        (void)PRINTF("\r\nFailed to create application task\r\n");
        for (;;)
        {
        }
    }
#else
    test_task_handle = xTaskCreateStatic(run_test_suite, "TEST_TASK", TEST_TASK_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, xStack, &xTaskBuffer);
#endif

    vTaskStartScheduler();
    return 0;
}
#elif defined(FSL_RTOS_XOS)
int main(void)
{
    BOARD_InitHardware();

    xos_start_main("main", 7, 0);
    run_test_suite(NULL);
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
