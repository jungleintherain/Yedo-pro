#ifndef SANDBOX_MODE_H
#define SANDBOX_MODE_H

// "Sandbox (Freestyle)": replaces the main menu "LAN + AI" entry.
// Every card is available, any deck can be used, pick a WindBot opponent and duel right away.
// The classic LAN window is still reachable from here.

#include <IEventReceiver.h>

namespace irr {
namespace gui {
class IGUIWindow;
class IGUIListBox;
class IGUIStaticText;
class IGUIComboBox;
class IGUIEditBox;
class IGUIButton;
}
}

namespace ygo {

enum SANDBOX_GUI {
	BUTTON_SANDBOX_DUEL = 30200,
	BUTTON_SANDBOX_DECKS,
	BUTTON_SANDBOX_LAN,
	BUTTON_SANDBOX_BACK,
	LISTBOX_SANDBOX_BOTS,
	COMBOBOX_SANDBOX_DECK,
	COMBOBOX_SANDBOX_BOTDECK,
	BUTTON_SANDBOX_SKILL_INFO,
};

class Sandbox {
public:
	void CreateGUI();
	bool OnEvent(const irr::SEvent& event);   // MenuHandler, before everything else
	void Open();

	bool return_to_sandbox = false;
	irr::gui::IGUIWindow* wSandbox = nullptr;

private:
	void Refresh();
	void UpdateInfo();
	void StartDuel();
	void OpenDeckEditor();
	void OpenLan();
	bool IsGeneric(int index) const;

	irr::gui::IGUIListBox* lstBots = nullptr;
	irr::gui::IGUIStaticText* stInfo = nullptr;
	irr::gui::IGUIComboBox* cbDeck = nullptr;
	irr::gui::IGUIComboBox* cbBotDeck = nullptr;
	irr::gui::IGUIStaticText* stBotDeck = nullptr;
	irr::gui::IGUIEditBox* ebLP = nullptr;
	irr::gui::IGUIComboBox* cbSkill = nullptr;
	irr::gui::IGUIButton* btnDuel = nullptr;
};

extern Sandbox gSandbox;

}

#endif
