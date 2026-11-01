// Builds docs/final/SafeTrail_final_presentation.pptx.
//
//   npm install pptxgenjs@3.12.0   (once, anywhere on NODE_PATH)
//   APPLY_THEME=<pptx skill>/scripts/apply_theme.js node tools/make_deck.js
//
// Charts are the PNGs tools/make_figures.py draws from bench/results/*.csv,
// not native PowerPoint charts: Keynote rendered pptxgenjs's native charts
// blank. Every number in slide text is read from the same CSVs here, so the
// slides show the committed measurements. Screenshots come from
// tools/app_screenshots.py.
const fs = require("fs");
const path = require("path");
const pptxgen = require("pptxgenjs");

const ROOT = path.resolve(__dirname, "..");
const IMG = (f) => path.join(ROOT, "docs", "final", "images", f);
const OUT = path.join(ROOT, "docs", "final", "SafeTrail_final_presentation.pptx");

// Width / height of a PNG, from its IHDR chunk.
function ratio(file) {
  const b = fs.readFileSync(IMG(file));
  return b.readUInt32BE(16) / b.readUInt32BE(20);
}

function csv(name) {
  const lines = fs.readFileSync(path.join(ROOT, "bench", "results", name), "utf8").trim().split("\n");
  const head = lines[0].split(",");
  return lines.slice(1).map((l) => Object.fromEntries(l.split(",").map((v, i) => [head[i], v])));
}

const THEME = {
  name: "SafeTrail Operations",
  headFontFace: "Calibri",
  bodyFontFace: "Calibri",
  colors: {
    dk1: "0D1116", lt1: "E3E8EE", dk2: "161D25", lt2: "A7B1BD",
    accent1: "3FB6A8", accent2: "D6A440", accent3: "E07B39", accent4: "E5484D",
    accent5: "6C9BD2", accent6: "8E7CD8", hlink: "3FB6A8", folHlink: "8E7CD8",
  },
};
const HEX = THEME.colors;

const pres = new pptxgen();
pres.layout = "LAYOUT_WIDE"; // 13.333 x 7.5
pres.title = "SafeTrail — final project presentation";
pres.author = "Adi Vishal";
pres.theme = { headFontFace: THEME.headFontFace, bodyFontFace: THEME.bodyFontFace };
const C = pres.SchemeColor;
const BG = C.text1, SURF = C.text2, INK = C.background1, MUTED = C.background2;
const TEAL = C.accent1, AMBER = C.accent2, ORANGE = C.accent3, RED = C.accent4, BLUE = C.accent5, VIOLET = C.accent6;

// ── Layouts ───────────────────────────────────────────────────────────────
pres.defineSlideMaster({
  title: "SafeTrail Title", background: { color: BG },
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.6, y: 2.2, w: 6.4, h: 1.4, fontSize: 54, bold: true, color: INK, valign: "bottom", align: "left", margin: 0 }, text: "" } },
    { placeholder: { options: { name: "body", type: "body", x: 0.6, y: 3.75, w: 6.2, h: 1.6, fontSize: 20, color: MUTED, valign: "top", align: "left", margin: 0 }, text: "" } },
  ],
});
pres.defineSlideMaster({
  title: "SafeTrail Content", background: { color: BG },
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.6, y: 0.4, w: 12.1, h: 0.85, fontSize: 36, bold: true, color: INK, valign: "middle", align: "left", margin: 0 }, text: "" } },
    { text: { text: "SafeTrail · Data Structures project", options: { x: 0.6, y: 7.0, w: 6, h: 0.3, fontSize: 10, color: MUTED, margin: 0 } } },
  ],
  slideNumber: { x: 12.2, y: 7.0, w: 0.6, h: 0.3, fontSize: 10, color: MUTED, align: "right" },
});
pres.defineSlideMaster({
  title: "SafeTrail Divider", background: { color: SURF },
  objects: [
    { placeholder: { options: { name: "title", type: "title", x: 0.6, y: 2.6, w: 12.1, h: 1.2, fontSize: 48, bold: true, color: INK, align: "left", margin: 0 }, text: "" } },
    { placeholder: { options: { name: "body", type: "body", x: 0.6, y: 3.9, w: 12.1, h: 2.6, fontSize: 18, color: MUTED, valign: "top", align: "left", margin: 0 }, text: "" } },
  ],
});

function content(section, title, notes) {
  const s = pres.addSlide({ masterName: "SafeTrail Content", sectionTitle: section });
  s.addText(title, { placeholder: "title" });
  if (notes) s.addNotes(notes);
  return s;
}
function card(s, x, y, w, h, name) {
  s.addShape(pres.shapes.ROUNDED_RECTANGLE, { x, y, w, h, fill: { color: SURF }, line: { color: SURF }, rectRadius: 0.08, objectName: name });
}
function badge(s, x, y, label, color, name) {
  s.addShape(pres.shapes.OVAL, { x, y, w: 0.46, h: 0.46, fill: { color }, line: { color }, objectName: name + " badge" });
  s.addText(label, { x, y, w: 0.46, h: 0.46, fontSize: 14, bold: true, color: BG, align: "center", valign: "middle", margin: 0, isTextBox: true, objectName: name + " badge text" });
}
function stat(s, x, y, w, value, label, color, name) {
  s.addText(value, { x, y, w, h: 0.9, fontSize: 40, bold: true, color, margin: 0, isTextBox: true, objectName: name + " value" });
  s.addText(label, { x, y: y + 0.88, w, h: 0.6, fontSize: 13, color: MUTED, margin: 0, valign: "top", isTextBox: true, objectName: name + " label" });
}
function shot(s, file, x, y, w, name) {
  // all app screenshots are 16:10
  s.addImage({ path: IMG(file), x, y, w, h: w / 1.6, rounding: false, objectName: name,
               altText: name });
}

// ── 1. Title ─────────────────────────────────────────────────────────────
pres.addSection({ title: "Introduction" });
{
  const s = pres.addSlide({ masterName: "SafeTrail Title", sectionTitle: "Introduction" });
  s.addText("SafeTrail", { placeholder: "title" });
  s.addText("Geofencing and incident response for tourist safety, built on hand-written data structures in C++17", { placeholder: "body" });
  s.addText("Data Structures course project · github.com/adivishall/safetrail", { x: 0.6, y: 6.6, w: 6.2, h: 0.35, fontSize: 12, color: MUTED, margin: 0, isTextBox: true, objectName: "footer line" });
  s.addImage({ path: IMG("shot-live.png"), x: 7.3, y: 1.15, w: 5.6, h: 3.5, objectName: "live monitoring screenshot", altText: "Live monitoring console" });
  s.addText("Live Monitoring: simulated tourists (SIM) over real OpenStreetMap hazard zones and roads around Shillong", { x: 7.3, y: 4.75, w: 5.6, h: 0.6, fontSize: 11, color: MUTED, margin: 0, isTextBox: true, objectName: "screenshot caption" });
  s.addNotes("SafeTrail is a geofencing and incident-response system for tourist safety in low-connectivity regions, built on hand-written data structures in C++17.");
}

// ── 2. Problem ───────────────────────────────────────────────────────────
{
  const s = content("Introduction", "Where people get hurt, phones lose signal",
    "In the hills around Shillong, cliffs, rivers and closed roads are where people get hurt, and phones lose signal exactly there. Help is late when nobody knows who is where. Checking every position against every hazard zone is O(n) per GPS fix — the cost the whole design is about.");
  s.addText([
    { text: "Remote treks in Meghalaya: cliffs, rivers, closed roads", options: { bullet: true, breakLine: true } },
    { text: "Help arrives late when nobody knows who is where", options: { bullet: true, breakLine: true } },
    { text: "GPS is noisy: a fix is a disc of ±5–35 m, not a point", options: { bullet: true, breakLine: true } },
    { text: "Every fix must be checked against every zone that could contain it", options: { bullet: true } },
  ], { x: 0.6, y: 1.6, w: 6.2, h: 3.4, fontSize: 18, color: INK, paraSpaceAfter: 12, margin: 0, isTextBox: true, objectName: "problem bullets" });
  card(s, 7.3, 1.6, 5.4, 4.8, "cost card");
  stat(s, 7.7, 1.9, 4.8, "O(n) per fix", "a linear scan of every hazard zone, for every person, every second", AMBER, "cost");
  stat(s, 7.7, 3.45, 4.8, "100,000 zones", "the scale the indexes are measured at", TEAL, "scale");
  stat(s, 7.7, 5.0, 4.8, "Offline", "the phones that need it most have no network", RED, "offline");
}

// ── 3. What it does ──────────────────────────────────────────────────────
{
  const s = content("Introduction", "What SafeTrail does, end to end",
    "People opt in. Every fix is checked against hazard zones, with GPS uncertainty treated as a disc, not a point. Transitions become alerts, related alerts become incidents, the nearest responder is routed along roads, groups that split are noticed, phones that go offline keep working and sync later, and everything is kept in a tamper-evident history.");
  const items = [
    ["1", "Opt-in tracking", "consent + session, per user", TEAL],
    ["2", "Geofence", "inside / outside / uncertain", TEAL],
    ["3", "Alerts", "enter, approach, dwell, SOS", AMBER],
    ["4", "Incidents", "related alerts → one card", AMBER],
    ["5", "Dispatch", "nearest responder, A* route", ORANGE],
    ["6", "Groups", "separated members detected", ORANGE],
    ["7", "Offline", "phone evaluates, syncs later", VIOLET],
    ["8", "History", "past state + Merkle evidence", VIOLET],
  ];
  items.forEach(([n, t, d, col], i) => {
    const x = 0.6 + (i % 4) * 3.07, y = 1.65 + Math.floor(i / 4) * 2.55;
    card(s, x, y, 2.85, 2.25, "capability " + n);
    badge(s, x + 0.25, y + 0.28, n, col, "capability " + n);
    s.addText(t, { x: x + 0.25, y: y + 0.95, w: 2.4, h: 0.5, fontSize: 18, bold: true, color: INK, margin: 0, isTextBox: true, objectName: "capability " + n + " title" });
    s.addText(d, { x: x + 0.25, y: y + 1.45, w: 2.4, h: 0.65, fontSize: 14, color: MUTED, margin: 0, valign: "top", isTextBox: true, objectName: "capability " + n + " text" });
  });
}

// ── 4. Architecture ──────────────────────────────────────────────────────
pres.addSection({ title: "Design" });
{
  const s = content("Design", "Three layers, one process, no dependencies",
    "Three layers: the engine — indexes, geometry, the evaluator; the platform — users, consent, sessions, alerts, incidents, groups, responders, history, offline sync; and a single-threaded HTTP server with a web console. No database, no map service, no dependencies.");
  s.addImage({ path: IMG("fig-architecture.png"), x: 0.6, y: 1.4, w: 7.4, h: 7.4 / ratio("fig-architecture.png"), objectName: "architecture diagram", altText: "Architecture: browser, server, engine" });
  s.addText([
    { text: "Engine", options: { bold: true, color: AMBER, breakLine: true } },
    { text: "indexes, geometry, the 9-step evaluator", options: { breakLine: true } },
    { text: " ", options: { breakLine: true } },
    { text: "Platform", options: { bold: true, color: TEAL, breakLine: true } },
    { text: "users, consent, alerts, incidents, groups, responders, history, offline sync", options: { breakLine: true } },
    { text: " ", options: { breakLine: true } },
    { text: "Server + console", options: { bold: true, color: BLUE, breakLine: true } },
    { text: "one thread: every update in one total order, no locks" },
  ], { x: 8.4, y: 1.5, w: 4.3, h: 5.0, fontSize: 16, color: INK, margin: 0, valign: "top", isTextBox: true, objectName: "layer notes" });
}

// ── 5. Core structures ───────────────────────────────────────────────────
{
  const s = content("Design", "Five core structures answer the geofencing questions",
    "Brute force as the oracle; a quadtree that splits space; an R-tree that groups items, bulk-loaded with STR; an AVL interval tree for validity windows; a persistent quadtree for every past version.");
  const rows = [
    ["Brute force", "the oracle: which zone boxes meet this box?", "Θ(n)"],
    ["Quadtree", "same, by splitting space into quadrants", "O(log n + k) expected"],
    ["R-tree + STR packing", "same, by grouping items into envelopes", "O(log n + k) expected"],
    ["AVL interval tree", "which rules were in force at time t?", "O(log n) per update"],
    ["Persistent quadtree", "what did the zone set look like at 14:32?", "O(depth) nodes per change"],
  ];
  const head = { bold: true, color: MUTED, fill: { color: SURF }, fontSize: 13 };
  s.addTable([
    [{ text: "Structure", options: head }, { text: "Answers", options: head }, { text: "Bound", options: head }],
    ...rows.map((r) => r.map((c, i) => ({ text: c, options: { fontSize: 16, color: i === 0 ? TEAL : INK, bold: i === 0 } }))),
  ], { x: 0.6, y: 1.55, w: 12.1, colW: [3.2, 5.9, 3.0], rowH: 0.62, border: { type: "solid", pt: 0.75, color: "273140" }, fill: { color: BG }, valign: "middle", objectName: "core structures table" });
  s.addText("k = number of results. No spatial library, no std::map / set / unordered_map / priority_queue.", { x: 0.6, y: 5.6, w: 12.1, h: 0.4, fontSize: 13, color: MUTED, margin: 0, isTextBox: true, objectName: "core footnote" });
}

// ── 6. Supporting structures ─────────────────────────────────────────────
{
  const s = content("Design", "Every supporting structure has a job in the product",
    "A binary heap for Dijkstra, A* and alert triage; union-find with rollback for groups and incidents; an open-addressing hash table for users, sessions and sync; a timer wheel for escalation; a ring buffer for recent fixes; a k-d tree for snapping to the road network; a Merkle tree for evidence.");
  const items = [
    ["Binary heap", "Dijkstra / A* frontier, alert triage", TEAL],
    ["Union-find + rollback", "group split, alert → incident", TEAL],
    ["Hash table (probing)", "users, sessions, sync de-duplication", AMBER],
    ["Timer wheel", "unacknowledged alerts escalate", AMBER],
    ["Ring buffer", "last 64 fixes: speed, stillness", ORANGE],
    ["k-d tree", "snap to 144,090 road junctions", ORANGE],
    ["Adjacency list graph", "Dijkstra, A*, Hungarian dispatch", BLUE],
    ["Merkle tree", "provably unedited event log", VIOLET],
  ];
  items.forEach(([t, d, col], i) => {
    const x = 0.6 + (i % 2) * 6.15, y = 1.55 + Math.floor(i / 2) * 1.32;
    card(s, x, y, 5.95, 1.12, t);
    s.addShape(pres.shapes.OVAL, { x: x + 0.25, y: y + 0.36, w: 0.4, h: 0.4, fill: { color: col }, line: { color: col }, objectName: t + " marker" });
    s.addText(t, { x: x + 0.85, y: y + 0.14, w: 4.9, h: 0.45, fontSize: 17, bold: true, color: INK, margin: 0, isTextBox: true, objectName: t + " title" });
    s.addText(d, { x: x + 0.85, y: y + 0.58, w: 4.9, h: 0.4, fontSize: 14, color: MUTED, margin: 0, isTextBox: true, objectName: t + " text" });
  });
}

// ── 7. Results ───────────────────────────────────────────────────────────
pres.addSection({ title: "Evidence" });
{
  const sc = csv("index_scaling.csv"), dn = csv("index_density.csv");
  const last = sc[sc.length - 1], dl = dn[dn.length - 1];
  const s = content("Evidence", "Indexes make it hundreds of times faster, never different",
    "At 100,000 zones in one district the R-tree answers 224 times faster than a scan, the quadtree 33 times — identical answers. At constant density the speedups keep growing, because the answer size k is what bounds them.");
  // Chart images (tools/make_figures.py, same CSVs): Keynote renders native
  // pptxgenjs charts blank, and this deck must work in PowerPoint, Keynote and
  // as a PDF fallback.
  s.addImage({ path: IMG("chart-speedups.png"), x: 0.6, y: 1.55, w: 7.4, h: 7.4 / ratio("chart-speedups.png"), objectName: "speedup chart",
               altText: "Quadtree " + Number(last.quad_speedup).toFixed(1) + "x and R-tree " + Number(last.rtree_speedup).toFixed(0) + "x in one district; " + Number(dl.quad_speedup).toFixed(0) + "x and " + Number(dl.rtree_speedup).toFixed(0) + "x at constant density" });
  card(s, 8.4, 1.5, 4.3, 4.9, "result stats");
  stat(s, 8.75, 1.75, 3.7, "0 mismatches", "every index returns exactly the brute-force answer", TEAL, "mismatch");
  stat(s, 8.75, 3.25, 3.7, Number(csv("index_build.csv").slice(-1)[0].query_gain).toFixed(1) + "× faster", "STR-packed vs insertion-built R-tree queries", BLUE, "str");
  stat(s, 8.75, 4.75, 3.7, "≈ 75 % fewer", "junctions settled by A* than by Dijkstra", AMBER, "astar");
  s.addText("Apple M4, medians of 11 interleaved rounds, paired ratios — bench/results/*.csv, regenerated by make bench", { x: 0.6, y: 6.5, w: 12.1, h: 0.35, fontSize: 11, color: MUTED, margin: 0, isTextBox: true, objectName: "results source" });
}

// ── 8. Multi-user ────────────────────────────────────────────────────────
{
  const mu = csv("multi_user.csv");
  const users = ["1", "10", "100", "1000", "5000"];
  const series = ["in-order", "user-batched", "noisy"].map((p) => ({
    name: p, labels: users.map((u) => Number(u).toLocaleString("en-US")),
    values: users.map((u) => Number(mu.find((r) => r.pattern === p && r.users === u).us_per_fix)),
  }));
  const s = content("Evidence", "Thousands of independent users, the same cost per fix",
    "The platform ingests a fix — validation, ordering checks, trail, evaluation, alerting — in about 3 microseconds, flat from 10 to 5,000 users. That benchmark also found a real bug: alert bookkeeping was quadratic until alerts were indexed per user.");
  s.addImage({ path: IMG("chart-multi-user.png"), x: 0.6, y: 1.55, w: 7.6, h: 7.6 / ratio("chart-multi-user.png"), objectName: "multi-user chart",
               altText: "Microseconds per fix for in-order, user-batched and noisy streams, 1 to 5000 users: " + series[0].values.map((v) => v.toFixed(2)).join(", ") });
  card(s, 8.6, 1.5, 4.1, 4.9, "multi-user stats");
  stat(s, 8.95, 1.75, 3.5, "≈ 300,000", "fixes per second on one core", TEAL, "throughput");
  stat(s, 8.95, 3.25, 3.5, "≈ 6.9 KB", "of state per tracked user", BLUE, "memory");
  stat(s, 8.95, 4.75, 3.5, "Exact", "duplicate and late-fix counters in the noisy stream", AMBER, "noisy");
}

// ── 9. Correctness ───────────────────────────────────────────────────────
{
  const s = content("Evidence", "Correctness is tested, not assumed",
    "Every fast structure has an oracle; invariants are checked after every operation; 51 test files, 12,137 checks; 28 injected bugs, all caught; byte-identical output on macOS and Linux.");
  const st = [["51", "test files", TEAL], ["12,137", "checks, 0 failed", TEAL], ["28 / 28", "injected bugs caught", AMBER], ["30", "end-to-end HTTP checks", BLUE]];
  st.forEach(([v, l, col], i) => stat(s, 0.6 + (i % 2) * 3.0, 1.6 + Math.floor(i / 2) * 1.75, 2.8, v, l, col, "correctness " + l));
  s.addText([
    { text: "Oracles for every fast structure", options: { bullet: true, breakLine: true } },
    { text: "Invariant audit after every operation", options: { bullet: true, breakLine: true } },
    { text: "Same events under all four indexes", options: { bullet: true, breakLine: true } },
    { text: "Same bytes on macOS and Linux (CI)", options: { bullet: true } },
  ], { x: 0.6, y: 5.15, w: 5.8, h: 1.6, fontSize: 15, color: INK, paraSpaceAfter: 4, margin: 0, isTextBox: true, objectName: "method bullets" });
  shot(s, "shot-lab-spatial.png", 6.9, 1.6, 5.8, "lab screenshot");
  s.addText("Data Structures Lab: the real quadtree's visited (teal) and pruned (red) nodes; every index vs brute force", { x: 6.9, y: 5.32, w: 5.8, h: 0.6, fontSize: 11, color: MUTED, margin: 0, isTextBox: true, objectName: "lab caption" });
}

// ── 10. Privacy ──────────────────────────────────────────────────────────
{
  const s = content("Evidence", "A safety system, not a surveillance tool",
    "Opt-in only, nobody else can switch it on, stop any time, history expires and can be erased, every look at someone's location is audited, responders see a person only while assigned.");
  const rows = [
    ["Opt-in", "only the person can consent and start sharing", TEAL],
    ["Stop any time", "zone state discarded, no invented exit", TEAL],
    ["Retention + erasure", "trails expire; the person can erase theirs", AMBER],
    ["Audited access", "every look at a location goes in a Merkle log", AMBER],
    ["Need to know", "responders see a person only while assigned", BLUE],
    ["Honest demo data", "simulated people marked SIM, fixtures DEMO", VIOLET],
  ];
  rows.forEach(([t, d, col], i) => {
    const y = 1.55 + i * 0.86;
    s.addShape(pres.shapes.OVAL, { x: 0.6, y: y + 0.12, w: 0.36, h: 0.36, fill: { color: col }, line: { color: col }, objectName: t + " marker" });
    s.addText(t, { x: 1.15, y, w: 5.0, h: 0.4, fontSize: 17, bold: true, color: INK, margin: 0, isTextBox: true, objectName: t + " title" });
    s.addText(d, { x: 1.15, y: y + 0.38, w: 5.0, h: 0.38, fontSize: 14, color: MUTED, margin: 0, isTextBox: true, objectName: t + " text" });
  });
  shot(s, "shot-tourist.png", 6.6, 1.6, 6.1, "tourist page screenshot");
  s.addText("My Safety: the tourist's own consent switch, sharing state, SOS and erasure", { x: 6.6, y: 5.45, w: 6.1, h: 0.4, fontSize: 11, color: MUTED, margin: 0, isTextBox: true, objectName: "tourist caption" });
}

// ── 11. Live demo divider ────────────────────────────────────────────────
pres.addSection({ title: "Demo" });
{
  const s = pres.addSlide({ masterName: "SafeTrail Divider", sectionTitle: "Demo" });
  s.addText("Live demo", { placeholder: "title" });
  s.addText([
    { text: "Simulated tourists over real OpenStreetMap zones and roads", options: { bullet: true, breakLine: true } },
    { text: "Restricted-zone entry → alert → incident → A* dispatch", options: { bullet: true, breakLine: true } },
    { text: "Group separation, and a phone that goes offline and syncs", options: { bullet: true, breakLine: true } },
    { text: "What the system knew at a past time; Merkle proof checked in the browser", options: { bullet: true, breakLine: true } },
    { text: "Data Structures Lab: the real quadtree, R-tree, AVL tree, Dijkstra and A*", options: { bullet: true } },
  ], { placeholder: "body", paraSpaceAfter: 8 });
  s.addNotes("Switch to the browser: docs/final/LIVE_DEMO_SCRIPT.md has the exact steps and fallbacks.");
}

// ── 12. Syllabus ─────────────────────────────────────────────────────────
pres.addSection({ title: "Close" });
{
  const s = content("Close", "Syllabus topics, each in working code",
    "Arrays and ring buffers, stacks, trees and recursion, BSTs and AVL rotations, augmented trees, hashing with collision handling, heaps and priority queues, graphs and adjacency lists, Dijkstra and A*, union-find, binary search, complexity and measurement. Beyond: persistence, Merkle proofs, Lamport clocks, computational geometry.");
  const head = { bold: true, color: MUTED, fill: { color: SURF }, fontSize: 13 };
  const rows = [
    ["Arrays, ring buffer", "zone store; last 64 fixes"],
    ["Trees, recursion, traversal", "quadtree and R-tree queries"],
    ["BST, AVL rotations, augmentation", "interval tree for validity windows"],
    ["Hashing, collision handling", "users, sessions, sync de-duplication"],
    ["Heap, priority queue", "Dijkstra, A*, alert triage"],
    ["Graphs, adjacency list, shortest paths", "road network, responder routing"],
    ["Union-find", "group cohesion, incident correlation"],
    ["Binary search", "position at a past time"],
  ];
  s.addTable([
    [{ text: "Syllabus concept", options: head }, { text: "Where SafeTrail uses it", options: head }],
    ...rows.map((r) => r.map((c, i) => ({ text: c, options: { fontSize: 14, color: i === 0 ? TEAL : INK } }))),
  ], { x: 0.6, y: 1.5, w: 8.0, colW: [3.9, 4.1], rowH: 0.5, border: { type: "solid", pt: 0.75, color: "273140" }, fill: { color: BG }, valign: "middle", objectName: "syllabus table" });
  card(s, 9.0, 1.5, 3.7, 4.5, "beyond card");
  s.addText("Beyond the syllabus", { x: 9.3, y: 1.7, w: 3.2, h: 0.45, fontSize: 17, bold: true, color: AMBER, margin: 0, isTextBox: true, objectName: "beyond title" });
  s.addText([
    { text: "Persistent data structures", options: { bullet: true, breakLine: true } },
    { text: "Merkle proofs, SHA-256", options: { bullet: true, breakLine: true } },
    { text: "Lamport clocks, offline sync", options: { bullet: true, breakLine: true } },
    { text: "Computational geometry", options: { bullet: true, breakLine: true } },
    { text: "Hysteresis, state machines", options: { bullet: true } },
  ], { x: 9.3, y: 2.25, w: 3.2, h: 3.5, fontSize: 14, color: INK, paraSpaceAfter: 6, margin: 0, valign: "top", isTextBox: true, objectName: "beyond list" });
}

// ── 13. Limitations ──────────────────────────────────────────────────────
{
  const s = content("Close", "What it does not claim, and what comes next",
    "Simulated people; an in-memory server that runs on this laptop — the public link is only a static replay; the OSM hazard polygons are thin, so 'uncertain' is often the honest answer; benchmarks from one laptop. Next: persistence, real GPS traces, a phone app running the same offline evaluator, public hosting behind TLS, and Argon2.");
  const col = (x, title, color, items, name) => {
    card(s, x, 1.55, 5.9, 4.0, name);
    s.addText(title, { x: x + 0.35, y: 1.8, w: 5.2, h: 0.5, fontSize: 20, bold: true, color, margin: 0, isTextBox: true, objectName: name + " title" });
    s.addText(items.map((t, i) => ({ text: t, options: { bullet: true, breakLine: i < items.length - 1 } })),
      { x: x + 0.35, y: 2.45, w: 5.2, h: 3.0, fontSize: 15, color: INK, paraSpaceAfter: 8, margin: 0, valign: "top", isTextBox: true, objectName: name + " list" });
  };
  col(0.6, "Limitations", AMBER, ["People are simulated; no field trial", "Server state is in memory, one thread",
    "Runs locally; the public link is a static replay", "OSM hazard polygons are thin: 'uncertain' is often the honest answer",
    "Benchmarks from one laptop; ratios move between sessions", "Battery figure is a model, not a measurement"], "limitations");
  col(6.8, "Next", TEAL, ["Durable event log, signed Merkle roots", "Evaluation on real GPS traces",
    "Phone app running the same offline evaluator", "O(log n) index removal; travel-time routing",
    "Public hosting behind TLS, with persistence", "Argon2id, external security review"], "next");
}

// ── 14. Close ────────────────────────────────────────────────────────────
{
  const s = pres.addSlide({ masterName: "SafeTrail Title", sectionTitle: "Close" });
  s.addText("Thank you", { placeholder: "title" });
  s.addText("Code, tests, benchmarks, report and this deck: github.com/adivishall/safetrail. Every benchmark figure here is regenerated by make bench.", { placeholder: "body" });
  s.addImage({ path: IMG("shot-incident.png"), x: 7.3, y: 1.15, w: 5.6, h: 3.5, objectName: "incident screenshot", altText: "Incident with A* road route" });
  s.addText("An incident assigned: the responder's route is A* over the OpenStreetMap road graph", { x: 7.3, y: 4.75, w: 5.6, h: 0.6, fontSize: 11, color: MUTED, margin: 0, isTextBox: true, objectName: "incident caption" });
  s.addNotes("Thank you — the code, tests, benchmarks and this deck are in the repository, and every benchmark figure on these slides is regenerated by make bench.");
}

(async () => {
  await pres.writeFile({ fileName: OUT });
  const { applyTheme } = require(process.env.APPLY_THEME);
  await applyTheme(OUT, THEME);
  console.log("wrote " + path.relative(ROOT, OUT));
})();
