#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Flat Codex Buddy packets from the host BLE bridge.
 * One JSON object per line. Unknown keys are ignored. Nested objects other
 * than "prompt", and arrays other than "time", are skipped. */

#define CODEX_STATE_MAX 16
#define CODEX_LABEL_MAX 8
#define CODEX_PROMPT_ID_MAX 40
#define CODEX_PROMPT_TOOL_MAX 20
#define CODEX_PROMPT_HINT_MAX 48
#define CODEX_CMD_MAX 16
#define CODEX_CMD_NAME_MAX 24

typedef struct {
    bool has_state;
    char state[CODEX_STATE_MAX];
    bool has_tokens;
    uint32_t tokens;
    bool has_primary;
    uint8_t primary;
    bool has_secondary;
    uint8_t secondary;
    bool has_primary_resets_at;
    uint32_t primary_resets_at;
    bool has_secondary_resets_at;
    uint32_t secondary_resets_at;
    bool has_now;
    uint32_t now_epoch;
    bool has_primary_label;
    char primary_label[CODEX_LABEL_MAX];
    bool has_secondary_label;
    char secondary_label[CODEX_LABEL_MAX];
    bool has_agent;
    char agent[CODEX_STATE_MAX];
    bool has_via;
    char via[CODEX_STATE_MAX];
    bool has_prompt;
    bool prompt_clear;
    char prompt_id[CODEX_PROMPT_ID_MAX];
    char prompt_tool[CODEX_PROMPT_TOOL_MAX];
    char prompt_hint[CODEX_PROMPT_HINT_MAX];
    bool has_cmd;
    char cmd[CODEX_CMD_MAX];
    bool has_cmd_name;
    char cmd_name[CODEX_CMD_NAME_MAX];
    bool has_time;
    uint32_t time_epoch;
    int32_t time_tz_offset;
} codex_packet_t;

/* 0 on a single JSON object, -1 when the line is not usable. */
int codex_protocol_parse(const char *json, size_t length, codex_packet_t *out);

/* Device -> host. decision is "accept" or "cancel", matching the Stick firmware. */
int codex_protocol_permission_json(char *json, size_t size, const char *id,
                                   const char *decision);

int codex_protocol_ack_json(char *json, size_t size, const char *cmd, bool ok);

/* pct < 0 or mv < 0 omits that battery field. */
int codex_protocol_status_json(char *json, size_t size, const char *name, bool encrypted,
                               int pct, int mv, uint32_t approvals, uint32_t denials);
