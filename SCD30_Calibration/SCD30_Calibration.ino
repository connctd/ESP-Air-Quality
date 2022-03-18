
/*           
 * ===============================================================================================================================
 *            ╔═══╗     ╔╗         ╔╗                        ╔═══╗╔═══╗╔═══╗╔═══╗╔═══╗    ╔═══╗     ╔╗   ╔╗           ╔╗        
 *            ║╔═╗║     ║║        ╔╝╚╗                       ║╔═╗║║╔═╗║╚╗╔╗║║╔═╗║║╔═╗║    ║╔═╗║     ║║   ║║          ╔╝╚╗       
 *            ║║ ║║╔╗╔═╗║║   ╔╗ ╔╗╚╗╔╝╔╗╔══╗╔══╗             ║╚══╗║║ ╚╝ ║║║║╚╝╔╝║║║ ║║    ║║ ╚╝╔══╗ ║║ ╔╗║╚═╗╔═╗╔══╗ ╚╗╔╝╔══╗╔═╗
 *            ║╚═╝║╠╣║╔╝║║ ╔╗║║ ║║ ║║ ╠╣║╔═╝║══╣    ╔═══╗    ╚══╗║║║ ╔╗ ║║║║╔╗╚╗║║║ ║║    ║║ ╔╗╚ ╗║ ║║ ╠╣║╔╗║║╔╝╚ ╗║  ║║ ║╔╗║║╔╝
 *            ║╔═╗║║║║║ ║╚═╝║║╚═╝║ ║╚╗║║║╚═╗╠══║    ╚═══╝    ║╚═╝║║╚═╝║╔╝╚╝║║╚═╝║║╚═╝║    ║╚═╝║║╚╝╚╗║╚╗║║║╚╝║║║ ║╚╝╚╗ ║╚╗║╚╝║║║ 
 *            ╚╝ ╚╝╚╝╚╝ ╚═══╝╚═╗╔╝ ╚═╝╚╝╚══╝╚══╝             ╚═══╝╚═══╝╚═══╝╚═══╝╚═══╝    ╚═══╝╚═══╝╚═╝╚╝╚══╝╚╝ ╚═══╝ ╚═╝╚══╝╚╝ 
 *                           ╔═╝║                                                                                               
 *                           ╚══╝                                                                               http://connctd.com              
 * ===============================================================================================================================
 * 
 * 
 * This program allows the manual calibration of SCD30 CO2 Sensors that is connctd via I2C. It was tested with the Sensirion SCD30. 
 * In general, this sensor has already been calibrated by the manufacturer. However, it may happen that this calibration is no 
 * longer valid. 
 *
 * There are two ways of calibrating the SCD30 with this program
 *      - place the sensor in an environment with a CO2 content of about 400ppm, wait until the measured CO2 value stabilizes and 
 *        press the button 
 *      - place the sensor in an environment with known CO2 value, wait until the measured CO2 value stabilizes and send the 
 *      - reference value via Serial. 
 *
 * 
 * Calibration Method 1 - pressing the button
 * ==========================================
 * 
 * This software was written for the connctd ESP32 Wroover Board. The trigger button is triggering PIN 14. If you use a different 
 * setup, change the value of TRIGGER_PIN to your setup.
 * 
 * 
 * 
 * Calibration Method 2 - sending calibration value
 * ================================================
 * 
 * The command to force the calibration is #cal:<your co2 value>$ where <your co2 value> has to be replaced by an integer value 
 * representing the reference value for your calibration. Example, to calibrate the SCD30 with a reference of 600ppm you have to
 * send #cal:600$.
 * 
 * 
 * 
 * The LED deviation indicator
 * ==========================
 * 
 * The upper part of the LED Ring (LEDs 1-11) could be used as deviation indicator for the CO2 measurements. It demonstrates the             
 * mean deviation of the last 5 measurements. The greater the mean deviation, the more LEDs will light up. 
 * 
 * When the deviation is lower than 5ppm, a single green LED will light up. This will be the right time to force the calbration. 
 * 
 */

#include <Adafruit_SCD30.h>
#include <FastLED.h>  

// Button
#define TRIGGER_PIN 14
// LED
#define LED_PIN   25
#define NUMPIXELS 13
#define ALLPIXELS 24
#define LED_TYPE    WS2811
#define COLOR_ORDER GRB

CRGB leds[ALLPIXELS];

// SCD30
#define CAL_REFERENCE 400
Adafruit_SCD30  scd30;

// Calibration
float co2_values[] = {0.0, 0.0, 0.0, 0.0, 5000.0};
int co2_deviation;

// temporarily command cache
String serialCommand = "";



// ===========================================
//                    SETUP
// ===========================================

void setup() {
  delay(1000);
  
  Serial.begin(115200);
  while (!Serial) delay(10);    
  delay(1500);
  
  Serial.println("AirLytics - SCD30 calibration");
  Serial.println("=============================");
  Serial.println("");
  Serial.print("Press the Button on the ESP-Board to force the recalibration with ");
  Serial.print(CAL_REFERENCE);
  Serial.println("ppm");
  Serial.println("");
  Serial.println("         OR ");
  Serial.println("");
  Serial.println("Send the command #cal:<reference>$ to force the recalibration with the given reference value.");
  Serial.println("");
  Serial.println("");
  Serial.print("Trying to initialize SCD30  ... ");

  pinMode(TRIGGER_PIN, INPUT);

  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, ALLPIXELS).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(255);  
  clearRing();

  if (!scd30.begin()) {
    Serial.println("ERROR");
    Serial.println("Failed to find SCD30 chip");
    Serial.println("Check connection and restart the system.");
    setRingColor(CRGB(255,0,0));          
    while (1) { delay(10); }
  }
  
  Serial.println("OK");

  Serial.print("Measurement Interval: "); 
  Serial.print(scd30.getMeasurementInterval()); 
  Serial.println(" seconds");

  Serial.print("Forced Recalibration reference: ");
  Serial.print(scd30.getForcedCalibrationReference());
  Serial.println(" ppm");


}

// ===========================================
//                    LOOP
// ===========================================


void loop() {
  if (scd30.dataReady()){
    readSCD30Value();
  }

  if (Serial.available()) {
    readSerial();
  }

  if (isButtonPressed()){    
    Serial.println("button pressed");
    calibrate();
    delay(1000);
  }
  
  delay(100);
}

// ===========================================
//                Value Reading
// ===========================================


bool isButtonPressed(){
  return  digitalRead(TRIGGER_PIN) == LOW ;
}

void readSCD30Value(){
   if (!scd30.read()){ 
        Serial.println("Error reading sensor data"); return; 
   }

    Serial.print("Temperature: ");
    Serial.print(scd30.temperature);
    Serial.println(" degrees C");
    
    Serial.print("Relative Humidity: ");
    Serial.print(scd30.relative_humidity);
    Serial.println(" %");
    
    Serial.print("CO2: ");
    Serial.print(scd30.CO2, 0);
    Serial.println(" ppm");
    Serial.println("");

    addValueToHistory();
    calcDeviation();
    update_LEDs();
}

// ==============================================================
//                      Serial Communication
// ==============================================================

void readSerial() {
  char c;
  while (Serial.available() > 0) {
    c = Serial.read();
    if (serialCommand == "") {
      // Serial Command does not started yet, waiting for char '#'
      if ((c == '#') || (c == '%')){
        serialCommand += c;
      }
    } else {
      serialCommand += c;
      if (c == '$') {
        // command is complete
        handleSerialCommand();
        serialCommand = "";
      }
    }
  }
}



void handleSerialCommand() {
  int cnt = 0;
  String cmd = "";
  String value   = "";

  // filter boardId, pinId, value
  // ignoring first and last character - thus start counting at 1 and finish one letter earlier
  for (int i = 1; i < serialCommand.length() - 1; i++) {
    if (serialCommand.substring(i, i + 1) == ":") {
      cnt++;
    } else {
      switch (cnt) {
        case 0:
          cmd += serialCommand.substring(i, i + 1);
          break;
        case 1:
          value += serialCommand.substring(i, i + 1);
          break;
      }
    }
  }
  
  if (serialCommand.substring(0,1) == "#") {
    handleCommand(cmd, value);
    
  } 
  
}

void handleCommand(String cmd, String value){
  Serial.print("processing command '");
  Serial.print(cmd);
  Serial.print("' with value '");
  Serial.print(value);
  Serial.print("'  ...   "); 
  if (cmd=="cal") {       
       Serial.println("OK");       
       int reference = value.toInt();
       calibrateReference(reference); 
       return;    
  }
  Serial.println("ERROR");
  Serial.print("command '");
  Serial.print(cmd);
  Serial.println("' is unkonwn");
}



// ==============================================================
//                            Calibration
// ==============================================================


void calibrate(){
  calibrateReference(CAL_REFERENCE);  
}

void calibrateReference(int reference){
  Serial.println("");
  Serial.println("will calibrate SCD30 now");
  scd30.forceRecalibrationWithReference(reference);
  Serial.print("SCD30 is recalibrated with reference ");
  Serial.print(reference);
  Serial.println("ppm");
  Serial.println("");
  successRing();  
}

void calcDeviation(){
  float tmp = 0;
  for (int i = 0; i<4; i++){
    tmp = tmp + getDifference(co2_values[i],co2_values[i+1]);
  }
  co2_deviation = tmp/5;
  Serial.print("CO2 value deviation of the last 5 values: ");
  Serial.println(co2_deviation,0);
}

float getDifference(float v1, float v2){
  if (v1 < v2) {
    return v2-v1;
  }
  return v1-v2;
}

void addValueToHistory(){
  for (int i = 0; i<4; i++) {
     co2_values[i]  = co2_values[i+1];
  }
  co2_values[4] = scd30.CO2;
}

// ==============================================================
//                            LED Ring
// ==============================================================

void update_LEDs(){
  clearRing();
  CRGB color = CRGB(255,255,255);
   if (co2_deviation < 5) {
     color = CRGB(0,255,0);
   }
   leds[6]=color;
   if (co2_deviation > 5) {
      leds[5] = color;
      leds[7] = color;
   }
   if (co2_deviation > 10) {
      leds[4] = color;
      leds[8] = color;
   }
   if (co2_deviation > 20) {
      leds[3] = color;
      leds[9] = color;
   }
   if (co2_deviation > 40) {
      leds[2] = color;
      leds[10] = color;
   }
    if (co2_deviation > 70) {
      leds[1] = color;
      leds[11] = color;
   }
     FastLED.show();  
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

void blinkRing(CRGB color){
  for (int i = 0; i< 3; i++){
      delay(150);    
      setRingColor(color);
      delay(150);
      clearRing();
    }
}

void successRing(){
  blinkRing(CRGB(0,255,0));      
}
