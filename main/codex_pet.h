#pragma once

#include "codex_model.h"

#include "lvgl.h"

/* Procedural companion. GIF pets from the Stick build are not used: this board
 * has no PSRAM and the Passport UI is drawn with LVGL shapes. */

typedef struct {
    lv_obj_t *root;
    lv_obj_t *shadow;
    lv_obj_t *body;
    lv_obj_t *ear_l;
    lv_obj_t *ear_r;
    lv_obj_t *eye_l;
    lv_obj_t *eye_r;
    lv_obj_t *pupil_l;
    lv_obj_t *pupil_r;
    lv_obj_t *mouth;
    lv_obj_t *badge;
    lv_timer_t *timer;
    codex_pose_t pose;
    uint32_t phase;
} codex_pet_t;

void codex_pet_create(codex_pet_t *pet, lv_obj_t *parent);
void codex_pet_set_pose(codex_pet_t *pet, codex_pose_t pose);
void codex_pet_destroy(codex_pet_t *pet);
