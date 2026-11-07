#include NAVBOT_PCH_FILE
#include "podbot_data.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>

namespace podbot {

const char* const kWeaponNames[kNumWeapons] = {
	"knife", "usp", "glock", "deagle", "p228", "elite", "fiveseven",
	"m3", "xm1014", "mp5navy", "tmp", "p90", "mac10", "ump45",
	"ak47", "sg552", "m4a1", "galil", "famas", "aug",
	"scout", "awp", "g3sg1", "sg550", "m249", "shield" };

namespace {

std::string Trim(const std::string& s)
{
	size_t b = 0, e = s.size();
	while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
	while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
	return s.substr(b, e - b);
}

std::string Upper(std::string s)
{
	for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

std::string Lower(std::string s)
{
	for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

// Reads a text file as lines; handles CRLF. Returns false if it cannot be opened.
bool ReadLines(const std::string& path, std::vector<std::string>& out, std::string* err)
{
	std::ifstream f(path);
	if (!f) { if (err) *err = "cannot open " + path; return false; }
	std::string line;
	while (std::getline(f, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		out.push_back(line);
	}
	return true;
}

bool IsSkippable(const std::string& trimmed) { return trimmed.empty() || trimmed[0] == '#'; }

// Splits on whitespace; a "quoted string" is one token (quotes removed).
std::vector<std::string> Tokenize(const std::string& line)
{
	std::vector<std::string> t;
	size_t i = 0, n = line.size();
	while (i < n) {
		while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
		if (i >= n) break;
		if (line[i] == '"') {
			size_t j = line.find('"', i + 1);
			if (j == std::string::npos) j = n;
			t.push_back(line.substr(i + 1, j - i - 1));
			i = j + 1;
		} else {
			size_t j = i;
			while (j < n && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
			t.push_back(line.substr(i, j - i));
			i = j;
		}
	}
	return t;
}

std::vector<std::string> ExtractQuoted(const std::string& line)
{
	std::vector<std::string> out;
	size_t i = 0;
	while ((i = line.find('"', i)) != std::string::npos) {
		size_t j = line.find('"', i + 1);
		if (j == std::string::npos) break;
		out.push_back(Upper(line.substr(i + 1, j - i - 1)));
		i = j + 1;
	}
	return out;
}

std::string Truncate(std::string s) { if (s.size() > kMaxChatLen) s.resize(kMaxChatLen); return s; }

bool AllDigits(const std::string& s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); }

} // namespace

unsigned ParseDebugFlags(const std::string& words)
{
	unsigned flags = 0;
	std::string word;
	for (size_t i = 0; i <= words.size(); ++i) {
		const char c = i < words.size() ? words[i] : ' ';
		if (c == ' ' || c == ',' || c == '\t') {
			word = Lower(word);
			if (word == "all") flags = kDebugAll;
			else if (word == "aim") flags |= kDebugAim;
			else if (word == "event" || word == "events") flags |= kDebugEvents;
			else if (word == "off" || word == "none" || word == "0") flags = 0;
			word.clear();
		} else {
			word += c;
		}
	}
	return flags;
}

std::string CleanPlayerName(const std::string& name)
{
	std::string s = Trim(name);
	if (s.size() > 2 && s[0] == '(') {                       // "(1)Name": Source adds this when two players share a name
		size_t close = s.find(')');
		if (close != std::string::npos && AllDigits(s.substr(1, close - 1))) s = Trim(s.substr(close + 1));
	}
	bool pod = false;
	for (const char* prefix : { "[POD]", "[P*D]", "[P0D]" }) {
		if (s.rfind(prefix, 0) == 0) { s = Trim(s.substr(std::string(prefix).size())); pod = true; break; }
	}
	if (pod && s.size() > 3 && s.back() == ')') {            // "Name (100)": the skill shown by pb_detailnames
		size_t open = s.rfind(" (");
		if (open != std::string::npos && AllDigits(s.substr(open + 2, s.size() - open - 3))) s = Trim(s.substr(0, open));
	}
	return s.empty() ? Trim(name) : s;
}

// ------------------------------------------------------------------ Settings
bool Settings::Load(const std::string& path, std::string* err)
{
	std::vector<std::string> lines;
	if (!ReadLines(path, lines, err)) return false;
	m_values.clear();   // a reload replaces the old settings and "pb add" lines
	m_cmds.clear();
	for (const auto& raw : lines) {
		std::string line = Trim(raw);
		if (IsSkippable(line)) continue;
		auto tok = Tokenize(line);
		if (tok.empty()) continue;
		std::string head = Lower(tok[0]);
		if (head.rfind("pb_", 0) == 0) {
			m_values[head] = tok.size() > 1 ? tok[1] : "";
		} else if (head == "pb" && tok.size() > 1) {
			ConsoleCommand c;
			c.verb = Lower(tok[1]);
			c.args.assign(tok.begin() + 2, tok.end());
			m_cmds.push_back(std::move(c));
		}
		// anything else (e.g. "bind") is client-side and ignored
	}
	return true;
}

bool Settings::Has(const std::string& key) const { return m_values.count(Lower(key)) > 0; }

std::string Settings::GetString(const std::string& key, const std::string& def) const
{
	auto it = m_values.find(Lower(key));
	return it == m_values.end() ? def : it->second;
}

int Settings::GetInt(const std::string& key, int def) const
{
	auto it = m_values.find(Lower(key));
	return (it == m_values.end() || it->second.empty()) ? def : std::atoi(it->second.c_str());
}

float Settings::GetFloat(const std::string& key, float def) const
{
	auto it = m_values.find(Lower(key));
	return (it == m_values.end() || it->second.empty()) ? def : std::strtof(it->second.c_str(), nullptr);
}

bool Settings::GetBool(const std::string& key, bool def) const
{
	auto it = m_values.find(Lower(key));
	if (it == m_values.end()) return def;
	std::string v = Lower(it->second);
	return v == "1" || v == "true" || v == "yes" || v == "on";
}

// --------------------------------------------------------------- SkillTable
bool SkillTable::Load(const std::string& path, std::string* err)
{
	std::vector<std::string> lines;
	if (!ReadLines(path, lines, err)) return false;
	m_b.clear();
	for (const auto& raw : lines) {
		std::string line = Trim(raw);
		if (IsSkippable(line)) continue;
		auto tok = Tokenize(line);
		if (tok.size() < 2) continue;
		std::string key = Upper(tok[0]);
		float v = std::strtof(tok[1].c_str(), nullptr);
		if (key == "MIN_DELAY") m_b.emplace_back();   // each bracket starts with MIN_DELAY
		if (m_b.empty()) continue;
		SkillBracket& b = m_b.back();
		if (key == "MIN_DELAY") b.minDelay = v;
		else if (key == "MAX_DELAY") b.maxDelay = v;
		else if (key == "MIN_TURNSPEED") b.minTurn = v;
		else if (key == "MAX_TURNSPEED") b.maxTurn = v;
		else if (key == "AIM_OFFS_X") b.offX = v;
		else if (key == "AIM_OFFS_Y") b.offY = v;
		else if (key == "AIM_OFFS_Z") b.offZ = v;
		else if (key == "HEADSHOT_ALLOW") b.headshot = static_cast<int>(v);
		else if (key == "HEAR_SHOOTTHRU") b.hearThru = static_cast<int>(v);
		else if (key == "SEEN_SHOOTTHRU") b.seenThru = static_cast<int>(v);
	}
	if (m_b.size() != 6) {
		if (err) *err = "botskill.cfg: expected 6 skill brackets, found " + std::to_string(m_b.size());
		return false;
	}
	static const int his[6] = { 19, 39, 59, 79, 99, 100 };
	for (size_t i = 0; i < 6; ++i) { m_b[i].lo = i ? his[i - 1] + 1 : 0; m_b[i].hi = his[i]; }
	return true;
}

const SkillBracket& SkillTable::ForSkill(int skill) const
{
	static const SkillBracket empty;
	if (m_b.empty()) return empty;
	skill = std::max(0, std::min(100, skill));
	for (const auto& b : m_b) if (skill <= b.hi) return b;
	return m_b.back();
}

// ---------------------------------------------------------------- NameList
bool NameList::Load(const std::string& path, std::string* err)
{
	std::vector<std::string> lines;
	if (!ReadLines(path, lines, err)) return false;
	m_names.clear();
	for (const auto& raw : lines) {
		std::string line = Trim(raw);
		if (!IsSkippable(line)) m_names.push_back(line);
	}
	if (m_names.size() < 9) {
		if (err) *err = "botnames.txt: need at least 9 names";
		return false;
	}
	return true;
}

std::string NameList::Format(const std::string& base, Personality p, int skill, int mode)
{
	const char* prefix = p == Personality::Aggressive ? "[P*D]" : p == Personality::Defensive ? "[P0D]" : "[POD]";
	std::string pre, suf;
	if (mode == 1 || mode == 2) pre = std::string(prefix) + " ";
	if (mode == 1 || mode == 3) suf = " (" + std::to_string(skill) + ")";
	size_t used = pre.size() + suf.size();
	size_t room = kMaxNameLen > used ? kMaxNameLen - used : 0;
	return pre + Trim(base.substr(0, room)) + suf;
}

// ----------------------------------------------------------- WeaponTables
bool WeaponTables::Load(const std::string& path, std::string* err)
{
	std::vector<std::string> lines;
	if (!ReadLines(path, lines, err)) return false;
	m_rows.clear();
	std::string section;
	for (const auto& raw : lines) {
		std::string line = Trim(raw);
		if (IsSkippable(line)) continue;
		if (line.front() == '[' && line.back() == ']') { section = Upper(line.substr(1, line.size() - 2)); continue; }
		if (section.empty()) continue;
		WeaponRow row{};
		int n = 0;
		size_t pos = 0;
		while (pos <= line.size()) {
			size_t comma = line.find(',', pos);
			std::string cell = Trim(line.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos));
			if (n < kNumWeapons) row[n] = std::atoi(cell.c_str());
			++n;
			if (comma == std::string::npos) break;
			pos = comma + 1;
		}
		if (n != kNumWeapons) {
			if (err) *err = "botweapons.cfg [" + section + "]: expected 26 entries, found " + std::to_string(n);
			return false;
		}
		m_rows[section] = row;
	}
	for (const char* need : { "STANDARD", "NORMAL", "AGRESSIVE", "DEFENSIVE" }) {
		if (!m_rows.count(need)) { if (err) *err = std::string("botweapons.cfg: missing section [") + need + "]"; return false; }
	}
	for (const char* pri : { "NORMAL", "AGRESSIVE", "DEFENSIVE" }) {
		std::set<int> seen(m_rows[pri].begin(), m_rows[pri].end());
		if (seen.size() != kNumWeapons || *seen.begin() != 0 || *seen.rbegin() != kNumWeapons - 1) {
			if (err) *err = std::string("botweapons.cfg [") + pri + "]: not a permutation of 0..25";
			return false;
		}
	}
	return true;
}

bool WeaponTables::CanBuy(const std::string& table, int weapon, int team) const
{
	if (weapon < 0 || weapon >= kNumWeapons) return false;
	auto it = m_rows.find(Upper(table));
	if (it == m_rows.end()) it = m_rows.find("STANDARD");
	if (it == m_rows.end()) return false;
	int v = it->second[weapon];
	return v == 2 || v == team;
}

const WeaponRow& WeaponTables::Ranking(Personality p) const
{
	const char* key = p == Personality::Aggressive ? "AGRESSIVE" : p == Personality::Defensive ? "DEFENSIVE" : "NORMAL";
	return m_rows.at(key);
}

// ------------------------------------------------------------------ ChatDB
bool ChatDB::Load(const std::string& path, std::string* err)
{
	std::vector<std::string> lines;
	if (!ReadLines(path, lines, err)) return false;
	m_killed.clear(); m_bomb.clear(); m_dead.clear(); m_unknown.clear(); m_replies.clear();
	std::string section;
	for (const auto& raw : lines) {
		std::string line = Trim(raw);
		if (IsSkippable(line)) continue;
		if (line.front() == '[' && line.back() == ']') { section = Upper(line.substr(1, line.size() - 2)); continue; }
		if (section == "REPLIES") {
			if (line.rfind("@KEY", 0) == 0) { m_replies.push_back({ ExtractQuoted(line), {} }); }
			else if (!m_replies.empty()) m_replies.back().answers.push_back(line);
		}
		else if (section == "KILLED") m_killed.push_back(line);
		else if (section == "BOMBPLANT") m_bomb.push_back(line);
		else if (section == "DEADCHAT") m_dead.push_back(line);
		else if (section == "UNKNOWN") m_unknown.push_back(line);
	}
	if (m_killed.empty() || m_bomb.empty() || m_dead.size() < 9) {
		if (err) *err = "botchat.txt: [KILLED]/[BOMBPLANT] need >=1 line and [DEADCHAT] >=9";
		return false;
	}
	return true;
}

std::string ChatDB::Expand(const std::string& t, const ChatContext& c)
{
	std::string out;
	for (size_t i = 0; i < t.size(); ++i) {
		if (t[i] == '%' && i + 1 < t.size()) {
			const std::string* rep = nullptr;
			switch (t[i + 1]) {
			case 'f': rep = &c.topFragger; break;
			case 'm': rep = &c.mapName; break;
			case 'r': rep = &c.timeLeft; break;
			case 's': rep = &c.replyTo; break;
			case 't': rep = &c.aliveMate; break;
			case 'v': rep = &c.lastVictim; break;
			}
			if (rep) { out += *rep; ++i; continue; }
		}
		out += t[i];
	}
	return Truncate(out);
}

std::string ChatDB::Pick(const std::vector<std::string>& v, Rng& r, const ChatContext& c)
{
	return v.empty() ? std::string() : Expand(v[r.Index(v.size())], c);
}

std::string ChatDB::Reply(const std::string& playerText, Rng& r, const ChatContext& c) const
{
	std::string text = Upper(playerText);
	for (const auto& g : m_replies) {
		if (g.answers.empty()) continue;
		for (const auto& kw : g.keywords)
			if (!kw.empty() && text.find(kw) != std::string::npos)
				return Expand(g.answers[r.Index(g.answers.size())], c);
	}
	return Pick(m_unknown, r, c);
}

// --------------------------------------------------------------- HintTable
bool HintTable::Load(const std::string& path, std::string* err)
{
	std::vector<std::string> lines;
	m_hints.clear();
	if (!ReadLines(path, lines, err)) return false;
	for (const auto& raw : lines) {
		std::string line = Trim(raw);
		if (IsSkippable(line)) continue;
		auto tok = Tokenize(line);
		if (tok.size() < 5) continue;
		Hint h;
		h.type = Lower(tok[0]);
		for (int i = 0; i < 3; ++i) h.pos[i] = std::strtof(tok[1 + i].c_str(), nullptr);
		h.team = std::atoi(tok[4].c_str());
		for (int i = 0; i < 4 && 5 + static_cast<size_t>(i) < tok.size(); ++i) h.look[i] = std::strtof(tok[5 + i].c_str(), nullptr);
		m_hints.push_back(std::move(h));
	}
	return true;
}

size_t HintTable::Count(const std::string& type) const
{
	size_t n = 0;
	for (const auto& h : m_hints) if (h.type == type) ++n;
	return n;
}

bool HintTable::Pick(const std::string& type, float x, float y, float maxDist, int team, Rng& rng, Hint& out) const
{
	std::vector<const Hint*> candidates;
	for (const auto& h : m_hints) {
		if (h.type != type) continue;
		if (h.team != 0 && h.team != team) continue;
		float dx = h.pos[0] - x, dy = h.pos[1] - y;
		if (dx * dx + dy * dy <= maxDist * maxDist) candidates.push_back(&h);
	}
	if (candidates.empty()) return false;
	out = *candidates[rng.Index(candidates.size())];
	return true;
}

} // namespace podbot
