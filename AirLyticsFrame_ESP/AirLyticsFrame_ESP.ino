
/*
 * 
 *  ----------------------------------------------------------------------------------------------------------------------
 *          
 *                 _      _           _   _            ______                                   ______  _____ _____  
 *           /\   (_)    | |         | | (_)          |  ____|                                 |  ____|/ ____|  __ \ 
 *          /  \   _ _ __| |    _   _| |_ _  ___ ___  | |__ _ __ __ _ _ __ ___   ___   ______  | |__  | (___ | |__) |
 *         / /\ \ | | '__| |   | | | | __| |/ __/ __| |  __| '__/ _` | '_ ` _ \ / _ \ |______| |  __|  \___ \|  ___/ 
 *        / ____ \| | |  | |___| |_| | |_| | (__\__ \ | |  | | | (_| | | | | | |  __/          | |____ ____) | |     
 *       /_/    \_\_|_|  |______\__, |\__|_|\___|___/ |_|  |_|  \__,_|_| |_| |_|\___|          |______|_____/|_|     
 *                               __/ |                                                                               
 *                              |___/                                                                          IoT connctd 
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
 *                         └ Gauge / LED-Ring / Warning LED 
 * 
 * 
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
#include <Adafruit_SCD30.h>   // https://github.com/adafruit/Adafruit_SCD30               library for SCD30 CO2 sensor
#include <sps30.h>
#include "bsec.h"             // https://github.com/BoschSensortec/BSEC-Arduino-library   library that works with a BME680 sensors and calculating CO2 equivalent


#define VERSION "1.0.59"  // major.minor.build   build will increase continously and never reset to 0, independend from major and minor numbers

// ++++++++++++++++++++ WIFI Management +++++++++++++++

WiFiManager wm;   
WiFiManagerParameter custom_field; 
const char* AP_SSID = "AirLytics Frame";
// ++++++++++++++++++++++ Gauge ++++++++++++++++++++
#define LED_PIN   25
#define NUMPIXELS 13
#define ALLPIXELS 24
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB

CRGB leds[ALLPIXELS];

int   animationSpeed = 50;  
int   oldScaleValue  = 1;     // needed for value change animation of gauge
int   gaugeValue     = -1;    // how much percent of gauge should light up (from left to right); -1 means no value set from backend
float dimmLevel      = 1.0F;  // value between 0 (off) and 1 (full brightness)

int notCalibratedAnimationState            = 0;
unsigned long lastCalibrationAnimationStep = 0;

// +++++++++++++++++++++ CONNCTD Communication +++++++++++++++++++++

#define DEVICE_CONFIG_MEMORY_SIZE 0xFF

#define property_gauge         0x01
#define property_co2           0x02
#define property_temperature   0x03
#define property_humidity      0x04
#define property_dimmlevel     0x05
#define property_pressure      0x06
#define property_indicator     0x07
#define property_particle_1pm  0x08
#define property_particle_2pm5 0x09
#define property_particle_4pm  0x10
#define property_particle_10pm 0x11
#define property_particle_1nc  0x12
#define property_particle_2nc5 0x13
#define property_particle_4nc  0x14
#define property_particle_10nc 0x15


#define actionID_gaugeValue  0x01
#define actionID_dimmLevel   0x05
#define actionID_indicator   0x07

const char* marconiUrl = "marconi-udp.connectors.connctd.io";
IPAddress marconiIp;
int port = 5683;

MarconiClient *marconiClient;
bool marconiSessionInitialized          = false;
bool marconiClientInitialized           = false;
unsigned long resubscribeInterval       = 60000; // in ms
unsigned long propertyUpdateInterval    = 30000; // in ms
unsigned long lastResubscribe           = 0; // periodically resubscribe
unsigned long lastInitTry               = 0;
unsigned long lastPropertyUpdate        = 0; // time when property updates were sent
unsigned long lastMarconiClientInitTry  = 0;
unsigned long lastObservationOngoingEventReceived = 0;
unsigned long lastActionReceived        = 0;
unsigned long observationTimeout        = 1000*60*5; // max time with no observation ongoing -> will not receive any actions from backend
unsigned long intervalMarconiClientInit = 20000;
int marconiInitTryCnt                   = 0;
unsigned long actionReceiveTimeout      = 1000 * 60 * 10; // 10 min


struct DeviceConfig {    
    char id[DEVICE_ID_SIZE];    
    unsigned char key[CHACHA_KEY_SIZE];
};
DeviceConfig deviceConfig;
EEPROMClass  deviceConfigMemory("devConfig", DEVICE_CONFIG_MEMORY_SIZE);
// +++++++++++++++++++++++ General +++++++++++++++++++++

#define TRIGGER_PIN 14
#define WARNING_PIN 13

#define ERR_GENERAL            0x00
#define ERR_NO_WIFI            0x01
#define ERR_NO_MARCONI_CLIENT  0x02
#define ERR_MARCONI_SESSION    0x03
#define ERR_EEPROM             0x04
#define ERR_NOT_FLASHED        0x05
#define ERR_REQUESTED_RESTART  0x06
#define ERR_BME680             0xF1
#define ERR_BSEC               0xF2
#define ERR_BME280             0xF3

#define WATCHDOG_TIMER         40 // 40 * 15s = 10min (600s)

bool warningLedOn = false;

int watchdogInterval            = 15000; // 15s 
unsigned long lastWatchdogCheck = 0;
int watchDogCounter             = WATCHDOG_TIMER; // will be decreased every watchdog Interval when bad behavior was detected
                                                  // system restarts when watchDogCounter reaches 0  
                                                  // will be set to WATCHDOG_TIMER when all conditions are fulfilled         

// +++++++++++++++++++++++ Sensoring +++++++++++++++++++
#define IAQA_NOT_CALIBRATED       0
#define IAQA_UNCERTAIN            1
#define IAQA_CALIBRATING          2
#define IAQA_CALIBRATION_COMPLETE 3

#define SEALEVELPRESSURE_HPA (1013.25)
#define TEMPERATURE_OFFSET -4.2

bool buttonPressed = false;
unsigned long buttonPressMillis = 0;

float temperature = 0.0;
float humidity    = 0.0;
float pressure    = 0.0;
int co2           = 0;

bool bme280_available = false;
bool bme680_available = false;
bool scd30_available  = false;
bool sps30_available = false;

Adafruit_SCD30  scd30;
Adafruit_BME280 bme280; 
Bsec iaqSensor;
int iaq_accuracy = IAQA_NOT_CALIBRATED;

EEPROMClass  bsecStateMemory("bsecState", BSEC_MAX_STATE_BLOB_SIZE+1);  // status of BSEC library coul be stored in EEPROM when SCD30 was calibrated
uint8_t bsecState[BSEC_MAX_STATE_BLOB_SIZE] = {0};
bool periodicallyBsecSave             = false; // periodically save the bsec state to EEPROM? will be addionally saved whenever bsec_accuracy switches to 1 or 3; 
unsigned long bsecStateUpdateInterval = 60*60*1000; // every 60min
unsigned long lastBsecUpdate          = 0;
unsigned long lastValueChange         = 0;
unsigned long valueChangeTimeout      = 1000*60*10; // 10 min

struct sps30_measurement m;

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                   SETUP
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void setup() {  
  marconiSessionInitialized = false;
  marconiClientInitialized = false;
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);   
  Serial.println("\n Starting");
  Serial.print("AirLytics - ESP v");
  Serial.println(VERSION);
  
  Wire.begin();
  
  initializeLedRing();   
  initializeRandomSeed();
  initWarningLed();
  initTriggerButton();
 
  if (!initEEProm()){
    Serial.println("ERROR - Failed to initialize EEPROM");
    Serial.println("ESP will be restarted");
    errorRing(ERR_EEPROM);
    ESP.restart();
  }
  
  if (!loadDeviceConfig()){
    Serial.println("ERROR - Device was not flashed with Device ID and KEY!!!");
    while(true){
      errorRing(ERR_NOT_FLASHED);
    }
  }
     
  initializeWiFi(); 

  if (!connectToWiFi()){   
    Serial.println("ERROR - Unable to connect to WiFi");
    Serial.println("System will restart now"); 
    errorRing(ERR_NO_WIFI);
    ESP.restart();
  }
 
  marconiClientInitialized = initMarconi();

  // try to 
  initSensors();
 
  refreshGauge();  
}

void initSensors(){
  sps30_available = initSPS30();
  delay(1000);
  bme680_available = initBME680();
  delay(1000);
  bme280_available = initBME280();   
  delay(1000);
  scd30_available  = initSCD30();
  delay(1000);
  // show status about sensors that have been found  
  sensorInfo();
}

void initWarningLed(){
  pinMode(WARNING_PIN, OUTPUT);
  digitalWrite(WARNING_PIN,false);
}


void initTriggerButton(){
    pinMode(TRIGGER_PIN, INPUT);
}

void initializeRandomSeed(){
    //srand(ESP8266TrueRandom.random()); ‚
     srand (analogRead(0));  // this one will not work on ESP8266!!
}

void initializeLedRing(){
   FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, ALLPIXELS).setCorrection( TypicalLEDStrip );
   FastLED.setBrightness(255);  
  clearRing();    
}

bool initMarconi(){
  Serial.println("initialize Marconi Library"); 
  marconiInitTryCnt++;
  lastMarconiClientInitTry = millis();
  if (!resolveMarconiIp()){
    Serial.println("ERROR, unable to initialize Marconi library");
    return false;
  }
  marconiClient = new MarconiClient(marconiIp, port, deviceConfig.id, deviceConfig.key, onConnectionStateChange, onDebug, onErr);
  marconiInitTryCnt = 0;
  return true;
}

bool initEEProm(){
  bool res = deviceConfigMemory.begin(DEVICE_CONFIG_MEMORY_SIZE);
  res = res && bsecStateMemory.begin(BSEC_MAX_STATE_BLOB_SIZE+1);
  return res;
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    THE LOOP 
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void loop() {    
  checkButton();        // check wether trigger button was pressed    
  watchdog(millis());   // trigger the watchdog routine
  
  if (bme680_available) {
    // do not do other stuff when calibrating -> high probability for calibration failure
    if (iaq_accuracy != IAQA_NOT_CALIBRATED) {        
      doMarconiStuff(millis());  // communication stuff with connctd library
    } 
  }  else {
      doMarconiStuff(millis());  // communication stuff with connctd library
  }
  
  doSensorStuff(millis());  // read sensor values
  
  // and wait for 10ms
  delay(10);
}

void watchdog(unsigned long currTime){

  if (gaugeValue == -255){    
    Serial.println("");
    Serial.println("======= WATCHDOG =======");
    Serial.println("    Requested Restart");    
    Serial.println("System will restart now!");
    Serial.println("========================");
    Serial.println("");
    Serial.println("");
    errorRing(ERR_REQUESTED_RESTART);
    ESP.restart();
    return;
  }

  // when there is nothing to check, return
  if (currTime - lastWatchdogCheck < watchdogInterval){
    return;
  } 

  // time to check conditions again
  lastWatchdogCheck = currTime;
  // when watchdog is active and time is up, restart everything
  if (watchDogCounter <= 0){
    Serial.println("");
    Serial.println("======= WATCHDOG =======");
    printWatchdogStatus();
    Serial.println("------------------------");
    Serial.println("System will reboot now!");    
    Serial.println("========================");
    Serial.println("");
    Serial.println("");
    ESP.restart();
  }

  // check all conditions. either start or stop the watchdog
  if ((isActionTimeout()) || (isValueChangeTimeout()) || (gaugeValue < 0) || (!marconiSessionInitialized) || (!marconiClientInitialized) || (isObservationTimeout())){
    // watchdog activation? or was it activated already?
      if (watchDogCounter == WATCHDOG_TIMER){
        // watchdog activation, print current status
        Serial.println("============ WATCHDOG ===========");
        printWatchdogStatus();
        Serial.println("================================");
      }
      watchDogCounter--;
  } else {
    if (watchDogCounter <  WATCHDOG_TIMER){
      Serial.println("============ WATCHDOG ===========");
      Serial.println("Watchdog Countdown Reset - everything seems to be fine");
      Serial.println("================================");
    }
    watchDogCounter = WATCHDOG_TIMER;
  }
  
}

void printWatchdogStatus(){
  Serial.println("Watchdog active, countown is running");
  Serial.print("ValueChangeTimeout        : ");
  Serial.println(isValueChangeTimeout());
  Serial.print("GaugeValue                : ");
  Serial.println(gaugeValue);
  Serial.print("marconiClientInitialized  : ");
  Serial.println(marconiClientInitialized);
  Serial.print("marconiSessionInitialized : ");
  Serial.println(marconiSessionInitialized);
  Serial.print("observation timeout       : ");
  Serial.println(isObservationTimeout());
  Serial.print("action timeout            : ");
  Serial.println(isActionTimeout());
}

// Do everything that is needed for the communication with the connctd backend
void doMarconiStuff(unsigned long currTime){
  
   // check if client is initialized
   if (!marconiClientInitialized){     
      if (currTime - lastMarconiClientInitTry > intervalMarconiClientInit){
        if (!initMarconi() && (marconiInitTryCnt >= 3)){              
          errorRing(ERR_NO_MARCONI_CLIENT);  // this is confusing for the user
        }
        if (!initMarconi() && (marconiInitTryCnt >= 10)){ 
          errorRing(ERR_NO_MARCONI_CLIENT);                       
          ESP.restart();     
        }
      }
      return;
   }
   
   marconiClient->loop();
   currTime = millis();
   
   if (!marconiSessionInitialized) {
      if (currTime - lastInitTry > 10000){
        lastInitTry = currTime;
        initMarconiSession();        
        if (marconiInitTryCnt >= 3){          
          Serial.print("Unable to initialize Session. Try ");
          Serial.print(marconiInitTryCnt);
          Serial.println("/10");
          errorRing(ERR_MARCONI_SESSION); // this is confusing for the user
        }
        if (marconiInitTryCnt >= 10){  
          Serial.println("Unable to initialize Session, System will reboot now");        
          errorRing(ERR_MARCONI_SESSION);          
           ESP.restart();  
        }
      }
      return;
  }  

  
   if (currTime - lastObservationOngoingEventReceived > observationTimeout) {
        Serial.println("Observation Timeout - System tries to init session again.");
        marconiSessionInitialized = false;  
        lastResubscribe == 0;
        refreshGauge();
        return;
   } 

   
   if (currTime - lastResubscribe > resubscribeInterval || lastResubscribe == 0 ) {
      lastResubscribe = currTime;
      marconiClient->subscribeForActions(onAction);    
   }   
   

   if ((gaugeValue >=0) && (currTime - lastObservationOngoingEventReceived > (observationTimeout/2))) {
         Serial.println("Connection seems to be lost. Did not received event 'Observation Ongoing'");
         Serial.println("Gauge will be disabled by setting color to white");
         //marconiSessionInitialized = false;         
         gaugeValue = -1;
         refreshGauge();
   }

   

   // periodically send property updates
   if (currTime - lastPropertyUpdate > propertyUpdateInterval) {    
      Serial.println("Sending Properties Update");  
      lastPropertyUpdate = currTime;
      sendGaugeDimmLevelValue();
      sendWarningLedState();
      if (sensorsAvailable()) {        
        if (scd30_available){
          if (scd30.dataReady()){
            if (!scd30.read()){ 
              Serial.println("Error reading SCD30 data"); 
            }
          }
        }              
        readTemperature();
        readHumidity();
        readPressure();       
        Serial.println("Sending property values");
        sendTemperatureValue();       
        sendHumidityValue();       
        sendPressureValue();        
        if (sps30_available){
          readParticles();  
          sendParticleValues();
        }                
        if (readCo2()){
          sendCo2Value();
        } else {
         if (readCo2Equivalent()){
           sendCo2Value();
         } 
        }
      }
    }     
  
}

void doSensorStuff(unsigned long currTime){  
  // BME680/BSEC stuff
  if (bme680_available){         
    if (!checkIaqSensorStatus()) {
      evalIaqSensorStatus();
      
      
    }
     if (periodicallyBsecSave) {     
        if (currTime - lastBsecUpdate > bsecStateUpdateInterval){
          saveBsecState();
          lastBsecUpdate = currTime;
        }
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
             errorGauge(ERR_BSEC);
           }
        }
     }
     // trigger animation when sensor is calibrating, do not animate when button is pressed
     if ((!isIaqCalibrated()) && (!buttonPressed)){        
        if (currTime - lastCalibrationAnimationStep > animationSpeed){        
          triggerNotCalibratedAnimation();
          lastCalibrationAnimationStep = currTime;
        }
     }
  }

  
  
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                 System Functions
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


bool loadDeviceConfig(){
   Serial.println("reading device configuration"); 

   deviceConfigMemory.get(0,deviceConfig);      
   Serial.print("Device ID = ");
   Serial.println(deviceConfig.id);    
    Serial.print("Device ID: ");
   for (int i = 0; i < DEVICE_ID_SIZE; i++){
      Serial.print( deviceConfig.id[i],HEX);
      Serial.print(" ");
   }
   Serial.println();
   Serial.print("last 5 bytes of Device Key: ");
   for (int i = CHACHA_KEY_SIZE-5; i < CHACHA_KEY_SIZE; i++){
      Serial.print( deviceConfig.key[i],HEX);
      Serial.print(" ");
   }
   Serial.println();

  return ((deviceConfig.id[0] != 0xFF) && (deviceConfig.id[0] != 0)) ; 
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

// ++++++++++++++++++++++++++++++++++++ SPS30 +++++++++++++++++++++++++++++++++++


bool initSPS30(){
   int16_t ret;
    sensirion_i2c_init();
    int cnt = 0;
    Serial.print("Initializing SPS30 .... ");
    while (sps30_probe() != 0) {
     // Serial.print("SPS sensor probing failed\n");
      delay(500);
      cnt++;
      if (cnt > 2){
        Serial.println("ERROR - SPS sensor probing failed");
        return false;
      }
    }
    Serial.println("OK");
   
   ret = sps30_set_fan_auto_cleaning_interval_days(4);
    if (ret) {
      Serial.print("ERROR, unable to set the auto-clean interval: ");
      Serial.println(ret);
      return false;
    }  

    // start measuring
  ret = sps30_start_measurement();
  if (ret < 0) {
    Serial.println("ERROR, starting measurement\n");    
  }

  return true;
}

// +++++++++++++++++++++++++++++++ BME680 / BSEC ++++++++++++++++++++++++++++++++

bool initBME680(){
  Serial.print("Initializing BME680 ... ");  
  //iaqSensor.begin(BME680_I2C_ADDR_SECONDARY,Wire);  

  int address = 0x76;
  // check for address 0x77 
  
   Wire.beginTransmission(address);
   if(Wire.endTransmission()!=0){ 
      address = 0x77;
      Wire.beginTransmission(address);
      if(Wire.endTransmission()!=0){ 
        Serial.println("ERROR");
        return false;
      }
   }
  
  iaqSensor.begin(address,Wire);  
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
  return bme280_available || bme680_available || scd30_available || sps30_available;
}

void eraseBsecState(){
  bool success = true;
  Serial.print("Erasing BSEC state ...");
  for (uint8_t i = 0; i < BSEC_MAX_STATE_BLOB_SIZE + 1; i++) {
      bsecStateMemory.write(i, 0xFF);      
        success = success && bsecStateMemory.commit();
        bsecState[i]=0xFF;      
  }    
  if(success){
    Serial.println("OK");
  } else {
    Serial.println("Error");
  }
  
  printBsecState();
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
  iaqSensor.getState(bsecState);
  printBsecState();
}

void printBsecState(){
  Serial.print("BSEC State : ");
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
  // only success gauge when switching from IAQA_NOT_CALIBRATED to IAQA_UNCERTAIN. 
  if (iaq_accuracy == IAQA_UNCERTAIN){
    successGauge();
  }
  lastPropertyUpdate = 0;
  clearRing();
  setGaugePercentage(gaugeValue);
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

// +++++++++++++++++++++++++++++++++++ SCD30 +++++++++++++++++++++++++++++++++++

bool initSCD30(){
  Serial.print("Initializing SCD30 .... ");
  bool res = scd30.begin();
  if (res) {
    Serial.println("OK");
    printScd30Configuration();  
    
  } else {
    Serial.println("ERROR");
    Serial.println("No SCD30 sensor found on I2C wire");
  }
  
  return res;
}

bool printScd30Configuration(){  
  Serial.println("---------------------- SCD30 Configuration ------------------------");
  Serial.print("Measurement interval: ");
  Serial.print(scd30.getMeasurementInterval());
  Serial.println(" seconds");
  Serial.print("Ambient pressure offset: ");
  Serial.print(scd30.getAmbientPressureOffset());
  Serial.println(" mBar");
  Serial.print("Altitude offset: ");
  Serial.print(scd30.getAltitudeOffset());
  Serial.println(" meters");
  Serial.print("Temperature offset: ");
  Serial.print((float)scd30.getTemperatureOffset()/100.0);
  Serial.println(" degrees C");
  Serial.print("Forced Recalibration reference: ");
  Serial.print(scd30.getForcedCalibrationReference());
  Serial.println(" ppm");
    if (scd30.selfCalibrationEnabled()) {
    Serial.println("Self calibration enabled");
  } else {
    Serial.println("Self calibration disabled");
  }

  Serial.println("-------------------------------------------------------------------");
}
// +++++++++++++++++++++++++++ updating Sensor values ++++++++++++++++++++++++++++

bool isValueChangeTimeout(){
  if (!sensorsAvailable()){
    return false;
  }
  return (millis() - lastValueChange > valueChangeTimeout);
}

bool readParticles(){
  char serial[SPS30_MAX_SERIAL_LEN];
  uint16_t data_ready;
  int16_t ret;
  Serial.println("Reading particle values");
  ret = sps30_read_data_ready(&data_ready);
  if (ret < 0) {
      Serial.print("error reading data-ready flag: ");
      Serial.println(ret);
      return false;
  };
    
  if (!data_ready){
    Serial.println("SPS30 data not ready yet");
    return false;
  }
   
  // data ready
  ret = sps30_read_measurement(&m);
  if (ret < 0) {
    Serial.print("error reading measurement\n");
    return false;
  } 

    Serial.print("PM  1.0: ");
    Serial.println(m.mc_1p0);
    Serial.print("PM  2.5: ");
    Serial.println(m.mc_2p5);
    Serial.print("PM  4.0: ");
    Serial.println(m.mc_4p0);
    Serial.print("PM 10.0: ");
    Serial.println(m.mc_10p0);
    Serial.print("NC  0.5: ");
    Serial.println(m.nc_0p5);
    Serial.print("NC  1.0: ");
    Serial.println(m.nc_1p0);
    Serial.print("NC  2.5: ");
    Serial.println(m.nc_2p5);
    Serial.print("NC  4.0: ");
    Serial.println(m.nc_4p0);
    Serial.print("NC 10.0: ");
    Serial.println(m.nc_10p0);

    Serial.print("Typical partical size: ");
    Serial.println(m.typical_particle_size);
  return true;
}

bool readTemperature(){  
  if (!sensorsAvailable()) {
    return false;
  }
 
 
  float newTemperature;
  
  if (bme680_available){
    newTemperature = iaqSensor.temperature;  // use 0.5 steps for temperature  
  } else if (bme280_available) {
    newTemperature = bme280.readTemperature();  // use 0.5 steps for temperature  
  } else if (scd30_available){
    newTemperature = scd30.temperature;  // use 0.5 steps for temperature  
    // adding the offset - this is only needed for connctd FRAME setup to compensate ESP32 heat
    newTemperature += TEMPERATURE_OFFSET;
  } 


  
  if (temperature != newTemperature){
    lastValueChange = millis();
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
  
  if (scd30_available){
    newHumidity = int(scd30.relative_humidity);
 } else if (bme680_available){
    newHumidity = int(iaqSensor.humidity);
  } else if (bme280_available){
    newHumidity = int(bme280.readHumidity()); // ignore values after . 
  }
  if (humidity != newHumidity){
    lastValueChange = millis();
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
    lastValueChange = millis();
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
      lastValueChange = millis();
      co2 = newCo2;  
      Serial.print("Co2 equivalent = ");
      Serial.print(co2);
      Serial.println(" ppm");
      return true;
     }
  }
  return false;
}

bool readCo2(){
  if (!scd30_available){
    return false;
  }
   float newCo2 = int(scd30.CO2);
   if (newCo2 != co2){
    lastValueChange = millis();
    co2 = newCo2;
    Serial.print("CO2 value = ");
    Serial.print(co2);
    Serial.println(" ppm");
    return true;
   }
   return false;
}

// +++++++++++++++++++++++++++++++++++ Button +++++++++++++++++++++++++++++++++++


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
      
      resetToFactorySettings();
      blinkRing(CRGB(255,0,0));     
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


bool isButtonPressed(){
  return  digitalRead(TRIGGER_PIN) == LOW ;
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    IoT Connctd
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// requestes a session id from connector which will be exchanged in every
// message to prevent replay attacks. Can be called multiple times
void initMarconiSession() {  
  if (marconiSessionInitialized) {
    return;
  }
  Serial.println("Initializing session");  
    
  marconiInitTryCnt++;
  marconiClient->init();
}


void sendHumidityValue(){
  if (!marconiSessionInitialized){
    return;
  }
  marconiClient->sendFloatPropertyUpdate(property_humidity, humidity);
}

void sendTemperatureValue(){
  if (!marconiSessionInitialized){
    return;
  }
   marconiClient->sendFloatPropertyUpdate(property_temperature, temperature);
}

void sendPressureValue(){
  if (!marconiSessionInitialized){
    return;
  }
 marconiClient->sendFloatPropertyUpdate(property_pressure, pressure);
}

void sendCo2Value(){
  if (!marconiSessionInitialized){
    return;
  }
 marconiClient->sendFloatPropertyUpdate(property_co2, co2);
}

void sendParticleValues(){
   if (!marconiSessionInitialized){
    return;
  }
  marconiClient->sendFloatPropertyUpdate(property_particle_1pm   , m.mc_1p0);
  marconiClient->sendFloatPropertyUpdate(property_particle_2pm5  , m.mc_2p5);
  marconiClient->sendFloatPropertyUpdate(property_particle_4pm   , m.mc_4p0);
  marconiClient->sendFloatPropertyUpdate(property_particle_10pm  , m.mc_10p0);
  marconiClient->sendFloatPropertyUpdate(property_particle_1nc   , m.nc_1p0);
  marconiClient->sendFloatPropertyUpdate(property_particle_2nc5  , m.nc_2p5);
  marconiClient->sendFloatPropertyUpdate(property_particle_4nc   , m.nc_4p0);
  marconiClient->sendFloatPropertyUpdate(property_particle_10nc  , m.nc_10p0);
}

void sendGaugeDimmLevelValue(){
  if (!marconiSessionInitialized){
    return;
  }
   marconiClient->sendFloatPropertyUpdate(property_dimmlevel, dimmLevel);
}

void sendGaugeValue(){
  if (!marconiSessionInitialized){
    return;
  }
   marconiClient->sendFloatPropertyUpdate(property_gauge, gaugeValue);
}

void sendWarningLedState(){
  if (!marconiSessionInitialized){
    return;
  }
  marconiClient->sendBooleanPropertyUpdate(property_indicator, warningLedOn);
}

// called whenever an action is invoked
void onAction(unsigned char actionId, char *value) {
  lastActionReceived = millis();
  Serial.printf("Action called. Id: %x Value: %s\n", actionId, value);
  switch (actionId){
    case actionID_gaugeValue:
      setGaugePercentage(String(value).toInt());
      break;
    case actionID_dimmLevel:
      setGaugeDimmLevel(String(value).toFloat()*100);
      break;
    case actionID_indicator:
      setWarningLed(value[0] == 0x31);
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
    Serial.print("[CON] ");
    switch (state) {
      case kConnectionStateInitialized:
        Serial.println("Session was Initialized");

        // enforce resubscription
        lastResubscribe = 0;

        // prevent observation timeout right after initialization was done
        lastObservationOngoingEventReceived = millis();
        
        marconiSessionInitialized = true;
        marconiInitTryCnt = 0;
        break;
      case kConnectionStateUninitialized:
        Serial.println("Session initialization ongoing");
        marconiSessionInitialized = false;
        break;
      case kConnectionStateInitRejected:
        Serial.println("Session initialization has failed");
        marconiSessionInitialized = false;   
        errorRing(ERR_MARCONI_SESSION);     
        break;
      case kConnectionObservationRequested:
        Serial.println("Observation was requested");
        break;
      case kConnectionObservationOngoing:
        Serial.println("Observation is now ongoing");
        lastObservationOngoingEventReceived = millis();         
        marconiSessionInitialized = true;
        break;
      case kConnectionObservationRejected:
        Serial.println("Observation was rejected");

        // reinit session in case connector was restarted
        marconiSessionInitialized = false;
        // after reinit we want to resubscribe
        lastResubscribe = 0;
        errorRing(ERR_MARCONI_SESSION);
        break;
      default:
        Serial.printf("Unknown connection event %x\n", state);
        break;
    }
    refreshGauge();
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
   refreshGauge();
}

bool isObservationTimeout() { 
 
  return (millis() - lastObservationOngoingEventReceived > observationTimeout);
}

bool isActionTimeout(){
  
  return (millis() - lastActionReceived > actionReceiveTimeout);
}

bool resolveMarconiIp(){
  Serial.print("Resolving IP address for ");
  Serial.print(marconiUrl);
  Serial.print("  ...  ");  
  if (!WiFi.hostByName(marconiUrl, marconiIp) == 1) { 
      Serial.println("ERROR");
      Serial.println("unable to resolve marconiUrl");
      return false;
  }
  Serial.println("OK");
  Serial.printf("Marconi-UDP IP Address : %d.%d.%d.%d\n", marconiIp[0], marconiIp[1], marconiIp[2], marconiIp[3]);  
  return true;
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
    //errorRing();  
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
      //errorRing();                 
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
  clearRing();
  animateGauge(0,oldScaleValue);
  animationSpeed = as_mem;
}

int getScaleValue(int value){
  int pixels = round((NUMPIXELS * value)/100.0);

  Serial.print("Pixels ");
  Serial.println(pixels);
  
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
  notCalibratedAnimationState++;
  if (notCalibratedAnimationState > (NUMPIXELS-1)) {
    notCalibratedAnimationState = -1 * (NUMPIXELS-1);
  }
  if (notCalibratedAnimationState > 0) {
    leds[notCalibratedAnimationState] = CRGB(255,255,255);
  } else {
    leds[(-1)*notCalibratedAnimationState] = CRGB(255,255,255);
  }
  FastLED.show();  
}


void sensorInfo(){
  clearRing();
  
  if (scd30_available) {
    leds[0] = CRGB(0,255,0);   
  } else {
    leds[0] = CRGB(255,255,255);   
  }
  if (bme680_available) {
    leds[1] = CRGB(0,255,0);   
  } else {
    leds[1] =CRGB(255,255,255);   
  }
  if (bme280_available) {
    leds[2] = CRGB(0,255,0);   
  } else {
    leds[2] = CRGB(255,255,255);   
  }
  if (sps30_available) {
    leds[3] = CRGB(0,255,0);   
  } else {
    leds[3] = CRGB(255,255,255);   
  }

  FastLED.show();
  delay(1000);
  clearRing();
}


void animateGauge(int startPixel, int stopPixel){

  if ((gaugeValue < 0) || (!marconiSessionInitialized)){
    leds[0] = CRGB(255,255,255); 
    FastLED.show();
    return;
  }

  
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


void errorRing(int error_id){
  blinkRing(CRGB(255,0,0));
  delay(500);    
  switch (error_id){
    case ERR_NO_WIFI:
       setRingColor(CRGB(0,0,255));   // blue
       break;
    case ERR_NO_MARCONI_CLIENT:
       setRingColor(CRGB(255,165,0)); // orange 
       break;
    case ERR_MARCONI_SESSION:
        setRingColor(CRGB(200,1,175));  // violette 
       break;
    case ERR_GENERAL:
       setRingColor(CRGB(255,255,255));  // white
       break;
    case ERR_EEPROM:       
       setRingColor(CRGB(255,0,0));  // red
       break;
    case ERR_NOT_FLASHED:
       setRingColor(CRGB(255,0,0));  // red
       break;      
    case ERR_REQUESTED_RESTART:
       setRingColor(CRGB(0,255,0));  // green
       break;
  }  
  delay(1000);
  refreshGauge();
}

void errorGauge(int error_id){   
  blinkGauge(CRGB(255,0,0)); 
  delay(500);       
   switch (error_id){
    case ERR_BME680:
       setGaugeColor(CRGB(255,165,0));   // orange 
       break;
    case ERR_BSEC:
        setGaugeColor(CRGB(200,1,175));  // violette 
       break;
    case ERR_GENERAL:
       setGaugeColor(CRGB(255,255,255)); // white
       break;
    case ERR_BME280:
       setRingColor(CRGB(255,255,0));      // yellow
       break;        
  }
  delay(1000);    
  refreshGauge();
}

void blinkRing(CRGB color){
  for (int i = 0; i< 3; i++){
      delay(150);    
      setRingColor(color);
      delay(150);
      clearRing();
    }
}

void blinkGauge(CRGB color){
  for (int i = 0; i< 3; i++){
      delay(150);    
      setGaugeColor(color);
      delay(150);
      clearRing();
    }
}

void successRing(){
  blinkRing(CRGB(0,255,0));      
}

void successGauge(){
  blinkGauge(CRGB(0,255,0));      
}

void blinkWarningLed(){
  for (int i = 0; i< 5; i++){    
    digitalWrite(WARNING_PIN,true);
    delay(100);
    digitalWrite(WARNING_PIN,false);
    delay(100);
  }
}

void setWarningLed(bool state){
   if (warningLedOn == state){
    return;
   }   
   warningLedOn = state;
   Serial.print("Turning Warning LED ");
   if (warningLedOn){
      Serial.println("ON");
   } else {
    Serial.println("OFF");
   }
   if (warningLedOn){
      blinkWarningLed();
   }
   digitalWrite(WARNING_PIN,warningLedOn);
   sendWarningLedState();
}
