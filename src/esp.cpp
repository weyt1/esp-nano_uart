#include <Arduino.h>
#include <SoftwareSerial.h>

// ================== ПИНЫ ==================
// TX UART1 у ESP8266 — фиксированно GPIO2 (TXD1), используем Serial1.
// RX делаем через SoftwareSerial на GPIO13 (RXD2).
static const uint8_t  PEER_RX_PIN = 13;    // GPIO13/RXD2 ← Nano D6 (через делитель)
// Второй аргумент SoftwareSerial — фиктивный TX, мы им не пользуемся (шлём через Serial1)
static const uint8_t  PEER_TX_PIN = 255;

static const uint8_t  LED_PIN     = 16;    // GPIO16 (D0) — внешний LED
// ВНИМАНИЕ: GPIO2 (встроенный LED) занят под Serial1 TX, использовать нельзя.

// ================== НАСТРОЙКИ ПРОТОКОЛА ==================
static const unsigned long PEER_BAUD    = 9600;
static const unsigned long PERIOD_MS    = 5000;
static const char*         PERIODIC_MSG = "PING";
static const size_t        MAX_LINE     = 64;

SoftwareSerial peer(PEER_RX_PIN, PEER_TX_PIN);

// ================== СОСТОЯНИЕ ==================
unsigned long lastSend = 0;
String        rxBuffer;

// ================== УТИЛИТЫ ==================
String trimAll(const String& s) {
    int start = 0, end = s.length();
    while (start < end && isspace((unsigned char)s[start])) start++;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;
    return s.substring(start, end);
}

bool isProtocolCharset(const String& s) {
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        bool ok = (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == ':';
        if (!ok) return false;
    }
    return true;
}

void sendLine(const String& msg) {
    Serial1.print(msg);      // TX UART1 = GPIO2/TXD1 → Nano D5
    Serial1.print('\n');
    Serial.print("[TX] ");
    Serial.println(msg);
}

// ================== ОБРАБОТКА ==================
void handleMessage(const String& msg) {
    if (msg == "PING") {
        Serial.println("[RX] PING -> PONG");
        sendLine("PONG");
        return;
    }
    if (msg == "PONG") {
        Serial.println("[RX] PONG (peer alive)");
        return;
    }

    int colon = msg.indexOf(':');
    if (colon < 0) {
        Serial.println("[RX] no ':' -> STATUS:ERROR");
        sendLine("STATUS:ERROR");
        return;
    }

    String type    = msg.substring(0, colon);
    String payload = msg.substring(colon + 1);

    if (type == "CMD") {
        if (payload == "LED_ON") {
            digitalWrite(LED_PIN, HIGH);   // внешний LED — активный HIGH
            Serial.println("[RX] CMD:LED_ON -> STATUS:OK");
            sendLine("STATUS:OK");
        } else if (payload == "LED_OFF") {
            digitalWrite(LED_PIN, LOW);
            Serial.println("[RX] CMD:LED_OFF -> STATUS:OK");
            sendLine("STATUS:OK");
        } else {
            Serial.print("[RX] CMD:"); Serial.print(payload);
            Serial.println(" -> STATUS:UNKNOWN_CMD");
            sendLine("STATUS:UNKNOWN_CMD");
        }
        return;
    }

    if (type == "STATUS") {
        Serial.print("[RX] status: "); Serial.println(payload);
        return;
    }

    Serial.print("[RX] unknown type '"); Serial.print(type);
    Serial.println("' -> STATUS:ERROR");
    sendLine("STATUS:ERROR");
}

// ================== ЧТЕНИЕ ==================
void readFromPeer() {
    while (peer.available()) {
        char c = peer.read();
        if (c == '\n') {
            String line = trimAll(rxBuffer);
            rxBuffer = "";
            if (line.length() == 0) continue;
            if (!isProtocolCharset(line)) {
                Serial.println("[RX] bad charset -> STATUS:ERROR");
                sendLine("STATUS:ERROR");
                continue;
            }
            Serial.print("[RX] "); Serial.println(line);
            handleMessage(line);
        } else if (c != '\r') {
            rxBuffer += c;
            if (rxBuffer.length() > MAX_LINE) {
                Serial.println("[RX] overflow -> reset, STATUS:ERROR");
                rxBuffer = "";
                sendLine("STATUS:ERROR");
            }
        }
    }
}

// ================== ПЕРИОДИКА ==================
void periodicSend() {
    if (millis() - lastSend >= PERIOD_MS) {
        lastSend = millis();
        sendLine(PERIODIC_MSG);
    }
}

// ================== SETUP / LOOP ==================
void setup() {
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    Serial.begin(9600);       // отладка через USB
    Serial1.begin(PEER_BAUD);   // UART1 TX = GPIO2/TXD1
    peer.begin(PEER_BAUD);      // SoftwareSerial RX = GPIO13

    Serial.println();
    Serial.println("=== ESP NodeMCU protocol ===");
    Serial.print("RX="); Serial.print(PEER_RX_PIN);
    Serial.print("  TX=Serial1(GPIO2)");
    Serial.print("  LED="); Serial.print(LED_PIN);
    Serial.print("  PERIOD="); Serial.println(PERIOD_MS);
    Serial.println("Type a line + Enter to send manually.");
}

void loop() {
    readFromPeer();
    periodicSend();

    if (Serial.available()) {
        String manual = trimAll(Serial.readStringUntil('\n'));
        if (manual.length() > 0) sendLine(manual);
    }
}