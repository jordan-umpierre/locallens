# Local boundary contract

LocalLens currently exposes a CLI and opaque local playback grants, not an HTTP service. Adding OpenAPI, rate-limit middleware, retries, or a service mesh would create a boundary that does not exist. The command grammar in `README.md` is compatibility-stable v1: additive commands are allowed; changed argument meanings require a new command name or explicit version flag.

Each structured operation emits a `trace_id`. A supervisor may provide `LOCALLENS_TRACE_ID`; otherwise LocalLens creates a random 128-bit value. Fatal and quarantine logs expose stable error codes, never exception messages, grant tokens, recovery-key paths, absolute source paths, or media bytes. Binary `read` output stays pure bytes and must be correlated by its invoking process.

| Boundary | Deadline and cancellation | Retry/idempotency | Limit |
|---|---|---|---|
| import | supervisor deadline based on card size; SIGTERM between invocations | safe to rerun; content hash and provenance constraints prevent duplicate media | one import process per catalog |
| playback grant/read | 5-minute grant; caller cancels the local stream | grant may be reacquired; a range read may retry from the last confirmed byte | 1 MiB maximum request range, one local reader per invocation |
| backup | supervisor-configured deadline; never interrupt key/object writes deliberately | a failed set may be retried as a new manifest; object names are content-addressed | one backup process per catalog |
| verify/restore | supervisor-configured deadline | verification is safe to retry; restore requires a clean target | one recovery process per target |

The current filesystem-backed remote simulation has no webhook, remote API, realtime channel, payment action, or network retry. A future S3 adapter may retry idempotent `HEAD`, `GET`, and content-addressed `PUT` operations for timeout/429/5xx twice with jitter and `Retry-After`; multipart completion requires persisted upload state and must not be blindly replayed. Connect timeout is 5 seconds and per-part deadline is 60 seconds. Five consecutive remote failures open a 30-second circuit while local ingest and playback remain available.

Indicators are import error ratio, age of last verified backup, and failed playback grants. Any import error blocks safe-eject status and requires operator review. A backup older than its configured schedule plus 24 hours opens an operator alert with the trace ID and manifest ID. Repeated invalid grants are logged as counts without tokens and trigger local access review, not an availability page.

`make test` reproduces duplicate import safety, bounded playback, wrong-key/tamper failure drills, correlated backup events, and fatal-log redaction. Raspberry Pi timing, power-loss behavior, live object-storage availability, and global availability remain unmeasured.
