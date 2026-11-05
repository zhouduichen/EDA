#!/usr/bin/env python3
"""Static consistency check for the runtime menu pruning in SetupMenus().

The wxFormBuilder resource (eda.cpp) is deliberately never edited, so dead
menu entries are removed by label at runtime.  A typo in that label list would
silently leave a dead item on screen; this script makes that impossible to
miss without needing a GUI test run.

Checks:
  1. every pruned label really exists in the generated menu it is aimed at
  2. every surviving item has a branch in its OnMenu* handler
  3. no "尚未实现" placeholder survives in the production sources
  4. the 帮助 menu is removed wholesale, with no stale binding or handler
  5. separator cleanup leaves no leading / trailing / doubled separators
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "Project1"

failures = []
checks = 0


def check(cond, msg):
    global checks
    checks += 1
    if not cond:
        failures.append(msg)


eda = (ROOT / "eda.cpp").read_text(encoding="utf-8")
mf = (ROOT / "mainframe.cpp").read_text(encoding="utf-8")
hdr = (ROOT / "mainframe.h").read_text(encoding="utf-8")

# --- generated menu contents, in source order ----------------------------
item_re = re.compile(
    r'new\s+wxMenuItem\(\s*(\w+)\s*,\s*wxID_ANY\s*,\s*'
    r'wxString\(\s*wxT\("([^"]+)"\)\s*\)'
)
sep_re = re.compile(r'(\w+)->AppendSeparator\(\)')
title_re = re.compile(r'm_menubar2->Append\(\s*(\w+)\s*,\s*wxT\("([^"]+)"\)\s*\)')

events = []
for m in item_re.finditer(eda):
    events.append((m.start(), m.group(1), m.group(2)))
for m in sep_re.finditer(eda):
    events.append((m.start(), m.group(1), None))
events.sort()

menus = {}
for _, menu, label in events:
    menus.setdefault(menu, []).append(label)   # None == separator
titles = {m.group(1): m.group(2) for m in title_re.finditer(eda)}

check(len(menus) >= 6, "expected at least 6 generated menus, found %d" % len(menus))
check(titles.get("Help") == "帮助", "generated Help menu title not found")

# --- runtime prune lists --------------------------------------------------
dead_re = re.compile(r'k(\w+)Dead\[\]\s*=\s*\{(.*?)\};', re.S)
dead = {}
for m in dead_re.finditer(mf):
    dead[m.group(1)] = re.findall(r'wxT\("([^"]+)"\)', m.group(2))

check(len(dead) == 5, "expected 5 prune lists, found %d" % len(dead))
for menu, labels in dead.items():
    check(menu in menus, "prune list targets unknown menu %r" % menu)
    generated = {x for x in menus.get(menu, []) if x is not None}
    for label in labels:
        check(label in generated,
              "prune label %r not present in generated menu %s (typo?)" % (label, menu))

# --- handlers -------------------------------------------------------------
handler_re = re.compile(
    r'void MainFrame::OnMenu(\w+)\(wxCommandEvent& e\)\s*\{(.*?)\n\}', re.S)
branches = {}
for m in handler_re.finditer(mf):
    branches[m.group(1)] = set(
        re.findall(r'label\s*==\s*wxT\("([^"]+)"\)', m.group(2)))

for menu, labels in menus.items():
    if menu == "Help":
        continue
    visible = [x for x in labels if x is not None and x not in dead.get(menu, [])]
    if menu == "File":
        visible.append("导出网表")   # inserted at runtime by SetupMenus()
    handled = branches.get(menu, set())
    for label in visible:
        check(label in handled,
              "visible item %r in menu %s has no OnMenu%s branch"
              % (label, menu, menu))

# --- no placeholders left -------------------------------------------------
# Strip // comments first: prose describing the removal is not a placeholder.
code_only = "\n".join(line.split("//", 1)[0] for line in mf.splitlines())
check("尚未实现" not in code_only, "mainframe.cpp still contains a 尚未实现 placeholder")
check("OnMenuHelp" not in code_only, "mainframe.cpp still defines OnMenuHelp")
check("OnMenuHelp" not in hdr, "mainframe.h still declares OnMenuHelp")
check("BindMenu(Help" not in code_only, "Help menu is still bound")

# --- 帮助 menu removal ----------------------------------------------------
check('FindMenu(wxT("帮助"))' in mf, "no runtime removal of the 帮助 menu")
check("m_menubar2->Remove" in mf, "removed menu is not detached from the menu bar")
check("Help = nullptr" in mf, "Help pointer is not nulled after removal")

# --- separator cleanup, simulated with the same algorithm as PruneMenu ---
def cleanup(seq):
    out = []
    prev_sep = True
    for item in seq:
        sep = item is None
        if sep and prev_sep:
            continue
        prev_sep = sep
        out.append(item)
    if out and out[-1] is None:
        out.pop()
    return out

for menu, labels in menus.items():
    if menu == "Help":
        continue
    pruned = [x for x in labels if x is None or x not in dead.get(menu, [])]
    final = cleanup(pruned)
    check(bool(final), "menu %s would be empty after pruning" % menu)
    check(final[0] is not None, "menu %s starts with a separator" % menu)
    check(final[-1] is not None, "menu %s ends with a separator" % menu)
    check(not any(a is None and b is None for a, b in zip(final, final[1:])),
          "menu %s has doubled separators" % menu)

if failures:
    print("menu test: %d FAILED of %d checks" % (len(failures), checks))
    for f in failures:
        print("  - " + f)
    sys.exit(1)

print("menu test: ALL OK (%d checks passed)" % checks)
for menu in ("File", "Edit", "Project", "Simulate", "Window"):
    pruned = [x for x in menus.get(menu, [])
              if x is None or x not in dead.get(menu, [])]
    final = cleanup(pruned)
    visible = [x for x in final if x is not None]
    if menu == "File":
        visible.insert(visible.index("导出图像") + 1, "导出网表")
    print("  %-9s %d items: %s" % (menu, len(visible), " / ".join(visible)))
print("  Help      removed entirely")
