#include "global.h"

# include <TimeLib.h>

#include "steppermotor.h"
#include "mqtt.h"
#include "ntp.h"
#include "settings.h"

/*
 MQTT paths:
 watercup/<sensorid>/valve_open
 */


enum State {
  SETUP_MODE,
  NORMAL
};
volatile State state;

StepperMotor g_steppermotor;
MQTT g_mqtt;
NTP g_ntp;
time_t local_time;
Settings g_settings;

volatile unsigned long failsafeDelay;

volatile bool valve_open;
volatile bool valve_changed_state;


void setup() {
  Serial.begin(9600);

  g_steppermotor.initialize();
  g_settings.enable();
  
  pinMode(I_SETUP_MODE_PIN, INPUT);
  delay(100);

  if (false && (LOW == digitalRead(I_SETUP_MODE_PIN)))
  {
    Serial.println("Setup");
    state = SETUP_MODE;
    g_settings.activateSetupAP();
  }
  else
  {
    Serial.println("Normal");
    state = NORMAL;

    pinMode(O_TRIGGER_PIN, OUTPUT);
    digitalWrite(O_TRIGGER_PIN, LOW);
    failsafeDelay = LONG_DELAY;
    
    pinMode(I_LOW_SENSOR_PIN, INPUT);
    pinMode(I_HIGH_SENSOR_PIN, INPUT);

    attachInterrupt(digitalPinToInterrupt(I_LOW_SENSOR_PIN), updateFlowStatus, CHANGE);
    attachInterrupt(digitalPinToInterrupt(I_HIGH_SENSOR_PIN), updateFlowStatus, CHANGE);

    if (!g_settings.activateWifi())
    {
      // reset?
    }

    valve_open = false;
    valve_changed_state = true;
    
    updateFlowStatus();
  }
}

void loop() {
  g_settings.processNetwork(state == SETUP_MODE);

  if (state == SETUP_MODE) {
    delay(100);
  }
  else
  {
    delay(failsafeDelay);
    updateFlowStatus();

    if (valve_changed_state) // Publishing to MQTT cannot be done in interrupt
    {
      g_mqtt.publishMQTTValue(F("valve_open"), String(valve_open ? "1" : "0"));
      valve_changed_state = false;
    }

    if (g_ntp.getLocalTime(local_time)) {
      short current_minute = hour(local_time)*60 + minute(local_time); // 0 - 1440
    } else {
      // Time is unknown
    }

    g_steppermotor.step();
  }
}

ICACHE_RAM_ATTR void updateFlowStatus()
{
  int low_status = digitalRead(I_LOW_SENSOR_PIN);
  int high_status = digitalRead(I_HIGH_SENSOR_PIN);
  int trigger_status = digitalRead(O_TRIGGER_PIN);
  if (trigger_status==LOW && low_status==LOW && high_status!=HIGH /*fail safe*/)
  {
    digitalWrite(O_TRIGGER_PIN, HIGH);
    failsafeDelay = SHORT_DELAY;
    valve_open = true;
    valve_changed_state = true;
  }
  
  if (trigger_status==HIGH && high_status==HIGH)
  {
    digitalWrite(O_TRIGGER_PIN, LOW);
    failsafeDelay = LONG_DELAY;
    valve_open = false;
    valve_changed_state = true;
  }
}
