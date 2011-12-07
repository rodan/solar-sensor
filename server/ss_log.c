
#include "ss_log.h"

#include <stdio.h>
#include <stdarg.h>

int init_log(char *file, char *mode)
{
    if (!(log_file = fopen(file, mode))) {
        return 1;
    }
    return 0;
}

int close_log()
{
    int rv;

    rv = fclose(log_file);
    return rv;
}

int ss_log(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(log_file, fmt, args);
    va_end(args);
    fprintf(log_file, "\n");
    fflush(log_file);
    return 0;
}
