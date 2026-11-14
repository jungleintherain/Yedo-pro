#ifndef YEDO_PATCHER_H
#define YEDO_PATCHER_H

// YEdo Mod Pro: in-game updater for the Android build.
// Downloads a data patch (zip made by _android\make-patch.bat on the PC, uploaded to Google Drive),
// checks that it belongs to the installed APK build and extracts the changed files over the game data.
// Saves, decks and settings are never touched.

#include <IEventReceiver.h>
#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include "text_types.h"
#include "compiler_features.h"
#include "epro_mutex.h"

namespace irr {
namespace gui {
class IGUIWindow;
class IGUIEditBox;
class IGUIStaticText;
class IGUIButton;
}
}

namespace ygo {

enum YEdoPatcherIds {
	BUTTON_YEDO_UPDATE = 30900,
	BUTTON_YEDO_PATCH_START,
	BUTTON_YEDO_PATCH_CLOSE,
};

class YEdoPatcher {
public:
	~YEdoPatcher();
	void Create();
	bool OnEvent(const irr::SEvent& event);
	void Tick();
	bool Enabled() const;
	irr::gui::IGUIButton* btnMenu = nullptr;
private:
	void Open();
	void Run(std::string link);
	void SetStatus(std::wstring text);
	irr::gui::IGUIWindow* wPatch = nullptr;
	irr::gui::IGUIEditBox* ebLink = nullptr;
	irr::gui::IGUIStaticText* stStatus = nullptr;
	irr::gui::IGUIButton* btnStart = nullptr;
	irr::gui::IGUIButton* btnClose = nullptr;
	std::thread worker;
	std::atomic<bool> running{ false };
	std::atomic<bool> dirty{ false };
	std::atomic<int> progress{ -1 };
	epro::mutex status_mutex;
	std::wstring status;
};

extern YEdoPatcher gPatcher;

}

#endif
