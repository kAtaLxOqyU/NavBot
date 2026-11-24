#!/usr/bin/env python3
"""Installs the CS:S human-behaviour edition into the NavBot fork: eco, fidget, look-around, fire control (burst, counter-strafe, recoil, AWP) and human aim. Run from the repo root AFTER podbot_navbot.patch and podbot_fix8.patch.
   Edits: podbot_manager.cpp (include + calls + human aim swap), podbot_commands.cpp (console settings), podbot.cfg (settings), css_mod.cpp (eco hooks).
   All edits are computed first; if any anchor is missing nothing is written. Safe to run twice."""
import os, re, shutil, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MGR = 'extension/podbot/podbot_manager.cpp'
CMD = 'extension/podbot/podbot_commands.cpp'
CSS = 'extension/mods/css/css_mod.cpp'
CFG = 'configs/podbot/podbot.cfg'
INL = 'extension/podbot/podbot_human.inl'

FIDGET = ['pb_fidget', 'pb_fidget_freeze_rate', 'pb_fidget_run_rate', 'pb_fidget_draw_time', 'pb_fidget_buffer', 'pb_fidget_jiggle', 'pb_fidget_jiggle_after']
LOOK = ['pb_look', 'pb_look_rate', 'pb_look_min_speed', 'pb_look_debug']
FIRE = ['pb_fire', 'pb_fire_burst', 'pb_fire_trigger_ms', 'pb_fire_cstrafe', 'pb_fire_cstrafe_ms', 'pb_fire_cstrafe_speed', 'pb_fire_recoil', 'pb_fire_recoil_max', 'pb_fire_awp_qq', 'pb_fire_awp_scope_min', 'pb_fire_debug']
AIMH = ['pb_aimh', 'pb_aimh_archetype', 'pb_aimh_overshoot_chance', 'pb_aimh_onset_ms', 'pb_aimh_tremor', 'pb_aimh_jitter', 'pb_aimh_level_down', 'pb_aimh_level_up', 'pb_aimh_debug']
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
CFG_FIRE = """
# CS:S human-behaviour edition: fire control (measured from the demos). Burst: median 2 shots, 12% of bursts 6+; far = taps, close = sprays.
pb_fire 1
pb_fire_burst 1
# median wait between the crosshair settling and the first shot (ms)
pb_fire_trigger_ms 110
# chance to tap the opposite key and wait pb_fire_cstrafe_ms before shooting while moving faster than pb_fire_cstrafe_speed (u/s). Snipers: 90%.
pb_fire_cstrafe 0.45
pb_fire_cstrafe_ms 130
pb_fire_cstrafe_speed 90
# share of the weapon recoil the view is pulled down by (measured 0.51), capped at pb_fire_recoil_max degrees
pb_fire_recoil 0.5
pb_fire_recoil_max 4.5
# AWP / Scout: quick switch to the pistol and back after every shot (cancels the automatic re-zoom); scope only beyond this many units
pb_fire_awp_qq 1
pb_fire_awp_scope_min 350
# 1 = print burst pauses, trigger delays and counter-strafes to the server console
pb_fire_debug 0
"""
CFG_AIMH = """
# CS:S human-behaviour edition: human aim on top of the spring. Read when a bot is created (pb_aimh); the rest is live.
pb_aimh 1
# spread of bot personalities (0 = all bots turn alike); 0.16 = the spread measured between 20 players
pb_aimh_archetype 0.16
# share of flicks that overshoot (6-15%); the rest land clean
pb_aimh_overshoot_chance 0.2
# median reaction hold (ms) before a turn of more than 15 degrees toward an enemy
pb_aimh_onset_ms 70
# hold tremor in degrees, per-flick speed variation
pb_aimh_tremor 0.15
pb_aimh_jitter 0.08
# relaxed looks (walking, idle) keep the view near level: at most this many degrees below / above level
pb_aimh_level_down 4
pb_aimh_level_up 8
# 1 = print every flick of 20+ degrees to the server console
pb_aimh_debug 0
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
INL_FIRE = 'extension/podbot/podbot_fire.inl'
INL_AIMH = 'extension/podbot/podbot_aim_human.inl'

CALL_FIRE = ("\t\tfire::UpdateFire(now,\n"
             "\t\t\t[this](const char* n, float d) { return m_settings.GetFloat(n, d); },\n"
             "\t\t\t[this](CCSSBot* b, const char* s) { DebugLog(b, \"%s\", s); });\n")
AIM_SWAP = ("{indent}if (human::detail::Get(\"pb_aimh\", 1.0f) > 0.5f)\n{indent}{{\n"
            "{indent}\t{recv}SetAimOverride(std::make_shared<aimh::HumanAimOverride>({args}));\n{indent}}}\n"
            "{indent}else\n{indent}{{\n"
            "{indent}\t{recv}SetAimOverride(std::make_shared<PodAimOverride>({args}));\n{indent}}}\n")

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
for inc, after in (('podbot_fire.inl', 'podbot_look.inl'), ('podbot_aim_human.inl', 'podbot_fire.inl')):
    if inc not in s:
        a = '#include "%s"' % after
        if a not in s: die('include anchor %s not found in %s' % (a, MGR))
        s = s.replace(a, a + '\n#include "%s"' % inc, 1); notes.append('manager: include ' + inc)
if 'fire::UpdateFire' not in s:
    m = re.search(r'look::UpdateLook\(now,.*?\}\);[ \t]*\r?\n', s, re.S)
    if not m: die('look::UpdateLook call not found')
    s = s[:m.end()] + CALL_FIRE + s[m.end():]; notes.append('manager: fire control call')
if 'aimh::HumanAimOverride' not in s:
    # works for any arguments and any receiver: <receiver>SetAimOverride(std::make_shared<PodAimOverride>(<args>));
    m = re.search(r'^([ \t]*)([^\n;]*?)SetAimOverride\(\s*std::make_shared<PodAimOverride>\((.*?)\)\s*\)\s*;[ \t]*\r?\n', s, re.M | re.S)
    if not m:
        lines = [(n + 1, l.strip()) for n, l in enumerate(s.split('\n')) if 'PodAimOverride' in l or 'SetAimOverride' in l]
        print('Lines in %s that mention the aim override:' % MGR)
        for n, l in lines: print('  %d: %s' % (n, l))
        die('could not find "SetAimOverride(std::make_shared<PodAimOverride>(...))" - send me the lines printed above')
    s = s[:m.start()] + AIM_SWAP.format(indent=m.group(1), recv=m.group(2), args=m.group(3).strip()) + s[m.end():]; notes.append('manager: human aim swap')
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
if 'X(pb_look_debug)' not in c:
    a = 'X(pb_look_min_speed)'
    if a not in c: die('X(pb_look_min_speed) not found in ' + CMD)
    c = c.replace(a, a + ' X(pb_look_debug)', 1); notes.append('commands: pb_look_debug')
if 'X(pb_fire)' not in c:
    a = 'X(pb_fidget_jiggle_after)'
    c = c.replace(a, a + ' \\\n\t' + ' '.join('X(%s)' % n for n in FIRE), 1); notes.append('commands: fire settings')
if 'X(pb_aimh)' not in c:
    a = 'X(pb_fidget_jiggle_after)'
    c = c.replace(a, a + ' \\\n\t' + ' '.join('X(%s)' % n for n in AIMH), 1); notes.append('commands: human aim settings')
if 'X(pb_aimh_level_down)' not in c:
    a = 'X(pb_aimh_debug)'
    if a not in c: die('X(pb_aimh_debug) not found in ' + CMD)
    c = c.replace(a, 'X(pb_aimh_level_down) X(pb_aimh_level_up) ' + a, 1); notes.append('commands: pb_aimh_level_down / _up')
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
if 'pb_fire ' not in g: g = add_after(g, 'pb_fidget_jiggle_after', CFG_FIRE); notes.append('cfg: fire block')
if 'pb_aimh ' not in g: g = add_after(g, 'pb_fidget_jiggle_after', CFG_AIMH); notes.append('cfg: human aim block')
for name, old, new in (('pb_aim_spring_stiffness_x', '13', '10'), ('pb_aim_spring_stiffness_y', '13', '14'), ('pb_aim_damper_coefficient_x', '0.22', '0.75'), ('pb_aim_damper_coefficient_y', '0.22', '0.73')):
    pat = re.compile(r'^(%s[ \t]+)%s(?:\.0+)?([ \t]*(?:#.*)?)$' % (name, re.escape(old)), re.M)
    if pat.search(g): g = pat.sub(lambda m: m.group(1) + new + m.group(2), g, count=1); notes.append('cfg: %s %s -> %s (fitted to the demos)' % (name, old, new))
if 'pb_aimh_level_down' not in g:
    g = add_after(g, 'pb_aimh_jitter', '# relaxed looks (walking, idle) keep the view near level: at most this many degrees below / above level\npb_aimh_level_down 4\npb_aimh_level_up 8\n'); notes.append('cfg: near-level limits')
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
shutil.copyfile(os.path.join(HERE, 'podbot_fire.inl'), INL_FIRE)
shutil.copyfile(os.path.join(HERE, 'podbot_aim_human.inl'), INL_AIMH)
for p, text in out.items(): write(p, text)
print('copied', INL)
print('\n'.join(' - ' + n for n in notes) if notes else 'already installed, nothing to change')
