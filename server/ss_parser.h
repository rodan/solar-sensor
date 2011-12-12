#ifndef _SS_PARSER_H_INCLUDED_
#define _SS_PARSER_H_INCLUDED_

#include "ss_core.h"

#define ss_str2_cmp(m, c0, c1) \
    m[0] == c0 && m[1] == c1
#define ss_str3_cmp(m, c0, c1, c2) \
    m[0] == c0 && m[1] == c1 && m[2] == c2
#define ss_str4_cmp(m, c0, c1, c2, c3) \
    m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3
#define ss_str5_cmp(m, c0, c1, c2, c3, c4) \
    m[0] == c0 && m[1] == c1 && m[2] == c2 && m[3] == c3 && m[4] == c4




int ss_process_in_str(ss_connection_t * t);
int ss_get_method_in_str(ss_request_t * r);

int ss_process_out(ss_connection_t * t);

int ss_process_get_str(ss_connection_t * c);
int ss_get_read(ss_connection_t * reply);

#endif
