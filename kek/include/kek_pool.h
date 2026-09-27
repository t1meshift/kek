#ifndef KEK_POOL_H
#define KEK_POOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include "kek_model.h"
#include "kek_texture.h"

/* The handle types and their invalid values live next to what they refer to,
   in kek_model.h and kek_texture.h — both are included above. */

/* A handle names a slot; the slot names the model's storage in the arena,
   sized for that model when it was created. The tables of slots are in the
   arena too, sized by KEK_desc. */
typedef struct KEK_ModelPoolSlot {
    KEK_model model;
    size_t block; /* the arena block holding the model's arrays */
    uint16_t generation;
    uint8_t used;
} KEK_ModelPoolSlot;

typedef struct KEK_TexturePoolSlot {
    KEK_texture texture;
    size_t block; /* the arena block holding the pixels */
    uint16_t generation;
    uint8_t used;
} KEK_TexturePoolSlot;

typedef struct KEK_ModelPool {
    KEK_ModelPoolSlot* slots;
    uint16_t capacity;
} KEK_ModelPool;

typedef struct KEK_TexturePool {
    KEK_TexturePoolSlot* slots;
    uint16_t capacity;
} KEK_TexturePool;

#ifdef __cplusplus
}
#endif

#endif // KEK_POOL_H
