
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
#include "marconi_client.h"
#include <FastLED.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
//#include <ESP8266TrueRandom.h>

#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "bsec.h"


// ++++++++++++++++++++ WIFI Management +++++++++++++++

#define TRIGGER_PIN 14
WiFiManager wm;   
WiFiManagerParameter custom_field; 
const char* AP_SSID = "Air-Quality";
  

// ++++++++++++++++++++++ Gauge ++++++++++++++++++++
#define LED_PIN   25
#define NUMPIXELS 13
#define ALLPIXELS 28
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB


CRGB leds[ALLPIXELS];

int animationSpeed = 50;  
int oldScaleValue = 1;     // needed for value change animation of gauge
int gaugeValue = 0;
float dimmLevel = 1.0F;   // value between 0 (off) and 1 (full brightness)

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

#define property_gauge       0x01
#define property_co2         0x02
#define property_temperature 0x03
#define property_humidity    0x04
#define property_dimmlevel   0x05
#define property_pressure    0x06


#define actionID_gaugeValue  0x01
#define actionID_dimmLevel   0x05

// +++++++++++++++++++++++ General +++++++++++++++++++++
unsigned int loopCnt = 0;


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
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                   SETUP
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void setup() {  
  Serial.begin(115200);
  Serial.setDebugOutput(true);   
  Serial.println("\n Starting");
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
  return deviceConfigMemory.begin(0x500);
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    THE LOOP 
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void loop() {  
  unsigned long currTime = millis();
  checkButton(); // check wether trigger button was pressed  
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
  
  loopCnt++;
  
  if (loopCnt > 65535){ // when 16bit max have been reached - do not care about 32bit systems
    loopCnt = 0;
  }
  c->loop();

  if (loopCnt % 500 == 0){
    if (bme680_available){         
       if (!iaqSensor.run()){
          Serial.println("Error reading SEC values");
          evalIaqSensorStatus();
       }           
    }   
  }
        
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
      triggerGaugeDimmLevel();
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

bool sensorsAvailable(){
  return bme280_available || bme680_available;
}

bool initBME680(){
  Serial.print("Initializing BME680 ... ");  
  iaqSensor.begin(BME680_I2C_ADDR_SECONDARY,Wire);  
  
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

bool initBME280(){
  Serial.print("Initializing BME280 ... ");
  bool res = bme280.begin(BME280_ADDRESS_ALTERNATE, &Wire);
  if (res){
    Serial.println("OK");
  } else {
    Serial.print("ERROR - No BME280 found on I2C wiring with address ");
    Serial.println(BME280_ADDRESS_ALTERNATE);
    
  }
  return res;
}

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
  blueRing();
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                       GAUGE
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void setGaugePercentage(int value){
  Serial.print("setting new value: ");
  Serial.println(value);
  gaugeValue = value;
  sendGaugeValue();
  
  if (value == 0){
    clearRing();
    oldScaleValue = 0;
    return;
  }
  int newScaleValue = getScaleValue(value);
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


void blueRing(){
  for (int i=0; i <= ALLPIXELS; i++){
      leds[i] = CRGB::Blue;    
  }
  FastLED.show();  
}

void redRing(){
  for (int i=0; i <= ALLPIXELS; i++){
      leds[i] = CRGB::Red;    
  }
  FastLED.show();  
}

void greenRing(){
  for (int i=0; i <= ALLPIXELS; i++){
      leds[i] = CRGB::Green;    
  }
  FastLED.show();  
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
