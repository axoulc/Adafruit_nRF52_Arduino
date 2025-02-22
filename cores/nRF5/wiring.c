/*
  Copyright (c) 2015 Arduino LLC.  All right reserved.
  Copyright (c) 2016 Sandeep Mistry All right reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "Arduino.h"
#include "nrf.h"
#include "nrf_nvic.h"

nrf_nvic_state_t nrf_nvic_state;

#define DFU_MAGIC_SERIAL_ONLY_RESET   0x4e
#define DFU_MAGIC_UF2_RESET           0x57
#define DFU_MAGIC_OTA_RESET           0xA8

// Must match temp register in bootloader
#define BOOTLOADER_VERSION_REGISTER     NRF_TIMER2->CC[0]
uint32_t bootloaderVersion = 0;
static uint32_t _reset_reason = 0;
static uint32_t _reset_pin = 0;

void init( void )
{
  _reset_reason = NRF_POWER->RESETREAS;

  // clear reset reason: can save it for application usage if needed.
  NRF_POWER->RESETREAS |= NRF_POWER->RESETREAS;

  _reset_pin = NRF_GPIO->IN;

  // Retrieve bootloader version
  bootloaderVersion = BOOTLOADER_VERSION_REGISTER;

  // Select Clock Source : XTAL or RC
#if defined( USE_LFXO )
  // 32Khz XTAL
  NRF_CLOCK->LFCLKSRC = (uint32_t)((CLOCK_LFCLKSRC_SRC_Xtal << CLOCK_LFCLKSRC_SRC_Pos) & CLOCK_LFCLKSRC_SRC_Msk);
#elif defined(USE_LFRC)
  // Internal OSC
  NRF_CLOCK->LFCLKSRC = (uint32_t)((CLOCK_LFCLKSRC_SRC_RC << CLOCK_LFCLKSRC_SRC_Pos) & CLOCK_LFCLKSRC_SRC_Msk);
#else
  #error Clock Source is not configured, define USE_LFXO or USE_LFRC according to your board
#endif

  NRF_CLOCK->TASKS_LFCLKSTART = 1UL;

  // RTC1 could be enabled by bootloader. Disable it
  NVIC_DisableIRQ(RTC1_IRQn);
  NRF_RTC1->EVTENCLR    = RTC_EVTEN_COMPARE0_Msk;
  NRF_RTC1->INTENCLR    = RTC_INTENSET_COMPARE0_Msk;
  NRF_RTC1->TASKS_STOP  = 1;
  NRF_RTC1->TASKS_CLEAR = 1;

  // Make sure all pin is set HIGH when pinmode() is called
  NRF_P0->OUTSET = UINT32_MAX;
#ifdef NRF_P1
  NRF_P1->OUTSET = UINT32_MAX;
#endif
}

uint32_t readResetReason(void)
{
  return _reset_reason;
}

uint32_t readResetPin(void)
{
  return _reset_pin;
}

void enterUf2Dfu(void)
{
  NRF_POWER->GPREGRET = DFU_MAGIC_UF2_RESET;
  NVIC_SystemReset();
}

void enterSerialDfu(void)
{
  NRF_POWER->GPREGRET = DFU_MAGIC_SERIAL_ONLY_RESET;
  NVIC_SystemReset();
}

void enterOTADfu(void)
{
  NRF_POWER->GPREGRET = DFU_MAGIC_OTA_RESET;
  NVIC_SystemReset();
}

void waitForEvent(void)
{
#if 0
  // Set bit 7 and bits 4..0 in the mask to one (0x ...00 1001 1111)
  enum { FPU_EXCEPTION_MASK = 0x0000009F };

  /* Clear exceptions and PendingIRQ from the FPU unit */
  __set_FPSCR(__get_FPSCR()  & ~(FPU_EXCEPTION_MASK));
  (void) __get_FPSCR();
  NVIC_ClearPendingIRQ(FPU_IRQn);
#endif

  uint8_t sd_en = 0;
  (void) sd_softdevice_is_enabled(&sd_en);

#ifdef NRF_CRYPTOCELL
  // manually clear CryptoCell else it could prevent low power mode
  NVIC_ClearPendingIRQ(CRYPTOCELL_IRQn);
#endif

  if ( sd_en )
  {
    (void) sd_app_evt_wait();
  }else
  {
    // SoftDevice is not enabled.
    __WFE();
    __SEV(); // Clear Event Register.
    __WFE();
  }
}


void systemOff(uint32_t pin, uint8_t wake_logic)
{
//  for(int i=0; i<8; i++)
//  {
//    NRF_POWER->RAM[i].POWERCLR = 0x03UL;
//  }

  pin = g_ADigitalPinMap[pin];

  if ( wake_logic )
  {
    nrf_gpio_cfg_sense_input(pin, NRF_GPIO_PIN_PULLDOWN, NRF_GPIO_PIN_SENSE_HIGH);
  }else
  {
    nrf_gpio_cfg_sense_input(pin, NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  }

  uint8_t sd_en;
  (void) sd_softdevice_is_enabled(&sd_en);

  // Enter System OFF state
  if ( sd_en )
  {
    sd_power_system_off();
  }else
  {
    NRF_POWER->SYSTEMOFF = 1;
  }
}


float readCPUTemperature( void )
{
  uint8_t en;
  int32_t temp;
  (void) sd_softdevice_is_enabled(&en);
  if (en) 
  {
    sd_temp_get(&temp);
  }
  else
  {
    NRF_TEMP->EVENTS_DATARDY = 0x00; // Only needed in case another function is also looking at this event flag
    NRF_TEMP->TASKS_START = 0x01; 
  
    while (!NRF_TEMP->EVENTS_DATARDY);
    temp = NRF_TEMP->TEMP;                      // Per anomaly 29 (unclear whether still applicable), TASKS_STOP will clear the TEMP register.

    NRF_TEMP->TASKS_STOP = 0x01;           // Per anomaly 30 (unclear whether still applicable), the temp peripheral needs to be shut down
    NRF_TEMP->EVENTS_DATARDY = 0x00;
  }
  return temp / 4.0F;
}
