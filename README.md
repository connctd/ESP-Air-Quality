# AirLytics Frame ESP

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

# Dependencies 

## Board Management

In order to install ESP32 extension to the Arduino IDE, add the line ```https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json``` to the additional board manager URL in the Arduino Settings. 

Open the Board Management and search for ```esp32```. Choose the version ```1.0.6``` developed by Espressif Systems and hit install. 

## Libraries

| library                 | version        | link |
|:----------------------- |:-------------- | :-------------------|
| Wifi Manager            | 2.0.3-alpha    | https://github.com/tzapu/WiFiManager |
| Fast LED                | 3.4.0          | https://github.com/FastLED/FastLED |
| Adafruit Unified Sensor | 1.1.4          | https://github.com/adafruit/Adafruit_Sensor |
| Adafruit BME280         | 2.1.3          | https://github.com/adafruit/Adafruit_BME280_Library|
| Adafruit SCD30		  |	1.0.7		   | https://github.com/adafruit/Adafruit_SCD30 |
| Bosh Sensortec          | 1.6.1480       | https://github.com/BoschSensortec/BSEC-Arduino-library |
| connctd Marconi         |                | https://github.com/connctd/marconi-lib |
| Sensirion SPS30		  | 1.0.0          | https://github.com/Sensirion/arduino-sps |

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


## ESP compiler error

For MacOS, go to folder `~/Library/Arduino15/packages/esp32/hardware/esp32/1.0.6` and open `platform.txt`. Search for line

```
recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} {compiler.c.elf.extra_flags} {compiler.libraries.ldflags} -Wl,--start-group {object_files} "{archive_file_path}" {compiler.c.elf.libs} {build.extra_libs} -Wl,--end-group -Wl,-EL -o "{build.path}/{build.project_name}.elf"
```

and change it to

```
recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} {compiler.c.elf.extra_flags} -Wl,--start-group {object_files} "{archive_file_path}" {compiler.c.elf.libs} {compiler.libraries.ldflags} -Wl,--end-group -Wl,-EL -o "{build.path}/{build.project_name}.elf"
```
