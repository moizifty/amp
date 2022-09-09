#include "ast.h"
#include "abi.h"
#include "types.h"
#include "compiler_internal.h"

size_t typeCounter = 0;

CheckerType *intType = NULL;
CheckerType *i8Type = NULL;
CheckerType *i16Type = NULL;
CheckerType *i32Type = NULL;
CheckerType *i64Type = NULL;
CheckerType *ui8Type = NULL;
CheckerType *ui16Type = NULL;
CheckerType *ui32Type = NULL;
CheckerType *ui64Type = NULL;

CheckerType *floatType = NULL;
CheckerType *f32Type = NULL;
CheckerType *f64Type = NULL;

CheckerType *boolType = NULL;

CheckerType *voidType = NULL;

CheckerType *variadicType = NULL;

CheckerType *anyType = NULL;
CheckerType *arrayViewType = NULL;
CheckerType *typeInfoType = NULL;
CheckerType *stringType = NULL;
CheckerType *rangeType = NULL;

extern GlobalContext globalContext;

void setupPrimitiveTypes(void)
{
    intType = newCheckerTypeInt(TYPE_INT_SIZE, 0);
    i8Type = newCheckerTypeInt(1, 0);
    i16Type = newCheckerTypeInt(2, 0);
    i32Type = newCheckerTypeInt(4, 0);
    i64Type = newCheckerTypeInt(8, 0);
    ui8Type = newCheckerTypeInt(1, TYPE_UNSIGNED_FLAG);
    ui16Type = newCheckerTypeInt(2, TYPE_UNSIGNED_FLAG);
    ui32Type = newCheckerTypeInt(4, TYPE_UNSIGNED_FLAG);
    ui64Type = newCheckerTypeInt(8, TYPE_UNSIGNED_FLAG);

    floatType = newCheckerTypeFloat(8, 0);
    f32Type = newCheckerTypeFloat(4, 0);
    f64Type = newCheckerTypeFloat(8, 0);

    boolType = newCheckerTypeBool();

    //stringType = newCheckerTypeString();

    voidType = newCheckerTypeVoid();
}

CheckerType *allocCheckerType(CheckerTypeKind kind)
{
    CheckerType *t = malloc(sizeof(CheckerType));
    t->kind = kind;
    t->byteSize = 0;
    t->flags = 0;
    t->typeId = 0;
    t->belongsToFile = NULL;
    t->namespaceName = NULL;
    t->typeFilters = NULL;
    t->backendType = NULL;
    t->byteSize = TYPE_SIZE_UNSET;
    t->alignment = 0;

    return t;
}

CheckerType *newCheckerTypeVoid()
{
    CheckerType *t = allocCheckerType(C_TYPE_VOID);
    t->typeId = typeCounter++;

    addTypeToTypeTable(t);

    return t;
}
CheckerType *newCheckerTypeInt(int size, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_INT);
    t->byteSize = size;
    t->flags |= flags;
    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);

    return t;
}
CheckerType *newCheckerTypeFloat(int size, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_FLOAT);
    t->byteSize = size;
    t->flags |= flags;
    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);

    return t;
}
CheckerType *newCheckerTypeBool()
{
    CheckerType *t = allocCheckerType(C_TYPE_BOOL);
    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypeString()
{
     CheckerType *t = allocCheckerType(C_TYPE_STRING);
    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}

CheckerType *newCheckerTypeAliased(CheckerType *base, char *name, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_ALIASED);
    t->aliasedType.base = base;
    t->aliasedType.name = name;
    t->typeId = typeCounter++;
    t->flags = flags;

    addTypeToTypeTable(t);
    return t;
}

//pass length as 0 if is slice
CheckerType *newCheckerTypeArray(CheckerType *base, int length, bool isSlice, bool isVariadic)
{
    CheckerType *t = allocCheckerType(C_TYPE_ARRAY);
    t->arrayType.base = base;
    t->arrayType.size = (isSlice) ? 0 : length;
    t->arrayType.isSlice = isSlice;
    t->arrayType.isVariadic = isVariadic;

    t->typeId = typeCounter++;

    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypePointer(CheckerType *base)
{
    CheckerType *t = allocCheckerType(C_TYPE_POINTER);
    t->pointerType.base = base;
    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}

ScopedDecl *newScopedDecl(Token name, CheckerType *type, struct ASTExpr *defExpr, int flags)
{
    ScopedDecl *sd = malloc(sizeof(ScopedDecl));
    sd->name = name;
    sd->type = type;
    sd->defExpr = defExpr;

    sd->flags = flags;
    sd->abiInfo = NULL;
    sd->mySymEntry = NULL;
    return sd;
}
ScopedDeclLL *newScopedDeclLL(ScopedDecl *item)
{
    ScopedDeclLL *ll = malloc(sizeof(ScopedDeclLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addScopedDeclLL(ScopedDeclLL **ll, ScopedDecl *item)
{
    ScopedDeclLL *n = newScopedDeclLL(item);
    
    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
ScopedDeclLL *getScopedDeclLLAt(ScopedDeclLL *ll, size_t index)
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

EnumMemb *newEnumMemb(char *name, int64_t val)
{
    EnumMemb *em = malloc(sizeof(EnumMemb));
    em->val = val;
    em->name = name;

    return em;
}
EnumMembLL *newEnumMembLL(EnumMemb *item)
{
    EnumMembLL *ll = malloc(sizeof(EnumMembLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addEnumMembLL(EnumMembLL **ll, EnumMemb *item)
{
    EnumMembLL *n = newEnumMembLL(item);
    
    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
EnumMembLL *getEnumMembLLAt(EnumMembLL *ll, size_t index)
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

CheckerType *newCheckerTypeGenericType(char *name)
{
    CheckerType *t = allocCheckerType(C_TYPE_GENERIC);
    t->genericType.name = name;
    
    return t;
}
CheckerType *newCheckerTypeStruct(ScopedDeclLL *declLL, char *name, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_STRUCT);
    t->structType.declLL = declLL;
    t->structType.name = name;
    t->flags = flags;
    t->structType.isFullyChecked = false;
    t->structType.hasGeneratedDefinition = false;
    t->structType.dependencies = NULL;

    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypeUnion(ScopedDeclLL *declLL,char *name, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_UNION);
    t->unionType.declLL = declLL;
    t->unionType.name = name;
    t->flags = flags;
    t->unionType.isFullyChecked = false;
    t->unionType.hasGeneratedDefinition = false;
    t->unionType.dependencies = NULL;

    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypeTaggedUnion(ScopedDeclLL *declLL, char *name, CheckerType *tagEnumType, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_TAGGED_UNION);
    t->taggedUnionType.declLL = declLL;
    t->taggedUnionType.name = name;
    t->taggedUnionType.isFullyChecked = false;
    t->taggedUnionType.hasGeneratedDefinition = false;
    t->taggedUnionType.dependencies = NULL;
    t->taggedUnionType.tagEnumType = tagEnumType;

    t->flags = flags;
    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypeEnum(EnumMembLL *membLL,char *name, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_ENUM);
    t->enumType.membLL = membLL;
    t->enumType.name = name;
    t->enumType.isTypeBelongingToTaggedUnionMemberAccess = false;

    t->flags = flags;
    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypeFunc(ScopedDeclLL *paramLL, CheckerType *ret, bool retAsArg, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_FUNC);
    t->funcType.paramLL = paramLL;
    t->funcType.ret = ret;
    t->funcType.retAsArg = retAsArg;
    t->funcType.isTypeMethod = false;
    t->funcType.isOperFunc = false;
    t->funcType.genericFuncAST = NULL;
    t->funcType.retABIInfo = NULL;
    
    t->flags = flags;

    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypeVariadic(CheckerType *base)
{
    CheckerType *t = newCheckerTypeArray(base, 0, true, true);

    return t;
}
CheckerType *newCheckerTypeOperFunc(char *name, ScopedDeclLL *paramLL, CheckerType *ret, int flags)
{
    CheckerType *t = allocCheckerType(C_TYPE_OPERATOR_OVERLOAD);
    t->opFuncType.declLL = paramLL;
    t->opFuncType.ret = ret;
    t->opFuncType.funcName = name;
    t->flags = flags;

    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypeNamespace(char *namespace, struct SymTable *tbl)
{
    CheckerType *t = allocCheckerType(C_TYPE_NAMESPACE);
    t->namespaceType.namespace = namespace;
    t->namespaceType.tble = tbl;

    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}
CheckerType *newCheckerTypeTuple(CheckerTypeLL *types)
{
    CheckerType *t = allocCheckerType(C_TYPE_TUPLE);

    size_t numTypes = types->first->numItems;

    CheckerType **typesList = malloc(numTypes * sizeof(CheckerType *));

    {
        CheckerTypeLL *currTypeLL = types->first;
        size_t index = 0;
        while(currTypeLL != NULL)
        {  
            typesList[index] = currTypeLL->item;
            index++;
            currTypeLL = currTypeLL->next;
        }
    }

    t->tupleType.types = typesList;
    t->tupleType.numTypes = numTypes;
    t->tupleType.dependencies = NULL;
    t->tupleType.hasGeneratedDefinition = false;

    t->typeId = typeCounter++;
    
    addTypeToTypeTable(t);
    return t;
}

CheckerTypeLL *newCheckerTypeLL(CheckerType *item)
{
    CheckerTypeLL *ll = malloc(sizeof(CheckerTypeLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addCheckerTypeLL(CheckerTypeLL **ll, CheckerType *item)
{
    CheckerTypeLL *n = newCheckerTypeLL(item);
    
    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}
bool CheckerTypeInCheckerTypeLL(CheckerTypeLL *ll, CheckerType *item)
{
    if((ll == NULL) || (item == NULL)) return false;

    CheckerTypeLL *curr = ll->first;

    while(curr != NULL)
    {
        if(areTypesEqual(curr->item, item)) return true;
        curr = curr->next;
    }

    return false;
}
bool haveCommonCheckerTypeLL(CheckerTypeLL *a, CheckerTypeLL *b)
{
    if((a == NULL) || (b == NULL)) return false;

    CheckerTypeLL *curr = a->first;

    while(curr != NULL)
    {
        if(CheckerTypeInCheckerTypeLL(b, curr->item)) return true;

        curr = curr->next;
    }

    return false;
}
void printCheckerTypeLL(CheckerTypeLL *ll)
{
    if(ll == NULL) return;

    ll = ll->first;

    printf("{");
    while(ll != NULL)
    {
        switch(ll->item->kind)
        {
            case C_TYPE_STRUCT: printf("%s", ll->item->structType.name); break;
            case C_TYPE_UNION: printf("%s", ll->item->unionType.name); break;
            default: printf("unhandled"); break;
        }

        if(ll->next != NULL) printf(", ");
        ll = ll->next;
    }
    printf("}\n");
}

CheckerTypeLLLL *newCheckerTypeLLLL(CheckerTypeLL *item)
{
    CheckerTypeLLLL *ll = malloc(sizeof(CheckerTypeLLLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addCheckerTypeLLLL(CheckerTypeLLLL **ll, CheckerTypeLL *item)
{
    CheckerTypeLLLL *n = newCheckerTypeLLLL(item);
    
    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

StructUnionDependencyNode *newStructUnionDependencyNode(CheckerType *type, StructUnionDependencyNode *parent, StructUnionDependencyNodeLL *children)
{
    StructUnionDependencyNode *d = malloc(sizeof(StructUnionDependencyNode));
    d->type = type;
    d->parent = parent;
    d->children = children;

    return d;
}
void addStructUnionDependencyNodeChildren(StructUnionDependencyNode *node, StructUnionDependencyNode *toAdd)
{
    if(node->children == NULL) node->children = newStructUnionDependencyNodeLL(toAdd);
    else addStructUnionDependencyNodeLL(&(node->children), toAdd);
}

StructUnionDependencyNodeLL *newStructUnionDependencyNodeLL(StructUnionDependencyNode *item)
{
    StructUnionDependencyNodeLL *ll = malloc(sizeof(StructUnionDependencyNodeLL));
    ll->next = NULL;
    ll->prev = NULL;
    ll->item = item;

    ll->numItems = 1;
    ll->first = ll;

    return ll;
}
void addStructUnionDependencyNodeLL(StructUnionDependencyNodeLL **ll, StructUnionDependencyNode *item)
{
    StructUnionDependencyNodeLL *n = newStructUnionDependencyNodeLL(item);

    if((*ll)->prev == NULL)
        (*ll)->first = *ll;

    (*ll)->next = n;
    n->prev = (*ll);
    n->first = (*ll)->first;
    n->first->numItems = n->numItems = (*ll)->numItems + 1;

    (*ll) = n;
}

TypeTable *newTypeTable()
{
    TypeTable *tt = malloc(sizeof(TypeTable));

    tt->typeLL = NULL;

    return tt;
}
void addTypeToTypeTable(CheckerType *t)
{
    if(globalContext.typeTable->typeLL == NULL)
    {
        globalContext.typeTable->typeLL = newCheckerTypeLL(t);
        return;
    }

    addCheckerTypeLL(&(globalContext.typeTable->typeLL), t);
}

CheckerType *copyCheckerType(CheckerType *toCopy)
{
    CheckerType *copied = allocCheckerType(toCopy->kind);
    *copied = *toCopy;
    
    return copied;
}

CheckerType *castSignedIntLiteralToCorrectType(int64_t val)
{
    if((val >= INT32_MIN) && (val <= INT32_MAX))
        return i32Type; 
    else if((val >= INT64_MIN) && (val <= INT64_MAX))
        return i64Type;

    return NULL;
}
CheckerType *castUnsignedIntLiteralToCorrectType(uint64_t val)
{
    if(val <= UINT32_MAX)
        return ui32Type; 
    else if(val <= UINT64_MAX)
        return ui64Type;

    return NULL;
}

CheckerType *castFloatLiteralToCorrectType(double val)
{
    if((val >= -FLT_MAX) && (val <= FLT_MAX))
        return f32Type; 
    else if((val >= -DBL_MAX) && (val <= DBL_MAX))
        return f64Type;

    return NULL;
}

//returns true if structType has circular dependencies
//structsList has to be initialized before passing
//structsList has to be initialized to have the first value being structType itself
//recursing has to be passed as false
bool getEmbeddedStructsInStructMember(CheckerType *structType, ScopedDecl *memb, CheckerTypeLLLL **structsList, bool recursing)
{
    //TODO fix struct union mixups
    if(structType == NULL) return false;
    bool isCyclical = false;

    CheckerType *membType = memb->type;

    if(isTypeArray(membType)) membType = getArrayNonArrayBase(membType);
    if(!isTypeStruct(membType) && !isTypeUnion(membType)) return false;


    if(isTypeAliased(membType)) membType = getAliasedTypeBase(membType);

    ScopedDeclLL *membMembs = membType->structType.declLL->first;

    if(!recursing) addCheckerTypeLLLL(structsList, newCheckerTypeLL(membType));
    
    CheckerTypeLL *sameLevelTypeList = NULL;
    //stores the structs that already have had their members added, avoids having repeated members added, 
    //causing false flags. only valid for same level members
    CheckerTypeLL *alreadyChecked = NULL;

    while(membMembs != NULL)
    {
        CheckerType *t = membMembs->item->type;

        if(isTypeArray(t)) t = getArrayNonArrayBase(t);

        if(isTypeStruct(t) || isTypeUnion(t))
        {
            if(isTypeAliased(t)) t = getAliasedTypeBase(t);

            if(sameLevelTypeList == NULL) sameLevelTypeList = newCheckerTypeLL(t);
            else addCheckerTypeLL(&sameLevelTypeList, t);
        }
        membMembs = membMembs->next;
    }
    
    if(sameLevelTypeList != NULL)
    {
        addCheckerTypeLLLL(structsList, sameLevelTypeList);

        ScopedDeclLL *membMembs = membType->structType.declLL->first;
        while(membMembs != NULL)
        {
            CheckerType *t = membMembs->item->type;

            if(isTypeArray(t)) t = getArrayNonArrayBase(t);

            if(isTypeStruct(t) || isTypeUnion(t))
            {
                if(isTypeAliased(t)) t = getAliasedTypeBase(t);
                
                //check for circular dependencies
                {
                    // CheckerTypeLLLL *s = *structsList;
                    // int numItems = s->first->numItems;
                    // s = s->first;

                    // for(int i = 0; (i < numItems) && (s != NULL); i++)
                    // {
                    //     CheckerTypeLLLL *s1 = s->next;
                    //     while(s1 != NULL)
                    //     {
                    //         if(haveCommonCheckerTypeLL(s->item, s1->item)) return true;

                    //         s1 = s1->next;
                    //     }
                    //     s = s->next;
                    // }

                    CheckerTypeLLLL *s = *structsList;
                    int numItems = s->first->numItems;

                    for(int i = 0; (i < numItems) && (s != NULL); i++)
                    {
                        CheckerTypeLLLL *s1 = s->prev;
                        while(s1 != NULL)
                        {
                            if(CheckerTypeInCheckerTypeLL(s->item, s1->item->item)) return true;

                            s1 = s1->prev;
                        }
                        s = s->prev;
                    }
                }
                
                bool c = CheckerTypeInCheckerTypeLL(alreadyChecked, t);
                if(!c)
                {
                    if(alreadyChecked == NULL) alreadyChecked = newCheckerTypeLL(t);
                    else addCheckerTypeLL(&alreadyChecked, t);
                    
                    isCyclical = isCyclical || getEmbeddedStructsInStructMember(membType, membMembs->item, structsList, true);
                }
            }
            membMembs = membMembs->next;
        }
    }

    return isCyclical;
}

//returns true if structType has circular dependencies
bool getImbeddedStructsInStruct(CheckerType *structType, CheckerTypeLL **structStack)
{
    if(structType == NULL) return false;
    bool isCyclical = false;
    //structStack has all structs or unions that are embedded within the struct
    //first item will always be structType (the struct itself).

    ScopedDeclLL *membs = structType->structType.declLL->first;

    while(membs != NULL)
    {
        CheckerType *membType = membs->item->type;

        if(isTypeStruct(membType) || isTypeUnion(membType))
        {
            if(isTypeAliased(membType)) membType = getAliasedTypeBase(membType);

            addCheckerTypeLL(structStack, membType);

            CheckerTypeLL *stack = *structStack;
            
            int numItems = stack->first->numItems;
            stack = stack->first;

            for(int i = 0; (i < numItems) && (stack != NULL); i++)
            {
                CheckerTypeLL *s1 = stack->next;
                while(s1 != NULL)
                {
                    if(areTypesEqual(stack->item, s1->item))
                    return true;

                    s1 = s1->next;
                }

                stack = stack->next;
            }

            isCyclical = isCyclical || getImbeddedStructsInStruct(membType, structStack);
        }
        membs = membs->next;
    }

    return isCyclical;
}

bool addChildrenToTypeDependencies(StructUnionDependencyNodeLL **children, StructUnionDependencyNode *root, CheckerType *type, ScopedDecl **whichMemberCausedCircular)
{
    bool added = false;
    bool success = true;

    type = isTypeAliased(type) ? getAliasedTypeBase(type) : type;

    if(isTypeUnion(type) || isTypeStruct(type) || isTypeTaggedUnion(type))
    {
        bool foundCircular = false;
        StructUnionDependencyNode *membDependencies = getStructUnionDependencies(type, root, &foundCircular, whichMemberCausedCircular);

        if(foundCircular)
        {
            success = false; //free other stuff
            return success;
        }

        if(membDependencies != NULL)
        {
            if(*children == NULL) 
                *children = newStructUnionDependencyNodeLL(membDependencies);
            else addStructUnionDependencyNodeLL(children, membDependencies);
        }


        added = true;
    }
    else if(isTypeArray(type))
    {
        CheckerType *membStuctType = getArrayNonArrayBase(type);

        bool foundCircular = false;
        StructUnionDependencyNode *membDependencies = getStructUnionDependencies(membStuctType, root, &foundCircular, whichMemberCausedCircular);

        if(foundCircular)
        {
            success = false; //free other stuff
            return success;
        }

        if(membDependencies != NULL)
        {
            if(*children == NULL) 
                *children = newStructUnionDependencyNodeLL(membDependencies);
            else addStructUnionDependencyNodeLL(children, membDependencies);
        }

        added = true;
    }
    else if(isTypeTuple(type))
    {
        CheckerType *tupleType = type;
        tupleType = (isTypeAliased(tupleType)) ? getAliasedTypeBase(tupleType) : tupleType;

        bool foundCircular = false;
        StructUnionDependencyNode *membDependencies = getStructUnionDependencies(tupleType, root, &foundCircular, whichMemberCausedCircular);

        if(foundCircular)
        {
            success = false; //free other stuff
            return success;
        }
        if(membDependencies != NULL)
        {
            if(*children == NULL) 
                *children = newStructUnionDependencyNodeLL(membDependencies);
            else addStructUnionDependencyNodeLL(children, membDependencies);
        }

        added = true;
    }

    return success;
}
StructUnionDependencyNode *getStructUnionDependencies(CheckerType *type, StructUnionDependencyNode *parent, bool *foundCircular, ScopedDecl **whichMemberCausedCircular)
{
    bool isUnion = isTypeUnion(type);
    bool isStruct = isTypeStruct(type);
    bool isTaggedUnion = isTypeTaggedUnion(type);
    bool isTuple = isTypeTuple(type);

    if(!isUnion && !isStruct && !isTaggedUnion && !isTuple)
    {
        if(!isTypeArray(type)) return NULL;
        else
        {
            CheckerType *base = getArrayNonArrayBase(type);

            if(!isTypeUnion(base) && 
               !isTypeStruct(base) &&
               !isTypeTaggedUnion(base) &&
               !isTypeTuple(base))
            {
                return NULL;
            }
        }
    }
    type = isTypeAliased(type) ? getAliasedTypeBase(type) : type;

    if(isUnion && (type->unionType.dependencies != NULL)) return type->unionType.dependencies;
    else if(isStruct && (type->structType.dependencies != NULL)) return type->structType.dependencies;
    else if(isTaggedUnion && (type->taggedUnionType.dependencies != NULL)) return type->structType.dependencies;
    else if(isTuple && (type->tupleType.dependencies != NULL)) return type->tupleType.dependencies;
    
    StructUnionDependencyNodeLL *children = NULL;
    StructUnionDependencyNode *root = newStructUnionDependencyNode(type, parent, NULL);

    { //check circular dependencies with parents
        StructUnionDependencyNode *currParent = root->parent;
        while(currParent != NULL)
        {
            if(areTypesEqual(currParent->type, type))
            {
                *foundCircular = true;
                return NULL; //circular dependency detected
            }
            currParent = currParent->parent;
        }
    }

    {
        ScopedDeclLL *currSD = NULL;
        CheckerType **tupleTypes = NULL;
        
        if(isTuple) tupleTypes = type->tupleType.types;
        else currSD = (isUnion) ? type->unionType.declLL->first : ((isStruct) ? type->structType.declLL->first : type->taggedUnionType.declLL->first);

        if(!isTuple)
        {
            while(currSD != NULL)
            {
                if(!addChildrenToTypeDependencies(&children, root, currSD->item->type, whichMemberCausedCircular))
                {
                     *foundCircular = true;
                     *whichMemberCausedCircular = currSD->item;
                    return NULL;
                }
                currSD = currSD->next;
            }
        }
        else
        {
            for(size_t i = 0; i < type->tupleType.numTypes; i++)
            {
                if(!addChildrenToTypeDependencies(&children, root, type->tupleType.types[i], whichMemberCausedCircular))
                {
                    *foundCircular = true;
                    return NULL;
                }
            }
        }
    }

    root->children = children;

    if(isUnion) type->unionType.dependencies = root;
    else if(isStruct) type->structType.dependencies = root;
    else if(isTaggedUnion) type->taggedUnionType.dependencies = root;
    else if(isTuple) type->tupleType.dependencies = root;

    return root;
}

bool isTypeInteger(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_INT: return true;
        case C_TYPE_ALIASED: return isTypeInteger(type->aliasedType.base);
    }

    return false;
}
bool isTypeFloat(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_FLOAT: return true;
        case C_TYPE_ALIASED: return isTypeFloat(type->aliasedType.base);
    }

    return false;
}
bool isTypeBoolean(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_BOOL: return true;
        case C_TYPE_ALIASED: return isTypeBoolean(type->aliasedType.base);
    }

    return false;
}
bool isTypeString(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_STRUCT:
        {
            return areTypesEqual(stringType, type);
        }break;
        case C_TYPE_ALIASED: return isTypeString(type->aliasedType.base);
    }

    return false;
}
bool isTypeAliased(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_ALIASED: return true;
    }

    return false;
}
CheckerType *getAliasedTypeBase(CheckerType *aliasedType)
{
    if(aliasedType == NULL) return false;

    CheckerType *base = aliasedType->aliasedType.base;

    switch(base->kind)
    {
        case C_TYPE_ALIASED: base = getAliasedTypeBase(base); break;
    }

    return base;
}

bool isTypeVariadic(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_ARRAY: return type->arrayType.isVariadic;
        case C_TYPE_ALIASED: return isTypeVariadic(getAliasedTypeBase(type));
    }

    return false;
}

bool isTypeStruct(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_STRUCT: return true;
        case C_TYPE_ALIASED: return isTypeStruct(type->aliasedType.base);
    }

    return false;
}
bool isTypeUnion(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_UNION: return true;
        case C_TYPE_ALIASED: return isTypeUnion(type->aliasedType.base);
    }

    return false;
}
bool isTypeTaggedUnion(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_TAGGED_UNION: return true;
        case C_TYPE_ALIASED: return isTypeTaggedUnion(type->aliasedType.base);
    }

    return false;
}
bool isTypeEnum(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_ENUM: return true;
        case C_TYPE_ALIASED: return isTypeEnum(type->aliasedType.base);
    }

    return false;
}

bool isTypeNamespace(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_NAMESPACE: return true;
    }

    return false;
}
bool isTypeGeneric(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_GENERIC: return true;
    }

    return false;
}

bool isTypeFunction(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_FUNC: return true;
        case C_TYPE_ALIASED: return isTypeFunction(type->aliasedType.base);
    }

    return false;
}
bool isTypeVoid(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_VOID: return true;
        case C_TYPE_ALIASED: return isTypeVoid(type->aliasedType.base);
    }

    return false;
}
bool isTypeVoidPtr(CheckerType *type)
{
    if(isTypePointer(type))
    {
        CheckerType *ptrType = (isTypeAliased(type)) ? getAliasedTypeBase(type) : type;
        return isTypeVoid(ptrType->pointerType.base);
    }

    return false;
}
bool isTypeArray(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_ARRAY: return true;
        case C_TYPE_ALIASED: return isTypeArray(type->aliasedType.base);
    }

    return false;
}
bool isTypeFixedArray(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_ARRAY: return !type->arrayType.isSlice;
        case C_TYPE_ALIASED: return isTypeFixedArray(type->aliasedType.base);
    }

    return false;
}
bool isTypeSliceArray(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_ARRAY: return type->arrayType.isSlice;
        case C_TYPE_ALIASED: return isTypeSliceArray(type->aliasedType.base);
    }

    return false;
}
CheckerType *getArrayBase(CheckerType *array)
{
    if(array == NULL) return NULL;

    switch(array->kind)
    {
        case C_TYPE_ARRAY: return array->arrayType.base;
        case C_TYPE_ALIASED: return getArrayBase(array->aliasedType.base);
    }

    return NULL;
}
CheckerType *getArrayNonArrayBase(CheckerType *array)
{
    if(array == NULL) return NULL;

    switch(array->kind)
    {
        case C_TYPE_ARRAY: 
        {
            if(isTypeArray(array->arrayType.base)) return getArrayNonArrayBase(array->arrayType.base);

            return array->arrayType.base;
        }
        case C_TYPE_ALIASED: return getArrayNonArrayBase(array->aliasedType.base);
    }

    return NULL;
}

bool isTypeTuple(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_TUPLE: return true;
        case C_TYPE_ALIASED: return isTypeTuple(type->aliasedType.base);
    }

    return false;
}

bool isTypePointer(CheckerType *type)
{
    if(type == NULL) return false;

    switch(type->kind)
    {
        case C_TYPE_POINTER: return true;
        case C_TYPE_ALIASED: return isTypePointer(type->aliasedType.base);
    }

    return false;
}
bool isTypePointerOrFunction(CheckerType *type)
{
    return isTypePointer(type) || isTypeFunction(type);
}

CheckerType *getPointerTypeBase(CheckerType *pointerType)
{
    if(pointerType == NULL) return false;

    CheckerType *base = pointerType->pointerType.base;

    switch(base->kind)
    {
        case C_TYPE_ALIASED: base = getPointerTypeBase(base); break;
    }

    return base;
}
//can you preform a [] operation on the type.
bool isTypeIndexable(CheckerType *type)
{
    //TODO: if type is struct or union
    //if so check if the index overload function has been declared for it.

    if(type == NULL) return false;

    if(isTypeArray(type) || isTypePointer(type) || isTypeVariadic(type)) return true;

    return false;
}
bool isTypeMemberAccessable(CheckerType *type, bool *wasEnum)
{
    if(type == NULL) return false;

    //only enum type name can be used with member access eg you can do:
    //EnumType.A, but not a.A, if a is defined ad being of type EnumType
    //hence u have to check this in the checker
    if(isTypeEnum(type)) 
    {
        *wasEnum = true;
        return true;
    }

    if(isTypePointer(type))
    {
        CheckerType *base = getPointerTypeBase(type);
        bool e = false;

        return isTypeMemberAccessable(base, &e);
    }
    if(isTypeStruct(type) || isTypeUnion(type) || isTypeArray(type) || isTypeTaggedUnion(type))
    {
        return true;
    }

    return false;
}
bool typeHasMember(CheckerType *t, char *name, size_t *membIndex)
{
    size_t i = 0;
    bool found = false;
    switch(t->kind)
    {
        case C_TYPE_STRUCT: case C_TYPE_UNION:
        {
            ScopedDeclLL *membs = (t->kind == C_TYPE_STRUCT) ? t->structType.declLL : t->unionType.declLL;

            membs = membs->first;

            while(membs != NULL)
            {
                if(!strcmp(name, membs->item->name.lexeme))
                {
                    found = true;
                    break;
                }

                i++;
                membs = membs->next;
            }
        }break;
        case C_TYPE_TAGGED_UNION:
        {
            if(!strcmp("kind", name))
            {
                found = true;
                i = -1; //-1 for .kind member
                break;
            }

            ScopedDeclLL *membs = t->taggedUnionType.declLL;

            membs = membs->first;

            while(membs != NULL)
            {
                if(!strcmp(name, membs->item->name.lexeme))
                {
                    found = true;
                    break;
                }

                i++;
                membs = membs->next;
            }
        }break;
        case C_TYPE_ENUM:
        {
            EnumMembLL *membs = t->enumType.membLL;

            membs = membs->first;

            while(membs != NULL)
            {
                if(!strcmp(name, membs->item->name))
                {
                    found = true;
                    break;
                }

                i++;
                membs = membs->next;
            }
        }break;
        case C_TYPE_ALIASED:
        {
            CheckerType *base = getAliasedTypeBase(t);
            found = typeHasMember(base, name, membIndex);
        }
    }

    if(found && (membIndex != NULL))
    {
        *membIndex = i;
    }
    return found;
}
bool taggedUnionTypeGetBestMember(CheckerType *taggedUnion, CheckerType *membTypeToCheck, bool *castableFound,size_t *membIndex)
{
    size_t i = 0;
    bool found = false;

    CheckerType *taggedUniontype = (isTypeAliased(taggedUnion) ? getAliasedTypeBase(taggedUnion) : taggedUnion);


    { //try find member tht exactly matches memTypeToCheck first
        ScopedDeclLL *membs = taggedUniontype->taggedUnionType.declLL;
        membs = membs->first;
        while(membs != NULL)
        {
            if(areTypesEqual(membTypeToCheck, membs->item->type))
            {
                found = true;
                *castableFound = false;
                break;
            }

            i++;
            membs = membs->next;
        }
    }

    if(!found)
    { //next if no exact match was found, try to see if one can be foudn where membTypeToCheck ccan be cast to it

        i = 0;
        ScopedDeclLL *membs = taggedUniontype->taggedUnionType.declLL;
        membs = membs->first;
        while(membs != NULL)
        {
            if(canImplicitCast(membTypeToCheck, membs->item->type))
            {
                found = true;
                *castableFound = true;
                break;
            }

            i++;
            membs = membs->next;
        }
    }

    if(found && (membIndex != NULL))
    {
        *membIndex = i;
    }

    return found;
}

bool funcHasParam(CheckerType *funcType, char *paramName, size_t *index)
{
    size_t i = 0;
    bool found = false;
    switch(funcType->kind)
    {
        case C_TYPE_FUNC: case C_TYPE_OPERATOR_OVERLOAD:
        {
            ScopedDeclLL *params = (funcType->kind == C_TYPE_FUNC) ? funcType->funcType.paramLL : funcType->opFuncType.declLL;

            if(params != NULL)
            {
                params = params->first;

                while(params != NULL)
                {
                    if(!strcmp(paramName, params->item->name.lexeme))
                    {
                        found = true;
                        break;
                    }

                    i++;
                    params = params->next;
                }
            }
        }break;
        case C_TYPE_ALIASED:
        {
            CheckerType *base = getAliasedTypeBase(funcType);
            found = funcHasParam(base, paramName, index);
        }
    }

    if(found && (index != NULL))
    {
        *index = i;
    }
    return found;
}
bool areTypesEqual(CheckerType *a, CheckerType *b)
{

    if((a == NULL) || (b == NULL)) return false;

    // if((a->belongsToFile == NULL) && (b->belongsToFile != NULL)) return false;
    // if((a->belongsToFile != NULL) && (b->belongsToFile == NULL)) return false;

    // if((a->belongsToFile != NULL) && (b->belongsToFile != NULL))
    // {
    //     if(strcmp(a->belongsToFile, b->belongsToFile)) return false;
    // }
    
    if(a->kind != b->kind) return false;

    if(a->typeId == b->typeId) return true;

    if(isTypeAliased(a)) //both are typealiased since th above if
    {
        return !strcmp(a->aliasedType.name, b->aliasedType.name);
    }

    switch(a->kind)
    {
        case C_TYPE_INT:
        {
            return (a->byteSize == b->byteSize) && 
                   (CHECK_TYPE_FLAG(a, TYPE_UNSIGNED_FLAG) == CHECK_TYPE_FLAG(b, TYPE_UNSIGNED_FLAG));
        }
        case C_TYPE_FLOAT: return a->byteSize == b->byteSize;
        case C_TYPE_BOOL: return a->kind == b->kind;
        case C_TYPE_STRING:return a->kind == b->kind;
        case C_TYPE_VOID:return a->kind == b->kind;
        case C_TYPE_ARRAY:
        {
            if(a->arrayType.isSlice != b->arrayType.isSlice) return false;
            
            bool equal = false;

            equal = (a->arrayType.isSlice) ? 
                    true : (a->arrayType.size == b->arrayType.size);
            
            equal = equal && areTypesEqual(a->arrayType.base, b->arrayType.base);

            return equal;

        }break;
        case C_TYPE_STRUCT:
        {
            bool isBothEqual = !strcmp(a->namespaceName, b->namespaceName) && !strcmp(a->structType.name, b->structType.name);
            
            return isBothEqual;
        }
        case C_TYPE_UNION:
        {
            return !strcmp(a->namespaceName, b->namespaceName) && !(strcmp(a->unionType.name, b->unionType.name));
        }
        case C_TYPE_TAGGED_UNION:
        {
            return !strcmp(a->namespaceName, b->namespaceName) && !(strcmp(a->taggedUnionType.name, b->taggedUnionType.name));
        }
        case C_TYPE_ENUM: return !(strcmp(a->enumType.name, b->enumType.name)) && !strcmp(a->namespaceName, b->namespaceName);
        case C_TYPE_POINTER: return areTypesEqual(a->pointerType.base, b->pointerType.base);
        case C_TYPE_FUNC: 
        {
            if(!areTypesEqual(a->funcType.ret, b->funcType.ret)) return false;

            ScopedDeclLL *aParams = a->funcType.paramLL;
            ScopedDeclLL *bParams = b->funcType.paramLL;

            size_t aNumParams = (aParams == NULL) ? 0 : aParams->first->numItems;
            size_t bNumParams = (bParams== NULL) ? 0 : bParams->first->numItems;

            if((aNumParams == 0) && (bNumParams == 0)) return true;

            if(aNumParams != bNumParams) return false;
            else
            {
                ScopedDeclLL *aCurrSDLL = aParams->first;
                ScopedDeclLL *bCurrSDLL = bParams->first;
                
                while(aCurrSDLL != NULL)
                {
                    if(!areTypesEqual(aCurrSDLL->item->type, bCurrSDLL->item->type)) return false;

                    aCurrSDLL = aCurrSDLL->next;
                    bCurrSDLL = bCurrSDLL->next;
                }
                return true;
            }
        }break;
        
        case C_TYPE_TUPLE:
        {
            if(a->tupleType.numTypes != b->tupleType.numTypes) return false;

            for(size_t i = 0; i < a->tupleType.numTypes; i++)
            {
                if(!areTypesEqual(a->tupleType.types[i], b->tupleType.types[i])) return false;
            }

        }break;

        default:
        {
            fprintf(stderr, "\n aretypesequal unimplemented type\n");
        }break;
    }

    return false;
}
bool canImplicitCast(CheckerType *from, CheckerType *to)
{
    if(areTypesEqual(from, to)) return true;

    if((from == NULL) || (to == NULL)) return false;

    from = (isTypeAliased(from)) ? getAliasedTypeBase(from) : from;
    to = (isTypeAliased(to)) ? getAliasedTypeBase(to) : to;

    if(areTypesEqual(from, to)) return true; //check again after getting typealias bases

    if(areTypesEqual(to, anyType)) 
    {
        if(isTypeVoid(from)) return false;

        if(to->typeFilters != NULL)
        {
            CheckerTypeLL *currType = to->typeFilters->first;
            while(currType != NULL)
            {
                if(areTypesEqual(currType->item, from)) return true;
                currType = currType->next;
            }

            return false;
        }
        return true;
    }
    if(isTypeVoidPtr(to))
    {
        return isTypePointer(from) || isTypeFunction(from);
    }
    if(isTypeVoidPtr(from))
    {
        return isTypePointer(to) || isTypeFunction(to);
    }

    switch(to->kind)
    {
        case C_TYPE_INT:
        {
            return canImplicitCastToInt(from, to);
        }break;

        case C_TYPE_ARRAY:
        {
            return canImplicitCastToArray(from, to);
        }break;

        case C_TYPE_FLOAT:
        {
            return canImplicitCastToFloat(from, to);
        }break;

        case C_TYPE_ENUM:
        {
            return canImplicitCastToEnum(from, to);
        }break;
        
        case C_TYPE_POINTER:
        {
            if(isTypePointer(from))
            {
                return canImplicitCast(from->pointerType.base, to->pointerType.base);
            }
        }break;
    }

    return false;
}
bool canImplicitCastToInt(CheckerType *from, CheckerType *intType)
{
    if(areTypesEqual(from, intType)) return true;

    if((from == NULL) || (intType == NULL)) return false;

    from = (isTypeAliased(from)) ? getAliasedTypeBase(from) : from;
    intType = (isTypeAliased(intType)) ? getAliasedTypeBase(intType) : intType;

    switch(from->kind)
    {
        case C_TYPE_INT:
        {
            if(from->byteSize >= intType->byteSize) return false;
            else if(CHECK_TYPE_FLAG(intType, TYPE_UNSIGNED_FLAG) && !CHECK_TYPE_FLAG(from, TYPE_UNSIGNED_FLAG)) return false;
            else return true;
        }break;
        case C_TYPE_BOOL: return true;
        case C_TYPE_ENUM: return true;
        default: return false;
    }
}
bool canImplicitCastValueToInt(CheckerType *iType, int64_t val)
{
    switch(iType->byteSize)
    {
        case 1:
        {
            if(CHECK_TYPE_FLAG(iType, TYPE_UNSIGNED_FLAG)) return (val <= UINT8_MAX) && (val >= 0);
            else return (val <= INT8_MAX) && (val >= INT8_MIN);
        }break;
        case 2:
        {
            if(CHECK_TYPE_FLAG(iType, TYPE_UNSIGNED_FLAG)) return (val <= UINT16_MAX) && (val >= 0);
            else return (val <= INT16_MAX) && (val >= INT16_MIN);
        }break;
        case 4:
        {
            if(CHECK_TYPE_FLAG(iType, TYPE_UNSIGNED_FLAG)) return (val <= UINT32_MAX) && (val >= 0);
            else return (val <= INT32_MAX) && (val >= INT32_MIN);
        }break;
        case 8:
        {
            if(CHECK_TYPE_FLAG(iType, TYPE_UNSIGNED_FLAG)) return (val <= UINT64_MAX) && (val >= 0);
            else return (val <= INT64_MAX) && (val >= INT64_MIN);
        }break;
        
        default:
        {
            fprintf(stderr, "Invalid type size: %lld", iType->byteSize);
        }break;
    }

    return false;
}
bool canImplicitCastToFloat(CheckerType *from, CheckerType *floatType)
{
    if(areTypesEqual(from, floatType)) return true;

    if((from == NULL) || (floatType == NULL)) return false;

    from = (isTypeAliased(from)) ? getAliasedTypeBase(from) : from;
    floatType = (isTypeAliased(floatType)) ? getAliasedTypeBase(floatType) : floatType;

    switch(from->kind)
    {
        case C_TYPE_FLOAT:
        {
            if(from->byteSize > intType->byteSize) return false;
            else return true;
        }break;
        case C_TYPE_INT:
        {
            return true;
        }break;
        default: return false;
    }
}
bool canImplicitCastToArray(CheckerType *from, CheckerType *to)
{
    if(areTypesEqual(from, to)) return true;

    if((from == NULL) || (to == NULL)) return false;

    from = (isTypeAliased(from)) ? getAliasedTypeBase(from) : from;
    to = (isTypeAliased(to)) ? getAliasedTypeBase(to) : to;

    switch(from->kind)
    {
        case C_TYPE_ARRAY:
        {
            if(to->arrayType.isSlice || to->arrayType.isVariadic)
            {
                return canImplicitCast(getArrayBase(from), getArrayBase(to));
            }
            else if(!to->arrayType.isSlice && !from->arrayType.isSlice) 
            return canImplicitCast(getArrayBase(from), getArrayBase(to)) && (from->arrayType.size == to->arrayType.size);
        
            if(from->arrayType.isSlice && (!to->arrayType.isSlice && !to->arrayType.isVariadic)) return false;
        }break;
        default: return false;
    }

    return false;
}
bool canImplicitCastToEnum(CheckerType *from, CheckerType *to)
{
    if(areTypesEqual(from, to)) return true;

    if((from == NULL) || (to == NULL)) return false;

    from = (isTypeAliased(from)) ? getAliasedTypeBase(from) : from;
    to = (isTypeAliased(to)) ? getAliasedTypeBase(to) : to;

    return false;
}
void printCheckerType(CheckerType *type)
{
    if(type == NULL)
    {
        fprintf(stderr, "UNDEFINED");
        return;
    }
        
    switch(type->kind)
    {
        case C_TYPE_INT: 
        {
            if(CHECK_TYPE_FLAG(type, TYPE_UNSIGNED_FLAG))
            {
                fprintf(stderr, "ui%lld", type->byteSize * 8); 
            }
            else
                fprintf(stderr, "i%lld", type->byteSize * 8); 

            break;
        }
        case C_TYPE_FLOAT:
        {
            fprintf(stderr, "f%lld", type->byteSize * 8);
            
            break;
        }
        case C_TYPE_BOOL: fprintf(stderr, "bool"); break;
        case C_TYPE_STRING: fprintf(stderr, "string"); break;
        case C_TYPE_ALIASED: fprintf(stderr, "%s", type->aliasedType.name); break;
        case C_TYPE_ARRAY: 
        {
            if(!type->arrayType.isSlice)
                fprintf(stderr, "[%lld]", type->arrayType.size); 
            else fprintf(stderr, "[]");
            printCheckerType(type->arrayType.base);
            break;
        }
        case C_TYPE_STRUCT: 
        {
            if(areTypesEqual(type, stringType)) fprintf(stderr, "string"); 
            else if(areTypesEqual(type, anyType))
            {
                fprintf(stderr, "any"); 
                if(type->typeFilters != NULL)
                {
                    fprintf(stderr, "<");

                    CheckerTypeLL *currFilter = type->typeFilters->first;
                    while(currFilter != NULL)
                    {
                        printCheckerType(currFilter->item);

                        currFilter = currFilter->next;

                        if(currFilter != NULL)
                        {
                            fprintf(stderr, ", ");
                        }
                    }

                    fprintf(stderr, ">");
                }
            }
            else fprintf(stderr, "%s::struct %s", type->namespaceName, type->structType.name); 
            break;
        }
        case C_TYPE_UNION: 
        {
            fprintf(stderr, "%s::union %s", type->namespaceName, type->unionType.name); 
            break;
        }
        case C_TYPE_TAGGED_UNION:
        {
            fprintf(stderr, "%s::tagged union %s", type->namespaceName, type->taggedUnionType.name); 
            break;
        }
        case C_TYPE_VARIADIC:
        {
            fprintf(stderr, "variadic ");
            printCheckerType(type->variadicType.base);
            break;
        }
        case C_TYPE_POINTER:
        {
            fprintf(stderr, "*");
            printCheckerType(type->pointerType.base);
            break;
        }
        case C_TYPE_ENUM:
        {
            fprintf(stderr, "%s::enum %s", type->namespaceName, type->enumType.name);
            break;
        }
        case C_TYPE_VOID:
        {
            fprintf(stderr, "void");
            break;
        }
        case C_TYPE_NAMESPACE:
        {
            fprintf(stderr, "namespace %s", type->namespaceType.namespace);
            break;
        }
        case C_TYPE_FUNC:
        {
            fprintf(stderr, "(");
            
            ScopedDeclLL *sdLL = type->funcType.paramLL;

            if(sdLL != NULL)
            {
                sdLL = sdLL->first;
                while(sdLL != NULL)
                {
                    fprintf(stderr, "%s: ", sdLL->item->name.lexeme);
                    printCheckerType(sdLL->item->type);

                    sdLL = sdLL->next;

                    if(sdLL != NULL) fprintf(stderr, ", ");
                }
            }

            fprintf(stderr, ") : ");

            printCheckerType(type->funcType.ret);
            break;
        }
        case C_TYPE_TUPLE:
        {
            fprintf(stderr, "tuple(");
            
            {//print member types

                for(size_t i = 0; i < type->tupleType.numTypes; i++)
                {
                    printCheckerType(type->tupleType.types[i]);

                    if(i < type->tupleType.numTypes - 1)
                        fprintf(stderr, ", ");
                }
            }

            fprintf(stderr, ")");
        }break;
        default:
        {
            fprintf(stderr, "TODO PRINTING");
            break;
        }
    } 
}

char *allocCheckerTypeName(CheckerType *type)
{
    char buf[2 * MAX_IDEN_LEN] = {0};

    switch(type->kind)
    {
        case C_TYPE_INT:
        {
            if(CHECK_TYPE_FLAG(type, TYPE_UNSIGNED_FLAG))
                sprintf(buf, "ui%lld", type->byteSize * 8);
            else sprintf(buf, "i%lld", type->byteSize * 8);
        }break;
        case C_TYPE_FLOAT: sprintf(buf, "f%lld", type->byteSize * 8); break;
        case C_TYPE_BOOL: sprintf(buf, "bool"); break;
        case C_TYPE_STRING: sprintf(buf, "string"); break;
        case C_TYPE_VOID: sprintf(buf, "void"); break;
        case C_TYPE_ALIASED: sprintf(buf, "%s_a_%s", type->namespaceName, type->aliasedType.name); break;
        case C_TYPE_STRUCT: sprintf(buf, "%s_s_%s", type->namespaceName, type->structType.name); break;
        case C_TYPE_UNION: sprintf(buf, "%s_u_%s", type->namespaceName, type->unionType.name); break;
        case C_TYPE_TAGGED_UNION: sprintf(buf, "%s_tu_%s", type->namespaceName, type->taggedUnionType.name); break;
        case C_TYPE_ENUM: sprintf(buf, "%s_e_%s", type->namespaceName, type->enumType.name); break;
        case C_TYPE_ARRAY:
        {
            char *baseName = allocCheckerTypeName(type->arrayType.base);

            if(!type->arrayType.isSlice)
                sprintf(buf, "array_%lld_%s", type->arrayType.size, baseName);
            else sprintf(buf, "array_%s", baseName);

            free(baseName);
        }break;
        case C_TYPE_POINTER:
        {
            char *baseName = allocCheckerTypeName(type->pointerType.base);

            sprintf(buf, "pointer_%s", baseName);

            free(baseName);
        }break;
        case C_TYPE_TUPLE:
        {
            sprintf(buf, "t_");

            for(size_t i = 0; i < type->tupleType.numTypes; i++)
            {
                char *n = allocCheckerTypeName(type->tupleType.types[i]);
                strcat(buf, n);
            }
        }break;
        default: fprintf(stderr, "Unhandled type name geenration in %s", __FUNCTION__);
    }

    size_t len = strlen(buf);
    char *name = malloc(len + 1);
    strcpy(name, buf);
    name[len] = '\0';

    return name;
}
char *allocOperFuncName(Token op, ScopedDeclLL *params, CheckerType *retType)
{
    if((params == NULL) || (retType == NULL)) return NULL;

    // 4 *, since u have 2 or 1 params, op and retType
    char buf[4 * MAX_IDEN_LEN] = {0};

    switch(op.type)
    {
        case '+': strcpy(buf, OPERFUNC_PLUS_OP_NAME "_"); break;
        case '-': strcpy(buf, OPERFUNC_MINUS_OP_NAME "_"); break;
        case TOK_INC_OP: strcpy(buf, OPERFUNC_INC_OP_NAME "_"); break;
        case TOK_DEC_OP: strcpy(buf, OPERFUNC_DEC_OP_NAME "_"); break;
        case '*': strcpy(buf, OPERFUNC_MULT_OP_NAME "_"); break;
        case '/': strcpy(buf, OPERFUNC_DIV_OP_NAME "_"); break;
        case '%': strcpy(buf, OPERFUNC_MOD_OP_NAME "_"); break;
        case '>': strcpy(buf, OPERFUNC_GREATER_OP_NAME "_"); break;
        case '<': strcpy(buf, OPERFUNC_LESSER_OP_NAME "_"); break;
        case TOK_GE_RELOP: strcpy(buf, OPERFUNC_GE_OP_NAME "_"); break;
        case TOK_LE_RELOP: strcpy(buf, OPERFUNC_LE_OP_NAME "_"); break;
        case TOK_EQ_RELOP: strcpy(buf, OPERFUNC_EQ_OP_NAME "_"); break;
        case TOK_NEQ_RELOP: strcpy(buf, "N" OPERFUNC_EQ_OP_NAME "_"); break;
        case '[': strcpy(buf, OPERFUNC_INDEX_OP_NAME "_"); break;
        default: return NULL;
    }

    ScopedDeclLL *currSD = params->first;

    while(currSD != NULL)
    {
        CheckerType *t = currSD->item->type;
        char *n = allocCheckerTypeName(t);
    
        strcat(buf, n);
        strcat(buf, "_");

        free(n);
        currSD = currSD->next;
    }

    // char *n = allocCheckerTypeName(retType);
    // strcat(buf, n);
    // free(n);
    
    size_t len = strlen(buf);
    char *name = malloc(len + 1);
    strcpy(name, buf);

    return name;
}
char *allocOperFuncNameFromTypes(TokType op, CheckerType *a, CheckerType *b)
{
    bool isUnary = ((a == NULL) || (b == NULL));

    // 4 *, since u have 2 or 1 params, op and retType
    char buf[4 * MAX_IDEN_LEN] = {0};

    switch(op)
    {
        case '+': strcpy(buf, OPERFUNC_PLUS_OP_NAME "_"); break;
        case '-': strcpy(buf, OPERFUNC_MINUS_OP_NAME "_"); break;
        case TOK_INC_OP: strcpy(buf, OPERFUNC_INC_OP_NAME "_"); break;
        case TOK_DEC_OP: strcpy(buf, OPERFUNC_DEC_OP_NAME "_"); break;
        case '*': strcpy(buf, OPERFUNC_MULT_OP_NAME "_"); break;
        case '/': strcpy(buf, OPERFUNC_DIV_OP_NAME "_"); break;
        case '%': strcpy(buf, OPERFUNC_MOD_OP_NAME "_"); break;
        case '>': strcpy(buf, OPERFUNC_GREATER_OP_NAME "_"); break;
        case '<': strcpy(buf, OPERFUNC_LESSER_OP_NAME "_"); break;
        case TOK_GE_RELOP: strcpy(buf, OPERFUNC_GE_OP_NAME "_"); break;
        case TOK_LE_RELOP: strcpy(buf, OPERFUNC_LE_OP_NAME "_"); break;
        case TOK_EQ_RELOP: strcpy(buf, OPERFUNC_EQ_OP_NAME "_"); break;
        case TOK_NEQ_RELOP: strcpy(buf, "N" OPERFUNC_EQ_OP_NAME "_"); break;
        case '[': strcpy(buf, OPERFUNC_INDEX_OP_NAME "_"); break;
        default: return NULL;
    }

    if(a != NULL)
    {
        char *n = allocCheckerTypeName(a);
    
        strcat(buf, n);
        strcat(buf, "_");

        free(n);
    }

    if(b != NULL)
    {
        char *n = allocCheckerTypeName(b);
    
        strcat(buf, n);
        strcat(buf, "_");

        free(n);
    }
    
    size_t len = strlen(buf);
    char *name = malloc(len + 1);
    strcpy(name, buf);

    return name;
}

//get size in bytes
int64_t checkerTypeGetSizeWin(CheckerType *type)
{
    if(type->byteSize != TYPE_SIZE_UNSET) return type->byteSize;

    switch(type->kind)
    {
        case C_TYPE_VOID:
        {
            return type->byteSize = 0;
        }break;
        case C_TYPE_INT: return type->byteSize; break;
        case C_TYPE_FLOAT: return type->byteSize; break;
        case C_TYPE_BOOL:
        {
            if(globalContext.useLLVMBackend) return type->byteSize = 1;
            else return type->byteSize = sizeof(bool);
        }break;
        case C_TYPE_ARRAY:
        {
            if(isTypeFixedArray(type))
            {
                return type->byteSize = type->arrayType.size * checkerTypeGetSizeWin(type->arrayType.base);
            }
            else return type->byteSize = checkerTypeGetSizeWin(arrayViewType);
        }break;
        case C_TYPE_ALIASED:
        {
            return type->byteSize = checkerTypeGetSizeWin(type->aliasedType.base);
        }break;
        case C_TYPE_STRUCT:
        {
            int64_t size = 0;

            ScopedDeclLL *currDLL = type->structType.declLL->first;
            while(currDLL != NULL)
            {
                int64_t membAlignment = checkerTypeGetAlignmentWin(currDLL->item->type);
                int64_t membSize = checkerTypeGetSizeWin(currDLL->item->type);

                while((size % membAlignment) != 0) size += 1;

                size += membSize;

                currDLL = currDLL->next;
            }
            
            int64_t structAlignment = checkerTypeGetAlignmentWin(type);
            
            while((size % structAlignment) != 0) size += 1;

            return type->byteSize = size;
        }break;
        case C_TYPE_UNION:
        {
            int64_t maxMembSize = 0;
            int64_t maxMembAlign = 0;

            ScopedDeclLL *currDLL = type->unionType.declLL->first;
            while(currDLL != NULL)
            {
                int64_t membAlignment = checkerTypeGetAlignmentWin(currDLL->item->type);
                int64_t membSize = checkerTypeGetSizeWin(currDLL->item->type);

                if(membSize > maxMembSize)
                {
                    maxMembAlign = membAlignment;
                    maxMembSize = membSize;
                }

                currDLL = currDLL->next;
            }
            
            int64_t unionAlignment = checkerTypeGetAlignmentWin(type);
            int64_t size = maxMembSize;
            
            while((size % unionAlignment) != 0) size += 1;

            return type->byteSize = size;
        }break;
        case C_TYPE_TAGGED_UNION:
        {
            int64_t size = 0;

            { //inner union
                int64_t maxMembSize = 0;
                int64_t maxMembAlign = 0;

                ScopedDeclLL *currDLL = type->taggedUnionType.declLL->first;
                while(currDLL != NULL)
                {
                    int64_t membAlignment = checkerTypeGetAlignmentWin(currDLL->item->type);
                    int64_t membSize = checkerTypeGetSizeWin(currDLL->item->type);

                    if(membSize > maxMembSize)
                    {
                        maxMembSize = membSize;
                    }

                    if(membAlignment > maxMembAlign)
                    {
                        maxMembAlign = membAlignment;
                    }

                    currDLL = currDLL->next;
                }
                
                int64_t unionAlignment = maxMembAlign;
                size = maxMembSize;
                
                while((size % unionAlignment) != 0) size += 1;
            }

            // Tagged unions are layed out like this:
            // example tagged union representation
            // struct global_tu1
            // {
            //     union
            //     {
            //         int32_t A;
            //     };
            //     enum global_e1 __kind;
            //     int64_t __indexInEnum;
            //     int64_t __indexInUnion;
            // };

            //hence do same procedure as struct, with last 3 members

            int64_t enumAlignment = 4;
            int64_t int64Alignment = checkerTypeGetAlignmentWin(i64Type);

            while((size % enumAlignment) != 0) size += 1;
            size += 4; //enum size;

            while((size % int64Alignment) != 0) size += 1;
            size += 8; //int64 byte size
            
            while((size % int64Alignment) != 0) size += 1;
            size += 8; //int64 byte size

            int64_t taggedUnionAlignment = checkerTypeGetAlignmentWin(type);
            while((size % taggedUnionAlignment) != 0) size += 1;

            return type->byteSize = size;
        }break;
        case C_TYPE_TUPLE:
        {
            int64_t size = 0;

            for(size_t i = 0; i < type->tupleType.numTypes; i++)
            {
                int64_t membAlignment = checkerTypeGetAlignmentWin(type->tupleType.types[i]);
                int64_t membSize = checkerTypeGetSizeWin(type->tupleType.types[i]);

                while((size % membAlignment) != 0) size += 1;

                size += membSize;
            }
            
            int64_t structAlignment = checkerTypeGetAlignmentWin(type);
            
            while((size % structAlignment) != 0) size += 1;

            return type->byteSize = size;
        }break; 
        case C_TYPE_ENUM: return type->byteSize = checkerTypeGetSizeWin(i32Type);
        case C_TYPE_NAMESPACE: return type->byteSize = 0;
        case C_TYPE_FUNC:
        case C_TYPE_POINTER:
        {
            if(PLATFORM == PLATFORM_WIN32) return type->byteSize = 4;
            else return type->byteSize = 8;
        }break;
        case C_TYPE_GENERIC: return type->byteSize = 0;
    }

    return -1;
}
int64_t checkerTypeGetAlignmentWin(CheckerType *type)
{
    if(type->alignment != 0) return type->alignment;

    switch(type->kind)
    {
        case C_TYPE_VOID:
        {
            return type->alignment = 0;
        }break;
        case C_TYPE_INT: 
        {
            int64_t size = checkerTypeGetSizeWin(type);
            switch(size)
            {
                case 1: return type->alignment = 1;
                case 2: return type->alignment = 2;
                case 4: return type->alignment = 4;
                case 8: return type->alignment = 8;
            }
        }break;
        case C_TYPE_FLOAT:
        {
            int64_t size = checkerTypeGetSizeWin(type);
            switch(size)
            {
                case 4: return type->alignment = 4;
                case 8: return type->alignment = 8;
            }
        }break;
        case C_TYPE_BOOL: return type->alignment = 1;
        case C_TYPE_ARRAY:
        {
            if(isTypeFixedArray(type))
            {
                return type->alignment = checkerTypeGetAlignmentWin(type->arrayType.base);
            }
            else return type->alignment = checkerTypeGetAlignmentWin(arrayViewType);
        }break;
        case C_TYPE_ALIASED:
        {
            return type->alignment = checkerTypeGetAlignmentWin(type->aliasedType.base);
        }break;
        case C_TYPE_STRUCT:
        {
            int64_t highestAlignment = 0;

            ScopedDeclLL *currDLL = type->structType.declLL->first;
            while(currDLL != NULL)
            {
                int64_t membAlignment = checkerTypeGetAlignmentWin(currDLL->item->type);

                if(membAlignment > highestAlignment) highestAlignment = membAlignment;

                currDLL = currDLL->next;
            }

            return type->alignment = highestAlignment;
        }break;
        case C_TYPE_UNION:
        {
            int64_t highestAlignment = 0;

            ScopedDeclLL *currDLL = type->unionType.declLL->first;
            while(currDLL != NULL)
            {
                int64_t membAlignment = checkerTypeGetAlignmentWin(currDLL->item->type);

                if(membAlignment > highestAlignment) highestAlignment = membAlignment;

                currDLL = currDLL->next;
            }

            return type->alignment = highestAlignment;
        }break;
        case C_TYPE_TAGGED_UNION:
        {
            // example tagged union representation
            // struct global_tu1
            // {
            //     union
            //     {
            //         int32_t A;
            //     };
            //     enum global_e1 __kind;
            //     int64_t __indexInEnum;
            //     int64_t __indexInUnion;
            // };

            int64_t highestAlignmentOfInnerUnion = 0;

            ScopedDeclLL *currDLL = type->taggedUnionType.declLL->first;
            while(currDLL != NULL)
            {
                int64_t membAlignment = checkerTypeGetAlignmentWin(currDLL->item->type);

                if(membAlignment > highestAlignmentOfInnerUnion) highestAlignmentOfInnerUnion = membAlignment;

                currDLL = currDLL->next;
            }

            //is the inner unions alignment bigger then then an int64 since the tagged union has them as fields
            if(8 > highestAlignmentOfInnerUnion)
            {
                highestAlignmentOfInnerUnion = 8;
            }

            return type->alignment = highestAlignmentOfInnerUnion;
        }break;
        case C_TYPE_TUPLE:
        {
            int64_t highestAlignment = 0;

            for(size_t i = 0; i < type->tupleType.numTypes; i++)
            {
                int64_t membAlignment = checkerTypeGetAlignmentWin(type->tupleType.types[i]);

                if(membAlignment > highestAlignment) highestAlignment = membAlignment;
            }

            return type->alignment = highestAlignment;
        }break; 
        case C_TYPE_ENUM: return type->alignment = checkerTypeGetAlignmentWin(i32Type);
        case C_TYPE_NAMESPACE: return type->alignment = 0;
        case C_TYPE_FUNC:
        case C_TYPE_POINTER:
        {
            if(PLATFORM == PLATFORM_WIN32) return type->alignment = 4;
            else return type->alignment = 8;
        }break;
        case C_TYPE_GENERIC: return type->alignment = 0;
    }

    return -1;
}

int64_t checkerTypeGetSize(CheckerType *type)
{
#if (PLATFORM == PLATFORM_WIN64) || (PLATFORM == PLATFORM_WIN32)
    return checkerTypeGetSizeWin(type);
#elif PLATFORM == PLATFORM_UNIX
    return checkerTypeGetSizeUnix(type);
#endif
}
int64_t checkerTypeGetAlignment(CheckerType *type)
{
#if (PLATFORM == PLATFORM_WIN64) || (PLATFORM == PLATFORM_WIN32)
    return checkerTypeGetAlignmentWin(type);
#elif PLATFORM == PLATFORM_UNIX
    return checkerTypeGetAlignmentUnix(type);
#endif
}

bool isTypeAggregate(CheckerType *type)
{
    switch(type->kind)
    {
        case C_TYPE_ARRAY: return true;
        case C_TYPE_STRUCT: return true;
        case C_TYPE_UNION: return true;
        case C_TYPE_TAGGED_UNION: return true;
        case C_TYPE_TUPLE: return true;
        case C_TYPE_ALIASED: return isTypeAggregate(type->aliasedType.base);
    }

    return false;
}

char *allocCheckerTypeToString(CheckerType *type)
{
    char *name = NULL;

    switch(type->kind)
    {
        case C_TYPE_VOID:
        {
            name = malloc(sizeof("void"));
            strcpy(name, "void");
        }break;
        case C_TYPE_INT:
        {
            name = allocCheckerTypeName(type);
        }break;
        case C_TYPE_FLOAT:
        {
            name = allocCheckerTypeName(type);
        }break;
        case C_TYPE_BOOL:
        {
            name = allocCheckerTypeName(type);
        }break;
        case C_TYPE_ARRAY:
        {
            char buf[256] = {0};

            char *baseTypeName = allocCheckerTypeToString(type->arrayType.base);

            if(isTypeSliceArray(type)) sprintf(buf, "[]%s", baseTypeName);
            else sprintf(buf, "[%lld]%s", type->arrayType.size, baseTypeName);

            free(baseTypeName);

            name = malloc(strlen(buf) + 1);
            strcpy(name, buf);
        }break;
        case C_TYPE_POINTER:
        {
            char buf[256] = {0};
            char *baseTypeName = allocCheckerTypeToString(type->pointerType.base);

            sprintf(buf, "*%s", baseTypeName);

            free(baseTypeName);

            name = malloc(strlen(buf) + 1);
            strcpy(name, buf);
        }break;
        case C_TYPE_ALIASED:
        {
            char buf[256] = {0};

            NSTEntry *ne = namespaceTableLookupEntry(globalContext.namespaceTable, type->namespaceName);
            char *nameSpaceName = allocNamespaceNameWithoutUnderscores(ne);

            sprintf(buf, "%s::%s", nameSpaceName, type->aliasedType.name);

            name = malloc(strlen(buf) + 1);
            
            strcpy(name, buf);
        }break;
        case C_TYPE_STRUCT:
        {
            char buf[256] = {0};

            NSTEntry *ne = namespaceTableLookupEntry(globalContext.namespaceTable, type->namespaceName);
            char *nameSpaceName = allocNamespaceNameWithoutUnderscores(ne);

            sprintf(buf, "%s::%s", nameSpaceName, type->structType.name);

            name = malloc(strlen(buf) + 1);
            
            strcpy(name, buf);
        }break;
        case C_TYPE_UNION:
        {
            char buf[256] = {0};

            NSTEntry *ne = namespaceTableLookupEntry(globalContext.namespaceTable, type->namespaceName);
            char *nameSpaceName = allocNamespaceNameWithoutUnderscores(ne);

            sprintf(buf, "%s::%s", nameSpaceName, type->unionType.name);

            name = malloc(strlen(buf) + 1);
            
            strcpy(name, buf);
        }break;
        case C_TYPE_TAGGED_UNION:
        {
            char buf[256] = {0};

            NSTEntry *ne = namespaceTableLookupEntry(globalContext.namespaceTable, type->namespaceName);
            char *nameSpaceName = allocNamespaceNameWithoutUnderscores(ne);

            sprintf(buf, "%s::%s", nameSpaceName, type->taggedUnionType.name);

            name = malloc(strlen(buf) + 1);
            
            strcpy(name, buf);
        }break;
        case C_TYPE_ENUM:
        {
            char buf[256] = {0};

            NSTEntry *ne = namespaceTableLookupEntry(globalContext.namespaceTable, type->namespaceName);
            char *nameSpaceName = allocNamespaceNameWithoutUnderscores(ne);

            sprintf(buf, "%s::%s", nameSpaceName, type->enumType.name);

            name = malloc(strlen(buf) + 1);
            
            strcpy(name, buf);
        }break;

        case C_TYPE_NAMESPACE:
        {
            char buf[256] = {0};

            NSTEntry *ne = namespaceTableLookupEntry(globalContext.namespaceTable, type->namespaceType.namespace);
            char *nameSpaceName = allocNamespaceNameWithoutUnderscores(ne);

            sprintf(buf, "%s", nameSpaceName);

            name = malloc(strlen(buf) + 1);
            
            strcpy(name, buf);
        }break;

        case C_TYPE_TUPLE:
        {
            char buf[256] = {0};

            strcat(buf,  "(");

            for(size_t i = 0; i < type->tupleType.numTypes; i++)
            {
                strcat(buf, allocCheckerTypeToString(type->tupleType.types[i]));

                if(i < type->tupleType.numTypes - 1) strcat(buf, ", ");
            }

            strcat(buf,  ")");
            name = malloc(strlen(buf) + 1);
            
            strcpy(name, buf);
        }break;
        case C_TYPE_FUNC:
        {
            char buf[256] = {0};

            strcat(buf,  "(");

            ScopedDeclLL *currParamLL = type->funcType.paramLL;
            if(currParamLL != NULL) currParamLL = currParamLL->first;

            while(currParamLL != NULL)
            {
                strcat(buf, allocCheckerTypeToString(currParamLL->item->type));

                currParamLL = currParamLL->next;
                if(currParamLL != NULL) strcat(buf, ", ");

            }
            strcat(buf,  ")");

            strcat(buf, " : ");

            strcat(buf, allocCheckerTypeToString(type->funcType.ret));

            name = malloc(strlen(buf) + 1);
            
            strcpy(name, buf);
        }break;
        default:
        {
            fprintf(stderr, "Unhandled type info type name generation");
        }break;
    }

    return name;
}
