"""Connection-page plumbing: stats gate, auto-start fields, validation."""
import unittest
from unittest.mock import patch

import server
from app_state import AppState


class FakeCore:
    def __init__(self):
        self.saved = None

    def save_config(self, config):
        self.saved = config


def form(**kw):
    base = {"esp32_ip": "192.168.0.153", "udp_port": "4210", "update_interval": "3"}
    base.update(kw)
    return {k: [str(v)] for k, v in base.items()}


class ConnectionTests(unittest.TestCase):
    def apply(self, **kw):
        core, state = FakeCore(), AppState({"metrics": []})
        with patch.object(server.audio_spectrum, "ensure") as ensure:
            result = server.apply_connection(core, state, form(**kw))
        return result, core.saved, ensure

    def test_defaults_keep_stats_on(self):
        result, saved, _ = self.apply()
        self.assertTrue(result["success"])
        self.assertTrue(saved["send_pc_stats"])
        self.assertFalse(saved["audio_viz_auto"])

    def test_stats_can_be_switched_off(self):
        result, saved, _ = self.apply(send_pc_stats="0", audio_viz="1", audio_viz_auto="1")
        self.assertTrue(result["success"])
        self.assertFalse(saved["send_pc_stats"])
        self.assertTrue(saved["audio_viz"])
        self.assertTrue(saved["audio_viz_auto"])

    def test_auto_start_values_round_trip(self):
        _r, saved, ensure = self.apply(audio_viz_auto="1", audio_viz_threshold="-52",
                                       audio_viz_start_delay="4.5", audio_viz_stop_delay="45")
        self.assertEqual(saved["audio_viz_threshold"], -52.0)
        self.assertEqual(saved["audio_viz_start_delay"], 4.5)
        self.assertEqual(saved["audio_viz_stop_delay"], 45.0)
        ensure.assert_called_once()

    def test_out_of_range_values_are_rejected(self):
        for bad in ({"audio_viz_threshold": "5"}, {"audio_viz_start_delay": "120"},
                    {"audio_viz_stop_delay": "0"}):
            result, saved, _ = self.apply(**bad)
            self.assertFalse(result["success"], bad)
            self.assertIsNone(saved, bad)


class ReachabilityTests(unittest.TestCase):
    """The probe must issue a real request: a bare connect stalls the device's
    render loop for ~150ms while its web server waits for a request line."""

    def test_probe_requests_a_page(self):
        with patch.object(server, "urlopen") as urlopen:
            urlopen.return_value.__enter__.return_value.read.return_value = b"{}"
            reachable, detail = server.device_reachable("192.168.0.153")
        self.assertTrue(reachable)
        self.assertEqual(detail, "")
        self.assertIn("http://192.168.0.153/api/status", urlopen.call_args[0][0])

    def test_http_error_still_counts_as_reachable(self):
        with patch.object(server, "urlopen", side_effect=server.HTTPError(
                "u", 404, "nf", {}, None)):
            self.assertEqual(server.device_reachable("192.168.0.153")[0], True)

    def test_unreachable_reports_detail(self):
        with patch.object(server, "urlopen", side_effect=OSError("no route")):
            reachable, detail = server.device_reachable("192.168.0.153")
        self.assertFalse(reachable)
        self.assertIn("no route", detail)

    def test_empty_address_is_not_probed(self):
        with patch.object(server, "urlopen") as urlopen:
            self.assertFalse(server.device_reachable("")[0])
        urlopen.assert_not_called()


if __name__ == "__main__":
    unittest.main()
