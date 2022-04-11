#ifndef _MQTT_H_
#define _MQTT_H_

#ifdef TESTING
# include "testing.h"
#else
# include <ESP8266WiFi.h>
# include <PubSubClient.h>
#endif

void globalMQTTCallback(char* topic, byte* payload, unsigned int length);

class MQTT
{
public:
  void callback(char* topic, byte* payload, unsigned int length);
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
