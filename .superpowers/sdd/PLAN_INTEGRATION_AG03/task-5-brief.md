# Task 5 Brief: Full QA Gate & Evidence Audit

## Goal
Execute the end-to-end regression gate across all 17 test suites, audit cryptographic evidence, and update the repository coordination records.

## Execution Requirements
1. Execute `scripts/antigravity/Invoke-SnsRegression.ps1 -Suite full-qa-gate`.
2. Verify:
   - 17/17 test suites PASS.
   - `HasDifferences = false`.
3. Audit evidence manifest using `scripts/antigravity/Test-SnsEvidence.ps1 -ManifestPath <manifest.json>`.
4. Update `docs/antigravity/follower-behavior-lab/CHECKPOINT.md` with AG-03 integration milestone.
5. Update `.ai-coordination/status/antigravity.json` and `.ai-coordination/messages/antigravity-to-claude.md`.
