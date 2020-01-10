#include "re.h"
#include "memory.h"
#include "vm.h"

#include <ctype.h>
#include <stddef.h>
#include <string.h>

#define MAX_CAPTURES        31
#define CAPTURE_UNFINISHED -1
#define CAPTURE_POSITION   -2

#define ESCAPE '%'

#define REG_ERR(error, ...)                                       \
    do {                                                          \
        rs->err = true;                                           \
        jsrRaise(rs->vm, "RegexException", error, ##__VA_ARGS__); \
    } while(0)                                                    \

typedef struct {
    const char *str;
    int capturec;
    JStarVM *vm;
    bool err;
    struct {
        const char *start;
        ptrdiff_t len;
    } captures[MAX_CAPTURES];
} RegexState;

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

static bool matchCustomClass(char c, const char *r, const char *er) {
    bool ret = true;
    if(r[1] == '^') {
        ret = false;
        r++;
    }

    while(++r < er) {
        if(*r == ESCAPE) {
            r++;
            if(matchClass(c, *r)) return ret;
        } else if(r[1] == '-' && r + 2 < er) {
            r += 2;
            if(*(r - 2) <= c && c <= *r) return ret;
        } else if(*r == c) {
            return ret;
        }
    }

    return !ret;
}

static bool matchClassOrChar(char matchChar, const char *r, const char *er) {
    switch(*r) {
    case '.':
        return true;
    case ESCAPE:
        return matchClass(matchChar, r[1]);
    case '[':
        return matchCustomClass(matchChar, r, er - 1);
    default:
        return matchChar == *r;
    }
}

static int captureToClose(RegexState *rs) {
    for(int i = rs->capturec - 1; i > 0; i--) {
        if(rs->captures[i].len == CAPTURE_UNFINISHED) return i;
    }
    REG_ERR("Invalid regex capture.");
    return -1;
}

static const char *match(RegexState *rs, const char *s, const char *r);

static const char *startCapture(RegexState *rs, const char *s, const char *r) {
    if(rs->capturec >= MAX_CAPTURES) {
        REG_ERR("Max capture number exceeded (%d).", MAX_CAPTURES);
        return NULL;
    }

    if(r[1] != ')')
        rs->captures[rs->capturec].len = CAPTURE_UNFINISHED;
    else {
        rs->captures[rs->capturec].len = CAPTURE_POSITION;
        r++;
    }
    rs->captures[rs->capturec].start = s;
    rs->capturec++;

    const char *res;
    if((res = match(rs, s, r + 1)) == NULL) {
        rs->capturec--;
    }
    return res;
}

static const char *endCapture(RegexState *rs, const char *s, const char *r) {
    int i = captureToClose(rs);
    if(i == -1) return NULL;
    rs->captures[i].len = s - rs->captures[i].start;
    const char *res;
    if((res = match(rs, s, r + 1)) == NULL) rs->captures[i].len = CAPTURE_UNFINISHED;
    return res;
}

static const char *greedyMatch(RegexState *rs, const char *s, const char *r, const char *er) {
    ptrdiff_t i = 0;
    while(s[i] != '\0' && matchClassOrChar(s[i], r, er)) {
        i++;
    }

    while(i >= 0) {
        const char *res;
        if((res = match(rs, s + i, er + 1)) != NULL) return res;
        if(rs->err) return NULL;
        i--;
    }

    return NULL;
}

static const char *lazyMatch(RegexState *rs, const char *s, const char *r, const char *er) {
    do {
        const char *res;
        if((res = match(rs, s, er + 1)) != NULL) return res;
        if(rs->err) return NULL;
    } while(*s != '\0' && matchClassOrChar(*s++, r, er));
    return NULL;
}

static const char *endClass(RegexState *rs, const char *r) {
    switch(*r++) {
    case ESCAPE:
        if(*r == '\0') {
            REG_ERR("Malformed regex (ends with `%c`).", ESCAPE);
            return NULL;
        }
        return r + 1;
    case '[':
        do {
            if(*r == '\0') {
                REG_ERR("Malformed regex (unmatched `[`).");
                return NULL;
            }
            if(*r++ == ESCAPE && *r != '\0') r++; // Skip escape
        } while(*r != ']');
        return r + 1;
    default:
        return r;
    }
}

static const char *match(RegexState *rs, const char *s, const char *r) {
    switch(*r) {
    case '(':
        return startCapture(rs, s, r);
    case ')':
        return endCapture(rs, s, r);
    case '$':
        if(r[1] == '\0') return *s == '\0' ? s : NULL;
        goto def;
    case '\0':
        return s;
    default:
    def : {
        const char *er = endClass(rs, r);
        if(er == NULL) return NULL;
        bool isMatch = *s != '\0' && matchClassOrChar(*s, r, er);

        switch(*er) {
        case '?': {
            const char *res;
            if(isMatch && (res = match(rs, s + 1, er + 1)) != NULL) return res;
            return match(rs, s, er + 1);
        }
        case '+':
            return isMatch ? greedyMatch(rs, s + 1, r, er) : NULL;
        case '*':
            return greedyMatch(rs, s, r, er);
        case '-':
            return lazyMatch(rs, s, r, er);
        default: {
            if(!isMatch)
                return NULL;
            else
                return match(rs, s + 1, er);
        }
        }
    }
    }

    return NULL;
}

static bool matchRegex(JStarVM *vm, RegexState *rs, const char *s, 
    size_t len, const char *r, int off)
{
    rs->vm = vm;
    rs->str = s;
    rs->err = false;
    rs->capturec = 1;
    rs->captures[0].start = s;
    rs->captures[0].len = CAPTURE_UNFINISHED;

    if(off < 0) off += len;
    if(off < 0 || (size_t)off > len) return false;

    s += off;

    if(*r == '^') {
        const char *res;
        if((res = match(rs, s, r + 1)) != NULL) {
            rs->captures[0].len = res - s;
            return true;
        }
        return false;
    }

    do {
        const char *res;
        if((res = match(rs, s, r)) != NULL) {
            rs->captures[0].start = s;
            rs->captures[0].len = res - s;
            return true;
        }
        if(rs->err) return false;
    } while(*s++ != '\0');

    return false;
}

typedef enum FindRes {
    FIND_ERR, FIND_MATCH, FIND_NOMATCH
} FindRes;

static FindRes findAux(JStarVM *vm, RegexState *rs) {
    if(!jsrCheckStr(vm, 1, "str") || !jsrCheckStr(vm, 2, "regex") || !jsrCheckInt(vm, 3, "off")) {
        return FIND_ERR;
    }

    const char *str = jsrGetString(vm, 1);
    size_t len = jsrGetStringSz(vm, 1);
    const char *regex = jsrGetString(vm, 2);
    double off = jsrGetNumber(vm, 3);

    if(!matchRegex(vm, rs, str, len, regex, off)) {
        if(rs->err) return FIND_ERR;
        jsrPushNull(vm);
        return FIND_NOMATCH;
    }

    return FIND_MATCH;
}

static bool pushCapture(JStarVM *vm, RegexState *rs, int n) {
    if(n < 0 || n >= rs->capturec) 
        JSR_RAISE(vm, "RegexException", "Invalid capture index (%d).", n);
    if(rs->captures[n].len == CAPTURE_UNFINISHED)
        JSR_RAISE(vm, "RegexException", "Unfinished capture.");

    if(rs->captures[n].len == CAPTURE_POSITION)
        jsrPushNumber(vm, rs->captures[n].start - rs->str);
    else
        jsrPushStringSz(vm, rs->captures[n].start, rs->captures[n].len);

    return true;
}

JSR_NATIVE(jsr_re_match) {
    RegexState rs;
    FindRes res = findAux(vm, &rs);

    if(res == FIND_ERR) return false;
    if(res == FIND_NOMATCH) return true;

    if(rs.capturec <= 2) {
        if(!pushCapture(vm, &rs, rs.capturec - 1)) return false;
    } else {
        ObjTuple *ret = newTuple(vm, rs.capturec - 1);
        push(vm, OBJ_VAL(ret));

        for(int i = 1; i < rs.capturec; i++) {
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

    ObjTuple *ret = newTuple(vm, rs.capturec + 1);
    push(vm, OBJ_VAL(ret));

    ptrdiff_t start = rs.captures[0].start - rs.str;
    ptrdiff_t end = start + rs.captures[0].len;

    ret->arr[0] = NUM_VAL(start);
    ret->arr[1] = NUM_VAL(end);

    for(int i = 1; i < rs.capturec; i++) {
        if(!pushCapture(vm, &rs, i)) return false;
        ret->arr[i + 1] = pop(vm);
    }

    return true;
}

JSR_NATIVE(jsr_re_gmatch) {
    if(!jsrCheckStr(vm, 1, "str") || !jsrCheckStr(vm, 2, "regex")) {
        return false;
    }

    const char *regex = jsrGetString(vm, 2);
    const char *str = jsrGetString(vm, 1);
    size_t len = jsrGetStringSz(vm, 1);

    jsrPushList(vm);

    size_t off = 0;
    const char *lastmatch = NULL;
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

        if(rs.capturec <= 2) {
            if(!pushCapture(vm, &rs, rs.capturec - 1)) return false;
            jsrListAppend(vm, -2);
            jsrPop(vm);
        } else {
            ObjTuple *tup = newTuple(vm, rs.capturec - 1);
            push(vm, OBJ_VAL(tup));

            for(int i = 1; i < rs.capturec; i++) {
                if(!pushCapture(vm, &rs, i)) return false;
                tup->arr[i - 1] = pop(vm);
            }

            jsrListAppend(vm, -2);
            jsrPop(vm);
        }

        ptrdiff_t offSinceLast;
        if(lastmatch != NULL)
            offSinceLast = rs.captures[0].start - lastmatch;
        else
            offSinceLast = rs.captures[0].start - str;

        // increment by the number of chars since last match
        off += offSinceLast + rs.captures[0].len;
        // set lastmatch to one past the end of current match
        lastmatch = rs.captures[0].start + rs.captures[0].len;
    }

    return true;
}

static bool substitute(JStarVM *vm, RegexState *rs, JStarBuffer *b, const char *s, const char *sub) {
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
            if(len == 0) goto def;

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

JSR_NATIVE(jsr_re_gsub) {
    if(!jsrCheckStr(vm, 1, "str") || !jsrCheckStr(vm, 2, "regex") || 
       !jsrCheckStr(vm, 3, "sub") || !jsrCheckInt(vm, 4, "num"))
    {
        return false;
    }

    const char *str = jsrGetString(vm, 1);
    size_t len = jsrGetStringSz(vm, 1);
    const char *regex = jsrGetString(vm, 2);
    const char *sub = jsrGetString(vm, 3);
    int num = jsrGetNumber(vm, 4);

    JStarBuffer b;
    jsrBufferInit(vm, &b);

    int numsub = 0;
    size_t off = 0;
    const char *lastmatch = NULL;

    while(off <= len) {
        if(num > 0 && numsub > num - 1) break;

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

        if(!substitute(vm, &rs, &b, str, sub)) {
            jsrBufferFree(&b);
            return false;
        }

        numsub++;

        // increment by the number of chars since last match
        off += offSinceLast + rs.captures[0].len;
        // set lastmatch to one past the end of current match
        lastmatch = rs.captures[0].start + rs.captures[0].len;
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