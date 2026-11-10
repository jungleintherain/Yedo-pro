#ifndef STORY_MODE_H
#define STORY_MODE_H

// Story Mode for EDOPro
// Reads chapters from ./story/story.json, shows dialogue scenes and
// starts a local duel against a WindBot that uses the chapter's deck.
// Progress is saved to ./story/progress.json

#include <atomic>
#include <set>
#include <string>
#include <vector>
#include <functional>
#include <IEventReceiver.h>
#include "text_types.h"

namespace irr {
namespace gui {
class IGUIElement;
class IGUIWindow;
class IGUIListBox;
class IGUIStaticText;
class IGUIButton;
class IGUIComboBox;
}
}

namespace ygo {

struct HostInfo;

// IDs far away from the ones in menu_handler.h
enum STORY_GUI {
	BUTTON_STORY_MODE = 30000,
	LISTBOX_STORY_CHAPTERS,
	BUTTON_STORY_PLAY,
	BUTTON_STORY_BACK,
	BUTTON_STORY_RESET,
	BUTTON_STORY_DIALOG_NEXT,
	BUTTON_STORY_DIALOG_SKIP,
	COMBOBOX_STORY_SKILL,
	BUTTON_STORY_SKILL_INFO,
};

struct StoryLine {
	std::wstring speaker;
	std::wstring text;
};

struct StoryChapter {
	std::string id;
	std::wstring arc;
	std::wstring title;
	std::wstring description;
	std::wstring opponent;      // name shown for the bot
	std::wstring deck;          // .ydk file, relative to ./story/decks/
	std::wstring engine;        // WindBot executor ("deck" field of bots.json), default "Lucky"
	std::wstring dialog;        // optional WindBot dialog file in ./WindBot/Dialogs (without .json)
	std::wstring source;        // where the deck came from (url / site), info only
	std::string unlock_after;   // chapter id that must be cleared first ("" = always open)
	std::wstring bot_deck_path; // sandbox: deck file (full path) for the generic AI
	int difficulty = 0;
	bool sandbox = false;       // sandbox duel: free decks, no dialogue after the duel
	bool quick = false;         // no dialogue after the duel (campaign)
	bool free_deck = false;     // any deck allowed (campaign free mode)
	uint32_t player_skill = 0;  // Duel Links style skill card added to the player's deck
	std::wstring portrait;      // opponent portrait file override (story/portraits/)
	std::wstring skill_label;   // opponent skill shown on the VS screen
	uint32_t bot_skill = 0;     // story boss: skill card added to the bot deck
	std::string kind = "story"; // story / roaming / campaign / sandbox (missions)
	int reward_gems = 0;        // extra first-clear gems (story.json "reward_gems")
	bool speed = false;         // Duel Links style Speed Duel (3 zones, no MP2)
	uint32_t start_lp = 8000;
	uint32_t start_hand = 5;
	uint32_t draw_count = 1;
	std::vector<StoryLine> intro, win, lose;
};

class StoryMode {
public:
	// setup
	void Load();
	void CreateGUI();
	// hooks
	bool OnEvent(const irr::SEvent& event);        // MenuHandler
	void Tick();                                   // Game::MainLoop, outside gMutex
	void ApplyHostInfo(HostInfo& info) const;      // DuelClient, before CTOS_CREATE_GAME
	void OnDuelResult(uint8_t local_winner);       // DuelClient MSG_WIN (0 = me, 1 = opponent, other = draw)
	bool IsActive() const { return active; }
	irr::gui::IGUIElement* ReturnWindow(irr::gui::IGUIElement* lan_window) const;

	// used by the Duel World hub
	const std::vector<StoryChapter>& Chapters() const { return chapters; }
	bool ChapterUnlocked(size_t i) const { return i < chapters.size() && IsUnlocked(chapters[i]); }
	bool ChapterCleared(size_t i) const { return i < chapters.size() && IsCleared(chapters[i]); }
	void OpenMenu();
	// play intro lines, duel the bot, play win/lose lines, then call done(result) and show return_to
	void StartCustomDuel(const StoryChapter& ch, const std::wstring& player_deck, irr::gui::IGUIElement* return_to,
						 std::function<void(int)> done);

	// presentation (md_ui)
	const StoryChapter& DuelChapter() const { return duel_ch; }
	bool FreeDuel() const { return active && (duel_ch.sandbox || duel_ch.free_deck); }
	uint32_t DuelSkill() const { return active ? duel_ch.player_skill : 0; }
	std::wstring SelectedOpponent() const { const int i = SelectedChapter(); return i < 0 ? std::wstring() : chapters[static_cast<size_t>(i)].opponent; }
	std::wstring SelectedArc() const { const int i = SelectedChapter(); return i < 0 ? std::wstring() : chapters[static_cast<size_t>(i)].arc; }
	irr::gui::IGUIElement* InfoBox() const;
	std::wstring DialogSpeaker() const { return dialog_pos < dialog_lines.size() ? dialog_lines[dialog_pos].speaker : std::wstring(); }

	irr::gui::IGUIButton* btnStoryMode = nullptr;
	irr::gui::IGUIWindow* wStory = nullptr;
	irr::gui::IGUIWindow* wDialog = nullptr;

private:
	enum class State {
		IDLE,
		DIALOG,
		WAIT_LOBBY,
		WAIT_OPPONENT,
		IN_DUEL,
	};
	void RefreshList();
	void UpdateInfo();
	bool IsUnlocked(const StoryChapter& ch) const;
	bool IsCleared(const StoryChapter& ch) const;
	void LoadProgress();
	void SaveProgress() const;
	void ShowDialog(const std::vector<StoryLine>& lines, std::function<void()> on_end);
	void ShowCurrentLine();
	void EndDialog();
	void StartDuel();
	void LaunchBot();
	void FinishDuel();
	void Fail(const std::wstring& msg);
	void ClickButton(irr::gui::IGUIElement* button);
	int SelectedChapter() const;

	std::vector<StoryChapter> chapters;
	StoryChapter duel_ch;
	std::function<void(int)> duel_done;
	irr::gui::IGUIElement* duel_return = nullptr;
	std::set<std::string> cleared;
	std::wstring load_error;
	int current = -1;
	State state = State::IDLE;
	bool active = false;
	bool bot_launched = false;
	bool ready_sent = false;
	bool start_sent = false;
	uint32_t wait_start_ms = 0;
	uint32_t last_ready_ms = 0;
	std::atomic<int> duel_result{ -1 };

	std::vector<StoryLine> dialog_lines;
	size_t dialog_pos = 0;
	std::function<void()> dialog_end;

	irr::gui::IGUIListBox* lstChapters = nullptr;
	irr::gui::IGUIStaticText* stChapterInfo = nullptr;
	irr::gui::IGUIComboBox* cbPlayerDeck = nullptr;
	irr::gui::IGUIComboBox* cbSkill = nullptr;
	irr::gui::IGUIButton* btnPlay = nullptr;
	irr::gui::IGUIButton* btnBack = nullptr;
	irr::gui::IGUIButton* btnReset = nullptr;
	irr::gui::IGUIStaticText* stPlaceholder = nullptr;
	irr::gui::IGUIStaticText* stSpeaker = nullptr;
	irr::gui::IGUIStaticText* stLine = nullptr;
	irr::gui::IGUIButton* btnNext = nullptr;
	irr::gui::IGUIButton* btnSkip = nullptr;
};

extern StoryMode gStory;

}

#endif // STORY_MODE_H
