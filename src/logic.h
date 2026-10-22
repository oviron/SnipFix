/*
 * SnipFix decision logic: a pure state machine, no Windows headers.
 *
 * With HDR on, Chromium/Electron windows that show a non-sRGB image switch to an
 * FP16 scRGB backbuffer and multiply their SDR colours by (SDR white / 80 nits);
 * screen capture clips that at 1.0, so screenshots of them come out overexposed.
 * For the moment a screenshot is taken SnipFix sets SDR white to 80 nits (the
 * multiplier becomes 1), then puts the user's level back.
 */
#ifndef SNIPFIX_LOGIC_H
#define SNIPFIX_LOGIC_H

#define SF_VK_SNAPSHOT 0x2C

/* Windows reports the SDR white level as a multiple of 80 nits, times 1000. */
#define SF_LEVEL_80_NITS 1000u

typedef enum {
    SF_EV_OVERLAY_CREATED,  /* Snipping Tool's overlay window was created: it grabs the screen next */
    SF_EV_OVERLAY_SHOWN,    /* ...became visible: the frame is frozen */
    SF_EV_OVERLAY_GONE,     /* ...was destroyed (cancelled, or done) */
    SF_EV_DIRECT_CAPTURE,   /* a key that captures without the overlay (Win+PrtScn, Alt+PrtScn) */
    SF_EV_TIMER             /* the restore or safety timer fired */
} SfEvent;

enum {
    SF_ACT_NONE = 0,
    SF_ACT_BEGIN,           /* set SDR white to 80 nits now; arm the safety timer */
    SF_ACT_END,             /* restore the user's SDR white now */
    SF_ACT_END_SOON         /* restore after a short delay */
};

typedef struct {
    int active;             /* SDR white is currently lowered by us */
} SfState;

unsigned sf_on_event(SfState *s, SfEvent e);
/* BEGIN turned out to have nothing to do (no HDR display above 80 nits). */
void sf_cancel(SfState *s);

/* Does this key press make Windows capture the screen without the snipping
 * overlay? Win+PrtScn saves a file, Alt+PrtScn copies the active window, and plain
 * PrtScn copies the screen when it is not set to open screen snipping. */
int sf_direct_capture(unsigned vk, int down, int win, int alt, int prtscn_opens_snipping);

/* Only displays whose SDR white is above 80 nits need lowering. */
int sf_needs_adjust(unsigned level);

#endif
