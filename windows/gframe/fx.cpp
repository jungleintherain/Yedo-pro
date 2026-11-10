#include "fx.h"
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <IrrlichtDevice.h>
#include <IVideoDriver.h>
#include <ISceneManager.h>
#include <ISceneCollisionManager.h>
#include <ICameraSceneNode.h>
#include <IImage.h>
#include <ITexture.h>
#include <SMaterial.h>
#include <S3DVertex.h>
#include "config.h"
#include "game.h"
#include "game_config.h"
#include "client_card.h"
#include "data_manager.h"
#include "image_manager.h"
#include "file_stream.h"
#include "CGUITTFont/CGUITTFont.h"
#include "ocgapi_constants.h"

namespace ygo {

DuelFx gFx;

namespace {

using irr::core::vector2df;
using irr::core::vector3df;
using irr::video::SColor;

constexpr float PI = 3.14159265f;

float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float EaseOut(float t) { t = Clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
float EaseIn(float t) { t = Clamp01(t); return t * t * t; }
float EaseOutBack(float t) {
	t = Clamp01(t);
	const float c1 = 1.70158f, c3 = c1 + 1;
	return 1 + c3 * std::pow(t - 1, 3) + c1 * std::pow(t - 1, 2);
}
SColor WithAlpha(SColor c, float a) {
	c.setAlpha(static_cast<irr::u32>(std::lround(Clamp01(a) * 255)));
	return c;
}
SColor Lerp(SColor a, SColor b, float t) {
	return b.getInterpolated(a, Clamp01(t));
}

irr::video::SMaterial MakeMaterial(irr::video::ITexture* tex, bool additive) {
	irr::video::SMaterial m;
	m.Lighting = false;
	m.BackfaceCulling = false;
	m.FrontfaceCulling = false;
#if IRRLICHT_VERSION_MAJOR==1 && IRRLICHT_VERSION_MINOR==9
	m.ZWriteEnable = irr::video::E_ZWRITE::EZW_OFF;
#else
	m.ZWriteEnable = false;
#endif
	m.ZBuffer = irr::video::ECFN_DISABLED;
	m.MaterialType = irr::video::EMT_ONETEXTURE_BLEND;
	m.MaterialTypeParam = irr::video::pack_textureBlendFunc(irr::video::EBF_SRC_ALPHA,
															additive ? irr::video::EBF_ONE : irr::video::EBF_ONE_MINUS_SRC_ALPHA,
															irr::video::EMFN_MODULATE_1X,
															irr::video::EAS_TEXTURE | irr::video::EAS_VERTEX_COLOR);
	m.setTexture(0, tex);
	m.TextureLayer[0].TextureWrapU = irr::video::ETC_CLAMP_TO_EDGE;
	m.TextureLayer[0].TextureWrapV = irr::video::ETC_CLAMP_TO_EDGE;
	return m;
}

// standard card frame: where the artwork is inside the full card picture
const irr::core::rectf ART_UV{ 0.12f, 0.185f, 0.88f, 0.69f };
const irr::core::rectf FULL_UV{ 0.f, 0.f, 1.f, 1.f };

}

/* ------------------------------------------------------------ setup */

float DuelFx::Rand() {
	rng ^= rng << 13;
	rng ^= rng >> 17;
	rng ^= rng << 5;
	return (rng & 0xffffff) / float(0xffffff);
}

void DuelFx::LoadConfig() {
	FileStream file{ EPRO_TEXT("./story/fx.json"), FileStream::in };
	if(!file.good())
		return;
	try {
		nlohmann::json j;
		file >> j;
		enabled = j.value("enabled", enabled);
		holograms = j.value("holograms", holograms);
		shake = j.value("shake", shake);
		speed = std::max(0.25f, std::min(4.0f, j.value("speed", speed)));
	}
	catch(...) {}
}

void DuelFx::Init() {
	LoadConfig();
	MakeTextures();
}

void DuelFx::MakeTextures() {
	auto* driver = mainGame->driver;
	auto make = [driver](const char* name, irr::u32 w, irr::u32 h, auto&& fn) -> irr::video::ITexture* {
		auto* img = driver->createImage(irr::video::ECF_A8R8G8B8, { w, h });
		if(!img)
			return nullptr;
		for(irr::u32 y = 0; y < h; y++) {
			for(irr::u32 x = 0; x < w; x++) {
				const float u = (x + 0.5f) / w * 2 - 1;
				const float v = (y + 0.5f) / h * 2 - 1;
				const float a = Clamp01(fn(u, v));
				img->setPixel(x, y, SColor(static_cast<irr::u32>(a * 255), 255, 255, 255));
			}
		}
		auto* tex = driver->addTexture(name, img);
		img->drop();
		return tex;
	};
	tGlow = make("fx_glow", 128, 128, [](float u, float v) {
		const float r = std::sqrt(u * u + v * v);
		return std::pow(Clamp01(1 - r), 2.2f);
	});
	tRing = make("fx_ring", 256, 256, [](float u, float v) {
		const float r = std::sqrt(u * u + v * v);
		const float d = (r - 0.86f) / 0.05f;
		return std::exp(-d * d) + 0.25f * std::exp(-((r - 0.80f) / 0.12f) * ((r - 0.80f) / 0.12f));
	});
	tRays = make("fx_rays", 256, 256, [](float u, float v) {
		const float r = std::sqrt(u * u + v * v);
		if(r > 1)
			return 0.f;
		const float ang = std::atan2(v, u);
		const float ray = std::pow(std::max(0.f, std::cos(ang * 9.f)), 18.f) + 0.6f * std::pow(std::max(0.f, std::cos(ang * 9.f + PI / 2)), 40.f);
		return ray * std::pow(1 - r, 1.3f) + 0.35f * std::pow(Clamp01(1 - r * 1.6f), 2.f);
	});
	tSpark = make("fx_spark", 32, 32, [](float u, float v) {
		const float r = std::sqrt(u * u + v * v);
		return std::pow(Clamp01(1 - r), 3.f) * 1.4f;
	});
	tLine = make("fx_line", 128, 64, [](float u, float v) {
		// horizontal energy beam: gaussian across, soft ends
		const float across = std::exp(-(v / 0.38f) * (v / 0.38f));
		const float ends = Clamp01((1 - std::fabs(u)) / 0.12f);
		return across * ends;
	});
	tBeam = make("fx_beam", 64, 128, [](float u, float v) {
		// bright at the bottom (v = 1), soft on the sides
		const float side = std::exp(-(u / 0.45f) * (u / 0.45f));
		return side * std::pow((v + 1) / 2, 1.6f);
	});
}

void DuelFx::Reset() {
	particles.clear();
	shards.clear();
	holos.clear();
	attacks.clear();
	summon.active = false;
	phase_banner.active = false;
	turn_banner.active = false;
	flash_alpha = 0;
	shake_amount = 0;
	shake_time = 0;
}

float DuelFx::Dt() const {
	return frame_dt;
}

vector2df DuelFx::Project(const vector3df& pos) const {
	auto* coll = mainGame->device->getSceneManager()->getSceneCollisionManager();
	const auto p = coll->getScreenCoordinatesFrom3DPosition(pos, mainGame->camera);
	return { static_cast<float>(p.X), static_cast<float>(p.Y) };
}

SColor DuelFx::CardColor(uint32_t code) const {
	const auto* data = gDataManager->GetCardData(code);
	if(!data)
		return SColor(255, 255, 230, 140);
	if(data->type & TYPE_LINK) return SColor(255, 60, 170, 255);
	if(data->type & TYPE_XYZ) return SColor(255, 255, 200, 70);
	if(data->type & TYPE_SYNCHRO) return SColor(255, 235, 240, 255);
	if(data->type & TYPE_FUSION) return SColor(255, 200, 110, 255);
	if(data->type & TYPE_RITUAL) return SColor(255, 90, 150, 255);
	switch(data->attribute) {
	case ATTRIBUTE_EARTH: return SColor(255, 220, 160, 90);
	case ATTRIBUTE_WATER: return SColor(255, 70, 160, 255);
	case ATTRIBUTE_FIRE: return SColor(255, 255, 90, 50);
	case ATTRIBUTE_WIND: return SColor(255, 90, 230, 140);
	case ATTRIBUTE_LIGHT: return SColor(255, 255, 225, 110);
	case ATTRIBUTE_DARK: return SColor(255, 180, 90, 255);
	case ATTRIBUTE_DIVINE: return SColor(255, 255, 245, 190);
	default: return SColor(255, 255, 230, 140);
	}
}

/* ------------------------------------------------------------ helpers */

void DuelFx::Burst(const vector2df& at, SColor color, int count, float spd, float size) {
	const float scale = mainGame->driver->getScreenSize().Height / 640.f;
	for(int i = 0; i < count; i++) {
		const float ang = Rand() * 2 * PI;
		const float v = spd * (0.35f + Rand() * 0.9f) * scale;
		Particle p;
		p.pos = at;
		p.vel = vector2df(std::cos(ang) * v, std::sin(ang) * v);
		p.max_life = p.life = 0.35f + Rand() * 0.5f;
		p.size = size * (0.5f + Rand()) * scale;
		p.color = Rand() < 0.3f ? SColor(255, 255, 255, 255) : color;
		p.additive = true;
		particles.push_back(p);
	}
}

void DuelFx::Flash(SColor color, float strength, float duration) {
	if(strength < flash_alpha)
		return;
	flash_color = color;
	flash_alpha = strength;
	flash_decay = strength / std::max(0.05f, duration);
}

void DuelFx::Shake(float amount, float duration) {
	if(!shake)
		return;
	if(amount < shake_amount * (1 - shake_time / std::max(0.01f, shake_duration)))
		return;
	shake_amount = amount;
	shake_duration = duration;
	shake_time = 0;
}

void DuelFx::AddHologram(uint32_t code, const vector3df& base, SColor color, float duration) {
	if(!holograms || !code)
		return;
	// replace an hologram already standing on the same spot
	holos.erase(std::remove_if(holos.begin(), holos.end(), [&](const Hologram& h) {
		return !h.attack && h.base.getDistanceFromSQ(base) < 0.05f;
	}), holos.end());
	Hologram h;
	h.code = code;
	h.base = base;
	h.lunge_to = base;
	h.color = color;
	h.t = 0;
	h.duration = duration;
	h.attack = false;
	holos.push_back(h);
}

void DuelFx::DrawQuad(irr::video::ITexture* tex, const irr::core::rectf& uv, const vector2df& center,
					  const vector2df& size, float rotation_deg, SColor color, bool additive) {
	if(!tex || color.getAlpha() == 0)
		return;
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float sw = static_cast<float>(ss.Width), sh = static_cast<float>(ss.Height);
	const float rad = rotation_deg * PI / 180.f;
	const float c = std::cos(rad), s = std::sin(rad);
	const vector2df local[4] = { { -0.5f, -0.5f }, { 0.5f, -0.5f }, { -0.5f, 0.5f }, { 0.5f, 0.5f } };
	const vector2df tc[4] = { { uv.UpperLeftCorner.X, uv.UpperLeftCorner.Y }, { uv.LowerRightCorner.X, uv.UpperLeftCorner.Y },
							  { uv.UpperLeftCorner.X, uv.LowerRightCorner.Y }, { uv.LowerRightCorner.X, uv.LowerRightCorner.Y } };
	irr::video::S3DVertex v[4];
	for(int i = 0; i < 4; i++) {
		const float lx = local[i].X * size.X, ly = local[i].Y * size.Y;
		const float px = center.X + lx * c - ly * s;
		const float py = center.Y + lx * s + ly * c;
		v[i].Pos = { (px / sw - 0.5f) * 2.f, (py / sh - 0.5f) * -2.f, 0.5f };
		v[i].Normal = { 0, 0, -1 };
		v[i].Color = color;
		v[i].TCoords = tc[i];
	}
	const irr::u16 idx[6] = { 0, 1, 2, 3, 2, 1 };
	const auto oldProj = driver->getTransform(irr::video::ETS_PROJECTION);
	const auto oldView = driver->getTransform(irr::video::ETS_VIEW);
	const auto oldWorld = driver->getTransform(irr::video::ETS_WORLD);
	driver->setTransform(irr::video::ETS_PROJECTION, irr::core::IdentityMatrix);
	driver->setTransform(irr::video::ETS_VIEW, irr::core::IdentityMatrix);
	driver->setTransform(irr::video::ETS_WORLD, irr::core::IdentityMatrix);
	driver->setMaterial(MakeMaterial(tex, additive));
	driver->drawIndexedTriangleList(v, 4, idx, 2);
	driver->setTransform(irr::video::ETS_PROJECTION, oldProj);
	driver->setTransform(irr::video::ETS_VIEW, oldView);
	driver->setTransform(irr::video::ETS_WORLD, oldWorld);
}

/* ------------------------------------------------------------ triggers */

void DuelFx::NoteMove(uint8_t controler, uint32_t location, uint32_t sequence, uint32_t from_location) {
	if(!(location & LOCATION_MZONE) || sequence >= 8)
		return;
	last_from[controler & 1][sequence] = from_location;
}

int DuelFx::Summon(uint32_t code, ClientCard* pcard, SummonKind kind) {
	if(!enabled || !code)
		return 0;
	const bool special = kind == SPECIAL_SUMMON;
	const bool from_extra = special && pcard && (pcard->location & LOCATION_MZONE) && pcard->sequence < 8 &&
		(last_from[pcard->controler & 1][pcard->sequence] & LOCATION_EXTRA);
	const auto* data = gDataManager->GetCardData(code);
	const uint32_t type = data ? data->type : 0;
	const uint32_t level = data ? data->level : 0;
	const bool extra_type = (type & (TYPE_FUSION | TYPE_SYNCHRO | TYPE_XYZ | TYPE_LINK)) != 0;
	const bool ritual = (type & TYPE_RITUAL) != 0;
	summon = SummonCutIn{};
	summon.active = true;
	summon.code = code;
	summon.big = (extra_type && from_extra) || (ritual && special) || (!(type & TYPE_LINK) && level >= 7);
	summon.color = CardColor(code);
	if(summon.big) {
		if(kind == FLIP_SUMMON)
			summon.label = L"FLIP SUMMON";
		else if(!special)
			summon.label = L"TRIBUTE SUMMON";
		else if(from_extra && (type & TYPE_LINK))
			summon.label = L"LINK SUMMON";
		else if(from_extra && (type & TYPE_XYZ))
			summon.label = L"XYZ SUMMON";
		else if(from_extra && (type & TYPE_SYNCHRO))
			summon.label = L"SYNCHRO SUMMON";
		else if(from_extra && (type & TYPE_FUSION))
			summon.label = L"FUSION SUMMON";
		else if(ritual)
			summon.label = L"RITUAL SUMMON";
		else
			summon.label = L"SPECIAL SUMMON";
	}
	if(pcard && (pcard->location & LOCATION_MZONE)) {
		summon.has_field = true;
		summon.field_pos3d = pcard->curPos;
	}
	float duration = summon.big ? 1.25f : 0.6f;
	if(gGameConfig->quick_animation)
		duration *= 0.6f;
	duration /= speed;
	summon.duration = duration;
	summon.t = 0;
	return static_cast<int>(std::ceil(duration * 60.f)) + 2;
}

enum ATTACK_STYLE { ATK_DEFAULT, ATK_MAGIC, ATK_SHADOW, ATK_BEAM, ATK_SLASH, ATK_BOLTS, ATK_SLIFER, ATK_OBELISK, ATK_RA };

int DuelFx::AttackStyleOf(uint32_t code, SColor& color) const {
	const auto* d = gDataManager->GetCardData(code);
	color = CardColor(code);
	if(!d)
		return ATK_DEFAULT;
	const uint32_t base = (d->alias && d->IsInArtworkOffsetRange()) ? d->alias : code;
	switch(base) {
	case 10000020: color = SColor(255, 255, 40, 30); return ATK_SLIFER;      // Slifer the Sky Dragon
	case 10000000: color = SColor(255, 255, 200, 80); return ATK_OBELISK;    // Obelisk the Tormentor
	case 10000010: color = SColor(255, 255, 190, 60); return ATK_RA;         // The Winged Dragon of Ra
	case 89631139: color = SColor(255, 170, 220, 255); return ATK_BEAM;      // Blue-Eyes: Burst Stream
	case 74677422: color = SColor(255, 255, 110, 30); return ATK_BEAM;       // Red-Eyes: Inferno Fire Blast
	case 46986414: case 38033121: color = SColor(255, 170, 70, 255); return ATK_MAGIC;
	default: break;
	}
	switch(d->attribute) {
	case 0x01: color = SColor(255, 210, 160, 90); break;   // EARTH
	case 0x02: color = SColor(255, 80, 180, 255); break;   // WATER
	case 0x04: color = SColor(255, 255, 120, 40); break;   // FIRE
	case 0x08: color = SColor(255, 110, 255, 150); break;  // WIND
	case 0x10: color = SColor(255, 255, 245, 190); break;  // LIGHT
	case 0x20: color = SColor(255, 170, 70, 255); break;   // DARK
	case 0x40: color = SColor(255, 255, 210, 90); break;   // DIVINE
	default: break;
	}
	const uint32_t r = d->race;
	if(r & 0x2) return ATK_MAGIC;                                          // Spellcaster
	if(r & (0x8 | 0x10 | 0x2000000)) return ATK_SHADOW;                    // Fiend, Zombie, Illusion
	if(r & (0x2000 | 0x800000 | 0x40000 | 0x200000 | 0x400000 | 0x4)) return ATK_BEAM;   // Dragon, Wyrm, Sea Serpent, Divine, Fairy
	if(r & (0x20 | 0x1000000 | 0x1000 | 0x80 | 0x100000 | 0x40 | 0x20000)) return ATK_BOLTS; // Machine, Cyberse, Thunder, Pyro, Psychic, Aqua, Fish
	if(r & (0x1 | 0x8000 | 0x4000 | 0x10000 | 0x200 | 0x800 | 0x80000 | 0x400 | 0x100)) return ATK_SLASH;
	return ATK_DEFAULT;
}

void DuelFx::Smoke(const vector2df& at, SColor color, int count, float size, float spread, float life) {
	const float scale = mainGame->driver->getScreenSize().Height / 640.f;
	for(int i = 0; i < count; i++) {
		const float ang = Rand() * 2 * PI;
		const float v = spread * (0.2f + Rand() * 0.8f) * scale;
		Particle p;
		p.pos = at + vector2df((Rand() - 0.5f) * size * scale, (Rand() - 0.5f) * size * scale);
		p.vel = vector2df(std::cos(ang) * v, std::sin(ang) * v - 25.f * scale);
		p.max_life = p.life = life * (0.7f + Rand() * 0.6f);
		p.size = size * (0.7f + Rand() * 0.6f) * scale;
		p.grow = size * 1.6f * scale;
		p.color = color;
		p.additive = false;
		p.smoke = true;
		particles.push_back(p);
	}
}

int DuelFx::Attack(ClientCard* attacker, ClientCard* target, const vector3df& direct_pos) {
	if(!enabled || !attacker)
		return 0;
	const vector3df from = attacker->curPos;
	const vector3df to = target ? target->curPos : direct_pos;
	SColor color;
	const int style = AttackStyleOf(attacker->code, color);
	const bool ranged = style == ATK_BEAM || style == ATK_BOLTS || style == ATK_SLIFER || style == ATK_RA;
	float duration = 0.75f;
	if(style == ATK_SLIFER || style == ATK_RA)
		duration = 1.6f;
	else if(ranged)
		duration = 1.2f;
	else if(style == ATK_OBELISK)
		duration = 1.1f;
	else if(style == ATK_MAGIC || style == ATK_SHADOW)
		duration = 0.95f;
	if(gGameConfig->quick_animation)
		duration *= 0.7f;
	duration /= speed;
	if(holograms) {
		holos.erase(std::remove_if(holos.begin(), holos.end(), [&](const Hologram& h) {
			return h.base.getDistanceFromSQ(from) < 0.05f;
		}), holos.end());
		Hologram h;
		h.code = attacker->code;
		h.base = from;
		h.lunge_to = ranged ? from : from + (to - from) * 0.82f;   // ranged attackers stay and shoot
		h.color = color;
		h.t = 0;
		h.duration = duration;
		h.attack = true;
		h.custom_impact = style != ATK_DEFAULT;
		holos.push_back(h);
	} else if(style == ATK_DEFAULT) {
		Burst(Project(to), color, 26, 260, 14);
		Flash(SColor(255, 255, 255, 255), 0.35f, 0.2f);
		Shake(0.07f, 0.3f);
	}
	if(style != ATK_DEFAULT) {
		AttackFx a;
		a.style = style;
		a.from = from;
		a.to = to;
		a.color = color;
		a.duration = duration;
		a.direct = target == nullptr;
		attacks.push_back(a);
	}
	// keep the duel waiting while beams are still flying
	return ranged || style == ATK_OBELISK ? static_cast<int>(duration * 60.f * 0.9f) : 0;
}

void DuelFx::UpdateAttacks(float dt) {
	for(auto& a : attacks) {
		const float before = a.t;
		a.t += dt;
		const float k = a.duration / 0.75f;
		const vector2df to2 = Project(a.to);
		auto crossed = [&](float when) { return before < when && a.t >= when; };
		switch(a.style) {
		case ATK_MAGIC:
		case ATK_SHADOW:
		case ATK_SLASH:
		case ATK_OBELISK: {
			const float out_s = 0.2f * k, impact = 0.38f * k;
			if(a.style != ATK_SLASH && a.style != ATK_OBELISK && a.t > out_s * 0.5f && a.t < impact + 0.12f * k) {
				// black smoke trail following the lunging hologram
				const float p = EaseIn((a.t - out_s) / (impact - out_s));
				const vector3df pos = a.from + (a.to - a.from) * 0.82f * Clamp01(p) + vector3df(0, 0, 0.35f);
				const int n = 2 + static_cast<int>(dt * 160.f);
				const SColor smoke = a.style == ATK_MAGIC ? SColor(235, 12, 4, 22) : SColor(235, 14, 2, 6);
				Smoke(Project(pos), smoke, n, 44.f, 36.f, 1.2f);
				if(Rand() < 0.5f)
					Burst(Project(pos), a.style == ATK_MAGIC ? a.color : SColor(255, 255, 60, 40), 1, 60, 9);
			}
			if(crossed(impact)) {
				if(a.style == ATK_MAGIC || a.style == ATK_SHADOW) {
					Smoke(to2, SColor(240, 10, 3, 18), 40, 60.f, 110.f, 1.5f);
					Burst(to2, a.color, 34, 320, 15);
					Flash(a.style == ATK_MAGIC ? SColor(255, 120, 40, 200) : SColor(255, 150, 20, 20), 0.3f, 0.3f);
					Shake(0.09f, 0.35f);
				} else if(a.style == ATK_SLASH) {
					Burst(to2, SColor(255, 255, 255, 255), 24, 360, 12);
					Burst(to2, a.color, 16, 220, 14);
					Flash(SColor(255, 255, 255, 255), 0.3f, 0.15f);
					Shake(0.08f, 0.3f);
				} else {
					Burst(to2, a.color, 60, 480, 20);
					Burst(to2, SColor(255, 255, 255, 255), 20, 260, 26);
					Smoke(to2, SColor(160, 90, 70, 40), 20, 50.f, 140.f, 1.2f);
					Flash(SColor(255, 255, 240, 200), 0.6f, 0.4f);
					Shake(0.22f, 0.7f);
				}
				a.impact_done = true;
			}
			break;
		}
		case ATK_BEAM:
		case ATK_SLIFER:
		case ATK_RA: {
			const float p = a.t / a.duration;
			const float hit = 0.32f;
			if(crossed(hit * a.duration)) {
				const bool god = a.style != ATK_BEAM;
				Burst(to2, a.color, god ? 60 : 36, god ? 460 : 330, god ? 18 : 15);
				Burst(to2, SColor(255, 255, 255, 255), 14, 200, 22);
				Flash(a.color, god ? 0.45f : 0.28f, god ? 0.5f : 0.3f);
				Shake(god ? 0.18f : 0.1f, god ? 0.9f : 0.45f);
				a.impact_done = true;
			}
			if(p > hit && p < 0.8f) {
				// sparks while the beam keeps hitting
				const int n = static_cast<int>(dt * (a.style == ATK_BEAM ? 60.f : 140.f)) + (Rand() < 0.5f ? 1 : 0);
				Burst(to2, a.color, n, 260, 12);
				if(a.style == ATK_RA || a.color.getRed() > 200)
					Smoke(to2, SColor(110, 40, 20, 10), Rand() < dt * 20.f ? 1 : 0, 34.f, 70.f, 0.8f);
			}
			break;
		}
		case ATK_BOLTS: {
			for(int i = a.bolts_hit; i < 3; i++) {
				const float arrive = (0.15f + i * 0.15f + 0.22f) * a.duration;
				if(a.t >= arrive) {
					Burst(to2, a.color, 16, 260, 12);
					Burst(to2, SColor(255, 255, 255, 255), 5, 140, 16);
					Shake(0.05f, 0.18f);
					if(i == 2)
						Flash(a.color, 0.22f, 0.25f);
					a.bolts_hit = i + 1;
				}
			}
			break;
		}
		default:
			break;
		}
	}
	attacks.erase(std::remove_if(attacks.begin(), attacks.end(), [](const AttackFx& a) { return a.t >= a.duration; }), attacks.end());
}

void DuelFx::DrawAttacks() {
	const float scale = mainGame->driver->getScreenSize().Height / 640.f;
	for(const auto& a : attacks) {
		const float p = a.t / a.duration;
		const vector2df to2 = Project(a.to);
		switch(a.style) {
		case ATK_BEAM:
		case ATK_SLIFER:
		case ATK_RA: {
			if(p < 0.18f || p > 0.85f)
				break;
			// the beam comes out of the monster's mouth (top of the hologram)
			const vector2df from2 = Project(a.from + vector3df(0, 0, a.style == ATK_BEAM ? 0.75f : 0.95f));
			const float grow = Clamp01((p - 0.18f) / 0.14f);          // beam travels to the target
			const float fade = 1 - Clamp01((p - 0.7f) / 0.15f);
			const vector2df end = from2 + (to2 - from2) * grow;
			const vector2df mid = (from2 + end) * 0.5f;
			const vector2df d = end - from2;
			const float len = static_cast<float>(d.getLength()) + 20.f * scale;
			const float ang = std::atan2(d.Y, d.X) * 180.f / PI;
			const bool god = a.style != ATK_BEAM;
			const float pulse = 0.85f + 0.15f * std::sin(time * 40.f);
			const float w = (god ? 100.f : 50.f) * scale * pulse * (0.4f + 0.6f * fade);
			DrawQuad(tLine, FULL_UV, mid, { len, w * 2.2f }, ang, WithAlpha(a.color, 0.45f * fade), true);
			DrawQuad(tLine, FULL_UV, mid, { len, w }, ang, WithAlpha(a.color, 0.95f * fade), true);
			DrawQuad(tLine, FULL_UV, mid, { len, w * 0.35f }, ang, SColor(static_cast<irr::u32>(240 * fade), 255, 255, 235), true);
			if(a.style == ATK_SLIFER) {
				// crackling red lightning around the beam
				for(int i = 0; i < 2; i++) {
					const float off = std::sin(time * 55.f + i * 2.1f) * w * 0.55f;
					const vector2df n{ -d.Y, d.X };
					vector2df nn = n;
					if(nn.getLengthSQ() > 0)
						nn.normalize();
					DrawQuad(tLine, FULL_UV, mid + nn * off, { len, 6.f * scale }, ang + std::sin(time * 30.f + i) * 1.5f,
							 SColor(static_cast<irr::u32>(220 * fade), 255, 120, 90), true);
				}
			}
			// glow at the mouth and at the impact point
			DrawQuad(tGlow, FULL_UV, from2, { w * 3.f, w * 3.f }, 0, WithAlpha(a.color, 0.9f * fade), true);
			if(grow >= 1.f) {
				DrawQuad(tGlow, FULL_UV, to2, { w * 5.f, w * 5.f }, 0, WithAlpha(a.color, 0.9f * fade), true);
				DrawQuad(tRays, FULL_UV, to2, { w * 6.f, w * 6.f }, time * 200.f, WithAlpha(a.color, 0.6f * fade), true);
			}
			break;
		}
		case ATK_BOLTS: {
			const vector2df from2 = Project(a.from + vector3df(0, 0, 0.5f));
			for(int i = 0; i < 3; i++) {
				const float start = (0.15f + i * 0.15f) * a.duration;
				const float s = (a.t - start) / (0.22f * a.duration);
				if(s < 0 || s > 1)
					continue;
				const vector2df pos = from2 + (to2 - from2) * EaseIn(s);
				const vector2df d = to2 - from2;
				const float ang = std::atan2(d.Y, d.X) * 180.f / PI;
				DrawQuad(tLine, FULL_UV, pos - d * 0.04f, { 90.f * scale, 22.f * scale }, ang, WithAlpha(a.color, 0.8f), true);
				DrawQuad(tGlow, FULL_UV, pos, { 46.f * scale, 46.f * scale }, 0, WithAlpha(a.color, 1.f), true);
				DrawQuad(tSpark, FULL_UV, pos, { 20.f * scale, 20.f * scale }, 0, SColor(255, 255, 255, 255), true);
			}
			break;
		}
		case ATK_SLASH: {
			const float impact = 0.38f / 0.75f;
			const float s = (p - impact) / 0.3f;
			if(s < 0 || s > 1)
				break;
			const float len = (60.f + 170.f * EaseOut(s * 2.f)) * scale;
			const float alpha = 1 - Clamp01((s - 0.4f) / 0.6f);
			for(float ang : { -38.f, 32.f }) {
				DrawQuad(tLine, FULL_UV, to2, { len, 26.f * scale }, ang, WithAlpha(a.color, 0.7f * alpha), true);
				DrawQuad(tLine, FULL_UV, to2, { len, 8.f * scale }, ang, SColor(static_cast<irr::u32>(255 * alpha), 255, 255, 255), true);
			}
			break;
		}
		case ATK_OBELISK: {
			const float impact = 0.38f / 0.75f;
			for(int i = 0; i < 3; i++) {
				const float s = (p - impact - i * 0.08f) / 0.45f;
				if(s < 0 || s > 1)
					continue;
				const float r = (60.f + 520.f * EaseOut(s)) * scale;
				DrawQuad(tRing, FULL_UV, to2, { r, r * 0.55f }, 0, WithAlpha(a.color, (1 - s) * 0.95f), true);
			}
			break;
		}
		default:
			break;
		}
	}
}

void DuelFx::Damage(int player, uint32_t amount) {
	if(!enabled)
		return;
	const float k = std::min(1.f, amount / 3000.f);
	if(player == 0)
		Flash(SColor(255, 255, 30, 30), 0.25f + 0.35f * k, 0.45f);
	else
		Flash(SColor(255, 255, 140, 60), 0.12f + 0.2f * k, 0.35f);
	Shake(0.03f + 0.09f * k, 0.25f + 0.25f * k);
}

void DuelFx::Recover(int /*player*/) {
	if(!enabled)
		return;
	Flash(SColor(255, 90, 255, 140), 0.15f, 0.4f);
}

void DuelFx::Destroy(ClientCard* pcard) {
	if(!enabled || !pcard)
		return;
	const vector3df corners_local[4] = { { -0.35f, -0.5f, 0 }, { 0.35f, -0.5f, 0 }, { -0.35f, 0.5f, 0 }, { 0.35f, 0.5f, 0 } };
	vector2df c[4];
	for(int i = 0; i < 4; i++) {
		vector3df p = corners_local[i];
		pcard->mTransform.transformVect(p);
		c[i] = Project(p);
	}
	// textures are resolved on the main thread while drawing
	const bool faceup = pcard->code && (pcard->position & POS_FACEUP);
	constexpr int COLS = 4, ROWS = 5;
	const vector2df ex = c[1] - c[0];
	const vector2df ey = c[2] - c[0];
	const float rot0 = std::atan2(ex.Y, ex.X) * 180.f / PI;
	const vector2df size{ static_cast<float>(ex.getLength()) / COLS, static_cast<float>(ey.getLength()) / ROWS };
	const vector2df center = (c[0] + c[1] + c[2] + c[3]) * 0.25f;
	const float scale = mainGame->driver->getScreenSize().Height / 640.f;
	for(int j = 0; j < ROWS; j++) {
		for(int i = 0; i < COLS; i++) {
			const float u = (i + 0.5f) / COLS, v = (j + 0.5f) / ROWS;
			const vector2df top = c[0] + (c[1] - c[0]) * u;
			const vector2df bottom = c[2] + (c[3] - c[2]) * u;
			Shard s;
			s.code = faceup ? pcard->code : 0;
			s.controler = pcard->controler & 1;
			s.src = irr::core::recti(i * 1000 / COLS, j * 1000 / ROWS, (i + 1) * 1000 / COLS, (j + 1) * 1000 / ROWS);
			s.pos = top + (bottom - top) * v;
			vector2df dir = s.pos - center;
			if(dir.getLengthSQ() < 1)
				dir = vector2df(0, -1);
			dir.normalize();
			const float spd = (120 + Rand() * 220) * scale;
			s.vel = dir * spd + vector2df{ (Rand() - 0.5f) * 80 * scale, -(60 + Rand() * 140) * scale };
			s.size = size;
			s.rot = rot0;
			s.vrot = (Rand() - 0.5f) * 720;
			s.max_life = s.life = 0.7f + Rand() * 0.35f;
			shards.push_back(s);
		}
	}
	Burst(center, SColor(255, 255, 200, 120), 22, 220, 12);
	Shake(0.04f, 0.2f);
}

void DuelFx::Phase(uint16_t phase, bool my_turn) {
	if(!enabled)
		return;
	const wchar_t* name = nullptr;
	switch(phase) {
	case PHASE_DRAW: name = L"DRAW PHASE"; break;
	case PHASE_STANDBY: name = L"STANDBY PHASE"; break;
	case PHASE_MAIN1: name = L"MAIN PHASE 1"; break;
	case PHASE_BATTLE_START:
	case PHASE_BATTLE: name = L"BATTLE PHASE"; break;
	case PHASE_MAIN2: name = L"MAIN PHASE 2"; break;
	case PHASE_END: name = L"END PHASE"; break;
	default: return;
	}
	phase_banner.active = true;
	phase_banner.text = name;
	phase_banner.color = my_turn ? SColor(255, 40, 120, 255) : SColor(255, 230, 50, 60);
	if(phase == PHASE_BATTLE_START || phase == PHASE_BATTLE)
		phase_banner.color = SColor(255, 255, 120, 30);
	phase_banner.t = 0;
	phase_banner.duration = 0.85f / speed;
	phase_banner.big = false;
}

int DuelFx::Turn(bool my_turn) {
	if(!enabled)
		return 0;
	turn_banner.active = true;
	turn_banner.text = my_turn ? L"YOUR TURN" : L"OPPONENT'S TURN";
	turn_banner.color = my_turn ? SColor(255, 40, 120, 255) : SColor(255, 230, 50, 60);
	turn_banner.t = 0;
	turn_banner.duration = 1.0f / speed;
	turn_banner.big = true;
	phase_banner.active = false;
	return static_cast<int>(turn_banner.duration * 60.f * 0.8f);
}

/* ------------------------------------------------------------ per frame */

#ifdef FX_DEBUG
#include <cstdio>
static void FxDebugPoll() {
	static float acc = 0;
	acc += mainGame->delta_time / 1000.f;
	if(acc < 0.5f)
		return;
	acc = 0;
	FILE* f = fopen("fx_test.txt", "r");
	if(!f)
		return;
	char cmd[64] = {};
	fscanf(f, "%63s", cmd);
	fclose(f);
	remove("fx_test.txt");
	ClientCard* mine = nullptr;
	ClientCard* theirs = nullptr;
	for(auto* c : mainGame->dField.mzone[0]) if(c && !mine) mine = c;
	for(auto* c : mainGame->dField.mzone[1]) if(c && !theirs) theirs = c;
	for(auto* c : mainGame->dField.szone[1]) if(c && !theirs) theirs = c;
	static ClientCard dummy{};
	if(!mine) {
		dummy.controler = 0;
		dummy.location = LOCATION_MZONE;
		dummy.sequence = 2;
		dummy.position = POS_FACEUP_ATTACK;
		dummy.code = 89631139;
		dummy.curPos = { 3.95f, 1.4f, 0.f };
		dummy.mTransform.makeIdentity();
		dummy.mTransform.setTranslation(dummy.curPos);
		mine = &dummy;
	}
	const std::string s(cmd);
	if(s.rfind("atk:", 0) == 0 || s.rfind("atkd:", 0) == 0) {
		static ClientCard att{};
		att.controler = 0;
		att.location = LOCATION_MZONE;
		att.sequence = 2;
		att.position = POS_FACEUP_ATTACK;
		att.code = static_cast<uint32_t>(std::stoul(s.substr(s.find(':') + 1)));
		att.curPos = { 3.95f, 1.4f, 0.f };
		att.mTransform.makeIdentity();
		att.mTransform.setTranslation(att.curPos);
		gFx.Attack(&att, s[3] == 'd' ? nullptr : theirs, { 3.95f, -3.5f, 0.f });
		return;
	}
	if(s == "big" && mine) gFx.Summon(89631139, mine, DuelFx::SPECIAL_SUMMON);
	if(s == "xyz" && mine) { gFx.NoteMove(mine->controler, LOCATION_MZONE, mine->sequence, LOCATION_EXTRA); gFx.Summon(84013237, mine, DuelFx::SPECIAL_SUMMON); }
	if(s == "small" && mine) gFx.Summon(mine->code, mine, DuelFx::NORMAL_SUMMON);
	if(s == "attack" && mine) gFx.Attack(mine, theirs, { 3.95f, -3.5f, 0.f });
	if(s == "damage") gFx.Damage(0, 2500);
	if(s == "destroy" && mine) gFx.Destroy(mine);
	if(s == "turn") gFx.Turn(true);
	if(s == "phase") gFx.Phase(PHASE_BATTLE_START, true);
}
#endif

void DuelFx::BeginDuelFrame() {
#ifdef FX_DEBUG
	FxDebugPoll();
#endif
	frame_dt = std::min(0.1f, mainGame->delta_time / 1000.f);
	time += frame_dt;
	const float dt = frame_dt * speed;
	// effects timers
	if(flash_alpha > 0)
		flash_alpha = std::max(0.f, flash_alpha - flash_decay * frame_dt);
	for(auto& p : particles) {
		p.life -= frame_dt;
		p.pos += p.vel * frame_dt;
		p.size += p.grow * frame_dt;
		p.vel *= std::pow(0.12f, frame_dt);
	}
	particles.erase(std::remove_if(particles.begin(), particles.end(), [](const Particle& p) { return p.life <= 0; }), particles.end());
	const float gravity = 900.f * mainGame->driver->getScreenSize().Height / 640.f;
	for(auto& s : shards) {
		s.life -= frame_dt;
		s.vel.Y += gravity * frame_dt;
		s.pos += s.vel * frame_dt;
		s.rot += s.vrot * frame_dt;
	}
	shards.erase(std::remove_if(shards.begin(), shards.end(), [](const Shard& s) { return s.life <= 0; }), shards.end());
	for(auto& h : holos) {
		const float before = h.t;
		h.t += dt;
		if(h.attack && !h.custom_impact) {
			const float impact = 0.38f * h.duration / 0.75f;
			if(before < impact && h.t >= impact) {
				const auto pos = Project(h.lunge_to);
				Burst(pos, h.color, 30, 300, 15);
				Burst(pos, SColor(255, 255, 255, 255), 10, 160, 22);
				Flash(SColor(255, 255, 255, 255), 0.35f, 0.2f);
				Shake(0.08f, 0.3f);
			}
		}
	}
	holos.erase(std::remove_if(holos.begin(), holos.end(), [](const Hologram& h) { return h.t >= h.duration; }), holos.end());
	UpdateAttacks(dt);
	if(summon.active) {
		summon.t += frame_dt;
		const float p = summon.t / summon.duration;
		if(summon.big && p >= 0.12f && p - frame_dt / summon.duration < 0.12f)
			Flash(SColor(255, 255, 255, 255), 0.35f, 0.3f);
		const float land = summon.big ? 0.86f : 0.72f;
		if(!summon.landed && p >= land) {
			summon.landed = true;
			if(summon.has_field) {
				const auto pos = Project(summon.field_pos3d);
				Burst(pos, summon.color, summon.big ? 40 : 18, summon.big ? 340 : 200, summon.big ? 16 : 11);
				AddHologram(summon.code, summon.field_pos3d, summon.color, summon.big ? 1.6f / speed : 1.1f / speed);
			}
			Shake(summon.big ? 0.10f : 0.035f, summon.big ? 0.4f : 0.2f);
			if(summon.big)
				Flash(summon.color, 0.2f, 0.3f);
		}
		// sparks around the card while it is shown
		if(summon.big && p > 0.12f && p < 0.8f) {
			const auto ss = mainGame->driver->getScreenSize();
			const vector2df center{ ss.Width * 0.5f, ss.Height * 0.44f };
			const float card_h = ss.Height * 0.5f;
			const int n = static_cast<int>(70 * frame_dt + Rand());
			for(int i = 0; i < n; i++) {
				const float ang = Rand() * 2 * PI;
				vector2df at = center + vector2df{ std::cos(ang) * card_h * 0.36f, std::sin(ang) * card_h * 0.52f };
				Burst(at, summon.color, 1, 140, 10);
			}
		}
		if(summon.t >= summon.duration)
			summon.active = false;
	}
	for(auto* b : { &phase_banner, &turn_banner }) {
		if(b->active) {
			b->t += frame_dt;
			if(b->t >= b->duration)
				b->active = false;
		}
	}
	// camera shake: move the whole 3D view for this frame
	if(shake_amount > 0) {
		shake_time += frame_dt;
		const float k = 1 - shake_time / shake_duration;
		if(k <= 0) {
			shake_amount = 0;
		} else {
			const float a = shake_amount * k * k;
			irr::core::matrix4 t;
			t.setTranslation({ (Rand() * 2 - 1) * a, (Rand() * 2 - 1) * a, 0 });
			auto* driver = mainGame->driver;
			driver->setTransform(irr::video::ETS_VIEW, t * driver->getTransform(irr::video::ETS_VIEW));
		}
	}
}

void DuelFx::Draw3D() {
	if(!enabled || holos.empty())
		return;
	auto* driver = mainGame->driver;
	auto* camera = mainGame->camera;
	const vector3df cam_pos = camera->getAbsolutePosition();
	const vector3df cam_up = camera->getUpVector();
	// far ones first
	std::vector<Hologram*> order;
	for(auto& h : holos)
		order.push_back(&h);
	std::sort(order.begin(), order.end(), [](const Hologram* a, const Hologram* b) { return a->base.Y < b->base.Y; });
	driver->setTransform(irr::video::ETS_WORLD, irr::core::IdentityMatrix);
	const irr::u16 idx[6] = { 0, 1, 2, 3, 2, 1 };
	auto quad = [&](irr::video::ITexture* tex, const irr::core::rectf& uv, const vector3df& bottom_center,
					const vector3df& right, const vector3df& up, SColor color, bool additive) {
		if(!tex)
			return;
		irr::video::S3DVertex v[4];
		const vector3df p[4] = { bottom_center - right * 0.5f + up, bottom_center + right * 0.5f + up,
								 bottom_center - right * 0.5f, bottom_center + right * 0.5f };
		const irr::core::vector2df tc[4] = { { uv.UpperLeftCorner.X, uv.UpperLeftCorner.Y }, { uv.LowerRightCorner.X, uv.UpperLeftCorner.Y },
											 { uv.UpperLeftCorner.X, uv.LowerRightCorner.Y }, { uv.LowerRightCorner.X, uv.LowerRightCorner.Y } };
		for(int i = 0; i < 4; i++) {
			v[i].Pos = p[i];
			v[i].Normal = { 0, 1, 0 };
			v[i].Color = color;
			v[i].TCoords = tc[i];
		}
		driver->setMaterial(MakeMaterial(tex, additive));
		driver->drawIndexedTriangleList(v, 4, idx, 2);
	};
	for(auto* h : order) {
		const float d = h->duration;
		float rise, fade = 1;
		vector3df base = h->base;
		if(h->attack) {
			const float k = d / 0.75f;
			rise = EaseOut(h->t / (0.18f * k));
			const float out_s = 0.2f * k, impact = 0.38f * k, back_e = 0.62f * k;
			if(h->t > out_s && h->t <= impact)
				base = h->base + (h->lunge_to - h->base) * EaseIn((h->t - out_s) / (impact - out_s));
			else if(h->t > impact)
				base = h->lunge_to + (h->base - h->lunge_to) * EaseOut((h->t - impact) / (back_e - impact));
			fade = 1 - Clamp01((h->t - 0.62f * k) / (0.13f * k));
		} else {
			rise = EaseOutBack(h->t / 0.35f);
			fade = 1 - Clamp01((h->t - (d - 0.4f)) / 0.4f);
		}
		if(fade <= 0)
			continue;
		const float flicker = 0.9f + 0.1f * std::sin(time * 37.f + h->base.X * 3.f);
		const float alpha = Clamp01(rise) * fade * flicker;
		base.Z += 0.08f;
		vector3df view = cam_pos - base;
		view.normalize();
		vector3df up = cam_up - view * view.dotProduct(cam_up);
		if(up.getLengthSQ() < 1e-4f)
			up = { 0, 0, 1 };
		up.normalize();
		up = up + vector3df{ 0, 0, 1 };
		up.normalize();
		vector3df right = view.crossProduct(up);
		if(right.getLengthSQ() < 1e-4f)
			right = { 1, 0, 0 };
		right.normalize();
		const float size = h->attack ? 1.15f : 1.25f;
		const float height = size * std::max(0.f, rise);
		// light pillar
		quad(tBeam, FULL_UV, base, right * (size * 1.5f), up * (height * 1.7f), WithAlpha(h->color, alpha * 0.55f), true);
		// glow behind the art
		quad(tGlow, FULL_UV, base - up * (size * 0.12f), right * (size * 1.7f), up * (height * 1.3f), WithAlpha(h->color, alpha * 0.8f), true);
		// the monster
		auto* art = mainGame->imageManager.GetTextureCard(h->code, imgType::ART);
		quad(art, ART_UV, base, right * size, up * height, SColor(static_cast<irr::u32>(alpha * 235), 235, 245, 255), false);
		// scan highlight
		quad(art, ART_UV, base, right * size, up * height, WithAlpha(h->color, alpha * 0.25f), true);
		// ring lying on the table
		{
			irr::video::S3DVertex v[4];
			const float r = 0.75f * (0.6f + 0.4f * Clamp01(rise));
			const vector3df c{ base.X, base.Y, 0.03f };
			const vector3df p[4] = { c + vector3df{ -r, r, 0 }, c + vector3df{ r, r, 0 }, c + vector3df{ -r, -r, 0 }, c + vector3df{ r, -r, 0 } };
			const irr::core::vector2df tc[4] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
			for(int i = 0; i < 4; i++) {
				v[i].Pos = p[i];
				v[i].Normal = { 0, 0, 1 };
				v[i].Color = WithAlpha(h->color, alpha * 0.9f);
				v[i].TCoords = tc[i];
			}
			driver->setMaterial(MakeMaterial(tRing, true));
			driver->drawIndexedTriangleList(v, 4, idx, 2);
		}
	}
}

void DuelFx::DrawSummon() {
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float W = static_cast<float>(ss.Width), H = static_cast<float>(ss.Height);
	const float p = Clamp01(summon.t / summon.duration);
	const vector2df center{ W * 0.5f, H * 0.44f };
	auto* art = mainGame->imageManager.GetTextureCard(summon.code, imgType::ART);
	vector2df field = summon.has_field ? Project(summon.field_pos3d) : vector2df{ W * 0.5f, H * 0.7f };
	if(summon.big) {
		// darken the duel
		float dark = p < 0.12f ? p / 0.12f : (p > 0.8f ? 1 - (p - 0.8f) / 0.2f : 1);
		driver->draw2DRectangle(SColor(static_cast<irr::u32>(175 * Clamp01(dark)), 0, 0, 10), irr::core::recti(0, 0, ss.Width, ss.Height));
		// rotating rays + rings
		const float rays_a = Clamp01(dark) * (p < 0.15f ? p / 0.15f : 1);
		DrawQuad(tRays, FULL_UV, center, { H * 1.6f, H * 1.6f }, time * 18.f, WithAlpha(summon.color, rays_a * 0.85f), true);
		DrawQuad(tRays, FULL_UV, center, { H * 1.2f, H * 1.2f }, -time * 11.f, WithAlpha(SColor(255, 255, 255, 255), rays_a * 0.35f), true);
		for(int i = 0; i < 3; i++) {
			const float rp = std::fmod(summon.t * 1.4f + i / 3.f, 1.f);
			const float r = H * (0.25f + rp * 1.1f);
			DrawQuad(tRing, FULL_UV, center, { r, r }, 0, WithAlpha(summon.color, (1 - rp) * rays_a * 0.9f), true);
		}
		// the card
		float scale, alpha = 1;
		vector2df pos = center;
		if(p < 0.15f) {
			scale = 0.2f + 0.8f * EaseOutBack(p / 0.15f);
		} else if(p < 0.78f) {
			scale = 1.0f + 0.05f * (p - 0.15f) / 0.63f;
		} else {
			const float q = EaseIn((p - 0.78f) / 0.14f);
			scale = 1.05f + (0.18f - 1.05f) * q;
			pos = center + (field - center) * q;
			alpha = 1 - 0.7f * q;
			if(p > 0.92f)
				alpha = 0;
		}
		const float card_h = H * 0.5f * scale;
		const vector2df card_size{ card_h * 0.697f, card_h };
		DrawQuad(tGlow, FULL_UV, pos, card_size * 1.9f, 0, WithAlpha(summon.color, alpha * 0.9f), true);
		DrawQuad(art, FULL_UV, pos, card_size, 0, SColor(static_cast<irr::u32>(alpha * 255), 255, 255, 255), false);
		// shine passing on the card
		if(p > 0.15f && p < 0.5f) {
			const float sp = (p - 0.15f) / 0.35f;
			DrawQuad(tGlow, FULL_UV, pos + vector2df{ (sp - 0.5f) * card_size.X * 1.4f, 0 }, { card_size.X * 0.5f, card_size.Y * 1.1f }, 20,
					 SColor(static_cast<irr::u32>(140 * std::sin(sp * PI)), 255, 255, 255), true);
		}
		// label
		if(!summon.label.empty() && p > 0.16f && p < 0.82f) {
			const float lp = (p - 0.16f) / 0.66f;
			const float slide = (1 - EaseOut(lp / 0.18f)) * W * 0.3f;
			const float la = Clamp01(lp / 0.12f) * Clamp01((1 - lp) / 0.12f);
			const irr::core::ustring text(summon.label);
			auto* font = mainGame->lpcFont;
			const auto dim = font->getDimensionustring(text);
			const int y = static_cast<int>(center.Y + H * 0.29f);
			const int x = static_cast<int>(W * 0.5f - dim.Width / 2.f - slide);
			const SColor band = WithAlpha(summon.color, la * 0.55f);
			const SColor clear = WithAlpha(summon.color, 0);
			const irr::s32 mid = ss.Width / 2;
			driver->draw2DRectangle(irr::core::recti(0, y - 6, mid, y + dim.Height + 6), clear, band, clear, band);
			driver->draw2DRectangle(irr::core::recti(mid, y - 6, ss.Width, y + dim.Height + 6), band, clear, band, clear);
			const auto a = static_cast<irr::u32>(la * 255);
			font->drawustring(text, irr::core::recti(x + 3, y + 3, x + dim.Width + 3, y + dim.Height + 3), SColor(a, 0, 0, 0));
			font->drawustring(text, irr::core::recti(x, y, x + dim.Width, y + dim.Height), SColor(a, 255, 255, 255));
		}
	} else {
		float scale, alpha = 1;
		vector2df pos = center;
		if(p < 0.3f) {
			scale = 0.35f + 0.45f * EaseOutBack(p / 0.3f);
			alpha = Clamp01(p / 0.15f);
		} else if(p < 0.65f) {
			scale = 0.8f;
		} else {
			const float q = EaseIn((p - 0.65f) / 0.2f);
			scale = 0.8f + (0.18f - 0.8f) * q;
			pos = center + (field - center) * q;
			alpha = p > 0.85f ? 0 : 1 - 0.6f * q;
		}
		const float ring_p = Clamp01(p / 0.6f);
		DrawQuad(tRing, FULL_UV, center, { H * (0.3f + ring_p * 0.7f), H * (0.3f + ring_p * 0.7f) }, 0,
				 WithAlpha(summon.color, (1 - ring_p) * 0.8f), true);
		const float card_h = H * 0.5f * scale;
		const vector2df card_size{ card_h * 0.697f, card_h };
		DrawQuad(tGlow, FULL_UV, pos, card_size * 1.7f, 0, WithAlpha(summon.color, alpha * 0.7f), true);
		DrawQuad(art, FULL_UV, pos, card_size, 0, SColor(static_cast<irr::u32>(alpha * 255), 255, 255, 255), false);
	}
}

void DuelFx::DrawBanner(Banner& b) {
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float W = static_cast<float>(ss.Width), H = static_cast<float>(ss.Height);
	const float p = Clamp01(b.t / b.duration);
	float offset = 0, a = 1;
	if(p < 0.18f) {
		offset = -(1 - EaseOut(p / 0.18f)) * W;
	} else if(p > 0.82f) {
		offset = EaseIn((p - 0.82f) / 0.18f) * W;
		a = 1 - (p - 0.82f) / 0.18f;
	}
	auto* font = mainGame->lpcFont;
	const irr::core::ustring text(b.text);
	const auto dim = font->getDimensionustring(text);
	const float bh = b.big ? std::max<float>(H * 0.12f, dim.Height * 1.8f) : std::max<float>(H * 0.075f, dim.Height * 1.35f);
	const float cy = b.big ? H * 0.45f : H * 0.33f;
	const irr::s32 top = static_cast<irr::s32>(cy - bh / 2), bottom = static_cast<irr::s32>(cy + bh / 2);
	const irr::s32 off = static_cast<irr::s32>(offset);
	const irr::s32 mid = ss.Width / 2 + off;
	const SColor solid = WithAlpha(b.color, a * (b.big ? 0.85f : 0.7f));
	const SColor clear = WithAlpha(b.color, 0);
	driver->draw2DRectangle(irr::core::recti(off, top, mid, bottom), clear, solid, clear, solid);
	driver->draw2DRectangle(irr::core::recti(mid, top, ss.Width + off, bottom), solid, clear, solid, clear);
	const SColor line = SColor(static_cast<irr::u32>(a * 230), 255, 255, 255);
	const SColor line_clear = SColor(0, 255, 255, 255);
	driver->draw2DRectangle(irr::core::recti(off, top - 2, mid, top), line_clear, line, line_clear, line);
	driver->draw2DRectangle(irr::core::recti(mid, top - 2, ss.Width + off, top), line, line_clear, line, line_clear);
	driver->draw2DRectangle(irr::core::recti(off, bottom, mid, bottom + 2), line_clear, line, line_clear, line);
	driver->draw2DRectangle(irr::core::recti(mid, bottom, ss.Width + off, bottom + 2), line, line_clear, line, line_clear);
	const irr::s32 tx = static_cast<irr::s32>(W / 2 - dim.Width / 2.f) + off;
	const irr::s32 ty = static_cast<irr::s32>(cy - dim.Height / 2.f);
	const auto ta = static_cast<irr::u32>(a * 255);
	font->drawustring(text, irr::core::recti(tx + 2, ty + 2, tx + dim.Width + 2, ty + dim.Height + 2), SColor(ta, 0, 0, 0));
	font->drawustring(text, irr::core::recti(tx, ty, tx + dim.Width, ty + dim.Height), SColor(ta, 255, 255, 255));
}

void DuelFx::TickMenu() {
	if(mainGame->dInfo.isInDuel)
		return;
	time += std::min(0.1f, mainGame->delta_time / 1000.f);
}

void DuelFx::SetDragArrow(bool active, const vector2df& from, const vector2df& to, bool on_target) {
	drag_active = active;
	drag_from = from;
	drag_to = to;
	drag_on_target = on_target;
}

void DuelFx::DrawDragArrow() {
	const vector2df d = drag_to - drag_from;
	const float len = static_cast<float>(d.getLength());
	if(len < 4)
		return;
	const float ang = std::atan2(d.Y, d.X) * 180.f / PI;
	const SColor col = drag_on_target ? SColor(255, 255, 90, 60) : SColor(255, 255, 210, 90);
	const float scale = mainGame->driver->getScreenSize().Height / 640.f;
	const float pulse = 0.75f + 0.25f * std::sin(time * 10.f);
	// beam
	DrawQuad(tGlow, FULL_UV, (drag_from + drag_to) * 0.5f, { len, 26 * scale }, ang, WithAlpha(col, 0.55f * pulse), true);
	DrawQuad(tGlow, FULL_UV, (drag_from + drag_to) * 0.5f, { len, 8 * scale }, ang, WithAlpha(SColor(255, 255, 255, 255), 0.8f), true);
	// moving sparks along the beam
	for(int i = 0; i < 6; i++) {
		const float k = std::fmod(time * 1.6f + i / 6.f, 1.f);
		DrawQuad(tSpark, FULL_UV, drag_from + d * k, { 14 * scale, 14 * scale }, 0, WithAlpha(col, 1 - k * 0.5f), true);
	}
	// target reticle
	const float r = (drag_on_target ? 70 : 50) * scale * pulse;
	DrawQuad(tRing, FULL_UV, drag_to, { r, r }, 0, WithAlpha(col, 0.95f), true);
	DrawQuad(tGlow, FULL_UV, drag_to, { r * 0.8f, r * 0.8f }, 0, WithAlpha(col, 0.5f), true);
}

void DuelFx::Draw2D() {
	if(!mainGame->dInfo.isInDuel)
		return;
	if(drag_active)
		DrawDragArrow();
	if(!enabled)
		return;
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	// shards of destroyed cards
	for(const auto& s : shards) {
		const float a = Clamp01(s.life / (s.max_life * 0.5f));
		const irr::core::rectf uv{ s.src.UpperLeftCorner.X / 1000.f, s.src.UpperLeftCorner.Y / 1000.f,
								   s.src.LowerRightCorner.X / 1000.f, s.src.LowerRightCorner.Y / 1000.f };
		auto* tex = s.code ? mainGame->imageManager.GetTextureCard(s.code, imgType::ART) : mainGame->imageManager.tCover[s.controler];
		DrawQuad(tex, uv, s.pos, s.size, s.rot, SColor(static_cast<irr::u32>(a * 255), 255, 255, 255), false);
		DrawQuad(tGlow, FULL_UV, s.pos, s.size * 1.6f, 0, SColor(static_cast<irr::u32>(a * 90), 255, 220, 160), true);
	}
	if(summon.active)
		DrawSummon();
	DrawAttacks();
	for(const auto& p : particles) {
		const float a = Clamp01(p.life / p.max_life);
		if(p.smoke)
			DrawQuad(tGlow, FULL_UV, p.pos, { p.size, p.size }, p.life * 40.f, SColor(static_cast<irr::u32>(p.color.getAlpha() * a), p.color.getRed(), p.color.getGreen(), p.color.getBlue()), false);
		else
			DrawQuad(tSpark, FULL_UV, p.pos, { p.size, p.size }, 0, WithAlpha(p.color, a), p.additive);
	}
	if(turn_banner.active)
		DrawBanner(turn_banner);
	else if(phase_banner.active)
		DrawBanner(phase_banner);
	if(flash_alpha > 0)
		driver->draw2DRectangle(WithAlpha(flash_color, flash_alpha), irr::core::recti(0, 0, ss.Width, ss.Height));
}

}
