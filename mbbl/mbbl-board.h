/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


/*= Board-specific parameters below this line ===================*/

/* DDR RAM */
#define DDR_BASEADDR XPAR_MPMC_0_MPMC_BASEADDR
#define DDR_HIGHADDR XPAR_MPMC_0_MPMC_HIGHADDR

/* End of usable memory -- use for stack and heap */
#define DDR_END (DDR_HIGHADDR+1)

/* Console address */
#define SERIAL_CONSOLE_BASEADDR XPAR_SERIAL_CONSOLE_BASEADDR

/* Timer */
#define TIMER_BASEADDR XPAR_XPS_TIMER_0_BASEADDR
#define CPU_DPLB_FREQ_HZ XPAR_CPU_DPLB_FREQ_HZ

#ifndef FLASH_IS_BPI
#define FLASH_IS_BPI 0
#endif

#if FLASH_IS_BPI
/* BPI flash */
#define BPI_FLASH_BASEADDR XPAR_FLASH_BPI_1_MEM0_BASEADDR
#define BPI_FLASH_HIGHADDR XPAR_FLASH_BPI_1_MEM0_HIGHADDR
#else
/* SPI flash */
#define SPI_FLASH_BASEADDR XPAR_XPS_SPI_2_BASEADDR
#define SPI_FLASH_TRANSFER_BITS XPAR_XPS_SPI_2_NUM_TRANSFER_BITS
#define SPI_FLASH_FIFO_EXIST XPAR_XPS_SPI_2_FIFO_EXIST
#define SPI_FLASH_NUM_SS_BITS XPAR_XPS_SPI_2_NUM_SS_BITS
#define SPI_FLASH_SIZE 0x1000000

#define SPI_FLASH_SLAVE_SELECT_MASK ((1<<SPI_FLASH_NUM_SS_BITS)-1)
#endif

#define HARDWARE_CAL_ICS

#ifdef HARDWARE_DMITRI_IO
#define GPIO_OLED_BUTTON_DEVICE_ID  XPAR_XPS_GPIO_0_DEVICE_ID
#define GPIO_OLED_BUTTON_BASEADDR   XPAR_XPS_GPIO_0_BASEADDR
#define SPI_OLED_BUTTON_DEVICE_ID   XPAR_OLED_SPI_0_DEVICE_ID
#define SPI_OLED_BUTTON_BASEADDR    XPAR_OLED_SPI_0_BASEADDR

#define GPIO_OLED_CHANNEL           1
#define GPIO_OLED_DIRECTION_MASK    0x00000000
#define GPIO_OLED_RESET_MASK        0x00
#define GPIO_OLED_UN_RESET_MASK     0x80
#define GPIO_OLED_DATA_MASK         0xc0
#define GPIO_OLED_BUTTON_IN_MASK    0x80

#else
#ifdef HARDWARE_CAL_ICS

/*
      GPIO MAP PORT 1

BIT    OUT                         IN
 0  --                     JUMPERS(0)
 1  --                     JUMPERS(1)
 2  --                     JUMPERS(2)
 3  --                     JUMPERS(3)
 4  SCaRF_DSP_ADDR(0)      --
 5  SCaRF_DSP_ADDR(1)      --
 6  SCaRF_DSP_ADDR(2)      --
 7  SCaRF_DSP_ADDR(3)      --
 8  LED(1)                 --
 9  LED(2)                 --
 10 LED(3)                 --
 11 LED(4)                 --
 12 LED(5)                 --
 13 LED(6)                 --
 14 LED(7)                 --
 15 LED(8)                 -- 
 16 LED(9)                 --
 17 LED(10)                --
 18 LED(11)                --
 19 LED(12)                --
 20 PHY_RESET_n            --
 21 --                     PHY_INT_n
 22 DEBUG_LED              --


      GPIO MAP PORT 2

BIT    OUT                         IN
 0  SCaRF_DSP_RESETn        --
 1  SCaRF_DSP_LOCAL_RESETn  --
 2  SCaRF_DSP_SYNC          --
 3  FAULT_OUT               --
 4  SMART_SWITCH_RST        --
 5  SMART_SWITCH_D_C        --
 6  --                      SMART_SWITCH_CONTACT
 7  --                      SCaRF_DSP_HSn
 8  --                      AES_RX_LOCKn
 9  --                      AES_INTn
 10 SMART_SWITCH_SPARE_1    -- 
    (actually smart switch screen power)
 11 SMART_SWITCH_SPARE_2    --
 12 --                      ANALOG_IN_1_CLIP
 13 --                      ANALOG_IN_2_CLIP
 14 --                      ANALOG_IN_3_CLIP
 15 --                      ANALOG_IN_4_CLIP
 16 --                      CONTACT_SENSOR_1
 17 --                      CONTACT_SENSOR_2
 18 --                      CONTACT_SENSOR_3
 19 --                      CONTACT_SENSOR_4
 20 ADC_SAMPLE_RATE_SEL     --
 21 CONVERTER_RSTn          --
 22 --                      --
*/

#define GPIO_OLED_BUTTON_DEVICE_ID  XPAR_XPS_GPIO_0_DEVICE_ID
#define GPIO_OLED_BUTTON_BASEADDR   XPAR_XPS_GPIO_0_BASEADDR
#define SPI_OLED_BUTTON_DEVICE_ID   XPAR_XPS_SPI_3_DEVICE_ID
#define SPI_OLED_BUTTON_BASEADDR    XPAR_XPS_SPI_3_BASEADDR
#define SPI_LED_LATCH_DEVICE_ID     XPAR_XPS_SPI_4_DEVICE_ID
#define SPI_LED_LATCH_BASEADDR      XPAR_XPS_SPI_4_BASEADDR

#define GPIO_OLED_CHANNEL           2


/*
  GPIO bits mapping:


x x x x   x x x x   x 0 0 0   0 0 0 0   0 0 0 1   1 1 1 1   1 1 1 1   1 2 1 2
x x x x   x x x x   x 0 1 2   3 4 5 6   7 8 9 0   1 2 3 4   5 6 7 8   9 0 1 2

0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0   0 0 0 0
*/

/* bits 6-9,12-19 */
#define GPIO_OLED_DIRECTION_MASK    0x0001e7f8
/* bits 6-19 */
//#define GPIO_OLED_DIRECTION_MASK  0x0001fff8
/* bit 4 on */
#define GPIO_OLED_RESET_MASK        0x007c1000
/* bit 10 on */
#define GPIO_OLED_UN_RESET_MASK     0x00781000
/* bits 5 and 10 on */
#define GPIO_OLED_DATA_MASK         0x007a1000
/* bit 6 */
#define GPIO_OLED_BUTTON_IN_MASK    0x00010000

#define TIMER_BASEADDR XPAR_XPS_TIMER_0_BASEADDR
#define CPU_DPLB_FREQ_HZ XPAR_CPU_DPLB_FREQ_HZ

#define GPIO_MISC_CHANNEL           1
#define GPIO_MISC_DIRECTION_MASK    0x00784002
//#define GPIO_MISC_RESET_MASK      0x00007ffd
/* also blink LEDs */
#define GPIO_MISC_RESET_MASK        0x00001555
//#define GPIO_MISC_UN_RESET_MASK   0x00007ff9
/* also blink LEDs */
#define GPIO_MISC_UN_RESET_MASK     0x00002aa8


/* labx */

#include "mvSwitch_6350R.h"
#include "labx_ethernet.h"

/* ICAP peripheral controller */
#define FINISH_FSL_BIT (0x80000000)
#define ICAP_FSL_FAILED (0x80000000)

#define XPAR_ICAP_CR_ABORT		BIT(4)
#define XPAR_ICAP_CR_RESET		BIT(3)
#define XPAR_ICAP_CR_FIFO_CLEAR	BIT(2)
#define XPAR_ICAP_CR_READ		BIT(1)
#define XPAR_ICAP_CR_WRITE		BIT(0)

#define XPAR_ICAP_SR_CFGERR		BIT(8)
#define XPAR_ICAP_SR_DALIGN		BIT(7)
#define XPAR_ICAP_SR_READ_IP	BIT(6)
#define XPAR_ICAP_SR_IN_ABORT	BIT(5)
#define XPAR_ICAP_SR_DONE		BIT(0)
 
/* Ethernet port */
#define CONFIG_SYS_ENET
#define CONFIG_CMD_PING
#define CONFIG_NET_MULTI
#define CONFIG_CMD_NET

#undef ET_DEBUG

/* Use the Lab X Ethernet driver */
#define CONFIG_LABX_ETHERNET  1

/* Use Marvell Link Street Ethernet Switch */
#define CONFIG_MVSWITCH_6350R 1

/* Top-level configuration setting to determine whether AVB port 0 or 1
 * is used by U-Boot.  AVB 0 is on top at the card edge, with AVB 1
 * located underneath of it.
 */
#define WHICH_ETH_PORT  0

/* The MDIO divisor is set to produce a 1.5 MHz interface */
#define LABX_ETHERNET_MDIO_DIV  (0x28)

/* Port zero is used for all MDIO operations, regardless of which
 * port is used for communications.
 */
#define LABX_MDIO_ETH_BASEADDR  (XPAR_ETH0_BASEADDR)

#if (WHICH_ETH_PORT == 0)
  /* Use port zero; the base address of the primary register file and the
   * FIFO used for data are specified, as well as the corresponding PHY address.
   */
#  define LABX_PRIMARY_ETH_BASEADDR    (XPAR_ETH0_BASEADDR)
#  define LABX_SECONDARY_ETH_BASEADDR  (XPAR_ETH1_BASEADDR)
#  define LABX_PRIMARY_ETHERNET_PHY_ADDR    (0x00)
#  define LABX_SECONDARY_ETHERNET_PHY_ADDR  (0x01)
#  define ETHERNET_DT_BUS    "plb@0"
#  define ETHERNET_DT_DEVICE_0 "ethernet@82050000"
#  define ETHERNET_DT_DEVICE_1 "ethernet@82060000"

#else

  /* Use port one instead */
#  define LABX_PRIMARY_ETH_BASEADDR    (XPAR_ETH1_BASEADDR)
#  define LABX_SECONDARY_ETH_BASEADDR  (XPAR_ETH0_BASEADDR)
#  define LABX_PRIMARY_ETHERNET_PHY_ADDR    (0x01)
#  define LABX_SECONDARY_ETHERNET_PHY_ADDR  (0x00)
#  define ETHERNET_DT_BUS    "plb@0"
#  define ETHERNET_DT_DEVICE_0 "ethernet@82060000"
#  define ETHERNET_DT_DEVICE_1 "ethernet@82050000"
#endif /* if(U_BOOT_PORT = 0) */

/* gpio */
#ifdef XPAR_XPS_GPIO_0_BASEADDR
#  define	CONFIG_SYS_GPIO_0		1
#  define	CONFIG_SYS_GPIO_0_ADDR		XPAR_XPS_GPIO_0_BASEADDR
#endif

/* interrupt controller */
#define	CONFIG_SYS_INTC_0		1
#define	CONFIG_SYS_INTC_0_ADDR		XPAR_INTC_0_BASEADDR
#define	CONFIG_SYS_INTC_0_NUM		32

#else
#error "Please define HARDWARE_*"
#endif
#endif

/*= Board-specific parameters above this line ===================*/
