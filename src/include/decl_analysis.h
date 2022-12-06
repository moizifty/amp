#ifndef DECL_ANALYSIS_H
#define DECL_ANALYSIS_H
#include "ast.h"
#include "checker.h"

typedef struct DeclDependency DeclDependency;
typedef struct DeclDependencyLL DeclDependencyLL;

typedef enum DeclDependencyKind
{
    DECL_DEPEND_AST_DECL,
    DECL_DEPEND_SCOPED_DECL,
    DECL_DEPEND_ENUM_MEMB,
}DeclDependencyKind;

struct DeclDependency
{
    DeclDependencyKind kind;

    union
    {
        struct
        {
            ASTDecl *decl;
        }astDecl;

        struct
        {
            ScopedDecl *decl;
        }scoped;

        struct
        {
            EnumMemb *decl;
        }enumMemb;
        
    };
};

struct DeclDependencyLL
{
    DeclDependency *item;
    struct DeclDependencyLL *prev;
    struct DeclDependencyLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct DeclDependencyLL *first; //first item in ll
};

DeclDependency *allocDeclDependency(DeclDependencyKind kind);
DeclDependency *newDeclDependencyASTDecl(ASTDecl *decl);
DeclDependency *newDeclDependencyScoped(ScopedDecl *decl);
DeclDependency *newDeclDependencyEnumMemb(EnumMemb *decl);

DeclDependencyLL *createASTDeclDependencies(ASTDecl *decl);
DeclDependencyLL *createAliasDeclDependencies(ASTDecl *decl);


DeclDependencyLL *newDependencyLL(DeclDependency *item);
void addDependencyLL(DeclDependencyLL **ll, DeclDependency *item);

#endif