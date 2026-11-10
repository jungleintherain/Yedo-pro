#ifndef MD_UI_H
#define MD_UI_H

// Master Duel / Duel Links style presentation layer for the story mod
//  - character portraits (story/portraits/*.png, mapping in portraits.json)
//  - visual-novel scene for story dialogue (portraits + name plate)
//  - styled panels / buttons for duel prompts (query, first/second, options, message...)
//  - card picture next to "activate this effect?" prompts
//  - story/world duels: VS splash, coin toss (instead of rock-paper-scissors),
//    duel start banner, VICTORY / LOSE screen, opponent speech bubbles

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <deque>
#include <SColor.h>
#include <rect.h>

namespace irr {
namespace video {
class ITexture;
}
namespace gui {
class IGUIButton;
class IGUIElement;
class CGUITTFont;
}
}

namespace ygo {

class ClientCard;

class MdUi {
public:
	void Init();              // after every GUI element exists
	void DrawUnderGui();      // right before env->drawAll()
	void DrawOverGui();       // after every other overlay
	void DrawMenuPortraits(); // after the gui, before the other overlays

	// portraits (nullptr if there is no picture for that name)
	irr::video::ITexture* Portrait(const std::wstring& name);
	irr::video::ITexture* PlayerPortrait();
	irr::video::ITexture* PortraitFile(const std::wstring& file);
	irr::video::ITexture* OpponentPortrait();   // current duel opponent (chapter portrait override first)
	static irr::video::SColor ArcColor(const std::wstring& arc);

	// story / world duel presentation. Called from the duel client thread with gMutex held.
	// Each returns how many frames (60 fps) the caller should wait.
	void ResetDuel();
	int Versus();
	int CoinToss(int outcome);      // 0 = you won, 1 = opponent won, 2 = tie
	int DuelStart(bool going_first);
	int Result(int outcome);        // 0 = win, 1 = lose, 2 = draw
	void Speak(const std::wstring& text);
	bool versus_shown = false;
	bool OverlayBusy() const { return overlay != Overlay::NONE; }

	void StyleCircleButton(irr::gui::IGUIButton* btn);
	// Master Duel style action circles under the card (replaces the floating command menu)
	void LayoutCommandMenu(ClientCard* card);
	// true while the cursor travels from the card to its action circles: hover must not jump to other cards
	bool KeepMenuHover(const irr::core::vector2di& pos) const;
	irr::core::recti menu_keep;
	void StyleButton(irr::gui::IGUIButton* btn, bool primary);

private:
	enum class Overlay { NONE, VERSUS, COIN, START, RESULT };
	struct Speech {
		std::wstring text;
		uint32_t start = 0, duration = 0;
	};

	void LoadMap();
	void MakeTextures();
	uint32_t Now() const;
	float Scl() const;
	void Text(irr::gui::CGUITTFont* font, const std::wstring& text, int x, int y, irr::video::SColor color, int align = 0,
			  bool shadow = true);
	void Rect(const irr::core::recti& r, irr::video::SColor top, irr::video::SColor bottom);
	void HRect(const irr::core::recti& r, irr::video::SColor left, irr::video::SColor right);
	void Quad(irr::video::ITexture* tex, float cx, float cy, float w, float h, float rot, irr::video::SColor color, bool additive = false);
	void DrawPortrait(irr::video::ITexture* tex, float cx, float bottom, float height, irr::video::SColor tint, bool flip = false);
	void DrawPanel(const irr::core::recti& rect, const std::wstring& title, irr::video::SColor accent);
	std::vector<std::wstring> Wrap(irr::gui::CGUITTFont* font, const std::wstring& text, int max_width);

	void DrawDialogScene();
	void DrawQueryCard();
	void DrawVersus(float t);
	void DrawCoin(float t);
	void DrawMatching();
	void DrawStart(float t);
	void DrawResult(float t);
	void DrawSpeech();

	bool ready = false;
	std::wstring player_file = L"player.png";
	std::wstring default_file;
	std::map<std::wstring, std::wstring> name_files;
	std::map<std::wstring, irr::video::ITexture*> cache;

	irr::video::ITexture* tWhite = nullptr;
	irr::video::ITexture* tBtn[2][4] = {};   // [primary][up, hover, down, disabled]
	irr::video::ITexture* tCircle[3] = {};    // Master Duel style action circle: up, hover, down
	irr::video::ITexture* tCoin[2] = {};      // front, back
	irr::video::ITexture* tVignette = nullptr;

	std::vector<irr::gui::IGUIElement*> styled;

	// overlays
	Overlay overlay = Overlay::NONE;
	uint32_t overlay_start = 0, overlay_len = 1;
	int overlay_arg = 0;

	// dialogue scene
	std::wstring last_speaker, right_name;
	uint32_t speaker_change = 0;

	std::deque<Speech> speech;
};

extern MdUi gUi;

}

#endif
