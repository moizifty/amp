#ifndef CODEGEN_LLVM_H
#define CODEGEN_LLVM_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/Scalar.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm-c/Transforms/IPO.h>

#include "llvm-c/DebugInfo.h"

#include "ast.h"
#include "namespacetable.h"
#include "symtable.h"
#include "abi.h"

void cgLLVMInitialValuesForProgGlobalVars(ASTDeclLL *globalVarConstLL);
void cgLLVMEntireProgram(TypeTable *typeTable, NamespaceTable *namespaceTable);
void initLLVMCodeGen();

void cgLLVMNamespaceTableDoAllTypes(NamespaceTable *nsTable);
void cgLLVMNamespaceTable(NamespaceTable *nsTable);
LLVMValueRef cgLLVMTypeTableEntry(CheckerType *type);
LLVMValueRef cgLLVMTypeTableEntryTypeUnion(CheckerType *type);

void cgLLVMTypesPrototypes(ASTProg *p);
void cgLLVMTypesDecls(ASTProg *p);
void cgLLVMStructUnionEmbedded(CheckerType *type);
void cgLLVMCompleteStructuredType(CheckerType *type);
LLVMTypeRef cgLLVMStructFromCheckerType(CheckerType *type);
LLVMTypeRef cgLLVMUnionFromCheckerType(CheckerType *type);
LLVMTypeRef cgLLVMTaggedUnionFromCheckerType(CheckerType *type);
LLVMTypeRef cgLLVMTupleFromCheckerType(CheckerType *type);

void cgLLVMProgsDeclValueRefs(ASTProg *p);
void cgLLVMDeclValueRef(ASTDecl *decl);

void cgLLVMMethodBlock(ASTDecl *d, bool isSignaturesOnly);
void cgLLVMDeclSignatureInMethodBlock(ASTDecl *methodBlock, ASTDecl *d);
void cgLLVMDeclInMethodBlock(ASTDecl *methodBlock, ASTDecl *d);

void cgLLVMProg(ASTProg *p);
void cgLLVMDecl(ASTDecl *decl);

void cgLLVMBlock(ASTBlock *block);
void cgLLVMStmt(ASTStmt *stmt);
void cgLLVMStmtAssign(ASTStmt *stmt);
void cgLLVMStmtDecl(ASTStmt *stmt);
void cgLLVMStmtWhile(ASTStmt *stmt);
void cgLLVMStmtFor(ASTStmt *stmt);
void cgLLVMStmtMatch(ASTStmt *stmt);
void cgLLVMStmtMatchArm(ASTStmt *matchStmt, ASTMatchArm *arm, 
                        LLVMValueRef *exprVals, LLVMBasicBlockRef myBlock, LLVMBasicBlockRef nextBlock, 
                        LLVMBasicBlockRef nextCondBlock, LLVMBasicBlockRef exitBlock);

void cgLLVMStmtIf(ASTStmt *stmt);
void cgLLVMImplicityStmtRet();
void cgLLVMStmtRet(ASTStmt *stmt);
void cgLLVMABIRet(LLVMValueRef exprToRet);

LLVMValueRef cgLLVMExpr(ASTExpr *expr);
LLVMValueRef cgLLVMBuildLogicalAndOrWithValueS(TokType op, LLVMValueRef lhs, LLVMValueRef rhs, CheckerType *lhsType, CheckerType *rhsType);
LLVMValueRef cgLLVMBuildEqWithValueS(TokType op, LLVMValueRef lhs, LLVMValueRef rhs, CheckerType *lhsType, CheckerType *rhsType);
LLVMValueRef cgLLVMStructLitFromExpr(ASTExpr *expr);
LLVMValueRef cgLLVMTaggedUnionLitFromExpr(ASTExpr *expr);

LLVMValueRef cgLLVMArrayViewLit(ASTExpr *expr);
LLVMValueRef cgLLVMAnyLit(ASTExpr *expr);
LLVMValueRef cgLLVMAnyLitFromValueRef(LLVMValueRef valRef, CheckerType *exprType);
LLVMValueRef cgLLVMRangeLit(ASTExpr *expr);
LLVMValueRef cgLLVMCastExpr(CheckerType *castTo, ASTExpr *expr);
LLVMValueRef cgLLVMSourceLocLit(TokenPos pos);

void cgLLVMAddParameterAttributes(LLVMValueRef fnRef, ArgABIInfo *abiInfo, bool isReturn, size_t indexOfParameter);
LLVMTypeRef cgLLVMCheckerTypeToTypeRef(CheckerType *type, bool isGlobalFuncType);

void cgLLVMAppendAndSwitchToBlock(LLVMValueRef fnRef, LLVMBasicBlockRef block);
LLVMValueRef cgLLVMTypesTypeEntry(CheckerType *type);
LLVMValueRef cgLLVMPossibleLValueToRValue(LLVMValueRef val, LLVMTypeRef rvalueType);
LLVMValueRef cgLLVMAlloc(LLVMTypeRef type, char *name, size_t alignment);
LLVMValueRef cgLLVMAllocAligned(LLVMTypeRef type, char *name);
LLVMValueRef cgLLVMBitcast(LLVMValueRef val, LLVMTypeRef target);
LLVMValueRef cgLLVMStore(LLVMValueRef toStore, LLVMValueRef ptr, LLVMTypeRef storageType);
LLVMValueRef cgLLVMLoad(LLVMValueRef toLoad, char *name);

LLVMValueRef cgLLVMCreateStringConstantAndGlobal(char *str, size_t len);
char *cgLLVMStringToLLVMCompatibleString(char *str, size_t *outLen);

#endif