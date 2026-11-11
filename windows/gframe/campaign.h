#ifndef CAMPAIGN_MODE_H
#define CAMPAIGN_MODE_H

// Campaign "Rank Challenge": 10 duels in a row against random WindBot opponents that get
// stronger every round (Rookie -> Diamond), round 9 and 10 are bosses with boss skills.
// 3 lives per run. Config: story/campaign/campaign.json, progress: story/campaign/progress.json

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <IEventReceiver.h>
#include <SColor.h>

namespace irr {
namespace gui {
class IGUIWindow;
class IGUIListBox;
class IGUIStaticText;
class IGUIComboBox;
class IGUICheckBox;
class IGUIButton;
class IGUIElement;
}
}

namespace ygo {

enum CAMPAIGN_GUI {
	BUTTON_CAMPAIGN_DUEL = 30300,
	BUTTON_CAMPAIGN_RESTART,
	BUTTON_CAMPAIGN_BACK,
	LISTBOX_CAMPAIGN_LADDER,
	CHECKBOX_CAMPAIGN_FREE,
	COMBOBOX_CAMPAIGN_SKILL,
	BUTTON_CAMPAIGN_SKILL_INFO,
};

class Campaign {
public:
	void CreateGUI();
	bool OnEvent(const irr::SEvent& event);
	void Open();

	// presentation
	irr::gui::IGUIElement* InfoBox() const;
	std::wstring CurrentPortrait() const;   // file in story/portraits/ ("" = default)
	std::wstring CurrentOpponent() const;
	// bot pools from campaign.json (also used by Ranked duels)
	const std::vector<std::wstring>* Pool(const std::string& name);

	irr::gui::IGUIWindow* wCampaign = nullptr;

private:
	struct Round {
		std::wstring rank;
		irr::video::SColor color{ 255, 220, 220, 220 };
		std::string pool;
		bool boss = false;
		uint32_t skill = 0;
		std::wstring skill_label;
		std::wstring portrait;
		int gold = 0, gems = 0;
		int card_tier = 0;   // 0 none, 2 SR+, 3 UR
	};
	struct Progress {
		bool active = false;
		bool free_deck = false;
		int round = 0;      // index of the next round to play
		int lives = 3;
		std::vector<std::wstring> opponents;
		int best = 0;
		int clears = 0;
		int runs = 0;
		uint32_t skill = 0;   // campaign skill (all skills are free here)
	};

	void LoadConfig();
	void LoadProgress();
	void SaveProgress() const;
	void NewRun();
	void Refresh();
	void StartDuel();
	void OnResult(int result);
	int BotIndex(const std::wstring& name) const;
	std::wstring BossDeckFile(const std::wstring& bot_deck, uint32_t skill, const std::wstring& bot_name) const;
	int Rand(int n);

	std::vector<Round> rounds;
	std::map<std::string, std::vector<std::wstring>> pools;
	std::map<std::wstring, std::wstring> boss_overrides;
	int start_lives = 3;
	int time_limit_min = 15;   // tournament clock per duel (campaign.json "time_limit_min", 0 = off)
	int free_percent = 30;
	Progress prog;
	uint32_t rng = 0;

	irr::gui::IGUIListBox* lstLadder = nullptr;
	irr::gui::IGUIStaticText* stHeader = nullptr;
	irr::gui::IGUIStaticText* stInfo = nullptr;
	irr::gui::IGUIComboBox* cbDeck = nullptr;
	irr::gui::IGUICheckBox* chkFree = nullptr;
	irr::gui::IGUIStaticText* stSkill = nullptr;
	irr::gui::IGUIComboBox* cbSkill = nullptr;
	void RefreshSkillCombo();
	irr::gui::IGUIButton* btnDuel = nullptr;
};

extern Campaign gCampaign;

}

#endif
