/* Page geometry below is a provisional stack so the layout can be restyled
 * after a direction is chosen. Do not treat coordinates or the 16/20 sizes as final. */
#include "codex_ui.h"

#include "codex_font.h"
#include "codex_pet.h"

#include <stdio.h>
#include <string.h>

#define BG 0x122033
#define CARD 0x1C3148
#define INK 0xF4F0E6
#define MUTED 0x9BB0C3
#define BLUE 0x4C8DFF
#define GREEN 0x3DDC97
#define ORANGE 0xFF9F43
#define RED 0xFF5D73

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
    lv_obj_t *info;
    lv_obj_t *info_text;
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

static uint32_t bar_color(codex_bar_band_t band)
{
    if (band == CODEX_BAR_GREEN) return GREEN;
    if (band == CODEX_BAR_ORANGE) return ORANGE;
    return BLUE;
}

static uint32_t reset_color(codex_reset_band_t band)
{
    if (band == CODEX_RESET_GREEN) return GREEN;
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
    lv_obj_set_size(bar, 128, 12);
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x0E1C2C), LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
}

static void paint_meter(lv_obj_t *bar, lv_obj_t *pct, lv_obj_t *reset, uint8_t percent,
                        const char *label, uint32_t now_ms, const codex_model_t *model, bool secondary)
{
    uint32_t remaining = 0;
    bool known = codex_reset_remaining(model, now_ms, secondary, &remaining);
    char count[20];
    char pct_text[16];
    codex_bar_band_t band = codex_bar_band(percent);
    codex_reset_band_t urgency = codex_reset_band(label, remaining, known);
    lv_bar_set_value(bar, percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_hex(bar_color(band)), LV_PART_INDICATOR);
    snprintf(pct_text, sizeof pct_text, "%u%%", (unsigned)percent);
    lv_label_set_text(pct, pct_text);
    codex_format_countdown(remaining, known, count, sizeof count);
    {
        char shown[32];
        snprintf(shown, sizeof shown, "重置 %s", count);
        lv_label_set_text(reset, shown);
    }
    label_font(reset, &codex_font_16, reset_color(urgency));
}

static const char *page_title(const codex_model_t *model)
{
    if (model->settings_open) return "设置";
    if (model->page == CODEX_PAGE_PET) return "姿态";
    if (model->page == CODEX_PAGE_INFO) return "信息";
    return "用量";
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
        value = model->confirm == CODEX_CONFIRM_UNPAIR ? "再按确定" : "";
        break;
    case CODEX_SET_RESET:
        name = "恢复设置";
        value = model->confirm == CODEX_CONFIRM_RESET ? "再按确定" : "";
        break;
    default:
        break;
    }
    snprintf(line, sizeof line, "%s%s %s\n", mark, name, value);
    if (used + 1 < out_size) snprintf(out + used, out_size - used, "%s", line);
}

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
        snprintf(out, out_size, "按键\n确定翻页\n长按打开设置\n上下切换\n上允许\n下拒绝");
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
        snprintf(out, out_size, "关于\n名称 %s\n版本 %s", shown, version);
        break;
    }
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
    lv_obj_set_pos(s_ui.status, 16, 14);

    s_ui.battery_shell = lv_obj_create(s_ui.screen);
    lv_obj_set_size(s_ui.battery_shell, 22, 12);
    lv_obj_set_pos(s_ui.battery_shell, 168, 16);
    lv_obj_set_style_radius(s_ui.battery_shell, 2, 0);
    lv_obj_set_style_bg_opa(s_ui.battery_shell, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(s_ui.battery_shell, lv_color_hex(INK), 0);
    lv_obj_set_style_border_width(s_ui.battery_shell, 1, 0);
    lv_obj_remove_flag(s_ui.battery_shell, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.battery_fill = lv_obj_create(s_ui.battery_shell);
    lv_obj_set_size(s_ui.battery_fill, 8, 8);
    lv_obj_set_pos(s_ui.battery_fill, 1, 1);
    lv_obj_set_style_radius(s_ui.battery_fill, 1, 0);
    lv_obj_set_style_bg_color(s_ui.battery_fill, lv_color_hex(GREEN), 0);
    lv_obj_set_style_border_width(s_ui.battery_fill, 0, 0);
    s_ui.battery_text = make_label(s_ui.screen, &codex_font_16, "--");
    lv_obj_set_pos(s_ui.battery_text, 194, 12);

    s_ui.title = make_label(s_ui.screen, &codex_font_20, "Codex 伙伴");
    lv_obj_set_pos(s_ui.title, 16, 38);
    s_ui.state = make_label(s_ui.screen, &codex_font_20, "空闲");
    lv_obj_set_pos(s_ui.state, 16, 66);

    codex_pet_create(&s_ui.pet, s_ui.screen);
    lv_obj_set_pos(s_ui.pet.root, 50, 96);

    s_ui.usage = lv_obj_create(s_ui.screen);
    lv_obj_set_size(s_ui.usage, 208, 108);
    lv_obj_set_pos(s_ui.usage, 16, 208);
    lv_obj_set_style_bg_color(s_ui.usage, lv_color_hex(CARD), 0);
    lv_obj_set_style_radius(s_ui.usage, 14, 0);
    lv_obj_set_style_border_width(s_ui.usage, 0, 0);
    lv_obj_set_style_pad_all(s_ui.usage, 8, 0);
    lv_obj_remove_flag(s_ui.usage, LV_OBJ_FLAG_SCROLLABLE);

    s_ui.primary_name = make_label(s_ui.usage, &codex_font_16, "5h");
    lv_obj_set_pos(s_ui.primary_name, 0, 0);
    s_ui.primary_bar = lv_bar_create(s_ui.usage);
    style_bar(s_ui.primary_bar);
    lv_obj_set_pos(s_ui.primary_bar, 36, 4);
    s_ui.primary_pct = make_label(s_ui.usage, &codex_font_16, "0%");
    lv_obj_set_pos(s_ui.primary_pct, 168, 0);
    s_ui.primary_reset = make_label(s_ui.usage, &codex_font_16, "--");
    lv_obj_set_pos(s_ui.primary_reset, 36, 18);

    s_ui.secondary_name = make_label(s_ui.usage, &codex_font_16, "7d");
    lv_obj_set_pos(s_ui.secondary_name, 0, 46);
    s_ui.secondary_bar = lv_bar_create(s_ui.usage);
    style_bar(s_ui.secondary_bar);
    lv_obj_set_pos(s_ui.secondary_bar, 36, 50);
    s_ui.secondary_pct = make_label(s_ui.usage, &codex_font_16, "0%");
    lv_obj_set_pos(s_ui.secondary_pct, 168, 46);
    s_ui.secondary_reset = make_label(s_ui.usage, &codex_font_16, "--");
    lv_obj_set_pos(s_ui.secondary_reset, 36, 64);

    s_ui.footer = make_label(s_ui.screen, &codex_font_16, "上允许  下拒绝");
    label_font(s_ui.footer, &codex_font_16, MUTED);
    lv_obj_set_pos(s_ui.footer, 16, 292);

    s_ui.info = lv_obj_create(s_ui.screen);
    lv_obj_set_size(s_ui.info, 208, 188);
    lv_obj_set_pos(s_ui.info, 16, 96);
    lv_obj_set_style_bg_color(s_ui.info, lv_color_hex(CARD), 0);
    lv_obj_set_style_radius(s_ui.info, 14, 0);
    lv_obj_set_style_border_width(s_ui.info, 0, 0);
    lv_obj_remove_flag(s_ui.info, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_ui.info, LV_OBJ_FLAG_HIDDEN);
    s_ui.info_text = make_label(s_ui.info, &codex_font_16, "令牌");
    lv_obj_set_width(s_ui.info_text, 188);
    lv_label_set_long_mode(s_ui.info_text, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_ui.info_text, 8, 8);

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
    lv_label_set_text(s_ui.title, page_title(model));
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
                                  lv_color_hex(model->battery_percent <= 15 ? RED : GREEN), 0);
    }

    lv_label_set_text(s_ui.primary_name, model->primary_label[0] ? model->primary_label : "5h");
    lv_label_set_text(s_ui.secondary_name, model->secondary_label[0] ? model->secondary_label : "7d");
    paint_meter(s_ui.primary_bar, s_ui.primary_pct, s_ui.primary_reset, model->primary,
                model->primary_label, now_ms, model, false);
    paint_meter(s_ui.secondary_bar, s_ui.secondary_pct, s_ui.secondary_reset, model->secondary,
                model->secondary_label, now_ms, model, true);

    if ((model->settings_open || model->page == CODEX_PAGE_INFO) && !model->passkey_visible) {
        lv_obj_add_flag(s_ui.pet.root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.usage, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ui.info, LV_OBJ_FLAG_HIDDEN);
        fill_sheet(model, info, sizeof info);
        lv_label_set_text(s_ui.info_text, info);
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

    if (model->prompt_active) lv_label_set_text(s_ui.footer, "上允许  下拒绝");
    else if (model->settings_open && model->confirm != CODEX_CONFIRM_NONE) {
        lv_label_set_text(s_ui.footer, "确定确认  下键取消");
    } else if (model->settings_open) lv_label_set_text(s_ui.footer, "上下选择  确定执行");
    else if (model->page == CODEX_PAGE_INFO) lv_label_set_text(s_ui.footer, "上下切换  确定翻页");
    else if (!model->packet_seen && model->link_up) lv_label_set_text(s_ui.footer, "暂无数据");
    else lv_label_set_text(s_ui.footer, "确定翻页  长按设置");
}

void codex_ui_destroy(void)
{
    codex_pet_destroy(&s_ui.pet);
    if (s_ui.screen != NULL) {
        lv_obj_delete(s_ui.screen);
        s_ui.screen = NULL;
    }
}
