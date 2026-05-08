# Project-XS

`Project-XS` is a small submodule repository used to host auxiliary scripts and
datasets for sparse linear algebra experiments.

## Contents

- `script/generate_cg_dataset.py`
  Generates deterministic sparse SPD datasets for Conjugate Gradient style
  solvers.
- `data/generated/`
  Default output location for generated datasets. This directory is ignored by
  git.
- `csrc/`
  Placeholder for future native utilities.

## Dataset Model

The generator builds a sparse symmetric positive definite matrix from a
heterogeneous diffusion-like graph:

- a 2D near-rectangular mesh with exact requested unknown count
- anisotropic horizontal and vertical conductances
- deterministic long-range contact links to mimic irregular couplings
- a positive diagonal mass term to keep the system well-conditioned for CG

The dataset format in this repository is intentionally kept to exactly three
files:

- `A.mtx`
- `b.txt`
- `x0.txt`

`A.mtx` uses the Matrix Market coordinate format, which keeps the sparse matrix
in a single text file and avoids mirroring the root repository's split CSR
layout.

## Usage

```bash
python3 script/generate_cg_dataset.py --size 4096
python3 script/generate_cg_dataset.py --size 8192 --output-dir data/generated/cgsolver/n8192
```
