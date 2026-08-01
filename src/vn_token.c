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

/*
 * 11.8: "The NON-BREAKING HYPHEN and the HYPHEN-MINUS should be treated as
 * identical in all names", with the note that My-Type is one name written
 * either way. U+2011 is three bytes in UTF-8, so it has to be recognised as a
 * unit rather than character by character.
 */
#define VT_NBH_LEN 3

static int
vt_is_nbh(const char *buf, size_t size, size_t p) {
    return p + VT_NBH_LEN <= size && (unsigned char)buf[p] == 0xe2
           && (unsigned char)buf[p + 1] == 0x80
           && (unsigned char)buf[p + 2] == 0x91;
}

/* A truncated U+2011 at the end of the buffer: the token may still continue. */
static int
vt_nbh_partial(const char *buf, size_t size, size_t p) {
    static const unsigned char nbh[VT_NBH_LEN] = {0xe2, 0x80, 0x91};
    size_t                     i;

    if(size - p >= VT_NBH_LEN) return 0;
    for(i = 0; p + i < size; i++)
        if((unsigned char)buf[p + i] != nbh[i]) return 0;
    return p < size;
}

/* Bytes of the identifier character at p, or 0 if there is none. */
static size_t
vt_ident_run(const char *buf, size_t size, size_t p) {
    if(vt_ident_char(buf[p])) return 1;
    if(vt_is_nbh(buf, size, p)) return VT_NBH_LEN;
    return 0;
}

static int
vt_digit(char c) {
    return c >= '0' && c <= '9';
}

/*
 * Skip whitespace and X.680 comments.
 *
 * 12.6.2 gives the comment two forms. A one-line comment runs to the next "--"
 * or to end of line (12.6.3); a multi-line one is bracketed and nests, and the
 * delimiters of each form are ordinary text inside the other (12.6.4). One that
 * reaches the end of the buffer unterminated may continue in the next chunk, so
 * it counts as incomplete rather than as ended.
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

        if(buf[p] == '/') {
            size_t q;
            int    depth;
            if(p + 1 >= size) {
                /* Either the opener of a comment or simply not a token; the
                 * next chunk decides. */
                *pos = p;
                return eof ? 1 : 0;
            }
            if(buf[p + 1] != '*') {
                *pos = p;
                return 1; /* not a comment; the caller will make sense of it */
            }
            for(q = p + 2, depth = 1; q < size && depth > 0;) {
                if(q + 1 < size && buf[q] == '/' && buf[q + 1] == '*') {
                    depth++;
                    q += 2;
                } else if(q + 1 < size && buf[q] == '*' && buf[q + 1] == '/') {
                    depth--;
                    q += 2;
                } else {
                    q++;
                }
            }
            if(depth > 0 && !eof) {
                *pos = p; /* may continue in the next chunk */
                return 0;
            }
            /* Unterminated at end of input: consume what there is. The value is
             * then missing and the parse fails for that, which is the honest
             * complaint. */
            p = q;
            continue;
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
        for(;;) {
            size_t n;
            if(q >= size) break;
            /* A U+2011 cut in half by the buffer edge is not "no identifier
             * character"; it is one that has not arrived yet. */
            if(vt_nbh_partial(buf, size, q) && !eof) {
                tok->kind = VT_INCOMPLETE;
                return tok->kind;
            }
            n = vt_ident_run(buf, size, q);
            if(!n) break;
            q += n;
        }
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

/*
 * Compare an identifier body with a name from the schema.
 *
 * The single place that knows a NON-BREAKING HYPHEN stands for a HYPHEN-MINUS
 * (11.8); a plain length-and-memcmp cannot, since the two spellings differ in
 * length. Every identifier the reader matches -- member names, alternatives,
 * enumerators, named numbers, named bits -- comes through here.
 */
int
vn_ident_eq(const char *body, size_t body_len, const char *name,
            size_t name_len) {
    size_t i = 0, j = 0;

    while(i < body_len && j < name_len) {
        char   c = body[i];
        size_t adv = 1;
        if(vt_is_nbh(body, body_len, i)) {
            c = '-';
            adv = VT_NBH_LEN;
        }
        if(c != name[j]) return 0;
        i += adv;
        j++;
    }
    return i == body_len && j == name_len;
}

int
vn_token_is(const vn_token_t *tok, const char *word) {
    return tok->kind == VT_IDENT
           && vn_ident_eq(tok->body, tok->body_len, word, strlen(word));
}
