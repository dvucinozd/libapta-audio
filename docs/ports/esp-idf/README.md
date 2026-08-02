# ESP-IDF port

Canonical integration and configuration guidance:

- [`../../../ports/espidf/README.md`](../../../ports/espidf/README.md)
- [`../../../examples/espidf/cooperative_scheduler/README.md`](../../../examples/espidf/cooperative_scheduler/README.md)
- [`../../reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md`](../../reference/APTA-ESP-IDF-MEMORY-PROFILES-0.1.md)
- [`../../status/S7-ESP-IDF-PORT-STATUS.md`](../../status/S7-ESP-IDF-PORT-STATUS.md)
- [`../../conformance/APTA-S7-READINESS-0.1.md`](../../conformance/APTA-S7-READINESS-0.1.md)

The CI evidence covers host regressions and firmware cross-build/link checks;
CI itself does not execute a physical board. A separate manual ESP32-P4
measurement is recorded in the S4 status and cooperative-example documents,
but it is not a certified performance or resource-class claim.
