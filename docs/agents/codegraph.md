# Horizon Codegraph Context
<!-- AGENT_DOCS_CODEGRAPH_ZH_CN_SHA256: 1039cbe3c186f28e632ea45ccddc0ea875b99f8b405fe313345ab6052b5c781d -->

Load this file only when codegraph tooling is available and the task involves code location, symbol flow, call chains, architecture tracing, bug localization, or refactor impact.

## Rules

- Use codegraph to get the code map first, then confirm facts with source, CMake, `git diff`, and validation commands.
- For questions like "how does X work", "who calls X", "what changes if X changes", or "where is this bug path", start with `codegraph_explore`.
- For overloaded names, mirrored trees, or ambiguous symbols, use `codegraph_node` with `file` or `line` to pin the exact definition.
- Before cross-layer refactors, use `codegraph_callers`, `codegraph_callees`, or `codegraph_impact`, especially around public API, Vulkan backend, resource lifetime, descriptors, Queue, Executor, and Helicon reflection.
- Codegraph is for orientation and impact analysis, not final authority. If the index conflicts with source, CMake, or build results, current repository facts win.
- Codegraph is optional local enhancement tooling. Its absence must not block ordinary repository work; agents should mention the fallback and continue with `rg`, source reading, CMake ownership checks, and targeted validation.
- If codegraph is unavailable or the index is missing, fall back to `rg`, source reading, and the smallest relevant build validation.
- When the user only asks whether codegraph needs updating, asks to inspect the current dirty diff, or says to confirm again, stay read-only by default: answer from `git status`, `git diff`, codegraph status/search/explore, and source checks. This is not a request for compile validation unless the user explicitly asks for validation, publication readiness, a commit, or post-implementation acceptance.

## Horizon Notes

- For Vulkan backend queries, include path or domain hints. The default target is the currently compiled `src/hardware_wrapper_vulkan/` tree.
- Do not edit historical mirror trees just because codegraph can see `src/HardwareWrapperVulkan/`, `src/hardware_wrapper/`, or `src/HardwareWrapper/`; edit them only when the user explicitly names them.
- When tracing public API, inspect `include/horizon.h` together with consumers, and keep internal Vulkan / VMA / Windows types out of public boundaries.
- When tracing execution, keep compute / dispatch, graphics / present, Queue serialization, resource lifetime, and keep-alive concerns separate. Do not treat present as generic execution semantics.
- `.codegraph/` is a local index directory and must stay uncommitted.
