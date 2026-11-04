Build the application specified in SPEC.md. Work fully autonomously from start to finish. Do not ask questions or wait for confirmation at any point.

## Source of truth
- SPEC.md defines the product. Build exactly what it specifies, with no extra features, screens or settings.
- Section 14 (Implementer's Latitude) lists what is yours to decide. Section 9.4 lists binding technical constraints. Everything else is decided.
- The engine formulas in §7.1 and the LCD strings in §5.3 are exact. Treat them as data, not as suggestions.

## When something is unclear
If the spec does not cover a situation, choose the simplest behaviour consistent with the spec's overview (§1), glossary (§2) and existing rules. Prefer what the original hardware does, where the spec's sources (§15.5) make that clear. Record the choice in DECISIONS.md (what, why, which spec section it relates to) and continue. Never stop to ask.

If two parts of the spec contradict each other, the more specific one wins: an acceptance criterion over a rule, and a rule over a table example. Record the conflict in DECISIONS.md.

## External services
There are none (§10). Tests use the Web MIDI and audio test doubles that §10 describes. Never block on missing hardware, browsers or credentials. Anything that needs them is listed in §15.4 and is not part of your acceptance.

## How to work
1. Read SPEC.md fully. Write PLAN.md: a task list ordered into vertical slices, each mapped to the R-/F-/AC- IDs it satisfies. The recommended order in §14 is a good default.
2. Set up the repository so it builds, runs and tests from a clean checkout on macOS, Windows, Ubuntu and with emcc. Set up CI (§15.1).
3. Implement slice by slice. After each slice, run all tests, fix failures, and commit.
4. Implement every acceptance criterion in §13 as an automated test, with the AC ID in the test name. User-facing flows get end-to-end tests that drive the real UI (Playwright).
5. When all slices are done:
   1. Run the full suite and the benchmarks.
   2. Start the standalone app and `npm run web`.
   3. Exercise every flow in §6 yourself, through the front panel and the programmer.
   4. Fix anything broken.

## Definition of done
- Every AC in SPEC.md §13 has a passing automated test, natively and under WASM where §13 says so.
- The licence check passes, and docs/LICENSES.md and docs/STATE.md exist.
- Everything builds and runs from a clean checkout with the commands documented in README.md.
- No TODOs, placeholder screens or stubbed features remain, except the test doubles defined in §10.

## Final report
When done, write REPORT.md:
- what was built;
- how to run it;
- test results (count and pass rate per suite, benchmark numbers);
- every entry in DECISIONS.md;
- known gaps, including anything from §15.4 a human now needs to check.
