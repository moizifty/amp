#include "lexer.h"
#include "compiler_internal.h"

int ch = ' ';
int linenum = 1;
int colnum = 0;

int numFileBuffers = 0;
LexFileBuffer *fileBuffers = NULL;
LexFileBuffer fileBuffer = NULL;
LexFileBuffer currentLocInFileBuffer;

extern GlobalContext globalContext;

void lexerError(char *msg, ...)
{
    va_list args;
    va_start(args, msg);
    HANDLE hConsole = GetStdHandle(STD_ERROR_HANDLE);

    CONSOLE_SCREEN_BUFFER_INFO consoleInfo;
    WORD savedAttributes;

    /* Save current attributes */
    GetConsoleScreenBufferInfo(hConsole, &consoleInfo);
    savedAttributes = consoleInfo.wAttributes;

    fprintf(stderr, "\033[1m\033[4m%s (%d: %d)\033[0m", globalContext.pc.currFileName, linenum, colnum);

    fprintf(stderr, "\033[1m\n        --> Lexer Error:\033[0m ");
    vfprintf(stderr, msg, args);
    fprintf(stderr, "\n        |\n");

    const int NUM_LINES = 3;
    

    int currLine = 0;
    LexFileBuffer currCh = currentLocInFileBuffer;
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

    if(*currCh == '\n') 
    {
        currLine++;
        currCh++;
    }

     for(; currLine < NUM_LINES; currLine++)
    {
        printf("%7d | ", currLine + linenum);

        while((*currCh != '\n') && (*currCh != EOF))
        {
            if(currLine == 0) colnum++;

            if(currCh == currentLocInFileBuffer) SetConsoleTextAttribute(hConsole, FOREGROUND_INTENSITY | FOREGROUND_RED);
            else if(currCh > currentLocInFileBuffer) SetConsoleTextAttribute(hConsole, savedAttributes);

            putchar((char) (*currCh++));
        }

        SetConsoleTextAttribute(hConsole, savedAttributes);
        
        if(*currCh != EOF) currCh++;
        else break;
        putchar('\n');
    }

    fprintf(stderr, "\n");

    va_end(args);

    exit(EXIT_FAILURE);
}

void advanceChar()
{
    if(ch == '\n')
    {
       linenum++;
       colnum = 0;
    }

    if((ch != '\n') && (ch != '\f') && (ch != '\r'))
        colnum++;

    ch = *currentLocInFileBuffer;

    currentLocInFileBuffer++;
}
bool isCharValidEscapeSequence(int ch)
{
    switch(ch)
    {
        case '0':
        case '\\':
        case '\'':
        case '\"':
        case 'n':
        case 'r':
        case 'b':
        case 'a':
        case 'f':
        case 'v':
        case 'e':
        case 't': return true;
    }

    return false;
}

LexFileBuffer allocReadSourceFileIntoBuffer(char *fileName)
{
    size_t fileSize = 0;
    int *buffer = NULL;
    size_t index = 0;

    FILE *fp = fopen(fileName, "r");
    if(fp == NULL)
        return NULL;
        
    fseek(fp, 0L, SEEK_END);
    fileSize = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    buffer = malloc(sizeof(int) * fileSize);
    
    while((buffer[index++] = getc(fp)) != EOF);

    fclose(fp);
    return buffer;
}

bool initLexer(char *fileName)
{
    if(fileBuffers == NULL) fileBuffers = malloc(sizeof(LexFileBuffer));
    else fileBuffers = realloc(fileBuffers, sizeof(LexFileBuffer) * (numFileBuffers + 1));

    LexFileBuffer fb = allocReadSourceFileIntoBuffer(fileName);
    if(fb == NULL) return false;

    fileBuffers[numFileBuffers++] = fb;


    fileBuffer = fileBuffers[numFileBuffers - 1];

    currentLocInFileBuffer = fileBuffer;

    resetLexer();
    
    return true;
}
Token lex(void)
{
LEX_START:
    {
        while(isspace(ch))
        {
            advanceChar();
        }

        Token t = {.lexeme = {0}, .type = 0};
        int *bufPos = currentLocInFileBuffer - 1;
        int fileBufIndex = numFileBuffers - 1;

        if(ch == '+')
        {
            advanceChar();
            if(ch == '+')
            {
                advanceChar();
                strcpy(t.lexeme, "++");
                t.type = TOK_INC_OP;
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufferIndex = fileBufIndex,
                    .fileBufPos = bufPos,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }
            t.lexeme[0] = '+';
            t.lexeme[1] = '\0';
            t.type = '+';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufferIndex = fileBufIndex,
                .fileBufPos = bufPos,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == '<')
        {
            advanceChar();
            if(ch == '=')
            {
                strcpy(t.lexeme, "<=");
                t.type = TOK_LE_RELOP;

                advanceChar();
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }
            else if(ch == '<')
            {
                strcpy(t.lexeme, "<<");
                t.type = TOK_LEFT_SHIFT;

                advanceChar();
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }
            t.lexeme[0] = '<';
            t.lexeme[1] = '\0';
            t.type = '<';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);
            
            return t;
        }
        else if(ch == '>')
        {
            advanceChar();
            if(ch == '=')
            {
                strcpy(t.lexeme, ">=");
                t.type = TOK_GE_RELOP;

                advanceChar();
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }
            else if(ch == '>')
            {
                strcpy(t.lexeme, ">>");
                t.type = TOK_RIGHT_SHIFT;

                advanceChar();
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }
            t.lexeme[0] = '>';
            t.lexeme[1] = '\0';
            t.type = '>';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == '=')
        {
            advanceChar();
            if(ch == '=')
            {
                advanceChar();
                strcpy(t.lexeme, "==");
                t.type = TOK_EQ_RELOP;
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }
            t.lexeme[0] = '=';
            t.lexeme[1] = '\0';
            t.type = '=';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == '!')
        {
            advanceChar();
            if(ch == '=')
            {
                advanceChar();
                strcpy(t.lexeme, "!=");
                t.type = TOK_NEQ_RELOP;
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }

            t.lexeme[0] = '!';
            t.lexeme[1] = '\0';
            t.type = '!';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == '-')
        {
            advanceChar();
            if(ch == '>')
            {
                advanceChar();
                strcpy(t.lexeme, "->");
                t.type = TOK_RIGHT_ARROW;
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }
            else if(ch == '-')
            {
                advanceChar();
                strcpy(t.lexeme, "--");
                t.type = TOK_DEC_OP;
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }
            t.lexeme[0] = '-';
            t.lexeme[1] = '\0';
            t.type = '-';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == '&')
        {
            advanceChar();
            if(ch == '&')
            {
                advanceChar();
                strcpy(t.lexeme, "&&");
                t.type = TOK_AND_LOGOP;
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }

            t.lexeme[0] = '&';
            t.lexeme[1] = '\0';
            t.type = '&';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == '|')
        {
            advanceChar();
            if(ch == '|')
            {
                advanceChar();
                strcpy(t.lexeme, "||");
                t.type = TOK_OR_LOGOP;
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }

            t.lexeme[0] = '|';
            t.lexeme[1] = '\0';
            t.type = '|';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == ':')
        {
            advanceChar();
            if(ch == ':')
            {
                advanceChar();
                strcpy(t.lexeme, "::");
                t.type = TOK_SCOPE_ACCESS;
                t.pos = (TokenPos)
                {
                    .linenum = linenum,
                    .colnum = colnum,
                    .fileBufPos = bufPos,
                    .fileBufferIndex = fileBufIndex,
                };
                strcpy(t.pos.filename, globalContext.pc.currFileName);

                return t;
            }

            t.lexeme[0] = ':';
            t.lexeme[1] = '\0';
            t.type = ':';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == '/')
        {
            advanceChar();
            if(ch == '/')
            {
                while( (ch != '\n') && (ch != EOF))
                    advanceChar();

                if(ch == EOF)
                {
                    fprintf(stderr, "line %d, comment ends with end of file, BAD!!\n", linenum);
                    exit(EXIT_FAILURE);
                }

                goto LEX_START;
            }
            else if(ch == '*')
            {
                int startcommentTokenCount = 1;
                int endcommentTokenCount = 0;

                do
                {
                    advanceChar();
                    if(ch == '*')
                    {
                        advanceChar();
                        if(ch == '/')
                            endcommentTokenCount++;
                    }
                    else if(ch == '/')
                    {
                        advanceChar();
                        if(ch == '*')
                            startcommentTokenCount++;
                    }

                }while((startcommentTokenCount != endcommentTokenCount) && (ch != EOF));
                if(ch != EOF)
                    advanceChar();
                else
                {
                    fprintf(stderr, "line %d, multiline comments  ends with end of file, BAD!!, it should end with '*/'\n", linenum);
                    exit(EXIT_FAILURE);
                }
                goto LEX_START;
            }
            else
            {
                t.lexeme[0] = '/';
                t.lexeme[1] = '\0';
                t.type = '/';
            }
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);
            
            return t;
        }
        else if(isalpha(ch) || (ch == '_'))
        {
            int len = 0;

            do
            {
                if(len < MAX_IDEN_LEN)
                    t.lexeme[len++] = ch;
                advanceChar();
            }while(isalnum(ch) || (ch == '_'));

            t.lexeme[len] = '\0';
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);
            
            if(!strcmp(t.lexeme, "int"))
            {
                t.type = TOK_INT_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "void"))
            {
                t.type = TOK_VOID_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "i8"))
            {
                t.type = TOK_I8_KW;
                return t;
            }else if(!strcmp(t.lexeme, "i16"))
            {
                t.type = TOK_I16_KW;
                return t;
            }else if(!strcmp(t.lexeme, "i32"))
            {
                t.type = TOK_I32_KW;
                return t;
            }else if(!strcmp(t.lexeme, "i64"))
            {
                t.type = TOK_I64_KW;
                return t;
            }else if(!strcmp(t.lexeme, "ui8"))
            {
                t.type = TOK_UI8_KW;
                return t;
            }else if(!strcmp(t.lexeme, "ui16"))
            {
                t.type = TOK_UI16_KW;
                return t;
            }else if(!strcmp(t.lexeme, "ui32"))
            {
                t.type = TOK_UI32_KW;
                return t;
            }else if(!strcmp(t.lexeme, "ui64"))
            {
                t.type = TOK_UI64_KW;
                return t;
            }else if(!strcmp(t.lexeme, "float"))
            {
                t.type = TOK_FLOAT_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "f32"))
            {
                t.type = TOK_F32_KW;
                return t;
            }else if(!strcmp(t.lexeme, "f64"))
            {
                t.type = TOK_F64_KW;
                return t;
            }
            // else if(!strcmp(t.lexeme, "string"))
            // {
            //     t.type = TOK_STRING_KW;
            //     return t;
            // }
            else if(!strcmp(t.lexeme, "bool"))
            {
                t.type = TOK_BOOL_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "var"))
            {
                t.type = TOK_VAR_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "const"))
            {
                t.type = TOK_CONST_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "typealias"))
            {
                t.type = TOK_TYPEALIAS_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "as"))
            {
                t.type = TOK_AS_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "struct"))
            {
                t.type = TOK_STRUCT_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "union"))
            {
                t.type = TOK_UNION_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "enum"))
            {
                t.type = TOK_ENUM_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "tuple"))
            {
                t.type = TOK_TUPLE_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "while"))
            {
                t.type = TOK_WHILE_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "return"))
            {
                t.type = TOK_RET_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "defer"))
            {
                t.type = TOK_DEFER_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "match"))
            {
                t.type = TOK_MATCH_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "for"))
            {
                t.type = TOK_FOR_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "in"))
            {
                t.type = TOK_IN_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "null"))
            {
                t.type = TOK_NULL_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "typeof"))
            {
                t.type = TOK_TYPEOF_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "asstring"))
            {
                t.type = TOK_ASSTRING_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "import"))
            {
                t.type = TOK_IMPORT_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "namespace"))
            {
                t.type = TOK_NAMESPACE_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "using"))
            {
                t.type = TOK_USING_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "break"))
            {
                t.type = TOK_BREAK_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "continue"))
            {
                t.type = TOK_CONTINUE_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "operator"))
            {
                t.type = TOK_OPERATOR_KW;
                return t;
            }
            // else if(!strcmp(t.lexeme, "foreign"))
            // {
            //     t.type = TOK_FOREIGN_KW;
            //     return t;
            // }
            else if(!strcmp(t.lexeme, "distinct"))
            {
                t.type = TOK_DISTINCT_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "cast"))
            {
                t.type = TOK_CAST_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "if"))
            {
                t.type = TOK_IF_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "else"))
            {
                t.type = TOK_ELSE_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "true") || !strcmp(t.lexeme, "false"))
            {
                t.type = TOK_BOOL_LITERAL;
                return t;
            }
            else if(!strcmp(t.lexeme, "ifx"))
            {
                t.type = TOK_IFX_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "whilex"))
            {
                t.type = TOK_WHILEX_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "matchx"))
            {
                t.type = TOK_MATCHX_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "method"))
            {
                t.type = TOK_METHOD_KW;
                return t;
            }
            else if(!strcmp(t.lexeme, "with"))
            {
                t.type = TOK_WITH_KW;
                return t;
            }
            else
            {
                t.type = TOK_IDEN;
                return t;
            }
        }
        else if(isdigit(ch))
        {
            int len = 0;
            bool seenDot = false;
            bool parsedHexDigit = false;
            bool parsedBinDigit = false;

            if(ch == '0')
            {
                t.lexeme[len++] = ch;
                advanceChar();

                if(ch == 'x')
                {
                    //parse hex number
                    parsedHexDigit = true;
                    do
                    {       
                        if(len < MAX_IDEN_LEN)
                            t.lexeme[len++] = ch;
                        advanceChar();

                        while(ch == '_') advanceChar(); 
                    }while(isxdigit(ch));
                }
                else if(ch == 'b')
                {
                    parsedBinDigit = true;
                    do
                    {       
                        if(len < MAX_IDEN_LEN)
                            t.lexeme[len++] = ch;
                        advanceChar();

                        while(ch == '_') advanceChar(); 
                    }while((ch == '0') || (ch == '1'));
                }
            }

            while(isdigit(ch) || (ch == '.'))
            {
                if(!seenDot && (ch == '.'))
                    seenDot = true;
                
                else if(ch == '.' && seenDot)
                    break;

                if(len < MAX_IDEN_LEN)
                    t.lexeme[len++] = ch;
                
                advanceChar();
                
                while(ch == '_') advanceChar(); 
            }

            t.lexeme[len] = '\0';    
            t.type = (seenDot) ? TOK_FLOAT_LITERAL : TOK_INT_LITERAL;
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == '\"' || ch == '$')
        {
            int len = 0;
            bool isMultiline = ch == '$';
            bool seenBackslash = false;
            if(isMultiline)
            {
                advanceChar();
                if(ch != '\"') lexerError("Expected a '\"' after '$' to begin string, got '%c'", ch);
            }
            do
            {
                if(len < MAX_IDEN_LEN)
                {
                    if(ch == '\n')
                    {
                        if(isMultiline)
                        {
                            t.lexeme[len++] = '\\';
                            t.lexeme[len++] = 'n';
                        }
                        else
                        {
                            lexerError("Beginning new line in string without terminating string literal, If you intend on a multiline string preface the string with '$'");
                        }
                    }
                    else
                    {
                        t.lexeme[len++] = ch;
                    }
                }

                advanceChar();

                seenBackslash = false;
                if(ch == '\\')
                {
                    seenBackslash = true;
                    t.lexeme[len++] = ch;

                    advanceChar();

                    bool validEscapeSeq = isCharValidEscapeSequence(ch);
                    
                    if(!validEscapeSeq) lexerError("Expected a valid escape sequence in string, but got '\\%c'", ch);
                    else if(ch == '\"')  //to avoid while loop stopping
                    {
                        t.lexeme[len++] = ch;
                        advanceChar();
                    }
                }
            }while((ch != '\"') && (ch != EOF));

            if(ch != '\"')
                lexerError("Expected a ending '\"' for string literal");

            t.lexeme[len++] = '\"';    
            t.lexeme[len] = '\0';    
            t.type = TOK_STRING_LITERAL;
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            advanceChar();

            return t;
        }
        else if(ch == '\'')
        {
            int len = 0;

            t.lexeme[len++] = ch;

            advanceChar();
            
            if(ch == '\\')
            {
                advanceChar();
                t.lexeme[len++] = '\\';

                switch(ch)
                {
                    case '0': t.lexeme[len++] = ch; break;
                    case '\\': t.lexeme[len++] = ch; break;
                    case '\'': t.lexeme[len++] = ch; break;
                    case '\"': t.lexeme[len++] = ch; break;
                    case 'n': t.lexeme[len++] = ch; break;
                    case 'r': t.lexeme[len++] = ch; break;
                    case 'b': t.lexeme[len++] = ch; break;
                    case 'a': t.lexeme[len++] = ch; break;
                    case 'f': t.lexeme[len++] = ch; break;
                    case 'v': t.lexeme[len++] = ch; break;
                    case 'e': t.lexeme[len++] = ch; break;
                    case 't': t.lexeme[len++] = ch; break;
                    default: lexerError("Unexpected escape character '\\%c'", ch);
                }

            }
            else t.lexeme[len++] = ch;

            advanceChar();

            if(ch == '\'')
            {
                t.lexeme[len++] = ch;
                advanceChar();
            }
            else lexerError("Expected a ending '\'' for character literal");

            t.lexeme[len] = '\0';    
            t.type = TOK_CHAR_LITERAL;
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }
        else if(ch == EOF)
        {
            t.type = TOK_EOF;
            strcpy(t.lexeme, "End of File");
            t.pos = (TokenPos)
            {
                .linenum = linenum,
                .colnum = colnum,
                .fileBufPos = bufPos,
                .fileBufferIndex = fileBufIndex,
            };
            strcpy(t.pos.filename, globalContext.pc.currFileName);

            return t;
        }

        t.lexeme[0] = ch;
        t.lexeme[1] = '\0';
        t.type = ch;
        t.pos = (TokenPos)
        {
            .linenum = linenum,
            .colnum = colnum,
            .fileBufPos = bufPos,
            .fileBufferIndex = fileBufIndex,
        };
        
        strcpy(t.pos.filename, globalContext.pc.currFileName);

        advanceChar();
        
        return t;
    }
}

extern FILE *currFile;
extern Token tok;

bool tryLex(Token *t, TokType expectedTok)
{
    bool success = false;

    Token temp = *t;
    char tempCh = ch;
    
    TokenPos tempTokPos = {.linenum = linenum, .colnum = colnum, .fileBufPos = currentLocInFileBuffer};

    *t = lex();
    success = (t->type == expectedTok);

    if(!success)
    {
        currentLocInFileBuffer = tempTokPos.fileBufPos;

        *t = temp;
        ch = tempCh;
        linenum = tempTokPos.linenum;
        colnum = tempTokPos.colnum;
    }
    
    return success;
}
bool peekLex(Token *t, TokType expectedTok)
{
    return peekAmountLex(t, expectedTok, 1);
}

bool peekAmountLex(Token *t, TokType expectedTok, int tokenLookAheadAmount)
{
    bool success = false;

    Token temp = *t;
    char tempCh = ch;

    TokenPos tempTokPos = {.linenum = linenum, .colnum = colnum, .fileBufPos = currentLocInFileBuffer};

    for(int i = 0; i < tokenLookAheadAmount; i++)
    {
        *t = lex();
    }

    success = (t->type == expectedTok);

    currentLocInFileBuffer = tempTokPos.fileBufPos;
    *t = temp;
    ch = tempCh;
    linenum = tempTokPos.linenum;
    colnum = tempTokPos.colnum;
    
    return success;
}

void resetLexer()
{
    ch = ' ';
    linenum = 1;
    colnum = 0;
}