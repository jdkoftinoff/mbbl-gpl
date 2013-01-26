#include <string.h>
#include <stdlib.h>
#include <mb_interface.h>

/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


/* Use only low-level headers for SPI and serial devices */
#include <xspi_l.h>
#include <xuartlite_l.h>

/* console */
#include "mbbl-console.h"

#include <zlib.h>
#include "fdt.h"
#include "mbbl-timer.h"
#include "mbbl-platform.h"

/* Xilinx tools generated this */
#include "xparameters.h"

#include "mbbl-board.h"

#ifdef HARDWARE_CAL_ICS
#include "avb-boot.h"
#endif

#define _STACK_SIZE 0x400
#define _HEAP_SIZE 0x18000
#define _EXEC_SIZE 0x10000

#ifndef DEBUG_PRINT
#define DEBUG_PRINT 0
#endif



/* Fixed addresses */
#define DT_ALT_ADDR	(DDR_BASEADDR+0x00800000)
#define RD_ALT_ADDR	(DDR_BASEADDR+0x07c00000)

/*
  Kernel command line does not work on the current Microblaze
  Linux implementation. Use device tree instead of this
*/
#define CMDLINE ((unsigned char*)"console=ttyUL0 root=/dev/ram0 "	\
		 "ramdisk_start=4096 load_ramdisk=1 prompt_ramdisk=0 "	\
		 "BUTTONS=0x00")

/*
         Firmware file layout
         ====================

          +---+---+---+---+---+---+---+---+
00000000  | M | B | B | L |   | F | I | R |
          +---+---+---+---+---+---+---+---+
00000008  | M | W | A | R | E |   | I | M |
          +---+---+---+---+---+---+---+---+
00000010  | A | G | E |   | V | 1 | . | 0 |
          +---+---+---+---+---+---+---+---+
00000018  |    00000000   |    00000000   |
          +---------------+---------------+
00000020  | REC_TYPE_FDT  | padded size 1 |
          +---------------+---------------+
00000028    ^
            |
    (padded size 1)      F D T
            |
            v
          +---------------+---------------+
n1*4-8    |REC_TYPE_KERNEL| padded size 2 |
          +---------------+---------------+
 n1*4 ----> ^
            |
    (padded size 2)     Kernel (gzip)
            |
            v
          +--------------------+---------------+
n2*4-8    |REC_TYPE_BOOT_SCREEN| padded size 3 |
          +--------------------+---------------+
 n2*4 ----> ^
            |
    (padded size 3)     Boot Screen (gzip)
            |
            v
          +---------------+---------------+
n3*4-8    |REC_TYPE_FONT  | padded size 4 |
          +---------------+---------------+
 n3*4 ----> ^
            |
    (padded size 4)     Font (gzip)
            |
            v
          +---------------+---------------+
n4*1024-8 |REC_TYPE_RDISK |     size 5    |
          +---------------+---------------+
 n4*1024--> ^
            |
         (size 5)      Ramdisk (gzip)
            |
            v
          +-------------------------------+
*/

/*
  image is supposed to be loaded at 3M offset (in 8M flash section)
  after FPGA configuration
*/
#define FLASH_BOOT_IMAGE_OFFSET (3*1024)

/* firmware file header */
#define BOOT_IMAGE_HEADER "MBBL FIRMWARE IMAGE V1.0\0\0\0\0\0\0\0\0"
#define BOOT_IMAGE_HEADER_SIZE 32

/* section types */
enum{
  REC_TYPE_FDT,         /* Flat device tree, not compressed */
  REC_TYPE_KERNEL,      /* Linux kernel, compressed with gzip */
  REC_TYPE_RDISK,       /* RAM disk image, compressed with gzip,
			   must be the last section in the image */
  REC_TYPE_BOOT_SCREEN, /* Boot screen, raw image format for
			   platform-specific display routines,
			   compressed with gzip */
  REC_TYPE_FONT,        /* Font, raw font format for platform-specific display
			   routines, compressed with gzip */
  REC_TYPE_END,		/* End markker (if RAM disk image is not present) */
  REC_TYPE_EMPTY,       /* Empty section, ignore when loading */
  REC_TYPE_BAD          /* Bad block, ignore when loading, do not overwrite */
};

/* max number of sections in the image */
#define MAX_SECTIONS_COUNT (1000)

/* max number of fonts in the image */
#define MAX_FONTS (32)

/* Print warning message for installation and debricking procedure */
void install_warning(char *str,int addr)
{
  print_simple("\r\n*** Either, the system in not installed "
	       "or flash image is corrupt. ***\r\n\n"
	       "To load the system, use JTAG cable and Xilinx Microprocessor "
	       "Debugger.\r\n"
	       " 1. Stop the processor.\r\n"
	       " 2. Load ");
  print_simple(str);
  print_simple(" at 0x");
  print_simple_hex_32(addr);
  print_simple(
	       ".\r\n"
	       " 3. Continue from the stop address to load the system.\r\n");
}

/*
  Launch kernel with arguments for Microblaze.
  only FDT and address are actually used
*/
void launch(const void volatile *addr,
	    const unsigned char volatile *cmdline,
	    const unsigned char volatile *ramdisk,
	    const unsigned char volatile *fdt)
{
  asm volatile ("\
	lwi r5,%1; \
	lwi r6,%2; \
	lwi r7,%3; \
	lwi r8,%0; \
	brad r8;   \
	nop;	   \
	"
		:
		:"m"(addr),"m"(cmdline),"m"(ramdisk),"m"(fdt)
		);
}

#if FLASH_IS_BPI
/* Place CFI flash into read mode */
static inline void cfi_read_init(void)
{
  *((volatile unsigned char*)BPI_FLASH_BASEADDR)=0xff;
}
#else
#define SPI_CMD_READ 0x03

static u32 spi_flash_slave_select_reg=SPI_FLASH_SLAVE_SELECT_MASK,
  spi_flash_control_reg=0;
  
/* Initialize SPI flash interface */
static inline void spi_flash_init(void)
{
  /* slave select -- all off */
  spi_flash_slave_select_reg=SPI_FLASH_SLAVE_SELECT_MASK;
  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_SSR_OFFSET,spi_flash_slave_select_reg);

  /* 
     control register -- read initial value, inhibit master transaction,
     manual slave select
  */
  spi_flash_control_reg=XSpi_ReadReg(SPI_FLASH_BASEADDR,XSP_CR_OFFSET);
  spi_flash_control_reg|=XSP_CR_TRANS_INHIBIT_MASK|XSP_CR_MANUAL_SS_MASK;

  /* FIFO reset (if any) */
#if SPI_FLASH_FIFO_EXIST
  spi_flash_control_reg|=XSP_CR_TXFIFO_RESET_MASK|XSP_CR_RXFIFO_RESET_MASK;
#endif

  /* write to the control register */
  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_CR_OFFSET,spi_flash_control_reg);

  /* reset */
  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_SRR_OFFSET,XSP_SRR_RESET_MASK);
  /* disable interrupt */
  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_DGIER_OFFSET,0);

  /* system enable, master mode */
  spi_flash_control_reg|=XSP_CR_ENABLE_MASK|XSP_CR_MASTER_MODE_MASK;

  /* write to the control register */
  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_CR_OFFSET,spi_flash_control_reg);
}

/*
  Perform transfer for SPI flash
  This function supports "pseudo-half-duplex" mode commonly used by
  flash and other devices -- it sends src_count bytes and pads them
  with 0 if necessary. First skip_receive bytes are skipped, and
  then dst_count bytes are received.

  For 16-bit and 32-bit modes, buffers are assumed to be padded but
  not necessarily aligned.
 */
static void spi_flash_transfer(unsigned char *src,u32 src_count,
			       unsigned char *dst,u32 dst_count,
			       u32 skip_receive)
{
  u32 count,sent=0,received=0,receive_index=0;
  u32 data,spi_flash_status_reg;

  /* determine the total length of the transfer */
  count=dst_count+skip_receive;
  if(count<src_count)
    count=src_count;

  /* read control register */
  spi_flash_control_reg=XSpi_ReadReg(SPI_FLASH_BASEADDR,XSP_CR_OFFSET);
  /* slave select */
  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_SSR_OFFSET,spi_flash_slave_select_reg);

  /* perform the transfer until all data is sent and received */
  while(sent<count)
    {
      /* 
	 send bytes into the tx FIFO until tx FIFO is full
	 or there is no more data 
      */
      while(((XSpi_ReadReg(SPI_FLASH_BASEADDR,XSP_SR_OFFSET)
	      &XSP_SR_TX_FULL_MASK)==0)
	    &&(sent<count))
	{
#if SPI_FLASH_TRANSFER_BITS == XSP_DATAWIDTH_WORD
	  if(sent<src_count)
	    {
	      data=(((u32)src[sent])<<24)
		|(((u32)src[sent+1])<<16)
		|(((u32)src[sent+2])<<8)
		|((u32)src[sent+3]);
	    }
	  else
	    data=0;
	  sent+=4;
#else
#if SPI_FLASH_TRANSFER_BITS == XSP_DATAWIDTH_HALF_WORD
	  if(sent<src_count)
	    {
	      data=(((u32)src[sent])<<8)
		|((u32)src[sent+1]);
	    }
	  else
	    data=0;
	  sent+=2;
#else
	  if(sent<src_count)
	    data=(u32)src[sent];
	  else
	    data=0;
	  sent++;
#endif
#endif
	  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_DTR_OFFSET,data);
	  /*
	    at this point "sent" is the number of bytes sent to FIFO,
	    this data is not transmitted yet
	  */

	}
      /* un-inhibit ro start the transfer */
      spi_flash_control_reg&=~XSP_CR_TRANS_INHIBIT_MASK;
      XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_CR_OFFSET,spi_flash_control_reg);

      /* perform the transfer until all data is sent and received */
      while(received<sent)
	{
	  /* read status */
	  spi_flash_status_reg=XSpi_ReadReg(SPI_FLASH_BASEADDR,XSP_SR_OFFSET);

	  /* if transfer is in progress, and tx FIFO is empty, inhibit */
	  if(((spi_flash_control_reg&XSP_CR_TRANS_INHIBIT_MASK)==0)
	     &&((spi_flash_status_reg&XSP_SR_TX_EMPTY_MASK)!=0))
	    {
	      /*
		inhibit -- transfer stops, however there may be still data
		in rx FIFO
	       */
	      spi_flash_control_reg|=XSP_CR_TRANS_INHIBIT_MASK;
	      XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_CR_OFFSET,
			    spi_flash_control_reg);
	    }
	  /* if there is data in rx FIFO, receive it */
	  if((spi_flash_status_reg&XSP_SR_RX_EMPTY_MASK)==0)
	    {
	      /* receive data from rx FIFO */
	      data=XSpi_ReadReg(SPI_FLASH_BASEADDR,XSP_DRR_OFFSET);
#if SPI_FLASH_TRANSFER_BITS == XSP_DATAWIDTH_WORD
	      if((received>=skip_receive)&&(receive_index<dst_count))
		{
		  dst[receive_index++]=(data>>24)&0xff;
		  dst[receive_index++]=(data>>16)&0xff;
		  dst[receive_index++]=(data>>8)&0xff;
		  dst[receive_index++]=data&0xff;
		}
	      received+=4;
#else
#if SPI_FLASH_TRANSFER_BITS == XSP_DATAWIDTH_HALF_WORD
	      if((received>=skip_receive)&&(receive_index<dst_count))
		{
		  dst[receive_index++]=(data>>8)&0xff;
		  dst[receive_index++]=data&0xff;
		}
	      received+=2;
#else
	      if((received>=skip_receive)&&(receive_index<dst_count))
		dst[receive_index++]=data&0xff;
	      received++;
#endif
#endif
	    }
	}
      /* if tx FIFO still was not seen empty, inhibit */
      if((spi_flash_control_reg&XSP_CR_TRANS_INHIBIT_MASK)==0)
	{
	  /* inhibit */
	  spi_flash_control_reg|=XSP_CR_TRANS_INHIBIT_MASK;
	  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_CR_OFFSET,
			spi_flash_control_reg);
	}
    }
  /* de-select slave */
  XSpi_WriteReg(SPI_FLASH_BASEADDR,XSP_SSR_OFFSET,
		SPI_FLASH_SLAVE_SELECT_MASK);
}

/* Set slave select bit for subsequent transfers */
static inline void spi_flash_slave_select(u32 mask)
{
    spi_flash_slave_select_reg=SPI_FLASH_SLAVE_SELECT_MASK&~mask;
}

/* invert bits for xilinx/intel formats conversion */
void invertbits(unsigned char *buf, unsigned int size)
{
  unsigned char c;
  while(size)
    {
      c=*buf;
      *buf=((c&0x01)<<7)|((c&0x02)<<5)|((c&0x04)<<3)|((c&0x08)<<1)
	|((c&0x10)>>1)|((c&0x20)>>3)|((c&0x40)>>5)|((c&0x80)>>7);
      buf++;
      size--;
    }
}

/* Read data from flash starting from the given offset */
static void spi_flash_read(unsigned char *dst,u32 offset,u32 count)
{
  /* send 4 bytes of command and address, then receive data */
  unsigned char spi_cmd_buffer[4];
  spi_cmd_buffer[0]=SPI_CMD_READ;
  spi_cmd_buffer[1]=(offset>>16)&0xff;
  spi_cmd_buffer[2]=(offset>>8)&0xff;
  spi_cmd_buffer[3]=offset&0xff;
  spi_flash_transfer(spi_cmd_buffer,4,dst,count,4);
  //invertbits(dst,count);
}
#endif

#ifdef DEBUG
char *rstrings[]={
  "Z_VERSION_ERROR",
  "Z_BUF_ERROR",
  "Z_MEM_ERROR",
  "Z_DATA_ERROR",
  "Z_STREAM_ERROR",
  "Z_ERRNO",
  "Z_OK",
  "Z_STREAM_END",
  "Z_NEED_DICT"
};
#endif

/* Flat Device Tree scanning */

/* this flag is set if any sanity check fails */
static int fdt_corrupt_flag=0;

/* sanity check for FDT pointers */
static int fdt_sanity_check(struct fdt_header *fdt, void *ptr)
{
  int r;
  r=(!ptr
     ||((unsigned char*)ptr <= (unsigned char*) fdt)
     ||((unsigned char*)ptr >= ((unsigned char*) fdt + fdt-> totalsize)));
  fdt_corrupt_flag |= r;
  return r;
}

/*
  sanity check for FDT strings 
  (including strings in property names and data)
*/
static int fdt_sanity_check_string(struct fdt_header *fdt, char *ptr)
{
  int r;
  r=(!ptr
     ||(ptr <= (char*) fdt)
     ||(ptr >= ((char*) fdt + fdt-> totalsize)));
  while(!r && *ptr)
    {
      ptr++;
      r|=(ptr >= ((char*) fdt + fdt-> totalsize));
    }
  fdt_corrupt_flag |= r;
  return r;
}

/* get first property of the node */
static struct fdt_property *fdt_first_property(struct fdt_header *fdt,
					       struct fdt_node_header *node)
{
  unsigned char *ptr;
  if(fdt_sanity_check (fdt, node))
    return NULL;

  ptr=(unsigned char *)node+sizeof(node->tag);
  while(*ptr && (ptr < ((unsigned char*) fdt + fdt-> totalsize)))
    ptr++;
  
  ptr++;
  if(fdt_sanity_check (fdt, ptr))
    return NULL;
  
  return (struct fdt_property*)(((unsigned int)(ptr-1)&(~3))+4);
}

/* skip property (may return a non-property pointer) */
static struct fdt_property *fdt_skip_property(struct fdt_header *fdt,
					      struct fdt_property *property)
{
  unsigned char *ptr;
  if(fdt_sanity_check (fdt, property))
    return NULL;

  ptr=(unsigned char *)property+sizeof(*property)+property->len;
  if(fdt_sanity_check (fdt, ptr))
    return NULL;

  return (struct fdt_property*)(((unsigned int)(ptr-1)& ~3)+4);
}

unsigned char mac_address_0[6],mac_address_1[6],
  mac_address_0_found=0,mac_address_1_found=0;
u32 serial_number=0;
u32 entity_model_id=0;

/* FDT scan and validation, returns command line arguments if found */
unsigned char *fdt_scan_find_arguments(struct fdt_header *fdt)
{
  struct fdt_node_header *curr_node;
  struct fdt_property *curr_property;
  char *property_name;
  unsigned char *retval=NULL;
  int node_level,found_chosen_node,
    found_ethernet_dt_bus,
    found_ethernet_dt_device_0,found_ethernet_dt_device_1;

  /* check if FDT signature is present */
  if(fdt->magic != FDT_MAGIC)
    {
#if DEBUG_PRINT > 0
      print_simple("FDT does not start from a valid signature\r\n");
#endif
     fdt_corrupt_flag=1;
      return NULL;
    }
  
  /* get root node */
  curr_node=(struct fdt_node_header*)
    ((unsigned char*)fdt+fdt->off_dt_struct);
  if(fdt_sanity_check(fdt,curr_node))
    return NULL;

  if(curr_node->tag != FDT_BEGIN_NODE)
    {
#if DEBUG_PRINT > 0
      print_simple("FDT does not have a valid begin node\r\n");
#endif
      fdt_corrupt_flag=1;
      return NULL;
    }
  curr_property=fdt_first_property(fdt,curr_node);
  node_level=0;
  found_chosen_node=0;
  found_ethernet_dt_bus=0;
  found_ethernet_dt_device_0=0;
  found_ethernet_dt_device_1=0;
  while(!fdt_sanity_check(fdt,curr_property)
	&& curr_property->tag != FDT_END 
	&& !(node_level==0 && curr_property->tag == FDT_END_NODE))
    {
      switch(curr_property->tag)
	{
	case FDT_BEGIN_NODE:
	  curr_node=(struct fdt_node_header*)curr_property;
	  node_level++;
	  if(fdt_sanity_check_string(fdt,curr_node->name))
	    return NULL;

	  found_chosen_node=node_level==1 && !strcmp(curr_node->name,"chosen");
#ifdef ETHERNET_DT_BUS
	  found_ethernet_dt_bus=node_level==1 &&
	    !strcmp(curr_node->name,ETHERNET_DT_BUS);
#ifdef ETHERNET_DT_DEVICE_0
	  found_ethernet_dt_device_0=node_level==2 &&
	    !strcmp(curr_node->name,ETHERNET_DT_DEVICE_0);
#endif
#ifdef ETHERNET_DT_DEVICE_1
	  found_ethernet_dt_device_1=node_level==2 &&
	    !strcmp(curr_node->name,ETHERNET_DT_DEVICE_1);
#endif
#endif
	  curr_property=fdt_first_property(fdt,curr_node);
	  break;
	  
	case FDT_PROP:
	  if(found_chosen_node)
	    {
	      property_name=(char*)fdt+curr_property->nameoff
		+fdt->off_dt_strings;
	      if(fdt_sanity_check_string(fdt,property_name))
		return NULL;
	      
	      if(!strcmp(property_name,"bootargs"))
		retval=(unsigned char*)&(curr_property->data);
	      if(!strcmp(property_name,"serial-number")
		 &&(curr_property->len==sizeof(serial_number)))
		memcpy(&serial_number,(unsigned char*)&(curr_property->data),
		       sizeof(serial_number));
	      if(!strcmp(property_name,"entity-model-id")
		 &&(curr_property->len==sizeof(entity_model_id)))
		memcpy(&entity_model_id,(unsigned char*)&(curr_property->data),
		       sizeof(entity_model_id));
	    }
#ifdef ETHERNET_DT_DEVICE_0
	  if(found_ethernet_dt_device_0)
	    {
	      property_name=(char*)fdt+curr_property->nameoff
		+fdt->off_dt_strings;
	      if(fdt_sanity_check_string(fdt,property_name))
		return NULL;
	      
	      if(!strcmp(property_name,"local-mac-address")
		 && curr_property->len==6)
		{
		  memcpy(mac_address_0,(unsigned char*)&(curr_property->data),6);
		  mac_address_0_found=1;
		}
	    }
#endif
#ifdef ETHERNET_DT_DEVICE_1
	  if(found_ethernet_dt_device_1)
	    {
	      property_name=(char*)fdt+curr_property->nameoff
		+fdt->off_dt_strings;
	      if(fdt_sanity_check_string(fdt,property_name))
		return NULL;
	      
	      if(!strcmp(property_name,"local-mac-address")
		 && curr_property->len==6)
		{
		  memcpy(mac_address_1,(unsigned char*)&(curr_property->data),6);
		  mac_address_1_found=1;
		}
	    }
#endif
	  curr_property=fdt_skip_property(fdt,curr_property);
	  break;
	  
	case FDT_END_NODE:
	  if(node_level==1)
	    {
	      found_chosen_node=0;
#ifdef ETHERNET_DT_BUS
	      found_ethernet_dt_bus=0;
#endif
	    }
	  if(node_level==2)
	    {
#ifdef ETHERNET_DT_DEVICE_0
	      found_ethernet_dt_device_0=0;
#endif
#ifdef ETHERNET_DT_DEVICE_1
	      found_ethernet_dt_device_1=0;
#endif
	    }
	  node_level--;
	  
	case FDT_NOP:
	  curr_property=(struct fdt_property*)
	    (((unsigned char*)curr_property)+FDT_TAGSIZE);
	  break;
	  
	default:
#if DEBUG_PRINT > 0
      print_simple("FDT contains unknown tag ");
      print_simple_dec(curr_property->tag);
      print_simple("\r\n");
#endif
	  fdt_corrupt_flag=1;
	  return NULL;
	}
    }
  return retval;
}

/*
  FIXME -- network/ethernet initialization should be in a
  platform-dependent file
*/

/* main */

int main(void)
{
#if FLASH_IS_BPI
#else
  static unsigned char flashbuffer[BOOT_IMAGE_HEADER_SIZE];
#endif
  int i,r;
  unsigned int boot_screen_compressed_size,font_compressed_size,
    kernel_compressed_size;
  /* zlib stream */
  z_stream zstream;
  
  /* Pointers to "important" areas in flash and RAM */
  
#if FLASH_IS_BPI
  /* Boot image is in flash at FLASH_BOOT_IMAGE_OFFSET kilobytes */
  unsigned char *bootimage_start;

  /* Current section for scanning */
  unsigned char *cur_section;
#else
  /* Boot image is in flash at FLASH_BOOT_IMAGE_OFFSET kilobytes */
  u32 bootimage_start;

  /* Current section for scanning */
  u32 cur_section;
#endif
  unsigned int cur_section_type,cur_section_len,
    cur_section_counter,cur_section_failure;
  
  unsigned int fonts_count=0;
  /*
    Device tree, Kernel, boot screen, fonts and ramdisk locations
    should be calculated 
  */
  volatile unsigned char *dt_blob=NULL;
#if FLASH_IS_BPI
  volatile unsigned char *kernel_orig=NULL,*ramdisk_orig=NULL,
    *boot_screen=NULL,*fonts[MAX_FONTS];
#else
  u32 kernel_orig=0,ramdisk_orig=0,boot_screen=0,fonts[MAX_FONTS];
#endif

  /* Kernel should be loaded at the start of DRAM */
  volatile unsigned char *kernel_load_addr=(unsigned char*)
    DDR_BASEADDR;
  
  /* Alternative location of the ramdisk -- for debugging only */
  volatile unsigned char *ramdisk_alt_addr=(unsigned char*)RD_ALT_ADDR;
  
  /* Command line */
  unsigned char *cmdline=NULL,*buttons_str, *ptr;
  microblaze_enable_icache();
  microblaze_enable_dcache();
#ifndef SINGLE_STAGE
  print_simple("\x1b[0m\x1b[JMBBL - microblaze bootloader, second stage\r\n");
#else
  print_simple("\r\n\x1b[0m\x1b[JMBBL - microblaze bootloader\r\n");
#endif
  
#if FLASH_IS_BPI
  /*
    CFI is supposed to be readable after power-up.
    Not necessarily so after FPGA initialization over JTAG.
  */
  cfi_read_init();
#else
  spi_flash_init();
  spi_flash_slave_select(0x01);
#endif
  
  /* get start offset at the start of the dynamic RAM */
#ifndef SINGLE_STAGE
  platform_rs_offset=*((u32*)DDR_BASEADDR);
#else
  platform_rs_offset=0;
#endif
  
  platform_init();
  
#if 0
  bootimage_start=(unsigned char*)(BPI_FLASH_BASEADDR
				   +FLASH_BOOT_IMAGE_OFFSET*1024
				   +platform_rs_offset);
#else
#if FLASH_IS_BPI  
  /* If bitstream header is present, use offset encoded in it */
  bootimage_start=(unsigned char*)(BPI_FLASH_BASEADDR
				   +platform_rs_offset);
  if(memcmp(bootimage_start,"MBBL BITSTREAM\0\0",16)
     &&memcmp(bootimage_start,"MBBL BOOTLOADER\0",16))
    bootimage_start+=FLASH_BOOT_IMAGE_OFFSET*1024;
  else
    bootimage_start=(unsigned char*)(BPI_FLASH_BASEADDR
				     +((((u32)bootimage_start[24])<<24)
				       |(((u32)bootimage_start[26])<<16)
				       |(((u32)bootimage_start[28])<<8)
				       |(u32)bootimage_start[30]));
#else
  spi_flash_read(flashbuffer,platform_rs_offset,sizeof(flashbuffer));
#if DEBUG_PRINT > 2
  print_simple("Start of the flash image:\r\n");
  for(i=0;i<BOOT_IMAGE_HEADER_SIZE;i++)
    {
      print_simple_hex_8(flashbuffer[i]);
      if((i&0x0f)==0x0f)
	print_simple("\r\n");
      else
	putchar_simple(' ');
    }
  print_simple("\r\n");
#endif
  if(memcmp(flashbuffer,"MBBL BITSTREAM\0\0",16)
     &&memcmp(flashbuffer,"MBBL BOOTLOADER\0",16))
    bootimage_start=FLASH_BOOT_IMAGE_OFFSET*1024;
  else
    bootimage_start=((((u32)flashbuffer[24])<<24)
		     |(((u32)flashbuffer[26])<<16)
		     |(((u32)flashbuffer[28])<<8)
		     |(u32)flashbuffer[30]);
  spi_flash_read(flashbuffer,bootimage_start,BOOT_IMAGE_HEADER_SIZE);
#endif
#endif
  /* Find all sections */
  if(!memcmp(
#if FLASH_IS_BPI
	     bootimage_start
#else
	     flashbuffer
#endif
	     ,BOOT_IMAGE_HEADER,BOOT_IMAGE_HEADER_SIZE))
    {
      /* Image header signarure matches, parse the image */
      cur_section=bootimage_start+BOOT_IMAGE_HEADER_SIZE;
      cur_section_counter=0;
      cur_section_failure=0;
      do
	{
#if FLASH_IS_BPI
	  cur_section_type=*((unsigned int*)cur_section);
	  cur_section_len=*((unsigned int*)(cur_section+
					    sizeof(unsigned int)));
#else
	  spi_flash_read(flashbuffer,cur_section,2*sizeof(unsigned int));
	  cur_section_type=*((unsigned int*)(&flashbuffer[0]));
	  cur_section_len=*((unsigned int*)
			    &(flashbuffer[sizeof(unsigned int)]));
#endif
	  switch(cur_section_type)
	    {
	    case REC_TYPE_FDT:
#if DEBUG_PRINT > 0
	      print_simple("FDT\r\n");
#endif
#if FLASH_IS_BPI
	      dt_blob=cur_section+2*sizeof(unsigned int);
#else
	      if(dt_blob==NULL)
		{
		  dt_blob=malloc(cur_section_len);
		  if(dt_blob!=NULL)
		    {
		      spi_flash_read((unsigned char *)dt_blob,
				     cur_section+2*sizeof(unsigned int),
				     cur_section_len);
#if DEBUG_PRINT > 0
		      print_simple("Starts with ");
		      print_simple_hex_32(*(int*)dt_blob);
		      print_simple("\r\n");
#endif
		    }
		}
#endif
	      break;
	    case REC_TYPE_KERNEL:
#if DEBUG_PRINT > 0
	      print_simple("Kernel\r\n");
#endif
	      kernel_orig=cur_section+2*sizeof(unsigned int);
	      break;
	    case REC_TYPE_RDISK:
#if DEBUG_PRINT > 0
	      print_simple("Ramdisk\r\n");
#endif
	      ramdisk_orig=cur_section+2*sizeof(unsigned int);
	      break;
	    case REC_TYPE_BOOT_SCREEN:
#if DEBUG_PRINT > 0
	      print_simple("Boot screen\r\n");
#endif
	      boot_screen=cur_section+2*sizeof(unsigned int);
	      break;
	    case REC_TYPE_FONT:
#if DEBUG_PRINT > 0
	      print_simple("Font\r\n");
#endif
	      if(fonts_count<MAX_FONTS)
		fonts[fonts_count++]=cur_section+2*sizeof(unsigned int);

	      break;
	    default:
	      /* ignore other sections */
	      break;
	    }
	  cur_section+=cur_section_len+2*sizeof(unsigned int);
	  
	  /* Sanity checks */
	  if(((++cur_section_counter) > MAX_SECTIONS_COUNT)
	     ||(cur_section > 
#if FLASH_IS_BPI
		(unsigned char*)BPI_FLASH_HIGHADDR
#else
		SPI_FLASH_SIZE
#endif
		)
	     ||(cur_section < bootimage_start+BOOT_IMAGE_HEADER_SIZE))
	    cur_section_failure=1;

	}
      while((cur_section_type!=REC_TYPE_RDISK)
	    && (cur_section_type!=REC_TYPE_END)
	    && (!cur_section_failure));
      /* Either ramdisk or end marker must be the last section */
      if(cur_section_failure)
	{
	  /*
	    dt_blob=NULL;
	    kernel_orig=NULL;
	  */
	  ramdisk_orig=NULL;
	}
    }



  /* 
     If fonts are present, uncompress them into the kernel buffer
     and let platform-dependent routines copy them to the heap
  */
  for(i=0;i<fonts_count;i++)
    {
      r=-1;
#if FLASH_IS_BPI
      font_compressed_size=*((unsigned int*)(fonts[i]-sizeof(unsigned int)));
#else
      spi_flash_read((unsigned char*)&font_compressed_size,
		     fonts[i]-sizeof(unsigned int),
		     sizeof(font_compressed_size));
#endif
      /* gzip */
      zstream.zalloc = Z_NULL;
      zstream.zfree = Z_NULL;
      zstream.opaque = Z_NULL;
      zstream.next_in  = (unsigned char*)DT_ALT_ADDR-font_compressed_size;
      zstream.avail_in = font_compressed_size;
      zstream.next_out = (unsigned char*)kernel_load_addr;
      zstream.avail_out = (unsigned char*)DT_ALT_ADDR
	-(unsigned char*)kernel_load_addr
	-font_compressed_size;
#if FLASH_IS_BPI
      memcpy((unsigned char*)DT_ALT_ADDR-font_compressed_size,
	     (unsigned char*)fonts[i],
	     font_compressed_size);
#else
       spi_flash_read((unsigned char*)DT_ALT_ADDR-font_compressed_size,
		      fonts[i],font_compressed_size);
#endif      
      r = inflateInit2(&zstream,16+MAX_WBITS);
      while ((zstream.total_in < font_compressed_size)&&(r==Z_OK))
	r = inflate(&zstream, Z_NO_FLUSH);

      /* If font uncompressed successfully, load it */
      if(r==Z_STREAM_END)
	platform_load_font((unsigned char*)kernel_load_addr,
			   zstream.total_out);

      inflateEnd(&zstream);
    }
  
  /* If boot screen image is present, uncompress it into the kernel buffer */
  if(boot_screen!=NULL)
    {
      r=-1;
#if FLASH_IS_BPI
      boot_screen_compressed_size=
	*((unsigned int*)(boot_screen-sizeof(unsigned int)));
#else
      spi_flash_read((unsigned char*)&boot_screen_compressed_size,
		     boot_screen-sizeof(unsigned int),
		     sizeof(boot_screen_compressed_size));
#endif
      
      /* gzip */
      zstream.zalloc = Z_NULL;
      zstream.zfree = Z_NULL;
      zstream.opaque = Z_NULL;
      zstream.next_in  = 
	(unsigned char*)DT_ALT_ADDR-boot_screen_compressed_size;
      zstream.avail_in = boot_screen_compressed_size;
      zstream.next_out = (unsigned char*)kernel_load_addr;
      zstream.avail_out = (unsigned char*)DT_ALT_ADDR
	-(unsigned char*)kernel_load_addr
	-boot_screen_compressed_size;
#if FLASH_IS_BPI      
      memcpy((unsigned char*)DT_ALT_ADDR-boot_screen_compressed_size,
	     (unsigned char*)boot_screen,
	     boot_screen_compressed_size);
#else
      spi_flash_read((unsigned char*)DT_ALT_ADDR-boot_screen_compressed_size,
		      boot_screen,boot_screen_compressed_size);
#endif
      
      r = inflateInit2(&zstream,16+MAX_WBITS);
      while ((zstream.total_in < boot_screen_compressed_size)&&(r==Z_OK))
	r = inflate(&zstream, Z_NO_FLUSH);
      
      /* If image uncompressed successfully, display it */
      if(r==Z_STREAM_END)
	platform_display_boot_screen((unsigned char*)kernel_load_addr,
				     zstream.total_out);

      inflateEnd(&zstream);
    }

  /* turn on the screen if it's not on yet */
  platform_display_on();

  /*
    FIXME -- network/ethernet initialization should be in a
    platform-dependent file
  */

#ifdef HARDWARE_CAL_ICS
      mvEthE6350RSwitchInit();
#endif

  /* Device tree */
  do
    {
      fdt_corrupt_flag=0;
      *(volatile unsigned char*)DT_ALT_ADDR='\0';
      if((dt_blob==NULL)||((*(int*)dt_blob)!=FDT_MAGIC))
	{
	  dt_blob=(volatile unsigned char*)DT_ALT_ADDR;
	  kernel_orig=NULL;
	  ramdisk_orig=NULL;
	  *dt_blob='\0';
	  platform_display_error(" NO FDT ");
	  print_simple("No device tree\r\n");
	  install_warning("binary device tree",DT_ALT_ADDR);
	  /* Hang until the user fixes it (or watchdog resets us) */
	  timer0_countdown_start(5000);
	  while(*dt_blob=='\0'&&!timer0_countdown_expired());
	  if(*dt_blob=='\0') 
	    platform_reboot_image(platform_rs_offset==0);
	}
      cmdline=fdt_scan_find_arguments((struct fdt_header *)dt_blob);
      
      /* show progress */
      platform_display_progress(1,fdt_corrupt_flag?
				PROGRESS_COLOR_RED:
				PROGRESS_COLOR_GREEN);
      if(fdt_corrupt_flag)
	platform_display_error(" NO FDT ");

    } while(fdt_corrupt_flag);

  if(mac_address_0_found)
    {
      print_simple("MAC address 0 ");
      print_simple_hex_8(mac_address_0[0]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_0[1]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_0[2]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_0[3]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_0[4]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_0[5]);
      print_simple("\r\n");
    }
  if(mac_address_1_found)
    {
      print_simple("MAC address 1 ");
      print_simple_hex_8(mac_address_1[0]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_1[1]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_1[2]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_1[3]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_1[4]);
      putchar_simple(':');
      print_simple_hex_8(mac_address_1[5]);
      print_simple("\r\n");
    }

  if(mac_address_0_found || mac_address_1_found)
    {
      print_simple("Serial number ");
      print_simple_dec(serial_number);
      print_simple(", entity model id ");
      print_simple_dec(entity_model_id);
      print_simple("\r\n");
      
      /*
	FIXME -- network/ethernet initialization should be in a
	platform-dependent file
      */
#ifdef HARDWARE_CAL_ICS
      labx_eth_init(mac_address_0,mac_address_1);

      /*
	use ADP/AEM protocol only while booted in the maintenance mode
	but intended to switch into regular mode
       */
      if((platform_rs_offset==0)&&(platform_buttons==0))
	{
	  int network_counter,received_aem=0;
	  for(network_counter=0;received_aem==0&&network_counter<5;network_counter++)
	    {
	      timer1_countdown_start(1000);
	      print_simple("Sending ADP frame\r\n");
	      if(send_adp_frame(0,mac_address_0,
				mac_address_0,
				serial_number,
				entity_model_id)<=0)
		print_simple("Link is down on port 0\r\n");
	      if(send_adp_frame(1,mac_address_0,
				mac_address_1,
				serial_number,
				entity_model_id)<=0)
		print_simple("Link is down on port 1\r\n");
	      
	      do
		{
		  received_aem=receive_aem_frame(0,mac_address_0,
						 mac_address_0,
						 serial_number);
		  received_aem|=receive_aem_frame(1,mac_address_0,
						  mac_address_1,
						  serial_number);
		}
	      while(!received_aem&&!timer1_countdown_expired());
	    }
	  if(received_aem)
	    {
	      print_simple("Received AEM boot request\r\n");
	    }
	}
#endif
    }

  /* reboot if requested */
  if(reboot_request>=0)
    {
      mdelay(100);
      platform_reboot_image(reboot_request);
    }
  
  /* Ramdisk */
  if(ramdisk_orig!=NULL
#if FLASH_IS_BPI
     && ramdisk_orig[0]==0x1f && ramdisk_orig[1]==0x8b
#endif
     )
    {
      /*
	If there is a ramdisk in flash, erase first two bytes of ramdisk in
	alternative area in RAM, so it won't be accidentally loaded from there
      */
      ((unsigned char*)RD_ALT_ADDR)[0]='\0';
      ((unsigned char*)RD_ALT_ADDR)[1]='\0';
    }
  else
    {
      platform_display_progress(2,PROGRESS_COLOR_RED);
      platform_display_error("NO RDISK");
      
      if(((unsigned char*)RD_ALT_ADDR)[0]==0x1f
	 && ((unsigned char*)RD_ALT_ADDR)[1]==0x8b)
	{
	  /*
	    Someone already uploaded a RAM disk -- print a huge warning
	    and let the kernel load it.
	  */
	  platform_display_progress(2,PROGRESS_COLOR_RED);
	  
	  print_simple("Ramdisk image not found in flash,\r\n"
	    "however alternative location in RAM has ramdisk signature\r\n\n"
		"*** UNLESS YOU ARE TESTING RAMDISK IMAGES, "
		"SOMETHING IS WRONG ! ***\r\n\n");
	}
      else
	{
	  *ramdisk_alt_addr='\0';
	  print_simple("No ramdisk image\r\n");
	  install_warning("compressed ramdisk image",RD_ALT_ADDR);
	  /* Hang until the user fixes it (or watchdog resets us) */
	  timer0_countdown_start(5000);
	  while(*ramdisk_alt_addr=='\0'&&!timer0_countdown_expired());
	  if(*ramdisk_alt_addr=='\0') 
	    platform_reboot_image(platform_rs_offset==0);
	}
    }
  
  /* show progress */
  platform_display_progress(2,PROGRESS_COLOR_GREEN);
  
  /* Kernel */
  /* at this point boot screen will be overwritten */
  r=-1;
  if(kernel_orig!=NULL)
    {
#if FLASH_IS_BPI
      kernel_compressed_size=*((unsigned int*)
			       (kernel_orig-sizeof(unsigned int)));
#else
      spi_flash_read((unsigned char*)&kernel_compressed_size,
		     kernel_orig-sizeof(unsigned int),
		     sizeof(kernel_compressed_size));
#endif
      
      /* gzip */
      zstream.zalloc = Z_NULL;
      zstream.zfree = Z_NULL;
      zstream.opaque = Z_NULL;
      zstream.next_in  = (unsigned char*)DT_ALT_ADDR-kernel_compressed_size;
      zstream.avail_in = kernel_compressed_size;
      zstream.next_out = (unsigned char*)kernel_load_addr;
      zstream.avail_out = (unsigned char*)DT_ALT_ADDR
	-(unsigned char*)kernel_load_addr
	-kernel_compressed_size;
#if FLASH_IS_BPI      
      memcpy((unsigned char*)DT_ALT_ADDR-kernel_compressed_size,
	     (unsigned char*)kernel_orig,
	     kernel_compressed_size);
#else
      spi_flash_read((unsigned char*)DT_ALT_ADDR-kernel_compressed_size,
		      kernel_orig,kernel_compressed_size);
#endif
      
      r = inflateInit2(&zstream,16+MAX_WBITS);
#ifdef DEBUG
      print_simple(rstrings[r+6]);
      print_simple("\r\n");
#endif
      print_simple("Unpacking kernel (press \x1b[1mCtrl-C\x1b[0m \x1b[1mCtrl-C"
	    "\x1b[0m to cancel)...");
      while ((kernel_orig!=NULL)
	     &&(zstream.total_in < kernel_compressed_size)&&(r==Z_OK))
	{
	  r = inflate(&zstream, Z_NO_FLUSH);
#ifdef DEBUG
	  print_simple(rstrings[r+6]);
	  print_simple("\r\n");
#endif
	  /* Check for two Ctrl-C */
	  if(console_input_ready()
	     &&(getchar_simple()==3)
	     &&console_input_ready()
	     &&(getchar_simple()==3))
	    {
	      kernel_orig=NULL;
	      print_simple("\x1b[35D *** cancelled by user ***\x1b[K\r\n");
	    }
	}
      inflateEnd(&zstream);
    }
  
  /* If kernel loading failed, let the user load the image directly */
  if(kernel_orig==NULL||r!=Z_STREAM_END)
    {
      platform_display_progress(3,PROGRESS_COLOR_RED);
      platform_display_error("NO KERNL");
      
      *kernel_load_addr='\0';
      if(kernel_orig!=NULL)
	print_simple("\x1b[35D failed\x1b[K\r\n");

      install_warning("kernel image",DDR_BASEADDR);
      /* Hang until the user fixes it (or watchdog resets us) */
      timer0_countdown_start(5000);
      while(*kernel_load_addr=='\0'&&!timer0_countdown_expired());
      if(*kernel_load_addr=='\0') 
	platform_reboot_image(platform_rs_offset==0);

      if((*(int*)DT_ALT_ADDR)==0xd00dfeed)
	{
	  dt_blob=(volatile unsigned char*)DT_ALT_ADDR;
	  cmdline=fdt_scan_find_arguments((struct fdt_header *)dt_blob);
	}
    }
  else
    print_simple("\x1b[35D done\x1b[K\r\n");
  
  /* show progress */
  platform_display_progress(3,PROGRESS_COLOR_GREEN);

  print_simple("Launching kernel\r\n");

  if(!cmdline)
    /* can't do anything with the command line if it's not in FDT */
    cmdline=(unsigned char*)CMDLINE;
  else
    {
      /* check if platform-dependent initialization left any button flags on */
      if(platform_buttons!=0)
	{
	  /* if FDT is in ROM, move it to the RAM */
	  if(dt_blob!=(volatile unsigned char*)DT_ALT_ADDR);
	  {
	    memcpy((unsigned char*)DT_ALT_ADDR,(unsigned char*)dt_blob,
		   ((struct fdt_header*)dt_blob)->totalsize);
	    cmdline+=(volatile unsigned char*)DT_ALT_ADDR-dt_blob;
	    dt_blob=(volatile unsigned char*)DT_ALT_ADDR;
	  }
	  /* find the command line entry "BUTTONS=0x..." */
	  buttons_str=(unsigned char*)strstr((char*)cmdline," BUTTONS=0x");
	  if(buttons_str!=NULL)
	    buttons_str++;
	  else
	    /* 
	       it's possible that this is the
	       start of the command line
	    */
	    if(!strcmp((char*)cmdline,"BUTTONS=0x"))
	      buttons_str=cmdline;
	  
	  /* 
	     modify the string in memory, so it says "BUTTONS=0x01",
	     add spaces so subsequent characters from the original string
	     won't overlap with it.
	  */
	  if(buttons_str!=0)
	    {
	      ptr=buttons_str+10;
	      while(*ptr > ' ')
		*ptr++=' ';
	      /* 
		 modify string only if there is enough space for the
		 new value.
		 FIXME: only one bit is supported.
	      */
	      if(ptr >= buttons_str+12)
		{
		  buttons_str[10]='0';
		  buttons_str[11]='1';
		}
	    }
	}
    }
  
  /* launcher function (only address and device tree actually work) */
  launch(kernel_load_addr,cmdline,NULL,dt_blob);

  /* not reached */
  while(1);
  return 0;
}
