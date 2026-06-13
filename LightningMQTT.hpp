#include <DFRobot_AS3935_I2C.h>
#include <ESP32MQTTClient.h>
#include <ArduinoJson.h>
#include <string>

class LightningMQTT {
private:
  DFRobot_AS3935_I2C &lightning;
  ESP32MQTTClient &mqtt;

  struct Config {
    bool disturberDetection = false;
    uint8_t noiseFloorLevel = 2;
    uint8_t watchdogThreshold = 2;
    uint8_t spikeRejection = 2;
    bool indoor = true;
  } config;

  struct State {
    uint8_t distance;
    uint32_t energy;
    uint32_t lightningDetectedCount;
    uint32_t disturberDiscoveredCount;
    uint32_t noiseLevelTooHighCount;
  } state;

public:
  LightningMQTT(DFRobot_AS3935_I2C &lightning, ESP32MQTTClient &mqtt)
    : lightning(lightning),
      mqtt(mqtt) {
    }

  void handleConnected() {
    mqtt.subscribe("home/lightning/config/#", [&](const std::string &topic, const std::string &message) {
      onConfigMessage(topic, message);
    });

    state = State();
    applyConfig();
    publishConfig();
    
    publishMQTTDiscovery();
    publishState();
    mqtt.publish("home/lightning/status", "online", 0, true);
  }

  void publishLightningDetected(uint8_t distance, uint32_t energyIntensity) {
    JsonDocument event;
    event["event_type"] = "lightningDetected";
    event["distance"] = distance;
    event["energy"] = energyIntensity;
    mqtt.publish("home/lightning/event", event.as<std::string>(), 0, false);

    state.distance = distance;
    state.energy = energyIntensity;
    state.lightningDetectedCount++;
    publishState();
  }

  void publishDisturberDiscovered() {
    JsonDocument event;
    event["event_type"] = "disturberDiscovered";
    mqtt.publish("home/lightning/event", event.as<std::string>(), 0, false);

    state.disturberDiscoveredCount++;
    publishState();
  }

  void publishNoiseLevelTooHigh() {
    JsonDocument event;
    event["event_type"] = "noiseLevelTooHigh";
    mqtt.publish("home/lightning/event", event.as<std::string>(), 0, false);

    state.noiseLevelTooHighCount++;
    publishState();
  }
  
private:
  void publishMQTTDiscovery() {
    JsonDocument json;

    auto dev = json["device"].to<JsonObject>();
    dev["identifiers"][0] = "AS3935";
    dev["name"] = "Lightning Sensor";
    dev["model"] = "AS3935 Lightning Sensor";
    dev["manufacturer"] = "DFRobot";

    auto origin = json["origin"].to<JsonObject>();
    origin["name"] = "ESP32C6_Lightning";

    // Config

    auto disturber = json["components"]["disturberDetection"].to<JsonObject>();
    disturber["platform"] = "switch";
    disturber["name"] = "Disturber detection";
    disturber["command_topic"] = "home/lightning/config/disturberDetection";
    disturber["state_topic"] = "home/lightning/config";
    disturber["payload_on"] = "ON";
    disturber["payload_off"] = "OFF";
    disturber["entity_category"] = "config";
    disturber["value_template"] = "{{ value_json.disturberDetection }}";
    disturber["unique_id"] = "AS3935_disturber";

    auto nf = json["components"]["noiseFloorLevel"].to<JsonObject>();
    nf["platform"] = "number";
    nf["name"] = "Noise Floor";
    nf["command_topic"] = "home/lightning/config/noiseFloorLevel";
    nf["state_topic"] = "home/lightning/config";
    nf["value_template"] = "{{ value_json.noiseFloorLevel }}";
    nf["min"] = 0;
    nf["max"] = 7;
    nf["entity_category"] = "config";
    nf["unique_id"] = "AS3935_noise";

    auto wd = json["components"]["watchdogThreshold"].to<JsonObject>();
    wd["platform"] = "number";
    wd["name"] = "Watchdog Threshold";
    wd["command_topic"] = "home/lightning/config/watchdogThreshold";
    wd["state_topic"] = "home/lightning/config";
    wd["value_template"] = "{{ value_json.watchdogThreshold }}";
    wd["min"] = 0;
    wd["max"] = 7;
    wd["entity_category"] = "config";
    wd["unique_id"] = "AS3935_watchdog";

    auto sp = json["components"]["spikeRejection"].to<JsonObject>();
    sp["platform"] = "number";
    sp["name"] = "Spike Rejection";
    sp["command_topic"] = "home/lightning/config/spikeRejection";
    sp["state_topic"] = "home/lightning/config";
    sp["value_template"] = "{{ value_json.spikeRejection }}";
    sp["min"] = 0;
    sp["max"] = 7;
    sp["entity_category"] = "config";
    sp["unique_id"] = "AS3935_spike";

    auto mode = json["components"]["mode"].to<JsonObject>();
    mode["platform"] = "select";
    mode["name"] = "Sensor Mode";
    mode["command_topic"] = "home/lightning/config/mode";
    mode["state_topic"] = "home/lightning/config";
    mode["value_template"] = "{{ value_json.mode }}";
    mode["options"][0] = "INDOOR";
    mode["options"][1] = "OUTDOOR";
    mode["entity_category"] = "config";
    mode["unique_id"] = "AS3935_mode";

    // Dignostic

    auto dist = json["components"]["distance"].to<JsonObject>();
    dist["platform"] = "sensor";
    dist["name"] = "Lightning Distance";
    dist["value_template"] = "{{ value_json.distance }}";
    dist["unit_of_measurement"] = "km";
    dist["device_class"] = "distance";
    dist["state_class"] = "measurement";
    dist["entity_category"] = "diagnostic";
    dist["unique_id"] = "AS3935_distance";

    auto energy = json["components"]["energy"].to<JsonObject>();
    energy["platform"] = "sensor";
    energy["name"] = "Lightning Energy";
    energy["value_template"] = "{{ value_json.energy }}";
    energy["state_class"] = "measurement";
    energy["entity_category"] = "diagnostic";
    energy["unique_id"] = "AS3935_energy";

    auto lcnt = json["components"]["lightning_count"].to<JsonObject>();
    lcnt["platform"] = "sensor";
    lcnt["name"] = "Lightning Count";
    lcnt["value_template"] = "{{ value_json.lightningDetectedCount }}";
    lcnt["state_class"] = "total_increasing";
    lcnt["entity_category"] = "diagnostic";
    lcnt["unique_id"] = "AS3935_lightning_count";

    auto dcnt = json["components"]["disturber_count"].to<JsonObject>();
    dcnt["platform"] = "sensor";
    dcnt["name"] = "Disturber Count";
    dcnt["value_template"] = "{{ value_json.disturberDiscoveredCount }}";
    dcnt["state_class"] = "total_increasing";
    dcnt["entity_category"] = "diagnostic";
    dcnt["unique_id"] = "AS3935_disturber_count";

    auto ncnt = json["components"]["noise_count"].to<JsonObject>();
    ncnt["platform"] = "sensor";
    ncnt["name"] = "Noise High Count";
    ncnt["value_template"] = "{{ value_json.noiseLevelTooHighCount }}";
    ncnt["state_class"] = "total_increasing";
    ncnt["entity_category"] = "diagnostic";
    ncnt["unique_id"] = "AS3935_noise_count";

    // Event
    auto evt = json["components"]["lightning_event"].to<JsonObject>();
    evt["platform"] = "event";
    evt["name"] = "Lightning Event";
    evt["state_topic"] = "home/lightning/event";
    evt["event_types"][0] = "lightningDetected";
    evt["event_types"][1] = "disturberDiscovered";
    evt["event_types"][2] = "noiseLevelTooHigh";
    evt["unique_id"] = "AS3935_event_v2";

    json["state_topic"] = "home/lightning/state";
    json["availability_topic"] = "home/lightning/status";
    json["qos"] = 2;
    
    mqtt.publish("homeassistant/device/DFRobot_AS3935/config", json.as<std::string>(), 0, true);
  }

  void onConfigMessage(const std::string &topic, const std::string &message) {
    Serial.printf("onConfigMessage: topic=%s, message=%s\n", topic.c_str(), message.c_str());
    updateConfig(topic, message);
    applyConfig();
    publishConfig();
  }

  void updateConfig(const std::string &topic, const std::string &message) {
    if (topic == "home/lightning/config/noiseFloorLevel") {
      config.noiseFloorLevel = std::stoi(message);
    } else if (topic == "home/lightning/config/watchdogThreshold") {
      config.watchdogThreshold = std::stoi(message);
    } else if (topic == "home/lightning/config/spikeRejection") {
      config.spikeRejection = std::stoi(message);
    } else if (topic == "home/lightning/config/mode") {
      config.indoor = (message == "INDOOR");
    } else if (topic == "home/lightning/config/disturberDetection") {
      config.disturberDetection = (message == "ON");
    } else {
      Serial.printf("updateConfig: unknown config, topic: %s, message: %s\n", topic.c_str(), message.c_str());
    }
  }

  void applyConfig() {
    config.disturberDetection ? lightning.disturberEn() : lightning.disturberDis();
    config.indoor ? lightning.setIndoors() : lightning.setOutdoors();

    lightning.setNoiseFloorLvl(config.noiseFloorLevel);
    config.noiseFloorLevel = lightning.getNoiseFloorLvl();

    lightning.setWatchdogThreshold(config.watchdogThreshold);
    config.watchdogThreshold = lightning.getWatchdogThreshold();

    lightning.setSpikeRejection(config.spikeRejection);
    config.spikeRejection = lightning.getSpikeRejection();

    Serial.printf(
      "applyConfig: disturberDetection=%s, mode=%s, noiseFloorLevel=%d, watchdogThreshold=%d, spikeRejection=%d\n",
      config.disturberDetection ? "ON" : "OFF",
      config.indoor ? "INDOOR" : "OUTDOOR",
      config.noiseFloorLevel,
      config.watchdogThreshold,
      config.spikeRejection
    );
  }

  void publishConfig() {
    JsonDocument json;
    json["disturberDetection"] = config.disturberDetection ? "ON" : "OFF";
    json["noiseFloorLevel"] = config.noiseFloorLevel;
    json["watchdogThreshold"] = config.watchdogThreshold;
    json["spikeRejection"] = config.spikeRejection;
    json["mode"] = config.indoor ? "INDOOR" : "OUTDOOR";
    mqtt.publish("home/lightning/config", json.as<std::string>(), 0, true);
  }

  void publishState() {
    JsonDocument json;
    json["distance"] = state.distance;
    json["energy"] = state.energy;
    json["lightningDetectedCount"] = state.lightningDetectedCount;
    json["disturberDiscoveredCount"] = state.disturberDiscoveredCount;
    json["noiseLevelTooHighCount"] = state.noiseLevelTooHighCount;
    mqtt.publish("home/lightning/state", json.as<std::string>(), 0, true);
  }
};