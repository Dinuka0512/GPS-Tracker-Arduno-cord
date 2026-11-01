#include <TinyGPS++.h>
#include <SoftwareSerial.h>
#include <ESP8266WiFi.h>
#include <FirebaseESP8266.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

// Wi-Fi Config
#define WIFI_SSID "TCL"
#define WIFI_PASSWORD "12345678"

// Firebase Config
#define FIREBASE_HOST ""
#define FIREBASE_AUTH ""
const char* deviceName = "GPS-Tracker-1";

// GPS Setup
TinyGPSPlus gps;
SoftwareSerial gpsSerial(D6, D7); // RX, TX

// Time Setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);  // UTC

// Firebase Setup
FirebaseData firebaseData;
FirebaseConfig config;
FirebaseAuth auth;
FirebaseJson json;

// Battery Monitoring
const int analogPin = A0;
float maxBatteryVoltage = 4.2;  // Full battery
float minBatteryVoltage = 3.0;  // Empty battery

// Default State
String lat = "Waiting-gps";
String lng = "Waiting-gps";
String speed = "Waiting-gps";
int batteryPercent = 0;
float batteryVoltage = 0.0;

unsigned long lastFirebaseSendTime = 0;
unsigned long lastGpsUpdateTime = 0;
unsigned long printInterval = 3000;
unsigned long lastPrint = 0;

// Get Time
String getISOTime() {
  time_t rawTime = timeClient.getEpochTime();
  struct tm* timeInfo = gmtime(&rawTime);
  char buffer[25];
  sprintf(buffer, "%04d-%02d-%02dT%02d:%02d:%02dZ",
          timeInfo->tm_year + 1900,
          timeInfo->tm_mon + 1,
          timeInfo->tm_mday,
          timeInfo->tm_hour,
          timeInfo->tm_min,
          timeInfo->tm_sec);
  return String(buffer);
}

// Battery % Calculation
int mapBatteryPercent(float voltage) {
  if (voltage >= maxBatteryVoltage) return 100;
  if (voltage <= minBatteryVoltage) return 0;
  return (int)(((voltage - minBatteryVoltage) / (maxBatteryVoltage - minBatteryVoltage)) * 100);
}

void readBatteryLevel() {
  int analogValue = analogRead(analogPin);
  float voltageAtA0 = (analogValue / 1023.0) * 3.3; // NodeMCU A0 max is 3.3V
  batteryVoltage = voltageAtA0 * 2.0; // Voltage divider (100k + 100k)
  batteryPercent = mapBatteryPercent(batteryVoltage);

  Serial.print("Battery Voltage: ");
  Serial.print(batteryVoltage, 2);
  Serial.print(" V | Battery Level: ");
  Serial.print(batteryPercent);
  Serial.println("%");
}

void setup() {
  Serial.begin(9600);
  gpsSerial.begin(9600);

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi!");

  // Start time client
  timeClient.begin();
  while (!timeClient.update()) timeClient.forceUpdate();

  // Firebase connection
  config.host = FIREBASE_HOST;
  config.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Waiting for GPS fix...");
}

void loop() {
  // Read GPS
  while (gpsSerial.available()) {
    char c = gpsSerial.read();
    gps.encode(c);

    if (gps.location.isUpdated()) {
      lat = String(gps.location.lat(), 6);
      lng = String(gps.location.lng(), 6);
      speed = String(gps.speed.kmph(), 2);
      lastGpsUpdateTime = millis();

      Serial.print("Latitude: "); Serial.print(lat);
      Serial.print(" Longitude: "); Serial.print(lng);
      Serial.print(" Speed: "); Serial.print(speed); Serial.println(" km/h");
    }
  }

  // Print waiting message if no GPS fix
  if ((millis() - lastGpsUpdateTime > 3000) && (millis() - lastPrint > printInterval)) {
    Serial.println("Waiting for GPS fix...");
    lastPrint = millis();
  }

  // Send to Firebase only if GPS is valid
  if (millis() - lastFirebaseSendTime >= 1000) {
    timeClient.update();
    readBatteryLevel();

    if (gps.location.isValid()) {
      sendDataToFirebase();
    } else {
      Serial.println("GPS not valid yet, skipping Firebase send.");
    }

    lastFirebaseSendTime = millis();
  }
}

void sendDataToFirebase() {
  json.clear();
  json.set("deviceId", deviceName);
  json.set("location/lat", lat);
  json.set("location/lng", lng);
  json.set("speed", speed);
  json.set("battery/percent", batteryPercent);
  json.set("battery/voltage", String(batteryVoltage, 2));

  String path = "/event/" + getISOTime();

  if (Firebase.setJSON(firebaseData, path, json)) {
    Serial.println("Data sent successfully");
  } else {
    Serial.print("Firebase send error: ");
    Serial.println(firebaseData.errorReason());
  }
}
