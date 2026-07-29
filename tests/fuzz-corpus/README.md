# Fuzzing regression corpus

Inputs that once broke the reader. Replay them with

    make fuzz-read && ./fuzz-read tests/fuzz-corpus

- `heap-overflow-strtol-past-token` — a token points into the caller's buffer and
  is not NUL-terminated, so `strtol(tok->body, ...)` on the lenient ENUMERATED
  path and `strtoul` on the OID arc path read past the end of the input.
  libFuzzer found it after roughly 300k executions.
