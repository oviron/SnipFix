#include "logic.h"

unsigned sf_on_event(SfState *s, SfEvent e)
{
    switch (e) {
    case SF_EV_OVERLAY_CREATED:
    case SF_EV_DIRECT_CAPTURE:
        if (s->active) return SF_ACT_NONE;  /* one capture at a time */
        s->active = 1;
        return SF_ACT_BEGIN;
    case SF_EV_OVERLAY_SHOWN:
        return s->active ? SF_ACT_END_SOON : SF_ACT_NONE;
    case SF_EV_OVERLAY_GONE:
    case SF_EV_TIMER:
        if (!s->active) return SF_ACT_NONE;
        s->active = 0;
        return SF_ACT_END;
    }
    return SF_ACT_NONE;
}

void sf_cancel(SfState *s)
{
    s->active = 0;
}

int sf_direct_capture(unsigned vk, int down, int win, int alt, int prtscn_opens_snipping)
{
    return down && vk == SF_VK_SNAPSHOT && (win || alt || !prtscn_opens_snipping);
}

int sf_needs_adjust(unsigned level)
{
    return level > SF_LEVEL_80_NITS;
}
