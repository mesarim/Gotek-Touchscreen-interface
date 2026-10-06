#pragma once
#include <stdint.h>
#include <string.h>
#define PROGMEM
#define PGM_P const char *
#define PGM_VOID_P const void *
#define PSTR(s) (s)
#define pgm_read_byte(addr) (*(const unsigned char *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_dword(addr) (*(const unsigned long *)(addr))
#define pgm_read_float(addr) (*(const float *)(addr))
#define pgm_read_ptr(addr) (*(const void **)(addr))
#define pgm_read_byte_near(a) pgm_read_byte(a)
#define pgm_read_word_near(a) pgm_read_word(a)
#define memcpy_P memcpy
#define memcmp_P memcmp
#define strcpy_P strcpy
#define strncpy_P strncpy
#define strcmp_P strcmp
#define strncmp_P strncmp
#define strcasecmp_P strcasecmp
#define strlen_P strlen
#define strstr_P strstr
#define sprintf_P sprintf
#define snprintf_P snprintf
#define vsnprintf_P vsnprintf
#define strnlen_P strnlen
#define strncasecmp_P strncasecmp
#define printf_P printf
