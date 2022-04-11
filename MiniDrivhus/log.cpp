#include "log.h"

#if (LOG_LEVEL>LOG_LEVEL_NONE)

#ifndef TESTING
# include <HardwareSerial.h>
#endif

#include "global.h"
#include "mqtt.h"
#include "settings.h"


void Log::enable()
{
  if (!enabled)
  {
    enabled = true;
    if (log_mode!=LOG_NONE)
    {
      Serial.begin(9600);
    }
  }
}

void Log::disable()
{
  if (enabled)
  {
    enabled = false;
    if (log_mode!=LOG_NONE)
    {
      Serial.flush();
      Serial.end();
    }
  }
}

void Log::print(const char* msg)
{
  if (enabled && msg)
  {
    if (log_mode==LOG_SERIAL)
    {
      Serial.println(msg);
    }
    else if (log_mode==LOG_MQTT && g_mqtt.isEnabled())
    {
      if (g_mqtt.connectMQTT())
      {
        g_mqtt.publishMQTTValue((String(g_settings.mqtt_sensorid_param)+F(MQTT_LOG_TOPIC)).c_str(), msg);
      }
      else
      {
        Serial.println("Logging failed - MQTT is not connected");
      }
    }
  }
}

#endif // (LOG_LEVEL>LOG_LEVEL_NONE)
