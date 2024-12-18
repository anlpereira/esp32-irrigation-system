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
#define LED_MAX_BRIGHTNESS 255
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
const int wetSoilValue = 2900;
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
unsigned long previousMillisLED = 0;
const long intervalLEDPrint = 5000; // Print LED brightness every 5 seconds
const int ledDayBrightness = (int)(LED_MAX_BRIGHTNESS * 0.8); // 80% PWM during the day

// --- WEB SERVER ---
WebServer server(80);
const char* host = "esp32";
const String googleAppsScriptGetDataURL = "https://script.google.com/macros/s/AKfycbxDGVDkJq0flGF0HogjjrIVOAG8Q-uaoD4vKtTMi1WsdYlbAmH-qf-iIzLhfkdVanJi/exec"; // URL for fetching data
const String googleAppsScriptURL = "https://script.google.com/macros/s/AKfycbxDGVDkJq0flGF0HogjjrIVOAG8Q-uaoD4vKtTMi1WsdYlbAmH-qf-iIzLhfkdVanJi/exec";

//Interval Upload
unsigned long previousMillisUpload = 0;
const long intervalUpload = 600000; // 10 minutes in milliseconds, the same as the other intervals

String globalCurrentDayStr;
String globalHourTimeStr;
String globalTemperatureValue;
String globalMoisturePercentage;
String globalHumidityData = "";
String globalHumidityValue;
// --- JSON ---
StaticJsonDocument<3000> dataJson;
String currentDayStr;

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
    
    time_t epochTime = timeClient.getEpochTime();
    struct tm * ptm = localtime(&epochTime);

    int year = ptm->tm_year + 1900;
    int month = ptm->tm_mon + 1;
    int day = ptm->tm_mday;
     int hour = ptm->tm_hour;
     int minute = ptm->tm_min;
      int second = ptm->tm_sec;
    
    String monthStr;
     switch (month) {
      case 1: monthStr = "Jan"; break;
      case 2: monthStr = "Fev"; break;
      case 3: monthStr = "Mar"; break;
      case 4: monthStr = "Abr"; break;
      case 5: monthStr = "Mai"; break;
      case 6: monthStr = "Jun"; break;
      case 7: monthStr = "Jul"; break;
      case 8: monthStr = "Ago"; break;
      case 9: monthStr = "Set"; break;
      case 10: monthStr = "Otu"; break;
      case 11: monthStr = "Nov"; break;
      case 12: monthStr = "Dez"; break;
      default: monthStr = ""; break;
    }

    currentDayStr = String(day) + "-" + monthStr + "-" + String(year);
    String hourStr = String(hour);
    String minuteStr = String(minute);
    if (hour < 10){
     hourStr = "0" + String(hour);
    }
    if(minute < 10){
      minuteStr = "0" + String(minute);
    }
    String currentTime = hourStr + ":" + minuteStr;
   
    globalCurrentDayStr = currentDayStr;
    globalHourTimeStr = currentTime;
    Serial.print("Current Time: ");
    Serial.println(globalCurrentDayStr);
    Serial.print("Current Hour: ");
    Serial.println(globalHourTimeStr);
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

void uploadDataToGoogleDrive(String day, String time, String temperature, String humidity) {
  HTTPClient http;
  http.begin(googleAppsScriptURL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> jsonDocument; // Reduced buffer size
  jsonDocument["day"] = day;
  jsonDocument["time"] = time;
  jsonDocument["temperature"] = temperature;
  jsonDocument["humidity"] = humidity;

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

void handleData() {
  String type = server.arg("type"); // Get request type parameter
  String interval = server.arg("interval"); // Get interval parameter

  if (type == "graph") {  // Handle Graph data retrieval

    if (interval.length() == 0) {
      interval = "1d";
    }

    HTTPClient http;
    http.begin(googleAppsScriptGetDataURL + "?interval=" + interval);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int httpResponseCode = http.GET();
    String payload = "";

    if (httpResponseCode > 0) {
      payload = http.getString();
      Serial.println(payload);
    } else {
       Serial.print("Error fetching data. HTTP Response code: ");
       Serial.println(httpResponseCode);
       payload = "{\"error\": \"Failed to fetch data from Google Apps Script\"}";
     }
     http.end();

    server.send(200, "application/json", payload);

  } else if (type == "periodic") { // Handle periodic data sending
          StaticJsonDocument<256> dataJson;
          dataJson["day"] = globalCurrentDayStr;
          dataJson["time"] =  globalHourTimeStr;
          dataJson["temperature"] = String(globalTemperatureValue.toFloat(), 1);
          dataJson["humidity"] = globalHumidityValue;
            String jsonString;
           serializeJson(dataJson, jsonString);
           server.send(200, "application/json", jsonString);

  } else {
        server.send(400, "text/plain", "Invalid request type"); // Handle invalid requests
    }
}

void uploadData() {
  unsigned long currentMillisUpload = millis();
  if (currentMillisUpload - previousMillisUpload >= intervalUpload) {
      previousMillisUpload = currentMillisUpload;
      if(globalCurrentDayStr.length() > 0 && globalTemperatureValue.length() > 0 && globalMoisturePercentage.length() > 0 && globalHumidityValue.length() > 0) {
          uploadDataToGoogleDrive(globalCurrentDayStr, globalHourTimeStr, globalTemperatureValue, globalHumidityValue);
          Serial.println("Data sent to GoogleSheet");
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

    String dataPoint = "\"" + currentDayStr + "\": " + String(moisturePercentage);
    if (globalHumidityData.length() > 0) {
        globalHumidityData += ", ";
    }
        globalHumidityData += dataPoint;

     globalCurrentDayStr = currentDayStr;
      globalMoisturePercentage = String(moisturePercentage);
    globalHumidityValue = String(moisturePercentage); // Same value of moisture
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
  
    // Set LED to 80% brightness during the day
    if (currentHour >= sunriseStartHour && currentHour < sunsetEndHour) {
      ledBrightness = ledDayBrightness;
    } else {
      ledBrightness = 0; // LED off
    }
    analogWrite(LED_PIN, ledBrightness);

    unsigned long currentMillisLED = millis();
    if (currentMillisLED - previousMillisLED >= intervalLEDPrint) {
      previousMillisLED = currentMillisLED;
      Serial.print("LED Brightness: ");
      Serial.println(ledBrightness);
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
   html += "let myChart = null;";


   html += "function updateData() {";
   html += "   fetch('/data.json?type=periodic')";
   html += "       .then(response => response.json())";
   html += "       .then(data => {";
      html += "       if(data && data.time && data.temperature && data.moisture){";
      html += "            document.getElementById('time').innerText = data.time;";
         html += "         document.getElementById('temperature').innerText = data.temperature;";
      html += "          document.getElementById('moisture').innerText = data.moisture;";
      html += "        }";
     html += "       }).catch(error => console.error('Error fetching data:', error));";
    html += "}";

   html += "setInterval(updateData, 60000);";
    html += "updateData();";

  html += " function drawChart(data) {";
    html += "    try{";
    html += "      const temperatureData = data.data.map(row => parseFloat(row[1]));";
   html += "    const moistureData = data.data.map(row => parseFloat(row[2]));";
  html += "    const timeData = data.data.map(row => row[0]);";
   html += "   const ctx = document.getElementById('myCanvas').getContext('2d');";
  html += "   if (myChart) {";
     html += "       myChart.destroy();";
    html += "   }";
    html += "   myChart = new Chart(ctx, {";
     html += "    type: 'line',";
    html += "    data: {";
    html += "      labels: timeData,";
     html += "       datasets: [{";
   html += "         label: 'Temperature (°C)',";
     html += "           data: temperatureData,";
  html += "           borderColor: 'red',";
    html += "            borderWidth: 1,";
     html += "        tension: 0.4,";
     html += "         yAxisID: 'y1'";
    html += "       }, {";
    html += "        label: 'Moisture (%)',";
    html += "         data: moistureData,";
    html += "          borderColor: 'blue',";
    html += "           borderWidth: 1,";
   html += "            tension: 0.4,";
   html += "           yAxisID: 'y2'";
   html += "      }]";
   html += "   },";
   html += "   options: {";
     html += "      scales: {";
   html += "          y1: {";
   html += "             beginAtZero: true,";
    html += "             position: 'left'";
   html += "         },";
    html += "        y2: {";
    html += "           position: 'right'";
   html += "         },";
    html += "        x: {";
   html += "            display: true,";
    html += "            title: {";
     html += "              display: true,";
    html += "             text: 'Time'";
    html += "           }";
    html += "         }";
   html += "       },";
     html += "      plugins: {";
     html += "        legend: {";
      html += "          display: true";
      html += "      }";
    html += "      }";
   html += "   }";
    html += "  });";
     html += "    }catch(error){";
      html += "      console.error('Error parsing data', error);";
      html += "   }";
   html += " }";

   html += "    fetch('/data.json?type=graph&interval=1d')";
   html += "        .then(response => response.json())";
   html += "        .then(data => {";
    html += "          drawChart(data);";
    html += "        })";
    html += "        .catch(error => console.error('Error fetching data:', error));";

  html += "</script>";
    // --- END OF HTML ---
    html += "</body></html>";
  server.send(200, "text/html", html);
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
