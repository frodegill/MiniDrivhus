#ifndef _NTP_H_
#define _NTP_H_

#include "Arduino.h"


void sendNTPpacket(const char* address);
time_t getNtpUtcTime();

class NTP
{
public:
  void initialize();
  bool getLocalTime(time_t& local_time);
};


#endif // _NTP_H_
