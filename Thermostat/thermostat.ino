// Include the libraries we need
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <stdlib.h>
#include <Ticker.h>

//Global variables
Ticker fanTicker;
Ticker furnaceTicker;

//WiFi settings
char ssid[] = "parulnet2";
char pass[] = "*******";

void callback(char* topic, byte* payload, unsigned int length);

WiFiClient espClient;
PubSubClient client("m10.cloudmqtt.com", 14729, callback, espClient);

//Thermostat contol globals
#define SYSTEM_MODE_OFF 0
#define SYSTEM_MODE_HEAT 1
#define SYSTEM_MODE_COOL 2
#define FAN_MODE_AUTO 0
#define FAN_MODE_ON 1
#define PIN_FAN D2
#define PIN_HEAT D3
#define PIN_COOL D4

#define ON LOW
#define OFF HIGH

#define MAGIC 2458

#define TEMPERATURE_SAMPLE_RATE 1

struct Config {
  int magic;
  int systemMode;
  int fanMode;
  float heatSetPoint;
  float coolSetPoint;
  int furnaceOnTime;
  int furnaceOffTime;
} cfg;

#define SYSTEM_STATE_RUNNING 0
#define SYSTEM_STATE_REACHED 1

struct State {
  float temperature;
  int autoFan;
  int system;
} state;

// Data wire is plugged into port 2 on the WEMOS D1 Mini
#define ONE_WIRE_BUS D1

// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);

// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

//Array to hold device address
DeviceAddress insideThermometer;

void autoFanOn() {
    fanTicker.detach();
    digitalWrite(PIN_FAN, ON);
    state.autoFan = ON;
}

void autoFanOff() {
  if (cfg.fanMode == FAN_MODE_AUTO) { digitalWrite(PIN_FAN, OFF); }
  state.autoFan = OFF;
}

void userFanOn() {
  digitalWrite(PIN_FAN, ON);
}

void userFanOff() {
  if (state.autoFan == OFF) { digitalWrite(PIN_FAN, OFF); }
}

void furnaceOn(bool startFan) {
  digitalWrite(PIN_HEAT, ON);
  furnaceTicker.once(cfg.furnaceOnTime, furnaceOff, true);
  if (startFan) {
    state.system = SYSTEM_STATE_RUNNING;
    fanTicker.once(30, autoFanOn);
  }
}

void furnaceOff(bool resumeAfterBreak) {
  digitalWrite(PIN_HEAT, OFF);
  if (resumeAfterBreak) { furnaceTicker.once(cfg.furnaceOffTime, furnaceOn, false); }
  else { 
    fanTicker.once(120, autoFanOff);
    furnaceTicker.detach();
    state.system = SYSTEM_STATE_REACHED;
  }
}

void coolingOn() {
  autoFanOn();
  digitalWrite(PIN_COOL, ON);
  state.system = SYSTEM_STATE_RUNNING;
}

void coolingOff() {
  digitalWrite(PIN_COOL, OFF);
  state.system = SYSTEM_STATE_REACHED;
  fanTicker.once(120, autoFanOff);
}

void setup(void)
{
   Serial.begin(9600);

  //Read configuration
  retrieveConfig();
  if (cfg.magic != MAGIC) {
    //Config is bad or not initialized, so let us initialize it
    Serial.print("Initializing configuration ... ");
    cfg.magic = MAGIC;
    cfg.systemMode = SYSTEM_MODE_OFF;
    cfg.fanMode = FAN_MODE_AUTO;
    cfg.heatSetPoint = 77.0;
    cfg.coolSetPoint = 85.0;
    cfg.furnaceOnTime = 45;
    cfg.furnaceOffTime = 60;
    storeConfig();
    Serial.println("done");
  }
  //Set pin modes
  pinMode(PIN_FAN, OUTPUT);
  pinMode(PIN_HEAT, OUTPUT);
  pinMode(PIN_COOL, OUTPUT);

  //Reset relays
  digitalWrite(PIN_FAN, OFF);
  digitalWrite(PIN_HEAT, OFF);
  digitalWrite(PIN_COOL, OFF);
      
  //System state
  state.system = SYSTEM_STATE_REACHED;
  
  //Auto Fan should be off
  autoFanOff();
  
  //Turn on fan if fan mode is configured to be ON
  if (cfg.fanMode == FAN_MODE_ON) { userFanOn(); }
  
  // locate temperature sensor devices on the bus
  Serial.print("Locating temperature sensor devices... ");
  sensors.begin();
  Serial.print("found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");

  if (!sensors.getAddress(insideThermometer, 0)) Serial.println("Unable to find address for Device 0"); 
  
  // set the resolution
  sensors.setResolution(insideThermometer, 12);
  
  //Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi ... ");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }
  Serial.println("done");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

long lastReconnectAttempt = 0;
boolean reconnect() {
  if (client.connect("*********", "*********", "*********")) {
    client.subscribe("/eagleschase/thermostat/setHeatSetPoint");
    client.subscribe("/eagleschase/thermostat/setCoolSetPoint");
    client.subscribe("/eagleschase/thermostat/setSystemMode");
    client.subscribe("/eagleschase/thermostat/setFanMode");
    client.subscribe("/eagleschase/thermostat/setFurnaceOnTime");
    client.subscribe("/eagleschase/thermostat/setFurnaceOffTime");
  }
  return client.connected();
}

float prevTemp = 0.0;

void loop(void)
{
  //Read temperature
  sensors.requestTemperatures();
  state.temperature = sensors.getTempF(insideThermometer);

  if (!client.connected()) {
    long now = millis();
    if (now - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect()) { lastReconnectAttempt = 0; }
    }
  } else {
    //We are online!
    client.loop();
    if (prevTemp != state.temperature) {
      char tempStr[10];
      dtostrf(state.temperature, 2, 3, tempStr);  //2 is mininum width, 3 is precision
      client.publish("/eagleschase/temperature", tempStr, true);
      prevTemp = state.temperature;
    }
  }

  if (cfg.systemMode == SYSTEM_MODE_HEAT) {
    if (state.system == SYSTEM_STATE_REACHED && state.temperature <= cfg.heatSetPoint - 0.25) { furnaceOn(true); }
    else if (state.system == SYSTEM_STATE_RUNNING && state.temperature >= cfg.heatSetPoint + 0.25) { furnaceOff(false); }
  }
  else if (cfg.systemMode == SYSTEM_MODE_COOL) {
    if (state.system == SYSTEM_STATE_RUNNING && state.temperature <= cfg.coolSetPoint - 0.25) {
      coolingOff();
    }
    else if (state.system == SYSTEM_STATE_REACHED && state.temperature >= cfg.coolSetPoint + 0.25) {
      coolingOn();
    }
  }
  delay(1000);
}

void storeConfig() {
  EEPROM.begin(512);
  EEPROM.put(0, cfg);
  EEPROM.end();
}

void retrieveConfig() {
  EEPROM.begin(512);
  EEPROM.get(0, cfg);
  EEPROM.end();
}

void callback(char* topic, byte* payload, unsigned int length) {
  //Serial.print("Message arrived [");
  //Serial.print(topic);
  //Serial.print("] ");
  char message[length + 1];
  for (int i=0;i<length;i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';
  if (strcmp(topic, "/eagleschase/thermostat/setHeatSetPoint") == 0) {
    float setPoint = atof(message);
    if (cfg.heatSetPoint != setPoint) {
      cfg.heatSetPoint = setPoint;
      storeConfig();
    }
  }
  else if (strcmp(topic, "/eagleschase/thermostat/setCoolSetPoint") == 0) {
    float setPoint = atof(message);
    if (cfg.coolSetPoint != setPoint) {
      cfg.coolSetPoint = setPoint;
      storeConfig();
    }
  }
  else if (strcmp(topic, "/eagleschase/thermostat/setSystemMode") == 0) {
    if (strcmp(message, "HEAT") == 0 && cfg.systemMode != SYSTEM_MODE_HEAT) {
      cfg.systemMode = SYSTEM_MODE_OFF;
      coolingOff();
      cfg.systemMode = SYSTEM_MODE_HEAT;
      storeConfig();
    }
    else if (strcmp(message, "COOL") == 0 && cfg.systemMode != SYSTEM_MODE_COOL) {
      cfg.systemMode = SYSTEM_MODE_OFF;
      furnaceOff(false);
      cfg.systemMode = SYSTEM_MODE_COOL;
      storeConfig();
    }
    else if (strcmp(message, "OFF") == 0 && cfg.systemMode != SYSTEM_MODE_OFF) {
      cfg.systemMode = SYSTEM_MODE_OFF;
      coolingOff();
      furnaceOff(false);
      storeConfig();
    }
  }
  else if (strcmp(topic, "/eagleschase/thermostat/setFanMode") == 0) {
    if (strcmp(message, "ON") == 0) {
      cfg.fanMode = FAN_MODE_ON;
      userFanOn();
      storeConfig();
    }
    else if (strcmp(message, "AUTO") == 0) {
      cfg.fanMode = FAN_MODE_AUTO;
      userFanOff();
      storeConfig();
    }
  }
  else if (strcmp(topic, "/eagleschase/thermostat/setFurnaceOnTime") == 0) {
    int t = atoi(message);
    if (t != cfg.furnaceOnTime) {
      cfg.furnaceOnTime = t;
      storeConfig();
      Serial.print("Setting furnace on time to ");
      Serial.println(t);

    }
  }
  else if (strcmp(topic, "/eagleschase/thermostat/setFurnaceOffTime") == 0) {
    int t = atoi(message);
    if (t != cfg.furnaceOffTime) {
      cfg.furnaceOffTime = t;
      storeConfig();
      Serial.print("Setting furnace off time to ");
      Serial.println(t);
    }
  }
  
  //Serial.println(message);
}
