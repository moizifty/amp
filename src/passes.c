#include "passes.h"
#include "compiler_internal.h"

extern GlobalContext globalContext;

void checkEntireProgram(void)
{
    if(globalContext.pc.numErrors > 0) return;
    //pass 1 on all namespaces
    {
        NSTEntryLL *currNSEntry = globalContext.namespaceTable->entries->first;
        while(currNSEntry != NULL)
        {
            ASTProgLL *currProgAst = currNSEntry->item->progASTs;
            if(currProgAst != NULL) currProgAst = currProgAst->first;
            while(currProgAst != NULL)
            {
                pass1ImportDecls(currProgAst->item->globalImportDecls);
                currProgAst = currProgAst->next;
            }

            currNSEntry = currNSEntry->next;
        }
    }

    //pass 2a
    {
        NSTEntryLL *currNSEntry = globalContext.namespaceTable->entries->first;
        while(currNSEntry != NULL)
        {
            ASTProgLL *currProgAst = currNSEntry->item->progASTs;
            if(currProgAst != NULL) currProgAst = currProgAst->first;
            while(currProgAst != NULL)
            {
                pass2TopLevelAliasesStructured(currProgAst->item->globalTypeDecls,
                                               currProgAst->item->globalVarOrConstDecls,
                                               currProgAst->item->globalStructuredDecls,
                                               currProgAst->item->globalEnumDecls,
                                               currProgAst->item->globalFuncDecls);
                currProgAst = currProgAst->next;
            }

            currNSEntry = currNSEntry->next;
        }
    }

    SymEntry *anyTypeEntry  = _symTableLookupType(globalContext.preludeNSEntry->namespaceSymTable, "any");
    SymEntry *arrayViewTypeEntry  = _symTableLookupType(globalContext.preludeNSEntry->namespaceSymTable, "ArrayView");
    SymEntry *typeInfoTypeEntry  = _symTableLookupType(globalContext.preludeNSEntry->namespaceSymTable, "TypeInfo");
    SymEntry *stringTypeEntry  = _symTableLookupType(globalContext.preludeNSEntry->namespaceSymTable, "string");
    SymEntry *rangeTypeEntry  = _symTableLookupType(globalContext.preludeNSEntry->namespaceSymTable, "range");
    
    if(anyTypeEntry == NULL)
        fprintf(stderr, "Failed to import the prelude module\n");
    else anyType = anyTypeEntry->type;

    if(arrayViewTypeEntry == NULL)
        fprintf(stderr, "Failed to import the prelude module\n");
    else arrayViewType = arrayViewTypeEntry->type;

    if(typeInfoTypeEntry == NULL)
        fprintf(stderr, "Failed to import the prelude module\n");
    else typeInfoType = typeInfoTypeEntry->type;

    if(stringTypeEntry == NULL)
        fprintf(stderr, "Failed to import the prelude module\n");
    else stringType = stringTypeEntry->type;

    if(rangeTypeEntry == NULL)
        fprintf(stderr, "Failed to import the prelude module\n");
    else rangeType = rangeTypeEntry->type;
    //pass 2b (catch reused identifiers when "using namespaces")
    {
        NSTEntryLL *currNSEntry = globalContext.namespaceTable->entries->first;
        while(currNSEntry != NULL)
        {
            SymTable *currSymTbl = currNSEntry->item->namespaceSymTable;

            SymTableLL *comparingAgainstSymTblLL = currSymTbl->continuationTbls;
            SymTable *comparingAgainstSymTbl = NULL;

            if(comparingAgainstSymTblLL != NULL)
            {
                comparingAgainstSymTblLL = comparingAgainstSymTblLL->first;
                comparingAgainstSymTbl = comparingAgainstSymTblLL->item;

                while((currSymTbl != NULL) && (comparingAgainstSymTbl != NULL))
                {
                    SymTableLL *compSymTblLL = comparingAgainstSymTblLL;
                    SymTable *compSymTbl = comparingAgainstSymTbl;
                
                    while(compSymTbl != NULL)
                    {
                        //types
                        {
                            SymEntryLL *currEntry = currSymTbl->typeTbl;
                            if(currEntry != NULL) currEntry = currEntry->first;
                            while(currEntry != NULL)
                            {
                                SymEntry *e = _symTableLookUp(compSymTbl, currEntry->item->name, LOOKUP_ALL_BUT_NOT_LOCAL);
                                if((e != NULL) && (e->mySymTable != currSymTbl))
                                {
                                    checkerErrorLn(currEntry->item->posInFile, "Namespace '%s' contains identifier '%s' at (%d: %d) in file %s, " 
                                                                               "this collides with identifer in module %s at (%d: %d) in file %s, try importing with an identifier and removing useNamespace tag", 
                                                                               currSymTbl->belongsToNamespace->namespace, currEntry->item->name, currEntry->item->posInFile.pos.linenum, currEntry->item->posInFile.pos.colnum,
                                                                               currEntry->item->posInFile.pos.filename, e->mySymTable->belongsToNamespace->namespace, e->posInFile.pos.linenum, e->posInFile.pos.colnum, e->posInFile.pos.filename);

                                    prettyPrintCheckerSourceError(currEntry->item->posInFile, currEntry->item->posInFile);
                                }

                                currEntry = currEntry->next;
                            }
                        }
                        
                        compSymTblLL = compSymTblLL->next;
                        compSymTbl = (compSymTblLL == NULL) ? NULL : compSymTblLL->item;
                    }

                    currSymTbl = comparingAgainstSymTbl;

                    comparingAgainstSymTblLL = comparingAgainstSymTblLL->next;
                    comparingAgainstSymTbl = (comparingAgainstSymTblLL == NULL) ? NULL : comparingAgainstSymTblLL->item;
                }
            }

            currNSEntry = currNSEntry->next;
        }
    }

    //pass 2c
    {
        NSTEntryLL *currNSEntry = globalContext.namespaceTable->entries->first;
        while(currNSEntry != NULL)
        {
            ASTProgLL *currProgAst = currNSEntry->item->progASTs;
            if(currProgAst != NULL) currProgAst = currProgAst->first;
            while(currProgAst != NULL)
            {
                pass2FillRemainingInfo(currProgAst->item->globalTypeDecls, 
                                       currProgAst->item->globalVarOrConstDecls,
                                       currProgAst->item->globalStructuredDecls, 
                                       currProgAst->item->globalEnumDecls);
                currProgAst = currProgAst->next;
            }

            currNSEntry = currNSEntry->next;
        }
    }

    //pass 2d
    {
        NSTEntryLL *currNSEntry = globalContext.namespaceTable->entries->first;
        while(currNSEntry != NULL)
        {
            ASTProgLL *currProgAst = currNSEntry->item->progASTs;
            if(currProgAst != NULL) currProgAst = currProgAst->first;
            while(currProgAst != NULL)
            {
                pass2completeTopLevelStructuredDeclChecking(currProgAst->item->globalStructuredDecls);
                currProgAst = currProgAst->next;
            }

            currNSEntry = currNSEntry->next;
        }
    }

    { //add dependencies for tuples

        CheckerTypeLL *ll = globalContext.cc.tupleTypes;
        if(ll != NULL) ll = ll->first;

        while (ll != NULL)
        {
            if(ll->item->tupleType.dependencies == NULL)
            {
                bool unusedCircularDependency = false;
                ScopedDecl *unused = NULL;
                StructUnionDependencyNode *r = getStructUnionDependencies(ll->item, NULL, &unusedCircularDependency, &unused);
            
                ll->item->tupleType.dependencies = r;
            }

            ll = ll->next;
        }
        
    }

    //pass 3a
    {
        NSTEntryLL *currNSEntry = globalContext.namespaceTable->entries->first;
        while(currNSEntry != NULL)
        {
            ASTProgLL *currProgAst = currNSEntry->item->progASTs;
            if(currProgAst != NULL) currProgAst = currProgAst->first;
            while(currProgAst != NULL)
            {
                pass3TopLevelFuncsMethodsVars(currProgAst->item->globalFuncDecls, currProgAst->item->globalMethodBlockDecls, currProgAst->item->globalVarOrConstDecls);
                currProgAst = currProgAst->next;
            }

            currNSEntry = currNSEntry->next;
        }
    }

    if(globalContext.globalMainFuncType == NULL)
    {
        fprintf(stderr, "Main function in the global namespace not found!\n");
    }

    //pass 3b
    {
        NSTEntryLL *currNSEntry = globalContext.namespaceTable->entries->first;
        while(currNSEntry != NULL)
        {
            ASTProgLL *currProgAst = currNSEntry->item->progASTs;
            if(currProgAst != NULL) currProgAst = currProgAst->first;
            while(currProgAst != NULL)
            {
                pass3TopLevelFuncBodys(currProgAst->item->globalFuncDecls, currProgAst->item->globalMethodBlockDecls);
                currProgAst = currProgAst->next;
            }

            currNSEntry = currNSEntry->next;
        }
    }
}

void pass1ImportDecls(ASTDeclLL *importDecls)
{
    SymTable *tempTable = globalContext.cc.checkingLocalsSymTble;

    if(importDecls == NULL) return;

    ASTDeclLL *currDecl = importDecls->first;
    SymTable *mySymTable = currDecl->item->tbl;

    while(currDecl != NULL)
    {
        ASTDecl *d = currDecl->item;
        globalContext.cc.checkingLocalsSymTble = d->tbl;

        checkImportDecl(d, true);

        currDecl = currDecl->next;
    }

    globalContext.cc.checkingLocalsSymTble = tempTable;
}

void pass2TopLevelAliasesStructured(ASTDeclLL *aliasesDecls, ASTDeclLL *constsDecls, ASTDeclLL *structuredDecls, ASTDeclLL *enumDecls, ASTDeclLL *operFuncDecls)
{
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;
    // Typealiases
    if(aliasesDecls != NULL)
    {
        ASTDeclLL *currDecl = aliasesDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;
            char *name = d->type.iden.lexeme;
            
            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupType(d->tbl, name);
            
            if(e != NULL)
            {
                checkerError(d->startTok, "Identifier '%s' is already being used for declaration of type ", name);
                printCheckerType(e->type);
                fprintf(stderr, "\n");

                prettyPrintCheckerSourceError(d->type.iden, d->type.type->iden);
            }
            else
            {
                CheckerType *t = newCheckerTypeAliased(NULL, name, checkDeclTags(d, d->tags));
                e = _symTableInsertType(d->tbl, name, t, d->startTok);
                e->type->belongsToFile = d->startTok.pos.filename;
                e->type->namespaceName = d->tbl->belongsToNamespace->namespace;
                d->declType = e->type;
            }

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }

    globalContext.cc.isIncompletePass = true;

     //consts so tht they can be used in types
    if(constsDecls != NULL)
    {
        ASTDeclLL *currDecl = constsDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;

            if(d->kind == A_DECL_VAR)
            {
                currDecl = currDecl->next;
                continue;
            }

            SymEntryLL *restoreLocal = d->tbl->localTbl;
            SymEntryLL *restoreConst = d->tbl->constTbl;
            SymEntryLL *restoreType = d->tbl->typeTbl;

            char *name = NULL;

            bool isVar = d->kind == A_DECL_VAR;
            bool isConst = d->kind == A_DECL_CONST;

            //todo: tuple lhs var decl
            name = (isVar) ? d->var.idenExpr->iden.lexeme : d->constDecl.iden.lexeme;

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupGlobal(d->tbl, name);
            
            if(e != NULL)
            {
                checkerError(d->startTok, "Identifier '%s' is already being used for declaration of type ", name);
                printCheckerType(e->type);
                fprintf(stderr, "\n");
            }
            else
            {
                CheckerType *type = NULL;

                if(isVar)
                {
                    d->var.globalVarInitialCodeGenStmts = newASTStmtLL(NULL);

                    globalContext.cc.currStmtBeingChecked = d->var.globalVarInitialCodeGenStmts;
                    if(d->var.type->kind != A_TYPE_INFER) 
                    {
                        checkType(d->var.type);
                        //todo: CHECK type ==  initializer type
                        if((d->var.initial != NULL) && !checkRHSExprWithTypeAndCast(d->var.type->checkType, &d->var.initial, true, false))
                        {
                            checkerError(d->startTok, "Expected variable declaration type to match initial value but type: ");
                            printCheckerType(d->var.type->checkType);
                            fprintf(stderr, " and initial value: ");
                            printCheckerType(d->var.initial->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(d->var.initial->startTok, d->var.initial->endTok);
                        }
                    }
                    else 
                    {
                        checkExpr(d->var.initial, false);
                        d->var.type->checkType = d->var.initial->checkType;
                    }

                    type = d->var.type->checkType;
                    
                    globalContext.cc.currStmtBeingChecked = NULL;
                }
                else
                {
                    //TODO CONSTS
                }

                if(type != NULL)
                    SET_TYPE_FLAG(type, checkDeclTags(d, d->tags));

                if(isVar)
                {
                    e = _symTableInsertGlobal(d->tbl, name, type, d->startTok);
                }
                else
                {
                    e = _symTableInsertGlobal(d->tbl, name, type, d->startTok);
                    e->isActualConst = true;
                }

                d->declType = e->type;

            }
            
            _symTableRestoreLocal(d->tbl, restoreLocal);
            _symTableRestoreConst(d->tbl, restoreConst);
            _symTableRestoreType(d->tbl, restoreType);

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }

    // STRUCT UNION ENUMS
    if(structuredDecls != NULL)
    {
        ASTDeclLL *currDecl = structuredDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;

            char *name = NULL;
            bool isEnum = false;
            bool isStruct = false;
            bool isUnion = false;
            bool isTaggedUnion = false;

            isEnum = d->kind == A_DECL_ENUM;
            isStruct = d->kind == A_DECL_STRUCT;
            isUnion = d->kind == A_DECL_UNION;
            isTaggedUnion = d->kind == A_DECL_UNION_TAGGED;

            name = (isEnum) ? d->enumDecl.iden.lexeme : ((isStruct) ? d->structDecl.iden.lexeme : ((isUnion) ? d->unionDecl.iden.lexeme : d->unionTaggedDecl.iden.lexeme));

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupType(d->tbl, name);

            if(e != NULL)
            {
                checkerError(d->startTok, "Identifier '%s' is already being used for declaration of type ", name);
                printCheckerType(e->type);
                fprintf(stderr, "\n");
                
                prettyPrintCheckerSourceError(d->structDecl.iden, d->structDecl.iden);
            }
            else
            {
                CheckerType *t = NULL;
                int flags = checkDeclTags(d, d->tags);
                
                //if enum u might aswell check members, since they hve to be contant expressions
                if(isEnum) checkDeclEnum(d, true);
                else 
                {
                    if(isStruct) t = newCheckerTypeStruct(NULL, name, flags);
                    else if(isUnion) t = newCheckerTypeUnion(NULL, name, flags);
                    else if(isTaggedUnion) t = newCheckerTypeTaggedUnion(NULL, name, NULL, flags);

                    e = _symTableInsertType(d->tbl, name, t, d->startTok);
                    e->type->belongsToFile = d->startTok.pos.filename;
                    e->type->namespaceName = d->tbl->belongsToNamespace->namespace;
                    d->declType = e->type;
                }
            }

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }
    
    if(enumDecls != NULL)
    {
        ASTDeclLL *currDecl = enumDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;

            char *name = d->enumDecl.iden.lexeme;

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupType(d->tbl, name);

            if(e != NULL)
            {
                checkerError(d->startTok, "Identifier '%s' is already being used for declaration of type ", name);
                printCheckerType(e->type);
                fprintf(stderr, "\n");
                
                prettyPrintCheckerSourceError(d->structDecl.iden, d->structDecl.iden);
            }
            else
            {
                CheckerType *t = NULL;
                int flags = checkDeclTags(d, d->tags);
                
                {
                    t = newCheckerTypeEnum(NULL, name, flags);

                    e = _symTableInsertType(d->tbl, name, t, d->startTok);
                    e->type->belongsToFile = d->startTok.pos.filename;
                    e->type->namespaceName = d->tbl->belongsToNamespace->namespace;
                    d->declType = e->type;
                }
            }

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }

    if(operFuncDecls != NULL)
    {
        ASTDeclLL *currDecl = operFuncDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;
            int declFlags = checkDeclTags(d, d->tags);

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            switch(d->kind)
            {
                case A_DECL_OPERATOR_FUNC:
                {
                    char *name = NULL;
                    ScopedDeclLL *sdLL = NULL;
                    
                    ASTFuncParamLL *astParamLL = d->operFunc.sig->params;
                    if(astParamLL == NULL) checkerErrorLn(d->startTok, "Operator overload function cannot have zero parameters, atleast 1 is required");
                    else if(astParamLL->first->numItems > 2)  checkerErrorLn(d->startTok, "Operator overload function cannot have more than 2 parameters");
                    else
                    {
                        ASTFuncParamLL *c = astParamLL->first;
                        
                        SymEntryLL *restoreTo = d->tbl->localTbl;
                        while(c != NULL)
                        {
                            ASTFuncParam *param = c->item;

                            SymEntry *paramEntry = _symTableLookupLocal(d->tbl, param->iden.lexeme);
                            if(paramEntry != NULL)
                            {
                                checkerError(param->iden, "Parameter with name '%s' has already been declared for parameter of type ", param->iden.lexeme);
                                printCheckerType(paramEntry->type);
                                fprintf(stderr, "\n");

                                prettyPrintCheckerSourceError(param->iden, param->iden);
                            }
                            else
                            {
                                if(param->defValue != NULL)
                                {
                                    checkerErrorLn(param->iden, "Parameters such as '%s' in Operator overload functions cannot have default values", param->iden.lexeme);
                                }
                                checkType(param->type);

                                if(sdLL == NULL) sdLL = newScopedDeclLL(newScopedDecl(param->iden, param->type->checkType, param->defValue, 0));
                                else addScopedDeclLL(&sdLL, newScopedDecl(param->iden, param->type->checkType, param->defValue, 0));

                                _symTableInsertLocal(d->tbl, param->iden.lexeme, param->type->checkType, param->iden);
                            }
                            c = c->next;
                        }
                        
                        _symTableRestoreLocal(d->tbl, restoreTo);

                        checkType(d->operFunc.sig->retType);
                        d->operFunc.sig->retAsArg = checkIfFuncRetTypeShouldBePassedAsArg(d->operFunc.sig->retType->checkType);
                        if(d->operFunc.sig->retAsArg)
                        {
                            d->operFunc.sig->retAsArgName = d->startTok;
                            d->operFunc.sig->retAsArgName.type = TOK_IDEN;
                            strcpy(d->operFunc.sig->retAsArgName.lexeme, "__retAsArg");
                        }

                        name = allocOperFuncName(d->operFunc.op, sdLL, d->operFunc.sig->retType->checkType);
                        //TODO: Check param types, and see if theyre are only struct, unions and arrays. but depending on operation
                        //sinec index overloads can take in a int or anything else as the second argument

                        SymEntry *e = _symTableLookUp(globalContext.symTable, name, LOOKUP_ALL);

                        if(e != NULL) checkerErrorLn(d->startTok, "Operator overload function with same signature already exists");
                        else
                        {
                            CheckerType *t = newCheckerTypeFunc(sdLL, d->operFunc.sig->retType->checkType, d->operFunc.sig->retAsArg, checkDeclTags(d, d->tags));
                            t->funcType.isOperFunc = true;
                            t->funcType.numDefaultParams = 0;
                            
                            e = _symTableInsertGlobal(globalContext.symTable, name, t, d->startTok);
                            
                            e->type->belongsToFile = d->startTok.pos.filename;
                            e->type->namespaceName = d->tbl->belongsToNamespace->namespace;
                            e->isGlobalFunc = true;
                            d->declType = e->type;
                            e->myDecl = d;
                            d->operFunc.mangledName = name;
                        }
                    }
                }break;
            }

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }
}
void pass2FillRemainingInfo(ASTDeclLL *aliasesDecls, ASTDeclLL *constsDecls, ASTDeclLL *structuredDecls, ASTDeclLL *enumDecls)
{
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;
    // Typealiases
    if(aliasesDecls != NULL)
    {
        ASTDeclLL *currDecl = aliasesDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;
            char *name = d->type.iden.lexeme;

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupType(d->tbl, name);

            if(!isTypeAliased(e->type)) continue;
            else
            {
                checkType(d->type.type);

                e->type->aliasedType.base = d->type.type->checkType;
            }

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }

     //consts so tht they can be used in types
    if(constsDecls != NULL)
    {
        ASTDeclLL *currDecl = constsDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;

            if(d->kind == A_DECL_VAR)
            {
                currDecl = currDecl->next;
                continue;
            }

            SymEntryLL *restoreLocal = d->tbl->localTbl;
            SymEntryLL *restoreConst = d->tbl->constTbl;
            SymEntryLL *restoreType = d->tbl->typeTbl;

            char *name = NULL;

            bool isVar = d->kind == A_DECL_VAR;
            bool isConst = d->kind == A_DECL_CONST;

            //todo: tuple lhs var decl
            name = (isVar) ? d->var.idenExpr->iden.lexeme : d->constDecl.iden.lexeme;

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupGlobal(d->tbl, name);
            
            {
                CheckerType *type = NULL;

                if(isVar)
                {
                    d->var.globalVarInitialCodeGenStmts = newASTStmtLL(NULL);

                    globalContext.cc.currStmtBeingChecked = d->var.globalVarInitialCodeGenStmts;
                    if(d->var.type->kind != A_TYPE_INFER) 
                    {
                        checkType(d->var.type);
                        //todo: CHECK type ==  initializer type
                        if((d->var.initial != NULL) && !checkRHSExprWithTypeAndCast(d->var.type->checkType, &d->var.initial, true, false))
                        {
                            checkerError(d->startTok, "Expected variable declaration type to match initial value but type: ");
                            printCheckerType(d->var.type->checkType);
                            fprintf(stderr, " and initial value: ");
                            printCheckerType(d->var.initial->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(d->var.initial->startTok, d->var.initial->endTok);
                        }
                    }
                    else 
                    {
                        checkExpr(d->var.initial, false);
                        d->var.type->checkType = d->var.initial->checkType;
                    }

                    type = d->var.type->checkType;
                    
                    globalContext.cc.currStmtBeingChecked = NULL;
                }
                else
                {       
                    if(d->constDecl.type->kind != A_TYPE_INFER) 
                    {
                        checkType(d->constDecl.type);
                        //todo: CHECK type ==  initializer type
                        if((d->constDecl.initial != NULL) && !checkRHSExprWithTypeAndCast(d->constDecl.type->checkType, &d->constDecl.initial, true, false))
                        {
                            checkerError(d->startTok, "Expected constant declaration type to match initial value but type: ");
                            printCheckerType(d->constDecl.type->checkType);
                            fprintf(stderr, " and initial value: ");
                            printCheckerType(d->constDecl.initial->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(d->constDecl.initial->startTok, d->constDecl.initial->endTok);
                        }
                    }
                    else 
                    {
                        checkExpr(d->constDecl.initial, false);
                        d->constDecl.type->checkType = d->constDecl.initial->checkType;
                    }

                    if(d->constDecl.initial->compTimeVal.kind == A_EXPR_COMP_TIME_RUNTIME)
                    {
                        checkerErrorLn(d->startTok, "Expected constant declaration to have constant initial value");

                        prettyPrintCheckerSourceError(d->constDecl.initial->startTok, d->constDecl.initial->endTok);
                    }

                    type = d->constDecl.type->checkType;
                }

                if(type != NULL)
                    SET_TYPE_FLAG(type, checkDeclTags(d, d->tags));

                if(isVar)
                {
                    e = _symTableInsertGlobal(d->tbl, name, type, d->startTok);
                }
                else
                {
                    e->type = type;
                    e->isActualConst = true;
                    e->constVal = d->constDecl.initial->compTimeVal;
                }

                d->declType = e->type;

            }
            
            _symTableRestoreLocal(d->tbl, restoreLocal);
            _symTableRestoreConst(d->tbl, restoreConst);
            _symTableRestoreType(d->tbl, restoreType);

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }


    if(enumDecls != NULL)
    {
        ASTDeclLL *currDecl = enumDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;

            char *name = d->enumDecl.iden.lexeme;
            SymTable *st = d->tbl;
            globalContext.cc.checkingLocalsSymTble = d->tbl;

            SymEntry *e = _symTableLookupType(st, name);

            int flags = checkDeclTags(d, d->tags);

            EnumMembLL **membLL = &(e->type->enumType.membLL);

            ASTEnumMembLL *astEnumMembLL = d->enumDecl.membs->first;

            SymEntryLL *restorePoint = st->localTbl;

            int64_t prevVal = -1;
            while(astEnumMembLL != NULL)
            {
                char *membName = astEnumMembLL->item->id.lexeme;
                int64_t val = 0;

                if(_symTableLookupLocal(st, membName) != NULL)
                {
                    checkerErrorLn(astEnumMembLL->item->id, "Enum member '%s' is already defined", membName);
                }
                else
                {
                    checkExpr(astEnumMembLL->item->value, false);
                    bool isFlagEnum = flags & TYPE_ENUM_FLAGS;

                    if(astEnumMembLL->item->value != NULL)
                    {
                        if(!isFlagEnum)
                        {
                            if(!IS_EXPR_COMPTIME_KIND(astEnumMembLL->item->value, A_EXPR_COMP_TIME_INT))
                            {
                                checkerErrorLn(astEnumMembLL->item->id, "Expected a constant integer expression for enum member");
                                prettyPrintCheckerSourceError(astEnumMembLL->item->id, astEnumMembLL->item->value->endTok);
                            }
                            else 
                            {
                                val = GET_EXPR_COMPTIME_VAL(astEnumMembLL->item->value, .i);
                                prevVal = val;
                            }
                        }
                        else checkerErrorLn(astEnumMembLL->item->id, "enum '%s' has the [flag] tag, which means this is an enum flag,"
                                                                    " members in an enum flag cannot have default values!", name);
                    }
                    else
                    {
                        if(!isFlagEnum) val = ++prevVal;
                        else val = 1 << (++prevVal);

                    }

                    EnumMemb *memb = newEnumMemb(membName, val);
                    if(*membLL == NULL) *membLL = newEnumMembLL(memb);
                    else addEnumMembLL(membLL, memb);

                    _symTableInsertLocal(st, membName, newCheckerTypeEnumMemb(e->type, val), astEnumMembLL->item->id);
                }

                astEnumMembLL = astEnumMembLL->next;
            }

            _symTableRestoreLocal(st, restorePoint);

            e->isGlobal = true;
            
            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }

    // Fill struct/union member data, (but dont check default values just yet)
    if(structuredDecls != NULL)
    {
        ASTDeclLL *currDecl = structuredDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;

            char *name = NULL;
            bool isEnum = false;
            bool isStruct = false;
            bool isUnion = false;
            bool isTaggedUnion = false;

            isEnum = d->kind == A_DECL_ENUM;
            isStruct = d->kind == A_DECL_STRUCT;
            isUnion = d->kind == A_DECL_UNION;
            isTaggedUnion = d->kind == A_DECL_UNION_TAGGED;

            if(isEnum) 
            {
                currDecl = currDecl->next;
                continue; //theyve already been fully checked.
            }

            name = (isStruct) ? d->structDecl.iden.lexeme : ((isUnion) ? d->unionDecl.iden.lexeme : d->unionTaggedDecl.iden.lexeme);

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupType(d->tbl, name);

            if(isTaggedUnion)
            {
                checkType(d->unionTaggedDecl.tagTypeName);
                if(!isTypeEnum(d->unionTaggedDecl.tagTypeName->checkType))
                {
                    checkerError(d->unionTaggedDecl.tagTypeName->startTok, "Expected tag type for tagged union to be an enum type, instead got: ");
                    printCheckerType(d->unionTaggedDecl.tagTypeName->checkType);
                    fprintf(stderr, "\n");

                    prettyPrintCheckerSourceError(d->unionTaggedDecl.tagTypeName->startTok, d->unionTaggedDecl.tagTypeName->endTok);

                    currDecl = currDecl->next;
                    continue;
                }
                else e->type->taggedUnionType.tagEnumType = d->unionTaggedDecl.tagTypeName->checkType;
            }

            
            SymEntryLL *restoreTo = d->tbl->localTbl;
            ScopedDeclLL **membs = (isStruct) ? &e->type->structType.declLL : ((isUnion) ? &e->type->unionType.declLL : &e->type->taggedUnionType.declLL);
            ASTDeclLL *astMembs = (isStruct) ? d->structDecl.membs->first :  ((isUnion) ? d->unionDecl.membs->first : d->unionTaggedDecl.membs->first);

            while(astMembs != NULL)
            {
                if(astMembs->item->var.idenExpr->kind == A_EXPR_IDEN)
                {
                    char *membName = astMembs->item->var.idenExpr->iden.lexeme;
                    SymEntry *membEntry = _symTableLookupLocal(d->tbl, membName);

                    if(membEntry != NULL)
                    {
                        checkerErrorLn(astMembs->item->startTok, "Identifier '%s' in struct/union has already been declared over here: ", membName);

                        prettyPrintCheckerSourceError(membEntry->posInFile, membEntry->posInFile);
                    }
                    else
                    {
                        if(isUnion || isStruct)
                        {
                            checkExpr(astMembs->item->var.initial, true);

                            if(astMembs->item->var.type->kind == A_TYPE_INFER) astMembs->item->var.type->checkType = astMembs->item->var.initial->checkType;
                            else checkType(astMembs->item->var.type);
                            
                            ASTDecl *membDecl = astMembs->item;
                            bool hasDefValue = astMembs->item->var.initial != NULL;

                            ScopedDecl *s = newScopedDecl(membDecl->var.idenExpr->iden, membDecl->var.type->checkType, membDecl->var.initial, checkScopedDeclTags(astMembs->item));

                            if(CHECK_SCOPED_DECL_FLAG(s, SCOPED_DECL_VALUE_REQUIRED) && hasDefValue)
                            {
                                checkerErrorLn(s->name, "Member '%s' is declared as having the [required] tag, which means it cant have a default value, the value can only be given in a struct instance.", membName);
                                prettyPrintCheckerSourceError(s->name, s->name);
                            }

                            if(hasDefValue) SET_SCOPED_DECL_FLAG(s, SCOPED_DECL_HAS_DEF_VALUE);

                            if((*membs) == NULL) *membs = newScopedDeclLL(s);
                            else addScopedDeclLL(membs, s);

                            _symTableInsertLocal(d->tbl, membName, (membDecl->var.type == NULL) ?
                                                                    NULL : membDecl->var.type->checkType, membDecl->startTok);
                        }
                        else if(isTaggedUnion)
                        {
                            CheckerType *tagType = d->unionTaggedDecl.tagTypeName->checkType;
                            
                            if(astMembs->item->var.initial != NULL)
                            {
                                checkerErrorLn(astMembs->item->startTok, "Members in tagged unions cannot have default values!");
                                prettyPrintCheckerSourceError(astMembs->item->startTok, astMembs->item->endTok);
                            }
                            else
                            {
                                CheckerType *enumType = (isTypeAliased(tagType)) ? getAliasedTypeBase(tagType) : tagType;
                                ASTDecl *membDecl = astMembs->item;

                                size_t membIndex = 0;
                                if(typeHasMember(enumType, membDecl->var.idenExpr->iden.lexeme, &membIndex))
                                {
                                    checkType(membDecl->var.type);
                                    
                                    ScopedDecl *s = newScopedDecl(membDecl->var.idenExpr->iden, membDecl->var.type->checkType, NULL,checkScopedDeclTags(astMembs->item));
                                    
                                    if((*membs) == NULL) *membs = newScopedDeclLL(s);
                                    else addScopedDeclLL(membs, s);

                                    _symTableInsertLocal(d->tbl, membName, (membDecl->var.type == NULL) ?
                                                                            NULL : membDecl->var.type->checkType, membDecl->startTok);
                                }
                                else
                                {
                                    checkerError(membDecl->startTok, "Member in tagged union, must have the same name as a member from the tag type enum, in this case from type: ");
                                    printCheckerType(enumType);
                                    fprintf(stderr, "\n");

                                    prettyPrintCheckerSourceError(membDecl->startTok, membDecl->endTok);
                                }
                            }
                        }

                    }
                }

                astMembs = astMembs->next;
            }

            _symTableRestoreLocal(d->tbl, restoreTo);
            
            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }

     // now check default values also any members as having type inference, assign their types based on default values
}
void finalCheckOnStructuredDecl(CheckerType *sType)
{
    bool isUnion = isTypeUnion(sType);
    bool isStruct = isTypeStruct(sType);

    if(!isStruct && !isUnion) return;

    if(isTypeAliased(sType)) sType = getAliasedTypeBase(sType);
    
    if((isUnion && sType->unionType.isFullyChecked) ||
       (sType->structType.isFullyChecked))
    {
        return;
    }

    ScopedDeclLL *membs = (isStruct) ? sType->structType.declLL : sType->unionType.declLL;

    membs = membs->first;

    while(membs != NULL)
    {
        ScopedDecl *memb = membs->item;

        if(memb->defExpr != NULL)
        {
            if(isTypePointer(memb->type))
            {
                bool isExprNullLit = ((memb->defExpr->kind == A_EXPR_LIT) && (memb->defExpr->lit.type == TOK_NULL_KW));
                if(!isExprNullLit) checkerErrorLn(memb->name, "Struct/Union '%s' has member '%s' which is a pointer, and has a default value, this is not allowed, unless its a 'null' literal",
                                                    (isStruct) ? sType->structType.name : sType->unionType.name, memb->name.lexeme);
            }

            if(!checkRHSExprWithTypeAndCast(memb->type, &memb->defExpr, true, false))
            {
                checkerError(memb->name, "Type mismatch, type of default value for member '%s' does not match members type, expected: ",
                             memb->name.lexeme);
                
                printCheckerType(memb->type);
                fprintf(stderr, " instead got: ");
                printCheckerType(memb->defExpr->checkType);
                fprintf(stderr, "\n");
            }
        }
        membs = membs->next;
    }

    if(isUnion) sType->unionType.isFullyChecked = true;
    else sType->structType.isFullyChecked = true;
}
void checkStructUnionDependencyNode(StructUnionDependencyNode *node)
{
    if(node->children != NULL)
    {
        StructUnionDependencyNodeLL *currChild = node->children->first;
        
        while(currChild != NULL)
        {
            checkStructUnionDependencyNode(currChild->item);

            currChild = currChild->next;
        }
    }

    finalCheckOnStructuredDecl(node->type);
}
void pass2completeTopLevelStructuredDeclChecking(ASTDeclLL *structuredDecls)
{
    //go through all the struct or union decls
    //get a list of all the embeded structs in them, if they cause cycles give circular dependecy errors
    // then goo through the list of the embedded srtucts, and check them from the TOP first
    //since the top will always be a struct that is only made of primitives (including pointers)

    globalContext.cc.isIncompletePass = false;

    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;

    if(structuredDecls != NULL)
    {
        ASTDeclLL *currDecl = structuredDecls->first;

        while(currDecl != NULL)
        {    
            ASTDecl *d = currDecl->item;

            char *name = NULL;
            bool isEnum = false;
            bool isStruct = false;
            bool isUnion = false;
            bool isTaggedUnion = false;

            isEnum = d->kind == A_DECL_ENUM;
            isStruct = d->kind == A_DECL_STRUCT;
            isUnion = d->kind == A_DECL_UNION;
            isTaggedUnion = d->kind == A_DECL_UNION_TAGGED;

            if(isEnum) 
            {
                currDecl = currDecl->next;
                continue; //theyve already been fully checked.
            }

            name = (isStruct) ? d->structDecl.iden.lexeme : ((isUnion) ? d->unionDecl.iden.lexeme : d->unionTaggedDecl.iden.lexeme);

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupType(d->tbl, name);

            bool foundCircularDependency = false;
            ScopedDecl *whereCircular = NULL;

            StructUnionDependencyNode *r = getStructUnionDependencies(e->type, NULL, &foundCircularDependency, &whereCircular);

            if(foundCircularDependency)
            {

                if(whereCircular != NULL)
                {
                    checkerErrorLn(d->startTok, "Member '%s' in type '%s' causes circular dependencies", whereCircular->name.lexeme, name);
                    prettyPrintCheckerSourceError(whereCircular->name, whereCircular->name);
                }
                else
                {
                    checkerErrorLn(d->startTok, "Struct/union '%s' has circular dependencies!", name);
                    prettyPrintCheckerSourceError(d->startTok, d->endTok);
                }
            }
            else
            { //check all structs/union this struct depends on first
                checkStructUnionDependencyNode(r);
            }

            if(isUnion) e->type->unionType.dependencies = r;
            else if(isStruct) e->type->structType.dependencies = r;
            else if(isTaggedUnion) e->type->taggedUnionType.dependencies = r;

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }
}

void pass3TopLevelFuncsMethodsVars(ASTDeclLL *funcDecls, ASTDeclLL *methodBlockDecls, ASTDeclLL *varConstsDecls)
{
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;

    //func
    if(funcDecls != NULL)
    {
        ASTDeclLL *currDecl = funcDecls->first;

        while(currDecl != NULL)
        {
            checkGlobalFuncDecl(&currDecl, true);
            currDecl = currDecl->next;
        }
    }

    if(methodBlockDecls != NULL)
    {
        ASTDeclLL *currDecl = methodBlockDecls->first;
        
        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;
            checkMethodBlock(d, true);
            currDecl = currDecl->next;
        }
    }
    //var and consts
    if(varConstsDecls != NULL)
    {
        ASTDeclLL *currDecl = varConstsDecls->first;

        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;

            if(d->kind == A_DECL_CONST)
            {
                currDecl = currDecl->next;
                continue;
            }
            SymEntryLL *restoreLocal = d->tbl->localTbl;
            SymEntryLL *restoreConst = d->tbl->constTbl;
            SymEntryLL *restoreType = d->tbl->typeTbl;

            char *name = NULL;

            bool isVar = d->kind == A_DECL_VAR;
            bool isConst = d->kind == A_DECL_CONST;

            //todo: tuple lhs var decl
            name = (isVar) ? d->var.idenExpr->iden.lexeme : d->constDecl.iden.lexeme;

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            SymEntry *e = _symTableLookupGlobal(d->tbl, name);
            
            if(e != NULL)
            {
                checkerError(d->startTok, "Identifier '%s' is already being used for declaration of type ", name);
                printCheckerType(e->type);
                fprintf(stderr, "\n");
            }
            else
            {
                CheckerType *type = NULL;

                if(isVar)
                {
                    d->var.globalVarInitialCodeGenStmts = newASTStmtLL(NULL);

                    globalContext.cc.currStmtBeingChecked = d->var.globalVarInitialCodeGenStmts;
                    if(d->var.type->kind != A_TYPE_INFER) 
                    {
                        checkType(d->var.type);
                        //todo: CHECK type ==  initializer type
                        if((d->var.initial != NULL) && !checkRHSExprWithTypeAndCast(d->var.type->checkType, &d->var.initial, true, false))
                        {
                            checkerError(d->startTok, "Expected variable declaration type to match initial value but type: ");
                            printCheckerType(d->var.type->checkType);
                            fprintf(stderr, " and initial value: ");
                            printCheckerType(d->var.initial->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(d->var.initial->startTok, d->var.initial->endTok);
                        }
                    }
                    else 
                    {
                        checkExpr(d->var.initial, false);
                        d->var.type->checkType = d->var.initial->checkType;
                    }

                    type = d->var.type->checkType;
                    
                    globalContext.cc.currStmtBeingChecked = NULL;
                }
                else
                {
                    //TODO CONSTS
                }

                if(type != NULL)
                    SET_TYPE_FLAG(type, checkDeclTags(d, d->tags));

                if(isVar)
                {
                    e = _symTableInsertGlobal(d->tbl, name, type, d->startTok);
                }
                else
                {
                    e = _symTableInsertConst(d->tbl, name, type, d->startTok);
                }

                d->declType = e->type;
                e->myDecl = d;
            }
            
            _symTableRestoreLocal(d->tbl, restoreLocal);
            _symTableRestoreConst(d->tbl, restoreConst);
            _symTableRestoreType(d->tbl, restoreType);

            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            currDecl = currDecl->next;
        }
    }
}
void pass3TopLevelFuncBodys(ASTDeclLL *funcDecls, ASTDeclLL *methodBlockDecls)
{
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;

    if(funcDecls != NULL)
    {
        ASTDeclLL *currDecl = funcDecls->first;
        CheckerType *prevFuncType = globalContext.cc.currFuncTypeBeingChecked;
        //TODO operator functions
        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;

            if(d->genParams != NULL)
            {
                checkGenericFuncBody(d);

                currDecl = currDecl->next;
                continue;
            }

            globalContext.cc.checkingLocalsSymTble = d->tbl;
            globalContext.cc.currFuncTypeBeingChecked = d->declType;

            SymEntryLL *restoreLocal = d->tbl->localTbl;
            SymEntryLL *restoreConst = d->tbl->constTbl;
            SymEntryLL *restoreType = d->tbl->typeTbl;

            bool isOperfunc = (d->kind == A_DECL_OPERATOR_FUNC);

            ASTFuncParamLL *paramsLL = (isOperfunc) ? d->operFunc.sig->params->first : d->func.sig->params;

            if((paramsLL != NULL) && !isOperfunc) paramsLL = paramsLL->first;

            //insert all local variables into scope
            ScopedDeclLL *paramSD = (d->declType->funcType.paramLL == NULL) ? NULL : d->declType->funcType.paramLL->first;
            while(paramsLL != NULL)
            {
                SymEntry *e = _symTableInsertLocal(d->tbl, paramsLL->item->iden.lexeme, paramsLL->item->type->checkType, paramsLL->item->iden);
                paramSD->item->mySymEntry = e;
                
                paramsLL = paramsLL->next;
                paramSD = paramSD->next;
            }

            if(d->kind == A_DECL_FUNC) checkBlock(d->func.block, false); //dont create new scope, ill do it manually
            else checkBlock(d->operFunc.block, false);

            _symTableRestoreLocal(d->tbl, restoreLocal);
            _symTableRestoreConst(d->tbl, restoreConst);
            _symTableRestoreType(d->tbl, restoreType);
            
            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            globalContext.cc.currFuncTypeBeingChecked = prevFuncType;

            currDecl = currDecl->next;
        }
    }

     if(methodBlockDecls != NULL)
    {
        ASTDeclLL *currDecl = methodBlockDecls->first;
        
        while(currDecl != NULL)
        {
            ASTDecl *d = currDecl->item;
            checkMethodBlock(d, false);
            currDecl = currDecl->next;
        }
    }
}