#include "mqtt.h"

#include "global.h"
#include "settings.h"


bool MQTT::publishMQTTValue(const String& topic, const String& msg)
{
  if (isEnabled() && connectMQTT())
  {
    if (!mqtt_client.publish((String(g_settings.mqtt_sensorid_param)+topic).c_str(), msg.c_str(), true))
    {
      return false;
    }
    return true;
  }
  else
  {
    return false;
  }
}

bool MQTT::publishMQTTValue(const String& topic, float value)
{
  return publishMQTTValue(topic, String(value, 4));
}

bool MQTT::connectMQTT()
{
  byte i = 0;
  while (i++<10 && isEnabled() && !mqtt_client.connected())
  {
    if (!mqtt_client.connect(g_settings.mqtt_sensorid_param, g_settings.mqtt_username_param, g_settings.mqtt_password_param))
    {
      // Wait 3 seconds before retrying
      delay(3000);
    }
  }
  return mqtt_client.connected();
}

bool MQTT::isRequested()
{
  return g_settings.mqtt_servername_param && *g_settings.mqtt_servername_param;
}

void MQTT::initialize()
{
  mqtt_client.setClient(esp_client);
  mqtt_client.setServer(g_settings.mqtt_servername_param, 1883);
  mqtt_enabled = true;
  connectMQTT();
}

void MQTT::loop()
{
  mqtt_client.loop();
}
