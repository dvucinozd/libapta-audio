# HS-04 registration count erratum

Date: 2026-10-27. Registered source:
`efc9c431dc9e6670bbed5f9653c1363b58ab74dc`.

The protocol checkpoint label `clean_exact_source_seven_case_run` and the word
"seven" in its ordered handoff are count typos. The authoritative explicit
`measured_cases` array and protocol section 7 both specify **six** cases, in order:
`fixtures`, `fullzero-44100`, `fulldc2-44100`, `fullpulse441-48000`,
`fullalternating2-48000`, `cancel`.

Execute all six. No seventh case is introduced, no registered case is removed,
and no algorithm, input, oracle, tolerance, resource limit or failure policy is
changed. Preserve the original protocol, its hash, fixtures and frozen authority
files unchanged. Bind this erratum beside that original protocol in implementation
and measurement receipts. No candidate implementation or measurement has run at
this correction checkpoint.
