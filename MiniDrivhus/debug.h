#ifndef _DEBUG_H_
#define _DEBUG_H_

class Debug
{
public:
enum DEBUG_MODE
{
  DEBUG_NONE,
  DEBUG_SERIAL,
  DEBUG_MQTT
} debug_mode = DEBUG_SERIAL;
#define MQTT_DEBUG_TOPIC "debug"

public:
  void enable();
  void disable();
  void print(const char* msg);

public:
  volatile bool enabled = false; // Internal, to keep track of Serial status
};

#endif // _DEBUG_H_
