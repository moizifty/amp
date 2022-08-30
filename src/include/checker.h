#ifndef CHECKER_H
#define CHECKER_H

#include "types.h"
#include "ast.h"
#include "symtable.h"

#define checkerErrorLn(TOK, ...)  _checkerError(TOK, true, __VA_ARGS__)
#define checkerError(TOK, ...)  _checkerError(TOK, false, __VA_ARGS__)

#define RET_AS_ARG_FUNC_CALL_IDEN_PREFIX    "__fncall"

typedef struct DeferStackEntryLL DeferStackEntryLL;
typedef struct DeferStackEntry DeferStackEntry;

struct DeferStackEntry
{
    ASTBlock *block;
    size_t level;
};

struct DeferStackEntryLL
{
    DeferStackEntry *item;
    struct DeferStackEntryLL *prev;
    struct DeferStackEntryLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct DeferStackEntryLL *first; //first item in ll
};

DeferStackEntry *newDeferStackEntry(ASTBlock *block, size_t level);
DeferStackEntryLL *newDeferStackEntryLL(DeferStackEntry *item);
void addDeferStackEntryLL(DeferStackEntryLL **ll, DeferStackEntry *item);
void removeDeferStackEntryLL(DeferStackEntryLL **ll);

CheckerContext newCheckerContext();

void _checkerError(Token tok, bool newLine, char *msg, ...);
void prettyPrintCheckerSourceError(Token startTok, Token endTok);
int charLitToInt(Token charLit);
char *allocNewAnyLitIden(void);
char *allocNewCompilerIden(char *prefix);

bool checkIfFuncRetTypeShouldBePassedAsArg(CheckerType *funcRetType);

void checkMethodBlock(ASTDecl *decl, bool isIncompleteCheck);
void checkIncompleteDeclInMethodBlock(ASTDecl *methodDecl, ASTDecl *d);
void checkCompleteDeclInMethodBlock(ASTDecl *methodDecl, ASTDecl *d);

void checkGlobalFuncDecl(ASTDeclLL **decl, bool isIncompleteChecl);
void checkIncompleteFuncDecl(ASTDeclLL **decl);
void checkCompleteFuncDecl(ASTDeclLL **decl);
void checkGenericFuncBody(ASTDecl *decl);
void checkImportDecl(ASTDecl *decl, bool isGlobal);

void checkLocalDecl(ASTDecl *decl);

int checkDeclTags(ASTDecl *decl, ASTTagLL *tags);
int checkScopedDeclTags(ASTDecl *decl);

void checkLocalDeclVar(ASTDecl *decl);
void checkLocalDeclConst(ASTDecl *decl);

void checkDeclEnum(ASTDecl *decl, bool isGlobalDecl);

void checkBlock(ASTBlock *block, bool createNewScope);
void checkCreateNewScope(SymTable *table, SymEntryLL **restoreLocal, SymEntryLL **restoreConst, SymEntryLL **restoreType);
void checkEndScope(SymTable *table, SymEntryLL **restoreLocal, SymEntryLL **restoreConst, SymEntryLL **restoreType);

void checkStmt(ASTStmt *stmt);
void checkIfStmt(ASTStmt *ifStmt);
void checkWhileStmt(ASTStmt *stmt);
void checkMatchStmt(ASTStmt *stmt);
void checkForStmt(ASTStmt *stmt);
void checkRetStmt(ASTStmt *stmt);
void checkBreakStmt(ASTStmt *stmt);
void checkContinueStmt(ASTStmt *stmt);
void checkAssignStmt(ASTStmt *stmt);
void checkDeferStmt(ASTStmt *stmt);
void checkUsingStmt(ASTStmt *stmt);

void checkType(ASTType *type);
void checkTypesFilters(ASTType *type);
CheckerType *checkIfTupleTypeAlreadyExists(CheckerTypeLL *types);
CheckerType *checkIfTupleNotExistsCreateNew(CheckerTypeLL *types);

void checkExpr(ASTExpr *expr, bool isIncompletePass);
void checkConvertUnaryExprToOperFuncCall(ASTExpr *expr, SymEntry *operFuncEntry);
void checkConvertBinaryExprToOperFuncCall(ASTExpr *expr, SymEntry *operFuncEntry);
void checkConvertIndexRefExprToOperFuncCall(ASTExpr *expr, SymEntry *operFuncEntry);
void checkConvertStructuredEqToFuncCall(ASTExpr *expr, SymEntry *operFuncEntry);
void checkConvertExprToUnaryExpr(ASTExpr *expr, TokType op);
void checkInsertCastExpr(CheckerType *castTo, ASTExpr **exprToCast);
void checkCastExpr(CheckerType *castTo, ASTExpr *wholeExpr, ASTExpr *exprToCast);
bool checkInferredExpr(CheckerType *inferredType, ASTExpr **expr,bool shouldInsertCast, bool shouldCheckImplicitCast, bool isIncompletePass);
bool checkInferredIfExpr(CheckerType *inferredtype, ASTExpr *expr, bool isIncomplete);
bool checkInferredMatchExpr(CheckerType *inferredType, ASTExpr *expr, bool isIncomplete);
bool checkInferredArrayLitExpr(CheckerType *inferredBaseType, ASTExpr *expr, bool isIncompletePass);
bool checkInferredStructuredLitExpr(CheckerType *inferredType, ASTExpr *expr, bool isIncompletePass);
bool checkInferredTaggedUnionLitExpr(CheckerType *inferredType, ASTExpr *expr, bool isIncompletePass);
bool checkInferredTupleLitExpr(CheckerType *inferredType, ASTExpr *expr, bool isIncompletePass);
void checkDeclLHSTuple(ASTExpr *lhs, ASTType *type ,ASTExpr **rhs);
void checkAssignLHSTuple(ASTExpr *lhs, ASTExpr **rhs);
bool checkRHSExprWithTypeAndCast(CheckerType *checkAgainst, ASTExpr **rhs, bool shouldInsertCast, bool isIncompletePass);

void checkInsertFuncRetAsArgStmts(ASTExpr *funcCallExpr);
void checkInsertAnyCastStmts(ASTExpr *expr);

bool isExprDiscardIdentifier(ASTExpr *expr);

enum
{
    MAIN_FUNC_INCORRECT_RET_TYPE = 1 << 0,
    MAIN_FUNC_INCORRECT_PARAMS = 1 << 1,
    MAIN_FUNC_HAS_VOID_RET = 1 << 2,
    MAIN_FUNC_HAS_VOID_PARAMS = 1 << 3,
};

int checkIfMainFuncHasCorrectSignature(CheckerType *mainFuncType);
SymEntry *checkTryfindOverloadFunc(TokType op, CheckerType *a, CheckerType *b);
#endif