/*
 * Homestead: whole-host URL rewrite for the http sysmodule.
 *
 * Linked at a fixed address in free space at the end of the http module's
 * .text (after Nimbus's own patch). Freestanding: no libc, no libgcc.
 *
 * For the Dev-environment account (Homestead's custom server), every URL
 * whose host is, or ends in ".<suffix>" for one of the suffixes below, has
 * the whole host replaced with the server address in hs_server. The result
 * is written to a caller-provided buffer so the URL can grow; the original
 * http code then sizes its own copy from the rewritten URL.
 */

typedef unsigned int size_t;

/* Filled in by the Homestead app at install time. The app replaces the
 * placeholder and NUL-pads the rest of the 64-byte slot. */
__attribute__((section(".hs_slot"), used, aligned(4)))
const char hs_server[64] = "homestead.invalid";

/* Packed list, each entry NUL-terminated, ended by an empty entry. */
static const char suffixes[] =
    "nintendowifi.net\0"
    "nintendo.net\0"
    "pokemon-gl.com\0"
    "pretendo.cc\0";

static size_t hs_strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int hs_tail_matches(const char *host, size_t host_len, const char *suffix) {
    size_t len = hs_strlen(suffix);
    if (host_len < len) return 0;
    const char *tail = host + host_len - len;
    for (size_t i = 0; i < len; i++) {
        char c = tail[i];
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != suffix[i]) return 0;
    }
    /* "nintendo.net" must not match "notnintendo.net" */
    return host_len == len || tail[-1] == '.';
}

/* Returns `out` holding the rewritten URL, or `url` unchanged when the host
 * is not a Nintendo/Pretendo host or the result would not fit. */
__attribute__((section(".text.hs_rewrite"), used))
const char *hs_rewrite(char *out, const char *url, size_t out_size) {
    const char *p = url;
    while (*p && !(p[0] == ':' && p[1] == '/' && p[2] == '/')) p++;
    if (!*p) return url;

    const char *host = p + 3;
    const char *host_end = host;
    while (*host_end && *host_end != '/' && *host_end != ':' && *host_end != '?' && *host_end != '#') host_end++;
    size_t host_len = (size_t)(host_end - host);
    if (host_len == 0) return url;

    int match = 0;
    for (const char *s = suffixes; *s; s += hs_strlen(s) + 1) {
        if (hs_tail_matches(host, host_len, s)) {
            match = 1;
            break;
        }
    }
    size_t server_len = hs_strlen(hs_server);
    if (!match || server_len == 0) return url;

    size_t prefix_len = (size_t)(host - url);
    size_t rest_len = hs_strlen(host_end);
    if (prefix_len + server_len + rest_len + 1 > out_size) return url;

    char *o = out;
    for (size_t i = 0; i < prefix_len; i++) *o++ = url[i];
    for (size_t i = 0; i < server_len; i++) *o++ = hs_server[i];
    for (size_t i = 0; i <= rest_len; i++) *o++ = host_end[i]; /* includes NUL */
    return out;
}
