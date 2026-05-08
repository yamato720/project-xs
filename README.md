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

The dataset format in this repository is intentionally kept to:

`A` is written in CSR form:

- `row_ptr.txt`
- `col_idx.txt`
- `values.txt`
- `b.txt`
- `x0.txt`

## Usage

```bash
python3 script/generate_cg_dataset.py --size 4096
python3 script/generate_cg_dataset.py --size 8192 --output-dir data/generated/cgsolver/n8192
```
