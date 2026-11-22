
#include "podbot_human.inl"
#include <iostream>
using namespace podbot::human;
static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::cout << "  FAIL: " << msg << "\n"; ++fails; } } while (0)
static std::string join(const std::vector<std::string>& v) { std::string s; for (size_t i = 0; i < v.size(); ++i) { s += (i ? ", " : "") + v[i]; } return s.empty() ? "(nothing)" : s; }
int main()
{
	EcoCfg c800; c800.startMoney = 800; EcoCfg c1000; c1000.startMoney = 1000;

	std::cout << "-- loss bonus / fidget windows\n";
	CHECK(LossBonus(c800,1)==1400 && LossBonus(c800,2)==1900 && LossBonus(c800,3)==2400 && LossBonus(c800,4)==2900 && LossBonus(c800,5)==3400 && LossBonus(c800,9)==3400, "loss ladder");
	CHECK(std::fabs(StopTime(4.0f,1.0f,0.15f)-2.85f)<1e-4 && std::fabs(StopTime(5.5f,1.0f,0.15f)-4.35f)<1e-4 && std::fabs(StopTime(7.5f,1.0f,0.15f)-6.35f)<1e-4 && std::fabs(StopTime(8.0f,1.0f,0.15f)-6.85f)<1e-4, "stop times (draw 1.0 + buffer 0.15)");

	std::cout << "-- team decisions\n";
	auto d = TeamShouldEco(c800, true, {1400,1400,1400,1850,2200}, 2, false);
	std::cout << "  [800] T r2 no plant: " << ModeName(d.mode) << "\n"; CHECK(d.mode == EcoMode::Eco, "800 r2 no plant = eco");
	d = TeamShouldEco(c800, true, {1400,1400,1400,1400,1400}, 2, true);
	std::cout << "  [800] T r2 plant: " << ModeName(d.mode) << "\n"; CHECK(d.mode == EcoMode::Force, "plant forces");
	d = TeamShouldEco(c1000, true, {1850,1850,1550,1550,1600}, 2, true); CHECK(d.mode == EcoMode::Force, "1000 plant forces");
	d = TeamShouldEco(c1000, true, {1900,1900,1900,1900,1900}, 2, false); CHECK(d.mode == EcoMode::Force, "1000 start forces at 1800+");
	d = TeamShouldEco(c800,  true, {1900,1900,1900,1900,1900}, 2, false); CHECK(d.mode == EcoMode::Eco, "800 start does not force");
	d = TeamShouldEco(c800, true, {3000,3000,3000,3000,3000}, 3, false); CHECK(d.mode == EcoMode::Eco, "no deco: avg 3000 with nobody at full buy = eco");
	d = TeamShouldEco(c800, true, {4200,4200,4200,2000,2000}, 3, false); CHECK(d.mode == EcoMode::Full, "3 of 5 can full-buy = full buy (deco rule gone)");
	CHECK(DetectStartMoney(800.0)==800 && DetectStartMoney(1000.0)==1000 && DetectStartMoney(850.0)==800 && DetectStartMoney(950.0)==1000, "startmoney detect");
	d = TeamShouldEco(c800, false, {4800,4800,1000,1000,1000}, 4, false); CHECK(d.mode == EcoMode::Eco, "2/5 can buy = eco");
	d = TeamShouldEco(c800, true, {5000,5000,5000,5000,5000}, 4, false); CHECK(d.mode == EcoMode::Full, "all rich = full");
	d = TeamShouldEco(c800, true, {800,800,800,800,800}, 1, false); CHECK(d.mode == EcoMode::Pistol, "round 1 = pistol");

	std::cout << "-- player buys\n";
	auto b = PlayerEcoBuy(c800, true, 2200, 1, true, false);
	CHECK(b.fullSave && b.projectedBank == 4100, "2200 + 1900 = 4100 -> save");
	b = PlayerEcoBuy(c1000, true, 1850, 1, true, false); CHECK(b.fullSave && b.projectedBank == 3750, "1850 save, 3750");
	b = PlayerEcoBuy(c800, true, 3000, 2, true, false); std::cout << "  T $3000 after 2 losses: " << join(b.items) << " bank " << b.projectedBank << "\n";
	CHECK(!b.fullSave && b.items.size() >= 1 && b.items[0] == "deagle", "surplus buys a deagle");
	auto f = ForceBuyItems(c800, 1300); CHECK(f.size() == 2 && f[0] == "deagle" && f[1] == "vest", "1300 = deagle + kevlar");
	f = ForceBuyItems(c800, 1500); CHECK(f.size() == 3 && f[2] == "flashbang", "1500 adds a flash");
	f = ForceBuyItems(c800, 700); CHECK(f.size() == 1 && f[0] == "deagle", "700 = deagle only");
	f = ForceBuyItems(c800, 600); CHECK(f.empty(), "600 = nothing");
	CHECK(ItemPrice(c800, "vest") == 650 && ItemPrice(c800, "deagle (drop to star aimer)") == 650 && std::string(BuyAlias("deagle (drop to star aimer)")) == "deagle", "prices / alias");

	std::cout << "-- round tracker\n";
	RoundTracker t; t.halfRounds = 15;
	CHECK(t.RoundInHalf() == 1, "starts at round 1");
	t.OnPlant(); t.OnRoundEnd(3, 9);                         // T plants in the pistol round, CT wins (T lost)
	CHECK(t.pistolPlant && t.losses[2] == 1 && t.losses[3] == 0 && t.RoundInHalf() == 2, "pistol plant remembered for round 2");
	t.OnRoundEnd(3, 8);                                      // T loses round 2 too
	CHECK(!t.pistolPlant && t.losses[2] == 2 && t.RoundInHalf() == 3, "pistol plant cleared after round 2");
	t.OnRoundEnd(2, 9);                                      // T wins
	CHECK(t.losses[2] == 0 && t.losses[3] == 1, "a win resets the streak");
	t.OnRoundEnd(0, 10);                                     // draw: no result
	CHECK(t.RoundInHalf() == 4, "draw does not count");
	for (int i = 0; i < 12; ++i) { t.OnRoundEnd(3, 8); }     // reach 15 rounds
	CHECK(t.roundsPlayed == 15 && t.RoundInHalf() == 1 && t.losses[2] == 0, "half time resets");
	t.OnRoundEnd(3, 8); t.OnRoundEnd(0, 16);                 // game restart
	CHECK(t.roundsPlayed == 0 && t.RoundInHalf() == 1, "restart resets");

	std::cout << (fails ? "SOME CHECKS FAILED" : "ALL CHECKS PASSED") << "\n";
	return fails;
}
