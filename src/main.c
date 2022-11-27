#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <windows.h>

#include "lexer.h"
#include "parser.h"
#include "checker.h"
#include "codegen-llvm.h"
#include "namespacetable.h"
#include "passes.h"
#include "compiler_internal.h"
#include "configfile.h"
#include <llvm-c/DataTypes.h>

#define PARSE_CMDLINE_MSG_PREFIX_ERROR  "Command Line Arguement Error"
#define PARSE_CMDLINE_MSG_PREFIX_USAGE  "Usage"

#define CMDLINE_BACKEND_ID    (0)
#define CMDLINE_LINKLIB_ID    (1)
#define CMDLINE_OUT_ID    (2)
#define CMDLINE_CREATE_OUT_DIR_ID    (2)

extern LexFileBuffer fileBuffer;

Token tok;
GlobalContext globalContext;

typedef struct SourceFilesList
{
    char **files;
    size_t allocated;
    size_t count;
}SourceFilesList;
//returns sources files to compile
void parseCmdLineError(char *prefix, char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    fprintf(stderr, PRINT_FORE_RED "%s" PRINT_RESET ": ", prefix);
    vfprintf(stderr, fmt, args);

    fprintf(stderr, "\n");
    va_end(args);
}
SourceFilesList *parseCmdLineArgs(int argc, char **argv, CmdLineArg *possibleCmdLineArgs)
{
    size_t numAllocated = argc * 2;
    char **sourceFilesToCompile = calloc(numAllocated, sizeof(char *));
    size_t currNumSourceFilesToCompile = 0;

    for(int i = 1; i < argc; i++)
    {
        char *currArg = argv[i];
        char *nextArg = argv[i + 1];

        if(!strcmp(currArg, "-backend"))
        {
            CmdLineArg c = possibleCmdLineArgs[CMDLINE_BACKEND_ID];

            if(nextArg != NULL)
            {
                if(!strcmp(stringToLower(nextArg), "llvm"))
                {
                    globalContext.gc.backend = BACKEND_LLVM;
                }
                else if(!strcmp(stringToLower(nextArg), "c"))
                {
                    globalContext.gc.backend = BACKEND_C;
                }
                else
                {
                    parseCmdLineError(PARSE_CMDLINE_MSG_PREFIX_ERROR, "For argument '%s', the value '%s' is not valid", currArg, nextArg);
                    parseCmdLineError(PARSE_CMDLINE_MSG_PREFIX_USAGE, c.help);
                }

                i += 1;
                continue;
            }
            else 
            {
                parseCmdLineError(PARSE_CMDLINE_MSG_PREFIX_ERROR, "Expected value for argument '%s'", currArg);
            }
        }
        else if(!strcmp(currArg, "-lib"))
        {
            CmdLineArg c = possibleCmdLineArgs[CMDLINE_LINKLIB_ID];

            if(nextArg != NULL)
            {
                addLinkerLibToGlobalContext(&globalContext, nextArg);

                i += 1;
                continue;
            }
            else 
            {
                parseCmdLineError(PARSE_CMDLINE_MSG_PREFIX_ERROR, "Expected value for argument '%s'", currArg);
            }
        }
        else if(!strcmp(currArg, "-out"))
        {
            CmdLineArg c = possibleCmdLineArgs[CMDLINE_OUT_ID];

            if(nextArg != NULL)
            {
                size_t pathLen = strlen(nextArg);
                char *path = nextArg;
                if(*path == '\"')
                {
                    while(*path == '\"')
                    {
                        path++;
                        pathLen -= 1;
                    }

                    while(path[pathLen - 1] == '\"')
                    {
                        path[pathLen - 1] = '\0';
                        pathLen -= 1;
                    }

                }

                globalContext.gc.outputName = malloc(pathLen + 1);
                strcpy(globalContext.gc.outputName, path);

                i += 1;
                continue;
            }
            else 
            {
                parseCmdLineError(PARSE_CMDLINE_MSG_PREFIX_ERROR, "Expected value for argument '%s'", currArg);
            }
        }
        else if(!strcmp(currArg, "-createOutDir"))
        {
            CmdLineArg c = possibleCmdLineArgs[CMDLINE_OUT_ID];

            char dir[MAX_PATH];
            getDirectoryFromFilename(globalContext.gc.outputName, dir);

            if(!doesDirExist(dir))
            {
                CreateDirectoryA(dir, NULL);
            }
        }
        else
        {
            if(currNumSourceFilesToCompile >= numAllocated)
            {
                sourceFilesToCompile = realloc(sourceFilesToCompile, (currNumSourceFilesToCompile + 5) * sizeof(char *));
            }

            sourceFilesToCompile[currNumSourceFilesToCompile++] = currArg;
        }
    }

    SourceFilesList *ret = malloc(sizeof(SourceFilesList));
    ret->allocated = numAllocated;
    ret->count = currNumSourceFilesToCompile;
    ret->files = sourceFilesToCompile;

    return ret;
}

int runLinker(char **args)
{
    char cmd[2046] = {0};

    sprintf(cmd, "\"\"%s\"", globalContext.linkerPath);

    for(size_t i = 0; args[i] != NULL; i++)
    {
        pathToWindowsPath(args[i]);
        
        if(cmd[strlen(cmd) - 1] != ':')
        {
            strcat(cmd, " ");
        }
        strcat(cmd, args[i]);
    }

    strcat(cmd, "\"");
    
    printf("linker cmd: %s\n", cmd);
    {   //set LIB environment variable
        char libVar[2046] = {0};

        {
            sprintf(libVar, "%s\\VC\\Tools\\MSVC\\%s\\ATLMFC\\lib\\x64;", globalContext.config->vsFolder, globalContext.config->bestVS.name);
        }

        {
            char buf[MAX_PATH + 1] = {0};
            sprintf(buf, "%s\\VC\\Tools\\MSVC\\%s\\lib\\x64;", globalContext.config->vsFolder, globalContext.config->bestVS.name);

            strcat(libVar, buf);
        }

        {
            char buf[MAX_PATH + 1] = {0};
            sprintf(buf, "%s\\lib\\%s\\ucrt\\x64;", globalContext.config->winKitFolder, globalContext.config->bestWinSDK.name);

            strcat(libVar, buf);
        }

        {
            char buf[MAX_PATH + 1] = {0};
            sprintf(buf, "%s\\lib\\%s\\um\\x64;", globalContext.config->winKitFolder, globalContext.config->bestWinSDK.name);

            strcat(libVar, buf);
        }

        SetEnvironmentVariableA("LIB", libVar);
    }

    return system(cmd);
}

int main(int argc, char **argv)
{
    char compilerExeFolderPath[1000];
    getCompilerExeFolder(compilerExeFolderPath, 2046);

    initTerminalForFancyPrinting();

    globalContext = newGlobalContext();
    globalContext.compilerExeFolderPath = strdup(compilerExeFolderPath);

    CmdLineArg cmdLineArgs[] =
    {
        [CMDLINE_BACKEND_ID] = 
        {
            .help = "This setting specifies the backend used by the compiler, 'C' sets the compiler to emit C code, 'LLVM' set the compiler to emit LLVM IR, eg '-backend=llvm'",
        },

        [CMDLINE_LINKLIB_ID] = 
        {
            .help = "This setting specifies a library to link, eg '-lib kernel32",
        },
        
        [CMDLINE_OUT_ID] = 
        {
            .help = "This setting sets the name of the output files, eg '-out 'build/main' will produce build/main.exe'",
        },
        
        [CMDLINE_CREATE_OUT_DIR_ID] = 
        {
            .help = "This setting is a flag, which tells the compiler to create the output dir in the -out path if it does not exis, setting must be after -out",
        },

        //last one should be null
        {
            0,
        }
    };

    SourceFilesList *sfl = parseCmdLineArgs(argc, argv, cmdLineArgs);

    setConfigFile(globalContext.compilerExeFolderPath);

    globalContextSetSourceFilesAndPaths(&globalContext, compilerExeFolderPath, sfl->files, sfl->count);

    setupPrimitiveTypes();
    
    printf(PRINT_FORE_GREEN_1 "[Parsing]\n" PRINT_RESET);
    parseEntireProgram(globalContext.sourceFiles, globalContext.numSourceFiles);

    size_t numParserErrors = globalContext.pc.numErrors;
    printf("%sParsing finished with %lld error(s)\n" PRINT_RESET, (numParserErrors == 0) ? PRINT_FORE_GREEN_1 : PRINT_FORE_RED , globalContext.pc.numErrors);

    if(numParserErrors == 0)
    {
        printf(PRINT_FORE_GREEN_1 "[Semantic Check]\n" PRINT_RESET);
        checkEntireProgram();

        size_t numCheckerErrors = globalContext.cc.numErrors;
        printf("%sSemantic Check finished with %lld error(s)\n" PRINT_RESET, (numCheckerErrors == 0) ? PRINT_FORE_GREEN_1 : PRINT_FORE_RED , globalContext.cc.numErrors);
    }
    
    if((globalContext.pc.numErrors == 0) && (globalContext.cc.numErrors == 0))
    {
        printf(PRINT_FORE_GREEN_1 "[Code Gen]\n" PRINT_RESET);

        { //set global, prelude and runtime namespaces to being used
            NSTEntry *glob = namespaceTableLookupEntry(globalContext.namespaceTable, "global");
            glob->isImported = true;
            glob->isTypesUsed = true;
            glob->isOtherSymbolsUsed = true;
            if(glob->importedBy == NULL) glob->importedBy = newNSTEntryLL(glob);
            else addNSTEntryLL(&(glob->importedBy), glob);


            NSTEntry *pre = namespaceTableLookupEntry(globalContext.namespaceTable, "prelude");
            pre->isImported = true;
            pre->isTypesUsed = true;
            pre->isOtherSymbolsUsed = true;
            if(pre->importedBy == NULL) pre->importedBy = newNSTEntryLL(pre);
            else addNSTEntryLL(&(pre->importedBy), pre);

            NSTEntry *run = namespaceTableLookupEntry(globalContext.namespaceTable, "__runtime");
            run->isImported = true;
            run->isTypesUsed = true;
            run->isOtherSymbolsUsed = true;
            if(run->importedBy == NULL) run->importedBy = newNSTEntryLL(run);
            else addNSTEntryLL(&(run->importedBy), run);
        }

        cgLLVMEntireProgram(globalContext.typeTable, globalContext.namespaceTable);

        printf(PRINT_FORE_GREEN_1 "[Linking]\n" PRINT_RESET);

        bool linkerSucessful = false;
        { //run linker

            char *outputFileNameWithoutExt = globalContext.gc.outputName;
            
            char outputObjName[MAX_PATH];
            sprintf(outputObjName, "\"%s.obj\"", outputFileNameWithoutExt);

            char outputExeName[MAX_PATH];
            sprintf(outputExeName, "\"%s.exe\"", outputFileNameWithoutExt);

            char *defaultLinkerArgs[] = 
            {
                outputObjName,
                "/out:",
                outputExeName,
                "/nologo", 
                "/nodefaultlib",
                "libcmt.lib vcruntime.lib ucrt.lib legacy_stdio_definitions.lib legacy_stdio_wide_specifiers.lib", 
            };
            
            addLinkerLibsFromNamespaceTableToGlobalContext(globalContext.namespaceTable, &globalContext);
            
            size_t numDefaultLinkerArgs = sizeof(defaultLinkerArgs)/sizeof(defaultLinkerArgs[0]);
            size_t numLinkerArgs = (numDefaultLinkerArgs + globalContext.numLinkerLibs + 1);  //plus 1 for null at the end
            char **linkerArgs = malloc(numLinkerArgs * sizeof(char *));

            memcpy(linkerArgs, defaultLinkerArgs, sizeof(defaultLinkerArgs));

            for(size_t i = 0; i < globalContext.numLinkerLibs; i++)
            {
                linkerArgs[i + numDefaultLinkerArgs] = globalContext.linkerLibs[i];
            }

            linkerArgs[numLinkerArgs - 1] = NULL;

            linkerSucessful = runLinker(linkerArgs) == 0;

            free(linkerArgs);
            free(globalContext.linkerLibs);

        }

        if(linkerSucessful) printf(PRINT_FORE_GREEN_1 "Compiled Successfully\n" PRINT_RESET);
        else
        {
            printf(PRINT_FORE_RED "Linker failed\n" PRINT_RESET);
        }
    }

    free(sfl->files);
    free(sfl);

    return 0;
}