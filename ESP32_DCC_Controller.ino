// Visual Micro is in vMicro>General>Tutorial Mode
// 
/*
    Name:       ESP32_DCC_Controller.ino
    Created:	15/12/2021 5:37:09 PM
    Author:     TOSHIBA\Julian

	//https://github.com/espressif/arduino-esp32/issues/1101
	//known problem ESPAsyncWebServer - it will crash on the ESP32

	//https://github.com/me-no-dev/ESPAsyncWebServer/issues/324
	//https://www.mischianti.org/2020/11/02/web-server-with-esp8266-and-esp32-multi-purpose-generic-web-server-3/
	//https://randomnerdtutorials.com/esp32-web-server-arduino-ide/ can use WiFi.h and Wifiserver
	//https://github.com/espressif/arduino-esp32/blob/master/libraries/WiFi/examples/SimpleWiFiServer/SimpleWiFiServer.ino  i doubt its multiclient


	create a class dccChannel, will be called during the int timer cycle. means it must be loaded in IRAM

*/


//2023-06-12 compile error  #include <hwcrypto/sha.h> is not found
//https://github.com/hardkernel/ODROID-GO/issues/24
//https://github.com/fhessel/esp32_https_server/issues/143
//To fix, find websockets.cpp and in it replace  <hwcrypto/sha.h>
//with <sha/sha_parallel_engine.h> 

/* apparently this replacement should also work in websockets.cpp
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL (4, 4, 0)
#include <sha/sha_parallel_engine.h>  
#else
#include <hwcrypto/sha.h>
#endif
*/


//#include "clsJog.h"
#include <WebSockets.h> //this was added for websockets
#include <WebSocketsClient.h> //this was added for websockets
#include <WebSocketsServer.h>  //this was added for websockets
#include "DCCcore.h"
#include "DCClayer1.h"
#include "DCCweb.h"
#include "WiThrottle.h"



//PIN ASSIGNMENTS - see global.h


uint16_t secCount;
bool    bLED;
//debugTimer dTimer;   // has to be declared here only


void setup() {
	Serial.begin(115200);
	Serial.println(F("\n\nBoot DCC ESP32"));
	trace(Serial.println(F("trace enabled"));)

		//dTimer.setBuffer(&DCCpacket);   //debug

		

	
		//2021-10-19 the unit can operate in DCC mode, or in DC mode pwm which supports a single loco, loco 3 with 28 speed steps
		//enable or disable the DC block as required in Global.h  DCC and DC are mutually exclusive

#ifdef	DC_PINS
//If DC_PINS is defined, this overrides DCC and we will create a DC system.  Entirel optional. If you want 
//a DCC system, then comment out or delete the DC_PINS definition in Global.h for the board you are using
//analogSetAttenuation() can be used to pre-scale the AD input, default is 11dB, allowing FSD of 2.6v

DC_PINS

#elif defined DCC_PINS
//we expect to find DCC_PINS defined
DCC_PINS



#else
//need to define at least DCC_PINS, else we throw a compile time error.
#error "DCC_PINS or DC_PINS must be defined.  Neither is."
#endif

	DCCcoreBoot();

	//restore settings from EEPROM
	dccGetSettings();

	pinMode(PIN_HEARTBEAT, INPUT_PULLUP); //D0 with led

#ifdef _JOGWHEEL_h
	nsJogWheel::jogInit();
#endif
	

#ifdef _DCCWEB_h		
	nsDCCweb::startWebServices();
#endif


	/*2020-04-02 start WiThrottle protocol*/
#ifdef _WITHROTTLE_h
	nsWiThrottle::startThrottle();
#endif


} //end boot




void loop() {
	//dTimer.doLoop();

#ifdef _DCCWEB_h
	nsDCCweb::loopWebServices();   // constantly check for websocket events

	/*2020-05-03 new approach to broadcasting changes over all channels.
	A change can occur on any comms channel, might be WiThrottle, Websockets or the local hardware UI
	Flags are set in the loco and turnout objects to denote the need to broadcast.
	These flags are cleared by the local UI updater as the last in sequence*/

	nsDCCweb::broadcastChanges();
#endif

#ifdef _JSONTHROTTLE_h
	nsJsonThrottle::broadcastJSONchanges(false);
#endif

#ifdef _WITHROTTLE_h
	nsWiThrottle::broadcastChanges(false);
#endif

	//broadcast turnout changes to line and clear the flags
	updateLocalMachine();

	//call DCCcore once per loop. We no longer use the return value
	DCCcore();


	if (quarterSecFlag) {
		/*isQuarterSecFlag will return true and also clear the flag*/
		quarterSecFlag = false;

		//2021-01-29 call processTimeout every 250mS to give better resolution over timeouts
#ifdef _WITHROTTLE_h
		nsWiThrottle::processTimeout();
#endif

		secCount++;
		if (secCount >= 8) {
			/*toggle heartbeat LED, every 2s*/
			bLED = !bLED;
			secCount = 0;

			//send power status out to web sockets
#ifdef _DCCWEB_h
			nsDCCweb::broadcastPower();
#endif

#ifdef _JSONTHROTTLE_h
			/*transmit power status to websocket*/
			nsJsonThrottle::broadcastJsonPower();
#endif

#ifdef _WITHROTTLE_h
			/*transmit power status to WiThrottles*/
			//2020-11-28 no need to do this so frequently
			//nsWiThrottle::broadcastWiPower();
#endif
		}


	}//qtr sec

}
