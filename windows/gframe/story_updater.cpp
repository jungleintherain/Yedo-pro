#include "story_updater.h"
#include "compiler_features.h"

#if EDOPRO_WINDOWS
#include <windows.h>
#include <string>
#include <vector>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace ygo {

namespace {

std::wstring Widen(const std::string& s) {
	if(s.empty())
		return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(static_cast<size_t>(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &out[0], n);
	return out;
}

bool Exists(const std::wstring& path) {
	return GetFileAttributesW(path.data()) != INVALID_FILE_ATTRIBUTES;
}

void Log(const std::string& text) {
	FILE* f = _wfopen(L"./story/_update/update_log.txt", L"ab");
	if(!f)
		return;
	fwrite(text.data(), 1, text.size(), f);
	fwrite("\n", 1, 1, f);
	fclose(f);
}

std::vector<char> ReadAll(const std::wstring& path) {
	std::vector<char> data;
	FILE* f = _wfopen(path.data(), L"rb");
	if(!f)
		return data;
	char buf[1 << 16];
	size_t n;
	while((n = fread(buf, 1, sizeof(buf), f)) > 0)
		data.insert(data.end(), buf, buf + n);
	fclose(f);
	return data;
}

}

bool ApplyStoryUpdate() {
	wchar_t buf[MAX_PATH * 4];
	const DWORD len = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(sizeof(buf) / sizeof(buf[0])));
	if(len == 0)
		return false;
	const std::wstring self(buf, len);
	const std::wstring dir = self.substr(0, self.find_last_of(L"\\/") + 1);
	// leftover of a previous update
	DeleteFileW((self + L".old").data());

	const std::wstring manifest_path = L"./story/_update/manifest.json";
	if(!Exists(manifest_path))
		return false;
	nlohmann::json j;
	try {
		const auto raw = ReadAll(manifest_path);
		j = nlohmann::json::parse(raw.begin(), raw.end());
	}
	catch(...) {
		Log("invalid manifest");
		DeleteFileW(manifest_path.data());
		return false;
	}
	const std::wstring tmp = L"./story/_update/new.exe";
	uint64_t expected = 0;
	std::vector<std::wstring> parts;
	try {
		expected = j.at("size").get<uint64_t>();
		for(const auto& p : j.at("parts"))
			parts.push_back(L"./story/_update/" + Widen(p.get<std::string>()));
	}
	catch(...) {
		Log("invalid manifest fields");
		return false;
	}
	for(const auto& p : parts)
		if(!Exists(p)) {
			Log("missing part, waiting for the rest");
			return false;   // not everything arrived yet: try again next launch
		}
	uint64_t written = 0;
	{
		FILE* out = _wfopen(tmp.data(), L"wb");
		if(!out) {
			Log("cannot write new.exe");
			return false;
		}
		for(const auto& p : parts) {
			const auto data = ReadAll(p);
			fwrite(data.data(), 1, data.size(), out);
			written += data.size();
		}
		fclose(out);
	}
	if(written != expected) {
		Log("size mismatch " + std::to_string(written) + " != " + std::to_string(expected));
		DeleteFileW(tmp.data());
		return false;
	}
	// copies that are not running (only when the story mod was installed over EDOPro.exe)
	std::vector<std::wstring> others;
	if(Exists(dir + L"EDOPro.original.exe")) {
		for(const auto* name : { L"EDOPro.exe", L"EDOPro-Story.exe" }) {
			const std::wstring path = dir + name;
			if(Exists(path) && lstrcmpiW(path.data(), self.data()) != 0)
				others.push_back(path);
		}
	}
	for(const auto& o : others)
		CopyFileW(tmp.data(), o.data(), FALSE);
	// swap the running executable: Windows allows renaming a running exe
	const std::wstring old = self + L".old";
	if(!MoveFileExW(self.data(), old.data(), MOVEFILE_REPLACE_EXISTING)) {
		Log("cannot rename the running exe");
		return false;
	}
	if(!CopyFileW(tmp.data(), self.data(), FALSE)) {
		MoveFileExW(old.data(), self.data(), MOVEFILE_REPLACE_EXISTING);
		Log("cannot place the new exe");
		return false;
	}
	for(const auto& p : parts)
		DeleteFileW(p.data());
	DeleteFileW(tmp.data());
	DeleteFileW(manifest_path.data());
	Log("updated");
	// restart
	STARTUPINFOW si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	std::wstring cmd = GetCommandLineW();
	if(CreateProcessW(self.data(), &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
		return true;
	}
	return false;
}

}

#else

namespace ygo {
bool ApplyStoryUpdate() {
	return false;
}
}

#endif
