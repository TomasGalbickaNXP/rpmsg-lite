# RPMSG Adapter V4 — Multicore Optimized Implementation

## Overview

The V4 RPMSG adapter is a major architectural improvement over V2, specifically designed for multicore applications where a single core needs to communicate with multiple peer cores simultaneously using `rpmsg-lite`.

While V2 fixed many bugs and added core-agnostic configuration, it still relied on global state, which limited it to a single peer-core communication link at a time. V4 refactors this state into a per-link structure, allowing multiple concurrent links.

## Key Improvements in V4

1.  **Multi-Link Support**: State is now encapsulated in `hal_rpmsg_link_state_t`. An array of these structures (`s_rpmsg_links`) allows supporting multiple peer cores (configurable via `HAL_RPMSG_MAX_LINKS`).
2.  **Explicit Link Initialization**: New `HAL_RpmsgMcmgrInitExt()` API allows initializing a specific link ID with a custom configuration (`hal_rpmsg_link_config_t`).
3.  **Backward Compatibility**: The legacy `HAL_RpmsgMcmgrInit()` API is preserved and maps to Link 0 using default configurations, ensuring zero breakage for existing applications.
4.  **Flexible Association**: Endpoints are associated with a specific link via the `imuLink` field in `hal_rpmsg_config_t`.

## New APIs

### `HAL_RpmsgMcmgrInitExt(uint8_t link_id, hal_rpmsg_link_config_t *config)`

Initializes a specific RPMSG link.
- `link_id`: Index from 0 to `HAL_RPMSG_MAX_LINKS - 1`.
- `config`: Configuration including role, peer core, shared memory details, and boot behavior.

### `hal_rpmsg_link_config_t`

```c
typedef struct _hal_rpmsg_link_config
{
    uint32_t role;          /* 0 - Master, 1 - Remote */
    uint32_t peer_core;     /* Peer core identifier (mcmgr_core_t) */
    void *sh_mem_base;      /* Shared memory base address */
    uint32_t sh_mem_size;   /* Shared memory size */
    void *sh_mem_start;     /* Shared memory start for remote (optional) */
    uint8_t boot_peer_core; /* 1 - Boot peer core, 0 - Don't boot */
} hal_rpmsg_link_config_t;
```

## Migration Guide (V2 → V4)

### Single-Link Application
No changes required. `HAL_RpmsgMcmgrInit()` and existing `HAL_RpmsgInit()` calls will continue to work using Link 0.

### Multi-Link Application
To talk to multiple cores (e.g., Core0 talking to both Core1 and Core2):

1.  Initialize both links:
    ```c
    hal_rpmsg_link_config_t config1 = { .peer_core = kMCMGR_Core1, ... };
    hal_rpmsg_link_config_t config2 = { .peer_core = kMCMGR_Core2, ... };

    HAL_RpmsgMcmgrInitExt(0, &config1);
    HAL_RpmsgMcmgrInitExt(1, &config2);
    ```

2.  Initialize endpoints for each link:
    ```c
    hal_rpmsg_config_t ept_cfg1 = { .imuLink = 0, ... };
    hal_rpmsg_config_t ept_cfg2 = { .imuLink = 1, ... };

    HAL_RpmsgInit(handle1, &ept_cfg1);
    HAL_RpmsgInit(handle2, &ept_cfg2);
    ```

3.  The adapter handles routing internally based on the link ID stored in the handle.

## Design Notes

- **MCMGR Event Dispatching**: The unified `RPMsgPeerReadyEventHandler` now uses the `coreNum` parameter provided by MCMGR to determine which internal link state should receive the event.
- **Race Condition Protection**: Inherits the V2 fix where MCMGR events are registered *before* hardware initialization to ensure no handshake signals are lost.
