
#include <stdio.h>
#include <sqlite3.h>
#include <string.h>
#include <stdlib.h>
#include "ss_sql.h"
#include "ss_core.h"

sqlite3 *db;

static int callback_db(void *NotUsed, int argc, char **argv, char **azColName)
{
    snprintf(sql_r, H_MAX, "%s", argv[0] ? argv[0] : "NULL");
    return 0;
}

int init_db(char *fname)
{

    char *zErrMsg = 0;
    int rc;

    if (sqlite3_open_v2(fname, &db, SQLITE_OPEN_READWRITE, NULL)) {
        // non-existant db file
        if (sqlite3_open_v2
            (fname, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL)) {
            fprintf(stderr, "error: db file cannot be created\n");
            sqlite3_close(db);
            return 1;
        }

        rc = sqlite3_exec(db,
                          "create table sensors (id REAL, date DATE PRIMARY KEY UNIQUE, t_ext REAL, h_ext REAL, td_ext REAL, p_ext REAL, l_ext REAL, counts REAL, t_int REAL, t_tk REAL);",
                          callback_db, 0, &zErrMsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
        }

        rc = sqlite3_exec(db,
                          "create table files (fname TEXT PRIMARY KEY UNIQUE, size INTEGER, retr INTEGER)",
                          callback_db, 0, &zErrMsg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", zErrMsg);
            sqlite3_free(zErrMsg);
        }
    }

    return 0;
}

int get_file_retr(char *fname)
{
    int rv;
    char q[H_MAX];

    snprintf(q, H_MAX, "select count(*) from files where fname='%s';", fname);

    rv = sql_exec_i(q);
    if (rv == 1) {
        snprintf(q, H_MAX, "select retr from files where fname='%s';", fname);
        rv = sql_exec_i(q);
        return rv;
    }
    return 0;
}

int set_file_retr(char *fname, int retr)
{
    int rv;
    char q[H_MAX];

    snprintf(q, H_MAX, "update files set retr='%d' where fname='%s';", retr,
             fname);

    rv = sql_exec(q);

    return rv;
}

int init_file_retr(char *fname, int len, int retr)
{
    int rv;
    char q[H_MAX];

    snprintf(q, H_MAX, "insert into files values ('%s','%d','%d');", fname, len,
             retr);

    rv = sql_exec(q);

    return rv;
}

int sql_exec(char *query)
{
    char *zErrMsg = 0;
    int rc;

    rc = sqlite3_exec(db, query, callback_db, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        if (strncmp(zErrMsg, "column date is not unique", 25))
            fprintf(stderr, "SQL error: %s\n", zErrMsg);
        else {
            if (verb > 0)
                fprintf(stdout, ".");
        }
        sqlite3_free(zErrMsg);
        return 1;
    } else if (verb > 0)
        fprintf(stdout, "+");
    return 0;
}

int sql_exec_i(char *query)
{
    char *zErrMsg = 0;
    int rc, rv;

    rc = sqlite3_exec(db, query, callback_db, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
        return -1;
    }
    rv = atoi(sql_r);
    return rv;
}

int close_db()
{
    int rv;
    rv = sqlite3_close(db);
    return rv;
}
