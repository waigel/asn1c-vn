#include <stdlib.h>
#include <string.h>
#include "vntest.h"

int         vnt_failures = 0;
const char *vnt_case = "(none)";

void
vnt_streq(const char *file, int line, const char *got, const char *want) {
    if(got && want && strcmp(got, want) == 0) return;
    fprintf(stderr, "FAIL [%s] %s:%d:\n  want |%s|\n  got  |%s|\n", vnt_case,
            file, line, want ? want : "(null)", got ? got : "(null)");
    vnt_failures++;
}

int
vnt_report(const char *suite) {
    if(vnt_failures) {
        fprintf(stderr, "%s: %d failure(s)\n", suite, vnt_failures);
        return 1;
    }
    printf("%s: ok\n", suite);
    return 0;
}

int
vnt_append(vnt_str_t *s, const void *data, size_t size) {
    if(s->len + size + 1 > s->cap) {
        size_t cap = s->cap ? s->cap : 256;
        char *nb;
        while(cap < s->len + size + 1) cap <<= 1;
        nb = (char *)realloc(s->buf, cap);
        if(!nb) return -1;
        s->buf = nb;
        s->cap = cap;
    }
    memcpy(s->buf + s->len, data, size);
    s->len += size;
    s->buf[s->len] = '\0';
    return 0;
}

static int
vnt_consume(const void *data, size_t size, void *key) {
    return vnt_append((vnt_str_t *)key, data, size);
}

static char *
vnt_run(const asn_TYPE_descriptor_t *td, const void *sptr,
        const vn_options_t *opts, char *reason, size_t reasonlen, int *ok) {
    vnt_str_t b;
    vn_options_t local;
    asn_enc_rval_t er;

    memset(&b, 0, sizeof b);
    memset(&local, 0, sizeof local);
    if(opts) local = *opts;
    if(reason && reasonlen) {
        reason[0] = '\0';
        local.errbuf = reason;
        local.errlen = reasonlen;
    }
    er = vn_encode(td, sptr, &local, vnt_consume, &b);
    *ok = er.encoded >= 0;
    if(!*ok) {
        free(b.buf);
        return 0;
    }
    if(!b.buf) { /* a zero-byte encode is still a success */
        b.buf = (char *)malloc(1);
        if(b.buf) b.buf[0] = '\0';
    }
    return b.buf;
}

char *
vnt_encode(const asn_TYPE_descriptor_t *td, const void *sptr,
           const vn_options_t *opts, char *reason, size_t reasonlen) {
    int ok = 0;
    return vnt_run(td, sptr, opts, reason, reasonlen, &ok);
}

int
vnt_encode_fails(const asn_TYPE_descriptor_t *td, const void *sptr,
                 const vn_options_t *opts, char *reason, size_t reasonlen) {
    int ok = 0;
    char *s = vnt_run(td, sptr, opts, reason, reasonlen, &ok);
    free(s);
    return !ok;
}
