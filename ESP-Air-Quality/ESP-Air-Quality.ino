
/*
 * 
 *  ----------------------------------------------------------------------------------------------------------------------
 *                          _   ___ ___    ___  _   _  _   _    ___ _______   __  ___ ___ ___ 
 *                         /_\ |_ _| _ \  / _ \| | | |/_\ | |  |_ _|_   _\ \ / / | __/ __| _ \
 *                        / _ \ | ||   / | (_) | |_| / _ \| |__ | |  | |  \ V /  | _|\__ \  _/
 *                       /_/ \_\___|_|_\  \__\_\\___/_/ \_\____|___| |_|   |_|   |___|___/_|                   IoT connctd 
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
#include <Wire.h>
// additional libraries
#include "marconi_client.h"   // https://github.com/connctd/marconi-lib                   communication with connctd backend
#include <FastLED.h>          // https://github.com/FastLED/FastLED                       LED-ring   
#include <WiFiManager.h>      // https://github.com/tzapu/WiFiManager                     configuration of WiFi settings via Smartphone
#include <Adafruit_Sensor.h>  // https://github.com/adafruit/Adafruit_Sensor              common library for arduino sensors
#include <Adafruit_BME280.h>  // https://github.com/adafruit/Adafruit_BME280_Library      library to work with BME280 sensors
#include "bsec.h"             // https://github.com/BoschSensortec/BSEC-Arduino-library   library that works with BME680 sensors and calculating CO2 equivalent


#define VERSION "0.9.16"  // major.minor.build

// ++++++++++++++++++++ WIFI Management +++++++++++++++

#define TRIGGER_PIN 14
WiFiManager wm;   
WiFiManagerParameter custom_field; 
const char* AP_SSID = "Air-Quality";
  

// ++++++++++++++++++++++ Gauge ++++++++++++++++++++
#define LED_PIN   25
#define NUMPIXELS 12
#define ALLPIXELS 24
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB

CRGB leds[ALLPIXELS];

int animationSpeed = 50;  
int oldScaleValue  = 1;     // needed for value change animation of gauge
int gaugeValue     = 0;
float dimmLevel    = 1.0F;  // value between 0 (off) and 1 (full brightness)

int notCalibratedState = 0;
unsigned long lastCalibrationAnimationStep = 0;

// +++++++++++++++++++++++ Connctd +++++++++++++++++++++

IPAddress ip(35,205,82,53);  // TODO: need to be configured as address, ip could change over time
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

#define property_gauge       0x01
#define property_co2         0x02
#define property_temperature 0x03
#define property_humidity    0x04
#define property_dimmlevel   0x05
#define property_pressure    0x06


#define actionID_gaugeValue  0x01
#define actionID_dimmLevel   0x05

// +++++++++++++++++++++++ General +++++++++++++++++++++



// +++++++++++++++++++++++ Sensoring +++++++++++++++++++

#define SEALEVELPRESSURE_HPA (1013.25)

Adafruit_BME280 bme280; 
Bsec iaqSensor;

bool buttonPressed = false;
unsigned long buttonPressMillis = 0;

float temperature = 0.0;
float humidity = 0.0;
float pressure = 0.0;
float voc = 0.0;
int co2 = 0;


bool bme280_available = false;
bool bme680_available = false;




#define IAQA_NOT_CALIBRATED       0
#define IAQA_UNCERTAIN            1
#define IAQA_CALIBRATING          2
#define IAQA_CALIBRATION_COMPLETE 3

int iaq_accuracy = IAQA_NOT_CALIBRATED;

uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
EEPROMClass  bsecStateMemory("bsecState", BSEC_MAX_STATE_BLOB_SIZE+1);
unsigned long bsecStateUpdateInterval = 5*60*1000; // every 5min
unsigned long lastBsecUpdate=0;

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                   SETUP
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void setup() {  
  initialized = false;
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);   
  Serial.println("\n Starting");
  Serial.print("Air Quality - ESP v");
  Serial.println(VERSION);
  Wire.begin();
  initializeLedRing();    
  initializeRandomSeed();
  
  pinMode(TRIGGER_PIN, INPUT);
  
  if (!initEEProm()){
    Serial.println("ERROR - Failed to initialize EEPROM");
    Serial.println("ESP will be restarted");
    errorRing();
    ESP.restart();
  }

  if (!loadDeviceConfig()){
    Serial.println("ERROR - Device was not flashed with Device ID and KEY!!!");
    while(true){
      errorRing();
    }
  }
     
  initializeWiFi(); 
  
  // ToDo - make non blocking and animate "connecting gauge"
  if (!connectToWiFi()){    
    ESP.restart();
  }

  initMarconi();
  
  bme680_available = initBME680();
  bme280_available = initBME280();   

  if (!sensorsAvailable()){
     noSensorAnimation();
  }  
  clearRing();

}

void initializeRandomSeed(){
    //srand(ESP8266TrueRandom.random());
     srand (analogRead(0));
}

void initializeLedRing(){
   FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, ALLPIXELS).setCorrection( TypicalLEDStrip );
   FastLED.setBrightness(255);  
  clearRing();  
  
}

void initMarconi(){
  Serial.println("initialize Marconi Library");  
  c = new MarconiClient(ip, port, deviceConfig.id, deviceConfig.key, onConnectionStateChange, onDebug, onErr);
}

bool initEEProm(){
  bool res = deviceConfigMemory.begin(0x500);
  res = res && bsecStateMemory.begin(BSEC_MAX_STATE_BLOB_SIZE+1);
  return res;
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    THE LOOP 
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void loop() {  
  unsigned long currTime = millis();
  checkButton(); // check wether trigger button was pressed  
  // init connection to connctd backend when necessary
  if (!initialized) {
      if (currTime - lastInitTry > 10000){
        lastInitTry = currTime;
        initSession();            
      }
  }  

  // if connection to connctd is estblished
  if (initialized){ 
    // resubscribe if necessary
    if (currTime - lastResubscribe > resubscribeInterval || lastResubscribe == 0 ) {
      lastResubscribe = currTime;
      c->subscribeForActions(onAction);
    }    
    // periodically send property updates
    if (currTime - lastPropertyUpdate > propertyUpdateInterval) {      
      lastPropertyUpdate = currTime;
      sendGaugeDimmLevelValue();
      if (sensorsAvailable()) {              
        if (readTemperature()){
          sendTemperatureValue();
        }
        if (readHumidity()){
          sendHumidityValue();
        }
        if (readPressure()){
          sendPressureValue();
        }
        if (readCo2Equivalent()){
          sendCo2Value();
        }
       
      }      
    }
    
  }
  
  // BME680/BSEC stuff
  if (bme680_available){         
     // periodically save BSEC-State
     // TODO, this is for debugging reasons, need to be adjusted later, depending on the outcome of testing phase
     if (currTime - lastBsecUpdate > bsecStateUpdateInterval){
        saveBsecState();
        lastBsecUpdate = currTime;
     }
     // periodicaly trigger iaqSensor
     if (iaqSensor.run()) {
         // in case new data is available
         printIAQdata();
         evalIaqAccuracy();         
     } else {        
        // iaqSensor.run() could be false when no new data available or when an error occurs
        // need to check the status for this.   
        if (!checkIaqSensorStatus()){
           while(true){
             errorGauge();
           }
        }
     }
     // trigger animation when sensor is calibrating
     if (!isIaqCalibrated()){
        if (currTime - lastCalibrationAnimationStep > animationSpeed){        
          triggerNotCalibratedAnimation();
          lastCalibrationAnimationStep = currTime;
        }
     }
     
  }      

  // ok, trigger marconi library 
  c->loop();
  // and wait for 10ms
  delay(10);
}



// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                 System Functions
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


bool loadDeviceConfig(){
   Serial.println("loading device configuration"); 

   deviceConfigMemory.get(0,deviceConfig);      
   Serial.print("Device ID = ");
   Serial.println(deviceConfig.id);    
    Serial.println(" -- id --");
   for (int i = 0; i < DEVICE_ID_SIZE; i++){
      Serial.print( deviceConfig.id[i],HEX);
      Serial.print(" ");
   }
   Serial.println();
   Serial.println(" ---------");     
   Serial.println(" -- key --");
   for (int i = 0; i < CHACHA_KEY_SIZE; i++){
      Serial.print( deviceConfig.key[i],HEX);
      Serial.print(" ");
   }
   Serial.println();
   Serial.println(" ---------");

  return ((deviceConfig.id[0] != 0xFF) && (deviceConfig.id[0] != 0)) ; 
}

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
      resetToFactorySettings();
      ESP.restart();
      return;
    }
    if(pressedMillis >= 10000){
      setRingColor(CRGB(255,0,0));
      return; 
    }
    if (pressedMillis >= 5000){
      setRingColor(CRGB(0,255,0));
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
      triggerGaugeDimmLevel();
      return;
   }
   if ((pressedMillis >= 5000) && (pressedMillis < 10000)){
        startWiFiConfiguration(120);       
   }
   clearRing();
   refreshGauge();
}

void resetToFactorySettings(){
   
   Serial.println("!!!!!!!!!!!!  Performing Factory Reset  !!!!!!!!!!!!!");
   Serial.println("Deleting Wifi Settings");
   wm.resetSettings();   
   eraseBsecState();
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    Sensoring
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

// +++++++++++++++++++++++++++++++ BME680 / BSEC ++++++++++++++++++++++++++++++++

bool initBME680(){
  Serial.print("Initializing BME680 ... ");  
  //iaqSensor.begin(BME680_I2C_ADDR_SECONDARY,Wire);  

  // check for address 0x77 
   Wire.beginTransmission(0x77);
   if(Wire.endTransmission()!=0){ 
      Serial.println("ERROR");
      return false;
   }
  
  iaqSensor.begin(0x77,Wire);  
  if (!checkIaqSensorStatus()){
    Serial.println("ERROR");
    evalIaqSensorStatus();
    return false;
  }
  
  bsec_virtual_sensor_t sensorList[10] = {
    BSEC_OUTPUT_RAW_TEMPERATURE,
    BSEC_OUTPUT_RAW_PRESSURE,
    BSEC_OUTPUT_RAW_HUMIDITY,
    BSEC_OUTPUT_RAW_GAS,
    BSEC_OUTPUT_IAQ,
    BSEC_OUTPUT_STATIC_IAQ,
    BSEC_OUTPUT_CO2_EQUIVALENT,
    BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
    BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
  };

  
  iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);
  
  if (!checkIaqSensorStatus()){
    Serial.println("ERROR");
    evalIaqSensorStatus();
    return false;
  }
  Serial.println("OK");
  String output = "BSEC library version " + String(iaqSensor.version.major) + "." + String(iaqSensor.version.minor) + "." + String(iaqSensor.version.major_bugfix) + "." + String(iaqSensor.version.minor_bugfix);
  Serial.println(output);  

  loadBsecState();
  return true;
}

bool checkBsecState(){
  for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE; i++) {   
      if (bsecState[i]!=0xFF) {
        return true;
      }
  }
  return false;
}

bool sensorsAvailable(){
  return bme280_available || bme680_available;
}

void eraseBsecState(){
  Serial.print("Erasing BSEC state ...");
  for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE + 1; i++) {
      bsecStateMemory.write(i, 0xFF);
      bsecState[i]=0xFF;
  }
  if(bsecStateMemory.commit()){
    Serial.println("OK");
  } else {
    Serial.println("Error");
  }
}

bool loadBsecState(){
  getBsecState();
  Serial.print("Reading BSEC state from EEPROM .... ");
  bsecStateMemory.get(0,bsecState);  
  Serial.println("OK");
  printBsecState();
  Serial.print("Checking BSEC state ............... ");
  if (!checkBsecState()){
    Serial.println("ERROR");
    Serial.println("Not a valid BSEC state, State was propably never saved before and will be ignored");
    return false;
  }
  Serial.println("OK"); 
  Serial.print("Setting BSEC state ................ ");
  iaqSensor.setState(bsecState);  
  Serial.println("OK");
  Serial.println("Reading State from BSEC again");
  getBsecState();
  return true;
}

void getBsecState(){
  Serial.print("BSEC State : ");
  iaqSensor.getState(bsecState);
  printBsecState();
}

void printBsecState(){
  for (int i = 0; i < BSEC_MAX_STATE_BLOB_SIZE+1; i++){
    Serial.print(bsecState[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

bool saveBsecState(){
  getBsecState();
  Serial.print("Writing BSEC state to EEPROM .... ");
  if (!checkIaqSensorStatus()){
    Serial.println("ERROR");
    evalIaqSensorStatus();
    return false;
  }  
  bsecStateMemory.put(0, bsecState);
  if (bsecStateMemory.commit()) {
     Serial.println("OK");
  } else {
     Serial.println("ERROR");    
     Serial.println("BSEC State (calibration data) could not be saved");    
     return false;
  }
  return true;  
}


bool checkIaqSensorStatus(void) {
  return (iaqSensor.status == BSEC_OK);
}

void evalIaqSensorStatus(){ {
    String output;
    if (iaqSensor.status == BSEC_OK) {
      output = "BSEC OK : " + String(iaqSensor.status);            
    } else { 
      if (iaqSensor.status < BSEC_OK) {
        output = "BSEC error code : " + String(iaqSensor.status);            
      } else {
        output = "BSEC warning code : " + String(iaqSensor.status);      
      }
    }
    Serial.println(output); 

    if (iaqSensor.bme680Status != BME680_OK) {
      output = "BME680 error code : " + String(iaqSensor.bme680Status);
    } else {
      if (iaqSensor.bme680Status < BME680_OK) {
        output = "BME680 error code : " + String(iaqSensor.bme680Status);          
      } else {
        output = "BME680 warning code : " + String(iaqSensor.bme680Status);      
      }
    }
    Serial.println(output);      
  }
}


bool isIaqCalibrated(){
  return (iaq_accuracy!=IAQA_NOT_CALIBRATED);
}

void evalIaqAccuracy(){ 
  if (iaq_accuracy == iaqSensor.iaqAccuracy){
    return;
  }
  Serial.println("New IAQ Accuracy detected");
  iaq_accuracy = iaqSensor.iaqAccuracy;
  switch (iaq_accuracy){
        case IAQA_NOT_CALIBRATED:      
             // do nothing
             break;  
        case IAQA_UNCERTAIN:
             // at least sensor is calibrated and delivers CO2 equivalents
             handleIaqCalibrationEvent();
             break;
        case IAQA_CALIBRATING:             
             break;
        case IAQA_CALIBRATION_COMPLETE:
              // fully calibrated, should be saved
             handleIaqCalibrationEvent();
             break;
        default:
             // unknown state   
             // does not need to be handled
             break;
  }
}

void handleIaqCalibrationEvent(){
  saveBsecState();
  successGauge();
  lastPropertyUpdate = 0;
  clearRing();
  refreshGauge();
}

void printIAQdata(){
   String output = "raw temperature [°C], pressure [hPa], raw relative humidity [%], gas [Ohm], IAQ, IAQ accuracy, temperature [°C], relative humidity [%], Static IAQ, CO2 equivalent, breath VOC equivalent";
   Serial.println(output);   
   output = String(iaqSensor.rawTemperature);
   output += ", " + String(iaqSensor.pressure);
   output += ", " + String(iaqSensor.rawHumidity);
   output += ", " + String(iaqSensor.gasResistance);
   output += ", " + String(iaqSensor.iaq);
   output += ", " + String(iaqSensor.iaqAccuracy);
   output += ", " + String(iaqSensor.temperature);
   output += ", " + String(iaqSensor.humidity);
   output += ", " + String(iaqSensor.staticIaq);
   output += ", " + String(iaqSensor.co2Equivalent);
   output += ", " + String(iaqSensor.breathVocEquivalent);
   Serial.println(output);
}

// +++++++++++++++++++++++++++++++++++ BME280 +++++++++++++++++++++++++++++++++++

bool initBME280(){
  Serial.print("Initializing BME280 ... ");
  bool res = bme280.begin(BME280_ADDRESS_ALTERNATE, &Wire);
  if (res){
    Serial.println("OK");
  } else {
    Serial.println("ERROR");
    Serial.print("No BME280 found on I2C wiring with address ");
    Serial.println(BME280_ADDRESS_ALTERNATE);    
  }
  return res;
}


// +++++++++++++++++++++++++++ updating Sensor values ++++++++++++++++++++++++++++

bool readTemperature(){  
  if (!sensorsAvailable()) {
    return false;
  }
  float newTemperature;
  
  if (bme680_available) {
    newTemperature = float(int(iaqSensor.temperature*2.0F))/2.0F;  // use 0.5 steps for temperature  
  } else if (bme280_available){
    newTemperature = float(int(bme280.readTemperature()*2.0F))/2.0F;  // use 0.5 steps for temperature  
  }
  
  if (temperature != newTemperature){
    temperature = newTemperature;
    Serial.print("new temperature value = ");
    Serial.println(temperature);
    return true;
  }
  
  return false;
}

bool readHumidity(){
  if (!sensorsAvailable()) {
    return false;
  }
  float newHumidity = 0.0;
  
  if (bme680_available){
    newHumidity = int(iaqSensor.humidity);
  } else if (bme280_available){
    newHumidity = int(bme280.readHumidity()); // ignore values after . 
  }
  if (humidity != newHumidity){
    humidity = newHumidity;
    Serial.print("new humidity value = ");
    Serial.println(humidity);  
    return true;
  }
  return false;
}

bool readPressure(){
  if (!sensorsAvailable()) {
    return false;
  }
  float newPressure =0.0;
  if (bme680_available){
    newPressure = int(iaqSensor.pressure)/100;
  } else if (bme280_available){
    newPressure = int(bme280.readPressure())/100; 
  }
  
  if (pressure != newPressure) {
    pressure = newPressure;
    Serial.print("new pressure value = ");
    Serial.println(pressure);
    return true;
  }
  
  return false;
}

bool readCo2Equivalent(){
  if (bme680_available){
    if (iaqSensor.iaqAccuracy==0){
      // sensor not calibrated yet
      return false;
    }
    float newCo2 = int(iaqSensor.co2Equivalent);
    
    if (newCo2 != co2){
      co2 = newCo2;  
      Serial.print("Co2 equivalent = ");
      Serial.print(co2);
      Serial.println(" ppm");
      return true;
     }
  }
  return false;
}

// +++++++++++++++++++++++++++++++++++ Button +++++++++++++++++++++++++++++++++++

bool isButtonPressed(){
  return  digitalRead(TRIGGER_PIN) == LOW ;
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    IoT Connctd
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// requestes a session id from connector which will be exchanged in every
// message to prevent replay attacks. Can be called multiple times
void initSession() {  
  if (initialized) {
    return;
  }
  Serial.println("Initializing session");    
  c->init();
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

void sendCo2Value(){
 c->sendFloatPropertyUpdate(property_co2, co2);
}

void sendGaugeDimmLevelValue(){
   c->sendFloatPropertyUpdate(property_dimmlevel, dimmLevel);
}

void sendGaugeValue(){
   c->sendFloatPropertyUpdate(property_gauge, gaugeValue);
}

// called whenever an action is invoked
void onAction(unsigned char actionId, char *value) {
  Serial.printf("Action called. Id: %x Value: %s\n", actionId, value);
  switch (actionId){
    case actionID_gaugeValue:
      setGaugePercentage(String(value).toInt());
      break;
    case actionID_dimmLevel:
      setGaugeDimmLevel(String(value).toFloat()*100);
      break;
     default :
        Serial.println("no matching Action found");
     break;
  }
}

// called whenever marconi lib sends debug data
void onDebug(const char *msg) {
    Serial.printf("[DEBUG] %s\n", msg);
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
        errorRing();
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
        errorRing();
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
  //wm.setSaveParamsCallback(saveParamCallback);
  std::vector<const char *> menu = {"wifi","info","sep","restart","exit"};
  wm.setMenu(menu);  
  wm.setClass("invert");
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(300);
}
 
bool connectToWiFi(){
  bool res;
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
  // nothing to save
}

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("[CALLBACK] configModeCallback fired");
  setRingColor(CRGB(0,0,255));
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                       GAUGE
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void setGaugePercentage(int value){
  Serial.print("setting new value: ");
  Serial.println(value);
  gaugeValue = value;
  sendGaugeValue();

  int newScaleValue;
  // at least one led should always be display for indication of proper connection and functionality
  if (value == 0){ 
    newScaleValue = 1;
  } else newScaleValue = getScaleValue(value);
  
  animateGauge(oldScaleValue, newScaleValue);
  oldScaleValue = newScaleValue;  
}

void setGaugeDimmLevel(int value){  
  Serial.print("Setting dimmlevel to ");
  Serial.print(value);
  Serial.println("%");  
  dimmLevel = float(value)/100.F;
  FastLED.setBrightness(dimmLevel * 255);
  refreshGauge();
  sendGaugeDimmLevelValue();
  
}

void triggerGaugeDimmLevel(){  
    dimmLevel -= 0.20F;
    if (dimmLevel > 0.0F && dimmLevel <= 0.15F){
      dimmLevel = 0.05F;
    }
    if (dimmLevel <= 0.0F){
      dimmLevel = 1.0F;
    }
    Serial.print("Setting gaouge dimm level to ");
    Serial.println(dimmLevel);
    FastLED.setBrightness(dimmLevel *255);
    refreshGauge();
    sendGaugeDimmLevelValue();
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

void triggerNotCalibratedAnimation(){ 
  clearRing();
  notCalibratedState++;
  if (notCalibratedState > NUMPIXELS) {
    notCalibratedState = -1 * (NUMPIXELS-1);
  }
  if (notCalibratedState > 0) {
    leds[notCalibratedState] = CRGB(255,255,255);
  } else {
    leds[(-1)*notCalibratedState] = CRGB(255,255,255);
  }
  FastLED.show();  
}

void noSensorAnimation(){
   setGaugeColor(CRGB(255,255,0));   
    delay(500);
    clearRing();
    delay(500);
    setGaugeColor(CRGB(255,255,0));   
    delay(2000);
    clearRing();
}

void animateGauge(int startPixel, int stopPixel){
  if (startPixel <= stopPixel) {  
    for (int i=0; i < stopPixel; i++){
      leds[i] = CRGB( (255/NUMPIXELS)*i,(150-(150/NUMPIXELS)*i),0); 
      FastLED.show();
      delay(animationSpeed); 
    }  
  } else {    
    for (int i=startPixel; i >= stopPixel; i--){   
      leds[i] = CRGB::Black;    
      FastLED.show(); 
      delay(animationSpeed); 
    }    
  }  
}

void clearRing(){
   for (int i=0; i <= ALLPIXELS; i++){
      leds[i] = CRGB::Black;    
  }
  FastLED.show();  
}

void setRingColor(const CRGB color){
    for (int i=0; i <= ALLPIXELS; i++){
      leds[i] = color;    
  }
  FastLED.show();  
}

void setGaugeColor(const CRGB color){
  for (int i=0; i <= NUMPIXELS; i++){
      leds[i] = color;    
  }
  FastLED.show();  
}


void errorRing(){
  for (int i = 0; i<3; i++){    
    delay(150);
    setRingColor(CRGB(255,0,0));
    delay(150);
    clearRing();
  }  
}

void errorGauge(){
  for (int i = 0; i<3; i++){    
    delay(150);
    setGaugeColor(CRGB(255,0,0));
    delay(150);
    clearRing();
  }
}

void successRing(){
  for (int i = 0; i< 3; i++){
      delay(150);    
      setRingColor(CRGB(0,255,0));
      delay(150);
      clearRing();
    }
}

void successGauge(){
  for (int i = 0; i< 3; i++){
      delay(150);    
      setGaugeColor(CRGB(0,255,0));
      delay(150);
      clearRing();
    }
}
