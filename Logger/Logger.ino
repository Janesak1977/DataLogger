/*
 * Temperature data logger.
 *
 * Main module
 *
 * Gérard Chevalier, Jan 2011
 * Nov 2011, changed to integrate La Crosse IT+ protocol
 */
// After moving from Arduino 18 to 23, had to add this include here because when compiling, include order seems
// to have changed. Strange!

#include <EtherCard.h>
#include "DataloggerDefs.h"
#include <avr/pgmspace.h>
#include <avr/wdt.h>

// *************************************************************
// Version Number
//**************************************************************
#define VersionMAJOR 1
#define VersionMINORHi 4
#define VersionMINORLo 0


#define ITPLUS_DEBUG_FRAME
//#define ITPLUS_DEBUG
//#define RF12_DEBUG
//#define DEBUG_ETH 1


extern void RF12Init();
extern void NetworkInitialize();
extern void WebSend();
extern void CheckRF12Recept();
extern void DebugPrint_P(const char *);
extern void DebugPrintln_P(const char *);
extern void CheckProcessBrowserRequest();


extern Type_Channel ITPlusChannels[];
extern Type_RF12Channel RF12Channels[];
extern Type_Discovered DiscoveredITPlus[];
extern Type_Config Config;
extern word LastServerSendOK;
byte RespRcvd = 0;

// SignalError tells if an error has to be signaled on LED, 0=No, 1&2=yes, 2 values for blinking
byte SignalError = 1;
boolean ErrorCondition = false;

unsigned long previousMillis = 0;

unsigned long prevblinkMillis = 0;

unsigned long TimeoutMillis = 0;

// Most of time calculation done in mn with 8 bits, having so a roll-over of about 4 h
// But total nb of mn stored as 16 bits (~1092 h, ~45.5 days)
word Minutes = 0;

// For seconds, 1 byte also used as we just want to check within 1 minute
byte Seconds = 0;
byte SendCountdown;

word SentCount;

byte ethernet_requests = 0;    // count ethernet requests without reply

byte justSent = 0;

unsigned long OnTimeSeconds = 0;

boolean FirstData = true;



 



//---------------------------------------------------------------------
// The PacketBuffer class is used to generate the json string that is send via ethernet - JeeLabs
//---------------------------------------------------------------------
class PacketBuffer : public Print
{
  public:
    PacketBuffer () : fill (0) {}
    const char* buffer() { return buf; }
    byte length() { return fill; }
    void reset()
    { 
      memset(buf,NULL,sizeof(buf));
      fill = 0; 
    }
    virtual size_t write (uint8_t ch)
        { if (fill < sizeof buf) buf[fill++] = ch; }
    byte fill;
    char buf[150];
  private:
};
PacketBuffer str;



/* main Initialization */
/* ------------------- */
void setup()
{
  Serial.begin(115200);
  DDRC |= _BV(DDC3);  // LED Pin = output
  RX_LED_OFF();
  LoadConfig();
  // Found this comment into network library: "init ENC28J60, must be done after SPI has been properly set up!"
  // So taking care of calling RF12 initialization prior ethernet initialization
  RF12Init();
  NetworkInitialize();
  ITPlusRXSetup();
  SendCountdown = NETWORKSENDPERIOD;
  ethernet_requests = 0;
  Serial.println("Datalogger ready");
  Serial.print("Version: ");Serial.print(VersionMAJOR);Serial.print(".");Serial.print(VersionMINORHi);Serial.println(VersionMINORLo);
  wdt_enable(WDTO_8S); 
  
  for (byte Channel = 0; Channel < MAX_JEENODE; Channel++)
  {
    RF12Channels[Channel].Enabled = false;
    RF12Channels[Channel].Power = 0;
    RF12Channels[Channel].Energy = 0;
    RF12Channels[Channel].LastReceiveTimer = 0;
  }
  RF12Channels[0].Enabled = true;
}

void loop()
{
  wdt_reset();
  
  // Error signaling code : must be run once per second.
  if (millis() - prevblinkMillis > BLINKTIME)
  {
    prevblinkMillis = millis();
    if (SignalError != 0)
    {
      if (SignalError == 1)
      {
        RX_LED_ON(); 
        SignalError = 2;
      } 
      else
      {
        RX_LED_OFF(); 
        SignalError = 1;
      }
    }
    else
    {
      RX_LED_OFF(); 
    }
    
  }
  
  // Check if a new second elapsed
  if (millis() - previousMillis > 1000L)
  {
    previousMillis = millis();
    Seconds++;
    OnTimeSeconds++;
    SendCountdown--;
    if (Seconds == 60)
    {
      Seconds = 0;
      Minutes++; 
    }
   }
   
  // If send period elapsed, check what we have to do
  if (SendCountdown==0)
  {
      SendCountdown=NETWORKSENDPERIOD;
    // Decrement LastReceiveTimer for all channels every mn...
    
    for (byte Channel = 0; Channel < MAX_JEENODE; Channel++)
    {
      if (RF12Channels[Channel].LastReceiveTimer != 0) RF12Channels[Channel].LastReceiveTimer--;
    }
    
    for (byte Channel = 0; Channel < ITPLUS_MAX_SENSORS; Channel++)
    {
      if (ITPlusChannels[Channel].LastReceiveTimer != 0) ITPlusChannels[Channel].LastReceiveTimer--;
    }
    
    for (byte Channel = 0; Channel < ITPLUS_MAX_DISCOVER; Channel++)
    {
      if (DiscoveredITPlus[Channel].LastReceiveTimer != 0) DiscoveredITPlus[Channel].LastReceiveTimer--;
    }
    
    str.reset();
    str.print("/input/bulk.json?");
    str.print("data=[");
    FirstData = false;
    for (byte Channel = 0; Channel < MAX_JEENODE; Channel++)
    {
      if (RF12Channels[Channel].LastReceiveTimer != 0)    // Send only if valid data received
      {
          str.print("[");                                       // Start node data; 
          str.print("0");                                       // 1. Time offset 0
          str.print(",");
          str.print(Channel+JEEDNODE_ENERGY_MON_NODEID_START);    // 2. Noded ID
          str.print(",");
          str.print(RF12Channels[Channel].Power);                 // 3. Power
          str.print(",");          
          str.print(RF12Channels[Channel].Energy, 3);             // 4. Energy
          str.print("]");                                        // End node data
          FirstData = true;
      }
      if (Channel!=MAX_JEENODE-1)
      {
        if (RF12Channels[Channel+1].LastReceiveTimer != 0)
             str.print(",");                  //next node delimite
      }
    }
    
    if (FirstData)
      str.print(",");                  //Power/Temp delimiter
   
    for (byte Channel = 0; Channel < ITPLUS_MAX_SENSORS; Channel++)
    {
      if (ITPlusChannels[Channel].SensorID != 0xff)   // Send only if registered
      { 
        if (ITPlusChannels[Channel].LastReceiveTimer != 0)    // Send only if valid temp received
        {
          str.print("[");                   // Start node data; 
          str.print("0");                  // 1. Time offset 0
          str.print(",");
          str.print(Channel);              // 2. Noded ID
          str.print(",");          
          if (ITPlusChannels[Channel].Temp & 0x80)              // 2. Temp
            str.print("-");
          str.print(ITPlusChannels[Channel].Temp & 0x7f); str.print("."); str.print(ITPlusChannels[Channel].DeciTemp);
          if (ITPlusChannels[Channel].Hygro != 106)
          {
             str.print(",");
             str.print(ITPlusChannels[Channel].Hygro);          // 3. Hygro
          }
          str.print("]");            // End node data
          if (Channel!=ITPLUS_MAX_SENSORS-1)
          {
             if (ITPlusChannels[Channel+1].LastReceiveTimer != 0)
             str.print(",");                  //next node delimiter
          }   
        }
      }   
    }
    
    str.print("]");  //End bulk packet   
    str.print("&apikey="); str.print(apikey);    // Add API key
  //  Serial.println();
  //  Serial.println(str.buf);
    RespRcvd = 0;
    ether.TcpEnded = false;
    ethernet_requests++;
    ether.browseUrl(PSTR("") ,str.buf, website, browserresult_callback);
    DebugPrintln_P(PSTR("Sended bulk data"));
    TimeoutMillis = millis();
    while (RespRcvd == 0)
    {
      CheckProcessBrowserRequest();
      CheckRF12Recept();
      wdt_reset();
       if (millis() - previousMillis > 1000L)
       {
          previousMillis = millis();
          Seconds++;
          OnTimeSeconds++;
          SendCountdown--;
          if (Seconds == 60)
          {
            Seconds = 0;
            Minutes++; 
          }
       }             
      if (ethernet_requests > 10)
      {
        DebugPrint_P(PSTR("EthErr>10, waiting for watchdog..."));
        delay(10000); // Reset the nanode if more than 10 request attempts have been tried without a reply
       }
       if ((millis()- TimeoutMillis) > 5000L)
       {
         DebugPrintln_P(PSTR("Resp timeout occured"));
         break;
       }
     }
     //After we have OK response, reset Energy value
     if (RespRcvd!=0)
       DebugPrintln_P(PSTR("Resp received"));
     TimeoutMillis = millis();
     wdt_reset();          
     while (ether.TcpEnded==false)
     {
       CheckProcessBrowserRequest();
       CheckRF12Recept();
       wdt_reset();
       if (millis() - previousMillis > 1000L)
       {
          previousMillis = millis();
          Seconds++;
          OnTimeSeconds++;
          SendCountdown--;
          if (Seconds == 60)
          {
            Seconds = 0;
            Minutes++; 
          }
       }       
       if ((millis()- TimeoutMillis) > 5000L)
       {
         DebugPrintln_P(PSTR("TCPEnd timeout occured"));
         break;
       }
     }
     if (ether.TcpEnded==true)
       DebugPrintln_P(PSTR("TCP ended"));
  }
  
  
  CheckProcessBrowserRequest();
  CheckRF12Recept();
     
  if (ethernet_requests > 10)
  {
      Serial.print("EthErr>10");
      delay(10000); // Reset the nanode if more than 10 request attempts have been tried without a reply
  }

  
  // Check error condition for signaling through LED
  ErrorCondition = false;
  for (byte Channel = 0; Channel < ITPLUS_MAX_SENSORS; Channel++)
  {
    if (ITPlusChannels[Channel].SensorID != 0xff && ITPlusChannels[Channel].LastReceiveTimer == 0)  // La Crosse receive OK check, only if registered
    {
      ErrorCondition = true;
      break;
    }
  }
  
    if (RF12Channels[0].LastReceiveTimer == 0)  // Jeenode receive OK check
    {
      ErrorCondition = true;
    }
  
  if (ErrorCondition)
  {
    if (SignalError == 0) SignalError = 1;  // Else, meaning that already signaled
  } 
  else
    SignalError = 0;

}
