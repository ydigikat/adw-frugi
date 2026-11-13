## Pinout/IO

Pinout for STM32F411 generic 'blackpill' board. 

| Pin | Peripheral | AF | Signal| Purpose/Notes 
| --- | ------- | -- | ----- | ------| 
| PA1 | GPIO | | LCD D/C | ILI9341| 
| PA2 | GPIO | | LCD RST | |
| PA4 | GPIO | | LCD CS| | |
| PA5 | SPI1 | AF5| SCK | LCD & EEPROM|
| PA6 | SPI1 | AF5|MISO | | 
| PA7 | SPI1 | AF5| MOSI | | 
| PA10| UART1 | AF7| RX | MIDI In| 
| PA0 | GPIO | | EEPROM CS | 25LC256 | 
| PB4 | TIM3 | AF2 | CH1 | Encoder A | 
| PB5 | TIM3 | AF2 | CH2 | Encoder B      | 
| PB6 | GPIO |  |  I | BTN N | 
| PB7 | GPIO | | I | BTN E | 
| PB0 | GPIO | | I | BTN S | 
| PB1 | GPIO | | I | BTN W | 
| PB3 | GPIO | | I | BTN C | 
| PB8 | GPIO | | O | LED Strobe | 
| PB9 | GPIO | | O | LED Fault | 
| PB10 | I2S2 | AF5 | SCK | UDA1334A DAC | 
| PB12 | I2S2 | AF5 | WS | | 
| PB15 | I2S2 | AF5 | SDO | | 

Prototype board wiring colours:

| Pin | Signal|  Wire |
| --- | ----- | ----- |
| PA0 | EEPROM CS | Orange |
| PA1 | LCD D/C |Purple |
| PA2 | LCD RST  | Green |
| PA4 | LCD CS| Yellow | 
| PA5 | SCK |  White | 
| PA6 | MISO |  Blue |
| PA7 | MOSI |  Yellow | 
| PA10| RX |  Green |
| PB4 | CH1 |  Gray | 
| PB5 | CH2 |  Brown |
| PB6 | BTN N | Orange |
| PB7 | BTN E | Green |
| PB0 | BTN S | Yellow |
| PB1 | BTN W | Purple |
| PB3 | BTN C | White |
| PB8 | LED Strobe | White |
| PB9 | LED Fault | Gray |
| PB10 |SCK |  White |
| PB12 |WS | Yellow |
| PB15 |SDO |  Blue |

