#include "mylib.h"

void AddToHashMapString(HashMapString *table, uint64_t key, char* value) 
{ 
    if(table == NULL) return; 
    if(IsHashMapStringFull(table)) 
    { 
        ResizeHashMapString(table); 
    } 
    uint64_t index = key % table->capacity; 
    HashMapStringEntry *entry = GetEntryHashMapStringByKey(table, key); 
    if(entry != NULL) 
    { 
        if(GetEntryHashMapStringByVal(table, key, value) == NULL) 
        AddHashMapStringEntryChainLL(&(entry->valueLL), value); 
    } 
    else 
    { 
        entry = malloc(sizeof(HashMapStringEntry)); 
        entry->key = key; 
        entry->valueLL = NewHashMapStringEntryChainLL(value); 
        table->numEntries += 1; 
        table->entries[index] = entry; 
    }
}
HashMapStringEntry *GetEntryHashMapStringByVal(HashMapString *table, uint64_t key, char* val) 
{ 
    if(table == NULL) return NULL; 
    uint64_t index = key % table->capacity; 
    HashMapStringEntry *entry = table->entries[index]; 
    if(entry == NULL) return NULL; 
    HashMapStringEntryChainLL *item = entry->valueLL->first; 
    while(item != NULL) 
    { 
        if(strcmp(item->item, val) == 0) return entry; 
        
        item = item->next; 
    } 
    return NULL; 
}
HashMapStringEntryChainLL *GetEntryChainNodeHashMapStringByVal(HashMapString *table, uint64_t key, char* val, int64_t *out_numberInChain) 
{ 
    if(table == NULL) return NULL; 
    uint64_t index = key % table->capacity; 
    HashMapStringEntry *entry = GetEntryHashMapStringByVal(table, key, val); 
    if(entry == NULL) return NULL; 
    HashMapStringEntryChainLL *item = entry->valueLL->first; 
    *out_numberInChain = 0; 
    while(item != NULL) 
    { 
        if(strcmp(item->item, val) == 0) return item; item = item->next; 
        *out_numberInChain += 1; 
    } 
    
    return NULL; 
}

HashMapStringEntry *GetEntryHashMapStringByKey(HashMapString *table, uint64_t key) 
{
     uint64_t index = key % table->capacity; 
     HashMapStringEntry *entry = table->entries[index]; 
     return entry; 
}
void ResizeHashMapString(HashMapString *table) 
{ 
    if(table == NULL) return; 
    int64_t newCapacity = (int64_t) ((float) table->capacity * 1.7f); 
    HashMapStringEntry **tempEntries = table->entries; 
    table->entries = calloc(newCapacity, sizeof(HashMapStringEntry *)); 
    for(int64_t i = 0; i < table->numEntries; i++) 
    { 
        if(tempEntries[i] == NULL) continue; 
        uint64_t newIndex = tempEntries[i]->key % newCapacity; 
        table->entries[newIndex] = tempEntries[i]; 
    } 
    
    table->capacity = newCapacity; 
    free(tempEntries);
}
HashMapString *NewHashMapString(int64_t initialCapacity) 
{ 
    HashMapString *ret = malloc(sizeof(HashMapString)); 
    ret->entries = (initialCapacity == 0) ? NULL : calloc(initialCapacity, sizeof(HashMapStringEntry *)); 
    ret->numEntries = 0; 
    ret->capacity = initialCapacity;
    return ret; 
}
bool IsHashMapStringFull(HashMapString *table) 
{ 
    return table->capacity == table->numEntries; 
}
HashMapStringEntryChainLL *NewHashMapStringEntryChainLL(char* item)
{ 
    HashMapStringEntryChainLL *ll = malloc(sizeof(HashMapStringEntryChainLL)); 
    ll->next = NULL; 
    ll->prev = NULL; 
    ll->item = strdup(item); 
    ll->numItems = 1; 
    ll->first = ll; 
    return ll;
} 
void AddHashMapStringEntryChainLL(HashMapStringEntryChainLL **chainLL, char* item)
{ 
    if((*chainLL) == NULL) return; 
    HashMapStringEntryChainLL *n = NewHashMapStringEntryChainLL(item); 
    if((*chainLL)->prev == NULL) 
        (*chainLL)->first = *chainLL; 
    
    (*chainLL)->next = n; 
    n->prev = (*chainLL); 
    n->first = (*chainLL)->first; 
    n->first->numItems = n->numItems = (*chainLL)->numItems + 1; 
    (*chainLL) = n;
}

uint64_t fnv1(void *bytes, size_t numBytes)
{
    uint64_t hash = 0xcbf29ce484222325;
    for(size_t i = 0; i < numBytes; i++)
    {
        hash = hash * 0x100000001b3;
        hash = hash ^ ((char *)bytes)[i];
    }

    return hash;
}
