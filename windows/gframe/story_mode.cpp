#include "story_mode.h"
#include "md_ui.h"
#include <nlohmann/json.hpp>
#include <IGUIFont.h>
#include "CGUITTFont/CGUITTFont.h"
#include <IrrlichtDevice.h>
#include <ITimer.h>
#include <IGUIEnvironment.h>
#include <IGUIButton.h>
#include <IGUIComboBox.h>
#include <IGUIEditBox.h>
#include <IGUIListBox.h>
#include <IGUIStaticText.h>
#include <IGUIWindow.h>
#include <IGUICheckBox.h>
#include "config.h"
#include "game.h"
#include "game_config.h"
#include "duelclient.h"
#include "netserver.h"
#include "network.h"
#include "deck_manager.h"
#include "data_manager.h"
#include "windbot.h"
#include "bufferio.h"
#include "file_stream.h"
#include "logging.h"
#include "utils.h"
#include "fmt.h"
#include "ocgapi_constants.h"
#include "world.h"

namespace ygo {

StoryMode gStory;

namespace {

constexpr auto STORY_FILE = EPRO_TEXT("./story/story.json");
constexpr auto PROGRESS_FILE = EPRO_TEXT("./story/progress.json");
constexpr auto DECKS_DIR = EPRO_TEXT("./story/decks/");

std::wstring GetWString(const nlohmann::json& obj, const char* key, const wchar_t* def = L"") {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_string())
		return def;
	return BufferIO::DecodeUTF8(it->get<std::string>());
}

uint32_t GetUInt(const nlohmann::json& obj, const char* key, uint32_t def) {
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_number_unsigned())
		return def;
	return it->get<uint32_t>();
}

std::vector<StoryLine> GetLines(const nlohmann::json& obj, const char* key) {
	std::vector<StoryLine> ret;
	auto it = obj.find(key);
	if(it == obj.end() || !it->is_array())
		return ret;
	for(const auto& line : *it) {
		if(line.is_string()) {
			ret.push_back({ L"", BufferIO::DecodeUTF8(line.get<std::string>()) });
		} else if(line.is_object()) {
			ret.push_back({ GetWString(line, "speaker"), GetWString(line, "text") });
		}
	}
	return ret;
}

bool IsThaiCombining(wchar_t c) {
	return c == 0x0E31 || (c >= 0x0E34 && c <= 0x0E3A) || (c >= 0x0E47 && c <= 0x0E4E);
}

// Irrlicht only wraps text on spaces, Thai sentences usually have none,
// so pre-break the text using the real font metrics.
std::wstring WrapText(const std::wstring& text, irr::gui::IGUIElement* elem) {
	auto* font = mainGame->guiFont;
	if(!font || !elem)
		return text;
	const auto max_width = elem->getAbsolutePosition().getWidth() - mainGame->Scale(12);
	if(max_width <= 0)
		return text;
	std::wstring out;
	std::wstring line;
	auto width_of = [font](const std::wstring& str) {
		return static_cast<irr::s32>(font->getDimension(str.data()).Width);
	};
	for(size_t i = 0; i < text.size(); i++) {
		const wchar_t c = text[i];
		if(c == L'\n') {
			out += line;
			out += L'\n';
			line.clear();
			continue;
		}
		line.push_back(c);
		if(width_of(line) <= max_width)
			continue;
		// find a break position: last space, otherwise just before this cluster
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

}

/* ---------------------------------------------------------------- loading */

void StoryMode::Load() {
	chapters.clear();
	load_error.clear();
	FileStream file{ STORY_FILE, FileStream::in };
	if(!file.good()) {
		load_error = L"ไม่พบไฟล์ story/story.json\n(No story/story.json found)";
		return;
	}
	nlohmann::json j;
	try {
		file >> j;
	}
	catch(const std::exception& e) {
		load_error = epro::format(L"story.json ผิดรูปแบบ (invalid json):\n{}", BufferIO::DecodeUTF8(e.what()));
		ErrorLog("Failed to load story.json: {}", e.what());
		return;
	}
	const nlohmann::json* list = &j;
	if(j.is_object()) {
		auto it = j.find("chapters");
		if(it == j.end()) {
			load_error = L"story.json ไม่มี \"chapters\"";
			return;
		}
		list = &*it;
	}
	if(!list->is_array()) {
		load_error = L"\"chapters\" ต้องเป็น array";
		return;
	}
	for(const auto& obj : *list) {
		if(!obj.is_object())
			continue;
		try {
			StoryChapter ch;
			ch.id = obj.at("id").get<std::string>();
			ch.title = GetWString(obj, "title", L"???");
			ch.arc = GetWString(obj, "arc");
			ch.description = GetWString(obj, "description");
			ch.opponent = GetWString(obj, "opponent", L"???");
			ch.deck = GetWString(obj, "deck");
			ch.engine = GetWString(obj, "engine", L"Lucky");
			ch.dialog = GetWString(obj, "dialog");
			ch.source = GetWString(obj, "source");
			if(obj.contains("unlock_after") && obj["unlock_after"].is_string())
				ch.unlock_after = obj["unlock_after"].get<std::string>();
			ch.start_lp = GetUInt(obj, "start_lp", 8000);
			ch.start_hand = GetUInt(obj, "start_hand", 5);
			ch.draw_count = GetUInt(obj, "draw_count", 1);
			ch.bot_skill = GetUInt(obj, "skill", 0);
			ch.skill_label = GetWString(obj, "skill_label");
			ch.portrait = GetWString(obj, "portrait");
			ch.reward_gems = static_cast<int>(GetUInt(obj, "reward_gems", 0));
			ch.intro = GetLines(obj, "intro");
			ch.win = GetLines(obj, "win");
			ch.lose = GetLines(obj, "lose");
			chapters.push_back(std::move(ch));
		}
		catch(const std::exception& e) {
			ErrorLog("Failed to parse story chapter: {}", e.what());
		}
	}
	if(chapters.empty())
		load_error = L"story.json ไม่มีบทที่ใช้ได้ (no valid chapters)";
	LoadProgress();
}

void StoryMode::LoadProgress() {
	cleared.clear();
	FileStream file{ PROGRESS_FILE, FileStream::in };
	if(!file.good())
		return;
	try {
		nlohmann::json j;
		file >> j;
		for(const auto& id : j.at("cleared"))
			if(id.is_string())
				cleared.insert(id.get<std::string>());
	}
	catch(...) {}
}

void StoryMode::SaveProgress() const {
	nlohmann::json j;
	j["cleared"] = nlohmann::json::array();
	for(const auto& id : cleared)
		j["cleared"].push_back(id);
	FileStream file{ PROGRESS_FILE, FileStream::out | FileStream::trunc };
	if(file.good())
		file << j.dump(2);
}

bool StoryMode::IsCleared(const StoryChapter& ch) const {
	return cleared.count(ch.id) != 0;
}

bool StoryMode::IsUnlocked(const StoryChapter& ch) const {
	return ch.unlock_after.empty() || cleared.count(ch.unlock_after) != 0;
}

/* ---------------------------------------------------------------- gui */

void StoryMode::CreateGUI() {
	auto* env = mainGame->env;
	// chapter select window
	wStory = env->addWindow(mainGame->Scale(160, 80, 860, 560), false, L"Story Mode");
	wStory->getCloseButton()->setVisible(false);
	wStory->setVisible(false);
	lstChapters = env->addListBox(mainGame->Scale(10, 30, 320, 420), wStory, LISTBOX_STORY_CHAPTERS, true);
	lstChapters->setItemHeight(mainGame->Scale(24));
	stChapterInfo = env->addStaticText(L"", mainGame->Scale(330, 30, 690, 300), true, true, wStory);
	env->addStaticText(L"เด็คของคุณ (Your deck):", mainGame->Scale(330, 308, 690, 326), false, false, wStory);
	cbPlayerDeck = env->addComboBox(mainGame->Scale(330, 328, 690, 353), wStory);
	env->addStaticText(L"สกิล (Skill):", mainGame->Scale(330, 362, 690, 380), false, false, wStory);
	cbSkill = env->addComboBox(mainGame->Scale(330, 382, 585, 407), wStory, COMBOBOX_STORY_SKILL);
	env->addButton(mainGame->Scale(590, 382, 690, 407), wStory, BUTTON_STORY_SKILL_INFO, L"ข้อมูลสกิล");
	btnPlay = env->addButton(mainGame->Scale(330, 430, 500, 465), wStory, BUTTON_STORY_PLAY, L"เริ่มดวล (Duel)");
	btnBack = env->addButton(mainGame->Scale(520, 430, 690, 465), wStory, BUTTON_STORY_BACK, L"กลับ (Back)");
	btnReset = env->addButton(mainGame->Scale(10, 430, 320, 465), wStory, BUTTON_STORY_RESET, L"เริ่มเนื้อเรื่องใหม่ (Reset progress)");

	stPlaceholder = env->addStaticText(L"", mainGame->Scale(0, 0, 1, 1), false, false);
	stPlaceholder->setVisible(false);
	// dialogue window (visual-novel style box at the bottom)
	wDialog = env->addWindow(mainGame->Scale(110, 420, 910, 620), false, L"");
	wDialog->getCloseButton()->setVisible(false);
	wDialog->setDraggable(false);
	wDialog->setDrawTitlebar(false);
	wDialog->setVisible(false);
	stSpeaker = env->addStaticText(L"", mainGame->Scale(15, 10, 600, 35), false, false, wDialog);
	stSpeaker->setOverrideColor(irr::video::SColor(255, 255, 210, 80));
	stLine = env->addStaticText(L"", mainGame->Scale(15, 40, 785, 150), false, true, wDialog);
	btnSkip = env->addButton(mainGame->Scale(560, 158, 670, 190), wDialog, BUTTON_STORY_DIALOG_SKIP, L"ข้าม (Skip)");
	btnNext = env->addButton(mainGame->Scale(680, 158, 785, 190), wDialog, BUTTON_STORY_DIALOG_NEXT, L"ถัดไป ▶");
}

void StoryMode::OpenMenu() {
	Load();
	// inside Duel World only its own decks (bought / built from owned cards)
	if(gWorld.CollectionLocked())
		gWorld.FillWorldDecks(cbPlayerDeck);
	else
		mainGame->RefreshDeck(cbPlayerDeck);
	gWorld.FillSkillCombo(cbSkill);
	RefreshList();
	mainGame->ShowElement(wStory);
}

void StoryMode::RefreshList() {
	int old = lstChapters->getSelected();
	lstChapters->clear();
	for(size_t i = 0; i < chapters.size(); i++) {
		const auto& ch = chapters[i];
		const wchar_t* tag = IsCleared(ch) ? L"[✓] " : (IsUnlocked(ch) ? L"[  ] " : L"[ล็อก] ");
		lstChapters->addItem(epro::format(L"{}{}. {}", tag, i + 1, ch.title).data());
	}
	if(old < 0 || old >= (int)chapters.size()) {
		// select the first chapter not cleared yet
		old = 0;
		for(size_t i = 0; i < chapters.size(); i++) {
			if(!IsCleared(chapters[i]) && IsUnlocked(chapters[i])) {
				old = (int)i;
				break;
			}
		}
	}
	if(!chapters.empty())
		lstChapters->setSelected(old);
	UpdateInfo();
}

irr::gui::IGUIElement* StoryMode::InfoBox() const {
	return stChapterInfo;
}

int StoryMode::SelectedChapter() const {
	int sel = lstChapters ? lstChapters->getSelected() : -1;
	if(sel < 0 || sel >= (int)chapters.size())
		return -1;
	return sel;
}

void StoryMode::UpdateInfo() {
	if(!load_error.empty()) {
		stChapterInfo->setText(WrapText(load_error, stChapterInfo).data());
		btnPlay->setEnabled(false);
		return;
	}
	int sel = SelectedChapter();
	if(sel < 0) {
		stChapterInfo->setText(L"");
		btnPlay->setEnabled(false);
		return;
	}
	const auto& ch = chapters[sel];
	std::wstring text = epro::format(L"{}\n\nคู่ต่อสู้: {}\nLP เริ่มต้น: {}\n", ch.title, ch.opponent, ch.start_lp);
	if(!ch.source.empty())
		text += epro::format(L"ที่มาของเด็ค: {}\n", ch.source);
	text += L"\n" + ch.description;
	const bool unlocked = IsUnlocked(ch);
	if(!unlocked) {
		std::wstring need = L"?";
		for(const auto& other : chapters)
			if(other.id == ch.unlock_after)
				need = other.title;
		text += epro::format(L"\n\n[ล็อก] ต้องผ่านบท \"{}\" ก่อน", need);
	} else if(IsCleared(ch)) {
		text += L"\n\n[✓] ผ่านบทนี้แล้ว (เล่นซ้ำได้)";
	}
	stChapterInfo->setText(WrapText(text, stChapterInfo).data());
	btnPlay->setEnabled(unlocked);
}

irr::gui::IGUIElement* StoryMode::ReturnWindow(irr::gui::IGUIElement* lan_window) const {
	// while a story / world duel is running, the menu comes back only after the win/lose dialogue
	return active ? stPlaceholder : lan_window;
}

/* ---------------------------------------------------------------- dialogue */

void StoryMode::ShowDialog(const std::vector<StoryLine>& lines, std::function<void()> on_end) {
	dialog_lines = lines;
	dialog_pos = 0;
	dialog_end = std::move(on_end);
	if(dialog_lines.empty()) {
		EndDialog();
		return;
	}
	ShowCurrentLine();
	mainGame->ShowElement(wDialog);
	mainGame->env->setFocus(btnNext);
}

void StoryMode::ShowCurrentLine() {
	const auto& line = dialog_lines[dialog_pos];
	stSpeaker->setText(line.speaker.data());
	stLine->setText(WrapText(line.text, stLine).data());
	btnNext->setText(dialog_pos + 1 >= dialog_lines.size() ? L"ตกลง ▶" : L"ถัดไป ▶");
}

void StoryMode::EndDialog() {
	if(wDialog->isVisible())
		mainGame->HideElement(wDialog);
	auto cb = std::move(dialog_end);
	dialog_end = nullptr;
	if(cb)
		cb();
}

/* ---------------------------------------------------------------- events */

bool StoryMode::OnEvent(const irr::SEvent& event) {
	if(event.EventType != irr::EET_GUI_EVENT)
		return false;
	auto* caller = event.GUIEvent.Caller;
	if(!caller)
		return false;
	const int id = caller->getID();
	switch(event.GUIEvent.EventType) {
	case irr::gui::EGET_BUTTON_CLICKED: {
		switch(id) {
		case BUTTON_STORY_MODE: {
			mainGame->HideElement(mainGame->wMainMenu);
			gWorld.Open();
			return true;
		}
		case BUTTON_STORY_BACK: {
			mainGame->HideElement(wStory);
			gWorld.ShowHubOrMenu();
			return true;
		}
		case BUTTON_STORY_RESET: {
			cleared.clear();
			SaveProgress();
			RefreshList();
			return true;
		}
		case BUTTON_STORY_SKILL_INFO:
			gWorld.ShowComboSkillInfo(cbSkill);
			return true;
		case BUTTON_STORY_PLAY: {
			int sel = SelectedChapter();
			if(sel < 0 || !IsUnlocked(chapters[sel]))
				return true;
			if(cbPlayerDeck->getSelected() < 0) {
				mainGame->PopupMessage(L"กรุณาเลือกเด็คของคุณก่อน\n(Please select your deck first)");
				return true;
			}
			const auto deck_path = epro::format(EPRO_TEXT("{}{}"), DECKS_DIR, Utils::ToPathString(chapters[sel].deck));
			if(chapters[sel].deck.empty() || !Utils::FileExists(deck_path)) {
				mainGame->PopupMessage(epro::format(L"ไม่พบเด็คของคู่ต่อสู้:\nstory/decks/{}", chapters[sel].deck));
				return true;
			}
			const std::wstring player_deck = cbPlayerDeck->getItem(cbPlayerDeck->getSelected());
			if(!gWorld.CheckDeckByName(player_deck))
				return true;
			const bool first_clear = !IsCleared(chapters[sel]);
			const size_t index = static_cast<size_t>(sel);
			mainGame->HideElement(wStory);
			StartCustomDuel(chapters[sel], player_deck, wStory, [this, index, first_clear](int result) {
				if(result == 0) {
					cleared.insert(chapters[index].id);
					SaveProgress();
				}
				RefreshList();
				gWorld.OnStoryResult(chapters[index], index, result, first_clear);
			});
			return true;
		}
		case BUTTON_STORY_DIALOG_NEXT: {
			if(++dialog_pos >= dialog_lines.size())
				EndDialog();
			else
				ShowCurrentLine();
			return true;
		}
		case BUTTON_STORY_DIALOG_SKIP: {
			EndDialog();
			return true;
		}
		default:
			break;
		}
		break;
	}
	case irr::gui::EGET_COMBO_BOX_CHANGED:
		if(id == COMBOBOX_STORY_SKILL) {
			gWorld.EquipFromCombo(cbSkill);
			return true;
		}
		break;
	case irr::gui::EGET_LISTBOX_CHANGED:
	case irr::gui::EGET_LISTBOX_SELECTED_AGAIN: {
		if(id == LISTBOX_STORY_CHAPTERS) {
			UpdateInfo();
			return true;
		}
		break;
	}
	default:
		break;
	}
	return false;
}

/* ---------------------------------------------------------------- duel */

void StoryMode::StartCustomDuel(const StoryChapter& ch, const std::wstring& player_deck, irr::gui::IGUIElement* return_to,
								std::function<void(int)> done) {
	duel_ch = ch;
	if(ch.sandbox)
		duel_ch.kind = "sandbox";
	// Duel World / story: the equipped (bought) skill. Sandbox and campaign pick their own skill for free.
	if(!ch.sandbox && ch.kind != "campaign" && !duel_ch.player_skill)
		duel_ch.player_skill = gWorld.EquippedSkill();
	// story bosses: put the boss skill in a copy of the bot deck
	if(duel_ch.bot_skill && duel_ch.bot_deck_path.empty() && !duel_ch.deck.empty()) {
		FileStream in{ epro::format(EPRO_TEXT("{}{}"), DECKS_DIR, Utils::ToPathString(duel_ch.deck)), FileStream::in };
		if(in.good()) {
			std::string text, line;
			while(std::getline(in, line)) {
				if(!line.empty() && line.back() == '\r')
					line.pop_back();
				text += line + "\n";
				if(line == "#main")
					text += std::to_string(duel_ch.bot_skill) + "\n";
			}
			const epro::path_string out_path = EPRO_TEXT("./story/decks/_story_boss.ydk");
			FileStream out{ out_path, FileStream::out | FileStream::trunc };
			out << text;
			duel_ch.bot_deck_path = L"./story/decks/_story_boss.ydk";
		}
	}
	gWorld.DuelBegin(duel_ch);
	gUi.ResetDuel();
	duel_done = std::move(done);
	duel_return = return_to;
	current = 0;
	gGameConfig->lastdeck = player_deck;
	state = State::DIALOG;
	ShowDialog(duel_ch.intro, [this] { StartDuel(); });
}

void StoryMode::ApplyHostInfo(HostInfo& info) const {
	if(!active || current < 0)
		return;
	const auto& ch = duel_ch;
	info.mode = 0;
	info.start_lp = ch.start_lp;
	info.start_hand = static_cast<uint8_t>(ch.speed ? 4 : ch.start_hand);
	info.draw_count = static_cast<uint8_t>(ch.draw_count);
	info.time_limit = 0;
	info.team1 = 1;
	info.team2 = 1;
	info.best_of = 1;
	// decks taken from the web may not follow the current banlist
	info.no_check_deck_content = 1;
	info.no_shuffle_deck = 0;
	const uint64_t flags = ch.speed ? DUEL_MODE_SPEED : DUEL_MODE_MR5;
	info.duel_flag_low = static_cast<uint32_t>(flags & 0xffffffff);
	info.duel_flag_high = static_cast<uint32_t>((flags >> 32) & 0xffffffff);
	info.forbiddentypes = DUEL_MODE_MR5_FORB;
	info.extra_rules = 0;
	info.sizes = { {0,999},{0,999},{0,999} };
}

void StoryMode::Fail(const std::wstring& msg) {
	active = false;
	state = State::IDLE;
	if(DuelClient::IsConnected())
		DuelClient::StopClient();
	NetServer::StopServer();
	mainGame->ShowElement(duel_return ? duel_return : wStory);
	RefreshList();
	mainGame->PopupMessage(msg);
}

void StoryMode::StartDuel() {
	if(current < 0)
		return;
	uint16_t port = 7911;
	try {
		port = static_cast<uint16_t>(std::stoul(gGameConfig->serverport));
	}
	catch(...) {}
	if(wcslen(mainGame->ebNickName->getText()) == 0)
		mainGame->ebNickName->setText(L"Player");
	mainGame->isHostingOnline = false;
	DuelClient::is_local_host = false;
	active = true;
	bot_launched = false;
	ready_sent = false;
	start_sent = false;
	wait_start_ms = mainGame->device->getTimer()->getRealTime();
	last_ready_ms = 0;
	duel_result = -1;
	mainGame->ebServerPass->setText(L"");
	if(!NetServer::StartServer(port)) {
		Fail(epro::format(L"เปิดห้องดวลไม่สำเร็จ (พอร์ต {} อาจถูกใช้อยู่)\nFailed to host on port {}", port, port));
		return;
	}
	const auto ip = 0x100007F; //127.0.0.1 in network byte order
	if(!DuelClient::StartClient({ &ip, epro::Address::INET }, port)) {
		Fail(L"เชื่อมต่อห้องดวลไม่สำเร็จ\nFailed to connect to the local room");
		return;
	}
	DuelClient::is_local_host = true;
	state = State::WAIT_LOBBY;
}

void StoryMode::LaunchBot() {
	const auto& ch = duel_ch;
	WindBot bot;
	bot.name = ch.opponent;
	bot.deck = ch.engine.empty() ? L"Lucky" : ch.engine;
	bot.difficulty = ch.difficulty;
	bot.dialog = ch.dialog;
	const auto deck_path = !ch.bot_deck_path.empty() ? Utils::GetAbsolutePath(Utils::ToPathString(ch.bot_deck_path))
		: Utils::GetAbsolutePath(epro::format(EPRO_TEXT("{}{}"), DECKS_DIR, Utils::ToPathString(ch.deck)));
	const auto deck_wpath = Utils::ToUnicodeIfNeeded(deck_path);
	int port = 7911;
	try {
		port = std::stoi(gGameConfig->serverport);
	}
	catch(...) {}
	auto res = bot.Launch(port, mainGame->dInfo.secret.pass, true, 0, (ch.deck.empty() && ch.bot_deck_path.empty()) ? nullptr : deck_wpath.data());
#if EDOPRO_LINUX || EDOPRO_MACOS
	if(res > 0)
		mainGame->gBot.windbotsPids.push_back(res);
#endif
	if(!res)
		Fail(L"เปิด WindBot ไม่สำเร็จ (ตรวจสอบโฟลเดอร์ WindBot)\nFailed to launch WindBot");
}

void StoryMode::ClickButton(irr::gui::IGUIElement* button) {
	irr::SEvent ev{};
	ev.EventType = irr::EET_GUI_EVENT;
	ev.GUIEvent.Caller = button;
	ev.GUIEvent.Element = nullptr;
	ev.GUIEvent.EventType = irr::gui::EGET_BUTTON_CLICKED;
	mainGame->menuHandler.OnEvent(ev);
}

void StoryMode::OnDuelResult(uint8_t local_winner) {
	if(!active)
		return;
	duel_result = local_winner;
}

void StoryMode::Tick() {
	if(!active)
		return;
#ifdef STORY_DEBUG
	{
		static int dbg = 0;
		if((++dbg % 60) == 0) {
			FILE* f = fopen("story_debug.txt", "a");
			if(f) {
				fprintf(f, "state=%d conn=%d lobby=%d hpvis=%d rdyvis=%d rdyen=%d deck=%d bot=%d ready=%d start=%d c0=%d c1=%d stvis=%d sten=%d\n", (int)state, (int)DuelClient::IsConnected(), (int)mainGame->dInfo.isInLobby,
						(int)mainGame->wHostPrepare->isVisible(), (int)mainGame->btnHostPrepReady->isVisible(), (int)mainGame->btnHostPrepReady->isEnabled(),
						mainGame->cbDeckSelect->getSelected(), (int)bot_launched, (int)ready_sent, (int)start_sent,
						(int)mainGame->chkHostPrepReady[0]->isChecked(), (int)mainGame->chkHostPrepReady[1]->isChecked(),
						(int)mainGame->btnHostPrepStart->isVisible(), (int)mainGame->btnHostPrepStart->isEnabled());
				fclose(f);
			}
		}
	}
#endif
	switch(state) {
	case State::WAIT_LOBBY:
	case State::WAIT_OPPONENT: {
		const uint32_t now = mainGame->device->getTimer()->getRealTime();
		if(!DuelClient::IsConnected() && !mainGame->dInfo.isInLobby) {
			if(now - wait_start_ms > 10000) // 10 seconds without a room
				Fail(L"ห้องดวลถูกปิด\nThe room was closed");
			return;
		}
		if(!mainGame->dInfo.isInLobby || !mainGame->wHostPrepare->isVisible())
			return;
		if(!bot_launched)
			wait_start_ms = now;
		if(!bot_launched) {
			bot_launched = true;
			LaunchBot();
			if(!active)
				return;
			state = State::WAIT_OPPONENT;
		}
		// press Ready once the room is fully set up, retry if the server did not take it
		if(now - wait_start_ms > 700 && now - last_ready_ms > 1000 && mainGame->btnHostPrepReady->isVisible()
		   && mainGame->btnHostPrepReady->isEnabled() && mainGame->cbDeckSelect->isEnabled()
		   && mainGame->cbDeckSelect->getSelected() >= 0) {
			ready_sent = true;
			last_ready_ms = now;
			ClickButton(mainGame->btnHostPrepReady);
		}
		if(!start_sent && mainGame->btnHostPrepStart->isVisible() && mainGame->btnHostPrepStart->isEnabled()
		   && mainGame->chkHostPrepReady[0]->isChecked() && mainGame->chkHostPrepReady[1]->isChecked()) {
			start_sent = true;
			ClickButton(mainGame->btnHostPrepStart);
			state = State::IN_DUEL;
		}
		break;
	}
	case State::IN_DUEL: {
		// wait until the duel is over and we are back in the menus
		if(mainGame->dInfo.isInDuel || mainGame->dInfo.isInLobby || DuelClient::IsConnected())
			return;
		FinishDuel();
		break;
	}
	default:
		break;
	}
}

void StoryMode::FinishDuel() {
	active = false;
	state = State::IDLE;
	NetServer::StopServer();
	if(wStory->isVisible())
		mainGame->HideElement(wStory);
	stPlaceholder->setVisible(false);
	for(auto& timing : mainGame->chatTiming) // no leftover duel chat over the dialogue
		timing = 0;
	auto& ch = duel_ch;
	const int result = duel_result.exchange(-1);
	gWorld.OnDuelEnd(ch, result);
	auto back_to_menu = [this, result] {
		mainGame->ShowElement(duel_return ? duel_return : wStory);
		auto done = std::move(duel_done);
		duel_done = nullptr;
		if(done)
			done(result);
	};
	if(ch.sandbox || ch.quick) { // sandbox / campaign: straight back to the menu
		back_to_menu();
		return;
	}
	if(result == 0) {
		auto lines = ch.win;
		if(lines.empty())
			lines.push_back({ L"", L"คุณชนะ! (You win!)" });
		ShowDialog(lines, back_to_menu);
	} else {
		auto lines = ch.lose;
		if(lines.empty())
			lines.push_back({ L"", result == 1 ? L"คุณแพ้... ลองใหม่อีกครั้ง! (You lost)" : L"การดวลจบลงโดยไม่มีผู้ชนะ" });
		ShowDialog(lines, back_to_menu);
	}
}

}
