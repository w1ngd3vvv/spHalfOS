#ifndef SP_MOUSE_H
#define SP_MOUSE_H

#include "types.h"

typedef struct {
    int x, y;
    int dx, dy;
    int buttons;          /* bit0 left, bit1 right, bit2 middle */
} mouse_state_t;

void mouse_init(void);
void mouse_state(mouse_state_t *out);
void mouse_irq(void);
void mouse_set_pos(int x, int y);

#endif
