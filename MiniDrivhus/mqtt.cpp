#include "mqtt.h"

#include "global.h"
#include "log.h"
#include "settings.h"


extern volatile bool plant_water_requested[MAX_PLANT_COUNT];

void globalMQTTCallback(char* topic, byte* payload, unsigned int length)
{
  //LOG_DEBUG("start mqtt callback");
  g_mqtt.callback(topic, payload, length);  
}


void MQTT::callback(char* topic, byte* payload, unsigned int length)
{
  if (0 != strncmp(g_settings.mqtt_sensorid_param, topic, strlen(g_settings.mqtt_sensorid_param)))
  {
    LOG_INFO((String("Ignoring unrelated mqtt::callback ")+String(topic)).c_str());
    return;
  }

  String value;
  value.concat(reinterpret_cast<const char*>(payload), length);

  LOG_DEBUG((String("mqttCallback: ")+String(topic)+String(" value ")+value).c_str());

  const char* key = topic + strlen(g_settings.mqtt_sensorid_param);

  if (0 == strncmp("plant", key, 5))
  {
    key += 5;
    byte plantno = 0;
    while (*key>='0' && *key<='9')
    {
      plantno = plantno*10 + (*key++-'0');
    }

    if (plantno < MAX_PLANT_COUNT && *key++=='/')
    {
      if (0 == strcmp("water_now", key))
      {
        plant_water_requested[plantno] = true;
        LOG_INFO((String("mqttCallback request water plant=")+String((int)plantno)).c_str());
      }
    }
  }
  else if (0 == strcmp("config/sec_between_reading", key))
  {
    g_settings.conf_sec_between_reading = max(1, atoi(value.c_str()));
    LOG_INFO((String("mqttCallback conf_sec_between_reading=")+String((int)g_settings.conf_sec_between_reading)).c_str());
  }
  else if (0 == strcmp("config/growlight_minutes_pr_day", key))
  {
    g_settings.conf_growlight_minutes_pr_day = min(60*24, max(0, atoi(value.c_str())));
    LOG_INFO((String("mqttCallback growlight_minutes_pr_day=")+String((short)g_settings.conf_growlight_minutes_pr_day)).c_str());
  }
  else if (0 == strcmp("config/fan_activate_temp", key))
  {
    g_settings.conf_fan_activate_temp_value = max(0.0, min(100.0, atof(value.c_str())));
    LOG_INFO((String("mqttCallback conf_fan_activate_temp_value=")+String((int)g_settings.conf_fan_activate_temp_value)).c_str());
    digitalWrite(O_FAN_RELAY_ACTIVATE_PIN, LOW);
  }
  else if (0 == strcmp("config/fan_activate_humid", key))
  {
    g_settings.conf_fan_activate_humid_value = max(0.0, min(100.0, atof(value.c_str())));
    LOG_INFO((String("mqttCallback conf_fan_activate_humid_value=")+String((int)g_settings.conf_fan_activate_humid_value)).c_str());
    digitalWrite(O_FAN_RELAY_ACTIVATE_PIN, LOW);
  }
  else if (0 == strncmp("config/plant", key, 12))
  {
    key += 12;
    byte plantno = 0;
    while (*key>='0' && *key<='9')
    {
      plantno = plantno*10 + (*key++-'0');
    }

    if (plantno<MAX_PLANT_COUNT && *key++=='/')
    {
      if (0 == strcmp("enabled", key))
      {
        g_settings.conf_plant_enabled[plantno] = (atoi(value.c_str())!=0) ? 1 : 0;
        LOG_INFO((String("mqttCallback plant=")+String((int)plantno)+String(" enabled=")+String((int)g_settings.conf_plant_enabled[plantno])).c_str());
        if (!g_settings.conf_plant_enabled[plantno])
        {
          digitalWrite(O_PLANT_WATERING_PINS[plantno], LOW);
        }
      }
      else if (0 == strcmp("dry_value", key))
      {
        g_settings.conf_dry_value[plantno] = max(0.0, min(100.0, atof(value.c_str())));
        LOG_INFO((String("mqttCallback plant=")+String((int)plantno)+String(" conf_dry_value=")+String(g_settings.conf_dry_value[plantno], 4)).c_str());
      }
      else if (0 == strcmp("wet_value", key))
      {
        g_settings.conf_wet_value[plantno] = max(0.0, min(100.0, atof(value.c_str())));
        LOG_INFO((String("mqttCallback plant=")+String((int)plantno)+String(" conf_wet_value=")+String(g_settings.conf_wet_value[plantno], 4)).c_str());
      }
      else if (0 == strcmp("watering_duration_ms", key))
      {
        g_settings.conf_watering_duration_ms[plantno] = max(1, atoi(value.c_str()));
        LOG_INFO((String("mqttCallback plant=")+String((int)plantno)+String(" conf_watering_duration_ms=")+String((int)g_settings.conf_watering_duration_ms[plantno])).c_str());
      }
      else if (0 == strcmp("watering_grace_period_sec", key))
      {
        g_settings.conf_watering_grace_period_sec[plantno] = max(1, atoi(value.c_str()));
        LOG_INFO((String("mqttCallback plant=")+String((int)plantno)+String(" conf_watering_grace_period_sec=")+String((int)g_settings.conf_watering_grace_period_sec[plantno])).c_str());
      }
    }
  }
}

bool MQTT::publishMQTTValue(const String& topic, const String& msg)
{
  //LOG_DEBUG("start mqtt::publishMQTTValue1");
  if (isEnabled() && connectMQTT())
  {
    if (!mqtt_client.publish((String(g_settings.mqtt_sensorid_param)+topic).c_str(), msg.c_str(), true))
    {
#if (LOG_LEVEL>LOG_LEVEL_NONE)
      if (g_log.log_mode!=Log::LOG_NONE)
      {
        Serial.println((String("publishMQTTValue topic=")+String(g_settings.mqtt_sensorid_param)+topic+String(" msg=")+msg+String(" returned false")).c_str());
      }
#endif // (LOG_LEVEL>=LOG_LEVEL_ERROR)
      return false;
    }
    return true;
  }
  else
  {
    LOG_ERROR("Cannot publish. Not connected");
    return false;
  }
}

bool MQTT::publishMQTTValue(const String& topic, float value)
{
  //LOG_DEBUG("start mqtt::publishMQTTValue2");
  return publishMQTTValue(topic, String(value, 4));
}

bool MQTT::connectMQTT()
{
  //LOG_DEBUG("start mqtt::connectMQTT");
  byte i = 0;
  while (i++<10 && isEnabled() && !mqtt_client.connected())
  {
    LOG_DEBUG("MQTT connect");
    if (mqtt_client.connect(g_settings.mqtt_sensorid_param, g_settings.mqtt_username_param, g_settings.mqtt_password_param))
    {
      LOG_INFO("MQTT connected");

      bool subscribe_ok = true;
      
      String topic;
      
      topic = String(g_settings.mqtt_sensorid_param)+F("config/sec_between_reading");
      LOG_INFO((String("Subscribing to ") + topic).c_str());
      subscribe_ok &= mqtt_client.subscribe(topic.c_str());

      topic = String(g_settings.mqtt_sensorid_param)+F("config/growlight_minutes_pr_day");
      LOG_INFO((String("Subscribing to ") + topic).c_str());
      subscribe_ok &= mqtt_client.subscribe(topic.c_str());

      topic = String(g_settings.mqtt_sensorid_param)+F("config/fan_activate_temp");
      LOG_INFO((String("Subscribing to ") + topic).c_str());
      subscribe_ok &= mqtt_client.subscribe(topic.c_str());

      topic = String(g_settings.mqtt_sensorid_param)+F("config/fan_activate_humid");
      LOG_INFO((String("Subscribing to ") + topic).c_str());
      subscribe_ok &= mqtt_client.subscribe(topic.c_str());
 
      byte j;
      for (j=0; j<MAX_PLANT_COUNT; j++)
      {
        String config = F("config/");
        
        String plant = F("plant");
        plant += String(j);
        plant += F("/");
        
        topic = String(g_settings.mqtt_sensorid_param)+config+plant+F("enabled");
        LOG_INFO((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());

        topic = String(g_settings.mqtt_sensorid_param)+config+plant+F("dry_value");
        LOG_INFO((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());

        topic = String(g_settings.mqtt_sensorid_param)+config+plant+F("wet_value");
        LOG_INFO((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());

        topic = String(g_settings.mqtt_sensorid_param)+config+plant+F("watering_duration_ms");
        LOG_INFO((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());

        topic = String(g_settings.mqtt_sensorid_param)+config+plant+F("watering_grace_period_sec");
        LOG_INFO((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());

        topic = String(g_settings.mqtt_sensorid_param)+plant+F("water_now");
        LOG_INFO((String("Subscribing to ") + topic).c_str());
        subscribe_ok &= mqtt_client.subscribe(topic.c_str());
      }

      if (!subscribe_ok)
      {
        LOG_ERROR("MQTT topics subscribed with error");
      }
    }
    else
    {
      LOG_INFO("MQTT waiting for reconnect");
      LOG_DEBUG(g_settings.mqtt_sensorid_param);
      LOG_DEBUG(g_settings.mqtt_username_param);
      LOG_DEBUG(g_settings.mqtt_password_param);
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
  LOG_DEBUG("start mqtt::initialize");
  mqtt_client.setClient(esp_client);
  mqtt_client.setServer(g_settings.mqtt_servername_param, 1883);
  mqtt_client.setCallback(globalMQTTCallback);
  mqtt_enabled = true;
  connectMQTT();
}

void MQTT::loop()
{
  do {
    mqtt_client.loop();
  } while (esp_client.available() > 0);
}
