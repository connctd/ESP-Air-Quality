#include <EEPROM.h>

// ++++++++++++++++++++++ LED ++++++++++++++++++++
#define LED_PIN   D4
#define NUMPIXELS 13
#define ALLPIXELS 28

//Adafruit_NeoPixel pixels = Adafruit_NeoPixel(ALLPIXELS,LED_PIN, NEO_GRB + NEO_KHZ800);

// +++++++++++++ Device Configuration +++++++++++++

#define DEVICE_ID_SIZE 17
#define CHACHA_KEY_SIZE 32
#define DEVICE_CONFIG_MEMORY_SIZE 0x128

struct DeviceConfig {    
    char id[DEVICE_ID_SIZE];    
    unsigned char key[CHACHA_KEY_SIZE];
};

DeviceConfig deviceConfig;

EEPROMClass  devConfigMemory("devConfig", DEVICE_CONFIG_MEMORY_SIZE);


// ===============================================
//                  SETUP
// ===============================================
void setup() {
  Serial.begin(115200);

  Serial.print("Starting in 3");
  delay(2000);
  Serial.print(" 2");
  delay(2000);
  Serial.println(" 1");
  delay(2000);

  Serial.println("Initialize EEPROM");
  devConfigMemory.begin(devConfigMemory.length());

  Serial.println("generating DeviceConfig object");
  delay(1000);


// deviceConfig = (DeviceConfig){ "<put device id here>", <put device key here - { 0x3d, 0x81, etc.. }>}; 


// In order to generate your device key, take the device secret and convert it to base64 hex.
// use this tool https://cryptii.com/pipes/base64-to-hex

  Serial.println("DeviceConfig generated");
  Serial.print("DeviceID = ");
  Serial.println(deviceConfig.id);
  printKey(deviceConfig);
  saveDeviceConfig();

  checkDeviceConfig();
  
  Serial.println("And done... Bye!");
}

bool checkDeviceConfig(){
   DeviceConfig devconf;
   devConfigMemory.get(0,devconf);    
   bool res = true;
   Serial.print("Checking device Id ... ");
   for (int i = 0; i < DEVICE_ID_SIZE; i++){
    res = res && devconf.id[i] == deviceConfig.id[i]; 
   }
   if (res) {
      Serial.println("OK");
   } else {
      Serial.println("ERROR");
      Serial.println("Device ID was not stored or loaded correctly");
      Serial.print("Device ID to save = ");
      Serial.println(deviceConfig.id);
      Serial.print("Device ID loaded  = ");
      Serial.println(devconf.id);
      return res;
   }
   Serial.print("Checking Device Key ... ");   
   for (int i = 0; i < CHACHA_KEY_SIZE; i++){
      res = res && devconf.key[i] == deviceConfig.key[i];   
   }
   
   if (res) {
      Serial.println("OK");      
   } else {
      Serial.println("ERROR");
      Serial.println("resotred key is not equal device key. Somethings went wrong.");
      Serial.println("----- Key to store -----");
      printKey(deviceConfig);
      Serial.println("------------------------");
      Serial.println("----- Key restored -----");
      printKey(devconf);
      Serial.println("------------------------");
   }
   Serial.println();
   return res;
}

bool saveDeviceConfig(){
  Serial.print("Save device configuration ... ");
  devConfigMemory.put(0, deviceConfig);
  if (devConfigMemory.commit()) {
     Serial.println("OK");
  } else {
     Serial.println("EEPROM error - Device Id and Device Code could not be saved");    
     return false;
  }
  return true;
}

void printKey(DeviceConfig dc){
   Serial.print("Device Key = ");
   for (int i = 0; i < CHACHA_KEY_SIZE; i++){     
      Serial.print(dc.key[i],HEX);
      Serial.print(" ");
   }
   Serial.println();
}

void loop() {
  delay(0xFFFFFFFF);
}
