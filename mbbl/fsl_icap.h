#ifndef FSL_ICAP_H
#define FSL_ICAP_H

/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


unsigned int fsl_read_general5(void);
void fsl_reboot_image(unsigned int addr, unsigned int general5);
#endif
