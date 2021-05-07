
#include <ESP8266WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>
#include <ESP8266TrueRandom.h>
#include "coap_client.h"
#include <EEPROM.h>
// ++++++++++++++++++++ WIFI Management +++++++++++++++

#define TRIGGER_PIN D6
WiFiManager wm; 
WiFiManagerParameter custom_field; 
const char* AP_SSID = "Air-Quality";
//bool connectedToWifi = false;
  

// ++++++++++++++++++++++ Gauge ++++++++++++++++++++
#define LED_PIN   D4
#define NUMPIXELS 13
#define ALLPIXELS 28


Adafruit_NeoPixel pixels = Adafruit_NeoPixel(ALLPIXELS,LED_PIN, NEO_GRB + NEO_KHZ800);

int animationSpeed = 50; 
int oldScaleValue = 1;


// +++++++++++++++++++++++ Connctd +++++++++++++++++++++

coapClient coap;
IPAddress ip(35,205,82,53);
StaticJsonDocument<200> jsonDoc;
int port = 5683;
char* actionPath = " api/v1/devices/xxxxxxxx/xxxx/action";
char* propertyPath = " api/v1/devices/xxxxxxxx/xxxx/status";

struct DeviceConfig {    
    char id[9];     // need one char more for String termination
    char code[5];   // same here 
} deviceConfig;


// +++++++++++++++++++++++ General +++++++++++++++++++++
unsigned int loopCnt = 0;



// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                   SETUP
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void setup() {
  
  Serial.begin(115200);
  Serial.setDebugOutput(true);   
  Serial.println("\n Starting");

  pinMode(TRIGGER_PIN, INPUT);

  EEPROM.begin(512);

  initializeRandomSeed();
  initializeLedRing();   
  initializeWiFi(); 
  
  // ToDo - make non blocking and animate "connecting gauge"
  if (!connectToWiFi()){    
    ESP.restart();
  }

  loadDeviceConfig();
  clearRing();
  initializeCoapClient();

    
  delay(1500);    
}

void initializeRandomSeed(){
    srand(ESP8266TrueRandom.random());
}

void initializeLedRing(){
  pixels.begin(); 
  clearRing();  
}

void initializeCoapClient(){
  buildPaths();
  coap.start(port);    
  coap.response(onServerMessage);
}

void loadDeviceConfig(){
   Serial.println("Loading device settings");
 
   EEPROM.get(0,deviceConfig);
    
   Serial.print("Device ID ");
   Serial.println(deviceConfig.id);   
}

void saveDeviceConfig(){
  EEPROM.put(0, deviceConfig);
  if (EEPROM.commit()) {
     Serial.println("Device configuration saved");
  } else {
     Serial.println("EEPROM error - Device Id and Device Code could not be saved");
     errorRing();
  }
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    THE LOOP
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void loop() {
  checkButton(); // check wether trigger button was pressed  

  if ((loopCnt % 5000) == 0){ // every 50s
    registerForAction();
  }

  if ((loopCnt % 3000) == 0){  // every 30s
    // TODO: measure Values
    //       send property changes when value changed
    sendCo2Value();    
    sendTemperatureValue();
    sendHumidityValue();
  }
  
  coap.loop();
  loopCnt++;

  
  if (loopCnt > 65535){ // when 16bit max have been reached - do not care about 32bit systems
    loopCnt = 0;
  }
  
  delay(10);

}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    IoT Connctd
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void buildPaths(){
  String tmp = "api/v1/devices/"+String(deviceConfig.id)+"/"+String(deviceConfig.code)+"/action";
  tmp.toCharArray(actionPath, tmp.length()+1);  
  tmp = "api/v1/devices/"+String(deviceConfig.id)+"/"+String(deviceConfig.code)+"/status";
  tmp.toCharArray(propertyPath, tmp.length()+1);
}

void registerForAction(){
   if (actionPath == NULL){    
     return;
   }
   Serial.println("observing for action");       
   int msgId = coap.observe(ip, port, actionPath, 0);
   Serial.print("Observe Message sent (ID = ");
   Serial.print(msgId);    
   Serial.println(")");
}

void onServerMessage(coapPacket &packet, IPAddress ip, int port) {
  Serial.print("Incoming message or response (msgID = ");
  Serial.print(packet.messageid);
  Serial.print(" | packet.type = ");
  Serial.print(packet.type);
  Serial.print(" | packet.code = ");
  Serial.print(packet.code);
  Serial.print(" | payload = ");
  Serial.print(packet.payloadlen);
  Serial.println(")");
  //response from coap server
  if (packet.type == COAP_ACK) {
    Serial.println("- ACK received");
  } else if (packet.type == COAP_CON || packet.type == COAP_NONCON) {
    Serial.println("- MSG received");
    if (packet.type == COAP_CON) {
        Serial.println("sending ACK");
        coapPacket ack = buildAck(packet.messageid);
        coap.sendPacket(ack, ip, port);
    }
    if (packet.payloadlen > 0) {
      processPacket(packet, ip, port);
    }
  }
}
void processPacket(coapPacket &packet, IPAddress ip, int port) {
    char p[packet.payloadlen + 1];
    memcpy(p, packet.payload, packet.payloadlen);
    p[packet.payloadlen] = NULL;    
    Serial.println(p);
    jsonDoc.clear();
    DeserializationError error = deserializeJson(jsonDoc, p);

  // Test if parsing succeeds.
    if (error) {
      Serial.print(F("deserializeJson() failed: "));
      Serial.println(error.f_str());
      return;
    }

    if (jsonDoc.containsKey("actionId")){
      const char* actionName = jsonDoc["id"];
      const char* valueStr = jsonDoc["value"];
      int value = String(valueStr).toInt();
      Serial.print("processing action ");
      Serial.print(actionName);
      Serial.print("(");
      Serial.print(value);
      Serial.println(")");
     
         setGaugePercentage(value);
     
    }    
}


coapPacket buildAck(uint16_t messageid) {
  coapPacket packet;
  //make packet
  packet.type = COAP_ACK;
  packet.code = COAP_EMPTY;
  packet.token = NULL;
  packet.tokenlen = 0;
  packet.payload = NULL;
  packet.payloadlen = 0;
  packet.optionnum = 0;
  packet.messageid = messageid;
  return packet;
}


uint16_t sendData(String data){
  if (propertyPath == NULL){
    return -1 ;
  }
  char dataChar[data.length() + 1];
  data.toCharArray(dataChar, data.length() + 1); 
  int msgID = coap.post(ip, port, propertyPath, dataChar, data.length());
  Serial.print("Property change sent ( ");
  Serial.print(data);
  Serial.print(" | msgID = ");
  Serial.print(msgID);
  Serial.println(")");
}

uint16_t sendHumidityValue(){
  jsonDoc.clear();
  jsonDoc["value"] = "62";
  jsonDoc["id"] = "humidity";

  String data;

  
  serializeJson(jsonDoc, data);  
  return sendData(data);
}

uint16_t sendTemperatureValue(){
  jsonDoc.clear();
  jsonDoc["value"] = "21.5";
  jsonDoc["id"] = "temperature";

  String data;

  serializeJson(jsonDoc, data);  
  return sendData(data);
}

uint16_t sendCo2Value(){
  jsonDoc.clear();
  jsonDoc["value"] = "865";
  jsonDoc["id"] = "co2";

  String data;

  serializeJson(jsonDoc, data);  
  
  char dataChar[data.length() + 1];
  data.toCharArray(dataChar, data.length() + 1); 
  return sendData(data);
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//                                    WiFi Manager
// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


void initializeWiFi(){
   
  
  int customFieldLength = 40;
  
  const char* custom_radio_str = "<br/><br/>Please enter Device ID <br/> <input type='text' name='deviceId' id='deviceId'/><br/>Please enter Device Code <br/> <input type='text' name='deviceCode' id='deviceCode'/>";
  
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


void checkButton(){  
  // TODO  shitty solution, include that in loop somehow
    if( digitalRead(TRIGGER_PIN) == LOW ){
  
        Serial.println("checking Button press");
        delay(100);
        if( digitalRead(TRIGGER_PIN) == LOW ){
        Serial.println("Button Pressed");
        greenRing();
        delay(5000);
        if (digitalRead(TRIGGER_PIN) != LOW ){
          clearRing();
          animateGauge(0,oldScaleValue);
          return;
        }
        blueRing();
        // still holding button for 3000 ms, reset settings, code not ideaa for production
        delay(5000); // reset delay hold
        if( digitalRead(TRIGGER_PIN) == LOW ){
          Serial.println("Button held");
          Serial.println("Erasing Config, restarting");
          errorRing();
          wm.resetSettings();
          ESP.restart();
        }
      
      // start portal w delay
        Serial.println("Starting config portal");
        wm.setConfigPortalTimeout(120);     
        if (!wm.startConfigPortal(AP_SSID,NULL)) {
          Serial.println("failed to connect or hit timeout");          
          errorRing();       
          delay(3000);
        } else {
          //if you get here you have connected to the WiFi
          Serial.println("connected...yeey :)");
        }
     
      }
    }
}

String getParam(String name){
  //read parameter from server, for customhmtl input
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback(){
  Serial.println("[CALLBACK] saveParamCallback fired");
 
  String deviceId = getParam("deviceId");
  deviceId.toCharArray(deviceConfig.id,9);
  deviceConfig.id[8] = '\0';
  String deviceCode = getParam("deviceCode");
  deviceCode.toCharArray(deviceConfig.code,5);
  deviceConfig.code[4] = '\0';

  Serial.println("PARAM deviceId   = " + String(deviceConfig.id));
  Serial.println("PARAM deviceCode = " + String(deviceConfig.code));
  saveDeviceConfig();
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
  int newScaleValue = getScaleValue(value);
  animateGauge(oldScaleValue, newScaleValue);
  oldScaleValue = newScaleValue;
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
      pixels.setPixelColor(i, pixels.Color((255/NUMPIXELS)*i,150-(150/NUMPIXELS)*i,0)); 
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
