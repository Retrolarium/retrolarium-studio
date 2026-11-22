# Original synthetic SMB documents

These three documents contain invented tile IDs, masks, area layouts, text, PPU
writes and a short musical exercise. They contain no ROM bytes or extracted game
tables, graphics, levels, screen contents or tunes. Contract identifiers such as
`ground_part_1` and `game-over` identify the editor's resource format.

The generator is `retrolarium-kit/tools/smb_test_corpus.py`, subcommand
`synthetic`. It uses only Python's standard library, does not load game files,
and does not call the production renderer to produce its golden expectations.
The expected grid cells, object placements and musical events are specified
independently. Kit's Python renderer and Studio's C++ renderer check the same
level expectations; Studio's decoders check the screen and music documents. Run
from the Kit checkout to reproduce them:

```text
python tools/smb_test_corpus.py synthetic --output ../retrolarium-studio/tests/fixtures/smb
```

Coverage includes four area types, item and row objects, ledges, cannons,
terrain, holes, unsorted columns, explicit pages, enemy page advances and
entrances; repeated, vertical and attribute PPU writes; text validation, fixed
widths, moving commands and capacity sharing; all four music channels, rests,
control bytes, shared streams and noise loops.

ROM-derived corpora must never replace these files. The local-only workflow is
described in `docs/testing_smb.md`.
