#include <SPI.h>
#include <LoRa.h>
#include <LiquidCrystal_I2C.h>

#define SS 25
#define DIO 26
#define RST 27

LiquidCrystal_I2C display(0x27, 16, 2);

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

struct PayloadData payload;

unsigned long lastDisplayMillis = 0;
unsigned long lastPacketMillis = 0;
int lcdPage = 0;             
bool hasNewPacket = false; 

int8_t nodeId = 0;
int16_t n = 0, p = 0, k = 0, ec = 0;
float moistS = 0.0, tempS = 0.0, phS = 0.0, tempA = 0.0, humA = 0.0;
uint16_t lux = 0;

void setupLoRa() {
  LoRa.setPins(SS, RST, DIO);
  if(!LoRa.begin(923E6)) {
    Serial.println("Lora Failed");
    display.setCursor(0,0);
    display.print("LoRa Failed");
    while(1);
  }
  LoRa.setSpreadingFactor(7);
  Serial.println("Lora connected");
  display.print("Lora Connected");
}

void setup() {
  Serial.begin(9600);
  display.init();
  display.backlight();
  setupLoRa(); 
  delay(3000);
  display.clear();
}

void loop() {
  unsigned long currentMillis = millis();


  int packetSize = LoRa.parsePacket();
  
  if (packetSize) {
    Serial.print("Paket Terdeteksi! Ukuran: ");
    Serial.println(packetSize);

    if (packetSize == sizeof(payload)) {
      LoRa.readBytes((uint8_t*)&payload, packetSize);
      
      nodeId  = payload.nodeId;
      n       = payload.nitrogen;
      p       = payload.phosporus;
      k       = payload.kalium;
      moistS  = payload.soilMoisture / 10.0;
      tempS   = payload.soilTemperature / 10.0;
      ec      = payload.conductivity;
      phS     = payload.soilPh / 10.0;
      tempA   = payload.airTemperature / 10.0;
      humA    = payload.airHumidity / 10.0;
      lux     = payload.lightIntensity;

      Serial.println(nodeId);

      hasNewPacket = true;
      lcdPage = 1;                     
      lastDisplayMillis = currentMillis;
      display.clear();
    } else {
      Serial.println("[WARNING] Paket diabaikan karena ukurannya tidak cocok (Korup)!");
      display.clear();
      display.setCursor(0, 0);
      display.print("Paket Korup!");
      lastPacketMillis = currentMillis;
    }
  }


  
  if (hasNewPacket) {
    if (lcdPage == 1) {
      display.setCursor(0,0);
      display.print("N:"); display.print(n);
      display.setCursor(8,0);
      display.print("P:"); display.print(p);
      display.setCursor(0,1);
      display.print("K:"); display.print(k);
      display.setCursor(8,1);
      display.print("M:"); display.print(moistS, 1);

      if (currentMillis - lastDisplayMillis >= 4000) {
        lcdPage = 2;
        lastDisplayMillis = currentMillis;
        display.clear();
      }
    } 
    else if (lcdPage == 2) {
      display.setCursor(0,0);
      display.print("Ts:"); display.print(tempS, 1);
      display.setCursor(8,0);
      display.print("pH:"); display.print(phS, 1);
      display.setCursor(0,1);
      display.print("Ta:"); display.print(tempA, 1);
      display.setCursor(8,1);
      display.print("Hu:"); display.print(humA, 1);

      if (currentMillis - lastDisplayMillis >= 4000) {
        hasNewPacket = false; 
        lcdPage = 0;          
        display.clear();
      }
    }
  } 
  else {
    if (currentMillis - lastDisplayMillis >= 500) {
      lastDisplayMillis = currentMillis;
      
      display.setCursor(0, 0);
      display.print("Menunggu Paket...");
      display.setCursor(0, 1);
      display.print("Node ID: Ready  "); 
    }
  }
}