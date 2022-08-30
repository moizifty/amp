#ifndef LEXER_H
#define LEXER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stdarg.h>

#define MAX_IDEN_LEN 255

typedef int * LexFileBuffer;

typedef enum
{
    TOK_VAR_KW = 256,
    TOK_CONST_KW,
    TOK_TYPEALIAS_KW,
    TOK_AS_KW,
    TOK_IF_KW,
    TOK_ELSE_KW,
    TOK_WHILE_KW,
    TOK_RET_KW,
    TOK_DEFER_KW,
    TOK_OPERATOR_KW,
    TOK_FOREIGN_KW,
    TOK_DISTINCT_KW,
    TOK_MATCH_KW,
    TOK_TYPEOF_KW,
    TOK_ASSTRING_KW,
    TOK_CAST_KW,
    TOK_IMPORT_KW,
    TOK_NAMESPACE_KW,
    TOK_USING_KW,
    TOK_BREAK_KW,
    TOK_CONTINUE_KW,
    TOK_FOR_KW,
    TOK_IN_KW,
    TOK_METHOD_KW,
    
    TOK_IFX_KW,
    TOK_WHILEX_KW,
    TOK_MATCHX_KW,

    TOK_NULL_KW,
    
    TOK_STRUCT_KW,
    TOK_UNION_KW,
    TOK_ENUM_KW,
    TOK_TUPLE_KW,

    TOK_VOID_KW,

    TOK_INT_KW,
    TOK_I8_KW,
    TOK_I16_KW,
    TOK_I32_KW,
    TOK_I64_KW,
    TOK_UI8_KW,
    TOK_UI16_KW,
    TOK_UI32_KW,
    TOK_UI64_KW,

    TOK_FLOAT_KW,
    TOK_F32_KW,
    TOK_F64_KW,

    //TOK_STRING_KW,
    TOK_BOOL_KW,

    TOK_IDEN,
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL, // TODO make lexer read these
    TOK_BOOL_LITERAL,
    TOK_STRING_LITERAL,
    TOK_CHAR_LITERAL,

    TOK_AND_LOGOP,
    TOK_OR_LOGOP,
    TOK_LE_RELOP,
    TOK_GE_RELOP,
    TOK_EQ_RELOP,
    TOK_NEQ_RELOP,

    TOK_INC_OP,
    TOK_DEC_OP,

    TOK_RIGHT_SHIFT,
    TOK_LEFT_SHIFT,
    TOK_RIGHT_ARROW,

    TOK_SCOPE_ACCESS,
    TOK_WITH_KW,
    TOK_EOF,
}TokType;

typedef struct
{
    char filename[FILENAME_MAX + 1];
    size_t linenum;
    int colnum;

    int fileBufferIndex;
    int *fileBufPos;
}TokenPos;

typedef struct
{
    char lexeme[MAX_IDEN_LEN + 1];
    TokType type;
    TokenPos pos;
}Token;

void lexerError(char *msg, ...);

void advanceChar();
bool isCharValidEscapeSequence(int ch);
LexFileBuffer allocReadSourceFileIntoBuffer(char *fileName);

bool initLexer(char *fileName);

Token lex(void);
bool tryLex(Token *t, TokType expectedTok);
bool peekLex(Token *t, TokType expectedTok);
bool peekAmountLex(Token *t, TokType expectedTok, int tokenLookAheadAmount);

void resetLexer();

#endif