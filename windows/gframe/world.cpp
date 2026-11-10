#include "world.h"
#include "campaign.h"
#include <cmath>
#include <ctime>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <IrrlichtDevice.h>
#include <IGUIEnvironment.h>
#include <IGUIButton.h>
#include <IGUIComboBox.h>
#include <IGUIEditBox.h>
#include <IGUIListBox.h>
#include <IGUIStaticText.h>
#include <IGUIWindow.h>
#include <IGUITabControl.h>
#include <IGUIFont.h>
#include "config.h"
#include "game.h"
#include "game_config.h"
#include "data_manager.h"
#include "deck_manager.h"
#include "image_manager.h"
#include "file_stream.h"
#include "bufferio.h"
#include "utils.h"
#include "fmt.h"
#include "fx.h"
#include "story_mode.h"
#include "CGUITTFont/CGUITTFont.h"
#include "CGUICustomText/CGUICustomText.h"
#include "ocgapi_constants.h"

namespace ygo {

DuelWorld gWorld;

namespace {

constexpr auto WORLD_FILE = EPRO_TEXT("./story/world/world.json");
constexpr auto PACKS_FILE = EPRO_TEXT("./story/world/packs.json");
constexpr auto RARITY_FILE = EPRO_TEXT("./story/world/rarity.json");
constexpr auto SAVE_FILE = EPRO_TEXT("./story/world/save.json");
constexpr float PI = 3.14159265f;

using irr::video::SColor;
using irr::core::vector2df;

std::wstring W(const nlohmann::json& obj, const char* key, const wchar_t* def = L"") {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_string())
		return def;
	return BufferIO::DecodeUTF8(it->get<std::string>());
}
std::string S(const nlohmann::json& obj, const char* key, const char* def = "") {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_string())
		return def;
	return it->get<std::string>();
}
int I(const nlohmann::json& obj, const char* key, int def) {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_number())
		return def;
	return it->get<int>();
}
bool ReadJson(epro::path_stringview path, nlohmann::json& out) {
	FileStream file{ epro::path_string(path), FileStream::in };
	if(!file.good())
		return false;
	try {
		file >> out;
		return true;
	}
	catch(const std::exception& e) {
		mainGame->PopupMessage(epro::format(L"อ่านไฟล์ไม่สำเร็จ: {}\n{}", Utils::ToUnicodeIfNeeded(path), BufferIO::DecodeUTF8(e.what())));
		return false;
	}
}
std::wstring Num(int64_t v) {
	std::wstring s = epro::to_wstring(v < 0 ? -v : v);
	for(int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3)
		s.insert(static_cast<size_t>(i), L",");
	return v < 0 ? L"-" + s : s;
}
float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
SColor WithAlpha(SColor c, float a) {
	c.setAlpha(static_cast<irr::u32>(std::lround(Clamp01(a) * 255)));
	return c;
}

bool IsThaiCombining(wchar_t c) {
	return c == 0x0E31 || (c >= 0x0E34 && c <= 0x0E3A) || (c >= 0x0E47 && c <= 0x0E4E);
}
// Irrlicht only wraps on spaces: pre-break Thai text with the real font metrics
std::wstring Wrap(const std::wstring& text, irr::gui::IGUIElement* elem) {
	auto* font = mainGame->guiFont;
	if(!font || !elem)
		return text;
	const auto max_width = elem->getAbsolutePosition().getWidth() - mainGame->Scale(12);
	if(max_width <= 0)
		return text;
	std::wstring out, line;
	for(size_t i = 0; i < text.size(); i++) {
		const wchar_t c = text[i];
		if(c == L'\n') {
			out += line;
			out += L'\n';
			line.clear();
			continue;
		}
		line.push_back(c);
		if(static_cast<irr::s32>(font->getDimension(line.data()).Width) <= max_width)
			continue;
		size_t brk = line.rfind(L' ');
		std::wstring rest;
		if(brk != std::wstring::npos && brk > 0) {
			rest = line.substr(brk + 1);
			line.erase(brk);
		} else {
			size_t pos = line.size() - 1;
			while(pos > 0 && IsThaiCombining(line[pos]))
				pos--;
			if(pos == 0)
				continue;
			rest = line.substr(pos);
			line.erase(pos);
		}
		out += line;
		out += L'\n';
		line = rest;
	}
	out += line;
	return out;
}
void SetText(irr::gui::IGUIStaticText* st, const std::wstring& text) {
	st->setText(Wrap(text, st).data());
}

}

// stages: 0 Rookie, 1-5 Bronze 5..1, 6-10 Silver, 11-15 Gold, 16-20 Platinum, 21-25 Diamond, 26 King of Games
namespace {
constexpr int RANK_LAST = 26;
const wchar_t* const RANK_TIERS[] = { L"Rookie", L"Bronze", L"Silver", L"Gold", L"Platinum", L"Diamond", L"King of Games" };
const char* const RANK_POOLS[] = { "rookie", "bronze", "silver", "gold", "platinum", "diamond", "boss" };
const SColor RANK_COLORS[] = { SColor(255, 200, 200, 210), SColor(255, 205, 127, 50), SColor(255, 200, 210, 225), SColor(255, 255, 205, 60),
							   SColor(255, 120, 230, 220), SColor(255, 150, 200, 255), SColor(255, 255, 90, 200) };
}

/* ================================================================== data */

int DuelWorld::Rand(int n) {
	if(n <= 0)
		return 0;
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return static_cast<int>(rng % static_cast<uint32_t>(n));
}

void DuelWorld::Init() {
	rng = static_cast<uint32_t>(std::time(nullptr)) | 1u;
	LoadData();
	LoadSave();
	CreateGUI();
}

void DuelWorld::LoadData() {
	items.clear();
	packs.clear();
	starters.clear();
	rarity_override.clear();
	rarity_cache.clear();
	nlohmann::json j;
	if(ReadJson(WORLD_FILE, j) && j.is_object()) {
		try {
			collection_lock = j.value("collection_lock", true);
			if(j.contains("start")) {
				const auto& st = j["start"];
				start_gems = I(st, "gems", start_gems);
				start_gold = I(st, "gold", start_gold);
				start_items.clear();
				if(st.contains("items"))
					for(const auto& [id, n] : st["items"].items())
						start_items[id] = n.get<int>();
			}
			if(j.contains("pack")) {
				const auto& p = j["pack"];
				pack_size = std::max(1, std::min(10, I(p, "size", pack_size)));
				if(p.contains("rates") && p["rates"].size() == 4)
					for(int i = 0; i < 4; i++)
						rates[i] = p["rates"][i].get<int>();
				last_slot_ur = I(p, "last_slot_ur", last_slot_ur);
			}
			if(j.contains("trader")) {
				const auto& t = j["trader"];
				trader_size = std::max(10, std::min(12, I(t, "size", trader_size)));   // at least 10 (2+ UR)
				if(t.contains("prices") && t["prices"].size() == 4)
					for(int i = 0; i < 4; i++)
						trader_prices[i] = t["prices"][i].get<int>();
			}
			if(j.contains("rewards")) {
				const auto& r = j["rewards"];
				story_first_gems = I(r, "story_first_gems", story_first_gems);
				story_first_gold = I(r, "story_first_gold", story_first_gold);
				story_repeat_gems = I(r, "story_repeat_gems", story_repeat_gems);
				story_repeat_gold = I(r, "story_repeat_gold", story_repeat_gold);
				lose_gold = I(r, "lose_gold", lose_gold);
			}
			if(j.contains("roaming")) {
				const auto& r = j["roaming"];
				roaming_slots = std::max(1, std::min(6, I(r, "slots", roaming_slots)));
				roam_gold_base = I(r, "gold_base", roam_gold_base);
				roam_gold_tier = I(r, "gold_per_tier", roam_gold_tier);
				roam_gems_base = I(r, "gems_base", roam_gems_base);
				roam_gems_tier = I(r, "gems_per_tier", roam_gems_tier);
				roam_card_drop = I(r, "card_drop", roam_card_drop);
				roam_item_drop = I(r, "item_drop", roam_item_drop);
				roam_item_pool.clear();
				if(r.contains("item_pool"))
					for(const auto& id : r["item_pool"])
						roam_item_pool.push_back(id.get<std::string>());
			}
			if(j.contains("items")) {
				for(const auto& it : j["items"]) {
					ItemDef def;
					def.id = S(it, "id");
					def.name = W(it, "name", L"???");
					def.desc = W(it, "desc");
					def.type = S(it, "type");
					def.value = I(it, "value", 0);
					def.pack = S(it, "pack");
					if(!def.id.empty())
						items.push_back(std::move(def));
				}
			}
			login_rewards.clear();
			if(j.contains("login_rewards")) {
				for(const auto& it : j["login_rewards"]) {
					LoginReward r;
					r.gems = I(it, "gems", 0);
					r.gold = I(it, "gold", 0);
					r.item = S(it, "item");
					r.item_n = I(it, "item_n", r.item.empty() ? 0 : 1);
					login_rewards.push_back(std::move(r));
				}
			}
			if(j.contains("starters")) {
				for(const auto& it : j["starters"]) {
					StarterDef def;
					def.name = W(it, "name", L"???");
					def.desc = W(it, "desc");
					def.deck_name = W(it, "deck_name", def.name.data());
					def.file = S(it, "file");
					def.cover = static_cast<uint32_t>(I(it, "cover", 0));
					starters.push_back(std::move(def));
				}
			}
		}
		catch(const std::exception& e) {
			mainGame->PopupMessage(epro::format(L"world.json ผิดรูปแบบ:\n{}", BufferIO::DecodeUTF8(e.what())));
		}
	}
	nlohmann::json r;
	if(ReadJson(RARITY_FILE, r) && r.is_object()) {
		for(const auto& [code, val] : r.items()) {
			if(!val.is_string())
				continue;
			const auto v = val.get<std::string>();
			Rarity rr = v == "UR" ? Rarity::UR : v == "SR" ? Rarity::SR : v == "R" ? Rarity::R : Rarity::N;
			try {
				rarity_override[static_cast<uint32_t>(std::stoul(code))] = rr;
			}
			catch(...) {}
		}
	}
	nlohmann::json p;
	if(ReadJson(PACKS_FILE, p) && p.is_object() && p.contains("packs")) {
		for(const auto& it : p["packs"]) {
			PackDef def;
			def.id = S(it, "id");
			def.name = W(it, "name", L"???");
			def.desc = W(it, "desc");
			def.arc = W(it, "arc");
			def.price = I(it, "price", 100);
			def.cover = static_cast<uint32_t>(I(it, "cover", 0));
			def.cat = S(it, "cat", "main");
			def.tag = W(it, "tag");
			if(it.contains("cards"))
				for(const auto& c : it["cards"]) {
					const auto code = c.get<uint32_t>();
					if(gDataManager->GetCardData(code))
						def.cards.push_back(code);
				}
			if(!def.cards.empty())
				packs.push_back(std::move(def));
		}
	}
	ComputeRarities();
	for(auto& p : packs)
		p.featured = Featured(p.cards, 7);
	LoadExtraData();
	Skills();
}

void DuelWorld::ComputeRarities() {
	for(auto& p : packs) {
		for(auto& v : p.by_rarity)
			v.clear();
		for(auto code : p.cards)
			p.by_rarity[static_cast<int>(RarityOf(code))].push_back(code);
	}
}

uint32_t DuelWorld::Key(uint32_t code) const {
	const auto* data = gDataManager->GetCardData(code);
	if(data && data->IsInArtworkOffsetRange())
		return data->alias;
	return code;
}

int DuelWorld::Owned(uint32_t code) const {
	auto it = save.cards.find(Key(code));
	return it == save.cards.end() ? 0 : it->second;
}

int DuelWorld::OwnedFor(const CardDataC* data) const {
	if(!data)
		return 0;
	return Owned(data->code);
}

Rarity DuelWorld::RarityOf(uint32_t code) const {
	code = Key(code);
	auto ov = rarity_override.find(code);
	if(ov != rarity_override.end())
		return ov->second;
	auto cached = rarity_cache.find(code);
	if(cached != rarity_cache.end())
		return cached->second;
	const auto* d = gDataManager->GetCardData(code);
	Rarity r = Rarity::N;
	if(d) {
		// well known staples
		static const std::set<uint32_t> ur_staples = {
			14558127, 23434538, 24224830, 10045474, 83764718, 12580477, 18144506, 53129443, 44095762, 41420027,
			14532163, 24299458, 25311006, 97268402, 59438930, 27204311, 94145021, 48130397, 35261759, 84211599,
			73628505, 52038441, 65681983, 60643553, 79571449, 55144522, 32807846, 5318639, 81439173, 2295440,
		};
		const bool archetype = !d->setcodes.empty();
		const int lv = static_cast<int>(d->level);
		if(ur_staples.count(code)) {
			r = Rarity::UR;
		} else if(d->type & TYPE_MONSTER) {
			if(d->type & TYPE_LINK) {
				r = lv >= 4 ? Rarity::UR : lv == 3 ? Rarity::SR : Rarity::R;
			} else if(d->type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ)) {
				r = lv >= 8 ? Rarity::UR : lv >= 6 ? Rarity::SR : Rarity::R;
			} else if(lv >= 7) {
				r = d->attack >= 3000 ? Rarity::UR : Rarity::SR;
			} else if(lv >= 5) {
				r = d->attack >= 2400 ? Rarity::SR : Rarity::R;
			} else if(d->type & TYPE_EFFECT) {
				r = (archetype || d->attack >= 1800) ? Rarity::R : Rarity::N;
			} else {
				r = Rarity::N;
			}
		} else if(d->type & TYPE_SPELL) {
			if(d->type & (TYPE_FIELD | TYPE_QUICKPLAY | TYPE_CONTINUOUS | TYPE_RITUAL | TYPE_EQUIP))
				r = Rarity::R;
			else
				r = archetype ? Rarity::R : Rarity::N;
		} else if(d->type & TYPE_TRAP) {
			if(d->type & TYPE_COUNTER)
				r = Rarity::SR;
			else if(d->type & TYPE_CONTINUOUS)
				r = Rarity::R;
			else
				r = archetype ? Rarity::R : Rarity::N;
		}
		// a bit of variety inside archetypes
		if(archetype && r < Rarity::UR && (code % 7) == 3)
			r = static_cast<Rarity>(static_cast<int>(r) + 1);
	}
	rarity_cache[code] = r;
	return r;
}

SColor DuelWorld::RarityColor(Rarity r) {
	switch(r) {
	case Rarity::UR: return SColor(255, 255, 200, 60);
	case Rarity::SR: return SColor(255, 210, 225, 255);
	case Rarity::R: return SColor(255, 90, 170, 255);
	default: return SColor(255, 170, 170, 170);
	}
}

const wchar_t* DuelWorld::RarityName(Rarity r) {
	switch(r) {
	case Rarity::UR: return L"UR";
	case Rarity::SR: return L"SR";
	case Rarity::R: return L"R";
	default: return L"N";
	}
}

std::vector<uint32_t> DuelWorld::ReadYdk(const std::string& path) const {
	std::vector<uint32_t> out;
	FileStream file{ Utils::ToPathString(path), FileStream::in };
	if(!file.good())
		return out;
	std::string line;
	while(std::getline(file, line)) {
		while(!line.empty() && (line.back() == '\r' || line.back() == ' '))
			line.pop_back();
		if(line.empty() || line[0] == '#' || line[0] == '!')
			continue;
		try {
			const auto code = static_cast<uint32_t>(std::stoul(line));
			if(gDataManager->GetCardData(code))
				out.push_back(code);
		}
		catch(...) {}
	}
	return out;
}

/* ================================================================== save */

void DuelWorld::LoadSave() {
	save = Save{};
	nlohmann::json j;
	FileStream file{ SAVE_FILE, FileStream::in };
	if(!file.good())
		return;
	try {
		file >> j;
		save.started = j.value("started", false);
		save.gems = j.value("gems", int64_t{ 0 });
		save.gold = j.value("gold", int64_t{ 0 });
		save.wins = j.value("wins", 0);
		save.losses = j.value("losses", 0);
		save.packs_opened = j.value("packs_opened", 0);
		if(j.contains("items"))
			for(const auto& [id, n] : j["items"].items())
				save.items[id] = n.get<int>();
		if(j.contains("cards"))
			for(const auto& [code, n] : j["cards"].items())
				save.cards[static_cast<uint32_t>(std::stoul(code))] = n.get<int>();
		if(j.contains("trader")) {
			const auto& t = j["trader"];
			save.trader_day = t.value("day", 0u);
			save.trader_seed = t.value("seed", 0);
			if(t.contains("stock"))
				for(const auto& c : t["stock"])
					save.trader_stock.push_back(c.get<uint32_t>());
			if(t.contains("bought"))
				for(const auto& c : t["bought"])
					save.trader_bought.insert(c.get<size_t>());
		}
		if(j.contains("duelists"))
			for(const auto& d : j["duelists"])
				save.duelists.push_back({ d.value("chapter", -1), d.value("tier", 0) });
		if(j.contains("skills"))
			for(const auto& c : j["skills"])
				save.skills.insert(c.get<uint32_t>());
		save.equipped_skill = j.value("equipped_skill", 0u);
		save.free_pack_day = j.value("free_pack_day", 0u);
		save.login_last = j.value("login_last", 0u);
		save.login_count = j.value("login_count", 0);
		save.daily_day = j.value("daily_day", 0u);
		if(j.contains("structs_bought"))
			for(const auto& [id, n] : j["structs_bought"].items())
				save.structs_bought[id] = n.get<int>();
		if(j.contains("stats"))
			for(const auto& [id, n] : j["stats"].items())
				save.stats[id] = n.get<int64_t>();
		if(j.contains("daily_stats"))
			for(const auto& [id, n] : j["daily_stats"].items())
				save.daily_stats[id] = n.get<int64_t>();
		if(j.contains("claimed"))
			for(const auto& c : j["claimed"])
				save.claimed.insert(c.get<std::string>());
		if(j.contains("daily_claimed"))
			for(const auto& c : j["daily_claimed"])
				save.daily_claimed.insert(c.get<std::string>());
		save.xp = j.value("xp", int64_t{ 0 });
		save.rank_stage = j.value("rank_stage", 0);
		save.rank_stars = j.value("rank_stars", 0);
		save.rank_best = j.value("rank_best", 0);
		save.rank_wins = j.value("rank_wins", 0);
		save.rank_losses = j.value("rank_losses", 0);
		save.rank_season = j.value("rank_season", 0u);
		save.anim_fast = j.value("anim_fast", false);
		if(j.contains("world_decks")) {
			save.world_decks_known = true;
			for(const auto& d : j["world_decks"])
				save.world_decks.insert(BufferIO::DecodeUTF8(d.get<std::string>()));
		}
		if(j.contains("gate_clears"))
			for(const auto& [id, n] : j["gate_clears"].items())
				save.gate_clears[id] = n.get<int>();
	}
	catch(const std::exception& e) {
		mainGame->PopupMessage(epro::format(L"save.json เสียหาย:\n{}", BufferIO::DecodeUTF8(e.what())));
	}
}

void DuelWorld::WriteSave() const {
	nlohmann::json j;
	j["started"] = save.started;
	j["gems"] = save.gems;
	j["gold"] = save.gold;
	j["wins"] = save.wins;
	j["losses"] = save.losses;
	j["packs_opened"] = save.packs_opened;
	j["items"] = nlohmann::json::object();
	for(const auto& [id, n] : save.items)
		if(n > 0)
			j["items"][id] = n;
	j["cards"] = nlohmann::json::object();
	for(const auto& [code, n] : save.cards)
		j["cards"][std::to_string(code)] = n;
	j["trader"]["day"] = save.trader_day;
	j["trader"]["seed"] = save.trader_seed;
	j["trader"]["stock"] = save.trader_stock;
	j["trader"]["bought"] = nlohmann::json::array();
	for(auto b : save.trader_bought)
		j["trader"]["bought"].push_back(b);
	j["duelists"] = nlohmann::json::array();
	for(const auto& d : save.duelists)
		j["duelists"].push_back({ { "chapter", d.chapter }, { "tier", d.tier } });
	j["skills"] = save.skills;
	j["equipped_skill"] = save.equipped_skill;
	j["free_pack_day"] = save.free_pack_day;
	j["login_last"] = save.login_last;
	j["login_count"] = save.login_count;
	j["daily_day"] = save.daily_day;
	j["structs_bought"] = nlohmann::json::object();
	for(const auto& [id, n] : save.structs_bought)
		j["structs_bought"][id] = n;
	j["stats"] = nlohmann::json::object();
	for(const auto& [id, n] : save.stats)
		j["stats"][id] = n;
	j["daily_stats"] = nlohmann::json::object();
	for(const auto& [id, n] : save.daily_stats)
		j["daily_stats"][id] = n;
	j["claimed"] = save.claimed;
	j["daily_claimed"] = save.daily_claimed;
	j["xp"] = save.xp;
	j["rank_stage"] = save.rank_stage;
	j["rank_stars"] = save.rank_stars;
	j["rank_best"] = save.rank_best;
	j["rank_wins"] = save.rank_wins;
	j["rank_losses"] = save.rank_losses;
	j["rank_season"] = save.rank_season;
	j["anim_fast"] = save.anim_fast;
	if(save.world_decks_known) {
		j["world_decks"] = nlohmann::json::array();
		for(const auto& d : save.world_decks)
			j["world_decks"].push_back(BufferIO::EncodeUTF8(d));
	}
	j["gate_clears"] = nlohmann::json::object();
	for(const auto& [id, n] : save.gate_clears)
		j["gate_clears"][id] = n;
	// write to a temp file first so a crash never corrupts the save
	const epro::path_string tmp = EPRO_TEXT("./story/world/save.json.tmp");
	{
		FileStream file{ tmp, FileStream::out | FileStream::trunc };
		if(!file.good())
			return;
		file << j.dump(1);
	}
	Utils::FileDelete(SAVE_FILE);
	Utils::FileMove(tmp, SAVE_FILE);
}

void DuelWorld::AddCard(uint32_t code, int count) {
	save.cards[Key(code)] += count;
}

/* ================================================================== economy */

uint32_t DuelWorld::Today() const {
	const std::time_t now = std::time(nullptr);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &now);
#else
	localtime_r(&now, &tm);
#endif
	return static_cast<uint32_t>((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
}

void DuelWorld::BuildTraderPool() {
	if(trader_pool_built)
		return;
	trader_pool_built = true;
	for(auto& p : trader_pool)
		p.clear();
	trader_pool_keys.clear();
	std::set<uint32_t> in_shop;
	for(const auto& p : packs)
		for(auto c : p.cards)
			in_shop.insert(Key(c));
	for(const auto& d : structs)
		for(auto c : d.cards)
			in_shop.insert(Key(c));
	for(const auto& [code, card] : gDataManager->cards) {
		const auto& d = card._data;
		if(d.IsInArtworkOffsetRange() || d.alias)
			continue;
		if(d.type & (0x8000000 | 0x4000))   // skills, tokens
			continue;
		if(!(d.ot & 0x3) || (d.ot & (0x200 | 0x1000)))   // real OCG/TCG cards only (no Rush / hidden)
			continue;
		if(in_shop.count(code))
			continue;
		if(!trader_pool_keys.insert(code).second)
			continue;
		trader_pool[static_cast<int>(RarityOf(code))].push_back(code);
	}
	for(auto& p : trader_pool)
		std::sort(p.begin(), p.end());   // stable order -> the daily seed always gives the same stock
}

void DuelWorld::RefreshTraderIfNeeded(bool force) {
	const uint32_t today = Today();
	// old saves had a smaller stock (and cards from the packs): rebuild today's stock once
	if(!force && save.trader_day == today && save.trader_stock.size() == static_cast<size_t>(trader_size))
		return;
	if(save.trader_day != today)
		save.trader_seed = 0;
	if(force)
		save.trader_seed++;
	save.trader_day = today;
	save.trader_stock.clear();
	save.trader_bought.clear();
	BuildTraderPool();
	auto& pool = trader_pool;
	uint32_t local = today * 131u + static_cast<uint32_t>(save.trader_seed) * 7919u + 17u;
	auto next = [&local](uint32_t n) {
		local ^= local << 13;
		local ^= local >> 17;
		local ^= local << 5;
		return n ? local % n : 0u;
	};
	std::set<uint32_t> used;
	auto pick = [&](int r) -> uint32_t {
		while(r > 0 && pool[r].empty())
			r--;
		if(pool[r].empty())
			return 0;
		for(int tries = 0; tries < 20; tries++) {
			const auto c = pool[r][next(static_cast<uint32_t>(pool[r].size()))];
			if(used.insert(c).second)
				return c;
		}
		return 0;
	};
	// 2 UR guaranteed, the rest N 30% / R 35% / SR 25% / UR 10%
	const int weights[4] = { 30, 35, 25, 10 };
	for(int i = 0; i < 2; i++)
		if(auto c = pick(3))
			save.trader_stock.push_back(c);
	for(int guard = 0; static_cast<int>(save.trader_stock.size()) < trader_size && guard < trader_size * 4; guard++) {
		int roll = static_cast<int>(next(100)), r = 0;
		while(r < 3 && roll >= weights[r]) {
			roll -= weights[r];
			r++;
		}
		if(auto c = pick(r))
			save.trader_stock.push_back(c);
	}
	// show the stock from rarest to common
	std::stable_sort(save.trader_stock.begin(), save.trader_stock.end(), [this](uint32_t a, uint32_t b) {
		return static_cast<int>(RarityOf(a)) > static_cast<int>(RarityOf(b));
	});
	WriteSave();
}

void DuelWorld::RerollDuelists(bool all, int slot) {
	const auto& chapters = gStory.Chapters();
	std::vector<int> pool;
	for(size_t i = 0; i < chapters.size(); i++)
		if(gStory.ChapterUnlocked(i))
			pool.push_back(static_cast<int>(i));
	if(pool.empty() && !chapters.empty())
		pool.push_back(0);
	save.duelists.resize(static_cast<size_t>(roaming_slots));
	for(int s = 0; s < roaming_slots; s++) {
		if(!all && s != slot && save.duelists[s].chapter >= 0)
			continue;
		if(pool.empty()) {
			save.duelists[s] = {};
			continue;
		}
		Duelist d;
		for(int tries = 0; tries < 8; tries++) {
			d.chapter = pool[static_cast<size_t>(Rand(static_cast<int>(pool.size())))];
			bool dup = false;
			for(int o = 0; o < roaming_slots; o++)
				if(o != s && save.duelists[o].chapter == d.chapter)
					dup = true;
			if(!dup || static_cast<int>(pool.size()) < roaming_slots)
				break;
		}
		// tier grows with the story position
		d.tier = std::min(5, d.chapter * 6 / std::max<int>(1, static_cast<int>(chapters.size())));
		save.duelists[s] = d;
	}
	WriteSave();
}

std::vector<DuelWorld::RevealCard> DuelWorld::OpenPack(const PackDef& pack) {
	std::vector<RevealCard> out;
	auto pick = [&](int r) -> uint32_t {
		for(int k = r; k >= 0; k--)
			if(!pack.by_rarity[k].empty())
				return pack.by_rarity[k][static_cast<size_t>(Rand(static_cast<int>(pack.by_rarity[k].size())))];
		for(int k = r; k < 4; k++)
			if(!pack.by_rarity[k].empty())
				return pack.by_rarity[k][static_cast<size_t>(Rand(static_cast<int>(pack.by_rarity[k].size())))];
		return 0;
	};
	const int total = rates[0] + rates[1] + rates[2] + rates[3];
	for(int i = 0; i < pack_size; i++) {
		int r;
		if(i == pack_size - 1) {
			r = Rand(100) < last_slot_ur ? 3 : 2;   // guaranteed SR or better
		} else {
			int roll = Rand(std::max(1, total));
			r = 0;
			while(r < 3 && roll >= rates[r]) {
				roll -= rates[r];
				r++;
			}
		}
		const uint32_t code = pick(r);
		if(!code)
			continue;
		const bool is_new = Owned(code) == 0;
		AddCard(code);
		out.push_back({ code, RarityOf(code), is_new });
	}
	// show the best cards last, like Master Duel
	std::stable_sort(out.begin(), out.end(), [](const RevealCard& a, const RevealCard& b) { return a.rarity < b.rarity; });
	save.packs_opened++;
	return out;
}

uint32_t DuelWorld::RandomCardFromDeck(const std::string& deck_file, bool sr_plus) {
	const auto cards = ReadYdk("./story/decks/" + deck_file);
	if(cards.empty())
		return 0;
	std::vector<uint32_t> candidates;
	std::vector<int> weight;
	const int w[4] = { 50, 30, 15, 5 };
	for(auto c : cards) {
		const auto r = RarityOf(c);
		if(sr_plus && r < Rarity::SR)
			continue;
		candidates.push_back(c);
		weight.push_back(w[static_cast<int>(r)]);
	}
	if(candidates.empty())
		return cards[static_cast<size_t>(Rand(static_cast<int>(cards.size())))];
	int sum = 0;
	for(auto x : weight)
		sum += x;
	int roll = Rand(sum);
	for(size_t i = 0; i < candidates.size(); i++) {
		if(roll < weight[i])
			return candidates[i];
		roll -= weight[i];
	}
	return candidates.back();
}

bool DuelWorld::UseItem(const std::string& id) {
	auto it = save.items.find(id);
	if(it == save.items.end() || it->second <= 0)
		return false;
	const ItemDef* def = nullptr;
	for(const auto& d : items)
		if(d.id == id)
			def = &d;
	if(!def)
		return false;
	if(def->type == "gems") {
		save.gems += def->value;
		Toast(epro::format(L"ได้รับเพชร +{}", def->value));
	} else if(def->type == "gold") {
		save.gold += def->value;
		Toast(epro::format(L"ได้รับทอง +{}", Num(def->value)));
	} else if(def->type == "pack_ticket") {
		const PackDef* pack = nullptr;
		for(const auto& p : packs)
			if(p.id == def->pack)
				pack = &p;
		if(!pack)
			return false;
		ShowReveal(pack->name, L"เปิดด้วยตั๋ว", OpenPack(*pack));
		AddStat("pack_open");
	} else if(def->type == "random_pack") {
		if(packs.empty())
			return false;
		const auto& pack = packs[static_cast<size_t>(Rand(static_cast<int>(packs.size())))];
		ShowReveal(pack.name, L"เปิดด้วยตั๋วซองสุ่ม", OpenPack(pack));
		AddStat("pack_open");
	} else if(def->type == "trader_refresh") {
		RefreshTraderIfNeeded(true);
		Toast(L"พ่อค้าการ์ดนำสินค้าใหม่มาแล้ว!");
	} else if(def->type == "duelist_call") {
		RerollDuelists(true);
		Toast(L"มีนักสู้กลุ่มใหม่มาท้าดวล!");
	} else if(def->type == "card") {
		const auto code = static_cast<uint32_t>(def->value);
		if(!gDataManager->GetCardData(code))
			return false;
		const bool is_new = Owned(code) == 0;
		AddCard(code);
		ShowReveal(def->name, L"", { { code, RarityOf(code), is_new } });
	} else {
		return false;
	}
	it->second--;
	WriteSave();
	return true;
}

/* ================================================================== deck checks */

bool DuelWorld::CheckDeck(const Deck& deck, bool popup) {
	if(!CollectionLocked())
		return true;
	std::map<uint32_t, int> need;
	for(const auto* list : { &deck.main, &deck.extra, &deck.side })
		for(const auto* c : *list)
			if(c)
				need[Key(c->code)]++;
	std::vector<std::wstring> missing;
	for(const auto& [code, n] : need) {
		const int have = Owned(code);
		if(have < n)
			missing.push_back(epro::format(L"• {} (มี {}/{})", gDataManager->GetName(code), have, n));
	}
	if(missing.empty())
		return true;
	if(popup) {
		std::wstring text = L"เด็คนี้มีการ์ดที่คุณยังไม่มี/มีไม่พอ:\n";
		for(size_t i = 0; i < missing.size() && i < 12; i++)
			text += missing[i] + L"\n";
		if(missing.size() > 12)
			text += epro::format(L"...และอีก {} ใบ\n", missing.size() - 12);
		text += L"\nหาการ์ดได้จากร้านค้า พ่อค้าการ์ด หรือชนะนักสู้ (Duel World)";
		mainGame->PopupMessage(text);
	}
	return false;
}

bool DuelWorld::CheckDeckByName(const std::wstring& deck_name) {
	if(!CollectionLocked())
		return true;
	Deck deck;
	if(!DeckManager::LoadDeckFromFile(Utils::ToPathString(deck_name), deck))
		return true;
	return CheckDeck(deck, true);
}

/* ================================================================== gui */

void DuelWorld::CreateGUI() {
	auto* env = mainGame->env;
	// ---------------- skill info popup (modal, drawn on top by MdUi)
	{
		auto* modal = env->addModalScreen(nullptr);
		wSkillInfo = env->addWindow(mainGame->Scale(252, 150, 772, 450), false, L"ข้อมูลสกิล", modal);
		wSkillInfo->getCloseButton()->setVisible(false);
		stSkillInfo = env->addStaticText(L"", mainGame->Scale(20, 40, 500, 240), false, true, wSkillInfo);
		env->addButton(mainGame->Scale(200, 250, 320, 285), wSkillInfo, BUTTON_WORLD_SKILL_INFO_OK, L"ปิด");
		wSkillInfo->setVisible(false);
	}
	// ---------------- shop card search popup (modal, drawn on top by MdUi)
	{
		auto* modal = env->addModalScreen(nullptr);
		wCardSearch = env->addWindow(mainGame->Scale(152, 70, 872, 570), false, L"ค้นหาการ์ด", modal);
		wCardSearch->getCloseButton()->setVisible(false);
		wCardSearch->setDraggable(false);
		ebSearchPop = env->addEditBox(L"", mainGame->Scale(15, 40, 470, 68), true, wCardSearch, EDITBOX_WORLD_SEARCH_POP);
		env->addButton(mainGame->Scale(478, 40, 590, 68), wCardSearch, BUTTON_WORLD_SEARCH_POP, L"ค้นหา");
		env->addButton(mainGame->Scale(598, 40, 705, 68), wCardSearch, BUTTON_WORLD_SEARCH_CLOSE, L"ปิด");
		lstSearch = env->addListBox(mainGame->Scale(15, 78, 330, 485), wCardSearch, LISTBOX_WORLD_SEARCH, true);
		lstSearch->setItemHeight(mainGame->Scale(26));
		imgSearch = env->addStaticText(L"", mainGame->Scale(340, 78, 470, 268), true, false, wCardSearch);
		stSearchInfo = env->addStaticText(L"", mainGame->Scale(480, 78, 705, 268), true, true, wCardSearch);
		stSearchSrc = env->addStaticText(L"", mainGame->Scale(340, 276, 705, 485), true, true, wCardSearch);
		wCardSearch->setVisible(false);
	}
	// ---------------- card detail popup (modal, drawn on top by MdUi)
	{
		auto* modal = env->addModalScreen(nullptr);
		wCardDetail = env->addWindow(mainGame->Scale(132, 70, 892, 570), false, L"รายละเอียดการ์ด", modal);
		wCardDetail->getCloseButton()->setVisible(false);
		wCardDetail->setDraggable(false);
		imgCD = env->addStaticText(L"", mainGame->Scale(15, 40, 255, 390), false, false, wCardDetail);
		stCDName = env->addStaticText(L"", mainGame->Scale(270, 38, 745, 62), false, true, wCardDetail);
		stCDName->setOverrideColor(SColor(255, 255, 225, 140));
		stCDType = env->addStaticText(L"", mainGame->Scale(270, 64, 745, 106), false, true, wCardDetail);
		stCDType->setOverrideColor(SColor(255, 170, 205, 255));
		stCDInfo = env->addStaticText(L"", mainGame->Scale(270, 108, 745, 130), false, true, wCardDetail);
		{
			auto* text = irr::gui::CGUICustomText::addCustomText(L"", false, env, wCardDetail, -1, mainGame->Scale(270, 136, 745, 300));
			text->enableScrollBar();
			text->setWordWrap(true);
			text->setDrawBorder(true);
			stCDText = text;
		}
		stCDSrc = env->addStaticText(L"", mainGame->Scale(270, 306, 745, 440), true, true, wCardDetail);
		env->addButton(mainGame->Scale(15, 400, 255, 440), wCardDetail, BUTTON_WORLD_CD_CLOSE, L"ปิด");
		wCardDetail->setVisible(false);
	}
	// ---------------- daily login reward popup (modal, drawn on top by MdUi)
	{
		auto* modal = env->addModalScreen(nullptr);
		wLogin = env->addWindow(mainGame->Scale(172, 110, 852, 480), false, L"รางวัลรายวัน", modal);
		wLogin->getCloseButton()->setVisible(false);
		wLogin->setDraggable(false);
		stLoginDays.clear();
		for(int i = 0; i < 7; i++) {
			auto* st = env->addStaticText(L"", mainGame->Scale(15 + i * 94, 50, 15 + i * 94 + 86, 225), true, true, wLogin);
			st->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
			stLoginDays.push_back(st);
		}
		stLoginInfo = env->addStaticText(L"", mainGame->Scale(15, 235, 665, 300), false, true, wLogin);
		stLoginInfo->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
		btnLoginClaim = env->addButton(mainGame->Scale(190, 310, 400, 355), wLogin, BUTTON_WORLD_LOGIN_CLAIM, L"รับรางวัล!");
		env->addButton(mainGame->Scale(410, 310, 490, 355), wLogin, BUTTON_WORLD_LOGIN_CLOSE, L"ปิด");
		wLogin->setVisible(false);
	}
	// ---------------- hub
	wHub = env->addWindow(mainGame->Scale(70, 35, 954, 605), false, L"Duel World");
	wHub->getCloseButton()->setVisible(false);
	wHub->setVisible(false);
	stHeader = env->addStaticText(L"", mainGame->Scale(420, 28, 870, 52), false, false, wHub);   // gems / gold boxes are drawn left of it
	stHeader->setOverrideColor(SColor(255, 255, 220, 120));
	tabs = env->addTabControl(mainGame->Scale(10, 56, 874, 515), wHub, true, true, TAB_WORLD);
	auto* tRank = tabs->addTab(L"Ranked");
	auto* tGate = tabs->addTab(L"Gate");
	auto* tDuelists = tabs->addTab(L"นักสู้สุ่ม");
	auto* tMissions = tabs->addTab(L"ภารกิจ");
	tabDuelists = tDuelists;
	tabGate = tGate;
	tabRank = tRank;
	auto* tShop = tabs->addTab(L"ร้านค้า");
	auto* tStructs = tabs->addTab(L"Structure Deck");
	auto* tTrader = tabs->addTab(L"พ่อค้าการ์ด");
	auto* tItems = tabs->addTab(L"ไอเท็ม");
	auto* tSkills = tabs->addTab(L"สกิล");

	lstDuelists = env->addListBox(mainGame->Scale(10, 10, 360, 330), tDuelists, LISTBOX_WORLD_DUELISTS, true);
	lstDuelists->setItemHeight(mainGame->Scale(30));
	stDuelist = env->addStaticText(L"", mainGame->Scale(370, 10, 850, 250), true, true, tDuelists);
	env->addStaticText(L"เด็คของคุณ:", mainGame->Scale(370, 262, 850, 282), false, false, tDuelists);
	cbDeck = env->addComboBox(mainGame->Scale(370, 284, 745, 309), tDuelists);
	newDeckButtons.push_back(env->addButton(mainGame->Scale(750, 284, 850, 309), tDuelists, BUTTON_WORLD_NEW_DECK, L"+ เด็คใหม่"));
	env->addStaticText(L"สกิล:", mainGame->Scale(370, 322, 420, 342), false, false, tDuelists);
	cbRoamSkill = env->addComboBox(mainGame->Scale(420, 318, 745, 343), tDuelists, COMBOBOX_WORLD_SKILL_ROAM);
	env->addButton(mainGame->Scale(750, 318, 850, 343), tDuelists, BUTTON_WORLD_SKILL_INFO_ROAM, L"ข้อมูลสกิล");
	btnDuel = env->addButton(mainGame->Scale(370, 352, 600, 405), tDuelists, BUTTON_WORLD_DUEL, L"ดวล!");
	btnReroll = env->addButton(mainGame->Scale(610, 352, 850, 405), tDuelists, BUTTON_WORLD_REROLL, L"เรียกนักสู้ใหม่ (ไอเท็ม)");

	// Ranked (Duel Links style ladder, normal Master Rule duels)
	stRankBig = env->addStaticText(L"", mainGame->Scale(10, 10, 420, 120), true, true, tRank);
	stRank = env->addStaticText(L"", mainGame->Scale(10, 128, 420, 400), true, true, tRank);
	imgRank = env->addStaticText(L"", mainGame->Scale(440, 10, 850, 250), false, false, tRank);
	env->addStaticText(L"เด็คของคุณ:", mainGame->Scale(440, 262, 850, 282), false, false, tRank);
	cbRankDeck = env->addComboBox(mainGame->Scale(440, 284, 745, 309), tRank);
	newDeckButtons.push_back(env->addButton(mainGame->Scale(750, 284, 850, 309), tRank, BUTTON_WORLD_NEW_DECK, L"+ เด็คใหม่"));
	env->addStaticText(L"สกิล:", mainGame->Scale(440, 322, 490, 342), false, false, tRank);
	cbRankSkill = env->addComboBox(mainGame->Scale(490, 318, 745, 343), tRank, COMBOBOX_WORLD_SKILL_RANK);
	env->addButton(mainGame->Scale(750, 318, 850, 343), tRank, BUTTON_WORLD_SKILL_INFO_RANK, L"ข้อมูลสกิล");
	btnRankDuel = env->addButton(mainGame->Scale(440, 352, 850, 405), tRank, BUTTON_WORLD_RANK_DUEL, L"ค้นหาคู่ต่อสู้ (Ranked Duel)");

	// Gate (legendary duelists, Lv10 - Lv40)
	lstGate = env->addListBox(mainGame->Scale(10, 10, 360, 400), tGate, LISTBOX_WORLD_GATE, true);
	lstGate->setItemHeight(mainGame->Scale(28));
	stGate = env->addStaticText(L"", mainGame->Scale(370, 10, 850, 220), true, true, tGate);
	env->addStaticText(L"เลเวลคู่ต่อสู้:", mainGame->Scale(370, 230, 500, 250), false, false, tGate);
	cbGateLv = env->addComboBox(mainGame->Scale(500, 226, 850, 251), tGate, COMBOBOX_WORLD_GATE_LV);
	for(const auto* lv : { L"Lv.10  (ปกติ)", L"Lv.20  (LP +2000)", L"Lv.30  (LP +4000, +1 การ์ด)", L"Lv.40  (LP +6000, +1 การ์ด) ★ดรอป 2 ใบ" })
		cbGateLv->addItem(lv);
	cbGateLv->setSelected(0);
	env->addStaticText(L"เด็คของคุณ:", mainGame->Scale(370, 262, 850, 282), false, false, tGate);
	cbGateDeck = env->addComboBox(mainGame->Scale(370, 284, 745, 309), tGate);
	newDeckButtons.push_back(env->addButton(mainGame->Scale(750, 284, 850, 309), tGate, BUTTON_WORLD_NEW_DECK, L"+ เด็คใหม่"));
	env->addStaticText(L"สกิล:", mainGame->Scale(370, 322, 420, 342), false, false, tGate);
	cbGateSkill = env->addComboBox(mainGame->Scale(420, 318, 745, 343), tGate, COMBOBOX_WORLD_SKILL_GATE);
	env->addButton(mainGame->Scale(750, 318, 850, 343), tGate, BUTTON_WORLD_SKILL_INFO_GATE, L"ข้อมูลสกิล");
	btnGateDuel = env->addButton(mainGame->Scale(370, 352, 850, 405), tGate, BUTTON_WORLD_GATE_DUEL, L"ท้าดวล! (Gate Duel)");

	// missions
	tabMissions = tMissions;
	lstMissions = env->addListBox(mainGame->Scale(10, 10, 540, 400), tMissions, LISTBOX_WORLD_MISSIONS, true);
	lstMissions->setItemHeight(mainGame->Scale(28));
	stMission = env->addStaticText(L"", mainGame->Scale(550, 10, 850, 270), true, true, tMissions);
	btnClaimAll = env->addButton(mainGame->Scale(550, 280, 850, 320), tMissions, BUTTON_WORLD_MISSION_CLAIM_ALL, L"รับรางวัลทั้งหมด");
	btnClaim = env->addButton(mainGame->Scale(550, 330, 850, 370), tMissions, BUTTON_WORLD_MISSION_CLAIM, L"รับรางวัล");

	// shop: packs with a featured cards strip
	auto strip = [&](irr::gui::IGUIElement* parent, std::vector<irr::gui::IGUIElement*>& out) {
		env->addStaticText(L"การ์ดเด่น:", mainGame->Scale(340, 222, 480, 240), false, false, parent);
		for(int i = 0; i < 7; i++) {
			const irr::s32 x = 340 + i * 73;
			out.push_back(env->addStaticText(L"", mainGame->Scale(x, 242, x + 64, 336), false, false, parent));
		}
	};
	ebSearch = env->addEditBox(L"", mainGame->Scale(10, 10, 235, 36), true, tShop, EDITBOX_WORLD_SEARCH);
	env->addButton(mainGame->Scale(240, 10, 330, 36), tShop, BUTTON_WORLD_SEARCH, L"ค้นหาการ์ด");
	lstPacks = env->addListBox(mainGame->Scale(10, 42, 330, 400), tShop, LISTBOX_WORLD_PACKS, true);
	lstPacks->setItemHeight(mainGame->Scale(28));
	imgPack = env->addStaticText(L"", mainGame->Scale(340, 10, 482, 214), true, false, tShop);
	stPack = env->addStaticText(L"", mainGame->Scale(492, 10, 850, 214), true, true, tShop);
	strip(tShop, featPack);
	env->addButton(mainGame->Scale(700, 218, 850, 240), tShop, BUTTON_WORLD_PACK_LIST, L"ดูการ์ดทั้งหมดในซอง");
	btnFreePack = env->addButton(mainGame->Scale(340, 350, 520, 395), tShop, BUTTON_WORLD_FREE_PACK, L"ซองฟรีวันนี้!");
	btnBuy1 = env->addButton(mainGame->Scale(530, 350, 688, 395), tShop, BUTTON_WORLD_BUY1, L"ซื้อ 1 ซอง");
	btnBuy10 = env->addButton(mainGame->Scale(696, 350, 850, 395), tShop, BUTTON_WORLD_BUY10, L"ซื้อ 10 ซอง");

	// structure decks
	lstStructs = env->addListBox(mainGame->Scale(10, 10, 330, 400), tStructs, LISTBOX_WORLD_STRUCTS, true);
	lstStructs->setItemHeight(mainGame->Scale(28));
	imgStruct = env->addStaticText(L"", mainGame->Scale(340, 10, 482, 214), true, false, tStructs);
	stStruct = env->addStaticText(L"", mainGame->Scale(492, 10, 850, 214), true, true, tStructs);
	strip(tStructs, featStruct);
	env->addButton(mainGame->Scale(700, 218, 850, 240), tStructs, BUTTON_WORLD_STRUCT_LIST, L"ดูการ์ดทั้งหมดในเด็ค");
	btnStructBuy = env->addButton(mainGame->Scale(492, 350, 850, 395), tStructs, BUTTON_WORLD_STRUCT_BUY, L"ซื้อ Structure Deck");

	lstTrader = env->addListBox(mainGame->Scale(10, 10, 420, 330), tTrader, LISTBOX_WORLD_TRADER, true);
	lstTrader->setItemHeight(mainGame->Scale(30));
	imgTrader = env->addStaticText(L"", mainGame->Scale(430, 10, 572, 214), true, false, tTrader);
	stTrader = env->addStaticText(L"", mainGame->Scale(582, 10, 850, 300), true, true, tTrader);
	btnTraderBuy = env->addButton(mainGame->Scale(430, 330, 630, 370), tTrader, BUTTON_WORLD_TRADER_BUY, L"ซื้อ");
	btnTraderRefresh = env->addButton(mainGame->Scale(640, 330, 850, 370), tTrader, BUTTON_WORLD_TRADER_REFRESH, L"รีเฟรช (ใช้ตั๋ว)");

	for(auto* el : { imgPack, imgStruct, imgTrader, imgSearch })
		el->setToolTipText(L"คลิกเพื่อดูรายละเอียดการ์ด");
	for(auto* el : featPack)
		el->setToolTipText(L"คลิกเพื่อดูรายละเอียดการ์ด");
	for(auto* el : featStruct)
		el->setToolTipText(L"คลิกเพื่อดูรายละเอียดการ์ด");
	lstItems = env->addListBox(mainGame->Scale(10, 10, 420, 400), tItems, LISTBOX_WORLD_ITEMS, true);
	lstItems->setItemHeight(mainGame->Scale(30));
	stItem = env->addStaticText(L"", mainGame->Scale(430, 10, 850, 250), true, true, tItems);
	btnItemUse = env->addButton(mainGame->Scale(430, 330, 630, 370), tItems, BUTTON_WORLD_ITEM_USE, L"ใช้");

	lstSkills = env->addListBox(mainGame->Scale(10, 10, 420, 400), tSkills, LISTBOX_WORLD_SKILLS, true);
	lstSkills->setItemHeight(mainGame->Scale(30));
	stSkill = env->addStaticText(L"", mainGame->Scale(430, 10, 850, 300), true, true, tSkills);
	btnSkillEquip = env->addButton(mainGame->Scale(430, 330, 640, 370), tSkills, BUTTON_WORLD_SKILL_EQUIP, L"ติดตั้งสกิล");
	btnSkillUnequip = env->addButton(mainGame->Scale(650, 330, 850, 370), tSkills, BUTTON_WORLD_SKILL_UNEQUIP, L"ถอดสกิล");

	env->addButton(mainGame->Scale(10, 525, 150, 560), wHub, BUTTON_WORLD_STORY, L"เนื้อเรื่อง (Story)");
	env->addButton(mainGame->Scale(156, 525, 296, 560), wHub, BUTTON_WORLD_DECKS, L"สร้างเด็ค (Deck)");
	env->addButton(mainGame->Scale(302, 525, 442, 560), wHub, BUTTON_WORLD_CAMPAIGN, L"แคมเปญ");
	env->addButton(mainGame->Scale(448, 525, 588, 560), wHub, BUTTON_WORLD_LOGIN, L"รางวัลรายวัน");
	btnAnim = env->addButton(mainGame->Scale(594, 525, 734, 560), wHub, BUTTON_WORLD_ANIM, L"แอนิเมชัน: x1");
	env->addButton(mainGame->Scale(740, 525, 874, 560), wHub, BUTTON_WORLD_BACK, L"กลับเมนูหลัก");

	// ---------------- starter selection
	wStarter = env->addWindow(mainGame->Scale(140, 70, 884, 590), false, L"เลือกเด็คเริ่มต้น - Choose your starter deck");
	wStarter->getCloseButton()->setVisible(false);
	wStarter->setVisible(false);
	stStarter = env->addStaticText(L"", mainGame->Scale(15, 28, 730, 70), false, true, wStarter);
	for(size_t i = 0; i < 3; i++) {
		const irr::s32 x = 15 + static_cast<irr::s32>(i) * 240;
		imgStarters.push_back(env->addStaticText(L"", mainGame->Scale(x + 45, 75, x + 187, 279), true, false, wStarter));
		auto* desc = env->addStaticText(L"", mainGame->Scale(x, 285, x + 230, 420), false, true, wStarter, BUTTON_WORLD_STARTER_BACK + 1 + static_cast<irr::s32>(i));
		(void)desc;
		btnStarters.push_back(env->addButton(mainGame->Scale(x, 430, x + 230, 470), wStarter, BUTTON_WORLD_STARTER_0 + static_cast<irr::s32>(i), L""));
	}
	env->addButton(mainGame->Scale(15, 478, 200, 510), wStarter, BUTTON_WORLD_STARTER_BACK, L"กลับ");
}

void DuelWorld::Open() {
	LoadData();
	// daily login rewards (7-day cycle, Duel Links style) - world.json "login_rewards" can override
	if(login_rewards.size() != 7) {
		login_rewards = {
			{ 50, 0, "", 0 },
			{ 0, 1000, "", 0 },
			{ 0, 0, "ticket_random", 1 },
			{ 80, 0, "", 0 },
			{ 0, 2000, "duelist_call", 1 },
			{ 100, 0, "", 0 },
			{ 200, 0, "ticket_random", 2 },
		};
	}
	if(std::none_of(items.begin(), items.end(), [](const ItemDef& d) { return d.id == "ticket_random"; })) {
		ItemDef def;
		def.id = "ticket_random";
		def.name = L"ตั๋วซองสุ่ม";
		def.desc = L"เปิดซองสุ่ม 1 ซองจากซองทั้งหมดในร้านค้า (ฟรี)";
		def.type = "random_pack";
		items.push_back(std::move(def));
	}
	LoadSave();
	if(!save.started) {
		if(starters.empty()) {
			mainGame->PopupMessage(L"ไม่พบข้อมูล Duel World (story/world/world.json)");
			mainGame->ShowElement(mainGame->wMainMenu);
			return;
		}
		SetText(stStarter, L"ยินดีต้อนรับสู่ Duel World! เลือกเด็คแรกของคุณ 1 เด็ค การ์ดในเด็คจะเข้าคลังของคุณ "
				L"และหลังจากนี้การ์ดที่ใช้ได้ทั้งเกมคือการ์ดที่คุณสะสมเท่านั้น");
		for(size_t i = 0; i < btnStarters.size(); i++) {
			const bool has = i < starters.size();
			btnStarters[i]->setVisible(has);
			imgStarters[i]->setVisible(has);
			auto* desc = static_cast<irr::gui::IGUIStaticText*>(wStarter->getElementFromId(BUTTON_WORLD_STARTER_BACK + 1 + static_cast<irr::s32>(i)));
			if(desc)
				desc->setVisible(has);
			if(!has)
				continue;
			btnStarters[i]->setText(epro::format(L"เลือก {}", starters[i].name).data());
			if(desc)
				SetText(desc, starters[i].desc);
		}
		mainGame->ShowElement(wStarter);
		return;
	}
	if(save.duelists.size() != static_cast<size_t>(roaming_slots))
		RerollDuelists(true);
	for(const auto& d : save.duelists)
		if(d.chapter < 0 || static_cast<size_t>(d.chapter) >= gStory.Chapters().size()) {
			RerollDuelists(true);
			break;
		}
	RefreshTraderIfNeeded(false);
	ApplyAnimSpeed();
	CheckSeason(true);
	RefreshHub();
	mainGame->ShowElement(wHub);
	if(LoginClaimable())
		ShowLogin();
}

void DuelWorld::ShowHubOrMenu() {
	if(save.started) {
		RefreshHub();
		mainGame->ShowElement(wHub);
	} else {
		mainGame->ShowElement(mainGame->wMainMenu);
	}
}

void DuelWorld::ChooseStarter(size_t index) {
	if(index >= starters.size())
		return;
	const auto& st = starters[index];
	const auto cards = ReadYdk("./story/world/starters/" + st.file);
	if(cards.empty()) {
		mainGame->PopupMessage(epro::format(L"ไม่พบเด็คเริ่มต้น: story/world/starters/{}", BufferIO::DecodeUTF8(st.file)));
		return;
	}
	save = Save{};
	save.started = true;
	save.gems = start_gems;
	save.gold = start_gold;
	save.items = start_items;
	for(auto c : cards)
		AddCard(c);
	// put the deck in the player's deck folder
	{
		std::string text;
		FileStream in{ Utils::ToPathString("./story/world/starters/" + st.file), FileStream::in };
		std::string line;
		while(std::getline(in, line))
			text += line + "\n";
		FileStream out{ epro::format(EPRO_TEXT("./deck/{}.ydk"), Utils::ToPathString(st.deck_name)), FileStream::out | FileStream::trunc };
		out << text;
	}
	save.world_decks_known = true;
	save.world_decks.insert(st.deck_name);
	gGameConfig->lastdeck = st.deck_name;
	RerollDuelists(true);
	RefreshTraderIfNeeded(true);
	WriteSave();
	mainGame->HideElement(wStarter);
	RefreshHub();
	mainGame->ShowElement(wHub);
	std::vector<RevealCard> show;
	std::set<uint32_t> seen;
	for(auto c : cards)
		if(seen.insert(Key(c)).second && RarityOf(c) >= Rarity::SR)
			show.push_back({ c, RarityOf(c), true });
	std::stable_sort(show.begin(), show.end(), [](const RevealCard& a, const RevealCard& b) { return a.rarity < b.rarity; });
	if(show.size() > 8)
		show.erase(show.begin(), show.begin() + static_cast<std::ptrdiff_t>(show.size() - 8));
	ShowReveal(epro::format(L"ได้รับเด็ค {}!", st.name),
			   epro::format(L"เพชร {}  |  ทอง {}  |  การ์ด {} ใบ  |  เด็คถูกบันทึกเป็น \"{}\"", Num(save.gems), Num(save.gold), cards.size(), st.deck_name),
			   show);
}

void DuelWorld::RefreshHeader() {
	int total = 0;
	for(const auto& [c, n] : save.cards)
		total += n;
	(void)total;
	stHeader->setText(L"");   // everything is drawn as framed boxes in Draw2D
}

void DuelWorld::RefreshHub() {
	if(!pending_levelup.empty()) {
		Toast(pending_levelup);
		pending_levelup.clear();
	}
	RefreshHeader();
	RefreshDuelists();
	RefreshPacks();
	RefreshTrader();
	RefreshItems();
	RefreshSkills();
	RefreshStructs();
	RefreshMissions();
	for(auto* cb : { cbDeck, cbGateDeck, cbRankDeck })
		FillWorldDecks(cb);
	RefreshGate();
	RefreshRank();
	RefreshSkillCombos();
	btnAnim->setText(save.anim_fast ? L"แอนิเมชัน: x2" : L"แอนิเมชัน: x1");
}

void DuelWorld::RefreshDuelists() {
	const auto& chapters = gStory.Chapters();
	const int sel = lstDuelists->getSelected();
	lstDuelists->clear();
	for(const auto& d : save.duelists) {
		if(d.chapter < 0 || static_cast<size_t>(d.chapter) >= chapters.size()) {
			lstDuelists->addItem(L"(ว่าง)");
			continue;
		}
		const auto& ch = chapters[static_cast<size_t>(d.chapter)];
		lstDuelists->addItem(epro::format(L"{} {}  [{}]", std::wstring(static_cast<size_t>(d.tier + 1), L'★'), ch.opponent, ch.arc).data());
	}
	lstDuelists->setSelected(sel >= 0 && sel < static_cast<int>(lstDuelists->getItemCount()) ? sel : 0);
	auto it = save.items.find("duelist_call");
	btnReroll->setText(epro::format(L"เรียกนักสู้ใหม่ (มีป้าย {})", it == save.items.end() ? 0 : it->second).data());
	UpdateDuelistInfo();
}

std::wstring DuelWorld::SelectedOpponent() const {
	const auto& chapters = gStory.Chapters();
	if(!tabs)
		return {};
	auto* active = tabs->getTab(tabs->getActiveTab());
	if(active == tabGate && lstGate) {
		const int sel = lstGate->getSelected();
		if(sel >= 0 && static_cast<size_t>(sel) < gate_rows.size())
			return chapters[static_cast<size_t>(gate_rows[static_cast<size_t>(sel)])].opponent;
		return {};
	}
	if(active != tabDuelists || !lstDuelists)
		return {};
	const int sel = lstDuelists->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= save.duelists.size() || save.duelists[sel].chapter < 0 ||
	   static_cast<size_t>(save.duelists[sel].chapter) >= chapters.size())
		return {};
	return chapters[static_cast<size_t>(save.duelists[sel].chapter)].opponent;
}

irr::gui::IGUIElement* DuelWorld::DuelistInfoBox() const {
	if(tabs && tabs->getTab(tabs->getActiveTab()) == tabGate)
		return stGate;
	return stDuelist;
}

void DuelWorld::UpdateDuelistInfo() {
	const auto& chapters = gStory.Chapters();
	const int sel = lstDuelists->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= save.duelists.size() || save.duelists[sel].chapter < 0 ||
	   static_cast<size_t>(save.duelists[sel].chapter) >= chapters.size()) {
		SetText(stDuelist, L"ยังไม่มีนักสู้ (ปลดล็อกบทในเนื้อเรื่องเพื่อเจอนักสู้มากขึ้น)");
		btnDuel->setEnabled(false);
		return;
	}
	const auto& d = save.duelists[sel];
	const auto& ch = chapters[static_cast<size_t>(d.chapter)];
	const int gold = roam_gold_base + roam_gold_tier * d.tier;
	const int gems = roam_gems_base + roam_gems_tier * d.tier;
	SetText(stDuelist, epro::format(L"{}\nยุค: {}   ระดับ: {}\n\nรางวัลถ้าชนะ:\n• ทอง ~{}\n• เพชร ~{}\n• โอกาสดรอปการ์ดจากเด็คคู่ต่อสู้ {}%\n• โอกาสได้ไอเท็ม {}%\n\nแพ้ได้ทอง {}",
									ch.opponent, ch.arc, std::wstring(static_cast<size_t>(d.tier + 1), L'★'),
									Num(gold), gems, roam_card_drop, roam_item_drop, lose_gold));
	btnDuel->setEnabled(true);
}

void DuelWorld::RefreshPacks() {
	const int sel = lstPacks->getSelected();
	lstPacks->clear();
	for(size_t i = 0; i < packs.size(); i++) {
		const auto& p = packs[i];
		const wchar_t* tag = p.cat == "meta" ? L"[เมต้า]" : p.cat == "theme" ? L"[ธีม]" : p.cat == "special" ? L"[พิเศษ]" : L"[หลัก]";
		lstPacks->addItem(epro::format(L"{} {}{}", tag, p.name, p.tag.empty() ? L"" : L"  " + p.tag).data());
		lstPacks->setItemOverrideColor(static_cast<irr::u32>(i), p.cat == "meta" ? SColor(255, 255, 120, 150)
									   : p.cat == "theme" ? SColor(255, 130, 220, 160) : p.cat == "special" ? SColor(255, 255, 205, 80)
									   : SColor(255, 150, 200, 255));
	}
	lstPacks->setSelected(sel >= 0 && sel < static_cast<int>(packs.size()) ? sel : 0);
	UpdatePackInfo();
}

void DuelWorld::UpdatePackInfo() {
	const int sel = lstPacks->getSelected();
	featPackCodes.clear();
	const bool free_today = save.free_pack_day != Today();
	btnFreePack->setEnabled(free_today && sel >= 0 && static_cast<size_t>(sel) < packs.size());
	btnFreePack->setText(free_today ? L"เปิดซองนี้ฟรี! (วันละ 1)" : L"ซองฟรี: พรุ่งนี้");
	if(sel < 0 || static_cast<size_t>(sel) >= packs.size()) {
		SetText(stPack, L"ยังไม่มีซองในร้าน (story/world/packs.json)");
		btnBuy1->setEnabled(false);
		btnBuy10->setEnabled(false);
		return;
	}
	const auto& p = packs[static_cast<size_t>(sel)];
	const int total = rates[0] + rates[1] + rates[2] + rates[3];
	auto pct = [total](int v) { return total ? v * 100.f / total : 0.f; };
	featPackCodes = p.featured;
	SetText(stPack, epro::format(L"{}\n{}\n\nการ์ด {} แบบ (UR {} / SR {} / R {} / N {})\n1 ซอง = {} ใบ (ใบสุดท้าย SR+ แน่นอน)\n"
								 L"อัตรา: N {:.0f}% R {:.0f}% SR {:.0f}% UR {:.0f}%   ราคา: {} เพชร",
								 p.name, p.desc, p.cards.size(), p.by_rarity[3].size(), p.by_rarity[2].size(), p.by_rarity[1].size(),
								 p.by_rarity[0].size(), pack_size, pct(rates[0]), pct(rates[1]), pct(rates[2]), pct(rates[3]), p.price));
	btnBuy1->setText(epro::format(L"ซื้อ 1 ซอง ({} เพชร)", p.price).data());
	btnBuy10->setText(epro::format(L"ซื้อ 10 ซอง ({} เพชร)", p.price * 10).data());
	btnBuy1->setEnabled(true);
	btnBuy10->setEnabled(true);
}

void DuelWorld::RefreshTrader() {
	const int sel = lstTrader->getSelected();
	lstTrader->clear();
	for(size_t i = 0; i < save.trader_stock.size(); i++) {
		const auto code = save.trader_stock[i];
		const auto r = RarityOf(code);
		if(save.trader_bought.count(i))
			lstTrader->addItem(epro::format(L"[{}] {} - ขายแล้ว", RarityName(r), gDataManager->GetName(code)).data());
		else
			lstTrader->addItem(epro::format(L"[{}] {} - {} ทอง", RarityName(r), gDataManager->GetName(code), Num(RarityPrice(r))).data());
		lstTrader->setItemOverrideColor(static_cast<irr::u32>(i), RarityColor(r));
	}
	lstTrader->setSelected(sel >= 0 && sel < static_cast<int>(save.trader_stock.size()) ? sel : 0);
	auto it = save.items.find("trader_refresh");
	btnTraderRefresh->setText(epro::format(L"รีเฟรชสินค้า (มีตั๋ว {})", it == save.items.end() ? 0 : it->second).data());
	UpdateTraderInfo();
}

void DuelWorld::UpdateTraderInfo() {
	const int sel = lstTrader->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= save.trader_stock.size()) {
		SetText(stTrader, L"พ่อค้าการ์ดเปลี่ยนสินค้าทุกวัน");
		btnTraderBuy->setEnabled(false);
		return;
	}
	const auto code = save.trader_stock[static_cast<size_t>(sel)];
	const auto r = RarityOf(code);
	const bool sold = save.trader_bought.count(static_cast<size_t>(sel)) != 0;
	SetText(stTrader, epro::format(L"{}\nความแรร์: {}\nราคา: {} ทอง\nมีอยู่แล้ว: {} ใบ\n\n{}", gDataManager->GetName(code), RarityName(r),
								   Num(RarityPrice(r)), Owned(code), sold ? L"(ขายแล้ว)" : L"สินค้าเปลี่ยนทุกวัน"));
	btnTraderBuy->setEnabled(!sold);
}

void DuelWorld::RefreshItems() {
	const int sel = lstItems->getSelected();
	lstItems->clear();
	for(const auto& def : items) {
		auto it = save.items.find(def.id);
		const int n = it == save.items.end() ? 0 : it->second;
		if(n > 0)
			lstItems->addItem(epro::format(L"{}  x{}", def.name, n).data());
	}
	if(lstItems->getItemCount() == 0)
		lstItems->addItem(L"(ยังไม่มีไอเท็ม)");
	lstItems->setSelected(sel >= 0 && sel < static_cast<int>(lstItems->getItemCount()) ? sel : 0);
	UpdateItemInfo();
}

void DuelWorld::UpdateItemInfo() {
	const int sel = lstItems->getSelected();
	int idx = 0;
	for(const auto& def : items) {
		auto it = save.items.find(def.id);
		if(it == save.items.end() || it->second <= 0)
			continue;
		if(idx++ == sel) {
			SetText(stItem, epro::format(L"{}\nมี {} ชิ้น\n\n{}", def.name, it->second, def.desc));
			btnItemUse->setEnabled(true);
			return;
		}
	}
	SetText(stItem, L"ไอเท็มได้จากการชนะนักสู้และเนื้อเรื่อง");
	btnItemUse->setEnabled(false);
}

void DuelWorld::Toast(const std::wstring& text) {
	mainGame->PopupMessage(text);
}

void DuelWorld::BuyPacks(int count) {
	const int sel = lstPacks->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= packs.size())
		return;
	const auto& p = packs[static_cast<size_t>(sel)];
	const int64_t cost = static_cast<int64_t>(p.price) * count;
	if(save.gems < cost) {
		Toast(epro::format(L"เพชรไม่พอ (ต้องการ {} มี {})", Num(cost), Num(save.gems)));
		return;
	}
	save.gems -= cost;
	for(int i = 0; i < count; i++)
		ShowReveal(count > 1 ? epro::format(L"{} ({}/{})", p.name, i + 1, count) : p.name, L"", OpenPack(p));
	AddStat("pack_open", count);
	WriteSave();
	RefreshHub();
}

void DuelWorld::StartRoamingDuel() {
	const auto& chapters = gStory.Chapters();
	const int sel = lstDuelists->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= save.duelists.size())
		return;
	const auto d = save.duelists[static_cast<size_t>(sel)];
	if(d.chapter < 0 || static_cast<size_t>(d.chapter) >= chapters.size())
		return;
	if(cbDeck->getSelected() < 0) {
		Toast(L"ยังไม่มีเด็ค Duel World - ซื้อ Structure Deck หรือจัดเด็คจากการ์ดที่มี (ปุ่มสร้างเด็ค)");
		return;
	}
	const std::wstring deck_name = cbDeck->getItem(static_cast<irr::u32>(cbDeck->getSelected()));
	if(!CheckDeckByName(deck_name))
		return;
	StoryChapter ch = chapters[static_cast<size_t>(d.chapter)];
	ch.intro = { { ch.opponent, L"เฮ้! เด็คของนายน่าสนใจดีนี่ มาดวลกันหน่อย!" } };
	ch.win = { { ch.opponent, L"เก่งจริงๆ... เอาไป นี่คือรางวัลของนาย" } };
	ch.lose = { { ch.opponent, L"ฮ่าๆ ยังอ่อนหัดนัก ไปฝึกมาใหม่นะ!" } };
	ch.kind = "roaming";
	ch.bot_skill = 0;
	ch.start_lp = 8000;
	roaming_slot_playing = sel;
	mainGame->HideElement(wHub);
	const std::string deck_file = BufferIO::EncodeUTF8(ch.deck);
	gStory.StartCustomDuel(ch, deck_name, wHub, [this, d, deck_file, sel](int result) {
		std::wstring sub;
		std::vector<RevealCard> cards;
		if(result == 0) {
			save.wins++;
			const int base_gold = roam_gold_base + roam_gold_tier * d.tier;
			const int base_gems = roam_gems_base + roam_gems_tier * d.tier;
			const int gold = base_gold * (80 + Rand(41)) / 100;
			const int gems = base_gems * (80 + Rand(41)) / 100;
			save.gold += gold;
			save.gems += gems;
			sub = epro::format(L"ทอง +{}   เพชร +{}", Num(gold), gems);
			if(Rand(100) < roam_card_drop) {
				const auto code = RandomCardFromDeck(deck_file, false);
				if(code) {
					cards.push_back({ code, RarityOf(code), Owned(code) == 0 });
					AddCard(code);
				}
			}
			if(!roam_item_pool.empty() && Rand(100) < roam_item_drop) {
				const auto& id = roam_item_pool[static_cast<size_t>(Rand(static_cast<int>(roam_item_pool.size())))];
				save.items[id]++;
				for(const auto& def : items)
					if(def.id == id)
						sub += epro::format(L"   ไอเท็ม: {}", def.name);
			}
		} else {
			save.losses++;
			save.gold += lose_gold;
			sub = epro::format(L"ทอง +{}", lose_gold);
		}
		RerollDuelists(false, sel);   // this duelist leaves, a new one arrives
		WriteSave();
		RefreshHub();
		ShowReveal(result == 0 ? L"ชนะ!" : L"แพ้...", sub, cards);
	});
}

void DuelWorld::OnStoryResult(const StoryChapter& ch, size_t /*chapter_index*/, int result, bool first_clear) {
	if(!save.started)
		return;
	std::wstring sub;
	std::vector<RevealCard> cards;
	const std::string deck_file = BufferIO::EncodeUTF8(ch.deck);
	if(result == 0) {
		save.wins++;
		const int gems = first_clear ? story_first_gems + ch.reward_gems : story_repeat_gems;
		const int gold = first_clear ? story_first_gold : story_repeat_gold;
		save.gems += gems;
		save.gold += gold;
		sub = epro::format(L"ทอง +{}   เพชร +{}", Num(gold), gems);
		if(first_clear || Rand(100) < 50) {
			const auto code = RandomCardFromDeck(deck_file, first_clear);
			if(code) {
				cards.push_back({ code, RarityOf(code), Owned(code) == 0 });
				AddCard(code);
			}
		}
		if(first_clear) {
			// a ticket for the pack of this era
			for(const auto& def : items) {
				if(def.type != "pack_ticket")
					continue;
				for(const auto& p : packs) {
					if(p.id == def.pack && p.arc == ch.arc) {
						save.items[def.id]++;
						sub += epro::format(L"   ไอเท็ม: {}", def.name);
						goto done;
					}
				}
			}
		done:;
		}
	} else {
		save.losses++;
		save.gold += lose_gold;
		sub = epro::format(L"ทอง +{}", lose_gold);
	}
	WriteSave();
	ShowReveal(result == 0 ? (first_clear ? L"ผ่านบทครั้งแรก!" : L"ชนะ!") : L"แพ้...", sub, cards);
}

/* ================================================================== structure decks / missions data */

std::vector<uint32_t> DuelWorld::Featured(const std::vector<uint32_t>& cards, size_t n) const {
	std::vector<uint32_t> uniq;
	std::set<uint32_t> seen;
	for(auto c : cards)
		if(seen.insert(Key(c)).second)
			uniq.push_back(c);
	std::stable_sort(uniq.begin(), uniq.end(), [this](uint32_t a, uint32_t b) {
		const auto ra = RarityOf(a), rb = RarityOf(b);
		if(ra != rb)
			return ra > rb;
		const auto* da = gDataManager->GetCardData(a);
		const auto* db = gDataManager->GetCardData(b);
		const int aa = da ? da->attack : 0, ab = db ? db->attack : 0;
		return aa > ab;
	});
	if(uniq.size() > n)
		uniq.resize(n);
	return uniq;
}

void DuelWorld::LoadExtraData() {
	structs.clear();
	missions.clear();
	tracked_codes.clear();
	nlohmann::json j;
	if(ReadJson(EPRO_TEXT("./story/world/structures.json"), j) && j.is_object() && j.contains("structures")) {
		for(const auto& it : j["structures"]) {
			StructDef d;
			d.id = S(it, "id");
			d.name = W(it, "name", L"???");
			d.desc = W(it, "desc");
			d.cat = S(it, "cat");
			d.file = S(it, "file");
			d.price = I(it, "price", 500);
			d.cover = static_cast<uint32_t>(I(it, "cover", 0));
			FileStream file{ Utils::ToPathString(d.file), FileStream::in };
			if(!file.good())
				continue;
			std::string line;
			int section = 0;   // 0 main, 1 extra, 2 side
			while(std::getline(file, line)) {
				while(!line.empty() && (line.back() == '\r' || line.back() == ' '))
					line.pop_back();
				if(line.empty())
					continue;
				if(line[0] == '#' || line[0] == '!') {
					if(line == "#extra")
						section = 1;
					else if(line == "!side")
						section = 2;
					else if(line == "#main")
						section = 0;
					continue;
				}
				try {
					const auto code = static_cast<uint32_t>(std::stoul(line));
					const auto* data = gDataManager->GetCardData(code);
					if(!data || (data->type & 0x8000000))   // no skill cards
						continue;
					d.cards.push_back(code);
					if(section == 0)
						d.main_count++;
					else if(section == 1)
						d.extra_count++;
				}
				catch(...) {}
			}
			if(d.id.empty() || d.cards.empty())
				continue;
			d.featured = Featured(d.cards, 7);
			if(!d.cover && !d.featured.empty())
				d.cover = d.featured.front();
			structs.push_back(std::move(d));
		}
	}
	nlohmann::json m;
	if(ReadJson(EPRO_TEXT("./story/world/missions.json"), m) && m.is_object()) {
		auto read = [&](const char* key, bool daily) {
			if(!m.contains(key))
				return;
			for(const auto& it : m[key]) {
				MissionDef d;
				d.daily = daily;
				d.id = S(it, "id");
				d.name = W(it, "name", L"???");
				d.desc = W(it, "desc");
				d.stat = S(it, "stat");
				d.target = std::max<int64_t>(1, it.value("target", int64_t{ 1 }));
				d.gems = I(it, "gems", 0);
				d.gold = I(it, "gold", 0);
				d.item = S(it, "item");
				d.item_n = I(it, "item_n", d.item.empty() ? 0 : 1);
				d.skill = it.value("skill", 0u);
				d.card = it.value("card", 0u);
				if(d.id.empty() || d.stat.empty())
					continue;
				if(d.stat.rfind("sp_", 0) == 0) {
					try {
						tracked_codes.insert(static_cast<uint32_t>(std::stoul(d.stat.substr(3))));
					}
					catch(...) {}
				}
				missions.push_back(std::move(d));
			}
		};
		read("daily", true);
		read("achievements", false);
	}
}

/* ================================================================== duel statistics */

void DuelWorld::DuelBegin(const StoryChapter& ch) {
	std::lock_guard<epro::mutex> lock(duel_mutex);
	duel_stats.clear();
	duel_tracking = save.started && !ch.sandbox && ch.kind != "sandbox";
}

void DuelWorld::DuelSummon(uint32_t code, bool special) {
	std::lock_guard<epro::mutex> lock(duel_mutex);
	if(!duel_tracking)
		return;
	duel_stats["summon"]++;
	if(!special)
		duel_stats["summon_normal"]++;
	const auto* d = gDataManager->GetCardData(code);
	if(special && d) {
		duel_stats["summon_special"]++;
		if(d->type & TYPE_SYNCHRO) duel_stats["summon_synchro"]++;
		if(d->type & TYPE_XYZ) duel_stats["summon_xyz"]++;
		if(d->type & TYPE_FUSION) duel_stats["summon_fusion"]++;
		if(d->type & TYPE_LINK) duel_stats["summon_link"]++;
		if(d->type & TYPE_RITUAL) duel_stats["summon_ritual"]++;
		if(d->type & TYPE_PENDULUM) duel_stats["summon_pendulum"]++;
		if(d->type & (TYPE_SYNCHRO | TYPE_XYZ | TYPE_FUSION | TYPE_LINK)) duel_stats["summon_extra"]++;
	}
	const auto key = Key(code);
	if(tracked_codes.count(key))
		duel_stats["sp_" + std::to_string(key)]++;
	else if(tracked_codes.count(code))
		duel_stats["sp_" + std::to_string(code)]++;
}

void DuelWorld::DuelDamage(uint32_t amount) {
	std::lock_guard<epro::mutex> lock(duel_mutex);
	if(duel_tracking)
		duel_stats["damage"] += amount;
}

void DuelWorld::DuelSkillUsed() {
	std::lock_guard<epro::mutex> lock(duel_mutex);
	if(duel_tracking)
		duel_stats["skill_use"]++;
}

void DuelWorld::CheckDailyReset() {
	const auto today = Today();
	if(save.daily_day != today) {
		save.daily_day = today;
		save.daily_stats.clear();
		save.daily_claimed.clear();
	}
}

void DuelWorld::AddStat(const std::string& stat, int64_t n) {
	if(!save.started || n == 0)
		return;
	CheckDailyReset();
	save.stats[stat] += n;
	save.daily_stats[stat] += n;
}

void DuelWorld::OnDuelEnd(const StoryChapter& ch, int result) {
	std::map<std::string, int64_t> st;
	{
		std::lock_guard<epro::mutex> lock(duel_mutex);
		if(!duel_tracking)
			return;
		duel_tracking = false;
		st.swap(duel_stats);
	}
	if(!save.started)
		return;
	st["duel_play"]++;
	if(result == 0) {
		st["duel_win"]++;
		st[ch.kind + "_win"]++;
	}
	for(const auto& [k, v] : st)
		AddStat(k, v);
	int64_t xp = result == 0 ? 100 : 30;
	if(ch.kind == "ranked")
		xp += result == 0 ? 50 : 0;
	else if(ch.kind == "campaign" || ch.kind == "story")
		xp += result == 0 ? 50 : 0;
	std::wstring msg;
	AddXp(xp, msg);
	pending_levelup = msg;
	WriteSave();
}

/* ================================================================== missions */

int64_t DuelWorld::MissionProgress(const MissionDef& m) const {
	if(m.stat == "cards_owned")
		return static_cast<int64_t>(save.cards.size());
	if(m.stat == "story_clear") {
		int64_t n = 0;
		for(size_t i = 0; i < gStory.Chapters().size(); i++)
			if(gStory.ChapterCleared(i))
				n++;
		return n;
	}
	if(m.stat == "skills_owned")
		return static_cast<int64_t>(save.skills.size());
	const auto& src = m.daily ? save.daily_stats : save.stats;
	auto it = src.find(m.stat);
	return it == src.end() ? 0 : it->second;
}

bool DuelWorld::MissionDone(const MissionDef& m) const {
	return MissionProgress(m) >= m.target;
}

bool DuelWorld::MissionClaimed(const MissionDef& m) const {
	return m.daily ? save.daily_claimed.count(m.id) != 0 : save.claimed.count(m.id) != 0;
}

int DuelWorld::ClaimableMissions() const {
	int n = 0;
	for(const auto& m : missions)
		if(!MissionClaimed(m) && MissionDone(m))
			n++;
	return n;
}

std::wstring DuelWorld::MissionReward(const MissionDef& m) const {
	std::vector<std::wstring> parts;
	if(m.gems)
		parts.push_back(epro::format(L"เพชร {}", m.gems));
	if(m.gold)
		parts.push_back(epro::format(L"ทอง {}", Num(m.gold)));
	if(!m.item.empty()) {
		std::wstring name = BufferIO::DecodeUTF8(m.item);
		for(const auto& d : items)
			if(d.id == m.item)
				name = d.name;
		parts.push_back(epro::format(L"{} x{}", name, std::max(1, m.item_n)));
	}
	if(m.skill) {
		std::wstring name = L"???";
		for(const auto& s : skills)
			if(s.code == m.skill)
				name = s.name;
		parts.push_back(epro::format(L"สกิล \"{}\"", name));
	}
	if(m.card)
		parts.push_back(epro::format(L"การ์ด {}", gDataManager->GetName(m.card)));
	std::wstring out;
	for(size_t i = 0; i < parts.size(); i++)
		out += (i ? L" + " : L"") + parts[i];
	return out.empty() ? L"-" : out;
}

std::wstring DuelWorld::SkillUnlockText(uint32_t code) const {
	for(const auto& m : missions)
		if(m.skill == code)
			return epro::format(L"ปลดล็อกฟรีจากภารกิจ: {}", m.name);
	return {};
}

std::vector<size_t> DuelWorld::MissionOrder() const {
	// claimable first, then in progress (daily before achievements), then claimed
	std::vector<size_t> order;
	for(int pass = 0; pass < 3; pass++)
		for(size_t i = 0; i < missions.size(); i++) {
			const auto& m = missions[i];
			const bool claimed = MissionClaimed(m), done = MissionDone(m);
			const int group = claimed ? 2 : (done ? 0 : 1);
			if(group == pass)
				order.push_back(i);
		}
	return order;
}

void DuelWorld::RefreshMissions() {
	if(!lstMissions)
		return;
	CheckDailyReset();
	const int old = lstMissions->getSelected();
	lstMissions->clear();
	mission_rows = MissionOrder();
	for(size_t row = 0; row < mission_rows.size(); row++) {
		const auto& m = missions[mission_rows[row]];
		const auto prog = std::min(MissionProgress(m), m.target);
		const bool claimed = MissionClaimed(m), done = MissionDone(m);
		std::wstring status = claimed ? L"✓ รับแล้ว" : done ? L"★ รับรางวัลได้!" : epro::format(L"{}/{}", Num(prog), Num(m.target));
		lstMissions->addItem(epro::format(L"{} {}  -  {}", m.daily ? L"[รายวัน]" : L"[ภารกิจ]", m.name, status).data());
		SColor col = claimed ? SColor(255, 120, 120, 130) : done ? SColor(255, 120, 255, 140) : (m.skill ? SColor(255, 255, 210, 90) : SColor(255, 230, 230, 240));
		lstMissions->setItemOverrideColor(static_cast<irr::u32>(row), col);
	}
	if(lstMissions->getItemCount() == 0)
		lstMissions->addItem(L"(ไม่มีภารกิจ - story/world/missions.json)");
	lstMissions->setSelected(old >= 0 && static_cast<irr::u32>(old) < lstMissions->getItemCount() ? old : 0);
	const int n = ClaimableMissions();
	if(tabMissions)
		tabMissions->setText(n > 0 ? epro::format(L"ภารกิจ ({})", n).data() : L"ภารกิจ");
	btnClaimAll->setEnabled(n > 0);
	btnClaimAll->setText(n > 0 ? epro::format(L"รับรางวัลทั้งหมด ({})", n).data() : L"รับรางวัลทั้งหมด");
	UpdateMissionInfo();
}

void DuelWorld::UpdateMissionInfo() {
	const int sel = lstMissions->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= mission_rows.size()) {
		SetText(stMission, L"ภารกิจรายวันรีเซ็ตทุกวัน ภารกิจหลักทำได้ครั้งเดียว");
		btnClaim->setEnabled(false);
		return;
	}
	const auto& m = missions[mission_rows[static_cast<size_t>(sel)]];
	const auto prog = std::min(MissionProgress(m), m.target);
	const bool claimed = MissionClaimed(m), done = MissionDone(m);
	SetText(stMission, epro::format(L"{}\n{}\n\n{}\nความคืบหน้า: {} / {}\n\nรางวัล: {}\n\n{}",
									m.daily ? L"ภารกิจรายวัน (รีเซ็ตทุกวัน)" : L"ภารกิจหลัก",
									m.name, m.desc, Num(prog), Num(m.target), MissionReward(m),
									claimed ? L"รับรางวัลไปแล้ว" : done ? L"สำเร็จแล้ว! กดรับรางวัลได้เลย" : L"ยังไม่สำเร็จ"));
	btnClaim->setEnabled(done && !claimed);
}

bool DuelWorld::ClaimMission(size_t index, std::wstring& summary, std::vector<RevealCard>& cards) {
	if(index >= missions.size())
		return false;
	const auto& m = missions[index];
	if(MissionClaimed(m) || !MissionDone(m))
		return false;
	(m.daily ? save.daily_claimed : save.claimed).insert(m.id);
	save.gems += m.gems;
	save.gold += m.gold;
	if(!m.item.empty())
		save.items[m.item] += std::max(1, m.item_n);
	if(m.skill)
		save.skills.insert(m.skill);
	if(m.card && gDataManager->GetCardData(m.card)) {
		cards.push_back({ m.card, RarityOf(m.card), Owned(m.card) == 0 });
		AddCard(m.card);
	}
	summary += (summary.empty() ? L"" : L"\n") + epro::format(L"{}: {}", m.name, MissionReward(m));
	return true;
}

void DuelWorld::ClaimSelected(bool all) {
	CheckDailyReset();
	std::wstring summary;
	std::vector<RevealCard> cards;
	int n = 0;
	if(all) {
		for(size_t i = 0; i < missions.size(); i++)
			n += ClaimMission(i, summary, cards) ? 1 : 0;
	} else {
		const int sel = lstMissions->getSelected();
		if(sel >= 0 && static_cast<size_t>(sel) < mission_rows.size())
			n += ClaimMission(mission_rows[static_cast<size_t>(sel)], summary, cards) ? 1 : 0;
	}
	if(n == 0)
		return;
	WriteSave();
	RefreshHub();
	if(!cards.empty())
		ShowReveal(L"รางวัลภารกิจ!", epro::format(L"รับรางวัล {} ภารกิจ", n), cards);
	Toast(epro::format(L"รับรางวัลภารกิจแล้ว!\n\n{}", summary));
}

/* ================================================================== structure decks */

void DuelWorld::RefreshStructs() {
	if(!lstStructs)
		return;
	const int sel = lstStructs->getSelected();
	lstStructs->clear();
	for(size_t i = 0; i < structs.size(); i++) {
		const auto& d = structs[i];
		const wchar_t* tag = d.cat == "legend" ? L"[ตำนาน]" : d.cat == "meta" ? L"[เมต้า]" : L"[คลาสสิก]";
		auto it = save.structs_bought.find(d.id);
		const int bought = it == save.structs_bought.end() ? 0 : it->second;
		lstStructs->addItem(epro::format(L"{} {}{}", tag, d.name, bought ? L"  ✓" : L"").data());
		lstStructs->setItemOverrideColor(static_cast<irr::u32>(i), d.cat == "legend" ? SColor(255, 255, 210, 90)
										 : d.cat == "meta" ? SColor(255, 255, 120, 150) : SColor(255, 150, 200, 255));
	}
	if(structs.empty())
		lstStructs->addItem(L"(ยังไม่มี Structure Deck)");
	lstStructs->setSelected(sel >= 0 && static_cast<irr::u32>(sel) < lstStructs->getItemCount() ? sel : 0);
	UpdateStructInfo();
}

void DuelWorld::UpdateStructInfo() {
	const int sel = lstStructs->getSelected();
	featStructCodes.clear();
	if(sel < 0 || static_cast<size_t>(sel) >= structs.size()) {
		SetText(stStruct, L"Structure Deck = เด็คสำเร็จรูปพร้อมเล่น ซื้อแล้วได้การ์ดครบทั้งเด็ค + บันทึกเป็นเด็คให้ทันที");
		btnStructBuy->setEnabled(false);
		return;
	}
	const auto& d = structs[static_cast<size_t>(sel)];
	featStructCodes = d.featured;
	auto it = save.structs_bought.find(d.id);
	const int bought = it == save.structs_bought.end() ? 0 : it->second;
	int ur = 0, sr = 0;
	for(auto c : d.cards) {
		const auto r = RarityOf(c);
		ur += r == Rarity::UR;
		sr += r == Rarity::SR;
	}
	SetText(stStruct, epro::format(L"{}\n{}\n\nเมนเด็ค {} ใบ  |  เอ็กซ์ตร้า {} ใบ\nUR {} ใบ  |  SR {} ใบ\nซื้อแล้ว: {} ครั้ง\n\nได้การ์ดทุกใบเข้าคลัง + เด็คชื่อ \"[Structure] {}\"",
								   d.name, d.desc, d.main_count, d.extra_count, ur, sr, bought, d.name));
	btnStructBuy->setText(epro::format(L"{} ({} เพชร)", bought ? L"ซื้ออีกชุด" : L"ซื้อ Structure Deck", Num(d.price)).data());
	btnStructBuy->setEnabled(true);
}

void DuelWorld::BuyStruct() {
	const int sel = lstStructs->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= structs.size())
		return;
	const auto& d = structs[static_cast<size_t>(sel)];
	if(save.gems < d.price) {
		Toast(epro::format(L"เพชรไม่พอ (ต้องการ {} มี {})\nเพชรได้จากภารกิจ เนื้อเรื่อง และแคมเปญ", Num(d.price), Num(save.gems)));
		return;
	}
	save.gems -= d.price;
	std::vector<RevealCard> show;
	std::set<uint32_t> seen;
	for(auto c : d.cards) {
		const bool is_new = Owned(c) == 0;
		AddCard(c);
		if(seen.insert(Key(c)).second && RarityOf(c) >= Rarity::SR)
			show.push_back({ c, RarityOf(c), is_new });
	}
	save.structs_bought[d.id]++;
	// copy the deck list (without skill cards / unknown codes) into the player's deck folder
	{
		FileStream in{ Utils::ToPathString(d.file), FileStream::in };
		std::string text, line;
		while(std::getline(in, line)) {
			while(!line.empty() && (line.back() == '\r' || line.back() == ' '))
				line.pop_back();
			if(!line.empty() && line[0] != '#' && line[0] != '!') {
				try {
					const auto code = static_cast<uint32_t>(std::stoul(line));
					const auto* data = gDataManager->GetCardData(code);
					if(!data || (data->type & 0x8000000))
						continue;
				}
				catch(...) {
					continue;
				}
			}
			text += line + "\n";
		}
		FileStream out{ epro::format(EPRO_TEXT("./deck/[Structure] {}.ydk"), Utils::ToPathString(d.name)), FileStream::out | FileStream::trunc };
		out << text;
	}
	AddWorldDeck(L"[Structure] " + d.name);
	AddStat("structure_buy");
	WriteSave();
	RefreshHub();
	std::stable_sort(show.begin(), show.end(), [](const RevealCard& a, const RevealCard& b) { return a.rarity < b.rarity; });
	if(show.size() > 8)
		show.erase(show.begin(), show.begin() + static_cast<std::ptrdiff_t>(show.size() - 8));
	ShowReveal(epro::format(L"ได้รับ {}!", d.name), epro::format(L"การ์ด {} ใบเข้าคลัง  |  เด็ค \"[Structure] {}\" พร้อมใช้", d.cards.size(), d.name), show);
}

void DuelWorld::BuyFreePack() {
	const int sel = lstPacks->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= packs.size())
		return;
	if(save.free_pack_day == Today()) {
		Toast(L"วันนี้รับซองฟรีไปแล้ว พรุ่งนี้มาใหม่นะ!");
		return;
	}
	save.free_pack_day = Today();
	const auto& p = packs[static_cast<size_t>(sel)];
	ShowReveal(p.name, L"ซองฟรีประจำวัน!", OpenPack(p));
	AddStat("pack_open");
	WriteSave();
	RefreshHub();
}

/* ================================================================== Duel Links: level, Gate, Ranked */

int64_t DuelWorld::XpForLevel(int level) {
	return 400 + 200 * static_cast<int64_t>(std::max(0, level - 1));
}

int DuelWorld::Level() const {
	int level = 1;
	int64_t xp = save.xp;
	while(xp >= XpForLevel(level) && level < 999) {
		xp -= XpForLevel(level);
		level++;
	}
	return level;
}

void DuelWorld::AddXp(int64_t xp, std::wstring& msg) {
	const int before = Level();
	save.xp += xp;
	const int after = Level();
	for(int lv = before + 1; lv <= after; lv++) {
		const int gems = 30 + 5 * std::min(lv, 40);
		save.gems += gems;
		msg += epro::format(L"เลเวลอัป! Lv.{}  รางวัล เพชร +{}\n", lv, gems);
		if(lv % 10 == 0) {
			save.items["ticket_staples"]++;
			msg += L"  + ตั๋วซอง Hand Trap & Staple\n";
		}
	}
}

void DuelWorld::ApplyAnimSpeed() {
	gGameConfig->quick_animation = save.anim_fast;
	gFx.speed = save.anim_fast ? 1.8f : 1.0f;
}

irr::gui::IGUIComboBox* DuelWorld::DeckComboFor(int which) {
	return which == 1 ? cbGateDeck : which == 2 ? cbRankDeck : cbDeck;
}

std::wstring DuelWorld::PickedDeck(irr::gui::IGUIComboBox* cb) {
	if(!cb || cb->getSelected() < 0)
		return {};
	return cb->getItem(static_cast<irr::u32>(cb->getSelected()));
}

/* ---------------- Gate */

namespace {
struct GateLevel {
	int lv;
	uint32_t skill;
	int gold, gems, drop, cards;
	bool sr_plus;
};
const GateLevel GATE_LEVELS[4] = {
	{ 10, 0,         300, 10,  50, 1, false },
	{ 20, 300990030, 500, 20,  70, 1, false },
	{ 30, 300990031, 800, 35, 100, 1, true },
	{ 40, 300990032, 1200, 60, 100, 2, true },
};
}

void DuelWorld::FillSkillCombo(irr::gui::IGUIComboBox* cb) {
	// Duel World: only owned skills, the selection is the equipped skill
	if(!cb)
		return;
	cb->clear();
	cb->addItem(L"ไม่ใช้สกิล", 0);
	if(save.started) {
		for(const auto& sk : Skills()) {
			if(!SkillOwned(sk.code))
				continue;
			const wchar_t* tier = sk.tier == "god" ? L"[เทพ] " : sk.tier == "legendary" ? L"[ตำนาน] " : sk.tier == "broken" ? L"[โกง] " : L"";
			const auto idx = cb->addItem(epro::format(L"{}{} ({})", tier, sk.name, sk.name_en).data(), sk.code);
			if(sk.code == save.equipped_skill)
				cb->setSelected(static_cast<irr::s32>(idx));
		}
	}
	if(cb->getSelected() < 0)
		cb->setSelected(0);
}

void DuelWorld::EquipFromCombo(irr::gui::IGUIComboBox* cb) {
	if(!cb || !save.started)
		return;
	const int sel = cb->getSelected();
	save.equipped_skill = sel > 0 ? cb->getItemData(static_cast<irr::u32>(sel)) : 0;
	WriteSave();
	if(lstSkills)
		RefreshSkills();
	RefreshSkillCombos();
}

void DuelWorld::ShowComboSkillInfo(irr::gui::IGUIComboBox* cb) {
	const int sel = cb ? cb->getSelected() : -1;
	ShowSkillInfo(sel > 0 ? cb->getItemData(static_cast<irr::u32>(sel)) : 0);
}

void DuelWorld::RefreshSkillCombos() {
	for(auto* cb : { cbGateSkill, cbRankSkill, cbRoamSkill })
		FillSkillCombo(cb);
}

/* ---------------- Duel World deck category */

void DuelWorld::AddWorldDeck(const std::wstring& name) {
	if(name.empty())
		return;
	save.world_decks_known = true;
	if(save.world_decks.insert(name).second)
		WriteSave();
}

void DuelWorld::RemoveWorldDeck(const std::wstring& name) {
	if(save.world_decks.erase(name))
		WriteSave();
}

void DuelWorld::RenameWorldDeck(const std::wstring& from, const std::wstring& to) {
	if(!save.world_decks.erase(from))
		return;
	save.world_decks.insert(to);
	WriteSave();
}

void DuelWorld::OpenDeckEditor(bool new_deck) {
	// same as the main menu "Deck Edit" button, but only the Duel World decks
	FillWorldDecks(mainGame->cbDBDecks);
	if(mainGame->cbDBDecks->getSelected() != -1) {
		mainGame->deckBuilder.SetCurrentDeckFromFile(Utils::ToPathString(mainGame->cbDBDecks->getItem(static_cast<irr::u32>(mainGame->cbDBDecks->getSelected()))), true);
	}
	mainGame->ebDeckname->setText(L"");
	mainGame->HideElement(wHub);
	return_to_hub = true;
	mainGame->deckBuilder.Initialize();
	if(new_deck)
		mainGame->deckBuilder.NewDeck();
}

void DuelWorld::FillWorldDecks(irr::gui::IGUIComboBox* cb) {
	if(!cb)
		return;
	cb->clear();
	std::vector<std::wstring> files;
	for(auto& file : Utils::FindFiles(DeckManager::GetDeckFolder(), { EPRO_TEXT("ydk") })) {
		file.erase(file.size() - 4);
		files.push_back(Utils::ToUnicodeIfNeeded(file));
	}
	if(!save.world_decks_known) {
		// old save: every deck that can be built from the collection becomes a Duel World deck
		save.world_decks_known = true;
		for(const auto& name : files) {
			Deck deck;
			if(DeckManager::LoadDeckFromFile(Utils::ToPathString(name), deck) && !deck.main.empty() && CheckDeck(deck, false))
				save.world_decks.insert(name);
		}
		WriteSave();
	}
	bool pruned = false;
	for(auto it = save.world_decks.begin(); it != save.world_decks.end();) {
		if(std::find(files.begin(), files.end(), *it) == files.end()) {
			it = save.world_decks.erase(it);
			pruned = true;
		} else
			++it;
	}
	if(pruned)
		WriteSave();
	for(const auto& name : files)
		if(save.world_decks.count(name))
			cb->addItem(name.data());
	for(irr::u32 i = 0; i < cb->getItemCount(); i++)
		if(gGameConfig->lastdeck == cb->getItem(i))
			cb->setSelected(static_cast<irr::s32>(i));
	if(cb->getSelected() < 0 && cb->getItemCount() > 0)
		cb->setSelected(0);
}

void DuelWorld::RefreshGate() {
	if(!lstGate)
		return;
	const auto& chapters = gStory.Chapters();
	const int old = lstGate->getSelected();
	lstGate->clear();
	gate_rows.clear();
	std::set<std::wstring> seen;
	for(size_t i = 0; i < chapters.size(); i++) {
		if(!gStory.ChapterUnlocked(i) || !seen.insert(chapters[i].opponent).second)
			continue;
		int best = 0;
		for(int l = 3; l >= 0; l--)
			if(save.gate_clears.count(chapters[i].id + ":" + std::to_string(GATE_LEVELS[l].lv))) {
				best = GATE_LEVELS[l].lv;
				break;
			}
		gate_rows.push_back(static_cast<int>(i));
		const auto idx = lstGate->addItem(epro::format(L"{}  [{}]{}", chapters[i].opponent, chapters[i].arc,
													   best ? epro::format(L"  ✓Lv.{}", best) : std::wstring()).data());
		if(best >= 40)
			lstGate->setItemOverrideColor(idx, SColor(255, 255, 205, 80));
	}
	if(gate_rows.empty())
		lstGate->addItem(L"(ผ่านเนื้อเรื่องเพื่อปลดล็อก Gate)");
	lstGate->setSelected(old >= 0 && static_cast<irr::u32>(old) < lstGate->getItemCount() ? old : 0);
	UpdateGateInfo();
}

void DuelWorld::UpdateGateInfo() {
	const int sel = lstGate->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= gate_rows.size()) {
		SetText(stGate, L"Gate: ท้าดวลตัวละครในตำนานซ้ำได้ไม่จำกัด ยิ่งเลเวลสูงยิ่งได้การ์ดจากเด็คของตัวละครมาก (ปลดล็อกจากเนื้อเรื่อง)");
		btnGateDuel->setEnabled(false);
		return;
	}
	const auto& ch = gStory.Chapters()[static_cast<size_t>(gate_rows[static_cast<size_t>(sel)])];
	const int l = std::max(0, std::min(3, cbGateLv->getSelected()));
	const auto& g = GATE_LEVELS[l];
	std::wstring clears;
	for(const auto& gl : GATE_LEVELS) {
		auto it = save.gate_clears.find(ch.id + ":" + std::to_string(gl.lv));
		clears += epro::format(L"Lv.{} {}   ", gl.lv, it == save.gate_clears.end() ? std::wstring(L"-") : epro::format(L"ชนะ {}", it->second));
	}
	const bool first = !save.gate_clears.count(ch.id + ":" + std::to_string(g.lv));
	SetText(stGate, epro::format(L"{}  (Gate Lv.{})\n{}\n\nรางวัลเมื่อชนะ: ทอง {}  เพชร {}{}\nการ์ดจากเด็ค {}: {}% x{} ใบ{}\n\n{}\nกติกาปกติ (Master Rule): LP 8000 การ์ดเริ่ม 5 ใบ + สกิลที่ติดตั้ง",
								 ch.opponent, g.lv, ch.arc, Num(g.gold), g.gems, first ? L"  (+100 เพชร ชนะครั้งแรก)" : L"",
								 ch.opponent, g.drop, g.cards, g.sr_plus ? L" (SR ขึ้นไป)" : L"", clears));
	btnGateDuel->setEnabled(true);
}

void DuelWorld::StartGateDuel() {
	const int sel = lstGate->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= gate_rows.size())
		return;
	const std::wstring deck_name = PickedDeck(cbGateDeck);
	if(deck_name.empty()) {
		Toast(L"ยังไม่มีเด็ค Duel World - ซื้อ Structure Deck หรือจัดเด็คจากการ์ดที่มี (ปุ่มสร้างเด็ค)");
		return;
	}
	if(!CheckDeckByName(deck_name))
		return;
	const int l = std::max(0, std::min(3, cbGateLv->getSelected()));
	const auto g = GATE_LEVELS[l];
	StoryChapter ch = gStory.Chapters()[static_cast<size_t>(gate_rows[static_cast<size_t>(sel)])];
	const std::string chapter_id = ch.id;
	ch.kind = "gate";
	ch.start_lp = 8000;
	ch.title = epro::format(L"Gate Lv.{}", g.lv);
	if(g.skill)
		ch.bot_skill = g.skill;
	ch.skill_label = g.skill ? epro::format(L"Gate Lv.{}", g.lv) : std::wstring();
	ch.intro = { { ch.opponent, epro::format(L"มาท้าดวลฉันที่ Lv.{} เหรอ? ดี! แสดงฝีมือให้ดูหน่อย!", g.lv) } };
	ch.win = { { ch.opponent, L"เก่งมาก... การ์ดใบนี้เป็นของนายแล้ว" } };
	ch.lose = { { ch.opponent, L"ยังไม่พอหรอก กลับไปปรับเด็คมาใหม่นะ!" } };
	mainGame->HideElement(wHub);
	const std::string deck_file = BufferIO::EncodeUTF8(ch.deck);
	gStory.StartCustomDuel(ch, deck_name, wHub, [this, g, chapter_id, deck_file](int result) {
		std::wstring sub;
		std::vector<RevealCard> cards;
		if(result == 0) {
			save.wins++;
			const std::string key = chapter_id + ":" + std::to_string(g.lv);
			const bool first = save.gate_clears[key]++ == 0;
			const int gems = g.gems + (first ? 100 : 0);
			save.gold += g.gold;
			save.gems += gems;
			sub = epro::format(L"ทอง +{}   เพชร +{}", Num(g.gold), gems);
			for(int i = 0; i < g.cards; i++) {
				if(Rand(100) >= g.drop)
					continue;
				const auto code = RandomCardFromDeck(deck_file, g.sr_plus);
				if(code) {
					cards.push_back({ code, RarityOf(code), Owned(code) == 0 });
					AddCard(code);
				}
			}
		} else {
			save.losses++;
			save.gold += lose_gold;
			sub = epro::format(L"ทอง +{}", lose_gold);
		}
		WriteSave();
		RefreshHub();
		ShowReveal(result == 0 ? epro::format(L"ชนะ Gate Lv.{}!", g.lv) : std::wstring(L"แพ้..."), sub, cards);
	});
}

/* ---------------- Ranked */


int DuelWorld::RankTier(int stage) {
	if(stage <= 0)
		return 0;
	if(stage >= RANK_LAST)
		return 6;
	return 1 + (stage - 1) / 5;
}

std::wstring DuelWorld::RankName(int stage) {
	const int tier = RankTier(stage);
	if(tier == 0 || tier == 6)
		return RANK_TIERS[tier];
	return epro::format(L"{} {}", RANK_TIERS[tier], 5 - (stage - 1) % 5);
}

int DuelWorld::StarsNeeded(int stage) {
	return stage == 0 ? 2 : 3;
}

void DuelWorld::CheckSeason(bool toast) {
	const uint32_t season = Today() / 100;   // yyyymm
	if(save.rank_season == season)
		return;
	if(save.rank_season != 0) {
		const int tier = RankTier(save.rank_best);
		const int gems = 100 + 150 * tier;
		save.gems += gems;
		if(tier >= 3)
			save.items["ticket_staples"] += tier - 2;
		// new season: back to the start of the tier below
		const int new_tier = std::max(0, RankTier(save.rank_stage) - 1);
		save.rank_stage = new_tier == 0 ? 0 : 1 + (new_tier - 1) * 5;
		save.rank_stars = 0;
		save.rank_best = save.rank_stage;
		if(toast)
			Toast(epro::format(L"ซีซั่นใหม่เริ่มแล้ว!\nรางวัลซีซั่นก่อน (แรงก์สูงสุด {}): เพชร +{}\nแรงก์เริ่มใหม่ที่ {}", RankName(RankTier(save.rank_best) ? save.rank_best : 0), gems, RankName(save.rank_stage)));
	}
	save.rank_season = season;
	WriteSave();
}

void DuelWorld::RefreshRank() {
	if(!stRank)
		return;
	CheckSeason(false);
	const int stage = save.rank_stage;
	const int tier = RankTier(stage);
	std::wstring stars;
	if(stage >= RANK_LAST)
		stars = epro::format(L"ชนะในแรงก์สูงสุด {} ครั้ง", save.rank_stars);
	else
		for(int i = 0; i < StarsNeeded(stage); i++)
			stars += i < save.rank_stars ? L"★ " : L"☆ ";
	stRankBig->setOverrideColor(RANK_COLORS[tier]);
	SetText(stRankBig, epro::format(L"แรงก์ปัจจุบัน\n{}\n{}", RankName(stage), stars));
	const uint32_t season = save.rank_season;
	SetText(stRank, epro::format(
		L"ซีซั่น {}/{}  (รีเซ็ตทุกต้นเดือน)\nแรงก์สูงสุดซีซั่นนี้: {}\nRanked: ชนะ {} / แพ้ {}\n\n"
		L"• ชนะ = +1 ดาว ครบดาว = เลื่อนขั้น (เพชร +50, ขึ้นระดับใหม่ เพชร +300)\n"
		L"• แพ้ตั้งแต่ Silver ขึ้นไป = -1 ดาว (ไม่ตกระดับ)\n"
		L"• คู่ต่อสู้สุ่มจากบอทตามระดับ ยิ่งสูงยิ่งเจอเด็คเมต้า\n"
		L"• กติกาปกติ (Master Rule) LP 8000 + สกิลที่ติดตั้ง\n"
		L"• จบซีซั่นได้รางวัลตามแรงก์สูงสุด",
		season % 100, season / 100, RankName(save.rank_best), save.rank_wins, save.rank_losses));
}

void DuelWorld::StartRankDuel() {
	const std::wstring deck_name = PickedDeck(cbRankDeck);
	if(deck_name.empty()) {
		Toast(L"ยังไม่มีเด็ค Duel World - ซื้อ Structure Deck หรือจัดเด็คจากการ์ดที่มี (ปุ่มสร้างเด็ค)");
		return;
	}
	if(!CheckDeckByName(deck_name))
		return;
	const int tier = RankTier(save.rank_stage);
	const auto* pool = gCampaign.Pool(RANK_POOLS[tier]);
	std::vector<size_t> candidates;
	const auto& bots = mainGame->gBot.bots;
	if(pool)
		for(const auto& name : *pool)
			for(size_t i = 0; i < bots.size(); i++)
				if(bots[i].name == name)
					candidates.push_back(i);
	if(candidates.empty())
		for(size_t i = 0; i < bots.size(); i++)
			if(&bots[i] != mainGame->gBot.genericEngine)
				candidates.push_back(i);
	if(candidates.empty()) {
		Toast(L"ไม่พบบอท (WindBot)");
		return;
	}
	const auto& bot = bots[candidates[static_cast<size_t>(Rand(static_cast<int>(candidates.size())))]];
	StoryChapter ch;
	ch.id = "ranked";
	ch.arc = L"Ranked";
	ch.title = RankName(save.rank_stage);
	ch.opponent = bot.name;
	ch.engine = bot.deck;
	ch.difficulty = bot.difficulty;
	ch.kind = "ranked";
	ch.quick = true;
	ch.start_lp = 8000;
	ch.intro = { { bot.name, epro::format(L"Ranked Duel - {} ! พร้อมนะ?", RankName(save.rank_stage)) } };
	mainGame->HideElement(wHub);
	gStory.StartCustomDuel(ch, deck_name, wHub, [this](int result) { OnRankResult(result); });
}

void DuelWorld::OnRankResult(int result) {
	std::wstring title, sub;
	const int tier_before = RankTier(save.rank_stage);
	if(result == 0) {
		save.wins++;
		save.rank_wins++;
		const int gold = 200 + 60 * tier_before, gems = 5 + 3 * tier_before;
		save.gold += gold;
		save.gems += gems;
		sub = epro::format(L"ทอง +{}   เพชร +{}", gold, gems);
		save.rank_stars++;
		title = L"ชนะ Ranked!";
		if(save.rank_stage < RANK_LAST && save.rank_stars >= StarsNeeded(save.rank_stage)) {
			save.rank_stage++;
			save.rank_stars = 0;
			const int tier_after = RankTier(save.rank_stage);
			if(tier_after != tier_before) {
				const int bonus = save.rank_stage >= RANK_LAST ? 1000 : 300;
				save.gems += bonus;
				save.items["ticket_staples"]++;
				title = epro::format(L"ขึ้นระดับ {}!", RankName(save.rank_stage));
				sub += epro::format(L"   โบนัสเลื่อนระดับ เพชร +{} + ตั๋วซอง Staple", bonus);
				AddStat("rank_tier_up");
			} else {
				save.gems += 50;
				title = epro::format(L"เลื่อนขั้นเป็น {}!", RankName(save.rank_stage));
				sub += L"   โบนัสเลื่อนขั้น เพชร +50";
			}
			save.rank_best = std::max(save.rank_best, save.rank_stage);
		}
	} else if(result == 1) {
		save.losses++;
		save.rank_losses++;
		save.gold += lose_gold;
		sub = epro::format(L"ทอง +{}", lose_gold);
		title = L"แพ้...";
		if(tier_before >= 2 && save.rank_stage < RANK_LAST) {
			if(save.rank_stars > 0)
				save.rank_stars--;
			else if(RankTier(save.rank_stage - 1) == tier_before) {
				save.rank_stage--;
				save.rank_stars = StarsNeeded(save.rank_stage) - 1;
				sub += epro::format(L"   ตกเป็น {}", RankName(save.rank_stage));
			}
		}
	} else {
		title = L"เสมอ";
	}
	WriteSave();
	RefreshHub();
	ShowReveal(title, sub, {});
}

/* ================================================================== skills */

void DuelWorld::LoadSkills() {
	skills.clear();
	skills_loaded = true;
	nlohmann::json j;
	if(!ReadJson(EPRO_TEXT("./story/world/skills.json"), j) || !j.is_object() || !j.contains("skills"))
		return;
	try {
		for(const auto& s : j["skills"]) {
			SkillDef def;
			def.code = s.value("code", 0u);
			def.name = W(s, "name");
			def.name_en = W(s, "name_en");
			def.desc = W(s, "desc");
			def.for_deck = W(s, "for");
			def.price = I(s, "price", 0);
			def.owned_default = s.value("owned", false);
			def.tier = S(s, "tier");
			if(def.code)
				skills.push_back(std::move(def));
		}
	}
	catch(...) {
	}
}

const std::vector<DuelWorld::SkillDef>& DuelWorld::Skills() {
	if(!skills_loaded)
		LoadSkills();
	return skills;
}

const DuelWorld::SkillDef* DuelWorld::FindSkill(uint32_t code) {
	for(const auto& s : Skills())
		if(s.code == code)
			return &s;
	return nullptr;
}

bool DuelWorld::SkillOwned(uint32_t code) const {
	if(save.skills.count(code))
		return true;
	for(const auto& s : skills)
		if(s.code == code)
			return s.owned_default;
	return false;
}

uint32_t DuelWorld::EquippedSkill() {
	if(!save.started || !save.equipped_skill)
		return 0;
	Skills();
	return SkillOwned(save.equipped_skill) ? save.equipped_skill : 0;
}

void DuelWorld::EquipSkill(uint32_t code) {
	if(!save.started)
		return;
	Skills();
	if(code && !SkillOwned(code))
		return;
	save.equipped_skill = code;
	WriteSave();
	RefreshSkills();
	RefreshSkillCombos();
}

std::wstring DuelWorld::SkillInfoText(const SkillDef& s) {
	std::wstring t = epro::format(L"สกิล: {}  ({})", s.name, s.name_en);
	if(!s.for_deck.empty())
		t += epro::format(L"\nเหมาะกับ: {}", s.for_deck);
	t += L"\n\n" + s.desc;
	t += L"\n\nวิธีใช้: การ์ดสกิลอยู่ซ้ายกลางสนาม\nเมื่อมีวงแสงหมุน = ใช้ได้ -> คลิกการ์ดสกิล แล้วกด \"ใช้สกิล\"";
	return t;
}

void DuelWorld::ShowSkillInfo(uint32_t code) {
	const auto* s = FindSkill(code);
	if(!s) {
		stSkillInfo->setText(L"ยังไม่ได้เลือกสกิล\n\nสกิลคือความสามารถพิเศษของผู้เล่น (แบบ Duel Links) ใช้ได้ระหว่างดวล\nเลือกสกิลจากช่องด้านซ้าย แล้วกดปุ่มนี้เพื่อดูคำอธิบาย");
	} else {
		stSkillInfo->setText(SkillInfoText(*s).data());
	}
	wSkillInfo->setVisible(true);
	// the modal screen was created before the menu windows: put it back on top of them
	mainGame->env->getRootGUIElement()->bringToFront(wSkillInfo->getParent());
	wSkillInfo->getParent()->bringToFront(wSkillInfo);
	mainGame->env->setFocus(wSkillInfo);
}

/* ---------------- daily login reward (7-day cycle) */

bool DuelWorld::LoginClaimable() const {
	return save.started && save.login_last != Today() && login_rewards.size() == 7;
}

int DuelWorld::LoginTodayIndex() const {
	// claimable: the next day of the cycle; already claimed: the day claimed today
	if(LoginClaimable())
		return save.login_count % 7;
	return (save.login_count + 6) % 7;
}

std::wstring DuelWorld::ItemLabel(const std::string& id) const {
	for(const auto& d : items)
		if(d.id == id)
			return d.name;
	return BufferIO::DecodeUTF8(id);
}

std::wstring DuelWorld::LoginRewardText(const LoginReward& r) const {
	std::wstring t;
	if(r.gems > 0)
		t += epro::format(L"เพชร +{}\n", r.gems);
	if(r.gold > 0)
		t += epro::format(L"ทอง +{}\n", Num(r.gold));
	if(!r.item.empty() && r.item_n > 0)
		t += epro::format(L"{} x{}\n", ItemLabel(r.item), r.item_n);
	return t;
}

void DuelWorld::ShowLogin() {
	if(!wLogin || login_rewards.size() != 7)
		return;
	RefreshLogin();
	wLogin->setVisible(true);
	mainGame->env->getRootGUIElement()->bringToFront(wLogin->getParent());
	wLogin->getParent()->bringToFront(wLogin);
}

void DuelWorld::RefreshLogin() {
	const int today = LoginTodayIndex();
	const bool can = LoginClaimable();
	for(int i = 0; i < 7; i++) {
		const bool done = i < today || (i == today && !can);
		std::wstring t = epro::format(L"วันที่ {}\n\n{}", i + 1, LoginRewardText(login_rewards[static_cast<size_t>(i)]));
		if(done)
			t += L"\n[รับแล้ว]";
		else if(i == today)
			t += L"\n>> วันนี้ <<";
		SetText(stLoginDays[static_cast<size_t>(i)], t);
		stLoginDays[static_cast<size_t>(i)]->setOverrideColor(done ? SColor(255, 140, 150, 160) : i == today ? SColor(255, 255, 220, 120) : SColor(255, 230, 235, 245));
	}
	SetText(stLoginInfo, epro::format(L"ล็อกอินรับรางวัลมาแล้ว {} วัน  |  รอบนี้วันที่ {}/7\n"
									  L"{}",
									  save.login_count, today + 1,
									  can ? L"กดรับรางวัลของวันนี้ได้เลย! (ขาดวันไหนก็ไม่รีเซ็ต รับต่อจากวันล่าสุด)"
										  : L"วันนี้รับแล้ว พรุ่งนี้มาใหม่นะ! (รีเซ็ตตอนเที่ยงคืน)"));
	btnLoginClaim->setEnabled(can);
	btnLoginClaim->setText(can ? L"รับรางวัล!" : L"รับแล้ววันนี้");
}

void DuelWorld::ClaimLogin() {
	if(!LoginClaimable())
		return;
	const auto& r = login_rewards[static_cast<size_t>(save.login_count % 7)];
	save.gems += r.gems;
	save.gold += r.gold;
	if(!r.item.empty() && r.item_n > 0)
		save.items[r.item] += r.item_n;
	save.login_last = Today();
	save.login_count++;
	WriteSave();
	RefreshHub();
	RefreshLogin();
	std::wstring got = LoginRewardText(r);
	std::replace(got.begin(), got.end(), L'\n', L' ');
	SetText(stLoginInfo, epro::format(L"ได้รับ: {}{}\nพรุ่งนี้มารับวันที่ {} ต่อนะ!", got,
									  r.item.empty() ? L"" : L" (ไอเท็มอยู่ในแท็บไอเท็ม)", save.login_count % 7 + 1));
	Toast(epro::format(L"รางวัลรายวัน: {}", got));
}

void DuelWorld::DrawLoginOverlay() {
	if(!wLogin || !wLogin->isVisible())
		return;
	// the hub fades in after the popup opened and would sit above it (eating the clicks): keep the popup on top
	auto* root = mainGame->env->getRootGUIElement();
	if(root->getChildren().empty() || *root->getChildren().getLast() != wLogin->getParent())
		root->bringToFront(wLogin->getParent());
	const int today = LoginTodayIndex();
	const bool can = LoginClaimable();
	auto* driver = mainGame->driver;
	for(int i = 0; i < 7; i++) {
		const auto rc = stLoginDays[static_cast<size_t>(i)]->getAbsolutePosition();
		const bool done = i < today || (i == today && !can);
		if(done)
			driver->draw2DRectangle(SColor(110, 20, 120, 60), rc);
		if(i == today) {
			const SColor gold(255, 255, 200, 60);
			for(int k = 0; k < 3; k++)
				driver->draw2DRectangleOutline(irr::core::recti(rc.UpperLeftCorner.X - k, rc.UpperLeftCorner.Y - k, rc.LowerRightCorner.X + k, rc.LowerRightCorner.Y + k), gold);
		}
	}
}

/* ---------------- shop card search */

void DuelWorld::BuildCardSources() {
	card_sources.clear();
	auto add = [&](uint32_t code, std::wstring text) {
		auto& v = card_sources[Key(code)];
		if(std::find(v.begin(), v.end(), text) == v.end())
			v.push_back(std::move(text));
	};
	for(const auto& p : packs)
		for(auto c : p.cards)
			add(c, epro::format(L"ซอง: {}  ({} เพชร/ซอง)", p.name, p.price));
	for(const auto& d : structs) {
		std::map<uint32_t, int> n;
		for(auto c : d.cards)
			n[Key(c)]++;
		for(const auto& [c, cnt] : n)
			add(c, epro::format(L"Structure Deck: {}  (x{}, {} เพชร)", d.name, cnt, Num(d.price)));
	}
	for(const auto& st : starters)
		for(auto c : ReadYdk("./story/world/starters/" + st.file))
			add(c, epro::format(L"เด็คเริ่มต้น: {}", st.name));
	std::set<std::wstring> seen;
	for(const auto& ch : gStory.Chapters()) {
		if(ch.deck.empty() || !seen.insert(ch.opponent + L"|" + ch.deck).second)
			continue;
		for(auto c : ReadYdk("./story/decks/" + BufferIO::EncodeUTF8(ch.deck)))
			add(c, epro::format(L"ดรอปจากการชนะ: {} (Gate / นักสู้สุ่ม)", ch.opponent));
	}
	for(const auto& m : missions)
		if(m.card)
			add(m.card, epro::format(L"รางวัลภารกิจ: {}", m.name));
	card_sources_built = true;
}

void DuelWorld::OpenCardSearch(const std::wstring& query) {
	if(!card_sources_built)
		BuildCardSources();
	search_title = L"ค้นหาการ์ด  (Card Search)";
	ebSearchPop->setText(query.data());
	wCardSearch->setVisible(true);
	mainGame->env->getRootGUIElement()->bringToFront(wCardSearch->getParent());
	wCardSearch->getParent()->bringToFront(wCardSearch);
	mainGame->env->setFocus(ebSearchPop);
	RunCardSearch(query);
}

void DuelWorld::RunCardSearch(const std::wstring& query) {
	search_results.clear();
	lstSearch->clear();
	const auto q = Utils::ToUpperNoAccents(query);
	if(q.size() < 2) {
		SetText(stSearchInfo, L"");
		SetText(stSearchSrc, L"พิมพ์ชื่อการ์ด (ภาษาอังกฤษ) อย่างน้อย 2 ตัวอักษร แล้วกด Enter หรือปุ่มค้นหา\n\n"
							 L"จะบอกว่าการ์ดนั้นหาได้จากซองไหน, Structure Deck ไหน, หรือดรอปจากนักสู้คนไหน "
							 L"รวมถึงพ่อค้าการ์ดวันนี้ และจำนวนที่คุณมีอยู่");
		return;
	}
	struct Hit { uint32_t code; bool obtainable; std::wstring name; };
	std::vector<Hit> hits;
	for(const auto& [code, card] : gDataManager->cards) {
		const auto& d = card._data;
		if(d.IsInArtworkOffsetRange() || (d.type & (0x8000000 | 0x4000)))   // alt arts, skills, tokens
			continue;
		const auto name = gDataManager->GetName(code);
		if(Utils::ToUpperNoAccents(name).find(q) == std::wstring::npos)
			continue;
		const bool ok = card_sources.count(Key(code)) != 0 || TraderCard(code);
		hits.push_back({ code, ok, std::wstring(name) });
	}
	std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
		if(a.obtainable != b.obtainable)
			return a.obtainable;
		return a.name < b.name;
	});
	if(hits.size() > 300)
		hits.resize(300);
	for(const auto& h : hits) {
		const auto r = RarityOf(h.code);
		const int own = Owned(h.code);
		const auto idx = lstSearch->addItem(epro::format(L"{}[{}] {}{}", h.obtainable ? L"" : L"✗ ", RarityName(r), h.name,
														 own > 0 ? epro::format(L"  (มี {})", own) : L"").data());
		lstSearch->setItemOverrideColor(static_cast<irr::u32>(idx), h.obtainable ? RarityColor(r) : SColor(255, 130, 130, 130));
		search_results.push_back(h.code);
	}
	if(search_results.empty()) {
		SetText(stSearchInfo, L"");
		SetText(stSearchSrc, epro::format(L"ไม่พบการ์ดที่ชื่อมีคำว่า \"{}\"", query));
		return;
	}
	lstSearch->setSelected(0);
	UpdateCardSearchInfo();
}

void DuelWorld::UpdateCardSearchInfo() {
	const int sel = lstSearch->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= search_results.size())
		return;
	const auto code = search_results[static_cast<size_t>(sel)];
	const auto r = RarityOf(code);
	std::wstring info = epro::format(L"{}\n\nระดับ: {}\nคุณมี: {} ใบ\nราคาที่พ่อค้าการ์ด: {} ทอง",
									 gDataManager->GetName(code), RarityName(r), Owned(code), Num(RarityPrice(r)));
	SetText(stSearchInfo, info);
	SetText(stSearchSrc, CardSourcesText(code));
}

/* ---------------- card detail popup */

void DuelWorld::ShowCardDetail(uint32_t code) {
	if(!wCardDetail || !code)
		return;
	auto* cd = gDataManager->GetCardData(code);
	cd_code = code;
	const uint32_t tcode = cd && cd->IsInArtworkOffsetRange() && cd->alias ? cd->alias : code;
	SetText(stCDName, std::wstring(gDataManager->GetName(tcode)));
	std::wstring type;
	if(cd) {
		if(cd->type & TYPE_MONSTER) {
			type = epro::format(L"[{}] {} {}\n", gDataManager->FormatType(cd->type), gDataManager->FormatAttribute(cd->attribute), gDataManager->FormatRace(cd->race));
			auto stat = [](int32_t v) { return v < 0 ? std::wstring(L"?") : epro::format(L"{}", v); };
			if(cd->type & TYPE_LINK)
				type += epro::format(L"ATK {}  /  LINK-{}   {}", stat(cd->attack), cd->level, gDataManager->FormatLinkMarker(cd->link_marker));
			else
				type += epro::format(L"{} {}   ATK {}  /  DEF {}", (cd->type & TYPE_XYZ) ? L"Rank" : L"Lv", cd->level, stat(cd->attack), stat(cd->defense));
			if(cd->type & TYPE_PENDULUM)
				type += epro::format(L"   Scale {}/{}", cd->lscale, cd->rscale);
		} else
			type = epro::format(L"[{}]", gDataManager->FormatType(cd->type));
	}
	SetText(stCDType, type);
	const auto r = RarityOf(code);
	SetText(stCDInfo, epro::format(L"ระดับ: {}     คุณมี: {} ใบ     ราคาพ่อค้า: {} ทอง", RarityName(r), Owned(code), Num(RarityPrice(r))));
	stCDInfo->setOverrideColor(RarityColor(r));
	stCDText->setText(gDataManager->GetText(tcode).data());
	SetText(stCDSrc, CardSourcesText(code));
	if(wCardSearch && wCardSearch->isVisible()) {
		cd_restore_search = true;
		wCardSearch->setVisible(false);
	}
	wCardDetail->setVisible(true);
	mainGame->env->getRootGUIElement()->bringToFront(wCardDetail->getParent());
	wCardDetail->getParent()->bringToFront(wCardDetail);
}

void DuelWorld::CloseCardDetail() {
	if(wCardDetail)
		wCardDetail->setVisible(false);
	if(cd_restore_search && wCardSearch) {
		cd_restore_search = false;
		wCardSearch->setVisible(true);
		mainGame->env->getRootGUIElement()->bringToFront(wCardSearch->getParent());
	}
}

void DuelWorld::DrawCardDetailArt() {
	if(!wCardDetail || !wCardDetail->isVisible() || !cd_code)
		return;
	auto* root = mainGame->env->getRootGUIElement();
	if(root->getChildren().empty() || *root->getChildren().getLast() != wCardDetail->getParent())
		root->bringToFront(wCardDetail->getParent());
	auto* tex = mainGame->imageManager.GetTextureCard(cd_code, imgType::ART);
	if(!tex)
		return;
	const auto rc = imgCD->getAbsolutePosition();
	const auto ts = tex->getOriginalSize();
	const int w = rc.getWidth(), h = rc.getHeight();
	int dw = w, dh = static_cast<int>(w * ts.Height / static_cast<float>(ts.Width));
	if(dh > h) {
		dh = h;
		dw = static_cast<int>(h * ts.Width / static_cast<float>(ts.Height));
	}
	const int x = rc.UpperLeftCorner.X + (w - dw) / 2, y = rc.UpperLeftCorner.Y + (h - dh) / 2;
	const auto r = RarityOf(cd_code);
	if(r >= Rarity::SR) {
		const float pulse = 0.6f + 0.25f * std::sin(gFx.Time() * 3.f);
		gFx.DrawQuad(gFx.GlowTexture(), { 0, 0, 1, 1 }, { x + dw / 2.f, y + dh / 2.f }, { dw * 1.35f, dh * 1.2f }, 0, WithAlpha(RarityColor(r), pulse), true);
	}
	mainGame->driver->draw2DImage(tex, irr::core::recti(x, y, x + dw, y + dh),
								  irr::core::recti(0, 0, static_cast<irr::s32>(ts.Width), static_cast<irr::s32>(ts.Height)), nullptr, nullptr, true);
}

void DuelWorld::OpenCardList(const std::wstring& title, const std::vector<uint32_t>& codes) {
	if(!card_sources_built)
		BuildCardSources();
	search_title = title;
	ebSearchPop->setText(L"");
	search_results.clear();
	lstSearch->clear();
	std::set<uint32_t> seen;
	std::vector<uint32_t> list;
	for(auto c : codes)
		if(seen.insert(Key(c)).second)
			list.push_back(c);
	std::stable_sort(list.begin(), list.end(), [this](uint32_t a, uint32_t b) {
		const int ra = static_cast<int>(RarityOf(a)), rb = static_cast<int>(RarityOf(b));
		if(ra != rb)
			return ra > rb;
		return gDataManager->GetName(a) < gDataManager->GetName(b);
	});
	for(auto c : list) {
		const auto r = RarityOf(c);
		const int own = Owned(c);
		const auto idx = lstSearch->addItem(epro::format(L"[{}] {}{}", RarityName(r), gDataManager->GetName(c),
														 own > 0 ? epro::format(L"  (มี {})", own) : L"").data());
		lstSearch->setItemOverrideColor(static_cast<irr::u32>(idx), RarityColor(r));
		search_results.push_back(c);
	}
	wCardSearch->setVisible(true);
	mainGame->env->getRootGUIElement()->bringToFront(wCardSearch->getParent());
	wCardSearch->getParent()->bringToFront(wCardSearch);
	if(!search_results.empty()) {
		lstSearch->setSelected(0);
		UpdateCardSearchInfo();
	}
}

bool DuelWorld::ClickCardAt(const irr::core::vector2di& pos) {
	// any card picture in Duel World -> detail popup
	if(wCardSearch && wCardSearch->isVisible()) {
		const int sel = lstSearch->getSelected();
		if(imgSearch->getAbsolutePosition().isPointInside(pos) && sel >= 0 && static_cast<size_t>(sel) < search_results.size()) {
			ShowCardDetail(search_results[static_cast<size_t>(sel)]);
			return true;
		}
		return false;
	}
	if(!wHub || !wHub->isTrulyVisible() || (wSkillInfo && wSkillInfo->isVisible()) || (wLogin && wLogin->isVisible()))
		return false;
	auto hit = [&](irr::gui::IGUIElement* el) { return el && el->isTrulyVisible() && el->getAbsolutePosition().isPointInside(pos); };
	if(hit(imgTrader)) {
		const int ts = lstTrader->getSelected();
		if(ts >= 0 && static_cast<size_t>(ts) < save.trader_stock.size()) {
			ShowCardDetail(save.trader_stock[static_cast<size_t>(ts)]);
			return true;
		}
	}
	if(hit(imgPack)) {
		const int ps = lstPacks->getSelected();
		if(ps >= 0 && static_cast<size_t>(ps) < packs.size()) {
			const auto& p = packs[static_cast<size_t>(ps)];
			ShowCardDetail(p.cover ? p.cover : (p.cards.empty() ? 0 : p.cards.front()));
			return true;
		}
	}
	if(hit(imgStruct)) {
		const int ss = lstStructs->getSelected();
		if(ss >= 0 && static_cast<size_t>(ss) < structs.size()) {
			ShowCardDetail(structs[static_cast<size_t>(ss)].cover);
			return true;
		}
	}
	for(size_t i = 0; i < featPack.size() && i < featPackCodes.size(); i++)
		if(hit(featPack[i])) {
			ShowCardDetail(featPackCodes[i]);
			return true;
		}
	for(size_t i = 0; i < featStruct.size() && i < featStructCodes.size(); i++)
		if(hit(featStruct[i])) {
			ShowCardDetail(featStructCodes[i]);
			return true;
		}
	return false;
}

std::wstring DuelWorld::CardSourcesText(uint32_t code) {
	if(!card_sources_built)
		BuildCardSources();
	const auto r = RarityOf(code);
	std::wstring src = L"หาได้จาก:\n";
	int n = 0;
	for(size_t i = 0; i < save.trader_stock.size(); i++)
		if(Key(save.trader_stock[i]) == Key(code) && !save.trader_bought.count(i)) {
			src += epro::format(L"• พ่อค้าการ์ด (วันนี้มีขาย!)  {} ทอง\n", Num(RarityPrice(r)));
			n++;
			break;
		}
	auto it = card_sources.find(Key(code));
	if(it != card_sources.end()) {
		for(size_t i = 0; i < it->second.size() && i < 14; i++, n++)
			src += L"• " + it->second[i] + L"\n";
		if(it->second.size() > 14)
			src += epro::format(L"...และอีก {} แหล่ง\n", it->second.size() - 14);
	}
	if(TraderCard(code)) {
		src += L"• พ่อค้าการ์ด (ไม่มีในซอง/Structure Deck - สุ่มของขายใหม่ทุกวัน 10 ใบ)\n";
		n++;
	}
	if(n == 0)
		src += L"• ยังไม่มีทางได้ใน Duel World\n  (ใช้ได้ใน Campaign / Sandbox)";
	return src;
}

void DuelWorld::DrawCardSearchArt() {
	if(!wCardSearch || !wCardSearch->isVisible())
		return;
	const int sel = lstSearch->getSelected();
	if(sel < 0 || static_cast<size_t>(sel) >= search_results.size())
		return;
	auto* tex = mainGame->imageManager.GetTextureCard(search_results[static_cast<size_t>(sel)], imgType::ART);
	if(!tex)
		return;
	auto rc = imgSearch->getAbsolutePosition();
	const auto ts = tex->getOriginalSize();
	// keep the card ratio inside the box
	const int w = rc.getWidth(), h = rc.getHeight();
	int dw = w, dh = static_cast<int>(w * ts.Height / static_cast<float>(ts.Width));
	if(dh > h) {
		dh = h;
		dw = static_cast<int>(h * ts.Width / static_cast<float>(ts.Height));
	}
	const irr::core::recti dst(rc.UpperLeftCorner.X + (w - dw) / 2, rc.UpperLeftCorner.Y + (h - dh) / 2,
							   rc.UpperLeftCorner.X + (w - dw) / 2 + dw, rc.UpperLeftCorner.Y + (h - dh) / 2 + dh);
	mainGame->driver->draw2DImage(tex, dst, irr::core::recti(0, 0, static_cast<irr::s32>(ts.Width), static_cast<irr::s32>(ts.Height)), nullptr, nullptr, true);
}

void DuelWorld::RefreshSkills() {
	if(!lstSkills)
		return;
	const int old = lstSkills->getSelected();
	lstSkills->clear();
	for(const auto& s : Skills()) {
		std::wstring tag;
		if(s.code == save.equipped_skill)
			tag = L"[ติดตั้งอยู่] ";
		else if(SkillOwned(s.code))
			tag = L"[มีแล้ว] ";
		else
			tag = epro::format(L"[{} เพชร] ", Num(s.price));
		const wchar_t* tier = s.tier == "god" ? L"[เทพ] " : s.tier == "legendary" ? L"★ตำนาน " : s.tier == "broken" ? L"[โกง] " : L"";
		const auto idx = lstSkills->addItem(epro::format(L"{}{}{}", tag, tier, s.name).data());
		if(s.tier == "god")
			lstSkills->setItemOverrideColor(idx, SColor(255, 255, 90, 70));
		else if(s.tier == "legendary")
			lstSkills->setItemOverrideColor(idx, SColor(255, 255, 205, 80));
		else if(s.tier == "broken")
			lstSkills->setItemOverrideColor(idx, SColor(255, 220, 140, 255));
	}
	if(lstSkills->getItemCount() > 0)
		lstSkills->setSelected(old >= 0 && static_cast<irr::u32>(old) < lstSkills->getItemCount() ? old : 0);
	UpdateSkillInfo();
}

void DuelWorld::UpdateSkillInfo() {
	const int sel = lstSkills->getSelected();
	const auto& list = Skills();
	if(sel < 0 || static_cast<size_t>(sel) >= list.size()) {
		SetText(stSkill, L"ยังไม่มีข้อมูลสกิล (story/world/skills.json)");
		btnSkillEquip->setEnabled(false);
		return;
	}
	const auto& s = list[static_cast<size_t>(sel)];
	const bool owned = SkillOwned(s.code);
	const auto* eq = FindSkill(save.equipped_skill);
	const std::wstring unlock = owned ? std::wstring() : SkillUnlockText(s.code);
	SetText(stSkill, epro::format(L"{}{}  ({})\nเหมาะกับ: {}\n\n{}\n{}\nสกิลที่ติดตั้งอยู่: {}\n(กดที่การ์ดสกิลบนสนามเพื่อใช้)",
								  s.tier == "god" ? L"[สกิลเทพเจ้า] " : s.tier == "legendary" ? L"[สกิลตำนาน] " : s.tier == "broken" ? L"[สกิลโกง] " : L"",
								  s.name, s.name_en, s.for_deck, s.desc, unlock.empty() ? L"" : L"\n" + unlock + L"\n",
								  eq ? eq->name : std::wstring(L"ไม่มี")));
	btnSkillEquip->setEnabled(s.code != save.equipped_skill);
	btnSkillEquip->setText(owned ? L"ติดตั้งสกิล" : epro::format(L"ซื้อ + ติดตั้ง ({} เพชร)", Num(s.price)).data());
	btnSkillUnequip->setEnabled(save.equipped_skill != 0);
}

/* ================================================================== rewards */

void DuelWorld::GrantReward(const std::wstring& title, int64_t gold, int64_t gems, int card_tier) {
	if(!save.started)
		return;
	save.gold += gold;
	save.gems += gems;
	std::vector<RevealCard> cards;
	if(card_tier >= 2) {
		std::vector<uint32_t> pool;
		for(const auto& p : packs)
			for(int r = card_tier; r <= 3; r++)
				pool.insert(pool.end(), p.by_rarity[r].begin(), p.by_rarity[r].end());
		if(!pool.empty()) {
			const auto code = pool[static_cast<size_t>(Rand(static_cast<int>(pool.size())))];
			cards.push_back({ code, RarityOf(code), Owned(code) == 0 });
			AddCard(code);
		}
	}
	WriteSave();
	std::wstring sub = epro::format(L"ทอง +{}   เพชร +{}", Num(gold), Num(gems));
	ShowReveal(title, sub, cards);
}

/* ================================================================== reveal overlay */

void DuelWorld::ShowReveal(std::wstring title, std::wstring subtitle, std::vector<RevealCard> cards) {
	Reveal r;
	r.title = std::move(title);
	r.subtitle = std::move(subtitle);
	r.cards = std::move(cards);
	r.t = 0;
	reveals.push_back(std::move(r));
	reveal_active = true;
}

void DuelWorld::NextReveal() {
	if(reveals.empty()) {
		reveal_active = false;
		return;
	}
	auto& cur = reveals.front();
	const float all_flipped = 0.5f + cur.cards.size() * 0.22f + 0.3f;
	if(cur.t < all_flipped) {
		cur.t = all_flipped;   // skip the animation
		return;
	}
	reveals.erase(reveals.begin());
	if(reveals.empty()) {
		reveal_active = false;
		RefreshHub();
	}
}

void DuelWorld::Draw2D() {
	gFx.TickMenu();
	auto* driver = mainGame->driver;
	// card pictures in the hub / starter windows (textures may arrive later, so draw every frame)
	auto draw_in = [&](irr::gui::IGUIElement* elem, uint32_t code) {
		if(!elem || !code || !elem->isTrulyVisible())
			return;
		auto* tex = mainGame->imageManager.GetTextureCard(code, imgType::ART);
		if(!tex)
			return;
		const auto rc = elem->getAbsolutePosition();
		const auto ts = tex->getOriginalSize();
		driver->draw2DImage(tex, rc, irr::core::recti(0, 0, static_cast<irr::s32>(ts.Width), static_cast<irr::s32>(ts.Height)), nullptr, nullptr, true);
	};
	if(wHub && wHub->isTrulyVisible()) {
		const int ps = lstPacks->getSelected();
		if(ps >= 0 && static_cast<size_t>(ps) < packs.size())
			draw_in(imgPack, packs[static_cast<size_t>(ps)].cover ? packs[static_cast<size_t>(ps)].cover : packs[static_cast<size_t>(ps)].cards.front());
		const int ts = lstTrader->getSelected();
		if(ts >= 0 && static_cast<size_t>(ts) < save.trader_stock.size())
			draw_in(imgTrader, save.trader_stock[static_cast<size_t>(ts)]);
		const int ss = lstStructs->getSelected();
		if(ss >= 0 && static_cast<size_t>(ss) < structs.size())
			draw_in(imgStruct, structs[static_cast<size_t>(ss)].cover);
		// featured cards strip with a rarity glow
		auto strip = [&](const std::vector<irr::gui::IGUIElement*>& slots, const std::vector<uint32_t>& codes) {
			for(size_t i = 0; i < slots.size(); i++) {
				if(i >= codes.size() || !slots[i]->isTrulyVisible())
					continue;
				const auto r = RarityOf(codes[i]);
				const auto rc = slots[i]->getAbsolutePosition();
				if(r >= Rarity::SR) {
					const float pulse = 0.6f + 0.25f * std::sin(gFx.Time() * 3.f + static_cast<float>(i));
					gFx.DrawQuad(gFx.GlowTexture(), { 0, 0, 1, 1 }, { rc.getCenter().X * 1.f, rc.getCenter().Y * 1.f },
								 { rc.getWidth() * 1.7f, rc.getHeight() * 1.4f }, 0, WithAlpha(RarityColor(r), pulse), true);
				}
				draw_in(slots[i], codes[i]);
				const irr::core::ustring l(RarityName(r));
				auto* font = mainGame->guiFont;
				const auto d = font->getDimensionustring(l);
				const irr::s32 lx = rc.UpperLeftCorner.X + 3, ly = rc.UpperLeftCorner.Y + 2;
				driver->draw2DRectangle(SColor(170, 0, 0, 0), irr::core::recti(lx - 2, ly - 1, lx + d.Width + 2, ly + d.Height));
				font->drawustring(l, irr::core::recti(lx, ly, lx + d.Width, ly + d.Height), RarityColor(r));
			}
		};
		strip(featPack, featPackCodes);
		strip(featStruct, featStructCodes);
		// Ranked emblem
		if(imgRank && imgRank->isTrulyVisible()) {
			const auto rc = imgRank->getAbsolutePosition();
			const int tier = RankTier(save.rank_stage);
			const SColor col = RANK_COLORS[tier];
			const vector2df c{ static_cast<float>(rc.getCenter().X), static_cast<float>(rc.UpperLeftCorner.Y) + rc.getHeight() * 0.40f };
			const float R = rc.getHeight() * 0.36f;
			gFx.DrawQuad(gFx.RaysTexture(), { 0, 0, 1, 1 }, c, { R * 3.2f, R * 3.2f }, gFx.Time() * 12.f, WithAlpha(col, 0.55f), true);
			gFx.DrawQuad(gFx.GlowTexture(), { 0, 0, 1, 1 }, c, { R * 2.6f, R * 2.6f }, 0, WithAlpha(col, 0.8f), true);
			gFx.DrawQuad(gFx.RingTexture(), { 0, 0, 1, 1 }, c, { R * 2.0f, R * 2.0f }, 0, col, true);
			gFx.DrawQuad(gFx.RingTexture(), { 0, 0, 1, 1 }, c, { R * 1.6f, R * 1.6f }, -gFx.Time() * 30.f, WithAlpha(col, 0.6f), true);
			// stars
			if(save.rank_stage < RANK_LAST) {
				const int need = StarsNeeded(save.rank_stage);
				const float sz = R * 0.42f, gapx = sz * 1.15f;
				for(int i = 0; i < need; i++) {
					const vector2df p{ c.X + (i - (need - 1) / 2.f) * gapx, c.Y + R * 1.25f };
					const bool on = i < save.rank_stars;
					gFx.DrawQuad(gFx.SparkTexture(), { 0, 0, 1, 1 }, p, { sz, sz }, 0, on ? SColor(255, 255, 230, 120) : SColor(120, 120, 120, 140), true);
					if(on)
						gFx.DrawQuad(gFx.GlowTexture(), { 0, 0, 1, 1 }, p, { sz * 1.8f, sz * 1.8f }, 0, SColor(200, 255, 200, 80), true);
				}
			}
		}
		// wallet: framed boxes for gems and gold
		{
			const auto hub = wHub->getAbsolutePosition();
			auto* font = mainGame->guiFont;
			auto box = [&](irr::s32 x, irr::s32 w, const std::wstring& label, const std::wstring& vs, SColor accent, int icon) {
				const irr::s32 y = hub.UpperLeftCorner.Y + mainGame->Scale(24);
				const irr::s32 h = mainGame->Scale(32);
				const irr::core::recti r(x, y, x + w, y + h);
				driver->draw2DRectangle(r, SColor(235, 10, 14, 30), SColor(235, 10, 14, 30), SColor(235, 26, 32, 60), SColor(235, 26, 32, 60));
				const SColor edge = WithAlpha(accent, 0.75f + 0.25f * std::sin(gFx.Time() * 2.f));
				for(int t = 0; t < 2; t++)
					driver->draw2DRectangleOutline(irr::core::recti(r.UpperLeftCorner.X + t, r.UpperLeftCorner.Y + t, r.LowerRightCorner.X - t, r.LowerRightCorner.Y - t), edge);
				// icon
				const vector2df ic{ static_cast<float>(x + h / 2 + mainGame->Scale(4)), static_cast<float>(y + h / 2) };
				const float is = static_cast<float>(h) * 0.62f;
				gFx.DrawQuad(gFx.GlowTexture(), { 0, 0, 1, 1 }, ic, { is * 1.9f, is * 1.9f }, 0, WithAlpha(accent, 0.7f), true);
				if(icon == 0) {   // gem
					gFx.DrawQuad(gFx.SparkTexture(), { 0, 0, 1, 1 }, ic, { is * 0.75f, is * 1.25f }, 0, SColor(255, 200, 250, 255), true);
					gFx.DrawQuad(gFx.SparkTexture(), { 0, 0, 1, 1 }, ic, { is * 1.25f, is * 0.5f }, 0, accent, true);
				} else if(icon == 2) {   // cards: two small card shapes
					for(int k = 0; k < 2; k++) {
						const irr::s32 cx = static_cast<irr::s32>(ic.X) - mainGame->Scale(5) + k * mainGame->Scale(5);
						const irr::s32 cy = static_cast<irr::s32>(ic.Y) - mainGame->Scale(2) + k * mainGame->Scale(3);
						const irr::core::recti cr(cx - mainGame->Scale(6), cy - mainGame->Scale(8), cx + mainGame->Scale(6), cy + mainGame->Scale(8));
						driver->draw2DRectangle(SColor(255, 40, 60, 110), cr);
						driver->draw2DRectangleOutline(cr, accent);
					}
				} else if(icon == 3) {   // trophy-ish star
					gFx.DrawQuad(gFx.RaysTexture(), { 0, 0, 1, 1 }, ic, { is * 1.6f, is * 1.6f }, gFx.Time() * 20.f, accent, true);
					gFx.DrawQuad(gFx.SparkTexture(), { 0, 0, 1, 1 }, ic, { is * 0.7f, is * 0.7f }, 0, SColor(255, 255, 255, 230), true);
				} else {
					gFx.DrawQuad(gFx.SparkTexture(), { 0, 0, 1, 1 }, ic, { is * 1.1f, is * 1.1f }, 0, SColor(255, 255, 230, 120), true);
					gFx.DrawQuad(gFx.SparkTexture(), { 0, 0, 1, 1 }, ic, { is * 0.6f, is * 0.6f }, 0, SColor(255, 255, 255, 220), true);
				}
				const irr::core::ustring l(label);
				const irr::core::ustring v(vs);
				const auto ld = font->getDimensionustring(l);
				const auto vd = font->getDimensionustring(v);
				const irr::s32 ty = y + (h - static_cast<irr::s32>(ld.Height)) / 2;
				const irr::s32 lx = x + h + mainGame->Scale(8);
				font->drawustring(l, irr::core::recti(lx, ty, lx + ld.Width, ty + ld.Height), SColor(255, 190, 200, 225));
				const irr::s32 vx = r.LowerRightCorner.X - mainGame->Scale(10) - static_cast<irr::s32>(vd.Width);
				font->drawustring(v, irr::core::recti(vx + 1, ty + 1, vx + vd.Width + 1, ty + vd.Height + 1), SColor(255, 0, 0, 0));
				font->drawustring(v, irr::core::recti(vx, ty, vx + vd.Width, ty + vd.Height), accent);
			};
			int64_t total = 0;
			for(const auto& [c, n] : save.cards)
				total += n;
			const irr::s32 x0 = hub.UpperLeftCorner.X + mainGame->Scale(14);
			const irr::s32 gap = mainGame->Scale(6);
			const irr::s32 wl = mainGame->Scale(140), w1 = mainGame->Scale(160), w2 = mainGame->Scale(215), w3 = mainGame->Scale(150);
			// player level + xp bar
			{
				const int lv = Level();
				int64_t xp = save.xp;
				for(int l = 1; l < lv; l++)
					xp -= XpForLevel(l);
				box(x0, wl, epro::format(L"Lv.{}", lv), epro::format(L"{}/{}", Num(xp), Num(XpForLevel(lv))), SColor(255, 255, 140, 90), 3);
				const irr::s32 by = hub.UpperLeftCorner.Y + mainGame->Scale(24) + mainGame->Scale(32) - mainGame->Scale(4);
				const irr::s32 bx = x0 + mainGame->Scale(4), bw = wl - mainGame->Scale(8);
				driver->draw2DRectangle(SColor(255, 40, 30, 30), irr::core::recti(bx, by, bx + bw, by + mainGame->Scale(2)));
				driver->draw2DRectangle(SColor(255, 255, 140, 90), irr::core::recti(bx, by, bx + static_cast<irr::s32>(bw * std::min<int64_t>(xp, XpForLevel(lv)) / std::max<int64_t>(1, XpForLevel(lv))), by + mainGame->Scale(2)));
			}
			irr::s32 x = x0 + wl + gap;
			box(x, w1, L"เพชร", Num(save.gems), SColor(255, 110, 220, 255), 0);
			x += w1 + gap;
			box(x, w1, L"ทอง", Num(save.gold), SColor(255, 255, 205, 80), 1);
			x += w1 + gap;
			box(x, w2, L"การ์ด", epro::format(L"{} ({} แบบ)", Num(total), Num(static_cast<int64_t>(save.cards.size()))),
				SColor(255, 170, 150, 255), 2);
			x += w2 + gap;
			box(x, w3, L"ชนะ/แพ้", epro::format(L"{} / {}", save.wins, save.losses), SColor(255, 120, 240, 150), 3);
		}
	}
	if(wStarter && wStarter->isTrulyVisible())
		for(size_t i = 0; i < imgStarters.size() && i < starters.size(); i++)
			draw_in(imgStarters[i], starters[i].cover);

	if(!reveal_active || reveals.empty())
		return;
	auto& rv = reveals.front();
	rv.t += std::min(0.1f, mainGame->delta_time / 1000.f);
	const auto ss = driver->getScreenSize();
	const float Wd = static_cast<float>(ss.Width), Hd = static_cast<float>(ss.Height);
	driver->draw2DRectangle(SColor(225, 5, 5, 20), irr::core::recti(0, 0, ss.Width, ss.Height));
	gFx.DrawQuad(gFx.RaysTexture(), { 0, 0, 1, 1 }, { Wd * 0.5f, Hd * 0.5f }, { Hd * 1.6f, Hd * 1.6f }, gFx.Time() * 6.f,
				 SColor(40, 120, 150, 255), true);
	// title
	auto* big = mainGame->lpcFont;
	auto* small = mainGame->guiFont;
	{
		const irr::core::ustring t(rv.title);
		const auto dim = big->getDimensionustring(t);
		const irr::s32 x = static_cast<irr::s32>(Wd / 2 - dim.Width / 2.f), y = static_cast<irr::s32>(Hd * 0.06f);
		big->drawustring(t, irr::core::recti(x + 2, y + 2, x + dim.Width + 2, y + dim.Height + 2), SColor(255, 0, 0, 0));
		big->drawustring(t, irr::core::recti(x, y, x + dim.Width, y + dim.Height), SColor(255, 255, 230, 140));
		if(!rv.subtitle.empty()) {
			const irr::core::ustring s(rv.subtitle);
			const auto sd = small->getDimensionustring(s);
			const irr::s32 sx = static_cast<irr::s32>(Wd / 2 - sd.Width / 2.f), sy = y + dim.Height + 8;
			small->drawustring(s, irr::core::recti(sx, sy, sx + sd.Width, sy + sd.Height), SColor(255, 230, 230, 255));
		}
	}
	// cards
	const size_t n = rv.cards.size();
	const size_t per_row = n <= 5 ? std::max<size_t>(n, 1) : 4;
	const size_t rows = n == 0 ? 0 : (n + per_row - 1) / per_row;
	const float card_h = rows <= 1 ? Hd * 0.42f : Hd * 0.3f;
	const float card_w = card_h * 0.697f;
	const float gap = card_w * 0.18f;
	const float top = rows <= 1 ? Hd * 0.27f : Hd * 0.2f;
	auto* back = mainGame->imageManager.tCover[0];
	reveal_hits.clear();
	for(size_t i = 0; i < n; i++) {
		const auto& c = rv.cards[i];
		const size_t row = i / per_row, col = i % per_row;
		const size_t in_row = std::min(per_row, n - row * per_row);
		const float row_w = in_row * card_w + (in_row - 1) * gap;
		const vector2df center{ Wd / 2 - row_w / 2 + col * (card_w + gap) + card_w / 2, top + row * (card_h + Hd * 0.08f) + card_h / 2 };
		const float start = 0.5f + i * 0.22f;
		const float p = Clamp01((rv.t - start) / 0.28f);
		const float appear = Clamp01((rv.t - i * 0.05f) / 0.25f);
		const bool front = p >= 0.5f;
		const float sx = std::max(0.02f, std::fabs(std::cos(p * PI)));
		const auto col_r = RarityColor(c.rarity);
		if(front && c.rarity >= Rarity::SR) {
			const float pulse = 0.75f + 0.25f * std::sin(gFx.Time() * 4.f + i);
			if(c.rarity == Rarity::UR)
				gFx.DrawQuad(gFx.RaysTexture(), { 0, 0, 1, 1 }, center, { card_h * 1.5f, card_h * 1.5f }, gFx.Time() * 25.f, WithAlpha(col_r, 0.8f * pulse), true);
			gFx.DrawQuad(gFx.GlowTexture(), { 0, 0, 1, 1 }, center, { card_w * 2.f, card_h * 1.6f }, 0, WithAlpha(col_r, 0.9f * pulse), true);
		} else if(front && c.rarity == Rarity::R) {
			gFx.DrawQuad(gFx.GlowTexture(), { 0, 0, 1, 1 }, center, { card_w * 1.6f, card_h * 1.3f }, 0, WithAlpha(col_r, 0.5f), true);
		}
		if(front)
			reveal_hits.emplace_back(irr::core::recti(static_cast<irr::s32>(center.X - card_w / 2), static_cast<irr::s32>(center.Y - card_h / 2),
													  static_cast<irr::s32>(center.X + card_w / 2), static_cast<irr::s32>(center.Y + card_h / 2)), c.code);
		auto* tex = front ? mainGame->imageManager.GetTextureCard(c.code, imgType::ART) : back;
		gFx.DrawQuad(tex, { 0, 0, 1, 1 }, center, { card_w * sx * appear, card_h * appear }, 0, SColor(255, 255, 255, 255), false);
		// flash when it turns
		if(p > 0.45f && p < 1.f && c.rarity >= Rarity::SR)
			gFx.DrawQuad(gFx.GlowTexture(), { 0, 0, 1, 1 }, center, { card_w * 1.4f, card_h * 1.2f }, 0,
						 SColor(static_cast<irr::u32>(220 * (1 - p)), 255, 255, 255), true);
		if(front) {
			std::wstring label = RarityName(c.rarity);
			if(c.is_new)
				label += L"  NEW";
			const irr::core::ustring l(label);
			const auto ld = small->getDimensionustring(l);
			const irr::s32 lx = static_cast<irr::s32>(center.X - ld.Width / 2.f), ly = static_cast<irr::s32>(center.Y + card_h / 2 + 4);
			small->drawustring(l, irr::core::recti(lx + 1, ly + 1, lx + ld.Width + 1, ly + ld.Height + 1), SColor(255, 0, 0, 0));
			small->drawustring(l, irr::core::recti(lx, ly, lx + ld.Width, ly + ld.Height), col_r);
		}
	}
	if(n == 0) {
		const irr::core::ustring t(L"(ไม่มีการ์ดดรอป)");
		const auto d = small->getDimensionustring(t);
		small->drawustring(t, irr::core::recti(static_cast<irr::s32>(Wd / 2 - d.Width / 2.f), static_cast<irr::s32>(Hd * 0.45f),
											   static_cast<irr::s32>(Wd / 2 + d.Width / 2.f), static_cast<irr::s32>(Hd * 0.45f) + d.Height),
						   SColor(255, 180, 180, 200));
	}
	const std::wstring hint = reveals.size() > 1 ? epro::format(L"คลิกการ์ด = ดูรายละเอียด   •   คลิกที่ว่าง = ไปต่อ (เหลืออีก {})   •   คลิกขวา = ข้ามทั้งหมด", reveals.size() - 1)
		: std::wstring(L"คลิกการ์ด = ดูรายละเอียด   •   คลิกที่ว่าง = ปิด");
	const irr::core::ustring h(hint);
	const auto hd = small->getDimensionustring(h);
	const irr::s32 hx = static_cast<irr::s32>(Wd / 2 - hd.Width / 2.f), hy = static_cast<irr::s32>(Hd * 0.93f);
	small->drawustring(h, irr::core::recti(hx, hy, hx + hd.Width, hy + hd.Height),
					   SColor(static_cast<irr::u32>(150 + 100 * std::sin(gFx.Time() * 3.f)), 255, 255, 255));
}

/* ================================================================== events */

bool DuelWorld::OnEvent(const irr::SEvent& event) {
	// the reveal overlay eats every input until it is closed
	if(reveal_active) {
		if(wCardDetail && wCardDetail->isVisible()) {
			// detail popup over the pack opening: let its button work, Esc/Enter closes it
			if(event.EventType == irr::EET_KEY_INPUT_EVENT) {
				if(!event.KeyInput.PressedDown && (event.KeyInput.Key == irr::KEY_RETURN || event.KeyInput.Key == irr::KEY_ESCAPE))
					CloseCardDetail();
				return true;
			}
			if(event.EventType == irr::EET_MOUSE_INPUT_EVENT)
				return false;
		}
		if(event.EventType == irr::EET_MOUSE_INPUT_EVENT) {
			if(event.MouseInput.Event == irr::EMIE_LMOUSE_LEFT_UP) {
				const irr::core::vector2di pos(event.MouseInput.X, event.MouseInput.Y);
				for(const auto& [rc, code] : reveal_hits)
					if(rc.isPointInside(pos)) {
						ShowCardDetail(code);
						return true;
					}
				NextReveal();
			}
			else if(event.MouseInput.Event == irr::EMIE_RMOUSE_LEFT_UP) {
				reveals.clear();
				reveal_active = false;
				RefreshHub();
			}
			return true;
		}
		if(event.EventType == irr::EET_KEY_INPUT_EVENT) {
			if(!event.KeyInput.PressedDown && (event.KeyInput.Key == irr::KEY_RETURN || event.KeyInput.Key == irr::KEY_SPACE || event.KeyInput.Key == irr::KEY_ESCAPE))
				NextReveal();
			return true;
		}
	}
	// click on any card picture (trader, shop, structure, featured strip, search) -> detail popup
	if(event.EventType == irr::EET_MOUSE_INPUT_EVENT && event.MouseInput.Event == irr::EMIE_LMOUSE_LEFT_UP
	   && !(wCardDetail && wCardDetail->isVisible())) {
		if(ClickCardAt({ event.MouseInput.X, event.MouseInput.Y }))
			return true;
	}
	if(event.EventType != irr::EET_GUI_EVENT || !event.GUIEvent.Caller)
		return false;
	const int id = event.GUIEvent.Caller->getID();
	if(id == BUTTON_WORLD_SKILL_INFO_OK && event.GUIEvent.EventType == irr::gui::EGET_BUTTON_CLICKED) {
		wSkillInfo->setVisible(false);
		return true;
	}
	switch(event.GUIEvent.EventType) {
	case irr::gui::EGET_BUTTON_CLICKED:
		if(id == BUTTON_WORLD_SEARCH) {
			OpenCardSearch(ebSearch->getText());
			return true;
		}
		if(id == BUTTON_WORLD_SEARCH_POP) {
			RunCardSearch(ebSearchPop->getText());
			return true;
		}
		if(id == BUTTON_WORLD_SEARCH_CLOSE) {
			wCardSearch->setVisible(false);
			return true;
		}
		if(id == BUTTON_WORLD_LOGIN) {
			ShowLogin();
			return true;
		}
		if(id == BUTTON_WORLD_CD_CLOSE) {
			CloseCardDetail();
			return true;
		}
		if(id == BUTTON_WORLD_PACK_LIST) {
			const int ps = lstPacks->getSelected();
			if(ps >= 0 && static_cast<size_t>(ps) < packs.size())
				OpenCardList(epro::format(L"การ์ดในซอง: {}  ({} แบบ)", packs[static_cast<size_t>(ps)].name, packs[static_cast<size_t>(ps)].cards.size()), packs[static_cast<size_t>(ps)].cards);
			return true;
		}
		if(id == BUTTON_WORLD_STRUCT_LIST) {
			const int ss = lstStructs->getSelected();
			if(ss >= 0 && static_cast<size_t>(ss) < structs.size())
				OpenCardList(epro::format(L"การ์ดใน Structure Deck: {}", structs[static_cast<size_t>(ss)].name), structs[static_cast<size_t>(ss)].cards);
			return true;
		}
		if(id == BUTTON_WORLD_LOGIN_CLAIM) {
			ClaimLogin();
			return true;
		}
		if(id == BUTTON_WORLD_LOGIN_CLOSE) {
			wLogin->setVisible(false);
			return true;
		}
		break;
	case irr::gui::EGET_EDITBOX_ENTER:
		if(id == EDITBOX_WORLD_SEARCH) {
			OpenCardSearch(ebSearch->getText());
			return true;
		}
		if(id == EDITBOX_WORLD_SEARCH_POP) {
			RunCardSearch(ebSearchPop->getText());
			return true;
		}
		break;
	case irr::gui::EGET_LISTBOX_CHANGED:
	case irr::gui::EGET_LISTBOX_SELECTED_AGAIN:
		if(id == LISTBOX_WORLD_SEARCH) {
			UpdateCardSearchInfo();
			if(event.GUIEvent.EventType == irr::gui::EGET_LISTBOX_SELECTED_AGAIN) {
				const int sel = lstSearch->getSelected();
				if(sel >= 0 && static_cast<size_t>(sel) < search_results.size())
					ShowCardDetail(search_results[static_cast<size_t>(sel)]);
			}
			return true;
		}
		break;
	default:
		break;
	}
	switch(event.GUIEvent.EventType) {
	case irr::gui::EGET_BUTTON_CLICKED: {
		switch(id) {
		case BUTTON_WORLD_BACK:
			mainGame->HideElement(wHub);
			mainGame->ShowElement(mainGame->wMainMenu);
			return true;
		case BUTTON_WORLD_STORY:
			mainGame->HideElement(wHub);
			gStory.OpenMenu();
			return true;
		case BUTTON_WORLD_DECKS:
			OpenDeckEditor(false);
			return true;
		case BUTTON_WORLD_NEW_DECK:
			OpenDeckEditor(true);
			return true;
		case BUTTON_WORLD_DUEL:
			StartRoamingDuel();
			return true;
		case BUTTON_WORLD_REROLL:
			if(!UseItem("duelist_call"))
				Toast(L"ไม่มี 'ป้ายเรียกนักสู้' (ได้จากการชนะนักสู้)");
			RefreshHub();
			return true;
		case BUTTON_WORLD_BUY1:
			BuyPacks(1);
			return true;
		case BUTTON_WORLD_BUY10:
			BuyPacks(10);
			return true;
		case BUTTON_WORLD_FREE_PACK:
			BuyFreePack();
			return true;
		case BUTTON_WORLD_GATE_DUEL:
			StartGateDuel();
			return true;
		case BUTTON_WORLD_SKILL_INFO_GATE:
			ShowComboSkillInfo(cbGateSkill);
			return true;
		case BUTTON_WORLD_SKILL_INFO_RANK:
			ShowComboSkillInfo(cbRankSkill);
			return true;
		case BUTTON_WORLD_SKILL_INFO_ROAM:
			ShowComboSkillInfo(cbRoamSkill);
			return true;
		case BUTTON_WORLD_RANK_DUEL:
			StartRankDuel();
			return true;
		case BUTTON_WORLD_ANIM:
			save.anim_fast = !save.anim_fast;
			ApplyAnimSpeed();
			WriteSave();
			btnAnim->setText(save.anim_fast ? L"แอนิเมชัน: x2" : L"แอนิเมชัน: x1");
			return true;
		case BUTTON_WORLD_STRUCT_BUY:
			BuyStruct();
			return true;
		case BUTTON_WORLD_MISSION_CLAIM:
			ClaimSelected(false);
			return true;
		case BUTTON_WORLD_MISSION_CLAIM_ALL:
			ClaimSelected(true);
			return true;
		case BUTTON_WORLD_TRADER_BUY: {
			const int sel = lstTrader->getSelected();
			if(sel < 0 || static_cast<size_t>(sel) >= save.trader_stock.size() || save.trader_bought.count(static_cast<size_t>(sel)))
				return true;
			const auto code = save.trader_stock[static_cast<size_t>(sel)];
			const int price = RarityPrice(RarityOf(code));
			if(save.gold < price) {
				Toast(epro::format(L"ทองไม่พอ (ต้องการ {} มี {})", Num(price), Num(save.gold)));
				return true;
			}
			save.gold -= price;
			const bool is_new = Owned(code) == 0;
			AddCard(code);
			save.trader_bought.insert(static_cast<size_t>(sel));
			WriteSave();
			RefreshHub();
			ShowReveal(L"ซื้อการ์ดแล้ว!", epro::format(L"ทอง -{}", Num(price)), { { code, RarityOf(code), is_new } });
			return true;
		}
		case BUTTON_WORLD_TRADER_REFRESH:
			if(!UseItem("trader_refresh"))
				Toast(L"ไม่มี 'ตั๋วรีเฟรชพ่อค้า' (ได้จากการชนะนักสู้) - สินค้าจะเปลี่ยนเองทุกวัน");
			RefreshHub();
			return true;
		case BUTTON_WORLD_ITEM_USE: {
			const int sel = lstItems->getSelected();
			int idx = 0;
			for(const auto& def : items) {
				auto it = save.items.find(def.id);
				if(it == save.items.end() || it->second <= 0)
					continue;
				if(idx++ == sel) {
					UseItem(def.id);
					break;
				}
			}
			RefreshHub();
			return true;
		}
		case BUTTON_WORLD_STARTER_0:
		case BUTTON_WORLD_STARTER_1:
		case BUTTON_WORLD_STARTER_2:
		case BUTTON_WORLD_STARTER_3:
			ChooseStarter(static_cast<size_t>(id - BUTTON_WORLD_STARTER_0));
			return true;
		case BUTTON_WORLD_STARTER_BACK:
			mainGame->HideElement(wStarter);
			mainGame->ShowElement(mainGame->wMainMenu);
			return true;
		case BUTTON_WORLD_CAMPAIGN:
			mainGame->HideElement(wHub);
			gCampaign.Open();
			return true;
		case BUTTON_WORLD_SKILL_EQUIP: {
			const int sel = lstSkills->getSelected();
			const auto& list = Skills();
			if(sel < 0 || static_cast<size_t>(sel) >= list.size())
				return true;
			const auto& sk = list[static_cast<size_t>(sel)];
			if(!SkillOwned(sk.code)) {
				if(save.gems < sk.price) {
					Toast(epro::format(L"เพชรไม่พอ (ต้องการ {} มี {})", sk.price, Num(save.gems)));
					return true;
				}
				save.gems -= sk.price;
				save.skills.insert(sk.code);
				Toast(epro::format(L"ได้รับสกิล \"{}\" แล้ว!", sk.name));
			}
			save.equipped_skill = sk.code;
			WriteSave();
			RefreshHub();
			return true;
		}
		case BUTTON_WORLD_SKILL_UNEQUIP:
			save.equipped_skill = 0;
			WriteSave();
			RefreshHub();
			return true;
		default:
			break;
		}
		break;
	}
	case irr::gui::EGET_COMBO_BOX_CHANGED:
		if(id == COMBOBOX_WORLD_GATE_LV) {
			UpdateGateInfo();
			return true;
		}
		if(id == COMBOBOX_WORLD_SKILL_GATE || id == COMBOBOX_WORLD_SKILL_RANK || id == COMBOBOX_WORLD_SKILL_ROAM) {
			EquipFromCombo(id == COMBOBOX_WORLD_SKILL_GATE ? cbGateSkill : id == COMBOBOX_WORLD_SKILL_RANK ? cbRankSkill : cbRoamSkill);
			return true;
		}
		break;
	case irr::gui::EGET_LISTBOX_CHANGED:
	case irr::gui::EGET_LISTBOX_SELECTED_AGAIN: {
		switch(id) {
		case LISTBOX_WORLD_DUELISTS: UpdateDuelistInfo(); return true;
		case LISTBOX_WORLD_PACKS: UpdatePackInfo(); return true;
		case LISTBOX_WORLD_TRADER: UpdateTraderInfo(); return true;
		case LISTBOX_WORLD_ITEMS: UpdateItemInfo(); return true;
		case LISTBOX_WORLD_SKILLS: UpdateSkillInfo(); return true;
		case LISTBOX_WORLD_STRUCTS: UpdateStructInfo(); return true;
		case LISTBOX_WORLD_MISSIONS: UpdateMissionInfo(); return true;
		case LISTBOX_WORLD_GATE: UpdateGateInfo(); return true;
		default: break;
		}
		break;
	}
	default:
		break;
	}
	return false;
}

}
