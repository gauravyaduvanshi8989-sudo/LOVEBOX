// ===================================================================
// 🔥 LOVEBOX MASTER FIRMWARE v34.3 - 1.8x DYNAMIC RGB SHIFTING CORE
// ===================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>
#include <FirebaseESP32.h> 
#include "time.h" 
#include <soc/soc.h>           
#include <soc/rtc_cntl_reg.h>   

#define FIREBASE_HOST "lovebox-2507-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "dCOFIwVRzAFpBO9Rbl3TVP049GVxrZcYUKYSO6qn"

const int bumpSwitchPin = 12; 
const int batteryAdcPin = 34; 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

WebServer server(80);
DNSServer dnsServer; 
const byte DNS_PORT = 53; 

FirebaseData firebaseNodeInbound;  
FirebaseData firebaseNodeOutbound; 
FirebaseConfig config;
FirebaseAuth auth;

enum SystemState {
  STATE_WELCOME,
  STATE_WIFI_COUNTDOWN,
  STATE_PORTAL_OPEN,
  STATE_PORTAL_WAITING,
  STATE_PORTAL_SUCCESS,
  STATE_HOME,
  STATE_MESSAGE_VIEW,
  STATE_TYPING_REPLY
};
volatile SystemState currentState = STATE_WELCOME;

void handleRoot();
void handleSave();
void launchCaptivePortal();
bool isStrictlyConnectedToRouter();
bool scanSavedWifiPresenceInAir();
void initFirebaseSilent();
String getLocalTimeString();
int calculateBatteryPercentage(int rawAdc);
int renderSmartWordWrapEngine(String text, int startY, int lineScrollOffset, bool executeRender);
void printSmartWordWrap(String text, int startY);
void writeServoAngle(int angle);
void runServoWaveEngine();
void triggerTwiceBeepNotification(); 
void runBuzzerSchedulerTick(); 
void runRgbBreatheEngineTick(); 

// --- KEYPAD CONFIG ---
const byte ROWS = 4; 
const byte COLS = 4; 
char hexaKeys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
}; 
byte rowPins[ROWS] = {14, 27, 26, 25}; 
byte colPins[COLS] = {33, 32, 15, 13}; 
Keypad customKeypad = Keypad(makeKeymap(hexaKeys), rowPins, colPins, ROWS, COLS);

const char* customT9Matrix[] = {
  "ST", "AB", "CD", "EF", "GH", "IJ", "KL", "MN", "OP", "QR", "UV", "WX", "YZ"
};

String qsid = "", qpass = "";
volatile int countdownSeconds = 15;
unsigned long stateTimer = 0;
bool forceRefresh = true;
volatile bool lidOpened = true;
bool lastLidState = true;

String currentMessage = "";
char lastKey = '\0';
int keyPressCount = 0;
unsigned long lastKeyPressTime = 0;
unsigned long lastKeypadActivity = 0;

unsigned long longPressTimer = 0;
bool keyIsPressed = false;
int blinkAnimationFrame = 0;
unsigned long lastBlinkTime = 0;

float currentLiveSystemVoltage = 3.80; 
int currentBatteryPercentage = -1;  
float smoothedRawAdcMemory = -1.0f; 
float debugRawAdcVal = 0; 

String lastDisplayMessage = "Waiting for message...";
String globalFirebaseMessage = "";
volatile bool newInboundMessageAvailable = false;
unsigned long messageDisplayStartTimestamp = 0;
volatile bool remoteWebUserTyping = false;
unsigned long lastTypingSignalReceived = 0; 
volatile bool firebaseInitialized = false; 

String partnerLastSeenStr = "--:--"; 
String globalOutboundReplyPayload = "";
volatile bool pendingBackgroundDispatchTrigger = false;

SemaphoreHandle_t dataMutex;
TaskHandle_t NetworkTaskHandle = NULL;

// ===== TITANIC MELODY INTEGRATION =====
#define NOTE_F5 698
#define NOTE_G5 784
#define NOTE_A5 880
#define NOTE_B5 988
#define NOTE_C6 1047

const int tMelody[] = {NOTE_F5,NOTE_G5,NOTE_A5,NOTE_G5,NOTE_F5,NOTE_G5,NOTE_C6,NOTE_B5,NOTE_A5,NOTE_F5};
const int tDurations[] = {400,400,600,400,400,400,600,400,400,800};
const int tTotalNotes = sizeof(tMelody)/sizeof(tMelody[0]);
TaskHandle_t TitanicTaskHandle = NULL;

void titanicAsyncCore0Worker(void *pvParameters);


const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 19800; 
const int daylightOffset_sec = 0;

unsigned long lastScrollTickTimestamp = 0;
int currentMessageScrollLineOffset = 0;
int totalComputedMessageLines = 0;

unsigned long lastBackgroundSTAAttemptTimestamp = 0; 
unsigned long lastWifiWatchdogCheck = 0; 
unsigned long lastPortalScanTimestamp = 0; 
unsigned long lastBatteryUpdateTimestamp = 0; 

// ===================================================================
// 🎯 SERVO & BUZZER ISOLATED HARDWARE CHANNELS
// ===================================================================
const int servoPin = 18;          
const int servoChannel = 0;       
const int pwmFrequency = 50;      
const int pwmResolution = 16;     

const int servoMinDuty = 1250;    
const int servoMaxDuty = 8580;    

const int buzzerPin = 4;          
const int buzzerChannel = 8;      
const int buzzerResolution = 8;   

// --- SAFE RGB ARDUINO DRIVER PIN MAPS ---
const int redPin = 5;             
const int greenPin = 2;           
const int bluePin = 19;           

float waveRadianTracker = 0.0f;   
float waveSpeedPacing = 0.07f;    
int centerOffsetAngle = 90;       
int extremeAmplitude = 85;        

unsigned long lastServoMoveTick = 0;
int currentServoAngle = 90;

unsigned long servoExecutionStartTime = 0;
volatile bool isServoCurrentlyActive = false;

volatile int buzzerStepSequence = 0;
unsigned long lastBuzzerStepMilli = 0;

// --- DYNAMIC RGB SCHEDULER VARS ---
unsigned long ledExecutionStartTime = 0;
volatile bool isLedCurrentlyActive = false;
unsigned long lastLedUpdateTick = 0;
float ledBreatheRadian = 0.0f;
int rMax = 255, gMax = 255, bMax = 255;

void writeServoAngle(int angle) {
  if(angle < 2) angle = 2;
  if(angle > 178) angle = 178;
  long duty = map(angle, 0, 180, servoMinDuty, servoMaxDuty);
  ledcWrite(servoChannel, duty);
}

void runServoWaveEngine() {
  if (!isServoCurrentlyActive) return;

  if (millis() - servoExecutionStartTime >= 7000) {
    isServoCurrentlyActive = false;
    writeServoAngle(90); 
    waveRadianTracker = 0.0f; 
    currentServoAngle = 90;
    Serial.println(">> [Engine] 7s Window Expired. Servo Centered.");
    return;
  }

  if (millis() - lastServoMoveTick >= 11) { 
    lastServoMoveTick = millis();
    
    waveRadianTracker += waveSpeedPacing;
    if (waveRadianTracker > 2 * PI) {
      waveRadianTracker -= 2 * PI;
    }
    
    float dynamicSineMultiplier = sin(waveRadianTracker);
    currentServoAngle = centerOffsetAngle + (int)(dynamicSineMultiplier * extremeAmplitude);
    
    writeServoAngle(currentServoAngle);
  }
}

void triggerTwiceBeepNotification() {
  if (buzzerStepSequence == 0) {
    buzzerStepSequence = 1;
    lastBuzzerStepMilli = millis();
    ledcWriteTone(buzzerChannel, 1600); 
  }
}

void runBuzzerSchedulerTick() {
  if (buzzerStepSequence == 0) return;
  unsigned long delta = millis() - lastBuzzerStepMilli;

  switch (buzzerStepSequence) {
    case 1:
      if (delta >= 120) {
        ledcWrite(buzzerChannel, 0); 
        buzzerStepSequence = 2;
        lastBuzzerStepMilli = millis();
      }
      break;
    case 2:
      if (delta >= 80) {
        ledcWriteTone(buzzerChannel, 1600); 
        buzzerStepSequence = 3;
        lastBuzzerStepMilli = millis();
      }
      break;
    case 3:
      if (delta >= 120) {
        ledcWrite(buzzerChannel, 0); 
        buzzerStepSequence = 0;       
        
        if (!lidOpened) {
          if (!isServoCurrentlyActive) {
            servoExecutionStartTime = millis();
            waveRadianTracker = 0.0f; 
            isServoCurrentlyActive = true; 
          }
          if (!isLedCurrentlyActive) {
            ledExecutionStartTime = millis();
            ledBreatheRadian = 0.0f;
            rMax = random(40, 256);
            gMax = random(40, 256);
            bMax = random(40, 256);
            isLedCurrentlyActive = true;
            Serial.println(">> [Sequence Core] Beeps Completed. Servo & RGB Shift Started!");
          }
        }
      }
      break;
  }
}

// 🌈 RE-ENGINEERED 1.8X FAST MULTI-COLOR LIVE SHIFTING DRIVER
void runRgbBreatheEngineTick() {
  if (!isLedCurrentlyActive) return;

  if (millis() - ledExecutionStartTime >= 10000) {
    isLedCurrentlyActive = false;
    analogWrite(redPin, 0);
    analogWrite(greenPin, 0);
    analogWrite(bluePin, 0);
    Serial.println(">> [RGB LED] 10s Window Expired. Lights Off.");
    return;
  }

  if (millis() - lastLedUpdateTick >= 25) {
    lastLedUpdateTick = millis();

    // --- REQ 1: SPEEDED UP UNTO 0.09f FOR EXACT 1.8X FLOW ACCELERATION ---
    ledBreatheRadian += 0.09f; 
    if (ledBreatheRadian > 2 * PI) {
      ledBreatheRadian -= 2 * PI;
      
      // --- REQ 2: REAL-TIME HUE SHIFT ON EVERY INTERPOLATION CYCLE ---
      // Changes bounds actively in RGB format format while wave rolls
      rMax = random(50, 256);
      gMax = random(50, 256);
      bMax = random(50, 256);
    }

    float fadeMultiplier = (sin(ledBreatheRadian - PI / 2) + 1.0f) / 2.0f;

    analogWrite(redPin, (int)(fadeMultiplier * rMax));
    analogWrite(greenPin, (int)(fadeMultiplier * gMax));
    analogWrite(bluePin, (int)(fadeMultiplier * bMax));
  }
}

int calculateBatteryPercentage(int rawAdc) {
  int minAdc = 1600; int maxAdc = 2500; 
  if (rawAdc >= maxAdc) return 100;
  if (rawAdc <= minAdc) return 0;
  return (int)((rawAdc - minAdc) * 100 / (maxAdc - minAdc));
}

String getLocalTimeString() {
  time_t now; struct tm timeinfo; time(&now); localtime_r(&now, &timeinfo);
  if (timeinfo.tm_year < 70) return ""; 
  char timeStringBuff[30]; 
  strftime(timeStringBuff, sizeof(timeStringBuff), "%I:%M %p", &timeinfo);
  return String(timeStringBuff);
}

bool isStrictlyConnectedToRouter() {
  if (WiFi.status() != WL_CONNECTED) return false;
  IPAddress localIP = WiFi.localIP();
  if (localIP == IPAddress(0,0,0,0) || localIP == IPAddress(192,168,4,1)) return false;
  return true;
}

bool scanSavedWifiPresenceInAir() {
  if (qsid.length() == 0) return false;
  int n = WiFi.scanNetworks(false, false, false, 80); 
  for (int i = 0; i < n; ++i) {
    if (WiFi.SSID(i) == qsid) { WiFi.scanDelete(); return true; }
  }
  WiFi.scanDelete(); return false;
}

void launchCaptivePortal() {
  server.stop(); dnsServer.stop(); WiFi.softAPdisconnect(true); WiFi.disconnect(true, true); WiFi.mode(WIFI_OFF); delay(50); 
  WiFi.mode(WIFI_AP_STA); delay(50);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP("My LoveBox", "");
  dnsServer.start(DNS_PORT, "*", IPAddress(192, 168, 4, 1));
  server.begin();
}

void initFirebaseSilent() {
  if (!firebaseInitialized) {
    config.host = FIREBASE_HOST; config.signer.tokens.legacy_token = FIREBASE_AUTH; 
    Firebase.begin(&config, &auth); Firebase.reconnectWiFi(true); 
    firebaseInitialized = true; configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
}

int getT9MatrixIndex(char key) {
  if (key == '*') return 0;
  if (key >= '1' && key <= '9') return (key - '0');
  if (key == '0') return 10;
  if (key == '#') return 11;
  if (key == 'D') return 12;
  return -1;
}

char getLongPressNumericValue(char key) {
  if (key == '*') return '0'; if (key >= '1' && key <= '9') return key;
  if (key == '0') return '0'; if (key == '#') return '.'; if (key == 'D') return '!'; 
  return '\0';
}

int renderSmartWordWrapEngine(String text, int startY, int lineScrollOffset, bool executeRender) {
  int currentLineLength = 0; int currentVerticalLineIdx = 0; int fontHeightPixels = 8; 
  if (executeRender) display.setCursor(0, startY);
  for (int i = 0; i < text.length(); i++) {
    if (text[i] == '\n') {
      currentVerticalLineIdx++; currentLineLength = 0;
      if (executeRender && (currentVerticalLineIdx >= lineScrollOffset) && ((currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY < 64)) {
        display.setCursor(0, (currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY);
      }
      continue;
    }
    int nextSpaceIdx = text.indexOf(' ', i); int endOfWordIdx = (nextSpaceIdx != -1) ? nextSpaceIdx : text.length();
    String word = text.substring(i, endOfWordIdx);
    if (word.length() > 21) {
      for (int j = 0; j < word.length(); j++) {
        if (currentLineLength >= 21) {
          currentVerticalLineIdx++; currentLineLength = 0;
          if (executeRender && (currentVerticalLineIdx >= lineScrollOffset) && ((currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY < 64)) {
            display.setCursor(0, (currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY);
          }
        }
        if (executeRender && (currentVerticalLineIdx >= lineScrollOffset) && ((currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY < 64)) { display.print(word[j]); }
        currentLineLength++;
      }
      i = endOfWordIdx - 1;
    }
    else if (currentLineLength + word.length() <= 21) {
      if (executeRender && (currentVerticalLineIdx >= lineScrollOffset) && ((currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY < 64)) { display.print(word); }
      currentLineLength += word.length(); i = endOfWordIdx - 1;
    } 
    else {
      currentVerticalLineIdx++; currentLineLength = word.length();
      if (executeRender && (currentVerticalLineIdx >= lineScrollOffset) && ((currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY < 64)) {
        display.setCursor(0, (currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY); display.print(word);
      }
      i = endOfWordIdx - 1;
    }
    if (i + 1 < text.length() && text[i + 1] == ' ') {
      if (currentLineLength < 21) {
        if (executeRender && (currentVerticalLineIdx >= lineScrollOffset) && ((currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY < 64)) { display.print(' '); }
        currentLineLength++;
      } else {
        currentVerticalLineIdx++; currentLineLength = 0;
        if (executeRender && (currentVerticalLineIdx >= lineScrollOffset) && ((currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY < 64)) {
          display.setCursor(0, (currentVerticalLineIdx - lineScrollOffset) * fontHeightPixels + startY);
        }
      }
      i++;
    }
  }
  return currentVerticalLineIdx + 1;
}

void printSmartWordWrap(String text, int startY) {
  renderSmartWordWrapEngine(text, startY, currentMessageScrollLineOffset, true);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1.0' charset='UTF-8'>";
  html += "<style>body{font-family:sans-serif;background-color:#fff5f5;text-align:center;padding:20px;}.card{background:white;padding:30px;border-radius:15px;box-shadow:0 4px 10px rgba(0,0,0,0.1);max-width:350px;margin:auto;}h2{color:#ff4d6d;}input{width:90%;padding:11px;margin:10px 0;border:1px solid #ffccd5;border-radius:8px;outline:none;}button{background:#ff4d6d;color:white;border:none;padding:12px;width:95%;border-radius:8px;cursor:pointer;font-size:16px;font-weight:bold;}</style></head><body><div class='card'><h2>❤️ LoveBox Dashboard</h2><form action='/save' method='POST'><input type='text' name='ssid' placeholder='WiFi Name' required><br><input type='password' name='pass' placeholder='WiFi Password'><br><br><button type='submit'>Connect LoveBox ✨</button></form></div></body></html>";
  server.send(200, "text/html", html);
}

void handleSave() {
  qsid = server.arg("ssid"); qpass = server.arg("pass"); qsid.trim(); qpass.trim(); 
  Preferences preferences; preferences.begin("wifi", false); preferences.clear(); preferences.putString("ssid", qsid); preferences.putString("pass", qpass); preferences.end();
  server.send(200, "text/html", "<html><body><h2>Credentials Saved! Connecting...</h2></body></html>");
  delay(100); 
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_PORTAL_WAITING; xSemaphoreGive(dataMutex); }
  stateTimer = millis(); forceRefresh = true;
}


void titanicAsyncCore0Worker(void *pvParameters){
  unsigned long start=millis();
  int idx=0;
  while(millis()-start<15000){
    ledcWriteTone(buzzerChannel,tMelody[idx]);
    vTaskDelay(pdMS_TO_TICKS(tDurations[idx]));
    ledcWrite(buzzerChannel,0);
    vTaskDelay(pdMS_TO_TICKS(70));
    idx++;
    if(idx>=tTotalNotes){idx=0;vTaskDelay(pdMS_TO_TICKS(300));}
  }
  ledcWrite(buzzerChannel,0);
  TitanicTaskHandle=NULL;
  vTaskDelete(NULL);
}

void networkCoreLoop(void * pvParameters) {
  unsigned long fastTicker = 0; unsigned long slowTicker = 0;
  for(;;) {
    if (isStrictlyConnectedToRouter() && firebaseInitialized) {
      if (pendingBackgroundDispatchTrigger) {
        String localReplyBuffer = "";
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) { localReplyBuffer = globalOutboundReplyPayload; xSemaphoreGive(dataMutex); }
        if (localReplyBuffer.length() > 0) {
          Firebase.setString(firebaseNodeOutbound, "/box_reply", localReplyBuffer); Firebase.setString(firebaseNodeOutbound, "/message_status", "seen");
          Preferences cache; cache.begin("recovery", false); cache.putString("saved_reply", ""); cache.end();
        }
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) { globalOutboundReplyPayload = ""; pendingBackgroundDispatchTrigger = false; xSemaphoreGive(dataMutex); }
      }
      
      SystemState localStateCopy;
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(30)) == pdTRUE) { localStateCopy = currentState; xSemaphoreGive(dataMutex); } else { localStateCopy = STATE_HOME; }

      if (localStateCopy >= STATE_HOME) {
        if (millis() - fastTicker > 200) { 
          fastTicker = millis();
          if (Firebase.ready()) {

if (Firebase.getString(firebaseNodeInbound, "/server_command")) {
  String cmd=firebaseNodeInbound.stringData();
  if(cmd=="trigger_titanic"){
    if(TitanicTaskHandle==NULL){
      xTaskCreatePinnedToCore(titanicAsyncCore0Worker,"TitanicTask",3072,NULL,1,&TitanicTaskHandle,0);
    }
    Firebase.setString(firebaseNodeInbound,"/server_command","null");
  } else if(cmd=="trigger_beep"){
    triggerTwiceBeepNotification();
    Firebase.setString(firebaseNodeInbound,"/server_command","null");
  }
}

            if (Firebase.getString(firebaseNodeInbound, "/message_status")) {
              String netStatus = firebaseNodeInbound.stringData();
              if (netStatus == "unseen") {
                if (Firebase.getString(firebaseNodeInbound, "/latest_message")) {
                  String freshMsg = firebaseNodeInbound.stringData();
                  if (freshMsg.length() > 0 && freshMsg != "null") {
                    
                    if (!lidOpened) {
                      triggerTwiceBeepNotification(); 
                    }

                    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                      globalFirebaseMessage = freshMsg; newInboundMessageAvailable = true; 
                      xSemaphoreGive(dataMutex);
                      Firebase.setString(firebaseNodeInbound, "/message_status", "seen"); 
                      Firebase.setString(firebaseNodeInbound, "/latest_message", "null"); 
                      Preferences cache; cache.begin("recovery", false); cache.putBool("has_msg", true); cache.putString("msg_txt", freshMsg); cache.end();
                    }
                  }
                }
              }
            }
            if (Firebase.getBool(firebaseNodeInbound, "/web_typing")) {
              bool tVal = firebaseNodeInbound.boolData();
              if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) { 
                if (tVal != remoteWebUserTyping) { remoteWebUserTyping = tVal; forceRefresh = true; } 
                if(tVal) { lastTypingSignalReceived = millis(); } 
                xSemaphoreGive(dataMutex); 
              }
            }
            if (Firebase.getString(firebaseNodeInbound, "/partner_last_seen")) {
              String statusVal = firebaseNodeInbound.stringData();
              if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50)) == pdTRUE) { if (statusVal != partnerLastSeenStr && statusVal != "null") { partnerLastSeenStr = statusVal; forceRefresh = true; } xSemaphoreGive(dataMutex); }
            }
          }
        }
        if (millis() - slowTicker > 18000) { 
          slowTicker = millis();
          if (Firebase.ready()) {
            Firebase.setInt(firebaseNodeInbound, "/box_status", 1); Firebase.setInt(firebaseNodeInbound, "/box_battery_pct", currentBatteryPercentage);
            if (Firebase.getBool(firebaseNodeInbound, "/restart_box") && firebaseNodeInbound.boolData()) { Firebase.setBool(firebaseNodeInbound, "/restart_box", false); vTaskDelay(pdMS_TO_TICKS(100)); ESP.restart(); }
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(25)); 
  }
}

void setup() {
  analogSetPinAttenuation((gpio_num_t)batteryAdcPin, ADC_11db);
  SET_PERI_REG_BITS(RTC_CNTL_BROWN_OUT_REG, RTC_CNTL_BROWN_OUT_ENA, 0, RTC_CNTL_BROWN_OUT_ENA_S); 
  Serial.begin(115200); pinMode(bumpSwitchPin, INPUT_PULLUP);
  Wire.begin(21, 22); Wire.setClock(400000);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); display.setRotation(2); display.setTextColor(SSD1306_WHITE); display.clearDisplay();
  customKeypad.setDebounceTime(25);
  Preferences preferences; preferences.begin("wifi", true); qsid = preferences.getString("ssid", ""); qpass = preferences.getString("pass", ""); preferences.end();
  qsid.trim(); qpass.trim();
  lidOpened = (digitalRead(bumpSwitchPin) == HIGH); lastLidState = lidOpened;
  dataMutex = xSemaphoreCreateMutex();
  server.on("/", handleRoot); server.on("/save", handleSave); server.onNotFound(handleRoot);
  
  ledcSetup(servoChannel, pwmFrequency, pwmResolution);
  ledcAttachPin(servoPin, servoChannel);
  writeServoAngle(90); 

  ledcSetup(buzzerChannel, 2000, buzzerResolution);
  ledcAttachPin(buzzerPin, buzzerChannel);
  ledcWrite(buzzerChannel, 0); 

  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
  analogWrite(redPin, 0);
  analogWrite(greenPin, 0);
  analogWrite(bluePin, 0);

  xTaskCreatePinnedToCore(networkCoreLoop, "NetworkTask", 8192, NULL, 1, &NetworkTaskHandle, 0);

  Preferences cache; cache.begin("recovery", true);
  bool pichlaMessageActiveTha = cache.getBool("has_msg", false); String cachedInboundMessage = cache.getString("msg_txt", ""); String cachedTypingReply = cache.getString("saved_reply", "");
  cache.end();

  if (cachedTypingReply.length() > 0) {
    currentMessage = cachedTypingReply; currentState = STATE_TYPING_REPLY; lastKeypadActivity = millis();
    if (qsid.length() > 0) { WiFi.mode(WIFI_STA); WiFi.begin(qsid.c_str(), qpass.c_str()); }
  } 
  else if (pichlaMessageActiveTha && cachedInboundMessage.length() > 0) {
    lastDisplayMessage = cachedInboundMessage; currentState = STATE_MESSAGE_VIEW; messageDisplayStartTimestamp = millis();
  } 
  else { currentState = STATE_WELCOME; }
  stateTimer = millis();
}

void loop() {
  runServoWaveEngine();
  runBuzzerSchedulerTick();
  runRgbBreatheEngineTick();

  if (millis() - lastWifiWatchdogCheck >= 5000) {
    lastWifiWatchdogCheck = millis();
    if(!isStrictlyConnectedToRouter() && currentState >= STATE_HOME) { 
      WiFi.mode(WIFI_STA);
      if (qsid.length() > 0) { WiFi.begin(qsid.c_str(), qpass.c_str()); } 
      if (lidOpened && currentState != STATE_TYPING_REPLY && currentMessage.length() == 0) {
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_WIFI_COUNTDOWN; xSemaphoreGive(dataMutex); } 
        countdownSeconds = 15; stateTimer = millis(); forceRefresh = true; 
      }
    }
  }

  if (millis() - lastBatteryUpdateTimestamp >= 5000 || lastBatteryUpdateTimestamp == 0) {
    lastBatteryUpdateTimestamp = millis();
    long adcSampleTotal = 0;
    for (int i = 0; i < 150; i++) { adcSampleTotal += analogRead(batteryAdcPin); delayMicroseconds(20); }
    float currentRawRead = adcSampleTotal / 150.0f;
    if (smoothedRawAdcMemory < 0) { smoothedRawAdcMemory = currentRawRead; } 
    else { smoothedRawAdcMemory = (smoothedRawAdcMemory * 0.95f) + (currentRawRead * 0.05f); }
    debugRawAdcVal = smoothedRawAdcMemory; 
    float measuredPinVoltage = (smoothedRawAdcMemory * 3.3f) / 4095.0f; currentLiveSystemVoltage = measuredPinVoltage * 2.0f; 
    int targetedPct = calculateBatteryPercentage((int)smoothedRawAdcMemory);
    if (currentBatteryPercentage < 0) { currentBatteryPercentage = targetedPct; } 
    else if (abs(targetedPct - currentBatteryPercentage) >= 4) { currentBatteryPercentage = targetedPct; forceRefresh = true; }
  }

  if (currentState == STATE_WIFI_COUNTDOWN && isStrictlyConnectedToRouter()) {
    initFirebaseSilent();
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_HOME; xSemaphoreGive(dataMutex); }
    forceRefresh = true;
  }

  if (remoteWebUserTyping && (millis() - lastTypingSignalReceived > 1500)) {
     remoteWebUserTyping = false; forceRefresh = true;
  }

  char key = '\0'; KeyState kState = IDLE;
  if (lidOpened) { key = customKeypad.getKey(); kState = customKeypad.getState(); }

  static char trackingActiveKey = '\0'; static unsigned long keyDebounceTimer = 0; static bool longPressExecutionFlag = false;
  if (lidOpened && kState == PRESSED && key != '\0') { trackingActiveKey = key; keyDebounceTimer = millis(); longPressExecutionFlag = false; }

  if (lidOpened && kState == HOLD && trackingActiveKey != '\0' && !longPressExecutionFlag) {
    if (millis() - keyDebounceTimer >= 450) { 
      longPressExecutionFlag = true;
      if (currentState == STATE_TYPING_REPLY) {
        lastKeypadActivity = millis();
        if (trackingActiveKey == 'C') { currentMessage = ""; lastKey = '\0'; forceRefresh = true; } 
        else {
          int matrixIdx = getT9MatrixIndex(trackingActiveKey);
          if (matrixIdx != -1) {
            char numVal = getLongPressNumericValue(trackingActiveKey);
            if (numVal != '\0') {
              if (trackingActiveKey == lastKey && currentMessage.length() > 0) { currentMessage[currentMessage.length() - 1] = numVal; } 
              else { currentMessage += numVal; }
              lastKey = '\0'; forceRefresh = true;
            }
          }
        }
        Preferences cache; cache.begin("recovery", false); cache.putString("saved_reply", currentMessage); cache.end();
      }
    }
  }

  if (lidOpened && kState == RELEASED && trackingActiveKey != '\0') {
    char keyToProcess = trackingActiveKey; trackingActiveKey = '\0'; 
    if (!longPressExecutionFlag) {
      if (currentState == STATE_HOME || currentState == STATE_MESSAGE_VIEW) {
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_TYPING_REPLY; xSemaphoreGive(dataMutex); } 
        currentMessage = ""; lastKeypadActivity = millis(); lastKey = '\0'; forceRefresh = true;
      }
      else if (currentState == STATE_TYPING_REPLY) {
        lastKeypadActivity = millis(); 
        if (keyToProcess == 'A') { 
          display.clearDisplay(); display.setTextSize(1); display.setCursor(0,20); display.print("Sending Reply..."); display.display();
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { globalOutboundReplyPayload = currentMessage; pendingBackgroundDispatchTrigger = true; currentState = STATE_HOME; xSemaphoreGive(dataMutex); }
          Preferences cache; cache.begin("recovery", false); cache.putBool("has_msg", false); cache.putString("saved_reply", ""); cache.end();
          currentMessage = ""; lastKey = '\0'; forceRefresh = true;
        }
        else if (keyToProcess == 'B') { currentMessage += " "; lastKey = '\0'; forceRefresh = true; }
        else if (keyToProcess == 'C') { if (currentMessage.length() > 0) currentMessage.remove(currentMessage.length() - 1); lastKey = '\0'; forceRefresh = true; }
        else { 
          int matrixIdx = getT9MatrixIndex(keyToProcess);
          if (matrixIdx != -1) {
            const char* options = customT9Matrix[matrixIdx];
            if (keyToProcess == lastKey && (millis() - lastKeyPressTime < 800)) { currentMessage[currentMessage.length() - 1] = options[(keyPressCount + 1) % strlen(options)]; keyPressCount++; } 
            else { keyPressCount = 0; currentMessage += options[0]; }
            lastKey = keyToProcess; lastKeyPressTime = millis(); forceRefresh = true;
          }
        }
        Preferences cache; cache.begin("recovery", false); cache.putString("saved_reply", currentMessage); cache.end();
      }
    }
    longPressExecutionFlag = false;
  }

  if (lidOpened && (currentState == STATE_HOME || currentState == STATE_MESSAGE_VIEW)) {
    if (key == '2' && kState == PRESSED) { keyIsPressed = true; longPressTimer = millis(); }
    if (kState == RELEASED && key == '2') { keyIsPressed = false; }
    if (keyIsPressed && (millis() - longPressTimer >= 3000)) { keyIsPressed = false; display.clearDisplay(); display.setCursor(5, 25); display.setTextSize(1); display.print("Hard Reset Triggered!"); display.display(); delay(1500); ESP.restart(); }
  }

  bool currentLidRead = (digitalRead(bumpSwitchPin) == HIGH);
  if (currentLidRead != lastLidState) {
    lastLidState = currentLidRead;
    if (!currentLidRead) { 
      lidOpened = false; 
      Preferences cache; cache.begin("recovery", false); cache.putBool("has_msg", false); cache.putString("msg_txt", ""); cache.end();
      lastDisplayMessage = "Waiting for message..."; 
      display.clearDisplay(); display.display(); display.ssd1306_command(SSD1306_DISPLAYOFF); 
    } 
    else { 
      lidOpened = true; display.ssd1306_command(SSD1306_DISPLAYON);
      if (isServoCurrentlyActive) { isServoCurrentlyActive = false; writeServoAngle(90); }
      if (isLedCurrentlyActive) { isLedCurrentlyActive = false; analogWrite(redPin, 0); analogWrite(greenPin, 0); analogWrite(bluePin, 0); }
      if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_HOME; xSemaphoreGive(dataMutex); }
      forceRefresh = true; 
    }
  }

  if (!lidOpened) { delay(1); return; }
  if (currentState == STATE_PORTAL_OPEN || currentState == STATE_PORTAL_WAITING) { dnsServer.processNextRequest(); server.handleClient(); }
  
  if ((currentState == STATE_HOME || currentState == STATE_MESSAGE_VIEW) && remoteWebUserTyping) { 
    if (millis() - lastBlinkTime > 80) { lastBlinkTime = millis(); blinkAnimationFrame = (blinkAnimationFrame + 1) % 4; forceRefresh = true; } 
  }
  if (currentState == STATE_HOME && !remoteWebUserTyping) { static unsigned long clockRefreshTicker = 0; if (millis() - clockRefreshTicker > 1000) { clockRefreshTicker = millis(); forceRefresh = true; } }

  if (currentState == STATE_MESSAGE_VIEW) {
    if (millis() - lastScrollTickTimestamp >= 3600) { 
      lastScrollTickTimestamp = millis();
      if (totalComputedMessageLines > 8) { currentMessageScrollLineOffset++; if (currentMessageScrollLineOffset > (totalComputedMessageLines - 8)) currentMessageScrollLineOffset = 0; forceRefresh = true; }
    }
  }

  if (currentState == STATE_PORTAL_OPEN) {
    if (millis() - lastPortalScanTimestamp >= 6000) { 
      lastPortalScanTimestamp = millis();
      if (scanSavedWifiPresenceInAir()) {
        WiFi.begin(qsid.c_str(), qpass.c_str()); unsigned long quickCheck = millis(); bool gotConnected = false;
        while (millis() - quickCheck < 3000) { if (isStrictlyConnectedToRouter()) { gotConnected = true; break; } delay(100); }
        if (gotConnected) {
          server.stop(); dnsServer.stop(); WiFi.softAPdisconnect(true);
          if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_PORTAL_SUCCESS; xSemaphoreGive(dataMutex); }
          stateTimer = millis(); forceRefresh = true;
        } else { WiFi.disconnect(); WiFi.mode(WIFI_AP_STA); }
      }
    }
  }

  switch (currentState) {
    case STATE_WELCOME:
      if (forceRefresh) { forceRefresh = false; display.clearDisplay(); display.setTextSize(1); display.setCursor(13, 10); display.print("You're welcome on"); display.setTextSize(2); display.setCursor(4, 32); display.print("SAURANSHWI"); display.setTextSize(1); display.setCursor(4, 54); display.print("LOVEBOX"); display.display(); }
      if (millis() - stateTimer >= 4000) { WiFi.disconnect(true, true); WiFi.mode(WIFI_OFF); delay(100); WiFi.mode(WIFI_STA); if (qsid.length() > 0) WiFi.begin(qsid.c_str(), qpass.c_str()); if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_WIFI_COUNTDOWN; xSemaphoreGive(dataMutex); } countdownSeconds = 15; stateTimer = millis(); forceRefresh = true; }
      break;
    case STATE_WIFI_COUNTDOWN:
      if (millis() - stateTimer >= 1000) { stateTimer = millis(); countdownSeconds--; forceRefresh = true; display.clearDisplay(); display.setTextSize(1); display.setCursor(14, 15); display.print("Syncing Network..."); int progressWidth = map(countdownSeconds, 15, 0, 10, 118); display.drawRect(10, 36, 108, 6, SSD1306_WHITE); display.fillRect(10, 36, progressWidth, 6, SSD1306_WHITE); display.display(); if (countdownSeconds <= 0) { launchCaptivePortal(); if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_PORTAL_OPEN; xSemaphoreGive(dataMutex); } forceRefresh = true; } }
      break;
    case STATE_PORTAL_OPEN:
      if (forceRefresh) { forceRefresh = false; display.clearDisplay(); display.setTextSize(1); display.setCursor(16, 2); display.print("HOTSPOT OPENED---"); display.setCursor(0, 16); display.print("MADAM JI AB APNE\nMOBILE SE WIFI JAAKR\nCONNECT KRIYE..."); display.display(); }
      break;
    case STATE_PORTAL_WAITING:
      if (forceRefresh) { forceRefresh = false; display.clearDisplay(); display.setTextSize(1); display.setCursor(0, 20); display.print("WAIT KREI MADAM JI...\nWIFI CONNECT HO\nRHA HAI..."); display.display(); }
      if (millis() - stateTimer >= 500) { stateTimer = millis(); WiFi.mode(WIFI_STA); WiFi.begin(qsid.c_str(), qpass.c_str()); unsigned long startCheck = millis(); bool success = false; while (millis() - startCheck < 4000) { if (isStrictlyConnectedToRouter()) { success = true; break; } dnsServer.processNextRequest(); server.handleClient(); delay(50); } if (success && isStrictlyConnectedToRouter()) { server.stop(); dnsServer.stop(); WiFi.softAPdisconnect(true); if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_PORTAL_SUCCESS; xSemaphoreGive(dataMutex); } } else { WiFi.disconnect(); WiFi.mode(WIFI_OFF); delay(100); launchCaptivePortal(); if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_PORTAL_OPEN; xSemaphoreGive(dataMutex); } } forceRefresh = true; }
      break;
    case STATE_PORTAL_SUCCESS:
      if (forceRefresh) { forceRefresh = false; display.clearDisplay(); display.setTextSize(1); display.setCursor(4, 10); display.print("CONGRATULATIONS!!!!!"); display.setCursor(0, 26); display.print("LOVEBOX INTERNET SE\nCONNECT HO GYA HAI\nMADAM JI."); display.display(); }
      if (millis() - stateTimer >= 4000) { initFirebaseSilent(); if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_HOME; xSemaphoreGive(dataMutex); } forceRefresh = true; }
      break;
    case STATE_HOME:
      if (newInboundMessageAvailable) { if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { newInboundMessageAvailable = false; lastDisplayMessage = globalFirebaseMessage; currentState = STATE_MESSAGE_VIEW; xSemaphoreGive(dataMutex); messageDisplayStartTimestamp = millis(); currentMessageScrollLineOffset = 0; lastScrollTickTimestamp = millis(); totalComputedMessageLines = renderSmartWordWrapEngine(lastDisplayMessage, 0, 0, false); forceRefresh = true; break; } }
      if (forceRefresh) { forceRefresh = false; display.clearDisplay(); display.setTextSize(2); display.setCursor(18, 12); if (getLocalTimeString() == "") { display.print("Syncing Net Clock..."); } else { display.print(getLocalTimeString().substring(0, 5)); display.setTextSize(1); display.setCursor(82, 12); display.print(getLocalTimeString().substring(5)); } display.setTextSize(1); display.setCursor(2, 52); display.print("B:" + String(currentBatteryPercentage) + "%"); int centerStartPix = 64 - ((partnerLastSeenStr.length() * 6) / 2); display.setCursor(centerStartPix, 52); display.print(partnerLastSeenStr); display.setCursor(102, 52); display.print("WiFi"); if (remoteWebUserTyping) { display.setCursor(4, 36); display.print("the user is typing"); int dotX = 114; int dotY = 42; if (blinkAnimationFrame >= 1) display.fillRect(dotX, dotY, 1, 1, SSD1306_WHITE); if (blinkAnimationFrame >= 2) display.fillRect(dotX + 4, dotY, 1, 1, SSD1306_WHITE); if (blinkAnimationFrame >= 3) display.fillRect(dotX + 8, dotY, 1, 1, SSD1306_WHITE); } display.display(); }
      break;
    case STATE_MESSAGE_VIEW:
      if (newInboundMessageAvailable) { if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { newInboundMessageAvailable = false; lastDisplayMessage = globalFirebaseMessage; xSemaphoreGive(dataMutex); messageDisplayStartTimestamp = millis(); currentMessageScrollLineOffset = 0; lastScrollTickTimestamp = millis(); totalComputedMessageLines = renderSmartWordWrapEngine(lastDisplayMessage, 0, 0, false); forceRefresh = true; } }
      if (remoteWebUserTyping) messageDisplayStartTimestamp = millis();
      if (forceRefresh) { forceRefresh = false; display.clearDisplay(); display.setTextSize(1); printSmartWordWrap(lastDisplayMessage, 0); if (remoteWebUserTyping) { int cornerX = 112; int cornerY = 58; if (blinkAnimationFrame >= 1) display.fillRect(cornerX, cornerY, 2, 2, SSD1306_WHITE); if (blinkAnimationFrame >= 2) display.fillRect(cornerX + 5, cornerY, 2, 2, SSD1306_WHITE); if (blinkAnimationFrame >= 3) display.fillRect(cornerX + 10, cornerY, 2, 2, SSD1306_WHITE); } display.display(); }
      if (millis() - messageDisplayStartTimestamp >= 30000) { Preferences cache; cache.begin("recovery", false); cache.putBool("has_msg", false); cache.end(); if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) { currentState = STATE_HOME; xSemaphoreGive(dataMutex); } forceRefresh = true; }
      break;
    case STATE_TYPING_REPLY:
      if (forceRefresh) { forceRefresh = false; display.clearDisplay(); display.setTextSize(1); display.setCursor(0, 0); display.print("Typing Reply..."); display.drawFastHLine(0, 10, 128, SSD1306_WHITE); int totalTypingLines = renderSmartWordWrapEngine(currentMessage + "_", 15, 0, false); int typingScrollOffset = 0; if (totalTypingLines > 6) typingScrollOffset = totalTypingLines - 6; renderSmartWordWrapEngine(currentMessage + "_", 15, typingScrollOffset, true); display.display(); }
      vTaskDelay(pdMS_TO_TICKS(5)); yield();
      break;
  }
}