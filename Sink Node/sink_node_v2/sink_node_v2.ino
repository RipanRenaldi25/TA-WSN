#include <SPI.h>
#include <LoRa.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <SD.h>
#include <RTClib.h>

#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_SCK 18
#define LORA_SS 25
#define LORA_DIO 26
#define LORA_RST 27


#define SD_MISO 5
#define SD_MOSI 13
#define SD_SCK 14
#define SD_CS 15

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

SPIClass loraSPI(VSPI);
SPIClass sdSPI(HSPI);

LiquidCrystal_I2C display(0x27, 16, 2);
WiFiClientSecure wifiClientSecure;
PubSubClient mqttClient;
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
bool hasNewPacket = false;

int8_t nodeId = 0;
int16_t soilMoisture = 0;
int16_t soilTemperature = 0;
int16_t conductivity = 0;
int16_t soilPh = 0;
int16_t nitrogen = 0;
int16_t phosporus = 0;
int16_t kalium = 0;
int16_t airTemperature = 0;
int16_t airHumidity = 0;
uint16_t lightIntensity = 0;

long int currentMillis = millis();
long int lastMQTTAttempt = 0;

unsigned long lastValve1Activate = 0;
unsigned long lastValve2Activate = 0;
unsigned long lastActuatorMessage = 0;
boolean valve1State = 1;
boolean valve2State = 1;
boolean lastValve1State = 1;
boolean lastValve2State = 1;
boolean isManual = false;

unsigned long lastStatusValve1MQTTSent = 0;
unsigned long lastStatusValve2MQTTSent = 0;
unsigned long lastManualActive = 0;
unsigned long lastSDCardSent = 0;
unsigned long syncInterval = 1000 * 60 * 5;
int syncBatchSize = 5;

boolean isShowInfo = false;
unsigned long lastShowInfo = 0;
unsigned long lastShowPage = 0;
byte currentPage = 0;
int showInfoDuration = 4000;
int durationEachPage = showInfoDuration / 2;


void showMessage(int x, int y, const char* message);
void connectWifi();
boolean connectMQTT();
void reconnectMQTT();
void initializeSubscribe();
void parseLoRaPacket(int packetSize);
void loraCallback(int packetSize);
void initLoRa();
void setupRelay();
void initSDCard();
void initRTC();
void mqttCallback(const char* topic, byte* payload, int length);
void controlActuator(const char* topic, byte* payload);
void controlValve1(boolean status);
void controlValve2(boolean status);
void sentStatusActuator(const char* topic);
void manageValve1();
void manageValve2();
void saveToSDCard(PayloadData& data);
void syncSDCard(int length);
void displayLoop();
String parseToCSV(PayloadData& data);


void setup() {
  Serial.begin(9600);
  setupRelay();
  lastValve1Activate = millis();
  lastValve2Activate = millis();

  display.init();
  display.backlight();
  showMessage(0, 0, "Initialize...");
  delay(2000);
  display.clear();
  initRTC();
  connectWiFi();
  connectMQTT();
  initLoRa();
}

void loop() {
  currentMillis = millis();

  if (hasNewPacket) {
    parseLoRaPacket(sizeof(payload));
  }

  if (isManual) {
    if (millis() - lastManualActive >= 15000) {
      controlValve1(1);
      controlValve2(1);
      isManual = false;
    }
  } else {
    manageValve1();
    manageValve2();
  }

  if (!valve1State && !isManual && (millis() - lastValve1Activate >= 15000)) {
    controlValve1(1);
  }

  if (!valve2State && !isManual && (millis() - lastValve2Activate >= 15000)) {
    controlValve2(1);
  }

  displayLoop();
}


void displayLoop() {
  unsigned long now = millis();

  if (!valve1State || !valve2State) {
    if (now - lastActuatorMessage >= 1000) {
      lastActuatorMessage = now;
      display.clear();
      display.setCursor(0, 0);
      display.print("Actuator ON");
      String actuatorType = "";
      if (!valve1State) {
        actuatorType += "V1 ";
      }
      if (!valve2State) {
        actuatorType += "V2 ";
      }
      display.setCursor(0, 1);
      display.print((actuatorType + "ON").c_str());
    }
    return;
  }
  if (isShowInfo) {
    if (now - lastShowPage >= durationEachPage) {
      lastShowPage = now;
      currentPage = (currentPage + 1) % 2;
      display.clear();

      if (currentPage == 0) {
        display.setCursor(0, 0);
        display.print("N:");
        display.print(nitrogen);
        display.setCursor(8, 0);
        display.print("P:");
        display.print(phosporus);
        display.setCursor(0, 1);
        display.print("K:");
        display.print(kalium);
        display.setCursor(8, 1);
        display.print("M:");
        display.print(soilMoisture);
      } else {
        display.setCursor(0, 0);
        display.print("Ts:");
        display.print(soilTemperature / 10);
        display.setCursor(8, 0);
        display.print("pH:");
        display.print(soilPh);
        display.setCursor(0, 1);
        display.print("Ta:");
        display.print(airTemperature / 10);
        display.setCursor(8, 1);
        display.print("Hu:");
        display.print(airHumidity);
      }
    }

    if (now - lastShowInfo >= showInfoDuration) {
      isShowInfo = false;
      display.clear();
      showMessage(0, 0, "Data tersimpan");
    }
    return;
  }
  int lastShowSync = 0;
  if (isSyncSD) {
    lastShowSync = millis();
    display.clear();
    display.setCursor(0, 0);
    display.print("Sync SD Card..");
    return;
  }

  if (!isMQTTConnected) {
    if (now - lastDisplayMillis >= 2000) {
      lastDisplayMillis = now;
      String message = "MQTT Failed ";
      display.clear();
      showMessage(0, 0, message.c_str());
    }
    return;
  }

  if (now - lastDisplayMillis >= 2000) {
    lastDisplayMillis = now;
    display.clear();
    display.setCursor(0, 0);
    display.print("Menunggu paket...");
    display.setCursor(0, 1);
    display.print((String("Node Id: ") + NODE_ID).c_str());
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

void initRTC() {
  showMessage(0, 0, "Init RTC");
  if (!rtc.begin()) {
    showMessage(0, 0, "RTC Failed");
    while (1)
      ;
  }
  if (rtc.lostPower()) {
    showMessage(0, 0, "RE-SET RTC");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  DateTime currentTime = rtc.now();
  char formatTime[] = "YYYY-MM-DD hh:mm:ss";
  Serial.println(currentTime.toString(formatTime));
}

void initLoRa() {
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setSPI(loraSPI);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO);
  if (!LoRa.begin(923E6)) {
    Serial.println("LoRa Failed");
    showMessage(0, 0, "Lora Failed");
    while (1)
      ;
  }
  showMessage(0, 0, "LoRa Success");
  delay(1000);
  LoRa.onReceive(loraCallback);
  LoRa.receive();
}

void initSDCard() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  showMessage(0, 0, "Init SD Card");
  if (!SD.begin(SD_CS, sdSPI)) {
    display.clear();
    showMessage(0, 0, "SD Failed");
    while (1);
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
  WiFi.begin(SSID, PASSWORD);
  byte counter = 1;
  while (WiFi.status() != WL_CONNECTED) {
    if (counter == 10) {
      display.clear();
      showMessage(0, 0, "Wifi Failed");
      break;
    }
    counter++;
    delay(5000);
  }
  wifiClientSecure.setInsecure();
  wifiClientSecure.setTimeout(5);
}

boolean connectMQTT() {
  display.clear();
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  mqttClient.setClient(wifiClientSecure);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(3);

  if (mqttClient.connect(CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
    initializeSubscribe();
    Serial.println("MQTT CONNECTED");
    Serial.println("")
  }
  return mqttClient.connected();
}

void reconnectMQTT() {
  if (millis() - lastMQTTAttempt >= 5000) {
    lastMQTTAttempt = millis();
    display.clear();
    showMessage(0, 0, "Attempting MQTT...");
    connectMQTT();
  } else {
    mqttClient.loop();
  }
}

void initializeSubscribe() {
  display.clear();

  if (!mqttClient.subscribe(("gh01/node/" + String(NODE_ID) + "/control/+").c_str())) {
    showMessage(0, 0, "Subscribe Failed");
    showMessage(0, 1, "Topic control");
    return;
  }

  if (!mqttClient.subscribe(("gh01/node/" + String(NODE_ID) + "/get/+").c_str())) {
    showMessage(0, 0, "Subscribe Failed");
    showMessage(0, 1, "Topic status");
    return;
  }

  showMessage(0, 0, "Subscribed Topik");
}

String parseToCSV(PayloadData& data) {
  DateTime currentTime = rtc.now();
  char formatTimeStamp[] = "YYYY-MM-DD hh:mm:ss";
  String timeStamp = currentTime.toString(formatTimeStamp);
  String csvFormat = "";
  csvFormat.reserve(120);
  csvFormat += timeStamp;
  csvFormat += ";";
  csvFormat += data.nodeId;
  csvFormat += ";";
  csvFormat += data.soilMoisture;
  csvFormat += ";";
  csvFormat += data.soilTemperature;
  csvFormat += ";";
  csvFormat += data.conductivity;
  csvFormat += ";";
  csvFormat += data.soilPh;
  csvFormat += ";";
  csvFormat += data.nitrogen;
  csvFormat += ";";
  csvFormat += data.phosporus;
  csvFormat += ";";
  csvFormat += data.kalium;
  csvFormat += ";";
  csvFormat += data.airTemperature;
  csvFormat += ";";
  csvFormat += data.airHumidity;
  csvFormat += ";";
  csvFormat += data.lightIntensity;

  return csvFormat;
}

void saveToSDCard(PayloadData& data) {
  File logFile = SD.open("/logFile.csv", FILE_APPEND);
  if (logFile) {
    String csvData = parseToCSV(data);
    logFile.print(csvData.c_str());
    logFile.println();
    logFile.close();
  } else {
    showMessage(0, 0, "File Not Found");
  }
}


void syncSDCard(int length) {

  if (!SD.exists("/logFile.csv")) {
    lastSDCardSent = millis();
    Serial.println("SD not exists");
    return;
  }

  File logFile = SD.open("/logFile.csv", FILE_READ);
  if (!logFile) {
    lastSDCardSent = millis();
    return;
  }

  if (logFile.size() == 0) {
    logFile.close();
    SD.remove("/logFile.csv");
    lastSDCardSent = millis();
    return;
  }
  isSyncronize = true;
  displayLoop();

  String topic = "gh01/node/" + String(NODE_ID) + "/parameter";
  String remainingRows = "";
  int sentCount = 0;
  bool stillSending = true;

  while (logFile.available()) {
    String currentRow = logFile.readStringUntil('\n');
    currentRow.trim();
    if (currentRow.length() == 0) {
      continue;
    }

    if (stillSending && sentCount < length && mqttClient.connected() && mqttClient.publish(topic.c_str(), currentRow.c_str())) {
      sentCount++;

      unsigned long waitStart = millis();

      while (millis() - waitStart < 100) {
        mqttClient.loop();
      }
    } else {
      stillSending = false;
      remainingRows += currentRow + "\n";
      isSyncronize = false;
    }
  }
  logFile.close();

  if (remainingRows.length() == 0) {
    SD.remove("/logFile.csv");
  } else if (sentCount > 0) {
    SD.remove("/logFile.csv");
    File remainingLogFile = SD.open("/logFile.csv", FILE_WRITE);
    if (remainingLogFile) {
      remainingLogFile.print(remainingRows);
      remainingLogFile.close();
    }
  }

  lastSDCardSent = millis();
  isSyncronize = false;
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

  nodeId = payload.nodeId;
  soilMoisture = payload.soilMoisture;
  soilTemperature = payload.soilTemperature;
  conductivity = payload.conductivity;
  soilPh = payload.soilPh;
  nitrogen = payload.nitrogen;
  phosporus = payload.phosporus;
  kalium = payload.kalium;
  airTemperature = payload.airTemperature;
  airHumidity = payload.airHumidity;
  lightIntensity = payload.lightIntensity;
  isShowInfo = true;
  lastShowInfo = millis();
  lastShowPage = millis();
  currentPage = 0;
  hasNewPacket = false;
  LoRa.receive();
}

void loraCallback(int packetSize) {
  if (packetSize == 0) return;
  if (packetSize == sizeof(payload)) {
    LoRa.readBytes((uint8_t*)&payload, packetSize);
  }
  hasNewPacket = true;
  xQueueSend(dataQueue, &payload, 0);
}

void mqttCallback(const char* topic, byte* payload, int length) {
  if (length == 0) return;
  display.clear();
  Serial.println("mqtt msg received");
  String currentTopic = String(topic);

  if (currentTopic.indexOf("control") != -1) {
    controlActuator(topic, payload);
  }

  if (currentTopic.indexOf("get") != -1) {
    sentStatusActuator(topic);
  }
}

void controlActuator(const char* topic, byte* payload) {
  String currentTopic = String(topic);
  boolean status = (*payload == '1') ? LOW : HIGH;
  Serial.print("Actuator ");
  Serial.println(!status ? "ON" : "OFF");

  isManual = (status == LOW);
  lastManualActive = millis();

  if (currentTopic.indexOf("valve1") != -1) {
    controlValve1(status);
  } else if (currentTopic.indexOf("valve2") != -1) {
    controlValve2(status);
  }
  sentStatusActuator(topic);
}

void controlValve1(boolean status) {
  if (valve1State == status) {
    return;
  }
  digitalWrite(RELAY_PIN_1, status);
  valve1State = status;
  lastValve1Activate = millis();
  lastValve1State = valve1State;
  showMessage(0, 1, !status ? "Valve1 ON" : "Valve1 OFF");
}

void controlValve2(boolean status) {
  if (valve2State == status) {
    return;
  }
  digitalWrite(RELAY_PIN_2, status);
  valve2State = status;
  lastValve2Activate = millis();
  lastValve2State = valve2State;
  showMessage(0, 1, !status ? "Valve2 ON" : "Valve2 OFF");
}

void sentStatusActuator(const char* topic) {
  String currentTopic = String(topic);

  if (currentTopic.indexOf("valve1") != -1) {
    String statusTopic = "gh01/node/" + String(NODE_ID) + "/status/valve1";
    if (mqttClient.publish(statusTopic.c_str(), !valve1State ? "ON" : "OFF")) {
      display.clear();
      showMessage(0, 0, "Status sent");
      showMessage(0, 1, (String("V1: ") + (!valve1State ? "ON" : "OFF")).c_str());
    }
  }

  if (currentTopic.indexOf("valve2") != -1) {
    String statusTopic = "gh01/node/" + String(NODE_ID) + "/status/valve2";
    if (mqttClient.publish(statusTopic.c_str(), !valve2State ? "ON" : "OFF")) {
      display.clear();
      showMessage(0, 0, "Status sent");
      showMessage(0, 1, (String("V2: ") + (!valve2State ? "ON" : "OFF")).c_str());
    }
  }
}

void manageValve1() {
  if (soilMoisture == 0) {
    return;
  }
  String statusTopic = "gh01/node/" + String(NODE_ID) + "/status/valve1";
  if (valve1State && (millis() - lastStatusValve1MQTTSent >= 5000)) {
    sentStatusActuator(statusTopic.c_str());
    lastStatusValve1MQTTSent = millis();
  }
  if (soilMoisture <= 20) {
    controlValve1(0);
  } else if (soilMoisture >= 60) {
    controlValve1(1);
  }
}

void manageValve2() {
  if (soilMoisture == 0) {
    return;
  }
  String statusTopic = "gh01/node/" + String(NODE_ID) + "/status/valve2";
  if (valve2State && (millis() - lastStatusValve2MQTTSent >= 5000)) {
    sentStatusActuator(statusTopic.c_str());
    lastStatusValve2MQTTSent = millis();
  }
  if (soilMoisture <= 20) {
    controlValve2(0);
  } else if (soilMoisture >= 60) {
    controlValve2(1);
  }
}
