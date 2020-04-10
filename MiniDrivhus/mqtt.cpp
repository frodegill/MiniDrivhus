#include "mqtt.h"

#include "global.h"
#include "debug.h"
#include "settings.h"


extern volatile bool plant_water_requested[MAX_PLANT_COUNT];

void globalMQTTCallback(char* topic, byte* payload, unsigned int length)
{
  //DEBUG_MSG("start mqtt callback");
  g_mqtt.callback(topic, payload, length);  
}


void MQTT::callback(char* topic, byte* payload, unsigned int length)
{
  if (0 != strncmp(g_settings.mqtt_sensorid_param, topic, strlen(g_settings.mqtt_sensorid_param)))
  {
    DEBUG_MSG((String("Ignoring unrelated mqtt::callback ")+String(topic)).c_str());
    return;
  }

  String value;
  value.concat(reinterpret_cast<const char*>(payload), length);

  DEBUG_MSG((String("mqttCallback: ")+String(topic)+String(" value ")+value).c_str());

  const char* key = topic + strlen(g_settings.mqtt_sensorid_param);

  if (0 == strncmp("plant", key, 5))
  {
    key += 5;
    byte plantno = 0;
    while (*key>='0' && *key<='9')
    {
      plantno = plantno*10 + (*key++-'0');
    }

    if (plantno <= MAX_PLANT_COUNT && *key++=='/')
    {
      if (0 == strcmp("water_now", key))
      {
        plant_water_requested[plantno] = true;
        DEBUG_MSG((String("mqttCallback request water plant=")+String((int)plantno)).c_str());
      }
    }
  }
  else if (0 == strcmp("config/sec_between_reading", key))
  {
    g_settings.conf_sec_between_reading = max(1, atoi(value.c_str()));
    DEBUG_MSG((String("mqttCallback conf_sec_between_reading=")+String((int)g_settings.conf_sec_between_reading)).c_str());
  }
  else if (0 == strcmp("config/plant_count", key))
  {
    g_settings.conf_plant_count = max(1, min(static_cast<int>(MAX_PLANT_COUNT), atoi(value.c_str())));
    DEBUG_MSG((String("mqttCallback conf_plant_count=")+String((int)g_settings.conf_plant_count)).c_str());
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
        g_settings.conf_watering_trigger_value[plantno] = max(0.0, min(100.0, atof(value.c_str())));
        DEBUG_MSG((String("mqttCallback plant=")+String((int)plantno)+String(" conf_watering_trigger_value=")+String(g_settings.conf_watering_trigger_value[plantno], 4)).c_str());
      }
      else if (0 == strcmp("watering_duration_ms", key))
      {
        g_settings.conf_watering_duration_ms[plantno] = max(1, atoi(value.c_str()));
        DEBUG_MSG((String("mqttCallback plant=")+String((int)plantno)+String(" conf_watering_duration_ms=")+String((int)g_settings.conf_watering_duration_ms[plantno])).c_str());
      }
      else if (0 == strcmp("watering_grace_period_sec", key))
      {
        g_settings.conf_watering_grace_period_sec[plantno] = max(1, atoi(value.c_str()));
        DEBUG_MSG((String("mqttCallback plant=")+String((int)plantno)+String(" conf_watering_grace_period_sec=")+String((int)g_settings.conf_watering_grace_period_sec[plantno])).c_str());
      }
    }
  }
}

bool MQTT::publishMQTTValue(const String& topic, const String& msg)
{
  //DEBUG_MSG("start mqtt::publishMQTTValue1");
  if (isEnabled() && connectMQTT())
  {
    if (!mqtt_client.publish((String(g_settings.mqtt_sensorid_param)+topic).c_str(), msg.c_str(), true))
    {
      if (g_debug.debug_mode!=Debug::DEBUG_NONE)
      {
        Serial.println((String("publishMQTTValue topic=")+String(g_settings.mqtt_sensorid_param)+topic+String(" msg=")+msg+String(" returned false")).c_str());
      }
      return false;
    }
    return true;
  }
  else
  {
    DEBUG_MSG("Cannot publish. Not connected");
    return false;
  }
}

bool MQTT::publishMQTTValue(const String& topic, float value)
{
  //DEBUG_MSG("start mqtt::publishMQTTValue2");
  return publishMQTTValue(topic, String(value, 4));
}

bool MQTT::connectMQTT()
{
  //DEBUG_MSG("start mqtt::connectMQTT");
  byte i = 0;
  while (i++<10 && isEnabled() && !mqtt_client.connected())
  {
    DEBUG_MSG("MQTT connect");
    if (mqtt_client.connect(g_settings.mqtt_sensorid_param, g_settings.mqtt_username_param, g_settings.mqtt_password_param))
    {
      DEBUG_MSG("MQTT connected");

      bool subscribe_ok = true;
      
      String topic;
      
      topic = String(g_settings.mqtt_sensorid_param)+F("config/sec_between_reading");
      DEBUG_MSG((String("Subscribing to ") + topic).c_str());
      subscribe_ok &= mqtt_client.subscribe(topic.c_str());

      topic = String(g_settings.mqtt_sensorid_param)+F("config/plant_count");
      DEBUG_MSG((String("Subscribing to ") + topic).c_str());
      subscribe_ok &= mqtt_client.subscribe(topic.c_str());

      byte i;
      for (i=0; i<MAX_PLANT_COUNT; i++)
      {
        String config = F("config/");
        
        String plant = F("plant");
        plant += String(i);
        plant += F("/");
        
        topic = String(g_settings.mqtt_sensorid_param)+config+plant+F("watering_trigger_value");
        DEBUG_MSG((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());

        topic = String(g_settings.mqtt_sensorid_param)+config+plant+F("watering_duration_ms");
        DEBUG_MSG((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());

        topic = String(g_settings.mqtt_sensorid_param)+config+plant+F("watering_grace_period_sec");
        DEBUG_MSG((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());

        topic = String(g_settings.mqtt_sensorid_param)+plant+F("water_now");
        DEBUG_MSG((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());
      }

      DEBUG_MSG((String("MQTT topics subscribed ") + String(subscribe_ok ? "ok" : "with error")).c_str());
    }
    else
    {
      DEBUG_MSG("MQTT waiting for reconnect");
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
  DEBUG_MSG("start mqtt::initialize");
  mqtt_client.setClient(esp_client);
  mqtt_client.setServer(g_settings.mqtt_servername_param, 1883);
  mqtt_client.setCallback(globalMQTTCallback);
  mqtt_enabled = true;
  connectMQTT();
}

void MQTT::loop()
{
  mqtt_client.loop();
}
