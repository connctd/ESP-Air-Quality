

#include <EEPROM.h>

// ++++++++++++++++++++++ Gauge ++++++++++++++++++++
#define LED_PIN   D4
#define NUMPIXELS 13
#define ALLPIXELS 28

//Adafruit_NeoPixel pixels = Adafruit_NeoPixel(ALLPIXELS,LED_PIN, NEO_GRB + NEO_KHZ800);

#define DEVICE_ID_SIZE 17
#define CHACHA_KEY_SIZE 32

struct DeviceConfig {    
    char id[DEVICE_ID_SIZE];    
    unsigned char key[CHACHA_KEY_SIZE];
};

DeviceConfig deviceConfig;

void setup() {
  Serial.begin(115200);
  //Serial.setDebugOutput(true); 

  Serial.print("Geht los in 3");
  delay(2000);
  Serial.print(" 2");
  delay(2000);
  Serial.println(" 1");
  delay(2000);

  Serial.println("Initialize EEPROM");
  EEPROM.begin(512);

  Serial.println("generating DeviceConfig object");
  delay(1000);
  deviceConfig = (DeviceConfig){ "0xl7n4igwd4k8g2t",{  0x61, 0x3e, 0x28, 0x39, 0x88, 0x5d, 0xf2, 0xbe,
                                                       0x74, 0x81, 0xb1, 0xc7, 0x3e, 0xe3, 0x8f, 0x36,
                                                       0x19, 0x4f, 0xe0, 0xbc, 0xd3, 0xf2, 0x1d, 0xab,
                                                       0x8a, 0x4c, 0x4a, 0x91, 0x7a, 0x97, 0x50, 0x5a }}; 

  Serial.println("DeviceConfig generated");
  Serial.print("DeviceID = ");
  Serial.println(deviceConfig.id);
  saveDeviceConfig();

  checkDeviceConfig();
  
  Serial.println("And Done... Bye!");
}

void checkDeviceConfig(){
   DeviceConfig devconf;
   Serial.println("Loading device settings"); 
   EEPROM.get(0,devconf);    
   Serial.print("Device ID = ");
   Serial.println(devconf.id);   
   Serial.print("Device Key = ");
   for (int i = 0; i < CHACHA_KEY_SIZE; i++){
      Serial.print(devconf.key[i],HEX);
      Serial.print(" ");
   }
   Serial.println();
}

void saveDeviceConfig(){
  Serial.print("Save device configuration ... ");
  EEPROM.put(0, deviceConfig);
  if (EEPROM.commit()) {
     Serial.println("OK");
  } else {
     Serial.println("EEPROM error - Device Id and Device Code could not be saved");
    // errorRing();
  }
}

void loop() {

}
