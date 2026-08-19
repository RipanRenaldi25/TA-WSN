/*
  List Register Address Masing-Masing parameter
  1. moisture 0x00
  2. temperature 0x01
  3. conductivity 0x02
  4. pH 0x03
  5. N 0x04
  6. P 0x05
  7. K 0x06
*/

#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <SPI.h>
#include <LoRa.h>
#include <esp_sleep.h>
#include "driver/rtc_io.h"

#define NODE_ID 2
#define SINK_NODE_ID 255

// SENSOR
#define RE_DE 4
#define RX2 16
#define TX2 17

// Screen
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// DHT22
#define dhtType DHT22
#define dhtPin 32

// LDR
#define ldrAnalogPin 34

// LoRa
#define SS 25
#define DIO 26
#define RST 27

// DEEP SLEEP
#define USTOS 1000000ULL   
#define PAGEDISPLAYDELAY 5000
#define BLUE_LED_PIN GPIO_NUM_2

// unsigned long long TIMETOSLEEP = 300; << 5 * 60

const int BASESLEEPTIME = 900;
RTC_DATA_ATTR unsigned long long TIMETOSLEEP = 0;
RTC_DATA_ATTR boolean hasBootBefore = false;

int minSleepRange = 10;
int maxSleepRange = 120;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const byte soilMoistureRequest[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
const byte allDataRequest[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};
byte result[19];
byte moistureResult[7];


DHT dht(dhtPin, dhtType);

float soilMoisture = 0.0;
float soilTemperature = 0.0;
float conductivity = 0.0;
float soilPh = 0.0;
int nitrogen = 0;
int phosporus = 0;
int kalium = 0;
float airTemperature = 0.0;
float airHumidity = 0.0;
int lightIntensity = 0;

const float soilMoistureThreshold = 60.0;

const float bN = 1.63;
const float aN = 16.65;

const float bP = 1.33;
const float aP = 19.08;

const float bK = 1.33;
const float aK = 38.85;

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
};
#pragma pack(pop)

byte calculateRemainingSpace(const char* title, byte textSize = 1);
byte calculateCenterCursor(const char* title, byte textSize = 1);
void setTitle(const char* title, byte textSize = 2);
void displayNPK();
void displayEnvironment();
void displaySoilParameter();
void requestData();
void goToSleep();
void handleWakeupStream(uint16_t wakeDurationInSec);
void readAndSendAllSensor();
PayloadData readSensor();
void sendSensor(PayloadData &payload);
void generateSleepTime();
boolean checkChannel();
void readSoilMoisture();
void initDisplay();
uint16_t calibratedData(char parameter, int data);

uint16_t calibratedData(char parameter, int data) {
  uint16_t result = 0;
  if(parameter == 'N') {
    result = aN + bN * data ;
  }else if(parameter == 'P') {
    result = aP + bP * data;
  }else {
    result = aK + bK * data;
  }
  return result <= 0 ? 0 : result;
}

void generateSleepTime() {
  if(hasBootBefore){
    return;
  }
  TIMETOSLEEP = BASESLEEPTIME + random(minSleepRange, maxSleepRange);
  hasBootBefore = true;
  clearScreen();
  Serial.println("=================== SLEEP TIME ================");
  Serial.print("Sleep Time:   ");
  Serial.println(TIMETOSLEEP);
  Serial.println("=============================================");
}

boolean checkChannel() {
  const byte maxAttempt = 3;
  byte counter = 0;
  while(counter < maxAttempt) {
    int packetSize = LoRa.parsePacket();
    if(packetSize == 0) {
      return true;
    }
    delay(random(1000, 3000));
    counter++;
  }
  return false;
}


void goToSleep() {
  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  pinMode(BLUE_LED_PIN, OUTPUT);
  digitalWrite(BLUE_LED_PIN, LOW);
  gpio_hold_en(BLUE_LED_PIN);
  esp_sleep_enable_timer_wakeup(TIMETOSLEEP * USTOS);
  LoRa.sleep();
  Serial.print("TIMETOSLEEP: ");
  Serial.println(TIMETOSLEEP * USTOS);
  Serial.flush();
  esp_deep_sleep_start();
}

float calculateLux(uint16_t ldrValue) {
  const float ldrResistance = calculateResistance(ldrValue);
  const float A = 500000;
  const float B = 0.7;

  float lux = pow(A / ldrResistance,(1.0 / B));
  return lux;
}

float calculateResistance(uint16_t ldrValue) {
  uint16_t twelveBit = 4095;
  float VCC = 3.3;
  float voltageOutput = ((float)ldrValue / (float)twelveBit) * VCC;
  if(voltageOutput >= VCC) {
    voltageOutput -= 0.01;
  }
  if(voltageOutput <= 0) {
    voltageOutput = 0.01;
  }
  float fixedResistor = 10000;

  float resistanceLdr = fixedResistor * (voltageOutput / ( VCC - voltageOutput ) );

  return resistanceLdr;
}

void requestData() {
  while (Serial2.available() > 0) {
    Serial2.read();
  }
  digitalWrite(RE_DE, HIGH);
  delay(10);
  Serial2.write(allDataRequest, sizeof(allDataRequest));
  Serial2.flush();
  digitalWrite(RE_DE, LOW);
  delay(100);

  unsigned long startRequest = millis();
  Serial.println("== Data Umum ==");
  Serial.print("Serial Available: ");
  Serial.print(Serial2.available());
  while(Serial2.available() < 19) {
    if(millis() - startRequest >= 3000) {
      Serial.println("Sensor NPK tidak merespon");
      return;
    }
  }

  while(Serial2.available() >= 19) {
    if(Serial2.peek() == 0x01) {
      break;
    }
    Serial2.read();
  }
  Serial.print("Sizeof Result: ");
  Serial.println(sizeof(result));
  Serial.println("===============");
  for(byte i = 0; i < 19; i++){
    result[i] = Serial2.read();
  }
  /*List parameter tanah
    1. moisture
    2. temperature
    3. conductivity
    4. pH
    5. N
    6. P
    7. K
  */
  soilMoisture = (uint16_t)((result[3] << 8) | result[4]) * 0.1;
  soilTemperature = (uint16_t)((result[5] << 8) | result[6]) * 0.1;
  conductivity = (uint16_t)((result[7] << 8) | result[8]);
  soilPh = (uint16_t)((result[9] << 8) | result[10]) * 0.1;
  nitrogen = (uint16_t)((result[11] << 8) | result[12]);
  phosporus = (uint16_t)((result[13] << 8) | result[14]);
  kalium = (uint16_t)((result[15] << 8) | result[16]);

  nitrogen = calibratedData('N', nitrogen);
  phosporus = calibratedData('P', phosporus);
  kalium = calibratedData('K', kalium);
  
  while (Serial2.available() > 0) {
    Serial2.read();
  }
}

void clearScreen() {
  display.clearDisplay();
}

void displayPage(byte pageNumber) {
  clearScreen();
  display.setTextColor(SSD1306_WHITE);
  switch(pageNumber) {
    case 1:
      displayNPK();
      break;
    case 2:
      displaySoilParameter();
      break;
    case 3:
      displayEnvironment();
      break;
    case 4:
      displayGoSleep();
      break;
    default:
      displayNPK();
      break;
  }
}

void displayGoSleep() {
  byte x;  
  clearScreen();
  display.setTextSize(3);
  x = calculateCenterCursor("3", 3);
  display.setCursor(x, 20); 
  display.print("3");
  display.display();
  delay(1000); 

  clearScreen();
  display.setTextSize(2);
  x = calculateCenterCursor("2", 3);
  display.setCursor(x, 20);
  display.print("2");
  display.display();
  delay(1000);

  clearScreen();
  display.setTextSize(1);
  x = calculateCenterCursor("1", 3);
  display.setCursor(x, 20); 
  display.print("1");
  display.display();
  delay(1000);

  clearScreen();
  display.setTextSize(1);
  String sleepText = "Sleep in " + String(round(TIMETOSLEEP / 60)) + " min";
  setTitle(sleepText.c_str(), 1);
  x = calculateCenterCursor("Sleep for x sec", 1);
  String sleepTextSec = "Sleep for " + String(TIMETOSLEEP) + " sec";
  display.setCursor(x, 28);
  display.print(sleepTextSec.c_str());
  display.display();
  delay(1500);
}

void displaySoilParameter() {
  setTitle("TANAH", 1);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 15);
  display.print("Suhu: ");
  display.print(soilTemperature, 1);
  display.println(" C");
  display.setCursor(0, 28);
  display.print("Kelembaban: ");
  display.print(soilMoisture, 1);
  display.println(" %");
  display.setCursor(0, 41);
  display.print("pH: ");
  display.println(soilPh);
  display.setCursor(0, 54);
  display.print("EC: ");
  display.print((int)conductivity);
  display.println(" uS");
  display.display();
}

void displayEnvironment() {
  setTitle("LINGKUNGAN", 1);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("Suhu Udara: "); 
  display.print(airTemperature, 1); 
  display.println(" C");
  display.setCursor(0, 38);
  display.print("Kelembaban U: "); 
  display.print(airHumidity, 1); 
  display.println(" %");
  display.setCursor(0, 56);
  display.print("Cahaya: "); 
  display.print(lightIntensity); 
  display.println(" lux");
  display.display();
}

void displayNPK() {
  setTitle("NUTRISI", 2);
  display.setTextSize(2);
  display.setCursor(0, 17);
  display.print("N:"); display.print(nitrogen); display.print(" mg/kg");
  display.setCursor(0, 33);
  display.print("P:"); display.print(phosporus); display.print(" mg/kg");
  display.setCursor(0, 49);
  display.print("K:"); display.print(kalium); display.print(" mg/kg");
  display.display();
}

void setTitle(const char* title, byte textSize) {
  display.setTextSize(textSize);
  byte centerCursor = calculateCenterCursor(title, textSize);
  display.setCursor(centerCursor, 0);
  display.print(title);
}

byte calculateRemainingSpace(const char* title, byte textSize) {
  byte textLength = strlen(title);
  byte widthForEachCharacter = 6;
  byte textWidth = textLength * widthForEachCharacter * textSize;
  return SCREEN_WIDTH - textWidth;
}

byte calculateCenterCursor(const char* title, byte textSize) {
  byte remainingSpace = calculateRemainingSpace(title, textSize);
  return remainingSpace / 2;
}

void initLoRa() {
  LoRa.setPins(SS, RST, DIO);

  if(!LoRa.begin(923E6)) {
    Serial.println("Lora Starting Failed");
    while(1);
  }
  LoRa.setSpreadingFactor(7);
  Serial.println("LoRa connected");
}

void sendDataViaLoRa() {
  struct PayloadData payload = {
    (uint8_t)NODE_ID,
    (uint8_t)SINK_NODE_ID,
    (int16_t)(soilMoisture * 10),
    (int16_t)(soilTemperature * 10),
    (int16_t)conductivity,
    (int16_t)(soilPh * 10),
    (int16_t)nitrogen,
    (int16_t)phosporus,
    (int16_t)kalium,
    (int16_t)(airTemperature * 10),
    (int16_t)(airHumidity * 10),
    (int16_t)lightIntensity
  };
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&payload, sizeof(payload));
  LoRa.endPacket();
}

void handleWakeupStream(uint16_t wakeDurationInSec) {
  unsigned long durationMs = wakeDurationInSec * 1000;
  unsigned long startTime = millis();
  unsigned long lastCheckTime = 0;

  const unsigned long CHECK_INTERVAL = 5000;
  while(millis() - startTime < durationMs) {

    if(millis() - lastCheckTime >= CHECK_INTERVAL) {
      lastCheckTime = millis();
      Serial.println("============== Data Saat Wakeup Stream ==============");
      requestData();
      Serial.print("Kelembaban Tanah: ");
      Serial.println(soilMoisture);
      clearScreen();
      setTitle("Kelembaban");
      display.drawFastHLine(0, 17, 128, SSD1306_WHITE);
      byte centerCursor = calculateCenterCursor("x.xx %", 2);
      display.setCursor(centerCursor, 17);
      display.print(soilMoisture);
      display.print(" %");
      display.display();
      delay(1000);
      readAndSendAllSensor();
      Serial.println("=====================================================");
      if(soilMoisture >= soilMoistureThreshold) {
      
        break;
      }
    }
    delay(10);
  }
}

PayloadData readSensor() {
  airTemperature = dht.readTemperature();
  airHumidity = dht.readHumidity();
  uint16_t ldrAnalogValue = analogRead(ldrAnalogPin);
  lightIntensity = calculateLux(ldrAnalogValue);
  requestData();

  PayloadData payload = {
    (uint8_t)NODE_ID,
    (uint8_t)SINK_NODE_ID,
    (int16_t)(soilMoisture * 10),
    (int16_t)(soilTemperature * 10),
    (int16_t)conductivity,
    (int16_t)(soilPh * 10),
    (int16_t)nitrogen,
    (int16_t)phosporus,
    (int16_t)kalium,
    (int16_t)(airTemperature * 10),
    (int16_t)(airHumidity * 10),
    (int16_t)lightIntensity
  };

  Serial.println("=== DATA SENSOR TANAH NPK (RS485) ===");
  
  Serial.print("Kelembapan Tanah : ");
  Serial.print(soilMoisture, 1);
  Serial.println(" %");

  Serial.print("Suhu Tanah       : ");
  Serial.print(soilTemperature, 1);
  Serial.println(" °C");

  Serial.print("Konduktivitas EC : ");
  Serial.print(conductivity);
  Serial.println(" us/cm");

  Serial.print("pH Tanah         : ");
  Serial.print(soilPh, 1);
  Serial.println("");

  Serial.print("Nitrogen (N)     : ");
  Serial.print(nitrogen);
  Serial.println(" mg/kg");

  Serial.print("Fosfor (P)       : ");
  Serial.print(phosporus);
  Serial.println(" mg/kg");

  Serial.print("Kalium (K)       : ");
  Serial.print(kalium);
  Serial.println(" mg/kg");

  Serial.print("Suhu Udara (K)       : ");
  Serial.print(airTemperature);
  Serial.println(" °C");
  
  Serial.print("Kelembaban Udara (K)       : ");
  Serial.print(airHumidity);
  Serial.println(" %");

  Serial.print("Intensitas Cahaya       : ");
  Serial.print(lightIntensity);
  Serial.println(" lux");


  Serial.println("====================================");

  
  return payload;
}

void sendSensor(PayloadData &payload) {
  boolean isChannelClear = checkChannel();
  if(!isChannelClear) {
    Serial.println("Channel Sibuk, Pengiriman dibatalkan");
    return;
  }
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&payload, sizeof(payload));
  LoRa.endPacket();
}

void readAndSendAllSensor() {
  PayloadData payloadToSend = readSensor();
  sendSensor(payloadToSend);
}

void readSoilMoisture() {
  digitalWrite(RE_DE, HIGH);
  delay(10);
  Serial2.write(soilMoistureRequest, sizeof(soilMoistureRequest));
  Serial2.flush();
  digitalWrite(RE_DE, LOW);
  delay(100);
  long lastCheck = millis();
  while(Serial2.available() < 7) {
    if(millis() - lastCheck >= 5000) {
      Serial.println("======== Moisture Pertama =========");
      Serial.println("Sensor NPK Tidak merespon");
      Serial.println("========================");
      return;
    }
  }
  while(Serial2.available() >= 7) {
    if(Serial2.peek() == 0x01) {
      break;
    }
    Serial2.read();
  }
  for(byte i = 0; i < 7; i++) {
    moistureResult[i] = Serial2.read();
    Serial.print(moistureResult[i], HEX);
  }
  Serial.println();
  soilMoisture = (uint16_t)((moistureResult[3] << 8) | moistureResult[4]) * 0.1;
  Serial.println("Nilai Soil moisture");
  Serial.println(soilMoisture);
}

void initDisplay() {
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)){
    Serial.println("Display alocation failed");
    while(1);
  }
  display.display();
  delay(1000);
  clearScreen();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);
}

void setup() {
  delay(3000);
  gpio_hold_dis(BLUE_LED_PIN);
  Serial.begin(9600);
  Serial2.begin(NODE_ID == 1 ? 9600 : 4800, SERIAL_8N1, RX2, TX2);
  delay(100);
  pinMode(RE_DE, OUTPUT);
  pinMode(ldrAnalogPin, INPUT);
  digitalWrite(RE_DE, LOW);
  delay(10);
  while(Serial2.available()){
    Serial2.read();
  }
  delay(1000);

  if(!Serial || !Serial2) {
    Serial.println("Serial or Serial 2 is not ok");
  }
  initDisplay();
  

  generateSleepTime();
  initLoRa();
  delay(500);
  dht.begin();
  delay(500);
  PayloadData dataToSend = readSensor();
  delay(500);
  sendSensor(dataToSend);
  delay(500);
  display.ssd1306_command(SSD1306_DISPLAYON);
  displayPage(1);
  delay(PAGEDISPLAYDELAY);
  displayPage(2);
  delay(PAGEDISPLAYDELAY);
  displayPage(3);
  delay(PAGEDISPLAYDELAY);
  // delay(PAGEDISPLAYDELAY);
  // displayPage(4);
  if(soilMoisture <= 20) {
    clearScreen();
    display.display();
    handleWakeupStream(60);
  }
  goToSleep();
}

void loop() {}