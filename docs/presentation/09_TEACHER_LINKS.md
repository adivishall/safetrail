# SafeTrail — links for the teacher

All links checked on 11 October 2026, after the deploy of `main` at `9f107fe`;
the two dashboard links again on 23 October 2026, after `main` reached `28f0e47`.
The Vercel mirror was added on 20 October 2026 and re-checked on 23 October: it
serves the same file as the Pages dashboard, byte for byte.
The live deck and dashboard were compared byte for byte with the committed files
and show the committed benchmark results.

| What | Link |
|---|---|
| Repository | https://github.com/adivishall/safetrail |
| Live review deck | https://adivishall.github.io/safetrail/slides.html |
| Live dashboard | https://adivishall.github.io/safetrail/ |
| Live dashboard, Vercel mirror (same file) | https://safetrail-adivishall.vercel.app |
| Slides as a PDF | https://github.com/adivishall/safetrail/blob/main/docs/presentation/SafeTrail_review_slides.pdf |
| Benchmark results | https://github.com/adivishall/safetrail/blob/main/docs/RESULTS.md |

Every push to `main` redeploys the site. Before sending a live link, open slide 8
of the live deck and check that it shows 239 µs. If it shows anything else, send
the PDF instead.

## Offline backup

Bring these two files; both open in any browser without a network:

- [SafeTrail_review_slides.pdf](SafeTrail_review_slides.pdf) (in this folder)
- `dashboard.html`, freshly generated from the repository root:

```bash
make dashboard
```

## On the day

From the repository root:

```bash
make review PAUSE=1
```

Press Enter once per step. The full runbook is [02_LIVE_DEMO_RUNBOOK.md](02_LIVE_DEMO_RUNBOOK.md),
and every other link is in [05_LINKS_AND_RESOURCES.md](05_LINKS_AND_RESOURCES.md).
