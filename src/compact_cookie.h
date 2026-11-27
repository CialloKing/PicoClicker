#ifndef PICOCLICKER_COMPACT_COOKIE_H
#define PICOCLICKER_COMPACT_COOKIE_H

#if defined(_M_IX86) || defined(__i386__)
#define INITIAL_COOKIE 0xBB40E64E
#elif defined(_M_X64)
#define INITIAL_COOKIE 0x00002B992DDFA232ull
#endif

#endif
