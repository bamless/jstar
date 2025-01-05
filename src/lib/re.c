// This file is heavily inspired by lstrlib.c of the LUA project.
// See copyright notice at the end of the file.

#include "re.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object.h"
#include "object_types.h"
#include "value.h"
#include "vm.h"

#define ESCAPE             '%'
#define MAX_CAPTURES       31
#define MAX_ERROR          256
#define CAPTURE_UNFINISHED -1
#define CAPTURE_POSITION   -2

typedef struct Substring {
    const char* start;
    ptrdiff_t length;
} Substring;

typedef struct RegexState {
    const char *string, *end;
    bool hadError;
    char errorMessage[MAX_ERROR];
    int captureCount;
    Substring captures[MAX_CAPTURES];
} RegexState;

static void initState(RegexState* rs, const char* string, size_t length) {
    rs->string = string;
    rs->end = string + length;
    rs->hadError = false;
    rs->captureCount = 1;
    rs->captures[0].start = string;
    rs->captures[0].length = CAPTURE_UNFINISHED;
}

static bool hadError(RegexState* rs) {
    return rs->hadError;
}

static void setError(RegexState* rs, const char* fmt, ...) {
    if(rs->hadError) return;
    rs->hadError = true;
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(rs->errorMessage, MAX_ERROR, fmt, args);
    va_end(args);
    JSR_ASSERT(written < MAX_ERROR, "Error message was truncated");
    (void)written;
}

static const char* getError(RegexState* rs) {
    return rs->errorMessage;
}

// -----------------------------------------------------------------------------
// MATCHING ENGINE
// -----------------------------------------------------------------------------

// Forward declaration as the regex matching algorithm is mutually recursive
static const char* match(RegexState* rs, const char* str, const char* regex);

static bool isAtEnd(const char* ptr) {
    return *ptr == '\0';
}

static bool matchClass(char c, char cls) {
    bool res;
    switch(tolower(cls)) {
    case 'a':
        res = isalpha(c);
        break;
    case 'c':
        res = iscntrl(c);
        break;
    case 'd':
        res = isdigit(c);
        break;
    case 'l':
        res = islower(c);
        break;
    case 'p':
        res = ispunct(c);
        break;
    case 's':
        res = isspace(c);
        break;
    case 'u':
        res = isupper(c);
        break;
    case 'w':
        res = isalnum(c);
        break;
    case 'x':
        res = isxdigit(c);
        break;
    default:
        return c == cls;
    }
    return isupper(cls) ? !res : res;
}

static bool matchCustomClass(char c, const char* regexPtr, const char* classEnd) {
    bool ret = true;
    if(regexPtr[1] == '^') {
        ret = false;
        regexPtr++;
    }

    while(++regexPtr < classEnd) {
        if(*regexPtr == ESCAPE) {
            regexPtr++;
            if(matchClass(c, *regexPtr)) {
                return ret;
            }
        } else if(regexPtr[1] == '-' && regexPtr + 2 < classEnd) {
            regexPtr += 2;
            if(regexPtr[-2] <= c && c <= *regexPtr) {
                return ret;
            }
        } else if(*regexPtr == c) {
            return ret;
        }
    }

    return !ret;
}

static bool matchClassOrChar(char c, const char* regex, const char* classEnd) {
    switch(*regex) {
    case '.':
        return true;
    case ESCAPE:
        return matchClass(c, regex[1]);
    case '[':
        return matchCustomClass(c, regex, classEnd - 1);
    default:
        return c == *regex;
    }
}

static int finishCaptures(RegexState* rs) {
    for(int i = rs->captureCount - 1; i > 0; i--) {
        if(rs->captures[i].length == CAPTURE_UNFINISHED) return i;
    }
    setError(rs, "Invalid regex capture");
    return -1;
}

static const char* startCapture(RegexState* rs, const char* stringPtr, const char* regexPtr) {
    if(rs->captureCount >= MAX_CAPTURES) {
        setError(rs, "Max capture number exceeded: %d", MAX_CAPTURES);
        return NULL;
    }

    if(regexPtr[1] != ')') {
        rs->captures[rs->captureCount].length = CAPTURE_UNFINISHED;
    } else {
        rs->captures[rs->captureCount].length = CAPTURE_POSITION;
        regexPtr++;
    }

    rs->captures[rs->captureCount++].start = stringPtr;
    const char* res = match(rs, stringPtr, regexPtr + 1);
    if(res == NULL) {
        rs->captureCount--;
    }

    return res;
}

static const char* endCapture(RegexState* rs, const char* stringPtr, const char* regexPtr) {
    int i = finishCaptures(rs);
    if(i == -1) return NULL;

    rs->captures[i].length = stringPtr - rs->captures[i].start;
    const char* res = match(rs, stringPtr, regexPtr + 1);
    if(res == NULL) {
        rs->captures[i].length = CAPTURE_UNFINISHED;
    }

    return res;
}

static const char* matchCapture(RegexState* rs, const char* stringPtr, int captureIdx) {
    if(captureIdx > rs->captureCount - 1 || rs->captures[captureIdx].length == CAPTURE_UNFINISHED ||
       rs->captures[captureIdx].length == CAPTURE_POSITION) {
        setError(rs, "Invalid capture index %%%d", captureIdx);
        return NULL;
    }

    const char* capture = rs->captures[captureIdx].start;
    size_t captureLen = rs->captures[captureIdx].length;
    if((size_t)(rs->end - stringPtr) < captureLen || memcmp(stringPtr, capture, captureLen) != 0) {
        return NULL;
    }

    return stringPtr + captureLen;
}

static const char* greedyMatch(RegexState* rs, const char* stringPtr, const char* regexPtr,
                               const char* clsEnd) {
    ptrdiff_t i = 0;
    while(!isAtEnd(&stringPtr[i]) && matchClassOrChar(stringPtr[i], regexPtr, clsEnd)) {
        i++;
    }

    while(i >= 0) {
        const char* res = match(rs, stringPtr + i, clsEnd + 1);
        if(res != NULL) {
            return res;
        }
        if(rs->hadError) {
            return NULL;
        }
        i--;
    }

    return NULL;
}

static const char* lazyMatch(RegexState* rs, const char* stringPtr, const char* regexPtr,
                             const char* clsEnd) {
    do {
        const char* res = match(rs, stringPtr, clsEnd + 1);
        if(res != NULL) {
            return res;
        }
        if(rs->hadError) {
            return NULL;
        }
    } while(!isAtEnd(stringPtr) && matchClassOrChar(*stringPtr++, regexPtr, clsEnd));

    return NULL;
}

static const char* findClassEnd(RegexState* rs, const char* regexPtr) {
    switch(*regexPtr++) {
    case ESCAPE:
        if(isAtEnd(regexPtr)) {
            setError(rs, "Malformed regex, unmatched `%c`", ESCAPE);
            return NULL;
        }
        return regexPtr + 1;
    case '[':
        do {
            if(isAtEnd(regexPtr)) {
                setError(rs, "Malformed regex, unmatched `[`");
                return NULL;
            }
            if(*regexPtr++ == ESCAPE && !isAtEnd(regexPtr)) {
                regexPtr++;
            }
        } while(*regexPtr != ']');

        return regexPtr + 1;
    default:
        return regexPtr;
    }
}

static const char* matchRepOperator(RegexState* rs, const char* stringPtr, const char* regexPtr) {
    const char* classEnd = findClassEnd(rs, regexPtr);
    if(classEnd == NULL) return NULL;

    bool isMatch = !isAtEnd(stringPtr) && matchClassOrChar(*stringPtr, regexPtr, classEnd);
    switch(*classEnd) {
    case '?': {
        const char* res;
        if(isMatch && (res = match(rs, stringPtr + 1, classEnd + 1)) != NULL) return res;
        return match(rs, stringPtr, classEnd + 1);
    }
    case '+':
        return isMatch ? greedyMatch(rs, stringPtr + 1, regexPtr, classEnd) : NULL;
    case '*':
        return greedyMatch(rs, stringPtr, regexPtr, classEnd);
    case '-':
        return lazyMatch(rs, stringPtr, regexPtr, classEnd);
    default:
        return isMatch ? match(rs, stringPtr + 1, classEnd) : NULL;
    }
}

static const char* match(RegexState* rs, const char* stringPtr, const char* regexPtr) {
    switch(*regexPtr) {
    case '\0':
        return stringPtr;
    case '(':
        return startCapture(rs, stringPtr, regexPtr);
    case ')':
        return endCapture(rs, stringPtr, regexPtr);
    case '$':
        // Treat `$` specially only if at string end
        if(isAtEnd(&regexPtr[1])) {
            return isAtEnd(stringPtr) ? stringPtr : NULL;
        }
        return matchRepOperator(rs, stringPtr, regexPtr);
    case ESCAPE:
        // If there are digits after a `%`, then it's a capture reference
        if(isdigit(regexPtr[1])) {
            int digitCount = 1;
            while(isdigit(regexPtr[digitCount])) {
                digitCount++;
            }

            int capture = strtol(regexPtr + 1, NULL, 10);
            if((stringPtr = matchCapture(rs, stringPtr, capture)) == NULL) {
                return NULL;
            }

            return match(rs, stringPtr, regexPtr + digitCount);
        }
        return matchRepOperator(rs, stringPtr, regexPtr);
    default:
        return matchRepOperator(rs, stringPtr, regexPtr);
    }
}

// Entry point of the regex matching algorithm
static bool matchRegex(RegexState* rs, const char* str, size_t len, const char* regex, int offset) {
    initState(rs, str, len);

    // negative offsets start from end of string
    if(offset < 0) {
        offset += len;
    }

    if(offset < 0 || (size_t)offset > len) {
        setError(rs, "Invalid starting offset: %d", offset);
        return false;
    }

    str += offset;

    if(*regex == '^') {
        const char* res = match(rs, str, regex + 1);
        if(res != NULL) {
            rs->captures[0].length = res - str;
            return true;
        }
        return false;
    }

    do {
        const char* res = match(rs, str, regex);
        if(res != NULL) {
            rs->captures[0].start = str;
            rs->captures[0].length = res - str;
            return true;
        }
        if(rs->hadError) return false;
    } while(!isAtEnd(str++));

    return false;
}

// -----------------------------------------------------------------------------
// J* NATIVES AND HELPER FUNCTIONS
// -----------------------------------------------------------------------------

typedef enum FindRes {
    FIND_ERR,
    FIND_MATCH,
    FIND_NOMATCH,
} FindRes;

static FindRes find(JStarVM* vm, RegexState* rs) {
    if(!jsrCheckString(vm, 1, "str") || !jsrCheckString(vm, 2, "regex") ||
       !jsrCheckInt(vm, 3, "off")) {
        return FIND_ERR;
    }

    size_t len = jsrGetStringSz(vm, 1);
    const char* string = jsrGetString(vm, 1);
    const char* regex = jsrGetString(vm, 2);
    double offset = jsrGetNumber(vm, 3);

    if(!matchRegex(rs, string, len, regex, offset)) {
        if(hadError(rs)) {
            jsrRaise(vm, "RegexException", "%s\n", getError(rs));
            return FIND_ERR;
        }
        jsrPushNull(vm);
        return FIND_NOMATCH;
    }

    return FIND_MATCH;
}

static bool pushCapture(JStarVM* vm, RegexState* rs, int captureIdx) {
    if(captureIdx < 0 || captureIdx >= rs->captureCount) {
        JSR_RAISE(vm, "RegexException", "Invalid capture index %%%d", captureIdx);
    }
    if(rs->captures[captureIdx].length == CAPTURE_UNFINISHED) {
        JSR_RAISE(vm, "RegexException", "Unfinished capture");
    }

    if(rs->captures[captureIdx].length == CAPTURE_POSITION) {
        jsrPushNumber(vm, rs->captures[captureIdx].start - rs->string);
    } else {
        jsrPushStringSz(vm, rs->captures[captureIdx].start, rs->captures[captureIdx].length);
    }

    return true;
}

JSR_NATIVE(jsr_re_match) {
    RegexState rs;
    FindRes res = find(vm, &rs);

    // An exception was thrown, return error
    if(res == FIND_ERR) return false;

    // No match could be found, return succesfully
    if(res == FIND_NOMATCH) return true;

    if(rs.captureCount <= 2) {
        if(!pushCapture(vm, &rs, rs.captureCount - 1)) return false;
    } else {
        ObjTuple* ret = newTuple(vm, rs.captureCount - 1);
        push(vm, OBJ_VAL(ret));

        for(int i = 1; i < rs.captureCount; i++) {
            if(!pushCapture(vm, &rs, i)) return false;
            ret->arr[i - 1] = pop(vm);
        }
    }

    return true;
}

JSR_NATIVE(jsr_re_find) {
    RegexState rs;
    FindRes res = find(vm, &rs);

    // An exception was thrown, return error
    if(res == FIND_ERR) return false;

    // No match could be found, return succesfully
    if(res == FIND_NOMATCH) return true;

    ObjTuple* ret = newTuple(vm, rs.captureCount + 1);
    push(vm, OBJ_VAL(ret));

    ptrdiff_t start = rs.captures[0].start - rs.string;
    ptrdiff_t end = start + rs.captures[0].length;

    ret->arr[0] = NUM_VAL(start);
    ret->arr[1] = NUM_VAL(end);

    for(int i = 1; i < rs.captureCount; i++) {
        if(!pushCapture(vm, &rs, i)) return false;
        ret->arr[i + 1] = pop(vm);
    }

    return true;
}

static bool madeProgress(const Substring* match, const char* lastMatch) {
    return match->start != lastMatch || match->length != 0;
}

JSR_NATIVE(jsr_re_matchAll) {
    JSR_CHECK(String, 1, "str");
    JSR_CHECK(String, 2, "regex");

    size_t len = jsrGetStringSz(vm, 1);
    const char* str = jsrGetString(vm, 1);
    const char* regex = jsrGetString(vm, 2);

    jsrPushList(vm);

    size_t offset = 0;
    const char* lastMatch = NULL;

    while(offset <= len) {
        RegexState rs;
        if(!matchRegex(&rs, str, len, regex, offset)) {
            if(hadError(&rs)) {
                JSR_RAISE(vm, "RegexException", getError(&rs));
            }
            return true;
        }

        const Substring* match = &rs.captures[0];

        // We got an empty match, increase the offset and try again
        if(!madeProgress(match, lastMatch)) {
            offset++;
            continue;
        }

        if(rs.captureCount <= 2) {
            if(!pushCapture(vm, &rs, rs.captureCount - 1)) return false;
            jsrListAppend(vm, -2);
            jsrPop(vm);
        } else {
            ObjTuple* tup = newTuple(vm, rs.captureCount - 1);
            push(vm, OBJ_VAL(tup));

            for(int i = 1; i < rs.captureCount; i++) {
                if(!pushCapture(vm, &rs, i)) return false;
                tup->arr[i - 1] = pop(vm);
            }

            jsrListAppend(vm, -2);
            jsrPop(vm);
        }

        ptrdiff_t offsetSinceLastMatch = match->start - (lastMatch ? lastMatch : str);
        offset += offsetSinceLastMatch + match->length;
        lastMatch = match->start + match->length;
    }

    return true;
}

static bool substitute(JStarVM* vm, RegexState* rs, JStarBuffer* b, const char* sub) {
    for(; !isAtEnd(sub); sub++) {
        switch(*sub) {
        case ESCAPE:
            sub++;

            if(isAtEnd(sub) || !isdigit(*sub)) {
                JSR_RAISE(vm, "RegexException", "Invalid sub string", ESCAPE);
            }

            int digitCount = 0;
            while(isdigit(sub[digitCount])) {
                digitCount++;
            }

            int capture = strtol(sub, NULL, 10);
            if(!pushCapture(vm, rs, capture)) return false;
            jsrBufferAppend(b, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
            jsrPop(vm);
            break;
        default:
            jsrBufferAppendChar(b, *sub);
            break;
        }
    }
    return true;
}

static bool substituteCall(JStarVM* vm, RegexState* rs, JStarBuffer* b, int fnSlot) {
    jsrPushValue(vm, fnSlot);
    for(int i = 1; i < rs->captureCount; i++) {
        if(!pushCapture(vm, rs, i)) return false;
    }

    if(jsrCall(vm, rs->captureCount - 1) != JSR_SUCCESS) return false;
    JSR_CHECK(String, -1, "sub() return value");

    jsrBufferAppendStr(b, jsrGetString(vm, -1));
    jsrPop(vm);

    return true;
}

JSR_NATIVE(jsr_re_substituteAll) {
    JSR_CHECK(String, 1, "str");
    JSR_CHECK(String, 2, "regex");
    JSR_CHECK(Int, 4, "num");

    if(!jsrIsString(vm, 3) && !jsrIsFunction(vm, 3)) {
        JSR_RAISE(vm, "TypeException", "sub must be either a String or a Function.");
    }

    size_t len = jsrGetStringSz(vm, 1);
    const char* str = jsrGetString(vm, 1);
    const char* regex = jsrGetString(vm, 2);
    int num = jsrGetNumber(vm, 4);

    JStarBuffer buf;
    jsrBufferInit(vm, &buf);

    int numSub = 0;
    size_t offset = 0;
    const char* lastMatch = NULL;

    while(offset <= len) {
        RegexState rs;
        if(!matchRegex(&rs, str, len, regex, offset)) {
            if(hadError(&rs)) {
                jsrBufferFree(&buf);
                JSR_RAISE(vm, "RegexException", getError(&rs));
            }
            break;
        }

        const Substring* match = &rs.captures[0];

        // We got an empty match, increase the offset and try again
        if(!madeProgress(match, lastMatch)) {
            offset++;
            continue;
        }

        // Append the characters between last match and current one to the output
        ptrdiff_t offsetSinceLastMatch = match->start - (lastMatch ? lastMatch : str);
        jsrBufferAppend(&buf, lastMatch ? lastMatch : str, offsetSinceLastMatch);

        if(jsrIsString(vm, 3)) {
            const char* sub = jsrGetString(vm, 3);
            if(!substitute(vm, &rs, &buf, sub)) {
                jsrBufferFree(&buf);
                return false;
            }
        } else {
            if(!substituteCall(vm, &rs, &buf, 3)) {
                jsrBufferFree(&buf);
                return false;
            }
        }

        offset += offsetSinceLastMatch + match->length;
        lastMatch = match->start + match->length;

        numSub++;
        if(num > 0 && numSub >= num) {
            break;
        }
    }

    if(lastMatch != NULL) {
        // Append the remaining string to the output
        jsrBufferAppend(&buf, lastMatch, str + len - lastMatch);
        jsrBufferPush(&buf);
    } else {
        // No substitutions performed, simply return the same string
        jsrBufferFree(&buf);
        jsrPushValue(vm, 1);
    }

    return true;
}

/**
 * MIT LICENSE
 *
 * Copyright (c) 2021 Fabrizio Pietrucci
 * Copyright (C) 1994–2021 Lua.org, PUC-Rio.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */
