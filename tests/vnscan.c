#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vnscan.h"

#define VS_MAX_TOKENS 16384

typedef enum {
    T_LBRACE,
    T_RBRACE,
    T_COMMA,
    T_COLON,
    T_NUMBER,
    T_IDENT,
    T_CSTRING, /* quotes stripped, "" collapsed to " */
    T_HSTRING, /* stored with the quotes and suffix: '00FF'H */
    T_BSTRING
} vs_kind;

typedef struct {
    vs_kind kind;
    char   *text;
} vs_tok;

typedef struct {
    vs_tok tok[VS_MAX_TOKENS];
    size_t n;
} vs_toks;

static void
vs_err(char *err, size_t errlen, const char *fmt, ...) {
    va_list ap;
    if(!err || !errlen) return;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static void
vs_free(vs_toks *ts) {
    size_t i;
    for(i = 0; i < ts->n; i++) free(ts->tok[i].text);
    ts->n = 0;
}

static int
vs_push(vs_toks *ts, vs_kind kind, const char *s, size_t len, char *err,
        size_t errlen) {
    char *copy;
    if(ts->n >= VS_MAX_TOKENS) {
        vs_err(err, errlen, "more than %d tokens", VS_MAX_TOKENS);
        return 0;
    }
    copy = (char *)malloc(len + 1);
    if(!copy) {
        vs_err(err, errlen, "out of memory");
        return 0;
    }
    memcpy(copy, s, len);
    copy[len] = '\0';
    ts->tok[ts->n].kind = kind;
    ts->tok[ts->n].text = copy;
    ts->n++;
    return 1;
}

/*
 * strdup is POSIX, not C99: under glibc with -std=c99 it is not declared, so gcc
 * assumes it returns int and truncates the pointer on a 64-bit target. Doing it
 * by hand keeps the tests portable.
 */
static char *
vs_dup(const char *s) {
    size_t n = strlen(s) + 1;
    char *copy = (char *)malloc(n);
    if(copy) memcpy(copy, s, n);
    return copy;
}

static int
vs_ident_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
           || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

/* Tokenise. Comments are skipped entirely; strings become single tokens. */
static int
vs_tokenize(const char *text, vs_toks *ts, char *err, size_t errlen) {
    const char *p = text;

    ts->n = 0;
    while(*p) {
        if(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
            p++;
            continue;
        }

        /* X.680 11.6: a comment runs to the next "--" or to end of line. */
        if(p[0] == '-' && p[1] == '-') {
            p += 2;
            while(*p) {
                if(*p == '\n') { p++; break; }
                if(p[0] == '-' && p[1] == '-') { p += 2; break; }
                p++;
            }
            continue;
        }

        if(*p == '{') { if(!vs_push(ts, T_LBRACE, p, 1, err, errlen)) return 0; p++; continue; }
        if(*p == '}') { if(!vs_push(ts, T_RBRACE, p, 1, err, errlen)) return 0; p++; continue; }
        if(*p == ',') { if(!vs_push(ts, T_COMMA,  p, 1, err, errlen)) return 0; p++; continue; }
        if(*p == ':') { if(!vs_push(ts, T_COLON,  p, 1, err, errlen)) return 0; p++; continue; }

        if(*p == '"') {
            /*
             * Keep the surrounding quotes in the token. They are what lets the
             * normaliser tell a string from an arc list: a PrintableString of
             * `"34 9"` and an OBJECT IDENTIFIER of `{ 34 9 }` would otherwise
             * both arrive as the bare text `34 9`.
             */
            const char *start = p;
            p++;
            for(;;) {
                if(!*p) {
                    vs_err(err, errlen, "unterminated cstring");
                    return 0;
                }
                if(*p == '"') {
                    if(p[1] == '"') { /* an escaped quote */
                        p += 2;
                        continue;
                    }
                    p++;
                    break;
                }
                p++;
            }
            if(!vs_push(ts, T_CSTRING, start, (size_t)(p - start), err, errlen))
                return 0;
            continue;
        }

        if(*p == '\'') {
            const char *start = p;
            p++;
            while(*p && *p != '\'') p++;
            if(*p != '\'') {
                vs_err(err, errlen, "unterminated hstring or bstring");
                return 0;
            }
            p++; /* closing quote */
            if(*p == 'H') {
                p++;
                if(!vs_push(ts, T_HSTRING, start, (size_t)(p - start), err,
                            errlen))
                    return 0;
            } else if(*p == 'B') {
                p++;
                if(!vs_push(ts, T_BSTRING, start, (size_t)(p - start), err,
                            errlen))
                    return 0;
            } else {
                vs_err(err, errlen,
                       "a quoted string must be followed by H or B");
                return 0;
            }
            continue;
        }

        if((*p >= '0' && *p <= '9') || (*p == '-' && p[1] >= '0' && p[1] <= '9')) {
            const char *start = p;
            if(*p == '-') p++;
            while(*p >= '0' && *p <= '9') p++;
            if(!vs_push(ts, T_NUMBER, start, (size_t)(p - start), err, errlen))
                return 0;
            continue;
        }

        if(vs_ident_char(*p)) {
            const char *start = p;
            while(vs_ident_char(*p)) p++;
            if(!vs_push(ts, T_IDENT, start, (size_t)(p - start), err, errlen))
                return 0;
            continue;
        }

        vs_err(err, errlen, "unexpected character '%c'", *p);
        return 0;
    }
    return 1;
}

static int vs_is_value_start(vs_kind k) {
    return k == T_NUMBER || k == T_IDENT || k == T_CSTRING || k == T_HSTRING
           || k == T_BSTRING || k == T_LBRACE;
}

static int
vs_validate(const vs_toks *ts, char *err, size_t errlen) {
    size_t i;
    int depth = 0;
    /* Within the innermost group, what did we last see? */
    int after_open = 0, after_comma = 0, seen_value = 0;

    for(i = 0; i < ts->n; i++) {
        vs_kind k = ts->tok[i].kind;

        switch(k) {
        case T_LBRACE:
            depth++;
            after_open = 1;
            after_comma = 0;
            seen_value = 0;
            break;

        case T_RBRACE:
            if(depth == 0) {
                vs_err(err, errlen, "closing brace without a matching open");
                return 0;
            }
            if(after_comma) {
                vs_err(err, errlen, "trailing comma before closing brace");
                return 0;
            }
            depth--;
            after_open = 0;
            after_comma = 0;
            seen_value = 1; /* the group itself is a value in its parent */
            break;

        case T_COMMA:
            if(depth == 0) {
                vs_err(err, errlen, "comma outside any braces");
                return 0;
            }
            if(after_open) {
                vs_err(err, errlen, "comma directly after an opening brace");
                return 0;
            }
            if(after_comma) {
                vs_err(err, errlen, "two commas in a row");
                return 0;
            }
            if(!seen_value) {
                vs_err(err, errlen, "comma with no preceding value");
                return 0;
            }
            after_comma = 1;
            seen_value = 0;
            break;

        case T_COLON:
            if(i + 1 >= ts->n || !vs_is_value_start(ts->tok[i + 1].kind)) {
                vs_err(err, errlen,
                       "`:` is not followed by a value");
                return 0;
            }
            after_open = 0;
            after_comma = 0;
            seen_value = 0;
            break;

        default: /* a value or a name */
            after_open = 0;
            after_comma = 0;
            seen_value = 1;
            break;
        }
    }

    if(depth != 0) {
        vs_err(err, errlen, "%d brace(s) left unclosed", depth);
        return 0;
    }
    if(ts->n == 0) {
        vs_err(err, errlen, "empty input");
        return 0;
    }
    return 1;
}

int
vn_scan_wellformed(const char *text, char *err, size_t errlen) {
    vs_toks ts;
    int ok;

    if(err && errlen) err[0] = '\0';
    if(!vs_tokenize(text, &ts, err, errlen)) {
        vs_free(&ts);
        return 0;
    }
    ok = vs_validate(&ts, err, errlen);
    vs_free(&ts);
    return ok;
}

/*
 * Is token i a value, as opposed to a field name or a CHOICE alternative name?
 *
 * Numbers and strings are always values. An identifier is a value only when the
 * next token ends the item -- a comma, a closing brace or end of input. An
 * identifier followed by `:` names an alternative; one followed by another value
 * names a field.
 */
static int
vs_is_scalar(const vs_toks *ts, size_t i) {
    vs_kind k = ts->tok[i].kind;
    vs_kind next;

    if(k == T_NUMBER || k == T_CSTRING || k == T_HSTRING || k == T_BSTRING)
        return 1;
    if(k != T_IDENT) return 0;
    if(i + 1 >= ts->n) return 1;
    next = ts->tok[i + 1].kind;
    return next == T_COMMA || next == T_RBRACE;
}

int
vn_scan_scalars(const char *text, char **out, size_t max, size_t *count,
                char *err, size_t errlen) {
    vs_toks ts;
    size_t i, n = 0;

    if(err && errlen) err[0] = '\0';
    *count = 0;
    if(!vs_tokenize(text, &ts, err, errlen)) {
        vs_free(&ts);
        return 0;
    }

    for(i = 0; i < ts.n; i++) {
        /*
         * An OBJECT IDENTIFIER prints as `{ 2 23 143 1 }`: two or more bare
         * numbers with no commas. Join those into one scalar so they can be
         * compared against XER's dotted form. A SEQUENCE OF INTEGER keeps its
         * commas, so it is unaffected, and a one-element list stays one scalar.
         */
        if(ts.tok[i].kind == T_LBRACE) {
            size_t j = i + 1, numbers = 0;
            while(j < ts.n && ts.tok[j].kind == T_NUMBER) {
                numbers++;
                j++;
            }
            if(numbers >= 2 && j < ts.n && ts.tok[j].kind == T_RBRACE) {
                char joined[512];
                size_t k, used = 0;
                joined[0] = '\0';
                for(k = 0; k < numbers; k++) {
                    int w = snprintf(joined + used, sizeof joined - used,
                                     "%s%s", k ? " " : "",
                                     ts.tok[i + 1 + k].text);
                    if(w < 0 || (size_t)w >= sizeof joined - used) {
                        vs_err(err, errlen, "arc list too long");
                        vs_free(&ts);
                        return 0;
                    }
                    used += (size_t)w;
                }
                if(n >= max) {
                    vs_err(err, errlen, "more than %u scalars", (unsigned)max);
                    vs_free(&ts);
                    return 0;
                }
                out[n] = vs_dup(joined);
                if(!out[n]) {
                    vs_err(err, errlen, "out of memory");
                    vs_free(&ts);
                    return 0;
                }
                n++;
                i = j; /* resume after the closing brace */
                continue;
            }
        }

        if(!vs_is_scalar(&ts, i)) continue;
        if(n >= max) {
            vs_err(err, errlen, "more than %u scalars", (unsigned)max);
            vs_free(&ts);
            return 0;
        }
        out[n] = vs_dup(ts.tok[i].text);
        if(!out[n]) {
            vs_err(err, errlen, "out of memory");
            vs_free(&ts);
            return 0;
        }
        n++;
    }

    vs_free(&ts);
    *count = n;
    return 1;
}
