#ifndef SHELL_H
#define SHELL_H

#include "keyboard.h"

void shell_prompt(void);
void shell_submit(char *line);
void shell_cancel_funny(void);
void shell_set_desktop_mode(int on);
int  shell_in_desktop_mode(void);
void shell_init(void);
void shell_shutdown(void);
void shell_cursor_blink(void);
void shell_keyboard_handler(const keyboard_event_t *event);

#endif