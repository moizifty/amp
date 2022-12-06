#include "compiler_internal.h"
#include "decl_analysis.h"

DeclDependency *allocDeclDependency(DeclDependencyKind kind)
{
    DeclDependency *dep = malloc(sizeof(DeclDependency));
    dep->kind = kind;

    return dep;
}
DeclDependency *newDeclDependencyASTDecl(ASTDecl *decl)
{
    DeclDependency *dep = allocDeclDependency(DECL_DEPEND_AST_DECL);
    dep->astDecl.decl = decl;

    return dep;
}
DeclDependency *newDeclDependencyScoped(ScopedDecl *decl)
{
    DeclDependency *dep = allocDeclDependency(DECL_DEPEND_SCOPED_DECL);
    dep->scoped.decl = decl;
    
    return dep;
}
DeclDependency *newDeclDependencyEnumMemb(EnumMemb *decl)
{
    DeclDependency *dep = allocDeclDependency(DECL_DEPEND_ENUM_MEMB);
    dep->enumMemb.decl = decl;
    
    return dep;
}


DeclDependencyLL *createASTDeclDependencies(ASTDecl *decl)
{
    return NULL;
}
DeclDependencyLL *createAliasDeclDependencies(ASTDecl *decl)
{
    DeclDependencyLL *dep;
    

    return dep;
}


DeclDependencyLL *newDependencyLL(DeclDependency *item)
{
    DeclDependencyLL *ll = malloc(sizeof(DeclDependencyLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addDependencyLL(DeclDependencyLL **ll, DeclDependency *item)
{
    DeclDependencyLL *n = newDependencyLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}