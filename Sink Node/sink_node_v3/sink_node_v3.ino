#include <SPI.h>
#include <LoRa.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Arduino_JSON.h>
#include <SD.h>
#include <SPI.h>
#include <RTClib.h>

#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SCK 18
#define LORA_SS 25
#define LORA_DIO 26
#define LORA_RST 27

#define MQTT_SERVER "ff2efa61874b4e338caf837fc41443b3.s1.eu.hivemq.cloud"
#define MQTT_USERNAME "cobamqtt"
#define MQTT_PASSWORD "cobaMQTT123"
// #define MQTT_SERVER "61b617dcb74f48908006ec3ab8840e52.s1.eu.hivemq.cloud"
// #define MQTT_USERNAME "smartgreenhouse"
// #define MQTT_PASSWORD "SmartGreenhouse123"
#define MQTT_PORT 8883
#define CLIENT_ID "gh01"
#define SSID "nap"
#define PASSWORD "napir123"
#define NODE_ID 255

#define TOPIC_TO_SUBSCRIBE "gh01/#"

#define RELAY_PIN_1 32
#define RELAY_PIN_2 33

// SD CARD
#define SD_MISO 5
#define SD_MOSI 13
#define SD_SCK 14
#define SD_CS 15

SPIClass loraSPI(VSPI);
SPIClass sdSPI(HSPI);

LiquidCrystal_I2C display(0x27, 16, 2);
WiFiClientSecure wifiClientSecure;
PubSubClient mqttClient;
File myFile;

RTC_DS3231 rtc;

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
boolean hasNewPacket = false;

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
unsigned long lastSDCardSent = 0;
// int syncInterval = 10 * 1000;
int syncInterval = 1000;
byte currentPage = 0;
boolean isShowInfo = false;
unsigned long lastShowPage = 0;
unsigned long lastShowInfo = 0;
int showInfoDuration = 2000;
int durationEachPage = 1000;
boolean isInternetConnected = false;
TaskHandle_t mqttTaskHandle = NULL;
boolean isReconnecting = false;

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
void setupSDCard();
void setupRTC();
void syncSDCard(int length);
void saveToSDCard(PayloadData &data);
void displayData();

void mqttTask(void *pvParameters) {
  Serial.print("MQTT RUN ON CORE: ");
  Serial.println(xPortGetCoreID());
  while(true){
    if (!mqttClient.connected()) {
      reconnectMQTT(); 
    }else {
      mqttClient.loop();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  Serial.begin(9600);
  setupRelay();
  setupRTC();
  lastWaterPumpActivate = millis();
  lastFanActivate = millis();


  display.init();
  display.backlight();
  showMessage(0, 0, "Initialize...");
  delay(2000);
  display.clear();
  setupLoRa();
  setupSDCard();
  connectWifi();
  showMessage(0, 0, "Init MQTT");
  // connectMQTT();
  xTaskCreatePinnedToCore(mqttTask, "MQTT_TASK", 10000, NULL, 1, &mqttTaskHandle, 0);
  if(mqttClient.connected()) {
    Serial.println("Connected");
  }

}

void loop() {
  currentMillis = millis();
  isInternetConnected = (WiFi.status() == WL_CONNECTED && mqttClient.connected());

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
  }else if(isShowInfo) {
    displayData();
    if(millis() - lastShowInfo >= showInfoDuration) {
      isShowInfo = false;
      if(!isInternetConnected){
        showMessage(0, 0, "Data Saved");
        showMessage(0, 1, "(Offline)");
      }else {
        showMessage(0, 0, "Data Saved");
        showMessage(0, 1, "(Synced)");
      }
    }
  } else {
    lastDisplayMillis = currentMillis;
    display.setCursor(0,0);
    display.print("MENUNGGU PAKET");
    display.setCursor(0,1);
    display.print((String("Node Id: ") + NODE_ID).c_str());
    display.print("        ");
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
  showMessage(0, 0, "Relay Success");
  delay(1000);
}

void setupRTC() {
  showMessage(0, 0, "Init RTC");
  if(!rtc.begin()) {
    showMessage(0, 0, "RTC Failed");
    while(1);
  }
  if(rtc.lostPower()) {
    showMessage(0, 0, "RE-SET RTC");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  DateTime currentTime = rtc.now();
  char formatTime[] = "YYYY-MM-DD hh:mm:ss";
  Serial.println(currentTime.toString(formatTime));
}

void setupLoRa() {
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setSPI(loraSPI);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO);
  if (!LoRa.begin(923E6)) {
    Serial.println("LoRa Failed");
    showMessage(0, 0, "Lora Failed");
    while (1);
  }
  showMessage(0, 0, "LoRa Success");    
  delay(1000);
  LoRa.onReceive(loraCallback);
  LoRa.receive();
}

void setupSDCard() {
 sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
 showMessage(0, 0, "Init SD Card");
 if(!SD.begin(SD_CS, sdSPI) ) {
  display.clear();
  showMessage(0, 0, "SD Failed");
  while(1);
 }
 showMessage(0, 0, "SD Card Success");
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
    showMessage(0, 0, "Re Connecting wifi...");
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
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setClient(wifiClientSecure);
  mqttClient.connect(CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
  mqttClient.setCallback(mqttCallback);
  initializeSubscribe();
  return mqttClient.connected();
}

void reconnectMQTT() {
  if (millis() - lastMQTTAttempt >= 5000) {
    lastMQTTAttempt = millis();
    isReconnecting = true;
    mqttClient.setSocketTimeout(1);
    if(connectMQTT()) {
      lastMQTTAttempt = 0;
      isReconnecting = false;
    }
  }
}

void saveToSDCard(PayloadData &data) {
  DateTime currentTime = rtc.now();
  char formatTimeStamp[] =  "YYYY-MM-DD hh:mm:ss";
  int adjustedHour = currentTime.hour() + 7; //WIB
  File logFile = SD.open("/logFile.csv", FILE_APPEND);
  if(logFile){
    String adjustedTimeStamp = currentTime.toString(formatTimeStamp);
    logFile.print(adjustedTimeStamp);
    logFile.print(",");
    logFile.print(data.nodeId);
    logFile.print(",");
    logFile.print(data.soilMoisture);
    logFile.print(",");
    logFile.print(data.soilTemperature / 10);
    logFile.print(",");
    logFile.print(data.conductivity);
    logFile.print(",");
    logFile.print(data.conductivity);
    logFile.print(",");
    logFile.print(data.soilPh);
    logFile.print(",");
    logFile.print(data.nitrogen);
    logFile.print(",");
    logFile.print(data.phosporus);
    logFile.print(",");
    logFile.print(data.kalium);
    logFile.print(",");
    logFile.print(data.airTemperature / 10);
    logFile.print(",");
    logFile.print(data.airHumidity);
    logFile.print(",");
    logFile.print(data.lightIntensity);
    logFile.println();
    logFile.close();    
  }else {
    showMessage(0, 0, "File Not Found");
  }
}

void initializeSubscribe() {
  display.clear();

  if(!mqttClient.subscribe("gh01/node/255/control/+")) {
    Serial.println("Subscribe Failed: Topic control");
    return;
  };

  if(!mqttClient.subscribe("gh01/node/255/get/+")){
    Serial.println("Subscribe Failed: Topic status");
    return;
  }
  Serial.println("Subscribed");
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
 
  isInternetConnected = (WiFi.status() == WL_CONNECTED && mqttClient.connected());
  if(!isInternetConnected) {
    display.clear();
    showMessage(0, 0, "Saving Offline..");
  }
  isShowInfo = true;
  lastShowInfo = millis();
  hasNewPacket = false;
  saveToSDCard(payload);
  LoRa.receive();

  if(!isInternetConnected){
    return;
  }

  if(WiFi.status() != WL_CONNECTED || !mqttClient.connected()) {
    display.clear();
    return;
  }
  if(millis() - lastSDCardSent  >= syncInterval) {
    syncSDCard(10);
  }else {
    syncSDCard(1);
  }
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
    String statusTopic = "gh01/node/" + String(NODE_ID) + "/status/waterpump";
    if(mqttClient.publish(statusTopic.c_str(), !waterPumpState ? "ON" : "OFF")) {
      display.clear();
      showMessage(0, 0, "Status sent");
      showMessage(0, 1, (String("State: ") + !waterPumpState).c_str());
    }
  }
  if(String(topic).indexOf("fan") != -1) {
    String statusTopic = "gh01/node/" + String(NODE_ID) + "/status/fan";
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

void syncSDCard(int length) {
  int position = 0;
  File positionFile = SD.open("/position.txt", FILE_READ);
  if(positionFile) {
    position = positionFile.parseInt();
    positionFile.close();
  }

  
  File logFile = SD.open("/logFile.csv", FILE_READ); 
  if(!logFile) {
    return;
  }

  if((unsigned long) logFile.size() <= (unsigned long) position) {
    showMessage(0, 0, "Already Sync...");
    return;
  }

  logFile.seek(position);
  String topic = "gh01/node/" + String(NODE_ID) + "/parameter";
  int index = 0;
  int lastPos = 0;
  while(logFile.available() && index < length) {
    String currentRow = logFile.readStringUntil('\n');
    boolean isSuccess = mqttClient.publish(topic.c_str(), currentRow.c_str());
    if(isSuccess) {
      index++;
      lastPos = logFile.position();
    }else {
      break;
    }
  }
  logFile.close();
  SD.remove("/position.txt");
  File positionFileWriter = SD.open("/position.txt", FILE_WRITE);
  if(positionFileWriter) {
    positionFileWriter.print(lastPos);
    positionFileWriter.close();
  }
  
  lastSDCardSent = millis();
}

byte lastPage = -1;
void displayData() {
  if (millis() - lastShowPage >= durationEachPage) {
    currentPage = (currentPage + 1) % 2;
    lastShowPage = millis();
    display.clear();
  }

  if(currentPage != lastPage) {
    lastPage = currentPage;
    return;
  }

  if (currentPage == 0) {
    display.setCursor(0, 0);
    display.print("N: ");
    display.print(payload.nitrogen);
    display.print("   ");
    display.setCursor(8, 0);
    display.print("P: ");
    display.print(payload.phosporus);
    display.print("   ");
    display.setCursor(0, 1);
    display.print("K: ");
    display.print(payload.kalium);
    display.print("   ");
    display.setCursor(8, 1);
    display.print("M: ");
    display.print(payload.soilMoisture);
    display.print("   ");
  } else if (currentPage == 1) {
    display.setCursor(0, 0);
    display.print("Ts: ");
    display.print(payload.soilTemperature / 10);
    display.print("  ");
    display.setCursor(8, 0);
    display.print("pH: ");
    display.print(payload.soilPh, 1);
    display.print("  ");
    display.setCursor(0, 1);
    display.print("Ta: ");
    display.print(payload.airTemperature / 10);
    display.print("  ");
    display.setCursor(8, 1);
    display.print("Hu: ");
    display.print(payload.airHumidity);
    display.print("  ");
  }
}