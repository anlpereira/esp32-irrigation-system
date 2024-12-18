#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESPmDNS.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>

// --- PIN DEFINITIONS ---
#define PUMP_PIN 10
#define SOIL_SENSOR_PIN A1
#define TEMP_SENSOR_PIN 4
#define LED_PIN 9
#define LED_MAX_BRIGHTNESS 230
#define ANALOG_READ_RESOLUTION 4095

// --- WIFI SETTINGS ---
const char* defaultSSID = "brisa-3649452";
const char* defaultPassword = "rwktgine";
const int eepromSSIDAddress = 0;
const int eepromPasswordAddress = 64;
char ssid[64] = "";
char password[64] = "";

// --- TIME SETTINGS ---
const long utcOffsetInSeconds = -3 * 3600; // São Paulo Time Zone
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
unsigned long previousMillisTime = 0;
const long intervalTime = 60000; // 1 minute

// --- PUMP CONTROL ---
const long pumpDuration = 5000; // 5 seconds
bool pumpActive = false;
unsigned long pumpStartTime = 0;
unsigned long lastPumpMillis = 0;
unsigned int pumpCount = 0;
const unsigned long pumpInterval = 12 * 60 * 60 * 1000; // 12 hours

// --- SOIL MOISTURE SENSOR ---
int soilMoistureValue = 0;
unsigned long previousMillisSoil = 0;
const long intervalSoil = 60000; // 1 minute
const int drySoilValue = 4095;
const int wetSoilValue = 1800;
const int soilMoistureMinLimit = 30;

// --- TEMPERATURE SENSOR ---
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature sensors(&oneWire);
float temperatureValue = 0.0;
unsigned long previousMillisTemp = 0;
const long intervalTemp = 60000; // 1 minute

// --- LED PWM CONTROL ---
int ledValue = 0;
int ledBrightness = 0;
const int sunriseStartHour = 8;
const int sunsetEndHour = 20;

// --- WEB SERVER ---
WebServer server(80);
const char* host = "esp32";
const char* googleAppsScriptURL = "https://script.google.com/macros/s/AKfycbw8esoFP9lHf0pE64-GdYSsJSfSQSRa7Epy5e-asbYlYTah39hHMMX3x2OZ-ONvthGp/exec";

//Interval Upload 
unsigned long previousMillisUpload = 0;
const long intervalUpload = 600000; // 10 minutes in milliseconds, the same as the other intervals

String globalCurrentTimeStr;
String globalTemperatureValue;
String globalMoisturePercentage;
String globalHumidityData = "";

// --- JSON ---
StaticJsonDocument<3000> dataJson;
String currentTimeStr;

// --- GRAPH ---
String humidityData = "";

void setup() {
  Serial.begin(115200);
  EEPROM.begin(128);

  //--- WIFI ---
  readCredentialsFromEEPROM();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //--- MDNS ---
  if (!MDNS.begin(host)) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");
  MDNS.addService("http", "tcp", 80);

  //--- TIME SYNC ---
  timeClient.begin();
  timeClient.update();

  //--- PUMP ---
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);

  //--- LED ---
  pinMode(LED_PIN, OUTPUT);

  //--- TEMPERATURE ---
  sensors.begin();

  //--- SETUP WEB SERVER ---
  setupWebRoutes();

  Serial.println("Setup complete");
}

void loop() {
  server.handleClient();
  timeUpdate();
  controlPump();
  readSoilMoisture();
  readTemperature();
  controlLED();
  uploadData();
}

// --- TIME ---
void timeUpdate(){
    unsigned long currentMillisTime = millis();
  if(currentMillisTime - previousMillisTime >= intervalTime){
    previousMillisTime = currentMillisTime;
     timeClient.update();
    currentTimeStr = timeClient.getFormattedTime();
    Serial.print("Current Time: ");
    Serial.println(currentTimeStr);
  }
}

// --- PUMP CONTROL ---
void controlPump() {
  if (pumpActive) {
    if (millis() - pumpStartTime >= pumpDuration) {
      digitalWrite(PUMP_PIN, LOW);
      pumpActive = false;
      Serial.println("Pump off");
    }
  } else {
      if(pumpCount < 2){
        unsigned long currentTime = millis();
        if(currentTime - lastPumpMillis >= pumpInterval){
            lastPumpMillis = currentTime;
            pumpOn();
             pumpCount++;
            Serial.println("Pump triggered (Schedule)");
          }
    }
  }
}

void pumpOn() {
  digitalWrite(PUMP_PIN, HIGH);
  pumpStartTime = millis();
  pumpActive = true;
  Serial.println("Pump on");
}

void webPumpOn() {
    pumpOn();
    server.send(200, "text/plain", "Pump activated for 5 seconds!");
    Serial.println("Pump on (Web)");
}

void uploadDataToGoogleDrive(String time, String temperature, String moisture, String humidityData) {
  HTTPClient http;
  http.begin(googleAppsScriptURL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<1024> jsonDocument;
  jsonDocument["time"] = time;
  jsonDocument["temperature"] = temperature;
  jsonDocument["moisture"] = moisture;
  jsonDocument["humidityData"] = humidityData;

  String json;
  serializeJson(jsonDocument, json);

  int httpResponseCode = http.POST(json);

  if (httpResponseCode > 0) {
    Serial.print("HTTP Response code: ");
    Serial.println(httpResponseCode);
  } else {
    Serial.print("Error on sending data: ");
    Serial.println(httpResponseCode);
  }

  http.end();
}


void uploadData() {
  unsigned long currentMillisUpload = millis();
  if (currentMillisUpload - previousMillisUpload >= intervalUpload) {
      previousMillisUpload = currentMillisUpload;
      if(globalCurrentTimeStr.length() > 0 && globalTemperatureValue.length() > 0 && globalMoisturePercentage.length() > 0) {
          uploadDataToGoogleDrive(globalCurrentTimeStr, globalTemperatureValue, globalMoisturePercentage, globalHumidityData);
           globalHumidityData = "";
      }
    }
}

// --- SOIL MOISTURE SENSOR ---
void readSoilMoisture() {
  unsigned long currentMillisSoil = millis();
  if (currentMillisSoil - previousMillisSoil >= intervalSoil) {
    previousMillisSoil = currentMillisSoil;
    soilMoistureValue = analogRead(SOIL_SENSOR_PIN);
    Serial.print("Raw Soil Moisture Value: ");
    Serial.println(soilMoistureValue);

    int moisturePercentage = 0;
    if(soilMoistureValue <= drySoilValue){
       moisturePercentage = map(soilMoistureValue, wetSoilValue, drySoilValue, 100, 0);
       //moisturePercentage = map(soilMoistureValue, wetSoilValue, drySoilValue, 100, 0);
    } else {
       moisturePercentage = 0;
    }
    if (moisturePercentage < 0) {
      moisturePercentage = 0;
    } else if (moisturePercentage > 100) {
      moisturePercentage = 100;
    }

    String dataPoint = "\"" + currentTimeStr + "\": " + String(moisturePercentage);
    if (globalHumidityData.length() > 0) {
        globalHumidityData += ", ";
    }
        globalHumidityData += dataPoint;

     globalCurrentTimeStr = currentTimeStr;
      globalMoisturePercentage = String(moisturePercentage);
    
    Serial.print("Soil Moisture: ");
    Serial.print(moisturePercentage);
    Serial.println("%");

    if (moisturePercentage < soilMoistureMinLimit) {
      pumpOn();
      Serial.println("Pump triggered (Soil Moisture)");
    }
  }
}

// --- TEMPERATURE SENSOR ---
void readTemperature() {
  unsigned long currentMillisTemp = millis();
  if (currentMillisTemp - previousMillisTemp >= intervalTemp) {
    previousMillisTemp = currentMillisTemp;
    sensors.requestTemperatures();
    temperatureValue = sensors.getTempCByIndex(0);
    Serial.print("Temperature: ");
    Serial.print(temperatureValue);
    Serial.println("ºC");
      globalTemperatureValue = String(temperatureValue, 1);
  }
}

// --- LED CONTROL ---
void controlLED() {
  int currentHour = timeClient.getHours();
  if (currentHour >= sunriseStartHour && currentHour < sunsetEndHour) {
    // Calculate brightness percentage based on time of day
    float progress = (float)(currentHour - sunriseStartHour) / (sunsetEndHour - sunriseStartHour);
    if (progress <= 0.5) {
        ledBrightness = map(progress * 100, 0, 50, 0, LED_MAX_BRIGHTNESS);
    } else {
        ledBrightness = map((1 - progress) * 100, 0, 50, 0, LED_MAX_BRIGHTNESS);
    }
    analogWrite(LED_PIN, ledBrightness);
    Serial.print("LED Brightness: ");
    Serial.println(ledBrightness);
  } else {
    analogWrite(LED_PIN, 0); // LED off
  }
}

// --- WEB SERVER ---
void setupWebRoutes() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/pump", HTTP_GET, webPumpOn);
  server.on("/data.json", HTTP_GET, handleData);
  server.begin();
}

void handleRoot() {
  String html = "";

  // --- HEAD SECTION ---
  html += "<!DOCTYPE html><html><head><title>ESP32 Irrigation System</title>";
  html += "<script src=\"https://cdn.jsdelivr.net/npm/chart.js\"></script>";
  html += "<style>body { font-family: sans-serif; padding: 20px; } button { padding: 10px 20px; margin-top: 10px; }</style>";
  html += "</head><body>";

  // --- BODY CONTENT ---
  html += "<h1>ESP32 Irrigation System</h1>";
  html += "<p>Current Time: <span id='time'></span></p>";
  html += "<p>Temperature: <span id='temperature'></span> °C</p>";
  html += "<p>Soil Moisture: <span id='moisture'></span> %</p>";
  html += "<button onclick='activatePump()'>Activate Pump (5 seconds)</button><br><br>";
  html += "<canvas id='myCanvas' width='800' height='300'></canvas>";

  // --- JAVASCRIPT SECTION ---
  html += "<script>";
  html += "var lastData = null; var myChart = null; function updateData() {fetch('/data.json').then(response => response.json()).then(data => { document.getElementById('time').innerText = data.time; document.getElementById('temperature').innerText = data.temperature; document.getElementById('moisture').innerText = data.moisture; if (data.humidityData != null && data.temperatureData != null){drawChart(JSON.parse('{' + data.humidityData + '}'), data.temperatureData);} lastData = data;}).catch(error => console.error('Error fetching data:', error));} setInterval(updateData, 60000); updateData(); function activatePump() { fetch('/pump').then(response => response.text()).then(message => alert(message)).catch(error => console.error('Error:', error)); }; function drawChart(data, tempData) { const labels = Object.keys(data); const values = Object.values(data); const tempValues = [].fill(parseFloat(tempData), 0, labels.length) ; const ctx = document.getElementById('myCanvas').getContext('2d'); if (myChart) { myChart.destroy(); } myChart = new Chart(ctx, { type: 'line', data: { labels: labels, datasets: [{ label: 'Humidity (%)', data: values, borderColor: 'blue', borderWidth: 1, tension: 0.4 , yAxisID: 'y1'}, {label: 'Temperature (°C)', data: tempValues, borderColor: 'red', borderWidth: 1, tension: 0.4, yAxisID: 'y2'}] }, options: { scales: { y1: { beginAtZero: true, position: 'left' }, y2: { position: 'right' } }, plugins: { legend: { display: true } } } }); };";
  html += "</script>";
  // --- END OF HTML ---
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleData() {
  dataJson.clear();
  dataJson["time"] = currentTimeStr;
  dataJson["temperature"] = String(temperatureValue, 1);
   int moisturePercentage = map(soilMoistureValue, drySoilValue, wetSoilValue, 0, 100);
    if (moisturePercentage < 0) moisturePercentage = 0;
    else if (moisturePercentage > 100) moisturePercentage = 100;
    dataJson["moisture"] = String(moisturePercentage);
    if(humidityData.length() > 0){
         dataJson["humidityData"] = humidityData;
    }
  
    String jsonString;
    serializeJson(dataJson, jsonString);
    server.send(200, "application/json", jsonString);
}

// --- EEPROM FUNCTIONS ---
void writeCredentialsToEEPROM() {
  Serial.println("Saving credentials to EEPROM...");
  for (int i = 0; i < strlen(ssid); i++) {
    EEPROM.write(eepromSSIDAddress + i, ssid[i]);
  }
  EEPROM.write(eepromSSIDAddress + strlen(ssid), '\0');
  for (int i = 0; i < strlen(password); i++) {
    EEPROM.write(eepromPasswordAddress + i, password[i]);
  }
  EEPROM.write(eepromPasswordAddress + strlen(password), '\0');
  EEPROM.commit();
  Serial.println("Credentials saved to EEPROM");
}

void readCredentialsFromEEPROM() {
  Serial.println("Loading credentials from EEPROM...");
  EEPROM.readString(eepromSSIDAddress, ssid, sizeof(ssid));
  EEPROM.readString(eepromPasswordAddress, password, sizeof(password));
  if (strlen(ssid) == 0 || strlen(password) == 0) {
      Serial.println("No credentials found on EEPROM. Using default values");
    strcpy(ssid, defaultSSID);
    strcpy(password, defaultPassword);
    writeCredentialsToEEPROM();
  } else{
    Serial.print("SSID from EEPROM: ");
     Serial.println(ssid);
    Serial.print("Password from EEPROM: ");
      Serial.println(password);
  }
}