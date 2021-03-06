/***************************************************************
 *
 * OpenBeacon.org - PN532 routines for LPC13xx based OpenPCD2
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
#include <openbeacon.h>
#include "rfid.h"

#define PN532_FIFO_SIZE 64
#define PN532_ACK_NACK_SIZE 6

#define RESET_PORT 0
#define RESET_PIN 0
#define CS_PORT 0
#define CS_PIN 2

#define BIT_REVERSE(x) ((unsigned char)(__RBIT(x)>>24))

static void
rfid_reset (unsigned char reset)
{
  GPIOSetValue (RESET_PORT, RESET_PIN, reset);
}

static void
rfid_cs (unsigned char cs)
{
  GPIOSetValue (CS_PORT, CS_PIN, cs);
}

static unsigned char
rfid_tx (unsigned char data)
{
  while ((LPC_SSP->SR & 0x02) == 0);
  LPC_SSP->DR = BIT_REVERSE (data);
  while ((LPC_SSP->SR & 0x04) == 0);
  data = BIT_REVERSE (LPC_SSP->DR);
  return data;
}

static unsigned char
rfid_rx (void)
{
  return rfid_tx (0x00);
}

static unsigned char
rfid_status (void)
{
  unsigned char res;

  /* enable chip select */
  rfid_cs (0);

  /* transmit status request */
  rfid_tx (0x02);
  res = rfid_rx ();

  /* release chip select */
  rfid_cs (1);

  return res;
}

int
rfid_read (void *data, unsigned char size)
{
  int res;
  unsigned char *p, c, pkt_size, crc, prev, t;

  /* wait till PN532 response is ready */
  while ((rfid_status () & 1) == 0);

  /* enable chip select */
  rfid_cs (0);

  /* read from FIFO command */
  rfid_tx (0x03);

  /* default result */
  res = -1;

  /* find preamble */
  t = 0;
  prev = rfid_rx ();
  while ((!(((c = rfid_rx ()) == 0xFF) && (prev == 0x00)))
	 && (t < PN532_FIFO_SIZE))
    {
      prev = c;
      t++;
    }

  if (t >= PN532_FIFO_SIZE)
    res = -3;
  else
    {
      /* read packet size */
      pkt_size = rfid_rx ();

      /* special treatment for NACK and ACK */
      if ((pkt_size == 0x00) || (pkt_size == 0xFF))
	{
	  /* verify if second length byte is inverted */
	  if (rfid_rx () != (unsigned char) (~pkt_size))
	    res = -2;
	  else
	    {
	      /* eat Postamble */
	      rfid_rx ();
	      /* -1 for NACK, 0 for ACK */
	      res = pkt_size ? 0 : -1;
	    }
	}
      else
	{
	  /* verify packet size against LCS */
	  if (((pkt_size + rfid_rx ()) & 0xFF) != 0)
	    res = -4;
	  else
	    {
	      /* remove TFI from packet size */
	      pkt_size--;
	      /* verify if packet fits into buffer */
	      if (pkt_size > size)
		res = -5;
	      else
		{
		  /* remember actual packet size */
		  size = pkt_size;
		  /* verify TFI */
		  if ((crc = rfid_rx ()) != 0xD5)
		    res = -6;
		  else
		    {
		      /* read packet */
		      p = (unsigned char *) data;
		      while (pkt_size--)
			{
			  /* read data */
			  c = rfid_rx ();
			  /* maintain crc */
			  crc += c;
			  /* save payload */
			  if (p)
			    *p++ = c;
			}

		      /* add DCS to CRC */
		      crc += rfid_rx ();
		      /* verify CRC */
		      if (crc)
			res = -7;
		      else
			{
			  /* eat Postamble */
			  rfid_rx ();
			  /* return actual size as result */
			  res = size;
			}
		    }
		}
	    }
	}
    }
  rfid_cs (1);

  /* everything fine */
  return res;
}

int
rfid_write (const void *data, int len)
{
  int i;
  static const unsigned char preamble[] = { 0x01, 0x00, 0x00, 0xFF };
  const unsigned char *p = preamble;
  unsigned char tfi = 0xD4, c;

  if (!data)
    len = 0xFF;

  /* enable chip select */
  rfid_cs (0);

  p = preamble;			/* Praeamble */
  for (i = 0; i < (int) sizeof (preamble); i++)
    rfid_tx (*p++);
  rfid_tx (len + 1);		/* LEN */
  rfid_tx (0x100 - (len + 1));	/* LCS */
  rfid_tx (tfi);		/* TFI */
  /* PDn */
  p = (const unsigned char *) data;
  while (len--)
    {
      c = *p++;
      rfid_tx (c);
      tfi += c;
    }
  rfid_tx (0x100 - tfi);	/* DCS */
  rfid_rx ();			/* Postamble */

  /* release chip select */
  rfid_cs (1);

  /* check for ack */
  return rfid_read (NULL, 0);
}

void
rfid_init (void)
{
  volatile int i;

  /* reset SSP peripheral */
  LPC_SYSCON->PRESETCTRL = 0x01;

  /* Enable SSP clock */
  LPC_SYSCON->SYSAHBCLKCTRL |= (1 << 11);

  // Enable SSP peripheral
  LPC_IOCON->PIO0_8 = 0x01 | (0x01 << 3);	/* MISO, Pulldown */
  LPC_IOCON->PIO0_9 = 0x01;	/* MOSI */

  LPC_IOCON->SCKLOC = 0x00;	/* route to PIO0_10 */
  LPC_IOCON->JTAG_TCK_PIO0_10 = 0x02;	/* SCK */

  /* Set SSP clock to 4.5MHz */
  LPC_SYSCON->SSPCLKDIV = 0x01;
  LPC_SSP->CR0 = 0x0707;
  LPC_SSP->CR1 = 0x0002;
  LPC_SSP->CPSR = 0x02;

  /* Initialize chip select line */
  rfid_cs (1);
  GPIOSetDir (CS_PORT, CS_PIN, 1);

  /* Initialize RESET line */
  rfid_reset (0);
  GPIOSetDir (RESET_PORT, RESET_PIN, 1);
  for (i = 0; i < 100000; i++);
  rfid_reset (1);
}
