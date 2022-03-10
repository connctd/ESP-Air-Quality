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


 
 deviceConfig = (DeviceConfig){ "obfwtd88ro81c6sc", {0xfc, 0x61, 0x1c, 0x6c, 0xd6, 0x0d, 0xcc, 0x8c, 0xf1, 0x92, 0xe4, 0xe8, 0xc3, 0x5b, 0xac, 0x99, 0x2d, 0x59, 0x45, 0x02, 0xd4, 0xbf, 0xc9, 0x6d, 0xac, 0xde, 0xc6, 0xf2, 0x26, 0xbf, 0xab, 0x0b}}; 
 // deviceConfig = (DeviceConfig){ "mwu63nbsfwwgn043", {0x41, 0xc6, 0x33, 0x2e, 0x65, 0xc2, 0xa8, 0xc4, 0x5f, 0x98, 0x96, 0x99, 0xf4, 0x6d, 0x7f, 0x46, 0x66, 0x14, 0x0a, 0x98, 0xde, 0x18, 0x64, 0xb2, 0xe9, 0x77, 0xde, 0xba, 0xdd, 0x50, 0xf1, 0xcf}}; 

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
