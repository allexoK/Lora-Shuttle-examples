/*
  Arduino I2C Master for LoRa Bridge (addr 0x55)
  - Writes OTAA keys,  DR, ADR, channel mask
  - Starts join and waits for JOINED bit
  - Sends periodic JSON payloads, polls DONE/SUCCESS, then clears DONE
*/

#include <Wire.h>

// ----- I2C device + registers (match your bridge) -----
#define I2C_ADDR            0x55
#define I2C_REG_DEVEUI      0x01
#define I2C_REG_APPEUI      0x02
#define I2C_REG_APPKEY      0x03
#define I2C_REG_CHANMSK     0x05
#define I2C_REG_DEF_DR      0x06
#define I2C_REG_ADR         0x07
#define I2C_REG_CONFIRM     0x08
#define I2C_REG_START       0x10  // "JOIN" on the slave
#define I2C_REG_PAYLOAD     0x11
#define I2C_REG_STATUS      0x12
#define I2C_REG_TRANSMIT    0x13
#define I2C_REG_CTRANSMIT   0x14  // clear DONE/SUCCESS

// status bits (must match the slave)
#define I2C_STATUS_DONE     0x01
#define I2C_STATUS_SUCCESS  0x02
#define I2C_STATUS_JOINED   0x04
// optional: #define I2C_STATUS_BUSY 0x80  // if you add it on the slave


// ----- Your LoRaWAN credentials (OTAA) -----
uint8_t DEV_EUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t APP_EUI[8] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
uint8_t APP_KEY[16]= { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

const uint8_t DEFAULT_DR = 4;     // e.g., DR_4 (EU868 SF8/125)

// Uplink behavior
bool useConfirmed = false;
uint8_t confirmedRetries = 4;

// Utility: write a register + optional data
bool i2cWriteReg(uint8_t reg, const uint8_t* data, size_t len) {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(reg);
  for (size_t i = 0; i < len; ++i) Wire.write(data[i]);
  uint8_t err = Wire.endTransmission(); // STOP
  return (err == 0);
}

// Utility: read 1 byte from a register (repeated-start)
bool i2cReadReg1(uint8_t reg, uint8_t &val) {
  Wire.beginTransmission(I2C_ADDR);
  Wire.write(reg);
  uint8_t err = Wire.endTransmission(false); // repeated-start
  if (err != 0) return false;
  uint8_t got = Wire.requestFrom(I2C_ADDR, (uint8_t)1);
  if (got != 1) return false;
  val = Wire.read();
  return true;
}

// Helpers for specific registers
bool setDevEui(const uint8_t eui[8])    { return i2cWriteReg(I2C_REG_DEVEUI, eui, 8); }
bool setAppEui(const uint8_t eui[8])    { return i2cWriteReg(I2C_REG_APPEUI, eui, 8); }
bool setAppKey(const uint8_t key[16])   { return i2cWriteReg(I2C_REG_APPKEY, key, 16); }

bool setChannelMask(const uint16_t m[6]) {
  uint8_t buf[12];
  for (uint8_t i = 0; i < 6; ++i) {
    buf[i*2 + 0] = (uint8_t)(m[i] & 0xFF);      // LSB
    buf[i*2 + 1] = (uint8_t)((m[i] >> 8) & 0xFF); // MSB
  }
  return i2cWriteReg(I2C_REG_CHANMSK, buf, sizeof(buf));
}

bool setDefaultDR(uint8_t dr)           { return i2cWriteReg(I2C_REG_DEF_DR, &dr, 1); }
bool setADR(bool adr)                   { uint8_t v = adr ? 1 : 0; return i2cWriteReg(I2C_REG_ADR, &v, 1); }
bool setConfirm(bool confirmed, uint8_t retries) {
  uint8_t buf[2] = { confirmed ? 1 : 0, retries };
  return i2cWriteReg(I2C_REG_CONFIRM, buf, 2);
}

bool startJoin()                        { return i2cWriteReg(I2C_REG_START, nullptr, 0); }
bool getStatus(uint8_t &s)              { return i2cReadReg1(I2C_REG_STATUS, s); }
bool clearFinishedBit()                 { return i2cWriteReg(I2C_REG_CTRANSMIT, nullptr, 0); }

// Payload helpers
// NOTE: keep ≤222 to fit your master buffer and slave Wire buffer.
bool writePayload(const uint8_t* data, size_t len) {
  if (len > 222) len = 222;
  return i2cWriteReg(I2C_REG_PAYLOAD, data, len);
}
bool triggerSend()                      { return i2cWriteReg(I2C_REG_TRANSMIT, nullptr, 0); }

// Wait until (status & mask) == mask or timeout (ms)
bool waitForBits(uint8_t mask, uint32_t timeoutMs, uint8_t &lastStatus) {
  uint32_t start = millis();
  while (millis() - start < timeoutMs) {
    if (!getStatus(lastStatus)) { delay(50); continue; }
    if ((lastStatus & mask) == mask) return true;
    delay(50);
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  Wire.begin(0,1);               // default SDA/SCL for your Arduino
  Wire.setClock(400000);      // fast mode I2C for snappy payload writes

  Serial.println(F("\nI2C LoRa Master starting..."));

  // --- Configure radio/credentials ---
  if (!setDevEui(DEV_EUI))   Serial.println(F("setDevEui failed"));
  if (!setAppEui(APP_EUI))   Serial.println(F("setAppEui failed"));
  if (!setAppKey(APP_KEY))   Serial.println(F("setAppKey failed"));

  if (!setDefaultDR(DEFAULT_DR))  Serial.println(F("setDefaultDR failed"));

  // default to unconfirmed (toggle via serial cmd later if you want)
  if (!setConfirm(useConfirmed, confirmedRetries)) Serial.println(F("setConfirm failed"));

  // --- Start join ---
  if (!startJoin()) {
    Serial.println(F("startJoin failed")); 
    while(1);
  } else {
    Serial.println(F("Joining..."));
  }

  // Wait for JOINED (requires slave to set the bit when network join succeeds)
  uint8_t st = 0;
  if (waitForBits(I2C_STATUS_JOINED, 20000, st)) {
    Serial.println(F("JOINED!"));
  } else {
    Serial.print(F("Join timeout, status=0x"));
    Serial.println(st, HEX);
  }
}

uint32_t lastSendMs = 0;
uint32_t sendPeriodMs = 10000;

void loop() {
  // Example: send a JSON payload every 10 seconds.
  if (millis() - lastSendMs >= sendPeriodMs) {
    lastSendMs = millis();

    // Build a small JSON: {"name":"demo","msg":"hello #N"}
    static uint32_t counter = 0;
    char json[96];
    snprintf(json, sizeof(json), "{\"name\":\"demo\",\"msg\":\"hello %lu\"}", (unsigned long)counter++);

    Serial.print(F("Sending: ")); Serial.println(json);

    // Write payload (no trailing NUL)
    if (!writePayload(reinterpret_cast<const uint8_t*>(json), strlen(json))) {
      Serial.println(F("writePayload failed"));
      return;
    }

    // (Optionally) switch confirmed on-the-fly:
    // useConfirmed = (counter % 5 == 0);
    // setConfirm(useConfirmed, confirmedRetries);

    // Trigger transmit
    if (!triggerSend()) {
      Serial.println(F("triggerSend failed"));
      return;
    }

    // Wait until DONE (send finished), then check SUCCESS
    uint8_t st = 0;
    if (!waitForBits(I2C_STATUS_DONE, 15000, st)) {
      Serial.println(F("Send timeout waiting for DONE"));
      // Optionally force rejoin on your slave by sending START again
      return;
    }

    bool ok = (st & I2C_STATUS_SUCCESS) != 0;
    Serial.print(F("DONE. SUCCESS="));
    Serial.println(ok ? F("yes") : F("no"));

    // Clear finished bit so next TX is clean
    if (!clearFinishedBit()) Serial.println(F("clearFinishedBit failed"));
  }

  // Simple status poll (optional)
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll > 2000) {
    lastPoll = millis();
    uint8_t st = 0;
    if (getStatus(st)) {
      Serial.print(F("Status: 0x")); Serial.println(st, HEX);
    }
  }
}
