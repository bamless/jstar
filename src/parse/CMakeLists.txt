include_directories("${PROJECT_SOURCE_DIR}/parse")

add_library(blparse
    linkedlist.c
    ast.c
    lex.c
    token.c
    parser.c
)