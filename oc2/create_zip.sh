#!/bin/sh
set -eu

output_dir=$(pwd -P)
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
project_root=$(dirname -- "$script_dir")
output="$output_dir/BOF-coerce-ipp-flat.zip"

cd -- "$project_root"

zip -r -FS "$output" README.md LICENSE \
    dist/coerce-ipp.x64.o dist/coerce-ipp.x86.o oc2 \
    -x '.git*' '*/.git*' \
       .venv '.venv/*' '*/.venv' '*/.venv/*' \
       __pycache__ '__pycache__/*' '*/__pycache__' '*/__pycache__/*' \
       .pytest_cache '.pytest_cache/*' '*/.pytest_cache' '*/.pytest_cache/*' \
       .mypy_cache '.mypy_cache/*' '*/.mypy_cache' '*/.mypy_cache/*' \
       .ruff_cache '.ruff_cache/*' '*/.ruff_cache' '*/.ruff_cache/*' \
       '*.pyc' '*/*.pyc' '*.pyo' '*/*.pyo' \
       .DS_Store '*/.DS_Store' \
       .env '*/.env' '.env.*' '*/.env.*' \
       tests 'tests/*' '*/tests' '*/tests/*' \
       create_zip.sh '*/create_zip.sh' \
       BOF-coerce-ipp-flat.zip '*/BOF-coerce-ipp-flat.zip'

# Preserve the distributable example while excluding local environment files.
zip "$output" oc2/.env.example

printf 'Created %s\n' "$output"
