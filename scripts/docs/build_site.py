#!/usr/bin/env python3
"""Builds the Verified Twin Studio documentation website (static HTML) from docs/studio/*.md.

The Markdown manual stays the single source; this renders it as a navigable site with a
sidebar, full-text search, per-page table of contents, previous/next links, a screenshot tour
and click-to-enlarge screenshots. The output is self-contained (relative links only), so it
works when served by twin-studio at /docs/ and when opened straight from disk.

  python3 scripts/docs/build_site.py                         # -> web/studio/dist/docs
  python3 scripts/docs/build_site.py --out build/docs-site   # anywhere else

Requires Python-Markdown (`pip install markdown`).
"""
import argparse
import html
import json
import pathlib
import re
import shutil
import sys

try:
    import markdown
except ImportError:  # pragma: no cover
    sys.exit("build_site.py needs Python-Markdown: pip install markdown")

ROOT = pathlib.Path(__file__).resolve().parents[2]
DOCS = ROOT / "docs"
SHOTS = DOCS / "screenshots"

# (source, output page, sidebar title, section). Order = reading order (prev/next).
PAGES = [
    ("studio/README.md", "index.html", "Overview", "Start here"),
    ("studio/getting-started.md", "getting-started.html", "Getting started", "Start here"),
    (None, "tour.html", "Screenshot tour", "Start here"),
    ("studio/operations.md", "operations.html", "Operations", "Using Studio"),
    ("studio/audit-and-replay.md", "audit-and-replay.html", "Audit, provenance & replay", "Using Studio"),
    ("studio/ontology-management.md", "ontology-management.html", "Ontology management", "Engineering"),
    ("studio/refinement.md", "refinement.html", "Refinement checking", "Engineering"),
    ("studio/impact-and-staleness.md", "impact-and-staleness.html", "Impact & staleness", "Engineering"),
    ("studio/release-and-deployment.md", "release-and-deployment.html", "Releases & deployment", "Engineering"),
    ("studio/tutorial-evolving-an-ontology.md", "tutorial-evolving-an-ontology.html", "Tutorial: evolving an ontology", "Tutorial"),
    ("studio/plugins.md", "plugins.html", "Domain plugins", "Developers"),
    ("studio/developer.md", "developer.html", "Developer guide", "Developers"),
    ("runtime-api.md", "runtime-api.html", "Runtime API", "Reference"),
    ("studio/aligner-findings.md", "aligner-findings.html", "Aligner findings", "Reference"),
]

# Screenshot tour: (file under docs/screenshots, title, caption, page to read more).
TOUR = [
    ("01-overview.png", "Overview", "The estate at a glance: assets by state, telemetry freshness, every twin with its evidence-backed trust badges, open engineering changes and recent events.", "getting-started.html"),
    ("02-asset-knowledge-graph.png", "Knowledge graph", "Asset instances and operational relationships, explored around a focus asset with depth, filters and neighbourhood expansion.", "operations.html#assets-and-the-knowledge-graph"),
    ("03-drone-live-navigation.png", "Drone: live navigation", "The drone plugin: physical world vs. what the twin knows, the active route and the kernel's mode.", "operations.html#domain-views"),
    ("04-drone-obstacle-discovered.png", "Drone: obstacle discovered", "A closed fire door is discovered; the twin's knowledge changes and the route becomes invalid.", "operations.html#domain-views"),
    ("05-drone-replanning.png", "Drone: replanning", "Planner candidates with the kernel's admissibility verdicts; the selected plan and the invalidated one.", "operations.html#prediction-what-if-and-planning"),
    ("06-behavioral-graph.png", "Behavioural graph", "The DT model from the kernel: current state, enabled and recently taken transitions, with details on click.", "operations.html#behaviour"),
    ("07-prediction-plans.png", "Prediction & planning", "Bounded future states explored by the kernel; every branch is admissible by the model.", "operations.html#prediction-what-if-and-planning"),
    ("08-ledger.png", "Execution ledger", "The tamper-evident ledger of an execution, with chain verification by the runtime.", "audit-and-replay.html#execution-ledger"),
    ("09-replay.png", "Replay", "A recorded execution re-executed with the exact historical package, synchronised across record, telemetry, graph and plugin.", "audit-and-replay.html#replay"),
    ("10-engineering-assurance.png", "Engineering assurance", "Which model, ontology, interpretation and package each twin runs, with alignment, refinement and integrity evidence.", "release-and-deployment.html#verified-packages"),
    ("11-ontology-editor.png", "Ontology editor", "The formal ontology with highlighting, located diagnostics, structure, symbols and dependencies.", "ontology-management.html#the-editor"),
    ("12-ontology-refinement.png", "Refinement result", "Definition 4 checked by Z3: conditions, obligations, counter-models, assumptions and checker identity.", "refinement.html#understanding-the-four-results"),
    ("13-pump-example.png", "Second example: industrial pump", "The same generic screens for a completely different twin, with no plugin and no custom UI.", "operations.html#domain-views"),
    ("tutorial/01-published-ontology.png", "Tutorial 1 · Published ontology", "The deployed version is immutable; evolution starts from a draft.", "tutorial-evolving-an-ontology.html#1-open-the-published-ontology"),
    ("tutorial/03-edit-diagnostics.png", "Tutorial 3 · Diagnostics", "Saving runs the strict parser; mistakes are located exactly.", "tutorial-evolving-an-ontology.html#3-edit-save-and-read-the-diagnostics"),
    ("tutorial/05-compare.png", "Tutorial 5 · Semantic diff", "What changed, and which interpretation entries depend on it.", "tutorial-evolving-an-ontology.html#5-compare-with-the-deployed-version"),
    ("tutorial/06-refinement-result.png", "Tutorial 6 · Valid refinement", "All 43 obligations discharged: the new ontology refines the deployed one.", "tutorial-evolving-an-ontology.html#6-run-the-refinement-check"),
    ("tutorial/07-impact.png", "Tutorial 7 · Impact", "Interpretations preserved, alignment preserved by Theorem 3, IR unaffected.", "tutorial-evolving-an-ontology.html#7-inspect-the-impact"),
    ("tutorial/08-pipeline.png", "Tutorial 8 · Release pipeline", "Every stage computed from stored evidence for the exact candidate artefacts.", "tutorial-evolving-an-ontology.html#8-resolve-the-required-verification-and-build-the-package"),
    ("tutorial/09-deployments.png", "Tutorial 9 · Deployments", "Append-only deployment history; rollback is always possible.", "tutorial-evolving-an-ontology.html#9-release-and-deploy"),
    ("tutorial/10-old-execution-replay.png", "Tutorial 10 · Historical replay", "An old execution still replays with the ontology it used at the time.", "tutorial-evolving-an-ontology.html#10-old-executions-keep-their-semantics"),
]

PAGE_OF = {src: out for src, out, _, _ in PAGES if src}
LINKED: set[str] = set()  # repository files linked from the manual, copied under source/


def rewrite_links(body: str, src: str) -> str:
    """Markdown-relative links and images -> site-relative ones."""
    src_dir = (DOCS / src).parent

    def fix(url: str) -> str:
        if re.match(r"^[a-z]+:|^#|^/", url):
            return url
        path, _, frag = url.partition("#")
        target = (src_dir / path).resolve()
        try:
            rel = target.relative_to(DOCS.resolve())
        except ValueError:
            rel = None
        if rel is not None:
            rel_s = rel.as_posix()
            if rel_s in PAGE_OF:
                return PAGE_OF[rel_s] + (f"#{frag}" if frag else "")
            if rel_s.startswith("screenshots/"):
                return rel_s
        try:  # api/*.yaml and other repository files are copied under source/
            rel_repo = target.relative_to(ROOT.resolve()).as_posix()
        except ValueError:
            return url
        if not target.is_file():
            print(f"warning: {src} links to missing {rel_repo}", file=sys.stderr)
            return url
        LINKED.add(rel_repo)
        return f"source/{rel_repo}" + (f"#{frag}" if frag else "")

    body = re.sub(r'(href|src)="([^"]+)"', lambda m: f'{m.group(1)}="{html.escape(fix(html.unescape(m.group(2))), quote=True)}"', body)
    return body


def enhance(body: str) -> str:
    """Screenshots become figures that open in the viewer; tables scroll on small screens."""
    def figure(alt: str, src: str) -> str:
        return (f'<figure class="shot"><a href="{src}" data-viewer data-caption="{alt}">'
                f'<img src="{src}" alt="{alt}" loading="lazy"></a><figcaption>{alt}</figcaption></figure>')

    img = r'<img alt="([^"]*)" src="([^"]+)" ?/?>'
    # Paragraphs that contain only screenshots (one or several) become figures.
    body = re.sub(r"<p>((?:\s*" + img + r"\s*)+)</p>",
                  lambda m: "".join(figure(a, s) for a, s in re.findall(img, m.group(1))), body)
    body = body.replace("<table>", '<div class="table-wrap"><table>').replace("</table>", "</table></div>")
    body = re.sub(r"<blockquote>", '<blockquote class="note">', body)
    return body


LIST_ITEM = re.compile(r"^(\s*)([-*+]|\d+\.)\s+")


def normalise(md_text: str) -> str:
    """GitHub-flavoured list layout -> what Python-Markdown expects.

    The manual is written for GitHub: lists may follow a paragraph line directly and nested
    items are indented by two spaces. Python-Markdown needs a blank line before a list and
    four-space nesting, so both are added here (outside code fences)."""
    out, fence, in_list = [], False, False
    for line in md_text.split("\n"):
        if line.lstrip().startswith("```"):
            fence = not fence
            out.append(line)
            continue
        if fence:
            out.append(line)
            continue
        item = LIST_ITEM.match(line)
        if item and not in_list and out and out[-1].strip() and not out[-1].lstrip().startswith("|"):
            out.append("")
        if item:
            in_list = True
        elif not line.strip():
            pass
        elif not line.startswith(" "):
            in_list = False
        if in_list and line.startswith(" "):
            indent = len(line) - len(line.lstrip(" "))
            line = " " * (indent * 2) + line.lstrip(" ")
        out.append(line)
    return "\n".join(out)


def render(md_text: str):
    md_text = normalise(md_text)
    md = markdown.Markdown(extensions=["tables", "fenced_code", "toc", "attr_list", "sane_lists"],
                           extension_configs={"toc": {"permalink": "#", "permalink_title": "Link to this section", "toc_depth": "2-3"}})
    body = md.convert(md_text)
    return body, md.toc_tokens


def plain(s: str) -> str:
    # Markdown output may escape entities inside code ("&amp;#39;"), so unescape until stable.
    text = re.sub(r"<[^>]+>", " ", s)
    while (once := html.unescape(text)) != text:
        text = once
    return text


def search_entries(body: str, page: str, page_title: str):
    """One entry per section (h1/h2/h3) with its text."""
    parts = re.split(r'(<h[123] id="[^"]+">.*?</h[123]>)', body, flags=re.S)
    entries, current = [], {"t": page_title, "u": page, "x": ""}
    for part in parts:
        h = re.match(r'<h[123] id="([^"]+)">(.*?)</h[123]>', part, flags=re.S)
        if h:
            if current["x"].strip() or current["t"]:
                entries.append(current)
            title = plain(re.sub(r'<a class="headerlink".*?</a>', "", h.group(2))).strip()
            current = {"t": title, "p": page_title, "u": f"{page}#{h.group(1)}", "x": ""}
        else:
            current["x"] += " " + plain(part)
    entries.append(current)
    for e in entries:
        e["x"] = re.sub(r"\s+", " ", e["x"]).strip()[:2000]
    return [e for e in entries if e["t"] or e["x"]]


def toc_html(tokens) -> str:
    items = []
    for t in tokens:
        for h in ([t] if t["level"] >= 2 else []) + [c for c in t.get("children", []) if t["level"] == 1]:
            items.append(f'<li class="l{h["level"]}"><a href="#{h["id"]}">{h["name"]}</a></li>')
            for c in h.get("children", []):
                items.append(f'<li class="l{c["level"]}"><a href="#{c["id"]}">{c["name"]}</a></li>')
    if len(items) < 2:
        return ""
    return '<nav class="toc" aria-label="On this page"><div class="toc-title">On this page</div><ul>' + "".join(items) + "</ul></nav>"


def sidebar(current: str) -> str:
    out, section = [], None
    for _, page, title, sec in PAGES:
        if sec != section:
            if section is not None:
                out.append("</ul>")
            out.append(f'<div class="nav-section">{sec}</div><ul>')
            section = sec
        cls = ' class="active" aria-current="page"' if page == current else ""
        out.append(f'<li><a href="{page}"{cls}>{title}</a></li>')
    out.append("</ul>")
    return "".join(out)


def tour_body() -> str:
    cards = []
    for i, (file, title, caption, more) in enumerate(TOUR):
        src = f"screenshots/{file}"
        cards.append(
            f'<article class="tour-card" id="shot-{i + 1}"><a class="tour-img" href="{src}" data-viewer data-caption="{html.escape(title)}: {html.escape(caption)}">'
            f'<img src="{src}" alt="{html.escape(title)}" loading="lazy"></a>'
            f'<div class="tour-text"><h3>{html.escape(title)}</h3><p>{html.escape(caption)}</p>'
            f'<a class="more" href="{more}">Read more →</a></div></article>')
    return ('<h1 id="screenshot-tour">Screenshot tour</h1>'
            '<p class="lead">Every picture is taken from the running product (no mocks) by '
            '<code>scripts/capture-screenshots.sh</code>. Click a screenshot to enlarge it; use ← → to move through them.</p>'
            '<div class="tour-grid">' + "".join(cards) + "</div>")


TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title} · Verified Twin Studio docs</title>
<link rel="icon" href="assets/favicon.svg" type="image/svg+xml">
<link rel="stylesheet" href="assets/site.css">
<script>try{{var t=localStorage.getItem('vts-docs-theme');if(t)document.documentElement.dataset.theme=t;}}catch(e){{}}</script>
</head>
<body>
<a class="skip" href="#content">Skip to content</a>
<header class="topbar">
  <button class="menu" aria-label="Open navigation" aria-expanded="false" aria-controls="sidebar">☰</button>
  <a class="brand" href="index.html"><img src="assets/favicon.svg" alt="" width="22" height="22"> Verified Twin Studio <span>docs</span></a>
  <div class="search">
    <input id="search" type="search" placeholder="Search the docs…  ( / )" aria-label="Search the documentation" autocomplete="off">
    <ul id="results" role="listbox" hidden></ul>
  </div>
  <a class="app-link" href="/" title="Open Verified Twin Studio (when served by twin-studio)">Open Studio ↗</a>
  <button class="theme" aria-label="Toggle dark mode" title="Toggle dark mode">◐</button>
</header>
<div class="layout">
  <aside id="sidebar" class="sidebar"><nav aria-label="Documentation">{sidebar}</nav></aside>
  <main id="content" class="content">
    <article class="doc">{body}</article>
    <nav class="pager" aria-label="Previous and next page">{pager}</nav>
    <footer class="foot">Generated from <code>{source}</code> by <code>scripts/docs/build_site.py</code>.</footer>
  </main>
  {toc}
</div>
<div class="viewer" hidden role="dialog" aria-modal="true" aria-label="Screenshot viewer">
  <button class="v-close" aria-label="Close">✕</button>
  <button class="v-prev" aria-label="Previous screenshot">‹</button>
  <figure><img alt=""><figcaption></figcaption></figure>
  <button class="v-next" aria-label="Next screenshot">›</button>
</div>
<script src="assets/search-index.js"></script>
<script src="assets/site.js"></script>
</body>
</html>
"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=str(ROOT / "web" / "studio" / "dist" / "docs"))
    out = pathlib.Path(ap.parse_args().out).resolve()
    if out.exists():
        shutil.rmtree(out)
    (out / "assets").mkdir(parents=True)
    shutil.copytree(SHOTS, out / "screenshots")
    assets = pathlib.Path(__file__).with_name("site_assets")
    for f in assets.iterdir():
        shutil.copy2(f, out / "assets" / f.name)
    fav = ROOT / "web" / "studio" / "public" / "favicon.svg"
    if fav.exists():
        shutil.copy2(fav, out / "assets" / "favicon.svg")

    index = []
    for i, (src, page, title, _) in enumerate(PAGES):
        if src:
            body, tokens = render((DOCS / src).read_text(encoding="utf-8"))
            body = enhance(rewrite_links(body, src))
            toc = toc_html(tokens)
            source = f"docs/{src}"
        else:
            body, toc, source = tour_body(), "", "scripts/docs/build_site.py (TOUR)"
        if page == "index.html":
            body = body.replace("</h1>", "</h1>\n" + '<p class="lead">The operations and engineering console for verified digital twins. '
                                '<a href="getting-started.html">Get started</a> · <a href="tour.html">Screenshot tour</a> · '
                                '<a href="tutorial-evolving-an-ontology.html">Tutorial</a></p>', 1)
        prev_ = PAGES[i - 1] if i > 0 else None
        next_ = PAGES[i + 1] if i + 1 < len(PAGES) else None
        pager = (f'<a class="prev" href="{prev_[1]}"><span>Previous</span>{prev_[2]}</a>' if prev_ else "<span></span>") + \
                (f'<a class="next" href="{next_[1]}"><span>Next</span>{next_[2]}</a>' if next_ else "<span></span>")
        (out / page).write_text(TEMPLATE.format(title=html.escape(title), sidebar=sidebar(page), body=body, pager=pager,
                                                toc=toc, source=html.escape(source)), encoding="utf-8")
        index += search_entries(body, page, title)

    for rel in sorted(LINKED):
        (out / "source" / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / rel, out / "source" / rel)
    (out / "assets" / "search-index.js").write_text(
        "window.VTS_SEARCH=" + json.dumps(index, ensure_ascii=False, separators=(",", ":")) + ";\n", encoding="utf-8")
    print(f"documentation site: {len(PAGES)} pages, {len(index)} search entries -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
