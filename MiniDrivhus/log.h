#ifndef _LOG_H_
#define _LOG_H_

#define LOG_LEVEL_NONE   (0)
#define LOG_LEVEL_ERROR  (LOG_LEVEL_NONE+1)
#define LOG_LEVEL_INFO   (LOG_LEVEL_ERROR+1)
#define LOG_LEVEL_DEBUG  (LOG_LEVEL_INFO+1)

#define LOG_LEVEL LOG_LEVEL_INFO

// ******************************************************

#define LOG_ERROR(x)
#define LOG_INFO(x)
#define LOG_DEBUG(x)

#if (LOG_LEVEL>=LOG_LEVEL_ERROR)
# undef LOG_ERROR
# define LOG_ERROR(x) g_log.print(x)
#endif

#if (LOG_LEVEL>=LOG_LEVEL_INFO)
# undef LOG_INFO
# define LOG_INFO(x) g_log.print(x)
#endif

#if (LOG_LEVEL>=LOG_LEVEL_DEBUG)
# undef LOG_DEBUG
# define LOG_DEBUG(x) g_log.print(x)
#endif


#if (LOG_LEVEL>LOG_LEVEL_NONE)

class Log
{
public:
enum LOG_MODE
{
  LOG_NONE,
  LOG_SERIAL,
  LOG_MQTT
} log_mode = LOG_SERIAL;

#define MQTT_LOG_TOPIC "debug"

public:
  void enable();
  void disable();
  void print(const char* msg);

public:
  volatile bool enabled = false; // Internal, to keep track of Serial status
};

#endif // (LOG_LEVEL>=LOG_LEVEL_ERROR)

#endif // _LOG_H_
