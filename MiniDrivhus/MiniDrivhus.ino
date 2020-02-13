#include "global.h"

#include "debug.h"
#include "mqtt.h"
#include "settings.h"

#include <DHTesp.h>           // Library: DHT_sensor_library_fror_ESPx
#include <Ticker.h>


/* PCB v4 for WeMos D1 R1

 MQTT paths:
 minidrivhus/<sensorid>/light
 minidrivhus/<sensorid>/temp
 minidrivhus/<sensorid>/humidity
 minidrivhus/<sensorid>/plant[1..n]/moisture
 minidrivhus/<sensorid>/plant[1..n]/watering_count
 minidrivhus/<sensorid>/config/sec_between_reading
 minidrivhus/<sensorid>/config/plant_count
 minidrivhus/<sensorid>/config/plant[1..n]/watering_trigger_value
 minidrivhus/<sensorid>/config/plant[1..n]/watering_duration_ms
 minidrivhus/<sensorid>/config/plant[1..n]/watering_grace_period_sec
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

  ACTIVATE_LIGHTSENSOR,
  READ_LIGHTSENSOR,

  READ_TEMPHUMIDSENSOR, //DHTxx cannot be read in interrupt, so this state is unused (reading is done in main loop)

  FINISHED
};


Debug g_debug;
MQTT g_mqtt;
Settings g_settings;
DHTesp dht;

Ticker ticker;

volatile State state;
volatile bool should_read_temp_sensor;

volatile uint16_t plant_sensor_value[2][MAX_PLANT_COUNT];
volatile uint16_t plant_watering_count[2][MAX_PLANT_COUNT];
volatile long previous_plant_watering_time[MAX_PLANT_COUNT];
volatile float lightsensor_value[2];
float tempsensor_value[2];
float humiditysensor_value[2];



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
        g_debug.print((String("onTick - start, plant_count=")+String((int)g_settings.conf_plant_count)).c_str());
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = ACTIVATE_PLANT_SENSOR_1;
        break;
      }

    case ACTIVATE_PLANT_SENSOR_1:
    case ACTIVATE_PLANT_SENSOR_2:
      {
        byte plant = (state==ACTIVATE_PLANT_SENSOR_1)?0:1;
        selectAnalogAddr(ANALOG_PLANT_SENSOR_ADDR[plant]);
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = (plant==0) ? READ_PLANT_SENSOR_1 : READ_PLANT_SENSOR_2;
        break;
      }

    case READ_PLANT_SENSOR_1:
    case READ_PLANT_SENSOR_2:
      {
        byte plant = (state==READ_PLANT_SENSOR_1)?0:1;
        plant_sensor_value[CURRENT][plant] = max(0, min(static_cast<int>(MAX_ANALOG_VALUE), analogRead(A0)));
        g_debug.print((String("onTick - read sensor ")+String((int)plant)+String(", got ")+String((int)plant_sensor_value[CURRENT][plant])).c_str());
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = (plant==0) ? ACTIVATE_PLANT_WATERING_1 : ACTIVATE_PLANT_WATERING_2;
        break;
      }

    case ACTIVATE_PLANT_WATERING_1:
    case ACTIVATE_PLANT_WATERING_2:
      {
        byte plant = (state==ACTIVATE_PLANT_WATERING_1)?0:1;
        byte sensor_percent_value = (plant_sensor_value[CURRENT][plant]*100)/MAX_ANALOG_VALUE;
        if (sensor_percent_value < g_settings.conf_watering_trigger_value[plant])
        {
          long current_sec = millis()/1000;
          if (current_sec < previous_plant_watering_time[plant]) //Wrapped. Happens every ~50 days
          {
            previous_plant_watering_time[plant] = current_sec;
          }
          
          if (previous_plant_watering_time[plant]+g_settings.conf_watering_grace_period_sec[plant] >= current_sec)
          {
            g_debug.print((String("onTick - skipping activate watering ")+String((int)plant)+String(" (in ")+String((long)(current_sec-previous_plant_watering_time[plant]))+String(" of ")+String((int)g_settings.conf_watering_grace_period_sec[plant])+String(" sec grace period")).c_str());
            ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
            state = (plant==0 && g_settings.conf_plant_count>1) ? ACTIVATE_PLANT_SENSOR_2 : ACTIVATE_LIGHTSENSOR;
          }
          else
          {
            g_debug.print((String("onTick - activate watering ")+String((int)plant)).c_str());
            digitalWrite(O_PLANT_WATERING_PINS[plant], HIGH);
            plant_watering_count[CURRENT][plant]++;
            previous_plant_watering_time[plant] = current_sec;
            ticker.attach_ms(g_settings.conf_watering_duration_ms[plant], onTick);
            state = (plant==0) ? DEACTIVATE_PLANT_WATERING_1 : DEACTIVATE_PLANT_WATERING_2;
          }
        }
        else
        {
          g_debug.print((String("onTick - activate watering ")+String((int)plant)+String(" ignored, ")+String((int)sensor_percent_value)+String(" >= ")+String((int)g_settings.conf_watering_trigger_value[plant])).c_str());
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = (plant==0 && g_settings.conf_plant_count>1) ? ACTIVATE_PLANT_SENSOR_2 : ACTIVATE_LIGHTSENSOR;
        }

        break;
      }

    case DEACTIVATE_PLANT_WATERING_1:
    case DEACTIVATE_PLANT_WATERING_2:
      {
        byte plant = (state==DEACTIVATE_PLANT_WATERING_1)?0:1;
        g_debug.print((String("onTick - deactivate watering ")+String((int)plant)).c_str());
        digitalWrite(O_PLANT_WATERING_PINS[plant], LOW);
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = (plant==0 && g_settings.conf_plant_count>1) ? ACTIVATE_PLANT_SENSOR_2 : ACTIVATE_LIGHTSENSOR;
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
        lightsensor_value[CURRENT] = 100.0 - 100.0*value/MAX_ANALOG_VALUE;
        g_debug.print((String("onTick - deactivate light sensor. Read value ")+String((int)lightsensor_value[CURRENT])).c_str());
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = READ_TEMPHUMIDSENSOR;
        break;
      }

    case READ_TEMPHUMIDSENSOR:
      {
        //DHTxx cannot be read in interrupt, so this state is unused (reading is done in main loop)
        g_debug.print("onTick - request temphumid value");
        should_read_temp_sensor = true;
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = FINISHED;
        break;
      }

    case FINISHED:
      {
        selectAnalogAddr(UNUSED_ANALOG_SENSOR_ADDR);
        g_debug.print("onTick - finished");
        ticker.attach_ms(g_settings.conf_sec_between_reading*1000, onTick);
        state = START;
        break;
      }
    
    default:
      {
        g_debug.print("Unknown state");
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
  g_debug.enable();
  g_settings.enable();
  
  pinMode(I_SETUP_MODE_PIN, INPUT_PULLUP);
  delay(100);

  if (LOW == digitalRead(I_SETUP_MODE_PIN))
  {
    state = SETUP_MODE;
    g_settings.activateSetupAP();
  }
  else
  {
    g_settings.activateWifi();

    pinMode(I_TEMPHUMIDSENSOR_PIN, OUTPUT); //DHT handles this pin itself, but it should be OUTPUT before setup
    dht.setup(I_TEMPHUMIDSENSOR_PIN, DHT_MODEL);

    //Prepare pins
    byte i;
    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      pinMode(O_PLANT_WATERING_PINS[i], OUTPUT);
    }
    pinMode(O_LIGHT_RELAY_ACTIVATE_PIN, OUTPUT);
    pinMode(O_ANALOG_ADDR_S0, OUTPUT);
    pinMode(O_ANALOG_ADDR_S1, OUTPUT);
    pinMode(A0, INPUT);

    //Set output pins
    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      digitalWrite(O_PLANT_WATERING_PINS[i], LOW);
    }
    digitalWrite(O_LIGHT_RELAY_ACTIVATE_PIN, LOW);
    selectAnalogAddr(UNUSED_ANALOG_SENSOR_ADDR);

    g_settings.conf_sec_between_reading = Settings::DEFAULT_CONF_SEC_BETWEEN_READING;

    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      plant_sensor_value[CURRENT][i] = plant_sensor_value[OLD][i] = 0;
      plant_watering_count[CURRENT][i] = plant_watering_count[OLD][i] = 0;
      g_settings.conf_watering_trigger_value[i] = Settings::DEFAULT_CONF_WATERING_TRIGGER_VALUE;
      g_settings.conf_watering_duration_ms[i] = Settings::DEFAULT_CONF_WATERING_DURATION_MS;
      g_settings.conf_watering_grace_period_sec[i] = Settings::DEFAULT_CONF_WATERING_GRACE_PERIOD_SEC;
      previous_plant_watering_time[i] = 0;
    }
    lightsensor_value[CURRENT] = lightsensor_value[OLD] = 0.0;
    tempsensor_value[CURRENT] = tempsensor_value[OLD] = 0.0;
    humiditysensor_value[CURRENT] = humiditysensor_value[OLD] = 0.0;
    should_read_temp_sensor = false;

    state = START;
    g_debug.print("state=START");
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
    g_debug.print("should_read_temp_sensor?");
    if (should_read_temp_sensor)
    {
      TempAndHumidity temp_and_humidity = dht.getTempAndHumidity();
      if (dht.getStatus() == DHTesp::ERROR_NONE)
      {
        should_read_temp_sensor = false;
        tempsensor_value[CURRENT] = temp_and_humidity.temperature;
        humiditysensor_value[CURRENT] = temp_and_humidity.humidity;
        g_debug.print((String("reading temp ")+String((int)tempsensor_value[CURRENT])+String(" and humidity ")+String((int)humiditysensor_value[CURRENT])).c_str());

        updateValue(F("temp"), tempsensor_value[CURRENT], tempsensor_value[OLD]);
        updateValue(F("humidity"), humiditysensor_value[CURRENT], humiditysensor_value[OLD]);
      }
      else
      {
        g_debug.print((String("reading temp and humidity failed with error ")+String((int)dht.getStatus())).c_str());
      }

      updateValue(F("light"), lightsensor_value[CURRENT], lightsensor_value[OLD]);
    }
  }

  byte plant;
  for (plant=0; plant<g_settings.conf_plant_count; plant++)
  {
    String plant_path = F("plant");
    plant_path += String(plant);

    updateValue(plant_path+F("/moisture"), plant_sensor_value[CURRENT][plant], plant_sensor_value[OLD][plant]);

    updateValue(plant_path+F("/watering_count"), plant_watering_count[CURRENT][plant], plant_watering_count[OLD][plant]);
    plant_watering_count[CURRENT][plant] = plant_watering_count[OLD][plant] = 0;
  }
}
