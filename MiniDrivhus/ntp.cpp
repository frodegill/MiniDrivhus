#include "ntp.h"

#ifndef TESTING
# include <TimeLib.h>
# include <Timezone.h>
# include <WiFiUdp.h>
#endif

static const char ntpServerName[] = "0.no.pool.ntp.org";
static const uint8_t NTP_PORT = 123;
static const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
uint8_t packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets

//Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     //Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       //Central European Standard Time
Timezone CE(CEST, CET);
time_t utc;


// send an NTP request to the time server at the given address
void sendNTPpacket(const char* address) {
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  if (0!=Udp.beginPacket(address, 123)) // NTP requests are to port 123. Returns 0 on error
  {
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
  }
}

time_t getNtpUtcTime()
{
  while (Udp.parsePacket() > 0) ; // discard any previously received packets

  sendNTPpacket(ntpServerName);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Udp.read(packetBuffer, NTP_PACKET_SIZE);

      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];

      // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
      const unsigned long seventyYears = 2208988800UL;
      
      return secsSince1900 - seventyYears;
    }
  }
  return 0; // return 0 if unable to get the time
}

void NTP::initialize()
{
  Udp.begin(localPort);
  setSyncProvider(getNtpUtcTime);
  setSyncInterval(300);
}

bool NTP::getLocalTime(time_t& local_time)
{
  if (timeStatus() == timeNotSet)
    return false;

  utc = now();
  local_time = CE.toLocal(utc);
  return true;
}
