// C++ code
//
#if defined(ESP32)
#include <WiFi.h>
#include <HTTPClient.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#endif

// Wi-Fi credentials
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Backend server URL (Cloudflare tunnel HTTPS)
// Replace with your live URL if it changes
const char* SERVER_URL = "https://variance-psychology-retain-immunology.trycloudflare.com/api/moisture";

// Device identity and optional fixed location (set if known)
const char* DEVICE_ID = "geoslope-device-001"; // change per device
const double DEVICE_LAT = NAN; // set to a number to send location
const double DEVICE_LNG = NAN; // set to a number to send location

int moisture = 0;
unsigned long lastPostMs = 0;
const unsigned long postIntervalMs = 2000; // send every 2 seconds

static void connectWiFi() {
#if defined(ESP32) || defined(ESP8266)
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    if (millis() - start > 20000) break;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected, IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi not connected");
  }
#endif
}

static void postMoisture(int value) {
#if defined(ESP32) || defined(ESP8266)
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  HTTPClient http;
  const String url = String(SERVER_URL);

  bool isHttps = url.startsWith("https://");
#if defined(ESP8266)
  if (isHttps) {
    BearSSL::WiFiClientSecure client;
    client.setInsecure(); // WARNING: skips certificate validation
    http.begin(client, url);
  } else {
    WiFiClient client;
    http.begin(client, url);
  }
#else // ESP32
  if (isHttps) {
    WiFiClientSecure client;
    client.setInsecure(); // WARNING: skips certificate validation
    http.begin(client, url);
  } else {
    WiFiClient client;
    http.begin(client, url);
  }
#endif

  http.addHeader("Content-Type", "application/json");
  // Build JSON payload with deviceId and optional lat/lng
  String payload = String("{\"value\":") + String(value) +
                   ",\"source\":\"geoslope.c++\"," +
                   "\"deviceId\":\"" + String(DEVICE_ID) + "\"";
  if (!isnan(DEVICE_LAT) && !isnan(DEVICE_LNG)) {
    payload += String(",\"lat\":") + String(DEVICE_LAT, 6) +
               String(",\"lng\":") + String(DEVICE_LNG, 6);
  }
  payload += "}";
  int code = http.POST(payload);
  if (code > 0) {
    Serial.print("POST status: ");
    Serial.println(code);
  } else {
    Serial.print("POST failed: ");
    Serial.println(http.errorToString(code));
  }
  http.end();
#else
  // Non-WiFi board: no network support
  (void)value;
#endif
}

void setup()
{
  pinMode(A0, OUTPUT);
  pinMode(A1, INPUT);
  Serial.begin(9600);
  pinMode(8, OUTPUT);
  pinMode(9, OUTPUT);
  pinMode(10, OUTPUT);
  pinMode(11, OUTPUT);
  pinMode(12, OUTPUT);
  connectWiFi();
}

void loop()
{
  // Apply power to the soil moisture sensor
  digitalWrite(A0, HIGH);
  delay(10); // Wait for 10 millisecond(s)
  moisture = analogRead(A1);
  // Turn off the sensor to reduce metal corrosion
  // over time
  digitalWrite(A0, LOW);
  Serial.println(moisture);
  digitalWrite(8, LOW);
  digitalWrite(9, LOW);
  digitalWrite(10, LOW);
  digitalWrite(11, LOW);
  digitalWrite(12, LOW);
  if (moisture < 200) {
    digitalWrite(12, HIGH);
  } else {
    if (moisture < 400) {
      digitalWrite(11, HIGH);
    } else {
      if (moisture < 600) {
        digitalWrite(10, HIGH);
      } else {
        if (moisture < 800) {
          digitalWrite(9, HIGH);
        } else {
          digitalWrite(8, HIGH);
        }
      }
    }
  }

  unsigned long now = millis();
  if (now - lastPostMs >= postIntervalMs) {
    lastPostMs = now;
    postMoisture(moisture);
  }

  delay(100); // Wait for 100 millisecond(s)
}