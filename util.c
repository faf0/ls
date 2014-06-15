#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <limits.h>
#include <bsd/stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util.h"

enum sort_type sort_key;
int reverse;

/*
 * Comparison function that returns a negative integer, if p1 is less than p2
 * according to sort_key.
 * Returns zero, if p1 equals p2.
 * Otherwise, returns a positive integer.
 * Set sort_key before running this function.
 * reverse which determines if the order is to be reversed must be set
 * beforehand.
 */
int
cmp(const void *p1, const void *p2)
{
  int res;

  assert((p1 != NULL) && (p2 != NULL));

  if (sort_key == SORT_LEXICO) {
    char const *name1;
    char const *name2;

    name1 = (* (struct file_entry const *)p1).name;
    name2 = (* (struct file_entry const *)p2).name;
    res = strcasecmp(name1, name2);
  } else {
    struct stat const *sb1;
    struct stat const *sb2;

    sb1 = &((* (struct file_entry const *)p1).sb);
    sb2 = &((* (struct file_entry const *)p2).sb);

    res = -1;

    switch (sort_key) {
    case SORT_SIZE:
      if (sb1->st_size < sb2->st_size)
        res = 1;
      else if (sb1->st_size == sb2->st_size)
        res = 0;
      break;
    case SORT_ATIME:
      if (sb1->st_atime < sb2->st_atime)
        res = 1;
      else if (sb1->st_atime == sb2->st_atime)
        res = 0;
      break;
    case SORT_MTIME:
      if (sb1->st_mtime < sb2->st_mtime)
        res = 1;
      else if (sb1->st_mtime == sb2->st_mtime)
        res = 0;
      break;
    case SORT_CTIME:
      if (sb1->st_ctime < sb2->st_ctime)
        res = 1;
      else if (sb1->st_ctime == sb2->st_ctime)
        res = 0;
      break;
    default:
      errx(EXIT_FAILURE, "unknown sort key %d", sort_key);
      /* NOTREACHED */
    }
 }

  if (reverse)
    return -res;
  else
    return res;
}

/*
 * Sorts the given paths lexicographically and such that
 * files come before directory paths. Also retrieves the
 * stat(2) information for each file and stores that along
 * with the file name in entries. Make sure to allocate
 * pathc elements for entries.
 * Returns the number of non-directory files.
 */
int
stat_and_sort(char *path[], int pathc, struct file_entry *entries)
{
  int non_dirc;
  int i;

  assert((path != NULL) && (pathc >= 0) && (entries != NULL));

  non_dirc = 0;
  for (i = 0; i < pathc; i++) {
    struct stat sb;
    int p;

    /*
     * Add directories from the end of sorted_path and add file
     * names from the front.
     */
    if (lstat(path[i], &sb) < 0)
      err(EXIT_FAILURE, "lstat error for path %s", path[i]);
    p = (S_ISDIR(sb.st_mode)) ? (pathc - 1 - i + non_dirc) : non_dirc;
    entries[p].sb = sb;
    if (strlen(path[i]) >= sizeof(entries[p].name))
      errx(EXIT_FAILURE, "path %s too long", path[i]);
    strncpy(entries[p].name, path[i], sizeof(entries[p].name));
    if (!S_ISDIR(sb.st_mode))
      non_dirc++;
  }

  reverse = 0;
  sort_key = SORT_LEXICO;
  qsort(entries, non_dirc, sizeof(entries[0]), cmp);
  qsort(entries + non_dirc, pathc - non_dirc, sizeof(entries[0]), cmp);

  return non_dirc;
}

/*
 * Appends given file name (second name) to the directory name (first
 * argument). The argument may be empty but not NULL.
 * Returns the result which needs to be free(3)ed!
 */
char *
full_path(const char *dir, const char *name)
{
  char *path;
  int length;
 
  assert((dir != NULL) && (name != NULL));

  path = (char *)malloc(sizeof(char) * PATH_MAX);
  if (path == NULL)
    err(EXIT_FAILURE, "not enough memory for path name: dir(%s) name(%s)",
      dir, name);
  length = strlen(dir);
  /* path_file must fit directory name + '/' + file name + '\0' */
  if ((length + NAME_MAX + 2) > PATH_MAX)
    errx(EXIT_FAILURE, "path name %s too long", path);
  strcpy(path, dir);
  /* append name */
  if (strlen(name) > 0) {
    /* append '/' to path name, if necessary */
    if ((length > 0) && (dir[length - 1] != '/')) {
      path[length] = '/';
      length++;
      path[length] = 0;
    }
    /* append the file name to directory name */
    strcpy(path + length, name);
  }

  return path;
}

/*
 * Calls lstat(2) on the given file and stores the result in sb.
 */
void
lstat_path(const char *dir, const char *file, struct stat *sb)
{
  char *path;

  assert ((dir != NULL) && (file != NULL) && (sb != NULL));

  path = full_path(dir, file);
  if (lstat(path, sb) < 0)
    err(EXIT_FAILURE, "lstat_path lstat error for %s", path);
  free(path);
}

/*
 * Returns whether the given path is '.' or '..'.
 */
int
is_dot_dir(const char *path)
{
  assert(path != NULL);
  return (strcmp(".", path) == 0) || (strcmp("..", path) == 0);
}

/*
 * Returns whether the given file name starts with a '.' and is
 * not '.' or '..'.
 */
static int
is_hidden_file(const char *path)
{
  assert(path != NULL);
  return ('.' == path[0]) && !is_dot_dir(path);
}

/*
 * Returns whether the file is to be printed on the output.
 */
int
display_file(const char *dir, const char *name, struct flags *flag)
{
  assert((dir != NULL) && (name != NULL) && (flag != NULL));
  return (is_dot_dir(name) || is_hidden_file(name)) ? flag->aflag : 1;
}

/*
 * Sets all flags to zero.
 */
void
flags_init(struct flags *flag)
{
  assert(flag != NULL);
  flag->Aflag = 0;
  flag->aflag = 0;
  flag->cflag = 0;
  flag->Cflag = 0;
  flag->dflag = 0;
  flag->Fflag = 0;
  flag->fflag = 0;
  flag->hflag = 0;
  flag->iflag = 0;
  flag->kflag = 0;
  flag->lflag = 0;
  flag->nflag = 0;
  flag->qflag = 0;
  flag->Rflag = 0;
  flag->rflag = 0;
  flag->Sflag = 0;
  flag->sflag = 0;
  flag->tflag = 0;
  flag->uflag = 0;
  flag->wflag = 0;
  flag->xflag = 0;
  flag->oneflag = 0;
}
