#ifndef COMP_INTERNAL_H
#define COMP_INTERNAL_H

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <windows.h>
#include "lexer.h"
#include "namespacetable.h"
#include "platform.h"
#include "mylib.h"

#define PRINT_FORE_RED  "\033[31m"
#define PRINT_FORE_BLUE  "\033[38;2;156;220;254m"
#define PRINT_FORE_ORANGE_1  "\033[38;2;255;127;80m"
#define PRINT_FORE_GREEN_1  "\033[38;2;156;254;220m"

#define PRINT_RESET  "\033[0m"

#define MODULES_FOLDER_NAME "..\\modules\\"
#define STRCMP_PRELUDE_FUNC_NAME (OPERFUNC_EQ_OP_NAME "_prelude_s_string_prelude_s_string_")
#define NSTRCMP_PRELUDE_FUNC_NAME (OPERFUNC_NEQ_OP_NAME "_prelude_s_string_prelude_s_string_")
#define PRELUDE_EQ_FUNC_NAME ("prelude_equals")
//whats being parsed right now
typedef enum ParsingKind
{
    PC_DECL,
    PC_STMT,
}ParsingKind;

typedef struct ParserContext
{
    FILE *currFile;
    char currFileName[_MAX_PATH];
    ParsingKind kind;
    struct SymTable *currSymTble;
    
    size_t numErrors;
}ParserContext;

typedef struct CheckerContext
{
    struct ASTStmtLL *currStmtBeingChecked;
    struct ASTMatchArm *currMatchArmBeingChecked;
    struct DeferStackEntryLL *deferStack;
    size_t blockLevel;
    struct
    {
        bool isCheckingScopeExpr;
        Token scopeNameBeingChecked;
    }currScopedExpr;
    bool isCheckingStmtExpr;
    bool isIncompletePass;
    bool isCheckingAssignStmtLHSTuple;
    struct SymTable *checkingLocalsSymTble;
    bool checkingBreakableBlock;
    bool checkingContinuableBlock;
    bool checkingDefer;
    bool isCheckingUsingStmt;
    
    struct CheckerType *currFuncTypeBeingChecked;
    struct CheckerType *currFuncCallTypeBeingChecked;
    
    struct CheckerTypeLL *tupleTypes;

    //used for when generating defer blocks
    size_t contBreakBlockLevelID;

    struct SymEntry *stdEqualsFunction; //the equals function in std::mem

    size_t numErrors;
}CheckerContext;

typedef enum BackendKind
{
    BACKEND_LLVM,
    BACKEND_C,
}BackendKind;

typedef struct GenCodeContext
{
    struct ASTDecl *currFuncBeingGenerated;
    FILE *outputFile;
    FILE *headerFile;
    FILE *typeInfoFile;

    bool generatingTypeMethod;
    CheckerType *typeWhichTypeMethodIsFor;

    BackendKind backend;

    LLVMBasicBlockRef blockToContinueTo; //when continue appears, what block should it jump to
    LLVMBasicBlockRef blockToBreakTo;
    HashMapString *stringHashMap;

    char **linkerArgs;
    char numLinkerArgs;
    char* outputName;
}GenCodeContext;

typedef struct GlobalContext
{
    char **sourceFiles;
    size_t numSourceFiles;

    char *compilerExeFolderPath;

    char *stdLibPath;
    char *preludePath;

    char *configPath;
    
    struct Config *config;
    
    char *clPath;
    char *linkerPath;

    char **linkerLibs;
    size_t numLinkerLibs;

    struct CheckerType *globalMainFuncType;
    struct
    {
        unsigned int IS_VOID_RET : 1;
        unsigned int IS_VOID_PARAMS : 1;
    }globalMainFuncTypeFlags;
    
    struct SymTable *symTable;
    struct TypeMethodTable *typeMethodTable;
    struct TypeTable *typeTable;
    struct NamespaceTable *namespaceTable;
    struct NSTEntry *preludeNSEntry;
    struct SymTable *genericsSymTbl;

    struct ParserContext pc;
    struct CheckerContext cc;
    struct GenCodeContext gc;
    
    bool useLLVMBackend;
}GlobalContext;

typedef enum CmdLineArgKind
{
    CMDLINE_STRING,
    CMDLINE_FLAG,
    CMDLINE_NUM,
}CmdLineArgKind;

typedef struct CmdLineArg
{
    char *help;
    CmdLineArgKind kind;
    union 
    {
        bool *flag;
        int *num;
        char **str;
    };
}CmdLineArg;

GenCodeContext newGenCodeContext();
GlobalContext newGlobalContext(void);
void globalContextSetSourceFilesAndPaths(GlobalContext *globCtx, char *compilerExeFolderPath, char **sourceFiles, int numSourceFiles);
void addLinkerLibToGlobalContext(GlobalContext *ctxt, char *lib);
void addLinkerLibToImport(NSTEntry *nsEntry, char *lib);
bool shouldUseLinkerLibsOfImport(NSTEntry *nsEntry);
void addLinkerLibsFromNamespaceTableToGlobalContext(struct NamespaceTable *nsTable, GlobalContext *ctxt);

void initTerminalForFancyPrinting();

void setFileName(char currName[], char newName[]);
//returns allocated director path,
// true or false, depending on wether the last character is a backslash or a forward slash
bool fileNameContainsExtention(char *fileName);

int getCompilerExeFullPath(char *out, int outCap);
void getCompilerExeFolder(char *out, int outCap);
char  *allocGetFileFolder(char *path);
int getAllFilesFromDirectory(char *path, char ***outList, int numFiles);
void pathToWindowsPath(char *path);

size_t getRealLengthOfCompilerStr(char *str);
char *convertFilePathToCFriendly(char *filePath);
//accepts hex numbers
int64_t stringToInteger(char *str);

//mopdieifies string
char *stringToLower(char *str);

int hexDigitToInteger(int ch);
int binDigitToInteger(int ch);

char *sourceAsString(Token startTok, Token endToken);
char *allocNamespaceNameWithoutUnderscores(struct NSTEntry *entry);
char *allocNamespaceNameFromASTWithoutUnderscores(struct ASTNamespaceName *name);
char *allocGetNamespaceName(struct ASTNamespaceName *namespace);

bool doesFileExist(char *path);
bool doesDirExist(char *path);

void getDirectoryFromFilename(char *path, char *out);
#endif