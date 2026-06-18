#ifndef UI_FIELD_DRAW_H
#define UI_FIELD_DRAW_H

#include "../stack.h"
#include "render.h"

/* Render all field text for one card onto the SDL renderer.
 * Background-layer fields are drawn first; card-layer fields on top.
 * Must be called after the bitmap texture has been blitted. */
void field_draw_text(Renderer *r, const Card *card, const Background *bg);

#endif /* UI_FIELD_DRAW_H */
