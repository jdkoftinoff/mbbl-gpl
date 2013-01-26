#ifndef MBBL_TIMER_H
#define MBBL_TIMER_H

/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


#include "xparameters.h"
#include <xbasic_types.h>
#include "mbbl-board.h"

/* Simple countdown timers (two are provided by a standard Xilinx device) */

static inline void timer0_countdown_start(unsigned int value)
{
  *(volatile u32*)(TIMER_BASEADDR+0x04)=
    value*(CPU_DPLB_FREQ_HZ/1000)-2;
  *(volatile u32*)(TIMER_BASEADDR+0x00)=0x00000122;
  *(volatile u32*)(TIMER_BASEADDR+0x00)=0x00000082;
}

static inline int timer0_countdown_expired(void)
{
  return ((*(volatile u32*)(TIMER_BASEADDR+0x00))&0x00000100)!=0;
}

static inline void timer1_countdown_start(unsigned int value)
{
  *(volatile u32*)(TIMER_BASEADDR+0x14)=
    value*(CPU_DPLB_FREQ_HZ/1000)-2;
  *(volatile u32*)(TIMER_BASEADDR+0x10)=0x00000122;
  *(volatile u32*)(TIMER_BASEADDR+0x10)=0x00000082;
}

static inline int timer1_countdown_expired(void)
{
  return ((*(volatile u32*)(TIMER_BASEADDR+0x10))&0x00000100)!=0;
}
static inline void mdelay(unsigned int value)
{
  timer0_countdown_start(value);
  while(!timer0_countdown_expired());
}

#endif
