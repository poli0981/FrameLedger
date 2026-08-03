# Rule fixture corpus

Directory trees that `rules/detection-rules.json` is evaluated against, by
`RuleFixtureCorpusTests` in `FrameLedger.Infrastructure.Tests`.

`05_DETECTION` and `13_CI_CD` both used to claim `tools/rules-validate` ran rules
against fixture trees in CI. It never evaluated a rule, and these trees did not
exist. Both documents now describe what actually happens: the **validator**
checks schema, imperative constraints and fixture *coverage*; the **evaluation**
is this corpus, run by `build.ps1 check` and therefore by CI.

## What a fixture is

One directory per case, containing zero-byte marker files and an
`expected.json`:

```json
{ "engine": "unity", "platform": null, "capabilities": [], "$why": "..." }
```

`$why` is required in spirit, not by a schema: a fixture whose expectation
nobody can explain is a fixture nobody can maintain.

## Rules for this directory

- **Zero-byte markers only.** No real game files, no vendor binaries, no
  copyrighted content. `14_TESTING` §Integration tests already sets this
  precedent for anti-cheat fixtures ("a renamed harmless DLL, not real
  anti-cheat software") and it applies here for the same reason.
- **No anti-cheat fixtures live here.** The blocklist has one matcher and it is
  native; its fixtures are Catch2 cases in `src/native/tests`.
- **`.gitattributes` marks this tree `-text`.** Line-ending normalisation must
  not mutate bytes a test depends on — today nothing here has content, but a
  `strings_contains` fixture would, and that is exactly the fixture that would
  fail mysteriously on someone else's clone.
- **Empty directories need a `.keep`.** Git stores no empty directory, and
  `Game_Data/`, `renpy/` and `.itch/` are load-bearing: they are what
  `dir_exists` looks for.

## What this corpus does NOT cover

`pe_company_contains`, `pe_product_contains` and `strings_contains` need real
bytes in a real PE. Zero-byte markers cannot carry a version resource, so those
signal types are exercised by `RuleEvaluatorTests` against in-memory snapshots
instead. Said plainly so nobody reads a green corpus as covering them.

## The two canaries

They are not extra cases; they are what stops the rest being decorative.

- `canaries/no_engine` — **over-match.** If the evaluator ever matches
  everything, every positive fixture above still passes. Only this one goes red.
- `canaries/every_engine_marker` — **under-match and ordering.** Four engines'
  markers are present at once; exactly one engine must be reported, and it must
  be the first in the array.

Together they assert both directions, which is the rule the rest of this
repository is held to.
