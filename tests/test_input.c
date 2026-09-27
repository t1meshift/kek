/* Keyboard state: kek_key_held/pressed/released, and the frame boundary that
   decides which scene update sees an edge.

   Driven the way the platform layer drives it: events through kek_key_down()
   and kek_key_up(), then kek_update(). What matters is what the scene's
   update callback sees, so a probe scene records the three queries for one
   key every time a callback of its runs; the tests assert on those records
   rather than on the engine's bits. */

#include <string.h>
#include "unity.h"
#include "kek.h"
#include "kek_keyboard.h"
#include "test_support.h"

#define KEY KEK_SCANCODE_SPACE
#define OTHER KEK_SCANCODE_W
#define DT 33.f
/* Typed, so a uint16_t loop over every scancode compares like with like. */
#define SCANCODES ((uint16_t)KEK_SCANCODE_SIZE)

typedef struct Seen {
    int held;
    int pressed;
    int released;
} Seen;

typedef struct Probe {
    KEK_scene base;
    int updates;
    int enters;
    int key_downs;
    int key_ups;
    Seen at_update;
    Seen at_enter;
    Seen at_render;
    Seen at_key_down;
    Seen at_key_up;
    /* When KEY is pressed, switch: from update through kek_request_scene(),
       or from the key_down callback through kek_set_scene(). */
    KEK_scene* request_on_press;
    KEK_scene* set_on_key_down;
} Probe;

static KEK_engine e;
static Probe A;
static Probe B;

static Seen see(const KEK_engine* engine) {
    Seen s;
    s.held = kek_key_held(engine, KEY);
    s.pressed = kek_key_pressed(engine, KEY);
    s.released = kek_key_released(engine, KEY);
    return s;
}

static void probe_enter(KEK_scene* scene, KEK_engine* engine) {
    Probe* p = (Probe*)scene;
    p->enters++;
    p->at_enter = see(engine);
}

static void probe_update(KEK_scene* scene, KEK_engine* engine, float dt) {
    Probe* p = (Probe*)scene;
    (void)dt;
    p->updates++;
    p->at_update = see(engine);
    if (p->request_on_press && kek_key_pressed(engine, KEY)) {
        kek_request_scene(engine, p->request_on_press);
    }
}

static void probe_render(KEK_scene* scene, KEK_engine* engine) {
    ((Probe*)scene)->at_render = see(engine);
}

static void probe_key_down(KEK_scene* scene, KEK_engine* engine, KEK_scancode key) {
    Probe* p = (Probe*)scene;
    p->key_downs++;
    p->at_key_down = see(engine);
    if (p->set_on_key_down && key == KEY) {
        kek_set_scene(engine, p->set_on_key_down);
    }
}

static void probe_key_up(KEK_scene* scene, KEK_engine* engine, KEK_scancode key) {
    Probe* p = (Probe*)scene;
    (void)key;
    p->key_ups++;
    p->at_key_up = see(engine);
}

static void probe_init(Probe* p) {
    memset(p, 0, sizeof(*p));
    p->base.enter = probe_enter;
    p->base.update = probe_update;
    p->base.render = probe_render;
    p->base.key_down = probe_key_down;
    p->base.key_up = probe_key_up;
}

void setUp(void) {
    kek_test_init(&e);
    probe_init(&A);
    probe_init(&B);
    kek_set_scene(&e, &A.base);
}

void tearDown(void) {
}

/* A macro so that a failure reports the line of the call, not of the helper:
   most tests assert on several updates in a row. */
#define assert_seen(held, pressed, released, s) \
    assert_seen_at(__LINE__, (held), (pressed), (released), (s))

static void assert_seen_at(int line, int held, int pressed, int released, Seen s) {
    UNITY_TEST_ASSERT_EQUAL_INT(held, s.held, line, "held");
    UNITY_TEST_ASSERT_EQUAL_INT(pressed, s.pressed, line, "pressed");
    UNITY_TEST_ASSERT_EQUAL_INT(released, s.released, line, "released");
}

/* One kek_update(), which must reach p's update exactly once; returns what
   that update saw. */
static Seen update(Probe* p) {
    int before = p->updates;
    kek_update(&e, DT);
    TEST_ASSERT_EQUAL_INT(before + 1, p->updates);
    return p->at_update;
}

/* ---- Edges across frames ---- */

void test_nothing_is_held_after_init(void) {
    for (uint16_t k = 0; k < SCANCODES; ++k) {
        TEST_ASSERT_FALSE(kek_key_held(&e, k));
        TEST_ASSERT_FALSE(kek_key_pressed(&e, k));
        TEST_ASSERT_FALSE(kek_key_released(&e, k));
    }
}

void test_a_press_is_seen_by_one_update_and_the_hold_by_every_update(void) {
    kek_key_down(&e, KEY);
    assert_seen(1, 1, 0, update(&A));
    assert_seen(1, 0, 0, update(&A));
    assert_seen(1, 0, 0, update(&A));

    kek_key_up(&e, KEY);
    assert_seen(0, 0, 1, update(&A));
    assert_seen(0, 0, 0, update(&A));
}

/* The pacing gate in SDL_AppIterate can let any number of events through
   between two kek_update() calls; none of them may be lost. */
void test_a_tap_within_one_frame_is_pressed_and_released_in_the_same_update(void) {
    kek_key_down(&e, KEY);
    kek_key_up(&e, KEY);
    assert_seen(0, 1, 1, update(&A));
    assert_seen(0, 0, 0, update(&A));
}

void test_a_release_and_press_within_one_frame_reports_both_edges(void) {
    kek_key_down(&e, KEY);
    update(&A);

    kek_key_up(&e, KEY);
    kek_key_down(&e, KEY);
    assert_seen(1, 1, 1, update(&A));
    assert_seen(1, 0, 0, update(&A));
}

void test_os_key_repeat_is_not_a_second_press(void) {
    kek_key_down(&e, KEY);
    kek_key_down(&e, KEY);
    assert_seen(1, 1, 0, update(&A));

    kek_key_down(&e, KEY);
    kek_key_down(&e, KEY);
    assert_seen(1, 0, 0, update(&A));

    /* The event path is untouched: every repeat still reaches the scene. */
    TEST_ASSERT_EQUAL_INT(4, A.key_downs);

    kek_key_up(&e, KEY);
    kek_key_up(&e, KEY);
    assert_seen(0, 0, 1, update(&A));
    assert_seen(0, 0, 0, update(&A));
    TEST_ASSERT_EQUAL_INT(2, A.key_ups);
}

/* Held since before the window had focus, for instance. */
void test_a_key_up_without_a_key_down_is_not_a_release(void) {
    kek_key_up(&e, KEY);
    assert_seen(0, 0, 0, update(&A));
    TEST_ASSERT_EQUAL_INT(1, A.key_ups);
}

void test_keys_are_independent(void) {
    kek_key_down(&e, KEY);
    update(&A);
    kek_key_down(&e, OTHER);

    assert_seen(1, 0, 0, update(&A));
    TEST_ASSERT_TRUE(kek_key_held(&e, OTHER));

    kek_key_up(&e, OTHER);
    assert_seen(1, 0, 0, update(&A));
}

/* Every scancode maps to its own bit: pressing one leaves the other 511
   alone, including its neighbours across a byte boundary. */
void test_every_scancode_is_its_own_key(void) {
    for (uint16_t k = 0; k < SCANCODES; ++k) {
        kek_key_down(&e, k);
        for (uint16_t j = 0; j < SCANCODES; ++j) {
            TEST_ASSERT_EQUAL_INT(j == k, kek_key_held(&e, j));
            TEST_ASSERT_EQUAL_INT(j == k, kek_key_pressed(&e, j));
        }
        kek_key_up(&e, k);
        TEST_ASSERT_FALSE(kek_key_held(&e, k));
        TEST_ASSERT_TRUE(kek_key_released(&e, k));
        kek_update(&e, DT);
    }
}

/* ---- Where the edges are visible ---- */

void test_the_event_callbacks_see_the_state_their_event_made(void) {
    kek_key_down(&e, KEY);
    assert_seen(1, 1, 0, A.at_key_down);

    update(&A);
    kek_key_up(&e, KEY);
    assert_seen(0, 0, 1, A.at_key_up);
}

/* Render follows update in the platform loop, and update has consumed the
   edges by then. */
void test_render_after_update_sees_the_hold_but_not_the_edge(void) {
    kek_key_down(&e, KEY);
    update(&A);
    kek_render(&e);
    assert_seen(1, 0, 0, A.at_render);
}

void test_the_state_is_kept_without_a_scene(void) {
    kek_test_init(&e);

    kek_key_down(&e, KEY);
    TEST_ASSERT_TRUE(kek_key_held(&e, KEY));
    kek_update(&e, DT);

    kek_set_scene(&e, &A.base);
    assert_seen(1, 0, 0, update(&A));
}

/* ---- Out of range ---- */

void test_out_of_range_scancodes_are_never_tracked(void) {
    const uint16_t keys[] = { KEK_SCANCODE_SIZE, KEK_SCANCODE_SIZE + 1, 0x7FFF, 0xFFFF };

    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        kek_key_down(&e, keys[i]);
        TEST_ASSERT_FALSE(kek_key_held(&e, keys[i]));
        TEST_ASSERT_FALSE(kek_key_pressed(&e, keys[i]));
        kek_key_up(&e, keys[i]);
        TEST_ASSERT_FALSE(kek_key_released(&e, keys[i]));
    }

    /* Nothing in range was touched either... */
    for (uint16_t k = 0; k < SCANCODES; ++k) {
        TEST_ASSERT_FALSE(kek_key_pressed(&e, k));
        TEST_ASSERT_FALSE(kek_key_released(&e, k));
    }

    /* ...but the scene still got the events, as it did before. */
    TEST_ASSERT_EQUAL_INT(4, A.key_downs);
    TEST_ASSERT_EQUAL_INT(4, A.key_ups);
}

void test_the_last_scancode_in_range_is_tracked(void) {
    const uint16_t last = KEK_SCANCODE_SIZE - 1;

    kek_key_down(&e, last);
    TEST_ASSERT_TRUE(kek_key_held(&e, last));
    TEST_ASSERT_TRUE(kek_key_pressed(&e, last));
    TEST_ASSERT_FALSE(kek_key_held(&e, KEK_SCANCODE_SIZE));
}

/* ---- Scene switches ---- */

void test_a_requested_switch_does_not_hand_the_press_to_the_new_scene(void) {
    A.request_on_press = &B.base;

    kek_key_down(&e, KEY);
    assert_seen(1, 1, 0, update(&A));

    TEST_ASSERT_EQUAL_PTR(&B.base, e.scene);
    TEST_ASSERT_EQUAL_INT(1, B.enters);
    assert_seen(1, 0, 0, B.at_enter);

    assert_seen(1, 0, 0, update(&B));
    TEST_ASSERT_EQUAL_INT(1, A.updates);

    /* Still held across the switch, so its release is the new scene's. */
    kek_key_up(&e, KEY);
    assert_seen(0, 0, 1, update(&B));
}

/* The same guarantee when the switch is immediate, from inside the event that
   caused it — before any update has run. */
void test_a_switch_from_a_key_down_callback_does_not_hand_the_press_over(void) {
    A.set_on_key_down = &B.base;

    kek_key_down(&e, KEY);
    TEST_ASSERT_EQUAL_PTR(&B.base, e.scene);
    assert_seen(1, 0, 0, B.at_enter);

    assert_seen(1, 0, 0, update(&B));
    TEST_ASSERT_EQUAL_INT(0, A.updates);
}

/* Events that arrive after the switch belong to the new scene. */
void test_a_press_after_a_switch_reaches_the_new_scene(void) {
    kek_set_scene(&e, &B.base);
    kek_key_down(&e, KEY);
    assert_seen(1, 1, 0, update(&B));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nothing_is_held_after_init);
    RUN_TEST(test_a_press_is_seen_by_one_update_and_the_hold_by_every_update);
    RUN_TEST(test_a_tap_within_one_frame_is_pressed_and_released_in_the_same_update);
    RUN_TEST(test_a_release_and_press_within_one_frame_reports_both_edges);
    RUN_TEST(test_os_key_repeat_is_not_a_second_press);
    RUN_TEST(test_a_key_up_without_a_key_down_is_not_a_release);
    RUN_TEST(test_keys_are_independent);
    RUN_TEST(test_every_scancode_is_its_own_key);
    RUN_TEST(test_the_event_callbacks_see_the_state_their_event_made);
    RUN_TEST(test_render_after_update_sees_the_hold_but_not_the_edge);
    RUN_TEST(test_the_state_is_kept_without_a_scene);
    RUN_TEST(test_out_of_range_scancodes_are_never_tracked);
    RUN_TEST(test_the_last_scancode_in_range_is_tracked);
    RUN_TEST(test_a_requested_switch_does_not_hand_the_press_to_the_new_scene);
    RUN_TEST(test_a_switch_from_a_key_down_callback_does_not_hand_the_press_over);
    RUN_TEST(test_a_press_after_a_switch_reaches_the_new_scene);
    return UNITY_END();
}
