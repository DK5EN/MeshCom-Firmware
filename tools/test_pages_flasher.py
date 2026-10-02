#!/usr/bin/env python3
"""Tests for tools/pages_flasher.py argument defaults.

    python3 -m unittest tools/test_pages_flasher.py
"""

import sys
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pages_flasher  # noqa: E402


def parsed_args(*argv: str):
    seen = {}

    def capture(args):
        seen["args"] = args
        return 0

    with mock.patch.object(pages_flasher, "cmd_publish", capture), \
            mock.patch.object(pages_flasher, "cmd_stage", capture):
        pages_flasher.main(list(argv))
    return seen["args"]


class KeepDefault(unittest.TestCase):
    def test_publish_without_keep_offers_only_one_release(self):
        self.assertEqual(parsed_args("publish", "--version", "v0").keep, 1)

    def test_stage_without_keep_offers_only_one_release(self):
        self.assertEqual(parsed_args("stage", "--version", "v0", "--out", "x").keep, 1)

    def test_help_text_does_not_suggest_keeping_more(self):
        self.assertNotIn("--keep 3", pages_flasher.__doc__)


if __name__ == "__main__":
    unittest.main()
