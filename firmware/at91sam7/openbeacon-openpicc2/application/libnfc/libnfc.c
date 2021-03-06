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

#include <stdio.h>
#include <string.h>
#include "libnfc.h"
#include "bitutils.h"

// Registers and symbols masks used to covers parts within a register
#define REG_CIU_TX_MODE           0x6302
  #define SYMBOL_TX_CRC_ENABLE      0x80
#define REG_CIU_RX_MODE           0x6303
  #define SYMBOL_RX_CRC_ENABLE      0x80
  #define SYMBOL_RX_NO_ERROR        0x08
  #define SYMBOL_RX_MULTIPLE        0x04
#define REG_CIU_TX_AUTO           0x6305
  #define SYMBOL_FORCE_100_ASK      0x40
  #define SYMBOL_AUTO_WAKE_UP       0x20
  #define SYMBOL_INITIAL_RF_ON      0x04
#define REG_CIU_MANUAL_RCV        0x630D
  #define SYMBOL_PARITY_DISABLE     0x10
#define REG_CIU_CONTROL           0x633C
  #define SYMBOL_INITIATOR          0x10
  #define SYMBOL_RX_LAST_BITS       0x07
#define REG_CIU_BIT_FRAMING       0x633D
  #define SYMBOL_TX_LAST_BITS       0x07

#define PARAM_NONE            0x00
#define PARAM_NAD_USED        0x01
#define PARAM_DID_USED        0x02
#define PARAM_AUTO_ATR_RES    0x04
#define PARAM_AUTO_RATS       0x10
#define PARAM_14443_4_PICC    0x20
#define PARAM_NO_AMBLE        0x40

// PN53X configuration
byte pncmd_get_firmware_version       [  2] = { 0xD4,0x02 };
byte pncmd_get_general_status         [  2] = { 0xD4,0x04 };
byte pncmd_get_register               [  4] = { 0xD4,0x06 };
byte pncmd_set_register               [  5] = { 0xD4,0x08 };
byte pncmd_set_parameters             [  3] = { 0xD4,0x12 };

// RF field configuration
byte pncmd_rf_configure_field         [  4] = { 0xD4,0x32,0x01 };
byte pncmd_rf_configure_timing        [  4] = { 0xD4,0x32,0x02 };
byte pncmd_rf_configure_retry_data    [  4] = { 0xD4,0x32,0x04 };
byte pncmd_rf_configure_retry_select  [  6] = { 0xD4,0x32,0x05 };

// Reader
byte pncmd_reader_list_passive        [264] = { 0xD4,0x4A };
byte pncmd_reader_select              [  3] = { 0xD4,0x54 };
byte pncmd_reader_deselect            [  3] = { 0xD4,0x44 };
byte pncmd_reader_release             [  3] = { 0xD4,0x52 };
byte pncmd_reader_set_baud_rate       [  5] = { 0xD4,0x4E };
byte pncmd_reader_exchange_data       [265] = { 0xD4,0x40 };
byte pncmd_reader_auto_poll           [  5] = { 0xD4,0x60 };

// Target
byte pncmd_target_get_data            [  2] = { 0xD4,0x86 };
byte pncmd_target_init                [ 39] = { 0xD4,0x8C };
byte pncmd_target_virtual_card        [  4] = { 0xD4,0x14 };
byte pncmd_target_receive             [  2] = { 0xD4,0x88 };
byte pncmd_target_send                [264] = { 0xD4,0x90 };
byte pncmd_target_get_status          [  2] = { 0xD4,0x8A };

// Exchange raw data frames
byte pncmd_exchange_raw_data          [266] = { 0xD4,0x42 };

// Global buffers used for communication with the PN53X chip
#define MAX_FRAME_LEN 264
static byte abtRx[MAX_FRAME_LEN];
static ui32 uiRxLen;

#ifndef ENABLED_DRIVERS
#error No reader drivers enabled, cannot compile libnfc
static const libnfc_driver_info_t const enabled_drivers[] = {};
#else
static const libnfc_driver_info_t const enabled_drivers[] = { ENABLED_DRIVERS };
#endif

bool pn53x_transceive(const dev_info* pdi, const byte* pbtTx, const ui32 uiTxLen)
{
  // Reset the receiving buffer
  uiRxLen = MAX_FRAME_LEN;

  // Call the tranceive callback function of the current device
  if (!pdi->driver->transceive(pdi->ds,pbtTx,uiTxLen,abtRx,&uiRxLen)) return false;

  // Make sure there was no failure reported by the PN53X chip (0x00 == OK)
  if (abtRx[0] != 0) return false;

  // Succesful transmission
  return true;
}

byte pn53x_get_reg(const dev_info* pdi, ui16 ui16Reg)
{
  ui8 ui8Value;
  ui32 uiValueLen = 1;
  pncmd_get_register[2] = ui16Reg >> 8;
  pncmd_get_register[3] = ui16Reg & 0xff;
  pdi->driver->transceive(pdi->ds,pncmd_get_register,4,&ui8Value,&uiValueLen);
  return ui8Value;
}

bool pn53x_set_reg(const dev_info* pdi, ui16 ui16Reg, ui8 ui8SybmolMask, ui8 ui8Value)
{
  pncmd_set_register[2] = ui16Reg >> 8;
  pncmd_set_register[3] = ui16Reg & 0xff;
  pncmd_set_register[4] = ui8Value | (pn53x_get_reg(pdi,ui16Reg) & (~ui8SybmolMask));
  return pdi->driver->transceive(pdi->ds,pncmd_set_register,5,null,null);
}

bool pn53x_set_parameters(const dev_info* pdi, ui8 ui8Value)
{
  pncmd_set_parameters[2] = ui8Value;
  return pdi->driver->transceive(pdi->ds,pncmd_set_parameters,3,null,null);
}

bool pn53x_set_tx_bits(const dev_info* pdi, ui8 ui8Bits)
{
  // Test if we need to update the transmission bits register setting
  if (pdi->ui8TxBits != ui8Bits)
  {
    // Set the amount of transmission bits in the PN53X chip register
    if (!pn53x_set_reg(pdi,REG_CIU_BIT_FRAMING,SYMBOL_TX_LAST_BITS,ui8Bits)) return false;
    
    // Store the new setting
    ((dev_info*)pdi)->ui8TxBits = ui8Bits;
  }
  return true;
}

bool pn53x_wrap_frame(const byte* pbtTx, const ui32 uiTxBits, const byte* pbtTxPar, byte* pbtFrame, ui32* puiFrameBits)
{
  byte btFrame;
  byte btData;
  ui32 uiBitPos;
  ui32 uiDataPos = 0;
  ui32 uiBitsLeft = uiTxBits;

  // Make sure we should frame at least something
  if (uiBitsLeft == 0) return false;
  
  // Handle a short response (1byte) as a special case
  if (uiBitsLeft < 9)
  {
    *pbtFrame = *pbtTx;
    *puiFrameBits = uiTxBits;
    return true;
  }

  // We start by calculating the frame length in bits
  *puiFrameBits = uiTxBits + (uiTxBits/8);
    
  // Parse the data bytes and add the parity bits
  // This is really a sensitive process, mirror the frame bytes and append parity bits
  // buffer = mirror(frame-byte) + parity + mirror(frame-byte) + parity + ...
    // split "buffer" up in segments of 8 bits again and mirror them
  // air-bytes = mirror(buffer-byte) + mirror(buffer-byte) + mirror(buffer-byte) + ..
  while(true)
  {
    // Reset the temporary frame byte;
    btFrame = 0;

    for (uiBitPos=0; uiBitPos<8; uiBitPos++)
    {
      // Copy as much data that fits in the frame byte
      btData = mirror(pbtTx[uiDataPos]);
      btFrame |= (btData >> uiBitPos);
      // Save this frame byte
      *pbtFrame = mirror(btFrame);
      // Set the remaining bits of the date in the new frame byte and append the parity bit
      btFrame = (btData << (8-uiBitPos));
      btFrame |= ((pbtTxPar[uiDataPos] & 0x01) << (7-uiBitPos));
      // Backup the frame bits we have so far
      pbtFrame++;
      *pbtFrame = mirror(btFrame);
      // Increase the data (without parity bit) position
      uiDataPos++;
      // Test if we are done
      if (uiBitsLeft < 9) return true;
      uiBitsLeft -= 8;
    }
    // Every 8 data bytes we lose one frame byte to the parities
    pbtFrame++;
  }
}

bool pn53x_unwrap_frame(const byte* pbtFrame, const ui32 uiFrameBits, byte* pbtRx, ui32* puiRxBits, byte* pbtRxPar)
{
  byte btFrame;
  byte btData;
  ui8 uiBitPos;
  ui32 uiDataPos = 0;
  byte* pbtFramePos = (byte*) pbtFrame;
  ui32 uiBitsLeft = uiFrameBits;

  // Make sure we should frame at least something
  if (uiBitsLeft == 0) return false;

  // Handle a short response (1byte) as a special case
  if (uiBitsLeft < 9)
  {
    *pbtRx = *pbtFrame;
    *puiRxBits = uiFrameBits;
    return true;
  }
  
  // Calculate the data length in bits
  *puiRxBits = uiFrameBits - (uiFrameBits/9);

  // Parse the frame bytes, remove the parity bits and store them in the parity array
  // This process is the reverse of WrapFrame(), look there for more info
  while(true)
  {
    for (uiBitPos=0; uiBitPos<8; uiBitPos++)
    {
      btFrame = mirror(pbtFramePos[uiDataPos]);
      btData = (btFrame << uiBitPos);
      btFrame = mirror(pbtFramePos[uiDataPos+1]);
      btData |= (btFrame >> (8-uiBitPos));
      pbtRx[uiDataPos] = mirror(btData);
      if(pbtRxPar != null) pbtRxPar[uiDataPos] = ((btFrame >> (7-uiBitPos)) & 0x01);
      // Increase the data (without parity bit) position
      uiDataPos++;
      // Test if we are done
      if (uiBitsLeft < 9) return true;
      uiBitsLeft -= 9;
    }
    // Every 8 data bytes we lose one frame byte to the parities
    pbtFramePos++;
  }
}

dev_info* nfc_connect()
{
  dev_info* pdi;
  unsigned int i;

  // Search for an available reader device
  for(i=0; i<sizeof(enabled_drivers)/sizeof(enabled_drivers[0]); i++) {
	  pdi = enabled_drivers[i]->connect(enabled_drivers[i], 0);
	  if (pdi != INVALID_DEVICE_INFO) {
		  pdi->driver = enabled_drivers[i];
		  return pdi;
	  }
  }

  // Too bad, no reader for this index was found
  return INVALID_DEVICE_INFO;
}

void nfc_disconnect(dev_info* pdi)
{
  // Make sure we reset the CRC and parity to chip handling.
  nfc_configure(pdi,DCO_HANDLE_CRC,true);
  nfc_configure(pdi,DCO_HANDLE_PARITY,true);
  pdi->driver->disconnect(pdi);
}

bool nfc_configure(dev_info* pdi, const dev_config_option dco, const bool bEnable)
{
	// Make sure we are dealing with a active device
  if (!pdi->bActive) return false;

  switch(dco)
  {
    case DCO_HANDLE_CRC:
      if (bEnable)
      {
        // Enable automatic receiving/sending of CRC bytes
        if (!pn53x_set_reg(pdi,REG_CIU_TX_MODE,SYMBOL_TX_CRC_ENABLE,0x80)) return false;
        if (!pn53x_set_reg(pdi,REG_CIU_RX_MODE,SYMBOL_RX_CRC_ENABLE,0x80)) return false;
      } else {
        // Disable automatic receiving/sending of CRC bytes
        if (!pn53x_set_reg(pdi,REG_CIU_TX_MODE,SYMBOL_TX_CRC_ENABLE,0x00)) return false;
        if (!pn53x_set_reg(pdi,REG_CIU_RX_MODE,SYMBOL_RX_CRC_ENABLE,0x00)) return false;
      }
      pdi->bCrc = bEnable;
    break;

    case DCO_HANDLE_PARITY:
      if (bEnable)
      {
        // Handle parity by chip
        if (!pn53x_set_reg(pdi,REG_CIU_MANUAL_RCV,SYMBOL_PARITY_DISABLE,0x00)) return false;

      } else {
        // Parse parity as data bit
        if (!pn53x_set_reg(pdi,REG_CIU_MANUAL_RCV,SYMBOL_PARITY_DISABLE,0x10)) return false;
      }
      pdi->bPar = bEnable;
    break;

    case DCO_ACTIVATE_FIELD:
      pncmd_rf_configure_field[3] = (bEnable) ? 1 : 0;
      if (!pdi->driver->transceive(pdi->ds,pncmd_rf_configure_field,4,null,null)) return false;
    break;

    case DCO_INFINITE_LIST_PASSIVE:
      // Retry format: 0x00 means only 1 try, 0xff means infinite
      pncmd_rf_configure_retry_select[3] = (bEnable) ? 0xff : 0x00; // MxRtyATR, default: active = 0xff, passive = 0x02
      pncmd_rf_configure_retry_select[4] = (bEnable) ? 0xff : 0x00; // MxRtyPSL, default: 0x01
      pncmd_rf_configure_retry_select[5] = (bEnable) ? 0xff : 0x00; // MxRtyPassiveActivation, default: 0xff
      if(!pdi->driver->transceive(pdi->ds,pncmd_rf_configure_retry_select,6,null,null)) return false;
    break;

    case DCO_ACCEPT_INVALID_FRAMES:
      if (bEnable)
      {
        if (!pn53x_set_reg(pdi,REG_CIU_RX_MODE,SYMBOL_RX_NO_ERROR,0x08)) return false;
      } else {
        if (!pn53x_set_reg(pdi,REG_CIU_RX_MODE,SYMBOL_RX_NO_ERROR,0x00)) return false;
      }
    break;

    case DCO_ACCEPT_MULTIPLE_FRAMES:
      if (bEnable)
      {
        if (!pn53x_set_reg(pdi,REG_CIU_RX_MODE,SYMBOL_RX_MULTIPLE,0x04)) return false;
      } else {
        if (!pn53x_set_reg(pdi,REG_CIU_RX_MODE,SYMBOL_RX_MULTIPLE,0x00)) return false;
      }
    return true;

    break;
  }
  
  // When we reach this, the configuration is completed and succesful
  return true;
}

bool nfc_reader_init(const dev_info* pdi)
{
  // Make sure we are dealing with a active device
  if (!pdi->bActive) return false;

  // Set the PN53X to force 100% ASK Modified miller decoding (default for 14443A cards)
  if (!pn53x_set_reg(pdi,REG_CIU_TX_AUTO,SYMBOL_FORCE_100_ASK,0x40)) return false;

  // Configure the PN53X to be an Initiator or Reader/Writer
  if (!pn53x_set_reg(pdi,REG_CIU_CONTROL,SYMBOL_INITIATOR,0x10)) return false;

  return true;
}

bool nfc_reader_list_passive(const dev_info* pdi, const init_modulation im, const byte* pbtInitData, const ui32 uiInitDataLen, tag_info* ti)
{
	// Make sure we are dealing with a active device
  if (!pdi->bActive) return false;

  pncmd_reader_list_passive[2] = 1; // MaxTg, we only want to select 1 tag at the time
  pncmd_reader_list_passive[3] = im; // BrTy, the type of init modulation used for polling a passive tag

  // Set the optional initiator data (used for Felica, ISO14443B, Topaz Polling or for ISO14443A selecting a specific UID).
  if (pbtInitData) memcpy(pncmd_reader_list_passive+4,pbtInitData,uiInitDataLen);

  // Try to find a tag, call the tranceive callback function of the current device
  uiRxLen = MAX_FRAME_LEN;
  if (!pdi->driver->transceive(pdi->ds,pncmd_reader_list_passive,4+uiInitDataLen,abtRx,&uiRxLen)) return false;

  // Make sure one tag has been found, the PN53X returns 0x00 if none was available
  if (abtRx[0] != 1) return false;

  // Is a tag info struct available
  if (ti)
  {
    // Fill the tag info struct with the values corresponding to this init modulation
    switch(im)
    {
      case IM_ISO14443A_106:
        // Somehow they switched the lower and upper ATQA bytes around for the PN532 chipset
        if (pdi->ct == CT_PN532)
        {
          ti->tia.abtAtqa[0] = abtRx[3];
          ti->tia.abtAtqa[1] = abtRx[2];
        } else {
          memcpy(ti->tia.abtAtqa,abtRx+2,2);
        }
        ti->tia.btSak = abtRx[4];
        // Copy the NFCID1
        ti->tia.uiUidLen = abtRx[5];
        memcpy(ti->tia.abtUid,abtRx+6,ti->tia.uiUidLen);
        // Did we received an optional ATS (Smardcard ATR)
        if (uiRxLen > ti->tia.uiUidLen+6)
        {
          ti->tia.uiAtsLen = abtRx[ti->tia.uiUidLen+6];
          memcpy(ti->tia.abtAts,abtRx+ti->tia.uiUidLen+6,ti->tia.uiAtsLen);
        } else {
          ti->tia.uiAtsLen = 0;
        }
      break;

      case IM_FELICA_212:
      case IM_FELICA_424:
        // Store the mandatory info
        ti->tif.uiLen = abtRx[2];
        ti->tif.btResCode = abtRx[3];
        // Copy the NFCID2t
        memcpy(ti->tif.abtId,abtRx+4,8);
        // Copy the felica padding
        memcpy(ti->tif.abtPad,abtRx+12,8);
        // Test if the System code (SYST_CODE) is available
        if (uiRxLen > 20)
        {
          memcpy(ti->tif.abtSysCode,abtRx+20,2);  
        }
      break;

      case IM_ISO14443B_106:
        // Store the mandatory info
        memcpy(ti->tib.abtAtqb,abtRx+2,12);
        // Ignore the 0x1D byte, and just store the 4 byte id
        memcpy(ti->tib.abtId,abtRx+15,4);
        ti->tib.btParam1 = abtRx[19];
        ti->tib.btParam2 = abtRx[20];
        ti->tib.btParam3 = abtRx[21];
        ti->tib.btParam4 = abtRx[22];
        // Test if the Higher layer (INF) is available
        if (uiRxLen > 22)
        {
          ti->tib.uiInfLen = abtRx[23];
          memcpy(ti->tib.abtInf,abtRx+24,ti->tib.uiInfLen);
        } else {
          ti->tib.uiInfLen = 0;
        }
      break;

      case IM_JEWEL_106:
        // Store the mandatory info
        memcpy(ti->tij.btSensRes,abtRx+2,2);
        memcpy(ti->tij.btId,abtRx+4,4);
      break;

      default:
        // Should not be possible, so whatever...
      break;
    }
  }
 
  return true;
}

bool nfc_reader_transceive_bits(const dev_info* pdi, const byte* pbtTx, const ui32 uiTxBits, const byte* pbtTxPar, byte* pbtRx, ui32* puiRxBits, byte* pbtRxPar)
{
  ui32 uiFrameBits = 0;
  ui32 uiFrameBytes = 0;
  ui8 ui8Bits = 0;

  // Check if we should prepare the parity bits ourself
  if (!pdi->bPar)
  {
    // Convert data with parity to a frame
    pn53x_wrap_frame(pbtTx,uiTxBits,pbtTxPar,pncmd_exchange_raw_data+2,&uiFrameBits);
  } else {
    uiFrameBits = uiTxBits;
  }

  // Retrieve the leading bits
  ui8Bits = uiFrameBits%8;
  
  // Get the amount of frame bytes + optional (1 byte if there are leading bits)
  uiFrameBytes = (uiFrameBits/8)+((ui8Bits==0)?0:1);

  // When the parity is handled before us, we just copy the data
  if (pdi->bPar) memcpy(pncmd_exchange_raw_data+2,pbtTx,uiFrameBytes);

  // Set the amount of transmission bits in the PN53X chip register
  if (!pn53x_set_tx_bits(pdi,ui8Bits)) return false;

  // Send the frame to the PN53X chip and get the answer
  // We have to give the amount of bytes + (the two command bytes 0xD4, 0x42)
  if (!pn53x_transceive(pdi,pncmd_exchange_raw_data,uiFrameBytes+2)) return false;
 
  // Get the last bit-count that is stored in the received byte
  ui8Bits = pn53x_get_reg(pdi,REG_CIU_CONTROL) & SYMBOL_RX_LAST_BITS;

  // Recover the real frame length in bits
  uiFrameBits = ((uiRxLen-1-((ui8Bits==0)?0:1))*8)+ui8Bits;

  // Ignore the status byte from the PN53X here, it was checked earlier in pn53x_transceive()
  // Check if we should recover the parity bits ourself
  if (!pdi->bPar)
  {
    // Unwrap the response frame
    pn53x_unwrap_frame(abtRx+1,uiFrameBits,pbtRx,puiRxBits,pbtRxPar);
  } else {
    // Save the received bits
    *puiRxBits = uiFrameBits;
    // Copy the received bytes
    memcpy(pbtRx,abtRx+1,uiRxLen-1);
  }

  // Everything went successful
  return true;
}

bool nfc_reader_transceive_bytes(const dev_info* pdi, const byte* pbtTx, const ui32 uiTxLen, byte* pbtRx, ui32* puiRxLen)
{
  // We can not just send bytes without parity if while the PN53X expects we handled them
  if (!pdi->bPar) return false;

  // Copy the data into the command frame
  memcpy(pncmd_exchange_raw_data+2,pbtTx,uiTxLen);

  // To transfer command frames bytes we can not have any leading bits, reset this to zero
  if (!pn53x_set_tx_bits(pdi,0)) return false;

  // Send the frame to the PN53X chip and get the answer
  // We have to give the amount of bytes + (the two command bytes 0xD4, 0x42)
  if (!pn53x_transceive(pdi,pncmd_exchange_raw_data,uiTxLen+2)) return false;
 
  // Save the received byte count
  *puiRxLen = uiRxLen-1;
  
  // Copy the received bytes
  memcpy(pbtRx,abtRx+1,*puiRxLen);

  // Everything went successful
  return true;
}

bool nfc_reader_mifare_cmd(const dev_info* pdi, const mifare_cmd mc, const ui8 ui8Block, mifare_param* pmp)
{
  ui32 uiParamLen;

	// Make sure we are dealing with a active device
  if (!pdi->bActive) return false;

  pncmd_reader_exchange_data[2] = 0x01;     // Use first target/card
  pncmd_reader_exchange_data[3] = mc;       // The MIFARE Classic command
  pncmd_reader_exchange_data[4] = ui8Block; // The block address (1K=0x00..0x39, 4K=0x00..0xff)
  
  switch (mc)
  {
    // Read and store command have no parameter
    case MC_READ:
    case MC_STORE:
      uiParamLen = 0;
    break;
   
    // Authenticate command
    case MC_AUTH_A:
    case MC_AUTH_B:
      uiParamLen = sizeof(mifare_param_auth);
    break;

    // Data command
    case MC_WRITE:
      uiParamLen = sizeof(mifare_param_data);
    break;

    // Value command
    case MC_DECREMENT:
    case MC_INCREMENT:
    case MC_TRANSFER:
      uiParamLen = sizeof(mifare_param_value);
    break;

    // Please fix your code, you never should reach this statement
    default:
      return false;
    break;
  }
  
  // When available, copy the parameter bytes
  if (uiParamLen) memcpy(pncmd_reader_exchange_data+5,(byte*)pmp,uiParamLen);
  
  // Fire the mifare command
  if (!pn53x_transceive(pdi,pncmd_reader_exchange_data,5+uiParamLen)) return false;

  // When we have executed a read command, copy the received bytes into the param
  if (mc == MC_READ) memcpy(pmp->mpd.abtData,abtRx+1,16);

  // Command succesfully executed
  return true;
}

bool nfc_target_init(const dev_info* pdi, byte* pbtRx, ui32* puiRxBits)
{
  ui8 ui8Bits;

  // Save the current configuration settings
  bool bCrc = pdi->bCrc;
	bool bPar = pdi->bPar;

  // Clear the target init struct, reset to all zeros
  memset(pncmd_target_init+2,0x00,37);

	// Set ATQA (SENS_RES)
	pncmd_target_init[3] = 0x04;
	pncmd_target_init[4] = 0x00;

	// Set SAK (SEL_RES)
  pncmd_target_init[8] = 0x20;

	// Set UID
	pncmd_target_init[5] = 0x00;
	pncmd_target_init[6] = 0xb0;
	pncmd_target_init[7] = 0x0b;
	
  // Make sure the CRC & parity are handled by the device, this is needed for target_init to work properly
  if (!bCrc) nfc_configure((dev_info*)pdi,DCO_HANDLE_CRC,true);
  if (!bPar) nfc_configure((dev_info*)pdi,DCO_HANDLE_CRC,true);

  // Let the PN53X be activated by the RF level detector from power down mode
  if (!pn53x_set_reg(pdi,REG_CIU_TX_AUTO, SYMBOL_INITIAL_RF_ON,0x04)) return false;

  // Request the initialization as a target, we can not use pn53x_transceive() because
  // abtRx[0] contains the emulation mode (baudrate, 14443-4?, DEP and framing type)
  uiRxLen = MAX_FRAME_LEN;
  if (!pdi->driver->transceive(pdi->ds,pncmd_target_init,39,abtRx,&uiRxLen)) return false;

  // Get the last bit-count that is stored in the received byte 
  ui8Bits = pn53x_get_reg(pdi,REG_CIU_CONTROL) & SYMBOL_RX_LAST_BITS;

  // We are sure the parity is handled by the PN53X chip, so we handle it this way
  *puiRxBits = ((uiRxLen-1-((ui8Bits==0)?0:1))*8)+ui8Bits;
  // Copy the received bytes
  memcpy(pbtRx,abtRx+1,uiRxLen-1);

  // Restore the CRC & parity setting to the original value (if needed)
  if (!bCrc) nfc_configure((dev_info*)pdi,DCO_HANDLE_CRC,false);
  if (!bPar) nfc_configure((dev_info*)pdi,DCO_HANDLE_CRC,false);

  return true;
}

bool nfc_target_receive_bits(const dev_info* pdi, byte* pbtRx, ui32* puiRxBits, byte* pbtRxPar)
{
	ui32 uiFrameBits;
  ui8 ui8Bits;

	// Try to gather a received frame from the reader
	if (!pn53x_transceive(pdi,pncmd_target_receive,2)) return false;

  // Get the last bit-count that is stored in the received byte 
  ui8Bits = pn53x_get_reg(pdi,REG_CIU_CONTROL) & SYMBOL_RX_LAST_BITS;

  // Recover the real frame length in bits
  uiFrameBits = ((uiRxLen-1-((ui8Bits==0)?0:1))*8)+ui8Bits;

  // Ignore the status byte from the PN53X here, it was checked earlier in pn53x_transceive()
  // Check if we should recover the parity bits ourself
  if (!pdi->bPar)
  {
    // Unwrap the response frame
    pn53x_unwrap_frame(abtRx+1,uiFrameBits,pbtRx,puiRxBits,pbtRxPar);
  } else {
    // Save the received bits
    *puiRxBits = uiFrameBits;
    // Copy the received bytes
    memcpy(pbtRx,abtRx+1,uiRxLen-1);
  }
  // Everyting seems ok, return true
	return true;
}

bool nfc_target_receive_bytes(const dev_info* pdi, byte* pbtRx, ui32* puiRxLen)
{
	// Try to gather a received frame from the reader
	if (!pn53x_transceive(pdi,pncmd_target_receive,2)) return false;

  // Save the received byte count
  *puiRxLen = uiRxLen-1;
  
  // Copy the received bytes
  memcpy(pbtRx,abtRx+1,*puiRxLen);

  // Everyting seems ok, return true
  return true;
}

bool nfc_target_send_bits(const dev_info* pdi, const byte* pbtTx, const ui32 uiTxBits, const byte* pbtTxPar)
{
  ui32 uiFrameBits = 0;
  ui32 uiFrameBytes = 0;
  ui8 ui8Bits = 0;

  // Check if we should prepare the parity bits ourself
  if (!pdi->bPar)
  {
    // Convert data with parity to a frame
    pn53x_wrap_frame(pbtTx,uiTxBits,pbtTxPar,pncmd_target_send+2,&uiFrameBits);
  } else {
    uiFrameBits = uiTxBits;
  }

  // Retrieve the leading bits
  ui8Bits = uiFrameBits%8;
  
  // Get the amount of frame bytes + optional (1 byte if there are leading bits) 
  uiFrameBytes = (uiFrameBits/8)+((ui8Bits==0)?0:1);

  // When the parity is handled before us, we just copy the data
  if (pdi->bPar) memcpy(pncmd_target_send+2,pbtTx,uiFrameBytes);

  // Set the amount of transmission bits in the PN53X chip register
  if (!pn53x_set_tx_bits(pdi,ui8Bits)) return false;

  // Try to send the bits to the reader
  if (!pn53x_transceive(pdi,pncmd_target_send,uiFrameBytes+2)) return false;

  // Everyting seems ok, return true
  return true;
}


bool nfc_target_send_bytes(const dev_info* pdi, const byte* pbtTx, const ui32 uiTxLen)
{
  // We can not just send bytes without parity if while the PN53X expects we handled them
  if (!pdi->bPar) return false;
  
  // Copy the data into the command frame
  memcpy(pncmd_target_send+2,pbtTx,uiTxLen);

  // Try to send the bits to the reader
  if (!pn53x_transceive(pdi,pncmd_target_send,uiTxLen+2)) return false;

  // Everyting seems ok, return true
  return true;
}
