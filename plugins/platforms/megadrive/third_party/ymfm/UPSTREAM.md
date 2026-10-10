# ymfm provenance

This directory contains the minimal OPN subset of Aaron Giles' `ymfm` needed by
the studio's YM2612 renderer (`retrolarium-ymfm-renderer`).

- Upstream: https://github.com/aaronsgiles/ymfm
- Commit: `81aec25ccbb98f4873a255f7551ac4dadac59b4a`
- License: BSD-3-Clause; see `LICENSE`
- Included: the common/FM, OPN, SSG, and ADPCM implementation files required to
  compile the upstream `ym2612` class

The files are copied without modification. VGM parsing, resampling, PSG
synthesis and WAV output live in `../../tools/ymfm_renderer_main.cpp`.
