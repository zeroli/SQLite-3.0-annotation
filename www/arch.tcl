#
# Run this Tcl script to generate the sqlite.html file.
#
set rcsid {$Id: arch.tcl,v 1.13 2004/06/16 03:02:04 drh Exp $}
source common.tcl
header {Architecture of SQLite}
puts {
<h2>The Architecture Of SQLite</h2>

<h3>Introduction</h3>

<table align="right" border="1" cellpadding="15" cellspacing="1">
<tr><th>Block Diagram Of SQLite</th></tr>
<tr><td><img src="arch2.gif"></td></tr>
</table>
<p>This document describes the architecture of the SQLite library.
The information here is useful to those who want to understand or
modify the inner workings of SQLite.
</p>

<p>
A block diagram showing the main components of SQLite
and how they interrelate is shown at the right.  The text that
follows will provide a quick overview of each of these components.
</p>

<h3>History</h3>

<p>
There are two main C interfaces to the SQLite library:
<b>sqlite_exec()</b> and <b>sqlite_compile()</b>.  Prior to
version 2.8.0 (2003-Feb-16) only sqlite_exec() was supported.
For version 2.8.0, the sqlite_exec and sqlite_compile methods
existed as peers.  Beginning with version 2.8.13, the sqlite_compile
method is the primary interface, and sqlite_exec is implemented
using sqlite_compile.  Externally, this change is an enhancement
that maintains backwards compatibility.  But internally,
the plumbing is very different.  The diagram at the right shows
the structure of SQLite for version 2.8.13 and following.
</p>

<p>
This document describes the structure for SQLite version 2.X.
SQLite version 3.0.0 introduces many new features and capabilities.
The basic architecture of the library remains the same.  However,
some of the details described here are different.  For example,
the code was in the file <b>os.c</b> has now been split out into
several file, on for each operating system.  And
the prefix on the names of API routines changed from <b>sqlite_</b>
to <b>sqlite3_</b>. 
</p>

<h3>Interface</h3>

<p>Much of the public interface to the SQLite library is implemented by
functions found in the <b>main.c</b> source file though some routines are
scattered about in other files where they can have access to data 
structures with file scope.  The
<b>sqlite_get_table()</b> routine is implemented in <b>table.c</b>.
<b>sqlite_step()</b> is found in <b>vdbe.c</b>.  
<b>sqlite_mprintf()</b> is found in <b>printf.c</b>.
The Tcl interface is implemented by <b>tclsqlite.c</b>.  More
information on the C interface to SQLite is
<a href="c_interface.html">available separately</a>.<p>

<p>To avoid name collisions with other software, all external
symbols in the SQLite library begin with the prefix <b>sqlite</b>.
Those symbols that are intended for external use (in other words,
those symbols which form the API for SQLite) begin
with <b>sqlite_</b>.</p>

<h3>SQL Command Processor</h3>

<p>

<h3>Tokenizer</h3>

<p>When a string containing SQL statements is to be executed, the
interface passes that string to the tokenizer.  The job of the tokenizer
is to break the original string up into tokens and pass those tokens
one by one to the parser.  The tokenizer is hand-coded in C.
All of the code for the tokenizer
is contained in the <b>tokenize.c</b> source file.</p>

<p>Note that in this design, the tokenizer calls the parser.  People
who are familiar with YACC and BISON may be used to doing things the
other way around -- having the parser call the tokenizer.  The author
of SQLite 
has done it both ways and finds things generally work out nicer for
the tokenizer to call the parser.  YACC has it backwards.</p>

<h3>Parser</h3>

<p>The parser is the piece that assigns meaning to tokens based on
their context.  The parser for SQLite is generated using the
<a href="http://www.hwaci.com/sw/lemon/">Lemon</a> LALR(1) parser
generator.  Lemon does the same job as YACC/BISON, but it uses
a different input syntax which is less error-prone.
Lemon also generates a parser which is reentrant and thread-safe.
And lemon defines the concept of a non-terminal destructor so
that it does not leak memory when syntax errors are encountered.
The source file that drives Lemon is found in <b>parse.y</b>.</p>

<p>Because
lemon is a program not normally found on development machines, the
complete source code to lemon (just one C file) is included in the
SQLite distribution in the "tool" subdirectory.  Documentation on
lemon is found in the "doc" subdirectory of the distribution.
</p>

<h3>Code Generator</h3>

<p>After the parser assembles tokens into complete SQL statements,
it calls the code generator to produce virtual machine code that
will do the work that the SQL statements request.  There are many
files in the code generator:  <b>build.c</b>, <b>copy.c</b>,
<b>delete.c</b>,
<b>expr.c</b>, <b>insert.c</b>, <b>pragma.c</b>,
<b>select.c</b>, <b>trigger.c</b>, <b>update.c</b>, <b>vacuum.c</b>
and <b>where.c</b>.
In these files is where most of the serious magic happens.
<b>expr.c</b> handles code generation for expressions.
<b>where.c</b> handles code generation for WHERE clauses on
SELECT, UPDATE and DELETE statements.  The files <b>copy.c</b>,
<b>delete.c</b>, <b>insert.c</b>, <b>select.c</b>, <b>trigger.c</b>
<b>update.c</b>, and <b>vacuum.c</b> handle the code generation
for SQL statements with the same names.  (Each of these files calls routines
in <b>expr.c</b> and <b>where.c</b> as necessary.)  All other
SQL statements are coded out of <b>build.c</b>.</p>

<h3>Virtual Machine</h3>

<p>The program generated by the code generator is executed by
the virtual machine.  Additional information about the virtual
machine is <a href="opcode.html">available separately</a>.
To summarize, the virtual machine implements an abstract computing
engine specifically designed to manipulate database files.  The
machine has a stack which is used for intermediate storage.
Each instruction contains an opcode and
up to three additional operands.</p>

<p>The virtual machine itself is entirely contained in a single
source file <b>vdbe.c</b>.  The virtual machine also has
its own header files: <b>vdbe.h</b> that defines an interface
between the virtual machine and the rest of the SQLite library and
<b>vdbeInt.h</b> which defines structure private the virtual machine.
The <b>vdbeaux.c</b> file contains utilities used by the virtual
machine and interface modules used by the rest of the library to
construct VM programs.</p>

<h3>Backend</h3>

<p>The backend is an abstraction layer that presents a uniform interface
to the virtual machine for either the B-Tree drivers for disk-based
databases or the Red/Black Tree driver for in-memory databases.
The <b>btree.h</b> source file contains the details.</p>

<h3>Red/Black Tree</h3>

<p>In SQLite version 2.X, in-memory databases are stored in a red/black
tree implementation contain in the <b>btree_rb.c</b> source file.  In
version 3.0, the red/black tree implementation is omitted.  In-memory
databases use the same B-Tree and pager code with a special flag that
tells the pager to maintain all data in cache and never write to the
disk.
</p>

<h3>B-Tree</h3>

<p>An SQLite database is maintained on disk using a B-tree implementation
found in the <b>btree.c</b> source file.  A separate B-tree is used for
each table and index in the database.  All B-trees are stored in the
same disk file.</p>

<p>The interface to the B-tree subsystem is defined by the header file
<b>btree.h</b>.
</p>

<h3>Page Cache</h3>

<p>The B-tree module requests information from the disk in 1024 byte
chunks.  The page cache is reponsible for reading, writing, and
caching these chunks.
The page cache also provides the rollback and atomic commit abstraction
and takes care of reader/writer locking of the database file.  The
B-tree driver requests particular pages from the page cache and notifies
the page cache when it wants to modify pages or commit or rollback
changes and the page cache handles all the messy details of making sure
the requests are handled quickly, safely, and efficiently.</p>

<p>The code to implement the page cache is contained in the single C
source file <b>pager.c</b>.  The interface to the page cache subsystem
is defined by the header file <b>pager.h</b>.
</p>

<h3>OS Interface</h3>

<p>
In order to provide portability between POSIX and Win32 operating systems,
SQLite uses an abstraction layer to interface with the operating system.
The <b>os.c</b> file contains about 20 routines used for opening and
closing files, deleting files, creating and deleting locks on files,
flushing the disk cache, and so forth.  Each of these functions contains
two implementations separated by #ifdefs: one for POSIX and the other
for Win32.  The interface to the OS abstraction layer is defined by
the <b>os.h</b> header file.
</p>
}
footer $rcsid
