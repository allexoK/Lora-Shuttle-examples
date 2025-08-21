#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "Wire.h"
#include "Ticker.h"

#define I2C_DEV_ADDR 0x55
#define I2C_REG_DEVEUI  0x01
#define I2C_REG_APPEUI  0x02
#define I2C_REG_APPKEY  0x03
#define I2C_REG_CHANMSK 0x05
#define I2C_REG_DEF_DR  0x06
#define I2C_REG_ADR     0x07
#define I2C_REG_CONFIRM 0x08
#define I2C_REG_JOIN    0x10
#define I2C_REG_PAYLOAD 0x11
#define I2C_REG_STATUS  0x12
#define I2C_STATUS_DONE     0x01
#define I2C_STATUS_SUCCESS  0x02
#define I2C_STATUS_JOINED   0x04
#define I2C_REG_SEND    0x13
#define I2C_REG_CSEND   0x14


char rxBuffer[LORAWAN_APP_DATA_MAX_SIZE];
uint8_t rxLen = 0;
uint32_t pktCounter = 0;
bool startRequested = false;
volatile bool lastTxSuccess = true;
volatile bool txDone = true;
volatile uint8_t txStatus = 0;
volatile uint8_t lastReadRegister = 0;
bool triggerSend = false;

/* OTAA para*/
uint8_t devEui[8] = {0};
uint8_t appEui[8] = {0};
uint8_t appKey[16] = {0};

/* ABP para*/
uint8_t nwkSKey[] = { 0x15, 0xb1, 0xd0, 0xef, 0xa4, 0x63, 0xdf, 0xbe, 0x3d, 0x11, 0x18, 0x1e, 0x1e, 0xc7, 0xda,0x85 };
uint8_t appSKey[] = { 0xd7, 0x2c, 0x78, 0x75, 0x8c, 0xdc, 0xca, 0xbf, 0x55, 0xee, 0x4a, 0x77, 0x8d, 0x16, 0xef,0x67 };
uint32_t devAddr =  ( uint32_t )0x007e6ae1;

/*LoraWan channelsmask, default channels 0-7*/
uint16_t userChannelsMask[6] = {0x00FF, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};

/*LoraWan region, select in arduino IDE tools*/
LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

/*LoraWan Class, Class A and Class C are supported*/
DeviceClass_t  loraWanClass = CLASS_A;

/*the application data transmission duty cycle.  value in [ms].*/
uint32_t appTxDutyCycle = 10000;

/*OTAA or ABP*/
bool overTheAirActivation = true;

/*ADR enable*/
bool loraWanAdr = true;

/* Indicates if the node is sending confirmed or unconfirmed messages */
bool isTxConfirmed = true;

/* Application port */
uint8_t appPort = 2;

/* Number of trials to transmit the frame, if the LoRaMAC layer did not receive an acknowledgment */
uint8_t confirmedNbTrials = 4;

uint8_t dataRate = 4;

void onRequest() {
  if (lastReadRegister == I2C_REG_STATUS) {
    Serial.println("Send status");
    Serial.println(txStatus);    
    Wire.write(txStatus);
  } else {
    Wire.write(0x00);
  }
}

void onReceive(int byteCount) {
  if (byteCount == 0) return;
  uint8_t reg = Wire.read();
  lastReadRegister = reg;

  switch (reg) {
    case I2C_REG_SEND:
      Serial.println("Set I2C_REG_SEND");
      triggerSend = true;      
      while (Wire.available()) Wire.read();
      break;
    case I2C_REG_CSEND:
      Serial.println("Set I2C_REG_CSEND");
      txStatus = txStatus & ~I2C_STATUS_SUCCESS & ~I2C_STATUS_DONE;
      while (Wire.available()) Wire.read();
      break;
    case I2C_REG_STATUS:
      while (Wire.available()) Wire.read();
      break;    
    case I2C_REG_DEVEUI:
      Serial.println("Set I2C_REG_DEVEUI");
      for (uint8_t i = 0; i < sizeof(devEui) && Wire.available(); i++) {
        devEui[i] = Wire.read();
      }
      break;

    case I2C_REG_APPEUI:
      Serial.println("Set I2C_REG_APPEUI");
      for (uint8_t i = 0; i < sizeof(appEui) && Wire.available(); i++) {
        appEui[i] = Wire.read();
      }
      break;

    case I2C_REG_APPKEY:
      Serial.println("Set I2C_REG_APPKEY");
      for (uint8_t i = 0; i < sizeof(appKey) && Wire.available(); i++) {
        appKey[i] = Wire.read();
      }
      break;

    case I2C_REG_CHANMSK:
      for (uint8_t i = 0; i < 6 && Wire.available() >= 2; i++) {
        uint8_t lsb = Wire.read();
        uint8_t msb = Wire.read();
        userChannelsMask[i] = (msb << 8) | lsb;
      }
      while (Wire.available()) Wire.read();
      break;

    case I2C_REG_DEF_DR:
      if (Wire.available()) {
        dataRate = Wire.read();
      }
      while (Wire.available()) Wire.read();
      break;

    case I2C_REG_ADR:
      if (Wire.available()) {
        loraWanAdr = Wire.read() != 0;
      }
      while (Wire.available()) Wire.read();
      break;

    case I2C_REG_CONFIRM:
      if (Wire.available()) {
        isTxConfirmed = Wire.read() != 0;
        if (Wire.available()) {
          confirmedNbTrials = Wire.read();
        }
      }
      while (Wire.available()) Wire.read();
      break;

    case I2C_REG_JOIN:
      Serial.println("Set I2C_REG_JOIN");
      startRequested = true;
      deviceState = DEVICE_STATE_INIT;
      while (Wire.available()) Wire.read();
      break;

    case I2C_REG_PAYLOAD:
      rxLen = 0;
      while (Wire.available() && rxLen < sizeof(rxBuffer)) {
        rxBuffer[rxLen++] = Wire.read();
      }
      memcpy(appData, rxBuffer, rxLen);
      appDataSize = rxLen;
      rxBuffer[rxLen] = 0;
      Serial.println(rxLen);
      Serial.println(txStatus);
      Serial.println(pktCounter);
      Serial.println(rxBuffer);
      break;

    default:
      while (Wire.available()) Wire.read();
      break;
  }
}

void setReadyFlag(void){
   txStatus = txStatus | I2C_STATUS_DONE | I2C_STATUS_SUCCESS;
   Serial.println("Ready to next message");
  }
Ticker readyToNextTicker;

void downLinkAckHandle()
{
  readyToNextTicker.once_ms(3000,setReadyFlag);
}

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD,SLOW_CLK_TPYE);

  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Wire.setBufferSize(255);
  Wire.begin((uint8_t)I2C_DEV_ADDR,0,1,0);

#if(LORAWAN_DEVEUI_AUTO)
  LoRaWAN.generateDeveuiByChipID();
#endif
  deviceState = DEVICE_STATE_INIT;
  delay(500);
  Serial.println("Device start");
}

void loop() {
  switch( deviceState )
  {
    case DEVICE_STATE_INIT:
    {
#if(LORAWAN_DEVEUI_AUTO)
      LoRaWAN.generateDeveuiByChipID();
#endif
      if (startRequested) {
        LoRaWAN.init(loraWanClass,loraWanRegion);
        LoRaWAN.setDefaultDR(dataRate);
        startRequested = false;        
        Serial.println("Device initialized");
      }
      break;
    }
    case DEVICE_STATE_JOIN:
    {
      LoRaWAN.join();
      break;
    }
    case DEVICE_STATE_SEND:
    {
      txStatus |= I2C_STATUS_JOINED;
      if(triggerSend == true){
        triggerSend = false;
        LoRaWAN.send();
        deviceState = DEVICE_STATE_CYCLE;
        Serial.println("Send request");
      }
      break;
    }
    case DEVICE_STATE_CYCLE:
    {
      txDutyCycleTime = appTxDutyCycle + randr( -APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND );
      LoRaWAN.cycle(txDutyCycleTime);
      deviceState = DEVICE_STATE_SLEEP;
      Serial.println("Send success");
      break;
    }
    case DEVICE_STATE_SLEEP:
        Mcu.timerhandler();
        Radio.IrqProcess();
      break;
    default:
    {
      break;
    }
  }
}
