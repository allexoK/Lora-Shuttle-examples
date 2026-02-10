#include "LoRaWan_APP.h"
#include "Arduino.h"


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

#define RX_TIMEOUT_VALUE           0   

// Musí sedět se senderem:
#define LORA_BANDWIDTH             0
#define LORA_SPREADING_FACTOR      7
#define LORA_CODINGRATE            1
#define LORA_PREAMBLE_LENGTH       8
#define LORA_IQ_INVERSION_ON       false

static RadioEvents_t RadioEvents;
static uint32_t gFrequencyHz = 0;

static uint8_t rxBuf[255];
static uint16_t rxLen = 0;

static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr);
static void OnRxTimeout(void);
static void OnRxError(void);

void setup() {
  Serial.begin(115200);
  Mcu.begin(HELTEC_BOARD, SLOW_CLK_TPYE);

  gFrequencyHz = defaultFreqForRegion(loraWanRegion);

  Serial.println("LoRa RX start");
  Serial.print("Freq=");
  Serial.println((unsigned long)gFrequencyHz);

  RadioEvents.RxDone    = OnRxDone;
  RadioEvents.RxTimeout = OnRxTimeout;
  RadioEvents.RxError   = OnRxError;

  Radio.Init(&RadioEvents);
  Radio.SetChannel(gFrequencyHz);

  Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH, RX_TIMEOUT_VALUE, false, 0, true, 0, 0, LORA_IQ_INVERSION_ON, true);
  Radio.Rx(0);
}

void loop() {
  Radio.IrqProcess();
}

static void OnRxDone(uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr) {
  (void)rssi; (void)snr;

  uint16_t n = size;
  if (n >= sizeof(rxBuf)) n = sizeof(rxBuf) - 1;
  memcpy(rxBuf, payload, n);
  rxBuf[n] = 0;
  rxLen = n;

  Serial.print("RX (");
  Serial.print((int)rxLen);
  Serial.print("): ");
  Serial.println((char*)rxBuf);

  Radio.Rx(0);
}

static void OnRxTimeout(void) {
  Serial.println("RX timeout");
  Radio.Rx(0);
}

static void OnRxError(void) {
  Serial.println("RX error");
  Radio.Rx(0);
}
