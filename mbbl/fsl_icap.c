/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/

#include "microblaze_fsl.h"
#include "mbbl-timer.h"
#include "fsl_icap.h"
#include "mbbl-platform.h"


unsigned int fsl_read_general5(void)
{
  unsigned int value;
  do
    {
      putfslx(0x0FFFF, 0, FSL_CONTROL_ATOMIC);
      mdelay(1);
      
      // Synchronize command bytes
      putfslx(0xFFFF, 0, FSL_ATOMIC); // Pad words
      putfslx(0xFFFF, 0, FSL_ATOMIC);
      putfslx(0xAA99, 0, FSL_ATOMIC); // SYNC
      putfslx(0x5566, 0, FSL_ATOMIC); // SYNC
      putfslx(0x2000, 0, FSL_ATOMIC); // NOOP
      putfslx(0x2ae1, 0, FSL_ATOMIC); // Read GENERAL5
      putfslx(0x2000, 0, FSL_ATOMIC); // NOOP
      putfslx(0x2000, 0, FSL_ATOMIC); // NOOP
      putfslx(0x2000, 0, FSL_ATOMIC); // NOOP
      putfslx(0x2000, 0, FSL_ATOMIC); // NOOP
      // Trigger the FSL peripheral to drain the FIFO into the ICAP
      putfslx(FINISH_FSL_BIT | 0x2000, 0, FSL_ATOMIC);
      mdelay(1);
      getfslx(value, 0, FSL_ATOMIC);
      putfslx(0x30a1, 0, FSL_ATOMIC); // Type 1 Write 1 Word to CMD
      putfslx(0x000d, 0, FSL_ATOMIC); // DESYNC
      putfslx(0x2000, 0, FSL_ATOMIC); // NOOP
      putfslx(0x2000, 0, FSL_ATOMIC); // NOOP
      // Trigger the FSL peripheral to drain the FIFO into the ICAP
      putfslx(FINISH_FSL_BIT | 0x2000, 0, FSL_ATOMIC);
    }
  while(value&ICAP_FSL_FAILED);
  value&=0xffff;
  return value;
}

void fsl_reboot_image(unsigned int addr,unsigned int general5)
{
  while (1)
    {
      putfslx(0x0FFFF, 0, FSL_CONTROL_ATOMIC);
      mdelay(1);

      // Synchronize command bytes
      putfslx(0xFFFF, 0, FSL_ATOMIC); // Pad words
      putfslx(0xFFFF, 0, FSL_ATOMIC);
      putfslx(0xAA99, 0, FSL_ATOMIC); // SYNC
      putfslx(0x5566, 0, FSL_ATOMIC); // SYNC
      
      // Write the reconfiguration FPGA offset.
      // Write GENERAL1
      putfslx(0x3261, 0, FSL_ATOMIC); 
      // Multiboot start address[15:0]
      putfslx(((addr >> 0) & 0x0FFFF), 0, FSL_ATOMIC);
      // Write GENERAL2
      putfslx(0x3281, 0, FSL_ATOMIC); 
      // Opcode 0x03 and address[23:16]
      putfslx(((addr >> 16) & 0x0FF) | 0x0300, 0, FSL_ATOMIC);
      
      // Write the fallback FPGA offset (this image)
      // Write GENERAL3 
      putfslx(0x32A1, 0, FSL_ATOMIC); 
      // Multiboot start address[15:0]
      putfslx(((platform_rs_offset >> 0) & 0x0FFFF), 0, FSL_ATOMIC);
      // Write GENERAL4
      putfslx(0x32C1, 0, FSL_ATOMIC); 
      // Opcode 0x03 and address[23:16]
      putfslx(((platform_rs_offset >> 16) & 0x0FF) | 0x0300, 0, FSL_ATOMIC);

      // Write GENERAL5
      putfslx(0x32e1, 0, FSL_ATOMIC); 
      // value to store in the register
      putfslx(general5, 0,  FSL_ATOMIC);
      
      // Write IPROG command
      putfslx(0x30A1, 0, FSL_ATOMIC); // Write CMD
      putfslx(0x000E, 0, FSL_ATOMIC); // IPROG Command
      putfslx(0x2000, 0, FSL_ATOMIC); // Type 1 NOP
      
      // Trigger the FSL peripheral to drain the FIFO into the ICAP
      putfslx(FINISH_FSL_BIT | 0x2000, 0, FSL_ATOMIC);
      mdelay(500);
  }
}
