#include "codex_model.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failed;

static void expect(int cond, const char *what)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", what);
        g_failed = 1;
    }
}

static codex_packet_t must_parse(const char *json)
{
    codex_packet_t packet;
    if (codex_protocol_parse(json, strlen(json), &packet) != 0) {
        fprintf(stderr, "parse failed: %s\n", json);
        exit(1);
    }
    return packet;
}

int main(void)
{
    codex_model_t model;
    codex_action_t action;
    codex_packet_t packet;
    uint32_t remaining = 1;
    char text[32];

    expect(codex_bar_band(0) == CODEX_BAR_BLUE, "0 blue");
    expect(codex_bar_band(34) == CODEX_BAR_BLUE, "34 blue");
    expect(codex_bar_band(35) == CODEX_BAR_GREEN, "35 green");
    expect(codex_bar_band(69) == CODEX_BAR_GREEN, "69 green");
    expect(codex_bar_band(70) == CODEX_BAR_ORANGE, "70 orange");
    expect(codex_reset_band("5h", 3 * 3600 + 1, true) == CODEX_RESET_GREEN, "5h green");
    expect(codex_reset_band("5h", 3 * 3600, true) == CODEX_RESET_ORANGE, "5h orange at 3h");
    expect(codex_reset_band("5h", 3600, true) == CODEX_RESET_RED, "5h red at 1h");
    expect(codex_reset_band("7d", 4 * 86400 + 1, true) == CODEX_RESET_GREEN, "7d green");
    expect(codex_reset_band("7d", 2 * 86400, true) == CODEX_RESET_RED, "7d red at 2d");
    expect(codex_reset_band("5h", 0, false) == CODEX_RESET_UNKNOWN, "unknown reset");

    codex_format_countdown(0, false, text, sizeof text);
    expect(strcmp(text, "--") == 0, "unknown countdown");
    codex_format_countdown(2 * 86400 + 3 * 3600, true, text, sizeof text);
    expect(strcmp(text, "2d 3h") == 0, "days");
    codex_format_countdown(3 * 3600 + 12 * 60, true, text, sizeof text);
    expect(strcmp(text, "3h 12m") == 0, "hours");
    codex_format_countdown(45 * 60 + 8, true, text, sizeof text);
    expect(strcmp(text, "45m 8s") == 0, "minutes");

    expect(codex_pose_from_state("dizzy") == CODEX_POSE_DIZZY, "dizzy pose");
    expect(codex_pose_from_state("nope") == CODEX_POSE_UNKNOWN, "unknown pose");

    codex_model_init(&model);
    expect(!codex_model_is_dim(&model, 1000), "no dim before input");
    codex_model_note_input(&model, 1000);
    expect(!codex_model_is_dim(&model, 1000 + CODEX_DIM_IDLE_MS - 1), "awake inside window");
    expect(codex_model_is_dim(&model, 1000 + CODEX_DIM_IDLE_MS), "auto dim");

    packet = must_parse(
        "{\"state\":\"busy\",\"primary\":40,\"secondary\":12,\"now\":1000,"
        "\"primary_resets_at\":1000,\"secondary_resets_at\":5000,"
        "\"primary_label\":\"5h\",\"secondary_label\":\"7d\"}");
    codex_model_apply_packet(&model, &packet, 2000, &action);
    expect(strcmp(model.state, "busy") == 0, "busy stored");
    expect(model.primary == 40 && model.secondary == 12, "bars stored");
    expect(codex_model_pose(&model, 2000) == CODEX_POSE_BUSY, "busy pose");
    expect(codex_reset_remaining(&model, 2000, false, &remaining) && remaining == 0, "reset due");
    expect(codex_reset_remaining(&model, 2000, true, &remaining) && remaining == 4000, "7d remaining");

    codex_model_tick(&model, 2000 + CODEX_LINK_STALE_MS);
    expect(!model.stale, "stale needs a link");
    codex_model_set_link(&model, true, true);
    codex_model_tick(&model, 2000 + CODEX_LINK_STALE_MS);
    expect(!model.stale, "fresh at 30s");
    codex_model_tick(&model, 2001 + CODEX_LINK_STALE_MS);
    expect(model.stale, "stale after 30s");

    packet = must_parse("{\"prompt\":{\"id\":\"h9\",\"tool\":\"Bash\",\"hint\":\"echo hi\"}}");
    codex_model_apply_packet(&model, &packet, 3000, &action);
    expect(model.prompt_active, "prompt armed");
    expect(!codex_model_is_dim(&model, 999999), "prompt keeps the screen awake");
    codex_model_on_key(&model, CODEX_KEY_UP, 4000, &action);
    expect(action.kind == CODEX_ACTION_PERMISSION, "allow action");
    expect(strcmp(action.decision, "accept") == 0 && strcmp(action.permission_id, "h9") == 0, "accept id");
    expect(!model.prompt_active && model.approvals == 1, "approval counted");
    expect(codex_model_pose(&model, 4000) == CODEX_POSE_HEART, "heart after allow");
    expect(codex_model_pose(&model, 4000 + CODEX_HEART_MS) == CODEX_POSE_BUSY, "heart ends");

    packet = must_parse("{\"prompt\":{\"id\":\"h10\",\"tool\":\"Edit\",\"hint\":\"file\"}}");
    codex_model_apply_packet(&model, &packet, 8000, &action);
    codex_model_on_key(&model, CODEX_KEY_DOWN, 8100, &action);
    expect(strcmp(action.decision, "cancel") == 0 && model.denials == 1, "deny");

    packet = must_parse("{\"state\":\"idle\"}");
    codex_model_apply_packet(&model, &packet, 9000, &action);
    expect(!model.prompt_active, "usage packet keeps prompt cleared");

    expect(model.page == CODEX_PAGE_DASH && !model.settings_open, "start on dashboard");
    codex_model_on_key(&model, CODEX_KEY_OK, 9100, &action);
    expect(model.page == CODEX_PAGE_PET, "ok opens the pet page");
    codex_model_on_key(&model, CODEX_KEY_OK, 9110, &action);
    expect(model.page == CODEX_PAGE_INFO, "ok opens info");
    codex_model_on_key(&model, CODEX_KEY_DOWN, 9120, &action);
    expect(model.info == CODEX_INFO_BUTTONS, "down moves info");
    codex_model_on_key(&model, CODEX_KEY_OK, 9130, &action);
    expect(model.page == CODEX_PAGE_DASH, "ok returns to the dashboard");
    codex_model_on_key(&model, CODEX_KEY_OK_LONG, 9200, &action);
    expect(model.settings_open && !codex_model_is_dim(&model, 9200), "long ok opens settings");
    codex_model_on_key(&model, CODEX_KEY_OK, 9210, &action);
    expect(model.brightness == 3, "ok cycles brightness");
    codex_model_on_key(&model, CODEX_KEY_DOWN, 9220, &action);
    codex_model_on_key(&model, CODEX_KEY_OK, 9230, &action);
    expect(model.setting == CODEX_SET_SOUND && model.brightness == 3, "sound is not available");
    for (int i = 0; i < 5; i++) codex_model_on_key(&model, CODEX_KEY_DOWN, 9300 + i, &action);
    expect(model.setting == CODEX_SET_UNPAIR, "reach unpair");
    codex_model_on_key(&model, CODEX_KEY_OK, 9310, &action);
    expect(action.kind == CODEX_ACTION_NONE && model.confirm == CODEX_CONFIRM_UNPAIR, "unpair asks again");
    codex_model_on_key(&model, CODEX_KEY_DOWN, 9320, &action);
    expect(model.confirm == CODEX_CONFIRM_NONE && model.setting == CODEX_SET_UNPAIR, "down cancels confirm");
    codex_model_on_key(&model, CODEX_KEY_DOWN, 9325, &action);
    expect(model.setting == CODEX_SET_RESET, "down reaches reset");
    codex_model_on_key(&model, CODEX_KEY_OK, 9330, &action);
    codex_model_on_key(&model, CODEX_KEY_OK, 9340, &action);
    expect(action.kind == CODEX_ACTION_RESET_PREFS && model.brightness == 2 && model.ble_enabled, "reset prefs");
    expect(!model.settings_open, "reset closes settings");
    codex_model_note_input(&model, 1000);
    expect(codex_model_is_dim(&model, 1000 + CODEX_DIM_IDLE_MS), "idle still dims");
    codex_model_on_key(&model, CODEX_KEY_OK, 1000 + CODEX_DIM_IDLE_MS + 10, &action);
    expect(!codex_model_is_dim(&model, 1000 + CODEX_DIM_IDLE_MS + 10), "wake consumes the key");
    expect(model.page == CODEX_PAGE_DASH, "wake does not change the page");

    packet = must_parse("{\"cmd\":\"unpair\"}");
    codex_model_apply_packet(&model, &packet, 9400, &action);
    expect(action.kind == CODEX_ACTION_UNPAIR && action.ack_ok, "unpair");
    packet = must_parse("{\"cmd\":\"status\"}");
    codex_model_apply_packet(&model, &packet, 9500, &action);
    expect(action.kind == CODEX_ACTION_STATUS, "status");

    codex_model_set_link(&model, false, false);
    expect(!model.link_up && !model.stale, "disconnect clears stale");
    codex_model_set_battery(&model, -5, -1);
    expect(model.battery_percent == -1 && model.battery_mv == -1, "missing battery");

    if (g_failed) return 1;
    puts("test_codex_model: PASS");
    return 0;
}
