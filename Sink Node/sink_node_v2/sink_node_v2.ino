#include <SPI.h>
#include <LoRa.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Arduino_JSON.h>

#define SS 25
#define DIO 26
#define RST 27

// #define MQTT_SERVER "ff2efa61874b4e338caf837fc41443b3.s1.eu.hivemq.cloud"
// #define MQTT_USERNAME "cobamqtt"
// #define MQTT_PASSWORD "cobaMQTT123"
#define MQTT_SERVER "61b617dcb74f48908006ec3ab8840e52.s1.eu.hivemq.cloud"
#define MQTT_USERNAME "smartgreenhouse"
#define MQTT_PASSWORD "SmartGreenhouse123"
#define MQTT_PORT 8883
#define CLIENT_ID "gh01"
#define SSID "R"
#define PASSWORD "ripan123"
#define NODE_ID 255

#define TOPIC_TO_SUBSCRIBE "gh01/#"

#define RELAY_PIN_1 32
#define RELAY_PIN_2 4

LiquidCrystal_I2C display(0x27, 16, 2);
WiFiClientSecure wifiClientSecure;
PubSubClient mqttClient;

struct PayloadData {
  int8_t nodeId;
  int16_t soilMoisture;
  int16_t soilTemperature;
  int16_t conductivity;
  int16_t soilPh;
  int16_t nitrogen;
  int16_t phosporus;
  int16_t kalium;
  int16_t airTemperature;
  int16_t airHumidity;
  int16_t lightIntensity;
} __attribute__((packed));

struct PayloadData payload;

unsigned long lastDisplayMillis = 0;
int lcdPage = 0;
bool hasNewPacket = false;

int8_t nodeId = 0;
int16_t n = 0, p = 0, k = 0, ec = 0;
float moistS = 0.0, tempS = 0.0, phS = 0.0, tempA = 0.0, humA = 0.0;
uint16_t lux = 0;
JSONVar payloadToPublish;

long int currentMillis = millis();
long int lastMQTTAttempt = 0;

unsigned long lastWaterPumpActivate = 0;
unsigned long lastFanActivate = 0;
unsigned long lastActuatorMessage = 0;
boolean waterPumpState = 1;
boolean fanState = 1;
boolean lastFanState = 1;
boolean lastWaterPumpState = 1;
boolean isManual = false;


unsigned long lastStatusWaterPumpMQTTSent= 0;
unsigned long lastStatusFanMQTTSent= 0;
unsigned long lastManualActive = 0;

void showMessage(int x, int y, const char* message);
void connectWifi();
boolean connectMQTT();
void reconnectMQTT();
void initializeSubscribe();
void parseLoRaPacket(int packetSize);
void loraCallback(int packetSize);
void setupLoRa();
void setupRelay();
void mqttCallback(const char* topic, byte* payload, int length);
void controlActuator(const char*topic, byte* payload);
void controlWaterPump(boolean status);
void controlFan(boolean status);
void sentStatusActuator(const char* topic);
void manageWaterPump();
void manageFan();

void setup() {
  Serial.begin(9600);
  setupRelay();
  lastWaterPumpActivate = millis();
  lastFanActivate = millis();

  display.init();
  display.backlight();
  showMessage(0, 0, "Initialize...");
  delay(2000);
  display.clear();
  setupLoRa();
  connectWifi();
  delay(2000);
  connectMQTT();
}

void loop() {
  currentMillis = millis();

  mqttClient.loop();

  if (!mqttClient.connected()) {
      reconnectMQTT();
  }

  if (hasNewPacket) {
    parseLoRaPacket(sizeof(payload));
  }

  if(isManual) {
    if(millis() - lastManualActive >= 15000) {
      controlWaterPump(1);
      controlFan(1);
      sentStatusActuator("waterpump");
      sentStatusActuator("fan");
      isManual = false;
    }
  }else {
    manageWaterPump();
    manageFan();
  }

  

  
  if(!fanState || !waterPumpState) {
    if(millis() - lastActuatorMessage >= 1000) {
      lastActuatorMessage = millis();
      display.clear();
      display.setCursor(0, 0);
      display.print("Actuator ON");
      String actuatorType = "";
      if(!fanState) {
        actuatorType += "Fan ";
      }
      if(!waterPumpState) {
        actuatorType += "Water ";
      }
      display.setCursor(0, 1);
      display.print((actuatorType + "ON").c_str());
    }
  } else if(!hasNewPacket) {
    lastDisplayMillis = currentMillis;
    display.setCursor(0,0);
    display.print("Menunggu paket...");
    display.setCursor(0,1);
    display.print((String("Node Id: ") + NODE_ID).c_str());
  }

  if(!waterPumpState && !isManual && (millis() - lastWaterPumpActivate >= 20000)) {
    controlWaterPump(1);
    sentStatusActuator("waterpump");
  }

  if(!fanState && !isManual && (millis() - lastFanActivate >= 20000)) {
    controlFan(1);
    sentStatusActuator("fan");
  }
}

void setupRelay() {
  pinMode(RELAY_PIN_1, OUTPUT);
  pinMode(RELAY_PIN_2, OUTPUT);

  digitalWrite(RELAY_PIN_1, HIGH);
  digitalWrite(RELAY_PIN_2, HIGH);

}

void setupLoRa() {
  LoRa.setPins(SS, RST, DIO);
  if (!LoRa.begin(923E6)) {
    showMessage(0, 0, "Lora Failed");
    while (1);
  }
  LoRa.setSpreadingFactor(7);
  showMessage(0, 0, "LoRa Connected");
  LoRa.onReceive(loraCallback);
  LoRa.receive();
  delay(2000);
}

void showMessage(int x, int y, const char* message) {
  display.setCursor(x, y);
  display.print(message);
  Serial.println(message);
}

void connectWifi() {
  WiFi.disconnect();
  delay(1000);
  WiFi.mode(WIFI_MODE_STA);
  showMessage(0, 0, "Connecting wifi...");
  WiFi.begin(SSID, PASSWORD);
  byte counter = 1;
  while (WiFi.status() != WL_CONNECTED) {
    showMessage(0, 0, "Connecting wifi...");
    showMessage(0, 1, (String("Attempt: ") + counter).c_str());
    if(counter == 12) {
      display.clear();
      showMessage(0, 0, "Wifi Failed");
    }
    counter++;
    delay(5000);
  }
  wifiClientSecure.setInsecure();
  display.clear();
  showMessage(0, 0, "Wifi Connected");
  showMessage(0, 1, (String("SSID: ") + SSID).c_str());
}

boolean connectMQTT() {
  display.clear();
  showMessage(0, 0, "Initialize mqtt...");
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setClient(wifiClientSecure);
  mqttClient.connect(CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
  mqttClient.setCallback(mqttCallback);
  if (!mqttClient.connected()) {
    showMessage(0, 0, "MQTT Failed...");
  }
  initializeSubscribe();
  display.clear();
  showMessage(0, 0, "MQTT Connected");
  return mqttClient.connected();
}

void reconnectMQTT() {
  if (millis() - lastMQTTAttempt >= 5000) {
    lastMQTTAttempt = millis();
    showMessage(0, 0, "Attempting MQTT...");
    connectMQTT();
  }
  if (!mqttClient.connected()) {
    showMessage(0, 0, "Re-MQTT failed...");
    const String mqttStatus = "Status: " + String(mqttClient.state());
    showMessage(0, 1, mqttStatus.c_str());
  }
}

void initializeSubscribe() {
  display.clear();

  if(!mqttClient.subscribe("gh01/node255/control/+")) {
    showMessage(0, 0, "Subscribe Failed");
    showMessage(0, 1, "Topic control");
    return;
  };

  if(!mqttClient.subscribe("gh01/node255/get/+")){
    showMessage(0, 0, "Subscribe Failed");
    showMessage(0, 1, "Topic status");
    return;
  }

  showMessage(0,0,"Subscribed Topik");
}

void parseLoRaPacket(int packetSize) {
  if (!packetSize) return;

  if (packetSize != sizeof(payload)) {
    display.clear();
    showMessage(0, 0, "Corrupt data..");
    hasNewPacket = false;
    LoRa.receive();
    return;
  }

  display.clear();
  showMessage(0, 0, "Packet detected");
  showMessage(0, 1, (String("Size: ") + packetSize).c_str());
  Serial.println(packetSize);

  nodeId = payload.nodeId;
  n      = payload.nitrogen;
  p      = payload.phosporus;
  k      = payload.kalium;
  moistS = payload.soilMoisture / 10.0;
  tempS  = payload.soilTemperature / 10.0;
  ec     = payload.conductivity;
  phS    = payload.soilPh / 10.0;
  tempA  = payload.airTemperature / 10.0;
  humA   = payload.airHumidity / 10.0;
  lux    = payload.lightIntensity;

  payloadToPublish["id"]  = nodeId;
  payloadToPublish["n"]   = n;
  payloadToPublish["p"]   = p;
  payloadToPublish["k"]   = k;
  payloadToPublish["sm"]  = moistS;
  payloadToPublish["st"]  = tempS;
  payloadToPublish["ec"]  = ec;
  payloadToPublish["pH"]  = phS;
  payloadToPublish["at"]  = tempA;
  payloadToPublish["ah"]  = humA;
  payloadToPublish["lux"] = lux;

  display.clear();
  display.setCursor(0, 0);
  display.print("N:");
  display.print(n);
  display.setCursor(8, 0);
  display.print("P:");
  display.print(p);
  display.setCursor(0, 1);
  display.print("K:");
  display.print(k);
  display.setCursor(8, 1);
  display.print("M:");
  display.print(moistS, 1);
  delay(2000);

  display.clear();
  display.setCursor(0, 0);
  display.print("Ts:");
  display.print(tempS, 1);
  display.setCursor(8, 0);
  display.print("pH:");
  display.print(phS, 1);
  display.setCursor(0, 1);
  display.print("Ta:");
  display.print(tempA, 1);
  display.setCursor(8, 1);
  display.print("Hu:");
  display.print(humA, 1);
  delay(2000);

  String topic = "gh01/node_" + String(nodeId) + "/parameter";
  if (mqttClient.publish(topic.c_str(), JSON.stringify(payloadToPublish).c_str())) {
    display.clear();
    showMessage(0, 0, "mqtt msg sent");
  } else {
    showMessage(0, 0, "mqtt msg failed");
  }

  delay(2000);
  hasNewPacket = false;
  display.clear();
  LoRa.receive();
}

void loraCallback(int packetSize) {
  if (packetSize == 0) return;
  if (packetSize == sizeof(payload)) {
    LoRa.readBytes((uint8_t*)&payload, packetSize);
  }
  hasNewPacket = true;
}

void mqttCallback(const char* topic, byte* payload, int length ) {
  display.clear();
  Serial.println("mqtt msg received");
  if(String(topic).indexOf("control") != -1) {
    controlActuator(topic, payload);
  }

  if(String(topic).indexOf("get") != -1) {
    sentStatusActuator(topic);
  }
}

void controlActuator(const char* topic, byte* payload) {
  String currentTopic = String(topic);
  boolean status = *payload == '1' ? LOW : HIGH;
  Serial.println(String("Actuator ") + !status ? "ON" : "OFF");
  if(status == HIGH) {
    isManual = false;
  }else {
    isManual = true;
  }
  lastManualActive = millis();
  
  if(currentTopic.indexOf("waterpump") != -1) {
    controlWaterPump(status);
  }else if(currentTopic.indexOf("fan") != -1) {
    controlFan(status);
  }
  sentStatusActuator(topic);
}

void controlWaterPump(boolean status) {
  if(waterPumpState == status) {
    return;
  }
  digitalWrite(RELAY_PIN_1, status);
  waterPumpState = status;
  if(!status) {
    lastWaterPumpActivate = millis();
    showMessage(0, 1, "Waterpump ON");
  }else {
    lastWaterPumpActivate = millis();
    showMessage(0, 1, "Waterpump OFF");
  }
  
  lastWaterPumpState = waterPumpState;

}

void controlFan(boolean status) {
  if(fanState == status) {
    return;
  }
  digitalWrite(RELAY_PIN_2, status);
  fanState = status;
  if(!status) {
    showMessage(0, 1, "Fan ON");
  }else {
    lastFanActivate = millis();
    showMessage(0, 1, "Fan OFF");
  }
  lastFanState = fanState;
}

void sentStatusActuator(const char* topic) {
  if(String(topic).indexOf("waterpump") != -1) {
    String statusTopic = "gh01/node" + String(NODE_ID) + "/status/waterpump";
    if(mqttClient.publish(statusTopic.c_str(), !waterPumpState ? "ON" : "OFF")) {
      display.clear();
      showMessage(0, 0, "Status sent");
      showMessage(0, 1, (String("State: ") + !waterPumpState).c_str());
    }
  }
  if(String(topic).indexOf("fan") != -1) {
    String statusTopic = "gh01/node" + String(NODE_ID) + "/status/fan";
    if(mqttClient.publish(statusTopic.c_str(), !fanState ? "ON" : "OFF")) {
      display.clear();
      showMessage(0, 0, "Status sent");
      showMessage(0, 1, (String("State: ") + !fanState).c_str());
    }
  }
}

void manageWaterPump() {
  if(moistS == 0) {
    return;
  }
  String statusTopic = "gh01/node" + String(NODE_ID) + "/status/waterpump";
  if(waterPumpState && (millis() - lastStatusWaterPumpMQTTSent >= 5000)) {
    sentStatusActuator(statusTopic.c_str());
    lastStatusWaterPumpMQTTSent = millis();
  }
  if(moistS <= 20) {
    controlWaterPump(0);
  } else if(moistS >= 60) {
    controlWaterPump(1);
  }
}

void manageFan(){
  if(tempA == 0){
    return;
  }
  String statusTopic = "gh01/node" + String(NODE_ID) + "/status/fan";

  if(!fanState && (millis() - lastStatusFanMQTTSent >= 5000)) {
    sentStatusActuator(statusTopic.c_str());
    lastStatusFanMQTTSent = millis();
  }

  if(tempA >= 35) {
    controlFan(0);
  }else if(tempA <= 30) {
    controlFan(1);
  }
}