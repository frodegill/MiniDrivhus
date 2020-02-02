#include "debug.h"

#include <HardwareSerial.h>

#include "global.h"
#include "mqtt.h"
#include "settings.h"


void Debug::enable()
{
  if (!enabled)
  {
    enabled = true;
    if (debug_mode==DEBUG_SERIAL)
    {
      Serial.begin(9600);
    }
  }
}

void Debug::disable()
{
  if (enabled)
  {
    enabled = false;
    if (debug_mode==DEBUG_SERIAL)
    {
      Serial.flush();
      Serial.end();
    }
  }
}

void Debug::print(const char* msg)
{
  if (enabled && msg)
  {
    if (debug_mode==DEBUG_SERIAL)
    {
      Serial.println(msg);
    }
    else if (debug_mode==DEBUG_MQTT && g_mqtt.mqtt_enabled)
    {
      if (g_mqtt.connectMQTT())
      {
        Serial.println((String("MQTT publish returned ") + String(g_mqtt.mqtt_client->publish((String(g_settings.mqtt_sensorid_param)+F(MQTT_DEBUG_TOPIC)).c_str(), msg)?"true":"false")).c_str());
      }
      else
      {
        Serial.println("Debug failed - MQTT is not connected");
      }
    }
  }
}
