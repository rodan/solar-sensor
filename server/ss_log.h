#ifndef _SS_LOG_H_INCLUDED_
#define _SS_LOG_H_INCLUDED_

#include <stdio.h>

int init_log(char *file, char *mode);
int close_log();
int ss_log(char *fmt, ...);

char verb;
FILE *log_file;

#endif
