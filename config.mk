VERSION = 2026-06-08T05-53-UTC-03

PREFIX    = /usr/local
MANPREFIX = $(PREFIX)/share/man

RANLIB  = ranlib
AR      = ar
ARFLAGS = rc

CPPFLAGS =\
	-Ishared\
	-DPREFIX=\"$(PREFIX)\"\
	-D_DEFAULT_SOURCE\
	-D_GNU_SOURCE\
	-D_NETBSD_SOURCE\
	-D_BSD_SOURCE\
	-D_XOPEN_SOURCE=700\
	-D_FILE_OFFSET_BITS=64\
	-DSTD_NON_POSIX

CFLAGS   = -std=c99 -Wall -Wextra -pedantic
LDFLAGS  =
LDLIBS   = -lcrypt -lresolv
