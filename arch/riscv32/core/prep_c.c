/*
 * Copyright (c) 2016 Jean-Paul Etienne <fractalclone@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Full C support initialization
 *
 *
 * Initialization of full C support: zero the .bss and call _Cstart().
 *
 * Stack is available in this module, but not the global data/bss until their
 * initialization is performed.
 */

#include <stddef.h>
#include <toolchain.h>
#include <kernel_structs.h>
#include <nano_internal.h>

#include "prci.h"
#include "spi.h"
#include "aon.h"
#include "clint.h"

#include "platform.h"


static unsigned long mtime_lo(void)
{
  return *(volatile unsigned long *)(CLINT_BASE_ADDR + CLINT_MTIME);
}


static void use_hfrosc(int div, int trim)
{
  // Make sure the HFROSC is running at its default setting
  PRCI_REG(PRCI_HFROSCCFG) = (ROSC_DIV(div) | ROSC_TRIM(trim) | ROSC_EN(1));
  while ((PRCI_REG(PRCI_HFROSCCFG) & ROSC_RDY(1)) == 0) ;
  PRCI_REG(PRCI_PLLCFG) &= ~PLL_SEL(1);
}

static void use_pll(int refsel, int bypass, int r, int f, int q)
{
  // Ensure that we aren't running off the PLL before we mess with it.
  if (PRCI_REG(PRCI_PLLCFG) & PLL_SEL(1)) {
    // Make sure the HFROSC is running at its default setting
    use_hfrosc(4, 16);
  }

  // Set PLL Source to be HFXOSC if available.
  uint32_t config_value = 0;

  config_value |= PLL_REFSEL(refsel);

  if (bypass) {
    // Bypass
    config_value |= PLL_BYPASS(1);

    PRCI_REG(PRCI_PLLCFG) = config_value;

    // If we don't have an HFXTAL, this doesn't really matter.
    // Set our Final output divide to divide-by-1:
    PRCI_REG(PRCI_PLLDIV) = (PLL_FINAL_DIV_BY_1(1) | PLL_FINAL_DIV(0));
  } else {
    // In case we are executing from QSPI,
    // (which is quite likely) we need to
    // set the QSPI clock divider appropriately
    // before boosting the clock frequency.

    // Div = f_sck/2
    SPI0_REG(SPI_REG_SCKDIV) = 8;

    // Set DIV Settings for PLL
    // Both HFROSC and HFXOSC are modeled as ideal
    // 16MHz sources (assuming dividers are set properly for
    // HFROSC).
    // (Legal values of f_REF are 6-48MHz)

    // Set DIVR to divide-by-2 to get 8MHz frequency
    // (legal values of f_R are 6-12 MHz)

    config_value |= PLL_BYPASS(1);
    config_value |= PLL_R(r);

    // Set DIVF to get 512Mhz frequncy
    // There is an implied multiply-by-2, 16Mhz.
    // So need to write 32-1
    // (legal values of f_F are 384-768 MHz)
    config_value |= PLL_F(f);

    // Set DIVQ to divide-by-2 to get 256 MHz frequency
    // (legal values of f_Q are 50-400Mhz)
    config_value |= PLL_Q(q);

    // Set our Final output divide to divide-by-1:
    PRCI_REG(PRCI_PLLDIV) = (PLL_FINAL_DIV_BY_1(1) | PLL_FINAL_DIV(0));

    PRCI_REG(PRCI_PLLCFG) = config_value;

    // Un-Bypass the PLL.
    PRCI_REG(PRCI_PLLCFG) &= ~PLL_BYPASS(1);

    // Wait for PLL Lock
    // Note that the Lock signal can be glitchy.
    // Need to wait 100 us
    // RTC is running at 32kHz.
    // So wait 4 ticks of RTC.
    uint32_t now = mtime_lo();
    while (mtime_lo() - now < 4) ;

    // Now it is safe to check for PLL Lock
    while ((PRCI_REG(PRCI_PLLCFG) & PLL_LOCK(1)) == 0) ;
  }

  // Switch over to PLL Clock source
  PRCI_REG(PRCI_PLLCFG) |= PLL_SEL(1);
}

static void use_default_clocks()
{
  // Turn off the LFROSC
  AON_REG(AON_LFROSC) &= ~ROSC_EN(1);

  // Use HFROSC
  use_hfrosc(4, 16);
}


/**
 *
 * @brief Prepare to and run C code
 *
 * This routine prepares for the execution of and runs C code.
 *
 * @return N/A
 */

void _PrepC(void)
{
	_bss_zero();
#ifdef CONFIG_XIP
	_data_copy();
#endif
	/* hacky hardware init */
	use_default_clocks();
	use_pll(0, 0, 1, 31, 1);

	_Cstart();

	CODE_UNREACHABLE;
}
