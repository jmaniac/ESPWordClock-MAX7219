A Word Clock using the MAX7219 to drive an 8x8 LED Matrix.

The word clock 8x8 LED matrix module to shine light through a word mask printed on paper. The clock face (word matrix) for the clock can be found in the doc folder of this download (Microsoft Word document and PDF versions). 

Additional hardware required is RTC clock module (DS3231 used here) and a momentary-on switch (tact switch or similar).

More information on the Word Clock can be found in the blog article at the [Arduino++ blog](https://arduinoplusplus.wordpress.com/2016/04/28/max7219-led-matrix-module-mini-word-clock/)

This fork is for Wemo D1 Mini ESP8266 one. The Wemo D1 mini and battery shield can be hidden under a single 8x8 MAX7219 Module

For now this fork uses the MD_MAX72xx libraries found on this site.

TODOs
* Add button support
* Deep sleep and wakeup support
* Optional RTC addon to reduce wifi calls
* KISS, Do not bloat ;)