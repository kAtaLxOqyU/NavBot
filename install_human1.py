#!/usr/bin/env python3
"""Installs human1 (fidget timer + money-driven eco) into the NavBot fork. Run from the repo root AFTER podbot_navbot.patch and podbot_fix8.patch.
   Edits: podbot_manager.cpp (fidget call), podbot_commands.cpp (console settings), podbot.cfg (settings), css_mod.cpp (eco hooks).
   All edits are computed first; if any anchor is missing nothing is written. Safe to run twice."""
import os, re, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MGR = 'extension/podbot/podbot_manager.cpp'
CMD = 'extension/podbot/podbot_commands.cpp'
CSS = 'extension/mods/css/css_mod.cpp'
CFG = 'configs/podbot/podbot.cfg'
INL = 'extension/podbot/podbot_human.inl'

FIDGET = ['pb_fidget', 'pb_fidget_freeze_rate', 'pb_fidget_run_rate', 'pb_fidget_draw_time', 'pb_fidget_buffer', 'pb_fidget_jiggle', 'pb_fidget_jiggle_after']
LOOK = ['pb_look', 'pb_look_rate', 'pb_look_min_speed']
ECO = ['pb_eco', 'pb_eco_startmoney', 'pb_eco_half_rounds', 'pb_eco_ready_ratio', 'pb_eco_debug']

CFG_FIDGET = """
# CS:S human-behaviour edition: fidgeting (quick-switch pairs, hold jiggle). Rates are events per minute.
# A bot stops fidgeting at (contact time - (pb_fidget_draw_time + pb_fidget_buffer)) seconds after freeze time ends, or at once when it sees an enemy or takes damage.
pb_fidget 1
pb_fidget_freeze_rate 20
pb_fidget_run_rate 6
pb_fidget_draw_time 1.0
pb_fidget_buffer 0.15
pb_fidget_jiggle 1
pb_fidget_jiggle_after 8.0
"""
CFG_LOOK = """
# CS:S human-behaviour edition: glances while running (SofTy demo: ~5.4 per minute, median 33 degrees, ~0.5 s).
pb_look 1
pb_look_rate 5.4
pb_look_min_speed 100
"""
CFG_ECO = """
# CS:S economy (playbook): synchronized eco / force rounds instead of the simple eco_limit checks. 0 = use the old behaviour.
pb_eco 1
# 0 = detect mp_startmoney (800 or 1000) from the round-1 cash. Set 800 or 1000 to force it.
pb_eco_startmoney 0
pb_eco_half_rounds 15
pb_eco_ready_ratio 60
pb_eco_debug 0
"""

CALL = ("\t\thuman::UpdateFidget(now,\n"
        "\t\t\t[this](const char* n, float d) { return m_settings.GetFloat(n, d); },\n"
        "\t\t\t[this](CCSSBot* b, const char* s) { DebugLog(b, \"%s\", s); });\n")

CALL_LOOK = ("\t\tlook::UpdateLook(now,\n"
             "\t\t\t[this](const char* n, float d) { return m_settings.GetFloat(n, d); },\n"
             "\t\t\t[this](CCSSBot* b, const char* s) { DebugLog(b, \"%s\", s); });\n")
INL_LOOK = 'extension/podbot/podbot_look.inl'

def read(p):
    with open(p, 'r', encoding='utf-8', newline='') as f: return f.read()
def write(p, s):
    with open(p, 'w', encoding='utf-8', newline='') as f: f.write(s)
def die(msg): print('STOP: ' + msg + '\nNothing was changed.'); sys.exit(1)

for p in (MGR, CMD, CSS, CFG):
    if not os.path.isfile(p): die('%s not found - run from the repo root after applying the PODBot patches' % p)

out = {}     # path -> new text
notes = []

# ---------------- podbot_manager.cpp
s = read(MGR)
if 'podbot_human.inl' not in s:
    m = re.search(r'^namespace podbot\b', s, re.M)
    if m: s = s[:m.start()] + '#include "podbot_human.inl"\n#include "podbot_look.inl"\n\n' + s[m.start():]
    else:
        a = '#include <bot/interfaces/profile.h>'
        if a not in s: die('no "namespace podbot" / profile.h include in ' + MGR)
        s = s.replace(a, a + '\n#include "podbot_human.inl"\n#include "podbot_look.inl"', 1)
    notes.append('manager: include')
if 'human::UpdateFidget' not in s:
    m = re.search(r'^([ \t]*)UpdateObstacles\(now\);[ \t]*\r?\n', s, re.M)
    if not m: die('UpdateObstacles(now); call not found in Frame() - is podbot_fix8 applied?')
    s = s[:m.end()] + CALL + s[m.end():]
    notes.append('manager: fidget call')
if 'look::UpdateLook' not in s:
    if 'podbot_look.inl' not in s: s = s.replace('#include "podbot_human.inl"', '#include "podbot_human.inl"\n#include "podbot_look.inl"', 1)
    m = re.search(r'human::UpdateFidget\(now,.*?\}\);[ \t]*\r?\n', s, re.S)
    if not m: die('human::UpdateFidget call not found')
    s = s[:m.end()] + CALL_LOOK + s[m.end():]
    notes.append('manager: look-around call')
out[MGR] = s

# ---------------- podbot_commands.cpp
c = read(CMD)
if 'X(pb_fidget)' not in c:
    a = 'X(pb_unstuck) X(pb_unstuck_time)'
    if a not in c: die('pb_unstuck settings not found in ' + CMD + ' - is podbot_fix8 applied?')
    c = c.replace(a, a + ' \\\n\t' + ' '.join('X(%s)' % n for n in FIDGET), 1); notes.append('commands: fidget settings')
if 'X(pb_look)' not in c:
    a = 'X(pb_fidget_jiggle_after)'
    c = c.replace(a, a + ' \\\n\t' + ' '.join('X(%s)' % n for n in LOOK), 1); notes.append('commands: look settings')
if 'X(pb_eco)' not in c:
    a = 'X(pb_fidget_jiggle_after)'
    c = c.replace(a, a + ' \\\n\t' + ' '.join('X(%s)' % n for n in ECO), 1); notes.append('commands: eco settings')
out[CMD] = c

# ---------------- podbot.cfg
g = read(CFG)
if not g.endswith('\n'): g += '\n'
def add_after(g, anchor, block):
    i = g.find(anchor)
    if i < 0: return g + block
    j = g.find('\n', i) + 1
    return g[:j] + block + g[j:]
if 'pb_fidget ' not in g: g = add_after(g, 'pb_unstuck_time', CFG_FIDGET); notes.append('cfg: fidget block')
if 'pb_look ' not in g: g = add_after(g, 'pb_fidget_jiggle_after', CFG_LOOK); notes.append('cfg: look block')
if 'pb_eco ' not in g: g = add_after(g, 'pb_fidget_jiggle_after', CFG_ECO); notes.append('cfg: eco block')
out[CFG] = g

# ---------------- css_mod.cpp
t = read(CSS)
def need(cond, what):
    if not cond: die('css_mod.cpp anchor not found: ' + what)

if 'podbot_human.inl' not in t:
    m = re.search(r'^#include\s*([<"])([^>"]*?)podbot_manager\.h([>"])[ \t]*\r?\n', t, re.M)
    if m:
        t = t[:m.end()] + '#include %s%spodbot_human.inl%s\n' % (m.group(1), m.group(2), m.group(3)) + t[m.end():]
    else:
        m = re.search(r'^#include\s*"css_mod\.h"[ \t]*\r?\n', t, re.M); need(m, '#include "css_mod.h"')
        t = t[:m.end()] + '#include "../../podbot/podbot_human.inl"\n' + t[m.end():]
    notes.append('css_mod: include')

a = 'if (m_teammoney[static_cast<int>(myteam)] < GetCSSModSettings()->GetTeamEcoLimit())'
if 'EcoActive() && m_teammoney' not in t:
    need(a in t, 'team eco limit check'); t = t.replace(a, 'if (!podbot::human::EcoActive() && m_teammoney[static_cast<int>(myteam)] < GetCSSModSettings()->GetTeamEcoLimit())', 1); notes.append('css_mod: team eco limit bypass')
a = 'if (money < GetCSSModSettings()->GetEcoLimit())'
if 'EcoActive() && money' not in t:
    need(a in t, 'bot eco limit check'); t = t.replace(a, 'if (!podbot::human::EcoActive() && money < GetCSSModSettings()->GetEcoLimit())', 1); notes.append('css_mod: bot eco limit bypass')

if 'podbot::human::EcoBuy' not in t:
    m = re.search(r'if \(podbuy && plan\.jasonMode\)\s*\{\s*return;\s*\}[ \t]*\r?\n', t); need(m, 'jasonMode check (is podbot_navbot.patch applied?)')
    t = t[:m.end()] + '\n\tif (podbuy && podbot::human::EcoBuy(bot, money))\n\t{\n\t\treturn;\n\t}\n' + t[m.end():]; notes.append('css_mod: eco buy hook')

if 'podbot::human::EcoOnRoundEnd' not in t:
    m = re.search(r'std::strcmp\(name, "round_end"\) == 0\)\s*\{[ \t]*\r?\n', t); need(m, 'round_end event branch')
    t = t[:m.end()] + '\t\t\tpodbot::human::EcoOnRoundEnd(event->GetInt("winner"), event->GetInt("reason"));\n' + t[m.end():]; notes.append('css_mod: round_end hook')

if 'podbot::human::EcoOnBombPlanted' not in t:
    m = re.search(r'^([ \t]*)OnBombPlanted\(event->GetInt\("userid"\)\);', t, re.M); need(m, 'bomb_planted event branch')
    t = t[:m.start()] + m.group(1) + 'podbot::human::EcoOnBombPlanted();\n' + t[m.start():]; notes.append('css_mod: bomb_planted hook')

if 'podbot::human::EcoOnRoundStart' not in t:
    m = re.search(r'if \(numplayers\[ctidx\] > 0\)\s*\{[^}]*\}[ \t]*\r?\n', t); need(m, 'end of the team money block in OnRoundStart')
    t = t[:m.end()] + '\n\tpodbot::human::EcoOnRoundStart();\n' + t[m.end():]; notes.append('css_mod: round start hook')
out[CSS] = t

# ---------------- write
shutil.copyfile(os.path.join(HERE, 'podbot_human.inl'), INL)
shutil.copyfile(os.path.join(HERE, 'podbot_look.inl'), INL_LOOK)
for p, text in out.items(): write(p, text)
print('copied', INL)
print('\n'.join(' - ' + n for n in notes) if notes else 'already installed, nothing to change')
