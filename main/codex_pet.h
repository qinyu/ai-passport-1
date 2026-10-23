#pragma once

#include "codex_model.h"

#include "lvgl.h"

/* Pi sprite frames taken from the Stick character pack and recolored only by
 * transparency. The same silhouette is the Passport buddy for local Pi and
 * for Pi reached through Herdr. */

typedef struct {
    lv_obj_t *root;
    lv_obj_t *image;
    lv_timer_t *timer;
    codex_pose_t pose;
    uint32_t phase;
} codex_pet_t;

void codex_pet_create(codex_pet_t *pet, lv_obj_t *parent);
void codex_pet_set_pose(codex_pet_t *pet, codex_pose_t pose);
void codex_pet_destroy(codex_pet_t *pet);
