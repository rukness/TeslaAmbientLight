#include <EEPROM.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>

#include "Car.h"
#include "CarLight.h"
#include "DoorLight.h"
#include "FootLight.h"
#include "MirrorLight.h"

// Board targets:
//   Master  (Role 0):   ESP32 original + 2x MCP2515, or Autosport S3 board (TWAI + MCP2515)
//   Door modules (Role 1-4): ESP32-S3 Mini (3 RMT channels for door/mirror/pocket strips)

// --- LED pin assignments per chip target ---
#if defined(CONFIG_IDF_TARGET_ESP32S3)
const int pocketLedPin = 10;
const int mirrorPin = 11;
const int ledPin = 12;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
const int pocketLedPin = 2;
const int mirrorPin = 4;
const int ledPin = 3;
#else // Original ESP32
const int pocketLedPin = 14;
const int mirrorPin = 15;
const int ledPin = 12;
#endif

const bool externalWifi = false;
const char* ssid = "T3LIGHT";
const char* password = "12345678";
const char* passwordOTA = "12345678";

// --- Master CAN + footwell pins ---
// Set to 1 for Autosport ESP32-S3 board (uses TWAI for VCAN + MCP2515 for CCAN)
#define AUTOSPORT_BOARD 0

#if AUTOSPORT_BOARD
const int vCanPin = 0;
const int cCanPin = 10;
const bool asBoard = true;
const int vCanRxPin = 6;
const int vCanTxPin = 7;
const int leftFootwellPin = 4;
const int rightFootwellPin = 5;
const int leftFootwellHLPin = 8;
const int rightFootwellHLPin = 9;
#else
const bool asBoard = false;
const int vCanPin = 5;
const int cCanPin = 13;
const int vCanRxPin = 0;
const int vCanTxPin = 0;
const int leftFootwellPin = 26;
const int rightFootwellPin = 27;
const int leftFootwellHLPin = 32;
const int rightFootwellHLPin = 33;
#endif

unsigned char role = DOOR_MASTER;  // DOOR_FRONT_RIGHT, DOOR_FRON_LEFT, DOOR_REAR_RIGHT, DOOR_REAR_LEFT
const bool saveRoleToEEPROM = false;
const bool readRoleFromEEPROM = true;

Car car;
CarLight carLight;

FootLight leftFootLight;
FootLight rightFootLight;

DoorLight doorLight;
MirrorLight mirrorLight;

void setup() {
  car.openFrunkWithDoor = true;

  EEPROM.begin(8);
  if (saveRoleToEEPROM) {
    EEPROM.write(0, role);
    EEPROM.commit();
  }
  if (readRoleFromEEPROM)
    role = byte(EEPROM.read(0));

  Serial.begin(921600);
  Serial.println();
  Serial.print("Role: ");
  Serial.println(role);

  if (role == 0) {
    if (asBoard)
      car.initAS(cCanPin, vCanRxPin, vCanTxPin);
    else
      car.init(vCanPin, cCanPin);
    leftFootLight.initHL(1, leftFootwellHLPin);
    rightFootLight.initHL(2, rightFootwellHLPin);
    //leftFootLight.initRGB(1, leftFootwellPin);
    //rightFootLight.initRGB(2, rightFootwellPin);
    if (externalWifi) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(ssid, password);
      while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Connection Failed! Rebooting...");
        delay(1000);
        ESP.restart();
      }
      Serial.println("Connected");
    } else {
      IPAddress local_IP(192, 168, 4, 10);
      IPAddress subnet(255, 255, 255, 0);
      IPAddress gateway(192, 168, 4, 1);
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ssid, password, 1, 0, 10, false);
      WiFi.softAPConfig(local_IP, gateway, subnet);
      Serial.println("WIFI started");
    }
  } else {
    IPAddress local_IP(192, 168, 4, 10 + role);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress gateway(192, 168, 4, 1);
    if (!WiFi.config(local_IP, gateway, subnet)) {
      Serial.println("Configuration Failed!");
    }
    doorLight.init(role - 1, ledPin, pocketLedPin);
    mirrorLight.init(role - 1, mirrorPin);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password, 1);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("Connection Failed! Rebooting...");
      delay(1000);
      ESP.restart();
    }
    Serial.println("Connected");
  }

  carLight.init();
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else
      type = "filesystem";
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.setPassword(passwordOTA);
  ArduinoOTA.begin();
}

unsigned long lastLightUpdate = 0;

void loop() {
  ArduinoOTA.handle();
  if (role == 0) {
    car.process();
    unsigned long now = millis();
    if (now - lastLightUpdate >= 10) {
      lastLightUpdate = now;
      carLight.processCarState(car);
      carLight.sendLightState();
      leftFootLight.setColorByCarState(carLight);
      rightFootLight.setColorByCarState(carLight);
    }
  } else {
    carLight.receiveLightState();
    doorLight.setColorByCarState(carLight);
    mirrorLight.setColorByCarState(carLight);
    yield();
  }
}
