/**
   firmware.ino -- part of rfidpad. 

   rfidpad is an ESP32 based device to enable and disable a Home 
   Assistant alarm system using RFID tags

   See https://github.com/janpascal/rfidpad for a hardware description
   and Home Assistant integration.

   Copyright (C) 2020 Jan-Pascal van Best <janpascal@vanbest.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <IotWebConf.h>
#include <ArduinoJson.h>
#include <esp_system.h>

#define DEBUG
#include <Wire.h>
#include <PN532_debug.h>
#include <PN532_I2C.h>
#include <PN532.h>


#define DBG_OUTPUT_PORT Serial
#include <SPIFFS.h>

// I2C pins
#define PIN_SDA (32)
#define PIN_SCL (33)

// Pushbutton pins
#define PIN_DISARM_BUTTON 27
#define PIN_ARM_HOME_BUTTON 13
#define PIN_ARM_AWAY_BUTTON 35

// LED pins
#define PIN_DISARMED_LED 19
#define PIN_ARMED_HOME_LED 18
#define PIN_ARMED_AWAY_LED 17

// FET pin
#define PIN_ENABLE_PN532 4

// Battery voltage divider pin
#define PIN_BATTERY_DIVIDER 34


// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
//      -1 means no config pin
#define CONFIG_PIN -1

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN -1

// Stay awake for 10 seconds during normal operations
#define NORMAL_WAKE_TIME 10000

// Stay away for 10 minutes when multiple buttons were pressed during startup
#define CONFIG_WAKE_TIME 600000

enum requested_state_t {
  requested_state_unknown,
  requested_state_disarm_button,
  requested_state_arm_home_button,
  requested_state_arm_away_button,
  requested_state_config
};

static const char *requested_state_actions[] = { "SCAN", "DISARM", "ARM_HOME", "ARM_AWAY", "SCAN" };

enum state_t {
  state_unknown,
  state_disarmed,
  state_disarmed_pending,
  state_armed_home,
  state_armed_home_pending,
  state_armed_away,
  state_armed_away_pending,
  state_config                // CA certificate incomplete or double buttons pressed
};

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "keypad";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "pad323232";

const char action_topic_name[] = "action";
const char state_topic_name[] = "state";
const char battery_level_topic_name[] = "bat";

const char* caFilename = "/ca.crt";

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "rfidpad_v1"

#define STRING_LEN 128
#define PORT_LEN 5
#define MAX_TOPIC_LEN 256

// -- Callback method declarations.
void configSaved();
boolean formValidator();

DNSServer dnsServer;
WiFiClientSecure net;
WebServer server(80);
PubSubClient mqttClient(net);
File fsUploadFile; //holds the current upload

TwoWire wire(0);
PN532_I2C pn532_i2c(wire);
PN532 pn532(pn532_i2c);

char mqttServerValue[STRING_LEN];
char mqttServerPortValue[PORT_LEN];
char mqttUserNameValue[STRING_LEN];
char mqttUserPasswordValue[STRING_LEN];
char mqttTopicPrefixValue[STRING_LEN];

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameter mqttServerParam = IotWebConfParameter("MQTT server", "mqttServer", mqttServerValue, STRING_LEN, "text", NULL, "bree.vanbest.eu");
IotWebConfParameter mqttServerPortParam = IotWebConfParameter("MQTT port", "mqttPort", mqttServerPortValue, PORT_LEN, "text", NULL, "1883"); // , "1883 for MQTT, 8883 for MQTTS");
IotWebConfParameter mqttUserNameParam = IotWebConfParameter("MQTT user", "mqttUser", mqttUserNameValue, STRING_LEN, "text", NULL, "keypad");
IotWebConfParameter mqttUserPasswordParam = IotWebConfParameter("MQTT password", "mqttPass", mqttUserPasswordValue, STRING_LEN, "password", NULL, "aeSheeciingeib6G");
IotWebConfParameter mqttTopicPrefixParam = IotWebConfParameter("MQTT prefix", "mqttPrefix", mqttTopicPrefixValue, STRING_LEN, "text", NULL, "rfidpad");
// -- We can add a legend to the separator
//IotWebConfSeparator separator2 = IotWebConfSeparator("Calibration factor");

boolean needMqttConnect = false;
unsigned long lastMqttConnectionAttempt = 0;
static int current_led = 0;
unsigned long lastMsg = 0;
unsigned long mqttLastMsgTimestamp = 0;

// Overwritten at startup
char mqtt_status_channel[MAX_TOPIC_LEN] = "rfidpad/status";
char mqtt_action_channel[MAX_TOPIC_LEN] = "rfidpad/action";
char mqtt_discovery_channel[MAX_TOPIC_LEN] = "rfidpad/discovery/000000000000";
char mqtt_bat_channel[MAX_TOPIC_LEN] = "rfidpad/bat";
char device_id[] = "000000000000";
char device_name[STRING_LEN] = "<unknown>";

requested_state_t requested_state = requested_state_unknown;
state_t current_state = state_unknown;
int last_state_change = 0;
char mqtt_message_to_send[255] = "";

void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting up...");

  if (PIN_BATTERY_DIVIDER > 0) pinMode(PIN_BATTERY_DIVIDER, INPUT);

  if (PIN_DISARMED_LED > 0) pinMode(PIN_DISARMED_LED, OUTPUT);
  if (PIN_ARMED_HOME_LED > 0) pinMode(PIN_ARMED_HOME_LED, OUTPUT);
  if (PIN_ARMED_AWAY_LED > 0) pinMode(PIN_ARMED_AWAY_LED, OUTPUT);

  pinMode(PIN_DISARM_BUTTON, INPUT);
  pinMode(PIN_ARM_HOME_BUTTON, INPUT);
  pinMode(PIN_ARM_AWAY_BUTTON, INPUT);

  if (PIN_ENABLE_PN532 > 0) {
    pinMode(PIN_ENABLE_PN532, OUTPUT);
    digitalWrite(PIN_ENABLE_PN532, LOW);
  }

  requested_state = get_requested_state();
  if (requested_state == requested_state_config) {
    current_state = state_config;
  } else {
    current_state = state_unknown;
  }


  // if (FORMAT_FILESYSTEM) SPIFFS.format();
  SPIFFS.begin();                           // Start the SPI Flash Files System

  iotWebConf.setStatusPin(STATUS_PIN);
  iotWebConf.setConfigPin(CONFIG_PIN);

  iotWebConf.addParameter(&mqttServerParam);
  iotWebConf.addParameter(&mqttServerPortParam);
  iotWebConf.addParameter(&mqttUserNameParam);
  iotWebConf.addParameter(&mqttUserPasswordParam);
  iotWebConf.addParameter(&mqttTopicPrefixParam);

  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);
  //iotWebConf.getApTimeoutParameter()->visible = true;
  iotWebConf.skipApStartup(); // Do not always spend first 30 seconds in AP mode, only if config invalid

  // -- Initializing the configuration.
  bool validConfig = iotWebConf.init();
  if (!validConfig)
  {
    Serial.println("Invalid config, resetting Wifi info...");
    iotWebConf.resetWifiAuthInfo();
    strncpy(mqttServerValue, mqttServerParam.defaultValue, STRING_LEN);
    strncpy(mqttServerPortValue, mqttServerPortParam.defaultValue, PORT_LEN);
    strncpy(mqttUserNameValue, mqttUserNameParam.defaultValue, STRING_LEN);
    strncpy(mqttUserPasswordValue, mqttUserPasswordParam.defaultValue, STRING_LEN);
    strncpy(mqttTopicPrefixValue, mqttTopicPrefixParam.defaultValue, STRING_LEN);
  }

  Serial.println("Setting ca certificate to connection to MQTT server");
  if (!SPIFFS.exists(caFilename)) {
    Serial.printf("CA file %s does not exist!\n", caFilename);
  }
  File file = SPIFFS.open(caFilename, "r");

  if(!file) {
    Serial.printf("Error opening the CA file %s\n.", caFilename);
  } else {
    String ca_cert = file.readString();
    Serial.printf("CA Root certificate: %s\n", ca_cert.c_str());
    char *tmp;  
    tmp = (char *)malloc(sizeof(char) * (ca_cert.length()+1));
    strcpy(tmp, ca_cert.c_str());
      
    net.setCACert(tmp);
    file.close();
    // free(tmp);      
  }

  IotWebConfParameter* ap_passwd_param = iotWebConf.getApPasswordParameter();
  char* pw = ap_passwd_param->valueBuffer;
  Serial.print("Config password: ");
  Serial.println(pw);

  IotWebConfParameter* thing_name_param = iotWebConf.getThingNameParameter();
  strncpy(device_name, thing_name_param->valueBuffer, sizeof(device_name));
  device_name[sizeof(device_name)-1] = 0;
  Serial.printf("Device name: %s\n", device_name);

  Serial.printf("Fetching hardware id...\n");
  fetch_device_id();
  Serial.printf("Device ID: %s\n", device_id);
  
  Serial.println("Setting up MQTT topic names...");
  snprintf(mqtt_status_channel, MAX_TOPIC_LEN, "%s/%s/%s", mqttTopicPrefixValue, device_id, state_topic_name);
  snprintf(mqtt_action_channel, MAX_TOPIC_LEN, "%s/%s/%s", mqttTopicPrefixValue, device_id, action_topic_name);
  snprintf(mqtt_discovery_channel, MAX_TOPIC_LEN, "%s/discovery/%s", mqttTopicPrefixValue, device_id);
  snprintf(mqtt_bat_channel,    MAX_TOPIC_LEN, "%s/%s/%s", mqttTopicPrefixValue, device_id, battery_level_topic_name);
  Serial.printf("* status topic: %s\n", mqtt_status_channel);
  Serial.printf("* action topic: %s\n", mqtt_action_channel);
  Serial.printf("* config topic: %s\n", mqtt_discovery_channel);
  Serial.printf("* battery level topic: %s\n", mqtt_bat_channel);

  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.on("/upload_cert", HTTP_POST,                       // if the client posts to the upload page
    [](){ server.send(200); },                          // Send status 200 (OK) to tell the client we are ready to receive
    handleCertUpload                                    // Receive and save the file
  );
  server.onNotFound([]() {
    iotWebConf.handleNotFound();
  });


  mqttClient.setCallback(mqttMessageReceived);

  Serial.println("Setting up i2c...");
  if (!wire.begin(PIN_SDA, PIN_SCL)) {
    Serial.println("Failed to initialise i2c");
    enter_deep_sleep();
  }

  Serial.println("Beginning pn532_i2c...");
  pn532_i2c.begin();

  Serial.println("Waking up pn532_i2c...");
  pn532_i2c.wakeup();

  Serial.println("Beginning PN532...");
  pn532.begin();

  Serial.println("Attempting to get pn532 firmware version...");
  uint32_t versiondata = pn532.getFirmwareVersion();
  if (! versiondata) {
    Serial.println("Didn't find PN53x board, bailing out!");
    Serial.println("Nope, ignoring to see what happens...");
    // enter_deep_sleep();
  }

  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata >> 24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata >> 16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata >> 8) & 0xFF, DEC);

  // Set the max number of retry attempts to read from a card
  // This prevents us from waiting forever for a card, which is
  // the default behaviour of the PN532.
  if (!pn532.setPassiveActivationRetries(0x01)) {
    Serial.println("Failed to set Passive activation retry count, ignoring!");
  }

  // configure board to read RFID tags
  if (!pn532.SAMConfig()) {
    Serial.println("SAMConfig failed, ignoring!");
  }

  wire.flush();

  Serial.println("Ready.");
}

void fetch_device_id() {
  uint8_t baseMac[6];
  // Get MAC address for WiFi station
  esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  snprintf(device_id, sizeof(device_id), "%02X%02X%02X%02X%02X%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
}

// call back
void wifiConnected()
{
  Serial.println("Wifi is connected, signalling to start MQTT...");
  needMqttConnect = true;
}

requested_state_t get_requested_state()
{
  uint64_t bitmask;
  requested_state_t response = requested_state_unknown;
  int count = 0;
        
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : 
        Serial.println("Wakeup caused by external signal using RTC_IO"); 
        break;
    case ESP_SLEEP_WAKEUP_EXT1 : 
        Serial.println("Wakeup caused by external signal using RTC_CNTL"); 
        bitmask = esp_sleep_get_ext1_wakeup_status();
        Serial.printf("Wake up trigger bitmask: %016llx\n", bitmask);
        if (digitalRead(PIN_DISARM_BUTTON) == HIGH) {
          response = requested_state_disarm_button;
          count++;
        }
        if (digitalRead(PIN_ARM_HOME_BUTTON) == HIGH) {
          response = requested_state_arm_home_button;
          count++;
        }
        if (digitalRead(PIN_ARM_AWAY_BUTTON) == HIGH) {
          response = requested_state_arm_away_button;
          count++;
        }
        /*
        if (bitmask & (1ULL << PIN_DISARM_BUTTON)) {
          response = requested_state_disarm_button;
          count++;
        } 
        if (bitmask & (1ULL << PIN_ARM_HOME_BUTTON)) {
          response = requested_state_arm_home_button;
          count++;
        } 
        if (bitmask & (1ULL << PIN_ARM_AWAY_BUTTON)) {
            response = requested_state_arm_away_button;
            count++;
        } 
        */
        if (count == 0) {
          Serial.println("Woken up by unknown pin");
          return requested_state_unknown;
        }
        if (count == 1) {
          Serial.printf("Woken up by single pin, wakeup reason %d\n", response);
          return response;
        }
        // Multiple buttons pressed on startup
        Serial.printf("Woken up by multiple pins, wakeup reason %d\n", requested_state_config);
        return requested_state_config;
        break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;
  }

  return requested_state_unknown;
}

void update_requested_state_from_buttons()
{
  if (digitalRead(PIN_DISARM_BUTTON) == HIGH) requested_state = requested_state_disarm_button;
  if (digitalRead(PIN_ARM_HOME_BUTTON) == HIGH) requested_state = requested_state_arm_home_button;
  if (digitalRead(PIN_ARM_AWAY_BUTTON) == HIGH) requested_state = requested_state_arm_away_button;
}

int lastBlinkMillis = 0;
int lastBlinkState = 0;
int lastBlinkLed = 0;

void update_state_leds(state_t state, boolean force=false)
{
  int now = millis();

  if (now -lastBlinkMillis < 200 && !force) {
    return;
  }
  // Serial.printf("Updating leds for state %d\n", state);
  
  lastBlinkMillis = now;

  lastBlinkState = 1 - lastBlinkState;
  lastBlinkLed = (lastBlinkLed + 1) % 3;
  
  int led_disarmed = 0;
  int led_armed_home = 0;
  int led_armed_away = 0;

  switch(state) {
    case state_unknown:
      if (lastBlinkState) led_disarmed = led_armed_home = led_armed_away = 1;
      break;
    case state_config:
      led_disarmed = (lastBlinkLed == 0);
      led_armed_home = (lastBlinkLed == 1);
      led_armed_away = (lastBlinkLed == 2);
      break;
    case state_disarmed:
      led_disarmed = 1;
      break;
    case state_disarmed_pending:
      if (lastBlinkState) led_disarmed = 1;
      break;
    case state_armed_home:
      led_armed_home = 1;
      break;
    case state_armed_home_pending:
      if (lastBlinkState) led_armed_home = 1;
      break;
    case state_armed_away:
      led_armed_away = 1;
      break;
    case state_armed_away_pending:
      if (lastBlinkState) led_armed_away = 1;
      break;
  }

  if (PIN_DISARMED_LED > 0) digitalWrite(PIN_DISARMED_LED, led_disarmed ? HIGH : LOW);
  if (PIN_ARMED_HOME_LED > 0) digitalWrite(PIN_ARMED_HOME_LED, led_armed_home ? HIGH : LOW);
  if (PIN_ARMED_AWAY_LED > 0) digitalWrite(PIN_ARMED_AWAY_LED, led_armed_away ? HIGH : LOW);

}

void set_state(state_t new_state)
{
  Serial.printf("Setting state to %d\n", new_state);
  current_state = new_state;
  last_state_change = millis();
  update_state_leds(new_state, true);
}


void reconnect_mqtt()
{
  if (needMqttConnect)
  {
    Serial.println("Connecting to MQTT...");
    if (connectMqtt())
    {
      Serial.println("MQTT connected");
      needMqttConnect = false;
    }
  }
  else if ((iotWebConf.getState() == IOTWEBCONF_STATE_ONLINE) && (!mqttClient.connected()))
  {
    Serial.println("Reconnecting to MQTT...");
    if (connectMqtt())
    {
      Serial.println("MQTT connected");
      needMqttConnect = false;
    }
  }
}

#define MSG_BUFFER_SIZE 50

float get_battery_percentage(float* voltage = 0)
{
    int raw = analogRead(PIN_BATTERY_DIVIDER);
    // Serial.printf("Raw value: %d\n", raw);
    
    // 3.3 is full scale (4095); assume linear measurement
    float measured_voltage = 3.30f * float(raw) / 4095.0f;
    // Serial.print("Measured value: "); Serial.print(measured_voltage, 2); Serial.println(" V");
    
    // Compensate for voltage divider (470k/470k)
    float Vbat = (940.0f/470.0f) * measured_voltage;

    // 100% is 4.2V
    // each 1% down, 5.14mV = 0.00514V
    // so empty = 4.2V - 0.514V = 3.686V
    // According to spec: max 4.2V, min 2.75V, nominal 3.63V
    float battery_percentage = (Vbat - 3.686) / 0.514 * 100;

    if (voltage != 0) *voltage = Vbat;

    return battery_percentage;
}


void publish_battery_level()
{
  unsigned long now = millis();
  if (mqttClient.connected() && now - mqttLastMsgTimestamp > 2000) {
    mqttLastMsgTimestamp = now;
    ++lastMsg;
    // First battery reading is unreliable
    if (lastMsg == 1) return;
    float voltage;
    float battery_percentage = get_battery_percentage(&voltage);
    int percent = battery_percentage;
    StaticJsonDocument<200> doc;
    doc["level"] = percent;
    doc["voltage"] = voltage;
    String msg;
    serializeJson(doc, msg);
    Serial.printf("Publish battery level message: %s\n", msg.c_str());
    mqttClient.publish(mqtt_bat_channel, msg.c_str());
  }
}

void loop()
{
  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
  mqttClient.loop();

  // Serial.println("Checking to (re)connect MQTT...");
  reconnect_mqtt();
  
  // Serial.println("Pinging MQTT...");
  publish_battery_level();

  //Serial.println("Handling nfc...");
  handle_nfc();
  //Serial.println("Checking for pressed button...");
  update_requested_state_from_buttons();
  //Serial.println("Updating state LEDs...");
  update_state_leds(current_state);

  //Serial.println("Checking if we need to send an MQTT message...");
  if (mqtt_message_to_send[0] != 0 && mqttClient.connected())
  {    
    Serial.printf("Publishing %s to %s\n", mqtt_message_to_send, mqtt_action_channel); 
    mqttClient.publish(mqtt_action_channel, mqtt_message_to_send);
    mqtt_message_to_send[0] = 0;
  }

  //Serial.println("Checking if we need to shut down...");
  if (current_state != state_config && millis() - last_state_change > NORMAL_WAKE_TIME) {
    int awake_time = millis() - last_state_change;
    Serial.printf("Been awake without action for more than %d ms, going to deep sleep...\n", millis() - last_state_change);
    mqttClient.publish(mqtt_action_channel, "Going to sleep");
    enter_deep_sleep();
  }

  //Serial.println("Checking if we need to shut down in config mode...");
  if (current_state == state_config && millis() - last_state_change > CONFIG_WAKE_TIME) {
    Serial.printf("In Config mode, but awake without action for more than %d ms, going to deep sleep...\n", millis() - last_state_change);
    enter_deep_sleep();
  }

  //Serial.println("Waiting 50ms...");
  delay(50);
}

void enter_deep_sleep()
{
  Serial.println("Preparing for deep sleep:");
  
  Serial.println("* Shutting down Wifi");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  Serial.println("* Turning off I2C");
  wire.~TwoWire();
  pinMode(PIN_SDA, INPUT);
  pinMode(PIN_SCL, INPUT);

  Serial.println("* Turning off PN532");
  // No use, led stays on anyways. Kill the power (using a FET) instead!
  // pn532.shutDown(0x80, false); // 0x80: wake up on I2C

  if (PIN_ENABLE_PN532 > 0) {
    digitalWrite(PIN_ENABLE_PN532, HIGH);
    pinMode(PIN_ENABLE_PN532, INPUT);
  }

  Serial.println("Entering deep sleep until woken by button...");
  uint64_t mask = ( 1ULL << PIN_DISARM_BUTTON ) | ( 1ULL << PIN_ARM_HOME_BUTTON ) | ( 1ULL << PIN_ARM_AWAY_BUTTON );
  //uint64_t mask = ( 1ULL << PIN_DISARM_BUTTON );
  // ( 0x8000000 + 0x400000000 + 0x800000000 )
  char msg[80];
  snprintf(msg, 80, "Deep sleep mask: %016llx", mask);
  Serial.println(msg);
  esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_deep_sleep_start();
}

int bytes_to_hex(char* s, int maxlen, byte* bytes, int count)
{
  if (maxlen < 2*count + 1) {
    return -1;
  }
  for (int i = 0; i < count; i++)
  {
    int hi = bytes[i] >> 4;
    int lo = bytes[i] && 0x0f;
    s[2*i] = (hi < 10 ? hi + '0' : hi + 'A' - 10);
    s[2*i + 1] = (lo < 10 ? lo + '0' : lo + 'A' - 10);
  }
  s[2*count] = 0;
  return 2*count;
}

void handle_nfc()
{
  // Serial.println("\nPlease scan an NFC tag\n");
  boolean success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  char uid_string[2*7+1];

  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);

  if (success) {
    Serial.println("Found a card!");
    if (bytes_to_hex(uid_string, sizeof(uid_string), uid, uidLength) < 0) {
      Serial.println("Failed to convert UID to hex string");
      return;
    }

    Serial.printf("UID Value: %s\n", uid_string);

    StaticJsonDocument<200> doc;
    doc["button"] = requested_state_actions[requested_state];
    doc["tag"] = uid_string;
    String msg;
    serializeJson(doc, msg);

    strncpy(mqtt_message_to_send, msg.c_str(), sizeof(mqtt_message_to_send));
    Serial.printf("Queueing message to send: '%s'\n", mqtt_message_to_send);

    if (requested_state == requested_state_disarm_button) set_state(state_disarmed_pending);
    if (requested_state == requested_state_arm_home_button) set_state(state_armed_home_pending);
    if (requested_state == requested_state_arm_away_button) set_state(state_armed_away_pending);
  }
  wire.flush();
}

boolean connectMqtt() {
  unsigned long now = millis();
  if (1000 > now - lastMqttConnectionAttempt)
  {
    // Do not repeat within 1 sec.
    return false;
  }
  Serial.println("Connecting to MQTT server...");
  if (!connectMqttOptions()) {
    Serial.println("Failed to connect to MQTT server!");
    lastMqttConnectionAttempt = now;
    return false;
  }
  Serial.println("MQTT Connected!");

  Serial.printf("Subscribing to mqtt %s\n", mqtt_status_channel);
  mqttClient.subscribe(mqtt_status_channel);

  Serial.printf("Sending discovery information to %s\n", mqtt_discovery_channel);
  StaticJsonDocument<200> doc;
  doc["name"] = device_name;
  doc["id"] = device_id;
  doc["base_topic"] = String(mqttTopicPrefixValue) + "/" + String(device_id);
  doc["status_topic"] = state_topic_name;
  doc["action_topic"] = action_topic_name;
  doc["battery_topic"] = battery_level_topic_name;

  String msg;
  serializeJson(doc, msg);
  
  Serial.printf("Discovery info: %s\n", msg.c_str());
  mqttClient.publish(mqtt_discovery_channel, msg.c_str());
  return true;
}

/**
   Handle web requests to "/" path.
*/
void handleRoot()
{
  Serial.println("Entering handleRoot()");
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    Serial.println("Connection had been handled by captive portal");
    return;
  }
  float battery_percentage = get_battery_percentage();
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>IotWebConf based keypad</title></head><body><p>Hello world!</p>";
  s += "<p>Go to <a href='config'>configure page</a> to change values.</p>";
  s += "<form action=\"upload_cert\" method=\"post\" enctype=\"multipart/form-data\">";
  s += "  <input type=\"file\" name=\"name\">";
  s += "  <input class=\"button\" type=\"submit\" value=\"Upload\">";
  s += "</form>";
  s += "</p>Battery level: ";
  s += battery_percentage;
  s += "%</p>";
  
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void handleCertUpload(){ // upload the MQTT CA Certificate to the SPIFFS
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = caFilename;
    Serial.print("handleCertUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");            // Open the file for writing in SPIFFS (create if it doesn't exist)
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize); // Write the received bytes to the file
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile) {                                    // If the file was successfully created
      fsUploadFile.close();                               // Close the file again
      Serial.print("handleCertUpload Size: "); Serial.println(upload.totalSize);
      server.sendHeader("Location","/");      // Redirect the client to the index
      server.send(303);
    } else {
      server.send(500, "text/plain", "500: couldn't create ca certificate file");
    }
  }
}

void configSaved()
{
  Serial.println("Configuration was updated.");
}

boolean formValidator()
{
  Serial.println("Validating form.");
  boolean valid = true;

  String port = server.arg(mqttServerPortParam.getId());
  for (int i = 0; i < port.length(); ++i) {
    if (port[i] < '0' || port[i] > '9') {
      mqttServerPortParam.errorMessage = "MQTT server port should be a number";
      valid = false;
    }
  }

  return valid;
}

boolean connectMqttOptions()
{
  int port = atoi(mqttServerPortValue);

  char msg[256];
  snprintf(msg, 256, "Connecting to MQTT server %s:%d with user %s and password %s", mqttServerValue, port, mqttUserNameValue, mqttUserPasswordValue);
  Serial.println(msg);
  mqttClient.setServer(mqttServerValue, port);
  return mqttClient.connect(iotWebConf.getThingName(), mqttUserNameValue, mqttUserPasswordValue);
}


void mqttMessageReceived(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Incoming: [");
  Serial.print(topic);
  Serial.print( "] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  StaticJsonDocument<100> doc;
  if (deserializeJson(doc, payload, length) != DeserializationError::Ok) {
    Serial.println("Error parsing incoming mqtt message as JSON");
    return;
  }

  const char* new_status = doc["new_status"];
  if (!new_status) {
    Serial.println("mqtt message does not contains new_status");
    return;
  }
  if (strcmp("DISARMED", new_status) == 0) {
    set_state(state_disarmed);
  } else if (strcmp("ARMED_HOME", new_status) == 0) {
    set_state(state_armed_home);
  } else if (strcmp("ARMED_AWAY", new_status) == 0) {
    set_state(state_armed_away);
  } else {
    Serial.println("Received unknown new status from mqtt!");
  }
}
