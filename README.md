# ESP32_DCC_Controller

Preliminary.  This is a port of a prior version of the ESP8266 controller.
Its old, and does not use LittleFS.
I abandoned it because the Async library on the 32 causes crashes, but you are welcome to browse the code.

The DCC signal is generated in the layer 1 block.  It works based on a fixed timebase and uses multiples of this to generate a 1 or a 0.
This differs from the ESP8266 version which re-sets a timer on every half-bit transition.
That said, this version using a fixed time base is a dead end, because it's not possible to support a railcom cutout using this method.
