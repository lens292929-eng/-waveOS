#ifndef EDIT_H
#define EDIT_H

#include "types.h"

#define EDIT_KEY_BACKSPACE  8
#define EDIT_KEY_ENTER      13

#define EDIT_KEY_LEFT       256
#define EDIT_KEY_RIGHT      257
#define EDIT_KEY_UP         258
#define EDIT_KEY_DOWN       259
#define EDIT_KEY_HOME       260
#define EDIT_KEY_END        261
#define EDIT_KEY_DELETE     262

void launch_edit(void);
void edit_handle_key(int key);
void edit_open(const char *name);
int editor_is_active(void);

#endif