/*
 
Public platform independent Near Field Communication (NFC) library
Copyright (C) 2009, Roel Verdult
 
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#ifndef _LIBNFC_TYPES_H_
#define _LIBNFC_TYPES_H_

#include "defines.h"

typedef enum {
  false = 0x00,
  true  = 0x01
} bool;

typedef enum {
  CT_PN531                    = 0x00,
  CT_PN532                    = 0x10,
  CT_PN533                    = 0x20,
} chip_type;

struct libnfc_driver_info;
typedef const struct libnfc_driver_info * libnfc_driver_info_t;

typedef struct {
  libnfc_driver_info_t driver;
  char acName[255]; // Device name string
  chip_type ct;
  dev_spec ds;      // Pointer to the device connection specification
  bool bActive;     // This represents if the PN53X device was initialized succesful
  bool bCrc;        // Is the crc automaticly added, checked and removed from the frames
  bool bPar;        // Does the PN53x chip handles parity bits, all parities are handled as data
  ui8 ui8TxBits;    // The last tx bits setting, we need to reset this if it does not apply anymore
} dev_info;

#ifdef LIBNFC_INTERNALS
struct libnfc_driver_info {
	dev_info* (*connect)(const libnfc_driver_info_t driver_info, const ui32 uiIndex);
	bool (*transceive)(const dev_spec ds, const byte* pbtTx, const ui32 uiTxLen, byte* pbtRx, ui32* puiRxLen);
	void (*disconnect)(dev_info* pdi);
	const char * const driver_name;
};
#endif


typedef enum {
  DCO_HANDLE_CRC              = 0x00,
  DCO_HANDLE_PARITY           = 0x01,
  DCO_ACTIVATE_FIELD          = 0x10,
  DCO_INFINITE_LIST_PASSIVE   = 0x20,
  DCO_ACCEPT_INVALID_FRAMES   = 0x30,
  DCO_ACCEPT_MULTIPLE_FRAMES  = 0x31
}dev_config_option;

////////////////////////////////////////////////////////////////////
// nfc_reader_list_passive - using InListPassiveTarget 

typedef enum {
  IM_ISO14443A_106  = 0x00,
  IM_FELICA_212     = 0x01,
  IM_FELICA_424     = 0x02,
  IM_ISO14443B_106  = 0x03,
  IM_JEWEL_106      = 0x04
}init_modulation;

typedef struct {
  byte abtAtqa[2];
  byte btSak;
  ui32 uiUidLen;
  byte abtUid[10];
  ui32 uiAtsLen;
  byte abtAts[36];
}tag_info_iso14443a;

typedef struct {
  ui32 uiLen;
  byte btResCode;
  byte abtId[8];
  byte abtPad[8];
  byte abtSysCode[2];
}tag_info_felica;

typedef struct {
  byte abtAtqb[12];
  byte abtId[4];
  byte btParam1;
  byte btParam2;
  byte btParam3;
  byte btParam4;
  byte btCid;
  ui32 uiInfLen;
  byte abtInf[64];
}tag_info_iso14443b;

typedef struct {
  byte btSensRes[2];
  byte btId[4];
}tag_info_jewel;

typedef union {
  tag_info_iso14443a tia;
  tag_info_felica tif;
  tag_info_iso14443b tib;
  tag_info_jewel tij;
}tag_info;

////////////////////////////////////////////////////////////////////
// InDataExchange, MIFARE Classic card 

typedef enum {
  MC_AUTH_A         = 0x60,
  MC_AUTH_B         = 0x61,
  MC_READ           = 0x30,
  MC_WRITE          = 0xA0,
  MC_TRANSFER       = 0xB0,
  MC_DECREMENT      = 0xC0,
  MC_INCREMENT      = 0xC1,
  MC_STORE          = 0xC2,
}mifare_cmd;

// MIFARE Classic command params
typedef struct {
  byte abtKey[6];
  byte abtUid[4];
}mifare_param_auth;

typedef struct {
  byte abtData[16];
}mifare_param_data;

typedef struct {
  byte abtValue[4];
}mifare_param_value;

typedef union {
  mifare_param_auth mpa;
  mifare_param_data mpd;
  mifare_param_value mpv;
}mifare_param;

#endif // _LIBNFC_TYPES_H_
