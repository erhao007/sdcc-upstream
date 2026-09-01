/* Project-owned clean-room API for a multi-translation-unit migration test. */

#ifndef CLEANROOM_APP_H
#define CLEANROOM_APP_H

void cleanroom_reset(void);
unsigned int cleanroom_accumulate(const unsigned char *input,
                                  unsigned char count) __reentrant;
unsigned char cleanroom_snapshot(unsigned char index);

#endif
