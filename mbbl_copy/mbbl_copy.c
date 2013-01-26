/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


#include <string.h>
/* Use only low-level headers for SPI and serial devices */
#include "xspi_l.h"
#include "xuartlite_l.h"

/* ELF file format */
#include "elf.h"

/* Xilinx tools generated this */
#include "xparameters.h"

/*= Board-specific parameters below this line ===================*/

/* DDR RAM */
#define DDR_BASEADDR XPAR_MPMC_0_MPMC_BASEADDR
#define DDR_HIGHADDR XPAR_MPMC_0_MPMC_HIGHADDR

/* End of usable memory -- use for stack and heap */
#define DDR_END (DDR_HIGHADDR+1)

/* Console address */
#define SERIAL_CONSOLE_BASEADDR XPAR_SERIAL_CONSOLE_BASEADDR

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

/*= Board-specific parameters above this line ===================*/

/*
  Definition of the boot mode, changes when built 
  for maintenance and regular mode
*/
#include "boot-config.h"

/* Flash start offset */
#ifdef MAINTENANCE_MODE
#define FLASH_START_OFFSET 0x0
#else
#define FLASH_START_OFFSET 0x800000
#endif

#define _STACK_SIZE 0x400
#define _HEAP_SIZE 0x18000
#define _EXEC_SIZE 0x10000

#ifndef DEBUG_PRINT
#define DEBUG_PRINT 0
#endif

/* Very simple poll-based blocking character output */
void putchar_simple(unsigned char data)
{
  while (XUartLite_ReadReg(SERIAL_CONSOLE_BASEADDR,XUL_STATUS_REG_OFFSET)
	 & XUL_SR_TX_FIFO_FULL);
  XUartLite_WriteReg(SERIAL_CONSOLE_BASEADDR,XUL_TX_FIFO_OFFSET,data);
}

/* Very simple string output */
void print_simple(char *s)
{
  while(*s)
    putchar_simple(*s++);
}

void print_simple_hex_8(unsigned char value)
{
  unsigned char c;
  c=(value>>4)+'0';
  if(c>'9')
    c+='a'-'9'-1;
  putchar_simple(c);
  c=(value&0x0f)+'0';
  if(c>'9')
    c+='a'-'9'-1;
  putchar_simple(c);
}

void print_simple_hex_32(u32 value)
{
  print_simple_hex_8((value>>24)&0xff);
  print_simple_hex_8((value>>16)&0xff);
  print_simple_hex_8((value>>8)&0xff);
  print_simple_hex_8(value&0xff);
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

int main(void)
{
  unsigned char *byteptr,*dstptr;
  Elf32_Ehdr *elfheader;
  Elf32_Phdr *phdr;
#if FLASH_IS_BPI
#else
  static unsigned char flashbuffer[32];
  static Elf32_Ehdr elfheader_buf;
  static Elf32_Phdr phdr_buf;
  u32 phdr_offset;
#endif
  u32 startaddr,len;
  int i;
#if DEBUG_PRINT > 0
  int j;
#endif

#if FLASH_IS_BPI
  cfi_read_init();
  byteptr=(unsigned char*)BPI_FLASH_BASEADDR + FLASH_START_OFFSET;
#else
  spi_flash_init();
  spi_flash_slave_select(0x01);
  spi_flash_read(flashbuffer,FLASH_START_OFFSET,sizeof(flashbuffer));
  byteptr=flashbuffer;
#endif
  print_simple("\r\n\x1b[0m\x1b[JMBBL - microblaze bootloader, first stage, "
#ifdef MAINTENANCE_MODE
	       "MAINTENANCE"
#else
	       "REGULAR"
#endif
	       " mode\r\n");
#if DEBUG_PRINT > 0
  for(i=0;i<32;i++)
    print_simple_hex_8(byteptr[i]);
  print_simple("\r\n");
#endif
  startaddr=
    ((((u32)byteptr[17])<<24)
     |(((u32)byteptr[19])<<16)
     |(((u32)byteptr[21])<<8)
     |((u32)byteptr[23]));
  
  len=
    ((((u32)byteptr[24])<<24)
     |(((u32)byteptr[26])<<16)
     |(((u32)byteptr[28])<<8)
     |((u32)byteptr[30]))
    -startaddr;
  
  dstptr=(unsigned char*)(DDR_END-_STACK_SIZE-_HEAP_SIZE - _EXEC_SIZE);

#if FLASH_IS_BPI
  /* for BPI flash: startaddr is the actual address */
  startaddr+=FLASH_START_OFFSET;
  elfheader=(Elf32_Ehdr*)(startaddr);
#else
  /* for SPI flash: "startaddr" is the offset in flash */
  spi_flash_read((unsigned char*)&elfheader_buf,startaddr,
		 sizeof(elfheader_buf));
  elfheader=&elfheader_buf;
#endif
  if(
#if FLASH_IS_BPI
     (startaddr>=FLASH_BASEADDR)&&(startaddr<=FLASH_HIGHADDR)
#else
    startaddr<SPI_FLASH_SIZE
#endif
    )
    {
#if DEBUG_PRINT > 1
      print_simple("Loader: address 0x");
      print_simple_hex_32(startaddr);
      print_simple(", length 0x");
      print_simple_hex_32(len);
      print_simple(", destination 0x");
      print_simple_hex_32((u32)dstptr);
      print_simple("\r\n");
#endif
      if(memcmp(ELFMAG,&elfheader->e_ident,SELFMAG)==0)
	{
#if DEBUG_PRINT > 1
	  print_simple("ELF header found, loading MBBL second stage\r\n");
	  print_simple("Program headers: 0x");
	  print_simple_hex_32(elfheader->e_phnum);
	  print_simple(", each 0x");
	  print_simple_hex_32(elfheader->e_phentsize);
	  print_simple(" bytes\r\n");
#endif
#if FLASH_IS_BPI
	  phdr=(Elf32_Phdr*)(startaddr+elfheader->e_phoff);
#else
	  phdr_offset=startaddr+elfheader->e_phoff;
	  spi_flash_read((unsigned char*)&phdr_buf,phdr_offset,
			 sizeof(phdr_buf));
	  phdr=&phdr_buf;
#endif
	  for(i=0;i<elfheader->e_phnum;i++)
	    {
#if DEBUG_PRINT > 1
	      print_simple("Program header type 0x");
	      print_simple_hex_32(phdr->p_type);
	      print_simple("\r\n");
#endif
	      if(phdr->p_type==PT_LOAD)
		{
#if DEBUG_PRINT > 1
		  print_simple("offset 0x");
		  print_simple_hex_32(phdr->p_offset);
		  print_simple("\r\nvaddr 0x");
		  print_simple_hex_32(phdr->p_vaddr);
		  print_simple("\r\npaddr 0x");
		  print_simple_hex_32(phdr->p_paddr);
		  print_simple("\r\nfilesz 0x");
		  print_simple_hex_32(phdr->p_filesz);
		  print_simple("\r\nmemsz 0x");
		  print_simple_hex_32(phdr->p_memsz);
		  print_simple("\r\n");
#endif
#if FLASH_IS_BPI
		  memcpy((unsigned char*)phdr->p_vaddr,
			 (unsigned char*)startaddr+phdr->p_offset,
			 phdr->p_filesz);
#else
		  spi_flash_read((unsigned char*)phdr->p_vaddr,
				 startaddr+phdr->p_offset,
				 phdr->p_filesz);
#endif
		  if(phdr->p_filesz<phdr->p_memsz)
		    memset((unsigned char*)phdr->p_vaddr+phdr->p_filesz,
			   0,phdr->p_memsz-phdr->p_filesz);
#if DEBUG_PRINT > 0
		  for(j=0;j<phdr->p_memsz;j++)
		    {
		      print_simple_hex_8(((unsigned char*)phdr->p_vaddr)[j]);
		      if((j&0x0f)==0x0f)
			print_simple("\r\n");
		      else
			putchar_simple(' ');
		    }
		  print_simple("\r\n");
#endif
		}
#if FLASH_IS_BPI
	      phdr=(Elf32_Phdr*)((unsigned char*)phdr+elfheader->e_phentsize);
#else
	      phdr_offset+=elfheader->e_phentsize;
	      spi_flash_read((unsigned char*)&phdr_buf,phdr_offset,
			     sizeof(phdr_buf));
#endif
	    }
#if DEBUG_PRINT > 2
	  print_simple("Section headers: 0x");
	  print_simple_hex_32(elfheader->e_shnum);
	  print_simple(", each 0x");
	  print_simple_hex_32(elfheader->e_shentsize);
	  print_simple(" bytes\r\n");
	  for(i=0,
	      shdr=(Elf32_Shdr*)(startaddr+elfheader->e_shoff);
	      i<elfheader->e_shnum;
	      i++,
	      shdr=(Elf32_Shdr*)((unsigned char*)shdr+elfheader->e_shentsize))
	    {
	      print_simple("Section header type 0x");
	      print_simple_hex_32(shdr->sh_type);
	      print_simple("\r\n");
	    }
#endif
#if DEBUG_PRINT > 1
	  print_simple("Entry point 0x");
	  print_simple_hex_32(elfheader->e_entry);
	  print_simple("\r\n");
#endif
	  /* store start offset at the start of dynamic RAM */
	  *((u32*)DDR_BASEADDR)=FLASH_START_OFFSET;
	  ((void(*)(void))elfheader->e_entry)();
	  while(1);
	}
    }
  print_simple("MBBL second stage is not installed\r\n");
  while(1);
  return 0;
}
