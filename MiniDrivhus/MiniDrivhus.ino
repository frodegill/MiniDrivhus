#include <DHTesp.h>
#include <DNSServer.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <Ticker.h>

/* PCB v2 for WeMos D1 R1

 MQTT paths:
 minidrivhus/<sensorid>/light
 minidrivhus/<sensorid>/temp
 minidrivhus/<sensorid>/humidity
 minidrivhus/<sensorid>/plant[1..n]/moisture
 minidrivhus/<sensorid>/plant[1..n]/valve_open_count
 minidrivhus/<sensorid>/config/sec_between_reading
 minidrivhus/<sensorid>/config/plant_count
 minidrivhus/<sensorid>/config/plant[1..n]/valve_trigger_value
 minidrivhus/<sensorid>/config/plant[1..n]/valve_open_ms
 minidrivhus/<sensorid>/config/plant[1..n]/sec_valve_grace_period
 */

static const char* SETUP_SSID = "sensor-setup";
static const byte EEPROM_INITIALIZED_MARKER = 0xF2; //Just a magic number

static const byte BUILTIN_LED_PIN = D9;
static const byte BUILTIN_LED_OFF = HIGH;

#define DHT_MODEL (DHTesp::AM2302)

// D5 = Board LED, D9 = Built in LED (HIGH == ON), D8 & D9 Pull-up, D10 pull-down
static const byte I_SETUP_MODE_PIN           = D0; //Uses internal pull-up
static const byte I_TEMPHUMIDSENSOR_PIN      = D1;
static const byte O_LIGHT_RELAY_ACTIVATE_PIN = D2;
static const byte O_LIGHTSENSOR_ACTIVATE_PIN = D3;
static const byte O_PLANT_VALVE_MODE_PIN     = D4;
static const byte O_PLANT_SENSOR_MODE_PIN    = D5;
static const byte O_PLANT_SELECT_PINS[]      = {D6, D7, D8, D10, BUILTIN_LED_PIN};
static const byte I_ANALOG_SENSOR_PIN        = A0;

static const byte MAX_PLANT_COUNT = sizeof(O_PLANT_SELECT_PINS)/sizeof(O_PLANT_SELECT_PINS[0]);

static const byte DELAY_BETWEEN_ACTIVE_SENSORS     = 25; //ms between activating sensors
static const byte DEFAULT_CONF_SEC_BETWEEN_READING = 5; //secoonds min. time between sensor activation
static const byte DEFAULT_CONF_VALVE_TRIGGER_VALUE = 50; //[0..100]
static const byte DEFAULT_CONF_VALVE_OPEN_MS       = 5*1000; //ms to keep valve open
static const byte DEFAULT_CONF_VALVE_SEC_GRACE_PERIOD = 20*60; //sec min. time between valve open

static const byte OLD = 0;
static const byte CURRENT = 1;

enum State {
  SETUP_MODE,
  START,
  ACTIVATE_PLANT_SENSOR_MODE,
  ACTIVATE_FIRST_PLANT_SENSOR,
  ACTIVATE_LAST_PLANT_SENSOR = ACTIVATE_FIRST_PLANT_SENSOR + (MAX_PLANT_COUNT-1),
  READ_FIRST_PLANT_SENSOR,
  READ_LAST_PLANT_SENSOR = READ_FIRST_PLANT_SENSOR + (MAX_PLANT_COUNT-1),
  DEACTIVATE_PLANT_SENSOR_MODE,

  ACTIVATE_PLANT_VALVE_MODE,
  ACTIVATE_FIRST_PLANT_VALVE,
  ACTIVATE_LAST_PLANT_VALVE = ACTIVATE_FIRST_PLANT_VALVE + (MAX_PLANT_COUNT-1),
  DEACTIVATE_FIRST_PLANT_VALVE,
  DEACTIVATE_LAST_PLANT_VALVE = DEACTIVATE_FIRST_PLANT_VALVE + (MAX_PLANT_COUNT-1),
  DEACTIVATE_PLANT_VALVE_MODE,

  ACTIVATE_LIGHTSENSOR,
  READ_LIGHTSENSOR,
  ACTIVATE_TEMPHUMIDSENSOR,
  READ_TEMPHUMIDSENSOR, //DHTxx cannot be read in interrupt, so this state is unused (reading is done in main loop)
  FINISHED
};


DHTesp dht;
ESP8266WebServer server(80);

WiFiClient esp_client;
PubSubClient mqtt_client(esp_client);
static const uint16_t MQTT_DEFAULT_PORT = 1883;

DNSServer dnsServer;
static const byte DNS_PORT = 53;
IPAddress apIP(192, 168, 4, 1);

Ticker ticker;

volatile State state;
volatile bool should_read_temp_sensor;

volatile uint16_t plant_sensor_value[2][MAX_PLANT_COUNT];
volatile uint16_t plant_valve_open_count[2][MAX_PLANT_COUNT];
volatile float lightsensor_value[2];
float tempsensor_value[2];
float humiditysensor_value[2];

unsigned int conf_sec_between_reading;
byte conf_plant_count = MAX_PLANT_COUNT;
byte conf_valve_trigger_value[MAX_PLANT_COUNT];
unsigned int conf_valve_open_ms[MAX_PLANT_COUNT];
unsigned int conf_valve_sec_grace_period[MAX_PLANT_COUNT];


#define MAX_SSID_LENGTH            (32)
#define MAX_PASSWORD_LENGTH        (64)
#define MAX_MQTT_SERVERNAME_LENGTH (64)
#define MAX_MQTT_SERVERPORT_LENGTH  (5)
#define MAX_MQTT_SENSORID_LENGTH   (16)
#define MAX_MQTT_USERNAME_LENGTH   (32)
#define MAX_MQTT_PASSWORD_LENGTH   (32)

char ssid_param[MAX_SSID_LENGTH+1];
char password_param[MAX_PASSWORD_LENGTH+1];
char mqtt_servername_param[MAX_MQTT_SERVERNAME_LENGTH+1];
uint16_t mqtt_serverport_param = MQTT_DEFAULT_PORT;
char mqtt_sensorid_param[MAX_MQTT_SENSORID_LENGTH+1];
char mqtt_username_param[MAX_MQTT_USERNAME_LENGTH+1];
char mqtt_password_param[MAX_MQTT_PASSWORD_LENGTH+1];
boolean mqtt_enabled;
String mqtt_path_prefix;

void onTick()
{
  if (state>=ACTIVATE_FIRST_PLANT_SENSOR && state<=ACTIVATE_LAST_PLANT_SENSOR)
  {
    byte plant = state-ACTIVATE_FIRST_PLANT_SENSOR;
    digitalWrite(O_PLANT_SELECT_PINS[plant], HIGH);
    ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    state = static_cast<State>(READ_FIRST_PLANT_SENSOR+plant);
  }
  else if (state>=READ_FIRST_PLANT_SENSOR && state<=READ_LAST_PLANT_SENSOR)
  {
    byte plant = state-READ_FIRST_PLANT_SENSOR;
    plant_sensor_value[CURRENT][plant] = max(0, min(1023, analogRead(I_ANALOG_SENSOR_PIN)));
    digitalWrite(O_PLANT_SELECT_PINS[plant], LOW);
    ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    state = (plant+1)<conf_plant_count ? static_cast<State>(ACTIVATE_FIRST_PLANT_SENSOR+plant+1) : DEACTIVATE_PLANT_SENSOR_MODE;
  }
  else if (state>=ACTIVATE_FIRST_PLANT_VALVE && state<=ACTIVATE_LAST_PLANT_VALVE)
  {
    byte plant = state-ACTIVATE_FIRST_PLANT_VALVE;
    byte sensor_percent_value = (plant_sensor_value[CURRENT][plant]*100)/1023;
    if (sensor_percent_value < plant_sensor_value[CURRENT][plant])
    {
      digitalWrite(O_PLANT_SELECT_PINS[plant], HIGH);
      plant_valve_open_count[CURRENT][plant]++;
      ticker.attach_ms(conf_valve_open_ms[plant], onTick);
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
    digitalWrite(O_PLANT_SELECT_PINS[plant], LOW);
    ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    state = (plant+1)<conf_plant_count ? static_cast<State>(ACTIVATE_FIRST_PLANT_VALVE+plant+1) : DEACTIVATE_PLANT_VALVE_MODE;
  }
  else
  {
    switch(state)
    {
      case START:
        {
          digitalWrite(O_PLANT_SENSOR_MODE_PIN, LOW);
          digitalWrite(O_PLANT_VALVE_MODE_PIN, LOW);
          byte i;
          for (i=0; i<conf_plant_count; i++)
          {
            digitalWrite(O_PLANT_SELECT_PINS[i], LOW);
          }
          digitalWrite(O_LIGHTSENSOR_ACTIVATE_PIN, LOW);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_PLANT_SENSOR_MODE;
          break;
        }
  
      case ACTIVATE_PLANT_SENSOR_MODE:
        {
          digitalWrite(O_PLANT_SENSOR_MODE_PIN, HIGH);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_FIRST_PLANT_SENSOR;
          break;
        }
  
      case DEACTIVATE_PLANT_SENSOR_MODE:
        {
          digitalWrite(O_PLANT_SENSOR_MODE_PIN, LOW);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_PLANT_VALVE_MODE;
          break;
        }
  
      case ACTIVATE_PLANT_VALVE_MODE:
        {
          digitalWrite(O_PLANT_VALVE_MODE_PIN, HIGH);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_FIRST_PLANT_VALVE;
          break;
        }
  
      case DEACTIVATE_PLANT_VALVE_MODE:
        {
          digitalWrite(O_PLANT_VALVE_MODE_PIN, LOW);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = ACTIVATE_LIGHTSENSOR;
          break;
        }
  
      case ACTIVATE_LIGHTSENSOR:
        {
          digitalWrite(O_LIGHTSENSOR_ACTIVATE_PIN, HIGH);
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = READ_LIGHTSENSOR;
          break;
        }
  
      case READ_LIGHTSENSOR:
        {
          int value = max(0, min(1023, analogRead(I_ANALOG_SENSOR_PIN)));
          digitalWrite(O_LIGHTSENSOR_ACTIVATE_PIN, LOW);
          lightsensor_value[CURRENT] = 100.0 - 100.0*value/1023.0;
  
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
          //DHTxx cannot be read in interrupt, so this state is unused (reading is done in main loop)
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = FINISHED;
          break;
        }
      case FINISHED:
        {
          ticker.attach_ms(conf_sec_between_reading*1000, onTick);
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
    mqtt_serverport_param = MQTT_DEFAULT_PORT;
    mqtt_sensorid_param[0] = 0;
    mqtt_username_param[0] = 0;
    mqtt_password_param[0] = 0;
  } else {
    readPersistentString(ssid_param, MAX_SSID_LENGTH, adr);
    readPersistentString(password_param, MAX_PASSWORD_LENGTH, adr);
    readPersistentString(mqtt_servername_param, MAX_MQTT_SERVERNAME_LENGTH, adr);

    char port[MAX_MQTT_SERVERPORT_LENGTH+1];
    readPersistentString(port, MAX_MQTT_SERVERPORT_LENGTH, adr);
    mqtt_serverport_param = atoi(port)&0xFFFF;

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

  char port[MAX_MQTT_SERVERPORT_LENGTH+1];
  sprintf(port, "%hu", mqtt_serverport_param);
  port[MAX_MQTT_SERVERPORT_LENGTH] = 0;
  writePersistentString(port, MAX_MQTT_SERVERPORT_LENGTH, adr);
  
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
      || server.hasArg("mqtt_server") || server.hasArg("mqtt_port") || server.hasArg("mqtt_id") || server.hasArg("mqtt_username") || server.hasArg("mqtt_password"))
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
    if (server.hasArg("mqtt_port"))
    {
      mqtt_serverport_param = server.arg("mqtt_port").toInt()&0xFFFF;
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
    if (server.hasArg("mqtt_password") && !server.arg("mqtt_password").equals(F("password")))
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
    body += F("\"/>MQTT port:<input type=\"number\" name=\"mqtt_port\" min=\"0\" max=\"65535\" value=\"");
    body += String(mqtt_serverport_param);
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
  if (0 != strncmp(mqtt_path_prefix.c_str(), topic, mqtt_path_prefix.length()))
  {
    return;
  }

  const char* key = topic + mqtt_path_prefix.length();

  if (0 == strcmp("config/sec_between_reading", key))
  {
    conf_sec_between_reading = max(1, atoi(reinterpret_cast<const char*>(payload)));
  }
  else if (0 == strcmp("config/plant_count", key))
  {
    conf_plant_count = min(static_cast<const int>(MAX_PLANT_COUNT), max(1, atoi(reinterpret_cast<const char*>(payload))));
    if (conf_plant_count < MAX_PLANT_COUNT)
    {
      //Deactivate all unused plants and turn off builtin LED
      byte i;
      for (i=conf_plant_count; i<MAX_PLANT_COUNT-1; i++)
      {
        digitalWrite(O_PLANT_SELECT_PINS[i], LOW);
      }
      digitalWrite(BUILTIN_LED_PIN, BUILTIN_LED_OFF);
    }
    else
    {
      digitalWrite(O_PLANT_SELECT_PINS[MAX_PLANT_COUNT-1], LOW);
    }
  }
  else if (0 == strncmp("config/plant", key, 12))
  {
    key += 12;
    byte plantno = 0;
    while (*key>='0' && *key<='9')
    {
      plantno = plantno*10 + (*key++-'0');
    }

    if (plantno <= MAX_PLANT_COUNT && *key++=='/')
    {
      if (0 == strcmp("valve_trigger_value", key))
      {
        conf_valve_trigger_value[plantno] = min(100, max(0, atoi(reinterpret_cast<const char*>(payload))));
      }
      else if (0 == strcmp("valve_open_ms", key))
      {
        conf_valve_open_ms[plantno] = max(1, atoi(reinterpret_cast<const char*>(payload)));
      }
      else if (0 == strcmp("sec_valve_grace_period", key))
      {
        conf_valve_sec_grace_period[plantno] = max(1, atoi(reinterpret_cast<const char*>(payload)));
      }
    }
  }
}

void publishMQTTValue(const String& topic, const String& msg)
{
  if (mqtt_enabled)
  {
    mqtt_client.publish((String(mqtt_path_prefix)+topic).c_str(), msg.c_str());
  }
}

void updateValue(const String& topic, float new_value, volatile float& old_value)
{
  if (new_value != old_value)
  {
    publishMQTTValue(topic, String(new_value));
    old_value = new_value;
  }
}

void reconnectMQTT() {
  while (mqtt_enabled && !mqtt_client.connected()) {
    if (mqtt_client.connect("ESP8266Client", mqtt_username_param, mqtt_password_param)) {
      mqtt_client.subscribe((mqtt_path_prefix+F("config/sec_between_reading")).c_str());
      mqtt_client.subscribe((mqtt_path_prefix+F("config/plant_count")).c_str());
      byte i;
      for (i=0; i<MAX_PLANT_COUNT; i++)
      {
        String plant = F("plant");
        plant += String(i);
        plant += F("/");
        
        mqtt_client.subscribe((mqtt_path_prefix+plant+F("valve_trigger_value")).c_str());
        mqtt_client.subscribe((mqtt_path_prefix+plant+F("valve_open_ms")).c_str());
      }
    }
    else
    {
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup()
{
  Serial.end();
  Serial1.end();
  
  EEPROM.begin(1 + MAX_SSID_LENGTH+1 + MAX_PASSWORD_LENGTH+1 + MAX_MQTT_SERVERNAME_LENGTH+1 + MAX_MQTT_SENSORID_LENGTH+1 + MAX_MQTT_USERNAME_LENGTH+1 + MAX_MQTT_PASSWORD_LENGTH+1);

  pinMode(I_SETUP_MODE_PIN, INPUT_PULLUP);
  delay(100);
  if (LOW == digitalRead(I_SETUP_MODE_PIN))
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
    dht.setup(I_TEMPHUMIDSENSOR_PIN, DHT_MODEL);

    //Prepare pins
    byte i;
    pinMode(O_PLANT_SENSOR_MODE_PIN, OUTPUT);
    pinMode(O_PLANT_VALVE_MODE_PIN, OUTPUT);
    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      pinMode(O_PLANT_SELECT_PINS[i], OUTPUT);
    }
    pinMode(O_LIGHTSENSOR_ACTIVATE_PIN, OUTPUT);
    pinMode(I_TEMPHUMIDSENSOR_PIN, OUTPUT);
    pinMode(I_ANALOG_SENSOR_PIN, INPUT);

    //Set output pins
    digitalWrite(O_PLANT_SENSOR_MODE_PIN, LOW);
    digitalWrite(O_PLANT_VALVE_MODE_PIN, LOW);
    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      digitalWrite(O_PLANT_SELECT_PINS[i], LOW);
    }
    digitalWrite(O_LIGHTSENSOR_ACTIVATE_PIN, LOW);

    conf_sec_between_reading = DEFAULT_CONF_SEC_BETWEEN_READING;

    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      plant_sensor_value[CURRENT][i] = plant_sensor_value[OLD][i] = 0;
      plant_valve_open_count[CURRENT][i] = plant_valve_open_count[OLD][i] = 0;
      conf_valve_trigger_value[i] = DEFAULT_CONF_VALVE_TRIGGER_VALUE;
      conf_valve_open_ms[i] = DEFAULT_CONF_VALVE_OPEN_MS;
      conf_valve_sec_grace_period[i] = DEFAULT_CONF_VALVE_SEC_GRACE_PERIOD;
    }
    lightsensor_value[CURRENT] = lightsensor_value[OLD] = 0.0;
    tempsensor_value[CURRENT] = tempsensor_value[OLD] = 0.0;
    humiditysensor_value[CURRENT] = humiditysensor_value[OLD] = 0.0;
    should_read_temp_sensor = false;

    readPersistentParams();
    
    mqtt_enabled = mqtt_servername_param && *mqtt_servername_param && mqtt_sensorid_param && *mqtt_sensorid_param;
    mqtt_path_prefix = F("minidrivhus/");
    mqtt_path_prefix += mqtt_sensorid_param;
    mqtt_path_prefix += F("/");

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid_param, password_param);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
    }
  
    if (mqtt_enabled)
    {
      mqtt_client.setServer(mqtt_servername_param, mqtt_serverport_param);
      mqtt_client.setCallback(mqttCallback);
      reconnectMQTT();
    }

    state = START;
    onTick();
  }
}

void loop()
{
  if (state == SETUP_MODE) {
      dnsServer.processNextRequest();
      server.handleClient();
      delay(100);
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

    delay(dht.getMinimumSamplingPeriod());
    if (should_read_temp_sensor)
    {
      should_read_temp_sensor = false;
      TempAndHumidity temp_and_humidity = dht.getTempAndHumidity();
      if (dht.getStatus() == DHTesp::ERROR_NONE)
      {
        tempsensor_value[CURRENT] = temp_and_humidity.temperature;
        humiditysensor_value[CURRENT] = temp_and_humidity.humidity;

        updateValue(F("temp"), tempsensor_value[CURRENT], tempsensor_value[OLD]);
        updateValue(F("humidity"), humiditysensor_value[CURRENT], humiditysensor_value[OLD]);
      }
    }

    updateValue(F("light"), lightsensor_value[CURRENT], lightsensor_value[OLD]);
  }
}
