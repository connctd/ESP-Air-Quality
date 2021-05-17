# Pin Usage

## LEDs

* DATA IO25 (I2S available on this pin to make handling WS2812 etc. easier)
* U3 can be used to switch level shifter and direct connect to IO25 for the LEDs

## BME680

* SDA IO21
* SCL IO22
* SD0 IO19
* CS IO18

## SCD30

* SDA IO21
* SCL IO22
* RDY IO15
* SEL IO4

## User Button

* Button IO14 (pulling low with 10k resistor, debounced with 100nF and 1k)

## Board LEDs

* LED1 RX for the serial programmer
* LED2 TX for the serial programmer
* LED3 power indicator for USB-VCC

## GPIO

Contains 9 additional GPIO, I2C, GND, 3.3V and 5V marked on the board
