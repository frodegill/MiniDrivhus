#include <DHTesp.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <Ticker.h>

// PCB v2

static const char* SETUP_SSID = "sensor-setup";
static const byte EEPROM_INITIALIZED_MARKER = 0xF1; //Just a magic number

static const byte SETUP_MODE_PIN           = D9;
static const byte PLANT_SENSOR_MODE_PIN    = D8;
static const byte PLANT_VALVE_MODE_PIN     = D4;
static const byte PLANT_SELECT_PINS[]      = {D3, D6, D7, D5};
static const byte LIGHTSENSOR_ACTIVATE_PIN = D10;
static const byte TEMPHUMIDSENSOR_PIN      = D2;
static const byte ANALOG_SENSOR_PIN        = A0;

static const byte PLANT_COUNT = sizeof(PLANT_SELECT_PINS)/sizeof(PLANT_SELECT_PINS[0]);

static const byte DELAY_BETWEEN_ACTIVE_SENSORS = 25;     //ms between activating sensors
static const byte DEFAULT_WATER_VALVE_DURATION        = 5*1000; //ms to keep valve open
static const byte DEFAULT_WATER_VALVE_IDLE_DELAY_TIME = 20*60*1000; //ms min. time between pump trigger
static const byte SENSOR_DELAY_TIME            = 5*1000; //ms min. time between sensor activation


enum State {
  SETUP_MODE,
  START,
  ACTIVATE_PLANT_SENSOR_MODE,
  ACTIVATE_FIRST_PLANT_SENSOR,
  ACTIVATE_LAST_PLANT_SENSOR = ACTIVATE_FIRST_PLANT_SENSOR + (PLANT_COUNT-1),
  READ_FIRST_PLANT_SENSOR,
  READ_LAST_PLANT_SENSOR = READ_FIRST_PLANT_SENSOR + (PLANT_COUNT-1),
  DEACTIVATE_PLANT_SENSOR_MODE,

  ACTIVATE_PLANT_VALVE_MODE,
  ACTIVATE_FIRST_PLANT_VALVE,
  ACTIVATE_LAST_PLANT_VALVE = ACTIVATE_FIRST_PLANT_VALVE + (PLANT_COUNT-1),
  DEACTIVATE_FIRST_PLANT_VALVE,
  DEACTIVATE_LAST_PLANT_VALVE = DEACTIVATE_FIRST_PLANT_VALVE + (PLANT_COUNT-1),
  DEACTIVATE_PLANT_VALVE_MODE,

  ACTIVATE_LIGHTSENSOR,
  READ_LIGHTSENSOR,
  ACTIVATE_TEMPHUMIDSENSOR,
  READ_TEMPHUMIDSENSOR, //DHT11 cannot be read in interrupt, so this state is unused (reading is done in main loop)
  FINISHED
};


DHTesp dht;
ESP8266WebServer server(80);

WiFiClient esp_client;
PubSubClient mqtt_client(esp_client);

DNSServer dnsServer;
static const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

Ticker ticker;

volatile State state;
volatile bool should_read_temp_sensor;

volatile uint16_t plant_sensor_value[PLANT_COUNT];
volatile uint16_t plant_valve_open_count[PLANT_COUNT];
volatile float lightsensor_value;
float tempsensor_value;
float humiditysensor_value;

#define MAX_SSID_LENGTH            (32)
#define MAX_PASSWORD_LENGTH        (64)
#define MAX_MQTT_SERVERNAME_LENGTH (64)
#define MAX_MQTT_SENSORID_LENGTH    (8)
#define MAX_MQTT_USERNAME_LENGTH   (32)
#define MAX_MQTT_PASSWORD_LENGTH   (32)

char ssid_param[MAX_SSID_LENGTH+1];
char password_param[MAX_PASSWORD_LENGTH+1];
char mqtt_servername_param[MAX_MQTT_SERVERNAME_LENGTH+1];
char mqtt_sensorid_param[MAX_MQTT_SENSORID_LENGTH+1];
char mqtt_username_param[MAX_MQTT_USERNAME_LENGTH+1];
char mqtt_password_param[MAX_MQTT_PASSWORD_LENGTH+1];
boolean mqtt_enabled;


void onTick()
{
  if (state>=ACTIVATE_FIRST_PLANT_SENSOR && state<=ACTIVATE_LAST_PLANT_SENSOR)
  {
    byte plant = state-ACTIVATE_FIRST_PLANT_SENSOR;
    digitalWrite(PLANT_SELECT_PINS[plant], HIGH);
    ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    state = static_cast<State>(READ_FIRST_PLANT_SENSOR+plant);
  }
  else if (state>=READ_FIRST_PLANT_SENSOR && state<=READ_LAST_PLANT_SENSOR)
  {
    byte plant = state-READ_FIRST_PLANT_SENSOR;
    plant_sensor_value[plant] = max(0, min(1023, analogRead(ANALOG_SENSOR_PIN)));
    digitalWrite(PLANT_SELECT_PINS[plant], LOW);
    ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    state = (plant+1)<PLANT_COUNT ? static_cast<State>(ACTIVATE_FIRST_PLANT_SENSOR+plant+1) : DEACTIVATE_PLANT_SENSOR_MODE;
  }
  else if (state>=ACTIVATE_FIRST_PLANT_VALVE && state<=ACTIVATE_LAST_PLANT_VALVE)
  {
    byte plant = state-ACTIVATE_FIRST_PLANT_VALVE;
    if (plant_sensor_value[plant] < 500) //TODO
    {
      digitalWrite(PLANT_SELECT_PINS[plant], HIGH);
      plant_valve_open_count[plant]++;
      ticker.attach_ms(DEFAULT_WATER_VALVE_DURATION, onTick);
    }
    else
    {
      ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    }
    state = static_cast<State>(DEACTIVATE_FIRST_PLANT_VALVE+plant);
  }
  else if (state>=DEACTIVATE_FIRST_PLANT_VALVE && state<=DEACTIVATE_LAST_PLANT_VALVE)
  {
    byte plant = state-DEACTIVATE_FIRST_PLANT_VALVE;
    digitalWrite(PLANT_SELECT_PINS[plant], LOW);
    ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    state = (plant+1)<PLANT_COUNT ? static_cast<State>(ACTIVATE_FIRST_PLANT_VALVE+plant+1) : DEACTIVATE_PLANT_VALVE_MODE;
  }
  else
  {
    switch(state)
    {
      case START:
        {
          digitalWrite(PLANT_SENSOR_MODE_PIN, LOW);
          digitalWrite(PLANT_VALVE_MODE_PIN, LOW);
          byte i;
          for (i=0; i<PLANT_COUNT; i++)
          {
            digitalWrite(PLANT_SELECT_PINS[i], LOW);
          }
          digitalWrite(LIGHTSENSOR_ACTIVATE_PIN, LOW);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_PLANT_SENSOR_MODE;
          break;
        }
  
      case ACTIVATE_PLANT_SENSOR_MODE:
        {
          digitalWrite(PLANT_SENSOR_MODE_PIN, HIGH);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_FIRST_PLANT_SENSOR;
          break;
        }
  
      case DEACTIVATE_PLANT_SENSOR_MODE:
        {
          digitalWrite(PLANT_SENSOR_MODE_PIN, LOW);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_PLANT_VALVE_MODE;
          break;
        }
  
      case ACTIVATE_PLANT_VALVE_MODE:
        {
          digitalWrite(PLANT_VALVE_MODE_PIN, HIGH);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_FIRST_PLANT_VALVE;
          break;
        }
  
      case DEACTIVATE_PLANT_VALVE_MODE:
        {
          digitalWrite(PLANT_VALVE_MODE_PIN, LOW);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_LIGHTSENSOR;
          break;
        }
  
      case ACTIVATE_LIGHTSENSOR:
        {
          digitalWrite(LIGHTSENSOR_ACTIVATE_PIN, HIGH);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = READ_LIGHTSENSOR;
          break;
        }
  
      case READ_LIGHTSENSOR:
        {
          int value = max(0, min(1023, analogRead(ANALOG_SENSOR_PIN)));
          digitalWrite(LIGHTSENSOR_ACTIVATE_PIN, LOW);
          lightsensor_value = 100.0 - 100.0*value/1023.0;
  
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_TEMPHUMIDSENSOR;
          break;
        }

      case ACTIVATE_TEMPHUMIDSENSOR:
        {
          should_read_temp_sensor = true;
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = READ_TEMPHUMIDSENSOR;
          break;
        }
      case READ_TEMPHUMIDSENSOR:
        {
          //DHT11 cannot be read in interrupt, so this state is unused (reading is done in main loop)
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = FINISHED;
          break;
        }
      case FINISHED:
        {
          ticker.attach_ms(SENSOR_DELAY_TIME, onTick);
          state = START;
          break;
        }
    }
  }
}

void readPersistentString(char* s, int max_length, int& adr)
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

void readPersistentParams()
{
  int adr = 0;
  if (EEPROM_INITIALIZED_MARKER != EEPROM.read(adr++))
  {
    ssid_param[0] = 0;
    password_param[0] = 0;
    mqtt_servername_param[0] = 0;
    mqtt_sensorid_param[0] = 0;
    mqtt_username_param[0] = 0;
    mqtt_password_param[0] = 0;
  } else {
    readPersistentString(ssid_param, MAX_SSID_LENGTH, adr);
    readPersistentString(password_param, MAX_PASSWORD_LENGTH, adr);
    readPersistentString(mqtt_servername_param, MAX_MQTT_SERVERNAME_LENGTH, adr);
    readPersistentString(mqtt_sensorid_param, MAX_MQTT_SENSORID_LENGTH, adr);
    readPersistentString(mqtt_username_param, MAX_MQTT_USERNAME_LENGTH, adr);
    readPersistentString(mqtt_password_param, MAX_MQTT_PASSWORD_LENGTH, adr);
  }
}

void writePersistentString(const char* s, size_t max_length, int& adr)
{
  for (int i=0; i<min(strlen(s), max_length); i++)
  {
    EEPROM.write(adr++, s[i]);
  }
  EEPROM.write(adr++, 0);
}

void writePersistentParams()
{
  int adr = 0;
  EEPROM.write(adr++, EEPROM_INITIALIZED_MARKER);
  writePersistentString(ssid_param, MAX_SSID_LENGTH, adr);
  writePersistentString(password_param, MAX_PASSWORD_LENGTH, adr);
  writePersistentString(mqtt_servername_param, MAX_MQTT_SERVERNAME_LENGTH, adr);
  writePersistentString(mqtt_sensorid_param, MAX_MQTT_SENSORID_LENGTH, adr);
  writePersistentString(mqtt_username_param, MAX_MQTT_USERNAME_LENGTH, adr);
  writePersistentString(mqtt_password_param, MAX_MQTT_PASSWORD_LENGTH, adr);
  EEPROM.commit();
}

void handleNotFound() {
  server.send(404, F("text/plain"), F("Page Not Found\n"));
}

void handleSetupRoot() {
  if (server.hasArg("ssid") || server.hasArg("password")
      || server.hasArg("mqtt_server") || server.hasArg("mqtt_id") || server.hasArg("mqtt_username") || server.hasArg("mqtt_password"))
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
    
    if (server.hasArg("mqtt_server"))
    {
      strncpy(mqtt_servername_param, server.arg("mqtt_server").c_str(), MAX_MQTT_SERVERNAME_LENGTH);
      mqtt_servername_param[MAX_MQTT_SERVERNAME_LENGTH] = 0;
    }
    if (server.hasArg("mqtt_id"))
    {
      strncpy(mqtt_sensorid_param, server.arg("mqtt_id").c_str(), MAX_MQTT_SENSORID_LENGTH);
      mqtt_sensorid_param[MAX_MQTT_SENSORID_LENGTH] = 0;
    }
    if (server.hasArg("mqtt_username"))
    {
      strncpy(mqtt_username_param, server.arg("mqtt_username").c_str(), MAX_MQTT_USERNAME_LENGTH);
      mqtt_username_param[MAX_MQTT_USERNAME_LENGTH] = 0;
    }
    if (server.hasArg("mqtt_password") && !server.arg("mqtt_password").equals(F("mqtt_password")))
    {
      strncpy(mqtt_password_param, server.arg("mqtt_password").c_str(), MAX_MQTT_PASSWORD_LENGTH);
      mqtt_password_param[MAX_MQTT_PASSWORD_LENGTH] = 0;
    }

    writePersistentParams();

    server.send(200, F("text/plain"), F("Settings saved"));
  }
  else
  {
    readPersistentParams();

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
    body += F("\" value=\"password\"/><br/><hr/>"\
              "MQTT server:<input type=\"text\" name=\"mqtt_server\" maxlength=\"");
    body += String(MAX_MQTT_SERVERNAME_LENGTH);
    body += F("\" value=\"");
    body += mqtt_servername_param;
    body += F("\"/><br/>"\
              "MQTT sensor id:<input type=\"text\" name=\"mqtt_id\" maxlength=\"");
    body += String(MAX_MQTT_SENSORID_LENGTH);
    body += F("\" value=\"");
    body += mqtt_sensorid_param;
    body += F("\"/><br/>"\
              "MQTT username:<input type=\"text\" name=\"mqtt_username\" maxlength=\"");
    body += String(MAX_MQTT_USERNAME_LENGTH);
    body += F("\" value=\"");
    body += mqtt_username_param;
    body += F("\"/><br/>"\
              "MQTT password:<input type=\"password\" name=\"mqtt_password\" maxlength=\"");
    body += String(MAX_MQTT_PASSWORD_LENGTH);
    body += F("\" value=\"password\"/><br/>"\
              "<input type=\"submit\" value=\"Submit\"/>"\
              "</form>"\
             "</body>"\
             "</html>");
    server.send(200, F("text/html"), body);
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
}

void publishMQTTValue(const String& topic, const String& msg)
{
  if (mqtt_enabled)
  {
    mqtt_client.publish((String(mqtt_sensorid_param)+F("/")+topic).c_str(), msg.c_str());
  }
}

void publishMQTTValue(const String& topic, float value)
{
  publishMQTTValue(topic, String(value));
}

void reconnectMQTT() {
  while (mqtt_enabled && !mqtt_client.connected()) {
    if (!mqtt_client.connect("ESP8266Client", mqtt_username_param, mqtt_password_param)) {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
    else if (mqtt_sensorid_param && *mqtt_sensorid_param)
    {
//      mqtt_client.subscribe((String(mqtt_sensorid_param)+F("/Heater")).c_str());
//      mqtt_client.subscribe((String(mqtt_sensorid_param)+F("/Fan")).c_str());
    }
  }
}

void setup()
{
  EEPROM.begin(1 + MAX_SSID_LENGTH+1 + MAX_PASSWORD_LENGTH+1 + MAX_MQTT_SERVERNAME_LENGTH+1 + MAX_MQTT_SENSORID_LENGTH+1 + MAX_MQTT_USERNAME_LENGTH+1 + MAX_MQTT_PASSWORD_LENGTH+1);

  dht.setup(TEMPHUMIDSENSOR_PIN, DHTesp::DHT11);

  byte i;
  //Prepare pins
  pinMode(SETUP_MODE_PIN, INPUT_PULLUP);
  pinMode(PLANT_SENSOR_MODE_PIN, OUTPUT);
  pinMode(PLANT_VALVE_MODE_PIN, OUTPUT);
  for (i=0; i<PLANT_COUNT; i++)
  {
    pinMode(PLANT_SELECT_PINS[i], OUTPUT);
  }
  pinMode(LIGHTSENSOR_ACTIVATE_PIN, OUTPUT);
  pinMode(TEMPHUMIDSENSOR_PIN, OUTPUT);
  pinMode(ANALOG_SENSOR_PIN, INPUT);

  //Set output pins
  digitalWrite(PLANT_SENSOR_MODE_PIN, LOW);
  digitalWrite(PLANT_VALVE_MODE_PIN, LOW);
  for (i=0; i<PLANT_COUNT; i++)
  {
    digitalWrite(PLANT_SELECT_PINS[i], LOW);
  }
  digitalWrite(LIGHTSENSOR_ACTIVATE_PIN, LOW);
  digitalWrite(TEMPHUMIDSENSOR_PIN, LOW);


  if (LOW == digitalRead(SETUP_MODE_PIN))
  {
    mqtt_enabled = false;
    state = SETUP_MODE;
    WiFi.softAP(SETUP_SSID);
    dnsServer.start(DNS_PORT, "*", apIP);

    server.on("/", handleSetupRoot);
    server.begin();
  }
  else
  {
    readPersistentParams();
    mqtt_enabled = mqtt_servername_param && *mqtt_servername_param;
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_param, password_param);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
  
    server.onNotFound(handleNotFound);
    server.begin();
  
    if (mqtt_enabled)
    {
      mqtt_client.setServer(mqtt_servername_param, 1883);
      mqtt_client.setCallback(mqttCallback);
      reconnectMQTT();
    }

    byte i;
    for (i=0; i<PLANT_COUNT; i++)
    {
      plant_sensor_value[i] = plant_valve_open_count[i] = 0;
    }
    lightsensor_value = 0.0;
    tempsensor_value = 0.0;
    humiditysensor_value = 0.0;
    should_read_temp_sensor = false;
    state = START;
    onTick();
  }
}

void loop()
{
  if (state == SETUP_MODE) {
      dnsServer.processNextRequest();
  }
  else
  {
    if (mqtt_enabled)
    {
      if (!mqtt_client.connected()) {
        reconnectMQTT();
      }
      mqtt_client.loop();
    }
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
