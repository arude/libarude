---
name: libarude-review
description: Review changed C++ against libarude's licensing, formatting and C++ conventions. Use before committing, when reviewing a branch or diff, or when asked to check compliance or review code in this repository.
---

# libarude review

Two passes: compliance, which is mechanical and pass/fail, then C++ review,
which is judgement.

## Before starting

Read the rules rather than restating them from memory — they change, and this
file deliberately does not duplicate them:

- `CLAUDE.md` — header convention and licensing constraints
- `support/template_file_header.hpp` — canonical file header
- `.clang-format` — formatting policy, documented inline

Determine the review set:

```sh
git diff --name-only --diff-filter=ACMR master...HEAD
```

For uncommitted work use `git status --porcelain` instead. Review only these
files unless asked otherwise.

## Pass 1 — compliance

Each check is pass/fail. Report every failure with file and line.

**Formatting.** Must be clean:

```sh
clang-format --dry-run --Werror <changed sources>
```

`.clang-format` requires clang-format 21 or newer. If the local binary is
older, say so and skip this check rather than reporting the diff — older
versions ignore options they do not know silently, so their output is
misleading rather than merely incomplete.

**Licence header.** Every added or modified `.hpp`/`.cpp` must contain:

```
SPDX-License-Identifier: AGPL-3.0-or-later
```

Match the identifier only. The year and surrounding wording may drift.

**Licensing integrity.** These quietly destroy the dual-licensing arrangement,
so treat them as hard failures rather than observations:

- A new MIT licence file anywhere in the tree. MIT is granted per licensee by
  private arrangement; a committed file reads as a standing grant to everyone.
- Weakened or removed contribution terms in `README.md`. Those terms are what
  keeps relicensing possible after a pull request is merged.
- Any modification to `LICENSE` itself.

## Pass 2 — C++ review

Review against `docs/cpp-conventions.md`. That file is the single source of
truth for how libarude code should be written; it is not reproduced here,
because a second copy would drift from it and this one would lose.

Weight findings by blast radius. libarude is header-mostly, so a flaw in a
public interface reaches every consumer and is hard to withdraw once released,
while the same flaw inside a `detail` helper is cheap to fix. Report the
former even when minor; apply judgement to the latter.

Beyond the conventions, look for what a checklist cannot encode: does the
interface do what its name claims, is the ownership model obvious to a caller,
would a reasonable use of this API be a trap?

## Reporting

Order by severity: compliance failures, then correctness, then design, then
style. For each give `file:line`, what is wrong, and the concrete consequence.

Do not report:

- anything `.clang-format` already governs — that is mechanical, and noise here
- preferences with no failing case behind them

If nothing is wrong, say so plainly rather than manufacturing findings.
