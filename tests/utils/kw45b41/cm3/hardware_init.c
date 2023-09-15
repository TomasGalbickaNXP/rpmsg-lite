/*
 * Copyright 2020 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/*${header:start}*/
#include "pin_mux.h"
#include "board.h"
/*${header:end}*/

static const uint32_t froPostDivFreq[] = {
    16000000U, 24000000U, 32000000U, 48000000U, 64000000U
};
/*${function:start}*/
void BOARD_InitHardware(void)
{
    uint32_t froPostDivSel = (FRO192M0->FROCCSR & FRO192M_FROCCSR_POSTDIV_SEL_MASK) >> FRO192M_FROCCSR_POSTDIV_SEL_SHIFT;

    SystemCoreClock = froPostDivFreq[froPostDivSel];
}
/*${function:end}*/
