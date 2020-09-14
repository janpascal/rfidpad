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

#define DEBUG
#include <Wire.h>
#include <PN532_debug.h>
#include <PN532_I2C.h>
#include <PN532.h>


#define LED_RED_PIN 16
#define LED_GREEN_PIN 17
#define LED_BLUE_PIN 18

// I2C pins
#define PIN_SDA (32)
#define PIN_SCL (33)

#define PIN_DISARM_BUTTON 27
#define PIN_ARM_HOME_BUTTON 34
#define PIN_ARM_AWAY_BUTTON 35

#define PIN_DISARMED_LED 19
#define PIN_ARMED_HOME_LED 18
#define PIN_ARMED_AWAY_LED 17

enum requested_state_t {
  requested_state_unknown,
  requested_state_disarm_button,
  requested_state_arm_home_button,
  requested_state_arm_away_button
};

static const char *requested_state_actions[] = { "SCAN", "DISARM", "ARM_HOME", "ARM_AWAY" };

enum state_t {
  state_unknown,
  state_disarmed,
  state_disarmed_pending,
  state_armed_home,
  state_armed_home_pending,
  state_armed_away,
  state_armed_away_pending
};

// Calculated in enter_deep_sleep()
// define BUTTON_PIN_BITMASK ( 0x8000000 + 0x400000000 + 0x800000000 )

#define VBAT_PIN 35

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "keypad";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "pad323232";


const char* defaultRootCa = \
                            "-----BEGIN CERTIFICATE-----\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX\n" \
                            "XXX=\n" \
                            "-----END CERTIFICATE-----\n";

#define STRING_LEN 128
#define NUMBER_LEN 32

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "key1"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
//      -1 means no config pin
#define CONFIG_PIN -1

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN -1

#define STRING_LEN 128
#define CACERT_LEN 2000
#define PORT_LEN 5

// -- Callback method declarations.
void configSaved();
boolean formValidator();

DNSServer dnsServer;
WiFiClientSecure net;
WebServer server(80);
PubSubClient mqttClient(net);

TwoWire wire(0);
PN532_I2C pn532_i2c(wire);
PN532 pn532(pn532_i2c);

char mqttServerValue[STRING_LEN];
char mqttServerPortValue[PORT_LEN];
char mqttServerCaCertValue[CACERT_LEN];
char mqttUserNameValue[STRING_LEN];
char mqttUserPasswordValue[STRING_LEN];

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfParameter mqttServerParam = IotWebConfParameter("MQTT server", "mqttServer", mqttServerValue, STRING_LEN, "text", NULL, "mqtt.example.com");
IotWebConfParameter mqttServerPortParam = IotWebConfParameter("MQTT port", "mqttPort", mqttServerPortValue, PORT_LEN, "text", NULL, "1883"); // , "1883 for MQTT, 8883 for MQTTS");
//IotWebConfParameter mqttServerCaCertParam = IotWebConfParameter("MQTT CA Certificate", "mqttCaCert", mqttServerCaCertValue, CACERT_LEN, "text", NULL, defaultRootCa); // , "CA certificate for MQTT server. Leave empty not to validate certficate");
IotWebConfParameter mqttUserNameParam = IotWebConfParameter("MQTT user", "mqttUser", mqttUserNameValue, STRING_LEN, "text", NULL, "rfidpad");
IotWebConfParameter mqttUserPasswordParam = IotWebConfParameter("MQTT password", "mqttPass", mqttUserPasswordValue, STRING_LEN, "password", NULL, "SECRETSECRET");
// -- We can add a legend to the separator
//IotWebConfSeparator separator2 = IotWebConfSeparator("Calibration factor");

boolean needMqttConnect = false;
unsigned long lastMqttConnectionAttempt = 0;
static int current_led = 0;
unsigned long lastMsg = 0;
unsigned long mqttLastMsgTimestamp = 0;
char mqtt_status_channel[] = "rfidpad/status";
char mqtt_action_channel[] = "rfidpad/action";

requested_state_t requested_state = requested_state_unknown;
state_t current_state = state_unknown;
int last_state_change = 0;
char mqtt_message_to_send[255] = "";

void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println("Starting up...");

  requested_state = get_requested_state();

  //pinMode(VBAT_PIN, INPUT);
  //pinMode(LED_RED_PIN, OUTPUT);
  //pinMode(LED_GREEN_PIN, OUTPUT);
  //pinMode(LED_BLUE_PIN, OUTPUT);

  if (PIN_DISARMED_LED > 0) pinMode(PIN_DISARMED_LED, OUTPUT);
  if (PIN_ARMED_HOME_LED > 0) pinMode(PIN_ARMED_HOME_LED, OUTPUT);
  if (PIN_ARMED_AWAY_LED > 0) pinMode(PIN_ARMED_AWAY_LED, OUTPUT);

  pinMode(PIN_DISARM_BUTTON, INPUT);
  pinMode(PIN_ARM_HOME_BUTTON, INPUT);
  pinMode(PIN_ARM_AWAY_BUTTON, INPUT);

  iotWebConf.setStatusPin(STATUS_PIN);
  iotWebConf.setConfigPin(CONFIG_PIN);

  iotWebConf.addParameter(&mqttServerParam);
  iotWebConf.addParameter(&mqttServerPortParam);
  //iotWebConf.addParameter(&mqttServerCaCertParam);

  iotWebConf.addParameter(&mqttUserNameParam);
  iotWebConf.addParameter(&mqttUserPasswordParam);

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
    //strncpy(mqttServerCaCertValue, mqttServerCaCertParam.defaultValue, mqttServerParam.getLength());
    strncpy(mqttUserNameValue, mqttUserNameParam.defaultValue, STRING_LEN);
    strncpy(mqttUserPasswordValue, mqttUserPasswordParam.defaultValue, STRING_LEN);
  }

  if (mqttServerCaCertValue[0] != 0) {
    Serial.println("Setting ca certificate to connection to MQTT server");
    net.setCACert(defaultRootCa);
  }

  IotWebConfParameter* ap_passwd_param = iotWebConf.getApPasswordParameter();
  char* pw = ap_passwd_param->valueBuffer;
  Serial.print("Config password: ");
  Serial.println(pw);

  IotWebConfParameter* thing_name_param = iotWebConf.getThingNameParameter();
  char* name = thing_name_param->valueBuffer;
  Serial.print("Config name: ");
  Serial.println(name);

  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() {
    iotWebConf.handleNotFound();
  });

  mqttClient.setCallback(mqttMessageReceived);

  Serial.println("Setting up i2c...");
  if (!wire.begin(PIN_SDA, PIN_SCL)) {
    Serial.println("Failed to initialise i2c");
    pinMode(PIN_SDA, INPUT); // needed because Wire.end() enables pullups, power Saving
    pinMode(PIN_SCL, INPUT);
    Serial.println("Halting...");
    while (1) ;
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
    //Serial.println("Nope, ignoring to see what happens...");
    pinMode(PIN_SDA, INPUT); // needed because Wire.end() enables pullups, power Saving
    pinMode(PIN_SCL, INPUT);
    Serial.println("Halting...");
    while (1); // halt
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

// call back
void wifiConnected()
{
  needMqttConnect = true;
}

requested_state_t get_requested_state()
{
  uint64_t bitmask;

  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0 : 
        Serial.println("Wakeup caused by external signal using RTC_IO"); 
        break;
    case ESP_SLEEP_WAKEUP_EXT1 : 
        Serial.println("Wakeup caused by external signal using RTC_CNTL"); 
        bitmask = esp_sleep_get_ext1_wakeup_status();
        Serial.printf("Wake up trigger bitmask: %016llx\n", bitmask);
      
        if (bitmask & (1ULL << PIN_DISARM_BUTTON)) {
          return requested_state_disarm_button;
        } else if (bitmask & (1ULL << PIN_ARM_HOME_BUTTON)) {
          return requested_state_arm_home_button;
        } else if (bitmask & (1ULL << PIN_ARM_AWAY_BUTTON)) {
          return requested_state_arm_away_button;
        } else {
          Serial.println("Woken up by unknown pin");
          return requested_state_unknown;
        }
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

void update_state_leds(state_t state, boolean force=false)
{
  int now = millis();

  if (now -lastBlinkMillis < 200 && !force) {
    return;
  }
  lastBlinkMillis = now;

  lastBlinkState = 1 - lastBlinkState;
  
  int led_disarmed = 0;
  int led_armed_home = 0;
  int led_armed_away = 0;

  switch(state) {
    case state_unknown:
      if (lastBlinkState) led_disarmed = led_armed_home = led_armed_away = 1;
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
    if (connectMqtt())
    {
      Serial.println("MQTT connected");
      needMqttConnect = false;
    }
  }
  else if ((iotWebConf.getState() == IOTWEBCONF_STATE_ONLINE) && (!mqttClient.connected()))
  {
    Serial.println("MQTT reconnect");
    connectMqtt();
  }
}

#define MSG_BUFFER_SIZE 50

void ping_mqtt()
{
  unsigned long now = millis();
  if (mqttClient.connected() && now - mqttLastMsgTimestamp > 2000) {
    mqttLastMsgTimestamp = now;
    ++lastMsg;
    char msg[MSG_BUFFER_SIZE];
    snprintf (msg, MSG_BUFFER_SIZE, "hello world #%ld", lastMsg);
    Serial.print("Publish message: ");
    Serial.println(msg);
    mqttClient.publish(mqtt_action_channel, msg);
  }
}

void loop()
{
  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
  mqttClient.loop();

  reconnect_mqtt();
  ping_mqtt();

  handle_nfc();
  update_requested_state_from_buttons();
  update_state_leds(current_state);

  if (mqtt_message_to_send[0] != 0 && mqttClient.connected())
  {    
    Serial.printf("Publishing %s to %s\n", mqtt_message_to_send, mqtt_action_channel); 
    mqttClient.publish(mqtt_action_channel, mqtt_message_to_send);
    mqtt_message_to_send[0] = 0;
  }

  if (millis() - last_state_change > 5000) {
    enter_deep_sleep();
  }

  delay(50);

  // TODO: sleep when nothing happens
  //    // Wait 1 second before continuing
  //  delay(1000);
  //  enter_deep_sleep();



}

void enter_deep_sleep()
{
  //Serial.println("Preparing for deep sleep:");
  //Serial.println("* Shutting down Wifi");

  // Serial.println("* Turning off PN532");
  // No use, led stays on anyways. Kill the power (using a FET?) instead!
  // pn532.shutDown(0x80, false); // 0x80: wake up on I2C

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

void handle_nfc()
{
  // Serial.println("\nPlease scan an NFC tag\n");
  boolean success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = pn532.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);

  if (success) {
    Serial.println("Found a card!");
    // Serial.print("UID Length: "); Serial.print(uidLength, DEC); Serial.println(" bytes");
    Serial.print("UID Value: ");
    for (uint8_t i = 0; i < uidLength; i++)
    {
      Serial.print(" 0x"); Serial.print(uid[i], HEX);
    }
    Serial.println("");

    int length = 0;
    char msg[256];

    length += snprintf(msg + length, 256 - length, requested_state_actions[requested_state]);
    length += snprintf(msg + length, 256 - length, " ");
    for (uint8_t i = 0; i < uidLength; i++)
    {
      length += snprintf(msg + length, 256 - length, "%02x", uid[i]);
    }

    strncpy(mqtt_message_to_send, msg, sizeof(mqtt_message_to_send));

    if (requested_state == requested_state_disarm_button) set_state(state_disarmed_pending);
    if (requested_state == requested_state_arm_home_button) set_state(state_armed_home_pending);
    if (requested_state == requested_state_arm_away_button) set_state(state_armed_away_pending);
  }
  wire.flush();
}

float get_battery_percentage()
{
  float average = 0;

  for (int i = 0; i < 10; ++i) {
    float VBATMV = (float)(analogRead(VBAT_PIN)) * 3600 / 4095 * 2;
    float VBATV = VBATMV / 1000;
    // 100% is 4.2V
    // each 1% down, 5.14mV = 0.00514V
    // so empty = 4.2V - 0.514V = 3.686V
    // According to spec: max 4.2V, min 2.75V, nominal 3.63V
    float battery_percentage = (VBATV - 3.686) / 0.514 * 100;

    average += battery_percentage / 10;
  }

  return average;
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
  Serial.println("Connected!");

  Serial.println("Subscribing to mqtt test/action");
  mqttClient.subscribe(mqtt_status_channel);

  Serial.println("Sending hello_world to mqtt test/status");
  mqttClient.publish(mqtt_action_channel, "Hello world from esp32");

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
  //float battery_percentage = get_battery_percentage();
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>IotWebConf based keypad</title></head><body><p>Hello world!</p>";
  //  s += "<ul>";
  //  s += "<li>String param value: ";
  //  s += stringParamValue;
  //  s += "<li>Int param value: ";
  //  s += atoi(intParamValue);
  //  s += "<li>Float param value: ";
  //  s += atof(floatParamValue);
  //  s += "</ul>";
  s += "<p>Go to <a href='config'>configure page</a> to change values.</p>";
  //  s += "</p>Battery level: ";
  //  s += battery_percentage;
  //  s += "%</p>";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
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
  
  if (strncmp("DISARMED", (char*) payload, length) == 0) {
    set_state(state_disarmed);
  } else if (strncmp("ARMED_HOME", (char*) payload, length) == 0) {
    set_state(state_armed_home);
  } else if (strncmp("ARMED_AWAY", (char*) payload, length) == 0) {
    set_state(state_armed_away);
  } else {
    Serial.println("Received unknown state from mqtt!");
  }
}
