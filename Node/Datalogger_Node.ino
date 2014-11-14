// Date and time functions using a DS1307 RTC connected via I2C and Wire lib
 
#include <RF12.h>
#include "kWh.h"
#include <Wire.h>
#include <Time.h>
#include <avr/wdt.h>



// *************************************************************
// Version Number
//**************************************************************
#define VersionMAJOR 1
#define VersionMINORHi 1
#define VersionMINORLo 3



/*Recommended node ID allocation
------------------------------------------------------------------------------------------------------------
-ID-	-Node Type- 
0	- Special allocation in JeeLib RFM12 driver - reserved for OOK use
1-4     - Control nodes 
5-10	- Energy monitoring nodes
11-14	--Un-assigned --
15-16	- Base Station & logging nodes
17-30	- Environmental sensing nodes (temperature humidity etc.)
31	- Special allocation in JeeLib RFM12 driver - Node31 can communicate with nodes on any network group
-------------------------------------------------------------------------------------------------------------
*/

const long logInterval = 5; // the interval in seconds between each log

const int  dataLedPin    =  4;  //LED indicating sensor data is received

const int logInterrupt = 1; // ATmega 168 and 328 - interrupt 0 = pin 2, 1 = pin 3
const int interruptPin = 3;


time_t nextTrigger;

//Number of pulses, used to measure energy.
long pulseCount = 0;  

//Used to measure power.
unsigned long pulseTime,lastTime;

//power and energy
double power;
uint32_t elapsedkWh;

boolean PulseOccur;

//Number of pulses per kWh - found or set on the meter.
uint16_t ppkWh = 1600; //1600 pulses/kwh

extern void rf12_initialize_overide_ITP ();

 
void setup ()
{
    pinMode(dataLedPin, OUTPUT);    // LED interrupt indicator initialization
    
    pinMode(interruptPin, INPUT);
    digitalWrite(interruptPin, HIGH);
    
    Serial.begin(115200);
    Wire.begin();
    Serial.print("NodeID: "); Serial.println(NODEID);
    Serial.println("Init RF12");
    rf12_initialize(NODEID, RF12_868MHZ, networkGroup);
    // Overide settings for RFM01/IT+ compliance
    rf12_initialize_overide_ITP();
    wdt_enable(WDTO_2S);
    Serial.println("Node ready");
    Serial.print("Version: ");Serial.print(VersionMAJOR);Serial.print(".");Serial.print(VersionMINORHi);Serial.println(VersionMINORLo);
    PulseOccur = false;
    power = 0;
    elapsedkWh = 0;
    // KWH interrupt attached to IRQ 1  = pin3
    attachInterrupt(logInterrupt, onPulse, FALLING);
 
    // following line sets the RTC to the date & time this sketch was compiled
//    RTC.adjust(DateTime(2013,7,4,8,58,0));
  
}
 
void loop ()
{
  wdt_reset();
  time_t timeStamp = now();
  // Note that even if you only want to send out packets, you still have to call rf12 recvDone periodically, because
  // it keeps the RF12 logic going. If you don't, rf12_canSend() will never return true.
  rf12_recvDone();  
  if( timeStamp >= nextTrigger)
  {
    payload_t packet;
    packet.Power = (uint16_t) power;
    if (rf12_canSend())
    {
       packet.Energy = pulseCount;   
       rf12_sendStart(0, &packet, sizeof packet);
       Serial.println("Packet sended.");
    }
        
    pulseCount = 0;
    Serial.println(power);
    nextTrigger += logInterval;// schedule the next log time        
  }
  
  if (digitalRead(interruptPin)== LOW ) // Light the LED when data pin is LOW 
    digitalWrite(dataLedPin, HIGH);    
  else
    digitalWrite(dataLedPin, LOW);        
 
}


// The interrupt routine
void onPulse()
{

//used to measure time between3.6 pulses.
  lastTime = pulseTime;
  pulseTime = micros();

//pulseCounter
  pulseCount++;

//Calculate power
  power =(3600000000.0 / (pulseTime-lastTime))/ (ppkWh/1000.0);
  
}
