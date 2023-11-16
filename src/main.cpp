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
#define LED_BUILTIN 4
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
    // Serial.println(WiFi.SSID(i));

    for (int j = 0; j < numSsid; ++j)
    {
      /*Serial.print(j);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i).c_str());
      Serial.print(" = ");
      Serial.println(credentials[j][0]);*/
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

bool mqttConnect()
{
  mqttClient.setServer(credentials[cred][2], 1883);
  mqttClient.setCallback(mqttCallback);

  String topic = mqttGetTopic("state");

  DynamicJsonDocument state(64);
  state["state"] = "offline";
  state["ip_address"] = "";

  // String jsonState = "{\"state\":\"offline\",\"ip_address\":\"\"}";
  String jsonState;
  serializeJson(state, jsonState);

  // connect client to retainable last will message
  return mqttClient.connect(ESP_NAME, mqtt_user, mqtt_pass, topic.c_str(), 0, true, jsonState.c_str());
}

void mqttPublishOnlineState()
{
  // get ip address
  IPAddress MyIP = WiFi.localIP();
  char ip[16];
  snprintf(ip, 16, "%d.%d.%d.%d", MyIP[0], MyIP[1], MyIP[2], MyIP[3]);

  // prepare json state
  char jsonState[50];
  snprintf(jsonState,
           sizeof(jsonState),
           "{\"state\":\"online\",\"ip_address\":\"%s\"}",
           ip);

  String topic = mqttGetTopic("state");

  // publish state
  mqttClient.publish(topic.c_str(), jsonState, true);
}

void mqttPublishState(Reading *values)
{
  String topic = mqttGetTopic("sensor/state");

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

  DynamicJsonDocument state(1024);
  state["volume"] = (float)values->volume / 1000;
  state["target"] = (float)values->target / 1000;
  state["temp"] = values->temperature;
  state["ambient_temp"] = values->ambient_temperature;

  String jsonString;
  serializeJson(state, Serial);
  serializeJson(state, jsonString);

  // char mqttjsondstring[100];
  // snprintf(mqttjsondstring,
  //          sizeof(mqttjsondstring),
  //          "{\"volume\":%d.%03d,\"target\":%d.%03d,\"temp\":%2d,\"ambient_temp\":%2d}",
  //          values->volume / 1000,
  //          values->volume % 1000,
  //          values->target / 1000,
  //          values->target % 1000,
  //          values->temperature,
  //          values->ambient_temperature);

  mqttClient.publish(topic.c_str(), jsonString.c_str(), true);
}

void mqttSubscribe()
{
  String s;

  // // publish online status
  // s = "water-meter/0/online";
  // mqttClient.publish(s.c_str(), "True", true);
  // Serial.print("MQTT-SEND: ");
  // Serial.print(s);
  // Serial.println(" True");

  // // publish ip address
  // s = "water-meter/0/ipaddr";
  // IPAddress MyIP = WiFi.localIP();
  // snprintf(MyIp, 16, "%d.%d.%d.%d", MyIP[0], MyIP[1], MyIP[2], MyIP[3]);
  // mqttClient.publish(s.c_str(), MyIp, true);
  // Serial.print("MQTT-SEND: ");
  // Serial.print(s);
  // Serial.print(" ");
  // Serial.println(MyIp);

  // if True -> perform an reset
  s = "espmeter/reset";
  mqttClient.subscribe(s.c_str());
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
    Reading reading;

    bool success = waterMeter.getValues(&reading);

    if (success)
    {
      mqttPublishState(&reading);
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
    // Serial.println("StateInit:");
    WiFi.mode(WIFI_STA);

    setMeterID();

    ControlState = StateNotConnected;
    break;

  case StateNotConnected:
    // Serial.println("StateNotConnected:");

    ControlState = StateWifiConnect;
    break;

  case StateWifiConnect:
    // Serial.println("StateWifiConnect:");
    //  station mode
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
      Serial.println("Connection failed.");

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
      break; // exit (hopefully) switch statement
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

      ControlState = StateOperating;
      digitalWrite(LED_BUILTIN, LOW); // on
      Serial.println("StateOperating:");
      // mqttDebug("up and running");
    }
    ArduinoOTA.handle();

    break;

  case StateOperating:
    // Serial.println("StateOperating:");

    if (WiFi.status() != WL_CONNECTED)
    {
      ControlState = StateWifiConnect;
      break; // exit (hopefully switch statement)
    }

    if (!mqttClient.connected())
    {
      Serial.println("not connected to MQTT server");
      ControlState = StateMqttConnect;
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
