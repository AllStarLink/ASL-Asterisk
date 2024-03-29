#
# Asterisk -- A telephony toolkit for Linux.
# 
# Makefile to build main Asterisk binary
#
# Copyright (C) 1999-2006, Digium, Inc.
#
# Mark Spencer <markster@digium.com>
#
# This program is free software, distributed under the terms of
# the GNU General Public License
#

-include $(ASTTOPDIR)/menuselect.makeopts $(ASTTOPDIR)/menuselect.makedeps $(ASTTOPDIR)/makeopts.embed_rules

all: asterisk

include $(ASTTOPDIR)/Makefile.moddir_rules

OBJS=	io.o sched.o logger.o frame.o loader.o config.o channel.o \
	translate.o file.o pbx.o cli.o md5.o term.o \
	ulaw.o alaw.o callerid.o fskmodem.o image.o app.o \
	cdr.o tdd.o acl.o rtp.o udptl.o manager.o asterisk.o \
	dsp.o chanvars.o indications.o autoservice.o db.o privacy.o \
	astmm.o enum.o srv.o dns.o aescrypt.o aestab.o aeskey.o \
	utils.o plc.o jitterbuf.o dnsmgr.o devicestate.o \
	netsock.o slinfactory.o ast_expr2.o ast_expr2f.o \
	cryptostub.o sha1.o http.o fixedjitterbuf.o abstract_jb.o \
	strcompat.o threadstorage.o dial.o astobj2.o global_datastores.o \
	audiohook.o

# we need to link in the objects statically, not as a library, because
# otherwise modules will not have them available if none of the static
# objects use it.
OBJS+=stdtime/localtime.o

# At the moment say.o is an optional component which can be overridden
# by a module.
OBJS+=say.o

ifneq ($(findstring darwin,$(OSARCH)),)
  OBJS+=poll.o
  ASTCFLAGS+=-DPOLLCOMPAT
else
  ifeq ($(wildcard /usr/include/sys/poll.h),)
    OBJS+=poll.o
    ASTCFLAGS+=-DPOLLCOMPAT
  endif
endif

ifneq ($(findstring $(OSARCH), linux-gnu uclinux linux-uclibc linux-gnueabi linux-gnueabihf ),)
  ifneq ($(findstring LOADABLE_MODULES,$(MENUSELECT_CFLAGS)),)
  AST_LIBS+=-ldl
  endif
  ifneq (x$(CAP_LIB),x)
    AST_LIBS+=$(CAP_LIB)
  endif
  AST_LIBS+=-lpthread $(EDITLINE_LIB) -lm -lresolv
else
  AST_LIBS+=$(EDITLINE_LIB) -lm
endif

ifneq ($(findstring darwin,$(OSARCH)),)
  AST_LIBS+=-lresolv
  ifneq ($(findstring LOADABLE_MODULES,$(MENUSELECT_CFLAGS)),)
    ASTLINK=-Wl,-dynamic
  endif
else
# These are used for all but Darwin
  ifneq ($(findstring LOADABLE_MODULES,$(MENUSELECT_CFLAGS)),)
    ASTLINK+=-Wl,--export-dynamic
  else
    ASTLINK+=${GC_LDFLAGS}
  endif
  ifneq ($(findstring BSD,$(OSARCH)),)
    LDFLAGS+=-L/usr/local/lib
  endif
endif

ifeq ($(OSARCH),FreeBSD)
  AST_LIBS+=-lcrypto
endif

ifeq ($(OSARCH),NetBSD)
  AST_LIBS+=-lpthread -lcrypto -lm -L/usr/pkg/lib $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),OpenBSD)
  AST_LIBS+=-lcrypto -lpthread -lm $(EDITLINE_LIB)
endif

ifeq ($(OSARCH),SunOS)
  AST_LIBS+=-lpthread -ldl -lnsl -lsocket -lresolv -L/opt/ssl/lib -L/usr/local/ssl/lib
  ASTLINK=
endif

editline/libedit.a:
	cd editline && test -f config.h || CFLAGS="$(PTHREAD_CFLAGS) $(subst $(ASTTOPDIR),../../,$(ASTCFLAGS:-Werror=))" LDFLAGS="$(ASTLDFLAGS)" ./configure --build=$(BUILD_PLATFORM) --host=$(HOST_PLATFORM) --with-ncurses=$(NCURSES_DIR) --with-curses=$(CURSES_DIR) --with-termcap=$(TERMCAP_DIR) --with-tinfo=$(TINFO_DIR)
	$(MAKE) -C editline libedit.a

db1-ast/libdb1.a:
	CFLAGS="$(subst $(ASTTOPDIR),../../,$(ASTCFLAGS))" LDFLAGS="$(ASTLDFLAGS)" $(MAKE) -C db1-ast libdb1.a

ast_expr2.c ast_expr2.h:
	bison -o $@ -d --name-prefix=ast_yy ast_expr2.y

ast_expr2f.c:
	flex -o $@ --full ast_expr2.fl # moved the correction of yyfree into the flex input file itself.
	sed 's@#if __STDC_VERSION__ >= 199901L@#if !defined __STDC_VERSION__ || __STDC_VERSION__ >= 199901L@' ast_expr2f.c > zz
	mv zz ast_expr2f.c

ast_expr2f.o: ASTCFLAGS+=-Wno-unused

testexpr2: ast_expr2f.c ast_expr2.c ast_expr2.h
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2f.c
	$(CC) -g -c -Iinclude -DSTANDALONE ast_expr2.c
	$(CC) -g -o testexpr2 ast_expr2f.o ast_expr2.o
	rm ast_expr2.o ast_expr2f.o 

asterisk.o channel.o: ASTCFLAGS+=$(ZAPTEL_INCLUDE) $(DAHDI_INCLUDE)

stdtime/localtime.o: ASTCFLAGS+=$(AST_NO_STRICT_OVERFLOW)

AST_EMBED_LDSCRIPTS:=$(sort $(EMBED_LDSCRIPTS))
AST_EMBED_LDFLAGS:=$(foreach dep,$(EMBED_LDFLAGS),$(value $(dep)))
AST_EMBED_LIBS:=$(foreach dep,$(EMBED_LIBS),$(value $(dep)))
OBJS:=$(sort $(OBJS))

ifneq ($(wildcard ../channels/h323/Makefile.ast),)
  include ../channels/h323/Makefile.ast
else
  H323LDFLAGS=
  H323LDLIBS=
endif

asterisk: $(OBJS) editline/libedit.a db1-ast/libdb1.a $(AST_EMBED_LDSCRIPTS)
	@$(ASTTOPDIR)/build_tools/make_build_h > $(ASTTOPDIR)/include/asterisk/build.h.tmp
	@if cmp -s $(ASTTOPDIR)/include/asterisk/build.h.tmp $(ASTTOPDIR)/include/asterisk/build.h ; then echo ; else \
		mv $(ASTTOPDIR)/include/asterisk/build.h.tmp $(ASTTOPDIR)/include/asterisk/build.h ; \
	fi
	@rm -f $(ASTTOPDIR)/include/asterisk/build.h.tmp
	@$(CC) -c -o buildinfo.o $(ASTCFLAGS) buildinfo.c
	$(ECHO_PREFIX) echo "   [LD] $^ -> $@"
ifneq ($(findstring chan_h323,$(MENUSELECT_CHANNELS)),)
	$(CMD_PREFIX) $(CC) $(STATIC_BUILD) -o $@ $(ASTLINK) $(AST_EMBED_LDFLAGS) $(ASTLDFLAGS) $^ buildinfo.o $(AST_LIBS) $(AST_EMBED_LIBS)
else
	$(CMD_PREFIX) $(CXX) $(STATIC_BUILD) -o $@ $(ASTLINK) $(AST_EMBED_LDFLAGS) $(ASTLDFLAGS) $(H323LDFLAGS) $^ buildinfo.o $(AST_LIBS) $(AST_EMBED_LIBS) $(H323LDLIBS)
endif
	$(CMD_PREFIX) $(ASTTOPDIR)/build_tools/strip_nonapi $@ || rm $@

clean::
	rm -f asterisk
	rm -f db1-ast/.*.d
	@if [ -f editline/Makefile ]; then $(MAKE) -C editline distclean ; fi
	@$(MAKE) -C db1-ast clean
	@$(MAKE) -C stdtime clean
