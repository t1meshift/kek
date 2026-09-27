#ifndef KEK_LIGHT_H
#define KEK_LIGHT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "kek_math.h"

/* The light kek_3d_draw_model shades with: one directional light plus
   ambient, and darkening with distance. It is fixed in the world — the draw
   brings it into view space alongside the faces, so turning or moving the
   camera does not change which side of a model is lit.

   Its own header because KEK_engine holds it and kek.h cannot include
   kek_3d.h, which includes kek.h. Set it through kek_3d_set_light and
   kek_3d_set_fog rather than by hand: they keep the invariants below. */
typedef struct KEK_light {
    /* Where the light travels, in world space. Unit length, or zero for no
       direct light at all. */
    KEK_FVec3 direction;
    /* 0..1, the brightness of a face turned away from the light. 1 lights
       everything fully, which is how lighting is switched off. */
    float ambient;
    /* View-space depths over which colours fall from full brightness to the
       darkest shading level. Off unless fog_end > fog_start. */
    float fog_start;
    float fog_end;
} KEK_light;

#ifdef __cplusplus
}
#endif

#endif // KEK_LIGHT_H
