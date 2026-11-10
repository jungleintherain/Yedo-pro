#ifndef DUEL_FX_H
#define DUEL_FX_H

// Master Duel-like visual effects for EDOPro (story mod)
// - big summon cut-in with rays / rings / particles / shake
// - 3D "hologram" of the monster art standing on the field
// - attack lunge + impact
// - screen flash + shake on damage, slower LP roll
// - turn / phase banners
// - destroyed cards shatter into pieces
//
// All state is touched only while holding mainGame->gMutex:
// the duel thread (DuelClient) triggers effects, the main loop draws them.

#include <cstdint>
#include <vector>
#include <string>
#include <vector3d.h>
#include <vector2d.h>
#include <SColor.h>
#include <rect.h>

namespace irr {
namespace video {
class ITexture;
class IVideoDriver;
}
}

namespace ygo {

class ClientCard;

class DuelFx {
public:
	// config (story/fx.json)
	bool enabled = true;
	bool holograms = true;
	bool shake = true;
	float speed = 1.0f;

	void Init();                    // after the driver exists
	bool Active() const { return enabled; }

	// hooks from the renderer (main thread)
	void BeginDuelFrame();          // before the 3D field is drawn (applies camera shake)
	void Draw3D();                  // after cards, before smgr->drawAll
	void Draw2D();                  // overlay, after the gui
	void Reset();                   // when a duel starts / ends

	// triggers from DuelClient (duel thread, gMutex held)
	// returns how many frames (60fps) the duel thread should wait
	enum SummonKind { NORMAL_SUMMON, SPECIAL_SUMMON, FLIP_SUMMON };
	int Summon(uint32_t code, ClientCard* pcard, SummonKind kind);
	void NoteMove(uint8_t controler, uint32_t location, uint32_t sequence, uint32_t from_location);
	int Attack(ClientCard* attacker, ClientCard* target, const irr::core::vector3df& direct_pos);
	void Damage(int player, uint32_t amount);
	void Recover(int player);
	void Destroy(ClientCard* pcard);
	void Phase(uint16_t phase, bool my_turn);
	int Turn(bool my_turn);
	int LpFrames() const { return enabled ? 30 : 10; }

	// shared with the Duel World overlay
	void DrawQuad(irr::video::ITexture* tex, const irr::core::rectf& uv, const irr::core::vector2df& center,
				  const irr::core::vector2df& size, float rotation_deg, irr::video::SColor color, bool additive);
	irr::video::ITexture* GlowTexture() const { return tGlow; }
	irr::video::ITexture* RaysTexture() const { return tRays; }
	irr::video::ITexture* RingTexture() const { return tRing; }
	irr::video::ITexture* SparkTexture() const { return tSpark; }
	float Time() const { return time; }
	void TickMenu();   // advance the clock outside duels

	// drag to attack arrow (screen space)
	void SetDragArrow(bool active, const irr::core::vector2df& from = {}, const irr::core::vector2df& to = {}, bool on_target = false);

private:
	struct Particle {
		irr::core::vector2df pos, vel;
		float life, max_life, size;
		irr::video::SColor color;
		bool additive;
		bool smoke = false;   // soft puff that grows (drawn with the glow texture)
		float grow = 0;
	};
	// per attacker attack style (spellcaster smoke, dragon breath, god attacks...)
	struct AttackFx {
		int style = 0;
		irr::core::vector3df from, to;
		irr::video::SColor color;
		float t = 0, duration = 1;
		bool direct = false;
		bool impact_done = false;
		int bolts_hit = 0;
	};
	struct Shard {
		uint32_t code;          // 0 = card back of `controler`
		uint8_t controler;
		irr::core::recti src;
		irr::core::vector2df pos, vel, size;
		float rot, vrot, life, max_life;
	};
	struct Hologram {
		uint32_t code;
		irr::core::vector3df base;
		irr::core::vector3df lunge_to;
		irr::video::SColor color;
		float t, duration;
		bool attack;
		bool custom_impact = false;
	};
	struct SummonCutIn {
		bool active = false;
		uint32_t code = 0;
		bool big = false;
		std::wstring label;
		irr::video::SColor color;
		irr::core::vector2df field_pos;
		irr::core::vector3df field_pos3d;
		bool has_field = false;
		bool landed = false;
		float t = 0, duration = 1;
	};
	struct Banner {
		bool active = false;
		std::wstring text;
		irr::video::SColor color;
		float t = 0, duration = 1;
		bool big = false;
	};

	void LoadConfig();
	void MakeTextures();
	float Dt() const;
	irr::core::vector2df Project(const irr::core::vector3df& pos) const;
	irr::video::SColor CardColor(uint32_t code) const;
	void Burst(const irr::core::vector2df& at, irr::video::SColor color, int count, float speed, float size);
	void Flash(irr::video::SColor color, float strength, float duration);
	void Shake(float amount, float duration);
	void AddHologram(uint32_t code, const irr::core::vector3df& base, irr::video::SColor color, float duration);

	int AttackStyleOf(uint32_t code, irr::video::SColor& color) const;
	void Smoke(const irr::core::vector2df& at, irr::video::SColor color, int count, float size, float spread, float life);
	void UpdateAttacks(float dt);
	void DrawAttacks();
	void DrawSummon();
	void DrawDragArrow();
	void DrawBanner(Banner& b);

	irr::video::ITexture* tGlow = nullptr;
	irr::video::ITexture* tRing = nullptr;
	irr::video::ITexture* tRays = nullptr;
	irr::video::ITexture* tSpark = nullptr;
	irr::video::ITexture* tBeam = nullptr;
	irr::video::ITexture* tLine = nullptr;
	std::vector<AttackFx> attacks;

	std::vector<Particle> particles;
	std::vector<Shard> shards;
	std::vector<Hologram> holos;
	SummonCutIn summon;
	Banner phase_banner, turn_banner;

	float flash_alpha = 0, flash_decay = 0;
	irr::video::SColor flash_color;
	float shake_amount = 0, shake_time = 0, shake_duration = 0;
	float time = 0;
	bool drag_active = false, drag_on_target = false;
	irr::core::vector2df drag_from, drag_to;
	float frame_dt = 0;
	uint32_t rng = 0x1234567;
	uint32_t last_from[2][8] = {};
	float Rand();
};

extern DuelFx gFx;

}

#endif
