#ifndef MBBL_CONSOLE_H
#define MBBL_CONSOLE_H

/*

Author:          Alex Belits (abelits@nyx.net)
Copyright (C) 2012 Meyer Sound Laboratories Inc.

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 2 of the License, or (at your
option) any later version.

*/


/* Check for character input on the console */
int console_input_ready(void);

/* Get character from the console */
unsigned char getchar_simple(void);

/* Send character to the console */
void putchar_simple(unsigned char data);

/* Print string to the console */
void print_simple(char *s);

/* Print the value of a 8-bit number in hex */
void print_simple_hex_8(unsigned char value);

/* Print the value of a 32-bit number in hex */
void print_simple_hex_32(unsigned long value);

/* Print the value of a number in decimal */
void print_simple_dec(unsigned long value);
#endif
