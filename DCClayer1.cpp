// 
// 
// 

#include "DCClayer1.h"

//debug special.  system crashes and reboots if you read the /hardware.htm page
//but is this an ISR conflict?  Will replace ISR with a dummy class that emulates the flags
//but is based on a timer.   issue is with ESPAsyncWS - known to have crash problems.
//
//structs can have methods. https://stackoverflow.com/questions/13125944/function-for-c-struct







//2021-12-17 ported to the ESP32

/*DCClayer1 puts a DCC signal on the track.  It will continuously write the DCCbuffer to the track
The routine also sets a msTickFlag every 10mS which the main loop can use for general timing such as
keyboard scans.

I don't see the need to operate a separate prog track from the main. If we want this, we need two channels
each with their own TX buffer, and the current/voltage trip for prog track needs to be separate to main.

For the first release, I will support only one channel, one packet buffer.

https://esp32.com/viewtopic.php?t=12621
https://www.visualmicro.com/page/Timer-Interrupts-Explained.aspx
need portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
https://arduino.stackexchange.com/questions/63062/reading-inside-critical-sections-from-isr
effectively portMux will disable the int so that it cannot write to a struct that main is reading or modifying
for this project, we sync between the two using a CTS flag so I don't think it necessary to lock out the ISR
when we are preparing a new DCC packet.  that is, unless the RTOS runtime crashes with a cache access error.

per vMicro site, reading an isr variable in the main is ok if its atomic, writing definately needs a lock in the main
and in the isr
https://esp32.com/viewtopic.php?t=5553  advanced

https://www.learncpp.com/cpp-tutorial/class-code-and-header-files/
*/

	volatile DCCBUFFER DCCpacket;  //externally visible
	volatile DCCBUFFER _TXbuffer;   //internal to this module

	//ESP32 uses pairs of 32 bit registers
	static uint32_t dcc_mask = 0;
	static uint32_t dcc_maskInverse = 0;
	static uint32_t enable_mask = 0;
	static uint32_t enable_maskInverse = 0;
	
	static uint8_t _ticks=0;  //ticks run at 58uS


	enum DCCbit { DCC_ONE_H, DCC_ONE_L, DCC_ZERO_H1, DCC_ZERO_H2, DCC_ZERO_L1, DCC_ZERO_L2 };
	static enum DCCbit DCCperiod = DCC_ONE_H;

	//DCC layer 1 
	volatile uint8_t  TXbyteCount;
	volatile uint8_t  TXbitCount;

	/*Interrupt handler ffor dcc
	  for a dcc_zero or dcc_one the reload periods are different.  We queue up the next-bit in the second half of the bit currently being transmitted
	  Some jitter is inevitable with maskable Ints, but it does not cause any problems with decooding in the locos at present.
	  The handler will work its way through the TXbuffer transmitting each byte.  when it reaches the end, it sets the byte pointer to zero
	  and sets the bitcounter to 22 indicating a preamble.  at this point it clears the DCCclearToSend flag to lock the DCCpacket buffer from writes for the next
	  12 preamble bits this is so that the main prog loop routines have long enough to finish writing to the DCCpacket  if they had just missed the flag clearing.
	  Once preamble is transmitted, the handler will copy the DCCpacket to the transmit buffer and set the DCCclearToSend flag indicating it is able to accept
	  a new packet.  If the DDCpacket is not modified by the main loop, this layer 1 handler will continuously transmit the same packet to line.  This is useful
	  as it allows an idle to be continuously transmitted when we are in Service Mode for example.
	*/

	/* create a hardware timer and a portMux*/
	hw_timer_t * timer = NULL;
	portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;



/*2021-12-27 gave up on writing an object for dccChannel.  Its possible, but it seems ESPAsyncWebServer is the cause
of our crashes on the ESP32 port, and until this is fixed by the dev, there's not much point continuing with this project
as the ESP12 works just fine.

Note, structs can have methods, so that's a handy way to encapsulate a clearflag method which uses portMux rather than
writing a class.

*/



void ICACHE_RAM_ATTR onTimer0() {
		
		//called every 58uS.  ONE bits have equal 58uS half cycles.  ZERO bits are implemented here as double-period
		//of 116 half cycles
		switch (DCCperiod) {
		case DCC_ZERO_H1:
			GPIO.out_w1ts = dcc_mask;  //set bits to logic 1
			GPIO.out_w1tc = dcc_maskInverse;  //set bits to logic 0
			break;
		case DCC_ZERO_L1:
			GPIO.out_w1tc = dcc_mask;  //set bits to logic 0
			GPIO.out_w1ts = dcc_maskInverse;  //set bits to logic 1
			break;
		case DCC_ONE_H:
			GPIO.out_w1ts = dcc_mask;  //set bits to logic 1
			GPIO.out_w1tc = dcc_maskInverse;  //set bits to logic 0
			break;
		case DCC_ONE_L:
			GPIO.out_w1tc = dcc_mask;  //set bits to logic 0
			GPIO.out_w1ts = dcc_maskInverse;  //set bits to logic 1
			break;
		
		default:
				//maintain output levels for second half of the zero bit: H2 or L2
		break;
		}

		
		//we asserted the existing bit state above, now queue up the next bit state
		switch (DCCperiod) {
		case DCC_ONE_H:
			DCCperiod = DCC_ONE_L;   //queue up second part of the one bit
			break;

		case DCC_ZERO_H1:
			DCCperiod = DCC_ZERO_H2;   //queue up second part of the zero bit
			break;
		case DCC_ZERO_H2:
			DCCperiod = DCC_ZERO_L1;  //zero-hi period complete, queue up 1st part of low
			break;
		case DCC_ZERO_L1:
			DCCperiod = DCC_ZERO_L2;   //queue up second part of the zero bit
			break;
		

		//default will see ONE_L, and ZERO_L2
		default:
			/*if executing the low (and final) part of a DCC zero or DCC one, then advance bit sequence and queue up next bit */
			DCCperiod = DCC_ONE_H;  //default
			if (TXbitCount == 21) { 
				portENTER_CRITICAL_ISR(&timerMux);
				DCCpacket.clearToSend = false;
				portEXIT_CRITICAL_ISR(&timerMux);
			}

			if (TXbitCount == 9) {
				/*copy DCCpacket to TXbuffer. memcpy woould be slower than direct assignment
				immediately after data is copied, set DCCclearToSend which flags to DCCcore module that a new
				DCCpacket may be written
				2019-12-05 increased to 6 packet buffer with copy-over*/

				portENTER_CRITICAL_ISR(&timerMux);
				_TXbuffer.data[0] = DCCpacket.data[0];
				_TXbuffer.data[1] = DCCpacket.data[1];
				_TXbuffer.data[2] = DCCpacket.data[2];
				_TXbuffer.data[3] = DCCpacket.data[3];
				_TXbuffer.data[4] = DCCpacket.data[4];
				_TXbuffer.data[5] = DCCpacket.data[5];
				_TXbuffer.packetLen = DCCpacket.packetLen;
				_TXbuffer.longPreamble = DCCpacket.longPreamble;
				
				TXbyteCount = 0;
				
				DCCpacket.clearToSend = true;
				portEXIT_CRITICAL_ISR(&timerMux);
			}
			if (TXbitCount <= 8) {
				if (TXbitCount == 8)
				{
					//8 is a start bit, or a preamble
					if (TXbyteCount == _TXbuffer.packetLen)
						//2020-06-08 end of a packet, it is now, and prior 
						//to preamble for next packet that we assert a RailCom cutout

					{
						if (DCCpacket.longPreamble)
						{
							TXbitCount = 32;
						}  //long peamble 24 bits
						else
						{
							TXbitCount = 22;
						}  //Preamble 14 bits

						TXbyteCount = 0;
					}
					else
					{
						DCCperiod = DCC_ZERO_H1; //queue up a zero separator
					}
				}
				else
				{
					/*must be 7-0, queue up databit*/
					if ((_TXbuffer.data[TXbyteCount] & (1 << TXbitCount)) == 0)
					{//queue a zero
						DCCperiod = DCC_ZERO_H1; //queue up a zero
					}

					/*special case 0, assert bit but set bit count as 9 as it immediatley decrements to 8 on exit*/
					if (TXbitCount == 0) { TXbyteCount++; TXbitCount = 9; }
				}
			}
			TXbitCount--;
		}

		//one millisecond fast tick flag
		portENTER_CRITICAL_ISR(&timerMux);
		DCCpacket.fastTickFlag = ((++_ticks % 17) == 0) ? true : false;

		//ten millisecond flag  
		if (_ticks >= 172) {
			_ticks = 0;
			DCCpacket.msTickFlag = true;
			GPIO.out_w1ts = DCCpacket.trackPower ? enable_mask : enable_maskInverse;
			GPIO.out_w1tc = DCCpacket.trackPower ? enable_maskInverse : enable_mask;	
		}
		portEXIT_CRITICAL_ISR(&timerMux);
	}


	//Initialisation. call repeatedly to activate additional DCC outputs
	void ICACHE_FLASH_ATTR dcc_init(uint8_t dcc_pin, uint8_t enable_pin, bool phase, bool invert)
	{
		//load with an IDLE packet
		DCCpacket.data[0] = 0xFF;
		DCCpacket.data[1] = 0;
		DCCpacket.data[2] = 0xFF;
		DCCpacket.packetLen = 3;

		//set as outputs
		pinMode(dcc_pin, OUTPUT);
		pinMode(enable_pin, OUTPUT);

		//debug, do not establish a timer
		return;


		//only support GPIO 31 or lower
		if ((dcc_pin > 32)||(enable_pin>32)) {
			Serial.println(F("dcc GPIO pin error"));
			return;
		}

		//note that a particular mask will remain zero'd if its not required
		//e.g. mask and its inverse won't both be active
		//set mask, invert if antiphase required
		if (phase) {
			dcc_mask |= (1 << dcc_pin);
		}
		else {
			dcc_maskInverse |= (1 << dcc_pin);
		}

		if (invert) {
			enable_maskInverse |= (1 << enable_pin);
		}
		else {
			enable_mask |= (1 << enable_pin);
		}

		
		//Use timer 0
		/* 1 tick take 1/(80MHZ/80) = 1us so we set divider 80 and count up */
		timer = timerBegin(0, 80, true);

		/* Attach onTimer function to our timer */
		timerAttachInterrupt(timer, &onTimer0, true);

		/* Set alarm to call onTimer function every second 1 tick is 1us
		=> 1mS second is 1000us.  we need 58uS dcc half-0 period */
		/* Repeat the alarm (third parameter) */
		timerAlarmWrite(timer, 58, true);

		/* Start an alarm */
		timerAlarmEnable(timer);

		
		
	}


	


	