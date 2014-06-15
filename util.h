#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <limits.h>
#include <unistd.h>

struct flags {
  int Aflag;
  int aflag;
  int Cflag;
  int cflag;
  int dflag;
  int Fflag;
  int fflag;
  int hflag;
  int iflag;
  int kflag;
  int lflag;
  int nflag;
  int qflag;
  int Rflag;
  int rflag;
  int Sflag;
  int sflag;
  int tflag;
  int uflag;
  int wflag;
  int xflag;
  int oneflag;
};

struct file_entry {
  char name[NAME_MAX + 1];
  struct stat sb;
};

enum sort_type {
  SORT_LEXICO,
  SORT_SIZE,
  SORT_ATIME,
  SORT_MTIME,
  SORT_CTIME
};

/*
 * Parameters that influence sorting. See function cmp.
 */
extern enum sort_type sort_key;
extern int reverse;

int cmp(const void *, const void *);
int stat_and_sort(char *[], int, struct file_entry *);
char *full_path(const char *, const char *);
void lstat_path(const char *, const char *, struct stat *);
int is_dot_dir(const char *);
int display_file(const char *, const char *, struct flags *);
void flags_init(struct flags *);

#endif /* !_UTIL_H_ */
