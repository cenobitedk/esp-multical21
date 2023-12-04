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

void WMBusFrame::check()
{
  // check meterId
  for (uint8_t i = 0; i < 4; i++)
  {
    if (meterId[i] != payload[6 - i])
    {

      Serial.println("Payload from a different address. Ignoring.");

      isValid = false;
      return;
    }
  }

  isValid = true;
}

void WMBusFrame::printMeterInfo(uint8_t *data, size_t len)
{
  // init positions for compact frame
  int pos_tt = 9;  // total consumption, 9, 10, 11, 12
  int pos_tg = 13; // target consumption 13, 14, 15, 16
  int pos_ic = 7;  // info codes
  int pos_ft = 17; // flow temp
  int pos_at = 18; // ambient temp

  char mqttstring[25];
  char mqttjsondstring[100];

  // Serial.printf("Data: ");
  // for(int k=0; k < len; k++) {
  //   Serial.printf("%02x", data[k]);
  // }
  // Serial.printf("\n\r");

  if (data[2] == 0x79) // compact frame
  {
    pos_ic = 7;
    pos_tt = 9;
    pos_tg = 13;
    pos_ft = 17;
    pos_at = 18;
  }
  else if (data[2] == 0x78) // long frame
  {
    // overwrite it with long frame positions
    pos_ic = 6;
    pos_tt = 10;
    pos_tg = 16;
    // pos_ft = 23;
    // pos_at = 29;
    pos_ft = 22;
    pos_at = 25;
  }
  else
    return;

  uint16_t calc_crc = crc16_EN13757(data + 2, len - 2);
  uint16_t read_crc = data[1] << 8 | data[0];
  Serial.printf("calc_crc: 0x%04x\n\r", calc_crc);
  Serial.printf("read_crc: 0x%04x\n\r", read_crc);

  if (calc_crc == read_crc)
  {
    Serial.printf("CRC: OK\n\r");
  }
  else
  {
    Serial.printf("CRC: ERROR\n\r");
    return;
  }

  char total[10];
  uint32_t tt = data[pos_tt] + (data[pos_tt + 1] << 8) + (data[pos_tt + 2] << 16) + (data[pos_tt + 3] << 24);
  snprintf(total, sizeof(total), "%d.%03d", tt / 1000, tt % 1000);
  Serial.printf("total: %s m%c - ", total, 179);
  snprintf(mqttstring, sizeof(mqttstring), "%d.%03d", tt / 1000, tt % 1000);
  mqttMyData(mqttstring);

  char target[10];
  uint32_t tg = data[pos_tg] + (data[pos_tg + 1] << 8) + (data[pos_tg + 2] << 16) + (data[pos_tg + 3] << 24);
  snprintf(target, sizeof(target), "%d.%03d", tg / 1000, tg % 1000);
  Serial.printf("target: %s m%c - ", target, 179);

  char flow_temp[3];
  snprintf(flow_temp, sizeof(flow_temp), "%2d", data[pos_ft]);
  Serial.printf("%s %cC - ", flow_temp, 176);

  char ambient_temp[3];
  snprintf(ambient_temp, sizeof(ambient_temp), "%2d", data[pos_at]);
  Serial.printf("%s %cC\n\r", ambient_temp, 176);

  snprintf(mqttjsondstring, sizeof(mqttjsondstring), "{\"CurrentValue\": %d.%03d,\"MonthStartValue\": %d.%03d,\"WaterTemp\": %2d,\"RoomTemp\": %2d}", tt / 1000, tt % 1000, tg / 1000, tg % 1000, data[pos_ft], data[pos_at]);
  mqttMyDataJson(mqttjsondstring);
}

// Get values from decoded frame.
void WMBusFrame::parse()
{
  // init positions for compact frame
  int pos_tt = 9;  // total consumption
  int pos_tg = 13; // target consumption
  int pos_ic = 7;  // info codes
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

  // char total[10];
  uint32_t volume = plaintext[pos_tt] + (plaintext[pos_tt + 1] << 8) + (plaintext[pos_tt + 2] << 16) + (plaintext[pos_tt + 3] << 24);
  // snprintf(total, sizeof(total), "%d.%03d", volume / 1000, volume % 1000);

  // char tg[10];
  uint32_t target = plaintext[pos_tg] + (plaintext[pos_tg + 1] << 8) + (plaintext[pos_tg + 2] << 16) + (plaintext[pos_tg + 3] << 24);
  // snprintf(tg, sizeof(tg), "%d.%03d", target / 1000, target % 1000);

  // char temp[3];
  int8_t flow_temp = plaintext[pos_ft];
  // snprintf(temp, sizeof(temp), "%2d", flow_temp);

  // char a_temp[3];
  int8_t ambient_temp = plaintext[pos_at];
  // snprintf(a_temp, sizeof(a_temp), "%2d", ambient_temp);

  uint16_t infocode = plaintext[pos_ic] + (plaintext[pos_ic + 1] << 8);

  values.volume = volume;
  values.target = target;
  values.temperature = flow_temp;
  values.ambient_temperature = ambient_temp;
  values.infocode = infocode;
}

bool WMBusFrame::decode()
{
  // check meterId
  check();
  if (!isValid)
    return false;

  uint8_t cipherLength = length - 2 - 16; // cipher starts at index 16, remove 2 crc bytes
  memcpy(cipher, &payload[16], cipherLength);

  // 0xCD, 0x1E, 0x62, 0xF7, 0x34, 0x95, 0xD4, 0x71, 0x89, 0x78, 0x7C, 0x8B, 0x8D, 0x58, 0xF2, 0x02, 0x11, 0xDF, 0xA1,
  // CD1E62F73495D47189787C8B8D58F20211DFA1

  memset(iv, 0, sizeof(iv)); // padding with 0
  memcpy(iv, &payload[1], 8);

  // 0x2D, 0x2C, 0x16, 0x30, 0x59, 0x57, 0x1B, 0x16,

  iv[8] = payload[10];

  // 0x20,

  memcpy(&iv[9], &payload[12], 4);

  // 0x90, 0x0A, 0xD5, 0xD5,

  aes128.setIV(iv, sizeof(iv));

  // 2D2C163059571B1620900AD5D5

  aes128.decrypt(plaintext, (const uint8_t *)cipher, cipherLength);

  /*
    Serial.printf("C:     ");
    for (size_t i = 0; i < cipherLength; i++)
    {
      Serial.printf("%02X", cipher[i]);
    }
    Serial.println();
    Serial.printf("P(%d): ", cipherLength);
    for (size_t i = 0; i < cipherLength; i++)
    {
      Serial.printf("%02X", plaintext[i]);
    }
    Serial.println();
  */

  // printMeterInfo(plaintext, cipherLength);

  return true;
}

void WMBusFrame::resetValues()
{
  values.volume = 0;
  values.target = 0;
  values.temperature = 0;
  values.ambient_temperature = 0;
  values.infocode = 0;
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