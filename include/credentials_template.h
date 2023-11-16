#ifndef CREDENTIALS_H
#define CREDENTIALS_H
#include <Arduino.h>

// Wifi settings: SSID, PW, MQTT broker
#define NUM_SSID_CREDENTIALS 1
static const char *credentials[NUM_SSID_CREDENTIALS][3] =
    // SSID,        PW,           MQTT
    {{"wifi name", "wifi pass", "192.168.1.xxx"}};

const char mqtt_user[] = "mosquitto-user";
const char mqtt_pass[] = "mosquitto-pass!";

const uint8_t meterId[4] = {0x00, 0x00, 0x00, 0x00};                                                                      // Multical21 serial. Printed as hex on meter.
const uint8_t key[16] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // AES-128 key. Ask your service provider.

#endif