/*
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
*
*/
#include "sqliteInt.h"

/*
** Delete a linked list of TriggerStep structures.
*/
void sqlite3DeleteTriggerStep(TriggerStep *pTriggerStep){
  while( pTriggerStep ){
    TriggerStep * pTmp = pTriggerStep;
    pTriggerStep = pTriggerStep->pNext;

    if( pTmp->target.dyn ) sqliteFree((char*)pTmp->target.z);
    sqlite3ExprDelete(pTmp->pWhere);
    sqlite3ExprListDelete(pTmp->pExprList);
    sqlite3SelectDelete(pTmp->pSelect);
    sqlite3IdListDelete(pTmp->pIdList);

    sqliteFree(pTmp);
  }
}

/*
** This is called by the parser when it sees a CREATE TRIGGER statement
** up to the point of the BEGIN before the trigger actions.  A Trigger
** structure is generated based on the information available and stored
** in pParse->pNewTrigger.  After the trigger actions have been parsed, the
** sqlite3FinishTrigger() function is called to complete the trigger
** construction process.
*/
void sqlite3BeginTrigger(
  Parse *pParse,      /* The parse context of the CREATE TRIGGER statement */
  Token *pName1,      /* The name of the trigger */
  Token *pName2,      /* The name of the trigger */
  int tr_tm,          /* One of TK_BEFORE, TK_AFTER, TK_INSTEAD */
  int op,             /* One of TK_INSERT, TK_UPDATE, TK_DELETE */
  IdList *pColumns,   /* column list if this is an UPDATE OF trigger */
  SrcList *pTableName,/* The name of the table/view the trigger applies to */
  int foreach,        /* One of TK_ROW or TK_STATEMENT */
  Expr *pWhen,        /* WHEN clause */
  int isTemp          /* True if the TEMPORARY keyword is present */
){
  Trigger *pTrigger;
  Table *pTab;
  char *zName = 0;        /* Name of the trigger */
  sqlite *db = pParse->db;
  int iDb;                /* The database to store the trigger in */
  Token *pName;           /* The unqualified db name */
  DbFixer sFix;

  if( isTemp ){
    /* If TEMP was specified, then the trigger name may not be qualified. */
    if( pName2 && pName2->n>0 ){
      sqlite3ErrorMsg(pParse, "temporary trigger may not have qualified name");
      goto trigger_cleanup;
    }
    iDb = 1;
    pName = pName1;
  }else{
    /* Figure out the db that the the trigger will be created in */
    iDb = sqlite3TwoPartName(pParse, pName1, pName2, &pName);
    if( iDb<0 ){
      goto trigger_cleanup;
    }
  }

  /* If the trigger name was unqualified, and the table is a temp table,
  ** then set iDb to 1 to create the trigger in the temporary database.
  ** If sqlite3SrcListLookup() returns 0, indicating the table does not
  ** exist, the error is caught by the block below.
  */
  if( !pTableName ) goto trigger_cleanup;
  pTab = sqlite3SrcListLookup(pParse, pTableName);
  if( pName2->n==0 && pTab && pTab->iDb==1 ){
    iDb = 1;
  }

  /* Ensure the table name matches database name and that the table exists */
  if( sqlite3_malloc_failed ) goto trigger_cleanup;
  assert( pTableName->nSrc==1 );
  if( sqlite3FixInit(&sFix, pParse, iDb, "trigger", pName) && 
      sqlite3FixSrcList(&sFix, pTableName) ){
    goto trigger_cleanup;
  }
  pTab = sqlite3SrcListLookup(pParse, pTableName);
  if( !pTab ){
    /* The table does not exist. */
    goto trigger_cleanup;
  }

  /* Check that the trigger name is not reserved and that no trigger of the
  ** specified name exists */
  zName = sqlite3TableNameFromToken(pName);
  if( !zName || SQLITE_OK!=sqlite3CheckObjectName(pParse, zName) ){
    goto trigger_cleanup;
  }
  if( sqlite3HashFind(&(db->aDb[iDb].trigHash), zName,pName->n+1) ){
    sqlite3ErrorMsg(pParse, "trigger %T already exists", pName);
    goto trigger_cleanup;
  }

  /* Do not create a trigger on a system table */
  if( (iDb!=1 && sqlite3StrICmp(pTab->zName, MASTER_NAME)==0) || 
      (iDb==1 && sqlite3StrICmp(pTab->zName, TEMP_MASTER_NAME)==0) 
  ){
    sqlite3ErrorMsg(pParse, "cannot create trigger on system table");
    pParse->nErr++;
    goto trigger_cleanup;
  }

  /* INSTEAD of triggers are only for views and views only support INSTEAD
  ** of triggers.
  */
  if( pTab->pSelect && tr_tm!=TK_INSTEAD ){
    sqlite3ErrorMsg(pParse, "cannot create %s trigger on view: %S", 
        (tr_tm == TK_BEFORE)?"BEFORE":"AFTER", pTableName, 0);
    goto trigger_cleanup;
  }
  if( !pTab->pSelect && tr_tm==TK_INSTEAD ){
    sqlite3ErrorMsg(pParse, "cannot create INSTEAD OF"
        " trigger on table: %S", pTableName, 0);
    goto trigger_cleanup;
  }

#ifndef SQLITE_OMIT_AUTHORIZATION
  {
    int code = SQLITE_CREATE_TRIGGER;
    const char *zDb = db->aDb[pTab->iDb].zName;
    const char *zDbTrig = isTemp ? db->aDb[1].zName : zDb;
    if( pTab->iDb==1 || isTemp ) code = SQLITE_CREATE_TEMP_TRIGGER;
    if( sqlite3AuthCheck(pParse, code, zName, pTab->zName, zDbTrig) ){
      goto trigger_cleanup;
    }
    if( sqlite3AuthCheck(pParse, SQLITE_INSERT, SCHEMA_TABLE(pTab->iDb), 0, zDb)){
      goto trigger_cleanup;
    }
  }
#endif

  /* INSTEAD OF triggers can only appear on views and BEFORE triggers
  ** cannot appear on views.  So we might as well translate every
  ** INSTEAD OF trigger into a BEFORE trigger.  It simplifies code
  ** elsewhere.
  */
  if (tr_tm == TK_INSTEAD){
    tr_tm = TK_BEFORE;
  }

  /* Build the Trigger object */
  pTrigger = (Trigger*)sqliteMalloc(sizeof(Trigger));
  if( pTrigger==0 ) goto trigger_cleanup;
  pTrigger->name = zName;
  zName = 0;
  pTrigger->table = sqliteStrDup(pTableName->a[0].zName);
  if( sqlite3_malloc_failed ) goto trigger_cleanup;
  pTrigger->iDb = iDb;
  pTrigger->iTabDb = pTab->iDb;
  pTrigger->op = op;
  pTrigger->tr_tm = tr_tm;
  pTrigger->pWhen = sqlite3ExprDup(pWhen);
  pTrigger->pColumns = sqlite3IdListDup(pColumns);
  pTrigger->foreach = foreach;
  sqlite3TokenCopy(&pTrigger->nameToken,pName);
  assert( pParse->pNewTrigger==0 );
  pParse->pNewTrigger = pTrigger;

trigger_cleanup:
  sqliteFree(zName);
  sqlite3SrcListDelete(pTableName);
  sqlite3IdListDelete(pColumns);
  sqlite3ExprDelete(pWhen);
}

/*
** This routine is called after all of the trigger actions have been parsed
** in order to complete the process of building the trigger.
*/
void sqlite3FinishTrigger(
  Parse *pParse,          /* Parser context */
  TriggerStep *pStepList, /* The triggered program */
  Token *pAll             /* Token that describes the complete CREATE TRIGGER */
){
  Trigger *nt = 0;          /* The trigger whose construction is finishing up */
  sqlite *db = pParse->db;  /* The database */
  DbFixer sFix;

  if( pParse->nErr || pParse->pNewTrigger==0 ) goto triggerfinish_cleanup;
  nt = pParse->pNewTrigger;
  pParse->pNewTrigger = 0;
  nt->step_list = pStepList;
  while( pStepList ){
    pStepList->pTrig = nt;
    pStepList = pStepList->pNext;
  }
  if( sqlite3FixInit(&sFix, pParse, nt->iDb, "trigger", &nt->nameToken) 
          && sqlite3FixTriggerStep(&sFix, nt->step_list) ){
    goto triggerfinish_cleanup;
  }

  /* if we are not initializing, and this trigger is not on a TEMP table, 
  ** build the sqlite_master entry
  */
  if( !db->init.busy ){
    static VdbeOpList insertTrig[] = {
      { OP_NewRecno,   0, 0,  0          },
      { OP_String8,     0, 0,  "trigger"  },
      { OP_String8,     0, 0,  0          },  /* 2: trigger name */
      { OP_String8,     0, 0,  0          },  /* 3: table name */
      { OP_Integer,    0, 0,  0          },
      { OP_String8,     0, 0,  "CREATE TRIGGER "},
      { OP_String8,     0, 0,  0          },  /* 6: SQL */
      { OP_Concat,     2, 0,  0          }, 
      { OP_MakeRecord, 5, 0,  "tttit"    },
      { OP_PutIntKey,  0, 0,  0          },
    };
    int addr;
    Vdbe *v;

    /* Make an entry in the sqlite_master table */
    v = sqlite3GetVdbe(pParse);
    if( v==0 ) goto triggerfinish_cleanup;
    sqlite3BeginWriteOperation(pParse, 0, nt->iDb);
    sqlite3OpenMasterTable(v, nt->iDb);
    addr = sqlite3VdbeAddOpList(v, ArraySize(insertTrig), insertTrig);
    sqlite3VdbeChangeP3(v, addr+2, nt->name, 0); 
    sqlite3VdbeChangeP3(v, addr+3, nt->table, 0); 
    sqlite3VdbeChangeP3(v, addr+6, pAll->z, pAll->n);
    if( nt->iDb!=0 ){
      sqlite3ChangeCookie(db, v, nt->iDb);
    }
    sqlite3VdbeAddOp(v, OP_Close, 0, 0);
    sqlite3EndWriteOperation(pParse);
  }

  if( !pParse->explain ){
    Table *pTab;
    sqlite3HashInsert(&db->aDb[nt->iDb].trigHash, 
                     nt->name, strlen(nt->name)+1, nt);
    pTab = sqlite3LocateTable(pParse, nt->table, db->aDb[nt->iTabDb].zName);
    assert( pTab!=0 );
    nt->pNext = pTab->pTrigger;
    pTab->pTrigger = nt;
    nt = 0;
  }

triggerfinish_cleanup:
  sqlite3DeleteTrigger(nt);
  sqlite3DeleteTrigger(pParse->pNewTrigger);
  pParse->pNewTrigger = 0;
  sqlite3DeleteTriggerStep(pStepList);
}

/*
** Make a copy of all components of the given trigger step.  This has
** the effect of copying all Expr.token.z values into memory obtained
** from sqliteMalloc().  As initially created, the Expr.token.z values
** all point to the input string that was fed to the parser.  But that
** string is ephemeral - it will go away as soon as the sqlite3_exec()
** call that started the parser exits.  This routine makes a persistent
** copy of all the Expr.token.z strings so that the TriggerStep structure
** will be valid even after the sqlite3_exec() call returns.
*/
static void sqlitePersistTriggerStep(TriggerStep *p){
  if( p->target.z ){
    p->target.z = sqliteStrNDup(p->target.z, p->target.n);
    p->target.dyn = 1;
  }
  if( p->pSelect ){
    Select *pNew = sqlite3SelectDup(p->pSelect);
    sqlite3SelectDelete(p->pSelect);
    p->pSelect = pNew;
  }
  if( p->pWhere ){
    Expr *pNew = sqlite3ExprDup(p->pWhere);
    sqlite3ExprDelete(p->pWhere);
    p->pWhere = pNew;
  }
  if( p->pExprList ){
    ExprList *pNew = sqlite3ExprListDup(p->pExprList);
    sqlite3ExprListDelete(p->pExprList);
    p->pExprList = pNew;
  }
  if( p->pIdList ){
    IdList *pNew = sqlite3IdListDup(p->pIdList);
    sqlite3IdListDelete(p->pIdList);
    p->pIdList = pNew;
  }
}

/*
** Turn a SELECT statement (that the pSelect parameter points to) into
** a trigger step.  Return a pointer to a TriggerStep structure.
**
** The parser calls this routine when it finds a SELECT statement in
** body of a TRIGGER.  
*/
TriggerStep *sqlite3TriggerSelectStep(Select *pSelect){
  TriggerStep *pTriggerStep = sqliteMalloc(sizeof(TriggerStep));
  if( pTriggerStep==0 ) return 0;

  pTriggerStep->op = TK_SELECT;
  pTriggerStep->pSelect = pSelect;
  pTriggerStep->orconf = OE_Default;
  sqlitePersistTriggerStep(pTriggerStep);

  return pTriggerStep;
}

/*
** Build a trigger step out of an INSERT statement.  Return a pointer
** to the new trigger step.
**
** The parser calls this routine when it sees an INSERT inside the
** body of a trigger.
*/
TriggerStep *sqlite3TriggerInsertStep(
  Token *pTableName,  /* Name of the table into which we insert */
  IdList *pColumn,    /* List of columns in pTableName to insert into */
  ExprList *pEList,   /* The VALUE clause: a list of values to be inserted */
  Select *pSelect,    /* A SELECT statement that supplies values */
  int orconf          /* The conflict algorithm (OE_Abort, OE_Replace, etc.) */
){
  TriggerStep *pTriggerStep = sqliteMalloc(sizeof(TriggerStep));
  if( pTriggerStep==0 ) return 0;

  assert(pEList == 0 || pSelect == 0);
  assert(pEList != 0 || pSelect != 0);

  pTriggerStep->op = TK_INSERT;
  pTriggerStep->pSelect = pSelect;
  pTriggerStep->target  = *pTableName;
  pTriggerStep->pIdList = pColumn;
  pTriggerStep->pExprList = pEList;
  pTriggerStep->orconf = orconf;
  sqlitePersistTriggerStep(pTriggerStep);

  return pTriggerStep;
}

/*
** Construct a trigger step that implements an UPDATE statement and return
** a pointer to that trigger step.  The parser calls this routine when it
** sees an UPDATE statement inside the body of a CREATE TRIGGER.
*/
TriggerStep *sqlite3TriggerUpdateStep(
  Token *pTableName,   /* Name of the table to be updated */
  ExprList *pEList,    /* The SET clause: list of column and new values */
  Expr *pWhere,        /* The WHERE clause */
  int orconf           /* The conflict algorithm. (OE_Abort, OE_Ignore, etc) */
){
  TriggerStep *pTriggerStep = sqliteMalloc(sizeof(TriggerStep));
  if( pTriggerStep==0 ) return 0;

  pTriggerStep->op = TK_UPDATE;
  pTriggerStep->target  = *pTableName;
  pTriggerStep->pExprList = pEList;
  pTriggerStep->pWhere = pWhere;
  pTriggerStep->orconf = orconf;
  sqlitePersistTriggerStep(pTriggerStep);

  return pTriggerStep;
}

/*
** Construct a trigger step that implements a DELETE statement and return
** a pointer to that trigger step.  The parser calls this routine when it
** sees a DELETE statement inside the body of a CREATE TRIGGER.
*/
TriggerStep *sqlite3TriggerDeleteStep(Token *pTableName, Expr *pWhere){
  TriggerStep *pTriggerStep = sqliteMalloc(sizeof(TriggerStep));
  if( pTriggerStep==0 ) return 0;

  pTriggerStep->op = TK_DELETE;
  pTriggerStep->target  = *pTableName;
  pTriggerStep->pWhere = pWhere;
  pTriggerStep->orconf = OE_Default;
  sqlitePersistTriggerStep(pTriggerStep);

  return pTriggerStep;
}

/* 
** Recursively delete a Trigger structure
*/
void sqlite3DeleteTrigger(Trigger *pTrigger){
  if( pTrigger==0 ) return;
  sqlite3DeleteTriggerStep(pTrigger->step_list);
  sqliteFree(pTrigger->name);
  sqliteFree(pTrigger->table);
  sqlite3ExprDelete(pTrigger->pWhen);
  sqlite3IdListDelete(pTrigger->pColumns);
  if( pTrigger->nameToken.dyn ) sqliteFree((char*)pTrigger->nameToken.z);
  sqliteFree(pTrigger);
}

/*
 * This function is called to drop a trigger from the database schema. 
 *
 * This may be called directly from the parser and therefore identifies
 * the trigger by name.  The sqlite3DropTriggerPtr() routine does the
 * same job as this routine except it take a spointer to the trigger
 * instead of the trigger name.
 *
 * Note that this function does not delete the trigger entirely. Instead it
 * removes it from the internal schema and places it in the trigDrop hash 
 * table. This is so that the trigger can be restored into the database schema
 * if the transaction is rolled back.
 */
void sqlite3DropTrigger(Parse *pParse, SrcList *pName){
  Trigger *pTrigger = 0;
  int i;
  const char *zDb;
  const char *zName;
  int nName;
  sqlite *db = pParse->db;

  if( sqlite3_malloc_failed ) goto drop_trigger_cleanup;
  if( SQLITE_OK!=sqlite3ReadSchema(db, &pParse->zErrMsg) ){
    pParse->nErr++;
    goto drop_trigger_cleanup;
  }

  assert( pName->nSrc==1 );
  zDb = pName->a[0].zDatabase;
  zName = pName->a[0].zName;
  nName = strlen(zName);
  for(i=0; i<db->nDb; i++){
    int j = (i<2) ? i^1 : i;  /* Search TEMP before MAIN */
    if( zDb && sqlite3StrICmp(db->aDb[j].zName, zDb) ) continue;
    pTrigger = sqlite3HashFind(&(db->aDb[j].trigHash), zName, nName+1);
    if( pTrigger ) break;
  }
  if( !pTrigger ){
    sqlite3ErrorMsg(pParse, "no such trigger: %S", pName, 0);
    goto drop_trigger_cleanup;
  }
  sqlite3DropTriggerPtr(pParse, pTrigger, 0);

drop_trigger_cleanup:
  sqlite3SrcListDelete(pName);
}

/*
** Drop a trigger given a pointer to that trigger.  If nested is false,
** then also generate code to remove the trigger from the SQLITE_MASTER
** table.
*/
void sqlite3DropTriggerPtr(Parse *pParse, Trigger *pTrigger, int nested){
  Table   *pTable;
  Vdbe *v;
  sqlite *db = pParse->db;

  assert( pTrigger->iDb<db->nDb );
  pTable = sqlite3FindTable(db, pTrigger->table,db->aDb[pTrigger->iTabDb].zName);
  assert(pTable);
  assert( pTable->iDb==pTrigger->iDb || pTrigger->iDb==1 );
#ifndef SQLITE_OMIT_AUTHORIZATION
  {
    int code = SQLITE_DROP_TRIGGER;
    const char *zDb = db->aDb[pTrigger->iDb].zName;
    const char *zTab = SCHEMA_TABLE(pTrigger->iDb);
    if( pTrigger->iDb==1 ) code = SQLITE_DROP_TEMP_TRIGGER;
    if( sqlite3AuthCheck(pParse, code, pTrigger->name, pTable->zName, zDb) ||
      sqlite3AuthCheck(pParse, SQLITE_DELETE, zTab, 0, zDb) ){
      return;
    }
  }
#endif

  /* Generate code to destroy the database record of the trigger.
  */
  if( pTable!=0 && (v = sqlite3GetVdbe(pParse))!=0 ){
    int base;
    static VdbeOpList dropTrigger[] = {
      { OP_Rewind,     0, ADDR(9),  0},
      { OP_String8,     0, 0,        0}, /* 1 */
      { OP_Column,     0, 1,        0},
      { OP_Ne,         0, ADDR(8),  0},
      { OP_String8,     0, 0,        "trigger"},
      { OP_Column,     0, 0,        0},
      { OP_Ne,         0, ADDR(8),  0},
      { OP_Delete,     0, 0,        0},
      { OP_Next,       0, ADDR(1),  0}, /* 8 */
    };

    sqlite3BeginWriteOperation(pParse, 0, pTrigger->iDb);
    sqlite3OpenMasterTable(v, pTrigger->iDb);
    base = sqlite3VdbeAddOpList(v,  ArraySize(dropTrigger), dropTrigger);
    sqlite3VdbeChangeP3(v, base+1, pTrigger->name, 0);
    sqlite3ChangeCookie(db, v, pTrigger->iDb);
    sqlite3VdbeAddOp(v, OP_Close, 0, 0);
  }

  /*
  ** If this is not an "explain", then delete the trigger structure.
  */
  if( !pParse->explain ){
    const char *zName = pTrigger->name;
    int nName = strlen(zName);
    if( pTable->pTrigger == pTrigger ){
      pTable->pTrigger = pTrigger->pNext;
    }else{
      Trigger *cc = pTable->pTrigger;
      while( cc ){ 
        if( cc->pNext == pTrigger ){
          cc->pNext = cc->pNext->pNext;
          break;
        }
        cc = cc->pNext;
      }
      assert(cc);
    }
    sqlite3HashInsert(&(db->aDb[pTrigger->iDb].trigHash), zName, nName+1, 0);
    sqlite3DeleteTrigger(pTrigger);
  }
}

/*
** pEList is the SET clause of an UPDATE statement.  Each entry
** in pEList is of the format <id>=<expr>.  If any of the entries
** in pEList have an <id> which matches an identifier in pIdList,
** then return TRUE.  If pIdList==NULL, then it is considered a
** wildcard that matches anything.  Likewise if pEList==NULL then
** it matches anything so always return true.  Return false only
** if there is no match.
*/
static int checkColumnOverLap(IdList *pIdList, ExprList *pEList){
  int e;
  if( !pIdList || !pEList ) return 1;
  for(e=0; e<pEList->nExpr; e++){
    if( sqlite3IdListIndex(pIdList, pEList->a[e].zName)>=0 ) return 1;
  }
  return 0; 
}

/* A global variable that is TRUE if we should always set up temp tables for
 * for triggers, even if there are no triggers to code. This is used to test 
 * how much overhead the triggers algorithm is causing.
 *
 * This flag can be set or cleared using the "trigger_overhead_test" pragma.
 * The pragma is not documented since it is not really part of the interface
 * to SQLite, just the test procedure.
*/
int always_code_trigger_setup = 0;

/*
 * Returns true if a trigger matching op, tr_tm and foreach that is NOT already
 * on the Parse objects trigger-stack (to prevent recursive trigger firing) is
 * found in the list specified as pTrigger.
 */
int sqlite3TriggersExist(
  Parse *pParse,          /* Used to check for recursive triggers */
  Trigger *pTrigger,      /* A list of triggers associated with a table */
  int op,                 /* one of TK_DELETE, TK_INSERT, TK_UPDATE */
  int tr_tm,              /* one of TK_BEFORE, TK_AFTER */
  int foreach,            /* one of TK_ROW or TK_STATEMENT */
  ExprList *pChanges      /* Columns that change in an UPDATE statement */
){
  Trigger * pTriggerCursor;

  if( always_code_trigger_setup ){
    return 1;
  }

  pTriggerCursor = pTrigger;
  while( pTriggerCursor ){
    if( pTriggerCursor->op == op && 
	pTriggerCursor->tr_tm == tr_tm && 
	pTriggerCursor->foreach == foreach &&
	checkColumnOverLap(pTriggerCursor->pColumns, pChanges) ){
      TriggerStack * ss;
      ss = pParse->trigStack;
      while( ss && ss->pTrigger != pTrigger ){
	ss = ss->pNext;
      }
      if( !ss )return 1;
    }
    pTriggerCursor = pTriggerCursor->pNext;
  }

  return 0;
}

/*
** Convert the pStep->target token into a SrcList and return a pointer
** to that SrcList.
**
** This routine adds a specific database name, if needed, to the target when
** forming the SrcList.  This prevents a trigger in one database from
** referring to a target in another database.  An exception is when the
** trigger is in TEMP in which case it can refer to any other database it
** wants.
*/
static SrcList *targetSrcList(
  Parse *pParse,       /* The parsing context */
  TriggerStep *pStep   /* The trigger containing the target token */
){
  Token sDb;           /* Dummy database name token */
  int iDb;             /* Index of the database to use */
  SrcList *pSrc;       /* SrcList to be returned */

  iDb = pStep->pTrig->iDb;
  if( iDb==0 || iDb>=2 ){
    assert( iDb<pParse->db->nDb );
    sDb.z = pParse->db->aDb[iDb].zName;
    sDb.n = strlen(sDb.z);
    pSrc = sqlite3SrcListAppend(0, &sDb, &pStep->target);
  } else {
    pSrc = sqlite3SrcListAppend(0, &pStep->target, 0);
  }
  return pSrc;
}

/*
** Generate VDBE code for zero or more statements inside the body of a
** trigger.  
*/
static int codeTriggerProgram(
  Parse *pParse,            /* The parser context */
  TriggerStep *pStepList,   /* List of statements inside the trigger body */
  int orconfin              /* Conflict algorithm. (OE_Abort, etc) */  
){
  TriggerStep * pTriggerStep = pStepList;
  int orconf;

  while( pTriggerStep ){
    int saveNTab = pParse->nTab;
 
    orconf = (orconfin == OE_Default)?pTriggerStep->orconf:orconfin;
    pParse->trigStack->orconf = orconf;
    switch( pTriggerStep->op ){
      case TK_SELECT: {
	Select * ss = sqlite3SelectDup(pTriggerStep->pSelect);		  
	assert(ss);
	assert(ss->pSrc);
	sqlite3Select(pParse, ss, SRT_Discard, 0, 0, 0, 0, 0);
	sqlite3SelectDelete(ss);
	break;
      }
      case TK_UPDATE: {
        SrcList *pSrc;
        pSrc = targetSrcList(pParse, pTriggerStep);
        sqlite3VdbeAddOp(pParse->pVdbe, OP_ListPush, 0, 0);
        sqlite3Update(pParse, pSrc,
		sqlite3ExprListDup(pTriggerStep->pExprList), 
		sqlite3ExprDup(pTriggerStep->pWhere), orconf);
        sqlite3VdbeAddOp(pParse->pVdbe, OP_ListPop, 0, 0);
        break;
      }
      case TK_INSERT: {
        SrcList *pSrc;
        pSrc = targetSrcList(pParse, pTriggerStep);
        sqlite3Insert(pParse, pSrc,
          sqlite3ExprListDup(pTriggerStep->pExprList), 
          sqlite3SelectDup(pTriggerStep->pSelect), 
          sqlite3IdListDup(pTriggerStep->pIdList), orconf);
        break;
      }
      case TK_DELETE: {
        SrcList *pSrc;
        sqlite3VdbeAddOp(pParse->pVdbe, OP_ListPush, 0, 0);
        pSrc = targetSrcList(pParse, pTriggerStep);
        sqlite3DeleteFrom(pParse, pSrc, sqlite3ExprDup(pTriggerStep->pWhere));
        sqlite3VdbeAddOp(pParse->pVdbe, OP_ListPop, 0, 0);
        break;
      }
      default:
        assert(0);
    } 
    pParse->nTab = saveNTab;
    pTriggerStep = pTriggerStep->pNext;
  }

  return 0;
}

/*
** This is called to code FOR EACH ROW triggers.
**
** When the code that this function generates is executed, the following 
** must be true:
**
** 1. No cursors may be open in the main database.  (But newIdx and oldIdx
**    can be indices of cursors in temporary tables.  See below.)
**
** 2. If the triggers being coded are ON INSERT or ON UPDATE triggers, then
**    a temporary vdbe cursor (index newIdx) must be open and pointing at
**    a row containing values to be substituted for new.* expressions in the
**    trigger program(s).
**
** 3. If the triggers being coded are ON DELETE or ON UPDATE triggers, then
**    a temporary vdbe cursor (index oldIdx) must be open and pointing at
**    a row containing values to be substituted for old.* expressions in the
**    trigger program(s).
**
*/
int sqlite3CodeRowTrigger(
  Parse *pParse,       /* Parse context */
  int op,              /* One of TK_UPDATE, TK_INSERT, TK_DELETE */
  ExprList *pChanges,  /* Changes list for any UPDATE OF triggers */
  int tr_tm,           /* One of TK_BEFORE, TK_AFTER */
  Table *pTab,         /* The table to code triggers from */
  int newIdx,          /* The indice of the "new" row to access */
  int oldIdx,          /* The indice of the "old" row to access */
  int orconf,          /* ON CONFLICT policy */
  int ignoreJump       /* Instruction to jump to for RAISE(IGNORE) */
){
  Trigger * pTrigger;
  TriggerStack * pTriggerStack;

  assert(op == TK_UPDATE || op == TK_INSERT || op == TK_DELETE);
  assert(tr_tm == TK_BEFORE || tr_tm == TK_AFTER );

  assert(newIdx != -1 || oldIdx != -1);

  pTrigger = pTab->pTrigger;
  while( pTrigger ){
    int fire_this = 0;

    /* determine whether we should code this trigger */
    if( pTrigger->op == op && pTrigger->tr_tm == tr_tm && 
        pTrigger->foreach == TK_ROW ){
      fire_this = 1;
      pTriggerStack = pParse->trigStack;
      while( pTriggerStack ){
        if( pTriggerStack->pTrigger == pTrigger ){
	  fire_this = 0;
	}
        pTriggerStack = pTriggerStack->pNext;
      }
      if( op == TK_UPDATE && pTrigger->pColumns &&
          !checkColumnOverLap(pTrigger->pColumns, pChanges) ){
        fire_this = 0;
      }
    }

    if( fire_this && (pTriggerStack = sqliteMalloc(sizeof(TriggerStack)))!=0 ){
      int endTrigger;
      SrcList dummyTablist;
      Expr * whenExpr;
      AuthContext sContext;

      dummyTablist.nSrc = 0;

      /* Push an entry on to the trigger stack */
      pTriggerStack->pTrigger = pTrigger;
      pTriggerStack->newIdx = newIdx;
      pTriggerStack->oldIdx = oldIdx;
      pTriggerStack->pTab = pTab;
      pTriggerStack->pNext = pParse->trigStack;
      pTriggerStack->ignoreJump = ignoreJump;
      pParse->trigStack = pTriggerStack;
      sqlite3AuthContextPush(pParse, &sContext, pTrigger->name);

      /* code the WHEN clause */
      endTrigger = sqlite3VdbeMakeLabel(pParse->pVdbe);
      whenExpr = sqlite3ExprDup(pTrigger->pWhen);
      if( sqlite3ExprResolveIds(pParse, &dummyTablist, 0, whenExpr) ){
        pParse->trigStack = pParse->trigStack->pNext;
        sqliteFree(pTriggerStack);
        sqlite3ExprDelete(whenExpr);
        return 1;
      }
      sqlite3ExprIfFalse(pParse, whenExpr, endTrigger, 1);
      sqlite3ExprDelete(whenExpr);

      sqlite3VdbeAddOp(pParse->pVdbe, OP_ContextPush, 0, 0);
      codeTriggerProgram(pParse, pTrigger->step_list, orconf); 
      sqlite3VdbeAddOp(pParse->pVdbe, OP_ContextPop, 0, 0);

      /* Pop the entry off the trigger stack */
      pParse->trigStack = pParse->trigStack->pNext;
      sqlite3AuthContextPop(&sContext);
      sqliteFree(pTriggerStack);

      sqlite3VdbeResolveLabel(pParse->pVdbe, endTrigger);
    }
    pTrigger = pTrigger->pNext;
  }
  return 0;
}
