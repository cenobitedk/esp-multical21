/*
 Copyright (C) 2020 chester4444@wolke7.net
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

#include "WMbusFrame.h"

void mqttMyData(const char *debug_str);
void mqttMyDataJson(const char *debug_str);

WMBusFrame::WMBusFrame()
{
  aes128.setKey(key, sizeof(key));
}

bool WMBusFrame::verifyId()
{
  // check meterId
  for (uint8_t i = 0; i < 4; i++)
  {
    if (meterId[i] != this->payload[6 - i])
    {
      // Serial.println("Payload from a different address. Ignoring.");
      this->isValid = false;
    }
    else
    {
      // Meter ID matches
      this->isValid = true;
    }
  }

  return this->isValid;
}

bool WMBusFrame::decode()
{
  uint8_t cipherLength = length - 2 - 16; // cipher starts at index 16, remove 2 crc bytes
  memcpy(cipher, &payload[16], cipherLength);

  memset(iv, 0, sizeof(iv)); // padding with 0

  // M field + A field
  memcpy(iv, &payload[1], 8);

  // CC field
  iv[8] = payload[10];

  // SN field
  memcpy(&iv[9], &payload[12], 4);

  aes128.setIV(iv, sizeof(iv));
  aes128.decrypt(plaintext, (const uint8_t *)cipher, cipherLength);

  return true;
}

void WMBusFrame::verifyCRC()
{
  // length - 2 (PLCRC) - 16 (ELL offset) - 2 (CRC)
  int l = length - 2 - 16 - 2;

  uint16_t calc_crc = crc16_EN13757(plaintext + 2, l);
  uint16_t read_crc = plaintext[1] << 8 | plaintext[0];

  if (calc_crc == read_crc)
  {
    Serial.printf("CRC: OK\n\r");
  }
  else
  {
    Serial.printf("CRC: ERROR\n\r");
    this->isValid = false;
  }
}

uint16_t WMBusFrame::crc16_EN13757(uint8_t *data, size_t len)
{
  uint16_t crc = 0x0000;

  assert(len == 0 || data != NULL);

  for (size_t i = 0; i < len; ++i)
  {
    crc = crc16_EN13757_per_byte(crc, data[i]);
  }

  return (~crc);
}

#define CRC16_EN_13757 0x3D65

uint16_t WMBusFrame::crc16_EN13757_per_byte(uint16_t crc, uint8_t b)
{
  unsigned char i;

  for (i = 0; i < 8; i++)
  {

    if (((crc & 0x8000) >> 8) ^ (b & 0x80))
    {
      crc = (crc << 1) ^ CRC16_EN_13757;
    }
    else
    {
      crc = (crc << 1);
    }

    b <<= 1;
  }

  return crc;
}

// Get values from decoded frame.
void WMBusFrame::getMeterValues()
{
  // Abort on CRC failure.
  if (!this->isValid)
  {
    return;
  }

  int pos_ic = 7;  // info codes
  int pos_tt = 9;  // total consumption, 9, 10, 11, 12
  int pos_tg = 13; // target consumption 13, 14, 15, 16
  int pos_ft = 17; // flow temp
  int pos_at = 18; // ambient temp

  if (plaintext[2] == 0x78) // long frame
  {
    // overwrite it with long frame positions
    pos_tt = 10;
    pos_tg = 16;
    pos_ic = 6;
    pos_ft = 22;
    // pos_ft = 23;
    pos_at = 25;
    // pos_at = 29;
  }

  values.volume = plaintext[pos_tt] | (plaintext[pos_tt + 1] << 8) | (plaintext[pos_tt + 2] << 16) | (plaintext[pos_tt + 3] << 24);
  values.target = plaintext[pos_tg] | (plaintext[pos_tg + 1] << 8) | (plaintext[pos_tg + 2] << 16) | (plaintext[pos_tg + 3] << 24);
  values.temperature = plaintext[pos_ft];
  values.ambient_temperature = plaintext[pos_at];
  values.infocode = plaintext[pos_ic] | (plaintext[pos_ic + 1] << 8);
}

void WMBusFrame::resetValues()
{
  values.volume = 0;
  values.target = 0;
  values.temperature = 0;
  values.ambient_temperature = 0;
  values.infocode = 0;
}
