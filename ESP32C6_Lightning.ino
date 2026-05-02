#include <Arduino.h>
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
// +-----+--------+
#define ENABLE_DBG
#include <DFRobot_AS3935_I2C.h>
#define LIGHTING_IRQ 2
#define LIGHTING_ADDRESS AS3935_ADD3
#define I2C_SDA 21
#define I2C_SCL 22

DFRobot_AS3935_I2C lightning(LIGHTING_IRQ);

#include "LightningMQTT.hpp"
LightningMQTT lightningMQTT(lightning, mqtt);

void setup()
{
  Serial.begin(115200);
  setupNetwork();
  setupMqtt();
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

void setupLightning() {
  Wire.setPins(I2C_SDA, I2C_SCL);
  lightning.setI2CAddress(LIGHTING_ADDRESS);

  while (lightning.begin() != 0) {
    delay(10);
  }
  lightning.defInit();
  lightning.powerUp();
}

void loop() {
  delay(10);
  
  uint8_t interruptSource = lightning.getInterruptSrc();
  if (interruptSource == 0) {
    return;
  } else if (interruptSource == 1) {
    uint8_t distance = lightning.getLightningDistKm();
    uint32_t energyIntensity = lightning.getStrikeEnergyRaw();
    Serial.printf("Lightning detected! distance=%d km, energyIntensity=%d\n", distance, energyIntensity);
    lightningMQTT.publishLightningDetected(distance, energyIntensity);
  } else if (interruptSource == 2) {
    Serial.printf("Disturber discovered!\n");
    lightningMQTT.publishDisturberDiscovered();
  } else if (interruptSource == 3) {
    Serial.printf("Noise level too high!\n");
    lightningMQTT.publishNoiseLevelTooHigh();
  }
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

