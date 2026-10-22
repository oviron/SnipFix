/* Unit tests for the SnipFix state machine. Plain C, runs on any OS:
 *   cc -std=c99 -Wall -Wextra -Isrc tests/test_logic.c src/logic.c -o test && ./test */
#include <stdio.h>
#include "logic.h"

static int failures, checks;

#define CHECK(cond) do { checks++; if (!(cond)) { failures++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static void test_overlay_cycle(void)
{
    SfState s = { 0 };
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_CREATED) == SF_ACT_BEGIN && s.active);
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_SHOWN) == SF_ACT_END_SOON && s.active);
    CHECK(sf_on_event(&s, SF_EV_TIMER) == SF_ACT_END && !s.active);
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_GONE) == SF_ACT_NONE);   /* the user finishes the snip later */
}

static void test_cancelled_before_shown(void)
{
    SfState s = { 0 };
    sf_on_event(&s, SF_EV_OVERLAY_CREATED);
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_GONE) == SF_ACT_END && !s.active);
    CHECK(sf_on_event(&s, SF_EV_TIMER) == SF_ACT_NONE);         /* the safety timer finds nothing to do */
}

static void test_overlay_never_shows(void)
{
    SfState s = { 0 };
    sf_on_event(&s, SF_EV_OVERLAY_CREATED);
    CHECK(sf_on_event(&s, SF_EV_TIMER) == SF_ACT_END && !s.active); /* safety timer restores */
}

static void test_one_capture_at_a_time(void)
{
    SfState s = { 0 };
    CHECK(sf_on_event(&s, SF_EV_DIRECT_CAPTURE) == SF_ACT_BEGIN);
    CHECK(sf_on_event(&s, SF_EV_DIRECT_CAPTURE) == SF_ACT_NONE);   /* key auto-repeat */
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_CREATED) == SF_ACT_NONE);  /* already lowered */
    CHECK(sf_on_event(&s, SF_EV_TIMER) == SF_ACT_END);
    CHECK(sf_on_event(&s, SF_EV_DIRECT_CAPTURE) == SF_ACT_BEGIN);   /* the next one works again */
}

static void test_shown_without_begin_is_ignored(void)
{
    SfState s = { 0 };
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_SHOWN) == SF_ACT_NONE);
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_GONE) == SF_ACT_NONE);
    CHECK(sf_on_event(&s, SF_EV_TIMER) == SF_ACT_NONE && !s.active);
}

static void test_cancel(void)
{
    SfState s = { 0 };
    sf_on_event(&s, SF_EV_OVERLAY_CREATED);
    sf_cancel(&s);                                    /* no HDR display needed lowering */
    CHECK(!s.active);
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_SHOWN) == SF_ACT_NONE);
    CHECK(sf_on_event(&s, SF_EV_OVERLAY_CREATED) == SF_ACT_BEGIN);
}

static void test_direct_capture_keys(void)
{
    CHECK(sf_direct_capture(SF_VK_SNAPSHOT, 1, 1, 0, 1));    /* Win+PrtScn */
    CHECK(sf_direct_capture(SF_VK_SNAPSHOT, 1, 0, 1, 1));    /* Alt+PrtScn */
    CHECK(!sf_direct_capture(SF_VK_SNAPSHOT, 1, 0, 0, 1));   /* PrtScn opens snipping: the overlay is watched */
    CHECK(sf_direct_capture(SF_VK_SNAPSHOT, 1, 0, 0, 0));    /* PrtScn copies the screen itself */
    CHECK(!sf_direct_capture(SF_VK_SNAPSHOT, 0, 1, 0, 1));   /* key up */
    CHECK(!sf_direct_capture(0x53, 1, 1, 0, 1));             /* Win+S */
}

static void test_needs_adjust(void)
{
    CHECK(!sf_needs_adjust(SF_LEVEL_80_NITS));      /* already 80 nits: nothing to do */
    CHECK(!sf_needs_adjust(0));
    CHECK(sf_needs_adjust(3000));                   /* 240 nits */
    CHECK(sf_needs_adjust(SF_LEVEL_80_NITS + 1));
}

int main(void)
{
    test_overlay_cycle();
    test_cancelled_before_shown();
    test_overlay_never_shows();
    test_one_capture_at_a_time();
    test_shown_without_begin_is_ignored();
    test_cancel();
    test_direct_capture_keys();
    test_needs_adjust();
    printf("%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
