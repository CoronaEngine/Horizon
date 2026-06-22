# Runtime Diagnostics And Crash Reporting Task Notes
<!-- TASK_DOCS_RUNTIME_DIAGNOSTICS_REPORTING_ZH_CN_SHA256: 0bd95e3fb550fa356993918c40b8f55a9d81ba3d0c5631a8fe4fdca67dace98f -->

## Goal

- Build a scalable information collection, crash reporting, and server analysis system for future Horizon / HorizonExamples / downstream app deployments across hundreds or thousands of machines.
- Make the local report package complete, reproducible, and usable offline before relying on automatic upload.
- Crash triage should answer: which machine class, which OS and driver, which Horizon build, which example or feature path, what the last Vulkan / Horizon diagnostics said, which thread crashed, what the stack was, and whether symbols can restore it.

## Design Principles

- Start with explicit modes such as `Off`, `LocalOnly`, `UploadDiagnostics`, and `UploadCrashMinidump`; product / release policy decides whether upload is enabled by default. The library layer must not silently phone home.
- Upload must not block rendering, submit, present, or crash handling. Normal runs write a local queue; a background worker or next launch can upload later.
- Crash handling may only do minimal async-safe or platform-recommended work: write a dump, flush existing ring logs, and record the exception code. Do not allocate heavily, take complex locks, or send HTTP from the crash handler.
- Collection must support redaction, rate limits, sampling, and a global off switch. Do not upload source, user files, complete environment variables, plaintext paths, tokens, secrets, or arbitrary memory.
- Stable anonymous machine / install IDs should be resettable; do not use real user names, machine names, or hardware serial numbers as primary keys.
- Public `include/` must not expose Vulkan, Windows, upload protocol, or server details. Keep this in internal diagnostics / tooling first, then consider public API only after the contract settles.
- Support multithreaded callers: report sinks, breadcrumb rings, upload queues, and crash state need clear locks, atomics, or owner-thread boundaries.

## Report Contents

- Report envelope: `schema_version`, `report_id`, timestamp, session ID, anonymous install ID, Horizon version, git commit/build id, build configuration, program name, example mode, and command-line allowlist.
- Machine information: OS version, CPU architecture/core count, memory size, GPU list, driver versions, Vulkan loader/API version, available layers/extensions, selected/skipped devices, and skip reasons.
- Horizon runtime information: enabled feature flags, `HORIZON_ENABLE_HARDWARE_VALIDATION`, `HORIZON_ENABLE_VULKAN_VALIDATION`, key environment-variable allowlist, resource/pipeline/queue counts, recent submit tokens, and window/swapchain state.
- Diagnostic attachments: `horizon-vulkan-diagnostics.txt`, stdout/stderr summaries, Horizon validation records, Vulkan validation records, recent breadcrumb ring logs, and required config snapshots.
- Crash attachments: exception code/signal, crashing thread, thread list, stack traces, loaded modules, symbol-file identifiers, minidump/crash dump, last unhandled error, and current report package path.
- Upload metadata: compression format, content hashes, attachment list, collection mode, sampling reason, redaction version, upload attempt count, and server response ID.

## Client Architecture

- Prefer local report packages: on abnormal exit, manual request, or key compatibility failure, create a directory or archive containing `report.json`, diagnostic txt, log snippets, and optional dump.
- Continue reusing `src/hardware_wrapper/diagnostics.*` and `horizon-vulkan-diagnostics.txt` for backend diagnostics; the new system aggregates them without moving Vulkan details into public API.
- Split crash capture by platform. On Windows, prefer WER LocalDumps or a `MiniDumpWriteDump`-style minidump path; add signal handler / core dump integration for other platforms later.
- Use a fixed-size breadcrumb ring for high-value events: device selection, pipeline creation, descriptor allocation, image layout transitions, queue submit/present, swapchain resize, validation errors, and selected example mode.
- Decouple uploader from collector: the collector only writes to disk; the uploader scans the local queue with backoff, rate limits, max disk usage, and a kill switch.
- Downstream apps may set product name, version, consent state, extra key/value data, extra attachments, and custom endpoint, but they must not bypass redaction or size limits.

## Server And Analysis

- The ingest service should only handle auth, rate limits, schema validation, size limits, virus/format checks, object-storage writes, and ingest ID responses.
- Analysis groups by fingerprint: exception code + normalized stack + Horizon build id + GPU/driver/Vulkan API + validation message.
- First dashboards: top crashes, unsupported GPU/driver/API reports, validation-message hotspots, build regressions, hardware/driver clusters, and upload failure rate.
- The symbol system must map build ids to PDB/debug symbols. Without symbols, reports can still group by module/offset, but triage is not complete.
- Retention policy must be explicit: diagnostic JSON, logs, minidumps, and full dumps may have different lifetimes. Deletion requests and project shutdown must clean object storage.

## Phased Implementation

- P0 local report package: define `report.json` schema, collect machine/Vulkan/Horizon build info, aggregate existing `horizon-vulkan-diagnostics.txt`, and support manual report generation.
- P1 crash evidence: Windows minidump/WER integration, PDB/build id records, forced-crash test program, symbolication verification, and no crash-handler deadlock.
- P2 upload queue: local pending/sent/failed queues, compression, hashes, backoff retry, offline recovery, global off switch, and upload CLI or background uploader.
- P3 server ingest: minimal HTTPS endpoint, auth, schema validation, object storage, report ID, and basic query/download.
- P4 aggregate analysis: fingerprints, dashboard, build/GPU/driver/validation clustering, dedupe, and alerts.
- P5 product policy: consent/privacy copy, sampling, retention, full-dump policy, and downstream-app integration guide.

## Non-Goals

- Do not build a full remote logging or continuous performance monitoring platform in the first phase; close the crash and compatibility diagnostics loop first.
- Do not send network requests directly from Horizon's core rendering path.
- Do not upload full memory dumps by default; full dumps may contain user data and require explicit consent in a controlled environment.
- Do not put Windows / Vulkan / HTTP types into public headers for reporting convenience.
- Do not collect user project source, asset contents, access tokens, complete paths, complete environment variables, or arbitrary clipboard/window contents.

## Acceptance

- Running an example without network and triggering a compatibility failure creates a local report package with machine info, Horizon build info, and `horizon-vulkan-diagnostics.txt`.
- A forced-crash test creates a minidump and `report.json`, and matching PDBs can restore function/file/line or at least module/offset.
- If the upload endpoint is unreachable, the program does not block or crash; the report remains in the local queue and uploads after network recovery.
- Redaction tests prove that user names, absolute user paths, environment-variable secrets, and tokens do not appear in default reports.
- Under multithreaded stress, breadcrumbs and the report sink have no data races, deadlocks, or unbounded memory growth.
- The server can group reports by crash fingerprint, GPU, driver, Vulkan API, and Horizon build, and a single report package can be downloaded for review.

## Related Entrypoints

- Vulkan local diagnostics rules: `docs/agents/vulkan.md`.
- Vulkan backend task notes: `docs/tasks/vulkan-backend.md`.
- Example visible-window and runtime crash smoke: `docs/tasks/examples-new-api-visible-window.md`.
- Current local diagnostics implementation: `src/hardware_wrapper/diagnostics.*`.
- Current diagnostics output: `horizon-vulkan-diagnostics.txt`, overrideable with `HORIZON_VULKAN_DIAGNOSTICS_PATH`.
