
#include <Adafruit_SCD30.h>

Adafruit_SCD30  scd30;

#define TRIGGER_PIN 14
#define CAL_REFERENCE 401

void setup() {
  delay(1000);
  
  Serial.begin(115200);
  while (!Serial) delay(10);    
  delay(1500);
  
  Serial.println("AirLytics - SCD30 calibration");
  Serial.print("Press the Button on the ESP-Board to force the recalibration with ");
  Serial.print(CAL_REFERENCE);
  Serial.println("ppm");
  Serial.println("");
  Serial.print("Trying to initialize SCD30  ... ");

  if (!scd30.begin()) {
    Serial.println("ERROR");
    Serial.println("Failed to find SCD30 chip");
    Serial.println("Check connection and restart the system.");
    while (1) { delay(10); }
  }
  
  Serial.println("OK");

  Serial.print("Measurement Interval: "); 
  Serial.print(scd30.getMeasurementInterval()); 
  Serial.println(" seconds");

  Serial.print("Forced Recalibration reference: ");
  Serial.print(scd30.getForcedCalibrationReference());
  Serial.println(" ppm");

  pinMode(TRIGGER_PIN, INPUT);
  
}

void loop() {
  if (scd30.dataReady()){
    

    if (!scd30.read()){ Serial.println("Error reading sensor data"); return; }

    Serial.print("Temperature: ");
    Serial.print(scd30.temperature);
    Serial.println(" degrees C");
    
    Serial.print("Relative Humidity: ");
    Serial.print(scd30.relative_humidity);
    Serial.println(" %");
    
    Serial.print("CO2: ");
    Serial.print(scd30.CO2, 3);
    Serial.println(" ppm");
    Serial.println("");
  }

  if (isButtonPressed()){
    calibrate();
    delay(1000);
  }

  delay(100);

}

bool isButtonPressed(){
  return  digitalRead(TRIGGER_PIN) == LOW ;
}

void calibrate(){
  Serial.println("");
  Serial.println("button pressed");
  Serial.println("will calibrate SCD30 now");
  scd30.forceRecalibrationWithReference(CAL_REFERENCE);
  Serial.print("SCD30 is recalibrated with reference");
  Serial.print(CAL_REFERENCE);
  Serial.println("ppm");
  Serial.println("");
  
}
