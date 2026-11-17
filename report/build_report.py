# generated with Claude Code
"""build the project 2 report pdf from summary.csv and the plots from analyze.py.

usage: python3 report/build_report.py results/local report/project2-report.pdf
"""
import csv
import sys
from pathlib import Path

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle, getSampleStyleSheet
from reportlab.lib.units import inch
from reportlab.platypus import (Image, KeepTogether, ListFlowable, ListItem, PageBreak, Paragraph,
                                SimpleDocTemplate, Spacer, Table, TableStyle)

data_dir = Path(sys.argv[1])
out = Path(sys.argv[2])

styles = getSampleStyleSheet()
body = ParagraphStyle("body", parent=styles["BodyText"], fontName="Helvetica", fontSize=10, leading=13.5,
                      spaceAfter=6)
h1 = ParagraphStyle("h1", parent=styles["Heading1"], fontName="Helvetica-Bold", fontSize=14, spaceBefore=10,
                    spaceAfter=6)
h2 = ParagraphStyle("h2", parent=styles["Heading2"], fontName="Helvetica-Bold", fontSize=11.5, spaceBefore=8,
                    spaceAfter=4, keepWithNext=1)
title = ParagraphStyle("title", parent=styles["Title"], fontName="Helvetica-Bold", fontSize=17, spaceAfter=4)
sub = ParagraphStyle("sub", parent=body, alignment=TA_CENTER, textColor=colors.HexColor("#52514e"))
caption = ParagraphStyle("caption", parent=body, fontSize=8.5, leading=11, alignment=TA_CENTER,
                         textColor=colors.HexColor("#52514e"))
cell = ParagraphStyle("cell", parent=body, fontSize=8, leading=10, spaceAfter=0)
code = ParagraphStyle("code", parent=body, fontName="Courier", fontSize=8.5, leading=11)


def p(text, style=body):
    return Paragraph(text, style)


def bullets(items):
    return ListFlowable([ListItem(p(t), leftIndent=12) for t in items], bulletType="bullet", start="•",
                        leftIndent=12, bulletFontSize=8)


rows = list(csv.DictReader(open(data_dir / "summary.csv")))
for r in rows:
    for k, v in r.items():
        if k != "sweep":
            r[k] = float(v)


def get(sweep, **fixed):
    for r in rows:
        if r["sweep"] == sweep and all(r[k] == v for k, v in fixed.items()):
            return r
    raise KeyError((sweep, fixed))


def figure_pair(names, captions):
    imgs = [Image(str(data_dir / n), width=3.35 * inch, height=3.35 * inch * 3.6 / 5.5) for n in names]
    caps = [p(c, caption) for c in captions]
    while len(imgs) < 2:
        imgs.append(""); caps.append("")
    t = Table([imgs, caps], colWidths=[3.45 * inch, 3.45 * inch])
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 2),
                           ("RIGHTPADDING", (0, 0), (-1, -1), 2)]))
    return t


C = lambda t: f"<font face='Courier'>{t}</font>"
story = []
story.append(p("CS 6378 Project 2 — Maekawa Mutex Report", title))
story.append(p("Keven Diaz (kxd210034)", sub))
story.append(Spacer(1, 6))

story.append(p("1. What I built", h1))
story.append(bullets([
    f"C++17, one process per node. The <b>application</b> module generates requests and calls {C('cs_enter()')} "
    f"(blocks until the node can enter) and {C('cs_leave()')}. The <b>mutex service</b> module underneath runs "
    "Maekawa's algorithm.",
    "Deadlocks are avoided with preemption: REQUEST, LOCKED, FAILED, INQUIRE, RELINQUISH, RELEASE. Priority is "
    "(sequence number, node ID), lower wins. A node gives back an inquired vote only once it has seen a FAILED; "
    "once it's in the CS it keeps all its votes until it leaves.",
    "Inter-request delay d and CS time c are exponential random variables (not uniform) with the configured means. "
    "d is counted from when the current request is granted. The CS itself just sleeps.",
    "Full mesh of TCP connections; one worker thread owns all protocol state, so the app can block while "
    "messages keep getting processed.",
]))

story.append(p("Testing", h2))
story.append(p(
    "Fully automated, no reading logs. The first node in the config runs an observer. Every node tells it "
    "\"entering\" before its CS body and \"leaving\" after, and waits for an ack each time (before releasing votes). "
    "If anyone enters while someone else is inside, the observer marks the run failed, and it also checks every "
    "node did exactly the right number of entries and exits. The launcher prints "
    f"{C('CHECK PASS')} or fails. It doesn't compare clocks between machines. Every experiment run below passed."))

story.append(p("2. Two bugs I hit", h1))
story.append(p(
    "<b>Deadlock from the paper's step 2.</b> One 9-node run hung with zero CS entries. In the paper a voter only "
    "sends FAILED to the request that just arrived. So: voter locked by H, R1 (beats H) arrives and gets queued "
    "with an INQUIRE sent to H but no FAILED, then R0 (beats R1) arrives and gets queued with nothing sent. "
    "Voter goes to R0. If R0 needs a vote R1 is holding, R1 sits on the INQUIRE forever because it never got a "
    "FAILED, and R1 is waiting on the voter R0 has. Fix: when a new request jumps to the front of a voter's queue, "
    "the voter also sends FAILED to anyone waiting there who hasn't gotten one. No hangs in any of the experiment "
    "runs after that. "
    "Turns out this is a known flaw in the original algorithm (Sanders, 1987)."))
story.append(p(
    f"<b>Nagle's algorithm.</b> Messages are tiny (24 bytes) and TCP was holding them back. Turning on "
    f"{C('TCP_NODELAY')} roughly doubled throughput (about 40 to 80 CS/s at n = 9, d = 20, c = 10). All results "
    "below are after both fixes."))

story.append(p("3. How I measured", h1))
story.append(p("The handout doesn't define the metrics exactly, so here's what I went with:", body))
story.append(bullets([
    "<b>Message complexity</b> = protocol messages sent over the network / number of CS executions. Doesn't count "
    "a node voting for itself (no network), the observer, or startup/shutdown messages.",
    f"<b>Response time</b> = from calling {C('cs_enter()')} until that CS finishes, averaged over all requests.",
    f"<b>Throughput</b> = total CS executions / run time. Each node times itself (first request to last "
    f"{C('cs_leave()')}) and I use the slowest node, so no clocks get compared across machines.",
]))
story.append(p(
    "Setup: all nodes on one Ubuntu 22.04 box (4 cores, g++ 11.4), talking over TCP loopback. Grid quorums "
    "(row + column, same as the handout example), so K = 2&radic;n &minus; 1 and n has to be a square. Baseline is "
    "the handout example, n = 9, d = 20 ms, c = 10 ms, changing one thing at a time. 40 requests per node, "
    f"<b>every point is the average of 5 runs</b> ({int(sum(r['runs'] for r in rows))} runs total), "
    "error bars are &plusmn;1 std dev. On real csa-dcXX machines the network adds latency per hop, so expect "
    "higher response times and lower throughput; message counts shouldn't change."))

story.append(p("4. Results", h1))
story.append(p(
    "Quick context: at the baseline the system is saturated. Nine nodes each wanting the CS every ~30 ms is "
    "~300 requests/s, but one CS at a time at 10 ms caps out around 100/s. So most of the time everybody is "
    "waiting, which explains a lot of the numbers."))

r4, r36 = get("n", n=4.0), get("n", n=36.0)
story.append(p("Varying n (d = 20, c = 10)", h2))
story.append(figure_pair(["messages_vs_n.png", "response_vs_n.png"],
                         ["Fig 1. Messages per CS vs n", "Fig 2. Response time vs n"]))
story.append(figure_pair(["throughput_vs_n.png"], ["Fig 3. Throughput vs n"]))
story.append(bullets([
    f"Messages per CS go from {r4['messages_per_cs']:.1f} to {r36['messages_per_cs']:.1f}, exactly 4(K&minus;1) "
    "every time, so it grows with &radic;n, not n. That's between the paper's 3(K&minus;1) (light load) and "
    "5(K&minus;1) (worst case). Probably REQUEST + LOCKED + RELEASE + one FAILED per member, but I didn't count "
    "message types separately.",
    f"Response time goes up about linearly, {r4['response_ms']:.0f} to {r36['response_ms']:.0f} ms, since you're "
    "waiting behind roughly everyone else's CS.",
    "Throughput stays flat around 78 to 84 CS/s. Only one CS runs at a time, so more nodes just means more waiting.",
]))

d5, d500 = get("d", d=5.0), get("d", d=500.0)
story.append(p("Varying d (n = 9, c = 10)", h2))
story.append(figure_pair(["messages_vs_d.png", "response_vs_d.png"],
                         ["Fig 4. Messages per CS vs d (log x)", "Fig 5. Response time vs d (log x)"]))
story.append(figure_pair(["throughput_vs_d.png"], ["Fig 6. Throughput vs d (log x)"]))
story.append(bullets([
    "Up to d = 50 it's still saturated, so everything's flat.",
    f"After that contention drops: messages go {d5['messages_per_cs']:.1f} &rarr; {d500['messages_per_cs']:.1f}, "
    "close to the 3(K&minus;1) = 12 floor (fewer FAILED/INQUIRE/RELINQUISH).",
    f"Response time drops {d5['response_ms']:.0f} &rarr; {d500['response_ms']:.0f} ms, basically just c plus a "
    "couple message hops.",
    f"Throughput drops {d5['throughput']:.0f} &rarr; {d500['throughput']:.0f} CS/s, but that's just because nodes "
    "aren't asking as often (9 nodes every ~0.5 s &asymp; 17/s max).",
]))

cs = [get("c", c=float(v)) for v in (5, 10, 20, 40)]
story.append(p("Varying c (n = 9, d = 20)", h2))
story.append(figure_pair(["messages_vs_c.png", "response_vs_c.png"],
                         ["Fig 7. Messages per CS vs c", "Fig 8. Response time vs c"]))
story.append(figure_pair(["throughput_vs_c.png"], ["Fig 9. Throughput vs c"]))
story.append(bullets([
    "All saturated, so messages stay at 16.",
    "Response time scales with c (" + ", ".join(f"{r['response_ms']:.0f}" for r in cs) + " ms), since you're "
    "mostly waiting on other nodes' critical sections.",
    "Throughput is about 1/c (" + ", ".join(f"{r['throughput']:.0f}" for r in cs) + " CS/s). Fitting "
    "1/(c + &delta;) gives &delta; around 2 to 3.5 ms of overhead per CS (vote handoff, observer acks, scheduling).",
]))

story.append(p("All the numbers", h2))
head = ["Sweep", "n", "d", "c", "Msgs/CS", "Response (ms)", "Throughput (CS/s)"]
table = [head]
for r in sorted(rows, key=lambda r: ("ndc".index(r["sweep"]), r[r["sweep"]])):
    table.append([r["sweep"], f"{r['n']:g}", f"{r['d']:g}", f"{r['c']:g}",
                  f"{r['messages_per_cs']:.2f} ± {r['messages_per_cs_sd']:.2f}",
                  f"{r['response_ms']:.1f} ± {r['response_ms_sd']:.1f}",
                  f"{r['throughput']:.1f} ± {r['throughput_sd']:.1f}"])
t = Table([[p(c, cell) for c in row] for row in table], repeatRows=1,
          colWidths=[w * inch for w in (0.6, 0.45, 0.5, 0.5, 1.2, 1.25, 1.3)], hAlign="CENTER")
t.setStyle(TableStyle([
    ("BACKGROUND", (0, 0), (-1, 0), colors.HexColor("#ecebe7")),
    ("LINEBELOW", (0, 0), (-1, 0), 0.6, colors.HexColor("#8a8985")),
    ("ROWBACKGROUNDS", (0, 1), (-1, -1), [colors.white, colors.HexColor("#f7f7f5")]),
    ("VALIGN", (0, 0), (-1, -1), "MIDDLE"),
]))
story.append(t)
story.append(p("d and c in ms. Mean over 5 runs &plusmn; 1 std dev. Raw data is in results/local/ in the repo.",
               caption))

story.append(p("5. Assumptions", h1))
story.append(bullets([
    "Handout says the config has n + 1 valid lines but then describes 1 header + n nodes + n quorums, so I parse "
    "1 + 2n.",
    "The i-th quorum line belongs to the i-th node, and every ID on it is a member (the first one isn't the owner).",
    f"If the d deadline already passed while still in the CS, the next request goes out right after "
    f"{C('cs_leave()')}. One outstanding request per node.",
    "The extra FAILEDs from the deadlock fix are counted in the message numbers.",
    "The observer stayed on during experiments (so every run is verified). Its messages aren't counted, but its "
    "acks add a little time while votes are held.",
    "Experiments ran on one machine; the demo runs on the csa-dcXX machines.",
]))

doc = SimpleDocTemplate(str(out), pagesize=letter, leftMargin=0.8 * inch, rightMargin=0.8 * inch,
                        topMargin=0.7 * inch, bottomMargin=0.7 * inch,
                        title="CS 6378 Project 2 Report", author="Keven Diaz")
doc.build(story, onFirstPage=lambda c, d: None,
          onLaterPages=lambda c, d: (c.setFont("Helvetica", 8), c.drawRightString(7.7 * inch, 0.45 * inch, f"{d.page}")))
print("wrote", out)
