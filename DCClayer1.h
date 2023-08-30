// DCClayer1.h
// 2021-10-14 modified to support DC pwm operation, i.e. non DCC. In this mode it responds only to loco 3
// DC mode is selected in the .INO setup routine


//NEEDS A REWRITE TO SUPPORT ESP32 AND WE ALSO INTEND TO PUT THIS IN CORE 1

#ifndef _DCCLAYER1_h
#define _DCCLAYER1_h

#if defined(ARDUINO) && ARDUINO >= 100
#include "arduino.h"
#else
#include "WProgram.h"
#endif



/*longest DCC packet is 6 bytes when using POM and long address*/
/*2021-10-19 trackPower now controlled from DCClayer1*/
	struct DCCBUFFER {
		uint8_t data[6];
		uint8_t packetLen;
		bool  clearToSend;
		bool  longPreamble;
		bool  msTickFlag;
		bool  trackPower;
		bool  fastTickFlag;

	};

	//decbugTimer class used for testing.  Removes the ISR timer0 and uses only a basic millis() difference timer
	class debugTimer {
	private:
		ulong _ticks = millis();
		 volatile DCCBUFFER *dccBuff;  //pointer

	public:
		//https://www.programiz.com/cpp-programming/structure-pointer
		void setBuffer(volatile DCCBUFFER *d) {
			dccBuff = d;
		}

		void doLoop() {
			if (millis() - _ticks < 10) return;
			_ticks = millis();
			//copy the buffer
			dccBuff->clearToSend = true;
			dccBuff->msTickFlag = true;
			dccBuff->fastTickFlag = true;
	}

	} ;



	/*2021-11-25 fastTickFlag added, this runs at 1mS and is used for analog detection of ACK pulse in service mode*/
	extern volatile DCCBUFFER DCCpacket;
	extern portMUX_TYPE timerMux;
	
	
	
	void ICACHE_FLASH_ATTR dcc_init(uint8_t dcc_pin, uint8_t enable_pin, bool phase, bool invert);

#endif
