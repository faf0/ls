#ifndef _PRINT_H_
#define _PRINT_H_

#include <sys/stat.h>
#include <sys/types.h>

#include <unistd.h>

#include "util.h"

#define DELIMITER '\b'
#define PWD_STRING "."
#define LINE_SIZE 512
#define TTY_COLUMNS 80
#ifndef _S_IFWHT
  #define _S_IFWHT 0160000
#endif

struct max_per_col {
  short *max_width;
  short cols;
};

void init_max_per_col(const char *, struct max_per_col *);
void update_max_per_col(const char *, struct max_per_col *);
void print_char(char **, size_t *, char);
void print_blks(char **, size_t *, blkcnt_t, struct flags *);
void print_intro(const char *, int, int, struct flags *);
void print_file(char const *, size_t, const char *, const char *,
	struct stat *, struct flags *);
void print_buf(const char *, struct max_per_col *, int);
void print_entries(const char *, struct file_entry *, int, struct flags *);

#endif /* !_PRINT_H_ */
