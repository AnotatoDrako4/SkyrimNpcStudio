# F0 Continuous Implementation Loop

Updated: 2026-10-24

## Objective

Implement the real, testable F0 scope from `docs/IMPLEMENTATION_PLAN_36_DECISIONS.md` on branch
`feature/f0-real-integration`, preserving Skyrim, MO2 profiles, `Data`, and personal saves.

## Baseline

- Repository: `D:\.Proyectos Games\SkyrimNpcStudio-ui2`
- Starting commit: `afe41e6948017211315685cd9ed4a0ee15af9a4e`
- Starting branch: `main`
- Work branch: `feature/f0-real-integration`
- Pre-existing untracked file: `docs/INTEGRATION_CONTEXT_FINGERPRINT_2026-10-24.md`

## Loop policy

Each iteration is `inspect -> RED -> implement -> GREEN -> review -> evidence`. A failed gate is reduced to the
smallest failing unit; the same command is not retried without a root-cause change. No merge to `main`, publication,
real-profile installation, `Skyrim/Data` write, or human acceptance claim is authorized.

## F0 checkpoints

- [x] F0-00: checkout, branch, worktrees and initial tool discovery recorded.
- [ ] F0-01: exact #21/#22/#23 ancestry/delta reconciled on isolated branch.
- [ ] F0-02: one necessary regression reproduced RED, fixed GREEN, then full C++/.NET suites.
- [ ] F0-03: release ZIP built twice and reproducibility verified.
- [ ] F0-04: isolated MO2/Skyrim kits prepared; human-only evidence remains pending.
- [ ] F0-05: merge proposal prepared; no merge without direct approval.

## Current iteration

1. Audit branch/PR ancestry without fetching.
2. Audit F0 code and tests to select the first real defect.
3. Validate local developer toolchain through `scripts/dev-env.ps1`.
4. Execute TDD on the selected defect.

## Evidence ledger

| Iteration | Change | RED | GREEN | Status |
|---|---|---|---|---|
| 0 | Preflight and loop initialization | N/A | N/A | `EN_DESARROLLO` |
