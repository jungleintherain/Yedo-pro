#include "yedo_patcher.h"
#include <cstdio>
#include <functional>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <IGUIEnvironment.h>
#include <IGUIWindow.h>
#include <IGUIEditBox.h>
#include <IGUIStaticText.h>
#include <IGUIButton.h>
#include "config.h"
#include "game.h"
#include "game_config.h"
#include "utils.h"
#include "fmt.h"
#include "bufferio.h"
#include "file_stream.h"

namespace ygo {

YEdoPatcher gPatcher;

namespace {
constexpr auto LINK_FILE = EPRO_TEXT("./config/yedo_update.txt");
constexpr auto BUILD_FILE = EPRO_TEXT("./config/yedo_build.txt");
constexpr auto INSTALLED_FILE = EPRO_TEXT("./config/yedo_patch_installed.txt");
constexpr auto ZIP_FILE = EPRO_TEXT("./_yedo_patch.zip");
constexpr auto TEMP_DIR = EPRO_TEXT("./_yedo_patch");

std::string ReadLine(epro::path_stringview path) {
	FileStream in{ path.data(), FileStream::in };
	std::string line;
	if(in.good())
		std::getline(in, line);
	while(!line.empty() && (line.back() == '\r' || line.back() == '\n' || line.back() == ' '))
		line.pop_back();
	while(!line.empty() && (line.front() == ' ' || line.front() == '\xef' || line.front() == '\xbb' || line.front() == '\xbf'))
		line.erase(line.begin());
	return line;
}

void WriteLine(epro::path_stringview path, const std::string& text) {
	FileStream out{ path.data(), FileStream::out | FileStream::trunc };
	out << text << '\n';
}

// accepts a Google Drive share link (".../file/d/<id>/view...", "...?id=<id>...") or any direct URL
std::string DownloadUrl(const std::string& link) {
	auto id_from = [&](const std::string& key) -> std::string {
		auto p = link.find(key);
		if(p == std::string::npos)
			return {};
		p += key.size();
		auto e = link.find_first_of("/?&#", p);
		return link.substr(p, e == std::string::npos ? std::string::npos : e - p);
	};
	if(link.find("drive.google.com") != std::string::npos || link.find("drive.usercontent.google.com") != std::string::npos) {
		auto id = id_from("/d/");
		if(id.empty())
			id = id_from("id=");
		if(!id.empty())
			return "https://drive.usercontent.google.com/download?id=" + id + "&export=download&confirm=t";
	}
	return link;
}

size_t WriteFile(char* ptr, size_t size, size_t nmemb, void* userdata) {
	return fwrite(ptr, size, nmemb, static_cast<FILE*>(userdata));
}

int Progress(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
	auto* p = static_cast<std::atomic<int>*>(clientp);
	if(dltotal > 0)
		*p = static_cast<int>(dlnow * 100 / dltotal);
	else
		*p = static_cast<int>(-1000 - dlnow / (1024 * 1024));  // unknown size: show MB
	return 0;
}
}

YEdoPatcher::~YEdoPatcher() {
	if(worker.joinable())
		worker.join();
}

bool YEdoPatcher::Enabled() const {
#if EDOPRO_ANDROID
	return true;
#else
	return false;
#endif
}

void YEdoPatcher::Create() {
	auto* env = mainGame->env;
	wPatch = env->addWindow(mainGame->Scale(200, 140, 824, 450), false, L"อัปเดตเกม (แพตช์)");
	wPatch->getCloseButton()->setVisible(false);
	wPatch->setVisible(false);
	auto* info = env->addStaticText(L"วางลิงก์ Google Drive ของไฟล์แพตช์ (yedo-patch.zip) แล้วกด \"อัปเดต\"\n"
									L"เซฟ เด็ค และการตั้งค่าจะไม่หาย - อัปเสร็จแล้วปิดเกมแล้วเปิดใหม่",
									mainGame->Scale(15, 30, 610, 80), false, true, wPatch);
	(void)info;
	ebLink = env->addEditBox(L"", mainGame->Scale(15, 88, 610, 118), true, wPatch);
	stStatus = env->addStaticText(L"", mainGame->Scale(15, 128, 610, 250), false, true, wPatch);
	btnStart = env->addButton(mainGame->Scale(15, 262, 300, 296), wPatch, BUTTON_YEDO_PATCH_START, L"อัปเดต");
	btnClose = env->addButton(mainGame->Scale(325, 262, 610, 296), wPatch, BUTTON_YEDO_PATCH_CLOSE, L"ปิด");
}

void YEdoPatcher::SetStatus(std::wstring text) {
	std::lock_guard<epro::mutex> lock(status_mutex);
	status = std::move(text);
	dirty = true;
}

void YEdoPatcher::Open() {
	if(!wPatch)
		return;
	ebLink->setText(BufferIO::DecodeUTF8(ReadLine(LINK_FILE)).data());
	const auto build = ReadLine(BUILD_FILE);
	const auto installed = ReadLine(INSTALLED_FILE);
	SetStatus(epro::format(L"APK รุ่น: {}\nแพตช์ที่ลงไว้: {}", build.empty() ? L"-" : BufferIO::DecodeUTF8(build),
						   installed.empty() ? L"ยังไม่เคยลง" : BufferIO::DecodeUTF8(installed)));
	mainGame->HideElement(mainGame->wMainMenu);
	mainGame->ShowElement(wPatch);
}

bool YEdoPatcher::OnEvent(const irr::SEvent& event) {
	if(event.EventType != irr::EET_GUI_EVENT || event.GUIEvent.EventType != irr::gui::EGET_BUTTON_CLICKED)
		return false;
	switch(event.GUIEvent.Caller->getID()) {
		case BUTTON_YEDO_UPDATE:
			Open();
			return true;
		case BUTTON_YEDO_PATCH_START: {
			if(running)
				return true;
			std::string link = BufferIO::EncodeUTF8(ebLink->getText());
			while(!link.empty() && (link.back() == ' ' || link.back() == '\n'))
				link.pop_back();
			if(link.empty()) {
				SetStatus(L"ยังไม่ได้ใส่ลิงก์");
				return true;
			}
			WriteLine(LINK_FILE, link);
			if(worker.joinable())
				worker.join();
			running = true;
			btnStart->setEnabled(false);
			btnClose->setEnabled(false);
			worker = std::thread(&YEdoPatcher::Run, this, link);
			return true;
		}
		case BUTTON_YEDO_PATCH_CLOSE:
			if(running)
				return true;
			mainGame->HideElement(wPatch);
			mainGame->ShowElement(mainGame->wMainMenu);
			return true;
	}
	return false;
}

void YEdoPatcher::Tick() {
	if(!wPatch || !wPatch->isVisible())
		return;
	if(running) {
		const int p = progress;
		if(p >= 0)
			stStatus->setText(epro::format(L"กำลังดาวน์โหลด... {}%", p).data());
		else if(p <= -1000)
			stStatus->setText(epro::format(L"กำลังดาวน์โหลด... {} MB", -1000 - p).data());
	}
	if(dirty) {
		std::lock_guard<epro::mutex> lock(status_mutex);
		stStatus->setText(status.data());
		dirty = false;
	}
	if(!running && !btnStart->isEnabled()) {
		btnStart->setEnabled(true);
		btnClose->setEnabled(true);
	}
}

void YEdoPatcher::Run(std::string link) {
	Utils::SetThreadName("YEdoPatcher");
	auto finish = [&](std::wstring text) {
		progress = -1;
		SetStatus(std::move(text));
		running = false;
	};
	progress = 0;
	SetStatus(L"กำลังเชื่อมต่อ...");
	Utils::FileDelete(ZIP_FILE);
	{
		FILE* fp = fopen(Utils::ToUTF8IfNeeded(ZIP_FILE).data(), "wb");
		if(!fp)
			return finish(L"สร้างไฟล์ชั่วคราวไม่ได้");
		auto* curl = curl_easy_init();
		if(!curl) {
			fclose(fp);
			return finish(L"เริ่มดาวน์โหลดไม่ได้");
		}
		char err[CURL_ERROR_SIZE]{};
		const auto url = DownloadUrl(link);
		curl_easy_setopt(curl, CURLOPT_URL, url.data());
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, err);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFile);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
		curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, Progress);
		curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress);
		if(gGameConfig->ssl_certificate_path.size() && Utils::FileExists(Utils::ToPathString(gGameConfig->ssl_certificate_path)))
			curl_easy_setopt(curl, CURLOPT_CAINFO, gGameConfig->ssl_certificate_path.data());
		const auto res = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		fclose(fp);
		if(res != CURLE_OK) {
			Utils::FileDelete(ZIP_FILE);
			return finish(epro::format(L"ดาวน์โหลดไม่สำเร็จ: {}\nเช็คเน็ต และลิงก์ต้องแชร์แบบ \"ทุกคนที่มีลิงก์\"", BufferIO::DecodeUTF8(err)));
		}
	}
	// a Drive page instead of the file (not shared publicly / wrong link)
	{
		bool ok = false;
		{
			FileStream in{ ZIP_FILE, FileStream::in | FileStream::binary };
			char magic[2]{};
			in.read(magic, 2);
			ok = in.good() && magic[0] == 'P' && magic[1] == 'K';
		}
		if(!ok) {
			Utils::FileDelete(ZIP_FILE);
			return finish(L"ไฟล์ที่ได้ไม่ใช่แพตช์ (zip)\nตั้งการแชร์ไฟล์ใน Drive เป็น \"ทุกคนที่มีลิงก์\" แล้วลองใหม่");
		}
	}
	progress = -1;
	SetStatus(L"กำลังแตกไฟล์...");
	Utils::DeleteDirectory(epro::format(EPRO_TEXT("{}/"), TEMP_DIR));
	if(!Utils::UnzipArchive(ZIP_FILE, nullptr, nullptr, TEMP_DIR)) {
		Utils::FileDelete(ZIP_FILE);
		return finish(L"แตกไฟล์แพตช์ไม่ได้ (ไฟล์อาจเสีย ลองใหม่อีกครั้ง)");
	}
	Utils::FileDelete(ZIP_FILE);
	nlohmann::json info;
	try {
		FileStream in{ epro::format(EPRO_TEXT("{}/yedo_patch.json"), TEMP_DIR), FileStream::in };
		info = nlohmann::json::parse(in);
	} catch(...) {
		Utils::DeleteDirectory(epro::format(EPRO_TEXT("{}/"), TEMP_DIR));
		return finish(L"ไฟล์นี้ไม่ใช่แพตช์ของ YEdo Mod Pro");
	}
	const std::string base = info.value("base", "");
	const std::string version = info.value("version", "");
	const std::string build = ReadLine(BUILD_FILE);
	if(!build.empty() && !base.empty() && base != build) {
		Utils::DeleteDirectory(epro::format(EPRO_TEXT("{}/"), TEMP_DIR));
		return finish(epro::format(L"แพตช์นี้ทำมาจาก APK รุ่น {} แต่เครื่องนี้เป็นรุ่น {}\n"
								   L"ให้ลง APK ล่าสุด หรือสร้างแพตช์ใหม่ด้วย make-patch.bat",
								   BufferIO::DecodeUTF8(base), BufferIO::DecodeUTF8(build)));
	}
	// copy every extracted file over the game data
	int copied = 0, failed = 0;
	const epro::path_string root = epro::format(EPRO_TEXT("{}/"), TEMP_DIR);
	std::function<void(const epro::path_string&)> walk = [&](const epro::path_string& rel) {
		Utils::FindFiles(root + rel, [&](epro::path_stringview name, bool isdir) {
			if(name == EPRO_TEXT(".") || name == EPRO_TEXT(".."))
				return;
			const epro::path_string sub = rel + epro::path_string(name);
			if(isdir) {
				walk(sub + EPRO_TEXT("/"));
				return;
			}
			if(sub == EPRO_TEXT("yedo_patch.json"))
				return;
			Utils::CreatePath(sub, EPRO_TEXT("./"));
			if(Utils::FileCopy(root + sub, EPRO_TEXT("./") + sub))
				copied++;
			else
				failed++;
		});
	};
	walk(EPRO_TEXT(""));
	int removed = 0;
	if(info.contains("delete") && info["delete"].is_array())
		for(const auto& f : info["delete"])
			if(f.is_string() && Utils::FileDelete(Utils::ToPathString(epro::format("./{}", f.get<std::string>()))))
				removed++;
	Utils::DeleteDirectory(root);
	if(failed > 0)
		return finish(epro::format(L"ลงแพตช์ได้ไม่ครบ ({} ไฟล์ล้มเหลว) ลองกดอัปเดตอีกครั้ง", failed));
	WriteLine(INSTALLED_FILE, version);
	finish(epro::format(L"อัปเดตเสร็จแล้ว! (แพตช์ {} - เปลี่ยน {} ไฟล์, ลบ {} ไฟล์)\n"
						L"ปิดเกมให้สนิท (ปัดทิ้งจากแอปล่าสุด) แล้วเปิดใหม่ เพื่อใช้ข้อมูลใหม่",
						BufferIO::DecodeUTF8(version), copied, removed));
}

}
