#include <DHTesp.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Ticker.h>

static const char* SETUP_SSID = "sensor-setup";
static const byte EEPROM_INITIALIZED_MARKER = 0xF1; //Just a magic number


#define SETUP_MODE_PIN              (D7)
#define DHT11_PIN                   (D2)
#define WATERSENSOR_PIN             (A0)
#define WATERSENSOR_ACTIVATE_PIN    (D4)
#define LIGHTSENSOR_PIN             (A0)
#define LIGHTSENSOR_ACTIVATE_PIN    (D6)
#define PUMP_PIN                    (D5)

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

DNSServer dnsServer;
static const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

volatile State state;
volatile float lightsensor_value;
volatile float watersensor_value;
volatile int pump_start_count;
volatile bool should_read_temp_sensor;

float tempsensor_value;
float humiditysensor_value;

#define MAX_SSID_LENGTH        (32)
#define MAX_PASSWORD_LENGTH    (64)
#define MAX_TRIGGER_LENGTH     (5)
#define TRIGGER_PARAM_DECIMALS (2)

char ssid_param[MAX_SSID_LENGTH+1];
char password_param[MAX_PASSWORD_LENGTH+1];
float pump_trigger_param; //100.0 - 100.0*WATER_SENSOR_PIN/1023.0. Trigger start of motor



float readAndDeactivateWaterSensor()
{
  int value = max(0, min(1023, analogRead(WATERSENSOR_PIN)));
  digitalWrite(WATERSENSOR_ACTIVATE_PIN, LOW);
  return 100.0 - 100.0*value/1023.0;
}

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
        watersensor_value = readAndDeactivateWaterSensor();
        if (watersensor_value < pump_trigger_param)
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

void read_persistent_string(char* s, int max_length, int& adr)
{
  int i = 0;
  byte c;
  do {
    c = EEPROM.read(adr++);
    if (i<max_length)
    {
      s[i++] = static_cast<char>(c);
    }
  } while (c!=0);
  s[i] = 0;
}

void read_persistent_float(float& f, int decimals, int& adr)
{
  int i = 0;
  byte c;
  do {
    c = EEPROM.read(adr++);
    if (c>='0' && c<='9')
    {
      i = 10*i + (c-'0');
    }
  } while (c!=0);

  f = i;
  for (i=0; i<decimals; i++)
  {
    f = f/10.0f;
  }
}

void read_persistent_params()
{
  int adr = 0;
  if (EEPROM_INITIALIZED_MARKER != EEPROM.read(adr++))
  {
    ssid_param[0] = 0;
    password_param[0] = 0;
    pump_trigger_param = 0.0;
  } else {
    read_persistent_string(ssid_param, MAX_SSID_LENGTH, adr);
    read_persistent_string(password_param, MAX_PASSWORD_LENGTH, adr);
    read_persistent_float(pump_trigger_param, TRIGGER_PARAM_DECIMALS, adr);
  }
}

void write_persistent_string(const char* s, size_t max_length, int& adr)
{
  for (int i=0; i<min(strlen(s), max_length); i++)
  {
    EEPROM.write(adr++, s[i]);
  }
  EEPROM.write(adr++, 0);
}

void write_persistent_params(const char* ssid, const char* password, float pump_trigger_value)
{
  int adr = 0;
  EEPROM.write(adr++, EEPROM_INITIALIZED_MARKER);
  write_persistent_string(ssid, MAX_SSID_LENGTH, adr);
  write_persistent_string(password, MAX_PASSWORD_LENGTH, adr);

  int i;
  for (i=0; i<TRIGGER_PARAM_DECIMALS; i++)
  {
    pump_trigger_value *= 10.0f;
  }
  char pump_trigger_strvalue[MAX_TRIGGER_LENGTH+1];
  int pump_trigger_intvalue = pump_trigger_value;
  for (i=0; i<MAX_TRIGGER_LENGTH; i++)
  {
    pump_trigger_strvalue[MAX_TRIGGER_LENGTH-i-1] = '0'+(pump_trigger_intvalue%10);
    pump_trigger_intvalue/=10;
  }
  pump_trigger_strvalue[MAX_TRIGGER_LENGTH] = 0;
  write_persistent_string(pump_trigger_strvalue, MAX_TRIGGER_LENGTH, adr);

  EEPROM.commit();
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

void handleSetupRoot() {
  if (server.hasArg("ssid") || server.hasArg("password") || server.hasArg("trigger_value"))
  {
    if (server.hasArg("ssid"))
    {
      strncpy(ssid_param, server.arg("ssid").c_str(), MAX_SSID_LENGTH);
      ssid_param[MAX_SSID_LENGTH] = 0;
    }
    
    if (server.hasArg("password") && !server.arg("password").equals(F("password")))
    {
      strncpy(password_param, server.arg("password").c_str(), MAX_PASSWORD_LENGTH);
      password_param[MAX_PASSWORD_LENGTH] = 0;
    }
    
    if (server.hasArg("trigger_value"))
    {
      pump_trigger_param = server.arg("trigger_value").toFloat();
    }
    
    write_persistent_params(ssid_param, password_param, pump_trigger_param);

    server.send(200, F("text/plain"), F("Settings saved"));
  }
  else
  {
    read_persistent_params();

    digitalWrite(WATERSENSOR_ACTIVATE_PIN, HIGH);
    delay(WATERSENSOR_ACTIVATION_TIME);
    float current_value = readAndDeactivateWaterSensor();
    
    String body = F("<!doctype html>"\
                    "<html lang=\"en\">"\
                    "<head>"\
                     "<meta charset=\"utf-8\">"\
                     "<title>Setup</title>"\
                     "<style>"\
                      "form {margin: 0.5em;}"\
                      "input {margin: 0.2em;}"\
                     "</style>"\
                    "</head>"\
                    "<body>"\
                     "<form method=\"post\">"\
                      "SSID:<input type=\"text\" name=\"ssid\" required maxlength=\"");
    body += String(MAX_SSID_LENGTH);
    body += F("\" autofocus value=\"");
    body += ssid_param;
    body += F("\"/><br/>"\
              "Password:<input type=\"password\" name=\"password\" maxlength=\"");
    body += String(MAX_PASSWORD_LENGTH);
    body += F("\" value=\"password\"/><br/>"\
              "Pump Trigger Value:<input type=\"number\" name=\"trigger_value\" required min=\"0\" max=\"100\" step=\"0.01\" value=\"");
    body += String(pump_trigger_param, TRIGGER_PARAM_DECIMALS);
    body += F("\"/> (Current sensor value: ");
    body += String(current_value, TRIGGER_PARAM_DECIMALS);
    body += F(")<br/>"\
              "<input type=\"submit\" value=\"Submit\"/>"\
              "</form>"\
             "</body>"\
             "</html>");
    server.send(200, F("text/html"), body);
  }
}

void setup()
{
  EEPROM.begin(1 + MAX_SSID_LENGTH + 1 + MAX_PASSWORD_LENGTH + 1 + MAX_TRIGGER_LENGTH + 1);

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
    WiFi.softAP(SETUP_SSID);
    dnsServer.start(DNS_PORT, "*", apIP);

    server.on("/", handleSetupRoot);
    server.begin();
  }
  else
  {
    read_persistent_params();
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_param, password_param);
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
  if (state == SETUP_MODE) {
      dnsServer.processNextRequest();
  }

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
