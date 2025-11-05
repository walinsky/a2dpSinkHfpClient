/*
 * ringtone.h - Simple ringtone beep for incoming calls
 */

#ifndef RINGTONE_H
#define RINGTONE_H

#ifdef __cplusplus
extern "C" {
#endif

// Play a 2-second ringtone beep (non-blocking)
void ringtone_play_beep(void);

// Stop any playing ringtone immediately
void ringtone_stop(void);

#ifdef __cplusplus
}
#endif

#endif // RINGTONE_H
