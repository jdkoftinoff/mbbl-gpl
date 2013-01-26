#ifndef MBBL_PLATFORM_H
#define MBBL_PLATFORM_H

/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


#include <stdlib.h>
/*
  Platform-specific functions -- should be implemented in platform support
  sources.
 */

/* platform-specific boot screen updates */
typedef enum {
  PROGRESS_COLOR_GREEN,
  PROGRESS_COLOR_RED,
  PROGRESS_COLOR_YELLOW
} progress_color_t;

extern int platform_buttons;
extern int reboot_request;
extern unsigned long platform_rs_offset;

int platform_init(void);
int platform_load_font(unsigned char *src,size_t size);
int platform_display_boot_screen(unsigned char *src,size_t size);
int platform_display_on(void);
int platform_display_progress(int n, progress_color_t color);
int platform_display_error(char *msg);
int platform_reboot_image(int image);

#endif
