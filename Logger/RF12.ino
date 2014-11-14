/*
 * Temperature data logger.
 *
 * This module is in charge of receiving other Jeenodes measures through RF12
 *
 * Gérard Chevalier, Jan 2011
 * Nov 2011, changed to integrate La Crosse IT+ protocol
 */

#include "DataloggerDefs.h"
#include <RF12.h>
#include <Ports.h>

// After IT+ addition into central node, added 2 preamble bytes to identify clearly the "Home Made Sensor" over RF12
#define CHECK1  0x10
#define CHECK2  0x01

extern void DebugPrint_P(const char *);
extern void DebugPrintln_P(const char *);
extern void ProcessITPlusFrame();

extern void rf12_initialize_overide_ITP ();
extern boolean ITPlusFrame;

Type_RF12Channel RF12Channels[MAX_JEENODE];
PayloadTX RF12Payload;

void RF12Init()
{
  Serial.print("Initalizing RFM12...");
  rf12_initialize(NODEID, RF12_868MHZ, networkGroup);
  Serial.println(" OK");
    
  // Overide settings for RFM01/IT+ compliance
  rf12_initialize_overide_ITP();
}

void CheckRF12Recept()
{
  if (rf12_recvDone())
  {
    DebugPrintln_P(PSTR("RF12_Rcvd"));
    // If a "Receive Done" condition is signaled, we can safely use the RF12 library buffer up to the next call to
    // rf12_recvDone: RF12 tranceiver is held in idle state up to the next call.
    // Is it IT+ or Jeenode frame ?
    if (ITPlusFrame)
      ProcessITPlusFrame();  // Keep IT+ logic outside this source files
    else
    {
      if ((rf12_crc == 0) && (rf12_hdr & RF12_HDR_CTL) == 0)  // Valid RF12 Jeenode frame received
      {  
      #ifdef RF12_DEBUG
        DebugPrint_P(PSTR("RF12 rcv: "));
        for (byte i = 0; i < rf12_len; ++i)
        {
          Serial.print(rf12_data[i], HEX); Serial.print(' ');
        }
        Serial.println();
      #endif
      
        // RF12 frame format: Word(bytes0-1) = Power, Long(bytes2-5) = Energy
        int node_id = (rf12_hdr & 0x1F);
        RF12Payload = *(PayloadTX*) rf12_data;

       #ifdef RF12_DEBUG 
        Serial.print("NodeID: "); Serial.println(node_id);        
        Serial.print(RF12Payload.power);Serial.print(" "); Serial.println(RF12Payload.pulses);
       #endif
       
        if (node_id >= JEEDNODE_ENERGY_MON_NODEID_START && node_id <= JEEDNODE_ENERGY_MON_NODEID_END)  //5-10 - Energy monitoring nodes
        {
          if (RF12Channels[node_id - JEEDNODE_ENERGY_MON_NODEID_START].Enabled)
          {
            RF12Channels[node_id - JEEDNODE_ENERGY_MON_NODEID_START].Power = RF12Payload.power;
            RF12Channels[node_id - JEEDNODE_ENERGY_MON_NODEID_START].Energy += (RF12Payload.pulses*WhpPulse);
            RF12Channels[node_id - JEEDNODE_ENERGY_MON_NODEID_START].LastReceiveTimer = SENSORS_RX_TIMEOUT;
            //Serial.print(RF12Channels[node_id - JEEDNODE_ENERGY_MON_NODEID_START].Power);Serial.print(" "); Serial.println(RF12Channels[node_id - JEEDNODE_ENERGY_MON_NODEID_START].Energy);
          }
        }
      }
    }
  }
}
   
   
