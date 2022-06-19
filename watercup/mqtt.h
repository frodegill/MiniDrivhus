#ifndef _MQTT_H_
#define _MQTT_H_

#include <ESP8266WiFi.h>
#include <PubSubClient.h>


class MQTT
{
public:
  bool publishMQTTValue(const String& topic, const String& msg);
  bool publishMQTTValue(const String& topic, float value);
  bool connectMQTT();
  bool isRequested();
  void initialize();
  void loop();

  bool isEnabled() const {return mqtt_enabled;}

private:
  bool mqtt_enabled;

public:
  WiFiClient esp_client;
  PubSubClient mqtt_client;
};

#endif // _MQTT_H_
