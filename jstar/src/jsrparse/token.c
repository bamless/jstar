const char *TokenNames[] = {
#define TOKEN(tok, name) name,
#include "jsrparse/token.def"
};
