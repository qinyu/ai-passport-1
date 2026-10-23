#include "codex_protocol.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const char *s;
    size_t n;
    size_t i;
} scan_t;

static void skip_ws(scan_t *sc)
{
    while (sc->i < sc->n) {
        char c = sc->s[sc->i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') sc->i++;
        else break;
    }
}

static bool eat(scan_t *sc, char expect)
{
    skip_ws(sc);
    if (sc->i >= sc->n || sc->s[sc->i] != expect) return false;
    sc->i++;
    return true;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parse_hex4(scan_t *sc, uint32_t *out)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (sc->i >= sc->n) return false;
        int n = hex_nibble(sc->s[sc->i++]);
        if (n < 0) return false;
        v = (v << 4) | (uint32_t)n;
    }
    *out = v;
    return true;
}

static int utf8_encode(uint32_t cp, char tmp[4])
{
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -1;
    if (cp < 0x80) {
        tmp[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        tmp[0] = (char)(0xC0 | (cp >> 6));
        tmp[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        tmp[0] = (char)(0xE0 | (cp >> 12));
        tmp[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        tmp[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    tmp[0] = (char)(0xF0 | (cp >> 18));
    tmp[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    tmp[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    tmp[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static size_t utf8_store(char *buf, size_t size, size_t used, uint32_t cp)
{
    char tmp[4];
    int n = utf8_encode(cp, tmp);
    if (n < 0 || buf == NULL || size == 0) return used;
    if (used + (size_t)n >= size) return used;
    memcpy(buf + used, tmp, (size_t)n);
    return used + (size_t)n;
}

static bool decode_utf8(scan_t *sc, unsigned char first, uint32_t *cp)
{
    int need = 0;
    uint32_t v = 0;
    if (first < 0x80) {
        *cp = first;
        return true;
    }
    if ((first & 0xE0) == 0xC0) {
        need = 1;
        v = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        need = 2;
        v = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        need = 3;
        v = first & 0x07;
    } else {
        return false;
    }
    for (int i = 0; i < need; i++) {
        if (sc->i >= sc->n) return false;
        unsigned char c = (unsigned char)sc->s[sc->i];
        if ((c & 0xC0) != 0x80) return false;
        sc->i++;
        v = (v << 6) | (c & 0x3F);
    }
    *cp = v;
    return true;
}

/* Reads a JSON string. buf may be NULL to discard. Truncates on a code point. */
static int parse_string(scan_t *sc, char *buf, size_t buf_size)
{
    size_t used = 0;
    if (!eat(sc, '"')) return -1;
    while (sc->i < sc->n) {
        unsigned char c = (unsigned char)sc->s[sc->i++];
        uint32_t cp = 0;
        if (c == '"') {
            if (buf != NULL && buf_size > 0) {
                if (used >= buf_size) used = buf_size - 1;
                buf[used] = '\0';
            }
            return 0;
        }
        if (c == '\\') {
            if (sc->i >= sc->n) return -1;
            char e = sc->s[sc->i++];
            if (e == '"' || e == '\\' || e == '/') cp = (unsigned char)e;
            else if (e == 'n') cp = '\n';
            else if (e == 't') cp = '\t';
            else if (e == 'r') cp = '\r';
            else if (e == 'b') cp = '\b';
            else if (e == 'f') cp = '\f';
            else if (e == 'u') {
                if (!parse_hex4(sc, &cp)) return -1;
                if (cp >= 0xD800 && cp <= 0xDBFF && sc->i + 6 <= sc->n &&
                    sc->s[sc->i] == '\\' && sc->s[sc->i + 1] == 'u') {
                    size_t save = sc->i;
                    sc->i += 2;
                    uint32_t low = 0;
                    if (parse_hex4(sc, &low) && low >= 0xDC00 && low <= 0xDFFF) {
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
                    } else {
                        sc->i = save;
                    }
                }
            } else {
                return -1;
            }
        } else if (c < 0x20) {
            return -1;
        } else if (!decode_utf8(sc, c, &cp)) {
            return -1;
        }
        if (buf != NULL) used = utf8_store(buf, buf_size, used, cp);
    }
    return -1;
}

static int parse_i64(scan_t *sc, int64_t *out)
{
    skip_ws(sc);
    if (sc->i >= sc->n) return -1;
    int sign = 1;
    if (sc->s[sc->i] == '-') {
        sign = -1;
        sc->i++;
    }
    if (sc->i >= sc->n || sc->s[sc->i] < '0' || sc->s[sc->i] > '9') return -1;
    uint64_t mag = 0;
    bool overflow = false;
    while (sc->i < sc->n && sc->s[sc->i] >= '0' && sc->s[sc->i] <= '9') {
        uint64_t digit = (uint64_t)(sc->s[sc->i] - '0');
        if (mag > (UINT64_MAX - digit) / 10) overflow = true;
        else mag = mag * 10 + digit;
        sc->i++;
    }
    if (sc->i < sc->n && sc->s[sc->i] == '.') {
        sc->i++;
        while (sc->i < sc->n && sc->s[sc->i] >= '0' && sc->s[sc->i] <= '9') sc->i++;
    }
    if (sc->i < sc->n && (sc->s[sc->i] == 'e' || sc->s[sc->i] == 'E')) {
        sc->i++;
        if (sc->i < sc->n && (sc->s[sc->i] == '+' || sc->s[sc->i] == '-')) sc->i++;
        if (sc->i >= sc->n || sc->s[sc->i] < '0' || sc->s[sc->i] > '9') return -1;
        while (sc->i < sc->n && sc->s[sc->i] >= '0' && sc->s[sc->i] <= '9') sc->i++;
    }
    if (overflow) mag = (uint64_t)INT64_MAX;
    if (sign < 0) {
        if (mag > (uint64_t)INT64_MAX + 1) *out = INT64_MIN;
        else *out = -(int64_t)mag;
    } else {
        *out = mag > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)mag;
    }
    return 0;
}

static int skip_value(scan_t *sc);

static int skip_container(scan_t *sc, char open, char close)
{
    int depth = 1;
    if (!eat(sc, open)) return -1;
    while (sc->i < sc->n && depth > 0) {
        skip_ws(sc);
        if (sc->i >= sc->n) return -1;
        char c = sc->s[sc->i];
        if (c == '"') {
            if (parse_string(sc, NULL, 0) != 0) return -1;
            continue;
        }
        if (c == '{') {
            depth++;
            sc->i++;
            continue;
        }
        if (c == '[') {
            depth++;
            sc->i++;
            continue;
        }
        if (c == '}' || c == ']') {
            depth--;
            sc->i++;
            if (depth == 0 && c != close) return -1;
            continue;
        }
        sc->i++;
    }
    return depth == 0 ? 0 : -1;
}

static bool consume_lit(scan_t *sc, const char *lit)
{
    size_t n = strlen(lit);
    if (sc->i + n > sc->n || memcmp(sc->s + sc->i, lit, n) != 0) return false;
    sc->i += n;
    return true;
}

static int skip_value(scan_t *sc)
{
    skip_ws(sc);
    if (sc->i >= sc->n) return -1;
    char c = sc->s[sc->i];
    if (c == '"') return parse_string(sc, NULL, 0);
    if (c == '{') return skip_container(sc, '{', '}');
    if (c == '[') return skip_container(sc, '[', ']');
    if (c == 't') return consume_lit(sc, "true") ? 0 : -1;
    if (c == 'f') return consume_lit(sc, "false") ? 0 : -1;
    if (c == 'n') return consume_lit(sc, "null") ? 0 : -1;
    int64_t ignored = 0;
    return parse_i64(sc, &ignored);
}

static uint32_t clamp_u32(int64_t v)
{
    if (v < 0) return 0;
    if (v > (int64_t)UINT32_MAX) return UINT32_MAX;
    return (uint32_t)v;
}

static uint8_t clamp_pct(int64_t v)
{
    if (v < 0) return 0;
    if (v > 100) return 100;
    return (uint8_t)v;
}

static int parse_prompt(scan_t *sc, codex_packet_t *out)
{
    skip_ws(sc);
    if (sc->i + 4 <= sc->n && memcmp(sc->s + sc->i, "null", 4) == 0) {
        sc->i += 4;
        out->has_prompt = true;
        out->prompt_clear = true;
        out->prompt_id[0] = '\0';
        out->prompt_tool[0] = '\0';
        out->prompt_hint[0] = '\0';
        return 0;
    }
    if (!eat(sc, '{')) return -1;
    out->has_prompt = true;
    out->prompt_clear = false;
    out->prompt_id[0] = '\0';
    out->prompt_tool[0] = '\0';
    out->prompt_hint[0] = '\0';
    skip_ws(sc);
    if (eat(sc, '}')) {
        out->prompt_clear = true;
        return 0;
    }
    while (sc->i < sc->n) {
        char key[16];
        if (parse_string(sc, key, sizeof key) != 0) return -1;
        if (!eat(sc, ':')) return -1;
        if (strcmp(key, "id") == 0) {
            if (parse_string(sc, out->prompt_id, sizeof out->prompt_id) != 0) return -1;
        } else if (strcmp(key, "tool") == 0) {
            if (parse_string(sc, out->prompt_tool, sizeof out->prompt_tool) != 0) return -1;
        } else if (strcmp(key, "hint") == 0) {
            if (parse_string(sc, out->prompt_hint, sizeof out->prompt_hint) != 0) return -1;
        } else if (skip_value(sc) != 0) {
            return -1;
        }
        skip_ws(sc);
        if (eat(sc, '}')) {
            if (out->prompt_id[0] == '\0') out->prompt_clear = true;
            return 0;
        }
        if (!eat(sc, ',')) return -1;
    }
    return -1;
}

static int parse_time(scan_t *sc, codex_packet_t *out)
{
    int64_t epoch = 0;
    int64_t offset = 0;
    if (!eat(sc, '[')) return -1;
    if (parse_i64(sc, &epoch) != 0) return -1;
    if (!eat(sc, ',')) return -1;
    if (parse_i64(sc, &offset) != 0) return -1;
    if (!eat(sc, ']')) return -1;
    out->has_time = true;
    out->time_epoch = clamp_u32(epoch);
    if (offset > 86400) offset = 86400;
    if (offset < -86400) offset = -86400;
    out->time_tz_offset = (int32_t)offset;
    return 0;
}

static int store_key_string(scan_t *sc, char *dst, size_t dst_size)
{
    return parse_string(sc, dst, dst_size);
}

static int apply_key(scan_t *sc, const char *key, codex_packet_t *out)
{
    int64_t num = 0;
    if (strcmp(key, "state") == 0) {
        out->has_state = true;
        return store_key_string(sc, out->state, sizeof out->state);
    }
    if (strcmp(key, "tokens") == 0) {
        if (parse_i64(sc, &num) != 0) return -1;
        out->has_tokens = true;
        out->tokens = clamp_u32(num);
        return 0;
    }
    if (strcmp(key, "primary") == 0) {
        if (parse_i64(sc, &num) != 0) return -1;
        out->has_primary = true;
        out->primary = clamp_pct(num);
        return 0;
    }
    if (strcmp(key, "secondary") == 0) {
        if (parse_i64(sc, &num) != 0) return -1;
        out->has_secondary = true;
        out->secondary = clamp_pct(num);
        return 0;
    }
    if (strcmp(key, "primary_resets_at") == 0) {
        if (parse_i64(sc, &num) != 0) return -1;
        out->has_primary_resets_at = true;
        out->primary_resets_at = clamp_u32(num);
        return 0;
    }
    if (strcmp(key, "secondary_resets_at") == 0) {
        if (parse_i64(sc, &num) != 0) return -1;
        out->has_secondary_resets_at = true;
        out->secondary_resets_at = clamp_u32(num);
        return 0;
    }
    if (strcmp(key, "now") == 0) {
        if (parse_i64(sc, &num) != 0) return -1;
        out->has_now = true;
        out->now_epoch = clamp_u32(num);
        return 0;
    }
    if (strcmp(key, "primary_label") == 0) {
        out->has_primary_label = true;
        return store_key_string(sc, out->primary_label, sizeof out->primary_label);
    }
    if (strcmp(key, "secondary_label") == 0) {
        out->has_secondary_label = true;
        return store_key_string(sc, out->secondary_label, sizeof out->secondary_label);
    }
    if (strcmp(key, "prompt") == 0) return parse_prompt(sc, out);
    if (strcmp(key, "time") == 0) return parse_time(sc, out);
    if (strcmp(key, "cmd") == 0) {
        out->has_cmd = true;
        return store_key_string(sc, out->cmd, sizeof out->cmd);
    }
    if (strcmp(key, "name") == 0) {
        out->has_cmd_name = true;
        return store_key_string(sc, out->cmd_name, sizeof out->cmd_name);
    }
    return skip_value(sc);
}

int codex_protocol_parse(const char *json, size_t length, codex_packet_t *out)
{
    if (json == NULL || out == NULL) return -1;
    memset(out, 0, sizeof(*out));
    scan_t sc = {.s = json, .n = length, .i = 0};
    if (!eat(&sc, '{')) return -1;
    skip_ws(&sc);
    if (eat(&sc, '}')) {
        skip_ws(&sc);
        return sc.i == sc.n ? 0 : -1;
    }
    while (sc.i < sc.n) {
        char key[32];
        if (parse_string(&sc, key, sizeof key) != 0) return -1;
        if (!eat(&sc, ':')) return -1;
        if (apply_key(&sc, key, out) != 0) return -1;
        skip_ws(&sc);
        if (eat(&sc, '}')) {
            skip_ws(&sc);
            return sc.i == sc.n ? 0 : -1;
        }
        if (!eat(&sc, ',')) return -1;
    }
    return -1;
}

static bool append_raw(char *json, size_t size, size_t *used, const char *text)
{
    size_t n = strlen(text);
    if (*used + n >= size) return false;
    memcpy(json + *used, text, n);
    *used += n;
    json[*used] = '\0';
    return true;
}

static bool append_json_string(char *json, size_t size, size_t *used, const char *text)
{
    if (!append_raw(json, size, used, "\"")) return false;
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) {
        char esc[8];
        const char *chunk = NULL;
        if (*p == '"' || *p == '\\') {
            esc[0] = '\\';
            esc[1] = (char)*p;
            esc[2] = '\0';
            chunk = esc;
        } else if (*p < 0x20) {
            snprintf(esc, sizeof esc, "\\u%04x", *p);
            chunk = esc;
        } else {
            esc[0] = (char)*p;
            esc[1] = '\0';
            chunk = esc;
        }
        if (!append_raw(json, size, used, chunk)) return false;
    }
    return append_raw(json, size, used, "\"");
}

int codex_protocol_permission_json(char *json, size_t size, const char *id,
                                   const char *decision)
{
    size_t used = 0;
    if (json == NULL || size == 0 || id == NULL || id[0] == '\0' || decision == NULL) return -1;
    if (strcmp(decision, "accept") != 0 && strcmp(decision, "cancel") != 0) return -1;
    json[0] = '\0';
    if (!append_raw(json, size, &used, "{\"cmd\":\"permission\",\"id\":")) return -1;
    if (!append_json_string(json, size, &used, id)) return -1;
    if (!append_raw(json, size, &used, ",\"decision\":")) return -1;
    if (!append_json_string(json, size, &used, decision)) return -1;
    if (!append_raw(json, size, &used, "}")) return -1;
    return 0;
}

int codex_protocol_ack_json(char *json, size_t size, const char *cmd, bool ok)
{
    size_t used = 0;
    if (json == NULL || size == 0 || cmd == NULL || cmd[0] == '\0') return -1;
    json[0] = '\0';
    if (!append_raw(json, size, &used, "{\"ack\":")) return -1;
    if (!append_json_string(json, size, &used, cmd)) return -1;
    if (!append_raw(json, size, &used, ok ? ",\"ok\":true,\"n\":0}" : ",\"ok\":false,\"n\":0}")) return -1;
    return 0;
}

int codex_protocol_status_json(char *json, size_t size, const char *name, bool encrypted,
                               int pct, int mv, uint32_t approvals, uint32_t denials)
{
    size_t used = 0;
    char num[96];
    if (json == NULL || size == 0) return -1;
    json[0] = '\0';
    if (!append_raw(json, size, &used, "{\"ack\":\"status\",\"ok\":true,\"data\":{\"name\":")) return -1;
    if (!append_json_string(json, size, &used, name != NULL ? name : "Codex")) return -1;
    snprintf(num, sizeof num, ",\"sec\":%s", encrypted ? "true" : "false");
    if (!append_raw(json, size, &used, num)) return -1;
    if (pct >= 0 && mv >= 0) {
        snprintf(num, sizeof num, ",\"bat\":{\"pct\":%d,\"mV\":%d}", pct, mv);
        if (!append_raw(json, size, &used, num)) return -1;
    } else if (pct >= 0) {
        snprintf(num, sizeof num, ",\"bat\":{\"pct\":%d}", pct);
        if (!append_raw(json, size, &used, num)) return -1;
    } else if (mv >= 0) {
        snprintf(num, sizeof num, ",\"bat\":{\"mV\":%d}", mv);
        if (!append_raw(json, size, &used, num)) return -1;
    }
    snprintf(num, sizeof num, ",\"stats\":{\"appr\":%u,\"deny\":%u}}}",
             (unsigned)approvals, (unsigned)denials);
    if (!append_raw(json, size, &used, num)) return -1;
    return 0;
}
