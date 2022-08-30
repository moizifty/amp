#ifndef PASSES_H
#define PASSES_H

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#include "lexer.h"
#include "parser.h"
#include "checker.h"
#include "namespacetable.h"

void checkEntireProgram(void);

void pass1ImportDecls(ASTDeclLL *importDecls);

void pass2TopLevelAliasesStructured(ASTDeclLL *aliasesDecls, ASTDeclLL *constsDecls, ASTDeclLL *structuredDecls, ASTDeclLL *enumDecls, ASTDeclLL *operFuncDecls);
void pass2FillRemainingInfo(ASTDeclLL *aliasesDecls, ASTDeclLL *constsDecls, ASTDeclLL *structuredDecls, ASTDeclLL *enumDecls);
void finalCheckOnStructuredDecl(CheckerType *sType);
void pass2completeTopLevelStructuredDeclChecking(ASTDeclLL *structuredDecls);

void pass3TopLevelFuncsMethodsVars(ASTDeclLL *funcDecls, ASTDeclLL *methodBlockDecls, ASTDeclLL *varConstsDecls);
void pass3TopLevelFuncBodys(ASTDeclLL *funcDecls, ASTDeclLL *methodBlockDecls);

#endif