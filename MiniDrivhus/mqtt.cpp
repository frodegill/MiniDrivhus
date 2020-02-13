#include "mqtt.h"

#include "global.h"
#include "debug.h"
#include "settings.h"


void mqttCallback(char* topic, byte* payload, unsigned int length)
{
  g_mqtt.mqttCallback(topic, payload, length);  
}


MQTT::MQTT()
{
  mqtt_client = std::make_shared<PubSubClient>(esp_client);
}

void MQTT::mqttCallback(char* topic, byte* payload, unsigned int length)
{
  if (0 != strncmp(g_settings.mqtt_sensorid_param, topic, strnlen(g_settings.mqtt_sensorid_param, length)))
  {
    return;
  }

  g_debug.print((String("mqttCallback: ")+String(topic)+String(" value ")+String(reinterpret_cast<const char*>(payload))).c_str());
  const char* key = topic + strlen(g_settings.mqtt_sensorid_param);

  if (0 == strcmp("config/sec_between_reading", key))
  {
    g_settings.conf_sec_between_reading = max(1, atoi(reinterpret_cast<const char*>(payload)));
    g_debug.print((String("mqttCallback conf_sec_between_reading=")+String((int)g_settings.conf_sec_between_reading)).c_str());
  }
  else if (0 == strcmp("config/plant_count", key))
  {
    g_settings.conf_plant_count = min(static_cast<int>(MAX_PLANT_COUNT), max(1, atoi(reinterpret_cast<const char*>(payload))));
    g_debug.print((String("mqttCallback conf_plant_count=")+String((int)g_settings.conf_plant_count)).c_str());
    if (g_settings.conf_plant_count < MAX_PLANT_COUNT)
    {
      //Turn off watering for all unused plants
      byte i;
      for (i=g_settings.conf_plant_count; i<MAX_PLANT_COUNT-1; i++)
      {
        digitalWrite(O_PLANT_WATERING_PINS[i], LOW);
      }
    }
  }
  else if (0 == strncmp("config/plant", key, 12))
  {
    key += 12;
    byte plantno = 0;
    while (*key>='0' && *key<='9')
    {
      plantno = plantno*10 + (*key++-'0');
    }

    if (plantno <= MAX_PLANT_COUNT && *key++=='/')
    {
      if (0 == strcmp("watering_trigger_value", key))
      {
        g_settings.conf_watering_trigger_value[plantno] = min(100, max(0, atoi(reinterpret_cast<const char*>(payload))));
        g_debug.print((String("mqttCallback plant=")+String((int)plantno)+String(" conf_watering_trigger_value=")+String((int)g_settings.conf_watering_trigger_value[plantno])).c_str());
      }
      else if (0 == strcmp("watering_duration_ms", key))
      {
        g_settings.conf_watering_duration_ms[plantno] = max(1, atoi(reinterpret_cast<const char*>(payload)));
        g_debug.print((String("mqttCallback plant=")+String((int)plantno)+String(" conf_watering_duration_ms=")+String((int)g_settings.conf_watering_duration_ms[plantno])).c_str());
      }
      else if (0 == strcmp("watering_grace_period_sec", key))
      {
        g_settings.conf_watering_grace_period_sec[plantno] = max(1, atoi(reinterpret_cast<const char*>(payload)));
        g_debug.print((String("mqttCallback plant=")+String((int)plantno)+String(" conf_watering_grace_period_sec=")+String((int)g_settings.conf_watering_grace_period_sec[plantno])).c_str());
      }
    }
  }
}

void MQTT::publishMQTTValue(const String& topic, const String& msg)
{
  if (mqtt_enabled && connectMQTT())
  {
    bool ret = mqtt_client->publish((String(g_settings.mqtt_sensorid_param)+topic).c_str(), msg.c_str(), true);
    g_debug.print((String("publishMQTTValue topic=")+String(g_settings.mqtt_sensorid_param)+topic+String(" msg=")+msg+String(" returned ")+String(ret?"true":"false")).c_str());
  }
}

void MQTT::publishMQTTValue(const String& topic, float value)
{
  publishMQTTValue(topic, String(value, 4));
}

bool MQTT::connectMQTT()
{
  byte i = 0;
  while (i++<10 && mqtt_enabled && !mqtt_client->connected())
  {
    if (mqtt_client->connect(g_settings.mqtt_sensorid_param, g_settings.mqtt_username_param, g_settings.mqtt_password_param))
    {
      g_debug.print("MQTT connected");

      mqtt_client->subscribe((String(g_settings.mqtt_sensorid_param)+F("config/sec_between_reading")).c_str());
      mqtt_client->subscribe((String(g_settings.mqtt_sensorid_param)+F("config/plant_count")).c_str());

      byte i;
      for (i=0; i<MAX_PLANT_COUNT; i++)
      {
        String plant = F("plant");
        plant += String(i);
        plant += F("/");
        
        mqtt_client->subscribe((String(g_settings.mqtt_sensorid_param)+plant+F("watering_trigger_value")).c_str());
        mqtt_client->subscribe((String(g_settings.mqtt_sensorid_param)+plant+F("watering_duration_ms")).c_str());
        mqtt_client->subscribe((String(g_settings.mqtt_sensorid_param)+plant+F("watering_grace_period_sec")).c_str());
      }

      g_debug.print("MQTT topics subscribed");
    }
    else
    {
      g_debug.print("MQTT waiting for reconnect");
      // Wait 3 seconds before retrying
      delay(3000);
    }
  }
  return mqtt_client->connected();
}

bool MQTT::isRequested()
{
  return g_settings.mqtt_servername_param && *g_settings.mqtt_servername_param;
}

void MQTT::initialize()
{
  mqtt_client->setServer(g_settings.mqtt_servername_param, 1883);
  mqtt_client->setCallback(::mqttCallback);
  connectMQTT();
}

void MQTT::loop()
{
  mqtt_client->loop();
}
