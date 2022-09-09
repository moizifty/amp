#ifndef MYLIB_H
#define MYLIB_H
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

uint64_t fnv1(void *bytes, size_t numBytes);

#define HASHMAP_TABLE_DECLARE(TABLE_NAME, VALUE_TYPE) \
typedef struct TABLE_NAME##EntryChainLL \
{ \
    VALUE_TYPE item; \
    struct TABLE_NAME##EntryChainLL *prev; \
    struct TABLE_NAME##EntryChainLL *next; \
    int64_t numItems; \
    struct TABLE_NAME##EntryChainLL *first; \
}TABLE_NAME##EntryChainLL; \
typedef struct TABLE_NAME##Entry \
{ \
    uint64_t key; \
    TABLE_NAME##EntryChainLL *valueLL; \
}TABLE_NAME##Entry; \
typedef struct TABLE_NAME \
{\
    TABLE_NAME##Entry **entries; \
    int64_t numEntries; \
    int64_t capacity; \
    int (*compfn)(VALUE_TYPE, VALUE_TYPE); \
}TABLE_NAME; \
\
void AddTo##TABLE_NAME(TABLE_NAME *table, uint64_t key, VALUE_TYPE value); \
TABLE_NAME##Entry *GetEntry##TABLE_NAME##ByVal(TABLE_NAME *table, uint64_t key, VALUE_TYPE val); \
TABLE_NAME##EntryChainLL *GetEntryChainNode##TABLE_NAME##ByVal(TABLE_NAME *table, uint64_t key, VALUE_TYPE val, int64_t *out_numberInChain); \
TABLE_NAME##Entry *GetEntry##TABLE_NAME##ByKey(TABLE_NAME *table, uint64_t key); \
void Resize##TABLE_NAME(TABLE_NAME *table); \
TABLE_NAME *New##TABLE_NAME(int (*compfn)(VALUE_TYPE, VALUE_TYPE), int64_t initialCapacity); \
bool Is##TABLE_NAME##Full(TABLE_NAME *table); \
TABLE_NAME##EntryChainLL *New##TABLE_NAME##EntryChainLL(VALUE_TYPE item);\
void Add##TABLE_NAME##EntryChainLL(TABLE_NAME##EntryChainLL **chainLL, VALUE_TYPE item);

#define HASHMAP_TABLE_DEFINE(TABLE_NAME, VALUE_TYPE) \
void AddTo##TABLE_NAME(TABLE_NAME *table, uint64_t key, VALUE_TYPE value) \
{\
    if(table == NULL) return; \
    if(Is##TABLE_NAME##Full(table)) \
    {\
        Resize##TABLE_NAME(table); \
    }\
\
    uint64_t index = key % table->capacity; \
    TABLE_NAME##Entry *entry = GetEntry##TABLE_NAME##ByKey(table, key); \
    if(entry != NULL) \
    {\
        if(GetEntry##TABLE_NAME##ByVal(table, key, value) == NULL) Add##TABLE_NAME##EntryChainLL(&(entry->valueLL), value); \
    }\
    else \
    {\
        entry = malloc(sizeof(TABLE_NAME##Entry)); \
        entry->key = key; \
        entry->valueLL = New##TABLE_NAME##EntryChainLL(value); \
        table->numEntries += 1; \
        table->entries[index] = entry; \
    }\
}\
TABLE_NAME##Entry *GetEntry##TABLE_NAME##ByVal(TABLE_NAME *table, uint64_t key, VALUE_TYPE val) \
{\
    if(table == NULL) return NULL;\
    uint64_t index = key % table->capacity; \
\
    TABLE_NAME##Entry *entry =  table->entries[index]; \
    if(entry == NULL) return NULL; \
\
    TABLE_NAME##EntryChainLL *item = entry->valueLL->first; \
    while(item != NULL) \
    { \
        if(table->compfn(item->item, val) == 0) \
            return entry; \
\
        item = item->next; \
    } \
    return NULL; \
}\
TABLE_NAME##EntryChainLL *GetEntryChainNode##TABLE_NAME##ByVal(TABLE_NAME *table, uint64_t key, VALUE_TYPE val, int64_t *out_numberInChain) \
{\
    if(table == NULL) return NULL;\
    uint64_t index = key % table->capacity; \
\
    TABLE_NAME##Entry *entry = GetEntry##TABLE_NAME##ByVal(table, key, val);\
    if(entry == NULL) return NULL; \
\
    TABLE_NAME##EntryChainLL *item = entry->valueLL->first; \
    *out_numberInChain = 0; \
    while(item != NULL) \
    { \
        if(table->compfn(item->item, val) == 0) \
            return item; \
\
        item = item->next; \
        *out_numberInChain += 1; \
    } \
    return NULL; \
}\
TABLE_NAME##Entry *GetEntry##TABLE_NAME##ByKey(TABLE_NAME *table, uint64_t key) \
{\
    uint64_t index = key % table->capacity; \
\
    TABLE_NAME##Entry *entry =  table->entries[index]; \
    return entry; \
}\
void Resize##TABLE_NAME(TABLE_NAME *table) \
{\
    if(table == NULL) return;\
\
    int64_t newCapacity = (int64_t) ((float) table->capacity * 1.7f); \
    TABLE_NAME##Entry **tempEntries = table->entries; \
\
    table->entries = calloc(newCapacity, sizeof(TABLE_NAME##Entry *)); \
\
    for(int64_t i = 0; i < table->numEntries; i++) \
    { \
        if(tempEntries[i] == NULL) continue; \
        uint64_t newIndex = tempEntries[i]->key % newCapacity; \
        table->entries[newIndex] = tempEntries[i]; \
    } \
\
    table->capacity = newCapacity; \
    free(tempEntries);\
}\
TABLE_NAME *New##TABLE_NAME(int (*compfn)(VALUE_TYPE, VALUE_TYPE), int64_t initialCapacity) \
{\
    TABLE_NAME *ret = malloc(sizeof(TABLE_NAME)); \
    ret->entries = (initialCapacity == 0) ? NULL : calloc(initialCapacity, sizeof(TABLE_NAME##Entry *)); \
    ret->numEntries = 0; \
    ret->capacity = initialCapacity; \
    ret->compfn = compfn; \
    return ret; \
}\
bool Is##TABLE_NAME##Full(TABLE_NAME *table) \
{\
    return table->capacity == table->numEntries; \
}\
TABLE_NAME##EntryChainLL *New##TABLE_NAME##EntryChainLL(VALUE_TYPE item)\
{\
    TABLE_NAME##EntryChainLL *ll = malloc(sizeof(TABLE_NAME##EntryChainLL));\
    ll->next = NULL;\
    ll->prev = NULL;\
    ll->item = item;\
\
    ll->numItems = 1;\
    ll->first = ll;\
\
    return ll;\
} \
void Add##TABLE_NAME##EntryChainLL(TABLE_NAME##EntryChainLL **chainLL, VALUE_TYPE item)\
{\
    if((*chainLL) == NULL) return;\
\
    TABLE_NAME##EntryChainLL *n = New##TABLE_NAME##EntryChainLL(item);\
\
    if((*chainLL)->prev == NULL)\
        (*chainLL)->first = *chainLL;\
\
    (*chainLL)->next = n;\
    n->prev = (*chainLL);\
    n->first = (*chainLL)->first;\
    n->first->numItems = n->numItems = (*chainLL)->numItems + 1;\
\
    (*chainLL) = n;\
}
#endif


typedef struct HashMapStringEntryChainLL 
{ 
    char* item; 
    struct HashMapStringEntryChainLL *prev; 
    struct HashMapStringEntryChainLL *next; 
    int64_t numItems; 
    struct HashMapStringEntryChainLL *first; 
}HashMapStringEntryChainLL; 

typedef struct HashMapStringEntry 
{ 
    uint64_t key; 
    HashMapStringEntryChainLL *valueLL; 
}HashMapStringEntry; 

typedef struct HashMapString 
{ 
    HashMapStringEntry **entries; 
    int64_t numEntries; 
    int64_t capacity; 
}HashMapString; 

void AddToHashMapString(HashMapString *table, uint64_t key, char* value); 
HashMapStringEntry *GetEntryHashMapStringByVal(HashMapString *table, uint64_t key, char* val); 
HashMapStringEntryChainLL *GetEntryChainNodeHashMapStringByVal(HashMapString *table, uint64_t key, char* val, int64_t *out_numberInChain); 
HashMapStringEntry *GetEntryHashMapStringByKey(HashMapString *table, uint64_t key); 
void ResizeHashMapString(HashMapString *table); 
HashMapString *NewHashMapString(int64_t initialCapacity); 
bool IsHashMapStringFull(HashMapString *table); 
HashMapStringEntryChainLL *NewHashMapStringEntryChainLL(char* item);
void AddHashMapStringEntryChainLL(HashMapStringEntryChainLL **chainLL, char* item);