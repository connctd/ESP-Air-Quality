
/*
 * 
 *  ----------------------------------------------------------------------------------------------------------------------
 *                          _   ___ ___    ___  _   _  _   _    ___ _______   __  ___ ___ ___ 
 *                         /_\ |_ _| _ \  / _ \| | | |/_\ | |  |_ _|_   _\ \ / / | __/ __| _ \
 *                        / _ \ | ||   / | (_) | |_| / _ \| |__ | |  | |  \ V /  | _|\__ \  _/
 *                       /_/ \_\___|_|_\  \__\_\\___/_/ \_\____|___| |_|   |_|   |___|___/_|  
 *  ----------------------------------------------------------------------------------------------------------------------                          
 *  
 * 
 *           FileStructure ┐
 *                         ├ Variable Declaration ┐
 *                         |                      ├ WiFi Management 
 *                         |                      ├ Gauge
 *                         |                      ├ Connctd / Coap
 *                         |                      ├ General
 *                         |                      └ Sensoring
 *                         ├ Setup
 *                         ├ The Loop
 *                         ├ System Functions
 *                         ├ Sensoring
 *                         ├ Connctd
 *                         ├ WiFi Management 
 *                         └ Gauge / LED-Ring
 * 
 * 
 */


#include <EEPROM.h>
//#include <WiFi.h>
#include "marconi_client.h"
#include <Adafruit_NeoPixel.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
//#include <ESP8266TrueRandom.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>

// ++++++++++++++++++++ WIFI Management +++++++++++++++

#define TRIGGER_PIN 6
WiFiManager wm; 
WiFiManagerParameter custom_field; 
const char* AP_SSID = "Air-Quality";
  

// ++++++++++++++++++++++ Gauge ++++++++++++++++++++
#define LED_PIN   4
#define NUMPIXELS 13
#define ALLPIXELS 28


Adafruit_NeoPixel pixels = Adafruit_NeoPixel(ALLPIXELS,LED_PIN, NEO_GRB + NEO_KHZ800);

int animationSpeed = 50;  
int oldScaleValue = 1;     // needed for value change animation of gauge
int gaugeValue = 0;
float brightness = 1.0F;   // value between 0 (off) and 1 (full brightness)

// +++++++++++++++++++++++ Connctd +++++++++++++++++++++

IPAddress ip(35,205,82,53);
int port = 5683;

struct DeviceConfig {    
    char id[DEVICE_ID_SIZE];    
    unsigned char key[CHACHA_KEY_SIZE];
};


DeviceConfig deviceConfig;
EEPROMClass  deviceConfigMemory("devConfig", 128);


MarconiClient *c;
bool initialized = false;
unsigned long resubscribeInterval = 60000; // in ms
unsigned long propertyUpdateInterval = 30000; // in ms
unsigned long lastResubscribe = 0; // periodically resubscribe
unsigned long lastInitTry = 0;
unsigned long lastPropertyUpdate = 0; // time when property updates were sent

byte property_gauge = 0x01;
byte property_co2 = 0x02;
byte property_temperature = 0x03;
byte property_humidity = 0x04;
byte property_brightnes = 0x05;
byte property_pressure = 0x06;

// +++++++++++++++++++++++ General +++++++++++++++++++++
unsigned int loopCnt = 0;


// +++++++++++++++++++++++ Sensoring +++++++++++++++++++

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme; 

bool buttonPressed = false;
unsigned long buttonPressMillis = 0;

float temperature = 0.0;
float humidity = 0.0;
float pressure = 0.0;

bool sensorsAvailable = false;
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                   SETUP
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void setup() {
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);   
  Serial.println("\n Starting");
  pinMode(TRIGGER_PIN, INPUT);

  if (!deviceConfigMemory.begin(0x500)){
    Serial.println("ERROR - Failed to initialize EEPROM");
    Serial.println("ESP will be restarted");
    ESP.restart();
  }

  loadDeviceConfig();  
 
  initializeRandomSeed();
  initializeLedRing();   
  initializeWiFi(); 
  
  // ToDo - make non blocking and animate "connecting gauge"
  if (!connectToWiFi()){    
    ESP.restart();
  }

  initMarconi();
  clearRing();
  
  sensorsAvailable = initBME280();    
}

void initializeRandomSeed(){
    //srand(ESP8266TrueRandom.random());
     srand (analogRead(0));
}

void initializeLedRing(){
  pixels.begin(); 
  clearRing();  
}

void initMarconi(){
  Serial.println("initialize Marconi Library");
  delay(2000);
  c = new MarconiClient(ip, port, deviceConfig.id, deviceConfig.key, onConnectionStateChange, onDebug, onErr);
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    THE LOOP 
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void loop() {  
  unsigned long currTime = millis();
  //checkButton(); // check wether trigger button was pressed  
  if (!initialized) {
      if (currTime - lastInitTry > 10000){
        lastInitTry = currTime;
        blockingInitSession();    
        
      }
  }
  
  
  if (initialized){ 
    // resubscribe if necessary
    if (currTime - lastResubscribe > resubscribeInterval || lastResubscribe == 0 ) {
      lastResubscribe = currTime;
      c->subscribeForActions(onAction);
    }
    // periodically send property updates
    if (currTime - lastPropertyUpdate > propertyUpdateInterval) {
      lastPropertyUpdate = currTime;
      sendGaugeBrightnessValue();
      if (sensorsAvailable) {
        if (readTemperature()){
          sendTemperatureValue();
        }
        if (readHumidity()){
          sendHumidityValue();
        }
        if (readPressure()){
          sendPressureValue();
        }
      }      
    }
  }
  
  loopCnt++;

  
  if (loopCnt > 65535){ // when 16bit max have been reached - do not care about 32bit systems
    loopCnt = 0;
  }
  c->loop();
  
  delay(10);

}



// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                 System Functions
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


bool loadDeviceConfig(){
   Serial.println("loading device configuration"); 
   Serial.println(DEVICE_ID_SIZE);
   Serial.println(CHACHA_KEY_SIZE);
   deviceConfigMemory.get(0,deviceConfig);      
   Serial.print("Device ID = ");
   Serial.println(deviceConfig.id);         
   Serial.println(sizeof(deviceConfig.id));
   Serial.println(sizeof(deviceConfig.key));
   Serial.println(" -- key --");
   for (int i = 0; i < CHACHA_KEY_SIZE; i++){
      Serial.print( deviceConfig.key[i],HEX);
      Serial.print(" ");
   }
   Serial.println();
   Serial.println(" ---------");
}

/*
void saveDeviceConfig(){
  Serial.print("save device configuration ... ");
  deviceConfigMemory.put(0, deviceConfig);
  if (EEPROM.commit()) {
     Serial.println("OK");
  } else {
     Serial.println("EEPROM error - Device Id and Device Code could not be saved");
     errorRing();
  }
}
*/

void checkButton(){  
  
  if(isButtonPressed()){     
      if (!buttonPressed){ 
        onButtonPressed();
      } 
  }    

  if (buttonPressed) {
    if (!isButtonPressed()){
        onButtonReleased();
        return;
    }    
    long pressedMillis = millis() - buttonPressMillis;
    if(pressedMillis >= 15000){
      errorRing();
      resetConfiguration();
      ESP.restart();
      return;
    }
    if(pressedMillis >= 10000){
      redRing();
      return; 
    }
    if (pressedMillis >= 5000){
      greenRing();
    }
  }
}


void onButtonPressed(){
   Serial.println("Button pressed");
   buttonPressed = true;
   buttonPressMillis = millis();         
}

void onButtonReleased(){
   long pressedMillis = millis() - buttonPressMillis;
 
   buttonPressed = false;
  
   Serial.print("Button released (");
   Serial.print(pressedMillis);
   Serial.println("ms)");  

   if (pressedMillis <= 1500) {   
      triggerGaugeBrightness();
      return;
   }
   if ((pressedMillis >= 5000) && (pressedMillis < 10000)){
        startWiFiConfiguration(120);       
   }
   clearRing();
   refreshGauge();
}

void resetConfiguration(){
   Serial.println("Resetting Configuration");
   wm.resetSettings();
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    Sensoring
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool initBME280(){
  Serial.print("Initializing BME280 ... ");
  bool res = bme.begin(BME280_ADDRESS_ALTERNATE, &Wire);
  if (res){
    Serial.println("OK");
  } else {
    Serial.println("ERROR - No BME280 found on I2C wiring");
    
  }
  return res;
}

bool readTemperature(){  
  float newTemperature = float(int(bme.readTemperature()*2.0F))/2.0F;  // use 0.5 steps for temperature  
  bool res = (temperature != newTemperature);
  temperature = newTemperature;
  return res;
}

bool readHumidity(){
  float newHumidity = int(bme.readHumidity()); // ignore values after . 
  bool res (humidity != newHumidity);
  humidity = newHumidity;
  return res;
}

bool readPressure(){
  float newPressure = int(bme.readPressure())/100; 
  bool res = (pressure != newPressure);
  pressure = newPressure;
  return res;
}

bool isButtonPressed(){
  return  digitalRead(TRIGGER_PIN) == LOW ;
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    IoT Connctd
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// requestes a session id from connector which will be exchanged in every
// message to prevent replay attacks. Can be called multiple times
void blockingInitSession() {
  initialized = false;
  Serial.println("Initializing session");
  
  int retries = 0;
  c->init();
  while (!initialized) {
    if (retries > 10) {
      Serial.println("\nSession can not be established. Resending init");
      retries = 0;
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("No wifi connection. Abort session init");
        return;
      }
      c->init();
    }

    Serial.print(".");
    retries += 1;
    c->loop();
    delay(200);
  }

  Serial.println("");
}


void sendHumidityValue(){
  c->sendFloatPropertyUpdate(property_humidity, humidity);
}

void sendTemperatureValue(){
   c->sendFloatPropertyUpdate(property_temperature, temperature);
}

void sendPressureValue(){
 c->sendFloatPropertyUpdate(property_pressure, pressure);
}

void sendGaugeBrightnessValue(){
   c->sendFloatPropertyUpdate(property_brightnes, brightness);
}

void sendGaugeValue(){
   //c->sendIntPropertyUpdate("gauge", gaugeValue);
}

// called whenever an action is invoked
void onAction(unsigned char actionId, char *value) {
  Serial.printf("Action called. Id: %x Value: %s\n", actionId, value);
}

// called whenever marconi lib sends debug data
void onDebug(const char *msg) {
    //Serial.printf("[DEBUG] %s\n", msg);
} 

// called whenever connection state changes
void onConnectionStateChange(const unsigned char state) {
    Serial.printf("[CON] ");
    switch (state) {
      case kConnectionStateInitialized:
        Serial.println("Session was initialized");
        initialized = true;
        break;
      case kConnectionStateUninitialized:
        Serial.println("Session initialization ongoing");
        initialized = false;
        break;
      case kConnectionStateInitRejected:
        Serial.println("Session initialization has failed");
        initialized = false;
        break;
      case kConnectionObservationRequested:
        Serial.println("Observation was requested");
        break;
      case kConnectionObservationOngoing:
        Serial.println("Observation is now ongoing");
        break;
      case kConnectionObservationRejected:
        Serial.println("Observation was rejected");

        // reinit session in case connector was restarted
        initialized = false;

        // after reinit we want to resubscribe
        lastResubscribe = 0;
        break;
      default:
        Serial.printf("Unknown connection event %x\n", state);
        break;
    }
}

// called whenever an error occurs in marconi lib
void onErr(const unsigned char error) {
    Serial.printf("[ERROR] ");
    switch (error) {
        case kErrorInvalidPlaintextSize:
            Serial.println("Plaintext size too small");
            break;
        case kErrorInvalidCipherstreamSize:
            Serial.println("Encryption failed. Cipherstream too small");
            break;
        case kErrorActionRequestRejected:
            Serial.println("Received action request was rejected");
            break;
        case kErrorDecryptionFailed:
            Serial.println("Decryption error");
            break;
        case kErrorEncryptionFailed:
            Serial.println("Encryption error");
            break;
        default:
            Serial.printf("Unknown error event %x\n", error);
            break;
    }
}
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    WiFi Manager
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


void initializeWiFi(){ 
  int customFieldLength = 40;  
  const char* custom_radio_str = "<br/><br/>Please enter your postal code <br/> <input type='text' name='postcode' id='postcode'/>";  
  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input  
  wm.addParameter(&custom_field);
  wm.setSaveParamsCallback(saveParamCallback);
  std::vector<const char *> menu = {"wifi","info","sep","restart","exit"};
  wm.setMenu(menu);  
  wm.setClass("invert");
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(300);
}
 
bool connectToWiFi(){
  bool res;

  // connect to alread configured WiFi or start AP  
  res = wm.autoConnect(AP_SSID,NULL); 

  if(!res) {
    Serial.println("Failed to connect or hit timeout");
    errorRing();  
  } else {
    //if you get here you have connected to the WiFi    
    Serial.print("connected to ");
    Serial.println(wm.getWiFiSSID());
    successRing();
  }
  return res;
}

void startWiFiConfiguration(int timeout){
  Serial.print("Starting config portal (");
  Serial.print(timeout);
  Serial.println("s)");
  wm.setConfigPortalTimeout(timeout);     
  if (!wm.startConfigPortal(AP_SSID,NULL)) {
     Serial.println("ERROR - failed to connect or hit timeout");          
      errorRing();                 
  }      
}


String getParam(String name){  
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback(){
  Serial.println("[CALLBACK] saveParamCallback fired");
 
/*  String deviceId = getParam("deviceId");
  deviceId.toCharArray(deviceConfig.id,9);
  deviceConfig.id[8] = '\0';
  String deviceCode = getParam("deviceCode");
  deviceCode.toCharArray(deviceConfig.code,5);
  deviceConfig.code[4] = '\0';

  Serial.println("deviceId   = " + String(deviceConfig.id));
  Serial.println("deviceCode = " + String(deviceConfig.code));*/
  //saveDeviceConfig();
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("[CALLBACK] configModeCallback fired");
  blueRing();
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                       GAUGE
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void setGaugePercentage(int value){
  Serial.print("setting new value: ");
  Serial.println(value);
  gaugeValue = value;
  //sendGaugeValue();
  
  if (value == 0){
    clearRing();
    oldScaleValue = 0;
    return;
  }
  int newScaleValue = getScaleValue(value);
  animateGauge(oldScaleValue, newScaleValue);
  oldScaleValue = newScaleValue;
  
}

void setGaugeBrightness(int value){  
  Serial.print("Setting brightness to ");
  Serial.print(value);
  Serial.println("%");  
  brightness = float(value)/100.F;
  refreshGauge();
  sendGaugeBrightnessValue();
  
}

void triggerGaugeBrightness(){  
    brightness -= 0.20F;
    if (brightness > 0.0F && brightness <= 0.15F){
      brightness = 0.05F;
    }
    if (brightness <= 0.0F){
      brightness = 1.0F;
    }
    refreshGauge();
    sendGaugeBrightnessValue();
}

void refreshGauge(){
  int as_mem = animationSpeed;  // remember animation speed, animation will be disabled temporarily
  animationSpeed = 0;
  animateGauge(0,oldScaleValue);
  animationSpeed = as_mem;
}

int getScaleValue(int value){
  int pixels = (NUMPIXELS * value)/100;
  
  if (pixels > NUMPIXELS) {
    return NUMPIXELS;    
  }
  if (pixels <= 0){
    return 1;
  }  
  return pixels;  
}


void animateGauge(int startPixel, int stopPixel){
  if (startPixel <= stopPixel) {  
    for (int i=0; i < stopPixel; i++){
      pixels.setPixelColor(i, pixels.Color(((255/NUMPIXELS)*i)*brightness,(150-(150/NUMPIXELS)*i)*brightness,0)); 
      pixels.show(); 
      delay(animationSpeed); 
    }  
  } else {    
    for (int i=startPixel; i >= stopPixel; i--){   
      pixels.setPixelColor(i, pixels.Color(0,0,0)); 
      pixels.show(); 
      delay(animationSpeed); 
    }    
  }  
}

void clearRing(){
  for (int i=0; i <= ALLPIXELS; i++){
     pixels.setPixelColor(i, pixels.Color(0,0,0)); 
  }
  pixels.show();
}


void blueRing(){
  for (int i=0; i <= ALLPIXELS; i++){
     pixels.setPixelColor(i, pixels.Color(0,0,255)); 
  }
  pixels.show();
}

void redRing(){
  for (int i=0; i <= ALLPIXELS; i++){
     pixels.setPixelColor(i, pixels.Color(255,0,0)); 
  }
  pixels.show();
}

void greenRing(){
  for (int i=0; i <= ALLPIXELS; i++){
     pixels.setPixelColor(i, pixels.Color(0,255,0)); 
  }
  pixels.show();
}

void errorRing(){
  for (int i = 0; i<3; i++){    
    delay(150);
    redRing();
    delay(150);
    clearRing();
  }  
}

void successRing(){
  for (int i = 0; i< 3; i++){
      delay(150);    
      greenRing();
      delay(150);
      clearRing();
    }
}
