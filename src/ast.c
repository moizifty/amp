#include "ast.h"

ASTExpr *allocASTExpr(ASTExprKind kind, Token startTok)
{
    ASTExpr *expr = malloc(sizeof(ASTExpr));
    expr->kind = kind;
    expr->startTok = startTok;
    expr->checkType = NULL;
    expr->compTimeVal.kind = A_EXPR_COMP_TIME_RUNTIME;
    expr->hasBeenChecked = false;
    expr->genIdenName = NULL;
    expr->genAnyCastForDefaultValueExpr = false;
    expr->idenSymEntry = NULL;
    expr->genFuncRetAsArgForDefaultValueExpr = false;

    return expr;
}
void freeASTExpr(ASTExpr **expr)
{
    if((*expr) == NULL)
        return;

    switch((*expr)->kind)
    {
        case A_EXPR_FUNC_CALL:
        {
            free(&(*expr)->funcCall.iden);
        }break;
        case A_EXPR_INDEX_REF:
        {
            free(&(*expr)->arrayRef.iden);
            free(&(*expr)->arrayRef.index);
        }break;
        case A_EXPR_MEMBER_ACCESS:
        {
            free(&(*expr)->membAccess.typeName);
        }break;
        case A_EXPR_POST:
        {
            free(&(*expr)->post.expr);
        }break;

        case A_EXPR_UNARY:
        {
            freeASTExpr(&(*expr)->unary.expr);
        }break;
        case A_EXPR_CAST:
        {
            freeASTType(&(*expr)->cast.castTo);
            freeASTExpr(&(*expr)->cast.expr);
        }break;
        case A_EXPR_EXPR_INTRO:
        {
            freeASTExpr(&(*expr)->exprIntro.expr);
        }break;
        case A_EXPR_TYPE_INTRO:
        {
            freeASTType(&(*expr)->typeIntro.type);
        }break;
        case A_EXPR_BINARY:
        {
            freeASTExpr(&(*expr)->binary.l);
            freeASTExpr(&(*expr)->binary.r);
        }break;

        case A_EXPR_RANGE:
        {
            freeASTExpr(&(*expr)->range.l);
            freeASTExpr(&(*expr)->range.r);
        }break;
    }

    free(*expr);
    *expr = NULL;

}

ASTExprLL *newASTExprLL(ASTExpr *item)
{
    ASTExprLL *ll = malloc(sizeof(ASTExprLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTExprLL(ASTExprLL **ll, ASTExpr *item)
{
    ASTExprLL *n = newASTExprLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
void freeASTExprLL(ASTExprLL **ll)
{
    ASTExprLL *prev = (*ll);
    while(prev != NULL)
    {
        ASTExprLL *temp = prev;

        freeASTExpr(&prev->item);
        prev = prev->prev;

        free(temp);
    }

    *ll = NULL;
}
void insertBeforeASTExprLL(ASTExprLL *before, ASTExprLL *toInsert)
{
    if(before == NULL)
    {
        return;
    }

    ASTExprLL *temp = before->prev;
    
    ASTExprLL *top = toInsert;
    while(top->next != NULL) top = toInsert->next;

    before->prev = top;
    top->next = before;

    if(temp != NULL)
    {
        temp->next = toInsert->first;
        toInsert->first->prev = temp;
    }

    if(before == before->first)
    {
        toInsert->first->numItems += before->first->numItems;
        ASTExprLL *s = before->first;

        while(s != NULL)
        {
            s->first = toInsert->first;

            s = s->next;
        }

    }
    else
    {
        before->first->numItems += toInsert->first->numItems;
        toInsert->first = before->first;
    }
}
void insertAfterASTExprLL(ASTExprLL *before, ASTExprLL *toInsert)
{
    if(before == NULL)
    {
        return;
    }

    ASTExprLL *temp = before->next;
    
    ASTExprLL *top = toInsert;
    while(top->next != NULL) top = toInsert->next;

    before->next = toInsert->first;
    top->first->prev = before;

    if(temp != NULL)
    {
        temp->prev = top;
        top->next = temp;
    }

    before->first->numItems += toInsert->first->numItems;
    toInsert = toInsert->first;
    while(toInsert != NULL)
    {
        toInsert->first = before->first;
        toInsert = toInsert->next;
    }
}
ASTNamedExprLL *newASTNamedExprLL(ASTNamedExpr *item)
{
    ASTNamedExprLL *ll = malloc(sizeof(ASTNamedExprLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTNamedExprLL(ASTNamedExprLL **ll, ASTNamedExpr *item)
{
    ASTNamedExprLL *n = newASTNamedExprLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;


    (*ll) = n;
}
void freeASTNamedExprLL(ASTNamedExprLL **ll)
{
    ASTNamedExprLL *prev = (*ll);
    while(prev != NULL)
    {
        ASTNamedExprLL *temp = prev;

        freeASTNamedExpr(&prev->item);
        prev = prev->prev;

        free(temp);
    }

    *ll = NULL;
}

ASTNamedExpr *allocASTNamedExpr(ASTNamedExprKind kind, Token startTok)
{
    ASTNamedExpr *n = malloc(sizeof(ASTNamedExpr));
    n->kind = kind;
    n->startTok = startTok;
    n->checkType = NULL;
    n->isVariadicArrayArg = false;
    
    return n;
}
void freeASTNamedExpr(ASTNamedExpr **n)
{
    if((*n) == NULL) return;

    switch((*n)->kind)
    {
        case A_NAMED_EXPR_NORMAL:
        {
            freeASTExpr(&(*n)->normal);
        }break;
        case A_NAMED_EXPR_NAMED:
        {
            freeASTExpr(&(*n)->named.expr);
        }break;
    }

    free((*n));

    *n = NULL;
}

ASTGenericType *newASTGenericType(Token iden)
{
    ASTGenericType *g = malloc(sizeof(ASTGenericType));
    g->iden = iden;
    
    return g;
}

ASTGenericTypeLL *newASTGenericTypeLL(ASTGenericType *item)
{
    ASTGenericTypeLL *ll = malloc(sizeof(ASTGenericTypeLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTGenericTypeLL(ASTGenericTypeLL **ll, ASTGenericType *item)
{
    ASTGenericTypeLL *n = newASTGenericTypeLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;


    (*ll) = n;
}


ASTFuncParam *newASTFuncParam(Token iden, ASTType *type, ASTExpr *defValue)
{
    ASTFuncParam *p = malloc(sizeof(ASTFuncParam));
    p->iden = iden;
    p->type = type;
    p->defValue = defValue;
    p->isVariadicArrayArg = false;
    p->isTypeMethodThisParam = false;
    
    return p;
}
void freeASTFuncParam(ASTFuncParam **param)
{
    if((*param) == NULL) return;

    freeASTExpr(&(*param)->defValue);
    freeASTType(&(*param)->type);
    
    free(*param);
    *param = NULL;
}


ASTFuncParamLL *newASTFuncParamLL(ASTFuncParam *item)
{
    ASTFuncParamLL *ll = malloc(sizeof(ASTFuncParamLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}

// imagine a list 3, 4, 5, 6 and ur adding 8
// this function adds 8 at the end, 3, 4, 5, 6, 8
// then sets the linked list head to 8
//so when a new itme is added, its added at the end
void addASTFuncParamLL(ASTFuncParamLL **ll, ASTFuncParam *item)
{
    ASTFuncParamLL *n = newASTFuncParamLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;


    (*ll) = n;
}
void freeASTFuncParamLL(ASTFuncParamLL **paramLL)
{
    ASTFuncParamLL *prev = (*paramLL);
    while(prev != NULL)
    {
        ASTFuncParamLL *temp = prev;

        freeASTFuncParam(&prev->item);
        prev = prev->prev;

        free(temp);
    }

    *paramLL = NULL;
}

ASTFuncSig *newASTFuncSig(Token startTok, ASTFuncParamLL *params, int paramCount, ASTType *retType)
{
    ASTFuncSig *sig = malloc(sizeof(ASTFuncSig));
    sig->params = params;
    sig->paramCount = paramCount;
    sig->retType = retType;
    sig->startTok = startTok;
    sig->retAsArg = false;

    return sig;
}
void freeASTFuncSig(ASTFuncSig **sig)
{
    if((*sig) == NULL) return;

    freeASTType(&(*sig)->retType);
    freeASTFuncParamLL(&(*sig)->params);

    free(*sig);
    *sig = NULL;
}

ASTType *allocASTType(ASTTypeKind kind, Token startTok)
{
    ASTType *t = malloc(sizeof(ASTType));
    t->kind = kind;
    t->startTok = startTok;
    t->isFreed = false;
    t->checkType = NULL;
    t->typeFilter = NULL;
    
    return t;
}
void freeASTType(ASTType **type)
{
    if((*type) == NULL)
        return;
    
    switch((*type)->kind)
    {
        case A_TYPE_ARRAY:
        {
            freeASTExpr(&(*type)->array.size);
            freeASTType(&(*type)->array.elem);
        }break;
        case A_TYPE_POINTER:
        {
            freeASTType(&(*type)->pointer.type);
        }break;
        case A_TYPE_FUNC_POINTER:
        {
            freeASTFuncSig(&(*type)->funcPointer.funcSig);
        }break;
    }

    free(*type);
    *type = NULL;
}

ASTExpr *newASTExprLit(Token lit)
{
    ASTExpr *e = allocASTExpr(A_EXPR_LIT, lit);
    e->lit = lit;

    return e;
}
ASTExpr *newASTExprIden(Token iden)
{
    ASTExpr *e = allocASTExpr(A_EXPR_IDEN, iden);
    e->iden = iden;
    e->endTok = iden;

    return e;
}

ASTExpr *newASTExprFuncCall(ASTExpr *iden, ASTNamedExprLL *args)
{
    ASTExpr *e = allocASTExpr(A_EXPR_FUNC_CALL, iden->startTok);
    e->funcCall.iden = iden;
    e->funcCall.args = args;
    e->funcCall.argCallOrder = NULL;
    e->funcCall.argCallOrderCount = 0;
    e->funcCall.retAsArg_argWhichIsRetName = NULL;
    e->funcCall.retAsArg_emittedIdenName = false;
    return e;
}
ASTExpr *newASTExprArrayRef(ASTExpr *iden, ASTExpr *index)
{
    ASTExpr *e = allocASTExpr(A_EXPR_INDEX_REF, iden->startTok);
    e->arrayRef.iden = iden;
    e->arrayRef.index = index;

    return e;
}
ASTExpr *newASTExprMembAccess(ASTExpr *typeName, Token memb)
{
    ASTExpr *e = allocASTExpr(A_EXPR_MEMBER_ACCESS, typeName->startTok);
    e->membAccess.typeName = typeName;
    e->membAccess.memb = memb;
    e->membAccess.isMethodAccess = false;
    
    return e;
}
ASTExpr *newASTExprScopeAccess(ASTExpr *scopeName, Token memb)
{
    ASTExpr *e = allocASTExpr(A_EXPR_SCOPE_ACCESS, scopeName->startTok);
    e->scopeAccess.scopeName = scopeName;
    e->scopeAccess.memb = memb;

    return e;
}
ASTExpr *newASTExprPost(Token op, ASTExpr *expr)
{
    ASTExpr *e = allocASTExpr(A_EXPR_POST, expr->startTok);
    e->post.op = op;
    e->post.expr = expr;

    return e;
}

ASTExpr *newASTExprUnary(Token op, ASTExpr *expr)
{
    ASTExpr *e = allocASTExpr(A_EXPR_UNARY, op);
    e->unary.op = op;
    e->unary.expr = expr;

    return e;
}
ASTExpr *newASTExprCast(ASTType *castTo, ASTExpr *expr)
{
    ASTExpr *e = allocASTExpr(A_EXPR_CAST, (castTo == NULL) ? expr->startTok : castTo->startTok);
    e->cast.castTo = castTo;
    e->cast.expr = expr;

    return e;
}
ASTExpr *newASTExprExprIntro(ASTExpr *expr)
{
    ASTExpr *e = allocASTExpr(A_EXPR_EXPR_INTRO, expr->startTok);
    e->exprIntro.expr = expr;

    return e;
}
ASTExpr *newASTExprTypeIntro(ASTType *type)
{
    ASTExpr *e = allocASTExpr(A_EXPR_TYPE_INTRO, type->startTok);
    e->typeIntro.type = type;

    return e;
}
ASTExpr *newASTExprAsString(Token startTok, ASTExpr *expr)
{
    ASTExpr *e = allocASTExpr(A_EXPR_ASSTRING, startTok);
    e->asString.expr = expr;

    return e;
}
ASTExpr *newASTExprBinary(Token op, ASTExpr *l, ASTExpr *r)
{
    ASTExpr *e = allocASTExpr(A_EXPR_BINARY, l->startTok);
    e->binary.op = op;
    e->binary.l = l;
    e->binary.r = r;

    return e;
}

ASTExpr *newASTExprRange(ASTExpr *l, ASTExpr *r)
{
    ASTExpr *e = allocASTExpr(A_EXPR_RANGE, l->startTok);
    e->range.l = l;
    e->range.r = r;

    return e;
}

ASTExpr *newASTExprStructunionLit(Token startTok, ASTType *typeName, ASTNamedExprLL *exprLL)
{
    ASTExpr *e = allocASTExpr(A_EXPR_STRUCT_LITERAL, startTok);
    e->structunionLit.exprLL = exprLL;
    e->structunionLit.typeName = typeName;

    return e;
}
ASTExpr *newASTExprArrayLit(Token startTok, ASTExpr *sizeExpr, ASTType *type, ASTExprLL *exprLL)
{
    ASTExpr *e = allocASTExpr(A_EXPR_ARRAY_LITERAL, startTok);
    e->arrayLit.sizeExpr = sizeExpr;
    e->arrayLit.type = type;
    e->arrayLit.exprLL = exprLL;
    
    return e;
}
ASTExpr *newASTExprTupleLit(Token startTok, ASTExprLL *exprLL)
{
    ASTExpr *e = allocASTExpr(A_EXPR_TUPLE_LITERAL, startTok);
    e->tupleLit.exprLL = exprLL;
    
    return e;
}

#define COMP_INTERNAL_IDEN "___compInternalIden"
int compilerInternalIdenCounter = 0;
ASTExpr *newASTExprBlock(ASTBlock *b)
{
    ASTExpr *e = allocASTExpr(A_EXPR_BLOCK, b->startTok);
    e->block.b = b;

    char buf[BUFSIZ] = {0};
    sprintf(buf, COMP_INTERNAL_IDEN "%d", compilerInternalIdenCounter);
    compilerInternalIdenCounter++;

    e->block.idenName = malloc(strlen(buf) + 1);
    strcpy( e->block.idenName, buf);

    return e;
}
ASTExpr *newASTExprIf(ASTStmt *ifStmt)
{
    ASTExpr *e = allocASTExpr(A_EXPR_IF, ifStmt->startTok);
    e->ifExpr.ifStmt = ifStmt;

    char buf[BUFSIZ] = {0};
    sprintf(buf, COMP_INTERNAL_IDEN "%d", compilerInternalIdenCounter);
    compilerInternalIdenCounter++;

    e->ifExpr.idenName = malloc(strlen(buf) + 1);
    strcpy( e->ifExpr.idenName, buf);

    return e;
}
ASTExpr *newASTExprWhile(ASTStmt *whileStmt)
{
    ASTExpr *e = allocASTExpr(A_EXPR_WHILE, whileStmt->startTok);
    e->whileExpr.whileStmt = whileStmt;

    char buf[BUFSIZ] = {0};
    sprintf(buf, COMP_INTERNAL_IDEN "%d", compilerInternalIdenCounter);
    compilerInternalIdenCounter++;

    e->whileExpr.idenName = malloc(strlen(buf) + 1);
    strcpy( e->whileExpr.idenName, buf);

    return e;
}
ASTExpr *newASTExprMatch(ASTStmt *matchStmt)
{
    ASTExpr *e = allocASTExpr(A_EXPR_MATCH, matchStmt->startTok);
    e->match.matchStmt = matchStmt;

    char buf[BUFSIZ] = {0};
    sprintf(buf, COMP_INTERNAL_IDEN "%d", compilerInternalIdenCounter);
    compilerInternalIdenCounter++;

    e->match.idenName = malloc(strlen(buf) + 1);
    strcpy( e->match.idenName, buf);

    return e;
}

ASTNamedExpr *newASTNamedExprNormal(ASTExpr *expr)
{
    ASTNamedExpr *n = allocASTNamedExpr(A_NAMED_EXPR_NORMAL, expr->startTok);
    n->normal = expr;

    return n;
}
ASTNamedExpr *newASTNamedExprNamed(Token name, ASTExpr *expr)
{
    ASTNamedExpr *n = allocASTNamedExpr(A_NAMED_EXPR_NAMED, name);
    n->named.name = name;
    n->named.expr = expr;

    return n;
}


ASTType *newASTTypeIden(Token iden)
{
    ASTType *t = allocASTType(A_TYPE_IDEN, iden);
    t->iden = iden;
    
    return t;
}
ASTType *newASTTypeVoid(Token iden)
{
    ASTType *t = allocASTType(A_TYPE_VOID, iden);
    t->iden = iden;
    
    return t;
}
ASTType *newASTTypeInt(Token iden)
{
    ASTType *t = allocASTType(A_TYPE_INT, iden);
    t->iden = iden;
    
    return t;
}
ASTType *newASTTypeFloat(Token iden)
{
    ASTType *t = allocASTType(A_TYPE_FLOAT, iden);
    t->iden = iden;
    
    return t;
}
ASTType *newASTTypeString(Token iden)
{
    ASTType *t = allocASTType(A_TYPE_STRING, iden);
    t->iden = iden;
    
    return t;
}
ASTType *newASTTypeBool(Token iden)
{
    ASTType *t = allocASTType(A_TYPE_BOOL, iden);
    t->iden = iden;
    
    return t;
}
ASTType *newASTTypeScoped(ASTExpr *expr)
{
    ASTType *t = allocASTType(A_TYPE_SCOPED, expr->startTok);
    t->scoped.expr = expr;

    return t;
}
ASTType *newASTTypeVariadic(Token startTok, ASTType *base)
{
    ASTType *t = allocASTType(A_TYPE_VARIADIC, startTok);
    t->variadic.base = base;
    return t;
}
ASTType *newASTTypeTuple(Token startTok, ASTTupleMembLL *membs)
{
    ASTType *t = allocASTType(A_TYPE_TUPLE, startTok);
    t->tuple.membs = membs;
    
    return t;
}

ASTType *newASTTypeInfer(Token startTok)
{
    ASTType *t = allocASTType(A_TYPE_INFER, startTok);

    return t;
}

ASTType *newASTTypeArray(Token startTok, ASTExpr *sizeExpr, ASTType *elemType, bool isSlice)
{
    ASTType *t = allocASTType(A_TYPE_ARRAY, startTok);
    t->array.size = sizeExpr;
    t->array.elem = elemType;
    t->array.isSlice = isSlice;

    return t;
}
ASTType *newASTTypePointer(ASTType *pointedType)
{
    ASTType *t = allocASTType(A_TYPE_POINTER, pointedType->startTok);
    t->pointer.type = pointedType;

    return t;
}
ASTType *newASTTypeFuncPointer(ASTFuncSig *sig)
{
    ASTType *t = allocASTType(A_TYPE_FUNC_POINTER, sig->startTok);
    t->funcPointer.funcSig = sig;

    return t;
}

ASTTypeLL *newASTTypeLL(ASTType *item)
{
    ASTTypeLL *ll = malloc(sizeof(ASTTypeLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTTypeLL(ASTTypeLL **ll, ASTType *item)
{
    ASTTypeLL *n = newASTTypeLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

ASTTupleMemb *newASTTupleMemb(Token startTok, Token iden, ASTType *type, bool hasIden)
{
    ASTTupleMemb *t = malloc(sizeof(ASTTupleMemb));
    t->startTok = startTok;
    t->hasIden = hasIden;
    t->iden = iden;
    t->type = type;

    return t;
}
ASTTupleMembLL *newASTTupleMembLL(ASTTupleMemb *item)
{
    ASTTupleMembLL *ll = malloc(sizeof(ASTTupleMembLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTTupleMembLL(ASTTupleMembLL **ll, ASTTupleMemb *item)
{
    ASTTupleMembLL *n = newASTTupleMembLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

ASTBlock *newASTBlock(Token startTok, ASTStmtLL *stmts)
{
    ASTBlock *b = malloc(sizeof(ASTBlock));

    b->startTok = startTok;
    b->stmts = stmts;

    return b;
}


ASTStmt *allocASTStmt(ASTStmtKind kind, Token startTok)
{
    ASTStmt *s = malloc(sizeof(ASTStmt));

    s->kind = kind;
    s->startTok = startTok;
    s->isTaggedUnionLetDecl = false;
    s->ifTaggedUnionType = NULL;
    s->belongsToMatchExprIndex = 0;
    
    return s;
}
ASTStmt *newASTStmtDecl(ASTDecl *decl)
{
    ASTStmt *s = allocASTStmt(A_STMT_DECL, decl->startTok);

    s->decl.decl = decl;

    return s;
}
ASTStmt *newASTStmtIf(Token startTok, Token endTok, ASTExpr *cond, ASTBlock *block, ASTBlock *elseBlock)
{
    ASTStmt *s = allocASTStmt(A_STMT_IF, startTok);
    s->endTok = endTok;

    s->ifStmt.cond = cond;
    s->ifStmt.block = block;
    s->ifStmt.elseBlock = elseBlock;

    return s;
}
ASTStmt *newASTStmtWhile(ASTExpr *expr, ASTBlock *block)
{
    ASTStmt *s = allocASTStmt(A_STMT_WHILE, expr->startTok);

    s->whileStmt.expr = expr;
    s->whileStmt.block = block;
    
    return s;
}
ASTStmt *newASTStmtFor(ASTExpr *l, ASTExpr *r, ASTBlock *block)
{
    ASTStmt *s = allocASTStmt(A_STMT_FOR, l->startTok);

    s->forStmt.l = l;
    s->forStmt.r = r;
    s->forStmt.block = block;

    s->forStmt.indexSymEntry = NULL;
    s->forStmt.lExprSymEntry = NULL;
    return s;
}
ASTStmt *newASTStmtRet(Token startTok, ASTExpr *expr)
{
    ASTStmt *s = allocASTStmt(A_STMT_RET, startTok);

    s->retStmt.expr = expr;

    return s;
}
ASTStmt *newASTStmtCont(Token startTok)
{
    ASTStmt *s = allocASTStmt(A_STMT_CONTINUE, startTok);

    return s;
}
ASTStmt *newASTStmtBreak(Token startTok)
{
    ASTStmt *s = allocASTStmt(A_STMT_BREAK, startTok);
    return s;
}
ASTStmt *newASTStmtDefer(ASTBlock *block)
{
    ASTStmt *s = allocASTStmt(A_STMT_DEFER, block->startTok);

    s->deferStmt.block = block;

    return s;
}
ASTStmt *newASTStmtUsingNormal(Token startTok, ASTExpr *expr)
{
    ASTStmt *s = allocASTStmt(A_STMT_USING, startTok);

    s->usingStmt.kind = A_STMT_USING_NORMAL;
    s->usingStmt.expr = expr;

    return s;
}
ASTStmt *newASTStmtUsingImport(Token startTok, ASTDecl *import)
{
    ASTStmt *s = allocASTStmt(A_STMT_USING, startTok);

    s->usingStmt.kind = A_STMT_USING_IMPORT;
    s->usingStmt.importDecl = import;

    return s;
}
ASTStmt *newASTStmtMatch(ASTExprLL *matchExprLL, ASTMatchArmLL *matchArmLL, Token startTok)
{
    ASTStmt *s = allocASTStmt(A_STMT_MATCH, startTok);

    s->match.matchExprLL = matchExprLL;
    s->match.matchArmLL = matchArmLL;

    s->match.matchExprList = NULL;
    s->match.matchExprCompilerIdens = NULL;
    s->match.matchExprCompilerIdenSymEntry = NULL;
    
    return s;
}
ASTStmt *newASTStmtAssign(ASTExpr *l, ASTExpr *r)
{
    ASTStmt *s = allocASTStmt(A_STMT_ASSIGN, l->startTok);

    s->assign.l = l;
    s->assign.r = r;

    return s;
}
ASTStmt *newASTStmtExpr(ASTExpr *expr)
{
    ASTStmt *s = allocASTStmt(A_STMT_EXPR, expr->startTok);

    s->expr.expr = expr;

    return s;
}
ASTStmt *newASTStmtBlock(ASTBlock *block)
{
    ASTStmt *s = allocASTStmt(A_STMT_BLOCK, block->startTok);
    s->blockStmt.block = block;

    return s;
}

ASTStmtLL *newASTStmtLL(ASTStmt *item)
{
    ASTStmtLL *ll = malloc(sizeof(ASTStmtLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTStmtlLL(ASTStmtLL **ll, ASTStmt *item)
{
    ASTStmtLL *n = newASTStmtLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
void insertBeforeASTStmtLL(ASTStmtLL *before, ASTStmtLL *toInsert)
{
    if(before == NULL)
    {
        return;
    }

    ASTStmtLL *temp = before->prev;
    
    ASTStmtLL *top = toInsert;
    while(top->next != NULL) top = toInsert->next;

    before->prev = top;
    top->next = before;

    if(temp != NULL)
    {
        temp->next = toInsert->first;
        toInsert->first->prev = temp;
    }

    if(before == before->first)
    {
        toInsert->first->numItems += before->first->numItems;
        ASTStmtLL *s = before->first;

        while(s != NULL)
        {
            s->first = toInsert->first;

            s = s->next;
        }

    }
    else
    {
        before->first->numItems += toInsert->first->numItems;
        toInsert->first = before->first;
    }
}
void insertAfterASTStmtLL(ASTStmtLL *before, ASTStmtLL *toInsert)
{
    if(before == NULL)
    {
        return;
    }

    ASTStmtLL *temp = before->next;
    
    ASTStmtLL *top = toInsert;
    while(top->next != NULL) top = toInsert->next;

    before->next = toInsert->first;
    top->first->prev = before;

    if(temp != NULL)
    {
        temp->prev = top;
        top->next = temp;
    }

    before->first->numItems += toInsert->first->numItems;
    toInsert = toInsert->first;
    while(toInsert != NULL)
    {
        toInsert->first = before->first;
        toInsert = toInsert->next;
    }
}
ASTStmtLL *getASTStmtLLAt(ASTStmtLL *ll, size_t index)
{
    if(ll == NULL) return NULL;

    size_t i = 0;
    
    ll = ll->first;

    while(ll != NULL)
    {
        if(i == index) return ll;

        i++;
        ll = ll->next;
    }

    return NULL;
}

ASTMatchArm *newASTMatchArm(Token startTok, ASTExprLL **armExprs, size_t armExprsCount, ASTBlock *block)
{
    ASTMatchArm *a = malloc(sizeof(ASTMatchArm));

    a->startTok = startTok;
    a->armExprLLs = armExprs;
    a->armExprLLsCount = armExprsCount;
    a->block = block;
    a->taggedUnionAutoCasts = NULL;

    return a;
}

ASTMatchArmLL *newASTMatchArmLL(ASTMatchArm *item)
{
    ASTMatchArmLL *ll = malloc(sizeof(ASTMatchArmLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTMatchArmlLL(ASTMatchArmLL **ll, ASTMatchArm *item)
{
    ASTMatchArmLL *n = newASTMatchArmLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}


ASTDecl *allocASTDecl(ASTDeclKind kind, Token startTok)
{
    ASTDecl *decl = malloc(sizeof(ASTDecl));
    decl->kind = kind;
    decl->startTok = startTok;
    decl->tags = NULL;
    decl->isCDecl = false;
    decl->tbl = NULL;
    decl->importedIdenTbl = NULL;
    decl->declType = NULL;
    decl->genParams = NULL;
    decl->genParamsResolvedSymTbl = NULL;
    decl->backendValRef = NULL;

    return decl;
}
ASTDecl *newASTDeclVar(ASTExpr *idenExpr, ASTType *type, ASTExpr *initial)
{
    ASTDecl *d = allocASTDecl(A_DECL_VAR, idenExpr->startTok);

    d->var.idenExpr = idenExpr;
    d->var.type = type;
    d->var.initial = initial;
    d->var.isUsingDecl = false;
    d->var.globalVarInitialCodeGenStmts = NULL;
    
    return d;
}
ASTDecl *newASTDeclConst(Token name, ASTType *type, ASTExpr *initial)
{
    ASTDecl *d = allocASTDecl(A_DECL_CONST, name);

    d->constDecl.iden = name;
    d->constDecl.type = type;
    d->constDecl.initial = initial;

    return d;
}
ASTDecl *newASTDeclType(Token name, ASTType *type)
{
    ASTDecl *d = allocASTDecl(A_DECL_TYPE, name);
    
    d->type.iden = name;
    d->type.type = type;

    return d;
}
ASTDecl *newASTDeclFunc(Token iden, ASTGenericTypeLL *genParams, ASTFuncSig *sig, ASTBlock *block, bool mustReturn)
{
    ASTDecl *d = allocASTDecl(A_DECL_FUNC, iden);

    d->func.iden = iden;
    d->genParams = genParams;
    d->func.sig = sig;
    d->func.block = block;
    d->func.mustReturn = mustReturn;
    
    return d;
}
ASTDecl *newASTDeclOperFunc(Token op, ASTFuncSig *sig, ASTBlock *block)
{
    ASTDecl *d = allocASTDecl(A_DECL_OPERATOR_FUNC, op);
    d->operFunc.op = op;
    d->operFunc.sig = sig;
    d->operFunc.block = block;
    d->operFunc.mangledName = NULL;
    
    return d;
}
ASTDecl *newASTDeclStruct(Token iden, ASTGenericTypeLL *genParams, ASTDeclLL *membs)
{
    ASTDecl *d = allocASTDecl(A_DECL_STRUCT, iden);
    
    d->structDecl.iden = iden;
    d->structDecl.membs = membs;
    d->genParams = genParams;

    return d;
}
ASTDecl *newASTDeclUnion(Token iden, ASTGenericTypeLL *genParams, ASTDeclLL *membs)
{
    ASTDecl *d = allocASTDecl(A_DECL_UNION, iden);
    
    d->unionDecl.iden = iden;
    d->unionDecl.membs = membs;
    d->genParams = genParams;

    return d;
}
ASTDecl *newASTDeclUnionTagged(Token iden, ASTType *tagTypeName, ASTGenericTypeLL *genParams, ASTDeclLL *membs)
{
    ASTDecl *d = allocASTDecl(A_DECL_UNION_TAGGED, iden);
    
    d->unionTaggedDecl.iden = iden;
    d->unionTaggedDecl.tagTypeName = tagTypeName;
    d->unionTaggedDecl.membs = membs;
    d->genParams = genParams;

    return d;
}
ASTDecl *newASTDeclEnum(Token iden, ASTEnumMembLL *membs)
{
    ASTDecl *d = allocASTDecl(A_DECL_ENUM, iden);
    
    d->enumDecl.iden = iden;
    d->enumDecl.membs = membs;

    return d;
}
ASTDecl *newASTDeclImport(Token iden, bool hasIden, ASTNamespaceName *namespace)
{
    ASTDecl *d = allocASTDecl(A_DECL_IMPORT, namespace->startTok);
    
    d->import.iden = iden;
    d->import.namespace = namespace;
    d->import.hasIden = hasIden;

    return d;
}
ASTDecl *newASTDeclDeclList(ASTDeclLL *declLL, Token startTok)
{
    ASTDecl *d = allocASTDecl(A_DECL_DECLLIST, startTok);
    
    d->declList.declLL = declLL;

    return d;
}
ASTDecl *newASTDeclMethodBlock(Token startTok, ASTType *type, ASTDeclLL *declLL)
{
    ASTDecl *d = allocASTDecl(A_DECL_METHOD_BLOCK, startTok);
    d->methodBlock.type = type;
    d->methodBlock.declLL = declLL;

    return d;
}

ASTDecl *deepCopyASTDecl(ASTDecl *decl)
{
    ASTDecl *d = malloc(sizeof(ASTDecl));
    
    *d = *decl;

    switch(d->kind)
    {
        case A_DECL_FUNC:
        {
            d->func.block = deepCopyASTBlock(decl->func.block);
            d->func.sig = deepCopyASTFuncSig(decl->func.sig);
        }break;
        case A_DECL_VAR:
        {
            d->var.initial = deepCopyASTExpr(decl->var.initial);
            d->var.type = deepCopyASTType(decl->var.type);

            d->var.globalVarInitialCodeGenStmts = NULL;
        }break;
        default:
        {
            fprintf(stderr, "Unhandled deepcopy of ast decl\n");
        }break;
    }

    return d;
}
ASTBlock *deepCopyASTBlock(ASTBlock *block)
{
    ASTBlock *b = malloc(sizeof(ASTBlock));
    *b = *block;
    b->stmts = deepCopyASTStmtLL(block->stmts);

    return b;
}
ASTStmtLL *deepCopyASTStmtLL(ASTStmtLL *stmtLL)
{
    if(stmtLL == NULL) return NULL;

    ASTStmtLL *s = newASTStmtLL(deepCopyASTStmt(stmtLL->first->item));

    {
        ASTStmtLL *currStmtLL = stmtLL->first->next;
        while(currStmtLL != NULL)
        {
            addASTStmtlLL(&s, deepCopyASTStmt(currStmtLL->item));
            currStmtLL = currStmtLL->next;
        }
    }

    return s;
}
ASTStmt *deepCopyASTStmt(ASTStmt *stmt)
{
    ASTStmt *s = malloc(sizeof(ASTStmt));
    *s = *stmt;

    switch(s->kind)
    {
        case A_STMT_DECL:
        {
            s->decl.decl = deepCopyASTDecl(stmt->decl.decl);
        }break;
        default:
        {
            fprintf(stderr, "Unhandled deepcopy ast stmt");
        }break;
    }

    return s;
}
ASTFuncSig *deepCopyASTFuncSig(ASTFuncSig *sig)
{
    ASTFuncSig *s = malloc(sizeof(ASTFuncSig));
    *s = *sig;

    s->params = deepCopyASTFuncParamLL(sig->params);

    return s;
}
ASTFuncParamLL *deepCopyASTFuncParamLL(ASTFuncParamLL *paramLL)
{
    if(paramLL == NULL) return NULL;

    ASTFuncParamLL *f = newASTFuncParamLL(deepCopyASTFuncParam(paramLL->first->item));

    {
        ASTFuncParamLL *currParamLL = paramLL->first->next;
        while(currParamLL != NULL)
        {
            addASTFuncParamLL(&f, deepCopyASTFuncParam(currParamLL->item));
            currParamLL = currParamLL->next;
        }
    }

    return f;
}
ASTFuncParam *deepCopyASTFuncParam(ASTFuncParam *param)
{
    ASTFuncParam *f = malloc(sizeof(ASTFuncParam));

    *f = *param;

    f->defValue = deepCopyASTExpr(param->defValue);

    return f;
}
ASTExpr *deepCopyASTExpr(ASTExpr *expr)
{
    if(expr == NULL) return NULL;

    ASTExpr *e = malloc(sizeof(ASTExpr));
    *e = *expr;
    e->hasBeenChecked = false;
    e->genAnyCastForDefaultValueExpr = false;
    e->genIdenName = NULL;
    e->genFuncRetAsArgForDefaultValueExpr = false;
    e->idenSymEntry = NULL;

    switch(e->kind)
    {
        default:
        {
            fprintf(stderr, "Unhandled deepcopy ast expr\n");
        }break;
    }

    return e;
}
ASTType *deepCopyASTType(ASTType *type)
{
    ASTType *t = malloc(sizeof(ASTType));
    
    *t = *type;

    switch(t->kind)
    {
        default:
        {
            fprintf(stderr, "Unhandled deepcopy of ast TYPE\n");
        }break;
    }

    return t;
}

ASTDeclLL *newASTDeclLL(ASTDecl *item)
{
    ASTDeclLL *ll = malloc(sizeof(ASTDeclLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTDeclLL(ASTDeclLL **ll, ASTDecl *item)
{
    if((*ll) == NULL) return;

    ASTDeclLL *n = newASTDeclLL(item);
    
    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
void insertBeforeASTDeclLL(ASTDeclLL *before, ASTDeclLL *toInsert)
{
    if(before == NULL)
    {
        return;
    }

    ASTDeclLL *temp = before->prev;
    
    ASTDeclLL *top = toInsert;
    while(top->next != NULL) top = toInsert->next;

    before->prev = top;
    top->next = before;

    if(temp != NULL)
    {
        temp->next = toInsert->first;
        toInsert->first->prev = temp;
    }

    if(before == before->first)
    {
        toInsert->first->numItems += before->first->numItems;
        ASTDeclLL *s = before->first;

        while(s != NULL)
        {
            s->first = toInsert->first;

            s = s->next;
        }

    }
    else
    {
        before->first->numItems += toInsert->first->numItems;
        toInsert->first = before->first;
    }
}
void insertAfterASTDeclLL(ASTDeclLL *before, ASTDeclLL *toInsert)
{
    if(before == NULL)
    {
        return;
    }

    ASTDeclLL *temp = before->next;
    
    ASTDeclLL *top = toInsert;
    while(top->next != NULL) top = toInsert->next;

    before->next = toInsert->first;
    top->first->prev = before;

    if(temp != NULL)
    {
        temp->prev = top;
        top->next = temp;
    }

    before->first->numItems += toInsert->first->numItems;
    toInsert = toInsert->first;
    while(toInsert != NULL)
    {
        toInsert->first = before->first;
        toInsert = toInsert->next;
    }
}
bool ASTDeclLLOccursBefore(ASTDeclLL *ll, ASTDeclLL *occursBefore)
{
    if( (ll == NULL) || (occursBefore == NULL)) return false;

    ll = ll->prev;
    while(ll != NULL)
    {
        if(ll == occursBefore) return true;
        ll = ll->prev;
    }

    return false;
}

ASTTag *newASTTag(Token name, ASTTagParamLL *params)
{
    ASTTag *t = malloc(sizeof(ASTTag));
    t->name = name;
    t->paramLL = params;

    return t;
}

ASTTagLL *newASTTagLL(ASTTag *item)
{
    ASTTagLL *ll = malloc(sizeof(ASTTagLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTTagLL(ASTTagLL **ll, ASTTag *item)
{
    ASTTagLL *n = newASTTagLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
void insertAfterASTTagLL(ASTTagLL *before, ASTTagLL *toInsert)
{
    if(before == NULL)
    {
        return;
    }

    ASTTagLL *temp = before->next;
    
    ASTTagLL *top = toInsert;
    while(top->next != NULL) top = toInsert->next;

    before->next = toInsert->first;
    top->first->prev = before;

    if(temp != NULL)
    {
        temp->prev = top;
        top->next = temp;
    }

    before->first->numItems += toInsert->first->numItems;

    toInsert = toInsert->first;
    while(toInsert != NULL)
    {
        toInsert->first = before->first;
        toInsert = toInsert->next;
    }
}

ASTTagParamLL *newASTTagParamLL(Token item)
{
    ASTTagParamLL *ll = malloc(sizeof(ASTTagParamLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTTagParamLL(ASTTagParamLL **ll, Token item)
{
    ASTTagParamLL *n = newASTTagParamLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

ASTEnumMemb *newASTEnumMemb(Token name, ASTExpr *val)
{
    ASTEnumMemb *e = malloc(sizeof(ASTEnumMemb));
    
    e->id = name;
    e->value = val;

    return e;
}

ASTEnumMembLL *newASTEnumMembLL(ASTEnumMemb *item)
{
    ASTEnumMembLL *ll = malloc(sizeof(ASTEnumMembLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTEnumMembLL(ASTEnumMembLL **ll, ASTEnumMemb *item)
{
    ASTEnumMembLL *n = newASTEnumMembLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}


ASTCompilerTag *newASTCompilerTag(Token startTok, Token endTok, Token name, ASTTagParamLL *params)
{
    ASTCompilerTag *t = malloc(sizeof(ASTCompilerTag));
    t->startTok = startTok;
    t->endTok = endTok;
    t->name = name;
    t->paramLL = params;

    return t;
}

ASTCompilerTagLL *newASTCompilerTagLL(ASTCompilerTag *item)
{
    ASTCompilerTagLL *ll = malloc(sizeof(ASTCompilerTagLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTCompilerTagLL(ASTCompilerTagLL **ll, ASTCompilerTag *item)
{
    ASTCompilerTagLL *n = newASTCompilerTagLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

ASTNamespaceWithBlock *newASTNamespaceWithBlock(Token startTok, Token endTok, ASTNamespaceWithStmtLL *stmts)
{
    ASTNamespaceWithBlock *b = malloc(sizeof(ASTNamespaceWithBlock));
    b->startTok = startTok;
    b->endTok = endTok;
    b->stmts = stmts;

    return b;
}
ASTNamespaceWithStmt *newASTNamespaceWithStmt(Token iden, ASTNamespaceWithValueLL *vals)
{
    ASTNamespaceWithStmt *s = malloc(sizeof(ASTNamespaceWithStmt));
    s->iden = iden;
    s->vals = vals;
    if(vals == NULL) s->endTok = s->iden;
    else s->endTok = s->vals->item->val;

    return s;
}
ASTNamespaceWithValue *newASTNamespaceWithValue(Token val)
{
    ASTNamespaceWithValue *v = malloc(sizeof(ASTNamespaceWithValue));
    v->val = val;

    return v;
}

ASTNamespaceWithStmtLL *newASTNamespaceWithStmtLL(ASTNamespaceWithStmt *item)
{
    ASTNamespaceWithStmtLL *ll = malloc(sizeof(ASTNamespaceWithStmtLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTNamespaceWithStmtLL(ASTNamespaceWithStmtLL **ll, ASTNamespaceWithStmt *item)
{
    ASTNamespaceWithStmtLL *n = newASTNamespaceWithStmtLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

ASTNamespaceWithValueLL *newASTNamespaceWithValueLL(ASTNamespaceWithValue *item)
{
    ASTNamespaceWithValueLL *ll = malloc(sizeof(ASTNamespaceWithValueLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTNamespaceWithValueLL(ASTNamespaceWithValueLL **ll, ASTNamespaceWithValue *item)
{
    ASTNamespaceWithValueLL *n = newASTNamespaceWithValueLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

ASTProg *newASTProg(ASTNamespaceName *namespace, ASTNamespaceWithBlock *withBlock, bool isNamespaced, ASTCompilerTagLL *tags,ASTDeclLL *decls)
{
    ASTProg *p = malloc(sizeof(ASTProg));

    p->namespace = namespace;
    p->withBlock = withBlock;
    p->isNamespaced = isNamespaced;
    p->progTags = tags;
    p->declLL = decls;

    p->globalImportDecls = NULL;
    p->globalTypeDecls = NULL;
    p->globalStructuredDecls = NULL;
    p->globalEnumDecls = NULL;
    p->globalFuncDecls = NULL;
    p->globalVarOrConstDecls = NULL;
    p->globalMethodBlockDecls = NULL;

    p->tbl = NULL;
    p->hasBeenChecked = false;

    p->hasGeneratedHeader = false;
    p->hasGeneratedDecls= false;

    return p;
}
ASTProgLL *newASTProgLL(ASTProg *item)
{
    ASTProgLL *ll = malloc(sizeof(ASTProgLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addASTProgLL(ASTProgLL **ll, ASTProg *item)
{
    ASTProgLL *n = newASTProgLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

ASTNamespaceName *newASTNamespaceName(Token parent, ASTNamespaceName *scopedChild)
{

    ASTNamespaceName *n = malloc(sizeof(ASTNamespaceName));
    n->parent = parent;
    n->scopedChild = scopedChild;
    n->startTok = parent;

    if(scopedChild != NULL) n->endTok = scopedChild->endTok;
    else n->endTok = n->startTok;

    return n;
}
bool isExprDiscardIdentifier(ASTExpr *expr)
{
    if(expr->kind == A_EXPR_IDEN) return !strcmp(expr->iden.lexeme, "_");

    return false;
}
