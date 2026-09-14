#ifndef KEK_TEXTURE_H
#define KEK_TEXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct KEK_engine KEK_engine;
#ifndef KEK_TEXTURE_HANDLE_DEFINED
#define KEK_TEXTURE_HANDLE_DEFINED
typedef uint32_t KEK_TextureHandle;
#endif

typedef enum KEK_TextureWarpMode {
    KEK_TEXTURE_WARP_CLAMP = 0,
    KEK_TEXTURE_WARP_REPEAT = 1
} KEK_TextureWarpMode;

typedef struct KEK_texture {
    uint8_t* data;
    uint16_t width;
    uint16_t height;
} KEK_texture;

uint8_t kek_texture_sample(const KEK_engine* e, const KEK_texture* texture, float u, float v);
void kek_texture_set_warp_mode(KEK_engine* e, KEK_TextureWarpMode mode);
KEK_TextureWarpMode kek_texture_get_warp_mode(const KEK_engine* e);
KEK_TextureHandle kek_texture_create(KEK_engine* e);
KEK_TextureHandle kek_texture_clone(KEK_engine* e, const KEK_texture* source);
KEK_texture* kek_texture_get(KEK_engine* e, KEK_TextureHandle handle);
void kek_texture_destroy(KEK_engine* e, KEK_TextureHandle handle);
KEK_TextureHandle kek_default_texture_handle(KEK_engine* e);

extern KEK_texture KEK_DEFAULT_TEXTURE;

#ifdef __cplusplus
}
#endif

#endif // KEK_TEXTURE_H
