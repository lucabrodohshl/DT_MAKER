# 8. Design system — "drafting table"

## 8.1 The aesthetic, in one sentence

**Ink on paper, ruled, quiet: an engineering instrument, not a dashboard.** Content is near-black
on an off-white ground; regions are separated by hairline rules instead of cards; hierarchy comes
from type weight, size and whitespace; colour is rare and therefore meaningful; identifiers and
values are set in a monospaced face, prose and labels in a grotesk. The reference points are
technical drawings, oscilloscope and CAD interfaces, and Swiss typography — not SaaS dashboards.

What it is not (and what the current UI does): pastel pill badges, gradient or tinted cards,
cards in cards, info banners on every page, uppercase letter-spaced micro-headings, coloured left
borders, icons in every heading, rounded segmented pills, green ticks on every navigation item.

## 8.2 Rules

1. **Rules, not boxes.** A region ends with a 1 px rule. No drop shadow on anything that is part of
   the page; only floating layers (menus, popovers, dialogs) get a border and one soft shadow.
2. **One accent.** Cobalt marks the primary action, the selection and links. It is never used for
   decoration or status.
3. **Status is typographic.** A glyph and a word in the status colour — `● 2 problems`,
   `▲ stale`, `✓ aligned`, `○ not checked`. No tinted backgrounds, no pills. The only boxed label
   is the lifecycle tag (`DRAFT`, `PUBLISHED`): 10 px mono caps, 1 px outline.
4. **Mono means "exact".** Identifiers, values, units, times, formulas, digests are in Plex Mono;
   everything a human reads as language is in Plex Sans.
5. **Sentence case** everywhere; no uppercase headings (table column headers are 11 px, muted,
   sentence case).
6. **Explain once, where it is needed.** No banners. Empty states carry one sentence and the next
   action; deeper explanations sit behind `?`.
7. **Density with air.** 13 px base, 28 px rows, 32 px controls, 4 px grid, 16 px panel padding.
8. **No motion** except 120 ms opacity for overlays; canvas updates are immediate.
9. **Icons are tools.** 16 px Lucide, 1.5 px stroke, in toolbars, trees and menus — never decorating
   headings or status.
10. **Graphs keep their notation** (8.6).

## 8.3 Tokens

```css
/* Light */
--paper: #f5f5f3;        /* application ground */
--sheet: #ffffff;        /* panels, inspector, tables */
--canvas: #fafaf8;       /* modelling canvas, with a dot grid in --rule */
--rule: #e3e3df;         /* hairlines */
--rule-strong: #c9c9c3;  /* control borders, node borders */
--ink: #1b1b19;          /* text: 16.9:1 on --sheet */
--ink-2: #4a4a45;        /* secondary text: 9.0:1 */
--ink-3: #6b6b64;        /* tertiary text, column headers: 5.4:1 */
--ink-4: #9b9b93;        /* placeholders, disabled (never essential text) */
--accent: #2846b0;       /* cobalt: primary, selection, links: 7.6:1 */
--accent-ink: #1f3a94;   /* hover / pressed */
--accent-wash: #eef1fa;  /* selected row / node fill */
--error: #b3261e;        --warn: #94590a;   --ok: #2d6a3e;
--formal: #5b3fa0;       /* evidence, "last taken" */
--sim: #8a5a00;          --sim-wash: #fbf4e6;  /* simulation framing */

/* Dark */
--paper: #141413; --sheet: #1b1b19; --canvas: #171716; --rule: #2c2c29; --rule-strong: #3d3d39;
--ink: #ececE8; --ink-2: #b8b8b0; --ink-3: #8f8f87; --ink-4: #66665f;
--accent: #8fa6f0; --accent-ink: #b2c2f5; --accent-wash: #20263a;
--error: #f08a80; --warn: #e2b062; --ok: #7cc492; --formal: #b9a4f0; --sim: #e7c06a; --sim-wash: #2a2416;

/* Type */
--font-sans: "IBM Plex Sans", "Helvetica Neue", Arial, sans-serif;
--font-mono: "IBM Plex Mono", "SF Mono", Menlo, monospace;
--fs-11: 11px; --fs-12: 12px; --fs-13: 13px; --fs-15: 15px; --fs-18: 18px; --fs-22: 22px;
--lh: 1.45;  weights: 400, 500 (labels), 600 (names, headings)

/* Space, size, shape */
--u: 4px;  --s1: 4px; --s2: 8px; --s3: 12px; --s4: 16px; --s6: 24px; --s8: 32px;
--row: 28px; --control: 32px; --control-sm: 26px; --header: 44px;
--r-control: 3px; --r-node: 4px; --r-state: 12px; --r-float: 6px;
--float-shadow: 0 6px 24px rgb(0 0 0 / 8%), 0 1px 2px rgb(0 0 0 / 6%);
```

## 8.4 Components

| Component | Spec |
|---|---|
| **Button** | 32 px (26 px small), 3 px radius, 13/500. *Primary*: accent fill, white text (one per screen). *Default*: sheet, 1 px rule-strong. *Quiet*: no border, ink-2, hover paper. *Danger*: default with error text. Icon optional, 16 px, left. |
| **Icon button** | 28 × 28, quiet, tooltip with the shortcut. |
| **Text field** | 32 px, 1 px rule-strong, 3 px radius, focus: 1 px accent + 2 px accent-wash ring. Label above (12/500 ink-2), hint below (12 ink-3), error below (12 error). |
| **Expression field** | text field in mono with completion, inline error underline, optional formula preview. |
| **Inline-row editor** | the one-line declaration row of the inspector tables (`name: type unit kind range = value`). |
| **Select / Menu** | sheet, float shadow, 28 px items, keyboard navigation, type-ahead. |
| **Tabs** | text tabs with a 2 px ink underline for the active one; no pills, no backgrounds. Used for views and sub-views. |
| **Tree** | 26 px rows, 12 px indent, chevrons, type in mono-11 muted at the right. |
| **Table** | 28 px rows, hairline row rules, sticky header 11 px ink-3, numbers right-aligned in mono, hover paper. |
| **Inspector section** | title 13/600 + optional count and `+`; content; 1 px rule below. Collapsible. |
| **Status text** | glyph + word in status colour; size inherits. Glyphs: `●` error/fail, `▲` warning/stale, `✓` pass, `○` not checked, `◌` running, `—` not applicable. |
| **Tag** | lifecycle only: `DRAFT` (ink-2 outline), `PUBLISHED` (ink outline), `SIMULATION` (sim). Mono 10, caps, 1 px border, 2 px radius. |
| **Popover / Dialog** | 6 px radius, float shadow, 16 px padding, title 15/600. Dialogs only for publish, delete-with-dependents, create. |
| **Drawer (bottom)** | rule on top, 32 px header with tab labels and counts, resizable. |
| **Empty state** | 13 ink-2 sentence + one default button, centred in the region. |
| **Skeleton** | 1 px rule-coloured bars, no shimmer. |
| **Kbd** | mono 11, 1 px rule, 2 px radius. |
| **Splitter** | 1 px rule, 5 px hit area, col-resize cursor. |

## 8.5 Layout

- App header 44 px: product mark (a small square glyph + "Verified Twin Studio" 13/600), area
  links (Models, Twins, Connections) as plain text with an underline for the current one, search,
  user menu.
- Studio: header 44 px; columns Explorer 240 px | canvas | Inspector 320 px (both resizable,
  collapsible); bottom drawer 32 px collapsed, 240 px open.
- Pages outside the Studio: a single column of max 1200 px, title 22/600, then content. No cards
  around tables.

## 8.6 Graph notation (kept from the current product, formalised)

| Token | Value |
|---|---|
| node fill / border | `--sheet` / 1 px `--rule-strong`; selected: 2 px `--accent` |
| asset node | 4 px radius, 168 × 52 min; name 12/600; type mono 11 `--ink-3` |
| state node | 12 px radius, 150 × 44 min; name 12/600; meaning 11 `--ink-3`; limit chip mono 10.5 |
| edge | 1.25 px `--ink-3`, curved (bezier), arrowhead 6 px; selected 2 px `--accent` |
| label chip | `--sheet`, 1 px `--rule`, 3 px radius, mono 10.5, 2 × 5 px padding |
| containment | dotted 1 px `--rule-strong` |
| link (event → action) | dashed 1 px `--ink-3` |
| canvas | `--canvas` with 1 px dots in `--rule` every 16 px |
| live legend | current: 2 px accent border; enabled now: `--ok` edge; enabled after a delay: dashed `--accent`; last taken: `--formal` |
| error on element | 1.5 px `--error` border + count at the top-right |

## 8.7 Interaction patterns

- **Selection is global** and survives view switches; Escape clears it.
- **Keyboard first**: every canvas action has a key (chapter 6.4); `?` lists them.
- **Inline over modal**: rename, add member, delete leaf — inline; dialogs only where 8.4 says.
- **Feedback after every action**: the element changes, the save state changes, problems update —
  no toasts for success.
- **Prevent data loss**: autosave of operations, one undo history, conflict detection with the
  server revision, *Changed elsewhere — reload* keeps local operations for re-application.
- **Accessibility**: WCAG 2.2 AA contrast (8.3), visible focus ring, all canvas elements reachable
  by keyboard (Tab cycles nodes, arrows move between neighbours, Enter opens the inspector), live
  region for problems count, `prefers-reduced-motion` honoured, status never by colour alone
  (glyph + word).
- **Large graphs**: virtualised trees and tables; canvas renders only the visible viewport
  (xyflow), collapses parts beyond 50 nodes into their parent, and offers "focus + neighbourhood"
  as in today's knowledge graph.
