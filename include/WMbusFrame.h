#ifndef __WMBUS_FRAME__
#define __WMBUS_FRAME__

#include <Arduino.h>
#include <Crypto.h>
#include <AES.h>
#include <CTR.h>
#include "credentials.h"

struct Reading
{
  uint32_t volume;
  uint32_t target;
  int8_t temperature;
  int8_t ambient_temperature;
  uint16_t infocode;
};

class WMBusFrame
{
public:
  static const uint8_t MAX_LENGTH = 127;

private:
  CTR<AESSmall128> aes128;
  uint8_t cipher[MAX_LENGTH];
  uint8_t plaintext[MAX_LENGTH];
  uint8_t iv[16];
  void verify(void);
  void printMeterInfo(uint8_t *data, size_t len);

public:
  // check frame and decrypt it
  bool decode(void);

  // get values from decrypted frame
  void parse();

  // true, if meter information is valid for the last received frame
  bool isValid = false;

  // payload length
  uint8_t length = 0;

  // payload data
  uint8_t payload[MAX_LENGTH];

  // parsed payload data
  Reading values;

  void resetValues();

  // constructor
  WMBusFrame();
};

#endif // __WMBUS_FRAME__
