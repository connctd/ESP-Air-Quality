# ESP-Air-Quality

# Dependencies

## Board Management

In order to install ESP32 extension to the Arduino IDE, add the line ```https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json``` to the additional board manager URL in the Arduino Settings. 

Open the Board Management and search for ```esp32```. Choose the version ```1.0.6``` developed by Espressif Systems and hit install. 


## Libraries

| library            | version            | link |
|:------------------ |:-------------------| :-------------------|
| Wifi Manager            | 2.0.3-alpha   | https://github.com/tzapu/WiFiManager |
| Fast LED                | 3.4.0         | https://github.com/FastLED/FastLED |
| Adafruit Unified Sensor | 1.1.4         | https://github.com/adafruit/Adafruit_Sensor |
| Adafruit BME280         | 2.1.3         | https://github.com/adafruit/Adafruit_BME280_Library|
| Bosh Sensortec          | 1.6.1480      | https://github.com/BoschSensortec/BSEC-Arduino-library |
| connctd Marconi         |               | https://github.com/connctd/marconi-lib |

# Error Handling

## Error Color Codes

Errors could be identified by a red blinking LED Ring. Either the complete ring is blinking in red, or the gauge only (half of the ring). Before the blinking starts, a color idicates the error type.



### Complete Ring

In general, a complete red blinking LED-ring idicates connection or system problems. 

| color    | error            |
|:-------- |:-------------------|
| white    | Error not further specified |
| blue     | Error has to do with Wifi Connection|
| orange   | Maroni Client could not be initialized. IP address could not resolved from URL or kind like that. |
| violette | Marconi Session could not be establihed |
| red      | EEPROM failure |


### Gauge Only
In contrast to complete red blinking LED-rings, a half blinking LED ring indicates Errors related to the connected Sensors.


| color    | error            |
|:-------- |:-------------------|
| white    | Error not further specified |
| yellow   | ERROR BME280 |
| orange   | Error BME680 |
| violette | Error BSEC library |


## ESP compiler error

For MacOS, go to folder ```~/Library/Arduino15/packages/esp32/hardware/esp32/1.0.6``` and open ```platform.txt```. Search for line

		recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} {compiler.c.elf.extra_flags} {compiler.libraries.ldflags} -Wl,--start-group {object_files} "{archive_file_path}" {compiler.c.elf.libs} {build.extra_libs} -Wl,--end-group -Wl,-EL -o "{build.path}/{build.project_name}.elf"

and change it to 

		recipe.c.combine.pattern="{compiler.path}{compiler.c.elf.cmd}" {compiler.c.elf.flags} {compiler.c.elf.extra_flags} {compiler.libraries.ldflags} -Wl,--start-group {object_files} "{archive_file_path}" {compiler.c.elf.libs} {compiler.libraries.ldflags} {build.extra_libs} -Wl,--end-group -Wl,-EL -o "{build.path}/{build.project_name}.elf"

