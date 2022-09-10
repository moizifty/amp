#ifndef NAMESPACETABLE_H
#define NAMESPACETABLE_H
#include "symtable.h"

typedef struct NSTEntry NSTEntry;
typedef struct NSTEntryLL NSTEntryLL;
typedef struct NSDependencyNode NSDependencyNode;
typedef struct NSDependencyNodeLL NSDependencyNodeLL;

struct NSTEntry
{
    char namespace[MAX_IDEN_LEN];
    struct ASTProgLL *progASTs;

    SymTable *namespaceSymTable;

    bool isNestedNamespace;
    struct NSTEntry *ifNestedParentEntry;
    char *withoutScopeName;

    bool isImported;
    bool isTypesUsed;
    bool isOtherSymbolsUsed;
    struct NSTEntryLL *importedBy;

    char **linkerLibs;
    size_t numLinkerLibs;

    bool __checkingImportLinkerLibs;
    size_t indexInNamespaceTable; //in backend output
};

struct NSTEntryLL
{
    NSTEntry *item;
    struct NSTEntryLL *prev;
    struct NSTEntryLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct NSTEntryLL *first; //first item in ll
};

typedef struct NamespaceTable
{
    NSTEntryLL *entries;
}NamespaceTable;

struct NSDependencyNode
{
    char namespace[MAX_IDEN_LEN];
    
    NSDependencyNodeLL *children;
};

struct NSDependencyNodeLL
{
    NSDependencyNode *item;
    struct NSDependencyNodeLL *prev;
    struct NSDependencyNodeLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct NSDependencyNodeLL *first; //first item in ll
};


NSTEntry *newNSTEntry(char *namespace, struct ASTProgLL *progs);
NSTEntryLL *newNSTEntryLL(NSTEntry *item);
void addNSTEntryLL(NSTEntryLL **ll, NSTEntry *item);
void removeNSTEntryLL(NSTEntryLL **ll);

NamespaceTable *newNamespaceTable(void);
NSTEntry *namespaceTableInsertEntry(NamespaceTable *table, char *namespace, struct ASTProgLL *progs);
NSTEntry *namespaceTableLookupEntry(NamespaceTable *table, char *namespace);

NSDependencyNode *newNSDependencyNode(char *namespace, NSDependencyNodeLL *children);
void addNSDependencyChildren(NSDependencyNode *node, NSDependencyNode *toAdd);

NSDependencyNodeLL *newNSDependencyNodeLL(NSDependencyNode *item);
void addNSDependencyNodeLL(NSDependencyNodeLL **ll, NSDependencyNode *item);

//the rootNodes parameter should be a preallocated linkedlist of rootnodes along a branch
NSDependencyNode *getNamespaceDependencyNode(NSTEntry *namespace, NSTEntryLL **rootNodes);

#endif