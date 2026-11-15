#!/usr/bin/env python3
"""Regenerate the bundled SCOWL 2026.02.25 English word list (no runtime dependency)."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('scowl', type=Path, help='en-wl/wordlist checkout at rel-2026.02.25, after make')
    args = parser.parse_args()
    words = {}
    for tier in (35, 50, 60, 70, 80):
        result = subprocess.run([sys.executable, 'scowl', 'word-list', str(tier),
                                 'A,B,Z,C', '1', '--deaccent'], cwd=args.scowl,
                                check=True, capture_output=True, text=True)
        for word in result.stdout.splitlines():
            # Match the ASCII keys accepted by the engine; omit proper names,
            # compounds and apostrophes instead of changing their spelling.
            if re.fullmatch('[a-z]{1,40}', word):
                words.setdefault(word, tier)
    output = ROOT / 'data/phono/english.tsv'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('# SCOWL 2026.02.25; see english.Copyright and docs/english.md\n' +
                      ''.join(f'{word}\t{words[word]}\n'
                              for word in sorted(words, key=lambda w: (len(w), w))))
    (output.parent / 'english.Copyright').write_bytes((args.scowl / 'Copyright').read_bytes())
    print(f'Generated {len(words)} words')


if __name__ == '__main__':
    main()
