
CC      = gcc
CFLAGS  = -g -Wall -O2
#LDFLAGS = 
PROGS   = ss_daemon

ss_daemon_OBJS    = ss_parser.o ss_log.o ss_sql.o ss_daemon.o
ss_daemon_EXTRA   = ss_parser.h ss_log.h ss_sql.h ss_core.h

SRCS   = ss_parser.c ss_log.c ss_sql.c ss_daemon.c
EXTRA  = ../AUTHORS ../COPYING ../ChangeLog \
			../INSTALL ../NEWS ../README \
			Makefile

all: ${PROGS} tags

.c.o:
	$(CC) -c $(CFLAGS) $*.c

ss_daemon : $(ss_daemon_OBJS) $(ss_daemon_EXTRA)
	$(CC) -o ss_daemon -lsqlite3 $(ss_daemon_OBJS) $(LIBS) $(LDFLAGS)

tags : $(ss_daemon_OBJS) $(ss_daemon_EXTRA)
	exuberant-ctags -h ".h.c" *

clean :
	rm -f *.o *.sibling *.combine $(PROGS)

strip : $(ss_daemon_OBJS) $(ss_daemon_EXTRA)
	strip ${PROGS}


