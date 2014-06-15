/*
 * Copyright (c) 2013
 *   Fabian Foerg. Stevens Institute of Technology.
 *
 * Implementation of the UNIX tool 'ls'.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <alloca.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <bsd/stdlib.h>
#include <string.h>
#include <bsd/string.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>

#include "print.h"
#include "util.h"

struct statdir_info {
  struct file_entry *entry;
  int entryc;
};

int main(int, char *[]);
static void traverse(const char *, struct flags *, int, int);
static void stat_and_print(const char *, const char *, struct flags *);
static struct statdir_info statdir(const char *, struct flags *);
static int file_count(const char*);
static blkcnt_t total_blks(const char *, struct statdir_info *,
  struct flags *);
static void usage(void);

/*
 * Parses flags and executes this 'ls'.
 */
int
main(int argc, char *argv[])
{
  struct flags flag;
  int ch;

  flags_init(&flag);  
  setprogname((char *)argv[0]);

  while ((ch = getopt(argc, argv, "AaCcdFfhiklnqRrSstuwx1")) != -1) {
    switch (ch) {
    case 'A':
      flag.Aflag = 1;
      break;
    case 'a':
      flag.aflag = 1;
      break;
    case 'C':
      flag.Cflag = 1;
      flag.lflag = 0;   /* override */
      flag.nflag = 0;   /* override */
      flag.xflag = 0;   /* override */
      flag.oneflag = 0; /* override */
      break;
    case 'c':
      flag.cflag = 1;
      flag.uflag = 0; /* override */
      break;
    case 'd':
      flag.dflag = 1;
      break;
    case 'F':
      flag.Fflag = 1;
      break;
    case 'f':
      flag.fflag = 1;
      break;
    case 'h':
      flag.hflag = 1;
      break;
    case 'i':
      flag.iflag = 1;
      break;
    case 'k':
      flag.kflag = 1;
      break;
    case 'l':
      flag.lflag = 1;
      flag.Cflag = 0;   /* override */
      flag.nflag = 0;   /* override */
      flag.xflag = 0;   /* override */
      flag.oneflag = 0; /* override */
      break;
    case 'n':
      flag.nflag = 1;
      flag.Cflag = 0;   /* override */
      flag.lflag = 0;   /* override */
      flag.xflag = 0;   /* override */
      flag.oneflag = 0; /* override */
      break;
    case 'q':
      flag.qflag = 1;
      flag.wflag = 0; /* override */
      break;
    case 'R':
      flag.Rflag = 1;
      break;
    case 'r':
      flag.rflag = 1;
      break;
    case 'S':
      flag.Sflag = 1;
      break;
    case 's':
      flag.sflag = 1;
      break;
    case 't':
      flag.tflag = 1;
      break;
    case 'u':
      flag.uflag = 1;
      flag.cflag = 0; /* override */
      break;
    case 'w':
      flag.wflag = 1;
      flag.qflag = 0; /* override */
      break;
    case 'x':
      flag.xflag = 1;
      flag.Cflag = 0;   /* override */
      flag.lflag = 0;   /* override */
      flag.nflag = 0;   /* override */
      flag.oneflag = 0; /* override */
      break;
    case '1':
      flag.oneflag = 1;
      flag.Cflag = 0;   /* override */
      flag.lflag = 0;   /* override */
      flag.nflag = 0;   /* override */
      flag.xflag = 0;   /* override */
      break;
    case '?':
    default:
      usage();
      /* NOTREACHED */
    }
  }
  argc -= optind;
  argv += optind;

  /* set this for cmp function from util.h */
  reverse = flag.rflag;

  /* flag A is always set for super user */
  if (getuid() == 0)
    flag.Aflag = 1;

  /*
   * flag q is default for terminal output
   * flag w is default for non-terminal output
   */
  if (!(flag.qflag || flag.wflag)) {
    if (isatty(STDOUT_FILENO))
      flag.qflag = 1;
    else
      flag.wflag = 1;
  }

  /*
   * 1 flag is default for non-terminal output
   * C flag is default for terminal output
   * flags 1, C, l, n, x override each other
   */
  if (!(flag.oneflag || flag.Cflag || flag.lflag || flag.nflag
       || flag.xflag)) {
    if (isatty(STDOUT_FILENO))
      flag.Cflag = 1;
    else
      flag.oneflag = 1;
  }

  if (argc == 0) {
    /* no file provided: list the current directory. */
    if (flag.dflag)
      stat_and_print(PWD_STRING, PWD_STRING, &flag);
    else
      traverse(PWD_STRING, &flag, 0, 0);
  } else {
    /* 
     * List non-directories before directories
     * Sort names lexicographically and separately for
     * non-directories and directories.
     */
    int non_dirc;
    struct file_entry *entries;

    entries = (struct file_entry *)malloc(sizeof(struct file_entry) * argc);
    if (entries == NULL)
      err(EXIT_FAILURE, "malloc error for entries");
    non_dirc = stat_and_sort(argv, argc, entries);
    if (flag.dflag)
      print_entries("", entries, argc, &flag);
    else {
      int i;

      if (non_dirc > 0) {
        print_entries("", entries, non_dirc, &flag);
        /* print a newline before directories */
        if ((argc - non_dirc) > 0)
          putchar('\n');
      }
      for (i = non_dirc; i < argc; i++)
        traverse(entries[i].name, &flag, argc > 1, i - non_dirc);
    }
    free(entries);
  }

  return EXIT_SUCCESS;
}

/*
 * Traverses the given directory according to the flags.
 * intro determines whether a directory pre-amble should be printed
 * and depth determines the depth in the recursive call relative to
 * the user-provided directory.
 */
static void
traverse(const char *dir, struct flags *flag, int intro, int depth)
{
  struct statdir_info dir_info;
  struct file_entry *entries;
  int entryc;
  int i;

  assert((dir != NULL) && (flag != NULL));

  print_intro(dir, intro, depth, flag);

  dir_info = statdir(dir, flag);
  entries = dir_info.entry;
  entryc = dir_info.entryc;

  /* f flag means no sorting */
  if (!flag->fflag) {
    reverse = flag->rflag;
    /* sort lexicographically first */
    sort_key = SORT_LEXICO;
    qsort(entries, entryc, sizeof(entries[0]), cmp);
    if (flag->tflag) {
      /* sort according to timestamp */
      sort_key = SORT_MTIME;
      if (flag->cflag)
        sort_key = SORT_CTIME;
      else if (flag->uflag)
        sort_key = SORT_ATIME;
      qsort(entries, entryc, sizeof(entries[0]), cmp);
    } else if (flag->Sflag) {
      /* sort according to size */
      sort_key = SORT_SIZE;
      qsort(entries, entryc, sizeof(entries[0]), cmp);
    }
  }

  /* print total FS blocks */
  if (flag->lflag || flag->nflag || (flag->sflag && isatty(STDOUT_FILENO))) {
    char buf[64];
    char *buf_ptr;
    size_t remain;
    blkcnt_t total;

    buf_ptr = buf;
    remain = 64;
    total = total_blks(dir, &dir_info, flag);
    print_blks(&buf_ptr, &remain, total, flag);
    print_char(&buf_ptr, &remain, 0);
    printf("total %s\n", buf);
  }

  /* print file entries itself */
  print_entries(dir, entries, entryc, flag);

  if (flag->Rflag) {
    /* recursively traverse sub-directories */
    for (i = 0; i < entryc; i++) {
      char *path;
      int traverse_dir;

      traverse_dir = S_ISDIR(entries[i].sb.st_mode)
        && !is_dot_dir(entries[i].name);
      path = full_path(dir, entries[i].name);
      if (traverse_dir)
        traverse(path, flag, intro, depth + 1);
      free(path);
    }
  }

  free(entries);
}

/*
 * Calls lstat(2) on the given file name and prints the information.
 */
static void
stat_and_print(const char *dir, const char *name, struct flags *flag)
{
  struct file_entry entry;

  assert((dir != NULL) && (name != NULL) && (flag != NULL));

  lstat_path(dir, name, &entry.sb);
  if (strlen(name) >= sizeof(entry.name))
    errx(EXIT_FAILURE, "name %s too long", name);
  strncpy(entry.name, name, sizeof(entry.name));
  print_entries(dir, &entry, 1, flag);
}

/*
 * Returns a structure containing the number of files in the given directory
 * and an array of file_entry structures with the entry names and entry
 * lstat(2) values.
 * free(3) the file_entry array contained in the returned structure.
 */
static struct statdir_info
statdir(const char *path, struct flags *flag)
{
  struct statdir_info dir_info;
  struct file_entry *entries;
  int entryc;
  DIR *dir;
  struct dirent *entry;
  int displayed;
  int i;

  entryc = file_count(path);
  assert(entryc >= 0);
  entries = (struct file_entry *)malloc(sizeof(struct file_entry) * entryc);
  if (entries == NULL)
    err(EXIT_FAILURE, "not enough memory for files names in %s", path);

  if ((dir = opendir(path)) == NULL)
    err(EXIT_FAILURE, "error opendir %s", path);
  displayed = 0;
  for (i = 0; (i < entryc) && ((entry = readdir(dir)) != NULL); i++) {
    if (display_file(path, entry->d_name, flag)) {
      if ((strlen(entry->d_name) + 1) > sizeof(entries[displayed].name))
        errx(EXIT_FAILURE, "file name too long %s", entry->d_name);
      strcpy(entries[displayed].name, entry->d_name);
      lstat_path(path, entry->d_name, &entries[displayed].sb);
      displayed++;
    }
  }

  if (closedir(dir) < 0)
    err(EXIT_FAILURE, "error closedir %s", path);
  /* was the directory modified during traversal? */
  if (i < entryc)
    err(EXIT_FAILURE, "files were removed from directory %s during traversal",
      path);
  else if (i > entryc)
    err(EXIT_FAILURE, "files were added to directory %s during traversal",
      path);

  dir_info.entry = entries;
  dir_info.entryc = displayed;

  return dir_info;
}

/*
 * Counts the number of files in the given directory and returns the result.
 */
static int
file_count(const char *path)
{
  DIR *dir;
  int file_c;

  assert (path != NULL);

  if ((dir = opendir(path)) == NULL)
    err(EXIT_FAILURE, "error opendir %s", path);
  /* count the number of entries in path */
  file_c = 0;
  while (readdir(dir) != NULL)
    file_c++;
  if (closedir(dir) < 0)
    err(EXIT_FAILURE, "error closedir %s", path);

  return file_c;
}

/*
 * Adds up the number of blocks in the given directory which are to be
 * displayed.
 */
static blkcnt_t
total_blks(const char *dir, struct statdir_info *dir_info, struct flags *flag)
{
  blkcnt_t total;
  int i;
  struct file_entry *entries;
  int entryc;

  assert ((dir != NULL) && (dir_info != NULL) && (flag != NULL));

  total = 0;
  entries = dir_info->entry;
  entryc = dir_info->entryc;

  for (i = 0; i < entryc; i++) {
    if (display_file(dir, entries[i].name, flag))
      total += entries[i].sb.st_blocks;
  }

  return total;
}

/*
 * Prints usage information and terminates this process.
 */
static void
usage(void)
{
  (void)fprintf(stderr, "usage: %s [−AaCcdFfhiklnqRrSstuwx1][file ...]\n",
    getprogname());
  exit(EXIT_FAILURE);
}
