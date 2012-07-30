
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/select.h>

#include "ss_parser.h"
#include "ss_log.h"
#include "ss_sql.h"

// process generic incoming strings

int ss_process_in_str(ss_connection_t * c)
{

    int rc;
    time_t now;
    struct tm t;

    // needed by GET
    int day;
    time_t tta;
    ss_connection_t *reply;
    char fname[9];
    int retr = 0, download = 0;

    rc = ss_get_method_in_str(&c->r);

    if (rc == 0) {

        if (c->r.method == SS_REQ_GET) {
            c->r.target = &c->r.in_str.str[4];
            if (verb > 3)
                fprintf(stdout, "ss_process_in_str r.target='%s'\n",
                        c->r.target);
            if ((c->r.in_str.len > 7)   //GET time
                && (strncmp(c->r.target, "time", 4) == 0)) {
                // sensor is asking for the local time on the PC
                // to get the rtc syncronized
                time(&now);
                t = *localtime(&now);
                snprintf(c->r.out_str.str, H_MAX,
                         "T%02d%02d%02d%d%02d%02d%04d\n", t.tm_sec, t.tm_min,
                         t.tm_hour, t.tm_wday, t.tm_mday, t.tm_mon + 1,
                         t.tm_year + 1900);
                c->r.out_str.len = 17;
                ss_process_out(c);
            }
        } else if (c->r.method == SS_REQ_EHLO) {
            // sensor is alive
            // do a select in the db to see for which of the last 7 days data is missing
            // then download that day's logs
            //c->r.target = &c->r.in_str.str[5];
            ss_log("%s", c->r.in_str.str);

            for (day = 1; day > -8; day--) {
                download = 0;

                if (day == 1) {
                    snprintf(fname, 9, "NOW     ");
                    download = 1;
                } else {
                    tta = time(NULL) + (day * 86400);
                    localtime_r(&tta, &t);

                    snprintf(fname, 9, "%d%02d%02d", t.tm_year + 1900,
                             t.tm_mon + 1, t.tm_mday);

                    retr = get_file_retr(fname);

                    if (retr < 0) {
                        init_file_retr(fname, 0, 0);
                    }

                    snprintf(sql_q, H_MAX,
                             "select count(*) from sensors where date like '%d-%02d-%02d%%';",
                             t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);

                    if (day == 0) {
                        if (sql_exec_i(sql_q) !=
                            (int)(t.tm_hour * 60 + t.tm_min) / sleep_period + 1)
                            download = 1;
                    } else if ((sql_exec_i(sql_q) != 1440 / sleep_period)
                               && (retr < 1440 / sleep_period + 8)) {
                        download = 1;
                    }
                }

                if (download == 1) {

                    if (day < 1)
                        set_file_retr(fname, retr + 1);

                    snprintf(c->r.out_str.str, H_MAX, "GET %s\n", fname);

                    c->r.out_str.len = 13;
                    ss_process_out(c);

                    reply = malloc(sizeof(*reply));
                    memset(&reply->r, 0, sizeof(struct ss_request_s));
                    reply->fd_dev = c->fd_dev;

                    ss_get_read(reply);

                    free(reply);

                }
            }

            snprintf(c->r.out_str.str, H_MAX, "HALT\n");
            c->r.out_str.len = 5;
            ss_process_out(c);

        } else if (c->r.method == SS_REQ_UNKNOWN) {;
            //fprintf(stdout,"%s\n",c->r.in_str.str);
            //ss_log("b__%s__e",c->r.in_str.str,c->r.in_str.len);
            //ss_log("%s", c->r.in_str.str, c->r.in_str.len);
        }
    }

    return 0;
}

// recognize generic incoming strings

int ss_get_method_in_str(ss_request_t * r)
{
    size_t skip;
    char buff[H_MAX];

    if (verb > 3) {
        fprintf(stdout,
                "ss_parse_in_str r->in_str.str='%s', r->in_str.len=%d",
                r->in_str.str, (int)r->in_str.len);
    }
    //ss_log("< %s\n",r->in_str.str);

    // 46 is openlog's control char
    // 0 is buffer garbage

    for (skip = 0; skip < H_MAX - 1; skip++)
        if (r->in_str.str[skip] != 46 && r->in_str.str[skip] != 0)
            break;

    strncpy(buff, &r->in_str.str[skip], H_MAX - skip);  // XXX ending 0?
    r->in_str.skip = skip;

    if (ss_str4_cmp(buff, 'G', 'E', 'T', ' ')) {
        r->method = SS_REQ_GET;
        r->target = &r->in_str.str[skip + 4];
    } else if (ss_str5_cmp(buff, 'E', 'H', 'L', 'O', ' ')) {
        r->method = SS_REQ_EHLO;
        r->target = &r->in_str.str[skip + 5];
    } else if (ss_str4_cmp(buff, 'L', 'E', 'N', ' ')) {
        r->method = SS_REQ_LEN;
        r->target = &r->in_str.str[skip + 4];
    } else if (ss_str3_cmp(buff, 'E', 'R', 'R')) {
        r->method = SS_REQ_ERR;
    } else if (ss_str2_cmp(buff, 'O', 'K')) {
        r->method = SS_REQ_OK;
    } else if (buff[0] == 115) {  // s[0-9]  sensor id
        r->method = SS_REQ_DATA;
        r->target = &r->in_str.str[skip];
    } else if (ss_str3_cmp(buff, 'D', 'B', 'G')) {
        r->method = SS_REQ_DBG;
    } else {
        r->method = SS_REQ_UNKNOWN;
        fprintf(stdout, "DBG unk %d, len=%d, skip=%d\n", r->in_str.str[0],
                (int)r->in_str.len, (int)r->in_str.skip);
        if (verb > 0)
            fprintf(stdout, "%s\n", &r->in_str.str[skip]);
        return 0;
    }

    if (verb > 3) {
        fprintf(stdout, " r->method=%d\n", r->method);
    }

    return 0;
}

// send out strings

int ss_process_out(ss_connection_t * c)
{

    if (verb > 0) {
        ss_log("%s", c->r.out_str.str);
        fprintf(stdout, "%s", c->r.out_str.str);
    }

    if (write(c->fd_dev, c->r.out_str.str, c->r.out_str.len) !=
        c->r.out_str.len) {
        fprintf(stdout, "error ss_process_out failed\n");
        return 1;
    }

    return 0;
}

// process the reply to a GET command

int ss_process_get_str(ss_connection_t * c)
{
    int rc;

    rc = ss_get_method_in_str(&c->r);

    if (c->r.method == SS_REQ_LEN) {
        if (verb > 0) {
            fprintf(stdout, "%s\n", c->r.target);
        }
    } else if (c->r.method == SS_REQ_DATA) {
        if (verb > 0)
            fprintf(stdout, "%s\n", &c->r.in_str.str[c->r.in_str.skip]);
        memset(sql_q, 0, H_MAX);
        snprintf(sql_q, H_MAX, "insert into sensors values ('");        // 29 chars to begin with

        int i, o = 29;
        unsigned char in;

        for (i = 0; i < H_MAX - c->r.in_str.skip; i++) {
            in = c->r.target[i];
            if (in == 's') {
            }                   // ignore units
            else if (in == ' ') {       // replace spaces with sql delimiters
                strncat(sql_q, "','", 4);
                o += 3;
            } else if (in == 0 || in == 10 || in == 13) {       // eol
                strncat(sql_q, "');\0", 4);
                break;
            } else {
                sql_q[o] = in;
                o++;
            }
        }
        sql_exec(sql_q);
    } else if (c->r.method == SS_REQ_OK) {
        if (verb > 0)
            fprintf(stdout, "%s\n", &c->r.in_str.str[c->r.in_str.skip]);
        return 10;
    } else if (c->r.method == SS_REQ_DBG) {
        if (verb > 0)
            fprintf(stdout, "%s\n", &c->r.in_str.str[c->r.in_str.skip]);
    } else {
        //return 1; 
        return 0;               //temp
    }
    return 0;
}

// listen for a reply to a GET command

int ss_get_read(ss_connection_t * reply)
{

    int i;
    fd_set read_fd_set;
    struct timeval timeout;
    char buff;

    i = 0;
    // wait for a reply
    FD_ZERO(&read_fd_set);
    FD_SET(reply->fd_dev, &read_fd_set);

    // if nothing comes back in X seconds, consider the GET reply finished
    for (;;) {
        // set the timeout each time since it gets rewritten
        // by the previous select
        timeout.tv_sec = 4;
        timeout.tv_usec = 0;
        switch (select(reply->fd_dev + 1, &read_fd_set, NULL, NULL, &timeout)) {
        case -1:
            fprintf(stderr, "select() error\n");
            return -1;
            break;
        case 0:
            fprintf(stderr, "select() timeout\n");
            return 1;
            break;
        case 1:
            if (read(reply->fd_dev, &buff, 1) == 1) {
                if (buff == 4 || buff == 13) {; // 4 is used for keepalives - ignore
                } else if (buff == 10 && i == 0) {;     // \n and nothing else - ignore
                } else if (buff == 10 && i > 1) {       // string_i_chars_long\n
                    reply->r.in_str.len = i;
                    switch (ss_process_get_str(reply)) {
                    case 1:
                        // the reply is not an expected one
                        return 1;
                        break;
                    case 10:
                        // got an OK
                        return 0;
                        break;
                    }

                    i = 0;
                    memset(&reply->r, 0, sizeof(struct ss_request_s));
                } else if (i < H_MAX - 1) {
                    reply->r.in_str.str[i] = buff;
                    i++;
                }
                buff = 0;
            }
            break;
        }
    }

    return 0;
}
