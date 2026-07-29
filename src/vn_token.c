/*
 * vn_token.c -- chunk-aware tokeniser for ASN.1 value notation.
 *
 * The one thing that makes this harder than the test scanner in tests/vnscan.c:
 * a token may be cut in half by the end of the buffer. At `'00AA` we cannot tell
 * a malformed hstring from a truncated one, so the tokeniser reports
 * VT_INCOMPLETE and leaves the position at the token's first byte. The caller
 * then re-presents that byte onward together with more input.
 *
 * Tokens point into the caller's buffer; nothing is copied.
 */

#include <string.h>
#include "vn_internal.h"

static int
vt_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v'
           || c == '\f';
}

static int
vt_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int
vt_ident_char(char c) {
    return vt_ident_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static int
vt_digit(char c) {
    return c >= '0' && c <= '9';
}

/*
 * Skip whitespace and X.680 comments.
 *
 * A comment runs to the next "--" or to end of line (11.6). One that reaches the
 * end of the buffer with neither may continue in the next chunk, so it counts as
 * incomplete rather than as terminated.
 */
static int
vt_skip_filler(const char *buf, size_t size, int eof, size_t *pos) {
    size_t p = *pos;

    for(;;) {
        while(p < size && vt_space(buf[p])) p++;
        if(p >= size) {
            *pos = p;
            return 1;
        }
        if(!(buf[p] == '-' && p + 1 < size && buf[p + 1] == '-')) {
            /* A lone '-' at the very end could begin a comment or a negative
             * number; either way we need more input to tell. */
            if(buf[p] == '-' && p + 1 >= size && !eof) {
                *pos = p;
                return 0;
            }
            *pos = p;
            return 1;
        }
        {
            size_t q = p + 2;
            int    closed = 0;
            while(q < size) {
                if(buf[q] == '\n') {
                    q++;
                    closed = 1;
                    break;
                }
                if(buf[q] == '-' && q + 1 < size && buf[q + 1] == '-') {
                    q += 2;
                    closed = 1;
                    break;
                }
                q++;
            }
            if(!closed) {
                if(!eof) {
                    *pos = p; /* may continue in the next chunk */
                    return 0;
                }
                p = q; /* at end of input an unterminated comment just ends */
            } else {
                p = q;
            }
            continue;
        }
    }
}

vn_token_e
vn_token_next(const char *buf, size_t size, int eof, size_t *pos,
               vn_token_t *tok) {
    size_t p;

    memset(tok, 0, sizeof *tok);

    if(!vt_skip_filler(buf, size, eof, pos)) {
        tok->kind = VT_INCOMPLETE;
        tok->start = buf + *pos;
        return tok->kind;
    }
    p = *pos;
    if(p >= size) {
        tok->kind = VT_END;
        tok->start = buf + p;
        return tok->kind;
    }

    tok->start = buf + p;

    switch(buf[p]) {
    case '{': tok->kind = VT_LBRACE; tok->len = 1; *pos = p + 1; return tok->kind;
    case '}': tok->kind = VT_RBRACE; tok->len = 1; *pos = p + 1; return tok->kind;
    case ',': tok->kind = VT_COMMA;  tok->len = 1; *pos = p + 1; return tok->kind;
    case ':': tok->kind = VT_COLON;  tok->len = 1; *pos = p + 1; return tok->kind;
    default: break;
    }

    if(buf[p] == '"') {
        size_t q = p + 1;
        for(;;) {
            if(q >= size) {
                /* Unterminated: truncated if more may come, malformed at eof. */
                tok->kind = eof ? VT_INVALID : VT_INCOMPLETE;
                return tok->kind;
            }
            if(buf[q] == '"') {
                if(q + 1 >= size) {
                    if(!eof) { /* or the first half of a doubled quote */
                        tok->kind = VT_INCOMPLETE;
                        return tok->kind;
                    }
                    q++;
                    break;
                }
                if(buf[q + 1] == '"') {
                    q += 2;
                    continue;
                }
                q++;
                break;
            }
            q++;
        }
        tok->kind = VT_CSTRING;
        tok->len = q - p;
        tok->body = buf + p + 1;
        tok->body_len = tok->len - 2;
        *pos = q;
        return tok->kind;
    }

    if(buf[p] == '\'') {
        size_t q = p + 1;
        while(q < size && buf[q] != '\'') q++;
        if(q >= size) {
            tok->kind = eof ? VT_INVALID : VT_INCOMPLETE;
            return tok->kind;
        }
        if(q + 1 >= size) { /* the H or B suffix has not arrived */
            tok->kind = eof ? VT_INVALID : VT_INCOMPLETE;
            return tok->kind;
        }
        if(buf[q + 1] == 'H') tok->kind = VT_HSTRING;
        else if(buf[q + 1] == 'B') tok->kind = VT_BSTRING;
        else {
            tok->kind = VT_INVALID;
            tok->len = q + 1 - p;
            return tok->kind;
        }
        tok->len = q + 2 - p;
        tok->body = buf + p + 1;
        tok->body_len = q - p - 1;
        *pos = p + tok->len;
        return tok->kind;
    }

    if(vt_digit(buf[p]) || (buf[p] == '-' && p + 1 < size && vt_digit(buf[p + 1]))) {
        size_t q = p;
        if(buf[q] == '-') q++;
        while(q < size && vt_digit(buf[q])) q++;
        if(q >= size && !eof) {
            tok->kind = VT_INCOMPLETE; /* more digits may follow */
            return tok->kind;
        }
        tok->kind = VT_NUMBER;
        tok->len = q - p;
        tok->body = buf + p;
        tok->body_len = tok->len;
        *pos = q;
        return tok->kind;
    }

    if(vt_ident_start(buf[p])) {
        size_t q = p;
        while(q < size && vt_ident_char(buf[q])) q++;
        if(q >= size && !eof) {
            tok->kind = VT_INCOMPLETE; /* the identifier may continue */
            return tok->kind;
        }
        tok->kind = VT_IDENT;
        tok->len = q - p;
        tok->body = buf + p;
        tok->body_len = tok->len;
        *pos = q;
        return tok->kind;
    }

    tok->kind = VT_INVALID;
    tok->len = 1;
    return tok->kind;
}

int
vn_token_is(const vn_token_t *tok, const char *word) {
    size_t n = strlen(word);
    return tok->kind == VT_IDENT && tok->body_len == n
           && memcmp(tok->body, word, n) == 0;
}
