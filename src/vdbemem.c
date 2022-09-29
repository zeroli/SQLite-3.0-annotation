/*
** 2004 May 26
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
**
** This file contains code use to manipulate "Mem" structure.  A "Mem"
** stores a single value in the VDBE.  Mem is an opaque structure visible
** only within the VDBE.  Interface routines refer to a Mem using the
** name sqlite_value
*/
#include "sqliteInt.h"
#include "os.h"
#include <ctype.h>
#include "vdbeInt.h"

/*
** If pMem is an object with a valid string representation, this routine
** ensures the internal encoding for the string representation is
** 'desiredEnc', one of SQLITE_UTF8, SQLITE_UTF16LE or SQLITE_UTF16BE.
**
** If pMem is not a string object, or the encoding of the string
** representation is already stored using the requested encoding, then this
** routine is a no-op.
**
** SQLITE_OK is returned if the conversion is successful (or not required).
** SQLITE_NOMEM may be returned if a malloc() fails during conversion
** between formats.
*/
int sqlite3VdbeChangeEncoding(Mem *pMem, int desiredEnc){
  if( !(pMem->flags&MEM_Str) || pMem->enc==desiredEnc ){
    return SQLITE_OK;
  }
  return sqlite3VdbeMemTranslate(pMem, desiredEnc);
}

/*
** Make the given Mem object MEM_Dyn.
**
** Return SQLITE_OK on success or SQLITE_NOMEM if malloc fails.
*/
int sqlite3VdbeMemDynamicify(Mem *pMem){
  int n = pMem->n;
  u8 *z;
  if( (pMem->flags & (MEM_Ephem|MEM_Static|MEM_Short))==0 ){
    return SQLITE_OK;
  }
  assert( (pMem->flags & MEM_Dyn)==0 );
  assert( pMem->flags & (MEM_Str|MEM_Blob) );
  z = sqliteMallocRaw( n+2 );
  if( z==0 ){
    return SQLITE_NOMEM;
  }
  pMem->flags |= MEM_Dyn|MEM_Term;
  pMem->xDel = 0;
  memcpy(z, pMem->z, n );
  z[n] = 0;
  z[n+1] = 0;
  pMem->z = z;
  pMem->flags &= ~(MEM_Ephem|MEM_Static|MEM_Short);
  return SQLITE_OK;
}

/*
** Make the given Mem object either MEM_Short or MEM_Dyn so that bytes
** of the Mem.z[] array can be modified.
**
** Return SQLITE_OK on success or SQLITE_NOMEM if malloc fails.
*/
int sqlite3VdbeMemMakeWriteable(Mem *pMem){
  int n;
  u8 *z;
  if( (pMem->flags & (MEM_Ephem|MEM_Static))==0 ){
    return SQLITE_OK;
  }
  assert( (pMem->flags & MEM_Dyn)==0 );
  assert( pMem->flags & (MEM_Str|MEM_Blob) );
  if( (n = pMem->n)+2<sizeof(pMem->zShort) ){
    z = pMem->zShort;
    pMem->flags |= MEM_Short|MEM_Term;
  }else{
    z = sqliteMallocRaw( n+2 );
    if( z==0 ){
      return SQLITE_NOMEM;
    }
    pMem->flags |= MEM_Dyn|MEM_Term;
    pMem->xDel = 0;
  }
  memcpy(z, pMem->z, n );
  z[n] = 0;
  z[n+1] = 0;
  pMem->z = z;
  pMem->flags &= ~(MEM_Ephem|MEM_Static);
  return SQLITE_OK;
}

/*
** Make sure the given Mem is \u0000 terminated.
*/
int sqlite3VdbeMemNulTerminate(Mem *pMem){
  if( (pMem->flags & MEM_Term)!=0 || (pMem->flags & (MEM_Str|MEM_Blob))==0 ){
    return SQLITE_OK;   /* Nothing to do */
  }

  if( pMem->flags & (MEM_Static|MEM_Ephem) ){
    return sqlite3VdbeMemMakeWriteable(pMem);
  }else{
    if( pMem->flags & MEM_Dyn ){
      if( pMem->xDel ){
        char *z = sqliteMalloc(pMem->n+2);
        if( !z ) return SQLITE_NOMEM;
        memcpy(z, pMem->z, pMem->n);
        pMem->xDel(pMem->z);
        pMem->xDel = 0;
        pMem->z = z;
      }else{
        pMem->z = sqliteRealloc(pMem->z, pMem->n+2);
        if( !pMem->z ) return SQLITE_NOMEM;
      }
    }else{
      assert( pMem->flags & MEM_Short );
      if( pMem->n+2>NBFS ){
        char *z = sqliteMalloc(pMem->n+2);
        if( !z ) return SQLITE_NOMEM;
        memcpy(z, pMem->z, pMem->n);
        pMem->flags &= !(MEM_Short);
        pMem->flags |= MEM_Dyn;
        pMem->xDel = 0;
        pMem->z = z;
      }
    }
    pMem->z[pMem->n++] = 0;
    pMem->z[pMem->n++] = 0;
  }
  return SQLITE_OK;
}

/*
** Add MEM_Str to the set of representations for the given Mem.
** A NULL is converted into an empty string.  Numbers are converted
** using sqlite3_snprintf().  Converting a BLOB to a string is a
** no-op.
**
** Existing representations MEM_Int and MEM_Real are *not* invalidated.
** But MEM_Null is.
*/
int sqlite3VdbeMemStringify(Mem *pMem, int enc){
  int rc = SQLITE_OK;
  int fg = pMem->flags;

  assert( !(fg&(MEM_Str|MEM_Blob)) );
  assert( fg&(MEM_Int|MEM_Real|MEM_Null) );

  if( fg & MEM_Null ){      
    /* A NULL value is converted to a zero length string */
    u8 *z = pMem->zShort;
    z[0] = 0;
    z[1] = 0;
    pMem->flags = MEM_Str | MEM_Short | MEM_Term;
    pMem->z = z;
    pMem->n = 0;
    pMem->enc = enc;
  }else{
    /* For a Real or Integer, use sqlite3_snprintf() to produce the UTF-8
    ** string representation of the value. Then, if the required encoding
    ** is UTF-16le or UTF-16be do a translation.
    ** 
    ** FIX ME: It would be better if sqlite3_snprintf() could do UTF-16.
    */
    u8 *z = pMem->zShort;
    if( fg & MEM_Real || (pMem->type==SQLITE_FLOAT) ){
      sqlite3_snprintf(NBFS, z, "%.15g", pMem->r);
    }else{
      assert( fg & MEM_Int );
      sqlite3_snprintf(NBFS, z, "%lld", pMem->i);
    }
    pMem->n = strlen(z);
    pMem->z = z;
    pMem->enc = SQLITE_UTF8;
    pMem->flags |= MEM_Str | MEM_Short | MEM_Term;
    sqlite3VdbeChangeEncoding(pMem, enc);
  }
  return rc;
}

/*
** Release any memory held by the Mem. This may leave the Mem in an
** inconsistent state, for example with (Mem.z==0) and
** (Mem.type==SQLITE_TEXT).
*/
void sqlite3VdbeMemRelease(Mem *p){
  if( p->flags & MEM_Dyn ){
    if( p->xDel ){
      p->xDel((void *)p->z);
    }else{
      sqliteFree(p->z);
    }
    p->z = 0;
    p->xDel = 0;
  }
}

/*
** Convert the Mem to have representation MEM_Int only.  All
** prior representations are invalidated.  NULL is converted into 0.
*/
int sqlite3VdbeMemIntegerify(Mem *pMem){
  int flags = pMem->flags;
  if( flags & MEM_Int ){
    /* Do nothing */
  }else if( flags & MEM_Real ){
    pMem->i = (i64)pMem->r;
  }else if( flags & (MEM_Str|MEM_Blob) ){
    if( sqlite3VdbeChangeEncoding(pMem, SQLITE_UTF8)
       || sqlite3VdbeMemNulTerminate(pMem) ){
      return SQLITE_NOMEM;
    }
    assert( pMem->z );
    sqlite3atoi64(pMem->z, &pMem->i);
  }else{
    pMem->i = 0;
  }
  pMem->flags |= MEM_Int;
  return SQLITE_OK;
}

/*
** Add MEM_Real to the set of representations for pMem.  Prior
** prior representations other than MEM_Null retained.  NULL is
** converted into 0.0.
*/
int sqlite3VdbeMemRealify(Mem *pMem){
  if( pMem->flags & MEM_Real ){
    /* Do nothing */
  }else if( (pMem->flags & MEM_Int) && pMem->type!=SQLITE_TEXT ){
    pMem->r = pMem->i;
  }else if( pMem->flags & (MEM_Str|MEM_Blob) ){
    if( sqlite3VdbeChangeEncoding(pMem, SQLITE_UTF8)
       || sqlite3VdbeMemNulTerminate(pMem) ){
      return SQLITE_NOMEM;
    }
    assert( pMem->z );
    pMem->r = sqlite3AtoF(pMem->z, 0);
  }else{
    pMem->r = 0.0;
  }
  pMem->flags |= MEM_Real;
  return SQLITE_OK;
}

/*
** Delete any previous value and set the value stored in *pMem to NULL.
*/
void sqlite3VdbeMemSetNull(Mem *pMem){
  sqlite3VdbeMemRelease(pMem);
  pMem->flags = MEM_Null;
  pMem->type = SQLITE_NULL;
}

/*
** Delete any previous value and set the value stored in *pMem to val,
** manifest type INTEGER.
*/
void sqlite3VdbeMemSetInt64(Mem *pMem, i64 val){
  sqlite3VdbeMemRelease(pMem);
  pMem->i = val;
  pMem->flags = MEM_Int;
  pMem->type = SQLITE_INTEGER;
}

/*
** Delete any previous value and set the value stored in *pMem to val,
** manifest type REAL.
*/
void sqlite3VdbeMemSetDouble(Mem *pMem, double val){
  sqlite3VdbeMemRelease(pMem);
  pMem->r = val;
  pMem->flags = MEM_Real;
  pMem->type = SQLITE_FLOAT;
}

/*
** Copy the contents of memory cell pFrom into pTo.
*/
int sqlite3VdbeMemCopy(Mem *pTo, const Mem *pFrom){
  sqlite3VdbeMemRelease(pTo);
  memcpy(pTo, pFrom, sizeof(*pFrom)-sizeof(pFrom->zShort));
  pTo->xDel = 0;
  if( pTo->flags & (MEM_Str|MEM_Blob) ){
    pTo->flags &= ~(MEM_Dyn|MEM_Static|MEM_Short);
    pTo->flags |= MEM_Ephem;
    sqlite3VdbeMemMakeWriteable(pTo);
  }
  return SQLITE_OK;
}

/*
** Transfer the contents of pFrom to pTo. Any existing value in pTo is
** deleted. pFrom contains an SQL NULL when this routine returns.
*/
int sqlite3VdbeMemMove(Mem *pTo, Mem *pFrom){
  int rc = SQLITE_OK;
  if( !(pFrom->flags&MEM_Dyn && pFrom->xDel) ){
    memcpy(pTo, pFrom, sizeof(Mem));
    if( pFrom->flags & MEM_Short ){
      pTo->z = pTo->zShort;
    }
  }else{
    rc = sqlite3VdbeMemCopy(pTo, pFrom);
    sqlite3VdbeMemRelease(pFrom);
  }
  return SQLITE_OK;
}

/*
** Change the value of a Mem to be a string or a BLOB.
*/
int sqlite3VdbeMemSetStr(
  Mem *pMem,          /* Memory cell to set to string value */
  const char *z,      /* String pointer */
  int n,              /* Bytes in string, or negative */
  u8 enc,             /* Encoding of z.  0 for BLOBs */
  void (*xDel)(void*) /* Destructor function */
){
  sqlite3VdbeMemRelease(pMem);
  if( !z ){
    pMem->flags = MEM_Null;
    pMem->type = SQLITE_NULL;
    return SQLITE_OK;
  }

  pMem->z = (char *)z;
  if( xDel==SQLITE_STATIC ){
    pMem->flags = MEM_Static;
  }else if( xDel==SQLITE_TRANSIENT ){
    pMem->flags = MEM_Ephem;
  }else{
    pMem->flags = MEM_Dyn;
    pMem->xDel = xDel;
  }

  pMem->enc = enc;
  pMem->type = enc==0 ? SQLITE_BLOB : SQLITE_TEXT;
  pMem->n = n;

  switch( enc ){
    case 0:
      pMem->flags |= MEM_Blob;
      break;

    case SQLITE_UTF8:
      pMem->flags |= MEM_Str;
      if( n<0 ){
        pMem->n = strlen(z);
        pMem->flags |= MEM_Term;
      }
      break;

    case SQLITE_UTF16LE:
    case SQLITE_UTF16BE:
      pMem->flags |= MEM_Str;
      if( pMem->n<0 ){
        pMem->n = sqlite3utf16ByteLen(pMem->z,-1);
        pMem->flags |= MEM_Term;
      }
      if( sqlite3VdbeMemHandleBom(pMem) ){
        return SQLITE_NOMEM;
      }
      break;

    default:
      assert(0);
  }
  if( pMem->flags&MEM_Ephem ){
    return sqlite3VdbeMemMakeWriteable(pMem);
  }
  return SQLITE_OK;
}

/*
** Compare the values contained by the two memory cells, returning
** negative, zero or positive if pMem1 is less than, equal to, or greater
** than pMem2. Sorting order is NULL's first, followed by numbers (integers
** and reals) sorted numerically, followed by text ordered by the collating
** sequence pColl and finally blob's ordered by memcmp().
**
** Two NULL values are considered equal by this function.
*/
int sqlite3MemCompare(const Mem *pMem1, const Mem *pMem2, const CollSeq *pColl){
  int rc;
  int f1, f2;
  int combined_flags;

  /* Interchange pMem1 and pMem2 if the collating sequence specifies
  ** DESC order.
  */
  f1 = pMem1->flags;
  f2 = pMem2->flags;
  combined_flags = f1|f2;
 
  /* If one value is NULL, it is less than the other. If both values
  ** are NULL, return 0.
  */
  if( combined_flags&MEM_Null ){
    return (f2&MEM_Null) - (f1&MEM_Null);
  }

  /* If one value is a number and the other is not, the number is less.
  ** If both are numbers, compare as reals if one is a real, or as integers
  ** if both values are integers.
  */
  if( combined_flags&(MEM_Int|MEM_Real) ){
    if( !(f1&(MEM_Int|MEM_Real)) ){
      return 1;
    }
    if( !(f2&(MEM_Int|MEM_Real)) ){
      return -1;
    }
    if( (f1 & f2 & MEM_Int)==0 ){
      double r1, r2;
      if( (f1&MEM_Real)==0 ){
        r1 = pMem1->i;
      }else{
        r1 = pMem1->r;
      }
      if( (f2&MEM_Real)==0 ){
        r2 = pMem2->i;
      }else{
        r2 = pMem2->r;
      }
      if( r1<r2 ) return -1;
      if( r1>r2 ) return 1;
      return 0;
    }else{
      assert( f1&MEM_Int );
      assert( f2&MEM_Int );
      if( pMem1->i < pMem2->i ) return -1;
      if( pMem1->i > pMem2->i ) return 1;
      return 0;
    }
  }

  /* If one value is a string and the other is a blob, the string is less.
  ** If both are strings, compare using the collating functions.
  */
  if( combined_flags&MEM_Str ){
    if( (f1 & MEM_Str)==0 ){
      return 1;
    }
    if( (f2 & MEM_Str)==0 ){
      return -1;
    }

    assert( pMem1->enc==pMem2->enc );
    assert( pMem1->enc==SQLITE_UTF8 || 
            pMem1->enc==SQLITE_UTF16LE || pMem1->enc==SQLITE_UTF16BE );

    /* This assert may fail if the collation sequence is deleted after this
    ** vdbe program is compiled. The documentation defines this as an
    ** undefined condition. A crash is usual result.
    */
    assert( !pColl || pColl->xCmp );

    if( pColl ){
      if( pMem1->enc==pColl->enc ){
        return pColl->xCmp(pColl->pUser,pMem1->n,pMem1->z,pMem2->n,pMem2->z);
      }else{
        return pColl->xCmp(
          pColl->pUser,
          sqlite3ValueBytes((sqlite3_value*)pMem1, pColl->enc),
          sqlite3ValueText((sqlite3_value*)pMem1, pColl->enc),
          sqlite3ValueBytes((sqlite3_value*)pMem2, pColl->enc),
          sqlite3ValueText((sqlite3_value*)pMem2, pColl->enc)
        );
      }
    }
    /* If a NULL pointer was passed as the collate function, fall through
    ** to the blob case and use memcmp().  */
  }
 
  /* Both values must be blobs.  Compare using memcmp().  */
  rc = memcmp(pMem1->z, pMem2->z, (pMem1->n>pMem2->n)?pMem2->n:pMem1->n);
  if( rc==0 ){
    rc = pMem1->n - pMem2->n;
  }
  return rc;
}

/*
** Move data out of a btree key or data field and into a Mem structure.
** The data or key is taken from the entry that pCur is currently pointing
** to.  offset and amt determine what portion of the data or key to retrieve.
** key is true to get the key or false to get data.  The result is written
** into the pMem element.
**
** The pMem structure is assumed to be uninitialized.  Any prior content
** is overwritten without being freed.
**
** If this routine fails for any reason (malloc returns NULL or unable
** to read from the disk) then the pMem is left in an inconsistent state.
*/
int sqlite3VdbeMemFromBtree(
  BtCursor *pCur,   /* Cursor pointing at record to retrieve. */
  int offset,       /* Offset from the start of data to return bytes from. */
  int amt,          /* Number of bytes to return. */
  int key,          /* If true, retrieve from the btree key, not data. */
  Mem *pMem         /* OUT: Return data in this Mem structure. */
){
  char *zData;      /* Data from the btree layer */
  int available;    /* Number of bytes available on the local btree page */

  if( key ){
    zData = (char *)sqlite3BtreeKeyFetch(pCur, &available);
  }else{
    zData = (char *)sqlite3BtreeDataFetch(pCur, &available);
  }

  pMem->n = amt;
  if( offset+amt<=available ){
    pMem->z = &zData[offset];
    pMem->flags = MEM_Blob|MEM_Ephem;
  }else{
    int rc;
    if( amt>NBFS-2 ){
      zData = (char *)sqliteMallocRaw(amt+2);
      if( !zData ){
        return SQLITE_NOMEM;
      }
      pMem->flags = MEM_Blob|MEM_Dyn|MEM_Term;
      pMem->xDel = 0;
    }else{
      zData = &(pMem->zShort[0]);
      pMem->flags = MEM_Blob|MEM_Short|MEM_Term;
    }
    pMem->z = zData;
    pMem->enc = 0;
    pMem->type = SQLITE_BLOB;

    if( key ){
      rc = sqlite3BtreeKey(pCur, offset, amt, zData);
    }else{
      rc = sqlite3BtreeData(pCur, offset, amt, zData);
    }
    zData[amt] = 0;
    zData[amt+1] = 0;
    if( rc!=SQLITE_OK ){
      if( amt>NBFS ){
        sqliteFree(zData);
      }
      return rc;
    }
  }

  return SQLITE_OK;
}

#ifndef NDEBUG
/*
** Perform various checks on the memory cell pMem. An assert() will
** fail if pMem is internally inconsistent.
*/
void sqlite3VdbeMemSanity(Mem *pMem, u8 db_enc){
  int flags = pMem->flags;
  assert( flags!=0 );  /* Must define some type */
  if( pMem->flags & (MEM_Str|MEM_Blob) ){
    int x = pMem->flags & (MEM_Static|MEM_Dyn|MEM_Ephem|MEM_Short);
    assert( x!=0 );            /* Strings must define a string subtype */
    assert( (x & (x-1))==0 );  /* Only one string subtype can be defined */
    assert( pMem->z!=0 );      /* Strings must have a value */
    /* Mem.z points to Mem.zShort iff the subtype is MEM_Short */
    assert( (pMem->flags & MEM_Short)==0 || pMem->z==pMem->zShort );
    assert( (pMem->flags & MEM_Short)!=0 || pMem->z!=pMem->zShort );

    if( (flags & MEM_Str) ){
      assert( pMem->enc==SQLITE_UTF8 || 
              pMem->enc==SQLITE_UTF16BE ||
              pMem->enc==SQLITE_UTF16LE 
      );
      /* If the string is UTF-8 encoded and nul terminated, then pMem->n
      ** must be the length of the string.
      */
      if( pMem->enc==SQLITE_UTF8 && (flags & MEM_Term) ){ 
        assert( strlen(pMem->z)==pMem->n );
      }
    }
  }else{
    /* Cannot define a string subtype for non-string objects */
    assert( (pMem->flags & (MEM_Static|MEM_Dyn|MEM_Ephem|MEM_Short))==0 );
  }
  /* MEM_Null excludes all other types */
  assert( (pMem->flags&(MEM_Str|MEM_Int|MEM_Real|MEM_Blob))==0
          || (pMem->flags&MEM_Null)==0 );
}
#endif

/* This function is only available internally, it is not part of the
** external API. It works in a similar way to sqlite3_value_text(),
** except the data returned is in the encoding specified by the second
** parameter, which must be one of SQLITE_UTF16BE, SQLITE_UTF16LE or
** SQLITE_UTF8.
*/
const void *sqlite3ValueText(sqlite3_value* pVal, u8 enc){
  if( !pVal ) return 0;
  assert( enc==SQLITE_UTF16LE || enc==SQLITE_UTF16BE || enc==SQLITE_UTF8);

  if( pVal->flags&MEM_Null ){
    return 0;
  }
  if( pVal->flags&MEM_Str ){
    sqlite3VdbeChangeEncoding(pVal, enc);
  }else if( !(pVal->flags&MEM_Blob) ){
    sqlite3VdbeMemStringify(pVal, enc);
  }
  return (const void *)(pVal->z);
}

sqlite3_value* sqlite3ValueNew(){
  Mem *p = sqliteMalloc(sizeof(*p));
  if( p ){
    p->flags = MEM_Null;
    p->type = SQLITE_NULL;
  }
  return p;
}

void sqlite3ValueSetStr(
  sqlite3_value *v, 
  int n, 
  const void *z, 
  u8 enc,
  void (*xDel)(void*)
){
  if( v ) sqlite3VdbeMemSetStr((Mem *)v, z, n, enc, xDel);
}

void sqlite3ValueFree(sqlite3_value *v){
  if( !v ) return;
  sqlite3ValueSetStr(v, 0, 0, SQLITE_UTF8, SQLITE_STATIC);
  sqliteFree(v);
}

int sqlite3ValueBytes(sqlite3_value *pVal, u8 enc){
  Mem *p = (Mem*)pVal;
  if( (p->flags & MEM_Blob)!=0 || sqlite3ValueText(pVal, enc) ){
    return p->n;
  }
  return 0;
}
