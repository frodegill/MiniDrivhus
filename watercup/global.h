#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#include <pins_arduino.h>

static constexpr uint8_t I_SETUP_MODE_PIN  = D2; // pull to Ground to enable setup mode
static constexpr uint8_t O_TRIGGER_PIN     = D5;
static constexpr uint8_t I_LOW_SENSOR_PIN  = D6;
static constexpr uint8_t I_HIGH_SENSOR_PIN = D7;

static constexpr unsigned long LONG_DELAY = 15*60*1000L;
static constexpr unsigned long SHORT_DELAY = 1000L;

class MQTT;
extern MQTT g_mqtt;

class NTP;
extern NTP g_ntp;

class Settings;
extern Settings g_settings;

#endif // _GLOBAL_H_
