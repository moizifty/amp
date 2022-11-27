#include "parser.h"
#include "checker.h"
#include "ast.h"
#include "types.h"
#include "symtable.h"
#include "namespacetable.h"
#include "compiler_internal.h"
#include "configfile.h"

GenCodeContext newGenCodeContext()
{
    return (GenCodeContext) 
    {
        .currFuncBeingGenerated = NULL,
        .generatingTypeMethod = false,
        .typeWhichTypeMethodIsFor = NULL,
        .backend = BACKEND_LLVM,
        .stringHashMap = NewHashMapString(128),
        .outputName = "out",
    };
}

GlobalContext newGlobalContext(void)
{
    return (GlobalContext)
    {
        .globalMainFuncType = NULL,
        .globalMainFuncTypeFlags = {0},
        .symTable = newSymTable(NULL),
        .genericsSymTbl = newSymTable(NULL),
        .typeMethodTable = newTypeMethodTable(),
        .typeTable = newTypeTable(),
        .namespaceTable = newNamespaceTable(),
        .preludeNSEntry = NULL,
        .pc = newParserContext(),
        .cc = newCheckerContext(),
        .gc = newGenCodeContext(),
        .useLLVMBackend = false,
    };
}

void globalContextSetSourceFilesAndPaths(GlobalContext *globCtx, char *compilerExeFolderPath, char **sourceFiles, int numSourceFiles)
{
    int pathLen = strlen(compilerExeFolderPath);
    char *stdLibPath = malloc((pathLen + strlen(MODULES_FOLDER_NAME)) + 1);
    char *preludePath = NULL;

    strcpy(stdLibPath, compilerExeFolderPath);
    strcat(stdLibPath, MODULES_FOLDER_NAME);

    char **source = malloc(numSourceFiles * sizeof(char *));
    memcpy(source, sourceFiles, sizeof(char *) * numSourceFiles);

    int numFilesInStdLibPath = getAllFilesFromDirectory(stdLibPath, &source, numSourceFiles);

    for(int i = 0; i < numFilesInStdLibPath + numSourceFiles; i++)
    {
        char *path = source[i];

        { //set prelude path
            char buf[1000];
            strcpy(buf, stdLibPath);
            strcat(buf, "\\prelude.amp");

            if(!strcmp(buf, path))
            {
                preludePath = source[i];
                source[i] = NULL;
            }
        }
    }

    if(preludePath == NULL)
    {
        fprintf(stderr, "Prelude file not found\n");
    }

    globCtx->sourceFiles = source;
    globCtx->numSourceFiles = numFilesInStdLibPath + numSourceFiles;
    globCtx->stdLibPath = stdLibPath;
    globCtx->preludePath = preludePath;

    {//set cl compiler path
        char path[MAX_PATH] = {0};
        sprintf(path, "%s\\VC\\Tools\\MSVC\\%s\\bin\\Hostx64\\x64\\cl.exe", globCtx->config->vsFolder, globCtx->config->bestVS.name);

        globCtx->clPath = strdup(path);
    }

    {//set linker  path
        char path[MAX_PATH] = {0};
        sprintf(path, "%s\\VC\\Tools\\MSVC\\%s\\bin\\Hostx64\\x64\\link.exe", globCtx->config->vsFolder, globCtx->config->bestVS.name);

        globCtx->linkerPath = strdup(path);
    }
}

void addLinkerLibToGlobalContext(GlobalContext *ctxt, char *lib)
{
    if(ctxt->numLinkerLibs == 0) ctxt->linkerLibs = malloc(sizeof(char *));
    else
    {
        ctxt->linkerLibs = realloc(ctxt->linkerLibs, (ctxt->numLinkerLibs + 1) * sizeof(char *));
    }

    ctxt->linkerLibs[ctxt->numLinkerLibs] = strdup(lib);
    ctxt->numLinkerLibs++;
}

void addLinkerLibToImport(NSTEntry *nsEntry, char *lib)
{   
    size_t lenOfLibStr = strlen(lib);
    bool hasExtention = false;
    { //check if lib has extention
        char *endPtr = lib + lenOfLibStr - 1;

        while(endPtr != lib)
        {
            if(*endPtr == '.')
            {
                break;
            }

            endPtr--;
        }

        if(*endPtr == '.') hasExtention = true;
    }

    char *strToAdd = malloc(lenOfLibStr + 1 + ((hasExtention) ? 0 : 4));
    
    sprintf(strToAdd, "%s", lib);

    if(!hasExtention) strcat(strToAdd, ".lib");

    if(nsEntry->numLinkerLibs == 0) nsEntry->linkerLibs = malloc(sizeof(char *));
    else
    {
        nsEntry->linkerLibs = realloc(nsEntry->linkerLibs, (nsEntry->numLinkerLibs  + 1) * sizeof(char *));
    }

    nsEntry->linkerLibs[nsEntry->numLinkerLibs ] = strToAdd;
    nsEntry->numLinkerLibs ++;
}

bool shouldUseLinkerLibsOfImport(NSTEntry *nsEntry)
{
    //check if the parent namesapces are used
    bool isImported = nsEntry->isImported;

    bool isAtLeast1NamespaceImportingThisNamespaceImported = false;
    NSTEntryLL *importedBy = nsEntry->importedBy;

    while(importedBy != NULL)
    {
        isAtLeast1NamespaceImportingThisNamespaceImported = isAtLeast1NamespaceImportingThisNamespaceImported | importedBy->item->isImported;
        importedBy = importedBy->prev;
    }

    return nsEntry->isOtherSymbolsUsed && isImported && isAtLeast1NamespaceImportingThisNamespaceImported;

    // if(!nsEntry->isUsed) return false;

    // if(nsEntry->__checkingImportLinkerLibs) return true;

    // nsEntry->__checkingImportLinkerLibs = true;
    // bool areImportsImportingThisBeingUsed = true;

    // NSTEntryLL *currNS = nsEntry->usedBy;
    // if(currNS != NULL)
    // {
    //     currNS = currNS->first;
    //     while(currNS != NULL)
    //     {
    //         areImportsImportingThisBeingUsed = areImportsImportingThisBeingUsed && shouldUseLinkerLibsOfImport(currNS->item);

    //         currNS = currNS->next;
    //     }
    // }

    // return areImportsImportingThisBeingUsed;
}

void addLinkerLibsFromNamespaceTableToGlobalContext(struct NamespaceTable *nsTable, GlobalContext *ctxt)
{
    NSTEntryLL *currNSE = nsTable->entries->first;

    while(currNSE != NULL)
    {
        bool isBeingUsed = shouldUseLinkerLibsOfImport(currNSE->item);

        if(isBeingUsed && (currNSE->item->numLinkerLibs > 0))
        {
            for(size_t i = 0; i < currNSE->item->numLinkerLibs; i++)
            {
                addLinkerLibToGlobalContext(ctxt, currNSE->item->linkerLibs[i]);
            }
        }

        currNSE = currNSE->next;
    }
}

void initTerminalForFancyPrinting()
{
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);
    DWORD consoleMode;
    GetConsoleMode(hConsole, &consoleMode);
    consoleMode |= 0x0004; //enable_virtual_terminal_processing

    SetConsoleMode(hConsole, consoleMode);
}

void setFileName(char currName[], char newName[])
{
    _fullpath(currName, newName, _MAX_PATH);
}

bool fileNameContainsExtention(char *fileName)
{
    char *endPtr = fileName;
    size_t len = 0;

    while(*endPtr != '\0') 
    {
        len++;
        endPtr++;
    }

    if(len < 4) return false;

    endPtr = endPtr - 4;
    
    return !strcmp(endPtr, ".amp");
}

int getCompilerExeFullPath(char *out, int outCap)
{
    return GetModuleFileNameA(NULL, out, outCap);
}

void pathToWindowsPath(char *path)
{
    char *p = path;
    while(*p != '\0')
    {
        switch(*p)
        {
            case '\\':
            {
                *p = '/';
            }break;
        }
        p++;
    }
}

void getCompilerExeFolder(char *out, int outCap)
{
    int pathLen = GetModuleFileNameA(NULL, out, outCap);

    for(int i = pathLen; i >= 0; i--)
    {
        switch(out[i])
        {
            case '\\':
            case '/':
            {
                out[i + 1] = '\0';
                return;
            }
        }
    }
}
char *allocGetFileFolder(char *path)
{
    size_t pathLen = strlen(path);

    for(int i = pathLen; i >= 0; i--)
    {
        switch(path[i])
        {
            case '\\':
            case '/':
            {
                char *ret = malloc(i + 1);
                strncpy(ret, path, i);
                ret[i] = '\0';

                return ret;
            }
        }
    }

    return path;
}

int getAllFilesFromDirectory(char *path, char ***outList, int numFiles)
{
    WIN32_FIND_DATAA fdFile;
    HANDLE hFind = NULL;

    char searchPath[2048];
    int numFilesFound = 0;

    sprintf(searchPath, "%s\\*", path);

    if((hFind = FindFirstFileA(searchPath, &fdFile)) == INVALID_HANDLE_VALUE)
    {
        printf("Path not found: [%s]\n", path);
        return 0;
    }

    do
    {
        //Find first file will always return "."
        //    and ".." as the first two directories.
        if( (strcmp(fdFile.cFileName, ".") != 0) && 
            (strcmp(fdFile.cFileName, "..") != 0))
        {
            //Build up our file path using the passed in
            //  [sDir] and the file/foldername we just found:
            sprintf(searchPath, "%s\\%s", path, fdFile.cFileName);
            size_t filePathLen = strlen(searchPath);

            //Is the entity a File or Folder?
            if(fdFile.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) numFilesFound += getAllFilesFromDirectory(searchPath, outList, numFiles + numFilesFound);
            else if(!strcmp(&(searchPath[filePathLen - 4]), ".amp"))
            {
                numFilesFound += 1;
                *outList = realloc(*outList, (numFiles + numFilesFound) * sizeof(char *));
                (*outList)[(numFilesFound + numFiles) - 1] = malloc(filePathLen + 1);

                memcpy((*outList)[(numFilesFound + numFiles) - 1], searchPath, filePathLen + 1);
            }
        }
    }
    while(FindNextFileA(hFind, &fdFile)); //Find the next file.

    FindClose(hFind); //Always, Always, clean things up!

    return numFilesFound;
}

size_t getRealLengthOfCompilerStr(char *str)
{
    size_t len = 0;

    char *chPtr = str;

    while(*chPtr != '\0')
    {
        if(*chPtr == '\\')
        {
            chPtr++;
        }

        chPtr++;
        len++;
    }

    return len;
}

int64_t stringToInteger(char *str)
{
    if((str[0] == '0') && (str[1] == 'x'))
    {
        //hex number

        int64_t num = 0;
        char *numStr = str + 2;

        while(*numStr != '\0')
        {
            num = (num * 16) + hexDigitToInteger(*numStr);

            numStr++;
        }

        return num;
    }
    else if((str[0] == '0') && (str[1] == 'b'))
    {
        //binary number
        int64_t num = 0;
        char *numStr = str + 2;

        while(*numStr != '\0')
        {
            num = (num * 2) + binDigitToInteger(*numStr);

            numStr++;
        }

        return num;
    }
    else return atoll(str);
}

char *stringToLower(char *str)
{
    char *s = str;
    for(; *str != '\0'; str++)
    {
        *str = tolower(*str);
    }

    return s;
}

int hexDigitToInteger(int ch)
{
    if((ch >= '0') && (ch <= '9'))
    {
        return ch - '0';
    }
    else if((ch >= 'a') && (ch <= 'f'))
    {
        return 10 + (ch - 'a');
    }
    else if((ch >= 'A') && (ch <= 'Z'))
    {
        return 10 + (ch - 'A');
    }
    else
    {
        fprintf(stderr, "Unexpected hex character: %c\n", ch);
    }

    return 0;
}

int binDigitToInteger(int ch)
{
    switch(ch)
    {
        case '0': return 0;
        case '1': return 1;
    }

    return -1;
}

char *convertFilePathToCFriendly(char *filePath)
{
    char *ch = filePath;
    while(*ch != '\0')
    {
        if(*ch == '\\') *ch = '/';

        ch++;
    }

    return filePath;
}

char *sourceAsString(Token startTok, Token endToken)
{
    int *startString;
    size_t len = 0;

    startString = startTok.pos.fileBufPos;

    if(startString == NULL) return NULL;

    len = (endToken.pos.fileBufPos) - startString;

    if(len == 0) len = strlen(startTok.lexeme);

    char buf[2048] = {0};
    for(size_t i = 0, writeOffset = 0; i < len; i++)
    {
        char ch = startString[i];
        if(ch == '\"')
        {
            buf[i + writeOffset] = '\\';
            writeOffset += 1;
        }
        if(ch == '\n') continue;

        buf[i + writeOffset] = ch;
    }

    size_t bufStrLen = strlen(buf);
    char *s = malloc(bufStrLen + 1);
    strncpy(s, buf, bufStrLen);

    return s;
}

//converts underscores to ::. std_io becomes std::io
char *allocNamespaceNameWithoutUnderscores(NSTEntry *entry)
{
    char *name = NULL;

    if(!entry->isNestedNamespace) 
    {
        name = malloc(strlen(entry->namespace) + 1);
        strcpy(name, entry->namespace);
    }
    else
    {
        char buf[1000] = {0};
        
        size_t numEntries = 0;
        NSTEntry *currEntry = entry;
        
        while(currEntry->isNestedNamespace)
        {
            char tempBuf[256] = {0};
            strcpy(tempBuf, buf);

            if(entry == currEntry) sprintf(buf, "%s", currEntry->withoutScopeName);
            else sprintf(buf, "%s::%s", currEntry->withoutScopeName, tempBuf);

            currEntry = currEntry->ifNestedParentEntry;
        }

        { //add root namespace
            char tempBuf[256] = {0};
            strcpy(tempBuf, buf);

            sprintf(buf, "%s::%s", currEntry->namespace, tempBuf);
        }

        name = malloc(strlen(buf) + 1);
        strcpy(name, buf);
    }

    return name;
}
char *allocNamespaceNameFromASTWithoutUnderscores(ASTNamespaceName *name)
{
    char buf[2046] = {0};

    strcpy(buf, name->parent.lexeme);

    ASTNamespaceName *next = name->scopedChild;
    while(next != NULL)
    {
        strcat(buf, "::");
        strcat(buf, next->parent.lexeme);

        next = next->scopedChild;
    }
    
    char *n = malloc(strlen(buf) + 1);
    strcpy(n, buf);

    return n;
}

char *allocGetNamespaceName(ASTNamespaceName *namespace)
{
    char *name = NULL;

    if(namespace->scopedChild == NULL)
    {
        name = malloc(strlen(namespace->parent.lexeme) + 1);
        strcpy(name, namespace->parent.lexeme);
    }
    else
    {
        int nameLen = 0;
        ASTNamespaceName *right = namespace;
        char buf[BUFSIZ] = {0};

        while(right != NULL)
        {
            nameLen += strlen(right->parent.lexeme);

            nameLen += 1; //for the underscore seperating them.

            strcat(buf, right->parent.lexeme);

            right = right->scopedChild;
            
            if(right != NULL) strcat(buf, "_");
        }

        name = malloc(nameLen + 1);
        strcpy(name, buf);
    }
    return name;
}

bool doesFileExist(char *path)
{
  DWORD dwAttrib = GetFileAttributesA(path);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         !(dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

bool doesDirExist(char *path)
{
  DWORD dwAttrib = GetFileAttributesA(path);

  return (dwAttrib != INVALID_FILE_ATTRIBUTES && 
         (dwAttrib & FILE_ATTRIBUTE_DIRECTORY));
}

void getDirectoryFromFilename(char *path, char *out)
{
    size_t i = strlen(path);

    while((path - i) != path)
    {
        char ch = *(path + i);

        if(ch == '\\')
        {
            break;
        }
        else if(ch == '/')
        {
            break;
        }

        i -= 1;
    }

    strncpy(out, path, i);
    out[i + 1] = '\0';
}