#!/usr/make
#
# Makefile for SQLITE
#
# This makefile is suppose to be configured automatically using the
# autoconf.  But if that does not work for you, you can configure
# the makefile manually.  Just set the parameters below to values that
# work well for your system.
#
# If the configure script does not work out-of-the-box, you might
# be able to get it to work by giving it some hints.  See the comment
# at the beginning of configure.in for additional information.
#

# The toplevel directory of the source tree.  This is the directory
# that contains this "Makefile.in" and the "configure.in" script.
#
TOP = .

# C Compiler and options for use in building executables that
# will run on the platform that is doing the build.
#
BCC = gcc -g -O0

# C Compile and options for use in building executables that 
# will run on the target platform.  (BCC and TCC are usually the
# same unless your are cross-compiling.)
#
TCC = gcc -g -O0 -DOS_UNIX=0 -DOS_WIN=1 -DHAVE_USLEEP=1 -I. -I${TOP}/src

# Some standard variables and programs
#
prefix = /usr/local
exec_prefix = ${prefix}
INSTALL = /usr/bin/install -c
LIBTOOL = ./libtool
RELEASE = 

# libtool compile/link/install
LTCOMPILE = $(LIBTOOL) --mode=compile $(TCC)
LTLINK = $(LIBTOOL) --mode=link $(TCC)
LTINSTALL = $(LIBTOOL) --mode=install $(INSTALL)

# Compiler options needed for programs that use the TCL library.
#
TCL_FLAGS = 

# The library that programs using TCL must link against.
#
LIBTCL = -ltcl  

# Compiler options needed for programs that use the readline() library.
#
READLINE_FLAGS = -DHAVE_READLINE=0 

# The library that programs using readline() must link against.
#
LIBREADLINE = -lreadline -lncurses 

# Should the database engine assume text is coded as UTF-8 or iso8859?
#
# ENCODING  = UTF8
# ENCODING  = ISO8859
ENCODING = ISO8859

# Flags controlling use of the in memory btree implementation
#
# SQLITE_OMIT_INMEMORYDB is defined in order to omit the in-memory
# red/black tree driver in the file btree_rb.c
#
# TEMP_STORE is 0 to force temporary tables to be in a file, 1 to
# default to file, 2 to default to memory, and 3 to force temporary
# tables to always be in memory.
#
INMEMORYDB = 1
INCOREFLAGS = -DTEMP_STORE=1

ifeq (${INMEMORYDB},0)
INCOREFLAGS += -DSQLITE_OMIT_INMEMORYDB=1
endif

# You should not have to change anything below this line
###############################################################################

# Object files for the SQLite library.
#
LIBOBJ = attach.lo auth.lo btree.lo build.lo date.lo delete.lo \
         expr.lo func.lo hash.lo insert.lo \
         main.lo opcodes.lo os_mac.lo os_unix.lo os_win.lo \
         pager.lo parse.lo pragma.lo printf.lo random.lo \
         select.lo table.lo tokenize.lo trigger.lo update.lo util.lo vacuum.lo \
         vdbe.lo vdbeapi.lo vdbeaux.lo vdbemem.lo \
         where.lo utf.lo legacy.lo

# All of the source code files.
#
SRC = \
  $(TOP)/src/attach.c \
  $(TOP)/src/auth.c \
  $(TOP)/src/btree.c \
  $(TOP)/src/btree.h \
  $(TOP)/src/build.c \
  $(TOP)/src/date.c \
  $(TOP)/src/delete.c \
  $(TOP)/src/encode.c \
  $(TOP)/src/expr.c \
  $(TOP)/src/func.c \
  $(TOP)/src/hash.c \
  $(TOP)/src/hash.h \
  $(TOP)/src/insert.c \
  $(TOP)/src/legacy.c \
  $(TOP)/src/main.c \
  $(TOP)/src/os_mac.c \
  $(TOP)/src/os_unix.c \
  $(TOP)/src/os_win.c \
  $(TOP)/src/pager.c \
  $(TOP)/src/pager.h \
  $(TOP)/src/parse.y \
  $(TOP)/src/pragma.c \
  $(TOP)/src/printf.c \
  $(TOP)/src/random.c \
  $(TOP)/src/select.c \
  $(TOP)/src/shell.c \
  $(TOP)/src/sqlite.h.in \
  $(TOP)/src/sqliteInt.h \
  $(TOP)/src/table.c \
  $(TOP)/src/tclsqlite.c \
  $(TOP)/src/tokenize.c \
  $(TOP)/src/trigger.c \
  $(TOP)/src/utf.c \
  $(TOP)/src/update.c \
  $(TOP)/src/util.c \
  $(TOP)/src/vacuum.c \
  $(TOP)/src/vdbe.c \
  $(TOP)/src/vdbe.h \
  $(TOP)/src/vdbeapi.c \
  $(TOP)/src/vdbeaux.c \
  $(TOP)/src/vdbemem.c \
  $(TOP)/src/vdbeInt.h \
  $(TOP)/src/where.c

# Source code to the test files.
#
TESTSRC = \
  $(TOP)/src/btree.c \
  $(TOP)/src/func.c \
  $(TOP)/src/os_mac.c \
  $(TOP)/src/os_unix.c \
  $(TOP)/src/os_win.c \
  $(TOP)/src/pager.c \
  $(TOP)/src/test1.c \
  $(TOP)/src/test2.c \
  $(TOP)/src/test3.c \
  $(TOP)/src/test4.c \
  $(TOP)/src/test5.c \
  $(TOP)/src/vdbe.c \
  $(TOP)/src/md5.c

# Header files used by all library source files.
#
HDR = \
   sqlite3.h  \
   $(TOP)/src/btree.h \
   config.h \
   $(TOP)/src/hash.h \
   opcodes.h \
   $(TOP)/src/os.h \
   $(TOP)/src/os_common.h \
   $(TOP)/src/os_mac.h \
   $(TOP)/src/os_unix.h \
   $(TOP)/src/os_win.h \
   $(TOP)/src/sqliteInt.h  \
   $(TOP)/src/vdbe.h \
   parse.h

# Header files used by the VDBE submodule
#
VDBEHDR = \
   $(HDR) \
   $(TOP)/src/vdbeInt.h

# This is the default Makefile target.  The objects listed here
# are what get build when you type just "make" with no arguments.
#
all:	sqlite3.h libsqlite3.la sqlite3.exe

Makefile: $(TOP)/Makefile.in
	./config.status

# Generate the file "last_change" which contains the date of change
# of the most recently modified source code file
#
last_change:	$(SRC)
	cat $(SRC) | grep '$$Id: ' | sort +4 | tail -1 \
          | awk '{print $$5,$$6}' >last_change

libsqlite3.la:	$(LIBOBJ)
	$(LTLINK) -o libsqlite3.la $(LIBOBJ) ${RELEASE} -rpath ${prefix}/lib \
		-version-info "8:6:8"

libtclsqlite3.la:	tclsqlite.lo libsqlite3.la
	$(LTLINK) -o libtclsqlite3.la tclsqlite.lo \
		libsqlite3.la $(LIBTCL) -rpath ${prefix}/lib/sqlite \
		-version-info "8:6:8"

sqlite3.exe:	$(TOP)/src/shell.c libsqlite3.la sqlite3.h
	$(LTLINK) $(READLINE_FLAGS) -o sqlite3 $(TOP)/src/shell.c \
		libsqlite3.la $(LIBREADLINE)

# This target creates a directory named "tsrc" and fills it with
# copies of all of the C source code and header files needed to
# build on the target system.  Some of the C source code and header
# files are automatically generated.  This target takes care of
# all that automatic generation.
#
target_source:	$(SRC) $(VDBEHDR) 
	rm -rf tsrc
	mkdir tsrc
	cp $(SRC) $(VDBEHDR) tsrc
	rm tsrc/sqlite.h.in tsrc/parse.y
	cp parse.c opcodes.c tsrc

# Rules to build the LEMON compiler generator
#
lemon.exe:	$(TOP)/tool/lemon.c $(TOP)/tool/lempar.c
	$(BCC) -o lemon $(TOP)/tool/lemon.c
	cp $(TOP)/tool/lempar.c .


# Rules to build individual files
#
attach.lo:	$(TOP)/src/attach.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/attach.c

auth.lo:	$(TOP)/src/auth.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/auth.c

btree.lo:	$(TOP)/src/btree.c $(HDR) $(TOP)/src/pager.h
	$(LTCOMPILE) -c $(TOP)/src/btree.c

build.lo:	$(TOP)/src/build.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/build.c

# The config.h file will contain a single #define that tells us how
# many bytes are in a pointer.  This only works if a pointer is the
# same size on the host as it is on the target.  If you are cross-compiling
# to a target with a different pointer size, you'll need to manually
# configure the config.h file.
#
config.h:	
	echo '#include <stdio.h>' >temp.c
	echo 'int main(){printf(' >>temp.c
	echo '"#define SQLITE_PTR_SZ %d",sizeof(char*));' >>temp.c
	echo 'exit(0);}' >>temp.c
	$(BCC) -o temp temp.c
	./temp >config.h
	echo >>config.h
	rm -f temp.c temp

date.lo:	$(TOP)/src/date.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/date.c

delete.lo:	$(TOP)/src/delete.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/delete.c

encode.lo:	$(TOP)/src/encode.c
	$(LTCOMPILE) -c $(TOP)/src/encode.c

expr.lo:	$(TOP)/src/expr.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/expr.c

func.lo:	$(TOP)/src/func.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/func.c

hash.lo:	$(TOP)/src/hash.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/hash.c

insert.lo:	$(TOP)/src/insert.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/insert.c

legacy.lo:	$(TOP)/src/legacy.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/legacy.c

main.lo:	$(TOP)/src/main.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/main.c

pager.lo:	$(TOP)/src/pager.c $(HDR) $(TOP)/src/pager.h
	$(LTCOMPILE) -c $(TOP)/src/pager.c

opcodes.lo:	opcodes.c
	$(LTCOMPILE) -c opcodes.c

opcodes.c:	$(TOP)/src/vdbe.c
	echo '/* Automatically generated file.  Do not edit */' >opcodes.c
	echo 'char *sqlite3OpcodeNames[] = { "???", ' >>opcodes.c
	grep '^case OP_' $(TOP)/src/vdbe.c | \
	  sed -e 's/^.*OP_/  "/' -e 's/:.*$$/", /' >>opcodes.c
	echo '};' >>opcodes.c

opcodes.h:	$(TOP)/src/vdbe.h
	echo '/* Automatically generated file.  Do not edit */' >opcodes.h
	grep '^case OP_' $(TOP)/src/vdbe.c | \
	  sed -e 's/://' | \
	  awk '{printf "#define %-30s %3d\n", $$2, ++cnt}' >>opcodes.h

os_mac.lo:	$(TOP)/src/os_mac.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/os_mac.c

os_unix.lo:	$(TOP)/src/os_unix.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/os_unix.c

os_win.lo:	$(TOP)/src/os_win.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/os_win.c

parse.lo:	parse.c $(HDR)
	$(LTCOMPILE) -c parse.c

parse.h:	parse.c

parse.c:	$(TOP)/src/parse.y lemon
	cp $(TOP)/src/parse.y .
	./lemon parse.y

pragma.lo:	$(TOP)/src/pragma.c $(HDR)
	$(LTCOMPILE) $(TCL_FLAGS) -c $(TOP)/src/pragma.c

printf.lo:	$(TOP)/src/printf.c $(HDR)
	$(LTCOMPILE) $(TCL_FLAGS) -c $(TOP)/src/printf.c

random.lo:	$(TOP)/src/random.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/random.c

select.lo:	$(TOP)/src/select.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/select.c

sqlite3.h:	$(TOP)/src/sqlite.h.in 
	sed -e s/--VERS--/`cat ${TOP}/VERSION`/ \
            -e s/--ENCODING--/$(ENCODING)/ \
                 $(TOP)/src/sqlite.h.in >sqlite3.h

table.lo:	$(TOP)/src/table.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/table.c

tclsqlite.lo:	$(TOP)/src/tclsqlite.c $(HDR)
	$(LTCOMPILE) $(TCL_FLAGS) -c $(TOP)/src/tclsqlite.c

tokenize.lo:	$(TOP)/src/tokenize.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/tokenize.c

trigger.lo:	$(TOP)/src/trigger.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/trigger.c

update.lo:	$(TOP)/src/update.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/update.c

utf.lo:	$(TOP)/src/utf.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/utf.c

util.lo:	$(TOP)/src/util.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/util.c

vacuum.lo:	$(TOP)/src/vacuum.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/vacuum.c

vdbe.lo:	$(TOP)/src/vdbe.c $(VDBEHDR)
	$(LTCOMPILE) -c $(TOP)/src/vdbe.c

vdbeapi.lo:	$(TOP)/src/vdbeapi.c $(VDBEHDR)
	$(LTCOMPILE) -c $(TOP)/src/vdbeapi.c

vdbeaux.lo:	$(TOP)/src/vdbeaux.c $(VDBEHDR)
	$(LTCOMPILE) -c $(TOP)/src/vdbeaux.c

vdbemem.lo:	$(TOP)/src/vdbemem.c $(VDBEHDR)
	$(LTCOMPILE) -c $(TOP)/src/vdbemem.c

where.lo:	$(TOP)/src/where.c $(HDR)
	$(LTCOMPILE) -c $(TOP)/src/where.c

tclsqlite-sh.lo:	$(TOP)/src/tclsqlite.c $(HDR)
	$(LTCOMPILE) $(TCL_FLAGS) -DTCLSH=1 -o $@ -c $(TOP)/src/tclsqlite.c

tclsqlite3:	tclsqlite-sh.lo libsqlite3.la
	$(LTLINK) $(TCL_FLAGS) -o tclsqlite3 tclsqlite-sh.lo \
		 libsqlite3.la $(LIBTCL)

testfixture.exe:	$(TOP)/src/tclsqlite.c libtclsqlite3.la libsqlite3.la $(TESTSRC)
	$(LTLINK) $(TCL_FLAGS) -DTCLSH=1 -DSQLITE_TEST=1\
                -o testfixture $(TESTSRC) $(TOP)/src/tclsqlite.c \
		libtclsqlite3.la libsqlite3.la $(LIBTCL)

fulltest:	testfixture.exe sqlite.exe
	./testfixture $(TOP)/test/all.test

test:	testfixture.exe sqlite.exe
	./testfixture $(TOP)/test/quick.test


# Rules used to build documentation
#
arch.html:	$(TOP)/www/arch.tcl
	tclsh $(TOP)/www/arch.tcl >arch.html

arch.png:	$(TOP)/www/arch.png
	cp $(TOP)/www/arch.png .

c_interface.html:	$(TOP)/www/c_interface.tcl
	tclsh $(TOP)/www/c_interface.tcl >c_interface.html

capi3.html:	$(TOP)/www/capi3.tcl
	tclsh $(TOP)/www/capi3.tcl >capi3.html

capi3ref.html:	$(TOP)/www/capi3ref.tcl
	tclsh $(TOP)/www/capi3ref.tcl >capi3ref.html

changes.html:	$(TOP)/www/changes.tcl
	tclsh $(TOP)/www/changes.tcl >changes.html

copyright.html:	$(TOP)/www/copyright.tcl
	tclsh $(TOP)/www/copyright.tcl >copyright.html

copyright-release.html:	$(TOP)/www/copyright-release.html
	cp $(TOP)/www/copyright-release.html .

copyright-release.pdf:	$(TOP)/www/copyright-release.pdf
	cp $(TOP)/www/copyright-release.pdf .

common.tcl:	$(TOP)/www/common.tcl
	cp $(TOP)/www/common.tcl .

conflict.html:	$(TOP)/www/conflict.tcl
	tclsh $(TOP)/www/conflict.tcl >conflict.html

datatypes.html:	$(TOP)/www/datatypes.tcl
	tclsh $(TOP)/www/datatypes.tcl >datatypes.html

datatype3.html:	$(TOP)/www/datatype3.tcl
	tclsh $(TOP)/www/datatype3.tcl >datatype3.html

docs.html:	$(TOP)/www/docs.tcl
	tclsh $(TOP)/www/docs.tcl >docs.html

download.html:	$(TOP)/www/download.tcl
	tclsh $(TOP)/www/download.tcl >download.html

faq.html:	$(TOP)/www/faq.tcl
	tclsh $(TOP)/www/faq.tcl >faq.html

fileformat.html:	$(TOP)/www/fileformat.tcl
	tclsh $(TOP)/www/fileformat.tcl >fileformat.html

formatchng.html:	$(TOP)/www/formatchng.tcl
	tclsh $(TOP)/www/formatchng.tcl >formatchng.html

index.html:	$(TOP)/www/index.tcl last_change
	tclsh $(TOP)/www/index.tcl >index.html

lang.html:	$(TOP)/www/lang.tcl
	tclsh $(TOP)/www/lang.tcl >lang.html

lockingv3.html:	$(TOP)/www/lockingv3.tcl
	tclsh $(TOP)/www/lockingv3.tcl >lockingv3.html

oldnews.html:	$(TOP)/www/oldnews.tcl
	tclsh $(TOP)/www/oldnews.tcl >oldnews.html

omitted.html:	$(TOP)/www/omitted.tcl
	tclsh $(TOP)/www/omitted.tcl >omitted.html

opcode.html:	$(TOP)/www/opcode.tcl $(TOP)/src/vdbe.c
	tclsh $(TOP)/www/opcode.tcl $(TOP)/src/vdbe.c >opcode.html

mingw.html:	$(TOP)/www/mingw.tcl
	tclsh $(TOP)/www/mingw.tcl >mingw.html

nulls.html:	$(TOP)/www/nulls.tcl
	tclsh $(TOP)/www/nulls.tcl >nulls.html

quickstart.html:	$(TOP)/www/quickstart.tcl
	tclsh $(TOP)/www/quickstart.tcl >quickstart.html

speed.html:	$(TOP)/www/speed.tcl
	tclsh $(TOP)/www/speed.tcl >speed.html

sqlite.gif:	$(TOP)/art/SQLite.gif
	cp $(TOP)/art/SQLite.gif sqlite.gif

sqlite.html:	$(TOP)/www/sqlite.tcl
	tclsh $(TOP)/www/sqlite.tcl >sqlite.html

support.html:	$(TOP)/www/support.tcl
	tclsh $(TOP)/www/support.tcl >support.html

tclsqlite.html:	$(TOP)/www/tclsqlite.tcl
	tclsh $(TOP)/www/tclsqlite.tcl >tclsqlite.html

vdbe.html:	$(TOP)/www/vdbe.tcl
	tclsh $(TOP)/www/vdbe.tcl >vdbe.html

version3.html:	$(TOP)/www/version3.tcl
	tclsh $(TOP)/www/version3.tcl >version3.html


# Files to be published on the website.
#
DOC = \
  arch.html \
  arch.png \
  c_interface.html \
  capi3.html \
  capi3ref.html \
  changes.html \
  copyright.html \
  copyright-release.html \
  copyright-release.pdf \
  conflict.html \
  datatypes.html \
  datatype3.html \
  docs.html \
  download.html \
  faq.html \
  fileformat.html \
  formatchng.html \
  index.html \
  lang.html \
  lockingv3.html \
  mingw.html \
  nulls.html \
  oldnews.html \
  omitted.html \
  opcode.html \
  quickstart.html \
  speed.html \
  sqlite.gif \
  sqlite.html \
  support.html \
  tclsqlite.html \
  vdbe.html \
  version3.html

doc:	$(DOC)
	mkdir -p doc
	mv $(DOC) doc

install:	sqlite3 libsqlite3.la sqlite3.h
	$(INSTALL) -d $(DESTDIR)$(exec_prefix)/lib
	$(LTINSTALL) libsqlite3.la $(DESTDIR)$(exec_prefix)/lib
	$(INSTALL) -d $(DESTDIR)$(exec_prefix)/bin
	$(LTINSTALL) sqlite3 $(DESTDIR)$(exec_prefix)/bin
	$(INSTALL) -d $(DESTDIR)$(prefix)/include
	$(INSTALL) -m 0644 sqlite3.h $(DESTDIR)$(prefix)/include
	$(INSTALL) -d $(DESTDIR)$(exec_prefix)/lib/pkgconfig; 
	$(INSTALL) -m 0644 sqlite.pc $(DESTDIR)$(exec_prefix)/lib/pkgconfig; 

clean:	
	rm -f *.lo *.la *.o sqlite3.exe libsqlite3.la
	rm -f sqlite3.h opcodes.*
	rm -rf .libs .deps 
	rm -f lemon.exe lempar.c parse.* sqlite*.tar.gz
	rm -f $(PUBLISH)
	rm -f *.da *.bb *.bbg gmon.out
	rm -f testfixture.exe test.db
	rm -rf doc
	rm -f sqlite.dll sqlite.lib

#
# Windows section; all this funky .dll stuff ;-)
#
dll: sqlite.dll

REAL_LIBOBJ = $(LIBOBJ:%.lo=.libs/%.o)

sqlite.dll: $(LIBOBJ) $(TOP)/sqlite.def
	dllwrap --dllname sqlite.dll --def $(TOP)/sqlite.def $(REAL_LIBOBJ)
	strip sqlite.dll

#target for dll import libraries
implib: sqlite.lib 

#make Borland C++ and/or Microsoft VC import library for the dll
#   ignore any errors (usually due to missing programs)
sqlite.lib: sqlite.dll
	-implib -a sqlite.lib sqlite.dll
	-lib /machine:i386 /def:$(TOP)/sqlite.def 

distclean:	clean
	rm -f config.log config.status libtool Makefile config.h
