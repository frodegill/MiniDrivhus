#include "global.h"

#include "log.h"
#include "mqtt.h"
#include "ntp.h"
#include "settings.h"

#ifdef TESTING
# include "testing.h"
#else
# include <DHTesp.h>           // Library: DHT_sensor_library_for_ESPx
# include <Ticker.h>
# include <TimeLib.h>
#endif

/* PCB v4 for WeMos D1 R1

 MQTT paths:
 minidrivhus/<sensorid>/light
 minidrivhus/<sensorid>/temp
 minidrivhus/<sensorid>/humidity
 minidrivhus/<sensorid>/fan_on
 minidrivhus/<sensorid>/plant[0..(n-1)]/moisture
 minidrivhus/<sensorid>/plant[0..(n-1)]/watering_count
 minidrivhus/<sensorid>/plant[0..(n-1)]/water_now
 minidrivhus/<sensorid>/config/sec_between_reading
 minidrivhus/<sensorid>/config/growlight_minutes_pr_day                   [0 . 1440]
 minidrivhus/<sensorid>/config/fan_activate_temp                          [0.0 - 100.0]
 minidrivhus/<sensorid>/config/fan_activate_humid                         [0.0 - 100.0]
 minidrivhus/<sensorid>/config/plant[0..(n-1)]/enabled                    [0, 1]
 minidrivhus/<sensorid>/config/plant[0..(n-1)]/dry_value                  [wet - 100.0]
 minidrivhus/<sensorid>/config/plant[0..(n-1)]/wet_value                  [0.0 - dry]
 minidrivhus/<sensorid>/config/plant[0..(n-1)]/watering_duration_ms
 minidrivhus/<sensorid>/config/plant[0..(n-1)]/watering_grace_period_sec
 minidrivhus/<sensorid>/debug
 */

#define DHT_MODEL (DHTesp::AM2302)


static const unsigned int DELAY_BETWEEN_ACTIVE_SENSORS     = 25; //ms between activating sensors

enum State {
  SETUP_MODE,

  START,
  
  ACTIVATE_PLANT_SENSOR_1,
  READ_PLANT_SENSOR_1,
  ACTIVATE_PLANT_WATERING_1,
  DEACTIVATE_PLANT_WATERING_1,

  ACTIVATE_PLANT_SENSOR_2,
  READ_PLANT_SENSOR_2,
  ACTIVATE_PLANT_WATERING_2,
  DEACTIVATE_PLANT_WATERING_2,

  ACTIVATE_PLANT_SENSOR_3,
  READ_PLANT_SENSOR_3,
  ACTIVATE_PLANT_WATERING_3,
  DEACTIVATE_PLANT_WATERING_3,

  ACTIVATE_LIGHTSENSOR,
  READ_LIGHTSENSOR,

  READ_TEMPHUMIDSENSOR, //DHTxx cannot be read in interrupt, so this state is unused (reading is done in main loop)

  FINISHED
};


#if (LOG_LEVEL>LOG_LEVEL_NONE)
  Log g_log;
#endif // (LOG_LEVEL>LOG_LEVEL_NONE)
MQTT g_mqtt;
NTP g_ntp;
time_t local_time;
Settings g_settings;
DHTesp dht;

Ticker ticker;

volatile State state;
volatile bool should_read_temp_sensor;

volatile float plant_sensor_value[2][MAX_PLANT_COUNT];
volatile uint16_t plant_watering_count[2][MAX_PLANT_COUNT];
volatile unsigned long previous_plant_watering_time[MAX_PLANT_COUNT];
volatile bool plant_water_requested[MAX_PLANT_COUNT];
volatile bool plant_in_watering_cycle[MAX_PLANT_COUNT];
volatile uint16_t fan_value[2];
volatile float lightsensor_value[2];
float tempsensor_value[2];
float humiditysensor_value[2];

bool growlight_lit;


byte whichOf(const volatile State& state, const State& /*o1_default*/, const State& o2, const State& o3)
{
  if (state==o2) return 1;
  else if (state==o3) return 2;
  else return 0;
}

State nextState(byte plant, const State& o1_default, const State& o2, const State& o3)
{
  if (plant==1) return o2;
  else if (plant==2) return o3;
  else return o1_default;
}

void selectAnalogAddr(byte addr)
{
  digitalWrite(O_ANALOG_ADDR_S0, (addr&0x01)==0?LOW:HIGH);
  digitalWrite(O_ANALOG_ADDR_S1, (addr&0x02)==0?LOW:HIGH);
}

void onTick()
{
  switch(state)
  {
    case START:
      {
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = ACTIVATE_PLANT_SENSOR_1;
        break;
      }

    case ACTIVATE_PLANT_SENSOR_1:
    case ACTIVATE_PLANT_SENSOR_2:
    case ACTIVATE_PLANT_SENSOR_3:
      {
        byte plant = whichOf(state, ACTIVATE_PLANT_SENSOR_1, ACTIVATE_PLANT_SENSOR_2, ACTIVATE_PLANT_SENSOR_3);
        selectAnalogAddr(ANALOG_PLANT_SENSOR_ADDR[plant]);
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = nextState(plant, READ_PLANT_SENSOR_1, READ_PLANT_SENSOR_2, READ_PLANT_SENSOR_3);
        break;
      }

    case READ_PLANT_SENSOR_1:
    case READ_PLANT_SENSOR_2:
    case READ_PLANT_SENSOR_3:
      {
        byte plant = whichOf(state, READ_PLANT_SENSOR_1, READ_PLANT_SENSOR_2, READ_PLANT_SENSOR_3);
        plant_sensor_value[CURRENT][plant] = max(0.0f, min(100.0f, 100.0f*static_cast<float>(analogRead(A0))/static_cast<float>(MAX_ANALOG_VALUE)));
        LOG_DEBUG((String("onTick - read sensor ")+String((int)plant)+String(", got ")+String((int)plant_sensor_value[CURRENT][plant])).c_str());
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = nextState(plant, ACTIVATE_PLANT_WATERING_1, ACTIVATE_PLANT_WATERING_2, ACTIVATE_PLANT_WATERING_3);
        break;
      }

    case ACTIVATE_PLANT_WATERING_1:
    case ACTIVATE_PLANT_WATERING_2:
    case ACTIVATE_PLANT_WATERING_3:
      {
        byte plant = whichOf(state, ACTIVATE_PLANT_WATERING_1, ACTIVATE_PLANT_WATERING_2, ACTIVATE_PLANT_WATERING_3);

        if (g_settings.conf_plant_enabled[plant])
        {
          if (plant_in_watering_cycle[plant] && plant_sensor_value[CURRENT][plant]<=g_settings.conf_wet_value[plant])
          {
            plant_in_watering_cycle[plant] = false;
            LOG_INFO((String("onTick - plant")+String((int)plant)+String(" exiting watering cycle: ")+String(plant_sensor_value[CURRENT][plant], 4)+String(", ")+String(g_settings.conf_wet_value[plant], 4) +String("-") + String(g_settings.conf_dry_value[plant], 4)).c_str());
          }
          else if (!plant_in_watering_cycle[plant] && plant_sensor_value[CURRENT][plant]>=g_settings.conf_dry_value[plant])
          {
            plant_in_watering_cycle[plant] = true;
            LOG_INFO((String("onTick - plant")+String((int)plant)+String(" entering watering cycle: ")+String(plant_sensor_value[CURRENT][plant], 4)+String(", ")+String(g_settings.conf_wet_value[plant], 4) +String("-") + String(g_settings.conf_dry_value[plant], 4)).c_str());
          }
        }
        
        if (g_settings.conf_plant_enabled[plant] && (plant_water_requested[plant] || plant_in_watering_cycle[plant]))
        {
          unsigned long current_sec = millis()/1000;
          if (current_sec < previous_plant_watering_time[plant]) //Wrapped. Happens every ~50 days
          {
            previous_plant_watering_time[plant] = current_sec;
          }
          
          if (plant_water_requested[plant] || (previous_plant_watering_time[plant]==0 || (previous_plant_watering_time[plant]+g_settings.conf_watering_grace_period_sec[plant] < current_sec)))
          {
            LOG_DEBUG((String("onTick - activate watering ")+String((int)plant) + ": " + String(plant_sensor_value[CURRENT][plant], 4)).c_str());
            digitalWrite(O_PLANT_WATERING_PINS[plant], HIGH);
            plant_watering_count[CURRENT][plant]++;
            previous_plant_watering_time[plant] = current_sec;
            plant_water_requested[plant] = false;
            ticker.attach_ms(g_settings.conf_watering_duration_ms[plant], onTick);
            state = nextState(plant, DEACTIVATE_PLANT_WATERING_1, DEACTIVATE_PLANT_WATERING_2, DEACTIVATE_PLANT_WATERING_3);
          }
          else
          {
            ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
            state = nextState(plant, ACTIVATE_PLANT_SENSOR_2, ACTIVATE_PLANT_SENSOR_3, ACTIVATE_LIGHTSENSOR);
          }
        }
        else
        {
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = nextState(plant, ACTIVATE_PLANT_SENSOR_2, ACTIVATE_PLANT_SENSOR_3, ACTIVATE_LIGHTSENSOR);
        }

        break;
      }

    case DEACTIVATE_PLANT_WATERING_1:
    case DEACTIVATE_PLANT_WATERING_2:
    case DEACTIVATE_PLANT_WATERING_3:
      {
        byte plant = whichOf(state, DEACTIVATE_PLANT_WATERING_1, DEACTIVATE_PLANT_WATERING_2, DEACTIVATE_PLANT_WATERING_3);
        digitalWrite(O_PLANT_WATERING_PINS[plant], LOW);
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = nextState(plant, ACTIVATE_PLANT_SENSOR_2, ACTIVATE_PLANT_SENSOR_3, ACTIVATE_LIGHTSENSOR);
        break;
      }

      case ACTIVATE_LIGHTSENSOR:
      {
        selectAnalogAddr(ANALOG_LIGHT_SENSOR_ADDR);
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = READ_LIGHTSENSOR;
        break;
      }
      
      case READ_LIGHTSENSOR:
      {
        int value = max(0, min(static_cast<int>(MAX_ANALOG_VALUE), analogRead(A0)));
        lightsensor_value[CURRENT] = 100.0f - 100.0f*static_cast<float>(value)/static_cast<float>(MAX_ANALOG_VALUE);
        LOG_DEBUG((String("onTick - light sensor. Read value ")+String((int)lightsensor_value[CURRENT])).c_str());
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = READ_TEMPHUMIDSENSOR;
        break;
      }

    case READ_TEMPHUMIDSENSOR:
      {
        //DHTxx cannot be read in interrupt, so this state is unused (reading is done in main loop)
        should_read_temp_sensor = true;
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = FINISHED;
        break;
      }

    case FINISHED:
      {
        selectAnalogAddr(0);
        ticker.attach_ms(g_settings.conf_sec_between_reading*1000, onTick);
        state = START;
        break;
      }
    
    default:
      {
        LOG_ERROR("Unknown state");
      }
  }
}

void updateValue(const String& topic, float new_value, volatile float& old_value)
{
  if (new_value != old_value)
  {
    g_mqtt.publishMQTTValue(topic, String(new_value));
    old_value = new_value;
  }
}

void updateValue(const String& topic, uint16_t new_value, volatile uint16_t& old_value)
{
  if (new_value != old_value)
  {
    g_mqtt.publishMQTTValue(topic, String(new_value));
    old_value = new_value;
  }
}

void setup()
{
  Serial.end();
  Serial1.end();
  delay(1000);

#if (LOG_LEVEL>LOG_LEVEL_NONE)
  g_log.enable();
  LOG_INFO("start setup");
#endif // (LOG_LEVEL>LOG_LEVEL_NONE)

  g_settings.enable();
  
  pinMode(I_SETUP_MODE_PIN, INPUT);
  delay(100);

  if (LOW == digitalRead(I_SETUP_MODE_PIN))
  {
    LOG_INFO("Mode = SETUP");

    state = SETUP_MODE;
    g_settings.activateSetupAP();
  }
  else
  {
    LOG_INFO("Mode = NORMAL");

    pinMode(I_TEMPHUMIDSENSOR_PIN, OUTPUT); //DHT handles this pin itself, but it should be OUTPUT before setup
    dht.setup(I_TEMPHUMIDSENSOR_PIN, DHT_MODEL);

    //Prepare pins
    byte i;
    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      pinMode(O_PLANT_WATERING_PINS[i], OUTPUT);
    }
    pinMode(O_FAN_RELAY_ACTIVATE_PIN, OUTPUT);
    pinMode(O_LIGHT_RELAY_ACTIVATE_PIN, OUTPUT);
    pinMode(O_ANALOG_ADDR_S0, OUTPUT);
    pinMode(O_ANALOG_ADDR_S1, OUTPUT);
    pinMode(A0, INPUT);

    //Set output pins
    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      digitalWrite(O_PLANT_WATERING_PINS[i], LOW);
    }
    digitalWrite(O_FAN_RELAY_ACTIVATE_PIN, LOW);
    digitalWrite(O_LIGHT_RELAY_ACTIVATE_PIN, LOW);
    selectAnalogAddr(0);

    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      plant_sensor_value[CURRENT][i] = plant_sensor_value[OLD][i] = 0.0f;
      plant_watering_count[CURRENT][i] = plant_watering_count[OLD][i] = 0;
      previous_plant_watering_time[i] = 0;
      plant_water_requested[i] = false;
      plant_in_watering_cycle[i] = false;
    }
    fan_value[CURRENT] = fan_value[OLD] = 0;
    lightsensor_value[CURRENT] = lightsensor_value[OLD] = 0.0f;
    tempsensor_value[CURRENT] = tempsensor_value[OLD] = 0.0f;
    humiditysensor_value[CURRENT] = humiditysensor_value[OLD] = 0.0f;
    should_read_temp_sensor = false;

    if (!g_settings.activateWifi())
    {
      // reset?
    }

    //Make some noise to say we're alive!
    for (i=0; i<4; i++)
    {
      delay(200);
      digitalWrite(O_LIGHT_RELAY_ACTIVATE_PIN, HIGH);
      delay(200);
      digitalWrite(O_LIGHT_RELAY_ACTIVATE_PIN, LOW);
    }
    growlight_lit = false;
    
    state = START;
    LOG_DEBUG("state=START");
    onTick();
  }
}

void loop()
{
  g_settings.processNetwork(state == SETUP_MODE);

  if (state == SETUP_MODE) {
    delay(100);
  }
  else
  {
    delay(max(1000, dht.getMinimumSamplingPeriod()));
    if (should_read_temp_sensor)
    {
      TempAndHumidity temp_and_humidity = dht.getTempAndHumidity();
      if (dht.getStatus() == DHTesp::ERROR_NONE)
      {
        should_read_temp_sensor = false;
        tempsensor_value[CURRENT] = temp_and_humidity.temperature;
        humiditysensor_value[CURRENT] = temp_and_humidity.humidity;

        updateValue(F("temp"), tempsensor_value[CURRENT], tempsensor_value[OLD]);
        updateValue(F("humidity"), humiditysensor_value[CURRENT], humiditysensor_value[OLD]);

        fan_value[CURRENT] = ((tempsensor_value[CURRENT]>=g_settings.conf_fan_activate_temp_value) || (humiditysensor_value[CURRENT]>=g_settings.conf_fan_activate_humid_value)) ? 1 : 0;
        if (fan_value[CURRENT] != fan_value[OLD])
        {
          digitalWrite(O_FAN_RELAY_ACTIVATE_PIN, fan_value[CURRENT]==0 ? LOW : HIGH);
          updateValue(F("fan_on"), fan_value[CURRENT], fan_value[OLD]);
        }
      }
      else
      {
        LOG_ERROR((String("reading temp and humidity failed with error ")+String((int)dht.getStatus())).c_str());
      }

      updateValue(F("light"), lightsensor_value[CURRENT], lightsensor_value[OLD]);
    }
  
    byte plant;
    for (plant=0; plant<MAX_PLANT_COUNT; plant++)
    {
      if (!g_settings.conf_plant_enabled[plant])
        continue;
        
      String plant_path = F("plant");
      plant_path += String(plant);
  
      updateValue(plant_path+F("/moisture"), plant_sensor_value[CURRENT][plant], plant_sensor_value[OLD][plant]);
  
      updateValue(plant_path+F("/watering_count"), plant_watering_count[CURRENT][plant], plant_watering_count[OLD][plant]);
      plant_watering_count[CURRENT][plant] = plant_watering_count[OLD][plant] = 0;
    }

    if (g_ntp.getLocalTime(local_time)) {
      short current_minute = hour(local_time)*60 + minute(local_time);
      short turn_on = 12*60 - g_settings.conf_growlight_minutes_pr_day/2;
      short turn_off = 12*60 + g_settings.conf_growlight_minutes_pr_day/2;
      if (growlight_lit && (current_minute<turn_on || current_minute>=turn_off)) {
        digitalWrite(O_LIGHT_RELAY_ACTIVATE_PIN, LOW);
        growlight_lit = false;
        LOG_INFO("Shutting off growlight");
      } else if (!growlight_lit && (current_minute>=turn_on && current_minute<turn_off)) {
        digitalWrite(O_LIGHT_RELAY_ACTIVATE_PIN, HIGH);
        growlight_lit = true;
        LOG_INFO("Turning on growlight");
      }
    } else {
      LOG_INFO("Time is unknown");
    }
  }
}
