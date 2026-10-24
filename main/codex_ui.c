/* 240x320 vertical stack. Key hints stay in the right gutter, beside Up, Down, and OK. */
#include "codex_ui.h"

#include "codex_font.h"
#include "codex_pet.h"

#include <stdio.h>
#include <string.h>

#define BG 0x071422
#define CARD 0x10283C
#define INK 0xE7F6FF
#define MUTED 0x7BA8C4
#define CYAN 0x3AD7FF
#define TRACK 0x143044
#define ORANGE 0xFFB15A
#define RED 0xFF6B7A
#define ROW_H 34
#define ROW_N 8

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *status;
    lv_obj_t *battery_shell;
    lv_obj_t *battery_fill;
    lv_obj_t *battery_text;
    lv_obj_t *title;
    lv_obj_t *state;
    codex_pet_t pet;
    lv_obj_t *usage;
    lv_obj_t *primary_name;
    lv_obj_t *primary_bar;
    lv_obj_t *primary_pct;
    lv_obj_t *primary_reset;
    lv_obj_t *secondary_name;
    lv_obj_t *secondary_bar;
    lv_obj_t *secondary_pct;
    lv_obj_t *secondary_reset;
    lv_obj_t *footer;
    lv_obj_t *hint_up;
    lv_obj_t *hint_down;
    lv_obj_t *hint_ok;
    lv_obj_t *info;
    lv_obj_t *info_rows[ROW_N];
    lv_obj_t *info_text[ROW_N];
    lv_obj_t *modal;
    lv_obj_t *modal_title;
    lv_obj_t *modal_body;
    lv_obj_t *modal_hint;
} codex_ui_t;

static codex_ui_t s_ui;

static void label_font(lv_obj_t *label, const lv_font_t *font, uint32_t color)
{
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    label_font(label, font, INK);
    lv_label_set_text(label, text);
    return label;
}

static uint32_t reset_color(codex_reset_band_t band)
{
    if (band == CODEX_RESET_GREEN) return CYAN;
    if (band == CODEX_RESET_ORANGE) return ORANGE;
    if (band == CODEX_RESET_RED) return RED;
    return MUTED;
}

static const char *pose_text(codex_pose_t pose)
{
    switch (pose) {
    case CODEX_POSE_SLEEP: return "休眠";
    case CODEX_POSE_IDLE: return "空闲";
    case CODEX_POSE_BUSY: return "忙碌";
    case CODEX_POSE_ATTENTION: return "留意";
    case CODEX_POSE_COMPLETED: return "完成";
    case CODEX_POSE_DIZZY: return "晕眩";
    case CODEX_POSE_HEART: return "心动";
    case CODEX_POSE_UNKNOWN: return "未知";
    default: return "未知";
    }
}

static const char *status_text(const codex_model_t *model)
{
    if (!model->link_up) return "未连接";
    if (model->passkey_visible) return "等待";
    if (!model->packet_seen) return "等待";
    if (model->stale) return "链路中断";
    return "实时";
}

static void style_bar(lv_obj_t *bar)
{
    lv_obj_set_size(bar, 176, 14);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(TRACK), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(CYAN), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
}

static void paint_meter(lv_obj_t *bar, lv_obj_t *pct, lv_obj_t *reset, uint8_t percent,
                        const char *label, uint32_t now_ms, const codex_model_t *model, bool secondary)
{
    uint32_t remaining = 0;
    bool known = codex_reset_remaining(model, now_ms, secondary, &remaining);
    char count[32];
    char pct_text[16];
    codex_bar_band_t band = codex_bar_band(percent);
    codex_reset_band_t urgency = codex_reset_band(label, remaining, known);
    lv_bar_set_value(bar, percent, LV_ANIM_OFF);
    (void)band;
    snprintf(pct_text, sizeof pct_text, "%u%%", (unsigned)percent);
    lv_label_set_text(pct, pct_text);
    if (!known) snprintf(count, sizeof count, "还剩 --");
    else if (remaining / 86400u > 0) snprintf(count, sizeof count, "还剩 %u天", (unsigned)(remaining / 86400u));
    else if (remaining >= 3600u) {
        snprintf(count, sizeof count, "还剩 %u时%u分",
                 (unsigned)(remaining / 3600u), (unsigned)((remaining % 3600u) / 60u));
    } else {
        snprintf(count, sizeof count, "还剩 %u分", (unsigned)(remaining / 60u));
    }
    lv_label_set_text(reset, count);
    label_font(reset, &codex_font_16, reset_color(urgency));
}

static const char *bright_name(uint8_t level)
{
    static const char *names[] = {"低", "中", "高", "最亮"};
    if (level > 3) level = 3;
    return names[level];
}

static void append_setting(char *out, size_t out_size, const codex_model_t *model, codex_setting_t item)
{
    char line[48];
    const char *mark = item == model->setting ? ">" : " ";
    const char *name = "不适用";
    const char *value = "不适用";
    size_t used = strlen(out);
    switch (item) {
    case CODEX_SET_BRIGHT:
        name = "亮度";
        value = bright_name(model->brightness);
        break;
    case CODEX_SET_SOUND:
        name = "声音";
        value = model->sound_enabled ? "开" : "关";
        break;
    case CODEX_SET_BLE:
        name = "蓝牙";
        value = model->ble_enabled ? "开" : "关";
        break;
    case CODEX_SET_WIFI:
        name = "无线";
        break;
    case CODEX_SET_LED:
        name = "指示灯";
        break;
    case CODEX_SET_CLOCK:
        name = "时钟";
        break;
    case CODEX_SET_UNPAIR:
        name = "解除配对";
        value = "";
        break;
    case CODEX_SET_RESET:
        name = "恢复设置";
        value = "";
        break;
    default:
        break;
    }
    if (value[0] == '\0') snprintf(line, sizeof line, "%s%s\n", mark, name);
    else snprintf(line, sizeof line, "%s%s %s\n", mark, name, value);
    if (used + 1 < out_size) snprintf(out + used, out_size - used, "%s", line);
}

static const char *source_label(const codex_model_t *model);

static void fill_sheet(const codex_model_t *model, char *out, size_t out_size)
{
    const char *shown = model->device_name[0] ? model->device_name : "--";
    const char *mac = model->mac[0] ? model->mac : "--";
    const char *version = model->fw_version[0] ? model->fw_version : "--";
    if (model->settings_open) {
        out[0] = '\0';
        append_setting(out, out_size, model, CODEX_SET_BRIGHT);
        append_setting(out, out_size, model, CODEX_SET_SOUND);
        append_setting(out, out_size, model, CODEX_SET_BLE);
        append_setting(out, out_size, model, CODEX_SET_WIFI);
        append_setting(out, out_size, model, CODEX_SET_LED);
        append_setting(out, out_size, model, CODEX_SET_CLOCK);
        append_setting(out, out_size, model, CODEX_SET_UNPAIR);
        append_setting(out, out_size, model, CODEX_SET_RESET);
        return;
    }
    switch (model->info) {
    case CODEX_INFO_BUTTONS:
        snprintf(out, out_size,
                 "短按确定翻页\n上下切换姿态\n长按进入设置\n再长按退出\n双击回用量\n上键批准\n下键拒绝\n配对码可关");
        break;
    case CODEX_INFO_DEVICE: {
        char bat[16];
        if (model->battery_percent < 0) snprintf(bat, sizeof bat, "--");
        else snprintf(bat, sizeof bat, "%d%%", model->battery_percent);
        snprintf(out, out_size, "设备\n版本 %s\n电量 %s\n地址 %s", version, bat, mac);
        break;
    }
    case CODEX_INFO_BLE:
        snprintf(out, out_size, "蓝牙\n%s\n六位配对码\n在电脑上输入",
                 model->link_up ? (model->encrypted ? "已加密" : "未加密") : "未连接");
        break;
    case CODEX_INFO_CREDITS:
        snprintf(out, out_size, "署名\nCodex 伙伴\n来自 Stick 移植");
        break;
    case CODEX_INFO_ABOUT:
    default:
        snprintf(out, out_size, "关于\n名称 %s\n版本 %s\n来源 %s", shown, version, source_label(model));
        break;
    }
}

static bool token_is(const char *value, const char *lower)
{
    if (value == NULL) return false;
    while (*lower != '\0') {
        char letter = *value++;
        if (letter >= 'A' && letter <= 'Z') letter = (char)(letter - 'A' + 'a');
        if (letter != *lower++) return false;
    }
    return *value == '\0';
}

static const char *source_label(const codex_model_t *model)
{
    if (token_is(model->via, "herdr")) return "经 Herdr 的 Pi";
    if (token_is(model->agent, "cursor")) return "Cursor";
    if (token_is(model->agent, "codex")) return "Codex";
    if (token_is(model->agent, "hermes")) return "Hermes";
    return "本机 Pi";
}

static void show_lines(const char *text)
{
    char buf[384];
    char *cursor;
    int row = 0;
    snprintf(buf, sizeof buf, "%s", text);
    cursor = buf;
    while (row < ROW_N && cursor[0] != '\0') {
        char *split = strchr(cursor, '\n');
        bool selected;
        if (split != NULL) *split = '\0';
        selected = cursor[0] == '>';
        lv_label_set_text(s_ui.info_text[row], selected ? cursor + 1 : cursor);
        label_font(s_ui.info_text[row], &codex_font_16, selected ? CYAN : INK);
        lv_obj_set_style_bg_color(s_ui.info_rows[row], lv_color_hex(CYAN), 0);
        lv_obj_set_style_bg_opa(s_ui.info_rows[row], selected ? LV_OPA_20 : LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(s_ui.info_rows[row], LV_OBJ_FLAG_HIDDEN);
        row++;
        if (split == NULL) break;
        cursor = split + 1;
    }
    for (; row < ROW_N; row++) lv_obj_add_flag(s_ui.info_rows[row], LV_OBJ_FLAG_HIDDEN);
}

static void show_modal(bool visible)
{
    if (visible) lv_obj_remove_flag(s_ui.modal, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ui.modal, LV_OBJ_FLAG_HIDDEN);
}

void codex_ui_create(void)
{
    s_ui.screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(s_ui.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(s_ui.screen, &codex_font_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ui.screen, lv_color_hex(INK), LV_PART_MAIN);
    lv_obj_remove_flag(s_ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.status = make_label(s_ui.screen, &codex_font_16, "未连接");
    label_font(s_ui.status, &codex_font_16, CYAN);
    lv_obj_set_pos(s_ui.status, 12, 8);

    s_ui.battery_shell = lv_obj_create(s_ui.screen);
    lv_obj_set_size(s_ui.battery_shell, 22, 12);
    lv_obj_set_pos(s_ui.battery_shell, 168, 10);
    lv_obj_set_style_radius(s_ui.battery_shell, 2, 0);
    lv_obj_set_style_bg_opa(s_ui.battery_shell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_ui.battery_shell, lv_color_hex(CYAN), 0);
    lv_obj_set_style_border_width(s_ui.battery_shell, 1, 0);
    lv_obj_remove_flag(s_ui.battery_shell, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.battery_fill = lv_obj_create(s_ui.battery_shell);
    lv_obj_set_size(s_ui.battery_fill, 8, 8);
    lv_obj_set_pos(s_ui.battery_fill, 1, 1);
    lv_obj_set_style_radius(s_ui.battery_fill, 1, 0);
    lv_obj_set_style_bg_color(s_ui.battery_fill, lv_color_hex(CYAN), 0);
    lv_obj_set_style_border_width(s_ui.battery_fill, 0, 0);
    s_ui.battery_text = make_label(s_ui.screen, &codex_font_16, "--");
    label_font(s_ui.battery_text, &codex_font_16, CYAN);
    lv_obj_set_pos(s_ui.battery_text, 194, 6);

    s_ui.title = make_label(s_ui.screen, &codex_font_20, "空闲");
    label_font(s_ui.title, &codex_font_20, CYAN);
    lv_obj_set_pos(s_ui.title, 12, 28);
    s_ui.state = make_label(s_ui.screen, &codex_font_16, "");
    lv_obj_add_flag(s_ui.state, LV_OBJ_FLAG_HIDDEN);

    codex_pet_create(&s_ui.pet, s_ui.screen);
    lv_obj_set_pos(s_ui.pet.root, 8, 52);

    s_ui.usage = lv_obj_create(s_ui.screen);
    lv_obj_set_size(s_ui.usage, 176, 96);
    lv_obj_set_pos(s_ui.usage, 8, 206);
    lv_obj_set_style_bg_opa(s_ui.usage, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_ui.usage, 14, 0);
    lv_obj_set_style_border_width(s_ui.usage, 0, 0);
    lv_obj_set_style_pad_all(s_ui.usage, 0, 0);
    lv_obj_remove_flag(s_ui.usage, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.primary_name = make_label(s_ui.usage, &codex_font_16, "5小时");
    lv_obj_set_pos(s_ui.primary_name, 0, 0);
    label_font(s_ui.primary_name, &codex_font_16, CYAN);
    s_ui.primary_bar = lv_bar_create(s_ui.usage);
    style_bar(s_ui.primary_bar);
    lv_obj_set_pos(s_ui.primary_bar, 0, 18);
    s_ui.primary_pct = make_label(s_ui.usage, &codex_font_16, "0%");
    label_font(s_ui.primary_pct, &codex_font_16, CYAN);
    lv_obj_set_pos(s_ui.primary_pct, 132, 0);
    s_ui.primary_reset = make_label(s_ui.usage, &codex_font_16, "还剩 --");
    lv_obj_set_pos(s_ui.primary_reset, 0, 34);

    s_ui.secondary_name = make_label(s_ui.usage, &codex_font_16, "7天");
    label_font(s_ui.secondary_name, &codex_font_16, CYAN);
    lv_obj_set_pos(s_ui.secondary_name, 0, 50);
    s_ui.secondary_bar = lv_bar_create(s_ui.usage);
    style_bar(s_ui.secondary_bar);
    lv_obj_set_pos(s_ui.secondary_bar, 0, 68);
    s_ui.secondary_pct = make_label(s_ui.usage, &codex_font_16, "0%");
    label_font(s_ui.secondary_pct, &codex_font_16, CYAN);
    lv_obj_set_pos(s_ui.secondary_pct, 132, 50);
    s_ui.secondary_reset = make_label(s_ui.usage, &codex_font_16, "还剩 --");
    lv_obj_set_pos(s_ui.secondary_reset, 0, 82);

    s_ui.footer = make_label(s_ui.screen, &codex_font_16, "");
    lv_obj_add_flag(s_ui.footer, LV_OBJ_FLAG_HIDDEN);
    s_ui.hint_up = make_label(s_ui.screen, &codex_font_16, "姿态");
    s_ui.hint_down = make_label(s_ui.screen, &codex_font_16, "姿态");
    s_ui.hint_ok = make_label(s_ui.screen, &codex_font_16, "翻页");
    label_font(s_ui.hint_up, &codex_font_16, CYAN);
    label_font(s_ui.hint_down, &codex_font_16, CYAN);
    label_font(s_ui.hint_ok, &codex_font_16, CYAN);
    lv_obj_set_width(s_ui.hint_up, 50);
    lv_obj_set_width(s_ui.hint_down, 50);
    lv_obj_set_width(s_ui.hint_ok, 50);
    lv_obj_set_style_text_align(s_ui.hint_up, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_align(s_ui.hint_down, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_align(s_ui.hint_ok, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_long_mode(s_ui.hint_up, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(s_ui.hint_down, LV_LABEL_LONG_CLIP);
    lv_label_set_long_mode(s_ui.hint_ok, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_ui.hint_up, 188, 72);
    lv_obj_set_pos(s_ui.hint_down, 188, 156);
    lv_obj_set_pos(s_ui.hint_ok, 188, 248);

    s_ui.info = lv_obj_create(s_ui.screen);
    lv_obj_set_size(s_ui.info, 176, 272);
    lv_obj_set_pos(s_ui.info, 8, 28);
    lv_obj_set_style_bg_opa(s_ui.info, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_ui.info, 0, 0);
    lv_obj_set_style_pad_all(s_ui.info, 0, 0);
    lv_obj_remove_flag(s_ui.info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.info, LV_OBJ_FLAG_HIDDEN);
    for (int row = 0; row < ROW_N; row++) {
        s_ui.info_rows[row] = lv_obj_create(s_ui.info);
        lv_obj_set_size(s_ui.info_rows[row], 176, ROW_H - 2);
        lv_obj_set_pos(s_ui.info_rows[row], 0, row * ROW_H);
        lv_obj_set_style_radius(s_ui.info_rows[row], 6, 0);
        lv_obj_set_style_border_width(s_ui.info_rows[row], 0, 0);
        lv_obj_set_style_bg_opa(s_ui.info_rows[row], LV_OPA_TRANSP, 0);
        lv_obj_remove_flag(s_ui.info_rows[row], LV_OBJ_FLAG_SCROLLABLE);
        s_ui.info_text[row] = make_label(s_ui.info_rows[row], &codex_font_16, "");
        lv_obj_align(s_ui.info_text[row], LV_ALIGN_LEFT_MID, 8, 0);
    }

    s_ui.modal = lv_obj_create(s_ui.screen);
    lv_obj_set_size(s_ui.modal, 208, 132);
    lv_obj_set_pos(s_ui.modal, 16, 150);
    lv_obj_set_style_bg_color(s_ui.modal, lv_color_hex(0x24384A), 0);
    lv_obj_set_style_radius(s_ui.modal, 16, 0);
    lv_obj_set_style_border_color(s_ui.modal, lv_color_hex(ORANGE), 0);
    lv_obj_set_style_border_width(s_ui.modal, 2, 0);
    lv_obj_remove_flag(s_ui.modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.modal, LV_OBJ_FLAG_HIDDEN);
    s_ui.modal_title = make_label(s_ui.modal, &codex_font_20, "需要批准");
    lv_obj_set_pos(s_ui.modal_title, 10, 8);
    s_ui.modal_body = make_label(s_ui.modal, &codex_font_16, "工具");
    lv_obj_set_width(s_ui.modal_body, 184);
    lv_obj_set_pos(s_ui.modal_body, 10, 40);
    s_ui.modal_hint = make_label(s_ui.modal, &codex_font_16, "");
    lv_obj_set_width(s_ui.modal_hint, 184);
    lv_label_set_long_mode(s_ui.modal_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_ui.modal_hint, 10, 64);
    label_font(s_ui.modal_hint, &codex_font_16, MUTED);
    lv_obj_move_foreground(s_ui.hint_up);
    lv_obj_move_foreground(s_ui.hint_down);
    lv_obj_move_foreground(s_ui.hint_ok);

    lv_screen_load(s_ui.screen);
}

void codex_ui_apply(const codex_model_t *model, uint32_t now_ms)
{
    char battery[16];
    char info[384];
    int fill;
    codex_pose_t pose;
    if (model == NULL || s_ui.screen == NULL) return;

    pose = codex_model_pose(model, now_ms);
    lv_label_set_text(s_ui.status, status_text(model));
    label_font(s_ui.status, &codex_font_16, model->stale ? ORANGE : INK);
    if (model->settings_open || model->page == CODEX_PAGE_INFO) {
        lv_obj_add_flag(s_ui.title, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_ui.title, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_ui.title, pose_text(pose));
    }
    lv_label_set_text(s_ui.state, pose_text(pose));
    codex_pet_set_pose(&s_ui.pet, pose);

    if (model->battery_percent < 0) {
        lv_label_set_text(s_ui.battery_text, "--");
        lv_obj_add_flag(s_ui.battery_fill, LV_OBJ_FLAG_HIDDEN);
    } else {
        snprintf(battery, sizeof battery, "%d%%", model->battery_percent);
        lv_label_set_text(s_ui.battery_text, battery);
        fill = 1 + (model->battery_percent * 18) / 100;
        if (fill > 18) fill = 18;
        lv_obj_set_width(s_ui.battery_fill, fill);
        lv_obj_remove_flag(s_ui.battery_fill, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s_ui.battery_fill,
                                  lv_color_hex(model->battery_percent <= 15 ? RED : CYAN), 0);
    }

    lv_label_set_text(s_ui.primary_name, "5小时");
    lv_label_set_text(s_ui.secondary_name, "7天");
    paint_meter(s_ui.primary_bar, s_ui.primary_pct, s_ui.primary_reset, model->primary,
                model->primary_label, now_ms, model, false);
    paint_meter(s_ui.secondary_bar, s_ui.secondary_pct, s_ui.secondary_reset, model->secondary,
                model->secondary_label, now_ms, model, true);

    if ((model->settings_open || model->page == CODEX_PAGE_INFO) && !model->passkey_visible) {
        lv_obj_add_flag(s_ui.pet.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.usage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ui.info, LV_OBJ_FLAG_HIDDEN);
        fill_sheet(model, info, sizeof info);
        show_lines(info);
    } else if (model->page == CODEX_PAGE_PET) {
        lv_obj_remove_flag(s_ui.pet.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.usage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.info, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_ui.pet.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ui.usage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.info, LV_OBJ_FLAG_HIDDEN);
    }

    if (model->passkey_visible) {
        char code[16];
        snprintf(code, sizeof code, "%06lu", (unsigned long)model->passkey);
        lv_label_set_text(s_ui.modal_title, "配对码");
        lv_label_set_text(s_ui.modal_body, code);
        label_font(s_ui.modal_body, &codex_font_20, INK);
        lv_label_set_text(s_ui.modal_hint, "");
        show_modal(true);
    } else if (model->prompt_active) {
        char tool_line[64];
        snprintf(tool_line, sizeof tool_line, "工具 %s", model->prompt_tool[0] ? model->prompt_tool : "--");
        lv_label_set_text(s_ui.modal_title, "需要批准");
        label_font(s_ui.modal_body, &codex_font_16, INK);
        lv_label_set_text(s_ui.modal_body, tool_line);
        lv_label_set_text(s_ui.modal_hint, model->prompt_hint);
        show_modal(true);
    } else {
        show_modal(false);
    }

    if (model->passkey_visible) {
        lv_label_set_text(s_ui.hint_up, "");
        lv_label_set_text(s_ui.hint_down, "关闭");
        lv_label_set_text(s_ui.hint_ok, "关闭");
    } else if (model->prompt_active) {
        lv_label_set_text(s_ui.hint_up, model->prompt_choose_allow ? ">允许" : "允许");
        lv_label_set_text(s_ui.hint_down, model->prompt_choose_allow ? "拒绝" : ">拒绝");
        lv_label_set_text(s_ui.hint_ok, "执行");
    } else if (model->settings_open) {
        lv_label_set_text(s_ui.hint_up, "上项");
        lv_label_set_text(s_ui.hint_down, "下项");
        lv_label_set_text(s_ui.hint_ok, "执行");
    } else if (model->page == CODEX_PAGE_INFO) {
        lv_label_set_text(s_ui.hint_up, "上项");
        lv_label_set_text(s_ui.hint_down, "下项");
        lv_label_set_text(s_ui.hint_ok, "翻页");
    } else {
        lv_label_set_text(s_ui.hint_up, "姿态");
        lv_label_set_text(s_ui.hint_down, "姿态");
        lv_label_set_text(s_ui.hint_ok, "翻页");
    }
    label_font(s_ui.hint_up, &codex_font_16, CYAN);
    label_font(s_ui.hint_down, &codex_font_16, CYAN);
    label_font(s_ui.hint_ok, &codex_font_16, CYAN);
    if (model->prompt_active && !model->passkey_visible) {
        label_font(model->prompt_choose_allow ? s_ui.hint_down : s_ui.hint_up, &codex_font_16, MUTED);
    }
    lv_obj_move_foreground(s_ui.hint_up);
    lv_obj_move_foreground(s_ui.hint_down);
    lv_obj_move_foreground(s_ui.hint_ok);
}

void codex_ui_destroy(void)
{
    codex_pet_destroy(&s_ui.pet);
    if (s_ui.screen != NULL) {
        lv_obj_delete(s_ui.screen);
        s_ui.screen = NULL;
    }
}
