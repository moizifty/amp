#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <float.h>
#include <llvm-c/Core.h>
#include "lexer.h"
#include "platform.h"

#define TYPE_SIZE_UNSET -1
#define TYPE_INT_SIZE 4

#define TYPE_UNSIGNED_FLAG          (1 << 0)
#define TYPE_FOREIGN_FLAG           (1 << 1)
#define TYPE_USE_NAMESPACE_FLAG     (1 << 2)
#define TYPE_FUNC_MUST_USE_RETURN   (1 << 3)
#define TYPE_DISTINCT_FLAG          (1 << 4)
#define TYPE_ENUM_FLAGS             (1 << 5)
#define TYPE_ENUM_STRICT_FLAG       (1 << 6)
#define TYPE_FUNC_VARIADIC_FLAG     (1 << 7)
#define TYPE_FUNC_THIS_FLAG         (1 << 8)
#define TYPE_FUNC_THISPTR_FLAG      (1 << 9)
#define TYPE_FUNC_THIS_NONE_FLAG    (1 << 10)
#define TYPE_TOSTRING_FUNC_FLAG    (1 << 11)
#define TYPE_EXPORT_AS    (1 << 12)
#define CHECK_TYPE_FLAG(type, flag) ((type)->flags & (flag))
#define SET_TYPE_FLAG(type, flag) ((type)->flags |= (flag))


#define SCOPED_DECL_HAS_DEF_VALUE                   (1 << 0)
#define SCOPED_DECL_OCCURS_AFTER_DEF_VALUE_PARAM    (1 << 1)
#define SCOPED_DECL_VALUE_REQUIRED                  (1 << 2)
#define SCOPED_DECL_CONST                           (1 << 3)
#define SET_SCOPED_DECL_FLAG(SCOPED_DECL_PTR, flag) ((SCOPED_DECL_PTR)->flags |= (flag))
#define CHECK_SCOPED_DECL_FLAG(SCOPED_DECL_PTR, flag) ((SCOPED_DECL_PTR)->flags & (flag))

#define OPERFUNC_PLUS_OP_NAME       "ADD"
#define OPERFUNC_MINUS_OP_NAME      "MINUS"
#define OPERFUNC_INC_OP_NAME        "INC"
#define OPERFUNC_DEC_OP_NAME        "DEC"
#define OPERFUNC_MULT_OP_NAME       "MULT"
#define OPERFUNC_DIV_OP_NAME        "DIV"
#define OPERFUNC_MOD_OP_NAME        "MOD"
#define OPERFUNC_GREATER_OP_NAME    "GREATER"
#define OPERFUNC_LESSER_OP_NAME     "LESSER"
#define OPERFUNC_GE_OP_NAME         "GREATEREQUAL"
#define OPERFUNC_LE_OP_NAME         "LESSEREQUAL"
#define OPERFUNC_EQ_OP_NAME         "EQUAL"
#define OPERFUNC_NEQ_OP_NAME        "NEQUAL"
#define OPERFUNC_INDEX_OP_NAME      "INDEX"
#define OPERFUNC_NULL_OP_NAME      "NULLOP"

typedef struct CheckerType CheckerType;
typedef enum CheckerTypeKind CheckerTypeKind;

typedef struct CheckerTypeLL CheckerTypeLL;
typedef struct CheckerTypeLLLL CheckerTypeLLLL;
typedef struct StructUnionDependencyNode StructUnionDependencyNode;
typedef struct StructUnionDependencyNodeLL StructUnionDependencyNodeLL;

typedef struct ScopedDeclLL ScopedDeclLL;
typedef struct EnumMembLL EnumMembLL;

typedef struct ScopedDecl
{
    Token name;
    CheckerType *type;
    struct ASTExpr *defExpr;

    int flags;

    struct ArgABIInfo *abiInfo;
    struct SymEntry *mySymEntry;
}ScopedDecl;

struct ScopedDeclLL
{
    ScopedDecl *item;
    struct ScopedDeclLL *prev;
    struct ScopedDeclLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ScopedDeclLL *first; //first item in ll
};

typedef struct EnumMemb
{
    char *name;
    int64_t val;
}EnumMemb;

struct EnumMembLL
{
    EnumMemb *item;
    struct EnumMembLL *prev;
    struct EnumMembLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct EnumMembLL *first; //first item in ll
};


enum CheckerTypeKind
{
    C_TYPE_VOID,
    C_TYPE_INT,
    C_TYPE_FLOAT,
    C_TYPE_BOOL,
    C_TYPE_STRING,
    C_TYPE_ARRAY,
    C_TYPE_POINTER,
    C_TYPE_ALIASED,
    C_TYPE_STRUCT,
    C_TYPE_UNION,
    C_TYPE_TAGGED_UNION,
    C_TYPE_ENUM,
    C_TYPE_NAMESPACE,

    C_TYPE_TUPLE,
    C_TYPE_FUNC,
    C_TYPE_VARIADIC,

    C_TYPE_OPERATOR_OVERLOAD,

    C_TYPE_GENERIC,
};
struct CheckerType
{
    CheckerTypeKind kind;
    char *belongsToFile; 
    char *namespaceName;
    size_t typeId;
    int64_t byteSize;
    size_t alignment;
    int flags;

    ScopedDeclLL *instMethods;

    CheckerTypeLL *typeFilters;

    void *backendType;
    union
    {
        struct
        {
            char *name;
        }genericType;
        
        struct
        {
            CheckerType *base;
            char *name;
        }aliasedType;

        struct
        {
            CheckerType *base;
            size_t size;
            bool isSlice;
            bool isVariadic;
        }arrayType;

        struct
        {
            CheckerType *base;
        }pointerType;

        struct
        {
            ScopedDeclLL *declLL;
            char *name;
            
            bool isFullyChecked;

            StructUnionDependencyNode *dependencies;
            bool hasGeneratedDefinition; 
        }structType;

        struct
        {
            ScopedDeclLL *declLL;
            char *name;

            bool isFullyChecked;

            StructUnionDependencyNode *dependencies;
            bool hasGeneratedDefinition; 
        }unionType;

        struct
        {
            ScopedDeclLL *declLL;
            char *name;

            bool isFullyChecked;

            StructUnionDependencyNode *dependencies;
            bool hasGeneratedDefinition; 

            CheckerType *tagEnumType;
        }taggedUnionType;

        struct
        {
            EnumMembLL *membLL;
            char *name;

            bool isTypeBelongingToTaggedUnionMemberAccess; //is this type for the expr u1.A, where u1 is a tagged union
            struct
            {
                CheckerType *taggedUnionType;
                ScopedDecl *unionMember;
            }taggedUnionMemberAccess;
        }enumType;

        struct
        {
            ScopedDeclLL *paramLL;
            CheckerType *ret;
            struct ArgABIInfo *retABIInfo;

            size_t numDefaultParams;

            bool isOperFunc;
            bool retAsArg;
            bool isTypeMethod;

            struct ASTDeclLL *genericFuncAST;
        }funcType;

        struct
        {
            ScopedDeclLL *declLL;
            CheckerType *ret;

            char *funcName;
        }opFuncType;

        struct
        {
            char *namespace;
            struct SymTable *tble;
        }namespaceType;

        struct
        {
            CheckerType *base;
        }variadicType;

        struct
        {
            CheckerType **types;
            size_t numTypes;

            StructUnionDependencyNode *dependencies;
            bool hasGeneratedDefinition;
        }tupleType;
    };
};

struct CheckerTypeLL
{
    CheckerType *item;
    struct CheckerTypeLL *prev;
    struct CheckerTypeLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct CheckerTypeLL *first; //first item in ll
};

struct CheckerTypeLLLL
{
    CheckerTypeLL *item;
    struct CheckerTypeLLLL *prev;
    struct CheckerTypeLLLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct CheckerTypeLLLL *first; //first item in ll
};

struct StructUnionDependencyNode
{
    CheckerType *type;
    
    StructUnionDependencyNode *parent;
    StructUnionDependencyNodeLL *children;
};

struct StructUnionDependencyNodeLL
{
    StructUnionDependencyNode *item;
    struct StructUnionDependencyNodeLL *prev;
    struct StructUnionDependencyNodeLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct StructUnionDependencyNodeLL *first; //first item in ll
};

typedef struct TypeTable
{
    CheckerTypeLL *typeLL;
}TypeTable;

void setupPrimitiveTypes(void);

CheckerType *allocCheckerType(CheckerTypeKind kind);

CheckerType *getVoidType();
CheckerType *getIntType(int size, int flags);
CheckerType *getFloatType(int size, int flags);
CheckerType *getBoolType();
CheckerType *getAliasedType(CheckerType *base, char *name, int flags);
CheckerType *getArrayType(CheckerType *base, int length, bool isSlice, bool isVariadic);
CheckerType *getPtrType(CheckerType *base);
CheckerType *getStructType(ScopedDeclLL *declLL, char *name, int flags);
CheckerType *getUnionType(ScopedDeclLL *declLL, char *name, int flags);
CheckerType *getTaggedUnionType(ScopedDeclLL *declLL, char *name, CheckerType *tagEnumType, int flags);
CheckerType *getEnumType(EnumMembLL *membLL,char *name, int flags);
CheckerType *getEnumMembType(CheckerType *enumType, size_t val);
CheckerType *getFuncType(ScopedDeclLL *paramLL, CheckerType *ret, bool retAsArg, int flags);
CheckerType *getOperFuncType(char *name, ScopedDeclLL *paramLL, CheckerType *ret, int flags);
CheckerType *getNamespaceType(char *namespace, struct SymTable *tbl);
CheckerType *getTypeTupleType(CheckerTypeLL *types);

CheckerType *newCheckerTypeVoid();
CheckerType *newCheckerTypeInt(int size, int flags);
CheckerType *newCheckerTypeFloat(int size, int flags);
CheckerType *newCheckerTypeBool();
CheckerType *newCheckerTypeString();

CheckerType *newCheckerTypeAliased(CheckerType *base, char *name, int flags);
CheckerType *newCheckerTypeArray(CheckerType *base, int length, bool isSlice, bool isVariadic);
CheckerType *newCheckerTypePointer(CheckerType *base);

ScopedDecl *newScopedDecl(Token name, CheckerType *type, struct ASTExpr *defExpr, int flags);
ScopedDeclLL *newScopedDeclLL(ScopedDecl *item);
void addScopedDeclLL(ScopedDeclLL **ll, ScopedDecl *item);
ScopedDeclLL *getScopedDeclLLAt(ScopedDeclLL *ll, size_t index);

EnumMemb *newEnumMemb(char *name, int64_t val);
EnumMembLL *newEnumMembLL(EnumMemb *item);
void addEnumMembLL(EnumMembLL **ll, EnumMemb *item);
EnumMembLL *getEnumMembLLAt(EnumMembLL *ll, size_t index);

CheckerType *newCheckerTypeGenericType(char *name);
CheckerType *newCheckerTypeStruct(ScopedDeclLL *declLL, char *name, int flags);
CheckerType *newCheckerTypeUnion(ScopedDeclLL *declLL,char *name, int flags);
CheckerType *newCheckerTypeTaggedUnion(ScopedDeclLL *declLL, char *name, CheckerType *tagEnumType, int flags);
CheckerType *newCheckerTypeEnum(EnumMembLL *membLL,char *name, int flags);
CheckerType *newCheckerTypeFunc(ScopedDeclLL *paramLL, CheckerType *ret, bool retAsArg, int flags);
CheckerType *newCheckerTypeVariadic(CheckerType *base);
CheckerType *newCheckerTypeOperFunc(char *name, ScopedDeclLL *paramLL, CheckerType *ret, int flags);
CheckerType *newCheckerTypeNamespace(char *namespace, struct SymTable *tbl);
CheckerType *newCheckerTypeTuple(CheckerTypeLL *types);

CheckerTypeLL *newCheckerTypeLL(CheckerType *item);
void addCheckerTypeLL(CheckerTypeLL **ll, CheckerType *item);
bool CheckerTypeInCheckerTypeLL(CheckerTypeLL *ll, CheckerType *item);
//does a have any items in common with b
bool haveCommonCheckerTypeLL(CheckerTypeLL *a, CheckerTypeLL *b);
void printCheckerTypeLL(CheckerTypeLL *ll);

CheckerTypeLLLL *newCheckerTypeLLLL(CheckerTypeLL *item);
void addCheckerTypeLLLL(CheckerTypeLLLL **ll, CheckerTypeLL *item);
StructUnionDependencyNode *newStructUnionDependencyNode(CheckerType *type, StructUnionDependencyNode *parent, StructUnionDependencyNodeLL *children);
void addStructUnionDependencyNodeChildren(StructUnionDependencyNode *node, StructUnionDependencyNode *toAdd);

StructUnionDependencyNodeLL *newStructUnionDependencyNodeLL(StructUnionDependencyNode *item);
void addStructUnionDependencyNodeLL(StructUnionDependencyNodeLL **ll, StructUnionDependencyNode *item);


TypeTable *newTypeTable();
void addTypeToTypeTable(CheckerType *t);

CheckerType *copyCheckerType(CheckerType *toCopy);
#include "primitiveTypes.h"

CheckerType *castSignedIntLiteralToCorrectType(int64_t val);
CheckerType *castUnsignedIntLiteralToCorrectType(uint64_t val);

CheckerType *castFloatLiteralToCorrectType(double val);

bool getEmbeddedStructsInStructMember(CheckerType *structType, ScopedDecl *memb, CheckerTypeLLLL **structsList, bool recursing);
bool getImbeddedStructsInStruct(CheckerType *structType, CheckerTypeLL **structStack);
StructUnionDependencyNode *getStructUnionDependencies(CheckerType *type, StructUnionDependencyNode *parent, bool *foundCircular, ScopedDecl **whichMemberCausedCircular);

bool isTypeInteger(CheckerType *type);
bool isTypeFloat(CheckerType *type);
bool isTypeBoolean(CheckerType *type);
bool isTypeString(CheckerType *type);

bool isTypeAliased(CheckerType *type);
CheckerType *getAliasedTypeBase(CheckerType *aliasedType);

bool isTypeVariadic(CheckerType *type);

bool isTypeStruct(CheckerType *type);
bool isTypeUnion(CheckerType *type);
bool isTypeTaggedUnion(CheckerType *type);
bool isTypeEnum(CheckerType *type);
bool isTypeNamespace(CheckerType *type);
bool isTypeGeneric(CheckerType *type);

bool isTypeFunction(CheckerType *type);
bool isTypeVoid(CheckerType *type);
bool isTypeVoidPtr(CheckerType *type);
bool isTypeArray(CheckerType *type);
bool isTypeFixedArray(CheckerType *type);
bool isTypeSliceArray(CheckerType *type);
CheckerType *getArrayBase(CheckerType *array);
CheckerType *getArrayNonArrayBase(CheckerType *array);

bool isTypeTuple(CheckerType *type);

bool isTypePointer(CheckerType *type);
bool isTypePointerOrFunction(CheckerType *type);
CheckerType *getPointerTypeBase(CheckerType *pointerType);
//can you preform a [] operation on the type.
bool isTypeIndexable(CheckerType *type);
//can yoiu use '. operator on type
bool isTypeMemberAccessable(CheckerType *type, bool *wasEnum);

bool typeHasMember(CheckerType *t, char *name, size_t *membIndex);
bool taggedUnionTypeGetBestMember(CheckerType *taggedUnion, CheckerType *membTypeToCheck, bool *castableFound,size_t *membIndex)
;

bool funcHasParam(CheckerType *funcType, char *paramName, size_t *index);

bool areTypesEqual(CheckerType *a, CheckerType *b);
bool canImplicitCast(CheckerType *from, CheckerType *to);
bool canImplicitCastToInt(CheckerType *from, CheckerType *intType);
bool canImplicitCastValueToInt(CheckerType *iType, int64_t val);
bool canImplicitCastToFloat(CheckerType *from, CheckerType *floatType);
bool canImplicitCastToArray(CheckerType *from, CheckerType *to);
bool canImplicitCastToEnum(CheckerType *from, CheckerType *to);

void printCheckerType(CheckerType *type);

//converts a checker type into a string name eg, [3]int becomes array_3_int or something
char *allocCheckerTypeName(CheckerType *type);
char *allocOperFuncName(Token op, ScopedDeclLL *params, CheckerType *retType);
char *allocOperFuncNameFromTypes(TokType op, CheckerType *a, CheckerType *b);

int64_t checkerTypeGetSizeWin(CheckerType *type);
int64_t checkerTypeGetAlignmentWin(CheckerType *type);

int64_t checkerTypeGetSizeUnix(CheckerType *type);
int64_t checkerTypeGetAlignmentUnix(CheckerType *type);


int64_t checkerTypeGetSize(CheckerType *type);
int64_t checkerTypeGetAlignment(CheckerType *type);

bool isTypeAggregate(CheckerType *type);

char *allocCheckerTypeToString(CheckerType *type);

#endif