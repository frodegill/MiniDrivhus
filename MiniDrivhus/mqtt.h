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
  bool connectMQTT();
  bool isRequested();
  void initialize();
  void loop();

  bool isEnabled() const {return mqtt_enabled;}

private:
  bool mqtt_enabled;

public:
  WiFiClient esp_client;
  std::shared_ptr<PubSubClient> mqtt_client;
};

#endif // _MQTT_H_
