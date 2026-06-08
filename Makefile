.POSIX:
include config.mk

.SUFFIXES:
.SUFFIXES: .y .o .c

HDR =\
	shared/arg.h\
	shared/compat.h\
	shared/config.h\
	shared/crypt.h\
	shared/fs.h\
	shared/md5.h\
	shared/queue.h\
	shared/sha1.h\
	shared/sha224.h\
	shared/sha256.h\
	shared/sha384.h\
	shared/sha512.h\
	shared/sha512-224.h\
	shared/sha512-256.h\
	shared/text.h\
	shared/utf.h\
	shared/util.h\
	shared/passwd.h\
	shared/reboot.h\
	shared/rtc.h\
	shared/proc.h

LIBUTFOBJ =\
	shared/libutf/fgetrune.o\
	shared/libutf/fputrune.o\
	shared/libutf/isalnumrune.o\
	shared/libutf/isalpharune.o\
	shared/libutf/isblankrune.o\
	shared/libutf/iscntrlrune.o\
	shared/libutf/isdigitrune.o\
	shared/libutf/isgraphrune.o\
	shared/libutf/isprintrune.o\
	shared/libutf/ispunctrune.o\
	shared/libutf/isspacerune.o\
	shared/libutf/istitlerune.o\
	shared/libutf/isxdigitrune.o\
	shared/libutf/lowerrune.o\
	shared/libutf/rune.o\
	shared/libutf/runetype.o\
	shared/libutf/upperrune.o\
	shared/libutf/utf.o\
	shared/libutf/utftorunestr.o

LIBUTILOBJ =\
	shared/libutil/concat.o\
	shared/libutil/cp.o\
	shared/libutil/crypt.o\
	shared/libutil/confirm.o\
	shared/libutil/ealloc.o\
	shared/libutil/enmasse.o\
	shared/libutil/eprintf.o\
	shared/libutil/eregcomp.o\
	shared/libutil/estrtod.o\
	shared/libutil/fnck.o\
	shared/libutil/fshut.o\
	shared/libutil/getlines.o\
	shared/libutil/human.o\
	shared/libutil/linecmp.o\
	shared/libutil/md5.o\
	shared/libutil/memmem.o\
	shared/libutil/mkdirp.o\
	shared/libutil/mode.o\
	shared/libutil/parseoffset.o\
	shared/libutil/putword.o\
	shared/libutil/reallocarray.o\
	shared/libutil/recurse.o\
	shared/libutil/rm.o\
	shared/libutil/sha1.o\
	shared/libutil/sha224.o\
	shared/libutil/sha256.o\
	shared/libutil/sha384.o\
	shared/libutil/sha512.o\
	shared/libutil/sha512-224.o\
	shared/libutil/sha512-256.o\
	shared/libutil/strcasestr.o\
	shared/libutil/strlcat.o\
	shared/libutil/strlcpy.o\
	shared/libutil/strsep.o\
	shared/libutil/strnsubst.o\
	shared/libutil/strtonum.o\
	shared/libutil/writeall.o\
	shared/libutil/unescape.o\
	shared/libutil/agetcwd.o\
	shared/libutil/agetline.o\
	shared/libutil/apathmax.o\
	shared/libutil/estrtol.o\
	shared/libutil/estrtoul.o\
	shared/libutil/explicit_bzero.o\
	shared/libutil/passwd.o\
	shared/libutil/proc.o\
	shared/libutil/tty.o\
	shared/libutil/fconcat.o\
	shared/libutil/recurse_dir.o

LIB = shared/libutf/libutf.a shared/libutil/libutil.a

POSIX_BIN =\
	cmd/posix/basename\
	cmd/posix/cal\
	cmd/posix/cat\
	cmd/posix/chgrp\
	cmd/posix/chmod\
	cmd/posix/chown\
	cmd/posix/cksum\
	cmd/posix/cmp\
	cmd/posix/comm\
	cmd/posix/cp\
	cmd/posix/cut\
	cmd/posix/date\
	cmd/posix/dd\
	cmd/posix/df\
	cmd/posix/dirname\
	cmd/posix/du\
	cmd/posix/echo\
	cmd/posix/ed\
	cmd/posix/env\
	cmd/posix/expand\
	cmd/posix/expr\
	cmd/posix/false\
	cmd/posix/find\
	cmd/posix/fold\
	cmd/posix/getconf\
	cmd/posix/grep\
	cmd/posix/head\
	cmd/posix/id\
	cmd/posix/join\
	cmd/posix/kill\
	cmd/posix/link\
	cmd/posix/ln\
	cmd/posix/logger\
	cmd/posix/logname\
	cmd/posix/ls\
	cmd/posix/mesg\
	cmd/posix/mkdir\
	cmd/posix/mkfifo\
	cmd/posix/mv\
	cmd/posix/nice\
	cmd/posix/nl\
	cmd/posix/nohup\
	cmd/posix/od\
	cmd/posix/paste\
	cmd/posix/pathchk\
	cmd/posix/printf\
	cmd/posix/ps\
	cmd/posix/pwd\
	cmd/posix/readlink\
	cmd/posix/renice\
	cmd/posix/rm\
	cmd/posix/rmdir\
	cmd/posix/sed\
	cmd/posix/sleep\
	cmd/posix/sort\
	cmd/posix/split\
	cmd/posix/tail\
	cmd/posix/tee\
	cmd/posix/test\
	cmd/posix/time\
	cmd/posix/touch\
	cmd/posix/tr\
	cmd/posix/true\
	cmd/posix/tsort\
	cmd/posix/tty\
	cmd/posix/uname\
	cmd/posix/unexpand\
	cmd/posix/uniq\
	cmd/posix/unlink\
	cmd/posix/uudecode\
	cmd/posix/uuencode\
	cmd/posix/wc\
	cmd/posix/who\
	cmd/posix/xargs

LINUX_BIN =\
	cmd/linux/blkdiscard\
	cmd/linux/chvt\
	cmd/linux/ctrlaltdel\
	cmd/linux/dmesg\
	cmd/linux/eject\
	cmd/linux/fallocate\
	cmd/linux/free\
	cmd/linux/freeramdisk\
	cmd/linux/fsfreeze\
	cmd/linux/hwclock\
	cmd/linux/insmod\
	cmd/linux/lsmod\
	cmd/linux/mkswap\
	cmd/linux/mount\
	cmd/linux/mountpoint\
	cmd/linux/pidof\
	cmd/linux/pivot_root\
	cmd/linux/pwdx\
	cmd/linux/readahead\
	cmd/linux/rmmod\
	cmd/linux/swaplabel\
	cmd/linux/swapoff\
	cmd/linux/swapon\
	cmd/linux/switch_root\
	cmd/linux/umount\
	cmd/linux/unshare\
	cmd/linux/uptime\
	cmd/linux/vtallow

NET_BIN =\
	cmd/net/netcat\
	cmd/net/tftp\
	cmd/net/tunctl\
	cmd/net/wget\
	cmd/net/ping\
	cmd/net/ifconfig\
	cmd/net/host\
	cmd/net/httpd

XSI_BIN =\
	cmd/xsi/mknod\
	cmd/xsi/passwd\
	cmd/xsi/su

PSEUDO_BIN =\
	cmd/pseudo/chroot\
	cmd/pseudo/clear\
	cmd/pseudo/cols\
	cmd/pseudo/cron\
	cmd/pseudo/flock\
	cmd/pseudo/getty\
	cmd/pseudo/halt\
	cmd/pseudo/hostname\
	cmd/pseudo/killall5\
	cmd/pseudo/last\
	cmd/pseudo/lastlog\
	cmd/pseudo/login\
	cmd/pseudo/md5sum\
	cmd/pseudo/mktemp\
	cmd/pseudo/nologin\
	cmd/pseudo/pagesize\
	cmd/pseudo/printenv\
	cmd/pseudo/respawn\
	cmd/pseudo/rev\
	cmd/pseudo/seq\
	cmd/pseudo/setsid\
	cmd/pseudo/sha1sum\
	cmd/pseudo/sha224sum\
	cmd/pseudo/sha256sum\
	cmd/pseudo/sha384sum\
	cmd/pseudo/sha512sum\
	cmd/pseudo/sha512-224sum\
	cmd/pseudo/sha512-256sum\
	cmd/pseudo/sponge\
	cmd/pseudo/stat\
	cmd/pseudo/tar\
	cmd/pseudo/truncate\
	cmd/pseudo/watch\
	cmd/pseudo/which\
	cmd/pseudo/whoami\
	cmd/pseudo/xinstall\
	cmd/pseudo/yes

MAKEOBJ =\
	cmd/posix/make/defaults.o\
	cmd/posix/make/main.o\
	cmd/posix/make/parser.o\
	cmd/posix/make/posix.o\
	cmd/posix/make/rules.o

OBJ = $(LIBUTFOBJ) $(LIBUTILOBJ) $(MAKEOBJ)

all: $(LIB) $(POSIX_BIN) $(LINUX_BIN) $(NET_BIN) $(XSI_BIN) $(PSEUDO_BIN) cmd/posix/make/make

$(POSIX_BIN) $(LINUX_BIN) $(NET_BIN) $(XSI_BIN) $(PSEUDO_BIN): $(LIB)

$(OBJ) $(POSIX_BIN) $(LINUX_BIN) $(NET_BIN) $(XSI_BIN) $(PSEUDO_BIN): $(HDR)

.o:
	$(CC) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ -c $<

.c:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LIB) $(LDLIBS)

cmd/posix/bc.c: cmd/posix/bc.y
	$(YACC) -d -o $@ $<

cmd/posix/bc: cmd/posix/bc.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ cmd/posix/bc.c $(LIB) $(LDLIBS)

$(MAKEOBJ): cmd/posix/make/make.h

cmd/posix/make/make: $(MAKEOBJ) $(LIB)
	$(CC) $(LDFLAGS) -o $@ $(MAKEOBJ) $(LIB) $(LDLIBS)

shared/libutf/libutf.a: $(LIBUTFOBJ)
	$(AR) $(ARFLAGS) $@ $?
	$(RANLIB) $@

shared/libutil/libutil.a: $(LIBUTILOBJ)
	$(AR) $(ARFLAGS) $@ $?
	$(RANLIB) $@

cmd/posix/getconf: cmd/posix/getconf.h

cmd/posix/getconf.h:
	scripts/getconf.sh > $@ || { rm -f $@; exit 1; }

box: $(LIB)
	CC='$(CC)' CPPFLAGS='$(CPPFLAGS)' CFLAGS='$(CFLAGS)' \
	LDFLAGS='$(LDFLAGS)' LDLIBS='$(LDLIBS)' OBJCOPY='$(OBJCOPY)' \
	scripts/mkbox

clean:
	rm -f shared/libutf/*.o shared/libutil/*.o cmd/posix/make/*.o
	rm -f $(POSIX_BIN) $(LINUX_BIN) $(NET_BIN) $(XSI_BIN) $(PSEUDO_BIN) $(LIB)
	rm -f cmd/posix/make/make cmd/posix/getconf.h cmd/posix/bc.c
	rm -rf aruu-box .box
