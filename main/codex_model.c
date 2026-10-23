#include "codex_model.h"

#include <stdio.h>
#include <string.h>

void codex_action_clear(codex_action_t *action)
{
    if (action == NULL) return;
    memset(action, 0, sizeof(*action));
}

void codex_model_init(codex_model_t *model)
{
    if (model == NULL) return;
    memset(model, 0, sizeof(*model));
    memcpy(model->state, "idle", 5);
    memcpy(model->primary_label, "5h", 3);
    memcpy(model->secondary_label, "7d", 3);
    model->battery_percent = -1;
    model->battery_mv = -1;
}

void codex_model_note_input(codex_model_t *model, uint32_t now_ms)
{
    if (model == NULL) return;
    model->input_seen = true;
    model->last_input_ms = now_ms;
    model->forced_dim = false;
}

void codex_model_set_link(codex_model_t *model, bool up, bool encrypted)
{
    if (model == NULL) return;
    model->link_up = up;
    model->encrypted = up && encrypted;
    if (!up) {
        model->passkey_visible = false;
        model->stale = false;
    }
}

void codex_model_set_passkey(codex_model_t *model, bool visible, uint32_t passkey)
{
    if (model == NULL) return;
    model->passkey_visible = visible;
    model->passkey = visible ? (passkey % 1000000u) : 0;
}

void codex_model_set_battery(codex_model_t *model, int percent, int mv)
{
    if (model == NULL) return;
    if (percent < 0) model->battery_percent = -1;
    else if (percent > 100) model->battery_percent = 100;
    else model->battery_percent = percent;
    model->battery_mv = mv < 0 ? -1 : mv;
}

static void copy_field(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0) return;
    if (src == NULL) src = "";
    snprintf(dst, dst_size, "%s", src);
}

static void clear_prompt(codex_model_t *model)
{
    model->prompt_active = false;
    model->prompt_id[0] = '\0';
    model->prompt_tool[0] = '\0';
    model->prompt_hint[0] = '\0';
}

void codex_model_apply_packet(codex_model_t *model, const codex_packet_t *packet,
                              uint32_t now_ms, codex_action_t *action)
{
    if (model == NULL || packet == NULL) return;
    codex_action_clear(action);
    model->packet_seen = true;
    model->last_packet_ms = now_ms;
    model->stale = false;

    if (packet->has_time || packet->has_now) {
        model->clock_valid = true;
        model->epoch_at_sync = packet->has_time ? packet->time_epoch : packet->now_epoch;
        model->sync_ms = now_ms;
    }
    if (packet->has_state && packet->state[0] != '\0') copy_field(model->state, sizeof model->state, packet->state);
    if (packet->has_tokens) model->tokens = packet->tokens;
    if (packet->has_primary) model->primary = packet->primary;
    if (packet->has_secondary) model->secondary = packet->secondary;
    if (packet->has_primary_resets_at) model->primary_resets_at = packet->primary_resets_at;
    if (packet->has_secondary_resets_at) model->secondary_resets_at = packet->secondary_resets_at;
    if (packet->has_primary_label && packet->primary_label[0] != '\0') {
        copy_field(model->primary_label, sizeof model->primary_label, packet->primary_label);
    }
    if (packet->has_secondary_label && packet->secondary_label[0] != '\0') {
        copy_field(model->secondary_label, sizeof model->secondary_label, packet->secondary_label);
    }
    if (packet->has_prompt) {
        if (packet->prompt_clear || packet->prompt_id[0] == '\0') clear_prompt(model);
        else {
            model->prompt_active = true;
            copy_field(model->prompt_id, sizeof model->prompt_id, packet->prompt_id);
            copy_field(model->prompt_tool, sizeof model->prompt_tool, packet->prompt_tool);
            copy_field(model->prompt_hint, sizeof model->prompt_hint, packet->prompt_hint);
        }
    }
    if (packet->has_cmd && action != NULL) {
        if (strcmp(packet->cmd, "status") == 0) {
            action->kind = CODEX_ACTION_STATUS;
        } else if (strcmp(packet->cmd, "unpair") == 0) {
            action->kind = CODEX_ACTION_UNPAIR;
            copy_field(action->ack_cmd, sizeof action->ack_cmd, "unpair");
            action->ack_ok = true;
        } else if (strcmp(packet->cmd, "name") == 0 || strcmp(packet->cmd, "owner") == 0) {
            action->kind = CODEX_ACTION_ACK;
            copy_field(action->ack_cmd, sizeof action->ack_cmd, packet->cmd);
            action->ack_ok = true;
        } else {
            action->kind = CODEX_ACTION_ACK;
            copy_field(action->ack_cmd, sizeof action->ack_cmd, packet->cmd[0] ? packet->cmd : "cmd");
            action->ack_ok = false;
        }
    }
}

void codex_model_tick(codex_model_t *model, uint32_t now_ms)
{
    if (model == NULL) return;
    if (model->link_up && model->packet_seen) {
        model->stale = (uint32_t)(now_ms - model->last_packet_ms) > CODEX_LINK_STALE_MS;
    }
}

static void decide(codex_model_t *model, const char *decision, uint32_t now_ms, codex_action_t *action)
{
    if (action != NULL) {
        action->kind = CODEX_ACTION_PERMISSION;
        copy_field(action->permission_id, sizeof action->permission_id, model->prompt_id);
        copy_field(action->decision, sizeof action->decision, decision);
    }
    if (strcmp(decision, "accept") == 0) {
        model->approvals++;
        model->heart_until_valid = true;
        model->heart_until_ms = now_ms + CODEX_HEART_MS;
    } else {
        model->denials++;
    }
    clear_prompt(model);
}

void codex_model_on_key(codex_model_t *model, codex_key_t key, uint32_t now_ms, codex_action_t *action)
{
    bool was_dim;
    if (model == NULL) return;
    codex_action_clear(action);
    was_dim = codex_model_is_dim(model, now_ms);
    model->input_seen = true;
    model->last_input_ms = now_ms;
    model->forced_dim = false;
    if (was_dim) return;

    if (key == CODEX_KEY_OK_LONG) {
        model->forced_dim = true;
        return;
    }
    if (model->prompt_active && key == CODEX_KEY_UP) {
        decide(model, "accept", now_ms, action);
        return;
    }
    if (model->prompt_active && key == CODEX_KEY_DOWN) {
        decide(model, "cancel", now_ms, action);
        return;
    }
    if (key == CODEX_KEY_OK && !model->prompt_active && !model->passkey_visible) {
        model->info_page = !model->info_page;
    }
}

codex_pose_t codex_pose_from_state(const char *state)
{
    if (state == NULL) return CODEX_POSE_UNKNOWN;
    if (strcmp(state, "sleep") == 0) return CODEX_POSE_SLEEP;
    if (strcmp(state, "idle") == 0) return CODEX_POSE_IDLE;
    if (strcmp(state, "busy") == 0) return CODEX_POSE_BUSY;
    if (strcmp(state, "attention") == 0) return CODEX_POSE_ATTENTION;
    if (strcmp(state, "completed") == 0) return CODEX_POSE_COMPLETED;
    if (strcmp(state, "dizzy") == 0) return CODEX_POSE_DIZZY;
    if (strcmp(state, "heart") == 0) return CODEX_POSE_HEART;
    return CODEX_POSE_UNKNOWN;
}

codex_pose_t codex_model_pose(const codex_model_t *model, uint32_t now_ms)
{
    if (model == NULL) return CODEX_POSE_UNKNOWN;
    if (model->heart_until_valid && (int32_t)(model->heart_until_ms - now_ms) > 0) return CODEX_POSE_HEART;
    return codex_pose_from_state(model->state);
}

codex_bar_band_t codex_bar_band(uint8_t percent)
{
    if (percent <= 34) return CODEX_BAR_BLUE;
    if (percent <= 69) return CODEX_BAR_GREEN;
    return CODEX_BAR_ORANGE;
}

codex_reset_band_t codex_reset_band(const char *label, uint32_t remaining_sec, bool known)
{
    if (!known) return CODEX_RESET_UNKNOWN;
    if (label != NULL && strcmp(label, "7d") == 0) {
        if (remaining_sec > 4u * 86400u) return CODEX_RESET_GREEN;
        if (remaining_sec > 2u * 86400u) return CODEX_RESET_ORANGE;
        return CODEX_RESET_RED;
    }
    if (remaining_sec > 3u * 3600u) return CODEX_RESET_GREEN;
    if (remaining_sec > 1u * 3600u) return CODEX_RESET_ORANGE;
    return CODEX_RESET_RED;
}

bool codex_estimate_epoch(const codex_model_t *model, uint32_t now_ms, uint32_t *epoch_out)
{
    uint32_t elapsed;
    if (model == NULL || epoch_out == NULL || !model->clock_valid) return false;
    elapsed = (now_ms - model->sync_ms) / 1000u;
    *epoch_out = model->epoch_at_sync + elapsed;
    return true;
}

bool codex_reset_remaining(const codex_model_t *model, uint32_t now_ms, bool secondary,
                           uint32_t *remaining_out)
{
    uint32_t epoch = 0;
    uint32_t at;
    if (remaining_out == NULL || !codex_estimate_epoch(model, now_ms, &epoch)) return false;
    at = secondary ? model->secondary_resets_at : model->primary_resets_at;
    if (at == 0) return false;
    *remaining_out = at > epoch ? at - epoch : 0;
    return true;
}

void codex_format_countdown(uint32_t remaining_sec, bool known, char *out, size_t out_size)
{
    uint32_t days;
    uint32_t hours;
    uint32_t minutes;
    uint32_t seconds;
    if (out == NULL || out_size == 0) return;
    if (!known) {
        snprintf(out, out_size, "--");
        return;
    }
    days = remaining_sec / 86400u;
    hours = (remaining_sec % 86400u) / 3600u;
    minutes = (remaining_sec % 3600u) / 60u;
    seconds = remaining_sec % 60u;
    if (days > 0) snprintf(out, out_size, "%ud %uh", (unsigned)days, (unsigned)hours);
    else if (hours > 0) snprintf(out, out_size, "%uh %um", (unsigned)hours, (unsigned)minutes);
    else snprintf(out, out_size, "%um %us", (unsigned)minutes, (unsigned)seconds);
}

bool codex_model_is_dim(const codex_model_t *model, uint32_t now_ms)
{
    if (model == NULL) return false;
    if (model->prompt_active || model->passkey_visible) return false;
    if (model->forced_dim) return true;
    if (!model->input_seen) return false;
    return (uint32_t)(now_ms - model->last_input_ms) >= CODEX_DIM_IDLE_MS;
}
