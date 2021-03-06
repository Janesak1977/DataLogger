/*
 * Temperature data logger.
 *
 * This module is in charge of receiving and decoding La Crosse technology IT+
 * protocol frames.
 *
 * Gérard Chevalier, Nov 2011
 * IT+ decoding was possible thank to
 *   - The great job done by fred, see here: http://fredboboss.free.fr/tx29/tx29_1.php?lang=en
 *   - And stuff found here: http://forum.jeelabs.net/node/110
 */

#include "DataloggerDefs.h"
#include <RF12.h>

extern void DebugPrint_P(const char *);
extern void DebugPrintln_P(const char *);

extern byte SignalError;
byte CheckITPlusRegistration(byte, byte, byte);

Type_Discovered DiscoveredITPlus[ITPLUS_MAX_DISCOVER];

#define ITPLUS_ID_MASK	0b00111111

Type_Channel ITPlusChannels[ITPLUS_MAX_SENSORS];

/* Initialization of this module */
/* ----------------------------- */
void ITPlusRXSetup()
{
  Serial.println("Initializing IT+");

  for (byte i = 0; i < ITPLUS_MAX_SENSORS; i++)
    ITPlusChannels[i].LastReceiveTimer = 0;
  for (byte i = 0; i < ITPLUS_MAX_DISCOVER; i++)
    DiscoveredITPlus[i].SensorID = 0xff;
}

#define CRC_POLY 0x31
boolean CheckITPlusCRC(byte *msge, byte nbBytes) {
  byte reg = 0;
  short int i;
  byte curByte, curbit, bitmask;
    byte do_xor;
  
  while (nbBytes-- != 0) {
    curByte = *msge++; bitmask = 0b10000000;
    while (bitmask != 0) {
      curbit = ((curByte & bitmask) == 0) ? 0 : 1;
      bitmask >>= 1;
#ifdef DEBUG_CRC
    DebugPrint_P("byte ");printHex(curByte);
#endif
    do_xor = (reg & 0x80);

    reg <<=1;
    reg |= curbit;
#ifdef DEBUG_CRC
    DebugPrint_P(" b=");Serial.print(curbit, DEC);DebugPrint_P(" ");
#endif

    if (do_xor)
    {
#ifdef DEBUG_CRC
    DebugPrint_P(" Xoring ");printHex(reg);
#endif
        reg ^= CRC_POLY;
#ifdef DEBUG_CRC
    DebugPrint_P(" > ");printHex(reg);
#endif
    }

#ifdef DEBUG_CRC
    Serial.print(" reg ");printHex(reg);
    Serial.println();
#endif
    }
  }
  return (reg == 0);
}

void printHex(byte data) {
  if (data < 16)
    Serial.print('0');
  Serial.print(data, HEX);
}

void ProcessITPlusFrame()
{
  byte Temp, DeciTemp, SensorId, ResetFlag, LoBatFlag, Channel, Hygro;

  // Here, there are chance that the frame just received is an IT+ one (flag ITPlusFrame set), but not sure.
  // So, check CRC, and decode if OK.
#ifdef ITPLUS_DEBUG_FRAME
    DebugPrintln_P(PSTR("GotIT+"));
    for(uint8_t i = 0; i < 5; i++) {
      printHex(rf12_buf[i]);
      Serial.print(' ');
    }
    Serial.println();
#endif

  if (CheckITPlusCRC((byte *)rf12_buf, 5))
  {
    // OK, CRC is valid, we do have an IT+ valid frame
    // Signal reception on LED. tbd: enhance this code as it actively take CPU cycle, and with IT+ 4 / 8 s frame pace
    // it's not a good idea...
    //if (SignalError == 0) {  // Otherwise don't touch LED
    //  RX_LED_ON(); delay(150);  // xx tbd: set new algorythm
    //  RX_LED_OFF(); delay(50);
      //RX_LED_ON(); delay(150);
      //RX_LED_OFF();
    //}

    SensorId = ((rf12_buf[0] & 0x0f) << 4) + ((rf12_buf[1] & 0xf0) >> 4) >> 2;
    // Reset flag is stored as bit #6 in sensorID.
    ResetFlag = (rf12_buf[1] & 0b00100000) << 1;
    //LoBat flag is stored as bit #7 in Hygro.
    LoBatFlag = (rf12_buf[3] & 0b10000000);
    // Sign bit is stored into bit #7 of temperature. IT+ add a 40° offset to temp, so < 40 means negative
        Temp = (((rf12_buf[1] & 0x0f) * 10) + ((rf12_buf[2] & 0xf0) >> 4));
        DeciTemp = rf12_buf[2] & 0x0f;
/*
        if (Temp >= 40)
          Temp -= 40;
        else
          Temp = (40 - Temp) | 0b10000000;
*/        
        if (Temp >= 40) {
          Temp -= 40;
        } else {
          if (DeciTemp == 0) {
            Temp = 40 - Temp;
          } else {
            Temp = 39 - Temp;
            DeciTemp = 10 - DeciTemp;
          }
          Temp |= 0b10000000;
        }
    Hygro = rf12_buf[3] & 0x7f;

#ifdef ITPLUS_DEBUG
    Serial.print("Id:"); printHex(SensorId);
    if (ResetFlag)
      Serial.print("R");
    else
      Serial.print(" ");
    Serial.print(" T:");
    if (Temp & 0b10000000)
      Serial.print("-");
    if (Temp < 10) Serial.print("0");
    Serial.print(Temp & 0x7f, DEC); Serial.print("."); Serial.print(DeciTemp, DEC);
    if (Hygro != 106) {
      Serial.print(" H:"); Serial.print(Hygro, DEC); Serial.print("%");
    }
    Serial.println();
#endif
    
    // Process received measures (only if sensor is registered)
    if ((Channel = CheckITPlusRegistration((SensorId | ResetFlag), Temp, DeciTemp, Hygro, LoBatFlag)) != 0xff)
    {
      ITPlusChannels[Channel].Temp = Temp;
      ITPlusChannels[Channel].DeciTemp = DeciTemp;
      ITPlusChannels[Channel].Hygro = Hygro;
      ITPlusChannels[Channel].LoBat = LoBatFlag;
      
    }
  } else {
#ifdef ITPLUS_DEBUG_FRAME
    DebugPrintln_P(PSTR("BadCRC"));
#endif
  }
}

// Find an IT+ ID into the registered IDs table. If found, return the index in table
// Bit 6 of ID is the "Sensor Reseted" indicator, meaning the battery was replaced and a new
// ID was generated. This flag is held on for about 4h30mn, enabling sensor / receiver peering.
// This function should be passed the "raw ID", including the flag (in bit #6) in order to store it
// into the discovered table with the sensor id to distinguish lists in display later on.
// If the ID is not found, the sensor is added to the discovered IDs table (if not already there).
// When ID not found, return 0xff
byte CheckITPlusRegistration(byte id, byte Temp, byte DeciTemp, byte Hygro, byte LoBatFlag) {
  byte i, FreeIndex;
  unsigned int MaxTime;

  for (i = 0; i < ITPLUS_MAX_SENSORS; i++) {
    if (ITPlusChannels[i].SensorID == (id & ITPLUS_ID_MASK)) {  // Do the search without reset flag
      // OK Found, reset receive timer & return channel = index
//      Serial.print("Ch:"); Serial.print(i);
//      Serial.print(", ID:"); Serial.print(ITPlusChannels[i].SensorID);
//      Serial.println();
      ITPlusChannels[i].LastReceiveTimer = SENSORS_RX_TIMEOUT;
      return i;
    }
  }

  // The sensor is not registered, try to find it into discovered table
  for (i = 0; i < ITPLUS_MAX_DISCOVER; i++) {
    if ((DiscoveredITPlus[i].SensorID & ITPLUS_ID_MASK) == (id & ITPLUS_ID_MASK)) {
      // Found! Update the last receive timer
      DiscoveredITPlus[i].LastReceiveTimer = ITPLUS_DISCOVERY_PERIOD;
      DiscoveredITPlus[i].Temp = Temp;
      DiscoveredITPlus[i].DeciTemp = DeciTemp;
      DiscoveredITPlus[i].Hygro = Hygro;
      DiscoveredITPlus[i].LoBat = LoBatFlag;
//      Serial.print("ID:"); Serial.print(DiscoveredITPlus[i].SensorID);
//      Serial.print(" in discovered on "); Serial.print("Chan:"); Serial.print(i);
//      Serial.println();
      // And return "NOT Found"
      return 0xff;
    }
  }
  
  // Not found: insert into a free slot, including the reset flag.
  for (i = 0; i < ITPLUS_MAX_DISCOVER; i++) {	// Find a free slot
    if (DiscoveredITPlus[i].SensorID == 0xff) {
      DiscoveredITPlus[i].SensorID = id;
      DiscoveredITPlus[i].LastReceiveTimer = ITPLUS_DISCOVERY_PERIOD;
      DiscoveredITPlus[i].Temp = Temp;
      DiscoveredITPlus[i].DeciTemp = DeciTemp;
      DiscoveredITPlus[i].Hygro = Hygro;
      DiscoveredITPlus[i].LoBat = LoBatFlag;
//      Serial.print("ID:"); Serial.print(DiscoveredITPlus[i].SensorID);
//      Serial.print(" inserted to "); Serial.print("Chan:"); Serial.print(i);
 //     Serial.println();
      return 0xff;
    }
  }
  
  // No free slot found. Use the one with oldest receiving time.
  MaxTime = ITPLUS_DISCOVERY_PERIOD;
  FreeIndex = 0;
  for (i = 0; i < ITPLUS_DISCOVERY_PERIOD; i++) {
    if (DiscoveredITPlus[i].LastReceiveTimer < MaxTime) {
      MaxTime = DiscoveredITPlus[i].LastReceiveTimer;
      FreeIndex = i;
    }
  }
  DiscoveredITPlus[FreeIndex].SensorID = id;
  DiscoveredITPlus[FreeIndex].LastReceiveTimer = ITPLUS_DISCOVERY_PERIOD;
  DiscoveredITPlus[FreeIndex].Temp = Temp;
  DiscoveredITPlus[FreeIndex].DeciTemp = DeciTemp;
  DiscoveredITPlus[FreeIndex].Hygro = Hygro;
  DiscoveredITPlus[FreeIndex].LoBat = LoBatFlag;
  return 0xff;
}

