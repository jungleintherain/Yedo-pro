#include "yedo_picker.h"
#include <algorithm>
#include <cwctype>
#include <IGUIEnvironment.h>
#include <IGUIWindow.h>
#include <IGUIEditBox.h>
#include <IGUIStaticText.h>
#include <IGUIButton.h>
#include <IGUIComboBox.h>
#include <IGUIListBox.h>
#include "config.h"
#include "game.h"
#include "fmt.h"
#include "world.h"

namespace ygo {

YEdoPicker gPicker;

namespace {
std::wstring Lower(std::wstring s) {
	for(auto& c : s)
		c = static_cast<wchar_t>(std::towlower(c));
	return s;
}
irr::gui::IGUIElement* FindUp(irr::gui::IGUIElement* e, irr::gui::EGUI_ELEMENT_TYPE type) {
	while(e && e != mainGame->env->getRootGUIElement()) {
		if(e->getType() == type)
			return e;
		e = e->getParent();
	}
	return nullptr;
}
void SendGuiEvent(irr::gui::IGUIElement* caller, irr::gui::EGUI_EVENT_TYPE type) {
	irr::SEvent ev{};
	ev.EventType = irr::EET_GUI_EVENT;
	ev.GUIEvent.Caller = caller;
	ev.GUIEvent.Element = nullptr;
	ev.GUIEvent.EventType = type;
	// same route a real change takes: the parents, up to the environment and the active event receiver
	if(caller->getParent())
		caller->getParent()->OnEvent(ev);
}
}

bool YEdoPicker::IsOpen() const {
	// own flag: a modal screen's isVisible() depends on Irrlicht internals and must not decide this
	return open && modal;
}

void YEdoPicker::Build() {
	auto* env = mainGame->env;
	modal = env->addModalScreen(nullptr);
	window = env->addWindow(irr::core::recti(0, 0, 100, 100), false, L"", modal);
	window->getCloseButton()->setVisible(false);
	window->setDraggable(false);
	ebFilter = env->addEditBox(L"", irr::core::recti(0, 0, 10, 10), true, window, EDITBOX_YEDO_PICK_FILTER);
	for(int i = 0; i < PER_PAGE; i++)
		btnItems[i] = env->addButton(irr::core::recti(0, 0, 10, 10), window, BUTTON_YEDO_PICK_0 + i, L"");
	btnPrev = env->addButton(irr::core::recti(0, 0, 10, 10), window, BUTTON_YEDO_PICK_PREV, L"< ก่อนหน้า");
	btnNext = env->addButton(irr::core::recti(0, 0, 10, 10), window, BUTTON_YEDO_PICK_NEXT, L"ถัดไป >");
	btnCancel = env->addButton(irr::core::recti(0, 0, 10, 10), window, BUTTON_YEDO_PICK_CANCEL, L"ยกเลิก");
	stPage = env->addStaticText(L"", irr::core::recti(0, 0, 10, 10), false, false, window);
	stPage->setTextAlignment(irr::gui::EGUIA_CENTER, irr::gui::EGUIA_CENTER);
	modal->setVisible(false);
}

void YEdoPicker::Layout() {
	const int W = static_cast<int>(mainGame->window_size.Width);
	const int H = static_cast<int>(mainGame->window_size.Height);
	const int ww = W * 92 / 100, wh = H * 90 / 100;
	modal->setRelativePosition(irr::core::recti(0, 0, W, H));
	window->setRelativePosition(irr::core::recti((W - ww) / 2, (H - wh) / 2, (W + ww) / 2, (H + wh) / 2));
	const int pad = std::max(6, wh / 60);
	const int top = wh / 13;          // window title bar
	const int rowh = wh / 10;         // filter + bottom rows
	ebFilter->setRelativePosition(irr::core::recti(pad, top, ww - pad, top + rowh * 8 / 10));
	const int gy = top + rowh;
	const int gh = wh - gy - rowh - pad * 2;
	const int rows = PER_PAGE / 2;
	const int bh = gh / rows;
	const int bw = (ww - pad * 3) / 2;
	for(int i = 0; i < PER_PAGE; i++) {
		const int col = i % 2, row = i / 2;
		const int x = pad + col * (bw + pad), y = gy + row * bh;
		btnItems[i]->setRelativePosition(irr::core::recti(x, y + 2, x + bw, y + bh - 2));
	}
	const int by = wh - rowh - pad;
	const int cw = (ww - pad * 5) / 4;
	btnPrev->setRelativePosition(irr::core::recti(pad, by, pad + cw, by + rowh));
	stPage->setRelativePosition(irr::core::recti(pad * 2 + cw, by, pad * 2 + cw * 2, by + rowh));
	btnNext->setRelativePosition(irr::core::recti(pad * 3 + cw * 2, by, pad * 3 + cw * 3, by + rowh));
	btnCancel->setRelativePosition(irr::core::recti(pad * 4 + cw * 3, by, pad * 4 + cw * 4, by + rowh));
}

void YEdoPicker::Open(const std::wstring& title, std::vector<std::wstring> list, int sel, std::function<void(int)> on_pick) {
	if(!modal)
		Build();
	items = std::move(list);
	selected = sel;
	callback = std::move(on_pick);
	window->setText(title.data());
	ebFilter->setText(L"");
	shown.clear();
	for(int i = 0; i < static_cast<int>(items.size()); i++)
		shown.push_back(i);
	page = selected >= 0 ? selected / PER_PAGE : 0;
	Layout();
	Refresh();
	modal->setVisible(true);
	window->setVisible(true);
	open = true;
	taps = 0;
	mainGame->env->getRootGUIElement()->bringToFront(modal);
	mainGame->env->setFocus(window);
}

void YEdoPicker::Refresh() {
	const int pages = std::max(1, static_cast<int>((shown.size() + PER_PAGE - 1) / PER_PAGE));
	page = std::clamp(page, 0, pages - 1);
	for(int i = 0; i < PER_PAGE; i++) {
		const size_t k = static_cast<size_t>(page * PER_PAGE + i);
		if(k < shown.size()) {
			const int idx = shown[k];
			btnItems[i]->setVisible(true);
			btnItems[i]->setText((idx == selected ? L">> " + items[idx] : items[idx]).data());
			btnItems[i]->setPressed(idx == selected);
		} else {
			btnItems[i]->setVisible(false);
		}
	}
	stPage->setText(epro::format(L"หน้า {}/{} ({} รายการ) · แตะ {}", page + 1, pages, shown.size(), taps).data());
	btnPrev->setEnabled(page > 0);
	btnNext->setEnabled(page + 1 < pages);
}

void YEdoPicker::Close() {
	if(pressed)
		pressed->setPressed(false);
	pressed = nullptr;
	open = false;
	if(modal)
		modal->setVisible(false);
	callback = nullptr;
}

bool YEdoPicker::TryOpenFor(irr::gui::IGUIElement* hovered) {
	if(!hovered || !hovered->isEnabled() || !hovered->isTrulyVisible())
		return false;
	if(auto* e = FindUp(hovered, irr::gui::EGUIET_COMBO_BOX)) {
		auto* cb = static_cast<irr::gui::IGUIComboBox*>(e);
		if(!cb->isEnabled() || cb->getItemCount() < 2)
			return false;
		std::vector<std::wstring> list;
		for(irr::u32 i = 0; i < cb->getItemCount(); i++)
			list.emplace_back(cb->getItem(i));
		std::wstring title = L"เลือก";
		Open(title, std::move(list), cb->getSelected(), [cb](int idx) {
			cb->setSelected(idx);
			mainGame->env->setFocus(cb);
			SendGuiEvent(cb, irr::gui::EGET_COMBO_BOX_CHANGED);
		});
		return true;
	}
	// Duel World skill list: tapping it opens the popup with every skill
	if(auto* e = FindUp(hovered, irr::gui::EGUIET_LIST_BOX)) {
		if(e != gWorld.SkillListElement())
			return false;
		auto* lb = static_cast<irr::gui::IGUIListBox*>(e);
		if(lb->getItemCount() < 2)
			return false;
		std::vector<std::wstring> list;
		for(irr::u32 i = 0; i < lb->getItemCount(); i++)
			list.emplace_back(lb->getListItem(i));
		Open(L"เลือกสกิล", std::move(list), lb->getSelected(), [lb](int idx) {
			lb->setSelected(idx);
			SendGuiEvent(lb, irr::gui::EGET_LISTBOX_CHANGED);
		});
		return true;
	}
	return false;
}

irr::gui::IGUIButton* YEdoPicker::ButtonAt(const irr::core::vector2di& pos) const {
	irr::gui::IGUIButton* all[PER_PAGE + 3];
	int n = 0;
	for(auto* b : btnItems) all[n++] = b;
	all[n++] = btnPrev;
	all[n++] = btnNext;
	all[n++] = btnCancel;
	for(int i = 0; i < n; i++) {
		auto* b = all[i];
		if(b && b->isVisible() && b->isEnabled() && b->getAbsolutePosition().isPointInside(pos))
			return b;
	}
	return nullptr;
}

bool YEdoPicker::Click(int id) {
	if(id >= BUTTON_YEDO_PICK_0 && id < BUTTON_YEDO_PICK_0 + PER_PAGE) {
		const size_t k = static_cast<size_t>(page * PER_PAGE + (id - BUTTON_YEDO_PICK_0));
		if(k < shown.size()) {
			auto cb = std::move(callback);
			const int idx = shown[k];
			Close();
			if(cb)
				cb(idx);
		}
		return true;
	}
	switch(id) {
		case BUTTON_YEDO_PICK_PREV: page--; Refresh(); return true;
		case BUTTON_YEDO_PICK_NEXT: page++; Refresh(); return true;
		case BUTTON_YEDO_PICK_CANCEL: Close(); return true;
		default: return false;
	}
}

bool YEdoPicker::OnEvent(const irr::SEvent& event) {
	if(modal && event.EventType == irr::EET_GUI_EVENT && event.GUIEvent.Caller && event.GUIEvent.EventType == irr::gui::EGET_BUTTON_CLICKED &&
	   event.GUIEvent.Caller->getParent() == window) {
		if(Click(event.GUIEvent.Caller->getID()))
			return true;
	}
	if(IsOpen()) {
		// The popup handles its own taps (hit test on the button rectangles) instead of relying on the
		// GUI focus/hover routing, which other screens' handlers or a stuck focus could swallow.
		if(event.EventType == irr::EET_MOUSE_INPUT_EVENT) {
			const irr::core::vector2di pos(event.MouseInput.X, event.MouseInput.Y);
			if(ebFilter && ebFilter->getAbsolutePosition().isPointInside(pos)) {
				if(event.MouseInput.Event == irr::EMIE_LMOUSE_PRESSED_DOWN)
					mainGame->env->setFocus(ebFilter);
				return false; // normal path (Android text input dialog)
			}
			switch(event.MouseInput.Event) {
				case irr::EMIE_LMOUSE_PRESSED_DOWN:
					taps++;
					Refresh();
					if(pressed)
						pressed->setPressed(false);
					pressed = ButtonAt(pos);
					if(pressed)
						pressed->setPressed(true);
					break;
				case irr::EMIE_LMOUSE_LEFT_UP: {
					auto* hit = ButtonAt(pos);
					auto* down = pressed;
					pressed = nullptr;
					if(down)
						down->setPressed(false);
					if(hit && hit == down)  // the release of the tap that opened the popup must not pick anything
						Click(hit->getID());
					else
						Refresh();
					break;
				}
				case irr::EMIE_MOUSE_WHEEL:
					page += event.MouseInput.Wheel < 0 ? 1 : -1;
					Refresh();
					break;
				default: break;
			}
			return true;
		}
		if(event.EventType == irr::EET_KEY_INPUT_EVENT && event.KeyInput.Key == irr::KEY_ESCAPE) {
			if(!event.KeyInput.PressedDown)
				Close();
			return true;
		}
		if(event.EventType == irr::EET_GUI_EVENT && event.GUIEvent.Caller) {
			const auto id = event.GUIEvent.Caller->getID();
			if(event.GUIEvent.EventType == irr::gui::EGET_BUTTON_CLICKED && Click(id))
				return true;
			if(id == EDITBOX_YEDO_PICK_FILTER &&
			   (event.GUIEvent.EventType == irr::gui::EGET_EDITBOX_CHANGED || event.GUIEvent.EventType == irr::gui::EGET_EDITBOX_ENTER)) {
				const auto f = Lower(ebFilter->getText());
				shown.clear();
				for(int i = 0; i < static_cast<int>(items.size()); i++)
					if(f.empty() || Lower(items[i]).find(f) != std::wstring::npos)
						shown.push_back(i);
				page = 0;
				Refresh();
				return false;
			}
		}
		return false;
	}
#if EDOPRO_ANDROID
	if(event.EventType == irr::EET_MOUSE_INPUT_EVENT && event.MouseInput.Event == irr::EMIE_LMOUSE_PRESSED_DOWN) {
		auto* hovered = mainGame->env->getRootGUIElement()->getElementFromPoint({ event.MouseInput.X, event.MouseInput.Y });
		return TryOpenFor(hovered);
	}
#endif
	return false;
}

}
