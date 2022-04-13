#ifndef _GLOBAL_H_
#define _GLOBAL_H_

#ifdef TESTING
# include "testing.h"
#else
# include <pins_arduino.h>
#endif

static constexpr uint8_t I_SETUP_MODE_PIN             = D2; // pull to Ground to enable setup mode
static constexpr uint8_t I_TEMPHUMIDSENSOR_PIN        = D3;
static constexpr uint8_t O_FAN_RELAY_ACTIVATE_PIN     = D9;
static constexpr uint8_t O_LIGHT_RELAY_ACTIVATE_PIN   = D10;
static constexpr uint8_t O_PLANT_WATERING_PINS[]      = {D6, D7, D8};
static constexpr uint8_t O_ANALOG_ADDR_S0             = D4;
static constexpr uint8_t O_ANALOG_ADDR_S1             = D5;
static constexpr uint8_t ANALOG_LIGHT_SENSOR_ADDR     = 0;
static constexpr uint8_t ANALOG_PLANT_SENSOR_ADDR[]   = {1, 2, 3};

static constexpr uint8_t MAX_PLANT_COUNT = sizeof(O_PLANT_WATERING_PINS)/sizeof(O_PLANT_WATERING_PINS[0]);

static constexpr uint16_t MAX_ANALOG_VALUE = 1023;


static constexpr uint8_t OLD = 0;
static constexpr uint8_t CURRENT = 1;


class Log;
extern Log g_log;

class MQTT;
extern MQTT g_mqtt;

class NTP;
extern NTP g_ntp;

class Settings;
extern Settings g_settings;

#endif // _GLOBAL_H_
