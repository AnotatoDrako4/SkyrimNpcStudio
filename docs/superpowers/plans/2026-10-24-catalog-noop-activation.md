# Skip Winner Rebuild for an Unchanged Load Order — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Skip rebuilding SQLite load-order and winner tables when an index pass activates the exact same persisted load-order snapshot, while preserving usage stamps and cross-process consistency checks.

**Architecture:** Keep the change inside `CatalogStore`. Extend the stable load-order digest to cover every persisted `LoadOrderRow` field, then compare it with `meta.active_load_order` before rebuilding. On a match, refresh `plugin_usage` transactionally and return; on a mismatch, keep the existing rebuild path.

**Tech Stack:** C# / .NET 10, Microsoft.Data.Sqlite, xUnit.

## Global Constraints

- Remain in F0 and preserve the QML → viewmodels → Qt-free/JSON-RPC → BethesdaWorker architecture.
- Do not modify game files, saves, Skyrim/Data, or the normal MO2 profile.
- Preserve cache correctness across concurrent processes; keep `EnsureActiveLoadOrder` behavior for a different active snapshot.
- Do not change cache schema or add dependencies.
- No commit, merge, branch change, or publication from the authorized in-place checkout.

## Review Focus

- Identical plugin rows with identical metadata: the second activation skips load-order and winner-table writes.
- Same plugin IDs but changed `FormIdPrefix`: the digest differs and tables rebuild.
- Changed persisted path or master list: the digest differs and tables rebuild.
- Another process activates a different snapshot: the existing stale-reader guard still returns `IndexReplaced`.
- Repeated activation still refreshes plugin usage stamps so cache purge retention is unchanged.

---

### Task 1: Avoid rebuilding winner tables for an unchanged snapshot

**Files:**
- Modify: `services/BethesdaWorker/NpcCatalog/CatalogStore.cs`
- Test: `tests/unit/BethesdaWorker.Tests/CatalogStoreConcurrencyTests.cs`

**Interfaces:**
- Preserve `CatalogStore.ActivateLoadOrder(IReadOnlyList<LoadOrderRow> rows)`.
- Preserve `CatalogStore.EnsureActiveLoadOrder()` and its `IndexReplaced` behavior.
- Preserve the existing `LoadOrderDigest(IReadOnlyList<LoadOrderRow>)` call contract.

- [ ] **Step 1: Add a failing no-rebuild test**

Add `ActivateLoadOrder_RepeatingIdenticalSnapshotDoesNotRewriteLoadOrder` to `CatalogStoreConcurrencyTests`. Activate one non-empty load-order row, add a SQLite trigger on `load_order` that records deletes, activate the same row again, and assert the audit count remains zero.

- [ ] **Step 2: Run the focused test and confirm it fails**

Run from the repository root:

```powershell
dotnet test .\tests\unit\BethesdaWorker.Tests\BethesdaWorker.Tests.csproj --filter FullyQualifiedName~ActivateLoadOrder_RepeatingIdenticalSnapshotDoesNotRewriteLoadOrder --no-restore
```

Expected: FAIL because the current implementation deletes and reinserts the load-order rows on every activation.

- [ ] **Step 3: Cover every persisted row field in the load-order digest**

Update `LoadOrderDigest` to include `FormIdPrefix`, `Path`, and the ordered `Masters` values in addition to its current fields. Keep ordering deterministic and case handling consistent with the stored Windows/plugin semantics.

- [ ] **Step 4: Add digest-change regression assertions**

Add `LoadOrderDigest_ChangesWhenPersistedRowMetadataChanges`; assert distinct digests when only `FormIdPrefix`, `Path`, or `Masters` changes. These cases prevent an unsafe early return when rows differ in persisted state.

- [ ] **Step 5: Skip materialization only when SQLite confirms the same digest**

In `ActivateLoadOrder`, compute the digest before starting the existing rebuild. If `meta.active_load_order` equals it, refresh `plugin_usage` for the incoming rows in a transaction, commit, set `_activeLoadOrder`, and return without deleting or recomputing the materialized tables. If it differs or is absent, use the existing transactional rebuild path.

- [ ] **Step 6: Run focused cache concurrency and purge tests**

Run:

```powershell
dotnet test .\tests\unit\BethesdaWorker.Tests\BethesdaWorker.Tests.csproj --filter "FullyQualifiedName~CatalogStoreConcurrencyTests|FullyQualifiedName~CatalogPurgeTests" --no-restore
```

Expected: PASS, including the unchanged-snapshot no-op, metadata-change, stale-reader, cache-purge retention, and duplicate-signature cases.

- [ ] **Step 7: Review the local diff**

Run `git diff --check -- services/BethesdaWorker/NpcCatalog/CatalogStore.cs tests/unit/BethesdaWorker.Tests/CatalogStoreConcurrencyTests.cs` and inspect the diff. Do not commit.