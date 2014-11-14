/*
 * Temperature data logger.
 *
 * Global Includes
 *
 * Gérard Chevalier, Jan 2011
 * Nov 2011, changed to integrate La Crosse IT+ protocol
 */
#include <arduino.h>

// IT+ Decoding debug flags
//#define ITPLUS_DEBUG_FRAME
//#define ITPLUS_DEBUG

// Network debuging flag
#define DEBUG_ETH 0 // set to 1 to show incoming requests on serial port
#define DEBUG_DNS 0
#define DEBUG_HTTP 0

// RF12 transmissions debuging flag
//#define RF12_DEBUG

// Energy meter constant
#define ppkWh   1600
#define WhpPulse  0.625  //(1/ppkWh)/1000
// Total number of tries for sending measures over the Web
#define MAX_WEB_RETRY		3
// xx comments
#define MAX_WEB_BAD_PERIOD	2
//#define BOX_REBOOT_IDLE		0
//#define BOX_REBOOT_INIT		1
//#define BOX_REBOOT_DOWN		2

#define SENSORS_RX_TIMEOUT 5

#define NETWORKSENDPERIOD  15    //Network Packet sending period [s] 

#ifndef DATALOGGERDEFS

#define ITPLUS_MAX_SENSORS  4
#define ITPLUS_MAX_DISCOVER  4
#define ITPLUS_DISCOVERY_PERIOD 255

#define NODEID 1            //becose we are receiving IT+ sensors too.
#define networkGroup 0xD4   //becose we are receiving IT+ sensors too.
#define MAX_JEENODE 5
#define JEEDNODE_ENERGY_MON_NODEID_START  5
#define JEEDNODE_ENERGY_MON_NODEID_END  10

#define BLINKTIME  70L    //Blinking time in [ms]

// remote website name
char website[] PROGMEM = "emoncms.org";

// Set to your account write apikey 
char apikey[] = "49574af50bf3c24edbdeb840180c94f2";


// Structure in RAM holding configuration, as stored in EEPROM
typedef struct {
    byte LocalIP[4];    // Keep those 3 parameters at the begining
    byte RouterIP[4];   // for the hard reset config
    byte ServerIP[4];   // working
    byte WebSendPeriod;
    byte ITPlusID[ITPLUS_MAX_SENSORS];  // IT+ Sensors ID for Registered sensors
} Type_Config;

#define DNS_INIT	0
#define DNS_WAIT_ANSWER	1
#define DNS_GOT_ANSWER	2
#define DNS_NO_HOST	3

// Radio Sensor structure (both RF12 & IT+)
typedef struct
{ 
  bool Enabled;
  byte LastReceiveTimer;
  uint16_t Power;     // actual power consumption
  double Energy;      // counted energy for period
} Type_RF12Channel;

typedef struct
{
  uint16_t power;    // received actual power consumption
  uint32_t pulses;   // received number of counted pulses
} PayloadTX;         // neat way of packaging data for RF comms


// Radio Sensor structure (IT+)
typedef struct
{
  byte SensorID;
  byte LastReceiveTimer;
  byte Temp, DeciTemp;
  byte Hygro;
  byte LoBat;
} Type_Channel;


// Radio Sensor structure for IT+ Sensors discovery process
typedef struct {
  byte SensorID;
  byte LastReceiveTimer;
  byte Temp, DeciTemp;
  byte Hygro;
  byte LoBat;
} Type_Discovered;

#define RX_LED_ON()   ((PORTC |=  (1<<PORTC3)))
#define RX_LED_OFF()  ((PORTC &= ~(1<<PORTC3)))

#define PARAM_RESET_DATA_PORT  PORTD
#define PARAM_RESET_PIN_PORT   PIND
#define PARAM_RESET_PIN_NB     6

#define DATALOGGERDEFS
#endif

