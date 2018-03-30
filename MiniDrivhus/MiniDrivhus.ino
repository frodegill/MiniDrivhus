#include <DHTesp.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>

static const char* SSID = "gill-roxrud";
static const char* PASSWORD = "******";

#define SETUP_MODE_PIN              (D3)
#define DHT11_PIN                   (D2)
#define WATERSENSOR_PIN             (A0)
#define WATERSENSOR_ACTIVATE_PIN    (D4)
#define LIGHTSENSOR_PIN             (A0)
#define LIGHTSENSOR_ACTIVATE_PIN    (D6)
#define PUMP_PIN                    (D5)

#define PUMP_TRIGGER_VALUE          (1023*0.35) //Value on WATER_SENSOR_PIN to trigger start of motor

#define LIGHTSENSOR_ACTIVATION_TIME (25)      //ms to activate sensor
#define DELAY_BETWEEN_ACTIVE_SENSORS (25)   //ms between activating sensor
#define WATERSENSOR_ACTIVATION_TIME (25)      //ms to activate sensor
#define PUMP_DURATION               (5*1000)  //ms to run pump
#define PUMP_IDLE_DELAY_TIME        (20*60*1000) //ms min. time between pump trigger
#define SENSOR_DELAY_TIME           (5*1000)  //ms min. time between sensor activation

enum State {
  SETUP_MODE,
  ACTIVATE_LIGHTSENSOR,
  READ_LIGHTSENSOR,
  ACTIVATE_WATERSENSOR,
  READ_WATERSENSOR,
  STOP_PUMP
};

DHTesp dht;
ESP8266WebServer server(80);
Ticker ticker;

volatile State state;
volatile float lightsensor_value;
volatile float watersensor_value;
volatile int pump_start_count;
volatile bool should_read_temp_sensor;

float tempsensor_value;
float humiditysensor_value;


void onTick()
{
  switch(state)
  {
    case ACTIVATE_LIGHTSENSOR:
      {
        digitalWrite(LIGHTSENSOR_ACTIVATE_PIN, HIGH);
        ticker.attach_ms(LIGHTSENSOR_ACTIVATION_TIME, onTick);
        state = READ_LIGHTSENSOR;
        break;
      }
    case READ_LIGHTSENSOR:
      {
        int value = max(0, min(1023, analogRead(LIGHTSENSOR_PIN)));
        digitalWrite(LIGHTSENSOR_ACTIVATE_PIN, LOW);
        lightsensor_value = 100.0 - 100.0*value/1023.0;

        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = ACTIVATE_WATERSENSOR;
        break;
      }
    case ACTIVATE_WATERSENSOR:
      {
        digitalWrite(WATERSENSOR_ACTIVATE_PIN, HIGH);
        ticker.attach_ms(WATERSENSOR_ACTIVATION_TIME, onTick);
        state = READ_WATERSENSOR;
        break;
      }
    case READ_WATERSENSOR:
      {
        int value = max(0, min(1023, analogRead(WATERSENSOR_PIN)));
        digitalWrite(WATERSENSOR_ACTIVATE_PIN, LOW);
        watersensor_value = 100.0 - 100.0*value/1023.0;

        if (value >= PUMP_TRIGGER_VALUE)
        {
          pump_start_count++;
          digitalWrite(PUMP_PIN, HIGH);
          ticker.attach_ms(PUMP_DURATION, onTick);
          state = STOP_PUMP;
        }
        else
        {
          ticker.attach_ms(SENSOR_DELAY_TIME, onTick);
          state = ACTIVATE_LIGHTSENSOR;
        }

        should_read_temp_sensor = true;
        break;
      }
    case STOP_PUMP:
      {
        digitalWrite(PUMP_PIN, LOW);
        ticker.attach_ms(PUMP_IDLE_DELAY_TIME, onTick);
        state = ACTIVATE_LIGHTSENSOR;
        break;
      }
  }
}

void handleCountConfig() {
  server.send(200, F("text/plain"), F("graph_title MiniDrivhus\n"\
                                      "graph_args --base 1000 -l 0\n"\
                                      "graph_vlabel Count\n"\
                                      "graph_category homeautomation\n"\
                                      "graph_info Number of times the MiniDrivhus water pump has started.\n"\
                                      "PUMP.label MiniDrivhus\n"\
                                      "PUMP.draw LINE2\n"\
                                      "PUMP.info count\n"));
}

void handleCountValues() {
  char msg[20];
  snprintf(msg, sizeof(msg)/sizeof(msg[0]), "PUMP.value %d\n", pump_start_count);
  pump_start_count = 0;
  server.send(200, F("text/plain"), msg);
}

void handleSensorsConfig() {
  server.send(200, F("text/plain"), F("graph_title MiniDrivhus\n"\
                                      "graph_args --base 1000 -l 0\n"\
                                      "graph_vlabel Value\n"\
                                      "graph_category homeautomation\n"\
                                      "graph_info MiniDrivhus sensor values.\n"\
                                      "WATER.label Water\n"\
                                      "WATER.draw LINE2\n"\
                                      "WATER.info value\n"\
                                      "LIGHT.label Light\n"\
                                      "LIGHT.draw LINE2\n"\
                                      "LIGHT.info value\n"\
                                      "TEMP.label Temperature\n"\
                                      "TEMP.draw LINE2\n"\
                                      "TEMP.info C\n"\
                                      "HUMIDITY.label Humidity\n"\
                                      "HUMIDITY.draw LINE2\n"\
                                      "HUMIDITY.info RH\n"));
}

void handleSensorsValues() {
  char msg[80];
  snprintf(msg, sizeof(msg)/sizeof(msg[0]), "WATER.value %0.2f\n"\
                                            "LIGHT.value %0.2f\n"\
                                            "TEMP.value %0.2f\n"\
                                            "HUMIDITY.value %0.2f\n", watersensor_value, lightsensor_value, tempsensor_value, humiditysensor_value);
  server.send(200, F("text/plain"), msg);
}

void handleNotFound() {
  server.send(404, F("text/plain"), F("Page Not Found\n"));
}

void setup()
{
  Serial.begin(115200);

  dht.setup(DHT11_PIN, DHTesp::DHT11);

  pinMode(SETUP_MODE_PIN, INPUT_PULLUP);
  pinMode(WATERSENSOR_PIN, INPUT);
  pinMode(WATERSENSOR_ACTIVATE_PIN, OUTPUT);
  pinMode(LIGHTSENSOR_PIN, INPUT);
  pinMode(LIGHTSENSOR_ACTIVATE_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);

  digitalWrite(WATERSENSOR_ACTIVATE_PIN, LOW);
  digitalWrite(LIGHTSENSOR_ACTIVATE_PIN, LOW);
  digitalWrite(PUMP_PIN, LOW);

  if (LOW == digitalRead(SETUP_MODE_PIN))
  {
    state = SETUP_MODE;
    WiFi.mode(WIFI_AP);
  }
  else
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(SSID, PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
  
    server.on(F("/count/config"), handleCountConfig);
    server.on(F("/count/values"), handleCountValues);
    server.on(F("/sensors/config"), handleSensorsConfig);
    server.on(F("/sensors/values"), handleSensorsValues);
    server.onNotFound(handleNotFound);
    server.begin();
  
    state = ACTIVATE_LIGHTSENSOR;
    watersensor_value = 0.0;
    lightsensor_value = 0.0;
    tempsensor_value = 0.0;
    humiditysensor_value = 0.0;
    pump_start_count = 0;
    should_read_temp_sensor = false;
    onTick();
  }
}

void loop()
{
  server.handleClient();

  delay(dht.getMinimumSamplingPeriod());
  if (should_read_temp_sensor)
  {
    should_read_temp_sensor = false;
    TempAndHumidity temp_and_humidity = dht.getTempAndHumidity();
    if (dht.getStatus() == DHTesp::ERROR_NONE)
    {
      tempsensor_value = temp_and_humidity.temperature;
      humiditysensor_value = temp_and_humidity.humidity;
    }
  }
}
