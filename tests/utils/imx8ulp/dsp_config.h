/*
 * Copyright 2021 NXP
 * All rights reserved.
 *
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _DSP_CONFIG_H_
#define _DSP_CONFIG_H_

/* Address of RAM, where the image for DSP should be copied */
/* These addresses are accessed by the ARM core and aliased to M33 code memory */
#define DSP_RESET_ADDRESS (uint32_t *)0x20008000
#define DSP_TEXT_ADDRESS  (uint32_t *)0x20008400
#define DSP_SRAM_ADDRESS  (uint32_t *)0x20020000

/* Inter processor communication common RAM */
/* This address is accessed by both cores, and aliased to M33 data memory */
#define RPMSG_LITE_LINK_ID    (RL_PLATFORM_IMX8ULP_M33_FUSION_DSP_USER_LINK_ID)
#define RPMSG_LITE_SHMEM_BASE (void *)0x1FFC0000
#define RPMSG_LITE_SHMEM_SIZE (64 * 1024)

#endif
