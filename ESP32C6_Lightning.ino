#include <Arduino.h>
#include <LittleFS.h>
#include "Secrets.hpp"

// Netowrk
#include <WiFi.h>
#include <ESPmDNS.h>
#define MDNS_HOST "ESP32C6-Lightning"

// MQTT
#include <ESP32MQTTClient.h>
#include <ArduinoJson.h>
#define MQTT_URI "mqtt://homeassistant.local:1883"

ESP32MQTTClient mqtt;

// DFRobot Lightning Sensor
// +-----+--------+
// | PIN | ESP32  |
// +-----+--------+
// | +   | 3V3    |
// | -   | GND    |
// | C   | GPIO22 | blue
// | D   | GPIO21 | green
// | IRQ | GPIO2  | dark green
// +-----+--------+
#define ENABLE_DBG
#include <DFRobot_AS3935_I2C.h>
#define LIGHTING_IRQ 2
#define LIGHTING_ADDRESS AS3935_ADD3
#define I2C_SDA 21
#define I2C_SCL 22

DFRobot_AS3935_I2C lightning(LIGHTING_IRQ, AS3935_ADD3);

#include "LightningMQTT.hpp"
LightningMQTT lightningMQTT(lightning, mqtt);

volatile bool isr_triggered = false;

#define CSV_FILENAME "/Lightning.csv"
struct tm now;
char time_str[32];

void IRAM_ATTR AS3935_ISR() {
  isr_triggered = true;
}

void setup() {
  Serial.begin(115200);
  setupNetwork();
  setupMqtt();
  setupStorage();
  setupLightning();
}

void setupNetwork() {
  Serial.println("Connecting to WiFi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWD);
  WiFi.setHostname("ESP32C6_Lightning");
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  Serial.print("WiFi connected. IP address: ");
  Serial.print(WiFi.localIP());
  Serial.print(", RSSI: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
  
  if (MDNS.begin(MDNS_HOST)) {
    Serial.printf("mDNS responder started: %s.local\n", MDNS_HOST);
  }
}

void setupMqtt() {
  mqtt.setURI(MQTT_URI, MQTT_USERNAME, MQTT_PASSWORD);
  mqtt.loopStart();
}

void setupStorage() {
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS failed");
    return;
  }
  Serial.printf("FS used: %ld, total: %ld\n", LittleFS.usedBytes(), LittleFS.totalBytes());
  
  configTime(3600, 3600, "pool.ntp.org");
  while (!getLocalTime(&now)) {
    Serial.println("Waiting for NTP...");
    delay(1000);
  }

  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &now);
  Serial.printf("Local time now: %s\n", time_str);

  createCsvIfNeeded();
}

void createCsvIfNeeded() {
  if (!LittleFS.exists(CSV_FILENAME)) {
    Serial.printf("File %s doesn't exists\n", CSV_FILENAME);
    File file = LittleFS.open(CSV_FILENAME, FILE_WRITE, true);
    if (!file) {
      Serial.println("Cannot create file!");
      return;
    }
    file.println("epoch,time,dist_km,energy_intensity");
    file.close();
  } else {
    Serial.printf("File %s exists\n", CSV_FILENAME);
  }
}

void setupLightning() {
  Wire.setPins(I2C_SDA, I2C_SCL);

  attachInterrupt(digitalPinToInterrupt(LIGHTING_IRQ), AS3935_ISR, RISING);

  while (lightning.begin() != 0) {
    delay(10);
  }
  lightning.defInit();
  lightning.powerUp();
}

void loop() {
  handleSerialInput();
  handleLightning();
  delay(10);
}

void handleSerialInput() {
  if (Serial.available() == 0) {
    return;
  }

  String cmd = Serial.readString();
  Serial.printf("Recevied cmd: %s\n", cmd);

  if (cmd.equals("read")) {
    File file = LittleFS.open(CSV_FILENAME, FILE_READ);
    String content = file.readString();
    file.close();

    Serial.printf("%s:\n", CSV_FILENAME);
    Serial.println(content);
  } else if (cmd.equals("clear")) {
    LittleFS.remove(CSV_FILENAME);
    createCsvIfNeeded();
  }
}

void handleLightning() {
  if (!isr_triggered) {
    return;
  }
  isr_triggered = false;
  delay(10);

  uint8_t interruptSource = lightning.getInterruptSrc();
  if (interruptSource == 0) {
    return;
  } else if (interruptSource == 1) {
    uint8_t distance = lightning.getLightningDistKm();
    uint32_t energyIntensity = lightning.getStrikeEnergyRaw();

    Serial.printf("Lightning detected! time=%ld, distance=%u km, energyIntensity=%lu\n", now, distance, energyIntensity);
    saveLightingToCSV(distance, energyIntensity);
    lightningMQTT.publishLightningDetected(distance, energyIntensity);
  } else if (interruptSource == 2) {
    Serial.printf("Disturber discovered!\n");
    lightningMQTT.publishDisturberDiscovered();
  } else if (interruptSource == 3) {
    Serial.printf("Noise level too high!\n");
    lightningMQTT.publishNoiseLevelTooHigh();
  }
}

void saveLightingToCSV(uint8_t distance, uint32_t energyIntensity) {
  if (!getLocalTime(&now)) {
    Serial.println("getLocalTime Failed");
    return;
  }
  strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &now);

  time_t epoch;
  time(&epoch);
    
  File file = LittleFS.open(CSV_FILENAME, FILE_APPEND);
  if (!file) {
    Serial.println("Cannot open file!");
    return;
  }

  file.printf("%lld,%s,%u,%lu\n", epoch, time_str, distance, energyIntensity);
  file.close();
}

void onMqttConnect(esp_mqtt_client_handle_t client) {
  Serial.println("onMqttConnect");
  if (mqtt.isMyTurn(client)) {
      lightningMQTT.handleConnected();
  }
}

void handleMQTT(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
    mqtt.onEventCallback(event);
}
