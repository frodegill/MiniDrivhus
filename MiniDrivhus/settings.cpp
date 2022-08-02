#include "settings.h"

#include "log.h"
#include "mqtt.h"
#include "ntp.h"


void handleNotFound()
{
  g_settings.handleNotFound();
}

void handleSetupRoot()
{
  g_settings.handleSetupRoot();
}

Settings::Settings()
{
  server = std::make_shared<ESP8266WebServer>(80);
  apIP = std::make_shared<IPAddress>(192, 168, 4, 1);
  ssid_param[0] = password_param[0] = mqtt_servername_param[0] = mqtt_sensorid_param[0] = mqtt_username_param[0] = mqtt_password_param[0] = 0;

  conf_sec_between_reading = DEFAULT_CONF_SEC_BETWEEN_READING;
  conf_growlight_minutes_pr_day = DEFAULT_CONF_GROWLIGHT_MINUTES_PR_DAY;
  for (byte i=0; i<MAX_PLANT_COUNT; i++)
  {
    conf_plant_enabled[i] = DEFAULT_CONF_PLANT_ENABLED;
    conf_dry_value[i] = DEFAULT_CONF_DRY_VALUE;
    conf_wet_value[i] = DEFAULT_CONF_WET_VALUE;
    conf_watering_duration_ms[i] = DEFAULT_CONF_WATERING_DURATION_MS;
    conf_watering_grace_period_sec[i] = DEFAULT_CONF_WATERING_GRACE_PERIOD_SEC;
  }
  conf_fan_activate_temp_value = DEFAULT_CONF_FAN_ACTIVATE_TEMP_VALUE;
  conf_fan_activate_humid_value = DEFAULT_CONF_FAN_ACTIVATE_HUMID_VALUE;
}

void Settings::enable()
{
  EEPROM.begin(1 + MAX_SSID_LENGTH+1 + MAX_PASSWORD_LENGTH+1 + MAX_MQTT_SERVERNAME_LENGTH+1 + MAX_MQTT_SERVERPORT_LENGTH+1 + MAX_MQTT_SENSORID_LENGTH+1 + MAX_MQTT_USERNAME_LENGTH+1 + MAX_MQTT_PASSWORD_LENGTH+1);
}

void Settings::readPersistentString(char* s, int max_length, int& adr)
{
  int i = 0;
  byte c;
  do
  {
    c = EEPROM.read(adr++);
    if (i<max_length)
    {
      s[i++] = static_cast<char>(c);
    }
  } while (c!=0);
  s[i] = 0;
}

void Settings::readPersistentByte(uint8_t& b, int& adr)
{
  b = EEPROM.read(adr++);
}

void Settings::readPersistentParams()
{
  int adr = 0;
  if (EEPROM_INITIALIZED_MARKER != EEPROM.read(adr++))
  {
    ssid_param[0] = 0;
    password_param[0] = 0;
    mqtt_servername_param[0] = 0;
    mqtt_serverport_param = MQTT_DEFAULT_PORT;
    strcpy(mqtt_sensorid_param, DEFAULT_SENSORID);
    mqtt_username_param[0] = 0;
    mqtt_password_param[0] = 0;
  }
  else
  {
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

void Settings::writePersistentString(const char* s, size_t max_length, int& adr)
{
  for (size_t i=0; i<min(strlen(s), max_length); i++)
  {
    EEPROM.write(adr++, s[i]);
  }
  EEPROM.write(adr++, 0);
}

void Settings::writePersistentByte(uint8_t b, int& adr)
{
  EEPROM.write(adr++, b);
}

void Settings::writePersistentParams(const char* ssid, const char* password)
{
  int adr = 0;
  EEPROM.write(adr++, EEPROM_INITIALIZED_MARKER);
  writePersistentString(ssid, MAX_SSID_LENGTH, adr);
  writePersistentString(password, MAX_PASSWORD_LENGTH, adr);
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

void Settings::handleNotFound()
{
  server->send(404, F("text/plain"), F("Page Not Found\n"));
}

void Settings::handleSetupRoot()
{
  LOG_DEBUG("start settings::handleSetupRoot");
  if (server->hasArg("ssid") || server->hasArg("password")
      || server->hasArg("mqtt_server") || server->hasArg("mqtt_port") || server->hasArg("mqtt_id") || server->hasArg("mqtt_username") || server->hasArg("mqtt_password"))
  {
    if (server->hasArg("ssid"))
    {
      strncpy(ssid_param, server->arg("ssid").c_str(), MAX_SSID_LENGTH);
      ssid_param[MAX_SSID_LENGTH] = 0;
    }
    
    if (server->hasArg("password") && !server->arg("password").equals(F("password")))
    {
      strncpy(password_param, server->arg("password").c_str(), MAX_PASSWORD_LENGTH);
      password_param[MAX_PASSWORD_LENGTH] = 0;
    }

    if (server->hasArg("mqtt_server"))
    {
      strncpy(mqtt_servername_param, server->arg("mqtt_server").c_str(), MAX_MQTT_SERVERNAME_LENGTH);
      mqtt_servername_param[MAX_MQTT_SERVERNAME_LENGTH] = 0;
    }
    if (server->hasArg("mqtt_port"))
    {
      mqtt_serverport_param = server->arg("mqtt_port").toInt()&0xFFFF;
    }
    if (server->hasArg("mqtt_id"))
    {
      strncpy(mqtt_sensorid_param, server->arg("mqtt_id").c_str(), MAX_MQTT_SENSORID_LENGTH);
      mqtt_sensorid_param[MAX_MQTT_SENSORID_LENGTH] = 0;
    }
    if (server->hasArg("mqtt_username"))
    {
      strncpy(mqtt_username_param, server->arg("mqtt_username").c_str(), MAX_MQTT_USERNAME_LENGTH);
      mqtt_username_param[MAX_MQTT_USERNAME_LENGTH] = 0;
    }
    if (server->hasArg("mqtt_password") && !server->arg("mqtt_password").equals(F("mqtt_password")))
    {
      strncpy(mqtt_password_param, server->arg("mqtt_password").c_str(), MAX_MQTT_PASSWORD_LENGTH);
      mqtt_password_param[MAX_MQTT_PASSWORD_LENGTH] = 0;
    }

    writePersistentParams(ssid_param, password_param);

    server->send(200, F("text/plain"), F("Settings saved"));
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
    server->send(200, F("text/html"), body);
  }
}

void Settings::activateSetupAP()
{
  LOG_DEBUG("start settings::activateSetupAP");
  WiFi.softAP(SETUP_SSID);
  dnsServer.start(DNS_PORT, "*", *apIP.get());
  
  server->on("/", ::handleSetupRoot);
  server->onNotFound(::handleNotFound);
  server->begin();
}

bool Settings::activateWifi()
{
  LOG_DEBUG("start settings::activateWifi");
  readPersistentParams();
  
  LOG_DEBUG("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_param, password_param);
  byte i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    if (++i >= 60) return false; //Try a maximum of 30sek (60*500ms)
    delay(500);    
  }
  LOG_INFO("WiFi connected");
  IPAddress ip = WiFi.localIP();
  LOG_INFO("IP Address: ");
  LOG_INFO(ip.toString().c_str());

  if (g_mqtt.isRequested())
  {
    LOG_INFO("Initializing MQTT");
    g_mqtt.initialize();
  }
  LOG_INFO("Initializing NTP");
  g_ntp.initialize();

  LOG_INFO("WiFi activated");
  return true;
}

void Settings::processNetwork(bool setup_mode)
{
  if (setup_mode)
  {
    dnsServer.processNextRequest(); //Route everything to 192.168.4.1
    server->handleClient(); //WebServer
  }
  else
  {
    if (g_mqtt.isRequested() && g_mqtt.connectMQTT())
    {
      g_mqtt.loop();
    }
  }
}
