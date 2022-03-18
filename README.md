# Airlytics Frame Manual

## Starting Sequence

After the system has been  powered up it tries to establish a connection with the WLAN. If a connection could be established, the entire LED ring flashes green. If this was not possible, for example because no WiFi has been configured yet, the LED RING lights up blue permanently and the System awaits to setup Wifi credentials (See setting up Wifi)

Afterwards, the sensor configuration is shown by the first 4 LEDs of the LED ring:

- First LED green, SCD30 installed and ready, otherwise white.
- Second LED green, BME680 installed and ready, otherwise white
- Third LED green, BME280 installed and ready, otherwise white
- Fourth LED green, SPS30 installed and ready, otherwise white

If all 4 LEDs are white, no sensor is installed or was found.

If the BME680 sensor is installed, it must first be adjusted so that usable values can be measured. This process can take up to 10min at the first start. If the calibration was completed successfully, it will not take so much time in the future. 

The same applies to the temperature values; these sensors generally need up to 15 minutes before the values reach their high precision.

## Setting Up the WiFi

When the system starts and cannot connect to the WLAN, it automatically goes into its WLAN configuration mode, recognizable by a blue LED ring. You can also start the configuration mode manually by pressing and holding the button on the back until the LED ring lights up green (5s). Then connect the smartphone (or PC) to the Network with the SSID "AirLytics Frame". You will be prompted to "Register to network". Follow this prompt and you will get to the configuration menu. Select the network from the list and enter the password, then press "Save". The system restarts and tries to log in to the network. If this is successful, the LED ring flashes green, otherwise the network configuration starts and the steps must be repeated.

## Factory Reset
In order to reset the device to it's factory settings, the button on the back must be pressed for at least 15s. 

After 5s the LED ring will light up green, after 12s it will light up red. After 15s it will blink red and the device will perform a factory reset. This will delete the network settings and the sensor settings like calibration data. 

# Error Handling

## Error Color Codes

Whenever an error occurs that was identified by the program, the LED ring will blink 3 times in red color. Either the complete ring is blinking, or the gauge only (half of the ring). After the ring was blinking red 3 times, a solid colored ring for is representig the error type.

### Complete Blinking Ring

In general, a complete red blinking LED-ring idicates connection or general system errors. The following color code might give further details on the error type. 

| color    | error              |
|:-------- |:-------------------|
| white    | Error not further specified |
| blue     | Error has to do with Wifi Connection|
| orange   | Maroni Client could not be initialized. IP address could not resolved from URL or kind like that. |
| violette | Marconi Session could not be established. |
| red      | EEPROM failure |


### Gauge Blinking Only (Half of the LED-Ring)
In contrast to the red blinking LED-ring, a half blinking LED-ring indicates errors related to the connected sensors.


| color    | error            |
|:-------- |:-------------------|
| white    | Error not further specified |
| yellow   | ERROR BME280 |
| orange   | Error BME680 |
| violette | Error BSEC library |



# Airlytics Frame Development

This section explains the development of the Airlytics firmware for an ESP32 with the Arduino IDE.   

There are 3 Arduino projects in this repository:
| Arduino Project    | Description              |
|:------------------ |:-------------------------|
| Config-Flasher/Config-Flasher.ino     | Software to flash device credentials (id and key) to the ESP32 EEPROM |
| SCD30_Calibration/SCD30_Calibration.ino  | Software to calibrate the CO2 Sensor (SCD30) |
| AirLyticsFrame_ESP/AirLyticsFrame_ESP.ino | Firmware of the AirLytics Frame |


## Board Management

In order to install ESP32 extension to the Arduino IDE, add the line ```https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json``` to the additional board manager URL in the Arduino Settings. 

Open the Board Management and search for ```esp32```. Choose the version ```1.0.6``` developed by Espressif Systems and hit install. 

With version 1.0.6, most ESP32 configurations of the Arduino IDE have to be modified. 

For MacOS, go to folder `~/Library/Arduino15/packages/esp32/hardware/esp32/1.0.6` and open `platform.txt`. Search for line

```
recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} {compiler.c.elf.extra_flags} {compiler.libraries.ldflags} -Wl,--start-group {object_files} "{archive_file_path}" {compiler.c.elf.libs} {build.extra_libs} -Wl,--end-group -Wl,-EL -o "{build.path}/{build.project_name}.elf"
```

and change it to

```
recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} {compiler.c.elf.extra_flags} -Wl,--start-group {object_files} "{archive_file_path}" {compiler.c.elf.libs} {compiler.libraries.ldflags} -Wl,--end-group -Wl,-EL -o "{build.path}/{build.project_name}.elf"
```

## Libraries

All libraries (except connctd Marconi) could be installed via the Library Management of Arduino IDE. In the menu bar go to ```Tools->Manage Libraries...``` and install the libraries below: 

| library                 | version        | link |
|:----------------------- |:-------------- | :-------------------|
| Wifi Manager            | 2.0.3-alpha    | https://github.com/tzapu/WiFiManager |
| Fast LED                | 3.4.0          | https://github.com/FastLED/FastLED |
| Adafruit Unified Sensor | 1.1.4          | https://github.com/adafruit/Adafruit_Sensor |
| Adafruit BME280         | 2.1.3          | https://github.com/adafruit/Adafruit_BME280_Library|
| Adafruit SCD30          | 1.0.7          | https://github.com/adafruit/Adafruit_SCD30 |
| Bosh Sensortec          | 1.6.1480       | https://github.com/BoschSensortec/BSEC-Arduino-library |
| connctd Marconi         |                | https://github.com/connctd/marconi-lib |
| Sensirion SPS30         | 1.0.0          | https://github.com/Sensirion/arduino-sps |

### Installing connctd Marconi library

If you do not have git installed on your machine, please open the browser and and go to the [github page](https://github.com/connctd/marconi-lib). Press on ```Code``` and download the ZIP file. Extract the content to your Arduino libraries folder and restart Arduino IDE again. The macroni library shall now be available. 

If you have git installed, navigate to your Arduino IDE libraries folder and run the following command ```git clone https://github.com/connctd/marconi-lib```




## Flashing the device credentials

Each Airlytics ESP device needs to authenticate itself with the backend by using a specific and unique device ID as well as a secret key. This has to be written to the EEProm of the ESP before the firmware will be flahed. In order to write the device credentials to the ESP EEProm, open the file ```Config-Flasher/Config-Flasher.ino```. 

In the setup section of the code, search for the line ```//deviceConfig = (DeviceConfig){ "<device id>", {<device secret in hex bytes>}};```. replace ```<device id>``` with the device ID you want to flash (IDs will be generated by connctd). 

Your device secret has to handled as hex byte array. There are several ways to generate a hex array of the base64 key. An easy way is to copy your secret key, go to [cryptii](https://cryptii.com/pipes/base64-to-hex), paste your key on the left box (If you do not follow the link, choose Decode, Base64 and the RFC 3548 variant ). A byte array will be calculated on the right. Copy the byte array and replace it with ```<device secret in hex bytes>```. Be aware of the C++ syntax for hex declaration (0x). 

Your final line should look like this (no valid id and secret used for the example):

    deviceConfig = (DeviceConfig){ "mku63nztfw0gn043", {0x41, 0xc6, 0x03, 0x2e, 0x15, 0xc2, 0xa8, 0xc4, 0x5f, 0x98, 0x96, 0x99, 0xf4, 0x6d, 0x7f, 0x46, 0x66, 0x14, 0x0a, 0x98, 0xde, 0x18, 0x64, 0xb2, 0xe9, 0x77, 0x55, 0xba, 0xdd, 0x50, 0xf1, 0xcf}};``` 
  
Transfer the code to your ESP. In the Serial Monitor, you'll might see something like: 

    Initialize EEPROM
    generating DeviceConfig object
    DeviceConfig generated
    DeviceID = mku63nztfw0gn043
    Device Key = 41 C6 33 2E ...
    Save device configuration ... OK
    Checking device Id ... OK
    Checking Device Key ... OK

    And done... Bye!

## Flashing the Airlytics firmware

In case you do not have flashed your device with the ```device id``` and ```device secret``` yet, please follow the steps for flashing the device credentials. 

Open the File ```AirLyticsFrame_ESP/AirLyticsFrame_ESP.ino``` and transfer it to your ESP. 

Your device is ready to use. 


## Calibrating the SCD30 manually

The program ```SCD30_Calibration``` allows the manual calibration of SCD30 CO2 Sensors. In general, this sensor has already been calibrated by the manufacturer. However, it may happen that this calibration is no longer valid. 
 
 There are two ways of calibrating the SCD30 with this program
       - place the sensor in an environment (outsite) with a CO2 value around 400ppm, wait until the measured CO2 value stabilizes and press the button 
       - place the sensor in an environment with a known CO2 value, wait until the measured CO2 value stabilizes and send the reference value via Serial. 
 
 The command to force the calibration is ```#cal:<your co2 value>$``` where ```<your co2 value>``` has to be replaced by an integer value 
 representing the reference value for your calibration. Example: in order to calibrate the SCD30 with a reference of 600ppm, you have to
 send ```#cal:600$```.