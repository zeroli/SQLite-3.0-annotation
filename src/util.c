/*
** 2001 September 15
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** Utility functions used throughout sqlite.
**
** This file contains functions for allocating memory, comparing
** strings, and stuff like that.
**
** $Id: util.c,v 1.103 2004/06/18 04:24:55 danielk1977 Exp $
*/
#include "sqliteInt.h"
#include <stdarg.h>
#include <ctype.h>

#if SQLITE_DEBUG>2 && defined(__GLIBC__)
#include <execinfo.h>
void print_stack_trace(){
  void *bt[30];
  int i;
  int n = backtrace(bt, 30);

  fprintf(stderr, "STACK: ");
  for(i=0; i<n;i++){
    fprintf(stderr, "%p ", bt[i]);
  }
  fprintf(stderr, "\n");
}
#else
#define print_stack_trace()
#endif

/*
** If malloc() ever fails, this global variable gets set to 1.
** This causes the library to abort and never again function.
*/
int sqlite3_malloc_failed = 0;

/*
** If SQLITE_DEBUG is defined, then use versions of malloc() and
** free() that track memory usage and check for buffer overruns.
*/
#ifdef SQLITE_DEBUG

/*
** For keeping track of the number of mallocs and frees.   This
** is used to check for memory leaks.
*/
int sqlite3_nMalloc;         /* Number of sqliteMalloc() calls */
int sqlite3_nFree;           /* Number of sqliteFree() calls */
int sqlite3_iMallocFail;     /* Fail sqliteMalloc() after this many calls */
#if SQLITE_DEBUG>1
static int memcnt = 0;
#endif

/*
** Number of 32-bit guard words
*/
#define N_GUARD 1

/*
** Allocate new memory and set it to zero.  Return NULL if
** no memory is available.
*/
void *sqlite3Malloc_(int n, int bZero, char *zFile, int line){
  void *p;
  int *pi;
  int i, k;
  if( sqlite3_iMallocFail>=0 ){
    sqlite3_iMallocFail--;
    if( sqlite3_iMallocFail==0 ){
      sqlite3_malloc_failed++;
#if SQLITE_DEBUG>1
      fprintf(stderr,"**** failed to allocate %d bytes at %s:%d\n",
              n, zFile,line);
#endif
      sqlite3_iMallocFail--;
      return 0;
    }
  }
  if( n==0 ) return 0;
  k = (n+sizeof(int)-1)/sizeof(int);
  pi = malloc( (N_GUARD*2+1+k)*sizeof(int));
  if( pi==0 ){
    sqlite3_malloc_failed++;
    return 0;
  }
  sqlite3_nMalloc++;
  for(i=0; i<N_GUARD; i++) pi[i] = 0xdead1122;
  pi[N_GUARD] = n;
  for(i=0; i<N_GUARD; i++) pi[k+1+N_GUARD+i] = 0xdead3344;
  p = &pi[N_GUARD+1];
  memset(p, bZero==0, n);
#if SQLITE_DEBUG>1
  print_stack_trace();
  fprintf(stderr,"%06d malloc %d bytes at 0x%x from %s:%d\n",
      ++memcnt, n, (int)p, zFile,line);
#endif
  return p;
}

/*
** Check to see if the given pointer was obtained from sqliteMalloc()
** and is able to hold at least N bytes.  Raise an exception if this
** is not the case.
**
** This routine is used for testing purposes only.
*/
void sqlite3CheckMemory(void *p, int N){
  int *pi = p;
  int n, i, k;
  pi -= N_GUARD+1;
  for(i=0; i<N_GUARD; i++){
    assert( pi[i]==0xdead1122 );
  }
  n = pi[N_GUARD];
  assert( N>=0 && N<n );
  k = (n+sizeof(int)-1)/sizeof(int);
  for(i=0; i<N_GUARD; i++){
    assert( pi[k+N_GUARD+1+i]==0xdead3344 );
  }
}

/*
** Free memory previously obtained from sqliteMalloc()
*/
void sqlite3Free_(void *p, char *zFile, int line){
  if( p ){
    int *pi, i, k, n;
    pi = p;
    pi -= N_GUARD+1;
    sqlite3_nFree++;
    for(i=0; i<N_GUARD; i++){
      if( pi[i]!=0xdead1122 ){
        fprintf(stderr,"Low-end memory corruption at 0x%x\n", (int)p);
        return;
      }
    }
    n = pi[N_GUARD];
    k = (n+sizeof(int)-1)/sizeof(int);
    for(i=0; i<N_GUARD; i++){
      if( pi[k+N_GUARD+1+i]!=0xdead3344 ){
        fprintf(stderr,"High-end memory corruption at 0x%x\n", (int)p);
        return;
      }
    }
    memset(pi, 0xff, (k+N_GUARD*2+1)*sizeof(int));
#if SQLITE_DEBUG>1
    fprintf(stderr,"%06d free %d bytes at 0x%x from %s:%d\n",
         ++memcnt, n, (int)p, zFile,line);
#endif
    free(pi);
  }
}

/*
** Resize a prior allocation.  If p==0, then this routine
** works just like sqliteMalloc().  If n==0, then this routine
** works just like sqliteFree().
*/
void *sqlite3Realloc_(void *oldP, int n, char *zFile, int line){
  int *oldPi, *pi, i, k, oldN, oldK;
  void *p;
  if( oldP==0 ){
    return sqlite3Malloc_(n,1,zFile,line);
  }
  if( n==0 ){
    sqlite3Free_(oldP,zFile,line);
    return 0;
  }
  oldPi = oldP;
  oldPi -= N_GUARD+1;
  if( oldPi[0]!=0xdead1122 ){
    fprintf(stderr,"Low-end memory corruption in realloc at 0x%x\n", (int)oldP);
    return 0;
  }
  oldN = oldPi[N_GUARD];
  oldK = (oldN+sizeof(int)-1)/sizeof(int);
  for(i=0; i<N_GUARD; i++){
    if( oldPi[oldK+N_GUARD+1+i]!=0xdead3344 ){
      fprintf(stderr,"High-end memory corruption in realloc at 0x%x\n",
              (int)oldP);
      return 0;
    }
  }
  k = (n + sizeof(int) - 1)/sizeof(int);
  pi = malloc( (k+N_GUARD*2+1)*sizeof(int) );
  if( pi==0 ){
    sqlite3_malloc_failed++;
    return 0;
  }
  for(i=0; i<N_GUARD; i++) pi[i] = 0xdead1122;
  pi[N_GUARD] = n;
  for(i=0; i<N_GUARD; i++) pi[k+N_GUARD+1+i] = 0xdead3344;
  p = &pi[N_GUARD+1];
  memcpy(p, oldP, n>oldN ? oldN : n);
  if( n>oldN ){
    memset(&((char*)p)[oldN], 0, n-oldN);
  }
  memset(oldPi, 0xab, (oldK+N_GUARD+2)*sizeof(int));
  free(oldPi);
#if SQLITE_DEBUG>1
  print_stack_trace();
  fprintf(stderr,"%06d realloc %d to %d bytes at 0x%x to 0x%x at %s:%d\n",
    ++memcnt, oldN, n, (int)oldP, (int)p, zFile, line);
#endif
  return p;
}

/*
** Make a duplicate of a string into memory obtained from malloc()
** Free the original string using sqliteFree().
**
** This routine is called on all strings that are passed outside of
** the SQLite library.  That way clients can free the string using free()
** rather than having to call sqliteFree().
*/
void sqlite3StrRealloc(char **pz){
  char *zNew;
  if( pz==0 || *pz==0 ) return;
  zNew = malloc( strlen(*pz) + 1 );
  if( zNew==0 ){
    sqlite3_malloc_failed++;
    sqliteFree(*pz);
    *pz = 0;
  }
  strcpy(zNew, *pz);
  sqliteFree(*pz);
  *pz = zNew;
}

/*
** Make a copy of a string in memory obtained from sqliteMalloc()
*/
char *sqlite3StrDup_(const char *z, char *zFile, int line){
  char *zNew;
  if( z==0 ) return 0;
  zNew = sqlite3Malloc_(strlen(z)+1, 0, zFile, line);
  if( zNew ) strcpy(zNew, z);
  return zNew;
}
char *sqlite3StrNDup_(const char *z, int n, char *zFile, int line){
  char *zNew;
  if( z==0 ) return 0;
  zNew = sqlite3Malloc_(n+1, 0, zFile, line);
  if( zNew ){
    memcpy(zNew, z, n);
    zNew[n] = 0;
  }
  return zNew;
}

/*
** A version of sqliteFree that is always a function, not a macro.
*/
void sqlite3FreeX(void *p){
  sqliteFree(p);
}
#endif /* SQLITE_DEBUG */

/*
** The following versions of malloc() and free() are for use in a
** normal build.
*/
#if !defined(SQLITE_DEBUG)

/*
** Allocate new memory and set it to zero.  Return NULL if
** no memory is available.  See also sqliteMallocRaw().
*/
void *sqliteMalloc(int n){
  void *p;
  if( (p = malloc(n))==0 ){
    if( n>0 ) sqlite3_malloc_failed++;
  }else{
    memset(p, 0, n);
  }
  return p;
}

/*
** Allocate new memory but do not set it to zero.  Return NULL if
** no memory is available.  See also sqliteMalloc().
*/
void *sqliteMallocRaw(int n){
  void *p;
  if( (p = malloc(n))==0 ){
    if( n>0 ) sqlite3_malloc_failed++;
  }
  return p;
}

/*
** Free memory previously obtained from sqliteMalloc()
*/
void sqliteFree(void *p){
  if( p ){
    free(p);
  }
}

/*
** Resize a prior allocation.  If p==0, then this routine
** works just like sqliteMalloc().  If n==0, then this routine
** works just like sqliteFree().
*/
void *sqliteRealloc(void *p, int n){
  void *p2;
  if( p==0 ){
    return sqliteMalloc(n);
  }
  if( n==0 ){
    sqliteFree(p);
    return 0;
  }
  p2 = realloc(p, n);
  if( p2==0 ){
    sqlite3_malloc_failed++;
  }
  return p2;
}

/*
** Make a copy of a string in memory obtained from sqliteMalloc()
*/
char *sqliteStrDup(const char *z){
  char *zNew;
  if( z==0 ) return 0;
  zNew = sqliteMallocRaw(strlen(z)+1);
  if( zNew ) strcpy(zNew, z);
  return zNew;
}
char *sqliteStrNDup(const char *z, int n){
  char *zNew;
  if( z==0 ) return 0;
  zNew = sqliteMallocRaw(n+1);
  if( zNew ){
    memcpy(zNew, z, n);
    zNew[n] = 0;
  }
  return zNew;
}
#endif /* !defined(SQLITE_DEBUG) */

/*
** Create a string from the 2nd and subsequent arguments (up to the
** first NULL argument), store the string in memory obtained from
** sqliteMalloc() and make the pointer indicated by the 1st argument
** point to that string.  The 1st argument must either be NULL or 
** point to memory obtained from sqliteMalloc().
*/
void sqlite3SetString(char **pz, const char *zFirst, ...){
  va_list ap;
  int nByte;
  const char *z;
  char *zResult;

  if( pz==0 ) return;
  nByte = strlen(zFirst) + 1;
  va_start(ap, zFirst);
  while( (z = va_arg(ap, const char*))!=0 ){
    nByte += strlen(z);
  }
  va_end(ap);
  sqliteFree(*pz);
  *pz = zResult = sqliteMallocRaw( nByte );
  if( zResult==0 ){
    return;
  }
  strcpy(zResult, zFirst);
  zResult += strlen(zResult);
  va_start(ap, zFirst);
  while( (z = va_arg(ap, const char*))!=0 ){
    strcpy(zResult, z);
    zResult += strlen(zResult);
  }
  va_end(ap);
#ifdef SQLITE_DEBUG
#if SQLITE_DEBUG>1
  fprintf(stderr,"string at 0x%x is %s\n", (int)*pz, *pz);
#endif
#endif
}

/*
** Works like sqlite3SetString, but each string is now followed by
** a length integer which specifies how much of the source string 
** to copy (in bytes).  -1 means use the whole string.  The 1st 
** argument must either be NULL or point to memory obtained from 
** sqliteMalloc().
*/
void sqlite3SetNString(char **pz, ...){
  va_list ap;
  int nByte;
  const char *z;
  char *zResult;
  int n;

  if( pz==0 ) return;
  nByte = 0;
  va_start(ap, pz);
  while( (z = va_arg(ap, const char*))!=0 ){
    n = va_arg(ap, int);
    if( n<=0 ) n = strlen(z);
    nByte += n;
  }
  va_end(ap);
  sqliteFree(*pz);
  *pz = zResult = sqliteMallocRaw( nByte + 1 );
  if( zResult==0 ) return;
  va_start(ap, pz);
  while( (z = va_arg(ap, const char*))!=0 ){
    n = va_arg(ap, int);
    if( n<=0 ) n = strlen(z);
    memcpy(zResult, z, n);
    zResult += n;
  }
  *zResult = 0;
#ifdef SQLITE_DEBUG
#if SQLITE_DEBUG>1
  fprintf(stderr,"string at 0x%x is %s\n", (int)*pz, *pz);
#endif
#endif
  va_end(ap);
}

/*
** Set the most recent error code and error string for the sqlite
** handle "db". The error code is set to "err_code".
**
** If it is not NULL, string zFormat specifies the format of the
** error string in the style of the printf functions: The following
** format characters are allowed:
**
**      %s      Insert a string
**      %z      A string that should be freed after use
**      %d      Insert an integer
**      %T      Insert a token
**      %S      Insert the first element of a SrcList
**
** zFormat and any string tokens that follow it are assumed to be
** encoded in UTF-8.
**
** To clear the most recent error for slqite handle "db", sqlite3Error
** should be called with err_code set to SQLITE_OK and zFormat set
** to NULL.
*/
void sqlite3Error(sqlite *db, int err_code, const char *zFormat, ...){
  if( db && (db->pErr || (db->pErr = sqlite3ValueNew())) ){
    db->errCode = err_code;
    if( zFormat ){
      char *z;
      va_list ap;
      va_start(ap, zFormat);
      z = sqlite3VMPrintf(zFormat, ap);
      va_end(ap);
      sqlite3ValueSetStr(db->pErr, -1, z, SQLITE_UTF8, sqlite3FreeX);
    }else{
      sqlite3ValueSetStr(db->pErr, 0, 0, SQLITE_UTF8, SQLITE_STATIC);
    }
  }
}

/*
** Add an error message to pParse->zErrMsg and increment pParse->nErr.
** The following formatting characters are allowed:
**
**      %s      Insert a string
**      %z      A string that should be freed after use
**      %d      Insert an integer
**      %T      Insert a token
**      %S      Insert the first element of a SrcList
*/
void sqlite3ErrorMsg(Parse *pParse, const char *zFormat, ...){
  va_list ap;
  pParse->nErr++;
  sqliteFree(pParse->zErrMsg);
  va_start(ap, zFormat);
  pParse->zErrMsg = sqlite3VMPrintf(zFormat, ap);
  va_end(ap);
}

/*
** Convert an SQL-style quoted string into a normal string by removing
** the quote characters.  The conversion is done in-place.  If the
** input does not begin with a quote character, then this routine
** is a no-op.
**
** 2002-Feb-14: This routine is extended to remove MS-Access style
** brackets from around identifers.  For example:  "[a-b-c]" becomes
** "a-b-c".
*/
void sqlite3Dequote(char *z){
  int quote;
  int i, j;
  if( z==0 ) return;
  quote = z[0];
  switch( quote ){
    case '\'':  break;
    case '"':   break;
    case '[':   quote = ']';  break;
    default:    return;
  }
  for(i=1, j=0; z[i]; i++){
    if( z[i]==quote ){
      if( z[i+1]==quote ){
        z[j++] = quote;
        i++;
      }else{
        z[j++] = 0;
        break;
      }
    }else{
      z[j++] = z[i];
    }
  }
}

/* An array to map all upper-case characters into their corresponding
** lower-case character. 
*/
static unsigned char UpperToLower[] = {
      0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17,
     18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35,
     36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53,
     54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 97, 98, 99,100,101,102,103,
    104,105,106,107,108,109,110,111,112,113,114,115,116,117,118,119,120,121,
    122, 91, 92, 93, 94, 95, 96, 97, 98, 99,100,101,102,103,104,105,106,107,
    108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,
    126,127,128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,
    144,145,146,147,148,149,150,151,152,153,154,155,156,157,158,159,160,161,
    162,163,164,165,166,167,168,169,170,171,172,173,174,175,176,177,178,179,
    180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,195,196,197,
    198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,
    216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,
    234,235,236,237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,
    252,253,254,255
};

/*
** This function computes a hash on the name of a keyword.
** Case is not significant.
*/
int sqlite3HashNoCase(const char *z, int n){
  int h = 0;
  if( n<=0 ) n = strlen(z);
  while( n > 0  ){
    h = (h<<3) ^ h ^ UpperToLower[(unsigned char)*z++];
    n--;
  }
  return h & 0x7fffffff;
}

/*
** Some systems have stricmp().  Others have strcasecmp().  Because
** there is no consistency, we will define our own.
*/
int sqlite3StrICmp(const char *zLeft, const char *zRight){
  register unsigned char *a, *b;
  a = (unsigned char *)zLeft;
  b = (unsigned char *)zRight;
  while( *a!=0 && UpperToLower[*a]==UpperToLower[*b]){ a++; b++; }
  return *a - *b;
}
int sqlite3StrNICmp(const char *zLeft, const char *zRight, int N){
  register unsigned char *a, *b;
  a = (unsigned char *)zLeft;
  b = (unsigned char *)zRight;
  while( N-- > 0 && *a!=0 && UpperToLower[*a]==UpperToLower[*b]){ a++; b++; }
  return N<0 ? 0 : UpperToLower[*a] - UpperToLower[*b];
}

/*
** Return TRUE if z is a pure numeric string.  Return FALSE if the
** string contains any character which is not part of a number. If
** the string is numeric and contains the '.' character, set *realnum
** to TRUE (otherwise FALSE).
**
** Am empty string is considered non-numeric.
*/
int sqlite3IsNumber(const char *z, int *realnum, u8 enc){
  int incr = (enc==SQLITE_UTF8?1:2);
  if( enc==SQLITE_UTF16LE ) z++;
  if( *z=='-' || *z=='+' ) z += incr;
  if( !isdigit(*z) ){
    return 0;
  }
  z += incr;
  if( realnum ) *realnum = 0;
  while( isdigit(*z) ){ z += incr; }
  if( *z=='.' ){
    z += incr;
    if( !isdigit(*z) ) return 0;
    while( isdigit(*z) ){ z += incr; }
    if( realnum ) *realnum = 1;
  }
  if( *z=='e' || *z=='E' ){
    z += incr;
    if( *z=='+' || *z=='-' ) z += incr;
    if( !isdigit(*z) ) return 0;
    while( isdigit(*z) ){ z += incr; }
    if( realnum ) *realnum = 1;
  }
  return *z==0;
}

/*
** The string z[] is an ascii representation of a real number.
** Convert this string to a double.
**
** This routine assumes that z[] really is a valid number.  If it
** is not, the result is undefined.
**
** This routine is used instead of the library atof() function because
** the library atof() might want to use "," as the decimal point instead
** of "." depending on how locale is set.  But that would cause problems
** for SQL.  So this routine always uses "." regardless of locale.
*/
double sqlite3AtoF(const char *z, const char **pzEnd){
  int sign = 1;
  LONGDOUBLE_TYPE v1 = 0.0;
  if( *z=='-' ){
    sign = -1;
    z++;
  }else if( *z=='+' ){
    z++;
  }
  while( isdigit(*z) ){
    v1 = v1*10.0 + (*z - '0');
    z++;
  }
  if( *z=='.' ){
    LONGDOUBLE_TYPE divisor = 1.0;
    z++;
    while( isdigit(*z) ){
      v1 = v1*10.0 + (*z - '0');
      divisor *= 10.0;
      z++;
    }
    v1 /= divisor;
  }
  if( *z=='e' || *z=='E' ){
    int esign = 1;
    int eval = 0;
    LONGDOUBLE_TYPE scale = 1.0;
    z++;
    if( *z=='-' ){
      esign = -1;
      z++;
    }else if( *z=='+' ){
      z++;
    }
    while( isdigit(*z) ){
      eval = eval*10 + *z - '0';
      z++;
    }
    while( eval>=64 ){ scale *= 1.0e+64; eval -= 64; }
    while( eval>=16 ){ scale *= 1.0e+16; eval -= 16; }
    while( eval>=4 ){ scale *= 1.0e+4; eval -= 4; }
    while( eval>=1 ){ scale *= 1.0e+1; eval -= 1; }
    if( esign<0 ){
      v1 /= scale;
    }else{
      v1 *= scale;
    }
  }
  if( pzEnd ) *pzEnd = z;
  return sign<0 ? -v1 : v1;
}

/*
** Return TRUE if zNum is a 64-bit signed integer and write
** the value of the integer into *pNum.  If zNum is not an integer
** or is an integer that is too large to be expressed with 64 bits,
** then return false.  If n>0 and the integer is string is not
** exactly n bytes long, return false.
**
** When this routine was originally written it dealt with only
** 32-bit numbers.  At that time, it was much faster than the
** atoi() library routine in RedHat 7.2.
*/
int sqlite3atoi64(const char *zNum, i64 *pNum){
  i64 v = 0;
  int neg;
  int i, c;
  if( *zNum=='-' ){
    neg = 1;
    zNum++;
  }else if( *zNum=='+' ){
    neg = 0;
    zNum++;
  }else{
    neg = 0;
  }
  for(i=0; (c=zNum[i])>='0' && c<='9'; i++){
    v = v*10 + c - '0';
  }
  *pNum = neg ? -v : v;
  return c==0 && i>0 && 
      (i<19 || (i==19 && memcmp(zNum,"9223372036854775807",19)<=0));
}

/*
** The string zNum represents an integer.  There might be some other
** information following the integer too, but that part is ignored.
** If the integer that the prefix of zNum represents will fit in a
** 32-bit signed integer, return TRUE.  Otherwise return FALSE.
**
** This routine returns FALSE for the string -2147483648 even that
** that number will, in theory fit in a 32-bit integer.  But positive
** 2147483648 will not fit in 32 bits.  So it seems safer to return
** false.
*/
static int sqlite3FitsIn32Bits(const char *zNum){
  int i, c;
  if( *zNum=='-' || *zNum=='+' ) zNum++;
  for(i=0; (c=zNum[i])>='0' && c<='9'; i++){}
  return i<10 || (i==10 && memcmp(zNum,"2147483647",10)<=0);
}

/*
** If zNum represents an integer that will fit in 32-bits, then set
** *pValue to that integer and return true.  Otherwise return false.
*/
int sqlite3GetInt32(const char *zNum, int *pValue){
  if( sqlite3FitsIn32Bits(zNum) ){
    *pValue = atoi(zNum);
    return 1;
  }
  return 0;
}

/*
** The string zNum represents an integer.  There might be some other
** information following the integer too, but that part is ignored.
** If the integer that the prefix of zNum represents will fit in a
** 64-bit signed integer, return TRUE.  Otherwise return FALSE.
**
** This routine returns FALSE for the string -9223372036854775808 even that
** that number will, in theory fit in a 64-bit integer.  Positive
** 9223373036854775808 will not fit in 64 bits.  So it seems safer to return
** false.
*/
int sqlite3FitsIn64Bits(const char *zNum){
  int i, c;
  if( *zNum=='-' || *zNum=='+' ) zNum++;
  for(i=0; (c=zNum[i])>='0' && c<='9'; i++){}
  return i<19 || (i==19 && memcmp(zNum,"9223372036854775807",19)<=0);
}

/*
** If zNum represents an integer that will fit in 64-bits, then set
** *pValue to that integer and return true.  Otherwise return false.
*/
int sqlite3GetInt64(const char *zNum, i64 *pValue){
  if( sqlite3FitsIn64Bits(zNum) ){
    sqlite3atoi64(zNum, pValue);
    return 1;
  }
  return 0;
}

/* This comparison routine is what we use for comparison operations
** between numeric values in an SQL expression.  "Numeric" is a little
** bit misleading here.  What we mean is that the strings have a
** type of "numeric" from the point of view of SQL.  The strings
** do not necessarily contain numbers.  They could contain text.
**
** If the input strings both look like actual numbers then they
** compare in numerical order.  Numerical strings are always less 
** than non-numeric strings so if one input string looks like a
** number and the other does not, then the one that looks like
** a number is the smaller.  Non-numeric strings compare in 
** lexigraphical order (the same order as strcmp()).
*/
int sqlite3Compare(const char *atext, const char *btext){
  int result;
  int isNumA, isNumB;
  if( atext==0 ){
    return -1;
  }else if( btext==0 ){
    return 1;
  }
  isNumA = sqlite3IsNumber(atext, 0, SQLITE_UTF8);
  isNumB = sqlite3IsNumber(btext, 0, SQLITE_UTF8);
  if( isNumA ){
    if( !isNumB ){
      result = -1;
    }else{
      double rA, rB;
      rA = sqlite3AtoF(atext, 0);
      rB = sqlite3AtoF(btext, 0);
      if( rA<rB ){
        result = -1;
      }else if( rA>rB ){
        result = +1;
      }else{
        result = 0;
      }
    }
  }else if( isNumB ){
    result = +1;
  }else {
    result = strcmp(atext, btext);
  }
  return result; 
}

/*
** This routine is used for sorting.  Each key is a list of one or more
** null-terminated elements.  The list is terminated by two nulls in
** a row.  For example, the following text is a key with three elements
**
**            Aone\000Dtwo\000Athree\000\000
**
** All elements begin with one of the characters "+-AD" and end with "\000"
** with zero or more text elements in between.  Except, NULL elements
** consist of the special two-character sequence "N\000".
**
** Both arguments will have the same number of elements.  This routine
** returns negative, zero, or positive if the first argument is less
** than, equal to, or greater than the first.  (Result is a-b).
**
** Each element begins with one of the characters "+", "-", "A", "D".
** This character determines the sort order and collating sequence:
**
**     +      Sort numerically in ascending order
**     -      Sort numerically in descending order
**     A      Sort as strings in ascending order
**     D      Sort as strings in descending order.
**
** For the "+" and "-" sorting, pure numeric strings (strings for which the
** isNum() function above returns TRUE) always compare less than strings
** that are not pure numerics.  Non-numeric strings compare in memcmp()
** order.  This is the same sort order as the sqlite3Compare() function
** above generates.
**
** The last point is a change from version 2.6.3 to version 2.7.0.  In
** version 2.6.3 and earlier, substrings of digits compare in numerical 
** and case was used only to break a tie.
**
** Elements that begin with 'A' or 'D' compare in memcmp() order regardless
** of whether or not they look like a number.
**
** Note that the sort order imposed by the rules above is the same
** from the ordering defined by the "<", "<=", ">", and ">=" operators
** of expressions and for indices.  This was not the case for version
** 2.6.3 and earlier.
*/
int sqlite3SortCompare(const char *a, const char *b){
  int res = 0;
  int isNumA, isNumB;
  int dir = 0;

  while( res==0 && *a && *b ){
    if( a[0]=='N' || b[0]=='N' ){
      if( a[0]==b[0] ){
        a += 2;
        b += 2;
        continue;
      }
      if( a[0]=='N' ){
        dir = b[0];
        res = -1;
      }else{
        dir = a[0];
        res = +1;
      }
      break;
    }
    assert( a[0]==b[0] );
    if( (dir=a[0])=='A' || a[0]=='D' ){
      res = strcmp(&a[1],&b[1]);
      if( res ) break;
    }else{
      isNumA = sqlite3IsNumber(&a[1], 0, SQLITE_UTF8);
      isNumB = sqlite3IsNumber(&b[1], 0, SQLITE_UTF8);
      if( isNumA ){
        double rA, rB;
        if( !isNumB ){
          res = -1;
          break;
        }
        rA = sqlite3AtoF(&a[1], 0);
        rB = sqlite3AtoF(&b[1], 0);
        if( rA<rB ){
          res = -1;
          break;
        }
        if( rA>rB ){
          res = +1;
          break;
        }
      }else if( isNumB ){
        res = +1;
        break;
      }else{
        res = strcmp(&a[1],&b[1]);
        if( res ) break;
      }
    }
    a += strlen(&a[1]) + 2;
    b += strlen(&b[1]) + 2;
  }
  if( dir=='-' || dir=='D' ) res = -res;
  return res;
}

#if 1  /* We are now always UTF-8 */
/*
** X is a pointer to the first byte of a UTF-8 character.  Increment
** X so that it points to the next character.  This only works right
** if X points to a well-formed UTF-8 string.
*/
#define sqliteNextChar(X)  while( (0xc0&*++(X))==0x80 ){}
#define sqliteCharVal(X)   sqlite3ReadUtf8(X)

#else /* !defined(SQLITE_UTF8) */
/*
** For iso8859 encoding, the next character is just the next byte.
*/
#define sqliteNextChar(X)  (++(X));
#define sqliteCharVal(X)   ((int)*(X))

#endif /* defined(SQLITE_UTF8) */


#if 1  /* We are now always UTF-8 */
/*
** Convert the UTF-8 character to which z points into a 31-bit
** UCS character.  This only works right if z points to a well-formed
** UTF-8 string.
*/
int sqlite3ReadUtf8(const unsigned char *z){
  int c;
  static const int initVal[] = {
      0,   1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,
     15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,
     30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
     45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,  59,
     60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,  73,  74,
     75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,  87,  88,  89,
     90,  91,  92,  93,  94,  95,  96,  97,  98,  99, 100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118, 119,
    120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134,
    135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149,
    150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164,
    165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179,
    180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,   0,   1,   2,
      3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,
     18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,  31,   0,
      1,   2,   3,   4,   5,   6,   7,   8,   9,  10,  11,  12,  13,  14,  15,
      0,   1,   2,   3,   4,   5,   6,   7,   0,   1,   2,   3,   0,   1, 254,
    255,
  };
  c = initVal[*(z++)];
  while( (0xc0&*z)==0x80 ){
    c = (c<<6) | (0x3f&*(z++));
  }
  return c;
}
#endif

/*
** Compare two UTF-8 strings for equality where the first string can
** potentially be a "glob" expression.  Return true (1) if they
** are the same and false (0) if they are different.
**
** Globbing rules:
**
**      '*'       Matches any sequence of zero or more characters.
**
**      '?'       Matches exactly one character.
**
**     [...]      Matches one character from the enclosed list of
**                characters.
**
**     [^...]     Matches one character not in the enclosed list.
**
** With the [...] and [^...] matching, a ']' character can be included
** in the list by making it the first character after '[' or '^'.  A
** range of characters can be specified using '-'.  Example:
** "[a-z]" matches any single lower-case letter.  To match a '-', make
** it the last character in the list.
**
** This routine is usually quick, but can be N**2 in the worst case.
**
** Hints: to match '*' or '?', put them in "[]".  Like this:
**
**         abc[*]xyz        Matches "abc*xyz" only
*/
int 
sqlite3GlobCompare(const unsigned char *zPattern, const unsigned char *zString){
  register int c;
  int invert;
  int seen;
  int c2;

  while( (c = *zPattern)!=0 ){
    switch( c ){
      case '*':
        while( (c=zPattern[1]) == '*' || c == '?' ){
          if( c=='?' ){
            if( *zString==0 ) return 0;
            sqliteNextChar(zString);
          }
          zPattern++;
        }
        if( c==0 ) return 1;
        if( c=='[' ){
          while( *zString && sqlite3GlobCompare(&zPattern[1],zString)==0 ){
            sqliteNextChar(zString);
          }
          return *zString!=0;
        }else{
          while( (c2 = *zString)!=0 ){
            while( c2 != 0 && c2 != c ){ c2 = *++zString; }
            if( c2==0 ) return 0;
            if( sqlite3GlobCompare(&zPattern[1],zString) ) return 1;
            sqliteNextChar(zString);
          }
          return 0;
        }
      case '?': {
        if( *zString==0 ) return 0;
        sqliteNextChar(zString);
        zPattern++;
        break;
      }
      case '[': {
        int prior_c = 0;
        seen = 0;
        invert = 0;
        c = sqliteCharVal(zString);
        if( c==0 ) return 0;
        c2 = *++zPattern;
        if( c2=='^' ){ invert = 1; c2 = *++zPattern; }
        if( c2==']' ){
          if( c==']' ) seen = 1;
          c2 = *++zPattern;
        }
        while( (c2 = sqliteCharVal(zPattern))!=0 && c2!=']' ){
          if( c2=='-' && zPattern[1]!=']' && zPattern[1]!=0 && prior_c>0 ){
            zPattern++;
            c2 = sqliteCharVal(zPattern);
            if( c>=prior_c && c<=c2 ) seen = 1;
            prior_c = 0;
          }else if( c==c2 ){
            seen = 1;
            prior_c = c2;
          }else{
            prior_c = c2;
          }
          sqliteNextChar(zPattern);
        }
        if( c2==0 || (seen ^ invert)==0 ) return 0;
        sqliteNextChar(zString);
        zPattern++;
        break;
      }
      default: {
        if( c != *zString ) return 0;
        zPattern++;
        zString++;
        break;
      }
    }
  }
  return *zString==0;
}

/*
** Change the sqlite.magic from SQLITE_MAGIC_OPEN to SQLITE_MAGIC_BUSY.
** Return an error (non-zero) if the magic was not SQLITE_MAGIC_OPEN
** when this routine is called.
**
** This routine is a attempt to detect if two threads use the
** same sqlite* pointer at the same time.  There is a race 
** condition so it is possible that the error is not detected.
** But usually the problem will be seen.  The result will be an
** error which can be used to debug the application that is
** using SQLite incorrectly.
**
** Ticket #202:  If db->magic is not a valid open value, take care not
** to modify the db structure at all.  It could be that db is a stale
** pointer.  In other words, it could be that there has been a prior
** call to sqlite3_close(db) and db has been deallocated.  And we do
** not want to write into deallocated memory.
*/
int sqlite3SafetyOn(sqlite *db){
  if( db->magic==SQLITE_MAGIC_OPEN ){
    db->magic = SQLITE_MAGIC_BUSY;
    return 0;
  }else if( db->magic==SQLITE_MAGIC_BUSY || db->magic==SQLITE_MAGIC_ERROR
             || db->want_to_close ){
    db->magic = SQLITE_MAGIC_ERROR;
    db->flags |= SQLITE_Interrupt;
  }
  return 1;
}

/*
** Change the magic from SQLITE_MAGIC_BUSY to SQLITE_MAGIC_OPEN.
** Return an error (non-zero) if the magic was not SQLITE_MAGIC_BUSY
** when this routine is called.
*/
int sqlite3SafetyOff(sqlite *db){
  if( db->magic==SQLITE_MAGIC_BUSY ){
    db->magic = SQLITE_MAGIC_OPEN;
    return 0;
  }else if( db->magic==SQLITE_MAGIC_OPEN || db->magic==SQLITE_MAGIC_ERROR
             || db->want_to_close ){
    db->magic = SQLITE_MAGIC_ERROR;
    db->flags |= SQLITE_Interrupt;
  }
  return 1;
}

/*
** Check to make sure we are not currently executing an sqlite3_exec().
** If we are currently in an sqlite3_exec(), return true and set
** sqlite.magic to SQLITE_MAGIC_ERROR.  This will cause a complete
** shutdown of the database.
**
** This routine is used to try to detect when API routines are called
** at the wrong time or in the wrong sequence.
*/
int sqlite3SafetyCheck(sqlite *db){
  if( db->pVdbe!=0 ){
    db->magic = SQLITE_MAGIC_ERROR;
    return 1;
  }
  return 0;
}

/*
** The variable-length integer encoding is as follows:
**
** KEY:
**         A = 0xxxxxxx    7 bits of data and one flag bit
**         B = 1xxxxxxx    7 bits of data and one flag bit
**         C = xxxxxxxx    8 bits of data
**
**  7 bits - A
** 14 bits - BA
** 21 bits - BBA
** 28 bits - BBBA
** 35 bits - BBBBA
** 42 bits - BBBBBA
** 49 bits - BBBBBBA
** 56 bits - BBBBBBBA
** 64 bits - BBBBBBBBC
*/

/*
** Write a 64-bit variable-length integer to memory starting at p[0].
** The length of data write will be between 1 and 9 bytes.  The number
** of bytes written is returned.
**
** A variable-length integer consists of the lower 7 bits of each byte
** for all bytes that have the 8th bit set and one byte with the 8th
** bit clear.  Except, if we get to the 9th byte, it stores the full
** 8 bits and is the last byte.
*/
int sqlite3PutVarint(unsigned char *p, u64 v){
  int i, j, n;
  u8 buf[10];
  if( v & 0xff00000000000000 ){
    p[8] = v;
    v >>= 8;
    for(i=7; i>=0; i--){
      p[i] = (v & 0x7f) | 0x80;
      v >>= 7;
    }
    return 9;
  }    
  n = 0;
  do{
    buf[n++] = (v & 0x7f) | 0x80;
    v >>= 7;
  }while( v!=0 );
  buf[0] &= 0x7f;
  assert( n<=9 );
  for(i=0, j=n-1; j>=0; j--, i++){
    p[i] = buf[j];
  }
  return n;
}

/*
** Read a 64-bit variable-length integer from memory starting at p[0].
** Return the number of bytes read.  The value is stored in *v.
*/
int sqlite3GetVarint(const unsigned char *p, u64 *v){
  u32 x;
  u64 x64;
  int n;
  unsigned char c;
  if( ((c = p[0]) & 0x80)==0 ){
    *v = c;
    return 1;
  }
  x = c & 0x7f;
  if( ((c = p[1]) & 0x80)==0 ){
    *v = (x<<7) | c;
    return 2;
  }
  x = (x<<7) | (c&0x7f);
  if( ((c = p[2]) & 0x80)==0 ){
    *v = (x<<7) | c;
    return 3;
  }
  x = (x<<7) | (c&0x7f);
  if( ((c = p[3]) & 0x80)==0 ){
    *v = (x<<7) | c;
    return 4;
  }
  x64 = (x<<7) | (c&0x7f);
  n = 4;
  do{
    c = p[n++];
    if( n==9 ){
      x64 = (x64<<8) | c;
      break;
    }
    x64 = (x64<<7) | (c&0x7f);
  }while( (c & 0x80)!=0 );
  *v = x64;
  return n;
}

/*
** Read a 32-bit variable-length integer from memory starting at p[0].
** Return the number of bytes read.  The value is stored in *v.
*/
int sqlite3GetVarint32(const unsigned char *p, u32 *v){
  u32 x;
  int n;
  unsigned char c;
  if( ((c = p[0]) & 0x80)==0 ){
    *v = c;
    return 1;
  }
  x = c & 0x7f;
  if( ((c = p[1]) & 0x80)==0 ){
    *v = (x<<7) | c;
    return 2;
  }
  x = (x<<7) | (c&0x7f);
  if( ((c = p[2]) & 0x80)==0 ){
    *v = (x<<7) | c;
    return 3;
  }
  x = (x<<7) | (c&0x7f);
  if( ((c = p[3]) & 0x80)==0 ){
    *v = (x<<7) | c;
    return 4;
  }
  n = 4;
  do{
    x = (x<<7) | ((c = p[n++])&0x7f);
  }while( (c & 0x80)!=0 && n<9 );
  *v = x;
  return n;
}

/*
** Return the number of bytes that will be needed to store the given
** 64-bit integer.
*/
int sqlite3VarintLen(u64 v){
  int i = 0;
  do{
    i++;
    v >>= 7;
  }while( v!=0 && i<9 );
  return i;
}

void *sqlite3HexToBlob(const char *z){
  char *zBlob;
  int i;
  int n = strlen(z);
  if( n%2 ) return 0;

  zBlob = (char *)sqliteMalloc(n/2);

  for(i=0; i<n; i++){
    u8 c;

    if     ( z[i]>47 && z[i]<58 ) c = (z[i]-48)<<4;
    else if( z[i]>64 && z[i]<71 ) c = (z[i]-55)<<4;
    else if( z[i]>96 && z[i]<103 ) c = (z[i]-87)<<4;
    else {
      sqliteFree(zBlob);
      return 0;
    }
    i++;
    if     ( z[i]>47 && z[i]<58 ) c += (z[i]-48);
    else if( z[i]>64 && z[i]<71 ) c += (z[i]-55);
    else if( z[i]>96 && z[i]<103 ) c += (z[i]-87);
    else {
      sqliteFree(zBlob);
      return 0;
    }

    zBlob[i/2] = c;
  }
  return zBlob;
}
