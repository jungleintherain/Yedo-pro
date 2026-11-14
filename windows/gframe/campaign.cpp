#include "campaign.h"
#include <ctime>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <IGUIEnvironment.h>
#include <IGUIWindow.h>
#include <IGUIListBox.h>
#include <IGUIStaticText.h>
#include <IGUIComboBox.h>
#include <IGUICheckBox.h>
#include <IGUIButton.h>
#include "game.h"
#include "game_config.h"
#include "file_stream.h"
#include "bufferio.h"
#include "utils.h"
#include "deck_manager.h"
#include "fmt.h"
#include "story_mode.h"
#include "world.h"
#include "windbot_panel.h"

namespace ygo {

Campaign gCampaign;

namespace {

constexpr auto CONFIG_FILE = EPRO_TEXT("./story/campaign/campaign.json");
constexpr auto PROGRESS_FILE = EPRO_TEXT("./story/campaign/progress.json");
constexpr auto BOSS_DECK_FILE = L"./story/campaign/_boss.ydk";

std::wstring JW(const nlohmann::json& obj, const char* key) {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_string())
		return {};
	return BufferIO::DecodeUTF8(it->get<std::string>());
}

irr::video::SColor HexColor(const std::string& hex, irr::video::SColor def) {
	if(hex.size() != 7 || hex[0] != '#')
		return def;
	try {
		const auto v = std::stoul(hex.substr(1), nullptr, 16);
		return irr::video::SColor(255, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
	}
	catch(...) {
		return def;
	}
}

std::wstring Hearts(int n, int max) {
	std::wstring s;
	for(int i = 0; i < max; i++)
		s += i < n ? L"♥" : L"♡";
	return s;
}

}

int Campaign::Rand(int n) {
	if(n <= 0)
		return 0;
	if(!rng)
		rng = static_cast<uint32_t>(std::time(nullptr)) | 1u;
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return static_cast<int>(rng % static_cast<uint32_t>(n));
}

/* ------------------------------------------------------------------ data */

void Campaign::LoadConfig() {
	rounds.clear();
	pools.clear();
	boss_overrides.clear();
	FileStream file{ CONFIG_FILE, FileStream::in };
	if(!file.good())
		return;
	try {
		nlohmann::json j;
		file >> j;
		start_lives = j.value("lives", 3);
		time_limit_min = std::max(0, j.value("time_limit_min", 15));
		free_percent = j.value("free_deck_reward_percent", 30);
		for(const auto& r : j.at("rounds")) {
			Round rd;
			rd.rank = JW(r, "rank");
			rd.color = HexColor(r.value("color", std::string()), rd.color);
			rd.pool = r.value("pool", std::string());
			rd.boss = r.value("boss", false);
			rd.skill = r.value("skill", 0u);
			rd.skill_label = JW(r, "skill_label");
			rd.portrait = JW(r, "portrait");
			rd.gold = r.value("gold", 0);
			rd.gems = r.value("gems", 0);
			const auto card = r.value("card", std::string());
			rd.card_tier = card == "UR" ? 3 : (card == "SR" ? 2 : 0);
			rounds.push_back(std::move(rd));
		}
		for(const auto& [name, list] : j.at("pools").items()) {
			auto& v = pools[name];
			for(const auto& b : list)
				if(b.is_string())
					v.push_back(BufferIO::DecodeUTF8(b.get<std::string>()));
		}
		if(j.contains("boss_overrides"))
			for(const auto& [name, file_name] : j["boss_overrides"].items())
				if(file_name.is_string() && name[0] != '_')
					boss_overrides[BufferIO::DecodeUTF8(name)] = BufferIO::DecodeUTF8(file_name.get<std::string>());
	}
	catch(const std::exception& e) {
		mainGame->PopupMessage(epro::format(L"campaign.json เสียหาย:\n{}", BufferIO::DecodeUTF8(e.what())));
	}
}

void Campaign::LoadProgress() {
	prog = Progress{};
	prog.lives = start_lives;
	FileStream file{ PROGRESS_FILE, FileStream::in };
	if(!file.good())
		return;
	try {
		nlohmann::json j;
		file >> j;
		prog.active = j.value("active", false);
		prog.free_deck = false;
		prog.round = j.value("round", 0);
		prog.lives = j.value("lives", start_lives);
		prog.best = j.value("best", 0);
		prog.clears = j.value("clears", 0);
		prog.runs = j.value("runs", 0);
		prog.skill = j.value("skill", 0u);
		if(j.contains("opponents"))
			for(const auto& o : j["opponents"])
				prog.opponents.push_back(BufferIO::DecodeUTF8(o.get<std::string>()));
	}
	catch(...) {
		prog = Progress{};
		prog.lives = start_lives;
	}
}

void Campaign::SaveProgress() const {
	nlohmann::json j;
	j["active"] = prog.active;
	j["free_deck"] = prog.free_deck;
	j["round"] = prog.round;
	j["lives"] = prog.lives;
	j["best"] = prog.best;
	j["clears"] = prog.clears;
	j["runs"] = prog.runs;
	j["skill"] = prog.skill;
	j["opponents"] = nlohmann::json::array();
	for(const auto& o : prog.opponents)
		j["opponents"].push_back(BufferIO::EncodeUTF8(o));
	FileStream file{ PROGRESS_FILE, FileStream::out | FileStream::trunc };
	if(file.good())
		file << j.dump(1);
}

int Campaign::BotIndex(const std::wstring& name) const {
	const auto& bots = mainGame->gBot.bots;
	for(size_t i = 0; i < bots.size(); i++)
		if(bots[i].name == name)
			return static_cast<int>(i);
	return -1;
}

void Campaign::NewRun() {
	prog.active = true;
	prog.round = 0;
	prog.lives = start_lives;
	prog.free_deck = chkFree && chkFree->isChecked();
	prog.opponents.clear();
	prog.runs++;
	for(const auto& rd : rounds) {
		std::vector<std::wstring> candidates;
		auto it = pools.find(rd.pool);
		if(it != pools.end())
			for(const auto& name : it->second)
				if(BotIndex(name) >= 0 && std::find(prog.opponents.begin(), prog.opponents.end(), name) == prog.opponents.end())
					candidates.push_back(name);
		if(candidates.empty() && it != pools.end()) // allow repeats rather than nothing
			for(const auto& name : it->second)
				if(BotIndex(name) >= 0)
					candidates.push_back(name);
		if(candidates.empty() && !mainGame->gBot.bots.empty())
			candidates.push_back(mainGame->gBot.bots.front().name);
		prog.opponents.push_back(candidates.empty() ? std::wstring() : candidates[static_cast<size_t>(Rand(static_cast<int>(candidates.size())))]);
	}
	SaveProgress();
}

std::wstring Campaign::BossDeckFile(const std::wstring& bot_deck, uint32_t skill, const std::wstring& bot_name) const {
	// the bot's own deck (or a meta list from boss_overrides) + the boss skill card
	epro::path_string src = epro::format(EPRO_TEXT("./WindBot/Decks/AI_{}.ydk"), Utils::ToPathString(bot_deck));
	auto ov = boss_overrides.find(bot_name);
	if(ov != boss_overrides.end())
		src = Utils::ToPathString(L"./story/campaign/" + ov->second);
	std::vector<std::string> lines;
	{
		FileStream in{ src, FileStream::in };
		if(!in.good())
			return {};
		std::string line;
		while(std::getline(in, line)) {
			while(!line.empty() && (line.back() == '\r' || line.back() == '\n'))
				line.pop_back();
			lines.push_back(line);
		}
	}
	FileStream out{ Utils::ToPathString(BOSS_DECK_FILE), FileStream::out | FileStream::trunc };
	if(!out.good())
		return {};
	bool added = false;
	for(const auto& line : lines) {
		out << line << '\n';
		if(!added && line.rfind("#main", 0) == 0) {
			out << skill << '\n';
			added = true;
		}
	}
	if(!added)
		return {};
	return BOSS_DECK_FILE;
}

/* ------------------------------------------------------------------- gui */

void Campaign::CreateGUI() {
	auto* env = mainGame->env;
	wCampaign = env->addWindow(mainGame->Scale(110, 40, 914, 600), false, L"แคมเปญ Rank Challenge - ชนะ 10 รอบติดกัน (มี 3 ชีวิต)");
	wCampaign->getCloseButton()->setVisible(false);
	wCampaign->setVisible(false);
	lstLadder = env->addListBox(mainGame->Scale(15, 34, 330, 545), wCampaign, LISTBOX_CAMPAIGN_LADDER, true);
	lstLadder->setItemHeight(mainGame->Scale(49));
	stHeader = env->addStaticText(L"", mainGame->Scale(345, 34, 790, 56), false, false, wCampaign);
	stHeader->setOverrideColor(irr::video::SColor(255, 255, 200, 120));
	stInfo = env->addStaticText(L"", mainGame->Scale(345, 60, 790, 280), true, true, wCampaign);
	env->addStaticText(L"เด็คของคุณ", mainGame->Scale(345, 290, 790, 310), false, false, wCampaign);
	cbDeck = env->addComboBox(mainGame->Scale(345, 312, 790, 337), wCampaign);
	chkFree = env->addCheckBox(false, mainGame->Scale(345, 345, 790, 370), wCampaign, CHECKBOX_CAMPAIGN_FREE,
							   L"โหมดเด็คอิสระ (ใช้การ์ดอะไรก็ได้ รางวัลเหลือ 30%) - เลือกได้ตอนเริ่มรอบใหม่");
	env->addStaticText(L"สกิล", mainGame->Scale(345, 380, 395, 400), false, false, wCampaign);
	cbSkill = env->addComboBox(mainGame->Scale(395, 376, 700, 401), wCampaign, COMBOBOX_CAMPAIGN_SKILL);
	env->addButton(mainGame->Scale(705, 376, 790, 401), wCampaign, BUTTON_CAMPAIGN_SKILL_INFO, L"ข้อมูลสกิล");
	stSkill = env->addStaticText(L"", mainGame->Scale(345, 403, 790, 421), false, false, wCampaign);
	stSkill->setOverrideColor(irr::video::SColor(255, 170, 170, 190));
	btnDuel = env->addButton(mainGame->Scale(345, 425, 790, 480), wCampaign, BUTTON_CAMPAIGN_DUEL, L"ดวล!");
	env->addButton(mainGame->Scale(345, 495, 560, 535), wCampaign, BUTTON_CAMPAIGN_RESTART, L"เริ่มใหม่ (สุ่มคู่ต่อสู้ใหม่)");
	env->addButton(mainGame->Scale(575, 495, 790, 535), wCampaign, BUTTON_CAMPAIGN_BACK, L"กลับ Duel World");
}

irr::gui::IGUIElement* Campaign::InfoBox() const {
	return stInfo;
}

const std::vector<std::wstring>* Campaign::Pool(const std::string& name) {
	if(pools.empty())
		LoadConfig();
	auto it = pools.find(name);
	return it == pools.end() ? nullptr : &it->second;
}

std::wstring Campaign::CurrentOpponent() const {
	if(!prog.active || prog.round < 0 || static_cast<size_t>(prog.round) >= prog.opponents.size())
		return {};
	return prog.opponents[static_cast<size_t>(prog.round)];
}

std::wstring Campaign::CurrentPortrait() const {
	if(!prog.active || prog.round < 0 || static_cast<size_t>(prog.round) >= rounds.size())
		return {};
	return rounds[static_cast<size_t>(prog.round)].portrait;
}

void Campaign::Open() {
	if(mainGame->gBot.bots.empty())
		mainGame->RefreshAiDecks();
	LoadConfig();
	LoadProgress();
	if(rounds.empty()) {
		mainGame->PopupMessage(L"ไม่พบข้อมูลแคมเปญ (story/campaign/campaign.json)");
		gWorld.ShowHubOrMenu();
		return;
	}
	if(!prog.active || prog.opponents.size() != rounds.size())
		NewRun();
	FillDecks();
	Refresh();
	mainGame->ShowElement(wCampaign);
}

// campaign: every deck is allowed - the player's own decks, then every Structure Deck (borrowed for the duel)
static constexpr auto CAMPAIGN_TEMP_DECK = L"_Campaign Deck";
static constexpr auto BORROW_TAG = L"[ยืม] ";

void Campaign::FillDecks() {
	mainGame->RefreshDeck(cbDeck);
	for(irr::u32 i = 0; i < cbDeck->getItemCount(); i++)
		if(cbDeck->getItem(i) == std::wstring(CAMPAIGN_TEMP_DECK)) {
			cbDeck->removeItem(i);
			break;
		}
	std::vector<std::wstring> have;
	for(irr::u32 i = 0; i < cbDeck->getItemCount(); i++)
		have.emplace_back(cbDeck->getItem(i));
	for(const auto& name : gWorld.AllStructDeckNames())
		if(std::find(have.begin(), have.end(), name) == have.end())
			cbDeck->addItem((BORROW_TAG + name).data());
	const std::wstring want = last_pick.empty() ? std::wstring(gGameConfig->lastdeck) : last_pick;
	cbDeck->setSelected(-1);
	for(irr::u32 i = 0; i < cbDeck->getItemCount(); i++)
		if(want == cbDeck->getItem(i))
			cbDeck->setSelected(static_cast<irr::s32>(i));
	if(cbDeck->getSelected() < 0 && cbDeck->getItemCount() > 0)
		cbDeck->setSelected(0);
}

void Campaign::Refresh() {
	lstLadder->clear();
	const int n = static_cast<int>(rounds.size());
	for(int i = n - 1; i >= 0; i--) {
		const auto& rd = rounds[static_cast<size_t>(i)];
		std::wstring mark, who;
		if(i < prog.round)
			mark = L"★ ";
		else if(i == prog.round)
			mark = L"▶ ";
		else
			mark = L"   ";
		if(i <= prog.round || rd.boss)
			who = prog.opponents.size() > static_cast<size_t>(i) ? prog.opponents[static_cast<size_t>(i)] : L"?";
		else
			who = L"???";
		const auto idx = lstLadder->addItem(epro::format(L"{}{:>2}. {}  -  {}", mark, i + 1, rd.rank, who).data());
		irr::video::SColor c = rd.color;
		if(i < prog.round)
			c = irr::video::SColor(255, 120, 200, 120);
		lstLadder->setItemOverrideColor(idx, c);
	}
	// select the current round
	lstLadder->setSelected(n - 1 - std::min(prog.round, n - 1));
	stHeader->setText(epro::format(L"ชีวิต {}    รอบ {}/{}    สถิติดีสุด: {} รอบ    เคลียร์: {} ครั้ง",
								   Hearts(prog.lives, start_lives), std::min(prog.round + 1, n), n, prog.best, prog.clears).data());
	chkFree->setChecked(false);
	chkFree->setVisible(false);
	const int r = std::min(prog.round, n - 1);
	const auto& rd = rounds[static_cast<size_t>(r)];
	const auto name = CurrentOpponent();
	const int bi = BotIndex(name);
	std::wstring stars;
	if(bi >= 0)
		for(int i = 0; i < 5; i++)
			stars += i < mainGame->gBot.bots[static_cast<size_t>(bi)].difficulty + 1 ? L"★" : L"☆";
	const int pct = prog.free_deck ? free_percent : 100;
	std::wstring reward = epro::format(L"ทอง {}  เพชร {}", rd.gold * pct / 100, rd.gems * pct / 100);
	if(rd.card_tier == 3)
		reward += L"  + การ์ด UR";
	else if(rd.card_tier == 2)
		reward += L"  + การ์ด SR ขึ้นไป";
	std::wstring info = epro::format(L"รอบที่ {}  ·  {}\n{}\nคู่ต่อสู้: {}   {}\n\nรางวัลเมื่อชนะ: {}", r + 1, rd.rank,
									 rd.boss ? L"★ รอบบอส - เด็คเมต้าเต็มสูบ พร้อมแฮนด์แทรป/เชน" : L"", name, stars, reward);
	if(!rd.skill_label.empty())
		info += L"\n" + rd.skill_label;
	info += L"\n\nแพ้ = เสีย 1 ชีวิต (ดวลรอบเดิมใหม่ได้)\nชีวิตหมด = เริ่มใหม่จากรอบ 1";
	stInfo->setText(info.data());
	RefreshSkillCombo();
	stSkill->setText(L"แคมเปญ: ใช้เด็คและสกิลได้ทุกอัน (รวม Structure Deck ทุกชุด) ได้รางวัลเต็ม");
	btnDuel->setText(rd.boss ? L"ท้าดวลบอส!" : L"ดวล!");
}

void Campaign::RefreshSkillCombo() {
	// every skill is free in the campaign (Duel World keeps its own shop / equipped skill)
	cbSkill->clear();
	cbSkill->addItem(L"ไม่ใช้สกิล", 0);
	for(const auto& sk : gWorld.Skills()) {
		const wchar_t* tier = sk.tier == "god" ? L"[เทพ] " : sk.tier == "legendary" ? L"[ตำนาน] " : sk.tier == "broken" ? L"[โกง] " : sk.tier == "special" ? L"[พิเศษ] " : L"";
		const auto idx = cbSkill->addItem(epro::format(L"{}{} ({})", tier, sk.name, sk.name_en).data(), sk.code);
		if(sk.code == prog.skill)
			cbSkill->setSelected(static_cast<irr::s32>(idx));
	}
	if(cbSkill->getSelected() < 0)
		cbSkill->setSelected(0);
}

void Campaign::StartDuel() {
	if(prog.round >= static_cast<int>(rounds.size()))
		NewRun();
	if(cbDeck->getSelected() < 0) {
		mainGame->PopupMessage(L"เลือกเด็คก่อน");
		return;
	}
	// campaign: every deck and every skill is unlocked, full rewards
	prog.free_deck = false;
	std::wstring deck = cbDeck->getItem(static_cast<irr::u32>(cbDeck->getSelected()));
	last_pick = deck;
	const std::wstring tag = BORROW_TAG;
	if(deck.compare(0, tag.size(), tag) == 0) {
		if(!gWorld.WriteStructDeckAs(deck.substr(tag.size()), CAMPAIGN_TEMP_DECK)) {
			mainGame->PopupMessage(L"โหลด Structure Deck นี้ไม่ได้");
			return;
		}
		deck = CAMPAIGN_TEMP_DECK;
	}
	const auto& rd = rounds[static_cast<size_t>(prog.round)];
	const auto name = CurrentOpponent();
	const int bi = BotIndex(name);
	if(bi < 0) {
		mainGame->PopupMessage(epro::format(L"ไม่พบบอท \"{}\" ในโฟลเดอร์ WindBot", name));
		return;
	}
	const auto& bot = mainGame->gBot.bots[static_cast<size_t>(bi)];
	StoryChapter ch;
	ch.id = "campaign";
	ch.arc = L"Campaign";
	ch.title = rd.rank;
	ch.opponent = name;
	ch.engine = bot.deck;
	ch.difficulty = bot.difficulty;
	ch.quick = true;
	ch.free_deck = prog.free_deck;
	ch.portrait = rd.portrait;
	ch.skill_label = rd.skill_label;
	if(rd.skill) {
		const auto file = BossDeckFile(bot.deck, rd.skill, name);
		if(!file.empty())
			ch.bot_deck_path = file;
	}
	SaveProgress();
	mainGame->HideElement(wCampaign);
	ch.kind = "campaign";
	ch.time_limit_ms = static_cast<uint32_t>(time_limit_min) * 60000u;
	ch.player_skill = prog.skill;
	gStory.StartCustomDuel(ch, deck, wCampaign, [this](int result) { OnResult(result); });
}

void Campaign::OnResult(int result) {
	const int n = static_cast<int>(rounds.size());
	Utils::FileDelete(DeckManager::GetDeckPath(Utils::ToPathString(std::wstring(CAMPAIGN_TEMP_DECK))));
	if(!last_pick.empty())
		gGameConfig->lastdeck = last_pick;
	if(result == 0) {
		const auto& rd = rounds[static_cast<size_t>(prog.round)];
		const int pct = prog.free_deck ? free_percent : 100;
		prog.round++;
		prog.best = std::max(prog.best, prog.round);
		std::wstring title = epro::format(L"ชนะรอบ {} - {}!", prog.round, rd.rank);
		if(rd.boss)
			gWorld.AddStat("campaign_boss");
		if(prog.round >= n) {
			gWorld.AddStat("campaign_clear");
			prog.clears++;
			prog.active = false;
			title = L"เคลียร์แคมเปญ! คุณคือ King of Games!";
		}
		gWorld.GrantReward(title, static_cast<int64_t>(rd.gold) * pct / 100, static_cast<int64_t>(rd.gems) * pct / 100, prog.free_deck ? 0 : rd.card_tier);
		if(!prog.active)
			NewRun();
	} else if(result == 1) {
		prog.lives--;
		if(prog.lives <= 0) {
			gWorld.Toast(epro::format(L"ชีวิตหมดแล้ว - ไปได้ถึงรอบ {} เริ่มรอบใหม่ด้วยคู่ต่อสู้ชุดใหม่", prog.round + 1));
			prog.active = false;
			NewRun();
		} else {
			gWorld.Toast(epro::format(L"แพ้... เหลือ {} ชีวิต ลองใหม่อีกครั้ง!", prog.lives));
		}
	}
	SaveProgress();
	Refresh();
}

bool Campaign::OnEvent(const irr::SEvent& event) {
	if(event.EventType != irr::EET_GUI_EVENT || !event.GUIEvent.Caller || !wCampaign || !wCampaign->isVisible())
		return false;
	const int id = event.GUIEvent.Caller->getID();
	switch(event.GUIEvent.EventType) {
	case irr::gui::EGET_BUTTON_CLICKED:
		switch(id) {
		case BUTTON_CAMPAIGN_DUEL: StartDuel(); return true;
		case BUTTON_CAMPAIGN_SKILL_INFO: {
			const int sel = cbSkill->getSelected();
			gWorld.ShowSkillInfo(sel > 0 ? cbSkill->getItemData(static_cast<irr::u32>(sel)) : 0);
			return true;
		}
		case BUTTON_CAMPAIGN_RESTART:
			prog.active = false;
			NewRun();
			Refresh();
			return true;
		case BUTTON_CAMPAIGN_BACK:
			mainGame->HideElement(wCampaign);
			gWorld.ShowHubOrMenu();
			return true;
		default: break;
		}
		break;
	case irr::gui::EGET_COMBO_BOX_CHANGED:
		if(id == COMBOBOX_CAMPAIGN_SKILL) {
			const int sel = cbSkill->getSelected();
			prog.skill = sel > 0 ? cbSkill->getItemData(static_cast<irr::u32>(sel)) : 0;
			SaveProgress();
			return true;
		}
		break;
	case irr::gui::EGET_CHECKBOX_CHANGED:
		if(id == CHECKBOX_CAMPAIGN_FREE) {
			prog.free_deck = chkFree->isChecked();
			SaveProgress();
			Refresh();
			return true;
		}
		break;
	default: break;
	}
	return false;
}

}
