#include "codex_pet.h"

#include "codex_font.h"

#include <string.h>

#define PET_BG 0xF6E7C1
#define PET_INK 0x1B2430
#define PET_EYE 0xF7F4EE
#define PET_CHEEK 0xE7A38A

static void style_round(lv_obj_t *obj, int w, int h, uint32_t color)
{
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void place(lv_obj_t *obj, int x, int y)
{
    lv_obj_set_pos(obj, x, y);
}

static void apply_pose(codex_pet_t *pet)
{
    uint32_t body = PET_BG;
    const char *badge = "";
    int bob = 0;
    int pupil = 6;
    int eye_h = 16;
    bool eyes_shut = false;

    if (pet->pose == CODEX_POSE_BUSY) bob = (pet->phase % 2) == 0 ? -4 : 2;
    else if (pet->pose == CODEX_POSE_IDLE || pet->pose == CODEX_POSE_UNKNOWN) bob = (pet->phase % 4) == 0 ? -2 : 0;
    else if (pet->pose == CODEX_POSE_COMPLETED) bob = (pet->phase % 3) == 0 ? -6 : 0;
    else if (pet->pose == CODEX_POSE_ATTENTION) bob = 0;
    else if (pet->pose == CODEX_POSE_DIZZY) bob = (pet->phase % 2) == 0 ? -2 : 3;
    else if (pet->pose == CODEX_POSE_HEART) bob = -3;
    else bob = 1;

    switch (pet->pose) {
    case CODEX_POSE_BUSY:
        body = 0xF3D48A;
        break;
    case CODEX_POSE_ATTENTION:
        badge = "!";
        eye_h = 18;
        break;
    case CODEX_POSE_COMPLETED:
        body = 0xD7E7C8;
        badge = "OK";
        break;
    case CODEX_POSE_DIZZY:
        body = 0xE7D7F2;
        pupil = (pet->phase % 2) == 0 ? 2 : 10;
        break;
    case CODEX_POSE_HEART:
        body = 0xF6D0D4;
        badge = "心";
        break;
    case CODEX_POSE_SLEEP:
        eyes_shut = true;
        badge = "z";
        break;
    case CODEX_POSE_IDLE:
    case CODEX_POSE_UNKNOWN:
    default:
        break;
    }

    style_round(pet->body, 86, 78, body);
    place(pet->body, 27, 22 + bob);
    place(pet->ear_l, 30, 12 + bob);
    place(pet->ear_r, 86, 12 + bob);
    place(pet->shadow, 34, 96);

    if (eyes_shut) {
        lv_obj_set_size(pet->eye_l, 16, 4);
        lv_obj_set_size(pet->eye_r, 16, 4);
        place(pet->eye_l, 42, 50 + bob);
        place(pet->eye_r, 76, 50 + bob);
        lv_obj_add_flag(pet->pupil_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pet->pupil_r, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_set_size(pet->eye_l, 16, eye_h);
        lv_obj_set_size(pet->eye_r, 16, eye_h);
        place(pet->eye_l, 42, 44 + bob);
        place(pet->eye_r, 76, 44 + bob);
        lv_obj_remove_flag(pet->pupil_l, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(pet->pupil_r, LV_OBJ_FLAG_HIDDEN);
        place(pet->pupil_l, 46 + (pupil > 6 ? 2 : 0), 50 + bob);
        place(pet->pupil_r, 80 - (pupil < 6 ? 2 : 0), 50 + bob);
    }
    place(pet->mouth, 58, 68 + bob);
    lv_label_set_text(pet->badge, badge);
    lv_obj_align_to(pet->badge, pet->body, LV_ALIGN_OUT_TOP_RIGHT, 8, -2);
}

static void pet_timer(lv_timer_t *timer)
{
    codex_pet_t *pet = lv_timer_get_user_data(timer);
    pet->phase++;
    apply_pose(pet);
}

void codex_pet_create(codex_pet_t *pet, lv_obj_t *parent)
{
    memset(pet, 0, sizeof(*pet));
    pet->pose = CODEX_POSE_IDLE;
    pet->root = lv_obj_create(parent);
    lv_obj_set_size(pet->root, 140, 112);
    lv_obj_set_style_bg_opa(pet->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pet->root, 0, 0);
    lv_obj_set_style_pad_all(pet->root, 0, 0);
    lv_obj_remove_flag(pet->root, LV_OBJ_FLAG_SCROLLABLE);

    pet->shadow = lv_obj_create(pet->root);
    style_round(pet->shadow, 72, 10, 0x0C1A2A);
    lv_obj_set_style_bg_opa(pet->shadow, LV_OPA_40, 0);

    pet->ear_l = lv_obj_create(pet->root);
    pet->ear_r = lv_obj_create(pet->root);
    style_round(pet->ear_l, 22, 22, PET_CHEEK);
    style_round(pet->ear_r, 22, 22, PET_CHEEK);

    pet->body = lv_obj_create(pet->root);
    style_round(pet->body, 86, 78, PET_BG);

    pet->eye_l = lv_obj_create(pet->root);
    pet->eye_r = lv_obj_create(pet->root);
    style_round(pet->eye_l, 16, 16, PET_EYE);
    style_round(pet->eye_r, 16, 16, PET_EYE);
    pet->pupil_l = lv_obj_create(pet->root);
    pet->pupil_r = lv_obj_create(pet->root);
    style_round(pet->pupil_l, 7, 7, PET_INK);
    style_round(pet->pupil_r, 7, 7, PET_INK);

    pet->mouth = lv_obj_create(pet->root);
    lv_obj_set_size(pet->mouth, 18, 6);
    lv_obj_set_style_radius(pet->mouth, 3, 0);
    lv_obj_set_style_bg_color(pet->mouth, lv_color_hex(PET_INK), 0);
    lv_obj_set_style_border_width(pet->mouth, 0, 0);

    pet->badge = lv_label_create(pet->root);
    lv_obj_set_style_text_font(pet->badge, &codex_font_16, LV_PART_MAIN);
    lv_obj_set_style_text_color(pet->badge, lv_color_hex(0xF4F0E6), LV_PART_MAIN);
    lv_label_set_text(pet->badge, "");

    apply_pose(pet);
    pet->timer = lv_timer_create(pet_timer, 280, pet);
}

void codex_pet_set_pose(codex_pet_t *pet, codex_pose_t pose)
{
    if (pet == NULL || pet->root == NULL) return;
    pet->pose = pose;
    apply_pose(pet);
}

void codex_pet_destroy(codex_pet_t *pet)
{
    if (pet == NULL) return;
    if (pet->timer != NULL) {
        lv_timer_delete(pet->timer);
        pet->timer = NULL;
    }
    if (pet->root != NULL) {
        lv_obj_delete(pet->root);
        pet->root = NULL;
    }
}
