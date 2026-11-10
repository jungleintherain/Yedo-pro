#include "sandbox.h"
#include <IGUIEnvironment.h>
#include <IGUIWindow.h>
#include <IGUIListBox.h>
#include <IGUIStaticText.h>
#include <IGUIComboBox.h>
#include <IGUIEditBox.h>
#include <IGUIButton.h>
#include "game.h"
#include "game_config.h"
#include "deck_manager.h"
#include "utils.h"
#include "fmt.h"
#include "story_mode.h"
#include "world.h"
#include "windbot_panel.h"

namespace ygo {

Sandbox gSandbox;

void Sandbox::CreateGUI() {
	auto* env = mainGame->env;
	wSandbox = env->addWindow(mainGame->Scale(160, 80, 864, 565), false, L"Sandbox (Freestyle) - ปลดล็อคการ์ดทุกใบ ดวลกับบอทได้อิสระ");
	wSandbox->getCloseButton()->setVisible(false);
	wSandbox->setVisible(false);

	env->addStaticText(L"เลือกคู่ต่อสู้ (WindBot)", mainGame->Scale(15, 32, 300, 52), false, false, wSandbox);
	lstBots = env->addListBox(mainGame->Scale(15, 54, 300, 420), wSandbox, LISTBOX_SANDBOX_BOTS, true);
	lstBots->setItemHeight(mainGame->Scale(22));

	stInfo = env->addStaticText(L"", mainGame->Scale(315, 32, 690, 150), true, true, wSandbox);
	env->addStaticText(L"เด็คของคุณ", mainGame->Scale(315, 158, 690, 178), false, false, wSandbox);
	cbDeck = env->addComboBox(mainGame->Scale(315, 180, 690, 205), wSandbox, COMBOBOX_SANDBOX_DECK);
	stBotDeck = env->addStaticText(L"เด็คของบอท (เฉพาะ Feelin' Lucky)", mainGame->Scale(315, 213, 690, 233), false, false, wSandbox);
	cbBotDeck = env->addComboBox(mainGame->Scale(315, 235, 690, 260), wSandbox, COMBOBOX_SANDBOX_BOTDECK);
	env->addStaticText(L"LP เริ่มต้น", mainGame->Scale(315, 272, 440, 292), false, false, wSandbox);
	ebLP = env->addEditBox(L"8000", mainGame->Scale(445, 268, 560, 294), true, wSandbox);
	ebLP->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	env->addStaticText(L"สกิล", mainGame->Scale(315, 302, 420, 322), false, false, wSandbox);
	cbSkill = env->addComboBox(mainGame->Scale(425, 299, 605, 324), wSandbox);
	env->addButton(mainGame->Scale(610, 299, 690, 324), wSandbox, BUTTON_SANDBOX_SKILL_INFO, L"ข้อมูลสกิล");
	env->addStaticText(L"ใช้การ์ดได้ทุกใบ ไม่ตรวจเด็ค ไม่มีรางวัล (ดับเบิลคลิกบอท = ดวลทันที)",
					   mainGame->Scale(315, 328, 690, 348), false, true, wSandbox);
	btnDuel = env->addButton(mainGame->Scale(315, 352, 690, 410), wSandbox, BUTTON_SANDBOX_DUEL, L"ดวล!  (Duel)");

	env->addButton(mainGame->Scale(15, 432, 230, 468), wSandbox, BUTTON_SANDBOX_DECKS, L"สร้าง / แก้เด็ค (ทุกการ์ด)");
	env->addButton(mainGame->Scale(245, 432, 460, 468), wSandbox, BUTTON_SANDBOX_LAN, L"LAN / ออนไลน์ (แบบเดิม)");
	env->addButton(mainGame->Scale(475, 432, 690, 468), wSandbox, BUTTON_SANDBOX_BACK, L"กลับเมนูหลัก");

	// main menu entry
	mainGame->btnLanMode->setText(L"Sandbox (Freestyle) ✦");
	auto& ds = mainGame->defaultStrings;
	ds.erase(std::remove_if(ds.begin(), ds.end(), [](const auto& p) { return p.first == mainGame->btnLanMode; }), ds.end());
}

bool Sandbox::IsGeneric(int index) const {
	const auto& bots = mainGame->gBot.bots;
	return index >= 0 && static_cast<size_t>(index) < bots.size() && &bots[static_cast<size_t>(index)] == mainGame->gBot.genericEngine;
}

void Sandbox::Refresh() {
	const int old = lstBots->getSelected();
	lstBots->clear();
	for(const auto& bot : mainGame->gBot.bots) {
		std::wstring stars;
		for(int i = 0; i < 5; i++)
			stars += i < bot.difficulty + 1 ? L"★" : L"☆";
		lstBots->addItem(epro::format(L"{}  {}", stars, bot.name).data());
	}
	if(lstBots->getItemCount() > 0)
		lstBots->setSelected(old >= 0 && static_cast<irr::u32>(old) < lstBots->getItemCount() ? old : 0);
	auto fill = [](irr::gui::IGUIComboBox* cb) {
		const std::wstring prev = cb->getSelected() >= 0 ? cb->getItem(static_cast<irr::u32>(cb->getSelected())) : gGameConfig->lastdeck;
		mainGame->RefreshDeck(cb);
		for(irr::u32 i = 0; i < cb->getItemCount(); i++)
			if(prev == cb->getItem(i))
				cb->setSelected(static_cast<irr::s32>(i));
		if(cb->getSelected() < 0 && cb->getItemCount() > 0)
			cb->setSelected(0);
	};
	fill(cbDeck);
	fill(cbBotDeck);
	const int old_skill = cbSkill->getSelected();
	cbSkill->clear();
	cbSkill->addItem(L"ไม่ใช้สกิล", 0);
	for(const auto& sk : gWorld.Skills())
		cbSkill->addItem(epro::format(L"{} ({})", sk.name, sk.name_en).data(), sk.code);
	cbSkill->setSelected(old_skill >= 0 && static_cast<irr::u32>(old_skill) < cbSkill->getItemCount() ? old_skill : 0);
	UpdateInfo();
}

void Sandbox::UpdateInfo() {
	const int sel = lstBots->getSelected();
	const auto& bots = mainGame->gBot.bots;
	if(sel < 0 || static_cast<size_t>(sel) >= bots.size()) {
		stInfo->setText(L"ไม่พบรายชื่อบอท (ตรวจสอบโฟลเดอร์ WindBot)");
		btnDuel->setEnabled(false);
		return;
	}
	btnDuel->setEnabled(true);
	const auto& bot = bots[static_cast<size_t>(sel)];
	const bool generic = IsGeneric(sel);
	std::wstring stars;
	for(int i = 0; i < 5; i++)
		stars += i < bot.difficulty + 1 ? L"★" : L"☆";
	stInfo->setText(epro::format(L"{}\nความยาก: {}\n{}", bot.name, stars,
								 generic ? L"AI ทั่วไป - จะเล่นด้วยเด็คที่คุณเลือกให้ด้านล่าง"
										 : L"บอทใช้เด็คประจำตัว (WindBot)").data());
	cbBotDeck->setEnabled(generic);
	stBotDeck->setEnabled(generic);
}

void Sandbox::Open() {
	Refresh();
	if(mainGame->wMainMenu->isVisible())
		mainGame->HideElement(mainGame->wMainMenu);
	mainGame->ShowElement(wSandbox);
}

void Sandbox::OpenDeckEditor() {
	// same as the main menu "Deck Edit" button, but comes back here (every card is available)
	mainGame->RefreshDeck(mainGame->cbDBDecks);
	const std::wstring want = cbDeck->getSelected() >= 0 ? cbDeck->getItem(static_cast<irr::u32>(cbDeck->getSelected())) : gGameConfig->lastdeck;
	for(irr::u32 i = 0; i < mainGame->cbDBDecks->getItemCount(); i++)
		if(want == mainGame->cbDBDecks->getItem(i))
			mainGame->cbDBDecks->setSelected(static_cast<irr::s32>(i));
	if(mainGame->cbDBDecks->getSelected() != -1) {
		mainGame->deckBuilder.SetCurrentDeckFromFile(Utils::ToPathString(mainGame->cbDBDecks->getItem(static_cast<irr::u32>(mainGame->cbDBDecks->getSelected()))), true);
		mainGame->ebDeckname->setText(L"");
	}
	mainGame->HideElement(wSandbox);
	return_to_sandbox = true;
	gWorld.return_to_hub = false;
	mainGame->deckBuilder.Initialize();
}

void Sandbox::OpenLan() {
	mainGame->isHostingOnline = false;
	mainGame->btnCreateHost->setEnabled(true);
	mainGame->btnJoinHost->setEnabled(true);
	mainGame->btnJoinCancel->setEnabled(true);
	mainGame->HideElement(wSandbox);
	mainGame->ShowElement(mainGame->wLanWindow);
}

void Sandbox::StartDuel() {
	const int sel = lstBots->getSelected();
	const auto& bots = mainGame->gBot.bots;
	if(sel < 0 || static_cast<size_t>(sel) >= bots.size())
		return;
	if(cbDeck->getSelected() < 0) {
		mainGame->PopupMessage(L"ยังไม่ได้เลือกเด็คของคุณ\n(กด \"สร้าง / แก้เด็ค\" เพื่อสร้างเด็คใหม่)");
		return;
	}
	const auto& bot = bots[static_cast<size_t>(sel)];
	StoryChapter ch;
	ch.id = "sandbox";
	ch.arc = L"Sandbox";
	ch.title = L"Sandbox";
	ch.opponent = bot.name;
	ch.engine = bot.deck;
	ch.difficulty = bot.difficulty;
	ch.sandbox = true;
	if(cbSkill->getSelected() > 0)
		ch.player_skill = cbSkill->getItemData(static_cast<irr::u32>(cbSkill->getSelected()));
	if(IsGeneric(sel)) {
		if(cbBotDeck->getSelected() < 0) {
			mainGame->PopupMessage(L"เลือกเด็คให้บอท Feelin' Lucky ก่อน");
			return;
		}
		ch.bot_deck_path = Utils::ToUnicodeIfNeeded(DeckManager::GetDeckPath(Utils::ToPathString(cbBotDeck->getItem(static_cast<irr::u32>(cbBotDeck->getSelected())))));
	}
	try {
		const long lp = std::stol(ebLP->getText());
		ch.start_lp = static_cast<uint32_t>(std::min<long>(std::max<long>(lp, 100), 999999));
	}
	catch(...) {
		ch.start_lp = 8000;
	}
	ebLP->setText(fmt::to_wstring(ch.start_lp).data());
	const std::wstring deck = cbDeck->getItem(static_cast<irr::u32>(cbDeck->getSelected()));
	mainGame->HideElement(wSandbox);
	gStory.StartCustomDuel(ch, deck, wSandbox, [this](int) { Refresh(); });
}

bool Sandbox::OnEvent(const irr::SEvent& event) {
	if(event.EventType != irr::EET_GUI_EVENT)
		return false;
	auto* caller = event.GUIEvent.Caller;
	if(!caller)
		return false;
	const int id = caller->getID();
	switch(event.GUIEvent.EventType) {
	case irr::gui::EGET_BUTTON_CLICKED: {
		if(caller == mainGame->btnLanMode) {
			Open();
			return true;
		}
		if(!wSandbox->isVisible())
			return false;
		switch(id) {
		case BUTTON_SANDBOX_DUEL: StartDuel(); return true;
			case BUTTON_SANDBOX_SKILL_INFO: {
				const int sel = cbSkill->getSelected();
				gWorld.ShowSkillInfo(sel > 0 ? cbSkill->getItemData(static_cast<irr::u32>(sel)) : 0);
				return true;
			}
		case BUTTON_SANDBOX_DECKS: OpenDeckEditor(); return true;
		case BUTTON_SANDBOX_LAN: OpenLan(); return true;
		case BUTTON_SANDBOX_BACK:
			mainGame->HideElement(wSandbox);
			mainGame->ShowElement(mainGame->wMainMenu);
			return true;
		default: break;
		}
		break;
	}
	case irr::gui::EGET_LISTBOX_CHANGED:
		if(id == LISTBOX_SANDBOX_BOTS) {
			UpdateInfo();
			return true;
		}
		break;
	case irr::gui::EGET_LISTBOX_SELECTED_AGAIN:
		if(id == LISTBOX_SANDBOX_BOTS) {
			StartDuel();
			return true;
		}
		break;
	default: break;
	}
	return false;
}

}
