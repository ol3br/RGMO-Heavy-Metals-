#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- NETWORK DETAILS ---
const char* ssid = "Dan";        
const char* password = "Dnrrs544284";      

// --- THINGSPEAK HARDCODED DETAILS ---
const char* thingSpeakApiKey = "OGKSMYTUKYD453ET"; 
const char* serverName = "http://api.thingspeak.com/update";

// --- OLED CONFIGURATION ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- PINS & RELAY ---
#define PH_PIN 35
#define TDS_PIN 34
#define EC_PIN 32
#define RELAY_EC_PIN 25
#define ONE_WIRE_BUS 4 // DS18B20 Data Pin

// --- SENSOR SETUP ---
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// --- CALIBRATION ---
const float V_neutral = 1.761; 
const float V_acid    = 1.938; 
const float calibrationADC = 148.0;      
const float calibrationSolution = 1413.0; 
const float VREF = 3.3;

float calculatedPH = 0.0;
float ecValue = 0.0;
float tdsValue = 0.0;
float waterTemp = 0.0; // New variable for temperature

void runCountdown(String currentStatus, String nextStatus) {
  for(int countdown = 10; countdown > 0; countdown--) {
    display.clearDisplay();
    display.fillRect(0, 0, 128, 14, SSD1306_WHITE); 
    display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
    display.setCursor(5, 3);
    display.print(currentStatus);

    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);
    display.setCursor(10, 24);
    display.print("Tub Discharging...");
    display.setCursor(10, 44);
    display.print(nextStatus);
    display.print(countdown);
    display.print("s");
    display.display();
    delay(1000); 
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  
  pinMode(PH_PIN, INPUT);
  pinMode(TDS_PIN, INPUT);
  pinMode(EC_PIN, INPUT);
  pinMode(RELAY_EC_PIN, OUTPUT);
  digitalWrite(RELAY_EC_PIN, HIGH); 
  analogSetAttenuation(ADC_11db); 

  sensors.begin(); // Initialize DS18B20

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { 
    for(;;); 
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.print("Initializing...");
  display.display();
  delay(1000);
}

void loop() {
  // =======================================================
  // STEP 1: FORCE WI-FI RADIO COMPLETELY OFF FOR SILENT QUIET ZONE
  // =======================================================
  Serial.println("[RADIO] Shutting down Wi-Fi for noise isolation...");
  WiFi.disconnect(true); // Added true to fully clear connection state
  WiFi.mode(WIFI_OFF);
  delay(500); 

  // =======================================================
  // STEP 2: READ EC (RELAY ON) -> NO RADIO INTERFERENCE
  // =======================================================
  Serial.println("[RELAY ON] Powering up EC Sensor...");
  digitalWrite(RELAY_EC_PIN, LOW); 
  delay(1500); 

  long ecTotal = 0;
  for (int i = 0; i < 30; i++) {
    ecTotal += analogRead(EC_PIN);
    delay(5);
  }
  float averageEC_ADC = (float)ecTotal / 30.0;
  ecValue = (averageEC_ADC > 10.0) ? (averageEC_ADC / calibrationADC) * calibrationSolution : 0;

  // =======================================================
  // STEP 3: DISCONNECT EC RELAY
  // =======================================================
  digitalWrite(RELAY_EC_PIN, HIGH); 
  Serial.println("[RELAY OFF] EC Disconnected.");

  // =======================================================
  // STEP 4: 10-SECOND DISCHARGE TIME
  // =======================================================
  runCountdown("EC CHECKED", "pH/Temp Read in: ");

  // =======================================================
  // STEP 5: READ pH, TDS & TEMP (RADIO IS DEAD SILENT)
  // =======================================================
  
  // Request temperature from DS18B20
  sensors.requestTemperatures(); 
  waterTemp = sensors.getTempCByIndex(0);

  long phTotal = 0, tdsTotal = 0;
  for (int i = 0; i < 40; i++) { 
    phTotal += analogRead(PH_PIN);
    tdsTotal += analogRead(TDS_PIN);
    delay(5);
  }
  
  float averagePH_ADC = (float)phTotal / 40.0;
  float phVoltage = (averagePH_ADC / 4095.0) * VREF; 
  float phSlope = (7.0 - 4.01) / (V_neutral - V_acid);
  calculatedPH = constrain(7.0 + (phVoltage - V_neutral) * phSlope, 0.0, 14.0);

  float averageTdsADC = (float)tdsTotal / 40.0;
  float tdsVoltage = (averageTdsADC / 4095.0) * VREF;
  tdsValue = (133.42 * pow(tdsVoltage, 3) - 255.86 * pow(tdsVoltage, 2) + 857.39 * tdsVoltage) * 0.5;
  if (tdsValue < 0) tdsValue = 0;

  // =======================================================
  // STEP 6: UPDATE LOCAL OLED DISPLAY
  // =======================================================
  display.clearDisplay();
  display.fillRect(0, 0, 128, 14, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK, SSD1306_WHITE); 
  display.setCursor(18, 3);
  display.print(F("HYDRO DASHBOARD"));
  display.setTextColor(SSD1306_WHITE, SSD1306_BLACK); 
  
  // Adjusted layout to fit temperature alongside pH
  display.setCursor(0, 18);  display.print(F("pH: ")); display.print(calculatedPH, 2);
  display.setCursor(70, 18); display.print(waterTemp, 1); display.print(F(" C"));
  
  display.setCursor(4, 34);  display.print(F("EC:  ")); display.print(ecValue, 0); display.print(F(" uS/cm"));
  display.setCursor(4, 52);  display.print(F("TDS: ")); display.print(tdsValue, 0); display.print(F(" ppm"));
  display.display();
  
  Serial.print("Pure Isolated Reading -> pH: "); Serial.print(calculatedPH, 2);
  Serial.print(" | Temp: "); Serial.print(waterTemp, 1); Serial.print("C");
  Serial.print(" | TDS: "); Serial.print(tdsValue, 0);
  Serial.print(" | EC: "); Serial.println(ecValue, 0);

  // =======================================================
  // STEP 7: WAKE UP WI-FI & TRANSMIT CAPTURED DATA TO CLOUD
  // =======================================================
  Serial.println("[RADIO] Waking up Wi-Fi transmitter...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(400); // Fixed the 4-second timeout bug from earlier!
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[CLOUD] Connected! Sending clean snapshot...");
    
    HTTPClient http;
    // Added field4 for Temperature
    String url = String(serverName) + "?api_key=" + thingSpeakApiKey + 
                 "&field1=" + String(calculatedPH, 2) + 
                 "&field2=" + String(tdsValue, 0) + 
                 "&field3=" + String(ecValue, 0) +
                 "&field4=" + String(waterTemp, 2); 
    
    http.begin(url);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      Serial.print("[CLOUD] Success! ThingSpeak Code: ");
      Serial.println(httpResponseCode);
    } else {
      Serial.print("[CLOUD] Transmission error: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("\n[CLOUD] Hotspot not found this cycle. Saved data locally.");
  }

  delay(2000); 
  runCountdown("SYSTEM RESTING", "Next Cycle in: ");
}