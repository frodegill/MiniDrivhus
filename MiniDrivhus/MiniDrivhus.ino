#include <DHTesp.h>           // Library: DHT_sensor_library_fror_ESPx
#include <DNSServer.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PubSubClient.h>     // Library: PubSubClient
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
 minidrivhus/<sensorid>/debug
 */

enum DEBUG_MODE {
  DEBUG_NONE,
  DEBUG_SERIAL,
  DEBUG_MQTT
} debug_mode = DEBUG_SERIAL;
#define MQTT_DEBUG_TOPIC "debug"

static const char* SETUP_SSID = "sensor-setup";
static const byte EEPROM_INITIALIZED_MARKER = 0xF0; //Just a magic number

#define DHT_MODEL (DHTesp::AM2302)

static const byte I_SETUP_MODE_PIN             = 33; // pull to Ground to enable setup mode
static const byte I_TEMPHUMIDSENSOR_PIN        = 32;
static const byte O_LIGHT_RELAY_ACTIVATE_PIN   = 18;
static const byte O_PLANT_VALVE_PINS[]         = {19, 23, 5};
static const byte I_ANALOG_PLANT_SENSOR_PINS[] = {34, 36, 38};
static const byte I_ANALOG_LIGHT_SENSOR_PIN    = 39;

static const byte MAX_PLANT_COUNT = sizeof(O_PLANT_VALVE_PINS)/sizeof(O_PLANT_VALVE_PINS[0]);

static const unsigned int DELAY_BETWEEN_ACTIVE_SENSORS     = 25; //ms between activating sensors
static const unsigned int DEFAULT_CONF_SEC_BETWEEN_READING = 5; //secoonds min. time between sensor activation
static const unsigned int DEFAULT_CONF_VALVE_TRIGGER_VALUE = 50; //[0..100]
static const unsigned int DEFAULT_CONF_VALVE_OPEN_MS       = 5*1000; //ms to keep valve open
static const unsigned int DEFAULT_CONF_VALVE_SEC_GRACE_PERIOD = 20*60; //sec min. time between valve open

static const byte OLD = 0;
static const byte CURRENT = 1;

enum State {
  SETUP_MODE,
  START,
  READ_FIRST_PLANT_SENSOR,
  READ_LAST_PLANT_SENSOR = READ_FIRST_PLANT_SENSOR + (MAX_PLANT_COUNT-1),

  ACTIVATE_FIRST_PLANT_VALVE,
  ACTIVATE_LAST_PLANT_VALVE = ACTIVATE_FIRST_PLANT_VALVE + (MAX_PLANT_COUNT-1),
  DEACTIVATE_FIRST_PLANT_VALVE,
  DEACTIVATE_LAST_PLANT_VALVE = DEACTIVATE_FIRST_PLANT_VALVE + (MAX_PLANT_COUNT-1),

  READ_LIGHTSENSOR,
  READ_TEMPHUMIDSENSOR, //DHTxx cannot be read in interrupt, so this state is unused (reading is done in main loop)

  FINISHED
};


DHTesp dht;
WebServer server(80);

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
volatile long plant_valve_open_time[MAX_PLANT_COUNT];
volatile float lightsensor_value[2];
float tempsensor_value[2];
float humiditysensor_value[2];

unsigned int conf_sec_between_reading;
byte conf_plant_count = MAX_PLANT_COUNT;
byte conf_valve_trigger_value[MAX_PLANT_COUNT];
unsigned int conf_valve_open_ms[MAX_PLANT_COUNT];
unsigned int conf_valve_sec_grace_period[MAX_PLANT_COUNT];


static const byte MAX_SSID_LENGTH            = 32;
static const byte MAX_PASSWORD_LENGTH        = 64;
static const byte MAX_MQTT_SERVERNAME_LENGTH = 64;
static const byte MAX_MQTT_SERVERPORT_LENGTH =  5;
static const byte MAX_MQTT_SENSORID_LENGTH   = 16;
static const byte MAX_MQTT_USERNAME_LENGTH   = 32;
static const byte MAX_MQTT_PASSWORD_LENGTH   = 32;

static const uint16_t MAX_ANALOG_VALUE = 4095;

char ssid_param[MAX_SSID_LENGTH+1];
char password_param[MAX_PASSWORD_LENGTH+1];
char mqtt_servername_param[MAX_MQTT_SERVERNAME_LENGTH+1];
uint16_t mqtt_serverport_param = MQTT_DEFAULT_PORT;
char mqtt_sensorid_param[MAX_MQTT_SENSORID_LENGTH+1];
char mqtt_username_param[MAX_MQTT_USERNAME_LENGTH+1];
char mqtt_password_param[MAX_MQTT_PASSWORD_LENGTH+1];
boolean mqtt_enabled;
String mqtt_path_prefix;

volatile bool debug_enabled = false; // Internal, to keep track of Serial status


void enableDebug()
{
  if (!debug_enabled)
  {
    debug_enabled = true;
    if (debug_mode==DEBUG_SERIAL)
    {
      Serial.begin(9600);
    }
  }
}

void disableDebug()
{
  if (debug_enabled)
  {
    debug_enabled = false;
    if (debug_mode==DEBUG_SERIAL)
    {
      Serial.flush();
      Serial.end();
    }
  }
}

void printDebug(const char* msg)
{
  if (debug_enabled && msg)
  {
    if (debug_mode==DEBUG_SERIAL)
    {
      Serial.println(msg);
    }
    else if (debug_mode==DEBUG_MQTT && mqtt_enabled)
    {
      if (mqtt_client.connected())
      {
        Serial.println((String("MQTT publish returned ") + String(mqtt_client.publish((String(mqtt_path_prefix)+F(MQTT_DEBUG_TOPIC)).c_str(), msg)?"true":"false")).c_str());
      }
      else
      {
        Serial.println("Debug failed - MQTT is not connected");
      }
    }
  }
}

void onTick()
{
  if (state>=READ_FIRST_PLANT_SENSOR && state<=READ_LAST_PLANT_SENSOR)
  {
    byte plant = state-READ_FIRST_PLANT_SENSOR;
    plant_sensor_value[CURRENT][plant] = max((uint16_t)0, min(MAX_ANALOG_VALUE, analogRead(I_ANALOG_PLANT_SENSOR_PINS[plant])));
    printDebug((String("onTick - read sensor ")+String((int)plant)+String(", got ")+String((int)plant_sensor_value[CURRENT][plant])).c_str());
    ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    state = (plant+1)<conf_plant_count ? static_cast<State>(READ_FIRST_PLANT_SENSOR+plant+1) : ACTIVATE_FIRST_PLANT_VALVE;
  }
  else if (state>=ACTIVATE_FIRST_PLANT_VALVE && state<=ACTIVATE_LAST_PLANT_VALVE)
  {
    byte plant = state-ACTIVATE_FIRST_PLANT_VALVE;
    byte sensor_percent_value = (plant_sensor_value[CURRENT][plant]*100)/MAX_ANALOG_VALUE;
    if (sensor_percent_value < conf_valve_trigger_value[plant])
    {
      long current_sec = millis()/1000;
      if (current_sec < plant_valve_open_time[plant]) //Wrapped. Happens every ~50 days
      {
        plant_valve_open_time[plant] = current_sec;
      }
      
      if (plant_valve_open_time[plant]+conf_valve_sec_grace_period[plant] >= current_sec)
      {
        printDebug((String("onTick - skipping activate valve ")+String((int)plant)+String(" (in ")+String((long)(current_sec-plant_valve_open_time[plant]))+String(" of ")+String((int)conf_valve_sec_grace_period[plant])+String(" sec grace period")).c_str());
        ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
        state = (plant+1)<conf_plant_count ? static_cast<State>(ACTIVATE_FIRST_PLANT_VALVE+plant+1) : READ_LIGHTSENSOR;
      }
      else
      {
        printDebug((String("onTick - activate valve ")+String((int)plant)).c_str());
        digitalWrite(O_PLANT_VALVE_PINS[plant], HIGH);
        plant_valve_open_count[CURRENT][plant]++;
        plant_valve_open_time[plant] = current_sec;
        ticker.attach_ms(conf_valve_open_ms[plant], onTick);
        state = static_cast<State>(DEACTIVATE_FIRST_PLANT_VALVE+plant);
      }
    }
    else
    {
      printDebug((String("onTick - activate valve ")+String((int)plant)+String(" ignored, ")+String((int)sensor_percent_value)+String(" >= ")+String((int)conf_valve_trigger_value[plant])).c_str());
      ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
      state = (plant+1)<conf_plant_count ? static_cast<State>(ACTIVATE_FIRST_PLANT_VALVE+plant+1) : READ_LIGHTSENSOR;
    }
  }
  else if (state>=DEACTIVATE_FIRST_PLANT_VALVE && state<=DEACTIVATE_LAST_PLANT_VALVE)
  {
    byte plant = state-DEACTIVATE_FIRST_PLANT_VALVE;
    printDebug((String("onTick - deactivate valve ")+String((int)plant)).c_str());
    digitalWrite(O_PLANT_VALVE_PINS[plant], LOW);
    ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
    state = (plant+1)<conf_plant_count ? static_cast<State>(ACTIVATE_FIRST_PLANT_VALVE+plant+1) : READ_LIGHTSENSOR;
  }
  else
  {
    switch(state)
    {
      case START:
        {
          printDebug((String("onTick - start, plant_count=")+String((int)conf_plant_count)).c_str());
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = READ_FIRST_PLANT_SENSOR;
          break;
        }
  
      case READ_LIGHTSENSOR:
        {
          int value = max((uint16_t)0, min(MAX_ANALOG_VALUE, analogRead(I_ANALOG_LIGHT_SENSOR_PIN)));
          lightsensor_value[CURRENT] = 100.0 - 100.0*value/MAX_ANALOG_VALUE;
          printDebug((String("onTick - deactivate light sensor. Read value ")+String((int)lightsensor_value[CURRENT])).c_str());
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = READ_TEMPHUMIDSENSOR;
          break;
        }

      case READ_TEMPHUMIDSENSOR:
        {
          //DHTxx cannot be read in interrupt, so this state is unused (reading is done in main loop)
          printDebug("onTick - request temphumid value");
          should_read_temp_sensor = true;
          ticker.attach_ms(DELAY_BETWEEN_ACTIVE_SENSORS, onTick);
          state = FINISHED;
          break;
        }

      case FINISHED:
        {
          printDebug("onTick - finished");
          ticker.attach_ms(conf_sec_between_reading*1000, onTick);
          state = START;
          break;
        }
      
      default:
        {
          printDebug("Unknown state");
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
    printDebug((String("readPersistentParams - ssid=")+String(ssid_param)).c_str());
    readPersistentString(password_param, MAX_PASSWORD_LENGTH, adr);
    readPersistentString(mqtt_servername_param, MAX_MQTT_SERVERNAME_LENGTH, adr);
    printDebug((String("readPersistentParams - mqtt servername=")+String(mqtt_servername_param)).c_str());

    char port[MAX_MQTT_SERVERPORT_LENGTH+1];
    readPersistentString(port, MAX_MQTT_SERVERPORT_LENGTH, adr);
    mqtt_serverport_param = atoi(port)&0xFFFF;
    printDebug((String("readPersistentParams - mqtt serverport=")+String((int)mqtt_serverport_param)).c_str());

    readPersistentString(mqtt_sensorid_param, MAX_MQTT_SENSORID_LENGTH, adr);
    printDebug((String("readPersistentParams - mqtt sensorid=")+String(mqtt_sensorid_param)).c_str());
    readPersistentString(mqtt_username_param, MAX_MQTT_USERNAME_LENGTH, adr);
    printDebug((String("readPersistentParams - mqtt username=")+String(mqtt_username_param)).c_str());
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

  printDebug((String("mqttCallback: ")+String(topic)+String(" value ")+String(reinterpret_cast<const char*>(payload))).c_str());
  const char* key = topic + mqtt_path_prefix.length();

  if (0 == strcmp("config/sec_between_reading", key))
  {
    conf_sec_between_reading = max(1, atoi(reinterpret_cast<const char*>(payload)));
    printDebug((String("mqttCallback conf_sec_between_reading=")+String((int)conf_sec_between_reading)).c_str());
  }
  else if (0 == strcmp("config/plant_count", key))
  {
    conf_plant_count = min(static_cast<const int>(MAX_PLANT_COUNT), max(1, atoi(reinterpret_cast<const char*>(payload))));
    printDebug((String("mqttCallback conf_plant_count=")+String((int)conf_plant_count)).c_str());
    if (conf_plant_count < MAX_PLANT_COUNT)
    {
      //Turn off valves for all unused plants
      byte i;
      for (i=conf_plant_count; i<MAX_PLANT_COUNT-1; i++)
      {
        digitalWrite(O_PLANT_VALVE_PINS[i], LOW);
      }
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
        printDebug((String("mqttCallback plant=")+String((int)plantno)+String(" conf_valve_trigger_value=")+String((int)conf_valve_trigger_value[plantno])).c_str());
      }
      else if (0 == strcmp("valve_open_ms", key))
      {
        conf_valve_open_ms[plantno] = max(1, atoi(reinterpret_cast<const char*>(payload)));
        printDebug((String("mqttCallback plant=")+String((int)plantno)+String(" conf_valve_open_ms=")+String((int)conf_valve_open_ms[plantno])).c_str());
      }
      else if (0 == strcmp("sec_valve_grace_period", key))
      {
        conf_valve_sec_grace_period[plantno] = max(1, atoi(reinterpret_cast<const char*>(payload)));
        printDebug((String("mqttCallback plant=")+String((int)plantno)+String(" conf_valve_sec_grace_period=")+String((int)conf_valve_sec_grace_period[plantno])).c_str());
      }
    }
  }
}

void reconnectMQTT() {
  while (mqtt_enabled && !mqtt_client.connected()) {
    if (mqtt_client.connect(mqtt_sensorid_param, mqtt_username_param, mqtt_password_param)) {
      printDebug("MQTT connected");
      mqtt_client.subscribe((mqtt_path_prefix+F("config/sec_between_reading")).c_str());
      mqtt_client.subscribe((mqtt_path_prefix+F("config/plant_count")).c_str());
      if (debug_mode==DEBUG_MQTT)
      {
        mqtt_client.subscribe((mqtt_path_prefix+F(MQTT_DEBUG_TOPIC)).c_str());
      }
      byte i;
      for (i=0; i<MAX_PLANT_COUNT; i++)
      {
        String plant = F("plant");
        plant += String(i);
        plant += F("/");
        
        mqtt_client.subscribe((mqtt_path_prefix+plant+F("valve_trigger_value")).c_str());
        mqtt_client.subscribe((mqtt_path_prefix+plant+F("valve_open_ms")).c_str());
      }
      printDebug("MQTT topics subscribed");
    }
    else
    {
      printDebug("MQTT waiting for reconnect");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void publishMQTTValue(const String& topic, const String& msg)
{
  if (mqtt_enabled)
  {
    reconnectMQTT();
    bool ret = mqtt_client.publish((String(mqtt_path_prefix)+topic).c_str(), msg.c_str(), true);
    printDebug((String("publishMQTTValue topic=")+String(mqtt_path_prefix)+topic+String(" msg=")+msg+String(" returned ")+String(ret?"true":"false")).c_str());
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

void setup()
{
  Serial.end();
  
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
    pinMode(I_TEMPHUMIDSENSOR_PIN, OUTPUT); //DHT handles this pin itself, but it should be OUTPUT before setup
    dht.setup(I_TEMPHUMIDSENSOR_PIN, DHT_MODEL);

    //Prepare pins
    byte i;
    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      pinMode(O_PLANT_VALVE_PINS[i], OUTPUT);
      pinMode(I_ANALOG_PLANT_SENSOR_PINS[i], INPUT);
    }
    pinMode(I_ANALOG_LIGHT_SENSOR_PIN, INPUT);
    pinMode(O_LIGHT_RELAY_ACTIVATE_PIN, OUTPUT);

    //Set output pins
    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      digitalWrite(O_PLANT_VALVE_PINS[i], LOW);
    }
    digitalWrite(O_LIGHT_RELAY_ACTIVATE_PIN, LOW);

    conf_sec_between_reading = DEFAULT_CONF_SEC_BETWEEN_READING;

    for (i=0; i<MAX_PLANT_COUNT; i++)
    {
      plant_sensor_value[CURRENT][i] = plant_sensor_value[OLD][i] = 0;
      plant_valve_open_count[CURRENT][i] = plant_valve_open_count[OLD][i] = 0;
      conf_valve_trigger_value[i] = DEFAULT_CONF_VALVE_TRIGGER_VALUE;
      conf_valve_open_ms[i] = DEFAULT_CONF_VALVE_OPEN_MS;
      conf_valve_sec_grace_period[i] = DEFAULT_CONF_VALVE_SEC_GRACE_PERIOD;
      plant_valve_open_time[i] = 0;
    }
    lightsensor_value[CURRENT] = lightsensor_value[OLD] = 0.0;
    tempsensor_value[CURRENT] = tempsensor_value[OLD] = 0.0;
    humiditysensor_value[CURRENT] = humiditysensor_value[OLD] = 0.0;
    should_read_temp_sensor = false;

    enableDebug();
    
    readPersistentParams();
    
    mqtt_enabled = mqtt_servername_param && *mqtt_servername_param && mqtt_sensorid_param && *mqtt_sensorid_param;
    mqtt_path_prefix = F("minidrivhus/");
    mqtt_path_prefix += mqtt_sensorid_param;
    mqtt_path_prefix += F("/");

    printDebug("Before WiFi");
    WiFi.begin(ssid_param, password_param);
    i = 0;
    while (!WiFi.isConnected()) {
      i++;
      if (i == 3)
      {
        printDebug("Trying WiFi.begin again");
        WiFi.disconnect(true, true);
        delay(1000);
        WiFi.mode(WIFI_STA);
        delay(100);
        WiFi.begin(ssid_param, password_param);
      }
      else if (i == 6)
      {
        printDebug("Forcing WiFi reconnect");
        WiFi.reconnect();
      }
      else if (i == 9)
      {
        printDebug("Giving up WiFi. Restarting");
        ESP.restart();
      }

      printDebug(".");
      WiFi.waitForConnectResult();
    }
    printDebug("WiFi connected");
  
    if (mqtt_enabled)
    {
      printDebug("Enabling MQTT");
      mqtt_client.setServer(mqtt_servername_param, mqtt_serverport_param);
      mqtt_client.setCallback(mqttCallback);
      reconnectMQTT();
    }

    state = START;
    printDebug("state=START");
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

    delay(max(1000, dht.getMinimumSamplingPeriod()));
    printDebug("should_read_temp_sensor?");
    if (should_read_temp_sensor)
    {
      TempAndHumidity temp_and_humidity = dht.getTempAndHumidity();
      if (dht.getStatus() == DHTesp::ERROR_NONE)
      {
        should_read_temp_sensor = false;
        tempsensor_value[CURRENT] = temp_and_humidity.temperature;
        humiditysensor_value[CURRENT] = temp_and_humidity.humidity;
        printDebug((String("reading temp ")+String((int)tempsensor_value[CURRENT])+String(" and humidity ")+String((int)humiditysensor_value[CURRENT])).c_str());

        updateValue(F("temp"), tempsensor_value[CURRENT], tempsensor_value[OLD]);
        updateValue(F("humidity"), humiditysensor_value[CURRENT], humiditysensor_value[OLD]);
      }
      else
      {
        printDebug((String("reading temp and humidity failed with error ")+String((int)dht.getStatus())).c_str());
      }
    }

    updateValue(F("light"), lightsensor_value[CURRENT], lightsensor_value[OLD]);
  }
}
