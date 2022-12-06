#include <windows.h>
#include "symtable.h"
#include "compiler_internal.h"
#include "parser.h"
#include "namespacetable.h"

extern Token tok;
extern int *fileBuffer;

extern GlobalContext globalContext;

jmp_buf pErrorDecl;

bool setErrorStmtJmp = false;
jmp_buf pErrorStmt;

ParserContext newParserContext()
{
    return (ParserContext){
        .currFile = NULL,
        .currFileName = {0},
        .currSymTble = NULL,
    };
}

void parserError(Token tok, char *msg, ...)
{
    globalContext.pc.numErrors += 1;

    va_list args;
    va_start(args, msg);

    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD savedAttributes;

    /* Save current attributes */
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    savedAttributes = consoleInfo.wAttributes;

    fprintf(stderr, "\033[1m\033[4m%s (%lld: %d)\033[0m", tok.pos.filename, tok.pos.linenum, tok.pos.colnum);

    fprintf(stderr, "\033[1m\n        --> Parser Error:\033[0m ");
    vfprintf(stderr, msg, args);
    fprintf(stderr, "\n        |\n");
    
    const int NUM_LINES = 3;
    
    size_t tokLen = strlen(tok.lexeme);
    int currLine = 0;
    int *currCh = tok.pos.fileBufPos;
    int colnum = 0;

    while( (currLine != -NUM_LINES) && (currCh != fileBuffer)) 
    {
        while((*currCh != '\n') && (currCh != fileBuffer)) currCh--;

        if(*currCh == '\n') 
        {
            currLine--;
            if(currLine != -NUM_LINES) currCh--;
        }
    }

    //if(currLine < -1) currLine++;

    if(*currCh == '\n') 
    {
        currLine++;
        currCh++;
    }

    for(; currLine < NUM_LINES; currLine++)
    {
        printf("%7lld | ", currLine + tok.pos.linenum);

        while((*currCh != '\n') && (*currCh != EOF))
        {
            if(currLine == 0) colnum++;

            if(currCh == tok.pos.fileBufPos) SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY | FOREGROUND_RED);
            else if(currCh >= tok.pos.fileBufPos + tokLen) SetConsoleTextAttribute(hConsole, savedAttributes);

            putchar((char) (*currCh++));
        }
        SetConsoleTextAttribute(hConsole, savedAttributes);
        if(currLine == 0)
        {
            putchar('\n');
            printf("        | ");
            for(int *c = currCh - colnum; c < tok.pos.fileBufPos; c++)
            {
                if(isspace(*c))
                    putchar((char)*c);
                else putchar(' ');

            }

            SetConsoleTextAttribute(hConsole, FOREGROUND_RED);

            for(int i = 0; i < tokLen; i++) putchar('^');

            SetConsoleTextAttribute(hConsole, savedAttributes);
        }
        
        if(*currCh != EOF) currCh++;
        else break;
        putchar('\n');
    }

    putchar('\n');
    
    va_end(args);

    if((globalContext.pc.kind == PC_DECL) || (setErrorStmtJmp == false))
        longjmp(pErrorDecl, 1);
    else if(globalContext.pc.kind == PC_STMT)
    {
        longjmp(pErrorStmt, 1);
    }
    //unrechable
}

bool findDeclSpecifierInTokenList(TokenLL *specs, TokType specToFind)
{
    TokenLL *curr = specs;
    while(curr != NULL)
    {
        if(curr->item.type == specToFind) return true;
        curr = curr->next;
    }

    return false;
}
void collectDecls(ASTProg *prog, ASTDeclLL *declLL)
{
    if(declLL == NULL) return;

    ASTDeclLL *currDecl = declLL->first;

    while(currDecl != NULL)
    {
        ASTDecl *d = currDecl->item;
        switch(d->kind)
        {
            case A_DECL_IMPORT: 
            {
                if(prog->globalImportDecls == NULL) prog->globalImportDecls = newASTDeclLL(d);
                else addASTDeclLL(&prog->globalImportDecls, d);

            }break;
            case A_DECL_TYPE: 
            {
                if(prog->globalTypeDecls == NULL) prog->globalTypeDecls = newASTDeclLL(d);
                else addASTDeclLL(&prog->globalTypeDecls, d);
                
            }break;
            case A_DECL_STRUCT: case A_DECL_UNION: case A_DECL_UNION_TAGGED:
            {
                if(prog->globalStructuredDecls == NULL) prog->globalStructuredDecls = newASTDeclLL(d);
                else addASTDeclLL(&prog->globalStructuredDecls, d);
                
            }break;
            case A_DECL_ENUM:
            {
                if(prog->globalEnumDecls == NULL) prog->globalEnumDecls = newASTDeclLL(d);
                else addASTDeclLL(&prog->globalEnumDecls, d);
                
            }break;
            case A_DECL_FUNC: case A_DECL_OPERATOR_FUNC:
            {
                if(prog->globalFuncDecls == NULL) prog->globalFuncDecls = newASTDeclLL(d);
                else addASTDeclLL(&prog->globalFuncDecls, d);
                
            }break;
            case A_DECL_VAR:
            {
                bool isConst = findDeclSpecifierInTokenList(d->var.specifiers, TOK_CONST_KW);
                bool isImmut = findDeclSpecifierInTokenList(d->var.specifiers, TOK_IMMUT_KW);

                if(isConst) 
                    d->var.specifierFlags |= A_DECL_VAR_SEPCIFIER_CONST;
                
                if(isImmut) 
                    d->var.specifierFlags |= A_DECL_VAR_SEPCIFIER_IMMUT;

                if(prog->globalVarOrConstDecls == NULL) prog->globalVarOrConstDecls = newASTDeclLL(d);
                else addASTDeclLL(&prog->globalVarOrConstDecls, d);
            }break;
            case A_DECL_METHOD_BLOCK:
            {
                if(prog->globalMethodBlockDecls == NULL) prog->globalMethodBlockDecls = newASTDeclLL(d);
                else addASTDeclLL(&prog->globalMethodBlockDecls, d);
            }break;
            case A_DECL_DECLLIST: collectDecls(prog, d->declList.declLL); break;
        }
        currDecl = currDecl->next;
    }
}

void parseEntireProgram(char **filesList, int fileCount)
{
    //do prelude before everything else, IMPORTANT !!
    {
        globalContext.pc.currFile = fopen(globalContext.preludePath, "r");
        setFileName(globalContext.pc.currFileName, globalContext.preludePath);

        bool success = initLexer(globalContext.pc.currFileName);
        
        tok = lex();

        ASTProg *p = prog(globalContext.symTable);
        
        NSTEntry *entry = p->myNSTEntry;
        ASTProgLL *entryProgs = entry->progASTs;

        if(entryProgs == NULL) entry->progASTs = newASTProgLL(p);
        else addASTProgLL(&entryProgs, p);

        globalContext.preludeNSEntry = entry;

        fclose(globalContext.pc.currFile);
    }
    
    namespaceTableInsertEntry(globalContext.namespaceTable, "global", NULL);

    for(int i = 0; i < fileCount; i++)
    {
        if(filesList[i] == NULL) continue; //prelude file entry is set to null as it is done seperatly

        globalContext.pc.currFile = fopen(filesList[i], "r");
        setFileName(globalContext.pc.currFileName, filesList[i]);

        bool success = initLexer(globalContext.pc.currFileName);
        if(!success)
        {
            fprintf(stderr, "File '%s' cannot be opened or found\n", globalContext.pc.currFileName);
            continue;
        }

        tok = lex();

        ASTProg *p = prog(globalContext.symTable);
        
        NSTEntry *entry = p->myNSTEntry;
        ASTProgLL *entryProgs = entry->progASTs;

        if(entryProgs == NULL) entry->progASTs = newASTProgLL(p);
        else addASTProgLL(&entryProgs, p);

        fclose(globalContext.pc.currFile);
    }
}

ASTProg *prog(SymTable *tbl)
{
    ASTProg *p = NULL;
    ASTCompilerTagLL *tags = NULL;
    ASTDeclLL *decls = NULL;
    bool isNamespaced = false;
    ASTNamespaceName *namespace;
    ASTNamespaceWithBlock *withBlock = NULL;
    
    tags = progCompilerTagList();
    
    if(tok.type == TOK_NAMESPACE_KW) 
    {
        isNamespaced = true;
        tok = lex();
        namespace = namespaceName(false);

        if(tok.type == TOK_WITH_KW)
        {
            tok = lex();

            withBlock = namespaceWithBlock();
        }
    }
    else namespace = newASTNamespaceName((Token){.lexeme = "global", .type = TOK_IDEN}, NULL);

    NSTEntry *preNSEntry = NULL;
    ASTNamespaceName *right = namespace;
    char buf[BUFSIZ] = {0};

    while(right != NULL)
    {
        strcat(buf, right->parent.lexeme);

        NSTEntry *nsEntry = namespaceTableLookupEntry(globalContext.namespaceTable, buf);

        if(nsEntry == NULL)
        {
            nsEntry = namespaceTableInsertEntry(globalContext.namespaceTable, buf, NULL);
        }

        if(preNSEntry != NULL)
        {
            nsEntry->isNestedNamespace = true;
            nsEntry->ifNestedParentEntry = preNSEntry;
            nsEntry->withoutScopeName = right->parent.lexeme;

            if(_symTableLookUp(preNSEntry->namespaceSymTable, right->parent.lexeme, LOOKUP_ALL) == NULL)
            {
                _symTableInsertNamespace(preNSEntry->namespaceSymTable, right->parent.lexeme, newCheckerTypeNamespace(nsEntry->namespace, nsEntry->namespaceSymTable), namespace->startTok);
            }
        }


        right = right->scopedChild;
        preNSEntry = nsEntry;

        if(right != NULL) strcat(buf, "_");
    }
    char *namespaceName = allocGetNamespaceName(namespace);

    NSTEntry *entry = namespaceTableLookupEntry(globalContext.namespaceTable, namespaceName);

    if(entry == NULL) entry = namespaceTableInsertEntry(globalContext.namespaceTable, namespaceName, NULL);

    { //check with block
        if(withBlock != NULL)
        {
            ASTNamespaceWithStmtLL *currWithStmt = withBlock->stmts;
            if(currWithStmt != NULL)
            {
                currWithStmt = currWithStmt->first;
                while(currWithStmt != NULL)
                {
                    Token iden = currWithStmt->item->iden;

                    if(!strcmp(iden.lexeme, "libs"))
                    {
                        ASTNamespaceWithValueLL *vals = currWithStmt->item->vals;
                        while(vals != NULL)
                        {
                            char fullPath[MAX_PATH] = {0};
                            char libName[MAX_PATH] = {0};
                            strncpy(libName, vals->item->val.lexeme + 1, strlen(vals->item->val.lexeme) - 2);

                            HMODULE dllMod = NULL;
                            bool foundSystemDll = false;
                    
                            { //try find system dll first, if that fails then find local dl
                                dllMod = LoadLibraryA(libName);

                                if(dllMod != NULL) 
                                {
                                    foundSystemDll = true;
                                    sprintf(fullPath, "\"%s\"", libName);
                                }
                            }

                            if(!foundSystemDll)
                            {
                                char *sourceFilesDir = allocGetFileFolder(namespace->startTok.pos.filename);
                                sprintf(fullPath, "\"%s\\%s\"", sourceFilesDir, libName);

                                // even if it is still not found just link the library by name
                                if(!doesFileExist(fullPath))
                                {
                                    sprintf(fullPath, "\"%s\"", libName);
                                }

                                free(sourceFilesDir);
                            }

                            addLinkerLibToImport(entry, fullPath);
                            vals = vals->prev;
                        }
                    }

                    currWithStmt = currWithStmt->next;
                }
            }
        }
    }


    //IMPORTANT NOTE: since namespaceTok is local variable entry NSTEntry struct must copy contents to its member
    //since ASTProg is allocated after this operation

    SymTable *declsTable = entry->namespaceSymTable;

    globalContext.pc.currSymTble = declsTable;

    decls = declList(declsTable);

    p = newASTProg(namespace, withBlock, isNamespaced, tags, decls);
    p->myNSTEntry = entry;
    p->tbl = declsTable;
    p->filename = strdup(globalContext.pc.currFileName);
    
    collectDecls(p, decls);
    resetLexer();

    return p;
}
ASTNamespaceName *namespaceName(bool isRecursing)
{
    ASTNamespaceName *namespace = NULL;
    Token parent;
    ASTNamespaceName *scopedChild = NULL;

    if(tok.type == TOK_IDEN)
    {
        parent = tok;
        tok = lex();
        if(tok.type == TOK_SCOPE_ACCESS)
        {
            tok = lex();
            scopedChild = namespaceName(true);
        }
        
        namespace = newASTNamespaceName(parent, scopedChild);
    }

    if(!isRecursing && (namespace == NULL))
    {
        parserError(tok, "Expected identifier or scoped namespace name, instead got '%s'", tok.lexeme);
    }
    return namespace;
}
ASTNamespaceWithBlock *namespaceWithBlock()
{
    ASTNamespaceWithBlock *b = NULL;
    ASTNamespaceWithStmtLL *stmtLL = NULL;
    Token startTok = tok;
    Token endTok = tok;
    
    if(tok.type = '{')
    {
        tok = lex();

        stmtLL = namespaceWithStmtList();

        if(tok.type == '}') 
        {
            endTok = tok;
            tok = lex();
        }
        else parserError(tok, "Expected '}' to close block, instead got '%s'", tok.lexeme);
    }
    else
    {
        stmtLL = newASTNamespaceWithStmtLL(namespaceWithStmt());
    }

    b = newASTNamespaceWithBlock(startTok, endTok, stmtLL);
    return b;
}
ASTNamespaceWithStmtLL *namespaceWithStmtList()
{
    ASTNamespaceWithStmt *withStmt = namespaceWithStmt();
    ASTNamespaceWithStmtLL *stmtLL = newASTNamespaceWithStmtLL(withStmt);

    while((tok.type != '}') && (tok.type != TOK_EOF))
    {
        addASTNamespaceWithStmtLL(&stmtLL, namespaceWithStmt());
    }

    return stmtLL;
}
ASTNamespaceWithStmt *namespaceWithStmt()
{
    Token iden;
    ASTNamespaceWithValueLL *vals = NULL;

    if(tok.type == TOK_IDEN)
    {
        iden = tok;
        tok = lex();

        if(tok.type == '=')
        {
            tok = lex();
            vals = namespaceWithValueList();
            if(tok.type == ';') tok = lex();
            else parserError(tok, "Expected ';' after namespace with block stmt, instead got '%s'", tok.lexeme);
        }
        else parserError(tok, "Expected '=', instead got '%s'", tok.lexeme);
    }
    else parserError(tok, "Expected an identifier for namespace with block stmt, instead got '%s'", tok.lexeme);

    return newASTNamespaceWithStmt(iden, vals);
}

ASTNamespaceWithValueLL *namespaceWithValueList()
{
    ASTNamespaceWithValueLL *valueLL = NULL;
    bool bracketRequired = false;

    if(tok.type == '(')
    {
        tok = lex();
        bracketRequired = true;
    }
    valueLL = newASTNamespaceWithValueLL(namespaceWithValue());

    while((tok.type == ',') && (tok.type != TOK_EOF))
    {
        tok = lex();
        addASTNamespaceWithValueLL(&valueLL, namespaceWithValue());
    }

    if(bracketRequired)
    {
        if(tok.type == ')') tok = lex();
        else parserError(tok, "Expected ')', instead got '%s'", tok.lexeme);
    }

    return valueLL;
}
ASTNamespaceWithValue *namespaceWithValue()
{
    ASTNamespaceWithValue *val = NULL;
    Token v;

    if(tok.type == TOK_STRING_LITERAL)
    {
        v = tok;
        tok = lex();
    }
    else parserError(tok, "Expected a string for namespace with value, instead got '%s'", tok.lexeme);

    val = newASTNamespaceWithValue(v);
    return val;
}

ASTCompilerTagLL *progCompilerTagList(void)
{
    if(tok.type != '#') return NULL;

    ASTCompilerTagLL *ll = NULL;
    ll = newASTCompilerTagLL(progCompilerTag());

    while((tok.type == '#') && (tok.type != TOK_EOF))
    {
        addASTCompilerTagLL(&ll,progCompilerTag());
    }

    return ll;
}
ASTCompilerTag *progCompilerTag(void)
{
    ASTCompilerTag *t = NULL;
    ASTTagParamLL *paramLL = NULL;
    Token name;
    Token endToken;

    if(tok.type == '#')
    {
        tok = lex();

        if(tok.type == '[')
        {
            tok = lex();

            if(tok.type == TOK_IDEN)
            {
                name = tok;
                tok = lex();

                if(tok.type == '(')
                {
                    tok = lex();
                    paramLL = declTagParamList();

                    if(tok.type == ')') tok = lex();
                    else parserError(tok, "Expected compiler tag parameter list to end in ')', instead got '%s'", tok.lexeme);
                }

                if(tok.type == ']') 
                {
                    tok = lex();
                    endToken = tok;
                }
                else parserError(tok, "Expected matching ']' at the end of compiler tag, got '%s'", tok.lexeme);
            }
            else parserError(tok, "Expected identifier token for compiler tag, instead got '%s'", tok.lexeme);
        }
        else parserError(tok, "Expected compiler tag to have '[' after '#', like this: #[tagname(optional params)], instead got '%s'", tok.lexeme);

    }
    else parserError(tok, "Expected '#' to begin compiler tag, got '%s'", tok.lexeme);

    t = newASTCompilerTag(name, endToken, name, paramLL);

    return t;
}
void progCheckCompilerSettings(ProgCompilerSettings *settings, ASTCompilerTagLL *compTags)
{
    if(compTags == NULL) return;

    ASTCompilerTagLL *currTagLL = compTags->first;

    while(currTagLL != NULL)
    {
        ASTCompilerTag *tag = currTagLL->item;

        if(!strcmp(tag->name.lexeme, "beingImportedSettings"))
        {
            if(tag->paramLL != NULL)
            {
                ASTTagParamLL *currParamLL = tag->paramLL->first;

                while(currParamLL != NULL)
                {
                    Token param = currParamLL->item;
                    
                    if(!strcmp(param.lexeme, "\"NoGlobal\""))
                    {
                        settings->importedSettings.shouldImportGlobal = false;
                    }
                    else parserError(param, "'%s' is not a valid parameter for the compiler tag '%s'", param.lexeme, tag->name.lexeme);

                    currParamLL = currParamLL->next;
                }
            }
        }
        else parserError(tag->startTok, "Not a valid compiler tag name, got '%s'", tag->name.lexeme);

        currTagLL = currTagLL->next;
    }
}

ASTDeclLL *declList(SymTable *tbl)
{
    if(tok.type == TOK_EOF) return NULL;

    ASTDeclLL *ll = NULL;
    ParsingKind prevPK = globalContext.pc.kind;

    globalContext.pc.kind = PC_DECL;

    int val = setjmp(pErrorDecl);

    if(val == 0)
    {
        ASTDecl *d = decl(tbl);
        d->tbl = tbl;
        ll = newASTDeclLL(d);
    }
    else
    {
        while((tok.type != '}') && (tok.type != TOK_EOF)) tok = lex();

        if((tok.type == ';') || (tok.type == '}')) tok = lex();
    }
    while((tok.type != TOK_EOF) && (tok.type != '}'))
    {
        ASTDecl *d = decl(tbl);
        d->tbl = tbl;

        addASTDeclLL(&ll, d);
    }
    globalContext.pc.kind = prevPK;

    return ll;

}
ASTDecl *decl(SymTable *tbl)
{
    ASTTagLL *tagLL = NULL;
    ASTDecl *d = NULL;

    ParsingKind prevPK = globalContext.pc.kind;
    globalContext.pc.kind = PC_DECL;

    tagLL = declTagList();
    
    switch(tok.type)
    {
        case '{': 
        {

            Token startTok = tok;

            tok = lex();
            
            ASTDeclLL *declLl = declList(tbl);

            if(tok.type == '}') 
            {
                tok = lex();
                d = newASTDeclDeclList(declLl, startTok);
            }
            else parserError(tok, "Expected matching '}' to indicate end of tag block, but got %s", tok.lexeme);

        }break;
        case TOK_IMPORT_KW: d = importDecl(); break;
        case TOK_OPERATOR_KW: d = operatorDecl(); break;
        case TOK_TYPEALIAS_KW: d = typealiasDecl(); break;
        case TOK_STRUCT_KW: d = structDecl(); break;
        case TOK_UNION_KW: d = unionDecl(); break;
        case TOK_ENUM_KW: d = enumDecl(); break;
        case TOK_METHOD_KW: d = methodBlockDecl(); break;
        case TOK_IMMUT_KW:
        case TOK_CONST_KW:
        case '(': d = varDecl(false); break;
        case TOK_IDEN:
        {
            if(peekLex(&tok, ':'))
            {
                d = varDecl(false);
            }
            else
            {
                d = funcDecl();
            }
        }break;
        default:
        {
            parserError(tok, "Unexpected token to begin declaration, got %s", tok.lexeme);
            break;
        }
    }
    d->endTok = tok;
    globalContext.pc.kind = prevPK;

    if(d->kind == A_DECL_DECLLIST)
    {        
        if(tagLL != NULL)
        {
            ASTDeclLL *dll = d->declList.declLL->first;
            while(dll != NULL)
            {
                if(dll->item->tags != NULL)
                    insertAfterASTTagLL(dll->item->tags, tagLL);
                else
                    dll->item->tags = tagLL;

                dll = dll->next;
            }
        }
    }
    else d->tags = tagLL;

    return d;
}
ASTTagLL *declTagList(void)
{
    if(tok.type != '[') return NULL;

    ASTTagLL *ll = NULL;

    ll = newASTTagLL(declTag());

    while ((tok.type == '[') && (tok.type != TOK_EOF))
    {
        addASTTagLL(&ll, declTag());
    }
    
    return ll;
}
ASTTag *declTag(void)
{
    ASTTag *t = NULL;
    Token name;
    ASTTagParamLL *paramLL = NULL;

    if(tok.type == '[')
    {
        tok = lex();
        if(tok.type == TOK_IDEN)
        {
            name = tok;
            tok = lex();
            
            if(tok.type == '(')
            {
                tok = lex();
                paramLL = declTagParamList();
                
                if(tok.type == ')') tok = lex();
                else parserError(tok, "Expected matching ')' to end tag parameters but got %s", tok.lexeme);
            }
            
            if(tok.type == ']') tok = lex();
            else parserError(tok, "Expected '(' to begin tag parameters or '] to end tag but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected identifier for tag name but got %s", tok.lexeme);
    }
    else parserError(tok, "Expected '[' to begin tag but got %s", tok.lexeme);

    return newASTTag(name, paramLL);
}
ASTTagParamLL *declTagParamList(void)
{
    ASTTagParamLL *ll = NULL;

    ll = newASTTagParamLL(declTagParam());

    while((tok.type == ',') && (tok.type != TOK_EOF))
    {
        tok = lex();
        addASTTagParamLL(&ll, declTagParam());
    }

    return ll;
}
Token declTagParam(void)
{
    Token param;
    if(tok.type == TOK_STRING_LITERAL) 
    {
        param = tok;
        tok = lex();
    }

    return param;
}

ASTDecl *importDecl(void)
{
    ASTDecl *d = NULL;
    Token iden;
    bool hasIden = false;
    ASTNamespaceName *namespace = NULL;

    if(tok.type == TOK_IMPORT_KW)
    {
        tok = lex();

        namespace = namespaceName(false);
            
        if(tok.type == '=') 
        {
            if(namespace->scopedChild == NULL)
            {
                hasIden = true;
                tok = lex();
                iden = namespace->parent;

                namespace = namespaceName(false);
            }
            else parserError(namespace->startTok, "Expected a single identifier, if using custom identifier for import");
        }

        d = newASTDeclImport(iden, hasIden, namespace);
    }
    else parserError(tok, "Expected import kw, to begin import declaration, but got %s", tok.lexeme);
    
    d->endTok = tok;
    return d;
}
ASTDecl *funcDecl(void)
{
    Token iden;
    ASTFuncSig *sig = NULL;
    ASTBlock *b = NULL;
    ASTGenericTypeLL *genericParams = NULL;

    if(tok.type == TOK_IDEN)
    {
        iden = tok;
        tok = lex();

        if(tok.type == '<')
        {
            tok = lex();
            genericParams = genericTypeLL();

            if(tok.type == '>') tok = lex();
            else parserError(tok, "Expected '>' to enclose generic paramters to function, instead got '%s'", tok.lexeme);
        }

        sig = funcSig();

        if(tok.type == ';') tok = lex();
        else b = block();
    }
    else parserError(tok, "Expected an identifier for function declaration, but instead got %s", tok.lexeme);

    ASTDecl *d = newASTDeclFunc(iden, genericParams, sig, b, false);
    d->endTok = tok;
    return d;
}
ASTDecl *operatorDecl(void)
{
    Token op;
    ASTFuncSig *sig = NULL;
    ASTBlock *b = NULL;
    if(tok.type == TOK_OPERATOR_KW)
    {
        tok = lex();

        if((tok.type == '+') || (tok.type == '-') ||
           (tok.type == TOK_INC_OP) || (tok.type == TOK_DEC_OP) ||
           (tok.type == '*') || (tok.type == '/') || (tok.type == '%') ||
           (tok.type == '>') || (tok.type == '<') || 
           (tok.type == TOK_GE_RELOP) || (tok.type == TOK_LE_RELOP) ||
           (tok.type == TOK_EQ_RELOP) || (tok.type == TOK_NEQ_RELOP) ||
           (tok.type == '[') || (tok.type == '?'))
        {
            op = tok;
            tok = lex();

            if(op.type == '[')
            {
                if(tok.type == ']') tok = lex();
                else parserError(tok, "Expected ']' after '[' for index overload function, got %s", tok.lexeme);
            }

            sig = funcSig();
            b = block();
        }
        else parserError(tok, "Unsupported operator for operator overload function, got %s", tok.lexeme);
    }
    else parserError(tok, "Expected operator kw for operator function, instead got %s", tok.lexeme);

    return newASTDeclOperFunc(op, sig, b);
}

TokenLL *declSpecifiers(void)
{
    // ADD CONST HERE TOO
    if((tok.type == TOK_IMMUT_KW) || (tok.type == TOK_CONST_KW))
    {
        TokenLL *ll = newTokenLL(tok);

        tok = lex();
        while((tok.type == TOK_IMMUT_KW) || (tok.type == TOK_CONST_KW))
        {
            addTokenLL(&ll, tok);
            tok = lex();
        }

        return ll;
    }
    else return NULL;
}
ASTDecl *varDeclWithExprIden(ASTExpr *idenExpr, TokenLL *specifiers)
{
    ASTType *type = NULL;
    ASTExpr *initial = NULL;
    ASTTagLL *tags = NULL;

    bool isUsingDeclInStruct = false;

    if((idenExpr->kind != A_EXPR_IDEN) && (idenExpr->kind != A_EXPR_TUPLE_LITERAL))
    {
        parserError(tok, "Expected identifier or tuple for variable declaration, instead got %s", tok.lexeme);
    }
    
    if(tok.type == ':')
    {
        tok = lex();
        if(tok.type == '=')
        {
            tok = lex();
            type = newASTTypeInfer(idenExpr->startTok);
            initial = expr();
        }
        else 
        {
            type = typeSpec();
            if(tok.type == '=')
            {
                tok = lex();
                initial = expr();
            }
        }

        if(tok.type == ';')
            tok = lex();
        else parserError(tok, "Expected ';' after variable declaration, instead got %s", tok.lexeme);
        
    }
    else parserError(tok, "Expected ':' after identifier for variable declaration, instead got %s", tok.lexeme);

    ASTDecl *d = newASTDeclVar(specifiers, idenExpr, type, initial);

    d->tags = tags;
    d->var.isUsingDecl = isUsingDeclInStruct;
    d->endTok = tok;
    return d;
}
ASTDecl *varDecl(bool isStructUnionMember)
{
    ASTExpr *idenExpr;
    ASTType *type = NULL;
    ASTExpr *initial = NULL;
    ASTTagLL *tags = NULL;

    bool isUsingDeclInStruct = false;
     //only if struct union members
    if(isStructUnionMember)
    {
        tags = declTagList();

        if(tok.type == TOK_USING_KW) 
        {
            tok = lex();
            isUsingDeclInStruct = true;
        }
    }

    TokenLL *specifiers = declSpecifiers();

    idenExpr = primaryTerm();
    
    ASTDecl *d = varDeclWithExprIden(idenExpr, specifiers);

    return d;
}
ASTDecl *typealiasDecl(void)
{
    Token iden;
    ASTType *type = NULL;

    if(tok.type == TOK_TYPEALIAS_KW)
    {
        tok = lex();

        if(tok.type == TOK_IDEN)
        {
            iden = tok;
            tok = lex();

            if(tok.type == '=')
            {
                tok = lex();

                type = typeSpec();

                if(tok.type == ';') tok = lex();
                else parserError(tok, "Expected ';' to end typealias declaration, instead got %s", tok.lexeme);
            }
            else parserError(tok, "Expected '=' after typealias identifer, instead got %s", tok.lexeme);
        }
        else parserError(tok, "Expected identifer for typealias, instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected typealias kw to begin typealias declaration, instead got %s", tok.lexeme);

    return newASTDeclType(iden, type);
}
ASTDecl *structDecl(void)
{
    Token iden;
    ASTDeclLL *membs = NULL;
    ASTGenericTypeLL *genParams = NULL;

    if(tok.type == TOK_STRUCT_KW)
    {
        tok = lex();

        if(tok.type == '<')
        {
            tok = lex();
            genParams = genericTypeLL();

            if(tok.type == '>') tok = lex();
            else parserError(tok, "Expected '>' to enclose generic parameters in struct decl, got '%s'", tok.lexeme);
        }

        if(tok.type == TOK_IDEN)
        {
            iden = tok;
            tok = lex();


            if(tok.type == '{')
            {
                tok = lex();
                membs = varDeclList(true);

                if(tok.type == '}') tok = lex();
                else parserError(tok, "Expected matching '}' to end struct declaration, but got %s", tok.lexeme);
            }
            else parserError(tok, "Expected '{' in struct declaration, but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected identifier for struct declaration, instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected struct kw, to begin struct declaration, got %s", tok.lexeme);

    return newASTDeclStruct(iden, genParams, membs);
}
ASTDecl *unionDecl(void)
{
    Token iden;
    ASTDeclLL *membs = NULL;
    ASTGenericTypeLL *genParams = NULL;

    bool isTaggedUnion = false;
    ASTType *tagNameIfTagged = NULL;

    if(tok.type == TOK_UNION_KW)
    {
        tok = lex();

        if(tok.type == '<')
        {
            tok = lex();
            genParams = genericTypeLL();

            if(tok.type == '>') tok = lex();
            else parserError(tok, "Expected '>' to enclose generic parameters in union decl, got '%s'", tok.lexeme);
        }

        if(tok.type == TOK_IDEN)
        {
            iden = tok;
            tok = lex();

            if(tok.type == '(')
            {
                isTaggedUnion = true;
                tok = lex();
                tagNameIfTagged = typeSpec();

                if(tok.type == ')') tok = lex();
                else parserError(tok, "Expected ')' to close matching bracket, instead got '%s'", tok.lexeme);
            }

            if(tok.type == '{')
            {
                tok = lex();
                membs = varDeclList(true);

                if(tok.type == '}') tok = lex();
                else parserError(tok, "Expected matching '}' to end union declaration, but got %s", tok.lexeme);
            }
            else parserError(tok, "Expected '{' in union declaration, but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected identifier for union declaration, instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected union kw, to begin union declaration, got %s", tok.lexeme);

    ASTDecl *d = (!isTaggedUnion) ? newASTDeclUnion(iden, genParams, membs) : newASTDeclUnionTagged(iden, tagNameIfTagged, genParams, membs);

    return d;
}
ASTDecl *enumDecl(void)
{
    Token iden;
    ASTEnumMembLL *membs = NULL;

    if(tok.type == TOK_ENUM_KW)
    {
        tok = lex();
        
        if(tok.type == TOK_IDEN)
        {
            iden = tok;
            tok = lex();

            if(tok.type == '{')
            {
                tok = lex();
                membs = enumMembList();

                if(tok.type == '}') tok = lex();
                else parserError(tok, "Expected matching '}' to end enum declaration, but got %s", tok.lexeme);
            }
            else parserError(tok, "Expected '{' in enum declaration, but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected identifier for enum declaration, instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected enum kw, to begin enum declaration, got %s", tok.lexeme);

    return newASTDeclEnum(iden, membs);
}
ASTDecl *methodBlockDecl(void)
{
    Token startTok;
    ASTType *type = NULL;
    ASTDeclLL *declLL = NULL;

    if(tok.type == TOK_METHOD_KW)
    {
        startTok = tok;
        tok = lex();

        type = typeSpec();
        if(tok.type == '{')
        {
            tok = lex();

            declLL = declList(globalContext.pc.currSymTble);

            if(tok.type == '}') tok = lex();
            else parserError(tok, "Expected matching '}' for method block, got '%s'", tok.lexeme);
        }
        else parserError(tok, "Expected '{' for beginning method block, got '%s'", tok.lexeme);

    }
    else parserError(tok, "Expected 'method' keyword for beginning method block decl, got '%s'", tok.lexeme);

    return newASTDeclMethodBlock(startTok, type, declLL);
}

ASTDeclLL *varDeclList(bool isStructUnionMember)
{
    if(tok.type == '}') return NULL;

    ASTDeclLL *ll = NULL;
    ll = newASTDeclLL(varDecl(isStructUnionMember));

    while((tok.type != TOK_EOF) && (tok.type != '}'))
    {
        addASTDeclLL(&ll, varDecl(isStructUnionMember));
    }

    return ll;
}
ASTEnumMembLL *enumMembList(void)
{
    if(tok.type == '}') return NULL;

    ASTEnumMembLL *ll = NULL;
    ll = newASTEnumMembLL(enumMemb());

    while((tok.type != TOK_EOF) && (tok.type != '}'))
    {
        addASTEnumMembLL(&ll, enumMemb());
    }

    return ll;
}
ASTEnumMemb *enumMemb(void)
{
    Token name;
    ASTExpr *val = NULL;

    if(tok.type == TOK_IDEN)
    {
        name = tok;
        tok = lex();
        if(tok.type == '=')
        {
            tok = lex();
            val = expr();
        }
        if(tok.type == ';') tok = lex();
        else parserError(tok, "Expected ';' after enum member, instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected identifier for enum member, but got %s", tok.lexeme);

    return newASTEnumMemb(name, val);
}

ASTBlock *block(void)
{
    ASTBlock *b = NULL;
    Token startTok;
    Token endTok;
    ASTStmtLL *ll = NULL;

    if(tok.type == TOK_RIGHT_ARROW)
    {
        startTok = tok;

        tok = lex();
        ll = newASTStmtLL(stmt());
        
        endTok = ll->item->endTok;
    }
    else if(tok.type == '{')
    {
        startTok = tok;

        tok = lex();
        ll = stmtList();
        
        if(tok.type == '}') 
        {
            endTok = tok;
            tok = lex();
        }
        else parserError(tok, "Expected matching '}' for closing block but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected '-> stmt' or '{ list of statements }' for block but instead got %s", tok.lexeme);

    b = newASTBlock(startTok, ll);
    b->endTok = endTok;
    return b;
}
ASTStmt *stmt(void)
{
    ASTStmt *s = NULL;
    ParsingKind prevPK = globalContext.pc.kind;

    globalContext.pc.kind = PC_STMT;

    switch(tok.type)
    {
        case TOK_TYPEALIAS_KW: s = newASTStmtDecl(typealiasDecl()); break;
        case TOK_STRUCT_KW: s = newASTStmtDecl(structDecl()); break;
        case TOK_UNION_KW: s = newASTStmtDecl(unionDecl()); break;
        case TOK_ENUM_KW: s = newASTStmtDecl(enumDecl()); break;
        case TOK_IMPORT_KW: 
        {
            s = newASTStmtDecl(importDecl()); 

            if(tok.type == ';')
            {
                tok = lex();
            }
            else parserError(tok, "Expected a ';' after import stmt, local imports need semi colons to stay consistent with the syntax");
        }break;
        case TOK_IF_KW: s = ifStmt(); break;
        case TOK_WHILE_KW: s = whileStmt(); break;
        case TOK_FOR_KW: s = forStmt(); break;
        case TOK_RET_KW: s = retStmt(); break;
        case TOK_CONTINUE_KW: s = contStmt(); break;
        case TOK_BREAK_KW: s = breakStmt(); break;
        case TOK_DEFER_KW: s = deferStmt(); break;
        case TOK_USING_KW: s = usingStmt(); break;
        case TOK_MATCH_KW: s = matchStmt(); break;
        case TOK_IDEN:
        {
            if(peekLex(&tok, ':')) s = newASTStmtDecl(varDecl(false)); 
            else
            {
                ASTExpr *l = expr();
                ASTExpr *r = NULL;

                if(tok.type == '=')
                {
                    tok = lex();
                    r = expr();
                }
                
                if(tok.type == ';')
                {
                    tok = lex();
                    if(r != NULL)
                        s = newASTStmtAssign(l, r);
                    else s = newASTStmtExpr(l);
                }
                else parserError(tok, "Expected ';' after statement, but got %s", tok.lexeme);
            }
        }break;
        default: 
        {
            ASTExpr *l = NULL;
            ASTExpr *r = NULL;
            TokenLL *specifiers = declSpecifiers();

            if(specifiers != NULL)
                l = primaryTerm();
            else l = expr();
            
            if(tok.type == ':')
            {
                s = newASTStmtDecl(varDeclWithExprIden(l, specifiers));
            }
            else 
            {
                if(tok.type == '=')
                {
                    tok = lex();
                    r = expr();
                }
                if(tok.type == ';') 
                {
                    tok = lex();

                    if(r != NULL) s = newASTStmtAssign(l, r);
                    else s = newASTStmtExpr(l);
                }
                else parserError(tok, "Expected ';' after expression statement, but got %s", tok.lexeme);
            }
            break;
        }
    }
    //todo remove when done with ast refactor
    s->endTok =tok;
    globalContext.pc.kind = prevPK;

    return s;
}
ASTStmtLL *stmtList(void)
{
    if(tok.type == '}') return NULL;

    ASTStmtLL *ll = NULL;
    ParsingKind prevPK = globalContext.pc.kind;

    globalContext.pc.kind = PC_STMT;

    int val = setjmp(pErrorStmt);
    setErrorStmtJmp = true;

    if(val == 0)
        ll = newASTStmtLL(stmt());
    else
    {
        while((tok.type != ';') && (tok.type != TOK_EOF)) tok = lex();

        if(tok.type == ';') tok = lex();
    }
    while((tok.type != TOK_EOF) && (tok.type != '}'))
    {
        addASTStmtlLL(&ll, stmt());
    }

    globalContext.pc.kind = prevPK;

    return ll;
}

ASTStmt *ifStmt(void)
{
    ASTExpr *cond = NULL;
    ASTBlock *b = NULL;
    ASTBlock *eBlock = NULL;
    Token startTok;
    Token endTok;

    if(tok.type == TOK_IF_KW)
    {
        startTok = tok;
        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();
            cond = expr();

            if(tok.type == ')')
            {
                tok = lex();

                b = block();

                if(tok.type == TOK_ELSE_KW)
                {
                    tok = lex();
                    if(tok.type == TOK_IF_KW) 
                    {
                        ASTStmt *s = ifStmt();

                        ASTStmtLL *stmtLL = newASTStmtLL(s);

                        eBlock = newASTBlock(s->startTok, stmtLL);
                    }
                    else eBlock = block();
                }

                endTok = (eBlock != NULL) ? eBlock->endTok : b->endTok;
            }
            else parserError(tok, "Expected matching ')' after if condition but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected '(' after if kw but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected if kw to begin if statement but instead got %s", tok.lexeme);

    return newASTStmtIf(startTok, endTok, cond, b, eBlock);
}
ASTStmt *whileStmt(void)
{
    Token startTok;
    Token endTok;

    ASTExpr *cond = NULL;
    ASTBlock *b = NULL;

    if(tok.type == TOK_WHILE_KW)
    {
        startTok = tok;

        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();
            cond = expr();

            if(tok.type == ')')
            {
                tok = lex();

                b = block();
                endTok = b->endTok;
            }
            else parserError(tok, "Expected matching ')' after while condition but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected '(' after while kw but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected while kw to begin while statement but instead got %s", tok.lexeme);

    ASTStmt *w = newASTStmtWhile(cond, b);
    w->startTok = startTok;
    w->endTok = endTok;
    return w;
}
ASTStmt *forStmt(void)
{
    ASTExpr *l = NULL;
    ASTExpr *r = NULL;
    ASTBlock *b = NULL;

    if(tok.type == TOK_FOR_KW)
    {
        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();
            l = expr();

            if(tok.type == TOK_IN_KW)
            {
                tok = lex();
                
                r = expr();
                if(tok.type == ')')
                {
                    tok = lex();

                    b = block();
                }
                else parserError(tok, "Expected matching ')' after for condition but got %s", tok.lexeme);
            }
        }
        else parserError(tok, "Expected '(' after for kw but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected for kw to begin for statement but instead got %s", tok.lexeme);

    return newASTStmtFor(l, r, b);
}
ASTStmt *retStmt(void)
{
    ASTExpr *e = NULL;
    Token startTok;
    if(tok.type == TOK_RET_KW)
    {
        startTok = tok;
        tok = lex();

        if(tok.type == ';') tok = lex();
        else
        {
            e = expr();
            if(tok.type == ';') tok = lex();
            else parserError(tok, "Expected ';' after return stmt, instead got %s", tok.lexeme);
        }
    }
    else parserError(tok, "Expected return kw for return statement, instead got %s", tok.lexeme);

    return newASTStmtRet(startTok, e);
}
ASTStmt *contStmt(void)
{
    Token startTok;
    if(tok.type == TOK_CONTINUE_KW)
    {
        startTok = tok;
        tok = lex();

        if(tok.type == ';') tok = lex();
        else parserError(tok, "Expected ';' after continue stmt, instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected continue kw for continue stmt, instead got %s", tok.lexeme);

    return newASTStmtCont(startTok);
}
ASTStmt *breakStmt(void)
{
    Token startTok;
    if(tok.type == TOK_BREAK_KW)
    {
        startTok = tok;
        tok = lex();

        if(tok.type == ';') tok = lex();
        else parserError(tok, "Expected ';' after break stmt, instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected break kw for break stmt, instead got %s", tok.lexeme);

    return newASTStmtBreak(startTok);
}
ASTStmt *deferStmt(void)
{
    ASTBlock *b = NULL;

    if(tok.type == TOK_DEFER_KW)
    {
        tok = lex();

        b = block();
    }
    else parserError(tok, "Expected defer kw for defer stmt, instead got %s", tok.lexeme);

    return newASTStmtDefer(b);
}
ASTStmt *usingStmt(void)
{
    ASTStmt *usingStmt = NULL;
    ASTExpr *e = NULL;
    Token startTok;
    if(tok.type == TOK_USING_KW)
    {
        startTok = tok;
        tok = lex();

        if(tok.type == TOK_IMPORT_KW)
        {
            ASTDecl *iD = importDecl();

            usingStmt = newASTStmtUsingImport(startTok, iD);
        }
        else if(tok.type == TOK_IDEN)
        {
            e = primaryTerm();
            
            if((e->kind != A_EXPR_IDEN) && (e->kind != A_EXPR_SCOPE_ACCESS))
            {
                parserError(e->startTok, "Expected an identifier or scope access expression");
            }

            usingStmt = newASTStmtUsingNormal(startTok, e);
        }
        else parserError(tok, "Expected identifer or 'import' after using kw, instead got %s", tok.lexeme);

        if(tok.type == ';') tok = lex();
        else parserError(tok, "Expected ';' after using stmt, instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected using kw for using stmt, instead got %s", tok.lexeme);

    return usingStmt;
}
ASTStmt *matchStmt(void)
{
    ASTExprLL *exprLL = NULL;
    ASTMatchArmLL *armLL = NULL;
    Token startTok;

    if(tok.type == TOK_MATCH_KW)
    {
        startTok = tok;

        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();
            exprLL = exprList();

            if(tok.type == ')')
            {
                tok = lex();

                if(tok.type == '{')
                {
                    tok = lex();

                    armLL = matchArmList();

                    if(tok.type == '}') tok = lex();
                    else parserError(tok, "Expected matching '}' for match stmt, got %s", tok.lexeme);
                }
                else parserError(tok, "Expected '{' for match stmt, got %s", tok.lexeme);
            
            }
            else parserError(tok, "Expected matching ')' but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected '(' after match kw but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected match kw to begin match stmt, got %s", tok.lexeme);

    return newASTStmtMatch(exprLL, armLL, startTok);
}
ASTMatchArmLL *matchArmList(void)
{
    if(tok.type == '}') return NULL;

    ASTMatchArmLL *ll = NULL;
    ll = newASTMatchArmLL(matchArm());

    while((tok.type != TOK_EOF) && (tok.type != '}'))
    {
        addASTMatchArmlLL(&ll, matchArm());
    }

    return ll;
}
ASTMatchArm *matchArm(void)
{
    ASTExprLL **exprLLs = NULL;
    int exprLLsCount = 0;

    ASTBlock *b = NULL;
    Token startTok;


    if(tok.type == TOK_ELSE_KW)
    {
        startTok = tok;

        tok = lex();
        if(tok.type == TOK_RIGHT_ARROW)
        {
            tok = lex();
            b = block();
        }
        else parserError(tok, "Expected '->' after match arm expressions, got %s", tok.lexeme);
        
        ASTMatchArm *arm = newASTMatchArm(startTok, exprLLs, 0, b);
        arm->endTok = tok;

        return arm;
    }

    exprLLs = malloc(sizeof(ASTExprLL *));

    startTok = tok;

    bool seenBracket = false;
    if(tok.type == '(') 
    {
        tok = lex();
        seenBracket = true;
    }

    if(!strcmp(tok.lexeme, "_"))
    {
        exprLLs[0] = newASTExprLL(expr());
    }
    else exprLLs[0] = exprList();
    exprLLsCount++;

    if(seenBracket) 
    {
        if(tok.type == ')') tok = lex();
        else parserError(tok, "Expected ')' to close arm expression list, instead got '%s'", tok.lexeme);
    }

    while((tok.type != TOK_EOF) && (tok.type != TOK_RIGHT_ARROW))
    {
        bool seenBracket = false;
        if(tok.type == '(') 
        {
            tok = lex();
            seenBracket = true;
        }

        if(!strcmp(tok.lexeme, "_"))
        {
            exprLLs = realloc(exprLLs, sizeof(ASTExprLL *) * (exprLLsCount + 1));
            exprLLs[exprLLsCount] = newASTExprLL(expr());
        }
        else 
        {
            exprLLs = realloc(exprLLs, sizeof(ASTExprLL *) * (exprLLsCount + 1));
            exprLLs[exprLLsCount] = exprList();
        }

        exprLLsCount++;

        if(seenBracket) 
        {
            if(tok.type == ')') tok = lex();
            else parserError(tok, "Expected ')' to close arm expression list, instead got '%s'", tok.lexeme);
        }
    }

    if(tok.type == TOK_RIGHT_ARROW)
    {
        tok = lex();
        b = block();
    }
    else parserError(tok, "Expected '->' after match arm expressions, got %s", tok.lexeme);

    ASTMatchArm *arm = newASTMatchArm(startTok, exprLLs, exprLLsCount, b);
    arm->endTok = tok;
    return arm;
}

ASTType *typeSpec(void)
{
    ASTType *t = NULL;

    switch(tok.type)
    {
        case TOK_IDEN: 
        {
            if(peekLex(&tok, TOK_SCOPE_ACCESS))
            {
                ASTExpr *e = scopeAccessExprMaybeIdenExpr();

                t = newASTTypeScoped(e);
            }
            else 
            {
                t = newASTTypeIden(tok); 
                tok = lex();
            } 
        }break;
        case TOK_VOID_KW: t = newASTTypeVoid(tok); tok = lex(); break;
        case TOK_INT_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_I8_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_I16_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_I32_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_I64_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_UI8_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_UI16_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_UI32_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_UI64_KW: t = newASTTypeInt(tok); tok = lex(); break;
        case TOK_F32_KW: t = newASTTypeFloat(tok); tok = lex(); break;
        case TOK_F64_KW: t = newASTTypeFloat(tok); tok = lex(); break;
        case TOK_FLOAT_KW: t = newASTTypeFloat(tok); tok = lex(); break;
        //case TOK_STRING_KW: t = newASTTypeString(tok); tok = lex(); break;
        case TOK_BOOL_KW: t = newASTTypeBool(tok); tok = lex(); break;
        case TOK_TUPLE_KW: t = tupleTypeSpec(); break;
        case '[': t = arrayTypeSpec(); break;
        case '*': t = pointerTypeSpec(); break;
        case '(': t = funcTypeSpec(); break;
        case TOK_2_DOTS:
        {
            Token startTok = tok;
            tok = lex();
            
            if(tok.type == '?') tok = lex();
            ASTType *base = typeSpec();

            t = newASTTypeVariadic(startTok, base);
        }break;
        case TOK_STRUCT_KW: t = newASTTypeIden(tok); tok = lex(); break;
        case TOK_UNION_KW: t = newASTTypeIden(tok); tok = lex(); break;
        case TOK_ENUM_KW: t = newASTTypeIden(tok); tok = lex(); break;
        default: parserError(tok, "Expected a valid type name but instead got %s", tok.lexeme);  break;
    }

    if(tok.type == '<')
    {
        ASTTypeLL *typeFilter = NULL;

        tok = lex();
        
        typeFilter = newASTTypeLL(typeSpec());

        while((tok.type == ',') && (tok.type != TOK_EOF))
        {
            tok = lex();

            addASTTypeLL(&typeFilter, typeSpec());
        }

        if(tok.type == '>') tok = lex();
        else parserError(tok, "Expected '>' to end parameter type filter, got %s", tok.lexeme);

        t->typeFilter = typeFilter;
    }

    t->endTok = tok;
    return t;
}
ASTType *arrayTypeSpecFromSizeAndBase(ASTExpr *size, ASTType *base, Token startTok)
{
    bool isSlice = (size == NULL);
    return newASTTypeArray(startTok, size, base, isSlice);
}
ASTType *arrayTypeSpec(void)
{
    ASTExpr *size = NULL;
    ASTType *type = NULL;
    bool isSlice = false;

    Token startTok;

    if(tok.type == '[')
    {
        startTok = tok;
        tok = lex();

        if(tok.type == ']') isSlice = true;
        else size = expr();

        if(tok.type == ']')
        {
            tok = lex();
            type = arrayTypeSpecFromSizeAndBase(size, typeSpec(), startTok);
        }
        else parserError(tok, "Expected a matching ']' but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected an '[' to begin array type but instead got %s", tok.lexeme);

    return type;
}
ASTType *pointerTypeSpec(void)
{
    ASTType *type = NULL;

    if(tok.type == '*')
    {
        tok = lex();
        type = newASTTypePointer(typeSpec());
    }
    else parserError(tok, "Expected an '*' to begin pointer type but instead got %s", tok.lexeme);

    return type;
}
ASTType *funcTypeSpec(void)
{
    return newASTTypeFuncPointer(funcSig());
}
ASTType *tupleTypeSpec(void)
{
    Token startTok;
    ASTTupleMembLL *membs = NULL;

    if(tok.type == TOK_TUPLE_KW)
    {
        startTok = tok;

        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();

            membs = tupleMembLL();

            if(tok.type == ')')
            {
                tok = lex();
            }
            else parserError(tok, "Expected matching ')', instead got '%s'", tok.lexeme);
        }
        else parserError(tok, "Expected '(' to begin tuple member list, got '%s'", tok.lexeme);
    }
    else parserError(tok, "Expected 'tuple' keyword for tuple type, got '%s'", tok.lexeme);

    ASTType *t = newASTTypeTuple(startTok, membs);
    t->endTok = tok;

    return t;
}

ASTTupleMemb *tupleMemb(void)
{
    ASTTupleMemb *memb = NULL;

    Token startTok;
    Token membIden;
    ASTType *membType = NULL;
    bool hasIden = false;

    if(peekLex(&tok, ':'))
    {
        if(tok.type == TOK_IDEN)
        {
            membIden = tok;
            startTok = tok;

            tok = lex();
            hasIden = true;

            if(tok.type == ':')
            {
                tok = lex();
                membType = typeSpec();
            }
            else parserError(tok, "Expected ':' after identifier to specify tuple member type, instead got '%s'", tok.lexeme);
        }
        else parserError(tok, "Expected an identifer for named field in tuple, but got '%s'", tok.lexeme);
    }
    else
    {
        startTok = tok;
        membType = typeSpec();
    }

    memb = newASTTupleMemb(startTok, membIden, membType, hasIden);
    memb->endTok = tok;

    return memb;
}
ASTTupleMembLL *tupleMembLL(void)
{
    ASTTupleMembLL *membs = NULL;

    membs = newASTTupleMembLL(tupleMemb());

    while( (tok.type == ',') && (tok.type != TOK_EOF))
    {
        tok = lex();
        addASTTupleMembLL(&membs, tupleMemb());
    }

    return membs;
}

ASTFuncSig *funcSig(void)
{
    ASTFuncSig *sig = NULL;
    ASTFuncParamLL *params = NULL;
    ASTType *retType = NULL;
    Token startTok;

    if(tok.type == '(')
    {
        startTok = tok;

        tok = lex();
        params = funcParamList();

        if(tok.type == ')')
        {
            tok = lex();

            if(tok.type == ':')
            {
                tok = lex();
                bool seenBracket = false;
                if(tok.type == '(') 
                {
                    tok = lex();
                    seenBracket = true;
                }

                retType = typeSpec();

                if(seenBracket)
                {
                    if(tok.type == ')') tok = lex();
                    else parserError(tok, "Expected matching ')' instead got '%s'", tok.lexeme);
                }
            }
            else
            {
                Token v = tok;
                v.type = TOK_VOID_KW;
                strcpy(v.lexeme, "void");

                retType = newASTTypeVoid(v);
            }
        }
        else parserError(tok, "Expected a matching ')' but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected a '(' to begin function signature but instead got %s", tok.lexeme);

    sig = newASTFuncSig(startTok, params, (params == NULL) ? 0 : params->numItems, retType);
    sig->endTok = tok;
    return sig;
}
ASTFuncParamLL *funcParamList(void)
{
    ASTFuncParamLL *ll = NULL;

    if(tok.type == ')') return ll;

    ll = newASTFuncParamLL(funcParam());
    while(tok.type == ',')
    {
        tok = lex();
        addASTFuncParamLL(&ll, funcParam());
    }

    return ll;
}
ASTFuncParam *funcParam(void)
{
    ASTFuncParam *param = NULL;
    ASTType *type =NULL;
    ASTExpr *e = NULL;
    Token id;

    if(tok.type == TOK_IDEN)
    {
        id = tok;

        tok = lex();

        if(tok.type == ':')
        {
            tok = lex();

            if(tok.type == '=')
            {
                tok = lex();
                type =newASTTypeInfer(id);
                e = expr();
            }
            else
            {
                type = typeSpec();
                if(tok.type == '=')
                {
                    tok = lex();
                    e = expr();
                }
            }
        }
        else parserError(tok, "Expected a ':' after function param identifier but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected an identifer for function param name but instead got %s", tok.lexeme);

    param = newASTFuncParam(id, type, e);
    return param;
}

ASTGenericType *genericType(void)
{
    ASTGenericType *g = NULL;
    if(tok.type == TOK_IDEN)
    {
        g = newASTGenericType(tok);
        tok = lex();
    }
    else parserError(tok, "Expected a identifier for generic paramter name, instead got '%s'", tok.lexeme);

    return g;
}
ASTGenericTypeLL *genericTypeLL(void)
{
    ASTGenericTypeLL *ll = NULL;

    ll = newASTGenericTypeLL(genericType());

    while( (tok.type == ',') && (tok.type != TOK_EOF))
    {
        tok = lex();
        addASTGenericTypeLL(&ll, genericType());
    }

    return ll;
}

ASTExpr *expr(void)
{
    ASTExpr *e = NULL;
    if(tok.type == TOK_EOF)
        return e;

    e = logOpExpr();

    e->endTok = tok;

    while(tok.type == '?') //null coalessing operator
    {
        Token op = tok;
        tok = lex();

        ASTExpr *eRight = logOpExpr();
        eRight->endTok = tok;

        e = newASTExprBinary(op, e, eRight);
        e->endTok = tok;

    }

    return e;
}
ASTExpr *logOpExpr(void)
{
    ASTExpr *e = NULL;
    if(tok.type == TOK_EOF)
        return e;

    e = rangeTerm();

    e->endTok = tok;

    while((tok.type == TOK_AND_LOGOP) || (tok.type == TOK_OR_LOGOP))
    {
        Token op = tok;
        tok = lex();

        ASTExpr *eRight = rangeTerm();
        eRight->endTok = tok;

        e = newASTExprBinary(op, e, eRight);
        e->endTok = tok;

    }

    return e;
}
ASTExpr *rangeTerm(void)
{
    ASTExpr *e = NULL;

    e = bitTerm();
    while((tok.type == ':'))
    {
        if(peekLex(&tok, '='))
            break;
        else if(peekLex(&tok, TOK_TUPLE_KW))
            break;

        tok = lex();
        e = newASTExprRange(e, bitTerm());
    }

    return e;
}
ASTExpr *bitTerm(void)
{
    ASTExpr *e = NULL;

    e = relEqTerm();
    while((tok.type == '&') || (tok.type == '|') || (tok.type == '^'))
    {
        Token op = tok;
        tok = lex();
        e = newASTExprBinary(op, e, relEqTerm());
    }

    return e;
}
ASTExpr *relEqTerm(void)
{   
    ASTExpr *e = NULL;

    e = relTerm();
    while((tok.type == TOK_EQ_RELOP) || (tok.type == TOK_NEQ_RELOP))
    {
        Token op = tok;

        tok = lex();
        e = newASTExprBinary(op, e, relTerm());
    }

    return e;
}
ASTExpr *relTerm(void)
{
    ASTExpr *e = NULL;

    e = shiftTerm();
    while((tok.type == '<') || (tok.type == TOK_LE_RELOP) || (tok.type == '>') || (tok.type == TOK_GE_RELOP))
    {
        Token op = tok;
        tok = lex();
        e = newASTExprBinary(op, e, shiftTerm());
    }

    return e;
}
ASTExpr *shiftTerm(void)
{
    ASTExpr *e = NULL;

    e = addTerm();
    while((tok.type == TOK_LEFT_SHIFT) || (tok.type == TOK_RIGHT_SHIFT))
    {
        Token op = tok;
        tok = lex();
        e = newASTExprBinary(op, e, addTerm());
    }

    return e;
}
ASTExpr *addTerm(void)
{
    ASTExpr *e = NULL;

    e = multTerm();
    while((tok.type == '+') || (tok.type == '-'))
    {
        Token op = tok;
        tok = lex();
        e = newASTExprBinary(op, e, multTerm());
    }

    return e;
}
ASTExpr *multTerm(void)
{
    ASTExpr *e = NULL;

    e = unaryTerm();
    while((tok.type == '*') || (tok.type == '/')|| (tok.type == '%'))
    {
        Token op = tok;
        tok = lex();
        e = newASTExprBinary(op, e, unaryTerm());
    }

    return e;
}
ASTExpr *unaryTerm(void)
{
    Token op = tok;
    op.lexeme[0] = '\0';
    op.type = 0;

    bool seenOp = false;
    ASTExpr *e = NULL;

    if((tok.type == '+') || (tok.type == '-')|| (tok.type == '@') || (tok.type == '*') || 
       (tok.type == '!')|| (tok.type == '~') || (tok.type == TOK_INC_OP) || (tok.type == TOK_DEC_OP))
    {
        op = tok;
        seenOp = true;
        tok = lex();
    }

    e = postExprTerm();
    if(seenOp) e = newASTExprUnary(op, e);

    return e;
}
ASTExpr *postExprTerm(void)
{
    ASTExpr *e = NULL;

    e = primaryTerm();
    while (((tok.type == '[') || (tok.type == '.') || (tok.type == TOK_NULL_ACCESS_OP) || (tok.type == '(')) && !isExprDiscardIdentifier(e))
    {
        if(tok.type == '[')
        {
            tok = lex();
            e = newASTExprArrayRef(e, expr());
            if(tok.type == ']') tok = lex();
            else parserError(tok, "Expected a matching ']' but instead got %s", tok.lexeme);
        }
        else if(tok.type == '.')
        {
            tok = lex();
            e = newASTExprMembAccess(e, tok);
            if(tok.type == TOK_IDEN) tok = lex();
            else parserError(tok, "Expected an identifier for member access but instead got %s", tok.lexeme);
        }
        else if(tok.type == TOK_NULL_ACCESS_OP)
        {
            tok = lex();
            Token iden = tok;
            if(tok.type == TOK_IDEN) 
            {
                ASTExpr *elseExpr = NULL;

                tok = lex();
                if(tok.type == TOK_ELSE_KW)
                {
                    tok = lex();
                    elseExpr = expr();
                }
                
                e = newASTExprNullAccess(newASTExprMembAccess(e, iden), elseExpr);
            }
            else parserError(tok, "Expected an identifier for null access but instead got %s", tok.lexeme);
        }
        else if(tok.type == '(')
        {
            ASTNamedExprLL *args = NULL;
            tok = lex();

            if(tok.type == ')')
            {
                tok = lex();
                args = NULL;
            }
            else 
            {
                args = namedExprList();
                if(tok.type == ')') tok = lex();
                else parserError(tok, "Expected a matching ')' but instead got %s", tok.lexeme);
            }
            
            e = newASTExprFuncCall(e, args);
        }

        e->endTok = tok;
    }

    if(tok.type == TOK_INC_OP)
    {
        e = newASTExprPost(tok, e);
        tok = lex();
    }
    else if(tok.type == TOK_DEC_OP)
    {
        e = newASTExprPost(tok, e);
        tok = lex();
    }

    e->endTok = tok;
    return e;
}
ASTExpr *primaryTerm(void)
{
    ASTExpr *e = NULL;

    if(tok.type == TOK_IDEN)
    {
        e = scopeAccessExprMaybeIdenExpr();

        if((tok.type == '{') && !isExprDiscardIdentifier(e))
        {
            tok = lex();
            ASTNamedExprLL *ll = namedExprList();

            if(tok.type == '}')
            {
                tok = lex();
            }
            else parserError(tok, "Expected matching '}' for struct literal but instead got %s", tok.lexeme);

            ASTType *structType = NULL;
            if(e->kind == A_EXPR_IDEN) structType = newASTTypeIden(e->iden);
            else structType = newASTTypeScoped(e);

            e = newASTExprStructunionLit(e->startTok, structType, ll);
        }
    }
    else if(tok.type == TOK_INT_LITERAL)
    {
        e = newASTExprLit(tok);
        tok = lex();

        e->endTok = tok;
    }
    else if(tok.type == TOK_FLOAT_LITERAL)
    {
        e = newASTExprLit(tok);
        tok = lex();
    }
    else if(tok.type == TOK_BOOL_LITERAL)
    {
        e = newASTExprLit(tok);
        tok = lex();
    }
    else if(tok.type == TOK_STRING_LITERAL)
    {
        e = newASTExprLit(tok);
        tok = lex();
    }
    else if(tok.type == TOK_CHAR_LITERAL)
    {
        e = newASTExprLit(tok);
        tok = lex();
    }
    else if(tok.type == TOK_NULL_KW)
    {
        e = newASTExprLit(tok);
        tok = lex();
    }
    else if(tok.type == ':')
    {
        e = blockExpr(false);
    }
    else if(tok.type == TOK_IFX_KW)
    {
        e = ifExpr();
    }
    else if(tok.type == TOK_WHILEX_KW)
    {
        e = whileExpr();
    }
    else if(tok.type == TOK_MATCHX_KW)
    {
        e = matchExpr();
    }
    else if(tok.type == '(')
    {
        Token startTok = tok;
        tok = lex();
        e = expr();

        if(tok.type == ',')
        {
            tok = lex();
            ASTExprLL *exprLL = exprList();
            insertBeforeASTExprLL(exprLL->first, newASTExprLL(e));

            e = newASTExprTupleLit(startTok, exprLL);
            e->endTok = tok;
        }

        if(tok.type == ')') tok = lex();
        else parserError(tok, "Expected a matching ')' but instead got %s", tok.lexeme);
    }
    else if((tok.type == TOK_TYPEOF_KW))
    {
        tok = lex();
        e = newASTExprExprIntro(primaryTerm());
    }
    else if((tok.type == TOK_ASSTRING_KW))
    {
        Token startTok = tok;
        tok = lex();
        e = newASTExprAsString(startTok, primaryTerm());
    }
    else if((tok.type == TOK_CAST_KW))
    {
        ASTType *castTo = NULL;

        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();
            castTo = typeSpec();
            if(tok.type == ')') 
            {
                tok = lex();
                e = newASTExprCast(castTo, unaryTerm());
            }
            else parserError(tok, "Expected a matching ')' but instead got %s", tok.lexeme);
        }
        else parserError(tok, "Expected a '(' after cast keyword %s", tok.lexeme);
    }
    else if((tok.type == '#'))
    {
        tok = lex();
        e = newASTExprTypeIntro(typeSpec());
    }
    else if((tok.type == '{'))
    {
        Token startTok = tok;

        tok = lex();
        ASTNamedExprLL *ll = namedExprList();

        if(tok.type == '}')
        {
            tok = lex();
        }
        else parserError(tok, "Expected matching '}' for struct literal but instead got %s", tok.lexeme);

        e = newASTExprStructunionLit(startTok, NULL, ll);

    }
    else if((tok.type == '['))
    {
        ASTExpr *sizeExpr = NULL;
        ASTType *baseType = NULL;
        ASTExprLL *ll = NULL;
        Token startTok = tok;
        
        tok = lex();
        if(tok.type != ']') sizeExpr = expr();

        if(tok.type == ']')
        {
            tok = lex();
            if(tok.type != '{') baseType = typeSpec();

            if(tok.type == '{')
            {
                tok = lex();
                ll = exprList();

                if(tok.type == '}') tok = lex();
                else parserError(tok, "Expected matching '}' for array literal, but instead got '%s'", tok.lexeme);
            }
            else parserError(tok, "Expected '{' for array literal but instead got %s", tok.lexeme);

            e = newASTExprArrayLit(startTok, sizeExpr, baseType, ll);
        }
        else parserError(tok, "Expected matching ']' for array literal - [size]basetype{expressions}, instead got '%s'", tok.lexeme);
    }
    else if((tok.type == '.'))
    {
        Token startTok = tok;
        Token iden;

        tok = lex();

        if(tok.type == TOK_IDEN)
        {
            iden = tok;
            tok = lex();

            e = newASTExprEnumInferLit(startTok, iden);
        }
        else parserError(tok, "Expected an identifer for enum infer literal, instead got '%s'", tok.lexeme);

    }
    else parserError(tok, "Expected a valid expression but got %s", tok.lexeme);
    return e;
}
ASTExpr *scopeAccessExprMaybeIdenExpr(void)
{
    ASTExpr *e = NULL;

    e = newASTExprIden(tok);
    tok = lex();

    while(tok.type == TOK_SCOPE_ACCESS)
    {
        tok = lex();
        if(tok.type == TOK_IDEN)
        {
            e = newASTExprScopeAccess(e, tok);
            tok = lex();
        }
        else parserError(tok, "Expected an identifier for scope access, instead got '%s'", tok.lexeme);
    }

    e->endTok = tok;
    return e;
}
//if the expression block belongs to an if expression or while or match expression then exprConstruct is true
ASTExpr *blockExpr(bool belongsToExprConstruct)
{
    Token startTok;
    ASTStmtLL *ll = NULL;
    ASTBlock *b = NULL;

    if(!belongsToExprConstruct && (tok.type != ':')) parserError(tok, "Expected ':' to begin block expression, instead got %s", tok.lexeme);
    
    startTok = tok;
    if(tok.type == ':') tok = lex();
    
    b = block();

    if(b->stmts != NULL)
    {
        if(b->stmts->item->kind != A_STMT_EXPR)
        {
            parserError(b->stmts->item->startTok, "Expected expression statement at the end of a block expression");
        }
    }
    else parserError(startTok, "Expected atleast one statement in block expression, but got none");    

    return newASTExprBlock(b);
}
ASTExpr *ifExpr(void)
{
    ASTStmt *ifS = NULL;
    ASTExpr *cond = NULL;
    ASTBlock *b = NULL;
    ASTBlock *eBlock = NULL;
    Token startTok;
    Token endTok;

    if(tok.type == TOK_IFX_KW)
    {
        startTok = tok;
        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();
            cond = expr();

            if(tok.type == ')')
            {
                tok = lex();

                b = blockExpr(true)->block.b;

                if(tok.type == TOK_ELSE_KW)
                {
                    tok = lex();
                    if(tok.type == TOK_IFX_KW) 
                    {
                        ASTStmt *s = newASTStmtExpr(ifExpr());

                        ASTStmtLL *stmtLL = newASTStmtLL(s);

                        eBlock = newASTBlock(s->startTok, stmtLL);

                    }
                    else eBlock = blockExpr(true)->block.b;
                }

                endTok = tok;
            }
            else parserError(tok, "Expected matching ')' after if expr condition but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected '(' after if kw but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected if kw to begin if expr but instead got %s", tok.lexeme);

    ifS = newASTStmtIf(startTok, endTok, cond, b, eBlock);
    ifS->endTok = tok;

    ASTExpr *e = newASTExprIf(ifS);
    e->endTok = tok;

    return e;
}
ASTExpr *whileExpr(void)
{
    ASTStmt *whileS = NULL;
    ASTExpr *cond = NULL;
    ASTBlock *b = NULL;

    if(tok.type == TOK_WHILEX_KW)
    {
        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();
            cond = expr();

            if(tok.type == ')')
            {
                tok = lex();

                b = blockExpr(true)->block.b;
            }
            else parserError(tok, "Expected matching ')' after while expr condition but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected '(' after while kw but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected while kw to begin while expr statement but instead got %s", tok.lexeme);

    whileS = newASTStmtWhile(cond, b);

    return newASTExprWhile(whileS);
}
ASTExpr *matchExpr(void)
{
    ASTStmt *matchS = NULL;
    ASTExprLL *exprLL = NULL;
    ASTMatchArmLL *armLL = NULL;

    Token startTok;

    if(tok.type == TOK_MATCHX_KW)
    {
        startTok = tok;

        tok = lex();
        if(tok.type == '(')
        {
            tok = lex();
            exprLL = exprList();

            if(tok.type == ')')
            {
                tok = lex();

                if(tok.type == '{')
                {
                    tok = lex();

                    armLL = matchExprArmList();

                    if(tok.type == '}') tok = lex();
                    else parserError(tok, "Expected matching '}' for match expr, got %s", tok.lexeme);
                }
                else parserError(tok, "Expected '{' for match expr, got %s", tok.lexeme);
            }
            else parserError(tok, "Expected matching ')' but got %s", tok.lexeme);
        }
        else parserError(tok, "Expected '(' after match kw but instead got %s", tok.lexeme);
    }
    else parserError(tok, "Expected match kw to begin match expr, got %s", tok.lexeme);

    matchS = newASTStmtMatch(exprLL, armLL, startTok);

    return newASTExprMatch(matchS);
}
ASTMatchArmLL *matchExprArmList(void)
{
    if(tok.type == '}') return NULL;

    ASTMatchArmLL *ll = NULL;
    ll = newASTMatchArmLL(matchExprArm());

    while((tok.type != TOK_EOF) && (tok.type != '}'))
    {
        addASTMatchArmlLL(&ll, matchExprArm());
    }

    return ll;
}
ASTMatchArm *matchExprArm(void)
{
    ASTExprLL **exprLLs = NULL;
    int exprLLsCount = 0;

    ASTBlock *b = NULL;
    Token startTok;


    if(tok.type == TOK_ELSE_KW)
    {
        startTok = tok;

        tok = lex();
        if(tok.type == TOK_RIGHT_ARROW)
        {
            tok = lex();
            b = blockExpr(true)->block.b;
        }
        else parserError(tok, "Expected '->' after match arm expressions, got %s", tok.lexeme);
        
        ASTMatchArm *arm = newASTMatchArm(startTok, exprLLs, 0, b);
        arm->endTok = tok;

        return arm;
    }

    exprLLs = malloc(sizeof(ASTExprLL *));
    
    startTok = tok;

    bool seenBracket = false;
    if(tok.type == '(') 
    {
        tok = lex();
        seenBracket = true;
    }

    if(!strcmp(tok.lexeme, "_"))
    {
        exprLLs[0] = newASTExprLL(expr());
    }
    else exprLLs[0] = exprList();
    exprLLsCount++;

    if(seenBracket) 
    {
        if(tok.type == ')') tok = lex();
        else parserError(tok, "Expected ')' to close arm expression list, instead got '%s'", tok.lexeme);
    }

    while((tok.type != TOK_EOF) && (tok.type != TOK_RIGHT_ARROW))
    {
        bool seenBracket = false;
        if(tok.type == '(') 
        {
            tok = lex();
            seenBracket = true;

        }

        tok = lex();
        if(!strcmp(tok.lexeme, "_"))
        {
            exprLLs = realloc(exprLLs, sizeof(ASTExprLL *) * (exprLLsCount + 1));
            exprLLs[exprLLsCount] = newASTExprLL(expr());
        }
        else 
        {
            exprLLs = realloc(exprLLs, sizeof(ASTExprLL *) * (exprLLsCount + 1));
            exprLLs[exprLLsCount] = exprList();
        }

        exprLLsCount++;

        if(seenBracket) 
        {
            if(tok.type == ')') tok = lex();
            else parserError(tok, "Expected ')' to close arm expression list, instead got '%s'", tok.lexeme);
        }
    }

    if(tok.type == TOK_RIGHT_ARROW)
    {
        tok = lex();
        b = blockExpr(true)->block.b;
    }
    else parserError(tok, "Expected '->' after match arm expressions, got %s", tok.lexeme);

    ASTMatchArm *arm = newASTMatchArm(startTok, exprLLs, exprLLsCount, b);
    arm->endTok = tok;

    return arm;
}

ASTExprLL *exprList(void)
{
    ASTExprLL *ll = NULL;

    ll = newASTExprLL(expr());

    while(tok.type == ',')
    {
        tok = lex();
        addASTExprLL(&ll, expr());
    }

    return ll;
}
ASTNamedExprLL *namedExprList(void)
{
    ASTNamedExprLL *ll = NULL;

    if(tok.type == '}' || tok.type == ')') return NULL;
    
    ll = newASTNamedExprLL(namedExpr());

    while(tok.type == ',')
    {
        tok = lex();
        addASTNamedExprLL(&ll, namedExpr());
    }

    return ll;
}
ASTNamedExpr *namedExpr(void)
{
    ASTNamedExpr *n = NULL;
    ASTExpr *e = NULL;
    bool isVariadicArrayArg = false;

    if(tok.type == TOK_2_DOTS)
    {
        tok = lex();
        isVariadicArrayArg = true;
    }

    e = expr();

    if(tok.type == '=')
    {
        if(isVariadicArrayArg)
        {
            parserError(tok, "Unexpected '..', since argument is being passed with a name, '..' should be before the rhs expression");
        }

        tok = lex();

        if(e->kind == A_EXPR_IDEN)
        {
            if(tok.type == TOK_2_DOTS)
            {
                tok = lex();
                isVariadicArrayArg = true;
            }

            n = newASTNamedExprNamed(e->iden, expr());
        }
        else parserError(e->startTok, "Expected an identifier to begin named expression");
    }
    else n = newASTNamedExprNormal(e);

    n->isVariadicArrayArg = isVariadicArrayArg;
    
    return n;
}
