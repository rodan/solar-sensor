
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "ss_core.h"
#include "ss_parser.h"
#include "ss_log.h"
#include "ss_sql.h"

ss_connection_t *c;
char lockfile[] = "/var/lock/LCK..ttyUSB0";

void exit_p(int sig)
{
    int rv;

    rv = close(c->fd_dev);
    free(c);

    rv += close_log();
    rv += close_db();
    unlink(lockfile);
    (void)signal(SIGINT, SIG_DFL);
    exit(rv);
}

int main()
{

    char buff;
    int i;
    char *logfile;
    char logfile_def[] = "/var/log/ss_daemon";
    char *dbfile;
    char dbfile_def[] = "/var/lib/ss_daemon/data.db";
    FILE *lock_file;

    (void)signal(SIGINT, exit_p);

    if (fopen(lockfile, "r") == NULL) {
        lock_file = fopen(lockfile, "a");
        if (lock_file == NULL) {
            fprintf(stderr, "error: cannot create %s file\n", lockfile);
            return 1;
        } else {
            fprintf(lock_file, "      %d\n", getpid());
            fclose(lock_file);
        }
    } else {
        fprintf(stderr, "error: %s lock file already in place\n", lockfile);
        return 1;
    }

    logfile = getenv("logfile");
    if (logfile == NULL)
        logfile = logfile_def;

    dbfile = getenv("dbfile");
    if (dbfile == NULL)
        dbfile = dbfile_def;

    if (getenv("verbosity") != NULL)
        verb = atoi(getenv("verbosity"));
    else
        verb = 0;

    c = malloc(sizeof(*c));

    /*
       stty -a -F /dev/ttyUSB0 # should look like

       speed 57600 baud; rows 0; columns 0; line = 0;
       intr = ^C; quit = ^\; erase = ^?; kill = ^U; eof = ^D; eol = <undef>;
       eol2 = <undef>; swtch = <undef>; start = ^Q; stop = ^S; susp = ^Z; rprnt = ^R;
       werase = ^W; lnext = ^V; flush = ^O; min = 1; time = 5;
       -parenb -parodd cs8 hupcl -cstopb cread clocal -crtscts
       ignbrk -brkint -ignpar -parmrk -inpck -istrip -inlcr -igncr -icrnl -ixon -ixoff
       -iuclc -ixany -imaxbel -iutf8
       -opost -olcuc -ocrnl -onlcr -onocr -onlret -ofill -ofdel nl0 cr0 tab0 bs0 vt0 ff0
       -isig -icanon -iexten -echo -echoe -echok -echonl -noflsh -xcase -tostop -echoprt
       -echoctl -echoke
     */

    if (system
        //("stty -F /dev/ttyUSB0 57600 ignbrk -icrnl -ixon -opost -onlcr -isig -icanon -iexten -echo -echoe -echok -echoctl -echoke time 5") == -1) {
        ("stty -F /dev/ttyUSB0 9600 cs8 raw ignbrk -onlcr -iexten -echo -echoe -echok -echoctl -echoke time 5") == -1) {
        fprintf(stderr, "error: stty cannot be executed\n");
        return 1;
    }

    if (init_log(logfile, "a") == 1) {
        fprintf(stderr, "error: logfile cannot be opened\n");
        return 1;
    }

    if (init_db(dbfile) == 1) {
        fprintf(stderr, "error: db fault, exiting\n");
        return 1;
    }

    c->fd_dev = open("/dev/ttyUSB0", O_RDWR);
    if (c->fd_dev < 0)
        return 1;

    // if pipe is used, I need more than an empty output
    setvbuf(stdout, NULL, _IONBF, 0);

    i = 0;
    memset(&c->r, 0, sizeof(struct ss_request_s));

    for (;;) {
        if (read(c->fd_dev, &buff, 1) == 1) {
            if (buff == 4 || buff == 13) {;     // 4 is used for keepalives, 13 is \r
            } else if (buff == 10 && i == 0) {;
            } else if (buff == 10 && i > 1) {   // if \n
                c->r.in_str.len = i;
                ss_process_in_str(c);
                i = 0;
                memset(&c->r, 0, sizeof(struct ss_request_s));
            } else if (i < H_MAX - 1) {
                c->r.in_str.str[i] = buff;
                i++;
            }
            buff = 0;
        }
    }

    exit_p(0);
}
