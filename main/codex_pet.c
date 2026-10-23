#include "codex_pet.h"

extern const lv_image_dsc_t pi_pet_idle;
extern const lv_image_dsc_t pi_pet_busy;
extern const lv_image_dsc_t pi_pet_attention;
extern const lv_image_dsc_t pi_pet_dizzy;

static const lv_image_dsc_t *frame_for(codex_pose_t pose)
{
    switch (pose) {
    case CODEX_POSE_BUSY:
    case CODEX_POSE_COMPLETED:
        return &pi_pet_busy;
    case CODEX_POSE_ATTENTION:
        return &pi_pet_attention;
    case CODEX_POSE_DIZZY:
        return &pi_pet_dizzy;
    case CODEX_POSE_SLEEP:
    case CODEX_POSE_IDLE:
    case CODEX_POSE_HEART:
    case CODEX_POSE_UNKNOWN:
    default:
        return &pi_pet_idle;
    }
}

static void apply_pose(codex_pet_t *pet)
{
    int bob = 0;
    if (pet->pose == CODEX_POSE_BUSY || pet->pose == CODEX_POSE_COMPLETED) {
        bob = (pet->phase % 2) == 0 ? -3 : 2;
    } else if (pet->pose == CODEX_POSE_DIZZY) {
        bob = (pet->phase % 2) == 0 ? -2 : 3;
    } else if (pet->pose != CODEX_POSE_SLEEP) {
        bob = (pet->phase % 4) == 0 ? -2 : 0;
    }
    lv_image_set_src(pet->image, frame_for(pet->pose));
    lv_obj_align(pet->image, LV_ALIGN_CENTER, 0, bob);
}

static void pet_timer(lv_timer_t *timer)
{
    codex_pet_t *pet = lv_timer_get_user_data(timer);
    pet->phase++;
    apply_pose(pet);
}

void codex_pet_create(codex_pet_t *pet, lv_obj_t *parent)
{
    pet->pose = CODEX_POSE_IDLE;
    pet->phase = 0;
    pet->root = lv_obj_create(parent);
    lv_obj_set_size(pet->root, 216, 150);
    lv_obj_set_style_bg_opa(pet->root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pet->root, 0, 0);
    lv_obj_set_style_pad_all(pet->root, 0, 0);
    lv_obj_remove_flag(pet->root, LV_OBJ_FLAG_SCROLLABLE);
    pet->image = lv_image_create(pet->root);
    lv_image_set_src(pet->image, &pi_pet_idle);
    lv_obj_align(pet->image, LV_ALIGN_CENTER, 0, 0);
    pet->timer = lv_timer_create(pet_timer, 280, pet);
}

void codex_pet_set_pose(codex_pet_t *pet, codex_pose_t pose)
{
    if (pet == NULL || pet->image == NULL) return;
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
