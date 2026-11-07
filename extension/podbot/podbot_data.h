// podbot_data.h - PODBot-style data layer for the CS:S remake.
// Standalone C++17 (no Source SDK dependency) so it can be unit tested anywhere
// and later dropped into the NavBot extension.
#pragma once
#include <array>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace podbot {

constexpr int kNumWeapons = 26;
constexpr size_t kMaxChatLen = 127;   // Source chat limit
constexpr size_t kMaxNameLen = 31;    // MAX_PLAYER_NAME_LENGTH (32) minus terminator

// Weapon index -> CS buy alias, in PODBot's numbering (botweapons.cfg).
extern const char* const kWeaponNames[kNumWeapons];

enum class Personality { Normal = 1, Aggressive = 2, Defensive = 3 };

class Rng {
public:
	explicit Rng(unsigned seed = std::random_device{}()) : m_(seed) {}
	int Int(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(m_); }
	float Float(float lo, float hi) { return lo >= hi ? lo : std::uniform_real_distribution<float>(lo, hi)(m_); }
	size_t Index(size_t n) { return n ? std::uniform_int_distribution<size_t>(0, n - 1)(m_) : 0; }
private:
	std::mt19937 m_;
};

// Debug categories for "pb debug" / pb_debug: aim (text above each bot), events (console and log lines for decisions).
constexpr unsigned kDebugAim = 1, kDebugEvents = 2, kDebugAll = 3;
// "aim events", "all", "off" (also "none" and "0") -> flags. Unknown words are ignored.
unsigned ParseDebugFlags(const std::string& words);

// Player name for chat: drops Source's "(1)" duplicate prefix and, for PODBot names, the [POD]/[P*D]/[P0D] prefix and
// the "(skill)" suffix. "(1)[P*D] Jack_Nicholson (100)" becomes "Jack_Nicholson".
std::string CleanPlayerName(const std::string& name);

// ---- podbot.cfg (pb_* settings and "pb add"-style commands) ----
struct ConsoleCommand { std::string verb; std::vector<std::string> args; };

class Settings {
public:
	bool Load(const std::string& path, std::string* err = nullptr);
	bool Has(const std::string& key) const;
	std::string GetString(const std::string& key, const std::string& def = "") const;
	int GetInt(const std::string& key, int def = 0) const;
	float GetFloat(const std::string& key, float def = 0.0f) const;
	bool GetBool(const std::string& key, bool def = false) const;
	void Set(const std::string& key, const std::string& value) { m_values[key] = value; }
	const std::vector<ConsoleCommand>& Commands() const { return m_cmds; }
private:
	std::map<std::string, std::string> m_values;
	std::vector<ConsoleCommand> m_cmds;
};

// ---- botskill.cfg ----
struct SkillBracket {
	int lo = 0, hi = 0;                    // skill range this bracket covers
	float minDelay = 0, maxDelay = 0;      // reaction time (seconds)
	float minTurn = 0, maxTurn = 0;        // degrees per GoldSrc frame
	float offX = 0, offY = 0, offZ = 0;    // aim offsets (units)
	int headshot = 0, hearThru = 0, seenThru = 0; // percent
};

class SkillTable {
public:
	bool Load(const std::string& path, std::string* err = nullptr);
	const SkillBracket& ForSkill(int skill) const;
	size_t Size() const { return m_b.size(); }
private:
	std::vector<SkillBracket> m_b;
};

// ---- botnames.txt ----
class NameList {
public:
	bool Load(const std::string& path, std::string* err = nullptr);
	size_t Size() const { return m_names.size(); }
	const std::string& Pick(Rng& rng) const { return m_names[rng.Index(m_names.size())]; }
	// detailMode follows pb_detailnames: 0 none, 1 prefix+skill, 2 prefix, 3 skill.
	static std::string Format(const std::string& base, Personality p, int skill, int detailMode);
private:
	std::vector<std::string> m_names;
};

// ---- botweapons.cfg ----
using WeaponRow = std::array<int, kNumWeapons>;

class WeaponTables {
public:
	bool Load(const std::string& path, std::string* err = nullptr);
	// team: 0 = Terrorist, 1 = Counter-Terrorist. table: "STANDARD" or "AS".
	bool CanBuy(const std::string& table, int weapon, int team) const;
	// Weapon ids ordered from least to most desired for this personality.
	const WeaponRow& Ranking(Personality p) const;
private:
	std::map<std::string, WeaponRow> m_rows;
};

// ---- botchat.txt ----
struct ChatContext {
	std::string topFragger, mapName, timeLeft, replyTo, aliveMate, lastVictim;
};
struct ReplyGroup { std::vector<std::string> keywords, answers; };

class ChatDB {
public:
	bool Load(const std::string& path, std::string* err = nullptr);
	const std::vector<std::string>& Killed() const { return m_killed; }
	const std::vector<std::string>& BombPlant() const { return m_bomb; }
	const std::vector<std::string>& DeadChat() const { return m_dead; }
	const std::vector<std::string>& Unknown() const { return m_unknown; }
	const std::vector<ReplyGroup>& Replies() const { return m_replies; }

	std::string PickKilled(Rng& r, const ChatContext& c) const { return Pick(m_killed, r, c); }
	std::string PickBombPlant(Rng& r, const ChatContext& c) const { return Pick(m_bomb, r, c); }
	std::string PickDeadChat(Rng& r, const ChatContext& c) const { return Pick(m_dead, r, c); }
	// Answer to something a player typed: first matching keyword group, else [UNKNOWN].
	std::string Reply(const std::string& playerText, Rng& r, const ChatContext& c) const;
	static std::string Expand(const std::string& tmpl, const ChatContext& c);
private:
	static std::string Pick(const std::vector<std::string>& v, Rng& r, const ChatContext& c);
	std::vector<std::string> m_killed, m_bomb, m_dead, m_unknown;
	std::vector<ReplyGroup> m_replies;
};

// ---- hints/<map>.hint (camp, goal, sniper and rescue spots converted from PODBot waypoints) ----
struct Hint {
	std::string type;        // "camp", "goal", "sniper", "rescue"
	float pos[3] = { 0, 0, 0 };   // feet position
	int team = 0;            // 0 any, 2 T, 3 CT
	float look[4] = { 0, 0, 0, 0 };
};

class HintTable {
public:
	// Returns false when the file does not exist or is unreadable (a map without hints is normal).
	bool Load(const std::string& path, std::string* err = nullptr);
	size_t Count(const std::string& type) const;
	// Random hint of this type, within maxDist (2D) of (x, y), usable by this team. False if none.
	bool Pick(const std::string& type, float x, float y, float maxDist, int team, Rng& rng, Hint& out) const;
private:
	std::vector<Hint> m_hints;
};

} // namespace podbot
