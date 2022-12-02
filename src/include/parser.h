#ifndef PARSER_H
#define PARSER_H

#include <setjmp.h>

#include "lexer.h"
#include "ast.h"
#include "compiler_internal.h"

ParserContext newParserContext();

void parserError(Token tok, char *msg, ...);

void collectDecls(ASTProg *prog, ASTDeclLL *allDecls);
void parseEntireProgram(char **filesList, int fileCount);

ASTProg *prog(SymTable *tbl);
ASTNamespaceName *namespaceName(bool isRecursing);
ASTNamespaceWithBlock *namespaceWithBlock();
ASTNamespaceWithStmtLL *namespaceWithStmtList();
ASTNamespaceWithStmt *namespaceWithStmt();

ASTNamespaceWithValueLL *namespaceWithValueList();
ASTNamespaceWithValue *namespaceWithValue();

ASTCompilerTagLL *progCompilerTagList(void);
ASTCompilerTag *progCompilerTag(void);
void progCheckCompilerSettings(ProgCompilerSettings *settings, ASTCompilerTagLL *compTags);

ASTDeclLL *declList(SymTable *tbl);
ASTDecl *decl(SymTable *tbl);
ASTTagLL *declTagList(void);
ASTTag *declTag(void);
ASTTagParamLL *declTagParamList(void);
Token declTagParam(void);

ASTDecl *importDecl(void);
ASTDecl *funcDecl(void);
ASTDecl *operatorDecl(void);

TokenLL *declSpecifiers(void);
ASTDecl *varDecl(bool isStructUnionMember);
ASTDecl *typealiasDecl(void);
ASTDecl *structDecl(void);
ASTDecl *unionDecl(void);
ASTDecl *enumDecl(void);
ASTDecl *methodBlockDecl(void);

ASTDeclLL *varDeclList(bool isStructUnionMember);
ASTEnumMembLL *enumMembList(void);
ASTEnumMemb *enumMemb(void);

ASTBlock *block(void);

ASTStmt *stmt(void);
ASTStmtLL *stmtList(void);

ASTStmt *ifStmt(void);
ASTStmt *whileStmt(void);
ASTStmt *forStmt(void);
ASTStmt *retStmt(void);
ASTStmt *contStmt(void);
ASTStmt *breakStmt(void);
ASTStmt *deferStmt(void);
ASTStmt *usingStmt(void);
ASTStmt *matchStmt(void);
ASTMatchArmLL *matchArmList(void);
ASTMatchArm *matchArm(void);

ASTType *typeSpec(void);
ASTType *arrayTypeSpec(void);
ASTType *pointerTypeSpec(void);
ASTType *funcTypeSpec(void);
ASTType *tupleTypeSpec(void);

ASTFuncSig *funcSig(void);
ASTFuncParamLL *funcParamList(void);
ASTFuncParam *funcParam(void);

ASTTupleMemb *tupleMemb(void);
ASTTupleMembLL *tupleMembLL(void);

ASTGenericType *genericType(void);
ASTGenericTypeLL *genericTypeLL(void);

ASTExpr *expr(void);
ASTExpr *logOpExpr(void);
ASTExpr *rangeTerm(void);
ASTExpr *bitTerm(void);
ASTExpr *relEqTerm(void);
ASTExpr *relTerm(void);
ASTExpr *shiftTerm(void);
ASTExpr *addTerm(void);
ASTExpr *multTerm(void);
ASTExpr *unaryTerm(void);
ASTExpr *postExprTerm(void);
ASTExpr *primaryTerm(void);
ASTExpr *scopeAccessExprMaybeIdenExpr(void);

ASTExpr *blockExpr(bool belongsToExprConstruct);
ASTExpr *ifExpr(void);
ASTExpr *whileExpr(void);
ASTExpr *matchExpr(void);
ASTMatchArmLL *matchExprArmList(void);
ASTMatchArm *matchExprArm(void);

ASTExprLL *exprList(void);
ASTNamedExprLL *namedExprList(void);
ASTNamedExpr *namedExpr(void);

#endif