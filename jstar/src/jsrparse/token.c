#include "jsrparse/token.h"

// clang-format off

const char *tokNames[] = {
    "(",
    ")",
    "[",
    "]",
    "{",
    "}",

    "!",
    "!=",
    ",",
    ".",
    "...",
    "=>",

    "=",
    "+=",
    "-=",
    "/=",
    "*=",
    "%=",

    "==",
    ">",
    ">=",
    "<",
    "<=",
    "+",
    "-",
    "/",
    "*",
    "%",
    "^",
    "#",
    "##",

    ":",
    "|",
    ";",
    "in",

    "then",
    "do",
    "begin",
    "end",

    "IDENTIFIER",
    "STRING",
    "NUMBER",

    "and",
    "class",
    "else",
    "false",
    "native",
    "fun",
    "for",
    "if",
    "elif",
    "null",
    "or",
    "print",
    "return",
    "import",
    "as",
    "is",
    "super",
    "true",
    "var",
    "while",
    "continue",
    "break",

    "try",
    "except",
    "ensure",
    "raise",
    "with",

    "unterminated string",
    "newline",
    "error",
    "end of file"
};

// clang-format on