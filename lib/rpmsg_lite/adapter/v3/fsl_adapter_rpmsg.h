/*
 * Copyright 2020, 2022-2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __FSL_RPMSG_ADAPTER_H__
#define __FSL_RPMSG_ADAPTER_H__

#include "fsl_common.h"

/*!
 * @addtogroup RPMSG_Adapter
 * @{
 */

/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*! @brief RPMSG Master/Remote role definition (0 - Master, 1 - Remote) */
#ifndef HAL_RPMSG_SELECT_ROLE
#define HAL_RPMSG_SELECT_ROLE (0U)
#endif

/*! @brief Peer core identifier. */
#ifndef HAL_RPMSG_PEER_CORE
#if (HAL_RPMSG_SELECT_ROLE == 0U)
#define HAL_RPMSG_PEER_CORE kMCMGR_Core1
#else
#define HAL_RPMSG_PEER_CORE kMCMGR_Core0
#endif
#endif

/*! @brief Whether this side is responsible for booting the peer core. */
#ifndef HAL_RPMSG_BOOT_PEER_CORE
#if (HAL_RPMSG_SELECT_ROLE == 0U)
#define HAL_RPMSG_BOOT_PEER_CORE (1U)
#else
#define HAL_RPMSG_BOOT_PEER_CORE (0U)
#endif
#endif

/*! @brief Maximum number of concurrent RPMSG links supported by the adapter. */
#ifndef HAL_RPMSG_MAX_LINKS
#define HAL_RPMSG_MAX_LINKS (2U)
#endif

/*! @brief RPMSG handle size definition */
#define HAL_RPMSG_HANDLE_SIZE (52U)

#ifndef REMOTE_CORE_BOOT_ADDRESS
#define REMOTE_CORE_BOOT_ADDRESS (0x01000000U)
#endif

#ifndef MAX_EP_COUNT
#define MAX_EP_COUNT (5U)
#endif

#define RPMSG_WAITFOREVER (0xFFFFFFFFU)

/*! @brief RPMSG link configuration structure. */
typedef struct _hal_rpmsg_link_config
{
    uint32_t role;          /*!< 0 - Master, 1 - Remote */
    uint32_t peer_core;     /*!< Peer core identifier (mcmgr_core_t) */
    void *sh_mem_base;      /*!< Shared memory base address */
    uint32_t sh_mem_size;   /*!< Shared memory size */
    void *sh_mem_start;     /*!< Shared memory start for remote (optional) */
    uint8_t boot_peer_core; /*!< 1 - Boot peer core, 0 - Don't boot */
} hal_rpmsg_link_config_t;

/*!
 * @brief Defines the rpmsg handle
 */
#define RPMSG_HANDLE_DEFINE(name) uint32_t name[((HAL_RPMSG_HANDLE_SIZE + sizeof(uint32_t) - 1U) / sizeof(uint32_t))]

/*! @brief The handle of RPMSG adapter */
typedef void *hal_rpmsg_handle_t;

/*! @brief RPMSG status */
typedef enum _hal_rpmsg_status
{
    kStatus_HAL_RpmsgSuccess = 0U,
    kStatus_HAL_RpmsgError,
    kStatus_HAL_RpmsgRxBusy,
    kStatus_HAL_RpmsgTxBusy,
    kStatus_HAL_RpmsgTxIdle,
    kStatus_HAL_RpmsgRxIdle,
    kStatus_HAL_RpmsgTimeout,
} hal_rpmsg_status_t;

/*! @brief RPMSG return status */
typedef enum _hal_rpmsg_return_status
{
    kStatus_HAL_RL_RELEASE = 0U,
    kStatus_HAL_RL_HOLD,
} hal_rpmsg_return_status_t;

/*! @brief The callback function of RPMSG adapter. */
typedef hal_rpmsg_return_status_t (*rpmsg_rx_callback_t)(void *param, uint8_t *data, uint32_t len);

/*! @brief The configure structure of RPMSG adapter. */
typedef struct _hal_rpmsg_config
{
    uint8_t local_addr;           /* Local address for rx */
    uint8_t remote_addr;          /* Remote address for tx */
    rpmsg_rx_callback_t callback; /* RPMSG Rx callback */
    void *param;                  /* RPMSG Rx callback parameter */
    uint8_t imuLink;              /* Link ID to associate with */
} hal_rpmsg_config_t;

/*******************************************************************************
 * API
 ******************************************************************************/

#if defined(__cplusplus)
extern "C" {
#endif /* _cplusplus */

/*!
 * @brief Initializes the RPMSG adapter module for a specific link.
 *
 * @param link_id Link identifier (0 to HAL_RPMSG_MAX_LINKS-1).
 * @param config  Pointer to link configuration.
 *
 * @retval kStatus_HAL_RpmsgSuccess Link initialized successfully.
 * @retval kStatus_HAL_RpmsgError   Initialization failure.
 */
hal_rpmsg_status_t HAL_RpmsgMcmgrInitExt(uint8_t link_id, hal_rpmsg_link_config_t *config);

/*!
 * @brief Initializes the RPMSG adapter module (legacy API).
 *
 * Equivalent to calling HAL_RpmsgMcmgrInitExt for link 0 with default configurations.
 */
hal_rpmsg_status_t HAL_RpmsgMcmgrInit(void);

/*!
 * @brief Initializes the RPMSG adapter for RPMSG channel configuration.
 */
hal_rpmsg_status_t HAL_RpmsgInit(hal_rpmsg_handle_t handle, hal_rpmsg_config_t *config);

/*!
 * @brief Deinitializes the RPMSG adapter module.
 */
hal_rpmsg_status_t HAL_RpmsgDeinit(hal_rpmsg_handle_t handle);

/*!
 * @brief Send data to another RPMSG module with timeout.
 */
hal_rpmsg_status_t HAL_RpmsgSendTimeout(hal_rpmsg_handle_t handle, uint8_t *data, uint32_t length, uint32_t timeout);

/*!
 * @brief Send data to another RPMSG module (blocking).
 */
hal_rpmsg_status_t HAL_RpmsgSend(hal_rpmsg_handle_t handle, uint8_t *data, uint32_t length);

/*!
 * @brief Allocates a TX buffer for zero-copy send with timeout.
 */
void *HAL_RpmsgAllocTxBufferTimeout(hal_rpmsg_handle_t handle, uint32_t size, uint32_t timeout);

/*!
 * @brief Allocates a TX buffer for zero-copy send (blocking).
 */
void *HAL_RpmsgAllocTxBuffer(hal_rpmsg_handle_t handle, uint32_t size);

/*!
 * @brief Send data with zero-copy to another RPMSG module.
 */
hal_rpmsg_status_t HAL_RpmsgNoCopySend(hal_rpmsg_handle_t handle, uint8_t *data, uint32_t length);

/*!
 * @brief Releases the RX buffer for future reuse.
 */
hal_rpmsg_status_t HAL_RpmsgFreeRxBuffer(hal_rpmsg_handle_t handle, uint8_t *data);

/*!
 * @brief Install RPMSG RX callback.
 */
hal_rpmsg_status_t HAL_RpmsgInstallRxCallback(hal_rpmsg_handle_t handle, rpmsg_rx_callback_t callback, void *param);

/*!
 * @brief Prepares to enter low power consumption.
 */
hal_rpmsg_status_t HAL_RpmsgEnterLowpower(hal_rpmsg_handle_t handle);

/*!
 * @brief Prepares to exit low power consumption.
 */
hal_rpmsg_status_t HAL_RpmsgExitLowpower(hal_rpmsg_handle_t handle);

#if defined(__cplusplus)
}
#endif
/*! @}*/
#endif /* __FSL_RPMSG_ADAPTER_H__ */
