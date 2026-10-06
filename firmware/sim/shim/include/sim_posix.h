#pragma once
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#ifdef __cplusplus
extern "C" {
#endif
int gv_stat(const char *path, struct stat *st);
FILE *gv_fopen(const char *path, const char *mode);
int gv_fclose(FILE *fp);
size_t gv_fread(void *buf, size_t sz, size_t n, FILE *fp);
size_t gv_fwrite(const void *buf, size_t sz, size_t n, FILE *fp);
int gv_fseek(FILE *fp, long off, int whence);
long gv_ftell(FILE *fp);
int gv_fflush(FILE *fp);
int gv_fileno(FILE *fp);
int gv_fsync(int fd);
int gv_fstat(int fd, struct stat *st);
int gv_setvbuf(FILE *fp, char *b, int mode, size_t size);
int gv_feof(FILE *fp);
int gv_ferror(FILE *fp);
int gv_ftruncate(int fd, off_t len);
DIR *gv_opendir(const char *path);
struct dirent *gv_readdir(DIR *d);
int gv_closedir(DIR *d);
void gv_rewinddir(DIR *d);
long gv_telldir(DIR *d);
void gv_seekdir(DIR *d, long pos);
int gv_mkdir(const char *path, mode_t m);
int gv_rmdir(const char *path);
int gv_unlink(const char *path);
int gv_remove(const char *path);
int gv_rename(const char *a, const char *b);
int gv_access(const char *path, int mode);
#ifdef __cplusplus
}
#endif
