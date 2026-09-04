# SPDX-License-Identifier: Apache-2.0
import unittest
import apta_key_blind_review as review


class ReviewTests(unittest.TestCase):
    def test_selection_balanced_unique_and_order_independent(self):
        rows = [dict(track=f"{group}-{mode}-{i}", group=group, key_mode=mode)
                for group in review.GROUPS for mode in ("major", "minor") for i in range(4)]
        selected = review.select(rows)
        self.assertEqual(selected, review.select(list(reversed(rows))))
        self.assertEqual(len({r["track"] for r in selected}), 12)
        for group in review.GROUPS:
            for mode in ("major", "minor"):
                self.assertEqual(sum(r["group"] == group and r["key_mode"] == mode for r in selected), 2)

    def test_missing_stratum_rejected(self):
        with self.assertRaises(ValueError):
            review.select([])

    def test_duplicate_ids_rejected(self):
        with self.assertRaises(ValueError):
            review.indexed([dict(track="x"), dict(track="x")])

    def test_tonic_and_mode_mapping(self):
        names = ("C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B")
        aliases = {"B#": 0, "Db": 1, "D#": 3, "Fb": 4, "E#": 5, "Gb": 6, "G#": 8, "A#": 10, "Cb": 11, "D♭": 1, "F♯": 6}
        for name, tonic in list(zip(names, range(12))) + list(aliases.items()):
            for mode in ("major", "minor"):
                self.assertEqual(review.corpus.transport.normalize_key(name + " " + mode), (tonic, mode))
        with self.assertRaises(ValueError):
            review.corpus.transport.normalize_key("H major")


if __name__ == "__main__":
    unittest.main()
