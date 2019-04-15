#include "re.h"
#include "vm.h"
#include "memory.h"

#include <string.h>
#include <stddef.h>
#include <ctype.h>

#define MAX_CAPTURES       31
#define CAPTURE_UNFINISHED -1
#define CAPTURE_POSITION   -2

#define ESCAPE '%'

typedef struct {
    const char *str;
    int capturec;
    BlangVM *vm;
    bool err;
    struct {
        const char *start;
        ptrdiff_t len;
    } captures[MAX_CAPTURES];
} RegexState;

static bool matchClass(char c, char cls) {
    bool res;
    switch(tolower(cls)) {
    case 'a': res = isalpha(c); break;
    case 'c': res = iscntrl(c); break;
    case 'd': res = isdigit(c); break;
    case 'l': res = islower(c); break;
    case 'p': res = ispunct(c); break;
    case 's': res = isspace(c); break;
    case 'u': res = isupper(c); break;
    case 'w': res = isalnum(c); break;
    case 'x': res = isxdigit(c); break;
    default: return c == cls;
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
            if(matchClass(c, *r))
                return ret;
        } else if(r[1] == '-' && r + 2 < er) {
            r += 2;
            if(*(r - 2) <= c && c <= *r)
                return ret;
        } else if (*r == c) {
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
    rs->err = true;
    blRaise(rs->vm, "RegexException", "Invalid regex capture.");
    return -1;
}

static const char *match(RegexState *rs, const char *s, const char *r);

static const char *startCapture(RegexState *rs, const char *s, const char *r) {
    if(rs->capturec >= MAX_CAPTURES) {
        rs->err = true;
        blRaise(rs->vm, "RegexException", "Max capture number exceeded (%d).", MAX_CAPTURES);
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
    if((res = match(rs, s, r + 1)) == NULL)
        rs->captures[i].len = CAPTURE_UNFINISHED;
    return res;
}

static const char *greedyMatch(RegexState *rs, const char *s, const char *r, const char *er) {
    ptrdiff_t i = 0;
    while(s[i] != '\0' && matchClassOrChar(s[i], r, er))
        i++;

    while(i >= 0) {
        const char *res;
        if((res = match(rs, s + i, er + 1)) != NULL)
            return res;
        if(rs->err) return NULL;
        i--;
    }

    return NULL;
}

static const char *lazyMatch(RegexState *rs, const char *s, const char *r, const char *er) {
    do {
        const char *res;
        if((res = match(rs, s, er + 1)) != NULL)
            return res;
        if(rs->err) return NULL;
    } while(*s != '\0' && matchClassOrChar(*s++, r, er));
    return NULL;
}

static const char *endClass(RegexState *rs, const char *r) {
    switch(*r++) {
    case ESCAPE:
        if(*r == '\0') {
            rs->err = true;
            blRaise(rs->vm, "RegexException", "Malformed regex (ends with `%c`).", ESCAPE);
            return NULL;
        }
        return r + 1;
    case '[':
        do {
            if(*r == '\0') {
                rs->err = true;
                blRaise(rs->vm, "RegexException", "Malformed regex (unmatched `[`).");
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
        if(r[1] == '\0')
            return *s == '\0' ? s : NULL;
        goto def;
    case '\0':
        return s;
    default: def: {
        const char *er = endClass(rs, r);
        if(er == NULL) return NULL;
        bool isMatch = matchClassOrChar(*s, r, er);
        
        switch(*er) {
        case '?': {
            const char *res;
            if(isMatch && (res = match(rs, s + 1, er + 1)) != NULL)
                return res;
            return match(rs, s, er + 1);
        }
        case '+': {
            return isMatch ? greedyMatch(rs, s + 1, r, er) : NULL;
        }
        case '*': {
            return greedyMatch(rs, s, r, er);
        }
        case '-': {
            return lazyMatch(rs, s, r, er);
        }
        default: {
            if(!isMatch) return NULL;
            else return match(rs, s + 1, er);
        }
        }
    }
    }
   
    return NULL;
}

static bool matchRegex(BlangVM *vm, RegexState *rs, const char *s, size_t len, const char *r, int off) {
    rs->vm = vm;
    rs->str = s;
    rs->err = false;
    rs->capturec = 1;
    rs->captures[0].start = s;
    rs->captures[0].len =  CAPTURE_UNFINISHED;

    if(off < 0) off += len;
    if(off < 0 || (size_t) off > len) return false;

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

#define FIND_ERR     0
#define FIND_MATCH   1
#define FIND_NOMATCH 2

static int find_aux(BlangVM *vm, RegexState *rs) {
    if(!blCheckStr(vm, 1, "str")   || 
       !blCheckStr(vm, 2, "regex") || 
       !blCheckInt(vm, 3, "off")) 
    {
        return FIND_ERR;
    }

    const char *str = blGetString(vm, 1);
    size_t len = blGetStringSz(vm, 1);
    const char *regex = blGetString(vm, 2);
    double off = blGetNumber(vm, 3);

    if(!matchRegex(vm, rs, str, len, regex, off)) {
        if(rs->err) return FIND_ERR;
        blPushNull(vm);
        return FIND_NOMATCH;
    }

    return FIND_MATCH;
}

static bool pushCapture(BlangVM *vm, RegexState *rs, int n) {
    if(n < 0 || n >= rs->capturec)
        BL_RAISE(vm, "RegexException", "Invalid capture index (%d).", n);
    if(rs->captures[n].len == CAPTURE_UNFINISHED)
        BL_RAISE(vm, "RegexException", "Unfinished capture.");

    if(rs->captures[n].len == CAPTURE_POSITION)
        blPushNumber(vm, rs->captures[n].start - rs->str);
    else
        blPushStringSz(vm, rs->captures[n].start, rs->captures[n].len);

    return true;
}

NATIVE(bl_re_match) {
    RegexState rs;
    int res = find_aux(vm, &rs);

    if(res == FIND_ERR) return false;
    if(res == FIND_NOMATCH) return true;

    size_t size = rs.capturec == 1 ? 1 : rs.capturec - 1;
    ObjTuple *ret = newTuple(vm, size);
    push(vm, OBJ_VAL(ret));

    if(rs.capturec == 1) {
        pushCapture(vm, &rs, 0);
        ret->arr[0] = pop(vm);
    } else {
        for(int i = 1; i < rs.capturec; i++) {
            if(!pushCapture(vm, &rs, i)) return false;
            ret->arr[i - 1] = pop(vm);
        }
    }

    return true;
}

NATIVE(bl_re_find) {
    RegexState rs;
    int res = find_aux(vm, &rs);

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

NATIVE(bl_re_gmatch) {
    if(!blCheckStr(vm, 1, "str")) return false;
    if(!blCheckStr(vm, 2, "regex")) return false;

    const char *str = blGetString(vm, 1);
    size_t len = blGetStringSz(vm, 1);
    const char *regex = blGetString(vm, 2);

    blPushList(vm);

    size_t off = 0;
    const char *lend = str;
    while(off < len) {
        RegexState rs;
        if(!matchRegex(vm, &rs, str, len, regex, off)) {
            if(rs.err) return false;
            return true;
        }
 
        off += rs.captures[0].len + (rs.captures[0].start - lend);
        lend = rs.captures[0].start + rs.captures[0].len;

        if(rs.capturec == 1) {
            pushCapture(vm, &rs, 0);
            blListAppend(vm, -2);
            blPop(vm);
        } else {
            ObjTuple *tup = newTuple(vm, rs.capturec - 1);
            push(vm, OBJ_VAL(tup));

            for(int i = 1; i < rs.capturec; i++) {
                if(!pushCapture(vm, &rs, i)) return false;
                tup->arr[i - 1] = pop(vm);
            }

            blListAppend(vm, -2);
            blPop(vm);
        }

    }

    return true;
}