/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


#include <xuartlite_l.h>

/* Xilinx tools generated this */
#include "xparameters.h"
#include "mbbl-console.h"

#include "mbbl-board.h"

/* Check for character input on the console */
int console_input_ready(void)
{
  return (XUartLite_ReadReg(SERIAL_CONSOLE_BASEADDR,XUL_STATUS_REG_OFFSET)
	  & XUL_SR_RX_FIFO_VALID_DATA)!=0;
}

/* Get character from the console */
unsigned char getchar_simple(void)
{
  while (!(XUartLite_ReadReg(SERIAL_CONSOLE_BASEADDR,XUL_STATUS_REG_OFFSET)
	   & XUL_SR_RX_FIFO_VALID_DATA));
  return XUartLite_ReadReg(SERIAL_CONSOLE_BASEADDR,XUL_RX_FIFO_OFFSET);
}

/* Send character to the console */
void putchar_simple(unsigned char data)
{
  while (XUartLite_ReadReg(SERIAL_CONSOLE_BASEADDR,XUL_STATUS_REG_OFFSET)
	 & XUL_SR_TX_FIFO_FULL);
  XUartLite_WriteReg(SERIAL_CONSOLE_BASEADDR,XUL_TX_FIFO_OFFSET,data);
}

/* Print string to the console */
void print_simple(char *s)
{
  while(*s)
    putchar_simple(*s++);
}

/* Print the value of a 8-bit number in hex */
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

/* Print the value of a 32-bit number in hex */
void print_simple_hex_32(unsigned long value)
{
  print_simple_hex_8((value>>24)&0xff);
  print_simple_hex_8((value>>16)&0xff);
  print_simple_hex_8((value>>8)&0xff);
  print_simple_hex_8(value&0xff);
}

/* Print the value of a number in decimal */
void print_simple_dec(unsigned long value)
{
  int n=1;
  while((value/n)>=10)
    n*=10;
  while(n)
    {
      putchar_simple((value/n)+'0');
      value%=n;
      n/=10;
    }
}
