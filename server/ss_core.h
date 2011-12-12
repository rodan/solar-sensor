#ifndef _SS_CORE_H_INCLUDED_
#define _SS_CORE_H_INCLUDED_

#define H_MAX   250

#define SS_REQ_UNKNOWN  0x01
#define SS_REQ_GET      0x02
#define SS_REQ_EHLO     0x03
#define SS_REQ_LEN      0x04
#define SS_REQ_DATA     0x05
#define SS_REQ_OK       0x06
#define SS_REQ_ERR      0x07
#define SS_REQ_DBG      0x08

typedef struct ss_request_s ss_request_t;
typedef struct ss_connection_s ss_connection_t;

typedef struct {
    size_t len;
    char str[H_MAX];
    size_t skip;                // if command has garbage as prefix, skip this many chars
} ss_str_t;

struct ss_request_s {
    ss_str_t in_str;
    ss_str_t out_str;
    char method;
    char *target;
};

struct ss_connection_s {
    int fd_dev;                 // file descriptor 
    ss_request_t r;
};

char verb;

#endif
