#ifndef _SS_SQL_H_INCLUDED_
#define _SS_SQL_H_INCLUDED_

#include "ss_core.h"

//static int callback_db(void *NotUsed, int argc, char **argv, char **azColName);
int init_db(char *fname);
int close_db();

int sql_exec(char *query);
int sql_exec_i(char *query);

int get_file_retr(char *fname);
int set_file_retr(char *fname, int retr);
int init_file_retr(char *fname, int len, int retr);

char sql_q[H_MAX];
char sql_r[H_MAX];

#endif
