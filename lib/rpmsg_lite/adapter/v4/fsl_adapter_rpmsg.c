/*
 * Copyright 2020, 2022-2026 NXP
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fsl_common.h"
#include "rpmsg_lite.h"
#include "fsl_component_generic_list.h"
#include "fsl_adapter_rpmsg.h"
#include "mcmgr.h"
#include <string.h>
#include <assert.h>

/*******************************************************************************
 * Definitions
 ******************************************************************************/

#if defined(__GNUC__)
#define __WEAK_FUNC __attribute__((weak))
#elif defined(__ICCARM__)
#define __WEAK_FUNC __weak
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define __WEAK_FUNC __attribute__((weak))
#elif defined(__DSC__) || defined(__CW__)
#define __WEAK_FUNC __attribute__((weak))
#endif

typedef struct _hal_rpmsg_rx_state
{
    rpmsg_rx_callback_t callback;
    void *param;
} hal_rpmsg_rx_state_t;

typedef struct _hal_rpmsg_state
{
    uint8_t local_addr;
    uint8_t remote_addr;
    volatile uint8_t rpmsg_lite_peer_ept_is_ready;
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
    struct rpmsg_lite_ept_static_context endpoint;
#endif
    struct rpmsg_lite_endpoint *pEndpoint;
    hal_rpmsg_rx_state_t rx;
    uint8_t link_id;
} hal_rpmsg_state_t;

typedef struct _hal_rpmsg_peer_ept_state
{
    list_element_t link;
    hal_rpmsg_state_t *rpmsgHandle;
} hal_rpmsg_peer_ept_state_t;

typedef struct _hal_rpmsg_link_state
{
    struct rpmsg_lite_instance *rpmsgContext;
#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
    struct rpmsg_lite_instance context;
#endif
    list_label_t rpmsgEpList;
    hal_rpmsg_peer_ept_state_t rpmsgPeerEptStat[MAX_EP_COUNT];
    uint8_t rpmsgPeerEptData[MAX_EP_COUNT];
    volatile uint8_t peerRpmsgEptCount;
    volatile uint16_t RPMsgRemoteReadyEventData;
    int32_t rpmsgEptCount; /* -1 = not initialized */
    uint8_t rpmsg_init_global;
    uint8_t mcmgr_hw_started;
    uint32_t peer_core;
    uint8_t role;
} hal_rpmsg_link_state_t;

/*******************************************************************************
 * Shared memory allocation (Default)
 ******************************************************************************/

#ifndef RPMSG_GLOBAL_VARIABLE_ALLOC
#if (defined(HAL_RPMSG_SELECT_ROLE) && (HAL_RPMSG_SELECT_ROLE == 0U))
#ifndef SH_MEM_TOTAL_SIZE
#define SH_MEM_TOTAL_SIZE (2U * RL_BUFFER_COUNT * (RL_WORD_ALIGN_UP(RL_BUFFER_PAYLOAD_SIZE + \
                           sizeof(struct rpmsg_std_hdr))) + RL_VRING_OVERHEAD)
#endif

#if defined(__ICCARM__)
#pragma location = "rpmsg_sh_mem_section"
static char rpmsg_lite_base[SH_MEM_TOTAL_SIZE];
#elif defined(__CC_ARM) || defined(__ARMCC_VERSION)
static char rpmsg_lite_base[SH_MEM_TOTAL_SIZE] __attribute__((section("rpmsg_sh_mem_section")));
#elif defined(__GNUC__)
static char rpmsg_lite_base[SH_MEM_TOTAL_SIZE] __attribute__((section(".noinit.$rpmsg_sh_mem")));
#else
/* Application must provide memory if using default HAL_RpmsgMcmgrInit */
#endif
#endif
#endif

extern uint32_t rpmsg_sh_mem_start[];
extern uint32_t rpmsg_sh_mem_end[];

/*******************************************************************************
 * Internal constants
 ******************************************************************************/

#define RPMSG_LITE_LINK_ID (0U)
#define APP_RPMSG_READY_EVENT_DATA    (1U)
#define APP_RPMSG_EP_READY_EVENT_DATA (2U)

#ifndef RPMSG_REMOTE_READY_RETRY_COUNT
#define RPMSG_REMOTE_READY_RETRY_COUNT 10000000U
#endif

/*******************************************************************************
 * Static state
 ******************************************************************************/

static hal_rpmsg_link_state_t s_rpmsg_links[HAL_RPMSG_MAX_LINKS];
static uint8_t s_mcmgr_event_registered = 0U;
static uint8_t s_mcmgr_initialized = 0U;

/*******************************************************************************
 * Weak helpers
 ******************************************************************************/

__WEAK_FUNC void RPMsg_EpReadyTimeDelay(uint32_t ms);
__WEAK_FUNC void RPMsg_EpReadyTimeDelay(uint32_t ms)
{
    (void)ms;
}

/*******************************************************************************
 * Internal: endpoint read callback
 ******************************************************************************/

static int32_t rpmsg_ept_read_cb(void *payload, uint32_t payload_len, uint32_t src, void *priv)
{
    hal_rpmsg_state_t *rpmsgHandle = (hal_rpmsg_state_t *)priv;
    assert(NULL != rpmsgHandle);
    assert(NULL != rpmsgHandle->rx.callback);
    return rpmsgHandle->rx.callback(rpmsgHandle->rx.param, payload, payload_len);
}

/*******************************************************************************
 * Internal: MCMGR event handler
 ******************************************************************************/

static void RPMsgPeerReadyEventHandler(mcmgr_core_t coreNum, uint16_t eventData, void *context)
{
    uint8_t link_idx;
    hal_rpmsg_link_state_t *link;
    list_element_handle_t list_element;
    hal_rpmsg_peer_ept_state_t *rpmsgPeerEptState;
    uint8_t address = 0U;

    (void)context;

    for (link_idx = 0U; link_idx < HAL_RPMSG_MAX_LINKS; link_idx++)
    {
        link = &s_rpmsg_links[link_idx];
        /* Check if link is being initialized (rpmsg_init_global set) and matches peer_core */
        if (link->rpmsg_init_global && link->peer_core == (uint32_t)coreNum)
        {
            /* Check if this is an EP_READY notification */
            if ((eventData & 0xFF00U) == ((uint16_t)APP_RPMSG_EP_READY_EVENT_DATA << 8U))
            {
                address = (uint8_t)(eventData & 0xFFU);
                if (link->peerRpmsgEptCount < MAX_EP_COUNT)
                {
                    link->rpmsgPeerEptData[link->peerRpmsgEptCount] = address;
                }
                link->peerRpmsgEptCount++;

                list_element = LIST_GetHead(&link->rpmsgEpList);
                while (NULL != list_element)
                {
                    rpmsgPeerEptState = (hal_rpmsg_peer_ept_state_t *)(void *)list_element;
                    if (rpmsgPeerEptState->rpmsgHandle->remote_addr == address)
                    {
                        rpmsgPeerEptState->rpmsgHandle->rpmsg_lite_peer_ept_is_ready = 1U;
                    }
                    list_element = LIST_GetNext(list_element);
                }
            }

            /* Store raw event data */
            link->RPMsgRemoteReadyEventData = eventData;
            break;
        }
    }
}

/*******************************************************************************
 * HAL_RpmsgMcmgrInitExt
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgMcmgrInitExt(uint8_t link_id, hal_rpmsg_link_config_t *config)
{
    hal_rpmsg_link_state_t *link;

    if (link_id >= HAL_RPMSG_MAX_LINKS || config == NULL)
    {
        return kStatus_HAL_RpmsgError;
    }

    link = &s_rpmsg_links[link_id];

    if (link->rpmsgContext != NULL)
    {
        return kStatus_HAL_RpmsgSuccess;
    }

    if (0U == s_mcmgr_event_registered)
    {
        s_mcmgr_event_registered = 1U;
        (void)MCMGR_RegisterEvent(kMCMGR_RemoteApplicationEvent,
                                  RPMsgPeerReadyEventHandler,
                                  NULL);
    }

    if (0U == link->rpmsg_init_global)
    {
        link->rpmsg_init_global = 1U;
        link->peer_core = config->peer_core;
        link->role = (uint8_t)config->role;
        LIST_Init((&link->rpmsgEpList), 0);
    }

    if (0U == s_mcmgr_initialized)
    {
        s_mcmgr_initialized = 1U;
        (void)MCMGR_Init();
    }

    if (config->boot_peer_core && 0U == link->mcmgr_hw_started)
    {
        link->mcmgr_hw_started = 1U;
        if (MCMGR_StartCore((mcmgr_core_t)config->peer_core,
                            (void *)(char *)REMOTE_CORE_BOOT_ADDRESS,
                            2U,
                            kMCMGR_Start_Asynchronous) != kStatus_MCMGR_Success)
        {
            return kStatus_HAL_RpmsgError;
        }
    }

    if (config->role == 0U) /* MASTER */
    {
        volatile uint32_t timeout = RPMSG_REMOTE_READY_RETRY_COUNT;
        while ((APP_RPMSG_READY_EVENT_DATA != link->RPMsgRemoteReadyEventData) && (--timeout != 0U))
        {
        }
        if (0U == timeout)
        {
            return kStatus_HAL_RpmsgTimeout;
        }

#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
        link->rpmsgContext = rpmsg_lite_master_init(config->sh_mem_base, config->sh_mem_size,
                                                   RPMSG_LITE_LINK_ID, RL_NO_FLAGS, &link->context);
#else
        link->rpmsgContext = rpmsg_lite_master_init(config->sh_mem_base, config->sh_mem_size,
                                                   RPMSG_LITE_LINK_ID, RL_NO_FLAGS);
#endif
    }
    else /* REMOTE */
    {
        uint32_t startupData;
        mcmgr_status_t mcmgrStatus;
        do
        {
            mcmgrStatus = MCMGR_GetStartupData((mcmgr_core_t)config->peer_core, &startupData);
        } while (mcmgrStatus != kStatus_MCMGR_Success);

#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
        link->rpmsgContext = rpmsg_lite_remote_init(config->sh_mem_start,
                                                   RPMSG_LITE_LINK_ID, RL_NO_FLAGS, &link->context);
#else
        link->rpmsgContext = rpmsg_lite_remote_init(config->sh_mem_start,
                                                   RPMSG_LITE_LINK_ID, RL_NO_FLAGS);
#endif

        if (RL_NULL == link->rpmsgContext)
        {
            return kStatus_HAL_RpmsgError;
        }

        (void)MCMGR_TriggerEvent((mcmgr_core_t)config->peer_core,
                                 kMCMGR_RemoteApplicationEvent,
                                 APP_RPMSG_READY_EVENT_DATA);

        do
        {
        } while (RL_TRUE != rpmsg_lite_is_link_up(link->rpmsgContext));
    }

    if (RL_NULL == link->rpmsgContext)
    {
        return kStatus_HAL_RpmsgError;
    }

    link->rpmsgEptCount = 0;
    return kStatus_HAL_RpmsgSuccess;
}

/*******************************************************************************
 * HAL_RpmsgMcmgrInit
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgMcmgrInit(void)
{
    hal_rpmsg_link_config_t config;
    config.role = HAL_RPMSG_SELECT_ROLE;
    config.peer_core = (uint32_t)HAL_RPMSG_PEER_CORE;
    config.boot_peer_core = HAL_RPMSG_BOOT_PEER_CORE;
#if (HAL_RPMSG_SELECT_ROLE == 0U)
    config.sh_mem_base = (void *)rpmsg_lite_base;
    config.sh_mem_size = SH_MEM_TOTAL_SIZE;
    config.sh_mem_start = NULL;
#else
    config.sh_mem_base = NULL;
    config.sh_mem_size = 0;
    config.sh_mem_start = (void *)rpmsg_sh_mem_start;
#endif
    return HAL_RpmsgMcmgrInitExt(0, &config);
}

/*******************************************************************************
 * HAL_RpmsgInit
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgInit(hal_rpmsg_handle_t handle, hal_rpmsg_config_t *config)
{
    hal_rpmsg_state_t *rpmsgHandle;
    hal_rpmsg_link_state_t *link;
    uint8_t count = 0U;

    assert(HAL_RPMSG_HANDLE_SIZE >= sizeof(hal_rpmsg_state_t));
    assert(NULL != handle);
    assert(NULL != config);
    assert(config->imuLink < HAL_RPMSG_MAX_LINKS);

    link = &s_rpmsg_links[config->imuLink];
    assert(link->rpmsgContext != NULL);

    rpmsgHandle = (hal_rpmsg_state_t *)handle;
    rpmsgHandle->link_id = config->imuLink;
    rpmsgHandle->rpmsg_lite_peer_ept_is_ready = 0U;
    rpmsgHandle->local_addr  = config->local_addr;
    rpmsgHandle->remote_addr = config->remote_addr;
    rpmsgHandle->rx.callback = NULL;
    rpmsgHandle->rx.param    = NULL;

#if defined(RL_USE_STATIC_API) && (RL_USE_STATIC_API == 1)
    rpmsgHandle->pEndpoint = rpmsg_lite_create_ept(link->rpmsgContext,
                                                   rpmsgHandle->local_addr,
                                                   rpmsg_ept_read_cb,
                                                   rpmsgHandle,
                                                   &rpmsgHandle->endpoint);
#else
    rpmsgHandle->pEndpoint = rpmsg_lite_create_ept(link->rpmsgContext,
                                                   rpmsgHandle->local_addr,
                                                   rpmsg_ept_read_cb,
                                                   rpmsgHandle);
#endif
    if (RL_NULL == rpmsgHandle->pEndpoint)
    {
        return kStatus_HAL_RpmsgError;
    }

    rpmsgHandle->rx.callback = config->callback;
    rpmsgHandle->rx.param    = config->param;

    (void)MCMGR_TriggerEvent((mcmgr_core_t)link->peer_core,
                             kMCMGR_RemoteApplicationEvent,
                             (uint16_t)((uint16_t)APP_RPMSG_EP_READY_EVENT_DATA << 8U) |
                                 (uint16_t)config->local_addr);

    link->rpmsgPeerEptStat[link->rpmsgEptCount].rpmsgHandle = rpmsgHandle;
    (void)LIST_AddTail(&link->rpmsgEpList,
                       (list_element_handle_t)&link->rpmsgPeerEptStat[link->rpmsgEptCount]);
    link->rpmsgEptCount++;

    while (count < link->peerRpmsgEptCount)
    {
        if (rpmsgHandle->remote_addr == link->rpmsgPeerEptData[count])
        {
            rpmsgHandle->rpmsg_lite_peer_ept_is_ready = 1U;
        }
        count++;
    }

    return kStatus_HAL_RpmsgSuccess;
}

/*******************************************************************************
 * HAL_RpmsgDeinit
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgDeinit(hal_rpmsg_handle_t handle)
{
    hal_rpmsg_state_t *rpmsgHandle;
    hal_rpmsg_link_state_t *link;
    uint8_t i;

    assert(NULL != handle);
    rpmsgHandle = (hal_rpmsg_state_t *)handle;
    link = &s_rpmsg_links[rpmsgHandle->link_id];

    if (link->rpmsgEptCount > 0)
    {
        (void)rpmsg_lite_destroy_ept(link->rpmsgContext, rpmsgHandle->pEndpoint);
        rpmsgHandle->pEndpoint = NULL;

        for (i = 0U; i < MAX_EP_COUNT; i++)
        {
            if (link->rpmsgPeerEptStat[i].rpmsgHandle == rpmsgHandle)
            {
                (void)LIST_RemoveElement((list_element_handle_t)&link->rpmsgPeerEptStat[i]);
                (void)memset(&link->rpmsgPeerEptStat[i].link, 0, sizeof(link->rpmsgPeerEptStat[i].link));
                link->rpmsgPeerEptStat[i].rpmsgHandle = NULL;
                break;
            }
        }
        link->rpmsgEptCount--;
    }

    if (0 == link->rpmsgEptCount)
    {
        link->rpmsgEptCount = -1;
        link->RPMsgRemoteReadyEventData = 0U;
        link->peerRpmsgEptCount = 0U;
        (void)memset((void *)link->rpmsgPeerEptData, 0, sizeof(link->rpmsgPeerEptData));
        (void)rpmsg_lite_deinit(link->rpmsgContext);
        link->rpmsgContext = NULL;
        for (i = 0U; i < MAX_EP_COUNT; i++)
        {
            link->rpmsgPeerEptStat[i].rpmsgHandle = NULL;
        }
    }

    return kStatus_HAL_RpmsgSuccess;
}

/*******************************************************************************
 * HAL_RpmsgSendTimeout
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgSendTimeout(hal_rpmsg_handle_t handle, uint8_t *data, uint32_t length, uint32_t timeout)
{
    hal_rpmsg_state_t *rpmsgHandle;
    hal_rpmsg_link_state_t *link;
    hal_rpmsg_status_t status = kStatus_HAL_RpmsgSuccess;
    int32_t rpmsgStatus;
    volatile uint32_t epTimeout = RPMSG_REMOTE_READY_RETRY_COUNT;
#if defined(HDI_MODE) && (HDI_MODE == 1)
    uint32_t primask;
#endif

    assert(NULL != handle);
    assert(NULL != data);

    rpmsgHandle = (hal_rpmsg_state_t *)handle;
    link = &s_rpmsg_links[rpmsgHandle->link_id];

    /* Wait for peer endpoint to be ready */
    while ((0U == rpmsgHandle->rpmsg_lite_peer_ept_is_ready) && (--epTimeout != 0U))
    {
    }
    if (0U == epTimeout)
    {
        return kStatus_HAL_RpmsgTimeout;
    }

#if defined(HDI_MODE) && (HDI_MODE == 1)
    primask = DisableGlobalIRQ();
#endif

    do
    {
        if (RL_TRUE != rpmsg_lite_is_link_up(link->rpmsgContext))
        {
            status = kStatus_HAL_RpmsgError;
            break;
        }

        rpmsgStatus = rpmsg_lite_send(link->rpmsgContext,
                                      rpmsgHandle->pEndpoint,
                                      rpmsgHandle->remote_addr,
                                      (char *)data,
                                      length,
                                      timeout);
        if (RL_SUCCESS != rpmsgStatus)
        {
            status = (RL_ERR_NO_MEM == rpmsgStatus)
                         ? kStatus_HAL_RpmsgTimeout
                         : kStatus_HAL_RpmsgError;
            break;
        }
    } while (false);

#if defined(HDI_MODE) && (HDI_MODE == 1)
    EnableGlobalIRQ(primask);
#endif

    return status;
}

/*******************************************************************************
 * HAL_RpmsgSend
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgSend(hal_rpmsg_handle_t handle, uint8_t *data, uint32_t length)
{
    return HAL_RpmsgSendTimeout(handle, data, length, RPMSG_WAITFOREVER);
}

/*******************************************************************************
 * HAL_RpmsgAllocTxBuffer / HAL_RpmsgAllocTxBufferTimeout
 ******************************************************************************/

void *HAL_RpmsgAllocTxBuffer(hal_rpmsg_handle_t handle, uint32_t size)
{
    return HAL_RpmsgAllocTxBufferTimeout(handle, size, RPMSG_WAITFOREVER);
}

void *HAL_RpmsgAllocTxBufferTimeout(hal_rpmsg_handle_t handle, uint32_t size, uint32_t timeout)
{
    hal_rpmsg_state_t *rpmsgHandle;
    hal_rpmsg_link_state_t *link;
    void *buf = NULL;
#if defined(HDI_MODE) && (HDI_MODE == 1)
    uint32_t primask;
    primask = DisableGlobalIRQ();
#endif

    assert(NULL != handle);
    rpmsgHandle = (hal_rpmsg_state_t *)handle;
    link = &s_rpmsg_links[rpmsgHandle->link_id];
    buf = rpmsg_lite_alloc_tx_buffer(link->rpmsgContext, &size, timeout);

#if defined(HDI_MODE) && (HDI_MODE == 1)
    EnableGlobalIRQ(primask);
#endif
    return buf;
}

/*******************************************************************************
 * HAL_RpmsgFreeRxBuffer
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgFreeRxBuffer(hal_rpmsg_handle_t handle, uint8_t *data)
{
    hal_rpmsg_state_t *rpmsgHandle;
    hal_rpmsg_link_state_t *link;
    hal_rpmsg_status_t status = kStatus_HAL_RpmsgSuccess;
#if defined(HDI_MODE) && (HDI_MODE == 1)
    uint32_t primask;
    primask = DisableGlobalIRQ();
#endif

    assert(NULL != handle);
    rpmsgHandle = (hal_rpmsg_state_t *)handle;
    link = &s_rpmsg_links[rpmsgHandle->link_id];

    if (RL_SUCCESS != rpmsg_lite_release_rx_buffer(link->rpmsgContext, data))
    {
        status = kStatus_HAL_RpmsgError;
    }

#if defined(HDI_MODE) && (HDI_MODE == 1)
    EnableGlobalIRQ(primask);
#endif
    return status;
}

/*******************************************************************************
 * HAL_RpmsgNoCopySend
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgNoCopySend(hal_rpmsg_handle_t handle, uint8_t *data, uint32_t length)
{
    hal_rpmsg_state_t *rpmsgHandle;
    hal_rpmsg_link_state_t *link;
    hal_rpmsg_status_t status = kStatus_HAL_RpmsgSuccess;
    volatile uint32_t epTimeout = RPMSG_REMOTE_READY_RETRY_COUNT;
#if defined(HDI_MODE) && (HDI_MODE == 1)
    uint32_t primask;
#endif

    assert(NULL != handle);
    assert(NULL != data);

    rpmsgHandle = (hal_rpmsg_state_t *)handle;
    link = &s_rpmsg_links[rpmsgHandle->link_id];

    /* Wait for peer endpoint to be ready */
    while ((0U == rpmsgHandle->rpmsg_lite_peer_ept_is_ready) && (--epTimeout != 0U))
    {
    }
    if (0U == epTimeout)
    {
        return kStatus_HAL_RpmsgTimeout;
    }

#if defined(HDI_MODE) && (HDI_MODE == 1)
    primask = DisableGlobalIRQ();
#endif

    do
    {
        if (RL_TRUE != rpmsg_lite_is_link_up(link->rpmsgContext))
        {
            status = kStatus_HAL_RpmsgError;
            break;
        }

        if (RL_SUCCESS != rpmsg_lite_send_nocopy(link->rpmsgContext,
                                                 rpmsgHandle->pEndpoint,
                                                 rpmsgHandle->remote_addr,
                                                 (char *)data, length))
        {
            status = kStatus_HAL_RpmsgError;
            break;
        }
    } while (false);

#if defined(HDI_MODE) && (HDI_MODE == 1)
    EnableGlobalIRQ(primask);
#endif

    return status;
}

/*******************************************************************************
 * HAL_RpmsgInstallRxCallback
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgInstallRxCallback(hal_rpmsg_handle_t handle, rpmsg_rx_callback_t callback, void *param)
{
    hal_rpmsg_state_t *rpmsgHandle;
    assert(NULL != handle);
    rpmsgHandle = (hal_rpmsg_state_t *)handle;
    rpmsgHandle->rx.callback = callback;
    rpmsgHandle->rx.param    = param;
    return kStatus_HAL_RpmsgSuccess;
}

/*******************************************************************************
 * HAL_RpmsgEnterLowpower / HAL_RpmsgExitLowpower
 ******************************************************************************/

hal_rpmsg_status_t HAL_RpmsgEnterLowpower(hal_rpmsg_handle_t handle)
{
    (void)handle;
    return kStatus_HAL_RpmsgError;
}

hal_rpmsg_status_t HAL_RpmsgExitLowpower(hal_rpmsg_handle_t handle)
{
    (void)handle;
    return kStatus_HAL_RpmsgError;
}
