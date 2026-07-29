#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xerscan.h"

static void
xs_err(char *err, size_t errlen, const char *fmt, ...) {
    va_list ap;
    if(!err || !errlen) return;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

static int
xs_emit(char **out, size_t max, size_t *n, const char *s, size_t len, char *err,
        size_t errlen) {
    char *copy;
    if(*n >= max) {
        xs_err(err, errlen, "more than %u XER scalars", (unsigned)max);
        return 0;
    }
    copy = (char *)malloc(len + 1);
    if(!copy) {
        xs_err(err, errlen, "out of memory");
        return 0;
    }
    memcpy(copy, s, len);
    copy[len] = '\0';
    out[*n] = copy;
    (*n)++;
    return 1;
}

int
xer_scan_scalars(const char *xer, char **out, size_t max, size_t *count,
                 char *err, size_t errlen) {
    const char *p = xer;
    size_t n = 0;

    if(err && errlen) err[0] = '\0';
    *count = 0;

    while(*p) {
        const char *lt, *gt, *name_end;
        int self_closing;

        lt = strchr(p, '<');
        if(!lt) break;
        gt = strchr(lt, '>');
        if(!gt) {
            xs_err(err, errlen, "unterminated XER tag");
            goto fail;
        }

        if(lt[1] == '/') { /* a closing tag: nothing to emit here */
            p = gt + 1;
            continue;
        }

        self_closing = (gt > lt + 1 && gt[-1] == '/');
        name_end = gt - (self_closing ? 1 : 0);

        if(self_closing) {
            /* <true/>, <green/>: the tag name *is* the value. */
            if(!xs_emit(out, max, &n, lt + 1, (size_t)(name_end - lt - 1), err,
                        errlen))
                goto fail;
            p = gt + 1;
            continue;
        }

        /*
         * An opening tag. If the next thing after its content is a closing tag,
         * the content is this element's text; otherwise this element only holds
         * children and contributes no scalar of its own.
         */
        {
            const char *content = gt + 1;
            const char *next_lt = strchr(content, '<');
            if(!next_lt) {
                p = content;
                continue;
            }
            if(next_lt[1] == '/') {
                /*
                 * Distinguish layout whitespace from a whitespace value. An
                 * empty constructed element comes out as `<numbers>\n    </...>`
                 * -- all whitespace, and containing a newline because BASIC-XER
                 * indents. A string value of a single space comes out as
                 * `<printable> </printable>` -- whitespace, but with no newline.
                 * The former is not a value at all; the latter is.
                 */
                const char *q2;
                int only_ws = 1, has_nl = 0;
                for(q2 = content; q2 < next_lt; q2++) {
                    if(*q2 == '\n' || *q2 == '\r') has_nl = 1;
                    else if(*q2 != ' ' && *q2 != '\t') only_ws = 0;
                }
                if(only_ws && has_nl) {
                    p = next_lt;
                    continue;
                }
            }
            if(next_lt[1] == '/') {
                /*
                 * Text content. Strip all whitespace: asn1c wraps long
                 * OCTET STRING and BIT STRING values across lines and separates
                 * hex pairs with spaces.
                 */
                char buf[8192];
                size_t len = 0;
                const char *q;
                for(q = content; q < next_lt; q++) {
                    /*
                     * Drop only the whitespace asn1c introduces for layout:
                     * newlines and tabs when it wraps long hex or indents. A
                     * space is kept, because it can be part of a string value --
                     * stripping it corrupted PrintableString values that contain
                     * one. The hex-pair separator spaces are handled instead by
                     * offering a space-stripped candidate in
                     * xer_norm_candidates.
                     */
                    if(*q == '\t' || *q == '\n' || *q == '\r') continue;
                    if(len >= sizeof buf) {
                        xs_err(err, errlen, "XER text content too long");
                        goto fail;
                    }
                    /* XER is XML, so it escapes the markup characters. */
                    if(*q == '&') {
                        static const struct {
                            const char *entity;
                            char        ch;
                        } ents[] = {{"&amp;", '&'},
                                    {"&lt;", '<'},
                                    {"&gt;", '>'},
                                    {"&quot;", '"'},
                                    {"&apos;", '\''}};
                        size_t e;
                        int matched = 0;
                        for(e = 0; e < sizeof ents / sizeof ents[0]; e++) {
                            size_t n2 = strlen(ents[e].entity);
                            if((size_t)(next_lt - q) >= n2
                               && !strncmp(q, ents[e].entity, n2)) {
                                buf[len++] = ents[e].ch;
                                q += n2 - 1;
                                matched = 1;
                                break;
                            }
                        }
                        if(matched) continue;
                    }
                    buf[len++] = *q;
                }
                if(!xs_emit(out, max, &n, buf, len, err, errlen)) goto fail;
                p = next_lt;
                continue;
            }
            p = content;
        }
    }

    *count = n;
    return 1;

fail:
    while(n > 0) free(out[--n]);
    return 0;
}

static char *
xs_fmt(const char *prefix, const char *body) {
    size_t len = strlen(prefix) + strlen(body) + 1;
    char *s = (char *)malloc(len);
    if(s) {
        strcpy(s, prefix);
        strcat(s, body);
    }
    return s;
}

static int xs_all(const char *s, const char *set) {
    if(!*s) return 0;
    for(; *s; s++)
        if(!strchr(set, *s)) return 0;
    return 1;
}

/* Convert a bit string to "H:hex" when its length divides by 4, else "Z:bits". */
static char *
xs_bits_to_norm(const char *bits) {
    size_t nbits = strlen(bits);
    char *outbuf;
    size_t i;

    if(nbits == 0) return xs_fmt("Z:", "");
    if(nbits % 4) return xs_fmt("Z:", bits);

    outbuf = (char *)malloc(2 + nbits / 4 + 1);
    if(!outbuf) return 0;
    outbuf[0] = 'H';
    outbuf[1] = ':';
    for(i = 0; i < nbits / 4; i++) {
        unsigned v = 0, j;
        for(j = 0; j < 4; j++) v = (v << 1) | (unsigned)(bits[i * 4 + j] - '0');
        outbuf[2 + i] = "0123456789ABCDEF"[v];
    }
    outbuf[2 + nbits / 4] = '\0';
    return outbuf;
}

char *
vn_norm_scalar(const char *s) {
    size_t len;

    if(!s) return 0;
    len = strlen(s);

    /* Booleans and NULL, from either dialect. */
    if(!strcmp(s, "TRUE") || !strcmp(s, "true")) return xs_fmt("B:", "1");
    if(!strcmp(s, "FALSE") || !strcmp(s, "false")) return xs_fmt("B:", "0");
    if(!strcmp(s, "NULL")) return xs_fmt("N:", "");

    /* A value notation cstring keeps its quotes; collapse "" back to ". */
    if(len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        char body[8192];
        size_t i, blen = 0;
        for(i = 1; i + 1 < len; i++) {
            if(blen + 1 >= sizeof body) return 0;
            if(s[i] == '"' && s[i + 1] == '"') i++;
            body[blen++] = s[i];
        }
        body[blen] = '\0';
        return xs_fmt("S:", body);
    }

    /* Value notation hstring and bstring: '..'H / '..'B */
    if(len >= 3 && s[len - 2] == '\'' && s[0] == '\'') {
        char body[8192];
        size_t i, blen = 0;
        /* Pretty mode wraps long hex, so whitespace can appear inside the
         * quotes. X.680 does not treat it as part of the value. */
        for(i = 1; i + 2 < len; i++) {
            char c = s[i];
            if(c == ' ' || c == '\t' || c == '\n' || c == '\r') continue;
            if(blen + 1 >= sizeof body) return 0;
            body[blen++] = c;
        }
        body[blen] = '\0';
        if(s[len - 1] == 'H') {
            if(blen && !xs_all(body, "0123456789ABCDEFabcdef")) return 0;
            return xs_fmt("H:", body);
        }
        if(s[len - 1] == 'B') {
            if(blen && !xs_all(body, "01")) return 0;
            return xs_bits_to_norm(body);
        }
        return 0;
    }

    /*
     * An arc list joined by the VN scanner: "2 23 143 1". Require at least two
     * groups of digits separated by single spaces, so a lone space or a string
     * such as "34 9" -- which arrives quoted and was handled above -- cannot be
     * mistaken for one.
     */
    if(strchr(s, ' ') && xs_all(s, "0123456789 ")) {
        char dotted[512];
        size_t i, j = 0;
        int groups = 0, in_group = 0, well_formed = 1;

        for(i = 0; i < len; i++) {
            if(s[i] == ' ') {
                if(!in_group) { well_formed = 0; break; } /* leading or double space */
                in_group = 0;
            } else {
                if(!in_group) groups++;
                in_group = 1;
            }
        }
        if(!in_group) well_formed = 0; /* trailing space */
        if(well_formed && groups >= 2) {
            for(i = 0; i < len && j + 1 < sizeof dotted; i++)
                dotted[j++] = (s[i] == ' ') ? '.' : s[i];
            dotted[j] = '\0';
            return xs_fmt("O:", dotted);
        }
    }

    /* An XER object identifier is already dotted: 2.23.143.1 */
    if(strchr(s, '.') && xs_all(s, "0123456789.")) return xs_fmt("O:", s);

    /* An integer, in either dialect. */
    if(xs_all(s, "0123456789")
       || (s[0] == '-' && len > 1 && xs_all(s + 1, "0123456789")))
        return xs_fmt("I:", s);

    /*
     * Reaching here from value notation means the scalar was neither quoted nor
     * numeric, so it is text: an identifier, an enum label or a cstring body
     * (the VN scanner strips the quotes).
     */
    return xs_fmt("S:", s);
}

#define XS_ADD(expr)                       \
    do {                                   \
        if(n < max) {                      \
            char *tmp = (expr);            \
            if(tmp) cands[n++] = tmp;      \
        }                                  \
    } while(0)

size_t
xer_norm_candidates(const char *s, char **cands, size_t max) {
    size_t n = 0, len;

    if(!s || max == 0) return 0;
    len = strlen(s);

    /* Unambiguous forms first. */
    if(!strcmp(s, "true")) { XS_ADD(xs_fmt("B:", "1")); return n; }
    if(!strcmp(s, "false")) { XS_ADD(xs_fmt("B:", "0")); return n; }

    /*
     * Everything below is ambiguous, because XER text carries no type. An empty
     * element is an empty string, an empty octet string or an empty bit string;
     * "129" is an integer or two octets; "AB" is a string or one octet; "0110"
     * is two octets, four bits, or the text "0110".
     *
     * Rather than guess, offer every applicable reading and let the caller match
     * the value-notation side against any of them. See the header for why this is
     * safe: character content still has to agree, and the choice of form is
     * pinned by the golden files and the per-type tests.
     */
    /* An empty element is an empty string, an empty octet string, an empty bit
     * string, or a NULL -- asn1c writes NULL as <void></void>. */
    if(len == 0) {
        XS_ADD(xs_fmt("S:", ""));
        XS_ADD(xs_fmt("H:", ""));
        XS_ADD(xs_fmt("Z:", ""));
        XS_ADD(xs_fmt("N:", ""));
        return n;
    }

    if(xs_all(s, "0123456789")
       || (s[0] == '-' && len > 1 && xs_all(s + 1, "0123456789")))
        XS_ADD(xs_fmt("I:", s));

    if(xs_all(s, "0123456789ABCDEF")) XS_ADD(xs_fmt("H:", s));
    if(xs_all(s, "01")) XS_ADD(xs_bits_to_norm(s));
    if(strchr(s, '.') && xs_all(s, "0123456789.")) XS_ADD(xs_fmt("O:", s));

    /*
     * asn1c separates hex pairs with spaces. Offer the space-stripped reading
     * too, without giving up the space-preserving one, since a string value may
     * legitimately contain spaces and hex digits.
     */
    if(strchr(s, ' ') && xs_all(s, "0123456789ABCDEF ")) {
        char packed[8192];
        size_t i, j = 0;
        for(i = 0; i < len && j + 1 < sizeof packed; i++)
            if(s[i] != ' ') packed[j++] = s[i];
        packed[j] = '\0';
        XS_ADD(xs_fmt("H:", packed));
        if(xs_all(packed, "01")) XS_ADD(xs_bits_to_norm(packed));
    }

    /* Any XER text can be the body of a cstring, an identifier or an enum label. */
    XS_ADD(xs_fmt("S:", s));
    if(!strcmp(s, "NULL")) XS_ADD(xs_fmt("N:", ""));

    return n;
}
