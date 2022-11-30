#ifndef AST_H
#define AST_H

#include "lexer.h"
#include <stdint.h>
#include "symtable.h"
#include "namespacetable.h"
#include <llvm-c/Core.h>

typedef struct ASTExpr  ASTExpr;
typedef enum ASTExprKind ASTExprKind;
typedef enum ExprCompTimeValKind ExprCompTimeValKind;

typedef struct ASTExprLL ASTExprLL;
typedef struct ASTNamedExprLL ASTNamedExprLL;
typedef struct ASTNamedExpr ASTNamedExpr;
typedef enum ASTNamedExprKind ASTNamedExprKind;

typedef struct ASTGenericType ASTGenericType;
typedef struct ASTGenericTypeLL ASTGenericTypeLL;

typedef struct ASTFuncParam ASTFuncParam;
typedef struct ASTFuncParamLL ASTFuncParamLL;
typedef struct ASTFuncSig ASTFuncSig;

typedef struct ASTTypeLL ASTTypeLL;
typedef struct ASTType  ASTType;
typedef enum ASTTypeKind ASTTypeKind;

typedef struct ASTTupleMemb ASTTupleMemb;
typedef struct ASTTupleMembLL ASTTupleMembLL;

typedef struct ASTBlock  ASTBlock;
typedef enum ASTBlockKind ASTBlockKind;

typedef struct ASTStmt  ASTStmt;
typedef enum ASTStmtKind ASTStmtKind;
typedef enum ASTStmtUsingKind ASTStmtUsingKind;
typedef struct ASTStmtLL  ASTStmtLL;

typedef struct ASTMatchArmLL  ASTMatchArmLL;
typedef struct ASTMatchArm  ASTMatchArm;

typedef struct ASTDecl  ASTDecl;
typedef enum ASTDeclKind ASTDeclKind;

typedef struct ASTDeclLL  ASTDeclLL;

typedef struct ASTTag ASTTag;

typedef struct ASTTagLL ASTTagLL;

typedef struct ASTTagParamLL ASTTagParamLL;

typedef struct ASTEnumMembLL  ASTEnumMembLL;

typedef struct ASTEnumMemb  ASTEnumMemb;

typedef struct ASTCompilerTag ASTCompilerTag;
typedef struct ASTCompilerTagLL ASTCompilerTagLL;

typedef struct ProgCompilerSettings ProgCompilerSettings;

typedef struct ASTNamespaceWithBlock ASTNamespaceWithBlock;
typedef struct ASTNamespaceWithStmt ASTNamespaceWithStmt;
typedef struct ASTNamespaceWithStmtLL ASTNamespaceWithStmtLL;
typedef struct ASTNamespaceWithValue ASTNamespaceWithValue;
typedef struct ASTNamespaceWithValueLL ASTNamespaceWithValueLL;

typedef struct ASTProg ASTProg;
typedef struct ASTProgLL ASTProgLL;

typedef struct ASTNamespaceName ASTNamespaceName;

typedef struct TokenLL TokenLL;


#include "types.h"

enum ASTExprKind
{
    A_EXPR_LIT,
    A_EXPR_ENUM_INFER_LIT,
    A_EXPR_IDEN,
    A_EXPR_FUNC_CALL,
    A_EXPR_INDEX_REF,
    A_EXPR_UNARY,
    A_EXPR_BINARY,
    A_EXPR_RANGE,
    A_EXPR_MEMBER_ACCESS, //UNION/STRUCT MEMEMBER ACCESS
    A_EXPR_NULL_ACCESS, //UNION/STRUCT MEMEMBER ACCESS
    A_EXPR_TYPE_INTRO,
    A_EXPR_EXPR_INTRO,
    A_EXPR_ASSTRING,
    A_EXPR_ARRAY_LITERAL,
    A_EXPR_STRUCT_LITERAL,
    A_EXPR_TUPLE_LITERAL,

    A_EXPR_POST, // for a++ or a--
    A_EXPR_CAST,

    A_EXPR_BLOCK,
    A_EXPR_WHILE,
    A_EXPR_IF,
    A_EXPR_MATCH,

    A_EXPR_SCOPE_ACCESS,
};

#define IS_EXPR_COMPTIME_KIND(EXPR_PTR, KIND) ((EXPR_PTR)->compTimeVal.kind == (KIND)) 
#define GET_EXPR_COMPTIME_VAL(EXPR_PTR, which) ((EXPR_PTR)->compTimeVal which)

        
struct ASTExpr
{
    ASTExprKind kind;
    Token startTok;
    Token endTok;
    CheckerType *checkType;
    
    bool hasBeenChecked;
    bool genAnyCastForDefaultValueExpr;
    bool genFuncRetAsArgForDefaultValueExpr;
    char *genIdenName; //when generating c code,this will be used instead of generating the expression
    LLVMValueRef bckendVal;

    SymEntry *idenSymEntry;
    CompTimeVal compTimeVal;
    bool isExprConst; // does the expression contain only contains expressions or is constant
    union
    {
        Token lit;    
        Token iden;

        struct
        {
            ASTExpr *iden;
            ASTNamedExprLL *args;
             //each item in arraw tells the index of the next item
             // 1 3 2 0 -1
             //means arg 1 goes first, then 3, then 2 then 0, -1 means end
            ASTExpr **argCallOrder;
            size_t argCallOrderCount;

            char *retAsArg_argWhichIsRetName;
            bool retAsArg_emittedIdenName;
        }funcCall;
        struct
        {
            ASTExpr *iden;
            ASTExpr *index;
        }arrayRef;
        struct
        {
            ASTExpr *typeName;
            Token memb;

            bool isMethodAccess;
        }membAccess;
        struct
        {
            ASTExpr *access;
            ASTExpr* elseExpr;

            //if access is casted this stores the uncased expr
            //used to ddetermine type in backend
            //if no cast then this equals access
            ASTExpr *preCastMembAccessExpr;
            ASTExpr *preCastElseExpr;
        }nullAccess;
        struct
        {
            ASTExpr *scopeName;
            Token memb;
        }scopeAccess;
        struct
        {
            Token op;
            ASTExpr *expr;
        }post;

        struct
        {
            Token op;
            ASTExpr *expr;
        }unary;
        struct
        {
            ASTType *castTo;
            ASTExpr *expr;
        }cast;
        struct
        {
            ASTExpr *expr;
        }exprIntro;
        struct
        {
            ASTType *type;
        }typeIntro;
        struct
        {
            ASTExpr *expr;
        }asString;
        struct
        {
            Token op;
            ASTExpr *l;
            ASTExpr *r;
        }binary;

        struct
        {
            ASTExpr *l;
            ASTExpr *r;
        }range;

        struct
        {
            ASTType *typeName;
            ASTNamedExprLL *exprLL;
             //each item in arraw tells the index of the next item
             // 1 3 2 0 -1
             //means arg 1 goes first, then 3, then 2 then 0, -1 means end
            ASTNamedExpr **membExprOrder;
            size_t membExprOrderCount;
        }structunionLit;
        struct
        {
            ASTExpr *sizeExpr;
            ASTType *type;
            ASTExprLL *exprLL;
        }arrayLit;

        struct
        {
            ASTBlock *b;
            char *idenName; 
            /* 
                * block expressions and other similar types of expressions
                * are gonna be generated as the following
                * lets say u do this: in my lang
                * a := { 
                *      b := 90;
                *      b;
                * }
                * 
                * in c this will be the following
                * 
                * int idenName = 0;
                * {
                *      int b = 90;
                *      idenName = b;
                * }
                * 
                * int a = idenName;
            */
        }block;

        struct
        {
            ASTStmt *ifStmt;
            char *idenName; 
        }ifExpr;

        struct
        {
            ASTStmt *whileStmt;
            char *idenName; 
        }whileExpr;

        struct
        {
            ASTStmt *matchStmt;
            char *idenName; 
        }match;

        struct
        {
            ASTExprLL *exprLL;
        }tupleLit;
    };
};

struct ASTExprLL //LL for linked list
{
    ASTExpr *item;
    struct ASTExprLL *prev;
    struct ASTExprLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTExprLL *first; //first item in ll
};
struct ASTNamedExprLL
{
    ASTNamedExpr *item;
    struct ASTNamedExprLL *prev;
    struct ASTNamedExprLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTNamedExprLL *first; //first item in ll
};
enum ASTNamedExprKind
{
    A_NAMED_EXPR_NORMAL,
    A_NAMED_EXPR_NAMED,
};
struct ASTNamedExpr
{
    Token startTok;
    Token endTok;

    ASTNamedExprKind kind;
    CheckerType *checkType;
    bool isVariadicArrayArg; //only valid if function call
    
    union
    {
        ASTExpr *normal;
        struct
        {
            Token name;
            ASTExpr *expr;
            int nameIndex; //index in the function sig parameter list, assigned during checking
                           // or index in struct/union literal
        }named;
    };
};

struct ASTGenericType
{
    Token iden;
    //also include filters here, eg if generic param is only allowed to be a struct or somethin
};
struct ASTGenericTypeLL
{
    ASTGenericType *item;
    struct ASTGenericTypeLL *prev;
    struct ASTGenericTypeLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTGenericTypeLL *first; //first item in ll
};

struct ASTFuncParam
{
    Token iden;
    Token endTok;

    ASTType *type;
    ASTExpr *defValue;

    bool isTypeMethodThisParam;
    bool isVariadicArrayArg; //if a variadic arg is ...int, is this param, []int ?
};
struct ASTFuncParamLL //LL for linked list
{
    ASTFuncParam *item;
    struct ASTFuncParamLL *prev;
    struct ASTFuncParamLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTFuncParamLL *first; //first item in ll
};
struct ASTFuncSig
{
    Token startTok; // used for positons (so errors can be given)
    Token endTok; // used for positons (so errors can be given)

    ASTFuncParamLL *params;
    int paramCount;

    ASTType *retType;

    bool retAsArg;
    Token retAsArgName;
};

enum ASTTypeKind
{
    A_TYPE_IDEN,
    A_TYPE_VOID,
    A_TYPE_INT,
    A_TYPE_FLOAT,
    A_TYPE_STRING,
    A_TYPE_BOOL,
    A_TYPE_ARRAY,
    A_TYPE_POINTER,
    A_TYPE_FUNC_POINTER,
    A_TYPE_TUPLE, 

    A_TYPE_VARIADIC,
    A_TYPE_INFER,

    A_TYPE_SCOPED,
};
struct ASTType
{
    ASTTypeKind kind;
    Token startTok; // used for positons (so errors can be given)
    Token endTok;
    bool isFreed;
    CheckerType *checkType;
    ASTTypeLL *typeFilter;
    
    union
    {
        Token iden;

        struct
        {
            ASTExpr *size;
            ASTType *elem;

            bool isSlice;
        }array;

        struct
        {
            ASTType *type;
        }pointer;

        struct
        {
            ASTFuncSig *funcSig;
        }funcPointer;

        struct
        {
            ASTType *base;
        }variadic;

        struct
        {
            ASTExpr *expr;
        }scoped;

        struct
        {
            ASTTupleMembLL *membs;
        }tuple;
    };
};

struct ASTTupleMemb
{
    Token startTok;
    Token endTok;

    bool hasIden;
    Token iden;

    ASTType *type;
};

struct ASTTupleMembLL
{
    ASTTupleMemb *item;
    struct ASTTupleMembLL *prev;
    struct ASTTupleMembLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTTupleMembLL *first; //first item in ll
};

struct ASTTypeLL
{
    ASTType *item;
    struct ASTTypeLL *prev;
    struct ASTTypeLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTTypeLL *first; //first item in ll
};

struct ASTBlock
{
    Token startTok;
    Token endTok;
    ASTStmtLL *stmts;
};

enum ASTStmtKind
{
    A_STMT_DECL,
    A_STMT_IF,
    A_STMT_WHILE,
    A_STMT_FOR,
    A_STMT_RET,
    A_STMT_CONTINUE,
    A_STMT_BREAK,
    A_STMT_DEFER,
    A_STMT_USING,
    A_STMT_MATCH,
    A_STMT_ASSIGN,
    A_STMT_EXPR,
    A_STMT_BLOCK,
};
enum ASTStmtUsingKind
{
    A_STMT_USING_IMPORT, //usimg  import std::io
    A_STMT_USING_NORMAL, //using SomeIden
};

struct ASTStmt
{
    ASTStmtKind kind;
    Token startTok;
    Token endTok;

    bool isTaggedUnionLetDecl;
    CheckerType *ifTaggedUnionType;
    size_t belongsToMatchExprIndex;
    union
    {
        struct
        {
            ASTDecl *decl;
        }decl;

        struct
        {
            ASTExpr *cond;
            ASTBlock *block;
            ASTBlock *elseBlock;
        }ifStmt;

        struct
        {
            ASTExpr *expr;
            ASTBlock *block;
        }whileStmt;

        struct
        {
            ASTExpr *l;
            ASTExpr *r;
            ASTBlock *block;

            SymEntry *lExprSymEntry;
            SymEntry *indexSymEntry;
        }forStmt;

        struct
        {
            ASTExpr *expr;
        }retStmt;

        struct
        {
            ASTBlock *block;
        }deferStmt;

        struct
        {
            ASTStmtUsingKind kind;
            union 
            {
                ASTExpr *expr;
                ASTDecl *importDecl;
            };
            
        }usingStmt;

        struct
        {
            ASTExprLL *matchExprLL;
            ASTMatchArmLL *matchArmLL;

            //fill during checking.
            ASTExpr **matchExprList;
            char **matchExprCompilerIdens;

            struct SymEntry **matchExprCompilerIdenSymEntry; //for llvm backend
        }match;

        struct
        {
            ASTExpr *l;
            ASTExpr *r;
        }assign;

        struct
        {
            ASTExpr *expr;
        }expr;

        struct //cannot be directly created by user, is internally used by compiler
        {
            ASTBlock *block;
        }blockStmt;
    };
};

struct ASTStmtLL
{
    ASTStmt *item;
    struct ASTStmtLL *prev;
    struct ASTStmtLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTStmtLL *first; //first item in ll
};

struct ASTMatchArmLL
{
    ASTMatchArm *item;
    struct ASTMatchArmLL *prev;
    struct ASTMatchArmLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTMatchArmLL *first; //first item in ll
};
struct ASTMatchArm
{
    Token startTok;
    Token endTok;
    ASTExprLL **armExprLLs;
    size_t armExprLLsCount;
    ASTBlock *block;

    //used in codeggen stage, assigned at checked
    ASTStmtLL *taggedUnionAutoCasts;
};

enum ASTDeclKind
{
    A_DECL_VAR,
    A_DECL_IMMUT,
    A_DECL_CONST,
    A_DECL_TYPE,
    A_DECL_FUNC,
    A_DECL_OPERATOR_FUNC,
    A_DECL_STRUCT,
    A_DECL_UNION,
    A_DECL_UNION_TAGGED,
    A_DECL_ENUM,
    A_DECL_IMPORT,
    A_DECL_DECLLIST,
    A_DECL_METHOD_BLOCK,
};

struct ASTDecl
{
    ASTDeclKind kind;
    Token startTok;
    Token endTok;

    ASTTagLL *tags;
    ASTGenericTypeLL *genParams;
    SymTable *genParamsResolvedSymTbl;

    bool isCDecl; //is it a decl from C
    
    SymTable *tbl;
    //if a import with no namespace  is being imported this is needed
    //so that the decl can be added to two symbol tables
    //the global one and one for the import identifier
    SymTable *importedIdenTbl;
    CheckerType *declType;

    LLVMValueRef backendValRef;
    union
    {
        struct
        {
            ASTExpr *idenExpr;
            ASTType *type;
            ASTExpr *initial;

            bool isUsingDecl; //is a using decl inside struct

            //any literal / func statements for global vars tht should be genenrated before global var
            ASTStmtLL *globalVarInitialCodeGenStmts;
        }var;

        struct
        {
            ASTExpr *idenExpr;
            ASTType *type;
            ASTExpr *initial;

            bool isUsingDecl; //is a using decl inside struct
        }immut;

        struct
        {
            Token iden;
            ASTType *type;
            ASTExpr *initial;
        }constDecl;

        struct
        {
            Token iden;
            ASTType *type;
        }type;

        struct
        {
            Token iden;
            ASTFuncSig *sig;
            ASTBlock *block;
            bool mustReturn;
        }func;

        struct
        {
            Token op;
            ASTFuncSig *sig;
            ASTBlock *block;

            char *mangledName;
        }operFunc;

        struct
        {
            Token iden;
            ASTDeclLL *membs;
        }structDecl;

        struct
        {
            Token iden;
            ASTDeclLL *membs;
        }unionDecl;

        struct
        {
            Token iden;
            ASTType *tagTypeName;
            ASTDeclLL *membs;
        }unionTaggedDecl;

        struct
        {
            Token iden;
            ASTEnumMembLL *membs;
        }enumDecl;

        struct
        {
            Token iden;
            bool hasIden;
            ASTNamespaceName *namespace;
        }import;

        struct
        {
            ASTDeclLL *declLL;
        }declList;

        struct
        {
            ASTType *type;
            ASTDeclLL *declLL;
        }methodBlock;
    };
};

struct ASTDeclLL
{
    ASTDecl *item;
    struct ASTDeclLL *prev;
    struct ASTDeclLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTDeclLL *first; //first item in ll
};

struct ASTTag
{
    Token name;
    Token endTok;

    ASTTagParamLL *paramLL;
};

struct ASTTagLL
{
    ASTTag *item;
    struct ASTTagLL *prev;
    struct ASTTagLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTTagLL *first; //first item in ll
};

struct ASTTagParamLL
{
    Token item;
    struct ASTTagParamLL *prev;
    struct ASTTagParamLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTTagParamLL *first; //first item in ll
};

struct ASTEnumMembLL
{
    ASTEnumMemb *item;
    struct ASTEnumMembLL *prev;
    struct ASTEnumMembLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTEnumMembLL *first; //first item in ll
};

struct ASTEnumMemb
{
    Token id;
    Token endTok;

    ASTExpr *value;
};

struct ASTCompilerTag
{
    Token name;
    Token startTok;
    Token endTok;
    ASTTagParamLL *paramLL;
};
struct ASTCompilerTagLL
{
    ASTCompilerTag *item;
    struct ASTCompilerTagLL *prev;
    struct ASTCompilerTagLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTCompilerTagLL *first; //first item in ll
};

struct ProgCompilerSettings
{
    struct
    {
        bool shouldImportGlobal;
    }importedSettings; //#[importedSettings] tag
};

struct ASTNamespaceWithBlock
{
    Token startTok;
    Token endTok;

    ASTNamespaceWithStmtLL *stmts;
};
struct ASTNamespaceWithStmt
{
    Token iden;
    Token endTok;

    ASTNamespaceWithValueLL *vals;
};
struct ASTNamespaceWithStmtLL
{
    ASTNamespaceWithStmt *item;
    struct ASTNamespaceWithStmtLL *prev;
    struct ASTNamespaceWithStmtLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTNamespaceWithStmtLL *first; //first item in ll
};
struct ASTNamespaceWithValue
{
    Token val;
};
struct ASTNamespaceWithValueLL
{
    ASTNamespaceWithValue *item;
    struct ASTNamespaceWithValueLL *prev;
    struct ASTNamespaceWithValueLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTNamespaceWithValueLL *first; //first item in ll
};

struct ASTProg
{
    Token startTok;
    Token endTok;
    char *filename;
    
    ASTNamespaceName *namespace;
    ASTNamespaceWithBlock *withBlock;
    
    bool isNamespaced;

    ASTCompilerTagLL *progTags;
    ASTDeclLL *declLL;

    ProgCompilerSettings compilerSettings;

    ASTDeclLL *globalImportDecls;
    ASTDeclLL *globalTypeDecls;
    ASTDeclLL *globalStructuredDecls;
    ASTDeclLL *globalEnumDecls;
    ASTDeclLL *globalFuncDecls;
    ASTDeclLL *globalVarOrConstDecls;
    ASTDeclLL *globalMethodBlockDecls;
    SymTable *tbl; //namespce table
    NSTEntry *myNSTEntry;
    bool hasBeenChecked;

    bool hasGeneratedHeader; //has the header for this prog been generated in the header file
    bool hasGeneratedDecls;
};
struct ASTProgLL
{
    ASTProg *item;
    struct ASTProgLL *prev;
    struct ASTProgLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct ASTProgLL *first; //first item in ll
};

struct ASTNamespaceName
{
    Token startTok;
    Token endTok;

    Token parent;
    struct ASTNamespaceName *scopedChild;
};

struct TokenLL
{
    Token item;
    struct TokenLL *prev;
    struct TokenLL *next;

    size_t numItems; //only true for last item in list, rest items have old item count
    struct TokenLL *first; //first item in ll
};

ASTExpr *allocASTExpr(ASTExprKind kind, Token startTok);
void freeASTExpr(ASTExpr **expr);

ASTExprLL *newASTExprLL(ASTExpr *item);
void addASTExprLL(ASTExprLL **ll, ASTExpr *item);
void freeASTExprLL(ASTExprLL **ll);
void insertBeforeASTExprLL(ASTExprLL *before, ASTExprLL *toInsert);
void insertAfterASTExprLL(ASTExprLL *before, ASTExprLL *toInsert);

ASTNamedExprLL *newASTNamedExprLL(ASTNamedExpr *item);
void addASTNamedExprLL(ASTNamedExprLL **ll, ASTNamedExpr *item);
void freeASTNamedExprLL(ASTNamedExprLL **ll);

ASTNamedExpr *allocASTNamedExpr(ASTNamedExprKind kind, Token startTok);
void freeASTNamedExpr(ASTNamedExpr **n);

ASTGenericType *newASTGenericType(Token iden);

ASTGenericTypeLL *newASTGenericTypeLL(ASTGenericType *item);
void addASTGenericTypeLL(ASTGenericTypeLL **ll, ASTGenericType *item);

ASTFuncParam *newASTFuncParam(Token iden, ASTType *type, ASTExpr *defValue);
void freeASTFuncParam(ASTFuncParam **param);

ASTFuncParamLL *newASTFuncParamLL(ASTFuncParam *item);
void addASTFuncParamLL(ASTFuncParamLL **ll, ASTFuncParam *item);
void freeASTFuncParamLL(ASTFuncParamLL **ll);

ASTFuncSig *newASTFuncSig(Token startTok, ASTFuncParamLL *params, int paramCount, ASTType *retType);
void freeASTFuncSig(ASTFuncSig **sig);

ASTType *allocASTType(ASTTypeKind kind, Token startTok);
void freeASTType(ASTType **type);

ASTExpr *newASTExprLit(Token lit);
ASTExpr *newASTExprIden(Token iden);
ASTExpr *newASTExprEnumInferLit(Token startTok, Token iden);

ASTExpr *newASTExprFuncCall(ASTExpr *iden, ASTNamedExprLL *args);
ASTExpr *newASTExprArrayRef(ASTExpr *iden, ASTExpr *index);
ASTExpr *newASTExprMembAccess(ASTExpr *typeName, Token memb);
ASTExpr *newASTExprNullAccess(ASTExpr *access, ASTExpr *elseExpr);
ASTExpr *newASTExprScopeAccess(ASTExpr *scopeName, Token memb);
ASTExpr *newASTExprPost(Token op, ASTExpr *expr);

ASTExpr *newASTExprUnary(Token op, ASTExpr *expr);
ASTExpr *newASTExprCast(ASTType *castTo, ASTExpr *expr);
ASTExpr *newASTExprExprIntro(ASTExpr *expr);
ASTExpr *newASTExprTypeIntro(ASTType *type);
ASTExpr *newASTExprAsString(Token startTok, ASTExpr *expr);
ASTExpr *newASTExprBinary(Token op, ASTExpr *l, ASTExpr *r);

ASTExpr *newASTExprRange(ASTExpr *l, ASTExpr *r);

ASTExpr *newASTExprStructunionLit(Token startTok, ASTType *typeName, ASTNamedExprLL *exprLL);
ASTExpr *newASTExprArrayLit(Token startTok, ASTExpr *sizeExpr, ASTType *type, ASTExprLL *exprLL);
ASTExpr *newASTExprTupleLit(Token startTok, ASTExprLL *exprLL);

ASTExpr *newASTExprBlock(ASTBlock *b);
ASTExpr *newASTExprIf(ASTStmt *ifStmt);
ASTExpr *newASTExprWhile(ASTStmt *whileStmt);
ASTExpr *newASTExprMatch(ASTStmt *matchStmt);

ASTNamedExpr *newASTNamedExprNormal(ASTExpr *expr);
ASTNamedExpr *newASTNamedExprNamed(Token name, ASTExpr *expr);


ASTType *newASTTypeIden(Token iden);
ASTType *newASTTypeVoid(Token iden);
ASTType *newASTTypeInt(Token iden);
ASTType *newASTTypeFloat(Token iden);
ASTType *newASTTypeString(Token iden);
ASTType *newASTTypeBool(Token iden);
ASTType *newASTTypeVariadic(Token startTok, ASTType *base);
ASTType *newASTTypeScoped(ASTExpr *expr);
ASTType *newASTTypeTuple(Token startTok, ASTTupleMembLL *membs);

ASTType *newASTTypeInfer(Token startTok);


ASTType *newASTTypeArray(Token startTok, ASTExpr *sizeExpr, ASTType *elemType, bool isSlice);
ASTType *newASTTypePointer(ASTType *pointedType);
ASTType *newASTTypeFuncPointer(ASTFuncSig *sig);

ASTTypeLL *newASTTypeLL(ASTType *item);
void addASTTypeLL(ASTTypeLL **ll, ASTType *item);

ASTTupleMemb *newASTTupleMemb(Token startTok, Token iden, ASTType *type, bool hasIden);
ASTTupleMembLL *newASTTupleMembLL(ASTTupleMemb *item);
void addASTTupleMembLL(ASTTupleMembLL **ll, ASTTupleMemb *item);

ASTBlock *newASTBlock(Token startTok, ASTStmtLL *stmts);


ASTStmt *allocASTStmt(ASTStmtKind kind, Token startTok);
ASTStmt *newASTStmtDecl(ASTDecl *decl);
ASTStmt *newASTStmtIf(Token startTok, Token endTok, ASTExpr *cond, ASTBlock *block, ASTBlock *elseBlock);
ASTStmt *newASTStmtWhile(ASTExpr *expr, ASTBlock *block);
ASTStmt *newASTStmtFor(ASTExpr *l, ASTExpr *r, ASTBlock *block);
ASTStmt *newASTStmtRet(Token startTok, ASTExpr *expr);
ASTStmt *newASTStmtCont(Token startTok);
ASTStmt *newASTStmtBreak(Token startTok);
ASTStmt *newASTStmtDefer(ASTBlock *block);
ASTStmt *newASTStmtUsingNormal(Token startTok, ASTExpr *expr);
ASTStmt *newASTStmtUsingImport(Token startTok, ASTDecl *import);
ASTStmt *newASTStmtMatch(ASTExprLL *matchExprLL, ASTMatchArmLL *matchArmLL, Token startTok);
ASTStmt *newASTStmtAssign(ASTExpr *l, ASTExpr *r);
ASTStmt *newASTStmtExpr(ASTExpr *expr);
ASTStmt *newASTStmtBlock(ASTBlock *block);

ASTStmtLL *newASTStmtLL(ASTStmt *item);
void addASTStmtlLL(ASTStmtLL **ll, ASTStmt *item);
void insertBeforeASTStmtLL(ASTStmtLL *before, ASTStmtLL *toInsert);
void insertAfterASTStmtLL(ASTStmtLL *before, ASTStmtLL *toInsert);
ASTStmtLL *getASTStmtLLAt(ASTStmtLL *ll, size_t index);

ASTMatchArm *newASTMatchArm(Token startTok, ASTExprLL **armExprs, size_t armExprsCount, ASTBlock *block);

ASTMatchArmLL *newASTMatchArmLL(ASTMatchArm *item);
void addASTMatchArmlLL(ASTMatchArmLL **ll, ASTMatchArm *item);


ASTDecl *allocASTDecl(ASTDeclKind kind, Token startTok);
ASTDecl *newASTDeclVar(ASTExpr *idenExpr, ASTType *type, ASTExpr *initial);
ASTDecl *newASTDeclImmut(ASTExpr *idenExpr, ASTType *type, ASTExpr *initial);
ASTDecl *newASTDeclConst(Token name, ASTType *type, ASTExpr *initial);
ASTDecl *newASTDeclType(Token name, ASTType *type);
ASTDecl *newASTDeclFunc(Token iden, ASTGenericTypeLL *genParams, ASTFuncSig *sig, ASTBlock *block, bool mustReturn);
ASTDecl *newASTDeclOperFunc(Token op, ASTFuncSig *sig, ASTBlock *block);
ASTDecl *newASTDeclStruct(Token iden, ASTGenericTypeLL *genParams, ASTDeclLL *membs);
ASTDecl *newASTDeclUnion(Token iden, ASTGenericTypeLL *genParams, ASTDeclLL *membs);
ASTDecl *newASTDeclUnionTagged(Token iden, ASTType *tagName, ASTGenericTypeLL *genParams, ASTDeclLL *membs);
ASTDecl *newASTDeclEnum(Token iden, ASTEnumMembLL *membs);
ASTDecl *newASTDeclImport(Token iden, bool hasIden, ASTNamespaceName *namespace);
ASTDecl *newASTDeclDeclList(ASTDeclLL *declLL, Token startTok);
ASTDecl *newASTDeclMethodBlock(Token startTok, ASTType *type, ASTDeclLL *declLL);

ASTDecl *deepCopyASTDecl(ASTDecl *decl);
ASTBlock *deepCopyASTBlock(ASTBlock *block);
ASTStmtLL *deepCopyASTStmtLL(ASTStmtLL *stmtLL);
ASTStmt *deepCopyASTStmt(ASTStmt *stmt);
ASTFuncSig *deepCopyASTFuncSig(ASTFuncSig *sig);
ASTFuncParamLL *deepCopyASTFuncParamLL(ASTFuncParamLL *paramLL);
ASTFuncParam *deepCopyASTFuncParam(ASTFuncParam *param);
ASTExpr *deepCopyASTExpr(ASTExpr *expr);
ASTType *deepCopyASTType(ASTType *type);

ASTDeclLL *newASTDeclLL(ASTDecl *item);
void addASTDeclLL(ASTDeclLL **ll, ASTDecl *item);
void insertBeforeASTDeclLL(ASTDeclLL *before, ASTDeclLL *toInsert);
void insertAfterASTDeclLL(ASTDeclLL *before, ASTDeclLL *toInsert);
bool ASTDeclLLOccursBefore(ASTDeclLL *ll, ASTDeclLL *occursBefore);

ASTTag *newASTTag(Token name, ASTTagParamLL *params);

ASTTagLL *newASTTagLL(ASTTag *item);
void addASTTagLL(ASTTagLL **ll, ASTTag *item);
void insertBeforeASTTagLL(ASTTagLL *before, ASTTagLL *toInsert);
void insertAfterASTTagLL(ASTTagLL *before, ASTTagLL *toInsert);

ASTTagParamLL *newASTTagParamLL(Token item);
void addASTTagParamLL(ASTTagParamLL **ll, Token item);

TokenLL *newTokenLL(Token item);
void addTokenLL(TokenLL **ll, Token item);

ASTEnumMemb *newASTEnumMemb(Token name, ASTExpr *val);
ASTEnumMembLL *newASTEnumMembLL(ASTEnumMemb *item);
void addASTEnumMembLL(ASTEnumMembLL **ll, ASTEnumMemb *item);


ASTCompilerTag *newASTCompilerTag(Token startTok, Token endTok, Token name, ASTTagParamLL *params);

ASTCompilerTagLL *newASTCompilerTagLL(ASTCompilerTag *item);
void addASTCompilerTagLL(ASTCompilerTagLL **ll, ASTCompilerTag *item);

ASTNamespaceWithBlock *newASTNamespaceWithBlock(Token startTok, Token endTok, ASTNamespaceWithStmtLL *stmts);
ASTNamespaceWithStmt *newASTNamespaceWithStmt(Token iden, ASTNamespaceWithValueLL *vals);
ASTNamespaceWithValue *newASTNamespaceWithValue(Token val);

ASTNamespaceWithStmtLL *newASTNamespaceWithStmtLL(ASTNamespaceWithStmt *item);
void addASTNamespaceWithStmtLL(ASTNamespaceWithStmtLL **ll, ASTNamespaceWithStmt *item);

ASTNamespaceWithValueLL *newASTNamespaceWithValueLL(ASTNamespaceWithValue *item);
void addASTNamespaceWithValueLL(ASTNamespaceWithValueLL **ll, ASTNamespaceWithValue *item);

ASTProg *newASTProg(ASTNamespaceName *namespace, ASTNamespaceWithBlock *withBlock, bool isNamespaced, ASTCompilerTagLL *tags, ASTDeclLL *decls);
ASTProgLL *newASTProgLL(ASTProg *item);
void addASTProgLL(ASTProgLL **ll, ASTProg *item);

ASTNamespaceName *newASTNamespaceName(Token parent, ASTNamespaceName *scopedChild);

bool isExprDiscardIdentifier(ASTExpr *expr);
#endif