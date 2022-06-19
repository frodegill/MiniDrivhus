#ifndef _NTP_H_
#define _NTP_H_

#ifdef TESTING
# include "testing.h"
#else
# include "Arduino.h"
#endif

void sendNTPpacket(const char* address);
time_t getNtpUtcTime();

class NTP
{
public:
  void initialize();
  bool getLocalTime(time_t& local_time);
};


#endif // _NTP_H_
