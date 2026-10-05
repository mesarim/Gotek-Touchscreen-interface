// sim: the bit of ESP-IDF's FAT VFS the GTi uses - POSIX calls on "/sdcard/..." paths, served by FatFs.
// vfs_api.cpp (Arduino FS) is compiled with its fopen/stat/opendir/... renamed to these (sim_posix.h).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include "ff.h"
#include "sim_posix.h"

int g_sim_sd_pdrv = -1;   // set by SD_MMC.begin

static bool xlate(const char *path, char *out, size_t n) {
  if (g_sim_sd_pdrv < 0 || !path) return false;
  if (strncmp(path, "/sdcard", 7) != 0) return false;
  const char *rest = path + 7;
  if (*rest && *rest != '/') return false;
  snprintf(out, n, "%d:%s", g_sim_sd_pdrv, *rest ? rest : "/");
  return true;
}
static int fr2errno(FRESULT r) {
  switch (r) {
    case FR_OK: return 0;
    case FR_NO_FILE: case FR_NO_PATH: case FR_INVALID_NAME: return ENOENT;
    case FR_DENIED: case FR_WRITE_PROTECTED: return EACCES;
    case FR_EXIST: return EEXIST;
    case FR_INVALID_OBJECT: return EBADF;
    case FR_NOT_ENOUGH_CORE: return ENOMEM;
    case FR_TOO_MANY_OPEN_FILES: return ENFILE;
    default: return EIO;
  }
}
static time_t fat2time(WORD fdate, WORD ftime) {
  struct tm t; memset(&t, 0, sizeof t);
  t.tm_year = ((fdate >> 9) & 0x7f) + 80; t.tm_mon = ((fdate >> 5) & 0x0f) - 1; t.tm_mday = fdate & 0x1f;
  t.tm_hour = (ftime >> 11) & 0x1f; t.tm_min = (ftime >> 5) & 0x3f; t.tm_sec = (ftime & 0x1f) * 2; t.tm_isdst = -1;
  return mktime(&t);
}

struct GvFile { FIL fil; int err; bool eof; bool append; };
struct GvDir { FF_DIR dir; char path[300]; long pos; union { struct dirent de; char pad[sizeof(struct dirent) + 260]; } u; };

extern "C" {

int gv_stat(const char *path, struct stat *st) {
  char p[300]; if (!xlate(path, p, sizeof p)) { errno = ENOENT; return -1; }
  memset(st, 0, sizeof *st);
  size_t l = strlen(p);
  if (l <= 3) { st->st_mode = S_IFDIR | 0777; return 0; }   // the root
  if (p[l - 1] == '/') p[l - 1] = 0;
  FILINFO fi; FRESULT r = f_stat(p, &fi);
  if (r != FR_OK) { errno = fr2errno(r); return -1; }
  st->st_size = (fi.fattrib & AM_DIR) ? 0 : (off_t)fi.fsize;
  st->st_mode = (fi.fattrib & AM_DIR) ? (S_IFDIR | 0777) : (S_IFREG | ((fi.fattrib & AM_RDO) ? 0444 : 0666));
  st->st_mtime = fat2time(fi.fdate, fi.ftime); st->st_atime = st->st_ctime = st->st_mtime;
  st->st_blksize = 512;
  return 0;
}

FILE *gv_fopen(const char *path, const char *mode) {
  char p[300]; if (!xlate(path, p, sizeof p)) { errno = ENOENT; return NULL; }
  BYTE m = 0; bool app = false;
  if (mode[0] == 'r') m = FA_READ | (strchr(mode, '+') ? FA_WRITE : 0) | FA_OPEN_EXISTING;
  else if (mode[0] == 'w') m = FA_WRITE | (strchr(mode, '+') ? FA_READ : 0) | FA_CREATE_ALWAYS;
  else if (mode[0] == 'a') { m = FA_WRITE | (strchr(mode, '+') ? FA_READ : 0) | FA_OPEN_APPEND; app = true; }
  GvFile *f = (GvFile *)calloc(1, sizeof(GvFile)); if (!f) { errno = ENOMEM; return NULL; }
  FRESULT r = f_open(&f->fil, p, m);
  if (r != FR_OK) { free(f); errno = fr2errno(r); return NULL; }
  f->append = app;
  return (FILE *)f;
}
int gv_fclose(FILE *fp) { GvFile *f = (GvFile *)fp; if (!f) return EOF; FRESULT r = f_close(&f->fil); free(f); return r == FR_OK ? 0 : EOF; }
size_t gv_fread(void *buf, size_t sz, size_t n, FILE *fp) {
  GvFile *f = (GvFile *)fp; UINT got = 0; size_t want = sz * n; if (!want) return 0;
  FRESULT r = f_read(&f->fil, buf, (UINT)want, &got);
  if (r != FR_OK) { f->err = 1; errno = fr2errno(r); }
  if (got < want) f->eof = true;
  return sz ? got / sz : 0;
}
size_t gv_fwrite(const void *buf, size_t sz, size_t n, FILE *fp) {
  GvFile *f = (GvFile *)fp; UINT put = 0; size_t want = sz * n; if (!want) return 0;
  if (f->append) f_lseek(&f->fil, f_size(&f->fil));
  FRESULT r = f_write(&f->fil, buf, (UINT)want, &put);
  if (r != FR_OK) { f->err = 1; errno = fr2errno(r); }
  return sz ? put / sz : 0;
}
int gv_fseek(FILE *fp, long off, int whence) {
  GvFile *f = (GvFile *)fp; long base = 0;
  if (whence == SEEK_CUR) base = (long)f_tell(&f->fil); else if (whence == SEEK_END) base = (long)f_size(&f->fil);
  long pos = base + off; if (pos < 0) { errno = EINVAL; return -1; }
  if ((FSIZE_t)pos > f_size(&f->fil) && !(f->fil.flag & FA_WRITE)) pos = (long)f_size(&f->fil);
  FRESULT r = f_lseek(&f->fil, (FSIZE_t)pos); f->eof = false;
  return r == FR_OK ? 0 : -1;
}
long gv_ftell(FILE *fp) { return (long)f_tell(&((GvFile *)fp)->fil); }
int gv_fflush(FILE *fp) { if (fp) f_sync(&((GvFile *)fp)->fil); return 0; }
int gv_fileno(FILE *fp) { return (int)(intptr_t)fp; }          // only ever handed back to gv_fstat / gv_fsync
int gv_fsync(int fd) { GvFile *f = (GvFile *)(intptr_t)fd; if (f) f_sync(&f->fil); return 0; }
int gv_fstat(int fd, struct stat *st) {
  GvFile *f = (GvFile *)(intptr_t)fd; memset(st, 0, sizeof *st); if (!f) return -1;
  st->st_mode = S_IFREG | 0666; st->st_size = (off_t)f_size(&f->fil); return 0;
}
int gv_setvbuf(FILE *fp, char *b, int mode, size_t size) { (void)fp; (void)b; (void)mode; (void)size; return 0; }
int gv_feof(FILE *fp) { GvFile *f = (GvFile *)fp; return f->eof || f_eof(&f->fil); }
int gv_ferror(FILE *fp) { return ((GvFile *)fp)->err; }
int gv_ftruncate(int fd, off_t len) {
  GvFile *f = (GvFile *)(intptr_t)fd; if (!f) return -1;
  FSIZE_t cur = f_tell(&f->fil); if (f_lseek(&f->fil, (FSIZE_t)len) != FR_OK) return -1;
  FRESULT r = f_truncate(&f->fil); f_lseek(&f->fil, cur < (FSIZE_t)len ? cur : (FSIZE_t)len); return r == FR_OK ? 0 : -1;
}

DIR *gv_opendir(const char *path) {
  char p[300]; if (!xlate(path, p, sizeof p)) { errno = ENOENT; return NULL; }
  size_t l = strlen(p); if (l > 3 && p[l - 1] == '/') p[l - 1] = 0;
  GvDir *d = (GvDir *)calloc(1, sizeof(GvDir)); if (!d) { errno = ENOMEM; return NULL; }
  FRESULT r = f_opendir(&d->dir, p);
  if (r != FR_OK) { free(d); errno = fr2errno(r); return NULL; }
  strncpy(d->path, p, sizeof d->path - 1);
  return (DIR *)d;
}
struct dirent *gv_readdir(DIR *dp) {
  GvDir *d = (GvDir *)dp; FILINFO fi;
  FRESULT r = f_readdir(&d->dir, &fi);
  if (r != FR_OK || fi.fname[0] == 0) return NULL;
  d->pos++;
  d->u.de.d_ino = (ino_t)d->pos;
  d->u.de.d_type = (fi.fattrib & AM_DIR) ? DT_DIR : DT_REG;
  strncpy(d->u.de.d_name, fi.fname, 256); d->u.de.d_name[255] = 0;
  return &d->u.de;
}
int gv_closedir(DIR *dp) { GvDir *d = (GvDir *)dp; if (!d) return -1; f_closedir(&d->dir); free(d); return 0; }
void gv_rewinddir(DIR *dp) { GvDir *d = (GvDir *)dp; f_readdir(&d->dir, NULL); d->pos = 0; }
long gv_telldir(DIR *dp) { return ((GvDir *)dp)->pos; }
void gv_seekdir(DIR *dp, long pos) {
  GvDir *d = (GvDir *)dp; gv_rewinddir(dp); FILINFO fi;
  while (d->pos < pos) { if (f_readdir(&d->dir, &fi) != FR_OK || !fi.fname[0]) break; d->pos++; }
}

int gv_mkdir(const char *path, mode_t m) { (void)m; char p[300]; if (!xlate(path, p, sizeof p)) { errno = ENOENT; return -1; } FRESULT r = f_mkdir(p); if (r != FR_OK) { errno = fr2errno(r); return -1; } return 0; }
int gv_rmdir(const char *path) { char p[300]; if (!xlate(path, p, sizeof p)) { errno = ENOENT; return -1; } FRESULT r = f_unlink(p); if (r != FR_OK) { errno = fr2errno(r); return -1; } return 0; }
int gv_unlink(const char *path) { return gv_rmdir(path); }
int gv_remove(const char *path) { return gv_rmdir(path); }
int gv_rename(const char *a, const char *b) {
  char p[300], q[300]; if (!xlate(a, p, sizeof p) || !xlate(b, q, sizeof q)) { errno = ENOENT; return -1; }
  // FatFs wants the destination without a drive prefix
  const char *qq = strchr(q, ':'); qq = qq ? qq + 1 : q;
  FRESULT r = f_rename(p, qq); if (r != FR_OK) { errno = fr2errno(r); return -1; } return 0;
}
int gv_access(const char *path, int mode) { (void)mode; struct stat st; return gv_stat(path, &st); }
int gv_utime(const char *path, const void *times) { (void)path; (void)times; return 0; }

}  // extern "C"
