#ifndef _DEBUG_H_INCLUDED
#define _DEBUG_H_INCLUDED

#define DEBUG_BUILD  // enable debug print.

#ifdef DEBUG_BUILD
# define DEBUG_PRINTF(fmt, ...)  Serial.printf(fmt, __VA_ARGS__);                   
# define DEBUG_PRINTLN(str) Serial.println(str);
# define DEBUG_PRINT(str) Serial.print(str);
#else
# define DEBUG_PRINTLN(str)
# define DEBUG_PRINTF(fmt, ...)
# define DEBUG_PRINT(fmt, ...)
#endif

#endif