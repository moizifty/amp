#pragma check_stack(off) 

#include <windows.h>
#include "compiler_internal.h"
#include "checker.h"
#include "primitiveTypes.h"
#include "abi.h"

extern GlobalContext globalContext;
extern LexFileBuffer *fileBuffers;

size_t anyLitIdenCounter = 0;
size_t compilerIdenCounter = 0;

DeferStackEntry *newDeferStackEntry(ASTBlock *block, size_t level)
{
    DeferStackEntry *d = malloc(sizeof(DeferStackEntry));
    d->block = block;
    d->level = level;

    return d;
}
DeferStackEntryLL *newDeferStackEntryLL(DeferStackEntry *item)
{
    DeferStackEntryLL *ll = malloc(sizeof(DeferStackEntryLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addDeferStackEntryLL(DeferStackEntryLL **ll, DeferStackEntry *item)
{
    DeferStackEntryLL *n = newDeferStackEntryLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}   

void removeDeferStackEntryLL(DeferStackEntryLL **ll)
{
    if((*ll) == NULL) return;

    //TODO: free DeferStackEntryLL when removing
    
    if((*ll)->prev == NULL)
    {
        (*ll)->first = NULL;
        (*ll) = NULL;

        return;
    }

    (*ll)->prev->next = NULL;

    if((*ll)->first != NULL)
        (*ll)->first->numItems -= 1;
    
    (*ll) = (*ll)->prev;
}

enum DeferStackEmitFlag
{
    EMIT_DEFER_FOR_BLOCK = 1,
    EMIT_DEFER_FOR_RETURN = 2,
    EMIT_DEFER_FOR_CONT_BREAK = 3,
};

void checkGoOverDeferStackAndInsertBlock(ASTStmtLL *stmtToInsertAfterOrBefore, int flag)
{
    bool emitForRet = flag == EMIT_DEFER_FOR_RETURN;
    bool emitForBlock = flag == EMIT_DEFER_FOR_BLOCK;
    bool emitForContBreak = flag == EMIT_DEFER_FOR_CONT_BREAK;

    if(globalContext.cc.deferStack != NULL)
    {
        DeferStackEntryLL *currEntry = globalContext.cc.deferStack;
        ASTStmtLL *prevDeferBlockStmt = NULL;

        bool removeEntry = false;
        while(currEntry != NULL)
        {
            if((emitForBlock) && (currEntry->item->level == globalContext.cc.blockLevel))
            {
                ASTStmt *deferBlockStmt = newASTStmtBlock(currEntry->item->block);
                ASTStmtLL *s = newASTStmtLL(deferBlockStmt);

                if(prevDeferBlockStmt == NULL) insertAfterASTStmtLL(stmtToInsertAfterOrBefore, s);
                else insertAfterASTStmtLL(prevDeferBlockStmt, s);

                prevDeferBlockStmt = s;
                removeDeferStackEntryLL(&(globalContext.cc.deferStack));

                removeEntry = true;
            }
            else if((emitForRet) && (currEntry->item->level <= globalContext.cc.blockLevel))
            {
                ASTStmt *deferBlockStmt = newASTStmtBlock(currEntry->item->block);
                ASTStmtLL *s = newASTStmtLL(deferBlockStmt);

                if(prevDeferBlockStmt == NULL) insertBeforeASTStmtLL(stmtToInsertAfterOrBefore, s);
                else insertAfterASTStmtLL(prevDeferBlockStmt, s);

                prevDeferBlockStmt = s;

                if((currEntry->item->level == globalContext.cc.blockLevel))
                    removeDeferStackEntryLL(&(globalContext.cc.deferStack));

                removeEntry = true;
            }
            else if((emitForContBreak) && ((currEntry->item->level >= globalContext.cc.contBreakBlockLevelID) && currEntry->item->level <= globalContext.cc.blockLevel))
            {
                ASTStmt *deferBlockStmt = newASTStmtBlock(currEntry->item->block);
                ASTStmtLL *s = newASTStmtLL(deferBlockStmt);

                if(prevDeferBlockStmt == NULL) insertBeforeASTStmtLL(stmtToInsertAfterOrBefore, s);
                else insertAfterASTStmtLL(prevDeferBlockStmt, s);

                prevDeferBlockStmt = s;
                if((currEntry->item->level == globalContext.cc.blockLevel))
                    removeDeferStackEntryLL(&(globalContext.cc.deferStack));

                removeEntry = true;
            }

            currEntry = currEntry->prev;
        }
    }
}

CheckerContext newCheckerContext()
{
    return (CheckerContext)
    {
        .currStmtBeingChecked = NULL,
        .currMatchArmBeingChecked = NULL,
        .deferStack = NULL,
        .blockLevel = 0,
        .currScopedExpr = {.isCheckingScopeExpr = false},
        .isCheckingStmtExpr = false,
        .checkingLocalsSymTble = NULL,
        .checkingBreakableBlock = false,
        .checkingContinuableBlock = false,
        .checkingDefer = false,
        .isCheckingUsingStmt = false,
        .currFuncTypeBeingChecked = NULL,
        .currFuncCallTypeBeingChecked = NULL,
        .tupleTypes = NULL,
        .isCheckingAssignStmtLHSTuple = false,
        .contBreakBlockLevelID = 0,
    };
}

void _checkerError(Token tok, bool newLine, char *msg, ...)
{
    va_list args;
    va_start(args, msg);

    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD savedAttributes;

    /* Save current attributes */
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    savedAttributes = consoleInfo.wAttributes;

    fprintf(stderr, "\033[1m\033[4m%s (%lld: %d)\033[0m", tok.pos.filename, tok.pos.linenum, tok.pos.colnum);

    fprintf(stderr, "\033[1m\n        --> Semantic Error:\033[0m ");
    vfprintf(stderr, msg, args);

    if(newLine)
    {
        fprintf(stderr, "\033[0m\n");
    }

    va_end(args);

    globalContext.cc.numErrors += 1;
}
void prettyPrintCheckerSourceError(Token startTok, Token endTok)
{
    LexFileBuffer fileBuffer = fileBuffers[startTok.pos.fileBufferIndex];
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD savedAttributes;

    /* Save current attributes */
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    savedAttributes = consoleInfo.wAttributes;

    const int NUM_LINES = 2;
    
    int currLine = 0;
    int *currCh = startTok.pos.fileBufPos;
    int colnum = 0;


    fprintf(stderr, "        |\n");
    while( (currLine != -NUM_LINES) && (currCh != fileBuffer)) 
    {
        while((*currCh != '\n') && (currCh != fileBuffer)) 
        {
            currCh--;
        }

        if(*currCh == '\n') 
        {
            currLine--;
            if((currLine != -NUM_LINES) && (currCh != fileBuffer)) currCh--;
        }
    }

    if(*currCh == '\n') 
    {
        currLine++;
        currCh++;
    }
    
    int numLinesFromStartTokToEndTok = endTok.pos.linenum - startTok.pos.linenum;

    bool isSingleToken = (startTok.pos.fileBufPos == endTok.pos.fileBufPos);
    size_t endTokLen = strlen(endTok.lexeme);

    for(; (currLine < NUM_LINES + numLinesFromStartTokToEndTok); currLine++)
    {
        printf("%7lld | ", currLine + startTok.pos.linenum);

        if(!isSingleToken)
        {
            while((*currCh != '\n') && (*currCh != EOF))
            {
                if(currCh >= startTok.pos.fileBufPos && currCh <= endTok.pos.fileBufPos + endTokLen) SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY | FOREGROUND_RED);
                else SetConsoleTextAttribute(hConsole, savedAttributes);

                if(currCh >= startTok.pos.fileBufPos && currCh <= endTok.pos.fileBufPos + endTokLen)
                    fprintf(stderr, "\033[4m%c", (char) (*currCh++));
                else
                    fprintf(stderr, "%c", (char) (*currCh++) );
            }
        }
        else
        {
            size_t tokLen = strlen(startTok.lexeme);
             while((*currCh != '\n') && (*currCh != EOF))
            {
                if(currLine == 0) colnum++;

                if(currCh == startTok.pos.fileBufPos) SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY | FOREGROUND_RED);
                else if(currCh >= startTok.pos.fileBufPos + tokLen) SetConsoleTextAttribute(hConsole, savedAttributes);

                if((currCh >= startTok.pos.fileBufPos && currCh < startTok.pos.fileBufPos + tokLen))
                    fprintf(stderr, "\033[4m%c", (char) (*currCh++));
                else
                    fprintf(stderr, "%c", (char) (*currCh++) );
            }
        }

        SetConsoleTextAttribute(hConsole, savedAttributes);
        
        if(*currCh != EOF) currCh++;
        else break;
        putchar('\n');
    }

    putchar('\n');
}
int charLitToInt(Token charLit)
{
    int val = 0;
    char *chPtr = charLit.lexeme + 1; //skip starting qoute mark

    while(*chPtr != '\'')
    {
        if(*chPtr == '\\')
        {
            chPtr++;

            switch(*chPtr)
            {
                case '0': val = 0; break;
                case '\\': val = '\\'; break;
                case '\'': val = '\''; break;
                case '\"': val = '\"'; break;
                case 'n': val = '\n'; break;
                case 'r': val = '\r'; break;
                case 'b': val = '\b'; break;
                case 'a': val = '\a'; break;
                case 'f': val = '\f'; break;
                case 'v': val = '\v'; break;
                case 't': val = '\t'; break;
                default: checkerErrorLn(charLit, "Unexpected escape character '%s'", charLit.lexeme);
            }
        }
        else val = *chPtr;

        chPtr++;
    }

    return val;
}
char *allocNewAnyLitIden(void)
{
    char buf[BUFSIZ] = {0};
    sprintf(buf, "__anyLit%lld", anyLitIdenCounter);
    anyLitIdenCounter++;

    char *s = malloc(strlen(buf) + 1);
    strcpy(s, buf);

    return s;
}
char *allocNewCompilerIden(char *prefix)
{
    char buf[BUFSIZ] = {0};
    char fmt[BUFSIZ] = {0};
    strcpy(fmt, prefix);
    strcat(fmt, "%lld");

    sprintf(buf, fmt, compilerIdenCounter);
    compilerIdenCounter++;

    char *s = malloc(strlen(buf) + 1);
    strcpy(s, buf);

    return s;
}

bool checkIfFuncRetTypeShouldBePassedAsArg(CheckerType *funcRetType)
{
    return isTypeArray(funcRetType);
    // return (isTypeStruct(funcRetType) || isTypeUnion(funcRetType) || isTypeArray(funcRetType) || isTypeTuple(funcRetType));
}

void checkMethodBlock(ASTDecl *decl, bool isIncompleteCheck)
{
    ASTDecl *d = decl;
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;

    globalContext.cc.checkingLocalsSymTble = d->tbl;

    checkType(d->methodBlock.type);

    CheckerType *typeMethodBlockIsFor = d->methodBlock.type->checkType;

    { //loop through all ddecls in method block
        ASTDeclLL *currMethodDecl = d->methodBlock.declLL;
        if(currMethodDecl != NULL)
        {
            currMethodDecl = currMethodDecl->first;
            while(currMethodDecl != NULL)
            {
                if(isIncompleteCheck) checkIncompleteDeclInMethodBlock(d, currMethodDecl->item);
                else checkCompleteDeclInMethodBlock(d, currMethodDecl->item);
                currMethodDecl = currMethodDecl->next;
            }
        }
    }

    globalContext.cc.checkingLocalsSymTble = tempSymTable;
}
void checkIncompleteDeclInMethodBlock(ASTDecl *methodDecl, ASTDecl *d)
{
    CheckerType *typeMethodBlockIsFor = methodDecl->methodBlock.type->checkType;

    int declFlags = checkDeclTags(d, d->tags);
    if(!(declFlags & TYPE_FUNC_THIS_FLAG) && !(declFlags & TYPE_FUNC_THISPTR_FLAG))
    declFlags |= TYPE_FUNC_THIS_NONE_FLAG;

    switch(d->kind)
    {
        case A_DECL_FUNC:
        {
            char *funcDeclName = d->func.iden.lexeme;
            char *typeName = allocCheckerTypeName(typeMethodBlockIsFor);
        
            SymEntry *e = typeMethodTableLookUp(globalContext.typeMethodTable, typeMethodBlockIsFor, funcDeclName);;
            
            if(e != NULL)
            {
                checkerError(d->startTok, "Identifier '%s' is already being used for declaration of type ", funcDeclName);
                printCheckerType(e->type);
                fprintf(stderr, "\n");

                prettyPrintCheckerSourceError(d->func.iden, d->func.iden);
            }
            else
            {
                checkType(d->func.sig->retType);
                d->func.sig->retAsArg = checkIfFuncRetTypeShouldBePassedAsArg(d->func.sig->retType->checkType);
                if(d->func.sig->retAsArg)
                {
                    d->func.sig->retAsArgName = d->startTok;
                    d->func.sig->retAsArgName.type = TOK_IDEN;
                    strcpy(d->func.sig->retAsArgName.lexeme, "__retAsArg");
                }


                size_t numDefParams = 0;

                ASTFuncParamLL *astParamLL = d->func.sig->params;
                ScopedDeclLL *paramsLL = NULL;
                SymEntryLL *restoreTo = d->tbl->localTbl;
                
                SymEntry *thisParamEntry = _symTableInsertLocal(d->tbl, "this", typeMethodBlockIsFor, d->startTok);
                Token thisParamToken = d->startTok;
                thisParamToken.type = TOK_IDEN;
                strcpy(thisParamToken.lexeme, thisParamEntry->name);

                ScopedDecl *thisSD = newScopedDecl(thisParamToken, 
                                                        (declFlags & TYPE_FUNC_THISPTR_FLAG) ?  newCheckerTypePointer(typeMethodBlockIsFor) : typeMethodBlockIsFor, 
                                                        NULL, 0);
                thisSD->abiInfo = classifyCheckerType(thisSD->type);

                paramsLL = newScopedDeclLL(thisSD);


                if(astParamLL != NULL)
                {

                    ASTFuncParamLL *c = astParamLL->first;
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
                            bool hasDefValue = param->defValue != NULL;
                            int flags = 0;

                            if(hasDefValue) flags |= SCOPED_DECL_HAS_DEF_VALUE;
                            
                            if(hasDefValue) numDefParams++;

                            if(param->type->kind != A_TYPE_INFER) 
                            {
                                checkType(param->type);
                                if((param->defValue != NULL) && !checkRHSExprWithTypeAndCast(param->type->checkType, &(param->defValue), true, false))
                                {
                                    checkerError(param->defValue->startTok, "Default value for parameter '%s' cannot be cast or inferred to be the type of the parameter, expected type: ", param->iden.lexeme);
                                    printCheckerType(param->type->checkType);
                                    fprintf(stderr, ", got: ");
                                    printCheckerType(param->defValue->checkType);
                                    fprintf(stderr, "\n");

                                    prettyPrintCheckerSourceError(param->defValue->startTok, param->defValue->endTok);
                                }
                            }
                            else 
                            {
                                checkExpr(param->defValue, false);
                                param->type->checkType = param->defValue->checkType;
                            }

                            if(isTypeVariadic(param->type->checkType))
                            {
                                if(declFlags & TYPE_FUNC_VARIADIC_FLAG)
                                {
                                    checkerErrorLn(param->iden, "Parameter '%s' is declared as being variadic, but there already exists a varaidic parameter in the function", param->iden.lexeme);
                                    prettyPrintCheckerSourceError(param->iden, param->iden);
                                }
                                else declFlags |= TYPE_FUNC_VARIADIC_FLAG;
                            }
                            
                            ScopedDecl *scopedD = newScopedDecl(param->iden, param->type->checkType, param->defValue, flags);
                            scopedD->abiInfo = classifyCheckerType(scopedD->type);

                            if(paramsLL == NULL) paramsLL = newScopedDeclLL(scopedD);
                            else addScopedDeclLL(&paramsLL, scopedD);

                             _symTableInsertLocal(d->tbl, param->iden.lexeme, param->type->checkType, param->iden);
                        }

                        c = c->next;
                    }                                        
                }

                _symTableRestoreLocal(d->tbl, restoreTo);

                bool isForeign = declFlags & TYPE_FOREIGN_FLAG;
                if(!isForeign && (d->func.block == NULL))
                {
                    checkerErrorLn(d->startTok, "function '%s' is not foreign, hence is not allowed to have no body, if it was intended to be foreign, put a [foreign] tag above", d->func.iden.lexeme);
                    prettyPrintCheckerSourceError(d->startTok, d->endTok);
                }
                else if(isForeign && (d->func.block != NULL))
                {
                    checkerErrorLn(d->startTok, "function '%s' is foreign, hence is not allowed to have a body, if it was intended to be not foreign, remove the [foreign] tag above", d->func.iden.lexeme);
                    prettyPrintCheckerSourceError(d->startTok, d->endTok);
                }

                CheckerType *t = newCheckerTypeFunc(paramsLL, d->func.sig->retType->checkType, d->func.sig->retAsArg, declFlags);
                t->funcType.numDefaultParams = numDefParams;
                t->funcType.isTypeMethod = true;
                t->funcType.retABIInfo = classifyCheckerType(t->funcType.ret);
                e = typeMethodTableInsert(globalContext.typeMethodTable, typeMethodBlockIsFor, funcDeclName, t, d->startTok);
                e->type->belongsToFile = d->startTok.pos.filename;
                e->type->namespaceName = d->tbl->belongsToNamespace->namespace;
                e->isGlobalFunc = true;

                d->declType = e->type;
                e->myDecl = d;
            }
        }break;

        case A_DECL_DECLLIST:
        {
            ASTDeclLL *declInDeclList = d->declList.declLL;

            if(declInDeclList != NULL)
            {
                declInDeclList = declInDeclList->first;
                while(declInDeclList != NULL)
                {
                    checkIncompleteDeclInMethodBlock(methodDecl, declInDeclList->item);
                    declInDeclList = declInDeclList->next;
                }
            }
        }break;
        default:
        {
            checkerErrorLn(d->startTok, "Unexpected declaration, method blocks only support functions at the moment!");

            prettyPrintCheckerSourceError(d->startTok, d->endTok);
        };
    }
}
void checkCompleteDeclInMethodBlock(ASTDecl *methodDecl, ASTDecl *d)
{
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;

    CheckerType *typeMethodBlockIsFor = methodDecl->methodBlock.type->checkType;
    CheckerType *prevFuncType = globalContext.cc.currFuncTypeBeingChecked;

    int declFlags = checkDeclTags(d, d->tags);
    if(!(declFlags & TYPE_FUNC_THIS_FLAG) && !(declFlags & TYPE_FUNC_THISPTR_FLAG))
        declFlags |= TYPE_FUNC_THIS_NONE_FLAG;
        
    switch(d->kind)
    {
        case A_DECL_FUNC:
        {
            globalContext.cc.checkingLocalsSymTble = d->tbl;
            globalContext.cc.currFuncTypeBeingChecked = d->declType;

            SymEntryLL *restoreLocal = d->tbl->localTbl;
            SymEntryLL *restoreConst = d->tbl->constTbl;
            SymEntryLL *restoreType = d->tbl->typeTbl;

            bool isOperfunc = (d->kind == A_DECL_OPERATOR_FUNC);

            ASTFuncParamLL *paramsLL = (isOperfunc) ? d->operFunc.sig->params->first : d->func.sig->params;

            if((paramsLL != NULL) && !isOperfunc) paramsLL = paramsLL->first;

            //insert all local variables into scope

            {
                SymEntry *e = _symTableInsertLocal(d->tbl, "this", (declFlags & TYPE_FUNC_THISPTR_FLAG) ? newCheckerTypePointer(typeMethodBlockIsFor) : typeMethodBlockIsFor, d->startTok);
                ScopedDecl *dl = getScopedDeclLLAt(d->declType->funcType.paramLL, 0)->item;

                dl->mySymEntry = e;
            }
            size_t index = 1;
            while(paramsLL != NULL)
            {
                SymEntry *e = _symTableInsertLocal(d->tbl, paramsLL->item->iden.lexeme, paramsLL->item->type->checkType, paramsLL->item->iden);
                
                ScopedDecl *dl = getScopedDeclLLAt(d->declType->funcType.paramLL, index)->item;
                dl->mySymEntry = e;

                index++;
                paramsLL = paramsLL->next;
            }

            if(d->kind == A_DECL_FUNC) checkBlock(d->func.block, false); //dont create new scope, ill do it manually
            else checkBlock(d->operFunc.block, false);

            _symTableRestoreLocal(d->tbl, restoreLocal);
            _symTableRestoreConst(d->tbl, restoreConst);
            _symTableRestoreType(d->tbl, restoreType);
            
            globalContext.cc.checkingLocalsSymTble = tempSymTable;
            globalContext.cc.currFuncTypeBeingChecked = prevFuncType;
        }break;

        case A_DECL_DECLLIST:
        {
            ASTDeclLL *declInDeclList = d->declList.declLL;

            if(declInDeclList != NULL)
            {
                declInDeclList = declInDeclList->first;
                while(declInDeclList != NULL)
                {
                    checkCompleteDeclInMethodBlock(methodDecl, declInDeclList->item);
                    declInDeclList = declInDeclList->next;
                }
            }
        }break;
    }
}

void checkGlobalFuncDecl(ASTDeclLL **decl, bool isIncompleteCheck)
{
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;

    ASTDecl *d = (*decl)->item;

    if(isIncompleteCheck) checkIncompleteFuncDecl(decl);
    else checkCompleteFuncDecl(decl);

    globalContext.cc.checkingLocalsSymTble = tempSymTable;
}
void checkIncompleteFuncDecl(ASTDeclLL **decl)
{
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;

    ASTDecl *d = (*decl)->item;
    int declFlags = checkDeclTags(d, d->tags);

    globalContext.cc.checkingLocalsSymTble = d->tbl;
    switch(d->kind)
    {
        case A_DECL_FUNC:
        {
            char *name = d->func.iden.lexeme;
            SymEntry *e = _symTableLookUp(d->tbl, name, LOOKUP_ALL);
            bool isFuncGeneric = false;

            if(e != NULL)
            {
                checkerError(d->startTok, "Identifier '%s' is already being used for declaration of type ", name);
                printCheckerType(e->type);
                fprintf(stderr, "\n");

                prettyPrintCheckerSourceError(d->func.iden, d->func.iden);
            }
            else
            {
                checkType(d->func.sig->retType);
                d->func.sig->retAsArg = checkIfFuncRetTypeShouldBePassedAsArg(d->func.sig->retType->checkType);
                if(d->func.sig->retAsArg)
                {
                    d->func.sig->retAsArgName = d->startTok;
                    d->func.sig->retAsArgName.type = TOK_IDEN;
                    strcpy(d->func.sig->retAsArgName.lexeme, "__retAsArg");
                }

                size_t numDefParams = 0;

                ASTFuncParamLL *astParamLL = d->func.sig->params;
                ScopedDeclLL *paramsLL = NULL;

                {   //check generic parametrs
                    if(d->genParams != NULL)
                    {
                        ASTGenericTypeLL *currGenericParam = d->genParams->first;
                        while(currGenericParam != NULL)
                        {
                            CheckerType *genParamType = NULL;
                            if(d->genParamsResolvedSymTbl != NULL)
                            {
                                SymEntry *e = _symTableLookUp(d->genParamsResolvedSymTbl, currGenericParam->item->iden.lexeme, LOOKUP_ALL);
                                if(e != NULL) genParamType = e->type;
                                else genParamType = newCheckerTypeGenericType(currGenericParam->item->iden.lexeme);
                            }
                            else genParamType = newCheckerTypeGenericType(currGenericParam->item->iden.lexeme);

                            SymEntry *e = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, currGenericParam->item->iden.lexeme, 
                                                               genParamType, currGenericParam->item->iden);
                            e->isType = true;
                            
                            currGenericParam = currGenericParam->next;
                        }
                        isFuncGeneric = true;
                    }
                }

                if(astParamLL != NULL)
                {
                    SymEntryLL *restoreTo = d->tbl->localTbl;

                    ASTFuncParamLL *c = astParamLL->first;
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
                            bool hasDefValue = param->defValue != NULL;
                            int flags = 0;

                            if(hasDefValue) flags |= SCOPED_DECL_HAS_DEF_VALUE;
                            
                            if(hasDefValue) numDefParams++;

                            if(param->type->kind != A_TYPE_INFER) 
                            {
                                checkType(param->type);
                                if((param->defValue != NULL) && !checkRHSExprWithTypeAndCast(param->type->checkType, &(param->defValue), true, false))
                                {
                                    checkerError(param->defValue->startTok, "Default value for parameter '%s' cannot be cast or inferred to be the type of the parameter, expected type: ", param->iden.lexeme);
                                    printCheckerType(param->type->checkType);
                                    fprintf(stderr, ", got: ");
                                    printCheckerType(param->defValue->checkType);
                                    fprintf(stderr, "\n");

                                    prettyPrintCheckerSourceError(param->defValue->startTok, param->defValue->endTok);
                                }
                            }
                            else 
                            {
                                checkExpr(param->defValue, false);
                                param->type->checkType = param->defValue->checkType;
                            }

                            if(isTypeVariadic(param->type->checkType))
                            {
                                if(declFlags & TYPE_FUNC_VARIADIC_FLAG)
                                {
                                    checkerErrorLn(param->iden, "Parameter '%s' is declared as being variadic, but there already exists a varaidic parameter in the function", param->iden.lexeme);
                                    prettyPrintCheckerSourceError(param->iden, param->iden);
                                }
                                else declFlags |= TYPE_FUNC_VARIADIC_FLAG;
                            }

                            ScopedDecl *scopedD = newScopedDecl(param->iden, param->type->checkType, param->defValue, flags);
                            scopedD->abiInfo = classifyCheckerType(scopedD->type);

                            if(paramsLL == NULL) paramsLL = newScopedDeclLL(scopedD);
                            else addScopedDeclLL(&paramsLL, scopedD);


                            _symTableInsertLocal(d->tbl, param->iden.lexeme, param->type->checkType, param->iden);
                        }

                        c = c->next;
                    }
                    
                    _symTableRestoreLocal(d->tbl, restoreTo);
                }

                bool isForeign = declFlags & TYPE_FOREIGN_FLAG;
                if(!isForeign && (d->func.block == NULL))
                {
                    checkerErrorLn(d->startTok, "function '%s' is not foreign, hence is not allowed to have no body, if it was intended to be foreign, put a [foreign] tag above", d->func.iden.lexeme);
                    prettyPrintCheckerSourceError(d->startTok, d->endTok);
                }
                else if(isForeign && (d->func.block != NULL))
                {
                    checkerErrorLn(d->startTok, "function '%s' is foreign, hence is not allowed to have a body, if it was intended to be not foreign, remove the [foreign] tag above", d->func.iden.lexeme);
                    prettyPrintCheckerSourceError(d->startTok, d->endTok);
                }

                CheckerType *t = newCheckerTypeFunc(paramsLL, d->func.sig->retType->checkType, d->func.sig->retAsArg, declFlags);
                t->funcType.numDefaultParams = numDefParams;
                
                t->funcType.retABIInfo = classifyCheckerType(t->funcType.ret);

                e = _symTableInsertGlobal(d->tbl, name, t, d->startTok);
                e->type->belongsToFile = d->startTok.pos.filename;
                e->type->namespaceName = d->tbl->belongsToNamespace->namespace;
                e->isGlobalFunc = true;
                e->myDecl = d;
                if(isFuncGeneric)
                {
                    e->type->funcType.genericFuncAST = *decl;
                }
                
                d->declType = e->type;

                if(!strcmp(d->tbl->belongsToNamespace->namespace, "global") && !strcmp(e->name, "main")) 
                {
                    globalContext.globalMainFuncType = e->type;
                    int flags = checkIfMainFuncHasCorrectSignature(e->type);

                    if(flags & MAIN_FUNC_INCORRECT_RET_TYPE)
                    {
                        checkerError(d->startTok, "Expected main function to have integer or void return type but instead got: ");
                        printCheckerType(e->type->funcType.ret);

                        fprintf(stderr, "\n");
                        prettyPrintCheckerSourceError(d->func.sig->retType->startTok, d->func.sig->retType->endTok);
                    }
                    else if(flags & MAIN_FUNC_HAS_VOID_RET) globalContext.globalMainFuncTypeFlags.IS_VOID_RET = true;

                    if(flags & MAIN_FUNC_INCORRECT_PARAMS)
                    {
                        checkerErrorLn(d->startTok, "Expected main function to have exactly 1 parameter of type []string or exactly 0 parameters");

                        prettyPrintCheckerSourceError(d->func.sig->startTok, d->func.sig->endTok);
                    }
                    else if(flags & MAIN_FUNC_HAS_VOID_PARAMS) globalContext.globalMainFuncTypeFlags.IS_VOID_PARAMS = true;

                }
                else if(!strcmp(d->tbl->belongsToNamespace->namespace, "prelude") && !strcmp(e->name, "equals"))
                {
                    globalContext.cc.stdEqualsFunction = e;
                }
            }
        }break;

        case A_DECL_OPERATOR_FUNC: //check operato func ret abi info here:
        {
            CheckerType *type = d->declType;

            ScopedDeclLL *currSD = type->funcType.paramLL;
            if(currSD != NULL) currSD = currSD->first;

            while(currSD != NULL)
            {
                currSD->item->abiInfo = classifyCheckerType(currSD->item->type);
                currSD = currSD->next;
            }

            type->funcType.retABIInfo = classifyCheckerType(type->funcType.ret);
        }break;
    }

    globalContext.cc.checkingLocalsSymTble = tempSymTable;
}
void checkCompleteFuncDecl(ASTDeclLL **decl)
{

}
void checkGenericFuncBody(ASTDecl *decl)
{
    SymTable *tempSymTable = globalContext.cc.checkingLocalsSymTble;

    CheckerType *prevFuncType = globalContext.cc.currFuncTypeBeingChecked;

    ASTDecl *d = decl;

    globalContext.cc.checkingLocalsSymTble = d->tbl;
    globalContext.cc.currFuncTypeBeingChecked = d->declType;

    SymEntryLL *restoreLocal = d->tbl->localTbl;
    SymEntryLL *restoreConst = d->tbl->constTbl;
    SymEntryLL *restoreType = d->tbl->typeTbl;

    bool isOperfunc = (d->kind == A_DECL_OPERATOR_FUNC);

    ASTFuncParamLL *paramsLL = (isOperfunc) ? d->operFunc.sig->params->first : d->func.sig->params;

    if((paramsLL != NULL) && !isOperfunc) paramsLL = paramsLL->first;

    //insert generic param types
    {
        if(d->genParamsResolvedSymTbl != NULL)
        {
            SymEntryLL *currEntry = d->genParamsResolvedSymTbl->globalTbl;
            if(currEntry != NULL) currEntry = currEntry->first;

            while(currEntry != NULL)
            {
                _symTableInsertType(d->tbl, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
                currEntry = currEntry->next;
            }
        }
    }

    //insert all local variables into scope
    while(paramsLL != NULL)
    {
        _symTableInsertLocal(d->tbl, paramsLL->item->iden.lexeme, paramsLL->item->type->checkType, paramsLL->item->iden);
        paramsLL = paramsLL->next;
    }

    if(d->kind == A_DECL_FUNC) checkBlock(d->func.block, false); //dont create new scope, ill do it manually
    else checkBlock(d->operFunc.block, false);

    _symTableRestoreLocal(d->tbl, restoreLocal);
    _symTableRestoreConst(d->tbl, restoreConst);
    _symTableRestoreType(d->tbl, restoreType);
    
    globalContext.cc.checkingLocalsSymTble = tempSymTable;
    globalContext.cc.currFuncTypeBeingChecked = prevFuncType;
}

void checkImportDecl(ASTDecl *decl, bool isGlobal)
{
    int flags = checkDeclTags(decl, decl->tags);

    bool hasIden = decl->import.hasIden;
    Token idenName = decl->import.iden;

    ASTNamespaceName *nspath = decl->import.namespace;
    char *nsName = allocGetNamespaceName(nspath);
    
    NSTEntry *nsEntry = namespaceTableLookupEntry(globalContext.namespaceTable, nsName);

    if(nsEntry == NULL)
    {
        char *nsNameWithoutUnderScores = allocNamespaceNameFromASTWithoutUnderscores(nspath);

        checkerErrorLn(decl->startTok, "Namespace '%s' does not exist", nsNameWithoutUnderScores);

        prettyPrintCheckerSourceError(decl->startTok, decl->endTok);
    }
    else
    {   
        SymTable *usedBySymTable = decl->tbl;
        if(usedBySymTable == NULL) usedBySymTable = globalContext.cc.checkingLocalsSymTble;

        nsEntry->isImported = true;
        if(nsEntry->importedBy == NULL) nsEntry->importedBy = newNSTEntryLL(usedBySymTable->belongsToNamespace);
        else addNSTEntryLL(&(nsEntry->importedBy), usedBySymTable->belongsToNamespace);

        CheckerType *nsType = newCheckerTypeNamespace(nsEntry->namespace, nsEntry->namespaceSymTable);

        if(globalContext.cc.isCheckingUsingStmt)
        {
            //check for colossion TODO
            _symTableImportSymtableIntoLocalScope(globalContext.cc.checkingLocalsSymTble, nsType->namespaceType.tble, LOOKUP_ALL);
        }
        else if(flags & TYPE_USE_NAMESPACE_FLAG)
        {
            if(globalContext.cc.checkingLocalsSymTble->continuationTbls == NULL) globalContext.cc.checkingLocalsSymTble->continuationTbls = newSymTableLL(nsEntry->namespaceSymTable);
            else addSymTableLL(&(globalContext.cc.checkingLocalsSymTble->continuationTbls), nsEntry->namespaceSymTable);
        }
        else if(decl->import.namespace->scopedChild != NULL)
        {
            char *lastScope = NULL;
            { //get last scope, eg from A::B::C::D, this gets 'D'
                ASTNamespaceName *right = decl->import.namespace;
                while(right != NULL) 
                {
                    lastScope = right->parent.lexeme;
                    right = right->scopedChild;
                }
            }

            if(!hasIden)
            {
                if(_symTableLookUp(globalContext.cc.checkingLocalsSymTble, lastScope, LOOKUP_ALL) == NULL)
                {
                    _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, lastScope, nsType, decl->startTok);
                }
            }
        }
        else if(!hasIden)
        {
            if(_symTableLookupNamespace(globalContext.cc.checkingLocalsSymTble, nsEntry->namespace) == NULL)
            {
                if(isGlobal)  _symTableInsertNamespace(globalContext.cc.checkingLocalsSymTble, nsEntry->namespace, newCheckerTypeNamespace(nsEntry->namespace, nsEntry->namespaceSymTable), decl->startTok);
                else _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, nsEntry->namespace, nsType, decl->startTok);
            }
        }

        if(hasIden)
        {
            if(_symTableLookUp(globalContext.cc.checkingLocalsSymTble, idenName.lexeme, LOOKUP_ALL) == NULL)
            {
                if(isGlobal) _symTableInsertNamespace(globalContext.cc.checkingLocalsSymTble, decl->import.iden.lexeme, newCheckerTypeNamespace(nsEntry->namespace, nsEntry->namespaceSymTable), decl->startTok);
                else _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, idenName.lexeme, nsType, decl->startTok);
            }
            else
            {
                checkerErrorLn(decl->startTok, "Identifier '%s' is already being used", idenName.lexeme);

                prettyPrintCheckerSourceError(idenName, idenName);
            }
        }
    
    }
}

void checkLocalDecl(ASTDecl *decl)
{
    if(decl == NULL) return;

    switch(decl->kind)
    {
        case A_DECL_VAR: checkLocalDeclVar(decl); break;
        case A_DECL_CONST: checkLocalDeclConst(decl); break;
        case A_DECL_ENUM: checkDeclEnum(decl, false); break;
        case A_DECL_IMPORT:
        {
            checkImportDecl(decl, false);
        }break;
        case A_DECL_DECLLIST:
        {
            if(decl->declList.declLL == NULL) return;

            ASTDeclLL *currDecl = decl->declList.declLL->first;

            while(currDecl != NULL)
            {
                checkLocalDecl(currDecl->item);
                currDecl = currDecl->next;
            }
        }break;
    }
}

int checkDeclTags(ASTDecl *decl, ASTTagLL *tags)
{
    if((decl == NULL) || (tags == NULL)) return 0;

    int flags = 0;

    ASTTagLL *currTag = tags->first;
    while(currTag != NULL)
    {
        char *tagName = currTag->item->name.lexeme;

        if(!strcmp(tagName, "foreign"))
        {
            bool hasAtLeast1Param = ((currTag->item->paramLL != NULL) && currTag->item->paramLL->first->numItems > 0);

            if(!hasAtLeast1Param && (decl->kind == A_DECL_FUNC))
            {
                checkerErrorLn(currTag->item->name, "The tag '%s' requres atleast 1 parameter, which is the name of the library, eg msvcrt", currTag->item->name.lexeme);
                prettyPrintCheckerSourceError(currTag->item->name, currTag->item->name);
            }
            else if(decl->kind == A_DECL_FUNC)
            {
                Token dllNameParam = currTag->item->paramLL->first->item;
                char dllName[BUFSIZ] = {0};
                strncpy(dllName, dllNameParam.lexeme + 1, strlen(dllNameParam.lexeme + 1) - 1);

                if(strlen(dllName) != 0) 
                {

                    HMODULE dllMod = NULL;
                    char dllFullPath[BUFSIZ] = {0};

                    bool foundSystemDll = false;
                    
                    { //try find system dll first, if that fails then find local dl
                        dllMod = LoadLibraryA(dllName);

                        if(dllMod != NULL) 
                        {
                            foundSystemDll = true;
                            strcpy(dllFullPath, dllName);
                        }
                    }

                    if(!foundSystemDll)
                    { //get dlls full path from current source files directory
                        char *sourceFilesDir = allocGetFileFolder(dllNameParam.pos.filename);
                        sprintf(dllFullPath, "%s\\%s", sourceFilesDir, dllName);

                        dllMod = LoadLibraryA(dllFullPath);
                    }

                    if(dllMod == NULL)
                    {
                        checkerErrorLn(currTag->item->name, "'%s.dll' was not found", dllName);
                        prettyPrintCheckerSourceError(dllNameParam, dllNameParam);
                    }
                    else
                    {
                        FARPROC procAddr = GetProcAddress(dllMod, decl->func.iden.lexeme);
                        if(procAddr == NULL)
                        {
                            checkerErrorLn(decl->startTok, "symbol '%s' was not found in '%s.dll'", decl->func.iden.lexeme, dllName);
                            prettyPrintCheckerSourceError(decl->func.iden, decl->func.iden);
                        }
                        else
                        {
                            char *libName = malloc(strlen(dllFullPath) + 1 + 4 + 4); // +4 for .lib, +4 for extra speechmarks
                            
                            if(!foundSystemDll)
                                sprintf(libName, "\"%s.lib\"", dllFullPath);
                            else sprintf(libName, "%s.lib", dllFullPath);

                            bool alreadyExists = false;

                            for(size_t i = 0; i < decl->tbl->belongsToNamespace->numLinkerLibs; i++)
                            {
                                if(!strcmp(libName, decl->tbl->belongsToNamespace->linkerLibs[i]))
                                {
                                    alreadyExists = true;
                                    break;
                                }
                            }
                            
                            if(!alreadyExists) addLinkerLibToImport(decl->tbl->belongsToNamespace, libName);
                        }
                    }

                    FreeLibrary(dllMod);
                }
            }

            flags |= TYPE_FOREIGN_FLAG;
        }
        else if(!strcmp(tagName, "useNamespace")) flags |= TYPE_USE_NAMESPACE_FLAG;
        else if(!strcmp(tagName, "useReturn")) flags |= TYPE_FUNC_MUST_USE_RETURN;
        else if(!strcmp(tagName, "distinct")) flags |= TYPE_DISTINCT_FLAG;
        else if(!strcmp(tagName, "flag")) flags |= TYPE_ENUM_FLAGS;
        else if(!strcmp(tagName, "strict")) flags |= TYPE_ENUM_STRICT_FLAG;
        else if(!strcmp(tagName, "this")) flags |= TYPE_FUNC_THIS_FLAG;
        else if(!strcmp(tagName, "thisPtr")) flags |= TYPE_FUNC_THISPTR_FLAG;
        else if(!strcmp(tagName, "fmt")) flags |= TYPE_TOSTRING_FUNC_FLAG;

        currTag = currTag->next;
    }

    //TODO check if flags or valid for each decl, eg, local tag can only be applied imports

    return flags;
}
int checkScopedDeclTags(ASTDecl *decl)
{
    if((decl == NULL) || (decl->tags == NULL)) return 0;

    int flags = 0;

    ASTTagLL *currTag = decl->tags->first;
    while(currTag != NULL)
    {
        char *tagName = currTag->item->name.lexeme;

        if(!strcmp(tagName, "required")) flags |= SCOPED_DECL_VALUE_REQUIRED;
        else if(!strcmp(tagName, "constant")) flags |= SCOPED_DECL_CONST;

        currTag = currTag->next;
    }

    //TODO check if flags or valid for each decl, eg, local tag can only be applied imports

    return flags;
}

void checkLocalDeclVar(ASTDecl *decl)
{
    SymTable *st = globalContext.cc.checkingLocalsSymTble;

    if(decl->var.idenExpr->kind == A_EXPR_IDEN)
    {
        SymEntry *e = _symTableLookUp(st, decl->var.idenExpr->iden.lexeme, LOOKUP_ALL);

        if(e != NULL)
        {
            checkerError(decl->startTok, "Identifier '%s' is already used for declaration of type: ", decl->var.idenExpr->iden.lexeme);
            printCheckerType(e->type);
            fprintf(stderr, "\n");

            prettyPrintCheckerSourceError(decl->startTok, decl->startTok);
        }
        else 
        {        
            if(decl->var.type->kind != A_TYPE_INFER) 
            {
                checkType(decl->var.type);
                //todo: CHECK type ==  initializer type
                // if((decl->var.initial != NULL) && !canImplicitCastThenInsertCast(decl->var.initial->checkType, decl->var.type->checkType, &decl->var.initial))
                // {
                //     checkerError(decl->startTok, "Expected variable declaration type to match initial value but type: ");
                //     printCheckerType(decl->var.type->checkType);
                //     fprintf(stderr, " and initial value: ");
                //     printCheckerType(decl->var.initial->checkType);
                //     fprintf(stderr, "\n");

                //     prettyPrintCheckerSourceError(decl->var.initial->startTok, decl->var.initial->endTok);
                // }

                if((decl->var.initial != NULL) && !checkRHSExprWithTypeAndCast(decl->var.type->checkType, &(decl->var.initial), true, false))
                {
                    checkerError(decl->startTok, "Expected variable declaration type to match initial value but type: ");
                    printCheckerType(decl->var.type->checkType);
                    fprintf(stderr, " and initial value: ");
                    printCheckerType(decl->var.initial->checkType);
                    fprintf(stderr, "\n");

                    prettyPrintCheckerSourceError(decl->var.initial->startTok, decl->var.initial->endTok);
                }
            }
            else
            {
                checkExpr(decl->var.initial, false);
                decl->var.type->checkType = decl->var.initial->checkType;
            }

            e = _symTableInsertLocal(st, decl->var.idenExpr->iden.lexeme, decl->var.type->checkType, decl->startTok);

            if(e->type != NULL) e->type->flags |= checkDeclTags(decl, decl->tags);

            e->myDecl = decl;
        }
    }
    else checkDeclLHSTuple(decl->var.idenExpr, decl->var.type, &(decl->var.initial));
}
void checkLocalDeclConst(ASTDecl *decl)
{
    SymTable *st = globalContext.cc.checkingLocalsSymTble;

    SymEntry *e = _symTableLookUp(st, decl->constDecl.iden.lexeme, LOOKUP_ALL);

    if(e != NULL)
    {
        checkerError(decl->startTok, "Identifier '%s' is already used for declaration of type: ", decl->constDecl.iden.lexeme);
        printCheckerType(e->type);
        fprintf(stderr, "\n");

        prettyPrintCheckerSourceError(decl->startTok, decl->startTok);
    }
    else 
    {        
        if(decl->constDecl.type->kind != A_TYPE_INFER) 
        {
            checkType(decl->constDecl.type);

            if((decl->constDecl.initial != NULL) && !checkRHSExprWithTypeAndCast(decl->constDecl.type->checkType, &(decl->constDecl.initial), true, false))
            {
                checkerError(decl->startTok, "Expected constant declaration type to match initial value but type: ");
                printCheckerType(decl->constDecl.type->checkType);
                fprintf(stderr, " and initial value: ");
                printCheckerType(decl->constDecl.initial->checkType);
                fprintf(stderr, "\n");

                prettyPrintCheckerSourceError(decl->constDecl.initial->startTok, decl->constDecl.initial->endTok);
            }
        }
        else
        {
            checkExpr(decl->constDecl.initial, false);
            decl->constDecl.type->checkType = decl->constDecl.initial->checkType;
        }

        if(decl->constDecl.initial->compTimeVal.kind == A_EXPR_COMP_TIME_RUNTIME)
        {
            checkerErrorLn(decl->startTok, "Expected constant declaration to have constant initial value");

            prettyPrintCheckerSourceError(decl->constDecl.initial->startTok, decl->constDecl.initial->endTok);
        }
            
        e = _symTableInsertLocal(st, decl->constDecl.iden.lexeme, decl->constDecl.type->checkType, decl->startTok);
        e->isActualConst = true;
        e->constVal = decl->constDecl.initial->compTimeVal;

        if(e->type != NULL) e->type->flags |= checkDeclTags(decl, decl->tags);
    }
}

void checkDeclEnum(ASTDecl *decl, bool isGlobalDecl)
{
    char *name = decl->enumDecl.iden.lexeme;
    SymTable *st = (isGlobalDecl) ? decl->tbl : globalContext.cc.checkingLocalsSymTble;

    SymEntry *e = _symTableLookupType(st, name);

    if(e != NULL)
    {
        checkerError(decl->startTok, "Identifier '%s' is already being used for declaration of type ", name);
        printCheckerType(e->type);
        fprintf(stderr, "\n");
    }
    else 
    {
        EnumMembLL *membLL = NULL;
        int flags = checkDeclTags(decl, decl->tags);

        CheckerType *t = newCheckerTypeEnum(membLL, name, flags);

        ASTEnumMembLL *astEnumMembLL = decl->enumDecl.membs->first;

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
                if(membLL == NULL) membLL = newEnumMembLL(memb);
                else addEnumMembLL(&membLL, memb);

                SymEntry *e = _symTableInsertLocal(st, membName, t, astEnumMembLL->item->id);
                e->isUnscopedEnumMemb = true;
            }

            astEnumMembLL = astEnumMembLL->next;
        }

        t->enumType.membLL = membLL;
        _symTableRestoreLocal(st, restorePoint);

        e = _symTableInsertType(st, name, t, decl->startTok);
        e->isGlobal = isGlobalDecl;
        e->type->namespaceName = st->belongsToNamespace->namespace;
        e->type->belongsToFile = decl->startTok.pos.filename;
        e->posInFile = decl->startTok;
        decl->declType = e->type;
    }
}

void checkBlock(ASTBlock *block, bool createNewScope)
{
    if(block == NULL) return;

    globalContext.cc.blockLevel += 1;

    ASTStmtLL *prevStmtBeingChecked = globalContext.cc.currStmtBeingChecked;

    SymEntryLL *restoreLocal = NULL;
    SymEntryLL *restoreConst = NULL;
    SymEntryLL *restoreType = NULL;

    if(createNewScope)
    {
        checkCreateNewScope(globalContext.cc.checkingLocalsSymTble, &restoreLocal, &restoreConst, &restoreType);
    }

    bool seenReturnStatement = false;
    bool seenContStatement = false;
    bool seenBreakStatement = false;
    if(block->stmts != NULL)
    {
        ASTStmtLL *currStmt = block->stmts->first;

        while(currStmt != NULL)
        {
            globalContext.cc.currStmtBeingChecked = currStmt;

            if(seenReturnStatement || seenContStatement || seenBreakStatement)
            {
                checkerErrorLn(currStmt->item->startTok, "Statement appears after terminating statement and will never be executed");

                prettyPrintCheckerSourceError(currStmt->item->startTok, currStmt->item->endTok);
            }   

            checkStmt(currStmt->item);
            if(currStmt->item->kind == A_STMT_RET) seenReturnStatement = true;
            if(currStmt->item->kind == A_STMT_CONTINUE) seenContStatement = true;
            if(currStmt->item->kind == A_STMT_BREAK) seenBreakStatement = true;

            globalContext.cc.currStmtBeingChecked = prevStmtBeingChecked;
            currStmt = currStmt->next;
        }
    }

    { //go over defer stack
        if(!seenReturnStatement && !seenContStatement && !seenBreakStatement)
            checkGoOverDeferStackAndInsertBlock(block->stmts, EMIT_DEFER_FOR_BLOCK);
    }

    if(createNewScope)
    {
        checkEndScope(globalContext.cc.checkingLocalsSymTble, &restoreLocal, &restoreConst, &restoreType);
    }

    globalContext.cc.blockLevel -= 1;
}
void checkCreateNewScope(SymTable *table, SymEntryLL **restoreLocal, SymEntryLL **restoreConst, SymEntryLL **restoreType)
{
    *restoreLocal = table->localTbl;
    *restoreConst = table->constTbl;
    *restoreType = table->typeTbl;
}
void checkEndScope(SymTable *table, SymEntryLL **restoreLocal, SymEntryLL **restoreConst, SymEntryLL **restoreType)
{
    _symTableRestoreLocal(table, *restoreLocal);
    _symTableRestoreConst(table, *restoreConst);
    _symTableRestoreType(table, *restoreType);
}

void checkStmt(ASTStmt *stmt)
{
    if(stmt == NULL) return;
    
    switch(stmt->kind)
    {
        case A_STMT_DECL: checkLocalDecl(stmt->decl.decl); break;
        case A_STMT_IF: checkIfStmt(stmt); break;
        case A_STMT_WHILE: checkWhileStmt(stmt); break;
        case A_STMT_MATCH: checkMatchStmt(stmt); break;
        case A_STMT_FOR: checkForStmt(stmt); break;
        case A_STMT_RET: checkRetStmt(stmt); break;
        case A_STMT_BREAK: checkBreakStmt(stmt); break;
        case A_STMT_CONTINUE: checkContinueStmt(stmt); break;
        case A_STMT_ASSIGN: checkAssignStmt(stmt); break;
        case A_STMT_DEFER: checkDeferStmt(stmt); break;
        case A_STMT_USING: checkUsingStmt(stmt); break;
        case A_STMT_EXPR: 
        {
            bool temp = globalContext.cc.isCheckingStmtExpr;
            globalContext.cc.isCheckingStmtExpr = true;
            
            checkExpr(stmt->expr.expr, false);

            globalContext.cc.isCheckingStmtExpr = temp;
        }break;
    }
}
void checkIfStmt(ASTStmt *ifStmt)
{
    if(ifStmt == NULL) return;

    if(!checkRHSExprWithTypeAndCast(boolType, &(ifStmt->ifStmt.cond), true, false))
    {
        checkerError(ifStmt->ifStmt.cond->startTok, "Expected expression with boolean type inside if condition but instead got type: ");
        printCheckerType(ifStmt->ifStmt.cond->checkType);
        fprintf(stderr, "\n");

        prettyPrintCheckerSourceError(ifStmt->ifStmt.cond->startTok, ifStmt->ifStmt.cond->endTok);
    }

   checkBlock(ifStmt->ifStmt.block, true);
   checkBlock(ifStmt->ifStmt.elseBlock, true);
}
void checkWhileStmt(ASTStmt *stmt)
{
    if(stmt == NULL) return;
    
    if(!checkRHSExprWithTypeAndCast(boolType, &(stmt->whileStmt.expr), true, false))
    {
        checkerError(stmt->whileStmt.expr->startTok, "Expected expression with boolean type inside while condition but instead got type: ");
        printCheckerType(stmt->whileStmt.expr->checkType);
        fprintf(stderr, "\n");

        prettyPrintCheckerSourceError(stmt->whileStmt.expr->startTok, stmt->whileStmt.expr->endTok);
    }

   bool prevCheckingBreakable = globalContext.cc.checkingBreakableBlock;
   bool prevCheckingCont = globalContext.cc.checkingContinuableBlock;

   globalContext.cc.checkingBreakableBlock = true;
   globalContext.cc.checkingContinuableBlock = true;

   size_t tempBlockId = globalContext.cc.contBreakBlockLevelID;
   globalContext.cc.contBreakBlockLevelID = globalContext.cc.blockLevel + 1;

   checkBlock(stmt->whileStmt.block, true);

   globalContext.cc.contBreakBlockLevelID = tempBlockId;
   globalContext.cc.checkingBreakableBlock = prevCheckingBreakable;
   globalContext.cc.checkingContinuableBlock = prevCheckingCont;
}
void checkMatchStmt(ASTStmt *stmt)
{
    if(stmt == NULL) return;

    ASTExprLL *matchExprLL = stmt->match.matchExprLL->first;
    ASTMatchArmLL *matchArmLL = stmt->match.matchArmLL;

    size_t numMatchExprs = (matchExprLL == NULL) ? 0 : matchExprLL->first->numItems;

    CheckerType **matchExprTypeList = malloc(sizeof(CheckerType *) * numMatchExprs);    
    ASTExpr **matchExprList = malloc(sizeof(ASTExpr *) * numMatchExprs);
    char **matchExprCompilerIdens = malloc(sizeof(char *) * numMatchExprs);
    size_t matchExprTypeListCounter = 0;
    
    SymEntryLL *matchRestoreLocal = NULL;
    SymEntryLL *matchRestoreConst = NULL;
    SymEntryLL *matchRestoreType = NULL;

    checkCreateNewScope(globalContext.cc.checkingLocalsSymTble,  &matchRestoreLocal, &matchRestoreConst, &matchRestoreType);
    
    stmt->match.matchExprCompilerIdenSymEntry = malloc(sizeof(SymEntry *) * numMatchExprs);
    while(matchExprLL != NULL)
    {
        checkExpr(matchExprLL->item, false);
        
        char buf[MAX_IDEN_LEN] = {0};
        sprintf(buf, "_m%lld_", matchExprTypeListCounter);
        
        char buf2[MAX_IDEN_LEN];
        sprintf(buf2, "m%lld", matchExprTypeListCounter);

        char *matchExprIden = allocNewCompilerIden(buf);

        SymEntry *e = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, buf2, matchExprLL->item->checkType, matchExprLL->item->startTok);
        stmt->match.matchExprCompilerIdenSymEntry[matchExprTypeListCounter] = e;

        matchExprTypeList[matchExprTypeListCounter] = matchExprLL->item->checkType;
        matchExprList[matchExprTypeListCounter] = matchExprLL->item;
        matchExprCompilerIdens[matchExprTypeListCounter] = matchExprIden;
        
        matchExprTypeListCounter++;
        matchExprLL = matchExprLL->next;
    }
    matchExprLL = stmt->match.matchExprLL->first;

    if(matchArmLL != NULL)
    {
        matchArmLL = matchArmLL->first;
        while(matchArmLL != NULL)
        {
            //manually create new scope since the arms might have tagged union auto cast expresions
            SymEntryLL *restoreLocal = NULL;
            SymEntryLL *restoreConst = NULL;
            SymEntryLL *restoreType = NULL;

            checkCreateNewScope(globalContext.cc.checkingLocalsSymTble,  &restoreLocal, &restoreConst, &restoreType);


            ASTExprLL **armExprs = matchArmLL->item->armExprLLs;
            
            ASTMatchArm *preCurrMatchArmBeingChecked = globalContext.cc.currMatchArmBeingChecked;
            globalContext.cc.currMatchArmBeingChecked = matchArmLL->item;

            //if not else match arm
            if(armExprs != NULL)
            {
                for(size_t i = 0; i < matchArmLL->item->armExprLLsCount; i++)
                {
                    ASTExprLL *exprLL = armExprs[i];

                    if(i >= numMatchExprs)
                    {
                        checkerErrorLn(exprLL->item->startTok, "Got to many expression lists in match arm, compared with how many match expressions match recieved, got '%lld', expected '%lld' in each arm", 
                                       matchArmLL->item->armExprLLsCount, numMatchExprs);

                        prettyPrintCheckerSourceError(exprLL->item->startTok, matchArmLL->item->endTok);
                        break;
                    }
                    //if `_` discard is used
                    if(isExprDiscardIdentifier(exprLL->item)) continue;
                    else
                    {
                        exprLL = exprLL->first;
                        while(exprLL != NULL)
                        {
                            bool success = false;
                            success = checkRHSExprWithTypeAndCast(matchExprTypeList[i], &exprLL->item, true,false);

                            if(!success)
                            {
                                if(isTypeTaggedUnion(matchExprTypeList[i]))
                                {
                                    CheckerType *taggedType = matchExprTypeList[i];
                                    taggedType = (isTypeAliased(taggedType)) ? getAliasedTypeBase(taggedType) : taggedType;

                                    CheckerType *taggedEnumType = taggedType->taggedUnionType.tagEnumType;

                                    if(!checkRHSExprWithTypeAndCast(taggedEnumType, &(exprLL->item), true, false))
                                    {
                                        checkerError(exprLL->item->startTok, "Expression in match arm for tagged union type: ");
                                        printCheckerType(matchExprTypeList[i]);
                                        fprintf(stderr, " should either have same type or have the type of the tagged union enum type, which is: ");
                                        printCheckerType(taggedEnumType);
                                        fprintf(stderr, " but instead got type: ");
                                        printCheckerType(exprLL->item->checkType);
                                        fprintf(stderr, "\n");

                                        prettyPrintCheckerSourceError(exprLL->item->startTok, exprLL->item->endTok);
                                    }
                                    else
                                    {
                                        if((exprLL->item->kind == A_EXPR_FUNC_CALL) && (matchArmLL->item->taggedUnionAutoCasts != NULL))
                                        {
                                            ASTMatchArm *arm = matchArmLL->item;
                                            ASTStmt *s = arm->taggedUnionAutoCasts->item;

                                            CheckerType *enumType = exprLL->item->checkType;
                                            
                                            Token exprIden = matchExprList[i]->startTok;
                                            exprIden.type = TOK_IDEN;
                                            strcpy(exprIden.lexeme, matchExprCompilerIdens[i]);

                                            s->decl.decl->var.type->checkType = enumType->enumType.taggedUnionMemberAccess.unionMember->type;
                                            s->isTaggedUnionLetDecl = true;
                                            s->ifTaggedUnionType = enumType->enumType.taggedUnionMemberAccess.taggedUnionType;
                                            s->belongsToMatchExprIndex = i;
                                        }
                                        success = true;
                                    }
                                }
                            }

                            if(!success)
                            {
                                checkerError(exprLL->item->startTok, "Expression in match arm does not match the type of the expression that is required, here is the type required: ");
                                printCheckerType(matchExprTypeList[i]);
                                fprintf(stderr, " but got type: ");
                                printCheckerType(exprLL->item->checkType);
                                fprintf(stderr, "\n");

                                prettyPrintCheckerSourceError(exprLL->item->startTok, exprLL->item->endTok);

                            }
                            exprLL = exprLL->next;
                        }
                    }


                }
            }
            else if(matchArmLL->next != NULL) //if else arm is not last then
            {
                checkerErrorLn(matchArmLL->item->startTok, "'Else' arm should be the last arm in a match statement!");

                prettyPrintCheckerSourceError(matchArmLL->item->startTok, matchArmLL->item->endTok);
            }

            checkBlock(matchArmLL->item->block, false);

            checkEndScope(globalContext.cc.checkingLocalsSymTble,  &restoreLocal, &restoreConst, &restoreType);

            globalContext.cc.currMatchArmBeingChecked = preCurrMatchArmBeingChecked;

            matchArmLL = matchArmLL->next;
        }
    }

    stmt->match.matchExprList = matchExprList;
    stmt->match.matchExprCompilerIdens = matchExprCompilerIdens;

    checkEndScope(globalContext.cc.checkingLocalsSymTble,  &matchRestoreLocal, &matchRestoreConst, &matchRestoreType);
    free(matchExprTypeList);
}
void checkForStmt(ASTStmt *stmt)
{
    ASTExpr *l = stmt->forStmt.l;
    ASTExpr *r = stmt->forStmt.r;
    ASTBlock *block = stmt->forStmt.block;

    bool validL = true;
    bool validR = true;

    checkExpr(r, false);

    bool isRHSArray = isTypeArray(r->checkType);
    bool isRHSRange = false;
    if(!isRHSArray) isRHSRange = r->checkType == rangeType;

    if(!isRHSArray && !isRHSRange)
    {
        checkerError(r->startTok, "Cannot use for statement with type: ");
        printCheckerType(r->checkType);
        fprintf(stderr, "\n");
        prettyPrintCheckerSourceError(r->startTok, r->endTok);

        validR = false;
    }

    if(l->kind != A_EXPR_IDEN)
    {
        checkerErrorLn(l->startTok, "Expected identifier expression before 'in' keyword in for statement");

        prettyPrintCheckerSourceError(l->startTok, l->endTok);
        validL= false;
    }

    SymEntryLL *restoreLocal = NULL;
    SymEntryLL *restoreConst = NULL;
    SymEntryLL *restoreType = NULL;

    checkCreateNewScope(globalContext.cc.checkingLocalsSymTble, &restoreLocal, &restoreConst, &restoreType);

    if(validL && validR)
    {
        CheckerType *forCounterType = NULL;

        if(isRHSArray)
        {
            CheckerType *rArrType = (isTypeAliased(r->checkType)) ? getAliasedTypeBase(r->checkType) : r->checkType;
            forCounterType = rArrType->arrayType.base;
        }
        else if (isRHSRange) forCounterType = i64Type;

        SymEntry *e = _symTableLookUp(globalContext.cc.checkingLocalsSymTble, l->iden.lexeme, LOOKUP_ALL);

        if(e != NULL)
        {
            checkerErrorLn(l->startTok, "Identifier '%s' is already being used. ", l->iden.lexeme);
            prettyPrintCheckerSourceError(l->startTok, l->endTok);
        }
        else
        {
            e = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, l->iden.lexeme, forCounterType, l->startTok);
            e->isConst = true;
            stmt->forStmt.lExprSymEntry = e;

            SymEntry *indexVar = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, "index", i64Type, l->startTok);
            indexVar->isConst = true;

            stmt->forStmt.indexSymEntry = indexVar;
        }
    }


    bool prevCheckingBreakable = globalContext.cc.checkingBreakableBlock;
    bool prevCheckingCont = globalContext.cc.checkingContinuableBlock;

    globalContext.cc.checkingBreakableBlock = true;
    globalContext.cc.checkingContinuableBlock = true;

    size_t tempBlockId = globalContext.cc.contBreakBlockLevelID;
    globalContext.cc.contBreakBlockLevelID = globalContext.cc.blockLevel + 1;

    //manually create scope
    checkBlock(block, false);

    globalContext.cc.contBreakBlockLevelID = tempBlockId;
    globalContext.cc.checkingBreakableBlock = prevCheckingBreakable;
    globalContext.cc.checkingContinuableBlock = prevCheckingCont;

    checkEndScope(globalContext.cc.checkingLocalsSymTble, &restoreLocal, &restoreConst, &restoreType);
}
void checkRetStmt(ASTStmt *stmt)
{
    if(stmt == NULL) return;

    if(globalContext.cc.checkingDefer)
    {
        checkerErrorLn(stmt->startTok, "Cannot have return statement in defer block, since an infinite loop will inccur, with the compiler inserting the statements in the defer block before each return");

        prettyPrintCheckerSourceError(stmt->startTok, stmt->endTok);
    }
    else checkGoOverDeferStackAndInsertBlock(globalContext.cc.currStmtBeingChecked, EMIT_DEFER_FOR_RETURN);

    if((stmt->retStmt.expr == NULL))
    {
        if(!isTypeVoid(globalContext.cc.currFuncTypeBeingChecked->funcType.ret))
        {
            checkerError(stmt->startTok, "Expected a expression of type: ");
            printCheckerType(globalContext.cc.currFuncTypeBeingChecked->funcType.ret);
            fprintf(stderr, " for return expression\n");

            prettyPrintCheckerSourceError(stmt->startTok, stmt->endTok);
        }
    }
    else if(!checkRHSExprWithTypeAndCast(globalContext.cc.currFuncTypeBeingChecked->funcType.ret, &(stmt->retStmt.expr), true, false))
    {
        checkerError(stmt->retStmt.expr->startTok, "Expression type can not be cast or inferred to be function return type ");
        fprintf(stderr, "function return type is: ");
        printCheckerType(globalContext.cc.currFuncTypeBeingChecked->funcType.ret);
        fprintf(stderr, " , return expression type is: ");
        printCheckerType(stmt->retStmt.expr->checkType);
        fprintf(stderr, "\n"); 

        prettyPrintCheckerSourceError(stmt->retStmt.expr->startTok, stmt->retStmt.expr->endTok);
    }
}
void checkBreakStmt(ASTStmt *stmt)
{
    if(!globalContext.cc.checkingBreakableBlock)
    {
        checkerErrorLn(stmt->startTok, "Break statement used in invalid block");

        prettyPrintCheckerSourceError(stmt->startTok, stmt->endTok);
    }

    if(globalContext.cc.checkingDefer)
    {
        checkerErrorLn(stmt->startTok, "Cannot have break statement in defer block, since an infinite loop will inccur, with the compiler inserting the statements in the defer block before each break");

        prettyPrintCheckerSourceError(stmt->startTok, stmt->endTok);
    }
    else checkGoOverDeferStackAndInsertBlock(globalContext.cc.currStmtBeingChecked, EMIT_DEFER_FOR_CONT_BREAK);
}
void checkContinueStmt(ASTStmt *stmt)
{
    if(!globalContext.cc.checkingContinuableBlock)
    {
        checkerErrorLn(stmt->startTok, "Continue statement used in invalid block");

        prettyPrintCheckerSourceError(stmt->startTok, stmt->endTok);
    }

    if(globalContext.cc.checkingDefer)
    {
        checkerErrorLn(stmt->startTok, "Cannot have continue statement in defer block, since an infinite loop will inccur, with the compiler inserting the statements in the defer block before each continue");

        prettyPrintCheckerSourceError(stmt->startTok, stmt->endTok);
    }
    else checkGoOverDeferStackAndInsertBlock(globalContext.cc.currStmtBeingChecked, EMIT_DEFER_FOR_CONT_BREAK);
}
void checkAssignStmt(ASTStmt *stmt)
{
    if(stmt == NULL) return;
    
    if(stmt->assign.l->kind == A_EXPR_TUPLE_LITERAL)
    {
        checkAssignLHSTuple(stmt->assign.l, &(stmt->assign.r));
    }
    else 
    {
        checkExpr(stmt->assign.l, false);

        if(stmt->assign.l->compTimeVal.isL_or_RValue != EXPR_L_VALUE)
        {
            checkerErrorLn(stmt->startTok, "Expected an L-VALUE on the left of the assignment operator");
            prettyPrintCheckerSourceError(stmt->assign.l->startTok, stmt->assign.l->endTok);
        }
        else 
        {
            if(!checkRHSExprWithTypeAndCast(stmt->assign.l->checkType, &stmt->assign.r, true, false))
            {
                checkerError(stmt->assign.r->startTok, "The type of RHS of assignment statement, does not match the required type, got type ");
                printCheckerType(stmt->assign.r->checkType);

                fprintf(stderr, " but wanted type ");
                printCheckerType(stmt->assign.l->checkType);

                fprintf(stderr, "\n");
                prettyPrintCheckerSourceError(stmt->assign.r->startTok, stmt->assign.r->endTok);
                
            }

            if((stmt->assign.r->compTimeVal.isL_or_RValue != EXPR_L_VALUE) && (stmt->assign.r->compTimeVal.isL_or_RValue != EXPR_R_VALUE))
            {
                checkerErrorLn(stmt->startTok, "Expected an L-VALUE or R-VALUE on the right of the assignment operator");
                prettyPrintCheckerSourceError(stmt->assign.r->startTok, stmt->assign.r->endTok);
            }
        }
    }
    
}
void checkDeferStmt(ASTStmt *stmt)
{
    bool prevCheckingDefer = globalContext.cc.checkingDefer;
    globalContext.cc.checkingDefer = true;
    
    DeferStackEntry *deferStackEntry = newDeferStackEntry(stmt->deferStmt.block, globalContext.cc.blockLevel);

    if(globalContext.cc.deferStack == NULL) 
        globalContext.cc.deferStack = newDeferStackEntryLL(deferStackEntry);
    else addDeferStackEntryLL(&(globalContext.cc.deferStack), deferStackEntry);

    checkBlock(stmt->deferStmt.block, true);

    globalContext.cc.checkingDefer = prevCheckingDefer;
}
void checkUsingStmt(ASTStmt *stmt)
{
    if(stmt == NULL) return;
    
    globalContext.cc.isCheckingUsingStmt = true;

    if(stmt->usingStmt.kind == A_STMT_USING_IMPORT)
    {
        checkLocalDecl(stmt->usingStmt.importDecl);
    }
    else
    {
        ASTExpr *e = stmt->usingStmt.expr;
        checkExpr(e, false);

        if(isTypeNamespace(e->checkType))
        {
            CheckerType *namespaceType = isTypeAliased(e->checkType) ? getAliasedTypeBase(e->checkType) : e->checkType;

            //todo check for symbol collisions
            _symTableImportSymtableIntoLocalScope(globalContext.cc.checkingLocalsSymTble, namespaceType->namespaceType.tble, LOOKUP_ALL);
        }
        else if(isTypeEnum(e->checkType))
        {
            CheckerType *enumType = isTypeAliased(e->checkType) ? getAliasedTypeBase(e->checkType) : e->checkType;

            {   //add all enum membs

                EnumMembLL *enumMembs = enumType->enumType.membLL->first;
                while(enumMembs != NULL)
                {
                    SymEntry *entry = _symTableLookUp(globalContext.cc.checkingLocalsSymTble, enumMembs->item->name, LOOKUP_ALL);
                    if(entry != NULL)
                    {
                        checkerErrorLn(stmt->startTok, "Cannot import enum member '%s' into current scope since the identifier already exists", entry->name);

                        prettyPrintCheckerSourceError(stmt->startTok, stmt->endTok);
                    }
                    else
                    {
                        SymEntry *e = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, enumMembs->item->name, enumType, stmt->startTok);
                        e->isUnscopedEnumMemb = true;
                    }

                    enumMembs = enumMembs->next;
                }
                
            }
        }
    }
    globalContext.cc.isCheckingUsingStmt = false;
}

void checkType(ASTType *type)
{
    if(type == NULL) return;

    switch(type->kind)
    {
        case A_TYPE_IDEN:
        {
            SymEntry *e = _symTableLookUp(globalContext.cc.checkingLocalsSymTble, type->iden.lexeme, LOOKUP_ALL);

            if(e == NULL)
            {   
                checkerErrorLn(type->startTok, "Undefined type '%s'", type->iden.lexeme);
                prettyPrintCheckerSourceError(type->startTok, type->startTok);
            }
            else type->checkType = e->type;
        }break;
        case A_TYPE_INT: 
        {
            switch(type->iden.type)
            {
                case TOK_I8_KW: type->checkType = i8Type; break;
                case TOK_I16_KW: type->checkType = i16Type; break;
                case TOK_I32_KW: type->checkType = i32Type; break;
                case TOK_I64_KW: type->checkType = i64Type; break;
                case TOK_UI8_KW: type->checkType = ui8Type; break;
                case TOK_UI16_KW: type->checkType = ui16Type; break;
                case TOK_UI32_KW: type->checkType = ui32Type; break;
                case TOK_UI64_KW: type->checkType = ui64Type; break;
                case TOK_INT_KW: type->checkType = intType; break; 
            }
        }break;
        case A_TYPE_FLOAT:
        {
            switch(type->iden.type)
            {
                case TOK_F32_KW: type->checkType = f32Type; break;
                case TOK_F64_KW: type->checkType = f64Type; break;
                case TOK_FLOAT_KW: type->checkType = floatType; break;
            }
        }break;
        case A_TYPE_BOOL: type->checkType = boolType; break;
        case A_TYPE_STRING: type->checkType = stringType; break;
        case A_TYPE_VOID: type->checkType = voidType; break;
        case A_TYPE_VARIADIC:
        {
            checkType(type->variadic.base);

            type->checkType = newCheckerTypeVariadic(type->variadic.base->checkType);
        }break;
        case A_TYPE_ARRAY:
        {
            bool isSlice = type->array.isSlice;

            checkType(type->array.elem);

            CheckerType *elemType = type->array.elem->checkType;

            if(!isSlice)
            {
                CheckerType *expectedSizeType = i64Type;

                if(!checkRHSExprWithTypeAndCast(expectedSizeType, &(type->array.size), true, false) || (IS_EXPR_COMPTIME_KIND(type->array.size, A_EXPR_COMP_TIME_RUNTIME)) || 
                    GET_EXPR_COMPTIME_VAL(type->array.size, .i) <= 0)
                {
                    checkerError(type->array.size->startTok, "Expected positive integer constant expression for array size got ");
                    printCheckerType(type->array.size->checkType);
                    
                    if(IS_EXPR_COMPTIME_KIND(type->array.size, A_EXPR_COMP_TIME_RUNTIME))
                        fprintf(stderr, ", which hasn't got a constant value");
                    else if(GET_EXPR_COMPTIME_VAL(type->array.size, .i) <= 0)
                        fprintf(stderr, ", which has a value of %lld, which is <= 0", GET_EXPR_COMPTIME_VAL(type->array.size, .i));
                    fprintf(stderr, "\n");

                    prettyPrintCheckerSourceError(type->array.size->startTok, type->array.size->endTok);
                }
                else type->checkType = newCheckerTypeArray(elemType, type->array.size->compTimeVal.i, false, false);
            }
            else type->checkType = newCheckerTypeArray(elemType, 0, true, false);
            
        }break;
        case A_TYPE_POINTER:
        {
            checkType(type->pointer.type);

            type->checkType = newCheckerTypePointer(type->pointer.type->checkType);
            
        }break;
        case A_TYPE_FUNC_POINTER:
        {
            ASTFuncSig *sig = type->funcPointer.funcSig;

            checkType(sig->retType);
            sig->retAsArg = checkIfFuncRetTypeShouldBePassedAsArg(sig->retType->checkType);
            if(sig->retAsArg)
            {
                sig->retAsArgName = sig->startTok;
                sig->retAsArgName.type = TOK_IDEN;
                strcpy(sig->retAsArgName.lexeme, "__retAsArg");
            }

            size_t numDefParams = 0;

            ASTFuncParamLL *astParamLL = sig->params;
            ScopedDeclLL *paramsLL = NULL;
            
            SymTable *funcTypeSymtable = newSymTable(globalContext.cc.checkingLocalsSymTble->belongsToNamespace);

            if(astParamLL != NULL)
            {
                SymEntryLL *restoreTo = funcTypeSymtable->localTbl;

                ASTFuncParamLL *c = astParamLL->first;
                while(c != NULL)
                {
                    ASTFuncParam *param = c->item;

                    SymEntry *paramEntry = _symTableLookupLocal(funcTypeSymtable, param->iden.lexeme);
                    if(paramEntry != NULL)
                    {
                        checkerError(param->iden, "Parameter with name '%s' has already been declared for parameter of type ", param->iden.lexeme);
                        printCheckerType(paramEntry->type);
                        fprintf(stderr, "\n");
                        prettyPrintCheckerSourceError(param->iden, param->iden);
                    }
                    else
                    {
                        bool hasDefValue = param->defValue != NULL;
                        int flags = 0;

                        if(hasDefValue) flags |= SCOPED_DECL_HAS_DEF_VALUE;

                        checkExpr(param->defValue, false);
                        
                        if(hasDefValue) numDefParams++;

                        if(param->type->kind != A_TYPE_INFER) checkType(param->type);
                        else param->type->checkType = param->defValue->checkType;

                        ScopedDecl *scopedD = newScopedDecl(param->iden, param->type->checkType, param->defValue, flags);
                        scopedD->abiInfo = classifyCheckerType(scopedD->type);

                        if(paramsLL == NULL) paramsLL = newScopedDeclLL(scopedD);
                        else addScopedDeclLL(&paramsLL, scopedD);

                        _symTableInsertLocal(funcTypeSymtable, param->iden.lexeme, param->type->checkType, param->iden);
                    }

                    c = c->next;
                }
                
                _symTableRestoreLocal(funcTypeSymtable, restoreTo);
            }

            //TODO FREE funcTypeSymtable

            CheckerType *t = newCheckerTypeFunc(paramsLL, sig->retType->checkType, sig->retAsArg, 0);
            t->funcType.numDefaultParams = numDefParams;
            
            t->funcType.retABIInfo = classifyCheckerType(t->funcType.ret);

            type->checkType = t;
        }break; 
        case A_TYPE_SCOPED:
        {
            if(type->scoped.expr->kind == A_EXPR_STRUCT_LITERAL) checkerErrorLn(type->startTok, "Expected a valid type, but got a struct/union literal as a type name!");
            else
            {
                //TODO fix: variables with union or struct types are registered fine.
                checkExpr(type->scoped.expr, false);

                SymEntry *entry = _symTableLookUp(type->scoped.expr->scopeAccess.scopeName->checkType->namespaceType.tble, 
                                                  type->scoped.expr->scopeAccess.memb.lexeme, LOOKUP_ALL);

                CheckerType *t = type->scoped.expr->checkType;
                if(!entry->isType)
                {
                    checkerErrorLn(type->startTok, "Expected a type name but instead got '%s'", type->scoped.expr->scopeAccess.memb.lexeme);

                    prettyPrintCheckerSourceError(type->scoped.expr->scopeAccess.memb, type->scoped.expr->scopeAccess.memb);
                }
                else type->checkType = t;
            }
        }break;

        case A_TYPE_TUPLE:
        {
            ASTTupleMembLL *astTupleMembs = type->tuple.membs;

            { //loop through tuple membs
                ASTTupleMembLL *currMemb = astTupleMembs->first;
                size_t membIndex = 0;

                SymTable *tuplesSymTable = newSymTable(globalContext.cc.checkingLocalsSymTble->belongsToNamespace);
                CheckerTypeLL *membTypes = NULL;

                while(currMemb != NULL)
                {
                    ASTTupleMemb *memb = currMemb->item;
                    CheckerType *membType = NULL;

                    if(memb->hasIden)
                    {
                        SymEntry *entry = _symTableLookupLocal(tuplesSymTable, memb->iden.lexeme);

                        if(entry != NULL)
                        {
                            checkerErrorLn(memb->startTok, "Member with name '%s' in tuple already exists", entry->name);
                            prettyPrintCheckerSourceError(memb->startTok, memb->endTok);

                            goto while_continue;
                        }
                        else
                        {
                            checkType(memb->type);
                            
                            entry = _symTableInsertLocal(tuplesSymTable, memb->iden.lexeme, memb->type->checkType, memb->iden);
                            membType = entry->type;
                        }
                    }
                    else
                    {
                        char buf[1000] = {0};
                        sprintf(buf, "_m%lld", membIndex);

                        SymEntry *entry = _symTableLookupLocal(tuplesSymTable, buf);

                        if(entry != NULL)
                        {
                            checkerErrorLn(memb->startTok, "Member with name '%s' in tuple already exists, a tuple member is not allowed names that begin with '_m' unless all members are named", entry->name);
                            
                            prettyPrintCheckerSourceError(memb->startTok, memb->endTok);
                            
                            goto while_continue;
                        }
                        else
                        {
                            checkType(memb->type);
                            entry = _symTableInsertLocal(tuplesSymTable, buf, memb->type->checkType, memb->startTok);
                            membType = entry->type;
                        }
                    }

                    if(membTypes == NULL) membTypes = newCheckerTypeLL(membType);
                    else addCheckerTypeLL(&membTypes, membType);
                    
                    while_continue:
                        membIndex++;
                        currMemb = currMemb->next;
                }
                
                type->checkType = checkIfTupleNotExistsCreateNew(membTypes);
                //todo free tuples symtable
            }
        }break;
    }

    //TODO: check if type->checkType does NOT equals Any type or a generic, if so, then set
    //give error if type filter is not NULL, since only Any type and generics can have them

    if(!areTypesEqual(anyType, type->checkType) && (type->typeFilter != NULL))
    {
        checkerError(type->startTok, "Type: "); 
        printCheckerType(type->checkType);
        fprintf(stderr, " has a type filter, but only 'any' types and generics can have type filters\n");

        prettyPrintCheckerSourceError(type->startTok, type->endTok);
    }
    else if(type->typeFilter != NULL)
    {
        checkTypesFilters(type);
    }
}
void checkTypesFilters(ASTType *type)
{
    if(type->typeFilter == NULL) return;

    type->checkType = copyCheckerType(type->checkType);

    CheckerTypeLL *checkerTypeFilters = NULL;
    ASTTypeLL *currASTType = type->typeFilter->first;

    while(currASTType != NULL)
    {
        checkType(currASTType->item);

        if(checkerTypeFilters == NULL) checkerTypeFilters = newCheckerTypeLL(currASTType->item->checkType);
        else addCheckerTypeLL(&(checkerTypeFilters), currASTType->item->checkType);

        currASTType = currASTType->next;
    }

    type->checkType->typeFilters = checkerTypeFilters;
}
CheckerType *checkIfTupleTypeAlreadyExists(CheckerTypeLL *types)
{
    { //loop through all tyup;le types

        CheckerTypeLL *currTypeLL = globalContext.cc.tupleTypes;
        while(currTypeLL != NULL)
        {
            { //check if types parameter equals any tuples types list

                CheckerTypeLL *t = types->first;
                size_t index = 0;
                bool success = true;

                CheckerType *tupleTypeToCompareAgainst = currTypeLL->item;

                while(t != NULL)
                {

                    if(tupleTypeToCompareAgainst->tupleType.numTypes <= index) 
                    {
                        success = false;
                        t = t->next;

                        continue;
                    }
                    CheckerType *typeOfMembToCompareAgainst = currTypeLL->item->tupleType.types[index];
                    if(!areTypesEqual(t->item, typeOfMembToCompareAgainst)) success = false;

                    index++;
                    t = t->next;
                }

                if(index != tupleTypeToCompareAgainst->tupleType.numTypes) success = false;
                
                if(success)
                {
                    return currTypeLL->item;
                }
            }

            currTypeLL = currTypeLL->prev;
        }
    }

    return NULL;
}
CheckerType *checkIfTupleNotExistsCreateNew(CheckerTypeLL *types)
{
    CheckerType *existingType = checkIfTupleTypeAlreadyExists(types);
                
    if(existingType != NULL) return existingType;
    else 
    {
        existingType = newCheckerTypeTuple(types);

        if(!globalContext.cc.isIncompletePass)
        {
            bool unusedCircularDependency = false;
            ScopedDecl *unused = NULL;
            StructUnionDependencyNode *r = getStructUnionDependencies(existingType, NULL, &unusedCircularDependency, &unused);
        
            existingType->tupleType.dependencies = r;
        }
        if(globalContext.cc.tupleTypes == NULL) globalContext.cc.tupleTypes = newCheckerTypeLL(existingType);
        else addCheckerTypeLL(&(globalContext.cc.tupleTypes), existingType);
    }

    return existingType;
}
void checkExpr(ASTExpr *expr, bool isIncompletePass)
{
    if(expr == NULL) return;

    switch(expr->kind)
    {
        case A_EXPR_LIT:
        {
            switch(expr->lit.type)
            {
                case TOK_INT_LITERAL:
                {                    
                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_INT;
                    expr->compTimeVal.i = stringToInteger(expr->lit.lexeme);

                    expr->checkType = castSignedIntLiteralToCorrectType(expr->compTimeVal.i);

                }break;
                case TOK_FLOAT_LITERAL:
                {
                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_FLOAT;
                    expr->compTimeVal.f = atof(expr->lit.lexeme);

                    expr->checkType = castFloatLiteralToCorrectType(expr->compTimeVal.f);
                }break;
                case TOK_STRING_LITERAL:
                {
                    size_t len = strlen(expr->lit.lexeme) - 2; //minus 2 because of the qoutes in the lexeme
                    if(len > 0)
                    {
                        expr->compTimeVal.str.data = malloc(len + 1);
                        strncpy(expr->compTimeVal.str.data, expr->lit.lexeme + 1, len); 

                        expr->compTimeVal.str.data[len] = '\0';
                    }
                    else expr->compTimeVal.str.data = NULL;

                    expr->compTimeVal.str.len = len;
                    expr->compTimeVal.str.realLenExcludingEscapedCharacters = getRealLengthOfCompilerStr(expr->lit.lexeme) - 2;
                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_STRING;
                    expr->checkType = stringType;
                }break;
                case TOK_CHAR_LITERAL:
                {
                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_INT;
                    expr->compTimeVal.i = charLitToInt(expr->lit);

                    expr->checkType = i8Type;
                }break;
                case TOK_BOOL_LITERAL:
                {
                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_BOOL;
                    expr->compTimeVal.i = (!strcmp("true", expr->lit.lexeme)) ? true : false;

                    expr->checkType = boolType;
                }break;
                case TOK_NULL_KW:
                {
                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_NULL;
                    expr->checkType = newCheckerTypePointer(voidType);
                }break;
            }

            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE; 
        }break;

        case A_EXPR_IDEN:
        {
            if(globalContext.cc.currScopedExpr.isCheckingScopeExpr)
                globalContext.cc.currScopedExpr.scopeNameBeingChecked = expr->iden;
            
            SymEntry *entry = _symTableLookUp(globalContext.cc.checkingLocalsSymTble, expr->iden.lexeme, LOOKUP_ALL);
            
            if(entry == NULL)
            {
                entry = _symTableLookUp(globalContext.symTable, expr->iden.lexeme, LOOKUP_ALL);
                if(entry != NULL)
                {
                    if(!isTypeFunction(entry->type)) entry = NULL; //try find operator overload
                }
            }
            
            if((expr->checkType != NULL) && (expr->idenSymEntry != NULL)) 
            {
                return;
            }

            if(entry != NULL)
            {
                if(entry->mySymTable != NULL)
                {
                    if(entry->mySymTable->belongsToNamespace != NULL) 
                    {
                        if(entry->isGlobal || entry->isGlobalFunc) entry->mySymTable->belongsToNamespace->isOtherSymbolsUsed = true;
                    }
                }

                expr->checkType = entry->type;
                
                // if(entry->type->kind == C_TYPE_ENUM_MEMBER)
                // {
                //     expr->compTimeVal.kind = A_EXPR_COMP_TIME_INT;
                //     expr->compTimeVal.i = entry->type->enumMembType.val;
                //     expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;

                //     expr->checkType = entry->type->enumMembType.enumtype;
                // }
                // else 
                if(entry->isConst) expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                else if(entry->isActualConst)
                {
                    expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                    expr->compTimeVal = entry->constVal;
                }
                else if(entry->isType) expr->compTimeVal.isL_or_RValue = EXPR_NEITHER_VALUE;
                else if(entry->isNamespace) 
                {
                    expr->compTimeVal.isL_or_RValue = EXPR_NEITHER_VALUE;
                    entry->type->namespaceType.tble->belongsToNamespace->isImported = true;
                }
                else //if global and local
                {
                    //check if its a function (but not a function pointer), if so,  then set to rvalue
                    if(isTypeFunction(expr->checkType))
                    {
                        if(entry->isGlobalFunc)
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        else expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                    }
                    else if(isTypeEnum(expr->checkType))
                    {
                        if(entry->isUnscopedEnumMemb)
                        {
                            size_t index = 0;
                            typeHasMember(expr->checkType, entry->name, &index);

                            EnumMembLL *memb = getEnumMembLLAt(entry->type->enumType.membLL, index);
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_INT;
                            expr->compTimeVal.i = memb->item->val;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                    }
                    else expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                }

                expr->idenSymEntry = entry;
            }
            else
            {
                checkerErrorLn(expr->iden, "Use of undeclared identifier '%s'", expr->iden.lexeme);
                prettyPrintCheckerSourceError(expr->startTok, expr->startTok);
                expr->checkType = NULL;
            }
            break;
        }break;

        case A_EXPR_INDEX_REF:
        {
            checkExpr(expr->arrayRef.iden, isIncompletePass);
            checkExpr(expr->arrayRef.index, isIncompletePass);

            CheckerType *refExprType = expr->arrayRef.iden->checkType;
            CheckerType *indexExprType = expr->arrayRef.index->checkType;

            // if(!isTypeIndexable(refExprType))
            // {
            //     checkerError(expr->arrayRef.iden->startTok, "Expected a valid indexable type, but got: ");
            //     printCheckerType(refExprType);
            //     fprintf(stderr, "\n");
            //     prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
            // }
            // else
            
            bool foundOperatorOverload = false;

            { //check for operator overload
                SymEntry *e = checkTryfindOverloadFunc('[', refExprType, indexExprType);

                if(e != NULL)
                {
                    foundOperatorOverload = true;

                    checkConvertIndexRefExprToOperFuncCall(expr, e);

                    checkExpr(expr, isIncompletePass);

                }
            }

            if(!foundOperatorOverload)
            {
                if(isTypeArray(refExprType))
                {
                    if(isTypeAliased(refExprType)) refExprType = getAliasedTypeBase(refExprType);

                    if(!checkRHSExprWithTypeAndCast(i64Type, &(expr->arrayRef.index), true, isIncompletePass))
                    {
                        checkerError(expr->arrayRef.index->startTok, "Expected an integer for array indexing, but instead got ");
                        printCheckerType(expr->arrayRef.index->checkType);
                        fprintf(stderr, "\n");

                        prettyPrintCheckerSourceError(expr->arrayRef.index->startTok, expr->arrayRef.index->endTok);
                    }
                    else if((expr->arrayRef.index->compTimeVal.kind != A_EXPR_COMP_TIME_RUNTIME) && isTypeFixedArray(refExprType))
                    {
                        size_t indexVal = indexVal = expr->arrayRef.index->compTimeVal.i;

                        if(indexVal >= refExprType->arrayType.size)
                        {
                            checkerErrorLn(expr->arrayRef.index->startTok, "Index is outside array range, array index should be between 0 - %ld but instead got %ld", refExprType->arrayType.size - 1, indexVal);
                            prettyPrintCheckerSourceError(expr->arrayRef.index->startTok, expr->arrayRef.index->endTok);
                        }
                    }
                    
                    expr->checkType = refExprType->arrayType.base;
                }
                else if(isTypePointer(refExprType))
                {
                    if(isTypeAliased(refExprType)) refExprType = getAliasedTypeBase(refExprType);
                    expr->checkType = refExprType->pointerType.base;
                }
                else if(isTypeVariadic(refExprType))
                {
                    if(isTypeAliased(refExprType)) refExprType = getAliasedTypeBase(refExprType);
                    expr->checkType = refExprType->variadicType.base;
                }
                else if(isTypeTuple(refExprType))
                {
                    if(isTypeAliased(refExprType)) refExprType = getAliasedTypeBase(refExprType);
                    
                    if(expr->arrayRef.index->compTimeVal.kind != A_EXPR_COMP_TIME_INT)
                    {
                        checkerErrorLn(expr->arrayRef.index->startTok, "Expected a compile time integer constant for indexing a tuple");

                        prettyPrintCheckerSourceError(expr->arrayRef.index->startTok, expr->arrayRef.index->endTok);
                    }
                    else
                    {
                        size_t i = expr->arrayRef.index->compTimeVal.i;

                        if(!(i > indexExprType->tupleType.numTypes - 1) && !(i >= 0))
                        {
                            checkerErrorLn(expr->arrayRef.index->startTok, "Index values for tuples must be between 0 and the number of items in a tuple - 1, in this case between 0 and %lld but instead got %lld", 
                                         refExprType->tupleType.numTypes - 1, 
                                         i);

                            prettyPrintCheckerSourceError(expr->arrayRef.index->startTok, expr->arrayRef.index->endTok);
                        }
                        else
                        {
                            expr->checkType = refExprType->tupleType.types[i];

                            expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        }
                    }
                }
                else
                {
                    //TODO: if there is  a operator overload for it, get the function from the 
                    //symbol table and get the return type
                    //TODO: this only works when getting indexed values, what about setting index values? eg b[90] = x;

                    char *fname = allocOperFuncNameFromTypes('[', refExprType, indexExprType);
                    SymEntry *fEntry = _symTableLookUp(globalContext.cc.checkingLocalsSymTble, fname, LOOKUP_ALL);

                    if(fEntry == NULL)
                    {
                        checkerError(expr->arrayRef.iden->startTok, "Expected a valid indexable type, but got: ");
                        printCheckerType(refExprType);
                        fprintf(stderr, ", Maybe define a operator overload for the index operation\n");
                        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                    }
                    else expr->checkType = fEntry->type->opFuncType.ret;

                    
                    free(fname);
                }

                expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
            }
        }break;

        case A_EXPR_POST:
        {
            ASTExpr *preExpr = expr->post.expr;
            checkExpr(preExpr, isIncompletePass);

            Token op = expr->post.op;

            switch(expr->post.op.type)
            {
                case TOK_DEC_OP:
                case TOK_INC_OP:
                {
                    if(isTypeInteger(preExpr->checkType))
                    {
                        expr->compTimeVal = preExpr->compTimeVal;
                        if(!IS_EXPR_COMPTIME_KIND(expr, A_EXPR_COMP_TIME_RUNTIME) || (expr->compTimeVal.isL_or_RValue != EXPR_L_VALUE))
                        {
                            checkerErrorLn(expr->startTok, "Expected an integer which is a L-VALUE for op '%s'", op.lexeme);
                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }

                        expr->checkType = preExpr->checkType;
                    }
                    else if(isTypePointer(preExpr->checkType))
                    {
                        expr->compTimeVal = preExpr->compTimeVal;
                        if(!IS_EXPR_COMPTIME_KIND(expr, A_EXPR_COMP_TIME_RUNTIME) || (expr->compTimeVal.isL_or_RValue != EXPR_L_VALUE))
                        {
                            checkerErrorLn(expr->startTok, "Expected a pointer which is a L-VALUE for unary '%s'", op.lexeme);
                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }

                        expr->checkType = preExpr->checkType;
                    }
                    else
                    {
                        checkerErrorLn(preExpr->startTok, "The expression that the unary op '%s' is being applied to, cannot be used with this unary op.", op.lexeme);
                        prettyPrintCheckerSourceError(preExpr->startTok, preExpr->endTok);
                    }
                }break;
            }
        }break;

        case A_EXPR_NULL_ACCESS:
        case A_EXPR_MEMBER_ACCESS:
        {
            checkExpr(expr->membAccess.typeName, isIncompletePass);

            if(expr->kind == A_EXPR_NULL_ACCESS)
            {
                if(!isTypePointer(expr->membAccess.typeName->checkType))
                {
                    checkerError(expr->membAccess.typeName->startTok, "Expression on LHS of '?.' operator must be a pointer type instead got ");
                    printCheckerType(expr->membAccess.typeName->checkType);
                    fprintf(stderr, "\n");

                    prettyPrintCheckerSourceError(expr->membAccess.typeName->startTok, expr->membAccess.typeName->endTok);
                }
            }

            bool wasEnum = false;
            CheckerType *membAccessLeftSideType = expr->membAccess.typeName->checkType;
            
            bool isValidTypeToMemberAccess = isTypeMemberAccessable(membAccessLeftSideType, &wasEnum);
            bool foundMethod = false;

            { //check first for method in type
                
                //if type is not valid to member access check for methods.
                SymEntry *e = typeMethodTableLookUp(globalContext.typeMethodTable, membAccessLeftSideType, expr->membAccess.memb.lexeme);

                if(e == NULL) foundMethod = false;
                // {
                //     checkerError(expr->membAccess.typeName->startTok, "Expected a valid type for member access, but got: ");
                //     printCheckerType(membAccessLeftSideType);
                //     fprintf(stderr, "\n");
                //     prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                // }
                else
                {
                    if(CHECK_TYPE_FLAG(e->type, TYPE_FUNC_THISPTR_FLAG))
                    {
                        if(expr->membAccess.typeName->compTimeVal.isL_or_RValue != EXPR_L_VALUE)
                        {
                            checkerError(expr->membAccess.memb, "Method cannot be called on non L-VALUE expression, since method '%s' on type: ", e->name);
                            printCheckerType(expr->membAccess.typeName->checkType);
                            fprintf(stderr, " has a pointer this parameter\n");
                            
                            prettyPrintCheckerSourceError(expr->membAccess.memb, expr->membAccess.memb);
                        }
                    }

                    expr->checkType = e->type;
                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                    expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                    expr->membAccess.isMethodAccess = true;
                    expr->idenSymEntry = e;
                    expr->idenSymEntry->myDecl;
                    foundMethod = true;
                }
            }
            
            if(!foundMethod)
            {
                if(wasEnum)
                {
                    CheckerType *enumType = (isTypeAliased(membAccessLeftSideType)) ? getAliasedTypeBase(membAccessLeftSideType) : membAccessLeftSideType;
                    char *enumTypeName = enumType->enumType.name;

                    if(expr->membAccess.typeName->compTimeVal.isL_or_RValue != EXPR_NEITHER_VALUE)
                    {
                        checkerErrorLn(expr->startTok, "Expected enum member access as '%s.MEMBER'", enumTypeName);
                    }
                    else
                    {
                        //TODO if enum is [strict] then set checkType to enum member else int.
                        //TODO: set comptimeval and stuff
                        size_t membIndex = 0;
                        if(!typeHasMember(enumType, expr->membAccess.memb.lexeme, &membIndex))
                        {
                            checkerError(expr->membAccess.memb, "Member '%s', does not exist in enum type: ", expr->membAccess.memb.lexeme);
                            printCheckerType(enumType);

                            prettyPrintCheckerSourceError(expr->membAccess.memb, expr->membAccess.memb);
                        }
                        else
                        {
                            EnumMembLL *memb = getEnumMembLLAt(enumType->enumType.membLL, membIndex);

                            expr->checkType = membAccessLeftSideType;//newCheckerTypeEnumMemb(entry->type, 0);
                            expr->compTimeVal.i = memb->item->val;
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_INT;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                    }
                }
                else if(isTypeNamespace(membAccessLeftSideType))
                {
                    size_t index = 0;
                    bool found = typeHasMember(namespaceInfoType, expr->membAccess.memb.lexeme, &index);
                   
                    if(!found)
                    {
                        checkerError(expr->startTok, "Type ");
                        printCheckerType(namespaceInfoType);
                        fprintf(stderr, " has not got member '%s'\n", expr->membAccess.memb.lexeme);
                    }
                    else
                    {
                        ScopedDeclLL *dll = namespaceInfoType->structType.declLL;

                        ScopedDecl *d = getScopedDeclLLAt(dll, index)->item;

                        expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        if(CHECK_SCOPED_DECL_FLAG(d, SCOPED_DECL_CONST)) expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        else expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;

                        expr->checkType = d->type;
                    }
                }
                else if(isTypeArray(membAccessLeftSideType))
                {
                    //TODO finish
                    //array.len
                    if(!strcmp(expr->membAccess.memb.lexeme, "len"))
                    {
                        expr->checkType = i64Type;
                        if(isTypeFixedArray(membAccessLeftSideType))
                        {
                            CheckerType *arrType = (isTypeAliased(membAccessLeftSideType)) ? getAliasedTypeBase(membAccessLeftSideType) : membAccessLeftSideType;

                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_INT;
                            expr->compTimeVal.i = arrType->arrayType.size;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                        else expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                    }
                    //arrayslice.data
                    else if(!strcmp(expr->membAccess.memb.lexeme, "data"))
                    {
                        expr->checkType = newCheckerTypePointer(voidType);
                        expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                    }
                    else checkerErrorLn(expr->startTok, "Identifier '%s',  is not a valid identifier for member access on an array type", expr->membAccess.memb.lexeme);
                    
                    if(isTypeSliceArray(membAccessLeftSideType)) expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                }
                else if(isValidTypeToMemberAccess)
                {
                    if(isTypePointer(membAccessLeftSideType))
                    {
                        membAccessLeftSideType = (isTypeAliased(membAccessLeftSideType)) ? 
                                                getAliasedTypeBase(membAccessLeftSideType)->pointerType.base : membAccessLeftSideType->pointerType.base;
                    }

                    if(isTypeAliased(membAccessLeftSideType))
                        membAccessLeftSideType = getAliasedTypeBase(membAccessLeftSideType);

                    size_t index = 0;
                    bool found = typeHasMember(membAccessLeftSideType, expr->membAccess.memb.lexeme, &index);

                    if(!found)
                    {
                        checkerError(expr->startTok, "Type ");
                        printCheckerType(membAccessLeftSideType);
                        fprintf(stderr, " has not got member '%s'\n", expr->membAccess.memb.lexeme);
                    }
                    else if(isTypeTaggedUnion(membAccessLeftSideType))
                    {
                        if(index == -1) //.kind access
                        {
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                            expr->checkType = membAccessLeftSideType->taggedUnionType.tagEnumType;
                        }
                        else
                        {
                            ScopedDeclLL *d = getScopedDeclLLAt(membAccessLeftSideType->taggedUnionType.declLL, index);
                            CheckerType *enumType = isTypeAliased(membAccessLeftSideType->taggedUnionType.tagEnumType) ?
                                                    getAliasedTypeBase(membAccessLeftSideType->taggedUnionType.tagEnumType) :
                                                    membAccessLeftSideType->taggedUnionType.tagEnumType;

                            size_t enumMembIndex = 0;
                            typeHasMember(enumType, expr->membAccess.memb.lexeme, &enumMembIndex);

                            EnumMembLL *em = getEnumMembLLAt(enumType->enumType.membLL, enumMembIndex);
                            
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_INT;
                            expr->compTimeVal.i = em->item->val;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                            expr->checkType = copyCheckerType(membAccessLeftSideType->taggedUnionType.tagEnumType);

                            if(isTypeAliased(expr->checkType)) 
                            {
                                CheckerType *alias =  getAliasedTypeBase(expr->checkType);
                                alias->enumType.isTypeBelongingToTaggedUnionMemberAccess = true;
                                alias->enumType.taggedUnionMemberAccess.taggedUnionType = membAccessLeftSideType;
                                alias->enumType.taggedUnionMemberAccess.unionMember = d->item;
                            }
                            else 
                            {
                                expr->checkType->enumType.isTypeBelongingToTaggedUnionMemberAccess = true;
                                expr->checkType->enumType.isTypeBelongingToTaggedUnionMemberAccess = true;
                                expr->checkType->enumType.taggedUnionMemberAccess.taggedUnionType = membAccessLeftSideType;
                                expr->checkType->enumType.taggedUnionMemberAccess.unionMember = d->item;
                            }
                        }
                    }
                    else
                    {
                        ScopedDeclLL *dll = (isTypeUnion(membAccessLeftSideType)) ? membAccessLeftSideType->unionType.declLL : membAccessLeftSideType->structType.declLL;

                        ScopedDecl *d = getScopedDeclLLAt(dll, index)->item;

                        expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        if(CHECK_SCOPED_DECL_FLAG(d, SCOPED_DECL_CONST)) expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        else expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;

                        expr->checkType = d->type;
                    }
                }
                else
                {
                    checkerError(expr->membAccess.typeName->startTok, "Expected a valid type for member access, but got: ");
                    printCheckerType(membAccessLeftSideType);
                    fprintf(stderr, "\n");
                    prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                }

                if((expr->kind == A_EXPR_NULL_ACCESS) && (expr->membAccess.nullAccess.elseExpr != NULL))
                {
                    if(!checkRHSExprWithTypeAndCast(expr->checkType, &(expr->membAccess.nullAccess.elseExpr), true, isIncompletePass))
                    {
                        checkerError(expr->membAccess.typeName->startTok, "Else expressions type should be equal or castable to members type, expected type ");
                        printCheckerType(expr->checkType);
                        fprintf(stderr, " but got type ");
                        printCheckerType(expr->membAccess.nullAccess.elseExpr->checkType);
                        fprintf(stderr, "\n");
                        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                    }
                }
                
            }
        }break;

        case A_EXPR_UNARY:
        {
            ASTExpr *uExpr = expr->unary.expr;
            Token uOp = expr->unary.op;

            checkExpr(uExpr, isIncompletePass);

            bool foundOperatorOverload = false;

            {   //try find operator overload
                SymEntry *e = checkTryfindOverloadFunc(expr->unary.op.type, uExpr->checkType, NULL);

                if(e != NULL)
                {
                    foundOperatorOverload = true;

                    checkConvertUnaryExprToOperFuncCall(expr, e);

                    checkExpr(expr, isIncompletePass);

                }
            }

            if(!foundOperatorOverload)
            {
                switch(uOp.type)
                {
                    case '+':
                    {
                        if(isTypeInteger(uExpr->checkType))
                        {
                            expr->compTimeVal = uExpr->compTimeVal;
                            expr->checkType = uExpr->checkType;
                        }
                        else if(isTypeFloat(uExpr->checkType))
                        {
                            expr->compTimeVal = uExpr->compTimeVal;
                            expr->checkType = uExpr->checkType;
                        }
                        else
                        {
                            char *fname = allocOperFuncNameFromTypes(uOp.type, uExpr->checkType, NULL);
                            SymEntry *fEntry = _symTableLookUp(globalContext.cc.checkingLocalsSymTble, fname, LOOKUP_ALL);

                            if(fEntry == NULL)
                            {
                                checkerError(expr->startTok, "No operator overload found for unary operator '%s' and type: ", uOp.lexeme);
                                printCheckerType(uExpr->checkType);
                                fprintf(stderr, "\n");
                                prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                            }
                            else
                            {
                                expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                                expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;

                                expr->checkType = fEntry->type->opFuncType.ret;
                            }

                            free(fname);
                        }
                    }break;

                    case '-':
                    {
                        if(isTypeInteger(uExpr->checkType))
                        {
                            expr->compTimeVal = uExpr->compTimeVal;
                            if(IS_EXPR_COMPTIME_KIND(expr, A_EXPR_COMP_TIME_INT)) expr->compTimeVal.i *= -1;

                            expr->checkType = uExpr->checkType;
                        }
                        else if(isTypeFloat(uExpr->checkType))
                        {
                            expr->compTimeVal = uExpr->compTimeVal;
                            if(IS_EXPR_COMPTIME_KIND(expr, A_EXPR_COMP_TIME_FLOAT)) expr->compTimeVal.f *= -1;

                            expr->checkType = uExpr->checkType;
                        }
                        else
                        {
                            //TODO check if unary op has been defined for type
                        }
                    }break;

                    case TOK_DEC_OP:
                    case TOK_INC_OP:
                    {
                        if(isTypeInteger(uExpr->checkType))
                        {
                            expr->compTimeVal = uExpr->compTimeVal;
                            if(!IS_EXPR_COMPTIME_KIND(expr, A_EXPR_COMP_TIME_RUNTIME) || (expr->compTimeVal.isL_or_RValue != EXPR_L_VALUE))
                            {
                                checkerErrorLn(expr->startTok, "Expected an integer which is a L-VALUE for unary '%s'", uOp.lexeme);
                                prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                            }

                            expr->checkType = uExpr->checkType;
                        }
                        else if(isTypePointer(uExpr->checkType))
                        {
                            expr->compTimeVal = uExpr->compTimeVal;
                            if(!IS_EXPR_COMPTIME_KIND(expr, A_EXPR_COMP_TIME_RUNTIME) || (expr->compTimeVal.isL_or_RValue != EXPR_L_VALUE))
                            {
                                checkerErrorLn(expr->startTok, "Expected a pointer which is a L-VALUE for unary '%s'", uOp.lexeme);
                                prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                            }

                            expr->checkType = uExpr->checkType;
                        }
                        else
                        {
                            checkerErrorLn(uExpr->startTok, "The expression that the unary op '%s' is being applied to, cannot be used with this unary op.", uOp.lexeme);
                            prettyPrintCheckerSourceError(uExpr->startTok, uExpr->endTok);
                        }
                    }break;

                    case '*':
                    {
                        if(uExpr->compTimeVal.isL_or_RValue == EXPR_R_VALUE)
                        {
                            checkerErrorLn(expr->startTok, "Cannot take the address of a R-VALUE");
                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }

                        expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;

                        expr->checkType = newCheckerTypePointer(uExpr->checkType);
                    }break;

                    case '@':
                    {
                        if(!isTypePointer(uExpr->checkType))
                        {
                            checkerError(expr->startTok, "Cannot dereference a non pointer type, got ");
                            printCheckerType(uExpr->checkType);
                            fprintf(stderr, "\n");
                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else
                        {
                            CheckerType *ptrType = (isTypeAliased(uExpr->checkType)) ? getAliasedTypeBase(uExpr->checkType) : uExpr->checkType;
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;

                            expr->checkType = ptrType->pointerType.base;
                        }
                    }break;

                    case '!':
                    {
                        if(!isTypeBoolean(uExpr->checkType))
                        {
                            checkerError(expr->startTok, "Cannot apply '%s' operator on a non boolean type, got ", uOp.lexeme);
                            printCheckerType(uExpr->checkType);
                            fprintf(stderr, "\n");
                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else
                        {
                            expr->compTimeVal = uExpr->compTimeVal;

                            if(IS_EXPR_COMPTIME_KIND(expr, A_EXPR_COMP_TIME_BOOL)) expr->compTimeVal.i = !expr->compTimeVal.i;

                            expr->checkType = uExpr->checkType;
                        }
                    }break;

                    case '~':
                    {
                        if(!isTypeInteger(uExpr->checkType))
                        {
                            checkerError(expr->startTok, "Cannot apply '%s' operator on a non integer type, got ", uOp.lexeme);
                            printCheckerType(uExpr->checkType);
                            fprintf(stderr, "\n");
                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else
                        {
                            expr->compTimeVal = uExpr->compTimeVal;

                            if(IS_EXPR_COMPTIME_KIND(expr, A_EXPR_COMP_TIME_INT)) expr->compTimeVal.i = ~expr->compTimeVal.i;

                            expr->checkType = uExpr->checkType;
                        }
                    }break;
                }
            }
        }break;

        case A_EXPR_BINARY:
        {
            ASTExpr **lhs = &(expr->binary.l);
            ASTExpr **rhs = &(expr->binary.r);
            checkExpr((*lhs), isIncompletePass);
            checkExpr((*rhs), isIncompletePass);

            bool lEqualOrCastableToR = checkRHSExprWithTypeAndCast((*rhs)->checkType, lhs, true, isIncompletePass);
            bool rEqualOrCastableTol = false;

            if(!lEqualOrCastableToR) //only check if l is not castable to r, aas you dont want to insert two casts
            {
                rEqualOrCastableTol = checkRHSExprWithTypeAndCast((*lhs)->checkType, rhs, true, isIncompletePass);
            }   

            //check for operator overload first
            bool foundOperatorOverload = false;

            {   //try find operator overload
                char *funcName = allocOperFuncNameFromTypes(expr->binary.op.type, (*lhs)->checkType, (*rhs)->checkType);
                SymEntry *e = checkTryfindOverloadFunc(expr->binary.op.type, (*lhs)->checkType, (*rhs)->checkType);
                
                if(e != NULL)
                {
                    foundOperatorOverload = true;

                    checkConvertBinaryExprToOperFuncCall(expr, e);

                    checkExpr(expr, isIncompletePass);

                }
            }

            if(!foundOperatorOverload)
            {
                switch(expr->binary.op.type)
                {
                    case '?':
                    {
                        CheckerType *typeOfLHSandRHS = (*lhs)->checkType;

                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "Invalid types to preform '%s' binary operator, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!isTypePointerOrFunction(typeOfLHSandRHS) && !isTypeBoolean(typeOfLHSandRHS))
                        {
                            //todo maybe handle oepratoro overload funcs here
                            checkerError(expr->startTok, "Expected valid types to preform '%s' binary operator, instead trying to preform '%s' on two expressions of type: ", 
                                         expr->binary.op.lexeme, expr->binary.op.lexeme);

                            printCheckerType(typeOfLHSandRHS);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = (*lhs)->compTimeVal.kind;
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_BOOL:
                                {
                                    expr->compTimeVal.i = ((*lhs)->compTimeVal.i) ? (*lhs)->compTimeVal.i : (*rhs)->compTimeVal.i;
                                }break;

                            }

                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                        else
                        {
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }

                    }break;

                    case '+': case '-':
                    {
                        bool isAdd = (expr->binary.op.type == '+');

                        CheckerType *typeOfLHSandRHS = (*lhs)->checkType;
                        //if the lhs or the rhs is a void pointer
                        //the compiler automatically converts teh ppointer to a char pointer
                        //this flag determines whether the entire expression should be cast back to void pointer
                        bool ifPointerArith_CastExprBackToVoidPtr = false;
                        if(isTypePointer((*lhs)->checkType))
                        {
                            {//check if void pointer, if so cast to byte ppointer
                                if(isTypeVoidPtr((*lhs)->checkType))
                                {
                                    checkInsertCastExpr(newCheckerTypePointer(i8Type), lhs);
                                    ifPointerArith_CastExprBackToVoidPtr = true;
                                }
                            }

                            if(!isTypeInteger((*rhs)->checkType))
                            {
                                checkerError((*rhs)->startTok, "Expected rhs of '%s' binary operator to be of an integer type since the lhs is a pointer, but rhs type is: ", expr->binary.op.lexeme);
                                printCheckerType((*rhs)->checkType);
                                fprintf(stderr, "\n");

                                prettyPrintCheckerSourceError((*rhs)->startTok, (*rhs)->endTok);
                            }
                            else
                            {
                                expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                                expr->checkType = (ifPointerArith_CastExprBackToVoidPtr) ? newCheckerTypePointer(voidType) : (*lhs)->checkType;
                                expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                            }
                        }
                        else if(isTypePointer((*rhs)->checkType))
                        {
                            if(!isAdd) //can do somethign like 1 - ptr, as it doesnt mke sense
                            {
                                checkerError((*rhs)->startTok, "Expected rhs of '%s' binary operator to be of an integer type, but rhs type is: ", expr->binary.op.lexeme);
                                printCheckerType((*rhs)->checkType);
                                fprintf(stderr, "\n");

                                prettyPrintCheckerSourceError((*rhs)->startTok, (*rhs)->endTok);
                            }
                            else
                            {
                                {//check if void pointer, if so cast to byte ppointer
                                    if(isTypeVoidPtr((*rhs)->checkType))
                                    {
                                        checkInsertCastExpr(newCheckerTypePointer(i8Type), rhs);
                                        ifPointerArith_CastExprBackToVoidPtr = true;
                                    }
                                }

                                if(!isTypeInteger((*lhs)->checkType))
                                {
                                    checkerError((*lhs)->startTok, "Expected lhs of '%s' binary operator to be of an integer type since the rhs is a pointer, but lhs type is: ", expr->binary.op.lexeme);
                                    printCheckerType((*lhs)->checkType);
                                    fprintf(stderr, "\n");

                                    prettyPrintCheckerSourceError((*lhs)->startTok, (*lhs)->endTok);
                                }
                                else
                                {
                                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                                    expr->checkType = (ifPointerArith_CastExprBackToVoidPtr) ? newCheckerTypePointer(voidType) : (*rhs)->checkType;
                                    expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                                }
                            }
                        }
                        else if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "Invalid types to preform '%s' binary operator, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!isTypeInteger(typeOfLHSandRHS) && !isTypeFloat(typeOfLHSandRHS))
                        {
                            //todo maybe handle oepratoro overload funcs here
                            checkerError(expr->startTok, "Expected valid types to preform '%s' binary operator, instead trying to preform '%s' on two expressions of type: ", 
                                        expr->binary.op.lexeme, expr->binary.op.lexeme);

                            printCheckerType(typeOfLHSandRHS);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = (*lhs)->compTimeVal.kind;
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_INT:
                                {
                                    if(isAdd) expr->compTimeVal.i = (*lhs)->compTimeVal.i +  (*rhs)->compTimeVal.i;
                                    else expr->compTimeVal.i = (*lhs)->compTimeVal.i -  (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_FLOAT:
                                {
                                    if(isAdd) expr->compTimeVal.f = (*lhs)->compTimeVal.f +  (*rhs)->compTimeVal.f;
                                    else expr->compTimeVal.f = (*lhs)->compTimeVal.f -  (*rhs)->compTimeVal.f;
                                }break;
                            }

                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                        else
                        {
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                    }break;

                    case '*': case '/':
                    {
                        bool isMult = (expr->binary.op.type == '*');
                        CheckerType *typeOfLHSandRHS = (*lhs)->checkType;

                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "Invalid types to preform '%s' binary operator, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!isTypeInteger(typeOfLHSandRHS) && !isTypeFloat(typeOfLHSandRHS))
                        {
                            //todo maybe handle oepratoro overload funcs here
                            checkerError(expr->startTok, "Expected valid types to preform '%s' binary operator, instead trying to preform '%s' on two expressions of type: ", 
                                         expr->binary.op.lexeme, expr->binary.op.lexeme);

                            printCheckerType(typeOfLHSandRHS);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = (*lhs)->compTimeVal.kind;
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_INT:
                                {
                                    if(isMult) expr->compTimeVal.i = (*lhs)->compTimeVal.i *  (*rhs)->compTimeVal.i;
                                    else expr->compTimeVal.i = (*lhs)->compTimeVal.i /  (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_FLOAT:
                                {
                                    if(isMult) expr->compTimeVal.f = (*lhs)->compTimeVal.f *  (*rhs)->compTimeVal.f;
                                    else expr->compTimeVal.f = (*lhs)->compTimeVal.f /  (*rhs)->compTimeVal.f;
                                }break;
                            }

                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                        else
                        {
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }

                    }break;

                    case '%':
                    {
                        CheckerType *typeOfLHSandRHS = (*lhs)->checkType;

                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "Invalid types to preform '%s' binary operator, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!isTypeInteger(typeOfLHSandRHS))
                        {
                            //todo maybe handle oepratoro overload funcs here
                            checkerError(expr->startTok, "Expected valid types to preform '%s' binary operator, instead trying to preform '%s' on two expressions of type: ", 
                                         expr->binary.op.lexeme, expr->binary.op.lexeme);

                            printCheckerType(typeOfLHSandRHS);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = (*lhs)->compTimeVal.kind;
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_INT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.i % (*rhs)->compTimeVal.i;
                                }break;
                            }

                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                        else
                        {
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }

                    }break;

                    case TOK_LEFT_SHIFT: case TOK_RIGHT_SHIFT:
                    {
                        CheckerType *typeOfLHSandRHS = (*lhs)->checkType;
                        bool isLeftShift = (expr->binary.op.type == TOK_LEFT_SHIFT);

                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "Invalid types to preform '%s' binary operator, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!isTypeInteger(typeOfLHSandRHS))
                        {
                            //todo maybe handle oepratoro overload funcs here
                            checkerError(expr->startTok, "Expected valid types to preform '%s' binary operator, instead trying to preform '%s' on two expressions of type: ", 
                                         expr->binary.op.lexeme, expr->binary.op.lexeme);

                            printCheckerType(typeOfLHSandRHS);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = (*lhs)->compTimeVal.kind;
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_INT:
                                {
                                    if(isLeftShift) expr->compTimeVal.i = (*lhs)->compTimeVal.i << (*rhs)->compTimeVal.i;
                                    else expr->compTimeVal.i = (*lhs)->compTimeVal.i >> (*rhs)->compTimeVal.i;
                                }break;
                            }

                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                        else
                        {
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }

                    }break;

                    case '&': case '|': case '^':
                    {
                        CheckerType *typeOfLHSandRHS = (*lhs)->checkType;
                        bool isAnd = (expr->binary.op.type == '&');
                        bool isOr = (expr->binary.op.type == '|');
                        bool isXor = (expr->binary.op.type == '^');

                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "Invalid types to preform '%s' binary operator, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!isTypeInteger(typeOfLHSandRHS) && !isTypeBoolean(typeOfLHSandRHS))
                        {
                            //todo maybe handle oepratoro overload funcs here
                            checkerError(expr->startTok, "Expected valid types to preform '%s' binary operator, instead trying to preform '%s' on two expressions of type: ", 
                                         expr->binary.op.lexeme, expr->binary.op.lexeme);

                            printCheckerType(typeOfLHSandRHS);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = (*lhs)->compTimeVal.kind;
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_INT:
                                {
                                    if(isAnd) expr->compTimeVal.i = (*lhs)->compTimeVal.i & (*rhs)->compTimeVal.i;
                                    else if(isOr) expr->compTimeVal.i = (*lhs)->compTimeVal.i | (*rhs)->compTimeVal.i;
                                    else if(isOr) expr->compTimeVal.i = (*lhs)->compTimeVal.i ^ (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_BOOL:
                                {
                                    if(isAnd) expr->compTimeVal.i = (*lhs)->compTimeVal.i & (*rhs)->compTimeVal.i;
                                    else if(isOr) expr->compTimeVal.i = (*lhs)->compTimeVal.i | (*rhs)->compTimeVal.i;
                                    else if(isXor) expr->compTimeVal.i = (*lhs)->compTimeVal.i ^ (*rhs)->compTimeVal.i;
                                }break;
                            }

                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                        else
                        {
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                    }break;

                    case TOK_AND_LOGOP: case TOK_OR_LOGOP:
                    {
                        CheckerType *typeOfLHSandRHS = (*lhs)->checkType;
                        bool isAnd = (expr->binary.op.type == TOK_AND_LOGOP);
                        bool isOr = (expr->binary.op.type == TOK_OR_LOGOP);

                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "Invalid types to preform '%s' binary operator, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!isTypeBoolean(typeOfLHSandRHS))
                        {
                            //todo maybe handle oepratoro overload funcs here
                            checkerError(expr->startTok, "Expected valid types to preform '%s' binary operator, instead trying to preform '%s' on two expressions of type: ", 
                                         expr->binary.op.lexeme, expr->binary.op.lexeme);

                            printCheckerType(typeOfLHSandRHS);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = (*lhs)->compTimeVal.kind;
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_INT:
                                {
                                    if(isAnd) expr->compTimeVal.i = (*lhs)->compTimeVal.i && (*rhs)->compTimeVal.i;
                                    else if(isOr) expr->compTimeVal.i = (*lhs)->compTimeVal.i || (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_BOOL:
                                {
                                    if(isAnd) expr->compTimeVal.i = (*lhs)->compTimeVal.i && (*rhs)->compTimeVal.i;
                                    else if(isOr) expr->compTimeVal.i = (*lhs)->compTimeVal.i || (*rhs)->compTimeVal.i;
                                }break;
                            }

                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                        else
                        {
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                            expr->checkType = (*lhs)->checkType;
                            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        }
                    }break;
                    
                    case '<':
                    {
                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "The type of LHS expression of '%s' binary operator cannot be cast or inferred to be the same as RHS, and vice versa, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_BOOL;                    
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_BOOL: case A_EXPR_COMP_TIME_INT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.i <  (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_FLOAT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.f <  (*rhs)->compTimeVal.f;
                                }break;

                                case A_EXPR_COMP_TIME_STRING:
                                {
                                    expr->compTimeVal.i = strcmp((*lhs)->compTimeVal.str.data, (*rhs)->compTimeVal.str.data) < 0;
                                }break;
                            }
                        }
                        else expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        expr->checkType = boolType;
                        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        
                    }break;

                    case TOK_LE_RELOP:
                    {
                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "The type of LHS expression of '%s' binary operator cannot be cast or inferred to be the same as RHS, and vice versa, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_BOOL;                    
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_BOOL: case A_EXPR_COMP_TIME_INT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.i <=  (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_FLOAT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.f <=  (*rhs)->compTimeVal.f;
                                }break;

                                case A_EXPR_COMP_TIME_STRING:
                                {
                                    expr->compTimeVal.i = strcmp((*lhs)->compTimeVal.str.data, (*rhs)->compTimeVal.str.data) <= 0;
                                }break;
                            }
                        }
                        else expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        expr->checkType = boolType;
                        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        
                    }break;

                    case '>':
                    {
                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "The type of LHS expression of '%s' binary operator cannot be cast or inferred to be the same as RHS, and vice versa, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_BOOL;                    
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_BOOL: case A_EXPR_COMP_TIME_INT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.i >  (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_FLOAT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.f >  (*rhs)->compTimeVal.f;
                                }break;

                                case A_EXPR_COMP_TIME_STRING:
                                {
                                    expr->compTimeVal.i = strcmp((*lhs)->compTimeVal.str.data, (*rhs)->compTimeVal.str.data) > 0;
                                }break;
                            }
                        }
                        else expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        expr->checkType = boolType;
                        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        
                    }break;

                    case TOK_GE_RELOP:
                    {
                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "The type of LHS expression of '%s' binary operator cannot be cast or inferred to be the same as RHS, and vice versa, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_BOOL;                    
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_BOOL: case A_EXPR_COMP_TIME_INT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.i >=  (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_FLOAT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.f >=  (*rhs)->compTimeVal.f;
                                }break;

                                case A_EXPR_COMP_TIME_STRING:
                                {
                                    expr->compTimeVal.i = strcmp((*lhs)->compTimeVal.str.data, (*rhs)->compTimeVal.str.data) >= 0;
                                }break;
                            }
                        }
                        else expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        expr->checkType = boolType;
                        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        
                    }break;

                    case TOK_EQ_RELOP:
                    {
                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "The type of LHS expression of '%s' binary operator cannot be cast or inferred to be the same as RHS, and vice versa, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(isTypeAggregate((*lhs)->checkType))
                        {
                            checkConvertBinaryExprToOperFuncCall(expr, globalContext.cc.stdEqualsFunction);

                            checkExpr(expr, isIncompletePass);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_BOOL;                    
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_BOOL: case A_EXPR_COMP_TIME_INT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.i ==  (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_FLOAT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.f ==  (*rhs)->compTimeVal.f;
                                }break;

                                case A_EXPR_COMP_TIME_STRING:
                                {
                                    expr->compTimeVal.i = strcmp((*lhs)->compTimeVal.str.data, (*rhs)->compTimeVal.str.data) == 0;
                                }break;
                            }
                        }
                        else expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        expr->checkType = boolType;
                        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        
                    }break;

                    case TOK_NEQ_RELOP:
                    {
                        if(!lEqualOrCastableToR && !rEqualOrCastableTol)
                        {
                            checkerError(expr->startTok, "The type of LHS expression of '%s' binary operator cannot be cast or inferred to be the same as RHS, and vice versa, LHS type: ", expr->binary.op.lexeme);
                            printCheckerType((*lhs)->checkType);
                            fprintf(stderr, ", RHS type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if(isTypeAggregate((*lhs)->checkType))
                        {
                            checkConvertBinaryExprToOperFuncCall(expr, globalContext.cc.stdEqualsFunction);

                            checkConvertExprToUnaryExpr(expr, '!');
                            
                            checkExpr(expr, isIncompletePass);
                        }
                        else if(!IS_EXPR_COMPTIME_KIND((*lhs), A_EXPR_COMP_TIME_RUNTIME) && 
                                !IS_EXPR_COMPTIME_KIND((*rhs), A_EXPR_COMP_TIME_RUNTIME))
                        {
                            //lhs and rhs comptime value kind should be the same at this point since theyve been cast
                            expr->compTimeVal.kind = A_EXPR_COMP_TIME_BOOL;                    
                            switch((*lhs)->compTimeVal.kind)
                            {
                                case A_EXPR_COMP_TIME_BOOL: case A_EXPR_COMP_TIME_INT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.i !=  (*rhs)->compTimeVal.i;
                                }break;

                                case A_EXPR_COMP_TIME_FLOAT:
                                {
                                    expr->compTimeVal.i = (*lhs)->compTimeVal.f !=  (*rhs)->compTimeVal.f;
                                }break;

                                case A_EXPR_COMP_TIME_STRING:
                                {
                                    expr->compTimeVal.i = strcmp((*lhs)->compTimeVal.str.data, (*rhs)->compTimeVal.str.data) != 0;
                                }break;
                            }
                        }
                        else expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                        expr->checkType = boolType;
                        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        
                    }break;
                }
            }
        }break;

        case A_EXPR_RANGE:
        {
            checkExpr(expr->range.l, isIncompletePass);
            checkExpr(expr->range.r, isIncompletePass);

            expr->checkType = rangeType;
            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

        }break;
        case A_EXPR_SCOPE_ACCESS:
        {
            checkScopeAccessExpr(expr, isIncompletePass, false);
        }break;

        case A_EXPR_FUNC_CALL:
        {
            if(expr->hasBeenChecked && expr->genFuncRetAsArgForDefaultValueExpr)
            {
                if(globalContext.cc.currStmtBeingChecked != NULL)
                {
                   checkInsertFuncRetAsArgStmts(expr);
                }
            }

            if(expr->hasBeenChecked) break;
            //todo: NAMED expression and default arguemnts and what not
            //todo: right now, checking function arguments will give a must use return value error
            //so set checking stmtexpr to false when checking arguements.
            checkExpr(expr->funcCall.iden, isIncompletePass);

            CheckerType *funcType = expr->funcCall.iden->checkType;
            funcType = (isTypeAliased(funcType)) ? getAliasedTypeBase(funcType) : funcType;

            CheckerType *prevFuncCallType = globalContext.cc.currFuncCallTypeBeingChecked;
            globalContext.cc.currFuncCallTypeBeingChecked = funcType;
            if((globalContext.cc.isCheckingStmtExpr) && (funcType != NULL))
            {
                if(CHECK_TYPE_FLAG(funcType, TYPE_FUNC_MUST_USE_RETURN) && !isTypeVoid(funcType->funcType.ret))
                {
                    checkerError(expr->startTok, "Function type ");
                    printCheckerType(funcType);
                    fprintf(stderr, " is defined as having the [useReturn] tag, which means the return value must be used\n");
                    prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                }
            }

            if(!isTypeFunction(funcType))
            {
                if(isTypeEnum(funcType) && funcType->enumType.isTypeBelongingToTaggedUnionMemberAccess)
                {
                    ASTNamedExprLL *args = expr->funcCall.args;
                    size_t argsLength = (args == NULL) ? 0 : args->first->numItems;

                    if(argsLength != 1)
                    {
                        checkerErrorLn(expr->funcCall.iden->startTok, "Expected exactly one identifier in tagged union auto cast expression, instead got %lld", argsLength);
                        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                    }
                    else if(globalContext.cc.currMatchArmBeingChecked != NULL)
                    {
                        ASTNamedExpr *arg = args->first->item;

                        if((arg->kind == A_NAMED_EXPR_NORMAL) && (arg->normal->kind == A_EXPR_IDEN))
                        {
                            SymEntry *entry = _symTableLookupLocal(globalContext.cc.checkingLocalsSymTble, arg->normal->iden.lexeme);

                            if(entry == NULL)
                            {
                                entry = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, arg->normal->iden.lexeme, funcType->enumType.taggedUnionMemberAccess.unionMember->type, arg->startTok);
                                
                                //set the intial value in the arm checking stage
                                ASTStmt *s = newASTStmtDecl(newASTDeclVar(newASTExprIden(entry->posInFile), newASTTypeInfer(entry->posInFile), NULL));
                                entry->myDecl = s->decl.decl;
                                ASTMatchArm *arm = globalContext.cc.currMatchArmBeingChecked;

                                if(arm->taggedUnionAutoCasts == NULL) arm->taggedUnionAutoCasts = newASTStmtLL(s);
                                else addASTStmtlLL(&(arm->taggedUnionAutoCasts), s);
                            }
                            else
                            {
                                checkerErrorLn(arg->startTok, "Identifier already exists with name '%s'", arg->normal->iden.lexeme);

                                prettyPrintCheckerSourceError(arg->startTok, arg->startTok);
                            }
                        }
                        else
                        {
                            checkerErrorLn(arg->startTok, "Expected an identifier expression!");
                            prettyPrintCheckerSourceError(arg->startTok, arg->startTok);
                        }

                        expr->checkType = funcType;
                        expr->compTimeVal = expr->funcCall.iden->compTimeVal;
                    }
                    else
                    {
                        checkerErrorLn(expr->startTok, "Tagged union auto cast expressions can only be used in match arms");
                        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                    }
                }
                else
                {
                    checkerError(expr->funcCall.iden->startTok, "Treating incorrect type as function, got type ");
                    printCheckerType(funcType);
                    fprintf(stderr, "\n");
                    prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                }
            }
            else
            {
                bool isFuncVaraidic = CHECK_TYPE_FLAG(funcType, TYPE_FUNC_VARIADIC_FLAG);
                bool isFuncForeign = CHECK_TYPE_FLAG(funcType, TYPE_FOREIGN_FLAG);
                bool isTypeMethod = funcType->funcType.isTypeMethod;
                ScopedDeclLL *params = funcType->funcType.paramLL;

                size_t paramCount = (params == NULL) ? 0 : params->first->numItems;
                size_t expectedArgCount = paramCount - funcType->funcType.numDefaultParams;
                if(isFuncVaraidic) expectedArgCount -= 1; //varaidic arguements can be empty, so minus 1 for each varaidic parameter, only one is allowed so minus 1
                if(isTypeMethod) expectedArgCount -= 1;

                size_t argCount = (expr->funcCall.args == NULL) ? 0 : expr->funcCall.args->first->numItems;

                bool *doneMap = calloc(paramCount, sizeof(bool));
                size_t argCorrectedOrderCount = ((isFuncForeign && isFuncVaraidic) ? ((paramCount < argCount) ? argCount : paramCount) : paramCount);
                ASTExpr **argCorrectedOrder = calloc(argCorrectedOrderCount, sizeof(ASTExpr *));

                SymTable *genParamsResolvedTbl = NULL;

                if((argCount > paramCount) && !isFuncVaraidic)
                {
                    checkerError(expr->startTok, "Preforming function call with too many arguments, got %lld argument(s) but function ", argCount);
                    printCheckerType(funcType);
                    fprintf(stderr, " has only %lld parameter(s)\n", paramCount);

                    prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                }
                else if(argCount < expectedArgCount)
                {
                    checkerError(expr->startTok, "Preforming function call with not enough arguments, got %lld argument(s) but function ", argCount);
                    printCheckerType(funcType);
                    fprintf(stderr, " has %lld default parameter(s) and %lld parameter(s), meaning %lld arguement(s) are expected\n", funcType->funcType.numDefaultParams, paramCount, expectedArgCount);

                    prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                }
                else if(argCount != 0)
                {
                    ASTNamedExprLL *args = expr->funcCall.args->first;
                    size_t index = 0;
                    
                    if(isTypeMethod) index += 1;

                    bool temp = globalContext.cc.isCheckingStmtExpr;
                    globalContext.cc.isCheckingStmtExpr = false;

                    char funcGenericIdenName[1000] = {0};
                    bool wasGeneric = false;
                    //func call can be scope access or type method too, not just iden todo: others
                    if((funcType->funcType.genericFuncAST != NULL) && (expr->funcCall.iden->kind == A_EXPR_IDEN))
                    {
                        sprintf(funcGenericIdenName, "%s_", funcType->funcType.genericFuncAST->item->func.iden.lexeme);
                    }

                    while(args != NULL)
                    {
                        ASTNamedExpr *namedExpr = args->item;
                        ASTExpr *exprToAddCorrectedOrderList = NULL;

                        switch(namedExpr->kind)
                        {
                            case A_NAMED_EXPR_NORMAL:
                            {                                  
                                ScopedDeclLL *sdAtIndex = getScopedDeclLLAt(params, index);
                                bool dontAddCorrectedArg = false;

                                if(isTypeGeneric(sdAtIndex->item->type))
                                {   
                                    checkExpr(namedExpr->normal, isIncompletePass);
                                    strcat(funcGenericIdenName, allocCheckerTypeName(namedExpr->normal->checkType));

                                    if(genParamsResolvedTbl == NULL) genParamsResolvedTbl = newSymTable(NULL);

                                    SymEntry *genParamResolveEntry = _symTableLookUp(genParamsResolvedTbl, sdAtIndex->item->type->genericType.name, LOOKUP_ALL);
                                    if(genParamResolveEntry != NULL)
                                    {
                                        if(!areTypesEqual(genParamResolveEntry->type, namedExpr->normal->checkType))
                                        {
                                            checkerError(namedExpr->normal->startTok, "Expected argument to have type of: ");
                                            printCheckerType(genParamResolveEntry->type);
                                            fprintf(stderr, "\n");

                                            prettyPrintCheckerSourceError(namedExpr->normal->startTok, namedExpr->normal->endTok);
                                        }
                                    }
                                    else
                                    {
                                        _symTableInsertGlobal(genParamsResolvedTbl, sdAtIndex->item->type->genericType.name, namedExpr->normal->checkType, sdAtIndex->item->name);
                                    }


                                    wasGeneric = true;
                                }
                                else
                                {
                                    if(sdAtIndex == NULL)
                                    {
                                        checkerErrorLn(namedExpr->startTok, "Unexpected arguement, maybe this is argument is preceded by a variadic argument, but this argument is not of valid type.");
                                        prettyPrintCheckerSourceError(namedExpr->normal->startTok, namedExpr->normal->endTok);

                                        args = args->next;
                                        continue;
                                    }
                                    if(isTypeVariadic(sdAtIndex->item->type))
                                    {
                                        bool equalOrCastableToVariadic = false;
                                        bool equalOrCastableToVariadicBase = false;

                                        if(namedExpr->isVariadicArrayArg)
                                        {
                                            equalOrCastableToVariadic = checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &namedExpr->normal, !isFuncForeign, isIncompletePass);
                                            if(!equalOrCastableToVariadic)
                                            {
                                                checkerError(namedExpr->startTok, "Expected expression to have type: ");
                                                printCheckerType(sdAtIndex->item->type);
                                                fprintf(stderr, ", but got type: ");
                                                printCheckerType(namedExpr->normal->checkType);
                                                fprintf(stderr, "\n");

                                                prettyPrintCheckerSourceError(namedExpr->normal->startTok, namedExpr->normal->endTok);
                                            }
                                        }
                                        else equalOrCastableToVariadicBase = checkRHSExprWithTypeAndCast(sdAtIndex->item->type->variadicType.base, &namedExpr->normal, !isFuncForeign, isIncompletePass);
                                        
                                        if(!equalOrCastableToVariadic && !equalOrCastableToVariadicBase)
                                        {
                                            checkerError(namedExpr->startTok, "Type mismatch, parameter '%s' is of type: ", sdAtIndex->item->name.lexeme);
                                            printCheckerType(sdAtIndex->item->type);

                                            fprintf(stderr, " but got type: ");
                                            printCheckerType(namedExpr->normal->checkType);
                                            fprintf(stderr, ", variadic parameters can accept the base type as a parameter or an array of the base type, or another variadic parameter of the same base type\n");

                                            prettyPrintCheckerSourceError(namedExpr->normal->startTok, namedExpr->normal->endTok);
                                        }
                                        else if(!equalOrCastableToVariadic)
                                        {
                                            ASTExpr *variadicArgsConvertedToArrayLitExpr = NULL;
                                            ASTExprLL *variadicArrayLitExprs = NULL;

                                            size_t variadicArgNumber = 0;
                                            while((args != NULL))
                                            {
                                                namedExpr = args->item;

                                                if(namedExpr->kind == A_NAMED_EXPR_NAMED)
                                                {  
                                                    args = args->prev;
                                                    break;
                                                }
                                                    
                                                ASTExpr *e = namedExpr->normal;
                                                                                            
                                                if(!checkRHSExprWithTypeAndCast(sdAtIndex->item->type->variadicType.base, &e, !isFuncForeign,isIncompletePass)) 
                                                {
                                                    args = args->prev;
                                                    break;
                                                }

                                                if(!isFuncForeign)
                                                {
                                                    if(variadicArrayLitExprs == NULL) variadicArrayLitExprs = newASTExprLL(e);
                                                    else addASTExprLL(&variadicArrayLitExprs, e);
                                                }
                                                else
                                                {
                                                    argCorrectedOrder[index + variadicArgNumber] = e;
                                                    dontAddCorrectedArg = true;
                                                }
                                                variadicArgNumber += 1;

                                                args = args->next;
                                            }

                                            if(!isFuncForeign)
                                            {
                                                variadicArgsConvertedToArrayLitExpr = newASTExprArrayLit(variadicArrayLitExprs->first->item->startTok, NULL, NULL, variadicArrayLitExprs);
                                                checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &variadicArgsConvertedToArrayLitExpr, !isFuncForeign, isIncompletePass);

                                                exprToAddCorrectedOrderList = variadicArgsConvertedToArrayLitExpr;
                                            }
                                        }
                                    }
                                    else if(!checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &namedExpr->normal, true, isIncompletePass))
                                    {
                                        checkerError(namedExpr->startTok, "Type mismatch, parameter '%s' is of type: ", sdAtIndex->item->name.lexeme);
                                        printCheckerType(sdAtIndex->item->type);

                                        fprintf(stderr, " but got type: ");
                                        printCheckerType(namedExpr->normal->checkType);
                                        fprintf(stderr, "\n");

                                        prettyPrintCheckerSourceError(namedExpr->normal->startTok, namedExpr->normal->endTok);
                                    }
                                }
                                if(doneMap[index] == true)
                                {
                                    checkerErrorLn(namedExpr->startTok, "Parameter '%s' has already been given in function call", sdAtIndex->item->name.lexeme);
                                    prettyPrintCheckerSourceError(namedExpr->normal->startTok, namedExpr->normal->endTok);
                                }
                                else doneMap[index] = true;

                                if(!dontAddCorrectedArg)
                                {
                                    if(exprToAddCorrectedOrderList == NULL) exprToAddCorrectedOrderList = namedExpr->normal;
                                    argCorrectedOrder[index] = exprToAddCorrectedOrderList;
                                }
                            }break;
                            case A_NAMED_EXPR_NAMED:
                            {
                                size_t indexInFunctionParam = 0;
                                if(funcHasParam(funcType, namedExpr->named.name.lexeme, &indexInFunctionParam))
                                {                                
                                    ScopedDeclLL *sdAtIndex = getScopedDeclLLAt(params, indexInFunctionParam);
                                    bool dontAddCorrectedArg = false;

                                    if(isTypeVariadic(sdAtIndex->item->type))
                                    {
                                        bool equalOrCastableToVariadic = false;
                                        bool equalOrCastableToVariadicBase = false;
                                        
                                        if(namedExpr->isVariadicArrayArg)
                                        {
                                            equalOrCastableToVariadic = checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &namedExpr->named.expr, !isFuncForeign, isIncompletePass);

                                            if(!equalOrCastableToVariadic)
                                            {
                                                checkerError(namedExpr->startTok, "Expected expression to have type: ");
                                                printCheckerType(sdAtIndex->item->type);
                                                fprintf(stderr, ", but got type: ");
                                                printCheckerType(namedExpr->named.expr->checkType);
                                                fprintf(stderr, "\n");

                                                prettyPrintCheckerSourceError(namedExpr->named.expr->startTok, namedExpr->named.expr->endTok);
                                            }
                                        }
                                        else equalOrCastableToVariadicBase = checkRHSExprWithTypeAndCast(sdAtIndex->item->type->variadicType.base, &namedExpr->named.expr, !isFuncForeign, isIncompletePass); 
                                        
                                        if(!equalOrCastableToVariadic && !equalOrCastableToVariadicBase)
                                        {
                                            checkerError(namedExpr->startTok, "Type mismatch, parameter '%s' is of type: ", sdAtIndex->item->name.lexeme);
                                            printCheckerType(sdAtIndex->item->type);

                                            fprintf(stderr, " but got type: ");
                                            printCheckerType(namedExpr->named.expr->checkType);
                                            fprintf(stderr, ", variadic parameters can accept the base type as a parameter or an array of the base type, or another variadic parameter of the same base type\n");

                                            prettyPrintCheckerSourceError(namedExpr->named.expr->startTok, namedExpr->named.expr->endTok);
                                        }
                                        else if(!equalOrCastableToVariadic)
                                        {
                                            ASTExpr *variadicArgsConvertedToArrayLitExpr = NULL;
                                            ASTExprLL *variadicArrayLitExprs = NULL;
                                            
                                            size_t variadicArgNumber = 0;
                                            while((args != NULL))
                                            {
                                                namedExpr = args->item;

                                                ASTExpr *e = (namedExpr->kind == A_NAMED_EXPR_NAMED) ? namedExpr->named.expr : namedExpr->normal;
                                                
                                                if(!checkRHSExprWithTypeAndCast(sdAtIndex->item->type->variadicType.base, &e, !isFuncForeign, isIncompletePass)) 
                                                {
                                                    args = args->prev;
                                                    break;
                                                }

                                                if(!isFuncForeign)
                                                {
                                                    if(variadicArrayLitExprs == NULL) variadicArrayLitExprs = newASTExprLL(e);
                                                    else addASTExprLL(&variadicArrayLitExprs, e);
                                                }
                                                else
                                                {
                                                    argCorrectedOrder[indexInFunctionParam + variadicArgNumber] = e;
                                                    dontAddCorrectedArg = true;
                                                }

                                                variadicArgNumber += 1;

                                                if((args->next != NULL) && 
                                                   (args->next->item->kind == A_NAMED_EXPR_NAMED))
                                                {  

                                                    break;
                                                }
                                                args = args->next;
                                            }
                                             if(!isFuncForeign)
                                            {
                                                variadicArgsConvertedToArrayLitExpr = newASTExprArrayLit(variadicArrayLitExprs->first->item->startTok, NULL, NULL, variadicArrayLitExprs);
                                                checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &variadicArgsConvertedToArrayLitExpr, !isFuncForeign, isIncompletePass);

                                                exprToAddCorrectedOrderList = variadicArgsConvertedToArrayLitExpr;
                                            }
                                        }
                                    }
                                    else if(!checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &namedExpr->named.expr, true, isIncompletePass))
                                    {
                                        checkerError(namedExpr->startTok, "Type mismatch, parameter '%s' is of type: ", sdAtIndex->item->name.lexeme);
                                        printCheckerType(sdAtIndex->item->type);

                                        fprintf(stderr, " but got type: ");
                                        printCheckerType(namedExpr->named.expr->checkType);
                                        fprintf(stderr, "\n");

                                        prettyPrintCheckerSourceError(namedExpr->named.expr->startTok, namedExpr->named.expr->endTok);

                                    }
                                    
                                    if(doneMap[indexInFunctionParam] == true)
                                    {
                                        checkerErrorLn(namedExpr->startTok, "Parameter '%s' has already been given in function call", sdAtIndex->item->name.lexeme);
                                        prettyPrintCheckerSourceError(namedExpr->named.expr->startTok, namedExpr->named.expr->endTok);
                                    }
                                    else doneMap[indexInFunctionParam] = true;

                                    if(exprToAddCorrectedOrderList == NULL) exprToAddCorrectedOrderList = namedExpr->named.expr;
                                    
                                    if(!dontAddCorrectedArg)
                                    {
                                        if(exprToAddCorrectedOrderList == NULL) exprToAddCorrectedOrderList = namedExpr->normal;
                                        argCorrectedOrder[indexInFunctionParam] = exprToAddCorrectedOrderList;
                                    }
                                    //carry on checkign expression of members from the poitn it left off, for example
                                    // if struct s1,  has members a, b, c and u do s1{b = 90};
                                    //it woill carry on checking from c NOT a.
                                    //index = indexInFunctionParam;
                                }
                                else 
                                {
                                    checkerError(namedExpr->startTok, "Parameter '%s' does not exist in function type ", namedExpr->named.name.lexeme);
                                    printCheckerType(funcType);
                                    fprintf(stderr, "\n");
                                    prettyPrintCheckerSourceError(namedExpr->startTok, namedExpr->startTok);
                                }
                            }break;

                        }

                        index++;

                        if(args != NULL) args = args->next; //checkign variadic parameter can set args to NULL 
                    }

                    if(wasGeneric) //todo func call iden is not just iden it can be other stuff, like scope access nd stuff
                    {

                        ASTDecl *copiedGenericDecl = deepCopyASTDecl(funcType->funcType.genericFuncAST->item);
                        copiedGenericDecl->genParamsResolvedSymTbl = genParamsResolvedTbl;
                        ASTDeclLL *temp = newASTDeclLL(copiedGenericDecl);

                        strcpy(copiedGenericDecl->func.iden.lexeme, funcGenericIdenName);
                        strcpy(expr->funcCall.iden->iden.lexeme, funcGenericIdenName);

                        checkGlobalFuncDecl(&temp, true);
                        checkGenericFuncBody(copiedGenericDecl);

                        _symTableInsertGlobal(globalContext.genericsSymTbl, funcGenericIdenName, copiedGenericDecl->declType, copiedGenericDecl->startTok);

                    }

                    globalContext.cc.isCheckingStmtExpr = temp;

                    for(int i = (isTypeMethod) ? 1 : 0; i < paramCount; i++)
                    {
                        ScopedDeclLL *sdAtIndex = getScopedDeclLLAt(params, i);

                        if(!CHECK_SCOPED_DECL_FLAG(sdAtIndex->item, SCOPED_DECL_HAS_DEF_VALUE) && !doneMap[i] && !isTypeVariadic(sdAtIndex->item->type))
                        {
                            checkerErrorLn(expr->startTok, "Parameter '%s' is missing from the function call", sdAtIndex->item->name.lexeme);
                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        else if((sdAtIndex->item->defExpr != NULL) && !doneMap[i])
                        {
                            argCorrectedOrder[i] = sdAtIndex->item->defExpr;

                            if((globalContext.cc.currStmtBeingChecked != NULL) &&
                                argCorrectedOrder[i]->genFuncRetAsArgForDefaultValueExpr)
                            {
                                checkInsertFuncRetAsArgStmts(argCorrectedOrder[i]);
                            }

                            if((globalContext.cc.currStmtBeingChecked != NULL) && 
                                argCorrectedOrder[i]->genAnyCastForDefaultValueExpr)
                            {

                                checkInsertAnyCastStmts(argCorrectedOrder[i]);
                            }
                        }
                    }
                }
                else if(argCount == 0) //add default parameters when no arguments given
                {
                    for(int i = (isTypeMethod) ? 1 : 0; i < paramCount; i++)
                    {
                        ScopedDeclLL *sdAtIndex = getScopedDeclLLAt(params, i);

                        if(sdAtIndex->item->defExpr != NULL)
                        {
                            argCorrectedOrder[i] = sdAtIndex->item->defExpr;

                            if((globalContext.cc.currStmtBeingChecked != NULL) &&
                                argCorrectedOrder[i]->genFuncRetAsArgForDefaultValueExpr)
                            {
                                checkInsertFuncRetAsArgStmts(argCorrectedOrder[i]);
                            }

                            if((globalContext.cc.currStmtBeingChecked != NULL) && 
                                argCorrectedOrder[i]->genAnyCastForDefaultValueExpr)
                            {
                                checkInsertAnyCastStmts(argCorrectedOrder[i]);
                            }
                        }
                    }
                }

                expr->funcCall.argCallOrder = argCorrectedOrder;
                expr->funcCall.argCallOrderCount = argCorrectedOrderCount;

                free(doneMap);
                expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
                expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                expr->checkType = funcType->funcType.ret;
                if(funcType->funcType.retAsArg)
                {
                    expr->genIdenName = expr->funcCall.retAsArg_argWhichIsRetName = allocNewCompilerIden(RET_AS_ARG_FUNC_CALL_IDEN_PREFIX);

                    if(globalContext.cc.currStmtBeingChecked != NULL)
                    {
                        checkInsertFuncRetAsArgStmts(expr);
                    }
                    else expr->genFuncRetAsArgForDefaultValueExpr = true;
                }
            }
            
            if(!isIncompletePass) expr->hasBeenChecked = true;
            
            globalContext.cc.currFuncCallTypeBeingChecked = prevFuncCallType;
        }break;

        case A_EXPR_STRUCT_LITERAL:
        {
            //TODO Complete;
            ASTType *t = expr->structunionLit.typeName;
            checkType(t);
            if((t != NULL) && isTypeTaggedUnion(t->checkType)) checkInferredTaggedUnionLitExpr((t == NULL) ? NULL : t->checkType, expr, isIncompletePass);
            else checkInferredStructuredLitExpr( (t == NULL) ? NULL : t->checkType, expr, isIncompletePass);
        }break;

        case A_EXPR_ARRAY_LITERAL:
        {
            //TODO Complete;
            checkType(expr->arrayLit.type);
            checkExpr(expr->arrayLit.sizeExpr, isIncompletePass);

            if(expr->arrayLit.sizeExpr != NULL)
            {
                if(expr->arrayLit.sizeExpr->compTimeVal.kind != A_EXPR_COMP_TIME_INT)
                {
                    checkerErrorLn(expr->arrayLit.sizeExpr->startTok, "Expected size given for array literal to be a compile time integer value");
                    prettyPrintCheckerSourceError(expr->arrayLit.sizeExpr->startTok, expr->arrayLit.sizeExpr->endTok);

                    return;
                }
            }
            
            {
                bool sizeInferred = false;
                bool typeInferred = false;

                ASTExpr *sizeExpr = expr->arrayLit.sizeExpr;
                if(sizeExpr== NULL) sizeInferred = true;
                else
                {
                    if(sizeExpr->compTimeVal.kind != A_EXPR_COMP_TIME_INT)
                    {
                        checkerErrorLn(sizeExpr->startTok, "Size given in array literal should be a compile time known integer constant");
                        
                        prettyPrintCheckerSourceError(sizeExpr->startTok, sizeExpr->endTok);
                    }
                }

                CheckerType *base = NULL;
                if(expr->arrayLit.type == NULL) typeInferred = true;
                else base = expr->arrayLit.type->checkType;

                ASTExprLL *exprLL = expr->arrayLit.exprLL;
                size_t arraySize = 0;

                if(exprLL != NULL)
                {
                    if(sizeExpr != NULL)
                    {
                        if(exprLL->first->numItems > sizeExpr->compTimeVal.i)
                        {
                            checkerErrorLn(expr->startTok, "Size given in array literal is '%lld' but number of elements in array literal is '%lld'", sizeExpr->compTimeVal.i, (size_t) exprLL->first->numItems);
                            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                        }
                        
                        if(sizeExpr->compTimeVal.kind == A_EXPR_COMP_TIME_INT) arraySize = sizeExpr->compTimeVal.i;
                    }
                    else arraySize = exprLL->first->numItems;
                }

                bool success = checkInferredArrayLitExpr(base, expr, isIncompletePass);
                
                if(base == NULL) base = exprLL->first->item->checkType;
                if(!success)
                {
                    checkerError(expr->startTok, "Array literal is not a valid literal, expected base to be of type: ");
                    printCheckerType(base);
                    fprintf(stderr, "\n");

                    prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                }

                expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                expr->checkType = newCheckerTypeArray(base, arraySize, false, false);
            }

        }break;

        case A_EXPR_IF:
        {
            checkInferredIfExpr(NULL, expr, false);
        }break;

        case A_EXPR_WHILE:
        {
            if(globalContext.cc.currStmtBeingChecked == NULL)
            {
                checkerErrorLn(expr->startTok, "Using expression somewhere it is not supposed to be used, while expression can only be used in function body");
                prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
            }
            else if(!expr->hasBeenChecked)
            {
                checkStmt(expr->whileExpr.whileStmt);

                //todo check if all if else, else if blocks have the same type of the last expression
                expr->checkType = expr->whileExpr.whileStmt->whileStmt.block->stmts->item->expr.expr->checkType;
                expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

                Token varName = expr->startTok;
                varName.type = TOK_IDEN;
                strcpy(varName.lexeme, expr->whileExpr.idenName);

                SymEntry *entry = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, expr->whileExpr.idenName, expr->checkType, expr->startTok);
                
                ASTExpr *compExpr = newASTExprIden(varName);
                compExpr->checkType = entry->type;
                compExpr->idenSymEntry = entry;

                expr->whileExpr.whileStmt->whileStmt.block->stmts->item = newASTStmtAssign(compExpr, expr->whileExpr.whileStmt->whileStmt.block->stmts->item->expr.expr);

                ASTStmt *varDeclStmt = newASTStmtDecl(newASTDeclVar(newASTExprIden(varName), newASTTypeInfer(varName), NULL));
                varDeclStmt->decl.decl->var.type->checkType = expr->checkType;
                entry->myDecl = varDeclStmt->decl.decl;
                expr->idenSymEntry = entry;

                ASTStmtLL *varDeclToInsert = newASTStmtLL(varDeclStmt);

                ASTStmtLL *whileStmtToInsert = newASTStmtLL(expr->whileExpr.whileStmt);
                insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, varDeclToInsert);
                insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, whileStmtToInsert);
            }

            expr->hasBeenChecked = true;
        }break;

        case A_EXPR_BLOCK:
        {
            if(globalContext.cc.currStmtBeingChecked == NULL)
            {
                checkerErrorLn(expr->startTok, "Using expression somewhere it is not supposed to be used, while expression can only be used in function body");
                prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
            }
            else if(!expr->hasBeenChecked)
            {
                checkBlock(expr->block.b, true);

                //todo check if all if else, else if blocks have the same type of the last expression
                expr->checkType = expr->block.b->stmts->item->expr.expr->checkType;
                expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

                Token varName = expr->startTok;
                varName.type = TOK_IDEN;
                strcpy(varName.lexeme, expr->block.idenName);
                
                SymEntry *entry = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, expr->block.idenName, expr->checkType, expr->startTok);

                ASTExpr *compExpr = newASTExprIden(varName);
                compExpr->checkType = entry->type;
                compExpr->idenSymEntry = entry;

                expr->block.b->stmts->item = newASTStmtAssign(compExpr,  expr->block.b->stmts->item->expr.expr);

                ASTStmt *varDeclStmt = newASTStmtDecl(newASTDeclVar(newASTExprIden(varName), newASTTypeInfer(varName), NULL));
                varDeclStmt->decl.decl->var.type->checkType = expr->checkType;
                entry->myDecl = varDeclStmt->decl.decl;
                expr->idenSymEntry = entry;

                ASTStmtLL *varDeclToInsert = newASTStmtLL(varDeclStmt);

                ASTStmtLL *whileStmtToInsert = newASTStmtLL(newASTStmtBlock(expr->block.b));
                insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, varDeclToInsert);
                insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, whileStmtToInsert);
            }

            expr->hasBeenChecked = true;
        }break;

        case A_EXPR_MATCH:
        {
            checkInferredMatchExpr(NULL, expr, false);
        }break;

        case A_EXPR_EXPR_INTRO:
        {
            checkExpr(expr->exprIntro.expr, isIncompletePass);
            expr->checkType = typeInfoType;
            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
        }break;

        case A_EXPR_TYPE_INTRO:
        {
            checkType(expr->typeIntro.type);
            expr->checkType = typeInfoType;
            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
        }break;
        case A_EXPR_CAST:
        {
            if(expr->cast.castTo != NULL)
            {
                checkType(expr->cast.castTo);

                if(!checkInferredExpr(expr->cast.castTo->checkType, &(expr->cast.expr), false, false, isIncompletePass))
                {
                    checkerErrorLn(expr->startTok, "Invalid cast applied to expression");

                    prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
                }
                else
                {
                    checkCastExpr(expr->cast.castTo->checkType, expr, expr->cast.expr);
                }

                expr->checkType = expr->cast.castTo->checkType;
                expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            }

        }break;
        case A_EXPR_TUPLE_LITERAL:
        {
            checkInferredTupleLitExpr(NULL, expr, isIncompletePass);
        }break;
        case A_EXPR_ASSTRING:
        {
            int *startString;
            size_t len = 0;

            startString = expr->asString.expr->startTok.pos.fileBufPos;
            len = (expr->asString.expr->endTok.pos.fileBufPos) - startString;

            char buf[2048] = {0};
            for(size_t i = 0, writeOffset = 0; i < len; i++)
            {
                char ch = startString[i];
                if(ch == '\"')
                {
                    buf[i + writeOffset] = '\\';
                    writeOffset += 1;
                }

                buf[i + writeOffset] = ch;
            }

            size_t bufStrLen = strlen(buf);
            char *s = malloc(bufStrLen + 1);
            strncpy(s, buf, bufStrLen);

            expr->checkType = stringType;
            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            expr->compTimeVal.kind = A_EXPR_COMP_TIME_STRING;

            expr->compTimeVal.str.data = s;
            expr->compTimeVal.str.len = bufStrLen;
            expr->compTimeVal.str.realLenExcludingEscapedCharacters = getRealLengthOfCompilerStr(s);

        }break;
    }
}
void checkScopeAccessExpr(ASTExpr *expr, bool isIncompletePass, bool isRecursing)
{
    bool temp1 = globalContext.cc.currScopedExpr.isCheckingScopeExpr;
    Token temp2 = globalContext.cc.currScopedExpr.scopeNameBeingChecked;
    globalContext.cc.currScopedExpr.isCheckingScopeExpr = true;

    if((expr->scopeAccess.scopeName->kind != A_EXPR_SCOPE_ACCESS) && (expr->scopeAccess.scopeName->kind != A_EXPR_IDEN))
    {
        checkerErrorLn(expr->startTok, "Invalid use of scope access, the scope expression should be an identifier or another scope access expression");
        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
    }

    if(expr->scopeAccess.scopeName->kind == A_EXPR_IDEN)
    {
        SymEntry *entry = _symTableLookUp(globalContext.cc.checkingLocalsSymTble, expr->scopeAccess.scopeName->iden.lexeme, LOOKUP_ALL);
        
        if(entry == NULL)
        {
            entry = _symTableLookUp(globalContext.symTable, expr->scopeAccess.scopeName->iden.lexeme, LOOKUP_ALL);
            if(entry != NULL)
            {
                if(!isTypeFunction(entry->type)) entry = NULL; //try find operator overload
            }
        }

        if((expr->scopeAccess.scopeName->checkType != NULL) && (expr->scopeAccess.scopeName->idenSymEntry != NULL)) 
        {
            return;
        }

        if(entry != NULL)
        {
            if(entry->mySymTable != NULL)
            {
                if(entry->mySymTable->belongsToNamespace != NULL) 
                {
                    if(entry->isGlobal || entry->isGlobalFunc) entry->mySymTable->belongsToNamespace->isOtherSymbolsUsed = true;
                }
            }

            expr->scopeAccess.scopeName->checkType = entry->type;
            
            if((entry->isNamespace))
            {
                expr->scopeAccess.scopeName->compTimeVal.isL_or_RValue = EXPR_NEITHER_VALUE;
                entry->type->namespaceType.tble->belongsToNamespace->isImported = true;
            }
            else if(!isTypeNamespace(entry->type))
            {
                checkerErrorLn(expr->scopeAccess.scopeName->iden, "identifer '%s' is not a namespace", expr->scopeAccess.scopeName->iden.lexeme);
                prettyPrintCheckerSourceError(expr->scopeAccess.scopeName->startTok, expr->scopeAccess.scopeName->startTok);
                expr->scopeAccess.scopeName->checkType = NULL;
            }

            expr->scopeAccess.scopeName->idenSymEntry = entry;
        }
        else
        {
            checkerErrorLn(expr->scopeAccess.scopeName->iden, "Use of undeclared identifier '%s'", expr->scopeAccess.scopeName->iden.lexeme);
            prettyPrintCheckerSourceError(expr->scopeAccess.scopeName->startTok, expr->scopeAccess.scopeName->startTok);
            expr->scopeAccess.scopeName->checkType = NULL;
        }
    }
    else
    {
        checkScopeAccessExpr(expr->scopeAccess.scopeName, isIncompletePass, true);
    }
    

    if(isTypeNamespace(expr->scopeAccess.scopeName->checkType))
    {
        CheckerType *importType = expr->scopeAccess.scopeName->checkType;
        SymEntry *importEntry = _symTableLookUp(importType->namespaceType.tble, expr->scopeAccess.memb.lexeme, LOOKUP_ALL);

        if(importEntry == NULL)
        {
            checkerError(expr->startTok, "'%s' is not a valid declaration in ", expr->scopeAccess.memb.lexeme);
            printCheckerType(importType);
            fprintf(stderr, "\n");

            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
        }
        else
        {
            if(importEntry->isType) importType->namespaceType.tble->belongsToNamespace->isTypesUsed = true;
            else 
            {
                if(importEntry->mySymTable != NULL)
                {
                    if(importEntry->mySymTable->belongsToNamespace != NULL) 
                    {
                        if(importEntry->isGlobal || importEntry->isGlobalFunc) importEntry->mySymTable->belongsToNamespace->isOtherSymbolsUsed = true;
                    }
                }
            }

            if((importEntry->isNamespace || isTypeNamespace(importEntry->type)) && !isRecursing)
                expr->checkType = namespaceInfoType;
            else expr->checkType = importEntry->type;
            
            if(importEntry->isConst) expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            else if(importEntry->isType) expr->compTimeVal.isL_or_RValue = EXPR_NEITHER_VALUE;
            else if(importEntry->isNamespace) 
            {
                expr->compTimeVal.isL_or_RValue = EXPR_NEITHER_VALUE;
                importEntry->type->namespaceType.tble->belongsToNamespace->isImported = true;

            }
            else //if global and local
            {
                //check if its a function (but not a function pointer), if so,  then set to rvalue
                if(isTypeFunction(expr->checkType))
                {
                    if(importEntry->isGlobalFunc)
                        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                    else expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
                }
                else if(isTypeEnum(expr->checkType) && importEntry->isUnscopedEnumMemb)
                {
                    size_t index = 0;
                    typeHasMember(expr->checkType, importEntry->name, &index);

                    EnumMembLL *memb = getEnumMembLLAt(importEntry->type->enumType.membLL, index);
                    expr->compTimeVal.kind = A_EXPR_COMP_TIME_INT;
                    expr->compTimeVal.i = memb->item->val;
                    expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                }
                else expr->compTimeVal.isL_or_RValue = EXPR_L_VALUE;
            }

            expr->idenSymEntry = importEntry;
        }
    }

    globalContext.cc.currScopedExpr.scopeNameBeingChecked = temp2;
    globalContext.cc.currScopedExpr.isCheckingScopeExpr = temp1;
} 
void checkConvertUnaryExprToOperFuncCall(ASTExpr *expr, SymEntry *operFuncEntry)
{
    Token funcIdenTok = expr->startTok;
    funcIdenTok.type = TOK_IDEN;
    strcpy(funcIdenTok.lexeme, operFuncEntry->name);

    ASTExpr *funcIdenExpr = newASTExprIden(funcIdenTok);
    funcIdenExpr->checkType = operFuncEntry->type;
    funcIdenExpr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
    funcIdenExpr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

    ASTNamedExprLL *args = newASTNamedExprLL(newASTNamedExprNormal(expr->unary.expr));

    expr->kind = A_EXPR_FUNC_CALL;
    expr->funcCall.iden = funcIdenExpr;
    expr->funcCall.argCallOrder = NULL;
    expr->funcCall.argCallOrderCount = 0;
    expr->funcCall.retAsArg_argWhichIsRetName = NULL;
    expr->funcCall.retAsArg_emittedIdenName = false;
    expr->funcCall.args = args;
}
void checkConvertBinaryExprToOperFuncCall(ASTExpr *expr, SymEntry *operFuncEntry)
{
    Token funcIdenTok = expr->startTok;
    funcIdenTok.type = TOK_IDEN;
    strcpy(funcIdenTok.lexeme, operFuncEntry->name);

    ASTExpr *funcIdenExpr = newASTExprIden(funcIdenTok);
    funcIdenExpr->checkType = operFuncEntry->type;
    funcIdenExpr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
    funcIdenExpr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

    ASTNamedExprLL *args = newASTNamedExprLL(newASTNamedExprNormal(expr->binary.l));

    addASTNamedExprLL(&args, newASTNamedExprNormal(expr->binary.r));

    expr->kind = A_EXPR_FUNC_CALL;
    expr->funcCall.iden = funcIdenExpr;
    expr->funcCall.argCallOrder = NULL;
    expr->funcCall.argCallOrderCount = 0;
    expr->funcCall.retAsArg_argWhichIsRetName = NULL;
    expr->funcCall.retAsArg_emittedIdenName = false;
    expr->funcCall.args = args;
}
void checkConvertIndexRefExprToOperFuncCall(ASTExpr *expr, SymEntry *operFuncEntry)
{
    Token funcIdenTok = expr->startTok;
    funcIdenTok.type = TOK_IDEN;
    strcpy(funcIdenTok.lexeme, operFuncEntry->name);

    ASTExpr *funcIdenExpr = newASTExprIden(funcIdenTok);
    funcIdenExpr->checkType = operFuncEntry->type;
    funcIdenExpr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
    funcIdenExpr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

    ASTNamedExprLL *args = newASTNamedExprLL(newASTNamedExprNormal(expr->arrayRef.iden));

    addASTNamedExprLL(&args, newASTNamedExprNormal(expr->arrayRef.index));

    expr->kind = A_EXPR_FUNC_CALL;
    expr->funcCall.iden = funcIdenExpr;
    expr->funcCall.argCallOrder = NULL;
    expr->funcCall.argCallOrderCount = 0;
    expr->funcCall.retAsArg_argWhichIsRetName = NULL;
    expr->funcCall.retAsArg_emittedIdenName = false;
    expr->funcCall.args = args;
}
void checkConvertStructuredEqToFuncCall(ASTExpr *expr, SymEntry *operFuncEntry)
{
    Token funcIdenTok = expr->startTok;
    funcIdenTok.type = TOK_IDEN;
    strcpy(funcIdenTok.lexeme, operFuncEntry->name);

    ASTExpr *funcIdenExpr = newASTExprIden(funcIdenTok);
    funcIdenExpr->checkType = operFuncEntry->type;
    funcIdenExpr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
    funcIdenExpr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;

    ASTNamedExprLL *args = newASTNamedExprLL(newASTNamedExprNormal(expr->binary.l));

    addASTNamedExprLL(&args, newASTNamedExprNormal(expr->binary.r));

    expr->kind = A_EXPR_FUNC_CALL;
    expr->funcCall.iden = funcIdenExpr;
    expr->funcCall.argCallOrder = NULL;
    expr->funcCall.argCallOrderCount = 0;
    expr->funcCall.retAsArg_argWhichIsRetName = NULL;
    expr->funcCall.retAsArg_emittedIdenName = false;
    expr->funcCall.args = args;
}
void checkConvertExprToUnaryExpr(ASTExpr *expr, TokType op)
{
    ASTExpr *inner = allocASTExpr(expr->kind, expr->startTok);
    *inner = *expr;

    expr->kind = A_EXPR_UNARY;
    expr->unary.expr = inner;
    expr->unary.op = expr->startTok;
    expr->unary.op.type = op;
}
void checkInsertCastExpr(CheckerType *castTo, ASTExpr **exprToCast)
{
    ASTExpr *e = *exprToCast;

    *exprToCast = newASTExprCast(NULL, e);
    (*exprToCast)->checkType = castTo;
    (*exprToCast)->endTok = e->endTok;
    (*exprToCast)->compTimeVal = e->compTimeVal;

    checkCastExpr(castTo, *exprToCast, e);
}
void checkCastExpr(CheckerType *castTo, ASTExpr *wholeExpr, ASTExpr *exprToCast)
{
    if(isTypeStruct(castTo) || isTypeUnion(castTo))
    {
        wholeExpr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
    }
    else if(exprToCast->compTimeVal.kind != A_EXPR_COMP_TIME_RUNTIME)
    {
        switch(exprToCast->compTimeVal.kind)
        {
            case A_EXPR_COMP_TIME_BOOL: break; //bool comptime is stored as int so its fine
            case A_EXPR_COMP_TIME_INT:
            {
                if(isTypeFloat(castTo))
                {
                    wholeExpr->compTimeVal.f = exprToCast->compTimeVal.i;
                    wholeExpr->compTimeVal.kind = A_EXPR_COMP_TIME_FLOAT;
                }
            }break;
        }
    }
}
bool checkInferredExpr(CheckerType *inferredType, ASTExpr **expr, bool shouldInsertCast, bool shouldCheckImplicitCast, bool isIncompletePass)
{
    ASTExpr *e = *expr;
    bool castExpression = shouldInsertCast;

    switch(e->kind)
    {
        case A_EXPR_ARRAY_LITERAL:
        {
            if(areTypesEqual(inferredType, anyType))
            {
                checkExpr(*expr, isIncompletePass);

                if(inferredType->typeFilters != NULL)
                {
                    if(!canImplicitCast((*expr)->checkType, inferredType)) return false;
                }
            }
            else if(isTypeArray(inferredType) || (inferredType == NULL))
            {
                CheckerType *inferArrType = (isTypeAliased(inferredType)) ? getAliasedTypeBase(inferredType) : inferredType;

                bool typeInferred = false;

                if(inferArrType == NULL)  typeInferred = true;

                CheckerType *baseType = (typeInferred) ? NULL : inferArrType->arrayType.base;
                bool sizeInferred = false;

                ASTExpr *sizeExpr = e->arrayLit.sizeExpr;
                if(sizeExpr == NULL) sizeInferred = true;
                else 
                {
                    checkExpr(sizeExpr, isIncompletePass);

                    if(sizeExpr->compTimeVal.kind != A_EXPR_COMP_TIME_INT)
                    {
                        checkerErrorLn(sizeExpr->startTok, "Size given in array literal should be a compile time known integer constant");
                        
                        prettyPrintCheckerSourceError(sizeExpr->startTok, sizeExpr->endTok);
                    }
                }

                ASTExprLL *exprLL = e->arrayLit.exprLL;
                size_t arraySize = 0;

                if(exprLL != NULL)
                {
                    if(sizeExpr != NULL)
                    {
                        if(exprLL->first->numItems > sizeExpr->compTimeVal.i)
                        {
                            checkerErrorLn(e->startTok, "Size given in array literal is '%lld' but number of elements in array literal is '%lld'", sizeExpr->compTimeVal.i, (size_t) exprLL->first->numItems);
                            prettyPrintCheckerSourceError(e->startTok, e->endTok);
                        }
                        
                        if(sizeExpr->compTimeVal.kind == A_EXPR_COMP_TIME_INT) arraySize = sizeExpr->compTimeVal.i;
                    }
                    else arraySize = exprLL->first->numItems;
                }
                //check each item in array literal
                {
                    if(checkInferredArrayLitExpr(baseType, e, isIncompletePass))
                    {
                        e->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
                        if(baseType == NULL) baseType = exprLL->first->item->checkType;

                        e->checkType = newCheckerTypeArray(baseType, arraySize, false, false);
                        if(!isTypeSliceArray(inferArrType) && (inferArrType != NULL))
                        {
                            if(!areTypesEqual(e->checkType, inferArrType))
                            {
                                checkerError(e->startTok, "Type for array literal is: ");
                                printCheckerType(e->checkType);
                                fprintf(stderr, ", but required type is: ");
                                printCheckerType(inferArrType);
                                fprintf(stderr, "\n");

                                prettyPrintCheckerSourceError(e->startTok, e->endTok);
                            }
                        }
                    }
                    else
                    {
                        checkerError(e->startTok, "Expected type to be ");
                        printCheckerType(inferArrType);
                        fprintf(stderr, "\n");

                        prettyPrintCheckerSourceError(e->startTok, e->endTok);
                        return false;
                    }
                }
            }
            else if(!isTypeArray(inferredType)) return false;
        }break;
        
        case A_EXPR_STRUCT_LITERAL:
        {
            if(isTypeTaggedUnion(inferredType))
            {
                if(!checkInferredTaggedUnionLitExpr(inferredType, *expr, isIncompletePass)) return false;
            }
            else if(!checkInferredStructuredLitExpr(inferredType, *expr, isIncompletePass)) return false;
        }break;
        case A_EXPR_IF:
        {
            if(!checkInferredIfExpr(inferredType, *expr, isIncompletePass)) return false;
        }break;
        case A_EXPR_MATCH:
        {
            if(!checkInferredMatchExpr(inferredType, *expr, isIncompletePass)) return false;
        }break;
        case A_EXPR_TUPLE_LITERAL:
        {
            if(areTypesEqual(inferredType, anyType)) checkExpr(*expr, isIncompletePass);
            else if(!(checkInferredTupleLitExpr(inferredType, *expr, isIncompletePass))) return false;
        }break;
        default:
        {
            checkExpr(e, isIncompletePass);
            if((inferredType != NULL) && !canImplicitCast(e->checkType, inferredType) && shouldCheckImplicitCast)
            {
                bool canCast = false;

                if(isTypeInteger(inferredType) && isTypeInteger(e->checkType) && (e->compTimeVal.kind == A_EXPR_COMP_TIME_INT))
                {
                    CheckerType *iType = isTypeAliased(inferredType) ? getAliasedTypeBase(inferredType) : inferredType;
                    if(!canImplicitCastValueToInt(iType, e->compTimeVal.i))
                    {
                        checkerError(e->startTok, "Cannot cast integer literal '%lld' to type: ", e->compTimeVal.i);
                        printCheckerType(inferredType);
                        fprintf(stderr, "\n");

                        prettyPrintCheckerSourceError(e->startTok, e->endTok);
                        return false;
                    }
                    else canCast = true;
                }

                if(!canCast) return false;
            }
        }break;
    }

    if((inferredType != NULL) && !areTypesEqual(inferredType, e->checkType) && castExpression)
    {
        {
        // *expr = newASTExprCast(NULL, e);
        // (*expr)->checkType = inferredType;
        // (*expr)->endTok = e->endTok;
        // (*expr)->compTimeVal = e->compTimeVal;

        // if(isTypeStruct(inferredType) || isTypeUnion(inferredType))
        // {
        //     (*expr)->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
        // }
        // else if(e->compTimeVal.kind != A_EXPR_COMP_TIME_RUNTIME)
        // {
        //     switch(e->compTimeVal.kind)
        //     {
        //         case A_EXPR_COMP_TIME_BOOL: break; //bool comptime is stored as int so its fine
        //         case A_EXPR_COMP_TIME_INT:
        //         {
        //             if(isTypeFloat(inferredType))
        //             {
        //                 (*expr)->compTimeVal.f = e->compTimeVal.i;
        //                 (*expr)->compTimeVal.kind = A_EXPR_COMP_TIME_FLOAT;
        //             }
        //         }break;
        //     }
        // }

        // if((inferredType == anyType) && (e->compTimeVal.isL_or_RValue == EXPR_R_VALUE))
        // {
        //     char *anyLitName = allocNewAnyLitIden();
            
        //     if(cc.currStmtBeingChecked != NULL)
        //     {
        //         Token varName = e->startTok;
        //         varName.type = TOK_IDEN;
        //         strcpy(varName.lexeme, anyLitName);

        //         ASTStmt *varDeclStmt = newASTStmtDecl(newASTDeclVar(varName, newASTTypeInfer(varName), e));
        //         varDeclStmt->decl.decl->var.type->checkType = e->checkType;

        //         ASTStmtLL *varDeclToInsert = newASTStmtLL(varDeclStmt);

        //         insertBeforeASTStmtLL(cc.currStmtBeingChecked, varDeclToInsert);
        //     }
        //     else (*expr)->genAnyCastForDefaultValueExpr = true; //for expressiosn which are default values for params and struct membs

        //     e->genIdenName = anyLitName;
        // }
        }
        checkInsertCastExpr(inferredType, expr);
    }
    return true;
}
bool checkInferredIfExpr(CheckerType *inferredtype, ASTExpr *expr, bool isIncomplete)
{
    bool success = true;
    if(globalContext.cc.currStmtBeingChecked == NULL)
    {
        checkerErrorLn(expr->startTok, "Using expression somewhere it is not supposed to be used, if expression can only be used in function body");
        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
        success = false;
    }
    else if(!expr->hasBeenChecked)
    {
        ASTStmt *ifStmtInExpr = expr->ifExpr.ifStmt;

        checkStmt(ifStmtInExpr);

        CheckerType *typeExpectedFromAllBlocks = inferredtype;

        if(typeExpectedFromAllBlocks == NULL)
        {
            checkExpr(ifStmtInExpr->ifStmt.block->stmts->item->expr.expr, isIncomplete);
            typeExpectedFromAllBlocks = ifStmtInExpr->ifStmt.block->stmts->item->expr.expr->checkType; 
        }
        else if(!checkRHSExprWithTypeAndCast(typeExpectedFromAllBlocks, &(ifStmtInExpr->ifStmt.block->stmts->item->expr.expr), true, isIncomplete))
        {
            checkerError(ifStmtInExpr->ifStmt.block->stmts->item->startTok, "Last expression in block of if expression, cannot be cast or inferred to required type: ");
            printCheckerType(typeExpectedFromAllBlocks);
            fprintf(stderr, " instead got: ");
            printCheckerType(ifStmtInExpr->ifStmt.block->stmts->item->expr.expr->checkType);
            fprintf(stderr, "\n");

            prettyPrintCheckerSourceError(ifStmtInExpr->ifStmt.block->stmts->item->startTok,ifStmtInExpr->ifStmt.block->stmts->item->endTok);

            success = false;
        }

        bool doesNeedElseBranch = true;
        if((ifStmtInExpr->ifStmt.cond->compTimeVal.kind == A_EXPR_COMP_TIME_BOOL))
        {
            bool condVal = ifStmtInExpr->ifStmt.cond->compTimeVal.i;
            if(condVal) doesNeedElseBranch = false;
        }

        if(doesNeedElseBranch && (ifStmtInExpr->ifStmt.elseBlock == NULL))
        {
            checkerErrorLn(ifStmtInExpr->startTok, "Expected else branch in if expression, since the condition does not evaluate to true");

            prettyPrintCheckerSourceError(ifStmtInExpr->startTok, ifStmtInExpr->endTok);
            success = false;
        }

        ASTBlock *elseBlock = ifStmtInExpr->ifStmt.elseBlock;
        ASTStmt *nextStmtToCheck = (elseBlock == NULL) ? NULL : ((elseBlock->stmts == NULL) ? NULL : elseBlock->stmts->item);

        if(nextStmtToCheck != NULL)
        {
            if(!checkRHSExprWithTypeAndCast(typeExpectedFromAllBlocks, &nextStmtToCheck->expr.expr, true, isIncomplete))
            {
                checkerError(nextStmtToCheck->expr.expr->startTok, "Expressions type does not match the expected type, got ");

                printCheckerType(nextStmtToCheck->expr.expr->checkType);
                fprintf(stderr, " but expected ");
                printCheckerType(typeExpectedFromAllBlocks);
                fprintf(stderr, " in the if expression, the first blocks final expressions type is taken to be the expected type.\n");

                prettyPrintCheckerSourceError(nextStmtToCheck->expr.expr->startTok, nextStmtToCheck->expr.expr->endTok);

                success = false;
            }
        }

        expr->checkType = typeExpectedFromAllBlocks;
        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
        expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
        
        Token varName = expr->startTok;
        varName.type = TOK_IDEN;
        strcpy(varName.lexeme, expr->ifExpr.idenName);

        SymEntry *entry = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, expr->ifExpr.idenName, typeExpectedFromAllBlocks, expr->startTok);

        ASTExpr *thenBlockAssignLHS = newASTExprIden(varName);
        thenBlockAssignLHS->checkType = entry->type;
        thenBlockAssignLHS->idenSymEntry = entry;

        ifStmtInExpr->ifStmt.block->stmts->item = newASTStmtAssign(thenBlockAssignLHS, ifStmtInExpr->ifStmt.block->stmts->item->expr.expr);

        if(nextStmtToCheck != NULL) 
        {
            ASTExpr *elseBlockAssignLHS = newASTExprIden(varName);
            elseBlockAssignLHS->checkType = entry->type;
            elseBlockAssignLHS->idenSymEntry = entry;
            elseBlock->stmts->item = newASTStmtAssign(elseBlockAssignLHS, nextStmtToCheck->expr.expr);
        }

        ASTStmt *varDeclStmt = newASTStmtDecl(newASTDeclVar(newASTExprIden(varName), newASTTypeInfer(varName), NULL));
        varDeclStmt->decl.decl->var.type->checkType = expr->checkType;
        entry->myDecl = varDeclStmt->decl.decl;

        expr->idenSymEntry = entry;

        ASTStmtLL *varDeclToInsert = newASTStmtLL(varDeclStmt);

        ASTStmtLL *ifStmtToInsert = newASTStmtLL(expr->ifExpr.ifStmt);
        insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, varDeclToInsert);
        insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, ifStmtToInsert);
    }

    expr->hasBeenChecked = true;

    return success;
}
bool checkInferredMatchExpr(CheckerType *inferredType, ASTExpr *expr, bool isIncomplete)
{
    bool success = true;

    if(globalContext.cc.currStmtBeingChecked == NULL)
    {
        checkerErrorLn(expr->startTok, "Using expression somewhere it is not supposed to be used, match expression can only be used in function body");
        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);
        success = false;
    }
    else if(!expr->hasBeenChecked)
    {
        checkStmt(expr->match.matchStmt);

        if(expr->match.matchStmt->match.matchArmLL != NULL)
        {
            ASTMatchArmLL *currCheckedArm = expr->match.matchStmt->match.matchArmLL->first;

            CheckerType *typeExpectedFromAllBlocks = inferredType;

            if(typeExpectedFromAllBlocks == NULL)
            {
                typeExpectedFromAllBlocks = currCheckedArm->item->block->stmts->item->expr.expr->checkType; 
            }
            else if(!checkRHSExprWithTypeAndCast(typeExpectedFromAllBlocks, &(currCheckedArm->item->block->stmts->item->expr.expr), true, isIncomplete))
            {
                checkerError(currCheckedArm->item->startTok, "Last expression in block of arm, cannot be cast or inferred to required type: ");
                printCheckerType(typeExpectedFromAllBlocks);
                fprintf(stderr, " instead got: ");
                printCheckerType(currCheckedArm->item->block->stmts->item->expr.expr->checkType);
                fprintf(stderr, "\n");

                prettyPrintCheckerSourceError(currCheckedArm->item->startTok, currCheckedArm->item->endTok);

                success = false;
            }

            ASTStmt *nextStmtToCheck = currCheckedArm->first->item->block->stmts->item;

            Token varName = expr->startTok;
            varName.type = TOK_IDEN;
            strcpy(varName.lexeme, expr->match.idenName);

            SymEntry *entry = _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, expr->match.idenName, typeExpectedFromAllBlocks, expr->startTok);

            while(nextStmtToCheck != NULL)
            {
                if(!checkRHSExprWithTypeAndCast(typeExpectedFromAllBlocks, &nextStmtToCheck->expr.expr, true, isIncomplete))
                {
                    checkerError(nextStmtToCheck->expr.expr->startTok, "Expressions type does not match the expected type, got ");

                    printCheckerType(nextStmtToCheck->expr.expr->checkType);
                    fprintf(stderr, " but expected ");
                    printCheckerType(typeExpectedFromAllBlocks);
                    fprintf(stderr, " in the match expression, the first arms block final expression statement type is taken to be the expected type.\n");

                    prettyPrintCheckerSourceError(currCheckedArm->item->startTok, currCheckedArm->item->endTok);

                    success = false;
                }
                
                ASTExpr *assignLHSExpr = newASTExprIden(varName);
                assignLHSExpr->checkType = typeExpectedFromAllBlocks;
                assignLHSExpr->idenSymEntry = entry;
                currCheckedArm->item->block->stmts->item = newASTStmtAssign(assignLHSExpr, currCheckedArm->item->block->stmts->item->expr.expr);
                
                currCheckedArm = currCheckedArm->next;
                if(currCheckedArm != NULL) nextStmtToCheck = currCheckedArm->item->block->stmts->item;
                else nextStmtToCheck = NULL;
            }

            expr->checkType = typeExpectedFromAllBlocks;
            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
            expr->idenSymEntry = entry;

            ASTStmt *varDeclStmt = newASTStmtDecl(newASTDeclVar(newASTExprIden(varName), newASTTypeInfer(varName), NULL));
            varDeclStmt->decl.decl->var.type->checkType = expr->checkType;

            entry->myDecl = varDeclStmt->decl.decl;

            ASTStmtLL *varDeclToInsert = newASTStmtLL(varDeclStmt);

            ASTStmtLL *matchStmtToInsert = newASTStmtLL(expr->match.matchStmt);
            insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, varDeclToInsert);
            insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, matchStmtToInsert);
        }
    }

    expr->hasBeenChecked = true;
    return success;
}
bool checkInferredArrayLitExpr(CheckerType *inferredBaseType, ASTExpr *expr, bool isIncompletePass)
{
    ASTExprLL *exprLL = expr->arrayLit.exprLL;
    bool invalidArrayLit = false;

    if(exprLL != NULL)
    {
        exprLL = exprLL->first;
        while(exprLL != NULL)
        {
            bool wasValidElement = checkRHSExprWithTypeAndCast(inferredBaseType, &(exprLL->item), true, isIncompletePass);
            if(inferredBaseType != NULL)
            {
                invalidArrayLit |= !wasValidElement;

                if(invalidArrayLit)
                {
                    checkerError(exprLL->item->startTok, "Expected array element to have type: ");
                    printCheckerType(inferredBaseType);
                    fprintf(stderr, ", but got type: ");
                    printCheckerType(exprLL->item->checkType);
                    fprintf(stderr, " instead\n");

                    prettyPrintCheckerSourceError(exprLL->item->startTok, exprLL->item->endTok);
                }
            }

            if(inferredBaseType == NULL) inferredBaseType = exprLL->item->checkType;

            exprLL = exprLL->next;
        }
    }

    return !invalidArrayLit;
}
bool checkInferredStructuredLitExpr(CheckerType *inferredType, ASTExpr *expr, bool isIncompletePass)
{
    bool success = true;
    if((expr->structunionLit.typeName != NULL) && (expr->structunionLit.typeName->checkType == NULL))
    {
        checkType(expr->structunionLit.typeName);
    }
    if((inferredType != NULL) && (expr->structunionLit.typeName != NULL))
    {
        if(!canImplicitCast(expr->structunionLit.typeName->checkType, inferredType))
        {
            // checkerError(expr->startTok, "Got struct/union type: ");
            // printCheckerType(expr->structunionLit.typeName->checkType);
            // fprintf(stderr, " , but wanted type: ");
            // printCheckerType(inferredType);
            // fprintf(stderr, "\n");

            // prettyPrintCheckerSourceError(expr->startTok, expr->endTok);

            success = false;
        }
        
    }
    else if((inferredType == NULL) && (expr->structunionLit.typeName == NULL))
    {
        checkerErrorLn(expr->startTok, "Cannot deduce the type of the struct/union literal, append the literal with a struc/union name");

        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);

        return false;
    }

    CheckerType *litType = (expr->structunionLit.typeName == NULL) ? inferredType : expr->structunionLit.typeName->checkType;
    
    if(isTypeTaggedUnion(litType)) return checkInferredTaggedUnionLitExpr(inferredType, expr, isIncompletePass);

    if(!isTypeStruct(litType) && !isTypeUnion(litType))
    {
        checkerError(expr->startTok, "Invalid use of struct/union literal, type given is not a struct or union but ");
        printCheckerType(litType);
        fprintf(stderr, "\n");

        success = false;
    }
    else 
    {
        //TODO: complete
        bool isUnion = isTypeUnion(litType);
        bool isStruct = isTypeStruct(litType);
        
        CheckerType *type = (isTypeAliased(litType)) ? getAliasedTypeBase(litType) : litType;
        char *typeName = (isUnion) ? type->unionType.name : type->structType.name;            
        ScopedDeclLL *decls = (isUnion) ? type->unionType.declLL : type->structType.declLL;
        size_t membCount = (decls == NULL) ? 0 : decls->first->numItems;
        size_t gotNumExprs = (expr->structunionLit.exprLL == NULL) ? 0 : expr->structunionLit.exprLL->first->numItems;

        bool *doneMap = calloc(membCount, sizeof(bool));
        size_t membExprOrderCount = (membCount < gotNumExprs) ? gotNumExprs : membCount;
        ASTNamedExpr **membExprOrder = calloc(membExprOrderCount, sizeof(ASTExpr *));

        if(!isIncompletePass && (expr->structunionLit.exprLL != NULL))
        {
            if(gotNumExprs > membCount)
            {
                checkerErrorLn(expr->startTok, "got %d expression(s) in struct/union literal but struct/union %s only has %d member(s)",
                                gotNumExprs, membCount);

                success = false;
            }
            else
            {
                ASTNamedExprLL *exprLL = expr->structunionLit.exprLL->first;
                size_t index = 0;

                while(exprLL != NULL)
                {
                    ASTNamedExpr *namedExpr = exprLL->item;
                    ASTNamedExpr *exprToAddToMembExprOrder = NULL;

                    switch(namedExpr->kind)
                    {
                        case A_NAMED_EXPR_NORMAL:
                        {                                    
                            ScopedDeclLL *sdAtIndex = getScopedDeclLLAt(decls, index);

                            if(CHECK_SCOPED_DECL_FLAG(sdAtIndex->item, SCOPED_DECL_CONST) && CHECK_SCOPED_DECL_FLAG(sdAtIndex->item, SCOPED_DECL_HAS_DEF_VALUE))
                            {
                                checkerErrorLn(namedExpr->startTok, "Member '%s' (%s: (%d: %d)) in struct/union '%s', is declared as having [constant] tag and already has a default value",
                                                sdAtIndex->item->name.lexeme, sdAtIndex->item->name.pos.filename, sdAtIndex->item->name.pos.linenum, 
                                                sdAtIndex->item->name.pos.colnum, typeName);

                                fprintf(stderr, "        Member definition: \n");
                                prettyPrintCheckerSourceError(sdAtIndex->item->name, sdAtIndex->item->name);

                                fprintf(stderr, "        struct literal: \n");
                                prettyPrintCheckerSourceError(namedExpr->normal->startTok, namedExpr->normal->endTok);

                                success = false;
                            }

                            if(!checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &namedExpr->normal, true, isIncompletePass))
                            {
                                checkerError(namedExpr->startTok, "Type mismatch, member '%s' (%s: (%d: %d)) in struct/union '%s', is of type: ",
                                                sdAtIndex->item->name.lexeme, sdAtIndex->item->name.pos.filename, sdAtIndex->item->name.pos.linenum, 
                                                sdAtIndex->item->name.pos.colnum, typeName);
                                printCheckerType(sdAtIndex->item->type);

                                fprintf(stderr, " but got type: ");
                                printCheckerType(namedExpr->normal->checkType);
                                fprintf(stderr, " for named expression in struct/union literal\n");

                                success = false;
                            }
                            
                            if(doneMap[index] == true)
                            {
                                checkerErrorLn(namedExpr->startTok, "Member '%s' (%s: (%d: %d)) in struct/union '%s' has already been given in struct/union literal",
                                                sdAtIndex->item->name.lexeme, sdAtIndex->item->name.pos.filename, sdAtIndex->item->name.pos.linenum, 
                                                sdAtIndex->item->name.pos.colnum, typeName);

                                success = false;
                            }

                            exprToAddToMembExprOrder = namedExpr;
                            membExprOrder[index] = exprToAddToMembExprOrder;

                        }break;
                        case A_NAMED_EXPR_NAMED:
                        {
                            size_t indexInStructMemb = 0;
                            if(typeHasMember(type, namedExpr->named.name.lexeme, &indexInStructMemb))
                            {                                    
                                ScopedDeclLL *sdAtIndex = getScopedDeclLLAt(decls, indexInStructMemb);

                                if(CHECK_SCOPED_DECL_FLAG(sdAtIndex->item, SCOPED_DECL_CONST) && CHECK_SCOPED_DECL_FLAG(sdAtIndex->item, SCOPED_DECL_HAS_DEF_VALUE))
                                {
                                    checkerErrorLn(namedExpr->startTok, "Member '%s' (%s: (%d: %d)) in struct/union '%s', is declared as having [constant] tag and already has a default value",
                                                sdAtIndex->item->name.lexeme, sdAtIndex->item->name.pos.filename, sdAtIndex->item->name.pos.linenum, 
                                                sdAtIndex->item->name.pos.colnum, typeName);

                                    fprintf(stderr, "        Member definition: \n");
                                    prettyPrintCheckerSourceError(sdAtIndex->item->name, sdAtIndex->item->name);

                                    fprintf(stderr, "        struct literal: \n");
                                    prettyPrintCheckerSourceError(namedExpr->named.expr->startTok, namedExpr->named.expr->endTok);

                                    success = false;
                                }

                                if(!checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &namedExpr->named.expr, true, isIncompletePass))
                                {
                                    checkerError(namedExpr->startTok, "Type mismatch, member '%s' (%s: (%d: %d)) in struct/union '%s', is of type: ",
                                                sdAtIndex->item->name.lexeme, sdAtIndex->item->name.pos.filename, sdAtIndex->item->name.pos.linenum, 
                                                sdAtIndex->item->name.pos.colnum, typeName);
                                    printCheckerType(sdAtIndex->item->type);

                                    fprintf(stderr, " but got type: ");
                                    printCheckerType(namedExpr->named.expr->checkType);
                                    fprintf(stderr, " for named expression in struct/union literal\n");

                                    success = false;
                                }
                                
                                if(doneMap[indexInStructMemb] == true)
                                {
                                    checkerErrorLn(namedExpr->startTok, "Member '%s' (%s: (%d: %d)) in struct/union '%s' has already been given in struct/union literal",
                                                sdAtIndex->item->name.lexeme, sdAtIndex->item->name.pos.filename, sdAtIndex->item->name.pos.linenum, 
                                                sdAtIndex->item->name.pos.colnum, typeName);
                                    
                                    success = false;
                                }

                                exprToAddToMembExprOrder = namedExpr;
                                membExprOrder[indexInStructMemb] = exprToAddToMembExprOrder;
                                //carry on checkign expression of members fr    om the poitn it left off, for example
                                // if struct s1,  has members a, b, c and u do s1{b = 90};
                                //it woill carry on checking from c NOT a.
                                index = indexInStructMemb;
                            }
                            else 
                            {
                                checkerErrorLn(namedExpr->startTok, "Member '%s' does not exist in struct/union '%s'", namedExpr->named.name.lexeme, typeName);
                                success = false;
                            }

                        }break;

                    }

                    doneMap[index] = true;

                    index++;
                    exprLL = exprLL->next;
                }                                             
            }
        }

        for(int i = 0; i < membCount; i++)
        {
            ScopedDecl *d = getScopedDeclLLAt(decls, i)->item;

            if(CHECK_SCOPED_DECL_FLAG(d, SCOPED_DECL_VALUE_REQUIRED) && !doneMap[i])
            {
                checkerErrorLn(expr->startTok, "Member '%s' of struct/union '%s' is declared as having the [required] tag, which means it must be given a value", d->name.lexeme, typeName);

                fprintf(stderr, "        Member definition: \n");
                prettyPrintCheckerSourceError(d->name, d->name);

                fprintf(stderr, "        struct literal: \n");
                prettyPrintCheckerSourceError(expr->startTok, expr->endTok);

                success = false;
            }
            else if((d->defExpr != NULL) && !doneMap[i])
            {
                membExprOrder[i] = newASTNamedExprNamed(d->name, d->defExpr);

                if((globalContext.cc.currStmtBeingChecked != NULL) &&
                    d->defExpr->genFuncRetAsArgForDefaultValueExpr)
                {
                    checkInsertFuncRetAsArgStmts(d->defExpr);
                }

                if((globalContext.cc.currStmtBeingChecked != NULL) && 
                    d->defExpr->genAnyCastForDefaultValueExpr)
                {
                    checkInsertAnyCastStmts(d->defExpr);
                }
            }
        }
        
        expr->structunionLit.membExprOrder = membExprOrder;
        expr->structunionLit.membExprOrderCount = membExprOrderCount;
        free(doneMap);

        expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
        expr->checkType = litType;
        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
    }

    return success;
}
bool checkInferredTaggedUnionLitExpr(CheckerType *inferredType, ASTExpr *expr, bool isIncompletePass)
{
    
    bool success = true;
    if((expr->structunionLit.typeName != NULL) && (expr->structunionLit.typeName->checkType == NULL))
    {
        checkType(expr->structunionLit.typeName);
    }
    if((inferredType != NULL) && (expr->structunionLit.typeName != NULL))
    {
        if(!canImplicitCast(expr->structunionLit.typeName->checkType, inferredType))
        {
            checkerError(expr->startTok, "Got struct/union type: ");
            printCheckerType(expr->structunionLit.typeName->checkType);
            fprintf(stderr, " , but wanted type: ");
            printCheckerType(inferredType);
            fprintf(stderr, "\n");

            prettyPrintCheckerSourceError(expr->startTok, expr->endTok);

            success = false;
        }
        
    }
    else if((inferredType == NULL) && (expr->structunionLit.typeName == NULL))
    {
        checkerErrorLn(expr->startTok, "Cannot deduce the type of the struct/union literal, append the literal with a struc/union name");

        prettyPrintCheckerSourceError(expr->startTok, expr->endTok);

        return false;
    }

    CheckerType *litType = (expr->structunionLit.typeName == NULL) ? inferredType : expr->structunionLit.typeName->checkType;

    if(!isTypeTaggedUnion(litType))
    {
        checkerError(expr->startTok, "Invalid use of tagged union literal, type given is not a tagged union but ");
        printCheckerType(litType);
        fprintf(stderr, "\n");

        success = false;
    }
    else 
    {
        CheckerType *type = (isTypeAliased(litType)) ? getAliasedTypeBase(litType) : litType;
        char *typeName = type->taggedUnionType.name;            
        ScopedDeclLL *decls = type->taggedUnionType.declLL;
        size_t membCount = (decls == NULL) ? 0 : decls->first->numItems;
        size_t gotNumExprs = (expr->structunionLit.exprLL == NULL) ? 0 : expr->structunionLit.exprLL->first->numItems;

        bool *doneMap = calloc(membCount, sizeof(bool));
        size_t membExprOrderCount = (membCount < gotNumExprs) ? gotNumExprs : membCount;
        ASTNamedExpr **membExprOrder = calloc(membExprOrderCount, sizeof(ASTExpr *));

        if(!isIncompletePass && (expr->structunionLit.exprLL != NULL))
        {
            if(gotNumExprs > 1)
            {
                checkerErrorLn(expr->startTok, "got %d expressions in tagged union literal, but tagged union literals can only have 1 value in the literal",
                                                gotNumExprs);
                success = false;
            }
            else
            {
                ASTNamedExprLL *exprLL = expr->structunionLit.exprLL->first;
                size_t index = 0;

                ASTNamedExpr *namedExpr = exprLL->item;
                ASTNamedExpr *exprToAddToMembExprOrder = NULL;

                switch(namedExpr->kind)
                {
                    case A_NAMED_EXPR_NORMAL:
                    {
                        checkExpr(namedExpr->normal, isIncompletePass);
                        
                        CheckerType *exprType = namedExpr->normal->checkType;

                        size_t indexInTaggedUnion = 0;
                        bool castableFound = false;
                        bool success = taggedUnionTypeGetBestMember(type, exprType, &castableFound, &indexInTaggedUnion);

                        if(success)
                        {
                            ScopedDeclLL *sdAtIndex = getScopedDeclLLAt(type->taggedUnionType.declLL, indexInTaggedUnion);
                            if(castableFound)
                            {
                                checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &namedExpr->normal, true, isIncompletePass);
                            }

                            membExprOrder[indexInTaggedUnion] = namedExpr;
                        }
                        else
                        {
                            checkerErrorLn(namedExpr->normal->startTok, "Expression given in tagged union literal cannot be cast to any of the members in tagged union '%s'", typeName);
                            prettyPrintCheckerSourceError(namedExpr->normal->startTok, namedExpr->normal->startTok);
                        }
                    }break;

                    case A_NAMED_EXPR_NAMED:
                    {
                        checkExpr(namedExpr->named.expr, isIncompletePass);

                        CheckerType *exprType = namedExpr->named.expr->checkType;
                        
                        size_t indexInTaggedUnion = 0;
                        if(typeHasMember(type, namedExpr->named.name.lexeme, &indexInTaggedUnion))
                        {
                            ScopedDeclLL *sdAtIndex = getScopedDeclLLAt(type->taggedUnionType.declLL, indexInTaggedUnion);
                            if(!checkRHSExprWithTypeAndCast(sdAtIndex->item->type, &namedExpr->named.expr, true, isIncompletePass))
                            {
                                checkerError(namedExpr->named.expr->startTok, "Type given in tagged union does not match type of member '%s', type give: ", namedExpr->named.name.lexeme);
                                printCheckerType(namedExpr->named.expr->checkType);
                                fprintf(stderr, ", but expected: ");
                                printCheckerType(sdAtIndex->item->type);
                                fprintf(stderr, "\n");

                                prettyPrintCheckerSourceError(namedExpr->named.expr->startTok, namedExpr->named.expr->endTok);
                            }
                            else membExprOrder[indexInTaggedUnion] = namedExpr;
                        }
                        else 
                        {
                            checkerErrorLn(namedExpr->startTok, "Member '%s' does not exist in tagged union '%s'", namedExpr->named.name.lexeme, typeName);

                            prettyPrintCheckerSourceError(namedExpr->startTok, namedExpr->startTok);
                        }

                    }break;
                } 
            }
        }
        
        expr->structunionLit.membExprOrder = membExprOrder;
        expr->structunionLit.membExprOrderCount = membExprOrderCount;
        free(doneMap);

        expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
        expr->checkType = litType;
        expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
    }

    return success;
}
bool checkInferredTupleLitExpr(CheckerType *inferredType, ASTExpr *expr, bool isIncompletePass)
{
   // if(expr->hasBeenChecked) return true;

    CheckerType *tupleType = inferredType;
    bool success = true;
    bool noInferredTypeGiven = false;

    if(tupleType == NULL) noInferredTypeGiven = true;
    else if(!isTypeTuple(inferredType)) success = false;
    
    if(!success || noInferredTypeGiven)
    {
        {   //loop through expressions in tuple lit

            ASTExprLL *currExprLL = expr->tupleLit.exprLL->first;

            //so tht the tuple lits type can be given in errors
            //even if the inferred type is not right
            CheckerTypeLL *tupleLitTypes = NULL;

            while(currExprLL != NULL)
            {
                if(isExprDiscardIdentifier(currExprLL->item))
                {
                    currExprLL = currExprLL->next;
                    
                    continue;
                }
                
                checkExpr(currExprLL->item, isIncompletePass);

                if(tupleLitTypes == NULL) tupleLitTypes = newCheckerTypeLL(currExprLL->item->checkType);
                else addCheckerTypeLL(&tupleLitTypes, currExprLL->item->checkType);

                currExprLL = currExprLL->next;
            }
            expr->checkType = checkIfTupleNotExistsCreateNew(tupleLitTypes);

            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
        }
    }
    else if(isTypeTuple(inferredType))
    {
        tupleType = isTypeAliased(inferredType) ? getAliasedTypeBase(inferredType) : inferredType;
        CheckerType **tupleMemberTypes = tupleType->tupleType.types;

        size_t tupleMemberCount = tupleType->tupleType.numTypes;

        {   //loop through expressions in tuple lit

            ASTExprLL *currExprLL = expr->tupleLit.exprLL->first;
            size_t index = 0;

            //so tht the tuple lits type can be given in errors
            //even if the inferred type is not right
            CheckerTypeLL *tupleLitTypes = NULL;

            while(currExprLL != NULL)
            {
                bool isDiscardExpr = false;
                if(isExprDiscardIdentifier(currExprLL->item))
                {
                    {
                        checkerError(currExprLL->item->startTok, "This expression cannot be a discard statement, since this type is expected: ");
                        printCheckerType(tupleMemberTypes[index]);
                    
                        fprintf(stderr, "\n");

                        prettyPrintCheckerSourceError(currExprLL->item->startTok, currExprLL->item->endTok);
                        success = false;
                    }
                    isDiscardExpr = true;
                }

                else if(index < tupleMemberCount)
                {
                    if(!checkRHSExprWithTypeAndCast(tupleMemberTypes[index], &(currExprLL->item), true, isIncompletePass))
                    {
                        checkerError(currExprLL->item->startTok, "This expression within the tuple has type: ");
                        printCheckerType(currExprLL->item->checkType);
                        
                        fprintf(stderr, " but expected type: ");
                        printCheckerType(tupleMemberTypes[index]);
                        fprintf(stderr, "\n");

                        prettyPrintCheckerSourceError(currExprLL->item->startTok, currExprLL->item->endTok);
                        success = false;
                    }
                }
                else 
                {
                    checkExpr(currExprLL->item, isIncompletePass);
                    success = false;
                }

                if(!isDiscardExpr)
                {
                    if(tupleLitTypes == NULL) tupleLitTypes = newCheckerTypeLL(currExprLL->item->checkType);
                    else addCheckerTypeLL(&tupleLitTypes, currExprLL->item->checkType);
                }
                else
                {
                    if(tupleLitTypes == NULL) tupleLitTypes = newCheckerTypeLL(NULL);
                    else addCheckerTypeLL(&tupleLitTypes, NULL);
                }

                index++;
                currExprLL = currExprLL->next;
            }

            if(success) expr->checkType = inferredType;
            else expr->checkType = checkIfTupleNotExistsCreateNew(tupleLitTypes);

            expr->compTimeVal.isL_or_RValue = EXPR_R_VALUE;
            expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
        }
        
    }
    expr->hasBeenChecked = true;
    return success;
}
void checkDeclLHSTuple(ASTExpr *lhs, ASTType *type ,ASTExpr **rhs)
{
    bool isTypeInfer = type->kind == A_TYPE_INFER;
    if(!isTypeInfer) checkType(type);

    if(!isTypeInfer && !isTypeTuple(type->checkType))
    {
        checkerError(type->startTok, "Type was expected to be a tuple type but instead got: ");
        printCheckerType(type->checkType);
        fprintf(stderr, "\n");

        prettyPrintCheckerSourceError(type->startTok, type->endTok);
    }
    else
    {   //loop through expressions in tuple lit

        CheckerType *declsTupleType = (!isTypeInfer) ? type->checkType : NULL;
        if(!isTypeInfer) declsTupleType = isTypeAliased(declsTupleType) ? getAliasedTypeBase(declsTupleType) : declsTupleType;

        {
            ASTExprLL *currLHSExpr = lhs->tupleLit.exprLL->first;

            if((*rhs)->kind != A_EXPR_TUPLE_LITERAL)
            {
                checkExpr((*rhs), false);

                CheckerType *rhsType = (*rhs)->checkType;

                if(!isTypeTuple(rhsType))
                {
                    size_t numLhsItems = currLHSExpr->numItems;
                    
                    if(numLhsItems > 1)
                    {
                        checkerError(lhs->startTok, "Tuple on LHS has too many items, RHS has only one item of type: ");
                        printCheckerType((*rhs)->checkType);
                        fprintf(stderr, "\n");

                        prettyPrintCheckerSourceError(lhs->startTok, lhs->endTok);
                    }
                }
                else
                {
                    rhsType = isTypeAliased(rhsType) ? getAliasedTypeBase(rhsType) : rhsType;
                    
                    size_t numLhsItems = currLHSExpr->numItems;
                    size_t numRhsItems = rhsType->tupleType.numTypes;

                    if(numLhsItems > numRhsItems)
                    {
                        checkerError(lhs->startTok, "LHS tuple has %lld item(s) whereas RHS has tuple type: ");
                        printCheckerType(rhsType);
                        fprintf(stderr, ", RHS tuple should have the same or higher arity than the LHS\n");

                        prettyPrintCheckerSourceError(lhs->startTok, (*rhs)->endTok);
                    }
                    else
                    {
                        Token varDeclTok = (*rhs)->startTok;
                        varDeclTok.type = TOK_IDEN;
                        strcpy(varDeclTok.lexeme, allocNewCompilerIden("__tupleLit"));

                        ASTDecl *tempDecl = newASTDeclVar(newASTExprIden(varDeclTok), newASTTypeInfer(varDeclTok), (*rhs));
                        ASTStmt *tempDeclStmt = newASTStmtDecl(tempDecl);

                        ASTStmtLL *sLL = newASTStmtLL(tempDeclStmt);
                        ASTStmtLL *temp = globalContext.cc.currStmtBeingChecked;

                        insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, sLL);

                        globalContext.cc.currStmtBeingChecked = sLL;
                        checkStmt(sLL->item);
                        globalContext.cc.currStmtBeingChecked = temp;

                        { //emit assignements for rexpressions in lhs tuple
                            ASTExpr **lhsTupleExprList = malloc(sizeof(ASTExpr *) * numLhsItems);

                            {
                                size_t i = 0;
                                while(currLHSExpr != NULL)
                                {
                                    lhsTupleExprList[i] = currLHSExpr->item;

                                    i++;
                                    currLHSExpr = currLHSExpr->next;
                                }

                                for(size_t i = 0; i < numRhsItems; i++)
                                {
                                    Token indexLitTok = varDeclTok;
                                    indexLitTok.type = TOK_INT_LITERAL;
                                    sprintf(indexLitTok.lexeme, "%lld", i);

                                    ASTExpr *indexExpr = newASTExprLit(indexLitTok);
                                    ASTExpr *rhsTupleMemberExpr = newASTExprArrayRef(newASTExprIden(varDeclTok), indexExpr);

                                    if(i < numLhsItems)
                                    {
                                        ASTStmt *s = NULL;
                                        if(isExprDiscardIdentifier(lhsTupleExprList[i])) continue;
                                        else 
                                        {
                                            ASTType *declASTType = newASTTypeInfer(lhsTupleExprList[i]->startTok);

                                            if(!isTypeInfer)
                                            {
                                                CheckerType *tupleElemType = declsTupleType->tupleType.types[i];
                                                if(!checkRHSExprWithTypeAndCast(tupleElemType, &(rhsTupleMemberExpr), true, false))
                                                {
                                                    checkerError(declASTType->startTok, "Type of element in tuple on RHS cannot be cast or inferred to be expected type: ");
                                                    printCheckerType(tupleElemType);
                                                    fprintf(stderr, "\n");

                                                    Token startTok;
                                                    Token endTok;

                                                    ASTTupleMembLL *asttypleElem = type->tuple.membs->first;

                                                    for(size_t tempCounter = 0; tempCounter <= i; tempCounter++)
                                                    {
                                                        startTok = asttypleElem->item->startTok;
                                                        endTok = asttypleElem->item->endTok;

                                                        asttypleElem = asttypleElem->next;
                                                    }

                                                    prettyPrintCheckerSourceError(startTok, endTok);
                                                }
                                            }
                                            s = newASTStmtDecl(newASTDeclVar(lhsTupleExprList[i], declASTType,rhsTupleMemberExpr));
                                        }

                                        ASTStmtLL *sLL = newASTStmtLL(s);
                                        insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, sLL);

                                        ASTStmtLL *temp = globalContext.cc.currStmtBeingChecked;
                                        globalContext.cc.currStmtBeingChecked = sLL;
                                        checkStmt(s);

                                        globalContext.cc.currStmtBeingChecked = temp;

                                    }
                                }
                            }
                        }
                    }
                }
            }
            else
            {
                ASTExprLL *currRHSExpr = (*rhs)->tupleLit.exprLL->first;

                size_t numLhsItems = currLHSExpr->numItems;
                size_t numRhsItems = currRHSExpr->numItems;

                ASTExpr **lhsTupleExprList = malloc(sizeof(ASTExpr *) * numLhsItems);
                ASTExpr **rhsTupleExprList = malloc(sizeof(ASTExpr *) * numRhsItems);

                {
                    size_t i = 0;
                    while(currLHSExpr != NULL)
                    {
                        lhsTupleExprList[i] = currLHSExpr->item;

                        i++;
                        currLHSExpr = currLHSExpr->next;
                    }
                }

                {
                    size_t i = 0;
                    while(currRHSExpr != NULL)
                    {
                        rhsTupleExprList[i] = currRHSExpr->item;

                        i++;
                        currRHSExpr = currRHSExpr->next;
                    }
                }

                if(numLhsItems > numRhsItems)
                {
                    checkerErrorLn(lhs->startTok, "LHS tuple has %lld item(s) whereas RHS has %lld item(s), RHS should have atleast just as many as the LHS",
                                    numLhsItems, numRhsItems);

                    prettyPrintCheckerSourceError(lhs->startTok, (*rhs)->endTok);

                    free(lhsTupleExprList);
                    free(rhsTupleExprList);
                }
                else
                {
                    //since rhs will have more exprs or equal
                    for(size_t i = 0; i < numRhsItems; i++)
                    {
                        ASTStmt *s = NULL;

                        //add rhs as expr stmt if lhs is discard or less then rhs
                        if((i < numLhsItems) && !(isExprDiscardIdentifier(lhsTupleExprList[i])))
                        {
                            ASTType *declASTType = newASTTypeInfer(lhsTupleExprList[i]->startTok);

                            if(!isTypeInfer)
                            {
                                CheckerType *tupleElemType = declsTupleType->tupleType.types[i];
                                if(!checkRHSExprWithTypeAndCast(tupleElemType, &(rhsTupleExprList[i]), true, false))
                                {
                                    checkerError(declASTType->startTok, "Type of element in tuple on RHS cannot be cast or inferred to be expected type: ");
                                    printCheckerType(tupleElemType);
                                    fprintf(stderr, "\n");

                                    Token startTok;
                                    Token endTok;

                                    ASTTupleMembLL *asttypleElem = type->tuple.membs->first;

                                    for(size_t tempCounter = 0; tempCounter <= i; tempCounter++)
                                    {
                                        startTok = asttypleElem->item->startTok;
                                        endTok = asttypleElem->item->endTok;

                                        asttypleElem = asttypleElem->next;
                                    }

                                    prettyPrintCheckerSourceError(startTok, endTok);
                                }
                            }

                            s = newASTStmtDecl(newASTDeclVar(lhsTupleExprList[i], declASTType, rhsTupleExprList[i]));
                        }
                        else s = newASTStmtExpr(rhsTupleExprList[i]);

                        ASTStmtLL *sLL = newASTStmtLL(s);
                        insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, sLL);

                        ASTStmtLL *temp = globalContext.cc.currStmtBeingChecked;
                        globalContext.cc.currStmtBeingChecked = sLL;
                        checkStmt(s);

                        globalContext.cc.currStmtBeingChecked = temp;
                    }
                }


            }
        }
    }
}
void checkAssignLHSTuple(ASTExpr *lhs, ASTExpr **rhs)
{

    {   //loop through expressions in tuple lit

            {
                ASTExprLL *currLHSExpr = lhs->tupleLit.exprLL->first;

                if((*rhs)->kind != A_EXPR_TUPLE_LITERAL)
                {
                    checkExpr((*rhs), false);

                    CheckerType *rhsType = (*rhs)->checkType;

                    if(!isTypeTuple(rhsType))
                    {
                        size_t numLhsItems = currLHSExpr->numItems;
                        
                        if(numLhsItems > 1)
                        {
                            checkerError(lhs->startTok, "Tuple on LHS has too many items, RHS has only one item of type: ");
                            printCheckerType((*rhs)->checkType);
                            fprintf(stderr, "\n");

                            prettyPrintCheckerSourceError(lhs->startTok, lhs->endTok);
                        }

                        //compiler does not support one element tuples yet
                        // else
                        // {
                        //     ASTStmt *s = newASTStmtAssign(currLHSExpr->item, (*rhs));
                        //     ASTStmtLL *sLL = newASTStmtLL(s);
                        //     insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, sLL);

                        //     ASTStmtLL *temp = globalContext.cc.currStmtBeingChecked;
                        //     globalContext.cc.currStmtBeingChecked = sLL;
                        //     checkStmt(s);

                        //     globalContext.cc.currStmtBeingChecked = temp;
                        // }
                    }
                    else
                    {
                        rhsType = isTypeAliased(rhsType) ? getAliasedTypeBase(rhsType) : rhsType;
                        
                        size_t numLhsItems = currLHSExpr->numItems;
                        size_t numRhsItems = rhsType->tupleType.numTypes;

                        if(numLhsItems > numRhsItems)
                        {
                            checkerError(lhs->startTok, "LHS tuple has %lld item(s) whereas RHS has tuple type: ");
                            printCheckerType(rhsType);
                            fprintf(stderr, ", RHS tuple should have the same or higher arity than the LHS\n");

                            prettyPrintCheckerSourceError(lhs->startTok, (*rhs)->endTok);
                        }
                        else
                        {
                            Token varDeclTok = (*rhs)->startTok;
                            varDeclTok.type = TOK_IDEN;
                            strcpy(varDeclTok.lexeme, allocNewCompilerIden("__tupleLit"));

                            ASTDecl *tempDecl = newASTDeclVar(newASTExprIden(varDeclTok), newASTTypeInfer(varDeclTok), (*rhs));
                            ASTStmt *tempDeclStmt = newASTStmtDecl(tempDecl);

                            ASTStmtLL *sLL = newASTStmtLL(tempDeclStmt);
                            ASTStmtLL *temp = globalContext.cc.currStmtBeingChecked;

                            insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, sLL);

                            globalContext.cc.currStmtBeingChecked = sLL;
                            checkStmt(sLL->item);
                            globalContext.cc.currStmtBeingChecked = temp;

                            { //emit assignements for rexpressions in lhs tuple
                                ASTExpr **lhsTupleExprList = malloc(sizeof(ASTExpr *) * numLhsItems);

                                {
                                    size_t i = 0;
                                    while(currLHSExpr != NULL)
                                    {
                                        lhsTupleExprList[i] = currLHSExpr->item;

                                        i++;
                                        currLHSExpr = currLHSExpr->next;
                                    }

                                    for(size_t i = 0; i < numRhsItems; i++)
                                    {
                                        Token indexLitTok = varDeclTok;
                                        indexLitTok.type = TOK_INT_LITERAL;
                                        sprintf(indexLitTok.lexeme, "%lld", i);

                                        ASTExpr *indexExpr = newASTExprLit(indexLitTok);
                                        ASTExpr *rhsTupleMemberExpr = newASTExprArrayRef(newASTExprIden(varDeclTok), indexExpr);

                                        if(i < numLhsItems)
                                        {
                                            ASTStmt *s = NULL;
                                            if(isExprDiscardIdentifier(lhsTupleExprList[i])) continue;
                                            else s = newASTStmtAssign(lhsTupleExprList[i], rhsTupleMemberExpr);

                                            ASTStmtLL *sLL = newASTStmtLL(s);
                                            insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, sLL);

                                            ASTStmtLL *temp = globalContext.cc.currStmtBeingChecked;
                                            globalContext.cc.currStmtBeingChecked = sLL;
                                            checkStmt(s);

                                            globalContext.cc.currStmtBeingChecked = temp;

                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                else
                {
                    ASTExprLL *currRHSExpr = (*rhs)->tupleLit.exprLL->first;

                    size_t numLhsItems = currLHSExpr->numItems;
                    size_t numRhsItems = currRHSExpr->numItems;

                    ASTExpr **lhsTupleExprList = malloc(sizeof(ASTExpr *) * numLhsItems);
                    ASTExpr **rhsTupleExprList = malloc(sizeof(ASTExpr *) * numRhsItems);

                    {
                        size_t i = 0;
                        while(currLHSExpr != NULL)
                        {
                            lhsTupleExprList[i] = currLHSExpr->item;

                            i++;
                            currLHSExpr = currLHSExpr->next;
                        }
                    }

                    {
                        size_t i = 0;
                        while(currRHSExpr != NULL)
                        {
                            rhsTupleExprList[i] = currRHSExpr->item;

                            i++;
                            currRHSExpr = currRHSExpr->next;
                        }
                    }

                    if(numLhsItems > numRhsItems)
                    {
                        checkerErrorLn(lhs->startTok, "LHS tuple has %lld item(s) whereas RHS has %lld item(s), RHS should have atleast just as many as the LHS",
                                        numLhsItems, numRhsItems);

                        prettyPrintCheckerSourceError(lhs->startTok, (*rhs)->endTok);

                        free(lhsTupleExprList);
                        free(rhsTupleExprList);
                    }
                    else
                    {
                        //since rhs will have more exprs or equal
                        for(size_t i = 0; i < numRhsItems; i++)
                        {
                            ASTStmt *s = NULL;

                            //add rhs as expr stmt if lhs is discard or less then rhs
                            if((i < numLhsItems) && !(isExprDiscardIdentifier(lhsTupleExprList[i])))
                            {
                                s = newASTStmtAssign(lhsTupleExprList[i], rhsTupleExprList[i]);
                            }
                            else s = newASTStmtExpr(rhsTupleExprList[i]);

                            ASTStmtLL *sLL = newASTStmtLL(s);
                            insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, sLL);

                            ASTStmtLL *temp = globalContext.cc.currStmtBeingChecked;
                            globalContext.cc.currStmtBeingChecked = sLL;
                            checkStmt(s);

                            globalContext.cc.currStmtBeingChecked = temp;
                        }
                    }


                }
            }
        
    }
}
bool checkRHSExprWithTypeAndCast(CheckerType *checkAgainst, ASTExpr **rhs, bool shouldInsertCast, bool isIncompletePass)
{
    return checkInferredExpr(checkAgainst, rhs, shouldInsertCast, true, isIncompletePass);
}

void checkInsertFuncRetAsArgStmts(ASTExpr *funcCallExpr)
{
    CheckerType *funcType = funcCallExpr->funcCall.iden->checkType;
    funcType = isTypeAliased(funcType) ? getAliasedTypeBase(funcType) : funcType;
    
    Token varName = funcCallExpr->startTok;
    varName.type = TOK_IDEN;
    strcpy(varName.lexeme, funcCallExpr->funcCall.retAsArg_argWhichIsRetName);

    SymEntry *e = _symTableLookupLocal(globalContext.cc.checkingLocalsSymTble, varName.lexeme);

     {//incert ret variable decl
        if(e == NULL)
        {
            ASTStmt *varDeclStmt = newASTStmtDecl(newASTDeclVar(newASTExprIden(varName), newASTTypeInfer(varName), NULL));
            varDeclStmt->decl.decl->var.type->checkType = funcType->funcType.ret;

            ASTStmtLL *varDeclToInsert = newASTStmtLL(varDeclStmt);

            insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, varDeclToInsert);
            _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, varName.lexeme, funcType->funcType.ret, varName);

        }
    }

    {//insert funccall
        if(e == NULL)
        {
            ASTExpr *funcCall = newASTExprFuncCall(funcCallExpr->funcCall.iden, funcCallExpr->funcCall.args);
            funcCall->funcCall.argCallOrder = funcCallExpr->funcCall.argCallOrder;
            funcCall->funcCall.argCallOrderCount = funcCallExpr->funcCall.argCallOrderCount;
            funcCall->genIdenName = funcCall->funcCall.retAsArg_argWhichIsRetName = funcCallExpr->genIdenName;

            ASTStmt *funcCallStmt = newASTStmtExpr(funcCallExpr);
            ASTStmtLL *stmtToInsert = newASTStmtLL(funcCallStmt);

            insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, stmtToInsert);
        }
    }
}
void checkInsertAnyCastStmts(ASTExpr *expr)
{
    if(globalContext.gc.backend != BACKEND_C) return;

    Token varName = expr->startTok;
    varName.type = TOK_IDEN;
    strcpy(varName.lexeme, expr->cast.expr->genIdenName);

    SymEntry *e = _symTableLookupLocal(globalContext.cc.checkingLocalsSymTble, varName.lexeme);
    if(e == NULL)
    {
        ASTStmt *varDeclStmt = newASTStmtDecl(newASTDeclVar(newASTExprIden(varName), newASTTypeInfer(varName), expr->cast.expr));
        varDeclStmt->decl.decl->var.type->checkType = expr->cast.expr->checkType;
        checkExpr(expr->cast.expr, false);

        ASTStmtLL *varDeclToInsert = newASTStmtLL(varDeclStmt);

        insertBeforeASTStmtLL(globalContext.cc.currStmtBeingChecked, varDeclToInsert);
        
        _symTableInsertLocal(globalContext.cc.checkingLocalsSymTble, varName.lexeme, expr->cast.expr->checkType, varName);
    }
}

int checkIfMainFuncHasCorrectSignature(CheckerType *mainFuncType)
{
    int retFlag = 0;
    ScopedDeclLL *paramsLL = mainFuncType->funcType.paramLL;
    CheckerType *retType = mainFuncType->funcType.ret;

    size_t numParams = (paramsLL == NULL) ? 0 : paramsLL->first->numItems;

    if(!isTypeVoid(retType) && !isTypeInteger(retType))
    {
        retFlag |= MAIN_FUNC_INCORRECT_RET_TYPE;
    }
    else
    {
        if(isTypeVoid(retType)) retFlag |= MAIN_FUNC_HAS_VOID_RET;
    }

    if(numParams != 0)
    {
        if(numParams != 1) retFlag |= MAIN_FUNC_INCORRECT_PARAMS;
        else
        {
            CheckerType *paramType = paramsLL->first->item->type;
            bool success = false;
            if(isTypeSliceArray(paramType))
            {
                CheckerType *arrType = (isTypeAliased(paramType)) ? getAliasedTypeBase(paramType) : paramType;

                if(areTypesEqual(stringType, arrType->arrayType.base)) success = true;
            }

            if(!success) retFlag |= MAIN_FUNC_INCORRECT_PARAMS;
        }
    }
    else retFlag |= MAIN_FUNC_HAS_VOID_PARAMS;

    return retFlag;
}

SymEntry *checkTryfindOverloadFunc(TokType op, CheckerType *a, CheckerType *b)
{
    char *funcName = allocOperFuncNameFromTypes(op, a, b);
    SymEntry *e = NULL;
    
    if(funcName != NULL)
    {
        e = _symTableLookupGlobal(globalContext.symTable, funcName);

        if(e == NULL)
        {
            bool lhsIsInt = isTypeInteger(a);
            bool rhsIsInt = isTypeInteger(b);

            bool lhsIsFloat = isTypeFloat(a);
            bool rhsIsFloat = isTypeFloat(b);

            bool lhsIsFArray = isTypeFixedArray(a);
            bool rhsIsFArray = isTypeFixedArray(b);

            if(lhsIsInt)
            {
                size_t size = checkerTypeGetSize(a);

                if(size < 8)
                {
                    CheckerType *iType = copyCheckerType(a);

                    //check for overloads for integer size bigger then current integer size, if one for current integer size is not found
                    for(size_t s = size * 2; size <= 8; size = size * 2)
                    {
                        iType->byteSize = size;

                        funcName = allocOperFuncNameFromTypes(op, iType, b);
                        e = _symTableLookupGlobal(globalContext.symTable, funcName);

                        if(e != NULL) return e;
                    }

                }
            }

            if(rhsIsInt)
            {
                size_t size = checkerTypeGetSize(b);

                if(size < 8)
                {
                    CheckerType *iType = copyCheckerType(b);

                    //check for overloads for integer size bigger then current integer size, if one for current integer size is not found
                    for(size_t s = size * 2; size <= 8; size = size * 2)
                    {
                        iType->byteSize = size;

                        funcName = allocOperFuncNameFromTypes(op, a, iType);
                        e = _symTableLookupGlobal(globalContext.symTable, funcName);

                        if(e != NULL) return e;
                    }

                }
            }

            if(lhsIsFloat)
            {
                size_t size = checkerTypeGetSize(a);

                if(size < 8)
                {
                    CheckerType *iType = copyCheckerType(a);

                    //check for overloads for float size bigger then current float size, if one for current float size is not found
                    for(size_t s = size * 2; size <= 8; size = size * 2)
                    {
                        iType->byteSize = size;

                        funcName = allocOperFuncNameFromTypes(op, iType, b);
                        e = _symTableLookupGlobal(globalContext.symTable, funcName);

                        if(e != NULL) return e;
                    }

                }
            }

            if(rhsIsFloat)
            {
                size_t size = checkerTypeGetSize(b);

                if(size < 8)
                {
                    CheckerType *iType = copyCheckerType(b);

                    //check for overloads for float size bigger then current float size, if one for current float size is not found
                    for(size_t s = size * 2; size <= 8; size = size * 2)
                    {
                        iType->byteSize = size;

                        funcName = allocOperFuncNameFromTypes(op, a, iType);
                        e = _symTableLookupGlobal(globalContext.symTable, funcName);

                        if(e != NULL) return e;
                    }

                }
            }
        
            if(lhsIsFArray && rhsIsFArray)
            {
                CheckerType *tempLHS = copyCheckerType(a);
                tempLHS->arrayType.isSlice = true;
                tempLHS->arrayType.size = 0;

                CheckerType *tempRHS = copyCheckerType(b);
                tempRHS->arrayType.isSlice = true;
                tempRHS->arrayType.size = 0;

                funcName = allocOperFuncNameFromTypes(op, tempLHS, tempRHS);
                e = _symTableLookupGlobal(globalContext.symTable, funcName);

                if(e != NULL) return e;
            }

            if(lhsIsFArray)
            {
                CheckerType *tempLHS = copyCheckerType(a);
                tempLHS->arrayType.isSlice = true;
                tempLHS->arrayType.size = 0;

                funcName = allocOperFuncNameFromTypes(op, tempLHS, b);
                e = _symTableLookupGlobal(globalContext.symTable, funcName);

                if(e != NULL) return e;
            }

            if(rhsIsFArray)
            {
                CheckerType *tempRHS = copyCheckerType(b);
                tempRHS->arrayType.isSlice = true;
                tempRHS->arrayType.size = 0;

                funcName = allocOperFuncNameFromTypes(op, a, tempRHS);
                e = _symTableLookupGlobal(globalContext.symTable, funcName);

                if(e != NULL) return e;
            }
        }
    }

    return e;
}