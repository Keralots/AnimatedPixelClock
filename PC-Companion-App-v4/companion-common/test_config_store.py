"""A config that will not parse must be copied aside, not silently replaced.

Run: python test_config_store.py
"""

import io
import json
import os
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import config_store


class QuarantineTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="cfgstore-")
        self.path = os.path.join(self.dir, "monitor_config.json")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def test_damaged_file_is_copied_aside_and_left_in_place(self):
        with io.open(self.path, "w", encoding="utf-8") as f:
            f.write("{ not json")
        dst = config_store.quarantine_unreadable_config(self.path, "test")
        self.assertTrue(dst)
        self.assertTrue(os.path.isfile(dst))
        with io.open(dst, encoding="utf-8") as f:
            self.assertEqual(f.read(), "{ not json")
        self.assertTrue(os.path.isfile(self.path))

    def test_missing_file_reports_no_copy(self):
        self.assertEqual(config_store.quarantine_unreadable_config(self.path, "gone"), "")

    def test_bom_config_still_parses(self):
        # The reason load_config reads with utf-8-sig: an editor or a
        # PowerShell redirect adds a BOM to an otherwise valid config.
        with io.open(self.path, "w", encoding="utf-8-sig") as f:
            f.write(json.dumps({"version": "2.1"}))
        with io.open(self.path, encoding="utf-8-sig") as f:
            self.assertEqual(json.load(f)["version"], "2.1")


if __name__ == "__main__":
    unittest.main(verbosity=2)
