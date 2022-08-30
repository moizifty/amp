#include "ast.h"
#include "namespacetable.h"
#include "compiler_internal.h"

char *allocGetNamespaceName(ASTNamespaceName *namespace);

extern GlobalContext globalContext;

NSTEntry *newNSTEntry(char *namespace, struct ASTProgLL *progs)
{
    NSTEntry *n = malloc(sizeof(NSTEntry));
    strcpy(n->namespace, namespace);
    n->progASTs = progs;
    n->namespaceSymTable = newSymTable(n);
    n->isNestedNamespace = false;
    n->ifNestedParentEntry = NULL;
    n->withoutScopeName = NULL;
    
    n->linkerLibs = NULL;
    n->numLinkerLibs = 0;
    n->isImported = false;
    n->isTypesUsed = false;
    n->isOtherSymbolsUsed = false;
    n->importedBy = NULL;
    n->__checkingImportLinkerLibs = false;
    
    if(strcmp("prelude", namespace))
    {
        if(n->namespaceSymTable->continuationTbls == NULL) 
            n->namespaceSymTable->continuationTbls = newSymTableLL(globalContext.preludeNSEntry->namespaceSymTable);
        else
            addSymTableLL(&(n->namespaceSymTable->continuationTbls), globalContext.preludeNSEntry->namespaceSymTable);
    }

    return n;
}
NSTEntryLL *newNSTEntryLL(NSTEntry *item)
{
    NSTEntryLL *ll = malloc(sizeof(NSTEntryLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addNSTEntryLL(NSTEntryLL **ll, NSTEntry *item)
{
    NSTEntryLL *n = newNSTEntryLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
void removeNSTEntryLL(NSTEntryLL **ll)
{
    if((*ll) == NULL) return;

    //TODO: free NSTEntryLL when removing
    
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

NamespaceTable *newNamespaceTable(void)
{
    NamespaceTable *nt = malloc(sizeof(NamespaceTable));
    nt->entries = NULL;

    return nt;
}
NSTEntry *namespaceTableInsertEntry(NamespaceTable *table, char *namespace, struct ASTProgLL *progs)
{
    if(table == NULL) return NULL;

    NSTEntry *entry = newNSTEntry(namespace, progs);

    if(table->entries == NULL)
    {
        table->entries = newNSTEntryLL(entry);
        return entry;
    }

    addNSTEntryLL(&(table->entries), entry);

    return entry;
}
NSTEntry *namespaceTableLookupEntry(NamespaceTable *table, char *namespace)
{
    if(table->entries == NULL) return NULL;

    NSTEntryLL *currEntry = table->entries->first;

    while(currEntry != NULL)
    {
        if(!strcmp(namespace, currEntry->item->namespace))
            return currEntry->item;

        currEntry = currEntry->next;
    }

    return NULL;
}

NSDependencyNode *newNSDependencyNode(char *nsEntry, NSDependencyNodeLL *children)
{
    NSDependencyNode *d = malloc(sizeof(NSDependencyNode));
    strcpy(d->namespace, nsEntry);
    d->children = children;

    return d;
}
void addNSDependencyChildren(NSDependencyNode *node, NSDependencyNode *toAdd)
{
    if(node->children == NULL) node->children = newNSDependencyNodeLL(toAdd);
    else addNSDependencyNodeLL(&(node->children), toAdd);
}

NSDependencyNodeLL *newNSDependencyNodeLL(NSDependencyNode *item)
{
    NSDependencyNodeLL *ll = malloc(sizeof(NSDependencyNodeLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addNSDependencyNodeLL(NSDependencyNodeLL **ll, NSDependencyNode *item)
{
    NSDependencyNodeLL *n = newNSDependencyNodeLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

NSDependencyNode *getNamespaceDependencyNode(NSTEntry *namespace, NSTEntryLL **rootNodes)
{
    if(namespace == NULL) return NULL;

    NSDependencyNodeLL *dependencies = NULL;

    ASTProgLL *currProgLL = namespace->progASTs->first;
    
    while (currProgLL != NULL)
    {
        {
            ASTDeclLL *currImportLL = currProgLL->item->globalImportDecls;
            if(currImportLL != NULL)
            {
                currImportLL = currImportLL->first;
                while(currImportLL != NULL)
                {
                    char *nsName = allocGetNamespaceName(currImportLL->item->import.namespace);
                    NSTEntry *nsEntry = namespaceTableLookupEntry(globalContext.namespaceTable, nsName);

                    //check for circular imports
                    NSTEntry *entryCausingCircularDependency = NULL;
                    bool foundCircular = false;
                    {
                        NSTEntryLL *currRootNode = (*rootNodes)->first;
                        while(currRootNode != NULL)
                        {
                            if(!strcmp(nsEntry->namespace, currRootNode->item->namespace))
                            {
                                foundCircular = true;
                                entryCausingCircularDependency = currRootNode->item;

                                break;
                            }

                            currRootNode = currRootNode->next;
                        }
                    }
                    NSTEntryLL *restoreTop = (*rootNodes);

                    addNSTEntryLL(rootNodes, nsEntry);

                    //TODO: check if namespace exists
                    NSDependencyNode *dependency = NULL;
                    if(!foundCircular) dependency = getNamespaceDependencyNode(nsEntry, rootNodes);
                    else dependency = newNSDependencyNode(entryCausingCircularDependency->namespace, NULL);

                    if(dependencies == NULL) dependencies = newNSDependencyNodeLL(dependency);
                    else addNSDependencyNodeLL(&dependencies, dependency);

                    while((*rootNodes != restoreTop) && (*rootNodes != NULL)) removeNSTEntryLL(rootNodes);

                    currImportLL = currImportLL->next;
                }
            }
        }

        currProgLL = currProgLL->next;
    }

    char buf[MAX_IDEN_LEN];
    strcpy(buf, namespace->namespace);

    NSDependencyNode *dn = newNSDependencyNode(buf, dependencies);

    return dn;
}

