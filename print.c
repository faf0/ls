#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <bsd/stdlib.h>
#include <bsd/string.h>
#include <pwd.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "util.h"
#include "print.h"

#define DEFAULT_BLOCK_SIZE 512

/*
 * Tries to get the BLOCKSIZE environment variable
 * or returns default, if that fails.
 */
static size_t
get_block_size(void)
{
  char *blocksize;

  if ((blocksize = getenv("BLOCKSIZE")) == NULL)
    return DEFAULT_BLOCK_SIZE;
  else {
    size_t blocksizeint = atol(blocksize);

    if (blocksizeint <= 0)
      return DEFAULT_BLOCK_SIZE;
    else
      return blocksizeint;
  }
}

/*
 * Tries to get the COLUMNS environment variable
 * or returns default, if that fails.
 */
static size_t
get_columns(void)
{
  char *columns_str;

  if ((columns_str = getenv("COLUMNS")) == NULL)
    return TTY_COLUMNS;
  else {
    size_t columns = atol(columns_str);

    if (columns <= 0)
      return TTY_COLUMNS;
    else
      return columns;
  }
}

/*
 * The given buffer columns must be DELIMITER-delimited and the buffer must be
 * null-terminated.
 * max determines whether the maximum should be saved.
 * Updates the maximum number of characters per column.
 */
static void
set_max_per_col(const char *buf, struct max_per_col *dst, int max)
{
  short col;
  short width;
  int i;

  /* find the maximum character width per column */
  col = 0;
  width = 0;
  for (i = 0; (buf[i] != 0) && (col < dst->cols); i++) {
    if (buf[i] == DELIMITER) {
      if (!max || (width > dst->max_width[col]))
        dst->max_width[col] = width;
      col++;
      width = 0;
    } else
      width++;
  }
  if (col == (dst->cols - 1)) {
    if (!max || (width > dst->max_width[col]))
      dst->max_width[col] = width;
  } else
    errx(EXIT_FAILURE, "entry has more columns than other entries!");
}

/*
 * Stores the largest given column widths in dst.
 */
static void
set_max(struct max_per_col *dst, struct max_per_col *m1)
{
  int i;

  assert ((dst != NULL) && (m1 != NULL) && (dst->cols == m1->cols));

  for (i = 0; i < dst->cols; i++) {
    if (m1->max_width[i] > dst->max_width[i])
      dst->max_width[i] = m1->max_width[i];
  }
}

/*
 * Counts the columns in buffer and stores the result in dst.
 * Allocates memory for dst->max_width which must be free(3)ed.
 */
void
init_max_per_col(const char *buf, struct max_per_col *dst)
{
  short cols;
  int i;

  cols = 0;
  /* count columns */
  for (i = 0; buf[i] != 0; i++) {
    if (buf[i] == DELIMITER)
      cols++;
  }
  /* last entry is null-terminated */
  cols++;

  dst->max_width = (short *)malloc(sizeof(short) * cols);
  if (dst->max_width == NULL)
    err(EXIT_FAILURE, "malloc failed for max_width");
  dst->cols = cols;

  set_max_per_col(buf, dst, 0);
}

/*
 * The given buffer columns must be DELIMITER-delimited and the buffer must be
 * null-terminated.
 * Updates the maximum number of characters per column.
 */
void
update_max_per_col(const char *buf, struct max_per_col *dst)
{
  set_max_per_col(buf, dst, 1);
}

/*
 * Prints a character.
 */
void
print_char(char **buf_ptr, size_t *remain, char c)
{
  assert((buf_ptr != NULL) && (remain != NULL));
  if ((*remain) >= 1) {
    **buf_ptr = c;
    (*buf_ptr)++;
    (*remain)--;
  }
}

/*
 * Prints the delimiter.
 */
static void
print_delim(char **buf_ptr, size_t *remain)
{
  assert((buf_ptr != NULL) && (remain != NULL));
  print_char(buf_ptr, remain, DELIMITER);
}

/*
 * Prints the given provided size in decimal notation. The size is not
 * converted.
 */
static void
print_size_dec(char **buf_ptr, size_t *remain, unsigned long size)
{
  int printed;

  assert((buf_ptr != NULL) && (remain != NULL));

  printed = snprintf(*buf_ptr, *remain, "%lu", size);
  if (printed < 0)
    errx(EXIT_FAILURE, "print decimal error");
  *buf_ptr += printed;
  *remain -= printed;
}

/*
 * Prints the inode.
 */
static void
print_inode(char **buf_ptr, size_t *remain, struct stat *sb)
{
  assert((buf_ptr != NULL) && (remain != NULL) && (sb != NULL));
  print_size_dec(buf_ptr, remain, sb->st_ino);
}

/*
 * Prints the given size in bytes in human-readable format.
 */
static void
print_size_human(char **buf_ptr, size_t *remain, unsigned long size)
{
  double result;
  int unit;
  int fracdigits;
  char units[] = {'B', 'K', 'M', 'G', 'T', 'P', 'E'};
  int printed;

  assert((buf_ptr != NULL) && (remain != NULL));

  unit = 0;
  result = size;
  while (result >= 1000) {
    result /= 1024;
    unit++;
  }

  fracdigits = (result >= 10 || (result == 0)) ? 0 : 1;

  /* print a space instead of 'B' */
  printed = snprintf(*buf_ptr, *remain, "%.*f", fracdigits, result);
  if (printed < 0)
    errx(EXIT_FAILURE, "print size in human-readable format error");
  *buf_ptr += printed;
  *remain -= printed;
  if ((unit > 0) && (unit < (sizeof(units) / sizeof(units[0]))))
    print_char(buf_ptr, remain, units[unit]);
}

/*
 * Converts the given size in bytes to kilobytes and prints the result.
 * width determines the minimum number of characters to be printed.
 */
static void
print_size_kilo(char **buf_ptr, size_t *remain, unsigned long size)
{
  unsigned long kilo;

  assert((buf_ptr != NULL) && (remain != NULL));

  kilo = size / 1024;

  /* round up if fractional part not zero */
  if ((size % 1024) > 0) 
    kilo++;
  print_size_dec(buf_ptr, remain, kilo);
}

/*
 * Returns the total size of the blocks in bytes.
 */
static unsigned long
blkcnt_t_to_bytes(blkcnt_t blks)
{
  return (blks * get_block_size());
}

/*
 * Returns the real number of blocks, if environment variable BLOCKSIZE is set.
 */
static unsigned long
blkcnt_t_to_blocks(blkcnt_t blks)
{
  size_t blocksize;
  double res;

  blocksize = get_block_size();
  /* blks are 512 byte blocks */
  res = (double)blks / (double)blocksize * (double)512;
  /* round up if necessary */
  if ((res - (unsigned long)res) > 0)
    res += (double)1;
  return (unsigned long)res;
}

/*
 * Prints the given blocks. The buffer buf_ptr is NOT null-terminated.
 */
void
print_blks(char **buf_ptr, size_t *remain, blkcnt_t blocks, struct flags *flag)
{
 assert((buf_ptr != NULL) && (remain != NULL) && (flag != NULL));

 if (flag->hflag || flag->kflag) {
    unsigned long bytes;

    bytes = blkcnt_t_to_bytes(blocks);
    if (flag->hflag)
      print_size_human(buf_ptr, remain, bytes);
    else
      print_size_kilo(buf_ptr, remain, bytes);
  } else {
    unsigned long real_blocks;

    real_blocks = blkcnt_t_to_blocks(blocks);
    print_size_dec(buf_ptr, remain, real_blocks);
  }
}

/*
 * Prints the type symbol (desired, if F flag is set).
 */
static void
print_type_symbol(char **buf_ptr, size_t *remain, struct stat *sb,
  struct flags *flag)
{
  assert((buf_ptr != NULL) && (remain != NULL) && (sb != NULL)
    && (flag != NULL));
  switch (sb->st_mode & S_IFMT) {
  case S_IFDIR:
    print_char(buf_ptr, remain, '/');
    break;
  case S_IFIFO:
    print_char(buf_ptr, remain, '|');
    break;
  case S_IFLNK:
    if (!(flag->lflag || flag->nflag))
      print_char(buf_ptr, remain, '@');
    break;
  case S_IFSOCK:
    print_char(buf_ptr, remain, '=');
    break;
  case _S_IFWHT:
    print_char(buf_ptr, remain, '%');
    break;
  default:
    if (sb->st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))
      print_char(buf_ptr, remain, '*');
    break;
  }
}

/*
 * Reads the given symbolic link and prints where the link points to.
 */
static void
print_link(char **buf_ptr, size_t *remain, const char *dir, const char *name,
  struct stat *sb, struct flags *flag)
{
  char *linkname;
  char *path;
  int r;
  int printed;

  assert((buf_ptr != NULL) && (remain != NULL) && (dir != NULL)
    && (name != NULL) && (sb != NULL) && (flag != NULL));

  linkname = (char *)alloca(sb->st_size + 1);
  path = full_path(dir, name);
  r = readlink(path, linkname, sb->st_size + 1);

  if (r < 0)
    err(EXIT_FAILURE, "readlink error for %s", path);
  if (r > sb->st_size)
    errx(EXIT_FAILURE, "symlink increased in size between lstat() and "
      "readlink() for %s", path);

  linkname[sb->st_size] = 0;
  printed = snprintf(*buf_ptr, *remain, " -> %s", linkname);
  if (printed < 0)
    errx(EXIT_FAILURE, "print link error");
  *buf_ptr += printed;
  *remain -= printed;
  if (flag->Fflag) {
    struct stat link_sb;
    const char *link_dir;
    char *link_path;

    link_dir = (linkname[0] == '/') ? "" : dir;
    link_path = full_path(link_dir, linkname);
    if (lstat(link_path, &link_sb) != -1)
      print_type_symbol(buf_ptr, remain, &link_sb, flag);
    free(link_path);
  }
  free(path);
}

/*
 * Prints the link count of the given file.
 */
static void
print_linkc(char **buf_ptr, size_t *remain, struct stat *sb)
{
  assert((buf_ptr != NULL) && (remain != NULL) &&  (sb != NULL));
  print_size_dec(buf_ptr, remain, (unsigned long)sb->st_nlink);
}

/*
 * Prints the name of the file according to flag.
 */
static void
print_name(char **buf_ptr, size_t *remain, const char *name,
  struct flags *flag)
{
  int length;
  int i;

  assert((buf_ptr != NULL) && (remain != NULL) && (name != NULL)
    && (flag != NULL));

  length = strlen(name);
  for (i = 0; i < length; i++) {
    char c;

    c = name[i];
    if (flag->qflag && !flag->wflag && !isprint(name[i]))
      c = '?';
    print_char(buf_ptr, remain, c);
  }
}

/*
 * Prints the owner of the given file.
 */
static void
print_owner(char **buf_ptr, size_t *remain, struct stat *sb,
  struct flags *flag)
{
  struct passwd *pwd;

  assert((buf_ptr != NULL) && (remain != NULL)
    && (sb != NULL) && (flag != NULL));

  if (!flag->nflag && (pwd = getpwuid(sb->st_uid)) != NULL)
    print_name(buf_ptr, remain, pwd->pw_name, flag);
  else
    print_size_dec(buf_ptr, remain, sb->st_uid);
}

/*
 * Prints the group of the given file.
 */
static void
print_group(char **buf_ptr, size_t *remain, struct stat *sb,
  struct flags *flag)
{
  struct group *grp;

  assert((buf_ptr != NULL) && (remain != NULL) && (sb != NULL)
    && (flag != NULL));

  if (!flag->nflag && (grp = getgrgid(sb->st_gid)) != NULL)
    print_name(buf_ptr, remain, grp->gr_name, flag);
  else
    print_size_dec(buf_ptr, remain, sb->st_gid);
}

/*
 * Prints the size of the given file.
 */
static void
print_size(char **buf_ptr, size_t *remain, struct stat *sb, struct flags *flag)
{
  unsigned long size;

  assert((buf_ptr != NULL) && (remain != NULL) && (sb != NULL)
    && (flag != NULL));

  size = sb->st_size;
  if (S_ISBLK(sb->st_mode) || S_ISCHR(sb->st_mode)) {
    print_size_dec(buf_ptr, remain, major(sb->st_rdev));
    print_char(buf_ptr, remain, ',');
    print_size_dec(buf_ptr, remain, minor(sb->st_rdev));
  } else if (flag->hflag)
    print_size_human(buf_ptr, remain, size);
  else if (flag->kflag)
    print_size_kilo(buf_ptr, remain, size);
  else
    print_size_dec(buf_ptr, remain, size);
}

/*
 * Prints the time (change, access, or modify as specified by flag) of the
 * given file.
 */
static void
print_time(char **buf_ptr, size_t *remain, struct stat *sb, struct flags *flag)
{
  char buf[200];
  struct tm *tmp;
  time_t tmt;
  time_t current;
  int printed;

  assert((buf_ptr != NULL) && (remain != NULL) && (sb != NULL) &&
    (flag != NULL));

  if (flag->cflag)
    tmt = sb->st_ctime;
  else if (flag->uflag)
    tmt = sb->st_atime;
  else
    tmt = sb->st_mtime;

  if (time(&current) < 0)
    errx(EXIT_FAILURE, "unable to determine current time");

  if ((tmp = localtime(&tmt)) == NULL)
    errx(EXIT_FAILURE, "localtime error");

  /* Display times older than 6 months with year */
  if ((current - tmt) < (6 * 30 * 24 * 60 * 60)) {
    if (strftime(buf, sizeof(buf), "%b %d %H:%M", tmp) == 0)
      errx(EXIT_FAILURE, "strftime error");
  } else {
    if (strftime(buf, sizeof(buf), "%b %d %Y", tmp) == 0)
      errx(EXIT_FAILURE, "strftime error");
  }

  printed = snprintf(*buf_ptr, *remain, "%s", buf);
  if (printed < 0)
    errx(EXIT_FAILURE, "print time error");
  *buf_ptr += printed;
  *remain -= printed;
}

/*
 * Prints the long options (l and n flag).
 */
static void
print_long(char **buf_ptr, size_t *remain, struct stat *sb, struct flags *flag)
{
  assert((buf_ptr != NULL) && (remain != NULL) && (sb != NULL)
    && (flag != NULL));
  /* type and permission */
  if (*remain < 12)
    errx(EXIT_FAILURE, "buffer too small to fit mode");
  strmode(sb->st_mode, *buf_ptr);
  *buf_ptr += 10;
  *remain -= 10;
  print_delim(buf_ptr, remain);
  /* link count */
  print_linkc(buf_ptr, remain, sb);
  print_delim(buf_ptr, remain);
  /* owner */
  print_owner(buf_ptr, remain, sb, flag);
  print_delim(buf_ptr, remain);
  /* group */
  print_group(buf_ptr, remain, sb, flag);
  print_delim(buf_ptr, remain);
  /* size */
  print_size(buf_ptr, remain, sb, flag);
  print_delim(buf_ptr, remain);
  /* time */
  print_time(buf_ptr, remain, sb, flag);
  print_delim(buf_ptr, remain);
}

/*
 * Prints the directory name if desired.
 */
void
print_intro(const char *dir, int intro, int depth, struct flags *flag)
{
  assert((dir != NULL) && (flag != NULL));
  if (depth > 0)
    putchar('\n');
  if (intro || flag->Rflag) {
    char buf[LINE_SIZE];
    char *buf_ptr;
    size_t remain;

    buf_ptr = buf;
    remain = LINE_SIZE;
    print_name(&buf_ptr, &remain, dir, flag);
    print_char(&buf_ptr, &remain, 0);
    printf("%s:\n", buf);
  }
}

/*
 * Print the directory contents in column mode (C or x flags).
 */
static void
print_dir(const char *dir, struct file_entry *entries, int entryc,
  struct flags *flag)
{
  int columns;
  struct max_per_col *entryc_width;
  struct max_per_col *max_col_width;
  int curr_col;
  int curr_row;
  int fits;
  int chars;
  int i;

  entryc_width = (struct max_per_col *)alloca(sizeof(struct max_per_col)
    * entryc);
  max_col_width = (struct max_per_col *)alloca(sizeof(struct max_per_col)
    * entryc);

  assert((dir != NULL) && (entries != NULL) && (flag != NULL));
  assert(entryc >= 0);

  if (!flag->Cflag && !flag->xflag)
    errx(EXIT_FAILURE, "print_dir must be called with either C or x flag set");

  /* handle trivial case: zero files */
  if (entryc == 0)
    return;

  /* Cflag XOR xflag must be set */
  assert((flag->Cflag || flag->xflag) && !(flag->Cflag && flag->xflag));
  /* get output columns */
  columns = get_columns();
  /* set column width for each entry */
  for (i = 0; i < entryc; i++) {
      char buf[LINE_SIZE];

      print_file(buf, LINE_SIZE, dir, entries[i].name, &entries[i].sb, flag);
      init_max_per_col(buf, &entryc_width[i]);
      init_max_per_col(buf, &max_col_width[i]);
  }
  /*
   * If C flag is set: Print entries along columns.
   * Try to iteratively increase row size, until we fit everything.
   *
   * If x flag is set: Print entries along rows and align per column.
   * Try to iteratively decrease column size, until we fit everything.
   */
  curr_col = entryc;
  curr_row = 1;
  do {
    if (flag->Cflag) {
      /* we fill rows column-wise, iteratively re-calculating needed columns */
      curr_col = entryc / curr_row;
      if ((entryc % curr_row) != 0)
        curr_col++;
    } else {
      /* we fill columns row-wise, iteratively re-calculating needed rows */
      curr_row = entryc / curr_col;
      if ((entryc % curr_col) != 0)
        curr_row++;
    }
    /* reset maximum widths */
    for (i = 0; i < entryc; i++) {
      int j;
      for (j = 0; j < max_col_width[i].cols; j++)
        max_col_width[i].max_width[j] = 1;
    }
    /*
     * Find the maximum size for each column.
     * Note that each column consists of more than one column, if more than
     * the file name is to be printed.
     */
    for (i = 0; i < entryc; i++) {
      int coli = flag->Cflag ?  (int)(i / curr_row) : i % curr_col;
      set_max(&max_col_width[coli], &entryc_width[i]);
    }

    if ((flag->Cflag && (curr_row == entryc))
      || (flag->xflag && (curr_col == 1)))
      /* we cannot change the format anymore */
      break;
    /* count maximum space per row */
    chars = 0;
    for (i = 0; i < curr_col; i++) {
      int j;
      for (j = 0; j < max_col_width[i].cols; j++)
        chars += max_col_width[i].max_width[j];
    }
    /* add whitespace */
    chars += (curr_col * entryc_width[0].cols) - 1;

    fits = chars <= columns;
    if (!fits) {
      if (flag->Cflag)
        curr_row++;
      else
        curr_col--;
    }
  } while (!fits);

  /* Do the printing */
  if (flag->Cflag) {
    for (i = 0; i < curr_row; i++) {
      int j;
      for (j = 0; j < curr_col; j++) {
        int p = i + j* curr_row;
        if (p < entryc) {
          char buf[LINE_SIZE];
          int newline;

          print_file(buf, LINE_SIZE, dir, entries[p].name,
            &entries[p].sb, flag);
          newline = (j == (curr_col - 1));
          print_buf(buf, &max_col_width[j], newline);
        } else
          putchar('\n');
      }
    }
  } else {
    /* x flag set */
    for (i = 0; i < entryc; i++) {
      int coli;
      char buf[LINE_SIZE];
      int newline;

      coli = i % curr_col;
      print_file(buf, LINE_SIZE, dir, entries[i].name, &entries[i].sb, flag);
      newline = ((coli == (curr_col - 1)) || (i == (entryc - 1)));
      print_buf(buf, &max_col_width[coli], newline);
    }
  }
  /* free saved widths */
  for (i = 0; i < entryc; i++) {
    free(entryc_width[i].max_width);
    free(max_col_width[i].max_width);
  }
}

/*
 * Prints the file as determined by flag.
 */
void
print_file(char const *buf, size_t buf_size, const char *dir, const char *name,
  struct stat *sb, struct flags *flag)
{
  char *buf_ptr;
  size_t remain;

  assert((buf != NULL) && (dir != NULL) && (name != NULL) && (sb != NULL)
    && (flag != NULL));

  buf_ptr = (char *)buf;
  remain = buf_size;

  /*
   * When d flag is set, the function is called on directories that need to be
   * printed.
   */
  if (!flag->dflag && !display_file(dir, name, flag))
    return;

  /* print inode */
  if (flag->iflag) {
    print_inode(&buf_ptr, &remain, sb);
    print_delim(&buf_ptr, &remain);
  }

  /* print FS blocks */
  if (flag->sflag) {
    print_blks(&buf_ptr, &remain, sb->st_blocks, flag);
    print_delim(&buf_ptr, &remain);
  }

  if (flag->lflag || flag->nflag)
    print_long(&buf_ptr, &remain, sb,flag);

  /* print file name */
  print_name(&buf_ptr, &remain, name, flag);

  /* print type symbol after name */
  if (flag->Fflag)
    print_type_symbol(&buf_ptr, &remain, sb, flag);

  /* print link target */
  if ((flag->lflag || flag->nflag) && S_ISLNK(sb->st_mode))
    print_link(&buf_ptr, &remain, dir, name, sb, flag);

  /* print null byte */
  print_char(&buf_ptr, &remain, 0);
}

/*
 * Prints the DELIMITER-separated buffer taking into account the given maximum
 * width per column.
 */
void
print_buf(const char *buf, struct max_per_col *widths, int newline)
{
  short col;
  short printed;
  int i;

  col = 0;
  printed = 0;
  for (i = 0; (buf[i] != 0) && (col < widths->cols); i++) {
    if (buf[i] == DELIMITER) {
      int j;
      for (j = printed; j < widths->max_width[col]; j++)
        putchar(' ');
      /* print delimiting whitespace */
      putchar(' ');
      printed = 0;
      col++;
    } else {
      putchar(buf[i]);
      printed++;
    }
  }
  if (newline)
    putchar('\n');
  else if (col == (widths->cols - 1)) {
    int j;
    for (j = printed; j < widths->max_width[col]; j++)
      putchar(' ');
    /* print delimiting whitespace */
    putchar(' ');
  }
}

/*
 * Prints the given entries.
 */
void
print_entries(const char *dir, struct file_entry *entries, int entryc,
  struct flags *flag)
{
  if (flag->Cflag || flag->xflag)
    print_dir(dir, entries, entryc, flag);
  else {
    /* print line-by-line */
    struct max_per_col max_widths;
    char buf[LINE_SIZE];
    int i;

    for (i = 0; i < entryc; i++) {
      print_file(buf, LINE_SIZE, dir, entries[i].name, &entries[i].sb, flag);
      if (i == 0)
        init_max_per_col(buf, &max_widths);
      else
        update_max_per_col(buf, &max_widths);
    }
    for (i = 0; i < entryc; i++) {
      print_file(buf, LINE_SIZE, dir, entries[i].name, &entries[i].sb, flag);
      print_buf(buf, &max_widths, 1);
    }
    if (entryc >= 1)
      free(max_widths.max_width);
  }
}
