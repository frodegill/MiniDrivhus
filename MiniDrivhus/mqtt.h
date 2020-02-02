#ifndef _MQTT_H_
#define _MQTT_H_

#include <ESP8266WiFi.h>
#include <PubSubClient.h>


void mqttCallback(char* topic, byte* payload, unsigned int length);

class MQTT
{
public:
  MQTT();
  void mqttCallback(char* topic, byte* payload, unsigned int length);
  void publishMQTTValue(const String& topic, const String& msg);
  void publishMQTTValue(const String& topic, float value);
  void publishMQTTValues(const float* g_temperature, const float* g_humidity, const float* g_mq2_value, const float* g_mq7_value, const float* g_mq8_value, const float* g_mq9_value, const float* g_mq135_value);
  bool connectMQTT();
  bool isRequested();
  void initialize();
  void loop();

public:
  bool mqtt_enabled;

  WiFiClient esp_client;
  std::shared_ptr<PubSubClient> mqtt_client;
};

#endif // _MQTT_H_
