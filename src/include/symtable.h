#ifndef SYMTABLE_H
#define SYMTABLE_H

#include "types.h"
#include "lexer.h"

typedef struct SymEntry SymEntry;
typedef struct SymEntryLL SymEntryLL;

typedef struct SymTable SymTable;
typedef struct SymTableLL SymTableLL;

typedef struct TypeMethodTableEntry TypeMethodTableEntry;
typedef struct TypeMethodTableEntryLL TypeMethodTableEntryLL;
typedef struct TypeMethodTable TypeMethodTable;

enum
{
    EXPR_NEITHER_VALUE, //for things like using module import identifier in expression, u cant use them
    EXPR_L_VALUE,
    EXPR_R_VALUE,
};

typedef enum ExprCompTimeValKind
{
    A_EXPR_COMP_TIME_INT,
    A_EXPR_COMP_TIME_FLOAT,
    A_EXPR_COMP_TIME_BOOL,
    A_EXPR_COMP_TIME_STRING,
    A_EXPR_COMP_TIME_NULL,
    A_EXPR_COMP_TIME_RUNTIME, //unknown
}ExprCompTimeValKind;

typedef struct CompTimeVal
{
    int isL_or_RValue;
    ExprCompTimeValKind kind;
    union
    {
        int64_t i;
        double f;
        struct
        {
            char *data;
            size_t len;
            size_t realLenExcludingEscapedCharacters; //real length of string. if string is "\r\n" reallength is 2, len is 4 as slahss r counted
        }str;
    };
}CompTimeVal;

struct SymEntry
{
    char name[4 * MAX_IDEN_LEN];
    CheckerType *type;
    bool importIsNamespaceImport; //is the import in the symbol table, linked to a namespace identifier, only used if import.

    bool isNamespace;
    bool isGlobal;
    // u need this to differentiate betweena global function and a global variable that is a 
    //function pointer.
    bool isGlobalFunc; 
    bool isConst;

    bool isActualConst;
    CompTimeVal constVal;

    bool isType;

    SymTable *mySymTable;
    Token posInFile;

    struct ASTDecl *myDecl;
    void *backendValRef;
};

struct SymEntryLL
{
    SymEntry *item;
    struct SymEntryLL *prev;
    struct SymEntryLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct SymEntryLL *first; //first item in ll
};

struct SymTable
{
    SymEntryLL *namespaceTbl;
    SymEntryLL *globalTbl;
    SymEntryLL *localTbl;
    SymEntryLL *constTbl;
    SymEntryLL *typeTbl;

    SymEntryLL *moduleTbl; //this has module string file names

    SymTableLL *continuationTbls; // more symbol table, which is joined onto this one. used for using namespaces

    struct NSTEntry *belongsToNamespace;
};

struct SymTableLL
{
    SymTable *item;
    struct SymTableLL *prev;
    struct SymTableLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct SymTableLL *first; //first item in ll
};

struct TypeMethodTableEntry
{
    CheckerType *type;
    SymTable *methodTble;
};

struct TypeMethodTableEntryLL
{
    TypeMethodTableEntry *item;
    struct TypeMethodTableEntryLL *prev;
    struct TypeMethodTableEntryLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct TypeMethodTableEntryLL *first; //first item in ll
};

struct TypeMethodTable
{
    TypeMethodTableEntryLL *entries;
};

SymEntry *newSymEntry(char *name, CheckerType *type);
SymEntryLL *newSymEntryLL(SymEntry *item);
void addSymEntryLL(SymEntryLL **ll, SymEntry *item);
void removeSymEntryLL(SymEntryLL **ll);

SymTable *newSymTable(struct NSTEntry *belongsToNamespace);
SymTableLL *newSymTableLL(SymTable *item);
void addSymTableLL(SymTableLL **ll, SymTable *item);
bool symTableInSymTableLL(SymTableLL *ll, SymTable *item);

TypeMethodTableEntry *newTypeMethodTableEntry(CheckerType *type);
TypeMethodTableEntryLL *newTypeMethodTableEntryLL(TypeMethodTableEntry *item);
void addTypeMethodTableEntryLL(TypeMethodTableEntryLL **ll, TypeMethodTableEntry *item);
void removeTypeMethodTableEntryyLL(TypeMethodTableEntryLL **ll);

TypeMethodTable *newTypeMethodTable();

SymEntry *typeMethodTableInsert(TypeMethodTable *tmt, CheckerType *type, char *str, CheckerType *funcType, Token posInFile);
SymEntry *typeMethodTableLookUp(TypeMethodTable *tmt, CheckerType *type, char *str);

// #define symTableInsertImport(str, type) _symTableInsertImport(symTable, str, type)
// #define symTableInsertGlobal(str, type) _symTableInsertGlobal(symTable, str, type)
// #define symTableInsertLocal(str, type) _symTableInsertLocal(symTable, str, type)
// #define symTableInsertConst(str, type) _symTableInsertConst(symTable, str, type)
// #define symTableInsertType(str, type) _symTableInsertType(symTable, str, type)
// #define symTableInsertModule(str) _symTableInsertModule(symTable, str)

SymEntry *_symTableInsertSymEntryNamespace(SymTable *table, SymEntry *symEntry);
SymEntry *_symTableInsertSymEntryGlobal(SymTable *table, SymEntry *symEntry);
SymEntry *_symTableInsertSymEntryConst(SymTable *table, SymEntry *symEntry);
SymEntry *_symTableInsertSymEntryType(SymTable *table, SymEntry *symEntry);

SymEntry *_symTableInsertNamespace(SymTable *table, char *str, CheckerType *type, Token posInFile);
SymEntry *_symTableInsertGlobal(SymTable *table, char *str, CheckerType *type, Token posInFile);
SymEntry *_symTableInsertLocal(SymTable *table, char *str, CheckerType *type, Token posInFile);
SymEntry *_symTableInsertConst(SymTable *table, char *str, CheckerType *type, Token posInFile);
SymEntry *_symTableInsertType(SymTable *table, char *str, CheckerType *type, Token posInFile);

#define LOOKUP_NAMESPACE (1 << 0)
#define LOOKUP_GLOBAL (1 << 1)
#define LOOKUP_LOCAL (1 << 2)
#define LOOKUP_TYPE (1 << 3)
#define LOOKUP_CONST (1 << 4)

#define LOOKUP_ALL ((LOOKUP_NAMESPACE) | (LOOKUP_GLOBAL) | (LOOKUP_LOCAL) | (LOOKUP_TYPE) | (LOOKUP_CONST))
#define LOOKUP_ALL_BUT_NOT_LOCAL ((LOOKUP_NAMESPACE) | (LOOKUP_GLOBAL) | (LOOKUP_TYPE) | (LOOKUP_CONST))

// #define symTableLookupImport(str) _symTableLookupImport(symTable, str)
// #define symTableLookupGlobal(str) _symTableLookupGlobal(symTable, str)
// #define symTableLookupLocal(str) _symTableLookupLocal(symTable, str)
// #define symTableLookupConst(str) _symTableLookupConst(symTable, str)
// #define symTableLookupType(str) _symTableLookupType(symTable, str)
// #define symTableLookupModule(str) _symTableLookupModule(symTable, str)
// #define symTableLookUp(str, flags) _symTableLookUp(symTable, str, flags)

SymEntry *_symTableLookupNamespace(SymTable *table, char *str);
SymEntry *_symTableLookupGlobal(SymTable *table, char *str);
SymEntry *_symTableLookupLocal(SymTable *table, char *str);
SymEntry *_symTableLookupLocalUpToEntry(SymTable *table, SymEntryLL *stopBefore, char *str);
SymEntry *_symTableLookupConst(SymTable *table, char *str);
SymEntry *_symTableLookupType(SymTable *table, char *str);
SymEntry *_symTableLookUp(SymTable *table, char *str, int lookupFlags);

// #define symTableRestoreLocal(restoreTo) _symTableRestoreLocal(symTable, restoreTo)
// #define symTableRestoreConst(restoreTo) _symTableRestoreConst(symTable, restoreTo)
// #define symTableRestoreType(restoreTo) _symTableRestoreType(symTable, restoreTo)

void _symTableRestoreLocal(SymTable *table, SymEntryLL *restoreTo);
void _symTableRestoreConst(SymTable *table, SymEntryLL *restoreTo);
void _symTableRestoreType(SymTable *table, SymEntryLL *restoreTo);

void _symTableImportSymtable(SymTable *table, SymTable *symTableToImport, int whatToImportFlags);
void _symTableImportSymtableIntoLocalScope(SymTable *table, SymTable *symTableToImport, int whatToImportFlags);

#endif