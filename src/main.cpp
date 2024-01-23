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

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>
#elif defined(ESP32)
#include <WiFi.h>
#include <ESPmDNS.h>
#endif

#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>

#include "credentials.h"
#include "WaterMeter.h"
#include "hwconfig.h"

#define ESP_NAME "WaterMeter"

#define DEBUG 0

#if defined(ESP32)
#define LED_BUILTIN 2
#endif

// template <typename... Args>
// std::string Sprintf(const char *fmt, Args... args)
// {
//   const size_t n = snprintf(nullptr, 0, fmt, args...);
//   std::vector<char> buf(n + 1);
//   snprintf(buf.data(), n + 1, fmt, args...);
//   return std::string(buf.data());
// }

WaterMeter waterMeter;
WiFiClient espMqttClient;
PubSubClient mqttClient(espMqttClient);

uint32_t meter_id;
char MyIp[16];
int cred = -1;

int getWifiToConnect(int numSsid)
{
  for (int i = 0; i < NUM_SSID_CREDENTIALS; i++)
  {
    for (int j = 0; j < numSsid; ++j)
    {
      if (strcmp(WiFi.SSID(j).c_str(), credentials[i][0]) == 0)
      {
        Serial.println("Credentials found for: ");
        Serial.println(credentials[i][0]);
        return i;
      }
    }
  }
  return -1;
}

// connect to wifi – returns true if successful or false if not
bool ConnectWifi(void)
{
  int i = 0;

  Serial.println("starting scan");
  // scan for nearby networks:
  int numSsid = WiFi.scanNetworks();

  Serial.print("scanning WIFI, found ");
  Serial.print(numSsid);
  Serial.println(" available access points:");

  if (numSsid == -1)
  {
    Serial.println("Couldn't get a wifi connection");
    return false;
  }

  for (int i = 0; i < numSsid; i++)
  {
    Serial.print(i + 1);
    Serial.print(") ");
    Serial.println(WiFi.SSID(i));
  }

  // search for given credentials
  cred = getWifiToConnect(numSsid);
  if (cred == -1)
  {
    Serial.println("No Wifi!");
    return false;
  }

  // try to connect
  WiFi.begin(credentials[cred][0], credentials[cred][1]);
  Serial.println("");
  Serial.print("Connecting to WiFi ");
  Serial.println(credentials[cred][0]);

  i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    digitalWrite(LED_BUILTIN, LOW);
    delay(300);
    Serial.print(".");
    digitalWrite(LED_BUILTIN, HIGH);
    delay(300);
    if (i++ > 30)
    {
      // giving up
      return false;
    }
  }
  return true;
}

void mqttDebug(const char *debug_str)
{
  char topic[30];
  sprintf(topic, "water-meter/0x%08X/debug", meter_id);
  mqttClient.publish(topic, debug_str);
}

void mqttCallback(char *topic, byte *payload, unsigned int len)
{
  // create a local copies of topic and payload
  // PubSubClient overwrites it internally
  String t(topic);
  byte *p = new byte[len];
  memcpy(p, payload, len);

  /*  Serial.print("MQTT-RECV: ");
    Serial.print(topic);
    Serial.print(" ");
    Serial.println((char)payload[0]); // FIXME LEN
  */
  if (strstr(topic, "/smarthomeNG/start"))
  {
    if (len == 4) // True
    {
      // maybe to something
    }
  }
  else if (strstr(topic, "/espmeter/reset"))
  {
    if (len == 4) // True
    {
      // maybe to something
      const char *topic = "/espmeter/reset/status";
      const char *msg = "False";
      mqttClient.publish(topic, msg);
      mqttClient.loop();
      delay(200);

      // reboot
      ESP.restart();
    }
  }
  // and of course, free it
  delete[] p;
}

String mqttGetTopic(String extension)
{
  char topic[64];
  sprintf(topic, "water-meter/0x%08X/%s", meter_id, extension.c_str());
  return topic;
}

String mqttGetAutoDiscoveryTopic(String extension)
{
  char topic[80];
  sprintf(topic, "homeassistant/sensor/0x%08X/%s/config", meter_id, extension.c_str());
  return topic;
}

String mqttGetUniqueId(String extension)
{
  char topic[32];
  sprintf(topic, "0x%08X_%s", meter_id, extension.c_str());
  return topic;
}

bool mqttConnect()
{
  mqttClient.setBufferSize(768);
  mqttClient.setServer(credentials[cred][2], 1883);
  mqttClient.setCallback(mqttCallback);

  String topic = mqttGetTopic("state");

  DynamicJsonDocument state(64);
  state["state"] = "offline";
  state["ip_address"] = "";

  String jsonState;
  serializeJson(state, jsonState);

  // connect client to retainable last will message
  return mqttClient.connect(ESP_NAME, mqtt_user, mqtt_pass, topic.c_str(), 0, true, jsonState.c_str());
}

void mqttPublishOnlineState()
{
  String topic = mqttGetTopic("state");

  // get ip address
  IPAddress MyIP = WiFi.localIP();
  char ip[16];
  snprintf(ip, 16, "%d.%d.%d.%d", MyIP[0], MyIP[1], MyIP[2], MyIP[3]);

  // prepare json state
  DynamicJsonDocument state(64);
  state["state"] = "online";
  state["ip_address"] = ip;

  String jsonState;
  serializeJson(state, jsonState);

  // publish state
  mqttClient.publish(topic.c_str(), jsonState.c_str(), true);
}

void mqttPublishAutoDiscoveryVolume()
{
  // Prepare volume payload.
  StaticJsonDocument<640> volume;
  volume["availability"][0]["topic"] = mqttGetTopic("state");
  volume["availability"][0]["value_template"] = "{{ value_json.state }}";
  volume["device"]["identifiers"][0] = "mc21";
  volume["device"]["manufacturer"] = "Kamstrup";
  volume["device"]["model"] = "Multical 21";
  volume["device"]["name"] = "water-meter";
  volume["device_class"] = "water";
  volume["name"] = "volume";
  volume["state_class"] = "total_increasing";
  volume["state_topic"] = mqttGetTopic("sensor/state");
  volume["unique_id"] = mqttGetUniqueId("volume");
  volume["unit_of_measurement"] = "m³";
  volume["value_template"] = "{{ value_json.volume }}";

  String volumePayload;
  serializeJson(volume, volumePayload);
  String topic = mqttGetAutoDiscoveryTopic("volume");

  mqttClient.publish(topic.c_str(), volumePayload.c_str(), true);
}

void mqttPublishAutoDiscoveryTarget()
{
  // Prepare target payload.
  StaticJsonDocument<640> target;
  target["availability"][0]["topic"] = mqttGetTopic("state");
  target["availability"][0]["value_template"] = "{{ value_json.state }}";
  target["device"]["identifiers"][0] = "mc21";
  target["device"]["manufacturer"] = "Kamstrup";
  target["device"]["model"] = "Multical 21";
  target["device"]["name"] = "water-meter";
  target["device_class"] = "water";
  target["name"] = "target";
  target["state_class"] = "total";
  target["state_topic"] = mqttGetTopic("sensor/state");
  target["unique_id"] = mqttGetUniqueId("target");
  target["unit_of_measurement"] = "m³";
  target["value_template"] = "{{ value_json.target }}";

  String targetPayload;
  serializeJson(target, targetPayload);
  String topic = mqttGetAutoDiscoveryTopic("target");

  mqttClient.publish(topic.c_str(), targetPayload.c_str(), true);
}

void mqttPublishAutoDiscoveryTemperature()
{
  // Prepare temperature payload.
  StaticJsonDocument<640> temperature;
  temperature["availability"][0]["topic"] = mqttGetTopic("state");
  temperature["availability"][0]["value_template"] = "{{ value_json.state }}";
  temperature["device"]["identifiers"][0] = "mc21";
  temperature["device"]["manufacturer"] = "Kamstrup";
  temperature["device"]["model"] = "Multical 21";
  temperature["device"]["name"] = "water-meter";
  temperature["device_class"] = "temperature";
  temperature["name"] = "temperature";
  temperature["state_class"] = "measurement";
  temperature["state_topic"] = mqttGetTopic("sensor/state");
  temperature["unique_id"] = mqttGetUniqueId("temperature");
  temperature["unit_of_measurement"] = "°C";
  temperature["value_template"] = "{{ value_json.temp }}";

  String temperaturePayload;
  serializeJson(temperature, temperaturePayload);
  String topic = mqttGetAutoDiscoveryTopic("temperature");

  mqttClient.publish(topic.c_str(), temperaturePayload.c_str(), true);
}

void mqttPublishAutoDiscoveryAmbientTemperature()
{
  // Prepare ambient temperature payload.
  StaticJsonDocument<640> ambient_temperature;
  ambient_temperature["availability"][0]["topic"] = mqttGetTopic("state");
  ambient_temperature["availability"][0]["value_template"] = "{{ value_json.state }}";
  ambient_temperature["device"]["identifiers"][0] = "mc21";
  ambient_temperature["device"]["manufacturer"] = "Kamstrup";
  ambient_temperature["device"]["model"] = "Multical 21";
  ambient_temperature["device"]["name"] = "water-meter";
  ambient_temperature["device_class"] = "temperature";
  ambient_temperature["name"] = "ambient temperature";
  ambient_temperature["state_class"] = "measurement";
  ambient_temperature["state_topic"] = mqttGetTopic("sensor/state");
  ambient_temperature["unique_id"] = mqttGetUniqueId("ambient_temperature");
  ambient_temperature["unit_of_measurement"] = "°C";
  ambient_temperature["value_template"] = "{{ value_json.ambient_temp }}";

  String ambientTemperaturePayload;
  serializeJson(ambient_temperature, ambientTemperaturePayload);
  String topic = mqttGetAutoDiscoveryTopic("ambient_temperature");

  mqttClient.publish(topic.c_str(), ambientTemperaturePayload.c_str(), true);
}

void mqttPublishAutoDiscovery()
{
  mqttPublishAutoDiscoveryVolume();
  mqttPublishAutoDiscoveryTarget();
  mqttPublishAutoDiscoveryTemperature();
  mqttPublishAutoDiscoveryAmbientTemperature();
}

void mqttPublishState(Values *values)
{
  String topic = mqttGetTopic("sensor/state");

  char json[100];
  snprintf(json,
           sizeof(json),
           "{\"volume\":%d.%03d,\"target\":%d.%03d,\"temp\":%2d,\"ambient_temp\":%2d,\"info_code\":%4X}",
           values->volume / 1000,
           values->volume % 1000,
           values->target / 1000,
           values->target % 1000,
           values->temperature,
           values->ambient_temperature,
           values->infocode);

  mqttClient.publish(topic.c_str(), json, true);
}

void mqttSubscribe()
{
}

void logValues(Values *values)
{
  Serial.printf("Meter ID: 0x%08X", meter_id);
  Serial.println("");

  Serial.printf("  volume: %d.%03d m3", values->volume / 1000, values->volume % 1000);
  Serial.println("");
  Serial.printf("  target: %d.%03d m3", values->target / 1000, values->target % 1000);
  Serial.println("");
  Serial.printf("  temp: %d C", values->temperature);
  Serial.println("");
  Serial.printf("  ambient temp: %d C", values->ambient_temperature);
  Serial.println("");
  Serial.printf("  info code: %04X", values->infocode);
  Serial.println("");
}

void setupOTA()
{
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(ESP_NAME);

  // No authentication by default
  // ArduinoOTA.setPassword((const char *)"123");

  ArduinoOTA.onStart([]()
                     {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type); });
  ArduinoOTA.onEnd([]()
                   { Serial.println("\nEnd"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
                        { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); });
  ArduinoOTA.onError([](ota_error_t error)
                     {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed"); });
  ArduinoOTA.begin();
}

// receive encrypted packets -> send it via MQTT to decrypter
void waterMeterLoop()
{
  if (waterMeter.isFrameAvailable())
  {
    Values values;

    bool success = waterMeter.processFrame(&values);

    if (success)
    {
      logValues(&values);
      mqttPublishState(&values);
    }
  }
}

void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);

  waterMeter.begin();
  Serial.println("Setup done...");
}

enum ControlStateType
{
  StateInit,
  StateNotConnected,
  StateWifiConnect,
  StateMqttConnect,
  StateConnected,
  StateOperating
};
ControlStateType ControlState = StateInit;

void setMeterID()
{
  meter_id = ((uint32_t)meterId[0] << 24) | ((uint32_t)meterId[1] << 16) |
             ((uint32_t)meterId[2] << 8) | ((uint32_t)meterId[3]);
}

void loop()
{
  switch (ControlState)
  {
  case StateInit:
    WiFi.mode(WIFI_STA);

    setMeterID();

    ControlState = StateNotConnected;
    break;

  case StateNotConnected:

    ControlState = StateWifiConnect;
    break;

  case StateWifiConnect:
    ConnectWifi();

    delay(500);

    if (WiFi.status() == WL_CONNECTED)
    {
      Serial.println("");
      Serial.print("Connected to ");
      Serial.println(credentials[cred][0]); // FIXME
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      setupOTA();

      ControlState = StateMqttConnect;
    }
    else
    {
      Serial.println("");
      Serial.println("Wifi connection failed.");

      // try again
      ControlState = StateNotConnected;

      // reboot
      ESP.restart();
    }
    break;

  case StateMqttConnect:
    Serial.println("StateMqttConnect:");
    digitalWrite(LED_BUILTIN, HIGH); // off

    if (WiFi.status() != WL_CONNECTED)
    {
      ControlState = StateNotConnected;
      break;
    }

    Serial.print("try to connect to MQTT server ");
    Serial.println(credentials[cred][2]); // FIXME

    if (mqttConnect())
    {
      ControlState = StateConnected;
    }
    else
    {
      Serial.println("MQTT connect failed");

      delay(1000);
      // try again
    }
    ArduinoOTA.handle();

    break;

  case StateConnected:
    Serial.println("StateConnected:");

    if (!mqttClient.connected())
    {
      ControlState = StateMqttConnect;
      delay(1000);
    }
    else
    {
      // subscribe to given topics
      // mqttSubscribe();

      mqttPublishOnlineState();
      mqttPublishAutoDiscovery();

      ControlState = StateOperating;
      digitalWrite(LED_BUILTIN, LOW); // on
      Serial.println("StateOperating:");
    }
    ArduinoOTA.handle();

    break;

  case StateOperating:

    if (WiFi.status() != WL_CONNECTED)
    {
      Serial.println("lost wifi connection");
      ControlState = StateWifiConnect;
      break;
    }

    if (!mqttClient.connected())
    {
      Serial.println("lost connection to MQTT server");
      ControlState = StateMqttConnect;
      break;
    }

    // here we go
    waterMeterLoop();

    mqttClient.loop();

    ArduinoOTA.handle();

    break;

  default:
    Serial.println("Error: invalid ControlState");
  }
}
