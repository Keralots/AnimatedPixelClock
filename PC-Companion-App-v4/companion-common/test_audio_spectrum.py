"""Auto-start decisions: arming delay, short-sound rejection, quiet release."""
import unittest

import audio_spectrum as audio


def trigger(threshold=-45.0, start=3.0, stop=20.0):
    t = audio.VizAutoTrigger()
    t.configure(True, threshold, start, stop)
    return t


def feed_span(t, level_db, t0, seconds, step=0.04):
    """Play one level for a span; returns the actions in order."""
    actions, now = [], t0
    end = t0 + seconds
    while now < end:
        a = t.feed(level_db, now)
        if a:
            actions.append((a, now))
        now += step
    return actions, now


class AutoTriggerTests(unittest.TestCase):
    def test_disabled_never_acts(self):
        t = audio.VizAutoTrigger()
        actions, _ = feed_span(t, -10.0, 0.0, 30.0)
        self.assertEqual(actions, [])

    def test_music_starts_after_delay(self):
        t = trigger(start=3.0)
        actions, _ = feed_span(t, -20.0, 0.0, 5.0)
        self.assertEqual([a for a, _ in actions], ["viz"])
        self.assertAlmostEqual(actions[0][1], 3.0, delta=0.05)
        self.assertTrue(t.forced)

    def test_short_notification_is_ignored(self):
        t = trigger(start=3.0)
        now = 0.0
        for _ in range(4):  # four 1s pops, 2s apart
            _, now = feed_span(t, -15.0, now, 1.0)
            actions, now = feed_span(t, -90.0, now, 2.0)
            self.assertEqual(actions, [])
        self.assertFalse(t.forced)

    def test_short_gap_does_not_rearm(self):
        t = trigger(start=3.0)
        actions, now = feed_span(t, -20.0, 0.0, 2.0)
        self.assertEqual(actions, [])
        _, now = feed_span(t, -90.0, now, 0.5)      # gap below AUTO_GAP_S
        actions, _ = feed_span(t, -20.0, now, 2.0)
        self.assertEqual([a for a, _ in actions], ["viz"])

    def test_release_after_stop_delay_only(self):
        t = trigger(start=1.0, stop=10.0)
        _, now = feed_span(t, -20.0, 0.0, 2.0)
        self.assertTrue(t.forced)
        actions, now = feed_span(t, -90.0, now, 8.0)
        self.assertEqual(actions, [])               # still holding the display
        actions, _ = feed_span(t, -90.0, now, 4.0)
        self.assertEqual([a for a, _ in actions], ["auto"])
        self.assertFalse(t.forced)

    def test_threshold_rejects_quiet_sound(self):
        t = trigger(threshold=-30.0, start=1.0)
        actions, _ = feed_span(t, -40.0, 0.0, 10.0)
        self.assertEqual(actions, [])

    def test_disabling_hands_the_display_back(self):
        t = trigger(start=1.0)
        feed_span(t, -20.0, 0.0, 2.0)
        self.assertTrue(t.configure(False, -45.0, 3.0, 20.0))
        self.assertFalse(t.forced)
        self.assertFalse(t.configure(False, -45.0, 3.0, 20.0))

    def test_settings_are_clamped(self):
        t = trigger()
        t.configure(True, -200.0, -5.0, 0.0)
        self.assertEqual((t.threshold_db, t.start_delay, t.stop_delay),
                         (-80.0, 0.0, 1.0))
        t.configure(True, 50.0, 900.0, 99999.0)
        self.assertEqual((t.threshold_db, t.start_delay, t.stop_delay),
                         (-10.0, 60.0, 3600.0))

    def test_auto_settings_from_config(self):
        self.assertEqual(audio.auto_settings({}),
                         (False, audio.AUTO_THRESHOLD_DB,
                          audio.AUTO_START_DELAY, audio.AUTO_STOP_DELAY))
        self.assertEqual(audio.auto_settings({
            "audio_viz_auto": True, "audio_viz_threshold": -55,
            "audio_viz_start_delay": 5, "audio_viz_stop_delay": 90}),
            (True, -55.0, 5.0, 90.0))


class PacerTests(unittest.TestCase):
    def test_short_gaps_hold_the_last_frame(self):
        self.assertEqual(audio.frame_action(0.0), "hold")
        self.assertEqual(audio.frame_action(audio.HOLD_S), "hold")

    def test_longer_gaps_fade_the_last_frame(self):
        self.assertEqual(audio.frame_action(audio.HOLD_S + 0.01), "decay")
        self.assertEqual(audio.frame_action(audio.GIVE_UP_S - 0.01), "decay")

    def test_dead_capture_stops_sending(self):
        self.assertEqual(audio.frame_action(audio.GIVE_UP_S), "stop")
        self.assertEqual(audio.frame_action(60.0), "stop")

    def test_decay_reaches_silence_before_giving_up(self):
        level = 255.0
        ticks = int((audio.GIVE_UP_S - audio.HOLD_S) / audio.GAP_S)
        for _ in range(ticks):
            level *= audio.STALL_DECAY
        self.assertLess(level, 1.0)


if __name__ == "__main__":
    unittest.main()
