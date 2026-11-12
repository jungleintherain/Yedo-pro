#ifndef DUEL_WORLD_H
#define DUEL_WORLD_H

// "Duel World": Master Duel / Duel Links style meta-game for the story mod.
//  - starter deck selection
//  - wallet (gems, gold), items, card collection (enforced in the deck editor and when getting ready)
//  - card rarity (N / R / SR / UR), automatic + overrides
//  - shop with booster packs + pack opening reveal
//  - card trader with a daily rotating stock
//  - random roaming duelists with rewards and card drops
// Everything is data driven: story/world/*.json ; progress in story/world/save.json

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include "epro_mutex.h"
#include <IEventReceiver.h>
#include <SColor.h>
#include <rect.h>
#include <vector2d.h>

namespace irr {
namespace gui {
class IGUIElement;
class IGUIWindow;
class IGUIListBox;
class IGUIEditBox;
class IGUIStaticText;
class IGUIButton;
class IGUIComboBox;
class IGUIImage;
class IGUITabControl;
}
}

namespace ygo {

struct CardDataC;
struct Deck;
struct StoryChapter;

enum WORLD_GUI {
	BUTTON_WORLD_BACK = 30100,
	BUTTON_WORLD_STORY,
	BUTTON_WORLD_DECKS,
	TAB_WORLD,
	LISTBOX_WORLD_DUELISTS,
	BUTTON_WORLD_DUEL,
	BUTTON_WORLD_REROLL,
	LISTBOX_WORLD_PACKS,
	BUTTON_WORLD_BUY1,
	BUTTON_WORLD_BUY10,
	LISTBOX_WORLD_TRADER,
	BUTTON_WORLD_TRADER_BUY,
	BUTTON_WORLD_TRADER_REFRESH,
	LISTBOX_WORLD_ITEMS,
	BUTTON_WORLD_ITEM_USE,
	BUTTON_WORLD_STARTER_0,
	BUTTON_WORLD_STARTER_1,
	BUTTON_WORLD_STARTER_2,
	BUTTON_WORLD_STARTER_3,
	BUTTON_WORLD_STARTER_BACK,
	LISTBOX_WORLD_SKILLS = 30140,
	BUTTON_WORLD_SKILL_EQUIP,
	BUTTON_WORLD_SKILL_UNEQUIP,
	BUTTON_WORLD_CAMPAIGN,
	BUTTON_WORLD_SKILL_INFO_OK,
	LISTBOX_WORLD_STRUCTS,
	BUTTON_WORLD_STRUCT_BUY,
	LISTBOX_WORLD_MISSIONS,
	BUTTON_WORLD_MISSION_CLAIM,
	BUTTON_WORLD_MISSION_CLAIM_ALL,
	BUTTON_WORLD_FREE_PACK,
	LISTBOX_WORLD_GATE,
	COMBOBOX_WORLD_GATE_LV,
	BUTTON_WORLD_GATE_DUEL,
	BUTTON_WORLD_RANK_DUEL,
	BUTTON_WORLD_ANIM,
	COMBOBOX_WORLD_SKILL_GATE,
	COMBOBOX_WORLD_SKILL_RANK,
	BUTTON_WORLD_SKILL_INFO_GATE,
	BUTTON_WORLD_SKILL_INFO_RANK,
	COMBOBOX_WORLD_SKILL_ROAM,
	BUTTON_WORLD_SKILL_INFO_ROAM,
	EDITBOX_WORLD_SEARCH,
	BUTTON_WORLD_SEARCH,
	EDITBOX_WORLD_SEARCH_POP,
	BUTTON_WORLD_SEARCH_POP,
	BUTTON_WORLD_SEARCH_CLOSE,
	LISTBOX_WORLD_SEARCH,
	BUTTON_WORLD_NEW_DECK,
	BUTTON_DECK_NEW,          // deck editor: create an empty deck
	BUTTON_WORLD_LOGIN,
	BUTTON_WORLD_LOGIN_CLAIM,
	BUTTON_WORLD_LOGIN_CLOSE,
	BUTTON_WORLD_CD_CLOSE,
	BUTTON_WORLD_PACK_LIST,
	BUTTON_WORLD_STRUCT_LIST,
};

enum class Rarity : uint8_t { N = 0, R = 1, SR = 2, UR = 3 };

class DuelWorld {
public:
	void Init();                             // after the gui exists
	void CreateGUI();
	bool OnEvent(const irr::SEvent& event);  // MenuHandler (before everything else)
	void Draw2D();                           // overlay (pack opening / rewards)

	// main menu entry
	void Open();
	void ShowHubOrMenu();
	bool Started() const { return save.started; }

	// collection
	// collection rules apply only inside Duel World / story (deck editor opened from the hub, story & world duels).
	// Sandbox, LAN and the main menu deck editor are free.
	bool CollectionLocked() const { return save.started && collection_lock; }
	bool CollectionActive() const { return CollectionLocked() && return_to_hub; }
	uint32_t Key(uint32_t code) const;
	int Owned(uint32_t code) const;
	int OwnedFor(const CardDataC* data) const;
	// true if the deck can be used; otherwise shows a popup with what is missing
	bool CheckDeck(const Deck& deck, bool popup);
	bool CheckDeckByName(const std::wstring& deck_name);
	// Duel World deck category: starter + bought Structure Decks + decks saved in the hub deck editor.
	// Campaign / Sandbox / main menu deck editor keep listing every deck.
	bool IsWorldDeck(const std::wstring& name) const { return save.world_decks.count(name) != 0; }
	void AddWorldDeck(const std::wstring& name);
	void RemoveWorldDeck(const std::wstring& name);
	void RenameWorldDeck(const std::wstring& from, const std::wstring& to);
	void FillWorldDecks(irr::gui::IGUIComboBox* cb);
	// owned skills (selection = equipped skill) - shared by every Duel World / story skill box
	void FillSkillCombo(irr::gui::IGUIComboBox* cb);
	void EquipFromCombo(irr::gui::IGUIComboBox* cb);
	void ShowComboSkillInfo(irr::gui::IGUIComboBox* cb);
	Rarity RarityOf(uint32_t code) const;
	static irr::video::SColor RarityColor(Rarity r);
	static const wchar_t* RarityName(Rarity r);

	// story hook: called when a story duel ends (result: 0 win, 1 lose, other draw)
	void OnStoryResult(const StoryChapter& ch, size_t chapter_index, int result, bool first_clear);

	// presentation: opponent of the selected roaming duelist + the box showing its info
	std::wstring SelectedOpponent() const;
	irr::gui::IGUIElement* DuelistInfoBox() const;

	// Duel Links style skills (story/world/skills.json)
	struct SkillDef {
		uint32_t code = 0;
		std::wstring name, name_en, desc, for_deck;
		std::string tier;      // "", "legendary", "broken"
		int price = 0;
		bool owned_default = false;
	};
	const std::vector<SkillDef>& Skills();
	const SkillDef* FindSkill(uint32_t code);
	uint32_t EquippedSkill();                 // 0 = none (only when Duel World started)
	bool SkillOwned(uint32_t code) const;
	void EquipSkill(uint32_t code);           // 0 = unequip; ignored if not owned
	static std::wstring SkillInfoText(const SkillDef& s);
	void ShowSkillInfo(uint32_t code);        // popup with the skill description

	// missions / duel statistics (called by StoryMode for every story, world and campaign duel)
	void DuelBegin(const StoryChapter& ch);
	void DuelSummon(uint32_t code, bool special);   // duel thread
	void DuelDamage(uint32_t amount);               // duel thread
	void DuelSkillUsed();                           // duel thread
	void OnDuelEnd(const StoryChapter& ch, int result);
	void AddStat(const std::string& stat, int64_t n = 1);

	// rewards from other modes (campaign). card_tier: 0 none, 2 SR or better, 3 UR
	void GrantReward(const std::wstring& title, int64_t gold, int64_t gems, int card_tier);
	void Toast(const std::wstring& text);

	bool return_to_hub = false;
	irr::gui::IGUIWindow* wHub = nullptr;
	irr::gui::IGUIWindow* wSkillInfo = nullptr;   // skill description popup (modal)
	irr::gui::IGUIStaticText* stSkillInfo = nullptr;
	// shop card search popup (modal, drawn on top by MdUi)
	irr::gui::IGUIWindow* wCardSearch = nullptr;
	std::vector<irr::gui::IGUIButton*> newDeckButtons;   // "+ new deck" next to every deck box
	// card detail popup (picture + effect + where to get it) - modal, drawn on top by MdUi
	irr::gui::IGUIWindow* wCardDetail = nullptr;
	void ShowCardDetail(uint32_t code);
	void DrawCardDetailArt();
	std::wstring search_title = L"ค้นหาการ์ด  (Card Search)";
	// daily login reward popup (modal, drawn on top by MdUi)
	irr::gui::IGUIWindow* wLogin = nullptr;
	void DrawLoginOverlay();
	void DrawCardSearchArt();
	irr::gui::IGUIWindow* wStarter = nullptr;

private:
	struct ItemDef {
		std::string id;
		std::wstring name, desc;
		std::string type;   // gems, gold, pack_ticket, trader_refresh, duelist_call, card
		int value = 0;
		std::string pack;
	};
	struct PackDef {
		std::string id;
		std::wstring name, desc, arc;
		int price = 100;
		uint32_t cover = 0;
		std::string cat;      // main / theme / meta
		std::wstring tag;     // NEW / HOT ...
		std::vector<uint32_t> cards;
		std::vector<uint32_t> by_rarity[4];
		std::vector<uint32_t> featured;
	};
	struct StructDef {
		std::string id, cat, file;
		std::wstring name, desc;
		int price = 500;
		uint32_t cover = 0;
		std::vector<uint32_t> cards;      // main + extra + side
		std::vector<uint32_t> featured;
		int main_count = 0, extra_count = 0;
	};
	struct MissionDef {
		std::string id, stat, item;
		bool daily = false;
		std::wstring name, desc;
		int64_t target = 1;
		int gems = 0, gold = 0, item_n = 0;
		uint32_t skill = 0, card = 0;
	};
	struct StarterDef {
		std::wstring name, desc, deck_name;
		std::string file;
		uint32_t cover = 0;
	};
	struct Duelist {
		int chapter = -1;
		int tier = 0;
	};
	struct Save {
		bool started = false;
		int64_t gems = 0, gold = 0;
		std::map<std::string, int> items;
		std::map<uint32_t, int> cards;
		uint32_t trader_day = 0;
		int trader_seed = 0;
		std::vector<uint32_t> trader_stock;
		std::set<size_t> trader_bought;
		std::vector<Duelist> duelists;
		std::set<uint32_t> skills;
		uint32_t equipped_skill = 0;
		int wins = 0, losses = 0, packs_opened = 0;
		uint32_t free_pack_day = 0;
		uint32_t login_last = 0;   // day (YYYYMMDD) the daily login reward was last claimed
		int login_count = 0;       // total login rewards claimed (7-day cycle = login_count % 7)
		std::map<std::string, int> structs_bought;
		std::map<std::string, int64_t> stats, daily_stats;
		uint32_t daily_day = 0;
		std::set<std::string> claimed, daily_claimed;
		// Duel Links style
		int64_t xp = 0;
		int rank_stage = 0, rank_stars = 0, rank_best = 0, rank_wins = 0, rank_losses = 0;
		uint32_t rank_season = 0;
		std::map<std::string, int> gate_clears;   // "<chapter id>:<level>" -> wins
		bool anim_fast = false;
		std::set<std::wstring> world_decks;
		bool world_decks_known = false;   // false = old save, build the list from owned cards once
	};
	struct RevealCard {
		uint32_t code;
		Rarity rarity;
		bool is_new;
	};
	struct Reveal {
		std::wstring title, subtitle;
		std::vector<RevealCard> cards;
		float t = 0;
	};

	// data
	void LoadData();
	void LoadSave();
	void WriteSave() const;
	void ComputeRarities();
	std::vector<uint32_t> ReadYdk(const std::string& path) const;
	void WriteStructDeck(const StructDef& d);
	bool RegrantStructCards();

	// economy
	uint32_t Today() const;
	void RefreshTraderIfNeeded(bool force);
	void RerollDuelists(bool all, int slot = -1);
	std::vector<RevealCard> OpenPack(const PackDef& pack);
	void AddCard(uint32_t code, int count = 1);
	bool UseItem(const std::string& id);
	uint32_t RandomCardFromDeck(const std::string& deck_file, bool sr_plus);
	int Rand(int n);
	int RarityPrice(Rarity r) const { return trader_prices[static_cast<int>(r)]; }

	// gui
	void RefreshHub();
	void RefreshHeader();
	void RefreshDuelists();
	void RefreshPacks();
	void RefreshTrader();
	void RefreshItems();
	void UpdateDuelistInfo();
	void UpdatePackInfo();
	void UpdateTraderInfo();
	void UpdateItemInfo();
	void ChooseStarter(size_t index);
	void BuyPacks(int count);
	void StartRoamingDuel();
	void ShowReveal(std::wstring title, std::wstring subtitle, std::vector<RevealCard> cards);
	void NextReveal();
	void LoadSkills();
	void RefreshSkills();
	void UpdateSkillInfo();
	// structure decks / missions
	void LoadExtraData();
	void RefreshStructs();
	void UpdateStructInfo();
	void BuyStruct();
	void BuyFreePack();
	void CheckDailyReset();
	int64_t MissionProgress(const MissionDef& m) const;
	bool MissionDone(const MissionDef& m) const;
	bool MissionClaimed(const MissionDef& m) const;
	int ClaimableMissions() const;
	std::vector<size_t> MissionOrder() const;
	void RefreshMissions();
	void UpdateMissionInfo();
	bool ClaimMission(size_t index, std::wstring& summary, std::vector<RevealCard>& cards);
	void ClaimSelected(bool all);
	std::wstring MissionReward(const MissionDef& m) const;
	std::wstring SkillUnlockText(uint32_t code) const;
	std::vector<uint32_t> Featured(const std::vector<uint32_t>& cards, size_t n) const;
	// Duel Links: player level, Gate duels, Ranked duels
	static int64_t XpForLevel(int level);
	int Level() const;
	void AddXp(int64_t xp, std::wstring& msg);
	void ApplyAnimSpeed();
	void RefreshGate();
	void UpdateGateInfo();
	void StartGateDuel();
	void RefreshRank();
	void StartRankDuel();
	void OnRankResult(int result);
	void CheckSeason(bool toast);
	static std::wstring RankName(int stage);
	static int RankTier(int stage);
	static int StarsNeeded(int stage);
	irr::gui::IGUIComboBox* DeckComboFor(int which);
	std::wstring PickedDeck(irr::gui::IGUIComboBox* cb);
	std::vector<int> gate_rows;   // chapter index per gate row
	std::wstring pending_levelup;
	std::vector<StructDef> structs;
	std::vector<MissionDef> missions;
	std::set<uint32_t> tracked_codes;     // codes with a "sp_<code>" mission
	std::map<std::string, int64_t> duel_stats;
	epro::mutex duel_mutex;
	bool duel_tracking = false;
	std::vector<SkillDef> skills;
	bool skills_loaded = false;

	// config
	bool collection_lock = true;
	int start_gems = 1000, start_gold = 3000;
	std::map<std::string, int> start_items;
	int rates[4] = { 60, 25, 11, 4 };       // per slot, in %
	int last_slot_ur = 20;                  // % of UR on the guaranteed SR+ slot
	int pack_size = 8;
	int trader_prices[4] = { 200, 500, 1500, 4000 };
	int trader_size = 10;
	// trader sells only cards that no pack / Structure Deck has (built once per session)
	std::vector<uint32_t> trader_pool[4];
	std::set<uint32_t> trader_pool_keys;
	bool trader_pool_built = false;
	void BuildTraderPool();
	bool TraderCard(uint32_t code) { BuildTraderPool(); return trader_pool_keys.count(Key(code)) != 0; }
	int roaming_slots = 3;
	int story_first_gems = 150, story_first_gold = 800, story_repeat_gems = 10, story_repeat_gold = 200;
	int roam_gold_base = 150, roam_gold_tier = 100, roam_gems_base = 10, roam_gems_tier = 5;
	int roam_card_drop = 60, roam_item_drop = 15, lose_gold = 50;
	std::vector<std::string> roam_item_pool;

	std::vector<ItemDef> items;
	struct LoginReward {
		int gems = 0, gold = 0;
		std::string item;
		int item_n = 0;
	};
	std::vector<LoginReward> login_rewards;
	std::vector<irr::gui::IGUIStaticText*> stLoginDays;
	irr::gui::IGUIStaticText* stLoginInfo = nullptr;
	irr::gui::IGUIButton* btnLoginClaim = nullptr;
	bool LoginClaimable() const;
	int LoginTodayIndex() const;
	std::wstring LoginRewardText(const LoginReward& r) const;
	std::wstring ItemLabel(const std::string& id) const;
	void ShowLogin();
	void RefreshLogin();
	void ClaimLogin();
	std::vector<PackDef> packs;
	std::vector<StarterDef> starters;
	std::map<uint32_t, Rarity> rarity_override;
	mutable std::map<uint32_t, Rarity> rarity_cache;
	Save save;
	uint32_t rng = 0;
	int roaming_slot_playing = -1;

	std::vector<Reveal> reveals;    // queue
	bool reveal_active = false;

	irr::gui::IGUIStaticText* stHeader = nullptr;
	irr::gui::IGUITabControl* tabs = nullptr;
	irr::gui::IGUIListBox* lstDuelists = nullptr;
	irr::gui::IGUIStaticText* stDuelist = nullptr;
	irr::gui::IGUIComboBox* cbDeck = nullptr;
	irr::gui::IGUIButton* btnDuel = nullptr;
	irr::gui::IGUIButton* btnReroll = nullptr;
	irr::gui::IGUIListBox* lstPacks = nullptr;
	irr::gui::IGUIStaticText* stPack = nullptr;
	irr::gui::IGUIElement* imgPack = nullptr;
	irr::gui::IGUIButton* btnBuy1 = nullptr;
	irr::gui::IGUIButton* btnBuy10 = nullptr;
	irr::gui::IGUIButton* btnFreePack = nullptr;
	std::vector<irr::gui::IGUIElement*> featPack, featStruct;
	std::vector<uint32_t> featPackCodes, featStructCodes;
	irr::gui::IGUIListBox* lstStructs = nullptr;
	irr::gui::IGUIStaticText* stStruct = nullptr;
	irr::gui::IGUIElement* imgStruct = nullptr;
	irr::gui::IGUIButton* btnStructBuy = nullptr;
	irr::gui::IGUIListBox* lstMissions = nullptr;
	irr::gui::IGUIStaticText* stMission = nullptr;
	irr::gui::IGUIButton* btnClaim = nullptr;
	irr::gui::IGUIButton* btnClaimAll = nullptr;
	irr::gui::IGUIElement* tabMissions = nullptr;
	std::vector<size_t> mission_rows;
	irr::gui::IGUIElement* tabDuelists = nullptr;
	irr::gui::IGUIElement* tabGate = nullptr;
	irr::gui::IGUIElement* tabRank = nullptr;
	irr::gui::IGUIListBox* lstGate = nullptr;
	irr::gui::IGUIComboBox* cbGateLv = nullptr;
	irr::gui::IGUIComboBox* cbGateDeck = nullptr;
	irr::gui::IGUIStaticText* stGate = nullptr;
	irr::gui::IGUIButton* btnGateDuel = nullptr;
	irr::gui::IGUIStaticText* stRank = nullptr;
	irr::gui::IGUIStaticText* stRankBig = nullptr;
	irr::gui::IGUIElement* imgRank = nullptr;
	irr::gui::IGUIComboBox* cbRankDeck = nullptr;
	irr::gui::IGUIButton* btnRankDuel = nullptr;
	irr::gui::IGUIButton* btnAnim = nullptr;
	irr::gui::IGUIComboBox* cbGateSkill = nullptr;
	irr::gui::IGUIComboBox* cbRankSkill = nullptr;
	irr::gui::IGUIComboBox* cbRoamSkill = nullptr;
	void OpenDeckEditor(bool new_deck);
	// card search
	irr::gui::IGUIEditBox* ebSearch = nullptr;
	irr::gui::IGUIEditBox* ebSearchPop = nullptr;
	irr::gui::IGUIListBox* lstSearch = nullptr;
	irr::gui::IGUIStaticText* stSearchInfo = nullptr;
	irr::gui::IGUIStaticText* stSearchSrc = nullptr;
	irr::gui::IGUIElement* imgSearch = nullptr;
	std::vector<uint32_t> search_results;
	std::map<uint32_t, std::vector<std::wstring>> card_sources;   // Key(code) -> where to get it
	bool card_sources_built = false;
	void BuildCardSources();
	void OpenCardSearch(const std::wstring& query);
	void RunCardSearch(const std::wstring& query);
	void UpdateCardSearchInfo();
	std::wstring CardSourcesText(uint32_t code);
	void OpenCardList(const std::wstring& title, const std::vector<uint32_t>& codes);
	irr::gui::IGUIElement* imgCD = nullptr;
	irr::gui::IGUIStaticText* stCDName = nullptr;
	irr::gui::IGUIStaticText* stCDType = nullptr;
	irr::gui::IGUIStaticText* stCDInfo = nullptr;
	irr::gui::IGUIStaticText* stCDText = nullptr;
	irr::gui::IGUIStaticText* stCDSrc = nullptr;
	uint32_t cd_code = 0;
	bool cd_restore_search = false;   // two visible modal screens fight over focus: hide the list while the detail is open
	void CloseCardDetail();
	std::vector<std::pair<irr::core::recti, uint32_t>> reveal_hits;   // card rects of the pack-opening screen
	bool ClickCardAt(const irr::core::vector2di& pos);
	void RefreshSkillCombos();
	irr::gui::IGUIListBox* lstTrader = nullptr;
	irr::gui::IGUIStaticText* stTrader = nullptr;
	irr::gui::IGUIElement* imgTrader = nullptr;
	irr::gui::IGUIButton* btnTraderBuy = nullptr;
	irr::gui::IGUIButton* btnTraderRefresh = nullptr;
	irr::gui::IGUIListBox* lstItems = nullptr;
	irr::gui::IGUIStaticText* stItem = nullptr;
	irr::gui::IGUIButton* btnItemUse = nullptr;
	irr::gui::IGUIListBox* lstSkills = nullptr;
	irr::gui::IGUIStaticText* stSkill = nullptr;
	irr::gui::IGUIButton* btnSkillEquip = nullptr;
	irr::gui::IGUIButton* btnSkillUnequip = nullptr;
	std::vector<irr::gui::IGUIButton*> btnStarters;
	std::vector<irr::gui::IGUIElement*> imgStarters;
	irr::gui::IGUIStaticText* stStarter = nullptr;
};

extern DuelWorld gWorld;

}

#endif
