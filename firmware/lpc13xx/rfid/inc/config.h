/***************************************************************
 *
 * OpenBeacon.org - config file
 *
 * Copyright 2010 Milosch Meriac <meriac@openbeacon.de>
 *
 ***************************************************************

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; version 2.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*/

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define LED_PORT 1	/* Port for led                      */
#define LED_BIT 9	/* Bit on port for led               */
#define LED_ON 1	/* Level to set port to turn on led  */
#define LED_OFF 0	/* Level to set port to turn off led */

#define USB_VENDOR_ID 0x16C0
#define USB_PROD_ID 0x08B4
#define USB_DEVICE 1

#define USB_HID_IN_REPORT_SIZE 9
#define USB_HID_OUT_REPORT_SIZE 0

#endif/*__CONFIG_H__*/
