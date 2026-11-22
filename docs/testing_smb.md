# SMB tests without game data

The public native suite uses the three original synthetic documents in
`tests/fixtures/smb`. A clean checkout needs no ROM, game checkout or Kit to run
the four SMB programs or `malformed_documents_test`. Missing public fixtures
fail the test; they are never skips. Run the normal suite with Python:

```text
python tools/test.py --suite native
```

The shared malformed-document test builds its own small rendering tables in
memory, so its Castle Excellent, Solomon, project and SMB checks always run.
`tests/fixtures/smb/README.md` describes coverage and reproduction. The Kit's
Python renderer checks the same independently specified synthetic expectations.

## Optional local ROM comparison

Full comparisons against all 34 SMB1 JU areas, screen/text resources and music
headers remain separate from public CI. Use a ROM you provide and the external
SMB1 game profile. From the Kit checkout:

```text
python tools/smb_test_corpus.py rom --rom "path/to/your/SMB1-JU.nes" --profile "path/to/smb1_src/config/game_profile.json"
```

The generator verifies the complete ROM size and hashes against the profile
before decoding. It writes `levels.json`, `screens.json` and `music.json` only
under Kit's ignored `build/smb-rom-corpus/`. `--output` may select another
subdirectory of Kit's ignored `build/`; tracked destinations are refused. Every
output carries `provenance.kind = "rom-derived"` and the ROM hash. The tool
reads the ROM and profile; it does not change the game checkout.

Build Studio's native tests first. Then from the Studio checkout:

```text
python tools/test_smb_rom.py --bin build/tests/test-bin --corpus ../retrolarium-kit/build/smb-rom-corpus
```

For shared Qt on Windows, add `--qt-prefix path/to/Qt`. This command requires
all three corpora from the same ROM and all four test programs. Missing files,
timeouts, failures and exit 77 all fail; synthetic fixtures cannot satisfy it.
The original 34-area names and counts are still checked in ROM mode.

## Keep ROM-derived data local

Never commit or upload these corpora, move them into `tests/fixtures`, or
include them in release packages. Both repositories' Python suites inspect Git
publication candidates, including force-added ignored files. They refuse ROM
files, the old corpus filenames and renamed JSON files marked ROM-derived.
Studio's release ZIP writer and final archive verification also reject those
files. OpenDiskSysROM, the separately licensed open firmware shipped as `.bin`,
is unaffected. Kit wheels package `src/` only; local corpora stay in `build/`.

These guards supplement the deliberately separate directories and provenance;
they are not a general detector for arbitrary copied game content.
