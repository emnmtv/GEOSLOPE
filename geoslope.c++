// Arduino + SIM800L: send sensor data to HTTP API and SMS alert
#include <Wire.h>
#include <SoftwareSerial.h>
#include <DHT.h>
#include <Arduino.h>
#include <math.h>

// Pin definitions
#define SOIL_SENSOR_PIN A0
#define DHT_PIN 2
#define BUZZER_PIN 3
#define GREEN_LED 4
#define YELLOW_LED 5
#define RED_LED 6

// DHT settings
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);

// GSM module (SIM800L) connections: D7 → SIM_RXD, D8 → SIM_TXD
SoftwareSerial gsm(7, 8);

// Threshold values
#define MOISTURE_THRESHOLD 500
#define HUMIDITY_THRESHOLD 80

// GPRS and API config
const char APN[] = "internet.globe.com.ph";
const char APN_USER[] = "";
const char APN_PASS[] = "";

// Use your Cloudflare / server HTTPS URL
const char API_URL[] = "https://agency-bikini-crossword-ranging.trycloudflare.com/api/moisture";
const char DEVICE_ID[] = "default-device";

// Post cadence
unsigned long lastPost = 0;
const unsigned long postIntervalMs = 5000; // 5s
bool lastPostOk = true;
unsigned long lastSmsAt = 0;
const unsigned long smsCooldownMs = 300000; // 5 minutes

// --- SIM800 helpers ---
// Enable verbose debug prints to Serial
#define DEBUG_GSM 1
bool waitFor(const char* token, uint16_t timeoutMs = 5000) {
  unsigned long start = millis();
  size_t idx = 0;
  while (millis() - start < timeoutMs) {
    if (gsm.available()) {
      char c = (char)gsm.read();
      if (DEBUG_GSM) Serial.write(c);
      if (token[idx] == c) {
        idx++;
        if (token[idx] == '\0') return true;
      } else {
        idx = (c == token[0]) ? 1 : 0;
      }
    }
  }
  return false;
}

void sendAT(const char* cmd, uint16_t waitMs = 500) {
  if (DEBUG_GSM) { Serial.print(F("→ ")); Serial.println(cmd); }
  gsm.println(cmd);
  unsigned long start = millis();
  while (millis() - start < waitMs) {
    if (gsm.available()) {
      char c = (char)gsm.read();
      if (DEBUG_GSM) Serial.write(c);
    }
  }
}

String readLine(uint16_t timeoutMs = 3000) {
  String out;
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (gsm.available()) {
      char c = (char)gsm.read();
      if (c == '\n') return out;
      if (c != '\r') out += c;
    }
  }
  return out;
}

bool gprsAttach(uint16_t timeoutMs = 15000) {
  // Check attach state
  sendAT("AT+CGATT?", 400);
  gsm.println("AT+CGATT?");
  String resp = readLine(1500);
  if (DEBUG_GSM) { Serial.print(F("CGATT?: ")); Serial.println(resp); }
  if (resp.indexOf("+CGATT: 1") >= 0) return true;
  // Try to attach
  sendAT("AT+CGATT=1", 500);
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    gsm.println("AT+CGATT?");
    String r = readLine(1500);
    if (DEBUG_GSM) { Serial.print(F("CGATT? ")); Serial.println(r); }
    if (r.indexOf("+CGATT: 1") >= 0) return true;
    delay(1000);
  }
  return false;
}

bool gprsOpenBearer() {
  sendAT("AT");
  sendAT("ATE0");
  sendAT("AT+CSQ", 400);
  sendAT("AT+CREG?", 400);
  if (!gprsAttach()) {
    Serial.println(F("[GPRS] Attach failed"));
  }
  // Define PDP context explicitly (helps some networks)
  {
    String s = String("AT+CGDCONT=1,\"IP\",\"") + APN + "\"";
    if (DEBUG_GSM) { Serial.print(F("→ ")); Serial.println(s); }
    gsm.println(s);
    waitFor("OK", 1000);
  }
  sendAT("AT+SAPBR=3,1,\"Contype\",\"GPRS\"");
  {
    String s = String("AT+SAPBR=3,1,\"APN\",\"") + APN + "\"";
    gsm.println(s);
    delay(400);
  }
  if (strlen(APN_USER)) { String s = String("AT+SAPBR=3,1,\"USER\",\"") + APN_USER + "\""; gsm.println(s); delay(300); }
  if (strlen(APN_PASS)) { String s = String("AT+SAPBR=3,1,\"PWD\",\"") + APN_PASS + "\""; gsm.println(s); delay(300); }
  bool ok = false;
  for (int attempt = 1; attempt <= 3 && !ok; attempt++) {
    Serial.print(F("[GPRS] Opening bearer, attempt ")); Serial.println(attempt);
    sendAT("AT+SAPBR=1,1", 7000);
    if (DEBUG_GSM) Serial.println(F("Query bearer:"));
    delay(500);
    gsm.println("AT+SAPBR=2,1");
    String line = readLine(4000);
    if (DEBUG_GSM) { Serial.print(F("← ")); Serial.println(line); }
    ok = line.indexOf("+SAPBR:") >= 0 && (line.indexOf(",1,\"") >= 0 || line.indexOf(",1," ) >= 0);
    waitFor("OK", 1500);
    if (!ok) {
      Serial.println(F("[GPRS] Bearer not ready, closing and retrying"));
      sendAT("AT+SAPBR=0,1", 3000);
      delay(1000);
      // Fallback bring-up using TCP/IP stack
      sendAT("AT+CIPSHUT", 3000);
      {
        String apnCmd = String("AT+CSTT=\"") + APN + "\",\"" + APN_USER + "\",\"" + APN_PASS + "\"";
        if (DEBUG_GSM) { Serial.print(F("→ ")); Serial.println(apnCmd); }
        gsm.println(apnCmd);
        waitFor("OK", 3000);
      }
      sendAT("AT+CIICR", 8000); // bring up wireless connection
      if (DEBUG_GSM) Serial.println(F("Query IP via CIFSR:"));
      gsm.println("AT+CIFSR");
      String ip = readLine(4000);
      if (DEBUG_GSM) { Serial.print(F("IP: ")); Serial.println(ip); }
    }
  }
  return ok;
}

void gprsCloseBearer() {
  sendAT("AT+SAPBR=0,1", 1500);
}

bool httpPostData(int moisture, float humidity, float temperature) {
  // Build JSON payload
  String payload = String("{\"value\":") + moisture +
                   ",\"source\":\"arduino-sim800l\"," +
                   "\"deviceId\":\"" + DEVICE_ID + "\"" +
                   ",\"humidity\":" + (isnan(humidity) ? String("null") : String(humidity, 1)) +
                   ",\"temperature\":" + (isnan(temperature) ? String("null") : String(temperature, 1)) +
                   "}";

  String url = String(API_URL);

  if (DEBUG_GSM) {
    Serial.print(F("POST ")); Serial.println(url);
    Serial.print(F("Payload (")); Serial.print(payload.length()); Serial.println(F("):"));
    Serial.println(payload);
  }

  // Init HTTP
  sendAT("AT+HTTPTERM", 300); // ensure clean state
  sendAT("AT+HTTPINIT", 300);
  sendAT("AT+HTTPPARA=\"CID\",1", 200);
  // Enable SSL for HTTPS and allow redirects (in case server issues 301)
  sendAT("AT+HTTPSSL=1", 200);
  sendAT("AT+HTTPPARA=\"REDIR\",1", 200);
  {
    String s = String("AT+HTTPPARA=\"URL\",\"") + url + "\"";
    gsm.println(s);
    delay(300);
  }
  sendAT("AT+HTTPPARA=\"CONTENT\",\"application/json\"", 200);

  // Provide data
  {
    String s = String("AT+HTTPDATA=") + payload.length() + ",10000";
    gsm.println(s);
    if (!waitFor("DOWNLOAD", 4000)) { if (DEBUG_GSM) Serial.println(F("No DOWNLOAD prompt")); return false; }
    gsm.print(payload);
    delay(300);
  }

  // POST action
  if (DEBUG_GSM) Serial.println(F("Executing HTTPACTION=1"));
  gsm.println("AT+HTTPACTION=1");
  unsigned long start = millis();
  int status = -1;
  String buf;
  while (millis() - start < 20000) {
    while (gsm.available()) {
      char c = (char)gsm.read();
      if (DEBUG_GSM) Serial.write(c);
      buf += c;
    }
    int idx = buf.indexOf("+HTTPACTION:");
    if (idx >= 0) {
      int comma1 = buf.indexOf(',', idx);
      int comma2 = buf.indexOf(',', comma1 + 1);
      if (comma1 > 0 && comma2 > comma1) {
        status = buf.substring(comma1 + 1, comma2).toInt();
        break;
      }
    }
  }

  if (DEBUG_GSM) { Serial.print(F("HTTP status: ")); Serial.println(status); }
  sendAT("AT+HTTPREAD", 400); // print any response body

  sendAT("AT+HTTPTERM", 200);
  return status == 200 || status == 201;
}

// Function to send SMS through SIM800L
void sendSMS(String message) {
  sendAT("AT+CMGF=1", 300);
  gsm.print("AT+CMGS=\"+639761979987\"\r");
  delay(400);
  gsm.print(message);
  delay(300);
  gsm.write(26);
  delay(2000);
}

void setup() {
  // Pin setup
  pinMode(SOIL_SENSOR_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(YELLOW_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  // Serial communication
  Serial.begin(9600);
  gsm.begin(9600);

  // Initialize DHT
  dht.begin();

  Serial.println("Initializing GPRS...");
  if (gprsOpenBearer()) {
    Serial.println("GPRS ready");
  } else {
    Serial.println("GPRS failed to start");
  }

  Serial.println("System Ready: Soil + Humidity + Temperature + HTTP POST");
}

void loop() {
  // Read sensors
  int moistureLevel = analogRead(SOIL_SENSOR_PIN);
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Print readings
  Serial.print("Soil Moisture: ");
  Serial.println(moistureLevel);
  Serial.print("Humidity: ");
  Serial.println(humidity);
  Serial.print("Temperature: ");
  Serial.println(temperature);

  // Detection logic
  bool critical = (moistureLevel > MOISTURE_THRESHOLD) || (humidity > HUMIDITY_THRESHOLD);
  bool moderate = (moistureLevel > (int)(MOISTURE_THRESHOLD * 0.8)) || (humidity > (HUMIDITY_THRESHOLD * 0.8));

  if (critical) {
    digitalWrite(BUZZER_PIN, HIGH);
    digitalWrite(RED_LED, HIGH);
    digitalWrite(YELLOW_LED, LOW);
    digitalWrite(GREEN_LED, LOW);
    // SMS disabled: log to console only
    if (!lastPostOk && (millis() - lastSmsAt > smsCooldownMs)) {
      Serial.println("[ALERT] Possible landslide detected (SMS disabled)");
      lastSmsAt = millis();
    }
  } else if (moderate) {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(YELLOW_LED, HIGH);
    digitalWrite(GREEN_LED, LOW);
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(RED_LED, LOW);
    digitalWrite(YELLOW_LED, LOW);
    digitalWrite(GREEN_LED, HIGH);
  }

  // Periodic POST to API
  unsigned long now = millis();
  if (now - lastPost >= postIntervalMs) {
    lastPost = now;
    if (!gprsOpenBearer()) {
      Serial.println("Bearer not ready");
    } else {
      Serial.println("Posting to API...");
      bool ok = httpPostData(moistureLevel, humidity, temperature);
      lastPostOk = ok;
      Serial.println(ok ? "HTTP POST OK" : "HTTP POST FAILED");
    }
  }

  delay(2000);
}
