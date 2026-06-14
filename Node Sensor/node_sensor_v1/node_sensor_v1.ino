  /*
    Urutan data yang ada di datasheetnya
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

#define NODE_ID 1

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

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const byte nitrogenRequest[8] = {0x01, 0x03, 0x00, 0x04, 0x00, 0x01, 0xC5, 0xCB};
const byte allDataRequest[8] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08};
byte result[19];

DHT dht(dhtPin, dhtType);

byte page = 1;

unsigned long startTime = millis();



float soilMoisture = 0.0;
float soilTemperature = 0.0;
float conductivity = 0.0;
float soilPh = 0.0;
int nitrogen = 0.0;
int phosporus = 0.0;
int kalium = 0.0;
float airTemperature = 0.0;
float airHumidity = 0.0;
int lightIntensity = 0;

#pragma pack(push, 1)
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
};
#pragma pack(pop)

// FUNCTION
byte calculateRemainingSpace(char* title, byte textSize = 1);
byte calculateCenterCursor(char* title, byte textSize = 1);
void setTitle(char* title, byte textSize = 2);
void displayNPK();
void displayEnvironment();
void displaySoilParameter();
void requestData();

void setup() {
  Serial.begin(9600);
  Serial2.begin(4800, SERIAL_8N1, RX2, TX2);
  

  if(!Serial || !Serial2) {
    Serial.println("Serial or Serial 2 is not ok");
  }
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)){
    Serial.println("Display alocation failed");
    while(1);
  }
  setupLoRa();
  dht.begin();
  display.display();
  delay(2000); 
  display.clearDisplay();
  display.display();

  pinMode(RE_DE, OUTPUT);
  pinMode(ldrAnalogPin, INPUT);
  digitalWrite(RE_DE, LOW);
  delay(1000);
}

void loop() {

  if(millis() - startTime >= 5000) {
    startTime = millis();
    if(page > 3) {
      page = 1;
    }
    airHumidity = dht.readHumidity();
    airTemperature = dht.readTemperature();

    if(isnan(airHumidity) || isnan(airTemperature)) {
      Serial.println("Failed to read dht value");
    }

    uint16_t ldrAnalogValue = analogRead(ldrAnalogPin);
    lightIntensity = calculateLux(ldrAnalogValue);
    requestData();
    displayToScreen();
    sendDataViaLoRa();
  }
}

float calculateLux(uint16_t ldrValue) {
  const float ldrResistance = calculateResistance(ldrValue);
  const float A = 500000;
  const float B = 0.7;

  float lux = pow(A/ldrResistance,(1.0 / B));
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
  digitalWrite(RE_DE, HIGH);
  delay(10);
  Serial2.write(allDataRequest, sizeof(allDataRequest));
  Serial2.flush();
  digitalWrite(RE_DE, LOW);
  delay(10);

  unsigned long startRequest = millis();
  while(Serial2.available() < 19) {
    if(millis() - startRequest >= 3000) {
      Serial.println("Sensor NPK tidak merespon");
      return; 
    }
  }

  for(byte i = 0; i < sizeof(result); i++){
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
  soilMoisture = (int)((result[3] << 8) | result[4]) * 0.1;
  soilTemperature = (int)((result[5] << 8) | result[6]) * 0.1;
  conductivity = (int)((result[7] << 8) | result[8]);
  soilPh = (int)((result[9] << 8) | result[10]) * 0.1;
  nitrogen = (int)((result[11] << 8) | result[12]);
  phosporus = (int)((result[13] << 8) | result[14]);
  kalium = (int)((result[15] << 8) | result[16]);
}

void clearScreen() {
  display.clearDisplay();
}

void displayToScreen() {
  clearScreen();
  display.setTextColor(SSD1306_WHITE);
  switch(page) {
    case 1:
      displayNPK();
      break;
    case 2:
      displaySoilParameter();
      break;
    case 3:
      displayEnvironment();
      break;
    default:
      displayNPK();
      break;
  }
  page++;
}

void displaySoilParameter() {
  setTitle("TANAH", 1);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 15);
  display.print("Suhu: "); display.print(soilTemperature, 1); display.println(" C");
  display.setCursor(0, 28);
  display.print("Kelembaban: "); display.print(soilMoisture, 1); display.println(" %");
  display.setCursor(0, 41);
  display.print("pH: "); display.println(soilPh);
  display.setCursor(0, 54);
  display.print("EC: "); display.print((int)conductivity); display.println(" uS");
  display.display();
}

void displayEnvironment() {
  setTitle("LINGKUNGAN", 1);
  display.drawFastHLine(0, 10, 128, SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 20);
  display.print("Suhu Udara: "); display.print(airTemperature, 1); display.println(" C");
  display.setCursor(0, 38);
  display.print("Kelembaban U: "); display.print(airHumidity, 1); display.println(" %");
  display.setCursor(0, 56);
  display.print("Cahaya: "); display.print(lightIntensity); display.println(" lux");
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

void setTitle(char* title, byte textSize) {
  display.setTextSize(textSize);
  byte centerCursor = calculateCenterCursor(title, textSize);
  display.setCursor(centerCursor, 0);
  display.print(title);
}

byte calculateRemainingSpace(char* title, byte textSize) {
  byte textLength = strlen(title);
  byte widthForEachCharacter = 6;
  byte textWidth = textLength * widthForEachCharacter * textSize;
  return SCREEN_WIDTH - textWidth;
}

byte calculateCenterCursor(char* title, byte textSize) {
  byte remainingSpace = calculateRemainingSpace(title, textSize);
  return remainingSpace / 2;
}


void setupLoRa() {
  LoRa.setPins(SS, RST, DIO);
  LoRa.setSpreadingFactor(7);
  
  if(!LoRa.begin(923E6)) {
    Serial.println("Lora Starting Failed");
    
    while(1);
  }
  Serial.println("LoRa connected");
}

void sendDataViaLoRa() {
  struct PayloadData payload = {
    NODE_ID,
    soilMoisture * 10,
    soilTemperature * 10,
    conductivity,
    soilPh * 10,
    nitrogen,
    phosporus,
    kalium,
    airTemperature * 10,
    airHumidity * 10,
    lightIntensity
  };
  LoRa.beginPacket();
  LoRa.write((uint8_t*)&payload, sizeof(payload));
  LoRa.endPacket();
}