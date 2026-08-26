# DSP experiment protocol

Use this reference for key, meter, downbeat, beatgrid, tempo, or confidence changes.

## Record before coding

Write a short experiment record containing:

- hypothesis and the specific failure mode it addresses;
- baseline revision, build flags, and baseline metric;
- development evidence to be used (never a formal holdout for tuning);
- expected accuracy/resource trade-off;
- an explicit no-regression veto and a stop condition.

The current development evidence says the main remaining deficits are key accuracy and beat-lattice phase. Key profile shape and fixed longer windows have already yielded insufficient gains; the next useful axes are harmonic-weighted evidence, tuning estimation, or adaptive aggregation. For beatgrid/downbeat, simple rescoring of the existing full-band flux is exhausted; prioritize new onset/temporal evidence and upstream lattice quality.

## Implement narrowly

- Add an opt-in CMake/build flag for a new detector path.
- Preserve default state layout, output semantics, and serialized ordering unless the experiment explicitly measures those contracts.
- Keep trace tools diagnostic and privacy-safe; do not include source filenames or audio in committed artifacts.
- Add focused selector/regression tests for every changed decision rule.

## Evaluate in layers

1. Run focused unit tests and the normal Werror suite.
2. Run the candidate on the ordered development corpus and compare baseline/candidate per track.
3. Check fixes, breaks, changed verdicts, confidence safety, and resource cost. A small gain with any safety regression is not a promotion candidate.
4. Run independent transfer checks on open development partitions (for example Ballroom or ASAP) without opening their holdouts.
5. Only after the candidate is stabilized, run one newly verified >=48-track acceptance corpus through the frozen evaluator.

Do not compare scores from different evidence spaces directly. Compare verdict changes and safety instead. Do not enable a candidate by default merely because it improves a spent corpus.

## Minimum evidence record

Record the full commit SHA, build flags, analyzer hash, corpus/manifest hash, test counts, accuracy per family, high-confidence error counts, fixes/breaks, changed verdict count, and memory/runtime delta. State whether the result is diagnostic-only, development evidence, rejected acceptance evidence, or accepted release evidence.
