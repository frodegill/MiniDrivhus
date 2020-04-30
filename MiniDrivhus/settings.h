#ifndef _SETTINGS_H_
#define _SETTINGS_H_

#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WebServer.h>

#include "global.h"


void handleNotFound();
void handleSetupRoot();

class Settings
{
public:
  static constexpr const char* SETUP_SSID = "sensor-setup";
  static constexpr byte  EEPROM_INITIALIZED_MARKER = 0xF2; //Just a magic number. CHange when EEPROM data format is incompatibly changed

  static constexpr byte MAX_SSID_LENGTH            = 32;
  static constexpr byte MAX_PASSWORD_LENGTH        = 64;
  static constexpr byte MAX_MQTT_SERVERNAME_LENGTH = 64;
  static constexpr byte MAX_MQTT_SERVERPORT_LENGTH =  5;
  static constexpr byte MAX_MQTT_SENSORID_LENGTH   = 32;
  static constexpr byte MAX_MQTT_USERNAME_LENGTH   = 32;
  static constexpr byte MAX_MQTT_PASSWORD_LENGTH   = 32;

  static constexpr uint16_t MQTT_DEFAULT_PORT = 1883;

  static constexpr unsigned int DEFAULT_CONF_SEC_BETWEEN_READING       = 5; //secoonds min. time between sensor activation
  static constexpr uint16_t DEFAULT_CONF_WATERING_TRIGGER_VALUE        = 80.0f; //[0.0 - 100.0]
  static constexpr unsigned int DEFAULT_CONF_WATERING_DURATION_MS      = 3*1000; //ms to keep watering
  static constexpr unsigned int DEFAULT_CONF_WATERING_GRACE_PERIOD_SEC = 20*60; //sec min. time between watering
  static constexpr const char* DEFAULT_SENSORID = "/MiniDrivhus/Sensor1/";

public:
  Settings();
  void enable();
  void readPersistentString(char* s, int max_length, int& adr);
  void readPersistentByte(uint8_t& b, int& adr);
  void readPersistentParams();
  void writePersistentString(const char* s, size_t max_length, int& adr);
  void writePersistentByte(uint8_t b, int& adr);
  void writePersistentParams(const char* ssid, const char* password);
  void handleNotFound();
  void handleSetupRoot();
  void activateSetupAP();
  bool activateWifi();
  void processNetwork(bool setup_mode);
  
public:
  std::shared_ptr<ESP8266WebServer> server;

  //For SETUP_SSID AP
  DNSServer dnsServer;
  static constexpr const byte DNS_PORT = 53;
  std::shared_ptr<IPAddress> apIP;

  char ssid_param[MAX_SSID_LENGTH+1];
  char password_param[MAX_PASSWORD_LENGTH+1];
  char mqtt_servername_param[MAX_MQTT_SERVERNAME_LENGTH+1];
  uint16_t mqtt_serverport_param = MQTT_DEFAULT_PORT;
  char mqtt_sensorid_param[MAX_MQTT_SENSORID_LENGTH+1];
  char mqtt_username_param[MAX_MQTT_USERNAME_LENGTH+1];
  char mqtt_password_param[MAX_MQTT_PASSWORD_LENGTH+1];

  unsigned int conf_sec_between_reading;
  byte conf_plant_count;
  float conf_watering_trigger_value[MAX_PLANT_COUNT];
  unsigned int conf_watering_duration_ms[MAX_PLANT_COUNT];
  unsigned int conf_watering_grace_period_sec[MAX_PLANT_COUNT];
};

#endif // _SETTINGS_H_
