# Final project deliverables

Everything for the final review, generated from the repository's own code,
tests and committed benchmark results.

## Start here

| If you want to… | Open |
|---|---|
| Run the system | `make app`, then http://127.0.0.1:8080 ([COMPLETE_TECHNICAL_DOCUMENTATION.md](COMPLETE_TECHNICAL_DOCUMENTATION.md) §2, [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md)) |
| Present it | [SafeTrail_final_presentation.pptx](SafeTrail_final_presentation.pptx) with [PRESENTATION_SCRIPT.md](PRESENTATION_SCRIPT.md), then [LIVE_DEMO_SCRIPT.md](LIVE_DEMO_SCRIPT.md) |
| Read the report | [SafeTrail_Project_Report.pdf](SafeTrail_Project_Report.pdf) (source: [PROJECT_REPORT.md](PROJECT_REPORT.md)) |
| Prepare for the viva | [SafeTrail_Viva_Guide.pdf](SafeTrail_Viva_Guide.pdf): [VIVA_QUESTIONS_AND_ANSWERS.md](VIVA_QUESTIONS_AND_ANSWERS.md), [SYLLABUS_MAPPING.md](SYLLABUS_MAPPING.md), [ADVANCED_CONCEPTS.md](ADVANCED_CONCEPTS.md) |
| Look things up | [SafeTrail_Technical_Reference.pdf](SafeTrail_Technical_Reference.pdf) |

## All documents

| Document | Contents |
|---|---|
| [AUDIT.md](AUDIT.md) | the phase-zero audit: what existed, what was missing, the roadmap |
| [PROJECT_REPORT.md](PROJECT_REPORT.md) | the academic report, 20 sections |
| [COMPLETE_TECHNICAL_DOCUMENTATION.md](COMPLETE_TECHNICAL_DOCUMENTATION.md) | setup on a Mac, every command, configuration, the full API, data formats, troubleshooting |
| [ARCHITECTURE.md](ARCHITECTURE.md) | layers, data flow, life cycles, offline sync, concurrency (diagrams) |
| [DATA_STRUCTURES_EXPLAINED.md](DATA_STRUCTURES_EXPLAINED.md) | each structure: problem, how, implementation, use, cost |
| [ALGORITHM_COMPLEXITY.md](ALGORITHM_COMPLEXITY.md) | bounds (guaranteed vs expected), trade-offs, why theory and timings differ |
| [SYLLABUS_MAPPING.md](SYLLABUS_MAPPING.md) | syllabus concept → file → use → complexity → Lab demo; what is NOT implemented |
| [ADVANCED_CONCEPTS.md](ADVANCED_CONCEPTS.md) | concepts beyond the syllabus that the code really uses |
| [TESTING_AND_BENCHMARKS.md](TESTING_AND_BENCHMARKS.md) | exact test and benchmark results, defects found, how to reproduce |
| [SECURITY_AND_PRIVACY.md](SECURITY_AND_PRIVACY.md) | threat model, controls, how each is tested, open items |
| [LIMITATIONS_AND_FUTURE_SCOPE.md](LIMITATIONS_AND_FUTURE_SCOPE.md) | what the project does not claim, and next steps |
| [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) | what is hosted (the static dashboard) and what is not (the server); running the full application locally; what a free public backend would need |
| [VIVA_QUESTIONS_AND_ANSWERS.md](VIVA_QUESTIONS_AND_ANSWERS.md) | 32 questions with answers and where to show the evidence |
| [PRESENTATION_SCRIPT.md](PRESENTATION_SCRIPT.md) | what to say on each of the 14 slides |
| [LIVE_DEMO_SCRIPT.md](LIVE_DEMO_SCRIPT.md) | exact demo steps, expected results, fallbacks, clean-start check |
| [INDIVIDUAL_CONTRIBUTION.md](INDIVIDUAL_CONTRIBUTION.md) | contribution record (personal fields to fill in) |

## Figures

`images/` holds the architecture and data-flow diagrams, benchmark charts and
screenshots of the running system. Regenerate them with:

```bash
python3 tools/make_figures.py
```

```bash
python3 tools/app_screenshots.py
```

The PDFs and the deck:

```bash
python3 tools/build_pdfs.py
```

```bash
node tools/make_deck.js
```

(`make_deck.js` needs `npm install pptxgenjs@3.12.0` and the PPTX skill's
`apply_theme.js` on `APPLY_THEME`; the committed `.pptx` is the built result.)
