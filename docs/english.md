# English dictionary

The bundled offline dictionary contains 195,225 lowercase ASCII words and
inflected forms from SCOWL 2026.02.25, covering American, British (-ise/-ize)
and Canadian English through size 80, variant level 1. Proper names, compounds,
apostrophes and words longer than 40 keys are omitted. Diacritics use SCOWL's
`--deaccent` export. Matching preserves the capitalization of exact input.
SCOWL size is a commonness tier, not a measured corpus frequency.

Source: https://github.com/en-wl/wordlist/tree/rel-2026.02.25
License and attribution: `share/fcitx5/phono/english.Copyright` in installed
packages. The word list is redistributable under SCOWL's MIT-like license;
no online service or Python package is needed at runtime.

To regenerate from the fixed source version, run `make` in that checkout, then
`pixi run python tools/import_english.py /absolute/path/to/wordlist`.
The generated list is bucketed by length and sorted lexicographically.

English settings are inside **Default dictionary → English dictionary**:
enable/disable, minimum input length (default 4), and placement before/after the
whole model group. English selections commit directly without learning Chinese
user words. Native segmentation suppresses English suggestions for the rest of
the composition, just like model suggestions.

Extra sources append to stable before-model and after-model lists. User words
register first, then English words; both default to before-model. They therefore
appear as user words → English words → model beams → native dictionary entries.
Each source can independently move after the model group. Model beams stay
contiguous. Text deduplication retains the first displayed occurrence and all
native choices retain their original libime indices.

The native config file has an intentionally hidden
`[DefaultDictionary/SourceOrder]` list (`0=user`, `1=english`) for source policy.
It is not exposed in the configuration UI. Unknown/duplicate entries are ignored;
omitted known sources are appended in the default order.

Fcitx5 controls candidate colors through its UI/theme. This addon does not add
source labels or override colors.
