#pragma once

#include "codex_model.h"

/* Redesigned Passport screen. Call only from the LVGL task or while holding
 * bsp_lvgl_lock(). The baseline test menu is not used. */

void codex_ui_create(void);
void codex_ui_apply(const codex_model_t *model, uint32_t now_ms);
void codex_ui_destroy(void);
