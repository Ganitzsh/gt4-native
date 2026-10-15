# Bulk GT4 decomp import

The native repository now has a **bulk importer** instead of manually copying individual functions.

## What is imported

The workflow checks out the upstream `gustavocanepa/gt4-decomp` repository at the **exact pinned commit**
`de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1` and copies **all** C/C++ source and header files under `src/` and `include/` into a disposable import workspace.

The resulting GitHub Actions artifact `gt4-decomp-bulk-import` contains:

- `gt4/src/` — every source file from the pinned decomp snapshot
- `gt4/include/` — upstream headers
- `manifest.json` — path, SHA-256, size, detected includes and symbol mentions per file

CI checks each copied file's SHA-256 and verifies more than 10,000 source translation units. This is **imported source inventory**, not a claim that 10,000 functions already run on PC.

## Run the importer locally

```sh
git clone https://github.com/gustavocanepa/gt4-decomp.git upstream-checkout
git -C upstream-checkout checkout de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1
python3 -m unittest discover -s tests -p test_import_decomp.py -v
python3 tools/import_decomp.py --upstream upstream-checkout \
  --dest bulk-import/gt4 --manifest bulk-import/manifest.json \
  --commit de3d7edbead9e8a6e473b1e68b8ddf3b1f639fd1
```

Or use the [GT4 bulk source inventory GitHub Actions workflow](https://github.com/gustavocanepa/gt4-native/actions/workflows/import-decomp.yml), which publishes the bundle without committing tens of thousands of duplicate files into the native repo.

## What is NOT done

- Importing does **not** mean the code compiles with Clang, GCC, or MSVC.
- The matching upstream source may contain PS2-specific inline assembly, EE-GCC assumptions, 32-bit pointer layouts, incompatible declarations and unresolved externals.
- `missing_local_includes` is a conservative file-path hint, **not** a full preprocessor or link dependency graph. System includes can appear there.
- `symbol_mentions` is a regex-based index, **not** a validated call graph or function count.
- The bulk-imported files are **not** added to the native executable or to the existing CMake targets automatically.
- No original executable, disc data or game assets are imported.

## Next gate

Build an automated host-compatibility classifier that attempts compilations in batches, records the exact failure category and groups translation units into dependency clusters. Promote only compatible, tested clusters into native CMake targets. This avoids mistaking raw import counts for running game logic.
