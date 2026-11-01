#!/usr/bin/env python3
"""Build the PDF deliverables in docs/final/ from their Markdown sources.

A small Markdown-to-HTML converter (headings, paragraphs, lists, tables, code,
block quotes, images, links, emphasis -- what these documents use) and Chrome's
headless printer. Standard library only, plus Google Chrome.

  SafeTrail_Project_Report.pdf       PROJECT_REPORT.md
  SafeTrail_Technical_Reference.pdf  technical documentation, architecture,
                                     structures, complexity, testing, security,
                                     deployment, limitations
  SafeTrail_Viva_Guide.pdf           viva Q&A, syllabus mapping, advanced
                                     concepts, presentation and demo scripts

Usage: python3 tools/build_pdfs.py
"""
import html
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FINAL = os.path.join(ROOT, "docs", "final")
REPO_BLOB = "https://github.com/adivishall/safetrail/blob/main/"
CHROME = os.environ.get("CHROME", "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")

BOOKS = {
    "SafeTrail_Project_Report.pdf": ("SafeTrail — Project Report", ["PROJECT_REPORT.md"]),
    "SafeTrail_Technical_Reference.pdf": ("SafeTrail — Technical Reference", [
        "COMPLETE_TECHNICAL_DOCUMENTATION.md", "ARCHITECTURE.md", "DATA_STRUCTURES_EXPLAINED.md",
        "ALGORITHM_COMPLEXITY.md", "TESTING_AND_BENCHMARKS.md", "SECURITY_AND_PRIVACY.md",
        "DEPLOYMENT_GUIDE.md", "LIMITATIONS_AND_FUTURE_SCOPE.md"]),
    "SafeTrail_Viva_Guide.pdf": ("SafeTrail — Viva Preparation Guide", [
        "VIVA_QUESTIONS_AND_ANSWERS.md", "SYLLABUS_MAPPING.md", "ADVANCED_CONCEPTS.md",
        "PRESENTATION_SCRIPT.md", "LIVE_DEMO_SCRIPT.md"]),
}

# Page numbers come from CSS page-margin boxes (Chrome 131+); %(title)s is filled
# in per book. The cover has none.
#
# Wrapping: a single unbreakable cell or code token wider than the page made
# Chrome shrink the WHOLE document to fit -- the viva guide printed its body at
# 7.3 pt and its tables at 6.2 pt. overflow-wrap: anywhere lets tables and code
# break long tokens instead, so the type prints at the size set here. Only code
# and links break anywhere: on table cells it let narrow columns split plain
# words ("Spatia l index es").
CSS = """
@page { size: A4; margin: 18mm 16mm 20mm 16mm;
  @bottom-left { content: "%(title)s"; font: 8pt -apple-system, "Helvetica Neue", Arial, sans-serif; color: #6f7b89; }
  @bottom-right { content: "Page " counter(page) " of " counter(pages); font: 8pt -apple-system, "Helvetica Neue", Arial, sans-serif; color: #6f7b89; }
}
@page :first { @bottom-left { content: none; } @bottom-right { content: none; } }
body { font: 10.5pt/1.5 -apple-system, "Helvetica Neue", Arial, sans-serif; color: #1b2430; }
h1 { font-size: 20pt; margin: 0 0 6pt; color: #0d1116; page-break-before: always; }
h1:first-of-type { page-break-before: avoid; }
h2 { font-size: 14pt; margin: 18pt 0 6pt; color: #0d1116; border-bottom: 1px solid #d6dbe1; padding-bottom: 3pt; }
h3 { font-size: 11.5pt; margin: 12pt 0 4pt; }
h2, h3 { break-after: avoid; page-break-after: avoid; }
p, li { orphans: 3; widows: 3; }
code { font: 9pt ui-monospace, Menlo, monospace; background: #f1f3f5; padding: 0 3px; border-radius: 3px; overflow-wrap: anywhere; }
pre { background: #f4f6f8; border: 1px solid #e1e5ea; padding: 8pt; border-radius: 4px; font-size: 8.5pt; white-space: pre-wrap; overflow-wrap: anywhere; page-break-inside: avoid; }
pre code { background: none; padding: 0; }
table { border-collapse: collapse; width: 100%; margin: 6pt 0 10pt; font-size: 9pt; page-break-inside: auto; }
tr { page-break-inside: avoid; }
th { text-align: left; background: #eef1f4; }
th, td { border: 1px solid #d6dbe1; padding: 3pt 5pt; vertical-align: top; overflow-wrap: break-word; }
img { max-width: 100%; display: block; margin: 8pt auto; border-radius: 4px; page-break-inside: avoid; }
blockquote { border-left: 3px solid #3fb6a8; margin: 8pt 0; padding: 2pt 10pt; color: #44505e; }
a { color: #1b6f66; text-decoration: none; overflow-wrap: anywhere; }
hr { border: 0; border-top: 1px solid #d6dbe1; margin: 12pt 0; }
.cover { height: 240mm; display: flex; flex-direction: column; justify-content: center; page-break-after: always; }
.cover h1 { font-size: 30pt; page-break-before: avoid; }
.cover p { font-size: 12pt; color: #44505e; }
.mermaid-note { font-style: italic; color: #6f7b89; }
"""


def href(target):
    """A link in a PDF cannot open ../RESULTS.md, so a relative link becomes
    the file's page in the repository; external links and anchors stay."""
    if re.match(r"^[a-z]+:", target) or target.startswith("#"):
        return target
    path, _, frag = target.partition("#")
    full = os.path.normpath(os.path.join("docs", "final", path))
    return REPO_BLOB + full + ("#" + frag if frag else "")


def inline(t):
    t = html.escape(t, quote=False)
    t = re.sub(r"`([^`]+)`", lambda m: "<code>%s</code>" % m.group(1), t)
    t = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", r'<img alt="\1" src="\2">', t)
    t = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", lambda m: '<a href="%s">%s</a>' % (href(m.group(2)), m.group(1)), t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"(?<![\w*])\*([^*\s][^*]*)\*(?![\w*])", r"<em>\1</em>", t)
    return t


def convert(md, base_dir):
    out, i = [], 0
    lines = md.split("\n")

    def fix_src(m):
        src = m.group(1)
        if not re.match(r"^[a-z]+:", src):
            src = "file://" + os.path.abspath(os.path.join(base_dir, src))
        return 'src="%s"' % src
    while i < len(lines):
        ln = lines[i]
        if ln.startswith("```"):
            lang = ln[3:].strip()
            j = i + 1
            block = []
            while j < len(lines) and not lines[j].startswith("```"):
                block.append(lines[j]); j += 1
            if lang == "mermaid":
                out.append('<p class="mermaid-note">(Diagram: rendered on GitHub; see the figures in this document.)</p>')
            else:
                out.append("<pre><code>%s</code></pre>" % html.escape("\n".join(block)))
            i = j + 1
            continue
        if re.match(r"^#{1,6} ", ln):
            n = len(ln) - len(ln.lstrip("#"))
            out.append("<h%d>%s</h%d>" % (n, inline(ln[n + 1:]), n)); i += 1; continue
        if ln.strip() == "---":
            out.append("<hr>"); i += 1; continue
        if ln.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append(lines[i]); i += 1
            # Split on unescaped pipes only: `a\|b` inside a cell is one cell.
            cells = lambda r: [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", r.strip().strip("|"))]
            t = ["<table><thead><tr>%s</tr></thead><tbody>" % "".join("<th>%s</th>" % inline(c) for c in cells(rows[0]))]
            for r in rows[2:]:
                t.append("<tr>%s</tr>" % "".join("<td>%s</td>" % inline(c) for c in cells(r)))
            out.append("".join(t) + "</tbody></table>")
            continue
        if ln.startswith(">"):
            block = []
            while i < len(lines) and lines[i].startswith(">"):
                block.append(lines[i][1:].strip()); i += 1
            out.append("<blockquote>%s</blockquote>" % inline(" ".join(block)))
            continue
        m = re.match(r"^(\s*)([-*]|\d+\.) ", ln)
        if m:
            ordered = m.group(2)[0].isdigit()
            items = []
            while i < len(lines) and re.match(r"^\s*([-*]|\d+\.) ", lines[i]):
                item = re.sub(r"^\s*([-*]|\d+\.) ", "", lines[i]); i += 1
                while i < len(lines) and lines[i].startswith("  ") and lines[i].strip() and not re.match(r"^\s*([-*]|\d+\.) ", lines[i]):
                    item += " " + lines[i].strip(); i += 1
                items.append("<li>%s</li>" % inline(item))
            tag = "ol" if ordered else "ul"
            out.append("<%s>%s</%s>" % (tag, "".join(items), tag))
            continue
        if not ln.strip():
            i += 1; continue
        para = [ln]
        i += 1
        while i < len(lines) and lines[i].strip() and not re.match(r"^(#|\||```|>|\s*([-*]|\d+\.) |---)", lines[i]):
            para.append(lines[i]); i += 1
        out.append("<p>%s</p>" % inline(" ".join(p.strip() for p in para)))
    body = "\n".join(out)
    return re.sub(r'src="([^"]+)"', fix_src, body)


def main():
    if not os.path.exists(CHROME):
        print("needs Google Chrome (set CHROME=/path/to/chrome)"); return 2
    tmp = tempfile.mkdtemp()
    for pdf, (title, sources) in BOOKS.items():
        parts = ['<div class="cover"><h1>%s</h1><p>Data Structures course project · github.com/adivishall/safetrail</p>'
                 '<p>Generated from docs/final/ by tools/build_pdfs.py</p></div>' % html.escape(title)]
        for src in sources:
            with open(os.path.join(FINAL, src), encoding="utf-8") as f:
                parts.append(convert(f.read(), FINAL))
        page = "<!doctype html><html><head><meta charset='utf-8'><title>%s</title><style>%s</style></head><body>%s</body></html>" % (
            html.escape(title), CSS.replace("%(title)s", title.replace('"', "'")), "\n".join(parts))
        hp = os.path.join(tmp, pdf.replace(".pdf", ".html"))
        with open(hp, "w", encoding="utf-8") as f:
            f.write(page)
        out = os.path.join(FINAL, pdf)
        subprocess.run([CHROME, "--headless=new", "--no-pdf-header-footer", "--generate-pdf-document-outline",
                        "--allow-file-access-from-files",
                        "--print-to-pdf=" + out, "file://" + hp], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("  %s (%d KB)" % (pdf, os.path.getsize(out) // 1024))
    return 0


if __name__ == "__main__":
    sys.exit(main())
