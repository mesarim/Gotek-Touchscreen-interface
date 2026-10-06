// force-included into vfs_api.cpp only: route its POSIX calls to the FatFs-backed sim versions
#pragma once
#include "sim_posix.h"
#define fopen gv_fopen
#define fclose gv_fclose
#define fread gv_fread
#define fwrite gv_fwrite
#define fseek gv_fseek
#define ftell gv_ftell
#define fflush gv_fflush
#define fileno gv_fileno
#define fsync gv_fsync
#define fstat gv_fstat
#define setvbuf gv_setvbuf
#define feof gv_feof
#define ferror gv_ferror
#define ftruncate gv_ftruncate
#define opendir gv_opendir
#define readdir gv_readdir
#define closedir gv_closedir
#define rewinddir gv_rewinddir
#define telldir gv_telldir
#define seekdir gv_seekdir
#define unlink gv_unlink
#define access gv_access
#define stat(p, b) gv_stat(p, b)
#ifndef ACCESSPERMS
#define ACCESSPERMS 0777
#endif
