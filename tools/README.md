# tools/

Build helpers used by the Makefile. Run them inside the OpenOrbis toolchain container.

| File | What it does |
|---|---|
| `build-create-fself.sh` | Builds the real `create-fself` (Go) into `tools/create-fself`. Run once. |
| `create-eboot-golden` | Wraps `create-fself` and fails unless `eboot.bin` starts with the SELF magic `4F 15 3D 1D`. |
| `make_sfo.sh` | Builds `sce_sys/param.sfo` with `PkgTool.Core` and checks the Title ID and Content ID lengths. |
| `copy-sce-modules.sh` | Copies the `sce_module/*.prx` stubs and `right.sprx` from the SDK. |
| `copy-right-sprx.sh` | Copies `right.sprx` from the SDK. |
| `create-gp4` | Writes the `pkg.gp4` project file for `PkgTool.Core`. |
| `gen_assets.py` | Generates backgrounds, icon, splash, font atlases (`src/font_data*.h`) and re-encodes the songs. Needs Python 3, Pillow, numpy, ffmpeg and `Poppins-Bold.ttf` (SIL OFL) in this folder or installed system-wide. |

After cloning: `chmod +x tools/*` then `sh tools/build-create-fself.sh`.
