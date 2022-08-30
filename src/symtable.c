#include "namespacetable.h"
#include "symtable.h"

SymTable *symTable = NULL;
SymTableLL *checkedSymTables = NULL;

SymEntry *newSymEntry(char *name, CheckerType *type)
{
    SymEntry *e = malloc(sizeof(SymEntry));
    strcpy(e->name, name);
    e->type = type;

    e->isNamespace = false;
    e->isConst = false;
    e->isGlobal = false;
    e->isGlobalFunc = false;
    e->isType = false;
    e->isActualConst = false;
    e->myDecl = NULL;
    e->importIsNamespaceImport = false;
    e->backendValRef = NULL;
    
    return e;
}
SymEntryLL *newSymEntryLL(SymEntry *item)
{
    SymEntryLL *ll = malloc(sizeof(SymEntryLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addSymEntryLL(SymEntryLL **ll, SymEntry *item)
{
    SymEntryLL *n = newSymEntryLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
void removeSymEntryLL(SymEntryLL **ll)
{
    if((*ll) == NULL) return;

    //TODO: free SymEntryLL when removing
    
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

SymTable *newSymTable(NSTEntry *belongsToNamespace)
{
    SymTable *table = malloc(sizeof(SymTable));

    table->namespaceTbl = NULL;
    table->globalTbl = NULL;
    table->localTbl = NULL;
    table->constTbl = NULL;
    table->typeTbl = NULL;
    table->moduleTbl = NULL;

    table->continuationTbls = NULL;
    table->belongsToNamespace = belongsToNamespace;

    return table;
}
SymTableLL *newSymTableLL(SymTable *item)
{
    SymTableLL *ll = malloc(sizeof(SymTableLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addSymTableLL(SymTableLL **ll, SymTable *item)
{
    SymTableLL *n = newSymTableLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
bool symTableInSymTableLL(SymTableLL *ll, SymTable *item)
{
    if((ll == NULL) || (item == NULL)) return false;

    SymTableLL *curr = ll->first;

    while(curr != NULL)
    {
        if(curr->item == item) return true;
        curr = curr->next;
    }

    return false;
}

TypeMethodTableEntry *newTypeMethodTableEntry(CheckerType *type)
{
    TypeMethodTableEntry *e = malloc(sizeof(TypeMethodTableEntry));
    e->type = type;
    e->methodTble = newSymTable(NULL);

    return e;
}
TypeMethodTableEntryLL *newTypeMethodTableEntryLL(TypeMethodTableEntry *item)
{
    TypeMethodTableEntryLL *ll = malloc(sizeof(TypeMethodTableEntryLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addTypeMethodTableEntryLL(TypeMethodTableEntryLL **ll, TypeMethodTableEntry *item)
{
    TypeMethodTableEntryLL *n = newTypeMethodTableEntryLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
void removeTypeMethodTableEntryyLL(TypeMethodTableEntryLL **ll)
{
    if((*ll) == NULL) return;

    //TODO: free SymEntryLL when removing
    
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

TypeMethodTable *newTypeMethodTable()
{
    TypeMethodTable *t = malloc(sizeof(TypeMethodTable));
    t->entries = NULL;

    return t;
}

SymEntry *typeMethodTableInsert(TypeMethodTable *tmt, CheckerType *type, char *str, CheckerType *funcType, Token posInFile)
{
    if(tmt == NULL) return NULL;
    
    TypeMethodTableEntry *existingEntry = NULL;

    if(tmt->entries == NULL)
    {
        TypeMethodTableEntry *entry = newTypeMethodTableEntry(type);
        tmt->entries = newTypeMethodTableEntryLL(entry);
        existingEntry = entry;
    }
    else
    {

        { //try find exisitng entry
            TypeMethodTableEntryLL *currEntry = tmt->entries->first;
            while(currEntry != NULL)
            {
                if(areTypesEqual(type, currEntry->item->type)) 
                {
                    existingEntry = currEntry->item;
                    break;
                }
                currEntry = currEntry->next;
            }
        }
    }
    
    if(existingEntry == NULL)
    {
        TypeMethodTableEntry *entry = newTypeMethodTableEntry(type);
        addTypeMethodTableEntryLL(&(tmt->entries), entry);
        existingEntry = entry;
    }

    SymEntry *e = _symTableInsertGlobal(existingEntry->methodTble, str, funcType, posInFile);
    return e;
}
SymEntry *typeMethodTableLookUp(TypeMethodTable *tmt, CheckerType *type, char *str)
{
    if(tmt == NULL) return NULL;
    
    if(tmt->entries == NULL) return NULL;

    TypeMethodTableEntry *existingEntry = NULL;

    { //try find
        TypeMethodTableEntryLL *currEntry = tmt->entries->first;
        while(currEntry != NULL)
        {
            if(areTypesEqual(type, currEntry->item->type)) 
            {
                existingEntry = currEntry->item;
                break;
            }
            currEntry = currEntry->next;
        }
    }

    if(existingEntry == NULL) return NULL;

    return _symTableLookupGlobal(existingEntry->methodTble, str);
}

SymEntry *_symTableInsertNamespace(SymTable *table, char *str, CheckerType *type, Token posInFile)
{
    if(table == NULL) return NULL;

    SymEntry *entry = newSymEntry(str, type);
    entry->isNamespace = true;
    entry->mySymTable = table;
    entry->posInFile = posInFile;

    if(table->namespaceTbl == NULL)
    {
        table->namespaceTbl = newSymEntryLL(entry);
        return entry;
    }

    addSymEntryLL(&(table->namespaceTbl), entry);

    return entry;

}
SymEntry *_symTableInsertGlobal(SymTable *table, char *str, CheckerType *type, Token posInFile)
{
    if(table == NULL) return NULL;
    
    SymEntry *entry = newSymEntry(str, type);
    entry->isGlobal = true;
    entry->mySymTable = table;
    entry->posInFile = posInFile;

    if(table->globalTbl == NULL)
    {
        table->globalTbl = newSymEntryLL(entry);
        return entry;
    }

    addSymEntryLL(&(table->globalTbl), entry);

    return entry;

}
SymEntry *_symTableInsertLocal(SymTable *table, char *str, CheckerType *type, Token posInFile)
{
    if(table == NULL) return NULL;
    
    SymEntry *entry = newSymEntry(str, type);
    entry->mySymTable = table;
    entry->posInFile = posInFile;

    if(table->localTbl == NULL)
    {
        table->localTbl = newSymEntryLL(entry);
        return entry;
    }

    addSymEntryLL(&(table->localTbl), entry);

    return entry;
}
SymEntry *_symTableInsertConst(SymTable *table, char *str, CheckerType *type, Token posInFile)
{
    if(table == NULL) return NULL;

    SymEntry *entry = newSymEntry(str, type);
    entry->isConst = true;
    entry->mySymTable = table;
    entry->posInFile = posInFile;

    if(table->constTbl == NULL)
    {
        table->constTbl = newSymEntryLL(entry);
        return entry;
    }

    addSymEntryLL(&(table->constTbl), entry);

    return entry;
}
SymEntry *_symTableInsertType(SymTable *table, char *str, CheckerType *type, Token posInFile)
{
    if(table == NULL) return NULL;

    SymEntry *entry = newSymEntry(str, type);
    entry->isType = true;
    entry->mySymTable = table;
    entry->posInFile = posInFile;

    if(table->typeTbl == NULL)
    {
        table->typeTbl = newSymEntryLL(entry);
        return entry;
    }

    addSymEntryLL(&(table->typeTbl), entry);

    return entry;
}

SymEntry *_symTableInsertSymEntryNamespace(SymTable *table, SymEntry *symEntry)
{
    if(table == NULL) return NULL;
    
    symEntry->isNamespace = true;

    if(table->namespaceTbl == NULL)
    {
        table->namespaceTbl = newSymEntryLL(symEntry);
        return symEntry;
    }

    addSymEntryLL(&(table->namespaceTbl), symEntry);

    return symEntry;
}
SymEntry *_symTableInsertSymEntryGlobal(SymTable *table, SymEntry *symEntry)
{
    if(table == NULL) return NULL;
    
    symEntry->isGlobal = true;

    if(table->globalTbl == NULL)
    {
        table->globalTbl = newSymEntryLL(symEntry);
        return symEntry;
    }

    addSymEntryLL(&(table->globalTbl), symEntry);

    return symEntry;
}
SymEntry *_symTableInsertSymEntryConst(SymTable *table, SymEntry *symEntry)
{
    if(table == NULL) return NULL;
    
    symEntry->isConst = true;

    if(table->constTbl == NULL)
    {
        table->constTbl = newSymEntryLL(symEntry);
        return symEntry;
    }

    addSymEntryLL(&(table->constTbl), symEntry);

    return symEntry;
}
SymEntry *_symTableInsertSymEntryType(SymTable *table, SymEntry *symEntry)
{
    if(table == NULL) return NULL;
    
    symEntry->isType = true;

    if(table->typeTbl == NULL)
    {
        table->typeTbl = newSymEntryLL(symEntry);
        return symEntry;
    }

    addSymEntryLL(&(table->typeTbl), symEntry);

    return symEntry;
}

SymEntry *_symTableLookupNamespace(SymTable *table, char *str)
{
    if(table->namespaceTbl != NULL)
    {
        SymEntryLL *currEntry = table->namespaceTbl->first;

        while(currEntry != NULL)
        {
            if(!strcmp(str, currEntry->item->name))
            {
                return currEntry->item;
            }

            currEntry = currEntry->next;
        }

    }

    {
        if(table->continuationTbls != NULL) 
        {
            SymTableLL *currSymTbl = table->continuationTbls->first;
            SymEntry *entry = NULL;
            while(currSymTbl != NULL)
            {
                if(symTableInSymTableLL(checkedSymTables, table)) 
                {
                    currSymTbl = currSymTbl->next;
                    continue;
                }

                SymTableLL *restore = checkedSymTables;
                
                if(checkedSymTables == NULL) checkedSymTables = newSymTableLL(table);
                else addSymTableLL(&checkedSymTables, table);

                entry = _symTableLookupNamespace(currSymTbl->item, str);

                //todo use remove function
                checkedSymTables = restore;
                if(entry != NULL) break;

                currSymTbl = currSymTbl->next;
            }

            return entry;
        }
    }
    
    return NULL;
}
SymEntry *_symTableLookupGlobal(SymTable *table, char *str)
{
    if(table->globalTbl != NULL)
    {
        SymEntryLL *currEntry = table->globalTbl->first;

        
        while(currEntry != NULL)
        {
            if(!strcmp(str, currEntry->item->name))
            {
                return currEntry->item;
            }

            currEntry = currEntry->next;
        }
    }

    if(table->continuationTbls != NULL) 
    {
        SymTableLL *currSymTbl = table->continuationTbls->first;
        SymEntry *entry = NULL;
        while(currSymTbl != NULL)
        {
            if(symTableInSymTableLL(checkedSymTables, table)) 
            {
                currSymTbl = currSymTbl->next;
                continue;
            }

            SymTableLL *restore = checkedSymTables;
            
            if(checkedSymTables == NULL) checkedSymTables = newSymTableLL(table);
            else addSymTableLL(&checkedSymTables, table);

            entry = _symTableLookupGlobal(currSymTbl->item, str);

            //todo use remove function
            checkedSymTables = restore;
            if(entry != NULL) break;

            currSymTbl = currSymTbl->next;
        }

        return entry;
    }

    return NULL;
}
SymEntry *_symTableLookupLocal(SymTable *table, char *str)
{
    if(table->localTbl != NULL)
    {
        SymEntryLL *currEntry = table->localTbl;
    
        while(currEntry != NULL)
        {
            if(!strcmp(str, currEntry->item->name))
            {
                return currEntry->item;
            }

            currEntry = currEntry->prev;
        }
    }


    if(table->continuationTbls != NULL) 
    {
        SymTableLL *currSymTbl = table->continuationTbls->first;
        SymEntry *entry = NULL;
        while(currSymTbl != NULL)
        {
            entry = _symTableLookupLocal(currSymTbl->item, str);
            if(entry != NULL) break;

            currSymTbl = currSymTbl->next;
        }

        return entry;
    }

    return NULL;
}
SymEntry *_symTableLookupLocalUpToEntry(SymTable *table, SymEntryLL *stopBefore, char *str)
{
    if(table->localTbl != NULL)
    {
        SymEntryLL *currEntry = table->localTbl;
    
        while((currEntry != NULL) && (currEntry != stopBefore))
        {
            if(!strcmp(str, currEntry->item->name))
            {
                return currEntry->item;
            }

            currEntry = currEntry->prev;
        }
    }


    if(table->continuationTbls != NULL) 
    {
        SymTableLL *currSymTbl = table->continuationTbls->first;
        SymEntry *entry = NULL;
        while(currSymTbl != NULL)
        {
            entry = _symTableLookupLocalUpToEntry(currSymTbl->item, stopBefore, str);
            if(entry != NULL) break;

            currSymTbl = currSymTbl->next;
        }

        return entry;
    }

    return NULL;
}
SymEntry *_symTableLookupConst(SymTable *table, char *str)
{
    if(table->constTbl != NULL)
    {
        SymEntryLL *currEntry = table->constTbl->first;
    
        while(currEntry != NULL)
        {
            if(!strcmp(str, currEntry->item->name))
            {
                return currEntry->item;
            }

            currEntry = currEntry->next;
        }
    }
    if(table->continuationTbls != NULL) 
    {
        SymTableLL *currSymTbl = table->continuationTbls->first;
        SymEntry *entry = NULL;
        while(currSymTbl != NULL)
        {
            if(symTableInSymTableLL(checkedSymTables, table)) 
            {
                currSymTbl = currSymTbl->next;
                continue;
            }

            SymTableLL *restore = checkedSymTables;
            
            if(checkedSymTables == NULL) checkedSymTables = newSymTableLL(table);
            else addSymTableLL(&checkedSymTables, table);

            entry = _symTableLookupConst(currSymTbl->item, str);

            //todo use remove function
            checkedSymTables = restore;
            if(entry != NULL) break;

            currSymTbl = currSymTbl->next;
        }
        
        return entry;
    }

    return NULL;
}
SymEntry *_symTableLookupType(SymTable *table, char *str)
{
    if(table->typeTbl != NULL)
    {
        SymEntryLL *currEntry = table->typeTbl->first;

        while(currEntry != NULL)
        {
            if(!strcmp(str, currEntry->item->name))
            {
                return currEntry->item;
            }

            currEntry = currEntry->next;
        }
    }
    if(table->continuationTbls != NULL) 
    {
        SymTableLL *currSymTbl = table->continuationTbls->first;
        SymEntry *entry = NULL;
        while(currSymTbl != NULL)
        {
            if(symTableInSymTableLL(checkedSymTables, table)) 
            {
                currSymTbl = currSymTbl->next;
                continue;
            }

            SymTableLL *restore = checkedSymTables;
            
            if(checkedSymTables == NULL) checkedSymTables = newSymTableLL(table);
            else addSymTableLL(&checkedSymTables, table);

            entry = _symTableLookupType(currSymTbl->item, str);

            //todo use remove function
            checkedSymTables = restore;
            if(entry != NULL) break;

            currSymTbl = currSymTbl->next;
        }

        return entry;
    }

    return NULL;
}

SymEntry *_symTableLookUp(SymTable *table, char *str, int lookupFlags)
{
    bool fNamespace= lookupFlags & LOOKUP_NAMESPACE;
    bool fGlobal = lookupFlags & LOOKUP_GLOBAL;
    bool fLocal = lookupFlags & LOOKUP_LOCAL;
    bool fType = lookupFlags & LOOKUP_TYPE;
    bool fConst = lookupFlags & LOOKUP_CONST;

    SymEntry *e = NULL;
    
    if(fLocal)
    {
        e = _symTableLookupLocal(table, str);
        if(e != NULL)
        {
            return e;
        }
    }

    if(fType)
    {
        e = _symTableLookupType(table, str);
        if(e != NULL)
        {
            return e;
        }
    }

    if(fNamespace)
    {
        e = _symTableLookupNamespace(table, str);
        if(e != NULL)
        {
            return e;
        }
    }

    if(fGlobal)
    {
        e = _symTableLookupGlobal(table, str);
        if(e != NULL)
        {
            return e;
        }
    }

    if(fConst)
    {
        e = _symTableLookupConst(table, str);
        if(e != NULL)
        {
            return e;
        }
    }

    if(table->continuationTbls != NULL) 
    {
        SymTableLL *currSymTbl = table->continuationTbls->first;
        SymEntry *entry = NULL;
        while(currSymTbl != NULL)
        {
            if(symTableInSymTableLL(checkedSymTables, table)) 
            {
                currSymTbl = currSymTbl->next;
                continue;
            }

            SymTableLL *restore = checkedSymTables;
            
            if(checkedSymTables == NULL) checkedSymTables = newSymTableLL(table);
            else addSymTableLL(&checkedSymTables, table);

            entry = _symTableLookUp(currSymTbl->item, str, lookupFlags);

            //todo use remove function
            checkedSymTables = restore;
            if(entry != NULL) break;

            currSymTbl = currSymTbl->next;
        }

        return entry;
    }
    return NULL;
}

void _symTableRestoreLocal(SymTable *table, SymEntryLL *restoreTo)
{
    while((table->localTbl != restoreTo) && (table->localTbl != NULL)) removeSymEntryLL(&table->localTbl);
}

void _symTableRestoreConst(SymTable *table, SymEntryLL *restoreTo)
{
    while((table->constTbl != restoreTo) && (table->constTbl != NULL)) removeSymEntryLL(&table->constTbl);
}

void _symTableRestoreType(SymTable *table, SymEntryLL *restoreTo)
{
    while((table->typeTbl != restoreTo) && (table->typeTbl != NULL)) removeSymEntryLL(&table->typeTbl);
}

void _symTableImportSymtable(SymTable *table, SymTable *symTableToImport, int whatToImportFlags)
{
    bool fNamespace= whatToImportFlags & LOOKUP_NAMESPACE;
    bool fGlobal = whatToImportFlags & LOOKUP_GLOBAL;
    bool fLocal = whatToImportFlags & LOOKUP_LOCAL;
    bool fType = whatToImportFlags & LOOKUP_TYPE;
    bool fConst = whatToImportFlags & LOOKUP_CONST;

    if(fLocal)
    {
        SymEntryLL *currEntry = symTableToImport->localTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            _symTableInsertLocal(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);

            currEntry = currEntry->next;
        }
    }

    if(fType)
    {
        SymEntryLL *currEntry = symTableToImport->typeTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            _symTableInsertType(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            
            currEntry = currEntry->next;
        }
    }

    if(fNamespace)
    {
        SymEntryLL *currEntry = symTableToImport->namespaceTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            _symTableInsertNamespace(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            
            currEntry = currEntry->next;
        }
    }

    if(fGlobal)
    {
        SymEntryLL *currEntry = symTableToImport->globalTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            _symTableInsertGlobal(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            
            currEntry = currEntry->next;
        }
    }

    if(fConst)
    {
        SymEntryLL *currEntry = symTableToImport->constTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            _symTableInsertConst(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            
            currEntry = currEntry->next;
        }
    }
}

void _symTableImportSymtableIntoLocalScope(SymTable *table, SymTable *symTableToImport, int whatToImportFlags)
{
    bool fNamespace= whatToImportFlags & LOOKUP_NAMESPACE;
    bool fGlobal = whatToImportFlags & LOOKUP_GLOBAL;
    bool fLocal = whatToImportFlags & LOOKUP_LOCAL;
    bool fType = whatToImportFlags & LOOKUP_TYPE;
    bool fConst = whatToImportFlags & LOOKUP_CONST;

    if(fLocal)
    {
        SymEntryLL *currEntry = symTableToImport->localTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            SymEntry *e = _symTableInsertLocal(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            *e = *currEntry->item;

            currEntry = currEntry->next;
        }
    }

    if(fType)
    {
        SymEntryLL *currEntry = symTableToImport->typeTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            SymEntry *e = _symTableInsertType(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            
            *e = *currEntry->item;

            currEntry = currEntry->next;
        }
    }

    if(fNamespace)
    {
        SymEntryLL *currEntry = symTableToImport->namespaceTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            SymEntry *e = _symTableInsertLocal(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            
            *e = *currEntry->item;;

            currEntry = currEntry->next;
        }
    }

    if(fGlobal)
    {
        SymEntryLL *currEntry = symTableToImport->globalTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            SymEntry *e = _symTableInsertLocal(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            
            *e = *currEntry->item;

            currEntry = currEntry->next;
        }
    }

    if(fConst)
    {
        SymEntryLL *currEntry = symTableToImport->constTbl;
        if(currEntry != NULL) currEntry = currEntry->first;

        while(currEntry != NULL)
        {
            SymEntry *e = _symTableInsertLocal(table, currEntry->item->name, currEntry->item->type, currEntry->item->posInFile);
            
            *e = *currEntry->item;

            currEntry = currEntry->next;
        }
    }
}
