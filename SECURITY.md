# Security policy

## Supported versions

APTA 1.0 is the current maintained stable release line.

| Version or branch | Security fixes |
|---|---|
| Current `main` branch | Supported |
| Latest published `1.x` release | Supported |
| `1.0.0` | Supported until superseded by a newer maintained 1.x release |
| Release candidates, older commits and untagged snapshots | Not supported |
| Historical `0.x` drafts | Not supported |

Security fixes are prepared first for the current development line and the
latest maintained 1.x release. Backports to an older maintained 1.x release are
considered when issue severity and compatibility risk justify them; no 0.x or
release-candidate backport commitment exists.

The exact supported release list may be narrowed or expanded by a later
security-policy update, but a release is never silently treated as maintained.

## Report a vulnerability privately

Do not open a public issue, discussion or pull request for a suspected
vulnerability.

Send the report to
[daniel.vucinovic@gmail.com](mailto:daniel.vucinovic@gmail.com) with the
subject:

```text
[libapta security] Short description
```

This mailbox is the project's private security contact. Email is not
end-to-end encrypted by default; minimize unnecessary sensitive data in the
first message and request an encrypted follow-up channel if the report
requires one.

When GitHub private vulnerability reporting is available and enabled for this
repository, reporters may instead use:

<https://github.com/dvucinozd/libapta-audio/security/advisories/new>

If that page does not offer **Report a vulnerability**, use the email address
above. Never fall back to a public issue.

## What to include

Provide enough information to reproduce and assess the problem:

- affected commit, version, platform and build configuration;
- vulnerability class and expected security impact;
- minimal malformed `.apta` input, PCM sequence or API call sequence;
- exact reproduction steps, sanitizer output or crash trace;
- whether the issue is already public or known to another party;
- suggested mitigation or patch, if available.

Do not send credentials, unrelated personal data, copyrighted audio or
production secrets. A minimal synthetic reproducer is preferred.

## Response and disclosure process

The project targets, but does not guarantee as a service-level agreement:

- acknowledgment within five business days;
- an initial triage update within ten business days;
- progress updates at least every fourteen days while remediation is active.

The maintainer will validate the report, determine affected versions, agree on
a coordinated disclosure timeline when appropriate, prepare tests and a fix,
and publish an advisory or release notes after users have a reasonable
opportunity to update. Credit is offered unless the reporter asks to remain
anonymous.

Please allow time for coordinated remediation before public disclosure.
Reports that are not security issues may be redirected to the regular issue
tracker without publishing sensitive details.

## Security-relevant scope

Reports are especially useful for:

- memory-safety violations;
- integer overflow or out-of-bounds parser behavior;
- denial of service or uncontrolled resource consumption from untrusted
  `.apta`, metadata or PCM input;
- validation bypasses, section-confusion or canonicalization flaws;
- unsafe ownership, lifetime or concurrency behavior;
- vulnerabilities in reference tools, platform adapters or build/release
  automation.

Audio-analysis accuracy disagreements without a security impact, unsupported
optional features and documented compatibility boundaries should use the
regular issue tracker.

## Safe-harbor intent

Good-faith research that respects user privacy, avoids data destruction and
service disruption, uses only systems and data the researcher is authorized
to test, and provides reasonable time for remediation is welcomed. This
statement does not authorize testing of third-party systems or data and is not
a waiver of applicable law.
