#ifndef YEDO_PICKER_H
#define YEDO_PICKER_H

// YEdo Mod Pro: big touch-friendly popup to pick an item (decks, skills, ...).
// On Android every combo box (and the Duel World skill list) opens this popup instead of a tiny dropdown.

#include <IEventReceiver.h>
#include <vector2d.h>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace irr {
namespace gui {
class IGUIElement;
class IGUIWindow;
class IGUIEditBox;
class IGUIStaticText;
class IGUIButton;
}
}

namespace ygo {

enum YEdoPickerIds {
	BUTTON_YEDO_PICK_0 = 30920,  // ..30931
	BUTTON_YEDO_PICK_PREV = 30940,
	BUTTON_YEDO_PICK_NEXT,
	BUTTON_YEDO_PICK_CANCEL,
	EDITBOX_YEDO_PICK_FILTER,
};

class YEdoPicker {
public:
	static constexpr int PER_PAGE = 12;
	// returns true when the event was used (opened the popup or clicked inside it)
	bool OnEvent(const irr::SEvent& event);
	void Open(const std::wstring& title, std::vector<std::wstring> items, int selected, std::function<void(int)> on_pick);
	bool IsOpen() const;
private:
	void Build();
	void Layout();
	void Refresh();
	void Close();
	bool TryOpenFor(irr::gui::IGUIElement* hovered);
	irr::gui::IGUIButton* ButtonAt(const irr::core::vector2di& pos) const;
	bool Click(int id);
	irr::gui::IGUIElement* modal = nullptr;
	irr::gui::IGUIWindow* window = nullptr;
	irr::gui::IGUIEditBox* ebFilter = nullptr;
	irr::gui::IGUIStaticText* stPage = nullptr;
	irr::gui::IGUIButton* btnItems[PER_PAGE]{};
	irr::gui::IGUIButton* btnPrev = nullptr;
	irr::gui::IGUIButton* btnNext = nullptr;
	irr::gui::IGUIButton* btnCancel = nullptr;
	std::vector<std::wstring> items;
	std::vector<int> shown;  // indexes into items after the filter
	int selected = -1;
	int page = 0;
	irr::gui::IGUIButton* pressed = nullptr;
	bool open = false;
	int taps = 0;  // shown in the page label: tells whether taps reach the popup
	std::function<void(int)> callback;
};

extern YEdoPicker gPicker;

}

#endif
