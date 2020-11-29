// This file is heavily inspired by lstrlib.c of the LUA project.
// See copyright notice at the end of the file.

#include "re.h"

#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "object.h"
#include "value.h"
#include "vm.h"

#define ESCAPE             '%'
#define MAX_CAPTURES       31
#define CAPTURE_UNFINISHED -1
#define CAPTURE_POSITION   -2

#define RAISE_REGX_EXC(error, ...)                                \
    do {                                                          \
        rs->err = true;                                           \
        jsrRaise(rs->vm, "RegexException", error, ##__VA_ARGS__); \
    } while(0)

typedef struct {
    const char *str, *end;
    JStarVM* vm;
    bool err;
    int captureCount;
    struct {
        const char* start;
        ptrdiff_t len;
    } captures[MAX_CAPTURES];
} RegexState;

// -----------------------------------------------------------------------------
// REGEX MATCHING ENGINE
// -----------------------------------------------------------------------------

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

static bool matchCustomClass(char c, const char* regex, const char* classEnd) {
    bool ret = true;
    if(regex[1] == '^') {
        ret = false;
        regex++;
    }

    while(++regex < classEnd) {
        if(*regex == ESCAPE) {
            regex++;
            if(matchClass(c, *regex)) return ret;
        } else if(regex[1] == '-' && regex + 2 < classEnd) {
            regex += 2;
            if(*(regex - 2) <= c && c <= *regex) return ret;
        } else if(*regex == c) {
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
        if(rs->captures[i].len == CAPTURE_UNFINISHED) return i;
    }
    RAISE_REGX_EXC("Invalid regex capture.");
    return -1;
}

static const char* match(RegexState* rs, const char* str, const char* regex);

static const char* startCapture(RegexState* rs, const char* str, const char* regex) {
    if(rs->captureCount >= MAX_CAPTURES) {
        RAISE_REGX_EXC("Max capture number exceeded: %d.", MAX_CAPTURES);
        return NULL;
    }

    if(regex[1] != ')') {
        rs->captures[rs->captureCount].len = CAPTURE_UNFINISHED;
    } else {
        rs->captures[rs->captureCount].len = CAPTURE_POSITION;
        regex++;
    }

    rs->captures[rs->captureCount++].start = str;
    const char* res = match(rs, str, regex + 1);
    if(res == NULL) {
        rs->captureCount--;
    }

    return res;
}

static const char* endCapture(RegexState* rs, const char* str, const char* regex) {
    int i = finishCaptures(rs);
    if(i == -1) return NULL;

    rs->captures[i].len = str - rs->captures[i].start;
    const char* res = match(rs, str, regex + 1);
    if(res == NULL) {
        rs->captures[i].len = CAPTURE_UNFINISHED;
    }

    return res;
}

static const char* matchCapture(RegexState* rs, const char* str, int captureIdx) {
    if(captureIdx > rs->captureCount - 1 || rs->captures[captureIdx].len == CAPTURE_UNFINISHED ||
       rs->captures[captureIdx].len == CAPTURE_POSITION) {
        return NULL;
    }

    const char* capture = rs->captures[captureIdx].start;
    size_t captureLen = rs->captures[captureIdx].len;
    if((size_t)(rs->end - str) < captureLen || memcmp(str, capture, captureLen) != 0) {
        return NULL;
    }

    return str + captureLen;
}

static const char* greedyMatch(RegexState* rs, const char* str, const char* regex,
                               const char* clsEnd) {
    ptrdiff_t i = 0;
    while(str[i] != '\0' && matchClassOrChar(str[i], regex, clsEnd)) {
        i++;
    }

    while(i >= 0) {
        const char* res = match(rs, str + i, clsEnd + 1);
        if(res != NULL) {
            return res;
        }
        if(rs->err) {
            return NULL;
        }
        i--;
    }

    return NULL;
}

static const char* lazyMatch(RegexState* rs, const char* str, const char* regex,
                             const char* clsEnd) {
    do {
        const char* res = match(rs, str, clsEnd + 1);
        if(res != NULL) {
            return res;
        }
        if(rs->err) {
            return NULL;
        }
    } while(*str != '\0' && matchClassOrChar(*str++, regex, clsEnd));

    return NULL;
}

static const char* endClass(RegexState* rs, const char* regex) {
    switch(*regex++) {
    case ESCAPE:
        if(*regex == '\0') {
            RAISE_REGX_EXC("Malformed regex (ends with `%c`).", ESCAPE);
            return NULL;
        }
        return regex + 1;
    case '[':
        do {
            if(*regex == '\0') {
                RAISE_REGX_EXC("Malformed regex (unmatched `[`).");
                return NULL;
            }
            if(*regex++ == ESCAPE && *regex != '\0') {
                regex++;
            }
        } while(*regex != ']');

        return regex + 1;
    default:
        return regex;
    }
}

static const char* match(RegexState* rs, const char* str, const char* regex) {
    switch(*regex) {
    case '\0':
        return str;
    case '(':
        return startCapture(rs, str, regex);
    case ')':
        return endCapture(rs, str, regex);
    case '$':
        if(regex[1] == '\0') {
            return *str == '\0' ? str : NULL;
        }
        goto def;
    case ESCAPE:
        if(isdigit(regex[1])) {
            int len = 1;
            while(isdigit(regex[len])) {
                len++;
            }

            int capture = strtol(regex + 1, NULL, 10);

            str = matchCapture(rs, str, capture);
            if(str == NULL) {
                return NULL;
            }

            return match(rs, str, regex + len);
        }
        goto def;
    default:
    def : {
        const char* classEnd = endClass(rs, regex);
        if(classEnd == NULL) return NULL;

        bool isMatch = *str != '\0' && matchClassOrChar(*str, regex, classEnd);
        switch(*classEnd) {
        case '?': {
            const char* res;
            if(isMatch && (res = match(rs, str + 1, classEnd + 1)) != NULL) return res;
            return match(rs, str, classEnd + 1);
        }
        case '+':
            return isMatch ? greedyMatch(rs, str + 1, regex, classEnd) : NULL;
        case '*':
            return greedyMatch(rs, str, regex, classEnd);
        case '-':
            return lazyMatch(rs, str, regex, classEnd);
        default:
            return isMatch ? match(rs, str + 1, classEnd) : NULL;
        }
    }
    }

    return NULL;
}

// -----------------------------------------------------------------------------
// J* NATIVES AND HELPER FUNCTIONS
// -----------------------------------------------------------------------------

static void initState(RegexState* rs, JStarVM* vm, const char* str, size_t len) {
    rs->vm = vm;
    rs->str = str;
    rs->end = str + len;
    rs->err = false;
    rs->captureCount = 1;
    rs->captures[0].start = str;
    rs->captures[0].len = CAPTURE_UNFINISHED;
}

static bool matchRegex(JStarVM* vm, RegexState* rs, const char* str, size_t len, const char* regex,
                       int off) {
    initState(rs, vm, str, len);

    // negative offset start from end of string
    if(off < 0) {
        off += len;
    }

    if(off < 0 || (size_t)off > len) {
        return false;
    }

    str += off;

    if(*regex == '^') {
        const char* res = match(rs, str, regex + 1);
        if(res != NULL) {
            rs->captures[0].len = res - str;
            return true;
        }
        return false;
    }

    do {
        const char* res = match(rs, str, regex);
        if(res != NULL) {
            rs->captures[0].start = str;
            rs->captures[0].len = res - str;
            return true;
        }
        if(rs->err) return false;
    } while(*str++ != '\0');

    return false;
}

typedef enum FindRes {
    FIND_ERR,
    FIND_MATCH,
    FIND_NOMATCH,
} FindRes;

static FindRes findAux(JStarVM* vm, RegexState* rs) {
    if(!jsrCheckString(vm, 1, "str") || !jsrCheckString(vm, 2, "regex") ||
       !jsrCheckInt(vm, 3, "off")) {
        return FIND_ERR;
    }

    size_t len = jsrGetStringSz(vm, 1);
    const char* str = jsrGetString(vm, 1);
    const char* regex = jsrGetString(vm, 2);
    double off = jsrGetNumber(vm, 3);

    if(!matchRegex(vm, rs, str, len, regex, off)) {
        if(rs->err) return FIND_ERR;
        jsrPushNull(vm);
        return FIND_NOMATCH;
    }

    return FIND_MATCH;
}

static bool pushCapture(JStarVM* vm, RegexState* rs, int n) {
    if(n < 0 || n >= rs->captureCount) {
        JSR_RAISE(vm, "RegexException", "Invalid capture index (%d).", n);
    }
    if(rs->captures[n].len == CAPTURE_UNFINISHED) {
        JSR_RAISE(vm, "RegexException", "Unfinished capture.");
    }

    if(rs->captures[n].len == CAPTURE_POSITION) {
        jsrPushNumber(vm, rs->captures[n].start - rs->str);
    } else {
        jsrPushStringSz(vm, rs->captures[n].start, rs->captures[n].len);
    }

    return true;
}

JSR_NATIVE(jsr_re_match) {
    RegexState rs;
    FindRes res = findAux(vm, &rs);

    if(res == FIND_ERR) return false;
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
    FindRes res = findAux(vm, &rs);

    if(res == FIND_ERR) return false;
    if(res == FIND_NOMATCH) return true;

    ObjTuple* ret = newTuple(vm, rs.captureCount + 1);
    push(vm, OBJ_VAL(ret));

    ptrdiff_t start = rs.captures[0].start - rs.str;
    ptrdiff_t end = start + rs.captures[0].len;

    ret->arr[0] = NUM_VAL(start);
    ret->arr[1] = NUM_VAL(end);

    for(int i = 1; i < rs.captureCount; i++) {
        if(!pushCapture(vm, &rs, i)) return false;
        ret->arr[i + 1] = pop(vm);
    }

    return true;
}

JSR_NATIVE(jsr_re_gmatch) {
    JSR_CHECK(String, 1, "str");
    JSR_CHECK(String, 2, "regex");

    size_t len = jsrGetStringSz(vm, 1);
    const char* str = jsrGetString(vm, 1);
    const char* regex = jsrGetString(vm, 2);

    jsrPushList(vm);

    size_t off = 0;
    const char* lastmatch = NULL;
    while(off <= len) {
        RegexState rs;
        if(!matchRegex(vm, &rs, str, len, regex, off)) {
            if(rs.err) return false;
            return true;
        }

        // if 0 match increment by one and retry
        if(rs.captures[0].start == lastmatch && rs.captures[0].len == 0) {
            off++;
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

        ptrdiff_t offSinceLast = lastmatch ? rs.captures[0].start - lastmatch
                                           : rs.captures[0].start - str;

        off += offSinceLast + rs.captures[0].len;
        lastmatch = rs.captures[0].start + rs.captures[0].len;
    }

    return true;
}

static bool substitute(JStarVM* vm, RegexState* rs, JStarBuffer* b, const char* sub) {
    for(; *sub != '\0'; sub++) {
        switch(*sub) {
        case ESCAPE:
            sub++;
            if(*sub == '\0') {
                jsrRaise(vm, "RegexException", "Invalid sub string (ends with %c)", ESCAPE);
                return false;
            }

            int len = 0;
            while(isdigit(sub[len])) {
                len++;
            }

            if(len == 0) {
                goto def;
            }

            int capture = strtol(sub, NULL, 10);
            if(!pushCapture(vm, rs, capture)) return false;
            jsrBufferAppend(b, jsrGetString(vm, -1), jsrGetStringSz(vm, -1));
            jsrPop(vm);
            break;
        default:
        def:
            jsrBufferAppendChar(b, *sub);
            break;
        }
    }
    return true;
}

static bool substituteCall(JStarVM* vm, RegexState* rs, JStarBuffer* b, int funSlot) {
    jsrPushValue(vm, funSlot);
    for(int i = 1; i < rs->captureCount; i++) {
        if(!pushCapture(vm, rs, i)) return false;
    }

    if(jsrCall(vm, rs->captureCount - 1) != JSR_EVAL_SUCCESS) return false;
    JSR_CHECK(String, -1, "sub() return value");

    jsrBufferAppendStr(b, jsrGetString(vm, -1));
    jsrPop(vm);

    return true;
}

JSR_NATIVE(jsr_re_gsub) {
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

    JStarBuffer b;
    jsrBufferInit(vm, &b);

    int numsub = 0;
    size_t off = 0;
    const char* lastmatch = NULL;
    while(off <= len) {
        if(num > 0 && numsub > num - 1) {
            break;
        }

        RegexState rs;
        if(!matchRegex(vm, &rs, str, len, regex, off)) {
            if(rs.err) {
                jsrBufferFree(&b);
                return false;
            }
            break;
        }

        // if 0 match increment by one and retry
        if(rs.captures[0].start == lastmatch && rs.captures[0].len == 0) {
            off++;
            continue;
        }

        ptrdiff_t offSinceLast;
        if(lastmatch != NULL) {
            offSinceLast = rs.captures[0].start - lastmatch;
            jsrBufferAppend(&b, lastmatch, offSinceLast);
        } else {
            offSinceLast = rs.captures[0].start - str;
            jsrBufferAppend(&b, str, offSinceLast);
        }

        if(jsrIsString(vm, 3)) {
            const char* sub = jsrGetString(vm, 3);
            if(!substitute(vm, &rs, &b, sub)) {
                jsrBufferFree(&b);
                return false;
            }
        } else {
            if(!substituteCall(vm, &rs, &b, 3)) {
                jsrBufferFree(&b);
                return false;
            }
        }

        off += offSinceLast + rs.captures[0].len;
        lastmatch = rs.captures[0].start + rs.captures[0].len;

        numsub++;
    }

    if(lastmatch != NULL) {
        jsrBufferAppend(&b, lastmatch, str + len - lastmatch);
        jsrBufferPush(&b);
    } else {
        jsrBufferFree(&b);
        jsrPushValue(vm, 1);
    }

    return true;
}

/**
 * MIT LICENSE
 *
 * Copyright (c) 2020 Fabrizio Pietrucci
 * Copyright (C) 1994–2020 Lua.org, PUC-Rio.
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