/*
 *
 * Lab X Ethernet driver for MBBL
 * Adapted from Lab X driver for U-Boot
 * Adapted from the Xilinx TEMAC driver
 *
 * The Lab X Ethernet peripheral implements a FIFO-based interface which
 * largely, directly, mimics the xps_ll_fifo peripheral but without the
 * unnecessary LocalLink adaptation layer.  A future enhancement may add
 * an optional integrated SGDMA controller.
 *
 * Authors: Yoshio Kashiwagi kashiwagi@co-nss.co.jp
 *          Eldridge M. Mount IV (eldridge.mount@labxtechnologies.com)
 *          Alex Belits (alexb@meyersound.com)
 *
 * Copyright (C) 2008 Michal Simek <monstr@monstr.eu>
 * June 2008 Microblaze optimalization, FIFO mode support
 *
 * Copyright (C) 2008 Nissin Systems Co.,Ltd.
 * March 2008 created
 *
 * Copyright (C) 2010 Lab X Technologies, LLC
 *
 * Copyright (C) 2012 Meyer Sound Laboratories Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

//#include <net.h>
#include <stdint.h>
#include <string.h>
//#include <malloc.h>
//#include <asm/processor.h>

/* Xilinx tools generated this */
#include "xparameters.h"

#include "mbbl-board.h"
#include "mbbl-timer.h"
#include "mbbl-console.h"

#include "labx_ethernet.h"

#ifndef LABX_PRIMARY_ETH_BASEADDR
#  error "Missing board definition for Ethernet peripheral base address"
#endif
#ifndef LABX_SECONDARY_ETH_BASEADDR
#  error "Missing board definition for Ethernet peripheral base address"
#endif

/* Ensure that there is a definition for the PHY address and MDIO divisor */
#ifndef LABX_PRIMARY_ETHERNET_PHY_ADDR
#  error "Missing board definition of MDIO PHY address"
#endif
#ifndef LABX_SECONDARY_ETHERNET_PHY_ADDR
#  error "Missing board definition of MDIO PHY address"
#endif
#ifndef LABX_ETHERNET_MDIO_DIV
#  error "Missing board definition of MDIO clock divisor"
#endif

/* Locate the FIFO structure offset from the Ethernet base address */
ll_fifo_s *ll_fifo[2] =
  {
    (ll_fifo_s *) (LABX_PRIMARY_ETH_BASEADDR + LABX_FIFO_REGS_BASE),
    (ll_fifo_s *) (LABX_SECONDARY_ETH_BASEADDR + LABX_FIFO_REGS_BASE)
  };


#if !defined(CONFIG_NET_MULTI)
static struct eth_device *xps_ll_dev = NULL;
#endif

/* FIXME: Marvell switch support */
#ifndef CONFIG_MVSWITCH_6350R
#define CONFIG_MVSWITCH_6350R 0
#endif
#define MV_REG_PHY(p)  (p)
#define MV_REG_PORT(p) (0x10 + (p))
#define MV_REG_GLOBAL  0x1b
#define MV_REG_GLOBAL2 0x1c

/* Performs a register write to a PHY */
static
#if (CONFIG_MVSWITCH_6350R == 0)
inline
#endif
void write_phy_register_local(int phy_addr, int reg_addr, int phy_data)
{
  unsigned int addr;

  /* Write the data first, then the control register */
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_DATA_REG);
  *((volatile unsigned int *) addr) = phy_data;
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_CONTROL_REG);
  *((volatile unsigned int *) addr) = 
    (PHY_MDIO_WRITE | ((phy_addr & PHY_ADDR_MASK) << PHY_ADDR_SHIFT) |
     (reg_addr & PHY_REG_ADDR_MASK));
  while(*((volatile unsigned int *) addr) & PHY_MDIO_BUSY);
}

/* Performs a register read from a PHY */
static
#if (CONFIG_MVSWITCH_6350R == 0)
inline
#endif
unsigned int read_phy_register_local(int phy_addr, int reg_addr)
{
  unsigned int addr;
  unsigned int readValue;

  /* Write to the MDIO control register to initiate the read */
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_CONTROL_REG);
  *((volatile unsigned int *) addr) = 
    (PHY_MDIO_READ | ((phy_addr & PHY_ADDR_MASK) << PHY_ADDR_SHIFT) |
     (reg_addr & PHY_REG_ADDR_MASK));
  while(*((volatile unsigned int *) addr) & PHY_MDIO_BUSY);
  addr = (LABX_MDIO_ETH_BASEADDR + MDIO_DATA_REG);
  readValue = *((volatile unsigned int *) addr);
  return(readValue);
}

#if CONFIG_MVSWITCH_6350R

/* Performs a register write to a switch PHY register */
static void write_phy_register(int phy_addr, int reg_addr, int phy_data)
{
  write_phy_register_local(MV_REG_GLOBAL2, 25, phy_data);
  write_phy_register_local(MV_REG_GLOBAL2, 24, 0x9400 | (phy_addr << 5) | reg_addr);
  while (read_phy_register_local(MV_REG_GLOBAL2, 24) & 0x8000); // Should only take a read or two...
}

/* Performs a register read from a switch PHY register */
static unsigned int read_phy_register(int phy_addr, int reg_addr)
{
  write_phy_register_local(MV_REG_GLOBAL2, 24, 0x9800 | (phy_addr << 5) | reg_addr);
  while (read_phy_register_local(MV_REG_GLOBAL2, 24) & 0x8000); // Should only take a read or two...
  return read_phy_register_local(MV_REG_GLOBAL2, 25);
}

#else

static void write_phy_register(int phy_addr, int reg_addr, int phy_data)
{
  write_phy_register_local(phy_addr, reg_addr, phy_data);
}

static unsigned int read_phy_register(int phy_addr, int reg_addr)
{
  return read_phy_register_local(phy_addr, reg_addr);
}

#endif

/* Writes a value to a MAC register */
static void labx_eth_write_mac_reg(int eth_port,int reg_offset, int reg_data)
{
  //printf("REG %04X = %08X (was %08X)\n", reg_offset, reg_data, *(volatile unsigned int *)(LABX_PRIMARY_ETH_BASEADDR + reg_offset));
  /* Apply the register offset to the base address */
  *(volatile unsigned int *)
    ((eth_port?LABX_SECONDARY_ETH_BASEADDR:LABX_PRIMARY_ETH_BASEADDR)
     + reg_offset) = reg_data;
}

/* Writes a value to the MDIO configuration register within the register
 * file of the controller being used for MDIO (may or may not be the same
 * as the primary!
 */
static void labx_eth_write_mdio_config(int config_data)
{
  /* Apply the MDIO register offset to the base address */
  *(volatile unsigned int *)(LABX_MDIO_ETH_BASEADDR + MAC_MDIO_CONFIG_REG) = config_data;
}

/* Reads a value from a MAC register */
int labx_eth_read_mac_reg(int eth_port,int reg_offset)
{
  unsigned int val = *(volatile unsigned int *)
    ((eth_port?LABX_SECONDARY_ETH_BASEADDR:LABX_PRIMARY_ETH_BASEADDR)
     + reg_offset);
  return(val);
}
/*
void mdelay(unsigned int msec)
{
  while(msec--)
    udelay(1000);
}
*/
static int phy_addr[2] ={
  LABX_PRIMARY_ETHERNET_PHY_ADDR,
  LABX_SECONDARY_ETHERNET_PHY_ADDR
};

static int link[2] = {0,0};
static int first = 1;

/* setting ll_temac and phy to proper setting */
static int labx_eth_phy_ctrl(int eth_port,int timeout)
{
  unsigned int result;
  int link_up=0;

  if(first) {
    unsigned int id_high,id_low;

    /* Read and report the PHY */
    id_high = read_phy_register(phy_addr[eth_port], 2);
    id_low = read_phy_register(phy_addr[eth_port], 3);
    print_simple("PHY ID at address 0x");
    print_simple_hex_8(phy_addr[eth_port]);
    print_simple(": 0x");
    print_simple_hex_32(((id_high&0xffff)<<16)|(id_low&0xffff));
    print_simple("\r\n");
    /*
      FIXME -- Broadcom stuff if'ed out -- AB
     */
#if 0    
    //    printf("PHY ID at address 0x%02X: 0x%04X%04X\n", phy_addr, id_high, id_low);
    if (id_high == BCM548x_ID_HIGH && (((id_low & BCM548x_ID_LOW_MASK) == BCM5481_ID_LOW) 
		|| ((id_low & BCM548x_ID_LOW_MASK) == BCM5482_ID_LOW)))
    {
    	/* RGMII Transmit Clock Delay: The RGMII transmit timing can be adjusted
		 * by software control. TXD-to-GTXCLK clock delay time can be increased
		 * by approximately 1.9 ns for 1000BASE-T mode, and between 2 ns to 6 ns
		 * when in 10BASE-T or 100BASE-T mode by setting Register 1ch, SV 00011,
		 * bit 9 = 1. Enabling this timing adjustment eliminates the need for
		 * board trace delays as required by the RGMII specification.
    	 */
    	write_phy_register(phy_addr[eth_port], 0x1C, BCM5481_CLOCK_ALIGNMENT_REGISTER_SEL);
    	result = read_phy_register(phy_addr[eth_port], 0x1C);
    	write_phy_register(phy_addr[eth_port], 0x1C,
    			BCM5481_SHADOW_WRITE | BCM5481_CLOCK_ALIGNMENT_REGISTER_SEL | BCM5481_XMIT_CLOCK_DELAY);
    	write_phy_register(phy_addr[eth_port], 0x1C, BCM5481_CLOCK_ALIGNMENT_REGISTER_SEL);
	print_simple("RGMII Transmit Clock Delay: ");
	print_simple(((result & BCM5481_XMIT_CLOCK_DELAY) != 0)?"yes":"no");
	print_simple(" (0x");
	print_simple_hex_32(result);
	print_simple(") => ");
	print_simple(((read_phy_register(phy_addr[eth_port], 0x1C)& BCM5481_XMIT_CLOCK_DELAY) != 0)?"yes":"no");
	print_simple("\r\n");
	/*
    	printf("RGMII Transmit Clock Delay: %d (0x%04X) => %d\n",
    			((result & BCM5481_XMIT_CLOCK_DELAY) != 0), result,
    			((read_phy_register(phy_addr[eth_port], 0x1C)& BCM5481_XMIT_CLOCK_DELAY) != 0));
	*/

	result = read_phy_register(phy_addr[eth_port], 0x00);
	result = result | BCM5481_AUTO_NEGOTIATE_ENABLE;
	write_phy_register(phy_addr[eth_port], 0x00, result);
	print_simple("Auto-Negotiate Enable: ");
	print_simple(((result & BCM5481_AUTO_NEGOTIATE_ENABLE) != 0)?"yes":"no");
	print_simple(" (0x");
	print_simple_hex_32(result);
	print_simple(") => ");
	print_simple(((read_phy_register(phy_addr[eth_port], 0x00) & BCM5481_AUTO_NEGOTIATE_ENABLE) != 0)?"yes":"no");
	print_simple("\r\n");
	/*
	printf("Auto-Negotiate Enable: %d (0x%04X) => %d\n",
	                ((result & BCM5481_AUTO_NEGOTIATE_ENABLE) != 0), result,
	                ((read_phy_register(phy_addr[eth_port], 0x00) & BCM5481_AUTO_NEGOTIATE_ENABLE) != 0));
	*/
	result = read_phy_register(phy_addr[eth_port], 0x18);
	result = result | BCM5481_SHADOW_WRITE | BCM5481_HPE_REGISTER_SELECT;
	result = result & ~BCM5481_HIGH_PERFORMANCE_ENABLE;
	print_simple("High-Performance Enable: ");
	print_simple(((result & BCM5481_HIGH_PERFORMANCE_ENABLE) != 0)?"yes":"no");
	print_simple(" (0x");
	print_simple_hex_32(result);
	print_simple(") => ");
	print_simple(((read_phy_register(phy_addr[eth_port], 0x18) & BCM5481_HIGH_PERFORMANCE_ENABLE) != 0)?"yes":"no");
	print_simple("\r\n");
	/*
	printf("High-Performance Enable: %d (0x%04X) => %d\n",
	                ((result & BCM5481_HIGH_PERFORMANCE_ENABLE) != 0), result,
	                ((read_phy_register(phy_addr[eth_port], 0x18) & BCM5481_HIGH_PERFORMANCE_ENABLE) != 0));
	*/

    	write_phy_register(phy_addr[eth_port], 0x18, BCM5481_RX_SKEW_REGISTER_SEL);
    	result = read_phy_register(phy_addr[eth_port], 0x18);
    	write_phy_register(phy_addr[eth_port], 0x18,
    			result | BCM5481_SHADOW_WRITE | BCM5481_RX_SKEW_REGISTER_SEL | BCM5481_RX_SKEW_ENABLE);
    	write_phy_register(phy_addr[eth_port], 0x18, BCM5481_RX_SKEW_REGISTER_SEL);
	print_simple("RGMII Receive Clock Skew: ");
	print_simple(((result & BCM5481_RX_SKEW_ENABLE) != 0)?"yes":"no");
	print_simple(" (0x");
	print_simple_hex_32(result);
	print_simple(") => ");
	print_simple(((read_phy_register(phy_addr[eth_port], 0x18)& BCM5481_RX_SKEW_ENABLE) != 0)?"yes":"no");
	print_simple("\r\n");
	/*
    	printf("RGMII Receive Clock Skew: %d (0x%04X) => %d\n",
    			((result & BCM5481_RX_SKEW_ENABLE) != 0), result,
    			((read_phy_register(phy_addr[eth_port], 0x18)& BCM5481_RX_SKEW_ENABLE) != 0));
	*/
    }
#endif
  }

  result = read_phy_register(phy_addr[eth_port], 1);

  if((result & 0x24) != 0x24)
    {
      if(link[eth_port])
	{
	  link[eth_port] = 0;
	  print_simple("Link has gone down\r\n");
	}
    }
  else
    {
      if(!link[eth_port])
	{
	  link[eth_port] = 1;
	  link_up = 1;
	}
    }

  if(!link[eth_port])
    {
      /* Requery the PHY general status register */
      /* Wait up to 5 secs for a link */
      while(!link[eth_port] && timeout--)
	{
	  mdelay(1);
	  result = read_phy_register(phy_addr[eth_port], 1);
	  if((result & 0x24) == 0x24)
	    {
	      link[eth_port] = 1;
	      link_up = 1;
	    }
	}
    }
  
  if(link_up)
    {
      print_simple("Link up\r\n");
      result = read_phy_register(phy_addr[eth_port], 10);
      if((result & 0x0800) == 0x0800)
	{
	  labx_eth_write_mac_reg(eth_port,MAC_SPEED_SELECT_REG,
				 MAC_SPEED_1_GBPS);
	  print_simple("1000BASE-T/FD\r\n");
	}
      else
	{
	  result = read_phy_register(phy_addr[eth_port], 5);
	  if((result & 0x0100) == 0x0100)
	    {
	      labx_eth_write_mac_reg(eth_port,MAC_SPEED_SELECT_REG,
				     MAC_SPEED_100_MBPS);
	      print_simple("100BASE-T/FD\r\n");
	    }
	  else
	    {
	      if((result & 0x0040) == 0x0040)
		{
		  labx_eth_write_mac_reg(eth_port,MAC_SPEED_SELECT_REG,
					 MAC_SPEED_10_MBPS);
		  print_simple("10BASE-T/FD\r\n");
		}
	      else
		{
		  print_simple("Half Duplex not supported\r\n");
		}
	    }
	}
    }
  return link[eth_port];
}

/* Rx buffer is also used by FIFO mode */
static unsigned char rx_buffer[2][ETHER_MTU] __attribute((aligned(32)));



void debugll(int count)
{
  print_simple("0x");
  print_simple_hex_32(count);
  print_simple(" fifo 0 isr 0x");
  print_simple_hex_32(ll_fifo[0]->isr);
  print_simple(" fifo 0 ier 0x");
  print_simple_hex_32(ll_fifo[0]->ier);
  print_simple(" fifo 0 rdfr 0x");
  print_simple_hex_32(ll_fifo[0]->rdfr);
  print_simple(" fifo 0 rdfo 0x");
  print_simple_hex_32(ll_fifo[0]->rdfo);
  print_simple(" fifo 0 rlf 0x");
  print_simple_hex_32(ll_fifo[0]->rlf);
  print_simple("\r\n");
  print_simple(" fifo 1 isr 0x");
  print_simple_hex_32(ll_fifo[1]->isr);
  print_simple(" fifo 1 ier 0x");
  print_simple_hex_32(ll_fifo[1]->ier);
  print_simple(" fifo 1 rdfr 0x");
  print_simple_hex_32(ll_fifo[1]->rdfr);
  print_simple(" fifo 1 rdfo 0x");
  print_simple_hex_32(ll_fifo[1]->rdfo);
  print_simple(" fifo 1 rlf 0x");
  print_simple_hex_32(ll_fifo[1]->rlf);
  print_simple("\r\n");

  /*
  printf ("%d fifo isr 0x%08x, fifo_ier 0x%08x, fifo_rdfr 0x%08x, fifo_rdfo 0x%08x fifo_rlr 0x%08x\n",count, ll_fifo->isr, \
	  ll_fifo->ier, ll_fifo->rdfr, ll_fifo->rdfo, ll_fifo->rlf);
  */
}


static int labx_eth_send_fifo(int eth_port,unsigned char *buffer, int length)
{
  unsigned int *buf = (unsigned int*) buffer;
  unsigned int len, i, val;

  len = ((length + 3) / 4);

  for (i = 0; i < len; i++) {
    val = *buf++;
    ll_fifo[eth_port]->tdfd = val;
  }

  ll_fifo[eth_port]->tlf = length;

  return length;
}

static int labx_eth_recv_fifo(int eth_port)
{
  int len, len2, i, val;
  int *buf = (int*) &rx_buffer[eth_port][0];

  if (ll_fifo[eth_port]->isr & FIFO_ISR_RC) {
    /* One or more packets have been received.  Check the read occupancy register
     * to see how much data is ready to be processed.
     */
    len = ll_fifo[eth_port]->rlf & RLF_MASK;
    len2 = ((len + 3) / 4);

    for (i = 0; i < len2; i++) {
      val = ll_fifo[eth_port]->rdfd;
      *buf++ = val ;
    }

    /* Re-check the occupancy register; if there are still FIFO contents
     * remaining, they are for valid packets.  Not sure if it's okay to call
     * NetReceive() more than once for each invocation, so we'll just leave
     * the ISR flag set instead and let the NetLoop invoke us again.
     */
    if(ll_fifo[eth_port]->rdfo == 0) ll_fifo[eth_port]->isr = FIFO_ISR_RC;

    /* Enqueue the received packet! */
    //    NetReceive (&rx_buffer[eth_port][0], len);
    // FIXME -- process data here
    /* just return the length for now */
    return len;
  } else if(ll_fifo[eth_port]->isr & FIFO_ISR_RX_ERR) {
    print_simple("Rx error 0x");
    print_simple_hex_32(ll_fifo[eth_port]->isr);
    print_simple("\r\n");

    /* A receiver error has occurred, reset the Rx logic */
    ll_fifo[eth_port]->isr = FIFO_ISR_ALL;
    ll_fifo[eth_port]->rdfr = FIFO_RESET_MAGIC;
  }

    return 0;
}

/* FOO */

#define MAC_MATCH_NONE 0
#define MAC_MATCH_ALL 1

static const u8 MAC_BROADCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const u8 MAC_ZERO[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

#define NUM_SRL16E_CONFIG_WORDS 8
#define NUM_SRL16E_INSTANCES    12

/* Busy loops until the match unit configuration logic is idle.  The hardware goes 
 * idle very quickly and deterministically after a configuration word is written, 
 * so this should not consume very much time at all.
 */
static void wait_match_config(int eth_port) {
  unsigned int addr;
  uint32_t statusWord;
  uint32_t timeout = 10000;

  addr = ((eth_port?LABX_SECONDARY_ETH_BASEADDR:LABX_PRIMARY_ETH_BASEADDR)
	  + MAC_CONTROL_REG);
  do {
    statusWord = *((volatile unsigned int *) addr);
    if (timeout-- == 0)
      {
        print_simple("labx_ethernet : wait_match_config timeout!\r\n");
        break;
      }
  } while(statusWord & MAC_ADDRESS_LOAD_ACTIVE);
}

/* Selects a set of match units for subsequent configuration loads */
typedef enum { SELECT_NONE, SELECT_SINGLE, SELECT_ALL } SelectionMode;
static void select_matchers(int eth_port,SelectionMode selectionMode,
                            uint32_t matchUnit) {
  unsigned int addr;

  addr = ((eth_port?LABX_SECONDARY_ETH_BASEADDR:LABX_PRIMARY_ETH_BASEADDR)
	  + MAC_SELECT_REG);

  switch(selectionMode) {
  case SELECT_NONE:
    /* De-select all the match units */
    //printk("MAC SELECT %08X\n", 0);
    *((volatile unsigned int *) addr) = 0x00000000;
    break;

  case SELECT_SINGLE:
    /* Select a single unit */
    //printk("MAC SELECT %08X\n", 1 << matchUnit);
    *((volatile unsigned int *) addr) = (1 << matchUnit);
    break;

  default:
    /* Select all match units at once */
    //printk("MAC SELECT %08X\n", 0xFFFFFFFF);
    *((volatile unsigned int *) addr) = 0xFFFFFFFF;
    break;
  }
}

/* Sets the loading mode for any selected match units.  This revolves around
 * automatically disabling the match units' outputs while they're being
 * configured so they don't fire false matches, and re-enabling them as their
 * last configuration word is loaded.
 */
typedef enum { LOADING_MORE_WORDS, LOADING_LAST_WORD } LoadingMode;
static void set_matcher_loading_mode(int eth_port,LoadingMode loadingMode) {
  unsigned int addr;
  uint32_t controlWord;

  addr = ((eth_port?LABX_SECONDARY_ETH_BASEADDR:LABX_PRIMARY_ETH_BASEADDR)
	  + MAC_CONTROL_REG);

  controlWord = *((volatile unsigned int *) addr);

  if(loadingMode == LOADING_MORE_WORDS) {
    /* Clear the "last word" bit to suppress false matches while the units are
     * only partially cleared out
     */
    controlWord &= ~MAC_ADDRESS_LOAD_LAST;
  } else {
    /* Loading the final word, flag the match unit(s) to enable after the
     * next configuration word is loaded.
     */
    controlWord |= MAC_ADDRESS_LOAD_LAST;
  }
  //printk("CONTROL WORD %08X\n", controlWord);

  *((volatile unsigned int *) addr) = controlWord;
}

/* Clears any selected match units, preventing them from matching any packets */
static void clear_selected_matchers(int eth_port) {
  unsigned int addr;
  uint32_t wordIndex;
  
  /* Ensure the unit(s) disable as the first word is load to prevent erronous
   * matches as the units become partially-cleared
   */
  set_matcher_loading_mode(eth_port,LOADING_MORE_WORDS);

  for(wordIndex = 0; wordIndex < NUM_SRL16E_CONFIG_WORDS; wordIndex++) {
    /* Assert the "last word" flag on the last word required to complete the clearing
     * of the selected unit(s).
     */
    if(wordIndex == (NUM_SRL16E_CONFIG_WORDS - 1)) {
      set_matcher_loading_mode(eth_port,LOADING_LAST_WORD);
    }

    //printk("MAC LOAD %08X\n", 0);
    addr = ((eth_port?LABX_SECONDARY_ETH_BASEADDR:LABX_PRIMARY_ETH_BASEADDR)
	    + MAC_LOAD_REG);
    *((volatile unsigned int *) addr) = 0x00000000;
  }
}

/* Loads truth tables into a match unit using the newest, "unified" match
 * architecture.  This is SRL16E based (not cascaded) due to the efficient
 * packing of these primitives into Xilinx LUT6-based architectures.
 */
static void load_unified_matcher(int eth_port,const uint8_t matchMac[6]) {
  unsigned int addr;
  int32_t wordIndex;
  int32_t lutIndex;
  uint32_t configWord = 0x00000000;
  uint32_t matchChunk;
  
  /* All local writes will be to the MAC filter load register */
  addr = ((eth_port?LABX_SECONDARY_ETH_BASEADDR:LABX_PRIMARY_ETH_BASEADDR)
	  + MAC_LOAD_REG);

  /* In this architecture, all of the SRL16Es are loaded in parallel, with each
   * configuration word supplying two bits to each.  Only one of the two bits can
   * ever be set, so there is just an explicit check for one.
   */
  for(wordIndex = (NUM_SRL16E_CONFIG_WORDS - 1); wordIndex >= 0; wordIndex--) {
    for(lutIndex = (NUM_SRL16E_INSTANCES - 1); lutIndex >= 0; lutIndex--) {
      matchChunk = ((matchMac[5-(lutIndex/2)] >> ((lutIndex&1) << 2)) & 0x0F);
      configWord <<= 2;
      if(matchChunk == (wordIndex << 1)) configWord |= 0x01;
      if(matchChunk == ((wordIndex << 1) + 1)) configWord |= 0x02;
    }
    /* 12 nybbles are packed to the MSB */
    configWord <<= 8;

    /* Two bits of truth table have been determined for each SRL16E, load the
     * word and wait for the configuration to occur.  Be sure to flag the last
     * word to automatically re-enable the match unit(s) as the last word completes.
     */
    if(wordIndex == 0) set_matcher_loading_mode(eth_port,LOADING_LAST_WORD);
    //printk("MAC LOAD %08X\n", configWord);
    *((volatile unsigned int *) addr) = configWord;
    wait_match_config(eth_port);
  }
}

static void configure_mac_filter(int eth_port,int unitNum, const u8 mac[6], int mode) {
  //printk("CONFIGURE MAC MATCH %d (%d), %02X:%02X:%02X:%02X:%02X:%02X\n", unitNum, mode,
  //	mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  /* Ascertain that the configuration logic is ready, then select the matcher */
  wait_match_config(eth_port);
  select_matchers(eth_port,SELECT_SINGLE, unitNum);

  if (mode == MAC_MATCH_NONE) {
    clear_selected_matchers(eth_port);
  } else {
    /* Set the loading mode to disable as we load the first word */
    set_matcher_loading_mode(eth_port,LOADING_MORE_WORDS);
      
    /* Calculate matching truth tables for the LUTs and load them */
    load_unified_matcher(eth_port,mac);
  }

  /* De-select the match unit */
  select_matchers(eth_port,SELECT_NONE, 0);
}

/* setup mac addr */
static int labx_eth_addr_setup(int eth_port,unsigned char *macaddr)
{
  unsigned int addr;
  uint32_t numMacFilters;

  /* Determine how many MAC address filters the instance supports, as reported
   * by a field within the revision register
   */
  addr = ((eth_port?LABX_SECONDARY_ETH_BASEADDR:LABX_PRIMARY_ETH_BASEADDR)
	  + REVISION_REG);
  numMacFilters = ((*((volatile unsigned int *) addr) & REVISION_MATCH_MASK) >>
                   REVISION_MATCH_SHIFT);
  print_simple("Lab X Ethernet has 0x");
  print_simple_hex_32(numMacFilters);
  print_simple(" MAC filters\r\n");
  
  /* Configure for our unicast MAC address first, and the broadcast MAC
   * address second, provided there are enough MAC address filters.
   */
  if(numMacFilters >= 1) {
    configure_mac_filter(eth_port, 0, macaddr, MAC_MATCH_ALL);
  }

  if(numMacFilters >= 2) {
    configure_mac_filter(eth_port, 1, MAC_BROADCAST, MAC_MATCH_ALL);
  }
  
  return(0);
}

static void labx_eth_restart(int eth_port)
{
  /* Enable the receiver and transmitter */
  labx_eth_write_mac_reg(eth_port,MAC_RX_CONFIG_REG,
			 RX_ENABLE | RX_VLAN_TAGS_ENABLE);
  labx_eth_write_mac_reg(eth_port,MAC_TX_CONFIG_REG,
			 TX_ENABLE | TX_VLAN_TAGS_ENABLE);
}


int labx_eth_init(unsigned char *macaddr0,unsigned char *macaddr1)
{
  //  struct labx_eth_private *lp = (struct labx_eth_private *)dev->priv;

  if(!first) {
    /* Short-circuit some of the setup; still return no error to permit
     * the client code to keep going
     */
    labx_eth_restart(0);
    labx_eth_restart(1);
    labx_eth_phy_ctrl(0,5000);
    labx_eth_phy_ctrl(1,5000);
    return(0);
  }

  /* Clear ISR flags and reset both transmit and receive FIFO logic */
  ll_fifo[0]->isr = FIFO_ISR_ALL;
  ll_fifo[0]->tdfr = FIFO_RESET_MAGIC;
  ll_fifo[0]->rdfr = FIFO_RESET_MAGIC;
  ll_fifo[1]->isr = FIFO_ISR_ALL;
  ll_fifo[1]->tdfr = FIFO_RESET_MAGIC;
  ll_fifo[1]->rdfr = FIFO_RESET_MAGIC;
  //	printf ("fifo isr 0x%08x, fifo_ier 0x%08x, fifo_tdfv 0x%08x, fifo_rdfo 0x%08x fifo_rlf 0x%08x\n", ll_fifo->isr, ll_fifo->ier, ll_fifo->tdfv, ll_fifo->rdfo,ll_fifo->rlf);

  /* Configure the MDIO divisor and enable the interface to the PHY.
   * XILINX_HARD_MAC Note: The hard MDIO controller must be configured or
   * the SGMII autonegotiation won't happen. Even if we never use that MDIO controller.
   * This operation must access the register file of the controller being used
   * for MDIO access, which may or may not be the same as that used for the actual
   * communications! 
   */
  labx_eth_write_mdio_config((LABX_ETHERNET_MDIO_DIV & MDIO_DIVISOR_MASK) |
			      MDIO_ENABLED);
  
  /* Set up the MAC address in the hardware */
  labx_eth_addr_setup(0,macaddr0);
  labx_eth_addr_setup(1,macaddr1);

  /* Enable the receiver and transmitter */
  labx_eth_write_mac_reg(0,MAC_RX_CONFIG_REG, RX_SOFT_RESET);
  labx_eth_write_mac_reg(0,MAC_RX_CONFIG_REG, RX_ENABLE | RX_VLAN_TAGS_ENABLE);
  labx_eth_write_mac_reg(0,MAC_TX_CONFIG_REG, TX_ENABLE | TX_VLAN_TAGS_ENABLE);

  labx_eth_write_mac_reg(1,MAC_RX_CONFIG_REG, RX_SOFT_RESET);
  labx_eth_write_mac_reg(1,MAC_RX_CONFIG_REG, RX_ENABLE | RX_VLAN_TAGS_ENABLE);
  labx_eth_write_mac_reg(1,MAC_TX_CONFIG_REG, TX_ENABLE | TX_VLAN_TAGS_ENABLE);

  /* Configure the PHY */
  labx_eth_phy_ctrl(0,5000);
  labx_eth_phy_ctrl(1,5000);
  first = 0;

  return(0);
}

static void labx_eth_halt(/*struct eth_device *dev*/)
{
  labx_eth_write_mac_reg(0,MAC_RX_CONFIG_REG, RX_DISABLE);
  labx_eth_write_mac_reg(0,MAC_TX_CONFIG_REG, TX_DISABLE);
  labx_eth_write_mac_reg(1,MAC_RX_CONFIG_REG, RX_DISABLE);
  labx_eth_write_mac_reg(1,MAC_TX_CONFIG_REG, TX_DISABLE);
}

int labx_eth_send(int eth_port, /*volatile*/ void *packet, int length)
{
  labx_eth_phy_ctrl(eth_port,100);
  if(!link[eth_port])
    return 0;

  return(labx_eth_send_fifo(eth_port,(unsigned char *)packet, length));
}

int labx_eth_recv(int eth_port, void *packet, int length)
{
  int l;
  labx_eth_phy_ctrl(eth_port,10);
  l=labx_eth_recv_fifo(eth_port);
  memcpy(packet,rx_buffer[eth_port],l>length?length:l);
  return l;
}
