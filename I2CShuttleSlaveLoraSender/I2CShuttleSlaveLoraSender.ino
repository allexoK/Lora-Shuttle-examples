#include "LoRaWan_APP.h"
#include "Arduino.h"
#include "Wire.h"
#include "Ticker.h"


#define I2C_DEV_ADDR       0x55

#define I2C_REG_DEVEUI     0x01  
#define I2C_REG_APPEUI     0x02  
#define I2C_REG_APPKEY     0x03  
#define I2C_REG_CHANMSK    0x05  
#define I2C_REG_DEF_DR     0x06  
#define I2C_REG_ADR        0x07  
#define I2C_REG_CONFIRM    0x08  

#define I2C_REG_JOIN       0x10  
#define I2C_REG_PAYLOAD    0x11 
#define I2C_REG_STATUS     0x12  
#define I2C_REG_SEND       0x13  
#define I2C_REG_CSEND      0x14  

#define I2C_STATUS_DONE     0x01
#define I2C_STATUS_SUCCESS  0x02
#define I2C_STATUS_JOINED   0x04  

LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;
static uint32_t defaultFreqForRegion(LoRaMacRegion_t r) {
  switch (r) {
    case LORAMAC_REGION_EU868:        return 868100000UL;
    case LORAMAC_REGION_EU433:        return 433175000UL;
    case LORAMAC_REGION_US915:        return 915000000UL;
    case LORAMAC_REGION_US915_HYBRID: return 915000000UL;
    case LORAMAC_REGION_AU915:        return 915000000UL;
    case LORAMAC_REGION_CN470:        return 470300000UL;
    case LORAMAC_REGION_CN779:        return 779500000UL;
    case LORAMAC_REGION_AS923:        return 923200000UL;
    case LORAMAC_REGION_KR920:        return 922100000UL;
    case LORAMAC_REGION_IN865:        return 865062500UL;
    default:                          return 868100000UL;
  }
}


static uint32_t gFrequencyHz = 0;
static int8_t   gTxPowerDbm  = 5;

static uint8_t gBw = 0; 
static uint8_t gSf = 7; 
static uint8_t gCr = 1;

static const uint16_t gPreambleLen = 8;
static const bool gFixLenPayload = false;
static const bool gIqInversion = false;

static RadioEvents_t RadioEvents;

static uint8_t txBuf[255];
static volatile uint8_t txLen = 0;

static volatile bool radioReady = false;
static volatile bool startRequested = false;
static volatile bool triggerSend = false;

static volatile uint8_t txStatus = 0;
static volatile uint8_t lastReadRegister = 0;

static void OnTxDone(void);
static void OnTxTimeout(void);

static void applyPseudoDR(uint8_t dr) {
  switch (dr) {
    case 0: gSf = 12; gBw = 0; break;
    case 1: gSf = 11; gBw = 0; break;
    case 2: gSf = 10; gBw = 0; break;
    case 3: gSf =  9; gBw = 0; break;
    case 4: gSf =  8; gBw = 0; break;
    case 5: gSf =  7; gBw = 0; break;
    case 6: gSf =  7; gBw = 1; break;
    default: gSf = 7; gBw = 0; break;
  }
}

static void initRadio() {
  gFrequencyHz = defaultFreqForRegion(loraWanRegion);

  RadioEvents.TxDone    = OnTxDone;
  RadioEvents.TxTimeout = OnTxTimeout;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(gFrequencyHz);

  Radio.SetTxConfig(MODEM_LORA, gTxPowerDbm, 0, gBw, gSf, gCr, gPreambleLen, gFixLenPayload, true, 0, 0, gIqInversion, 3000);

  radioReady = true;
  txStatus |= I2C_STATUS_JOINED;
  Serial.print("Radio initialized. Freq=");
  Serial.println((unsigned long)gFrequencyHz);
}

void onRequest() {
  if (lastReadRegister == I2C_REG_STATUS) {
    Wire.write((uint8_t)txStatus);
  } else {
    Wire.write((uint8_t)0x00);
  }
}

void onReceive(int byteCount) {
  if (byteCount <= 0) return;

  uint8_t reg = Wire.read();
  lastReadRegister = reg;

  switch (reg) {
    case I2C_REG_JOIN: {
      startRequested = true;
      while (Wire.available()) Wire.read();
      Serial.println("I2C: JOIN (init radio)");
      break;
    }

    case I2C_REG_PAYLOAD: {
      uint8_t n = 0;
      while (Wire.available() && n < sizeof(txBuf)) {
        txBuf[n++] = Wire.read();
      }
      txLen = n;
      txStatus &= (uint8_t)~(I2C_STATUS_DONE | I2C_STATUS_SUCCESS);
      Serial.print("I2C: PAYLOAD len=");
      Serial.println((int)txLen);
      break;
    }

    case I2C_REG_SEND: {
      triggerSend = true;
      while (Wire.available()) Wire.read();
      Serial.println("I2C: SEND trigger");
      break;
    }

    case I2C_REG_CSEND: {
      txStatus &= (uint8_t)~(I2C_STATUS_DONE | I2C_STATUS_SUCCESS);
      while (Wire.available()) Wire.read();
      Serial.println("I2C: CSEND clear DONE/SUCCESS");
      break;
    }

    case I2C_REG_DEF_DR: {
      if (Wire.available()) {
        uint8_t dr = Wire.read();
        applyPseudoDR(dr);
        Serial.print("I2C: DEF_DR=");
        Serial.println(dr);

        if (radioReady) {
          Radio.SetTxConfig(MODEM_LORA, gTxPowerDbm, 0, gBw, gSf, gCr,
                            gPreambleLen, gFixLenPayload, true, 0, 0, gIqInversion, 3000);
          Serial.println("Radio TX config updated");
        }
      }
      while (Wire.available()) Wire.read();
      break;
    }

    default: {
      while (Wire.available()) Wire.read();
      break;
    }
  }
}

static void OnTxDone(void) {
  Serial.println("Radio: TX done");
  Radio.Sleep();
  txStatus |= (I2C_STATUS_DONE | I2C_STATUS_SUCCESS);
}

static void OnTxTimeout(void) {
  Serial.println("Radio: TX timeout");
  Radio.Sleep();
  txStatus |= I2C_STATUS_DONE;
  txStatus &= (uint8_t)~I2C_STATUS_SUCCESS;
}

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  Wire.onReceive(onReceive);
  Wire.onRequest(onRequest);
  Wire.setBufferSize(255);
  Wire.begin((uint8_t)I2C_DEV_ADDR, 0, 1, 0);

  Serial.println("LoRa-I2C-slave start");
}

void loop() {
  Radio.IrqProcess();

  if (startRequested) {
    startRequested = false;
    initRadio();
  }

  if (triggerSend) {
    triggerSend = false;

    if (!radioReady) {
      Serial.println("Send rejected: JOIN first");
      txStatus |= I2C_STATUS_DONE;
      txStatus &= (uint8_t)~I2C_STATUS_SUCCESS;
      return;
    }
    if (txLen == 0) {
      Serial.println("Send rejected: empty payload");
      txStatus |= I2C_STATUS_DONE;
      txStatus &= (uint8_t)~I2C_STATUS_SUCCESS;
      return;
    }

    txStatus &= (uint8_t)~(I2C_STATUS_DONE | I2C_STATUS_SUCCESS);

    Serial.print("LoRa sending len=");
    Serial.println((int)txLen);

    Radio.Send((uint8_t*)txBuf, txLen);
  }
}
