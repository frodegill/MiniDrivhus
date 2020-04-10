#ifndef _DEBUG_H_
#define _DEBUG_H_

#define DEBUG_SUPPORT

#ifndef DEBUG_SUPPORT
# define DEBUG_MSG(x)
#else
# define DEBUG_MSG(x) g_debug.print(x)

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

#endif // DEBUG_SUPPORT

#endif // _DEBUG_H_
