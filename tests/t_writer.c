/*
 * t_writer.c -- the writer in isolation: indentation, mode policy, comments
 * and error stickiness. No ASN.1 types involved.
 */
#include <string.h>
#include "vntest.h"
#include "vn_internal.h"

typedef struct { char buf[512]; size_t len; } sink_t;

static int
sink_consume(const void *data, size_t size, void *key) {
    sink_t *s = (sink_t *)key;
    if(s->len + size >= sizeof(s->buf)) return -1;
    memcpy(s->buf + s->len, data, size);
    s->len += size;
    s->buf[s->len] = '\0';
    return 0;
}

static int
refusing_consume(const void *d, size_t n, void *k) {
    (void)d; (void)n; (void)k;
    return -1;
}

int
main(void) {
    sink_t s;
    vn_writer_t w;
    vn_options_t o;
    char reason[128];

    memset(&s, 0, sizeof s);
    vn_writer_init(&w, 0, sink_consume, &s);
    VNT_CASE("pretty break indents 4 per level");
    vn_puts(&w, "{");
    vn_break(&w, 1);
    vn_puts(&w, "a 1");
    vn_break(&w, 0);
    vn_puts(&w, "}");
    VNT_STREQ(s.buf, "{\n    a 1\n}");
    VNT_TRUE(w.written == strlen("{\n    a 1\n}"));

    VNT_CASE("indent_width is honoured in pretty mode");
    memset(&s, 0, sizeof s);
    memset(&o, 0, sizeof o);
    o.mode = VN_MODE_PRETTY;
    o.indent_width = 2;
    vn_writer_init(&w, &o, sink_consume, &s);
    vn_break(&w, 3);
    vn_puts(&w, "x");
    VNT_STREQ(s.buf, "\n      x");

    VNT_CASE("deep indent exceeds the internal space chunk");
    memset(&s, 0, sizeof s);
    vn_writer_init(&w, 0, sink_consume, &s);
    vn_break(&w, 6); /* 24 spaces, more than the 16-byte chunk */
    vn_puts(&w, "x");
    VNT_STREQ(s.buf, "\n                        x");

    VNT_CASE("canonical ignores indent_width and line_width");
    memset(&s, 0, sizeof s);
    memset(&o, 0, sizeof o);
    o.mode = VN_MODE_CANONICAL;
    o.indent_width = 9;
    o.line_width = 20;
    vn_writer_init(&w, &o, sink_consume, &s);
    vn_break(&w, 2);
    vn_puts(&w, "x");
    VNT_STREQ(s.buf, "\n    x");
    VNT_TRUE(w.indent_width == 2);
    VNT_TRUE(w.line_width == 0);

    VNT_CASE("pretty suppresses comments");
    memset(&s, 0, sizeof s);
    memset(&o, 0, sizeof o);
    o.mode = VN_MODE_PRETTY;
    vn_writer_init(&w, &o, sink_consume, &s);
    VNT_TRUE(vn_is_annotated(&w) == 0);
    VNT_TRUE(vn_comment(&w, "Type %s", "Holder") == 0);
    VNT_STREQ(s.buf, "");

    VNT_CASE("annotated emits the inline comment form");
    memset(&s, 0, sizeof s);
    o.mode = VN_MODE_ANNOTATED;
    vn_writer_init(&w, &o, sink_consume, &s);
    VNT_TRUE(vn_is_annotated(&w) == 1);
    vn_comment(&w, "Type %s", "Holder");
    VNT_STREQ(s.buf, "-- Type Holder --");

    /* X.680 11.6: a comment ends at the first "--", so embedded ones would
     * truncate the comment and leave the remainder as stray syntax. */
    VNT_CASE("a double hyphen inside a comment is defused");
    memset(&s, 0, sizeof s);
    vn_writer_init(&w, &o, sink_consume, &s);
    vn_comment(&w, "a--b");
    VNT_STREQ(s.buf, "-- a~-b --");

    VNT_CASE("printf formats and counts");
    memset(&s, 0, sizeof s);
    vn_writer_init(&w, 0, sink_consume, &s);
    vn_printf(&w, "%d/%s", 42, "x");
    VNT_STREQ(s.buf, "42/x");
    VNT_TRUE(w.written == 4);

    VNT_CASE("an over-long formatted value fails rather than truncating");
    memset(&s, 0, sizeof s);
    memset(&o, 0, sizeof o);
    o.errbuf = reason;
    o.errlen = sizeof reason;
    vn_writer_init(&w, &o, sink_consume, &s);
    VNT_TRUE(vn_printf(&w, "%300d", 1) == -1);
    VNT_TRUE(strstr(reason, "exceeds") != 0);

    VNT_CASE("sink failure is sticky and suppresses later output");
    memset(&s, 0, sizeof s);
    vn_writer_init(&w, 0, refusing_consume, &s);
    VNT_TRUE(vn_puts(&w, "a") == -1);
    VNT_TRUE(w.failed == 1);
    VNT_TRUE(vn_puts(&w, "b") == -1);
    VNT_TRUE(vn_break(&w, 1) == -1);
    VNT_STREQ(s.buf, "");

    VNT_CASE("writing zero bytes is not an error");
    memset(&s, 0, sizeof s);
    vn_writer_init(&w, 0, refusing_consume, &s);
    VNT_TRUE(vn_put(&w, "", 0) == 0);
    VNT_TRUE(w.failed == 0);

    VNT_CASE("the first failure reason wins");
    memset(&s, 0, sizeof s);
    memset(&o, 0, sizeof o);
    o.errbuf = reason;
    o.errlen = sizeof reason;
    vn_writer_init(&w, &o, sink_consume, &s);
    VNT_TRUE(vn_fail(&w, 0, 0, "first %d", 1) == -1);
    VNT_TRUE(vn_fail(&w, 0, 0, "second") == -1);
    VNT_STREQ(reason, "first 1");

    VNT_CASE("a missing callback is reported, not dereferenced");
    memset(&o, 0, sizeof o);
    o.errbuf = reason;
    o.errlen = sizeof reason;
    vn_writer_init(&w, &o, 0, 0);
    VNT_TRUE(vn_puts(&w, "a") == -1);
    VNT_TRUE(strstr(reason, "callback") != 0);

    return vnt_report("t_writer");
}
