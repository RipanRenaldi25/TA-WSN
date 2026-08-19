#include <SPI.h>
#include <LoRa.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
// #include <WiFiClientSecure.h>
#include <WiFiClient.h>
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

// #define MQTT_SERVER "ff2efa61874b4e338caf837fc41443b3.s1.eu.hivemq.cloud"
// #define MQTT_USERNAME "cobamqtt"
// #define MQTT_PASSWORD "cobaMQTT123"
// #define MQTT_SERVER "61b617dcb74f48908006ec3ab8840e52.s1.eu.hivemq.cloud"
// #define MQTT_USERNAME "smartgreenhouse"
// #define MQTT_PASSWORD "SmartGreenhouse123"
// #define MQTT_PORT 8883
#define MQTT_SERVER "168.110.214.70"
#define MQTT_PORT 1883
#define CLIENT_ID "gh01"
#define SSID "nap"
#define PASSWORD "napir123"
#define NODE_ID 255


#define TOPIC_TO_SUBSCRIBE "gh01/#"

#define RELAY_PIN_1 32
#define RELAY_PIN_2 33
#define SENSOR_ID_1 1
#define SENSOR_ID_2 2

SPIClass loraSPI(VSPI);
SPIClass sdSPI(HSPI);

LiquidCrystal_I2C display(0x27, 16, 2);
// WiFiClientSecure wifiClientSecure;
WiFiClient wifiClient;
PubSubClient mqttClient;
RTC_DS3231 rtc;

#pragma pack(push, 1)
struct PayloadData {
  uint8_t nodeId;
  uint8_t nodeTarget;
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
#pragma pack(pop)

struct PayloadData payload;

unsigned long lastDisplayMillis = 0;
int lcdPage = 0;
bool hasNewPacket = false;

uint8_t nodeId = 0;
float soilMoisture = 0;
float soilTemperature = 0;
int16_t conductivity = 0;
float soilPh = 0;
int16_t nitrogen = 0;
int16_t phosporus = 0;
int16_t kalium = 0;
float airTemperature = 0;
float airHumidity = 0;
uint16_t lightIntensity = 0;

long int lastMQTTAttempt = 0;
long int lastWiFiAttempt = 0;

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
int showInfoDuration = 6000;
int durationEachPage = showInfoDuration / 2;

SemaphoreHandle_t displayMutex;
QueueHandle_t dataQueue;
volatile uint8_t counter = 0;
volatile uint8_t netState = 0;
volatile boolean isSyncSD = false;
volatile boolean reqValve1 = HIGH;
volatile boolean reqValve2 = HIGH;

volatile boolean reportValve1 = false;
volatile boolean reportValve2 = false;
volatile boolean isMQTTConnected = false;
volatile boolean isWiFiConnected = false;

unsigned long lastNetStateShown = 0;

struct NodeSensorState {
  int8_t nodeId;
  int16_t soilMoisture;
  boolean hasData;
  unsigned long lastUpdateMillis;
  boolean isWatering;
  unsigned long lastWatering;
  boolean actedOnCurrentReading;
};

NodeSensorState node1State = {
  SENSOR_ID_1, 0, false, 0, false, 0, false
};

NodeSensorState node2State = {
  SENSOR_ID_2, 0, false, 0, false, 0, false
};

int minInterval = 5000;
int maxInterval = 60000;

int currentInterval = minInterval;


void showMessage(int x, int y, const char* message);
void connectWifi();
boolean connectMQTT();
void reconnectMQTT();
void initializeSubscribe();
void parseLoRaPacket(int packetSize);
void loraCallback(int packetSize);
void initLoRa();
void initRelay();
void initSDCard();
void initRTC();
void mqttCallback(const char* topic, byte* payload, int length);
void controlValve1(boolean status);
void controlValve2(boolean status);
void sentStatusActuator(const char* topic);
void manageValve1();
void manageValve2();
void saveToSDCard(PayloadData& data);
void syncSDCard(int length);
void displayLoop();
String parseToCSV(PayloadData& data);
void networkManagementTask(void *parameter);
void manageWiFi();
void checkLoRaPacket();

void networkManagementTask(void *parameter) {

  if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
    initRTC();
    initSDCard();
    connectWifi();
    connectMQTT();
    xSemaphoreGive(displayMutex);
  };


  PayloadData currentComingData;

  while(true) {
    manageWiFi();
    reconnectMQTT();
    isMQTTConnected = mqttClient.connected(); 
    if(isMQTTConnected && isWiFiConnected) {
      netState = 0;
      if(millis() - lastSDCardSent >= syncInterval) {
        isSyncSD = true;
        syncSDCard(syncBatchSize);
        isSyncSD = false;
      }
    }


    if(xQueueReceive(dataQueue, &currentComingData, 30 / portTICK_PERIOD_MS) == pdTRUE) {
      boolean isSent = false;
      if(isMQTTConnected) {
        String topic = "gh01/node/" + String(currentComingData.nodeId) + "/parameter";
        String csvPayloadToSent = parseToCSV(currentComingData);
        Serial.print("CSV Payload: ");
        Serial.println(csvPayloadToSent);
        isSent = mqttClient.publish(topic.c_str(), csvPayloadToSent.c_str());
      }else {
        saveToSDCard(currentComingData);
      }

      if(xSemaphoreTake(displayMutex, portMAX_DELAY) == pdTRUE) {
          display.clear();
          String statusMessage = "Data " + String(isSent ? "Sent" : "Saved"); 
          showMessage(0, 0, statusMessage.c_str());
          xSemaphoreGive(displayMutex);
      }
    }

    if(isMQTTConnected){
      if(reportValve1) {
        String statusTopic = "gh01/node/" + String(NODE_ID) + "/status/valve1";
        mqttClient.publish(statusTopic.c_str(), !valve1State ? "ON" : "OFF");
        reportValve1 = false;
      }

      if(reportValve2) {
        String statusTopic = "gh01/node/" + String(NODE_ID) + "/status/valve2";
        mqttClient.publish(statusTopic.c_str(), !valve2State ? "ON" : "OFF");
        reportValve2 = false;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }

}

void displayLoop() {
  unsigned long now = millis();

    if (isShowInfo) {
    if (now - lastShowPage >= durationEachPage) {
      lastShowPage = now;
      currentPage = (currentPage + 1) % 2;
      if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
        display.clear();
        if (currentPage == 1) {
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
        xSemaphoreGive(displayMutex);
      }
    }

    if (now - lastShowInfo >= showInfoDuration) {
      isShowInfo = false;
      if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
        display.clear();
        showMessage(0, 0, "Data tersimpan");
        xSemaphoreGive(displayMutex);
      }
    }
    return;
  }

  if (!valve1State || !valve2State) {
    if (now - lastActuatorMessage >= 1000) {
      lastActuatorMessage = now;
      if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
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
        xSemaphoreGive(displayMutex);
      }
    }
    return;
  }



  int lastShowSync = 0;
  if (isSyncSD) {
    lastShowSync = millis();
    if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
      display.clear();
      display.setCursor(0, 0);
      display.print("Sync SD Card..");
      xSemaphoreGive(displayMutex);
    }
    return;
  }
  
  if(millis() - lastNetStateShown >= 2000) {
    lastNetStateShown = millis();
    if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {

      switch(netState) {
        case 0:
          display.print("Menunggu paket..");
          display.setCursor(0, 1);
          display.print((String("Node Id: ") + NODE_ID).c_str());
          break;
        case 1:
          showMessage(0, 0, "Connecting Wifi");
          break;
        case 2:
          showMessage(0, 0, "WiFi Connected");
          break;
        case 3:
          showMessage(0, 0, "WiFi Failed");
          break;
        case 4:
          showMessage(0, 0, "Attempt MQTT");
          break;
        case 5:
          showMessage(0, 0, "MQTT Connected");
          netState = 0;
          break;
        case 6:
          showMessage(0, 0, "MQTT Failed");
          break;
        default:
          display.print("Menunggu paket..");
          display.setCursor(0, 1);
          display.print((String("Node Id: ") + NODE_ID).c_str());
      } 

      xSemaphoreGive(displayMutex);

    }
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

void initRelay() {
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
    while (1);
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
    if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
      showMessage(0, 0, "Lora Failed");
      xSemaphoreGive(displayMutex);
    }
    while (1);
  }
  if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
    showMessage(0, 0, "LoRa Success"); 
    xSemaphoreGive(displayMutex);
  }
  delay(1000);
}

void initSDCard() {
  sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  showMessage(0, 0, "Init SD Card");
  if (!SD.begin(SD_CS, sdSPI)) {
    display.clear();
    showMessage(0, 0, "SD Failed");
    while (1);
  }
  File testFile = SD.open("/test.txt", FILE_WRITE);
  if(testFile){
    testFile.print("This is new file");
    testFile.println();
    Serial.println("Test.txt file created");
  }else {
    Serial.println("Test.txt not created");
  }
  testFile.close();
  showMessage(0, 0, "SD Card Success");
}

void showMessage(int x, int y, const char* message) {
  display.setCursor(x, y);
  display.print(message);
  Serial.println(message);
}

void connectWifi() {
  WiFi.mode(WIFI_MODE_STA);
  WiFi.begin(SSID, PASSWORD);
  netState = 1;
  while (WiFi.status() != WL_CONNECTED) {
    if (counter == 10) {
      break;
    }
    counter++;
    netState = 3;
    delay(500);
  }
  netState = 2;
  wifiClient.setTimeout(5000);
  // wifiClientSecure.setInsecure();
  // wifiClientSecure.setTimeout(5000);
}

boolean connectMQTT() {
  netState = 4;
  mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
  // mqttClient.setClient(wifiClientSecure);
  mqttClient.setClient(wifiClient);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(3);
  // if (mqttClient.connect(CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD)) {
    if(mqttClient.connect(CLIENT_ID)){
    initializeSubscribe();
    netState = 0;
    if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
      display.clear();
      showMessage(0, 0, "MQTT CONNECTED");
      xSemaphoreGive(displayMutex);
    }
  } else {
    netState = 6;
  }
  return mqttClient.connected();
}

void reconnectMQTT() {
  if(!isWiFiConnected) {
    return;
  }

  if(mqttClient.connected()) {
    currentInterval = minInterval;
    mqttClient.loop();
    return;
  }
  
  if (millis() - lastMQTTAttempt >= currentInterval) {
    boolean isConnected = false;
    netState = 4;
    lastMQTTAttempt = millis();
    if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
      display.clear();
      showMessage(0, 0, "Attempting MQTT...");
      xSemaphoreGive(displayMutex);
    }
    isConnected = connectMQTT();
    
    if(currentInterval >= maxInterval) {
      currentInterval = maxInterval;
    }else {
      currentInterval *= 2;
    }
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
  DateTime currentTime;
  if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
    currentTime = rtc.now();
    xSemaphoreGive(displayMutex);
  }
  char formatTimeStamp[] = "YYYY-MM-DD hh:mm:ss";
  String timeStamp = currentTime.toString(formatTimeStamp);
  String csvFormat = "";
  csvFormat.reserve(120);
  csvFormat += timeStamp;
  csvFormat += ";";
  csvFormat += data.nodeId;
  csvFormat += ";";
  csvFormat+= data.nodeTarget;
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
    Serial.println("File Not Found");
  }
}


void syncSDCard(int length) {

  if (!SD.exists("/logFile.csv")) {
    lastSDCardSent = millis();
    return;
  }

  File logFile = SD.open("/logFile.csv", FILE_READ);
  if (!logFile) {
    return;
  }

  if (logFile.size() == 0) {
    logFile.close();
    SD.remove("/logFile.csv");
    lastSDCardSent = millis();
    return;
  }
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
      mqttClient.loop();
      vTaskDelay(100 / portTICK_PERIOD_MS);
      
    } else {
      stillSending = false;
      remainingRows += currentRow + "\n";
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
}

void parseLoRaPacket(int packetSize) {
  isShowInfo = true;
  lastShowInfo = millis();
  lastShowPage = millis();
  currentPage = 0;
  hasNewPacket = false;
}

void checkLoRaPacket() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) {
    return;
  }

  if (packetSize == sizeof(PayloadData)) {
    PayloadData incomingData;
    LoRa.readBytes((uint8_t*)&incomingData, packetSize);

    if (incomingData.nodeTarget != NODE_ID) {
      return;
    }

    xQueueSend(dataQueue, &incomingData, 0);

    nodeId          = incomingData.nodeId;
    soilMoisture    = incomingData.soilMoisture / 10;
    soilTemperature = incomingData.soilTemperature / 10;
    conductivity    = incomingData.conductivity;
    soilPh          = incomingData.soilPh / 10;
    nitrogen        = incomingData.nitrogen;
    phosporus       = incomingData.phosporus;
    kalium          = incomingData.kalium;
    airTemperature  = incomingData.airTemperature / 10;
    airHumidity     = incomingData.airHumidity / 10;
    lightIntensity  = incomingData.lightIntensity;
    hasNewPacket = true;

    if (incomingData.nodeId == SENSOR_ID_1) {
      node1State.soilMoisture = incomingData.soilMoisture / 10;
      node1State.hasData = true;
      node1State.lastUpdateMillis = millis();
      node1State.actedOnCurrentReading = false;
    } else if (incomingData.nodeId == SENSOR_ID_2) {
      node2State.soilMoisture = incomingData.soilMoisture / 10;
      node2State.hasData = true;
      node2State.lastUpdateMillis = millis();
      node2State.actedOnCurrentReading = false;
    }
  }
}

// void loraCallback(int packetSize) {
//   if (packetSize == 0) {
//     return;
//   };

//   if (packetSize == sizeof(PayloadData)) {
//     PayloadData incomingData;
//     LoRa.readBytes((uint8_t*)&incomingData, packetSize);

//     if(incomingData.nodeTarget != NODE_ID) {
//       LoRa.receive();
//       return;
//     }
//     BaseType_t higherPriorityTaskWoken = pdFALSE;
//     xQueueSendFromISR(dataQueue, &incomingData, &higherPriorityTaskWoken);

//     nodeId          = incomingData.nodeId;
//     soilMoisture    = incomingData.soilMoisture / 10;
//     soilTemperature = incomingData.soilTemperature / 10;
//     conductivity    = incomingData.conductivity;
//     soilPh          = incomingData.soilPh / 10;
//     nitrogen        = incomingData.nitrogen;
//     phosporus       = incomingData.phosporus;
//     kalium          = incomingData.kalium;
//     airTemperature  = incomingData.airTemperature / 10;
//     airHumidity     = incomingData.airHumidity / 10;
//     lightIntensity  = incomingData.lightIntensity;
//     hasNewPacket = true;

//     if(incomingData.nodeId == SENSOR_ID_1) {
//       node1State.soilMoisture = incomingData.soilMoisture;
//       node1State.hasData = true;
//       node1State.lastUpdateMillis = millis();
//       node1State.actedOnCurrentReading = false;
//     }else if(incomingData.nodeId == SENSOR_ID_2){
//       node2State.soilMoisture = incomingData.soilMoisture;
//       node2State.hasData = true;
//       node2State.lastUpdateMillis = millis();
//       node2State.actedOnCurrentReading = false;
//     }
//     if(higherPriorityTaskWoken) {
//       portYIELD_FROM_ISR();
//     }
//     LoRa.receive();
//   }
// }

void mqttCallback(const char* topic, byte* payload, int length) {
  if (length == 0) return;
  Serial.println("mqtt msg received");
  String currentTopic = String(topic);

  if (currentTopic.indexOf("control") != -1) {
    boolean status = (payload[0] == '1' ? LOW : HIGH);
    if(currentTopic.indexOf("valve1") != -1) {
      reqValve1 = status;
    }
    if(currentTopic.indexOf("valve2") != -1 ) {
      reqValve2 = status;
    }
  }

  if (currentTopic.indexOf("get") != -1) {
    reportValve1 = true;
    reportValve2 = true;
  }
}

void controlValve1(boolean status) {
  if (valve1State == status) {
    return;
  }
  digitalWrite(RELAY_PIN_1, status);
  valve1State = status;
  reqValve1 = status;
  lastValve1Activate = millis();
  lastValve1State = valve1State;
  if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
    showMessage(0, 1, !status ? "Valve1 ON" : "Valve1 OFF");
    xSemaphoreGive(displayMutex);
  }
  reportValve1 = true;
}

void controlValve2(boolean status) {
  if (valve2State == status) {
    return;
  }
  digitalWrite(RELAY_PIN_2, status);
  valve2State = status;
  reqValve2 = status;
  lastValve2Activate = millis();
  lastValve2State = valve2State;
  if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
    showMessage(0, 1, !status ? "Valve2 ON" : "Valve2 OFF");
    xSemaphoreGive(displayMutex);
  }
  reportValve2 = true;
}

void sentStatusActuator(const char* topic) {
  String currentTopic = String(topic);

  if (currentTopic.indexOf("valve1") != -1) {
    reportValve1 = true;
  }

  if (currentTopic.indexOf("valve2") != -1) {
   reportValve2 = true;
  }
}

void manageValve1() {
  if(!node1State.hasData){
    return;
  }

  if(node1State.isWatering && (millis() - node1State.lastWatering >= 15 * 1000)) {
    controlValve1(HIGH);
    node1State.isWatering = false;
  }

  if(node1State.isWatering && node1State.soilMoisture >= 60) {
    controlValve1(HIGH);
    node1State.isWatering = false;
    node1State.actedOnCurrentReading = false;
    return;
  }

  if(node1State.actedOnCurrentReading) {
    return;
  }

  if (node1State.soilMoisture <= 20) {
    controlValve1(LOW);
    node1State.lastWatering = millis();
    node1State.isWatering = true;
    node1State.actedOnCurrentReading = true;
    
  } else if (node1State.soilMoisture >= 60) {
    controlValve1(HIGH);
    node1State.isWatering = false;
    node1State.actedOnCurrentReading = false;
  }else {
    node1State.actedOnCurrentReading = true;
  }
}

void manageValve2() {
  if(!node2State.hasData){
    return;
  }

  if(node2State.isWatering && (millis() - node2State.lastWatering >= 15 * 1000)) {
    controlValve2(HIGH);
    node2State.isWatering = false;
  }

  if(node2State.isWatering && node2State.soilMoisture >= 60) {
    controlValve2(HIGH);
    node2State.isWatering = false;
    node2State.actedOnCurrentReading = false;
    return;
  }

  if(node2State.actedOnCurrentReading) {
    return;
  }

  if (node2State.soilMoisture <= 20) {
    controlValve2(LOW);
    node2State.isWatering = true;
    node2State.lastWatering = millis();
    node2State.actedOnCurrentReading = true;
    
  } else if (node2State.soilMoisture >= 60) {
    controlValve2(HIGH);
    node2State.isWatering = false;
    node2State.actedOnCurrentReading = false;
    
  }else {
    node2State.actedOnCurrentReading = true;
  }
}

void manageWiFi() {
  if(WiFi.status() == WL_CONNECTED && isWiFiConnected) {
    netState = 0;
    return;
  }
  if(WiFi.status() == WL_CONNECTED && !isWiFiConnected) {
    isWiFiConnected = true;
    netState = 2;
    return;
  }
  
  isWiFiConnected = false;

  if(millis() - lastWiFiAttempt >= 10000) {
    lastWiFiAttempt = millis();
    netState = 1;
    WiFi.disconnect();
    WiFi.begin(SSID, PASSWORD);
  }
}

void setup() {
  Serial.begin(9600);

  displayMutex = xSemaphoreCreateMutex();
  dataQueue = xQueueCreate(10, sizeof(PayloadData));

  initRelay();
  lastValve1Activate = millis();
  lastValve2Activate = millis();

  if(xSemaphoreTake(displayMutex, portMAX_DELAY)) {
    display.init();
    display.backlight();
    showMessage(0, 0, "Initialize...");
    delay(2000);
    display.clear();
    xSemaphoreGive(displayMutex);
  }
  initLoRa();

  xTaskCreatePinnedToCore(
    networkManagementTask,
    "NETWORK_MANAGEMENT_TASK",
    15000,
    NULL,
    1,
    NULL,
    0
  );
}

void loop() {
  checkLoRaPacket();
  if (hasNewPacket) {
    parseLoRaPacket(sizeof(payload));
  }

  if (isManual) {
    if (millis() - lastManualActive >= 15 * 1000) {
      reqValve1 = 1;
      reqValve2 = 1;
      controlValve1(1);
      controlValve2(1);
      isManual = false;
    }
  } else {
    manageValve1();
    manageValve2();
  }

  if (!valve1State && !isManual && (millis() - lastValve1Activate >= 15 * 1000)) {
    controlValve1(1);
  }

  if (!valve2State && !isManual && (millis() - lastValve2Activate >= 15 * 1000)) {
    controlValve2(1);
  }

  if (reqValve1 != valve1State) {
    controlValve1(reqValve1);
    isManual = true;

    lastManualActive = millis();
  }
  if (reqValve2 != valve2State) {
    controlValve2(reqValve2);
    isManual = true;
    lastManualActive = millis();
  }

  displayLoop();
}