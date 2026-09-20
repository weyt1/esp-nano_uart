#include <Arduino.h>
#include <SoftwareSerial.h>

const uint8_t PEER_RX_PIN = 13;       

const uint8_t LED_PIN = 16;

const bool LED_ACTIVE_HIGH = true;

const uint32_t DEBUG_BAUD = 115200;
const uint32_t PEER_BAUD  = 9600;

const uint32_t PING_PERIOD_MS = 5000;
const uint32_t ASK_TIMEOUT    = 1000;

const uint8_t MAX_LINE = 64;

const char PERIODIC_MSG[] = "PING";

SoftwareSerial peer(PEER_RX_PIN, PEER_RX_PIN);

uint32_t lastPing = 0;
uint32_t pendingUntil = 0;

uint16_t nextId = 0;
uint16_t pendingId = 0;

char rxBuffer[MAX_LINE + 1];
uint8_t rxLength = 0;

char lastAckResult[MAX_LINE + 1];


void setLed(bool on) {
  digitalWrite(
    LED_PIN,
    on == LED_ACTIVE_HIGH ? HIGH : LOW
  );
}


void trimAll(char* s, uint8_t& length) {
  uint8_t start = 0;

  while (start < length &&
         isspace((unsigned char)s[start])) {
    start++;
  }

  uint8_t end = length;

  while (end > start &&
         isspace((unsigned char)s[end - 1])) {
    end--;
  }

  if (start > 0 && end > start) {
    memmove(s, s + start, end - start);
  }

  length = end - start;
  s[length] = '\0';
}


bool isProtocolCharset(const char* s) {
  while (*s != '\0') {
    char c = *s++;

    bool valid =
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '_' ||
      c == ':';

    if (!valid) {
      return false;
    }
  }

  return true;
}


void sendLine(const char* msg) {
  // Serial1 TX ESP8266 = GPIO2
  Serial1.print(msg);
  Serial1.print('\n');

  Serial.print(F("[TX] "));
  Serial.println(msg);
}

void sendStatus(uint16_t id, const char* result) {
  Serial1.print(F("STATUS:"));
  Serial1.print(id);
  Serial1.print(':');
  Serial1.println(result);

  Serial.print(F("[TX] STATUS:"));
  Serial.print(id);
  Serial.print(':');
  Serial.println(result);
}


void sendCommand(const char* payload) {
  ++nextId;

  Serial1.print(F("CMD:"));
  Serial1.print(nextId);
  Serial1.print(':');
  Serial1.println(payload);

  Serial.print(F("[TX] CMD:"));
  Serial.print(nextId);
  Serial.print(':');
  Serial.println(payload);

  pendingId = nextId;
  pendingUntil = millis() + ASK_TIMEOUT;

  lastAckResult[0] = '\0';

  Serial.print(F("[ASK] pending ID="));
  Serial.println(pendingId);
}


void handleMessage(char* msg) {
  // ---------- PING ----------
  if (strcmp(msg, "PING") == 0) {
    Serial.println(F("[RX] PING -> PONG"));
    sendLine("PONG");
    return;
  }

  if (strcmp(msg, "PONG") == 0) {
    Serial.println(F("[RX] PONG"));
    return;
  }

  char* c1 = strchr(msg, ':');

  if (c1 == nullptr) {
    Serial.println(F("[RX] invalid message"));
    sendStatus(0, "ERROR");
    return;
  }

  *c1 = '\0';

  char* type = msg;
  char* rest = c1 + 1;

  char* c2 = strchr(rest, ':');

  if (c2 == nullptr) {
    Serial.println(F("[RX] invalid message format"));
    sendStatus(0, "ERROR");
    return;
  }

  *c2 = '\0';

  char* idText = rest;
  char* payload = c2 + 1;

  if (*idText == '\0') {
    sendStatus(0, "ERROR");
    return;
  }

  for (char* p = idText; *p != '\0'; p++) {
    if (*p < '0' || *p > '9') {
      Serial.println(F("[RX] invalid ID"));
      sendStatus(0, "ERROR");
      return;
    }
  }

  uint16_t id = (uint16_t)atoi(idText);

  // ---------- CMD ----------
  if (strcmp(type, "CMD") == 0) {
    if (strcmp(payload, "LED_ON") == 0) {
      setLed(true);
      sendStatus(id, "OK");
    }
    else if (strcmp(payload, "LED_OFF") == 0) {
      setLed(false);
      sendStatus(id, "OK");
    }
    else {
      sendStatus(id, "UNKNOWN_CMD");
    }

    return;
  }

  if (strcmp(type, "STATUS") == 0) {
    if (pendingId != 0 && id == pendingId) {
      strncpy(lastAckResult, payload, MAX_LINE);
      lastAckResult[MAX_LINE] = '\0';

      Serial.print(F("[ASK] ACK ID="));
      Serial.print(id);
      Serial.print(F(", RESULT="));
      Serial.println(lastAckResult);

      pendingId = 0;
    }
    else {
      Serial.print(F("[RX] STATUS:"));
      Serial.print(id);
      Serial.print('=');
      Serial.println(payload);
    }

    return;
  }

  Serial.print(F("[RX] unknown type: "));
  Serial.println(type);

  sendStatus(0, "ERROR");
}


void readFromPeer() {
  while (peer.available()) {
    char c = peer.read();

    if (c == '\n') {
      trimAll(rxBuffer, rxLength);

      if (rxLength == 0) {
        continue;
      }

      if (!isProtocolCharset(rxBuffer)) {
        Serial.println(F("[RX] bad charset"));
        sendStatus(0, "ERROR");

        rxLength = 0;
        rxBuffer[0] = '\0';
        continue;
      }

      Serial.print(F("[RX] "));
      Serial.println(rxBuffer);

      handleMessage(rxBuffer);

      rxLength = 0;
      rxBuffer[0] = '\0';
    }
    else if (c != '\r') {
      if (rxLength < MAX_LINE) {
        rxBuffer[rxLength++] = c;
        rxBuffer[rxLength] = '\0';
      }
      else {
        Serial.println(F("[RX] buffer overflow"));

        rxLength = 0;
        rxBuffer[0] = '\0';

        sendStatus(0, "ERROR");
      }
    }
  }
}

void periodicPing() {
  if (millis() - lastPing < PING_PERIOD_MS) {
    return;
  }

  lastPing = millis();

  sendLine(PERIODIC_MSG);
}

void checkAskTimeout() {
  if (pendingId != 0 &&
      (int32_t)(millis() - pendingUntil) >= 0) {

    strcpy(lastAckResult, "TIMEOUT");

    Serial.print(F("[ASK] TIMEOUT ID="));
    Serial.println(pendingId);

    pendingId = 0;
  }
}

void readManualCommand() {
  if (!Serial.available()) {
    return;
  }

  char manual[MAX_LINE + 1];

  uint8_t length =
    Serial.readBytesUntil('\n', manual, MAX_LINE);

  manual[length] = '\0';

  trimAll(manual, length);

  if (length == 0) {
    return;
  }

  if (strncmp(manual, "CMD:", 4) == 0) {
    sendCommand(manual + 4);
  }
  else {
    sendLine(manual);
  }
}

void setup() {
  pinMode(LED_PIN, OUTPUT);
  setLed(false);

  Serial.begin(DEBUG_BAUD);

  Serial1.begin(PEER_BAUD);

  peer.begin(PEER_BAUD);

  lastAckResult[0] = '\0';

  Serial.println();
  Serial.println(F("=== ESP8266 protocol + PING ==="));
  Serial.println(F("Automatic message: PING"));
  Serial.println(F("Manual commands: CMD:LED_ON / CMD:LED_OFF"));
}

void loop() {
  readFromPeer();
  periodicPing();
  checkAskTimeout();
  readManualCommand();
}