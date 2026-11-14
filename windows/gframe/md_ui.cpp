#include "md_ui.h"
#include "netserver.h"
#include <cmath>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <IrrlichtDevice.h>
#include <ITimer.h>
#include <IGUIEnvironment.h>
#include <IGUIButton.h>
#include <IGUIWindow.h>
#include <IGUIStaticText.h>
#include <IGUIEditBox.h>
#include <ISceneManager.h>
#include <ISceneCollisionManager.h>
#include <ICursorControl.h>
#include "game.h"
#include "image_manager.h"
#include "file_stream.h"
#include "bufferio.h"
#include "utils.h"
#include "fmt.h"
#include "fx.h"
#include "story_mode.h"
#include "world.h"
#include "sandbox.h"
#include "campaign.h"
#include "CGUITTFont/CGUITTFont.h"
#include "ocgapi_constants.h"
#include "client_card.h"

namespace ygo {

MdUi gUi;

namespace {

constexpr float PI = 3.14159265f;
using irr::video::SColor;
using irr::core::recti;

constexpr auto PORTRAIT_DIR = L"./story/portraits/";

float Clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
float EaseOut(float t) { t = Clamp01(t); return 1 - (1 - t) * (1 - t) * (1 - t); }
float EaseIn(float t) { t = Clamp01(t); return t * t; }
float Seg(float t, float a, float b) { return Clamp01((t - a) / (b - a)); }
irr::u32 A(float a) { return static_cast<irr::u32>(Clamp01(a) * 255.f); }
SColor WithA(SColor c, float a) { c.setAlpha(A(a * (c.getAlpha() / 255.f))); return c; }
SColor Mix(SColor a, SColor b, float t) {
	t = Clamp01(t);
	return SColor(static_cast<irr::u32>(a.getAlpha() + (b.getAlpha() - (float)a.getAlpha()) * t),
				  static_cast<irr::u32>(a.getRed() + (b.getRed() - (float)a.getRed()) * t),
				  static_cast<irr::u32>(a.getGreen() + (b.getGreen() - (float)a.getGreen()) * t),
				  static_cast<irr::u32>(a.getBlue() + (b.getBlue() - (float)a.getBlue()) * t));
}

float SdRoundRect(float px, float py, float hw, float hh, float r) {
	const float qx = std::fabs(px) - hw + r, qy = std::fabs(py) - hh + r;
	const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
	return std::min(std::max(qx, qy), 0.f) + std::sqrt(ox * ox + oy * oy) - r;
}

bool InPolygon(const float* xs, const float* ys, int n, float x, float y) {
	bool in = false;
	for(int i = 0, j = n - 1; i < n; j = i++) {
		if(((ys[i] > y) != (ys[j] > y)) && (x < (xs[j] - xs[i]) * (y - ys[i]) / (ys[j] - ys[i]) + xs[i]))
			in = !in;
	}
	return in;
}

irr::video::SMaterial UiMaterial(irr::video::ITexture* tex, bool additive) {
	irr::video::SMaterial m;
	m.Lighting = false;
	m.ZBuffer = irr::video::ECFN_DISABLED;
	m.ZWriteEnable = irr::video::EZW_OFF;
	m.BackfaceCulling = false;
	m.setTexture(0, tex);
	m.MaterialType = irr::video::EMT_ONETEXTURE_BLEND;
	m.MaterialTypeParam = irr::video::pack_textureBlendFunc(irr::video::EBF_SRC_ALPHA,
															additive ? irr::video::EBF_ONE : irr::video::EBF_ONE_MINUS_SRC_ALPHA,
															irr::video::EMFN_MODULATE_1X, irr::video::EAS_VERTEX_COLOR | irr::video::EAS_TEXTURE);
	return m;
}

}

/* ------------------------------------------------------------------ setup */

uint32_t MdUi::Now() const {
	return mainGame->device->getTimer()->getRealTime();
}

float MdUi::Scl() const {
	return mainGame->window_scale.Y * mainGame->dpi_scale;
}

void MdUi::LoadMap() {
	name_files.clear();
	FileStream file{ Utils::ToPathString(std::wstring(PORTRAIT_DIR) + L"portraits.json"), FileStream::in };
	if(!file.good())
		return;
	try {
		nlohmann::json j;
		file >> j;
		if(j.contains("default") && j["default"].is_string())
			default_file = BufferIO::DecodeUTF8(j["default"].get<std::string>());
		if(j.contains("player") && j["player"].is_string())
			player_file = BufferIO::DecodeUTF8(j["player"].get<std::string>());
		if(j.contains("names") && j["names"].is_object()) {
			for(auto& [k, v] : j["names"].items()) {
				if(v.is_string())
					name_files[BufferIO::DecodeUTF8(k)] = BufferIO::DecodeUTF8(v.get<std::string>());
			}
		}
	}
	catch(...) {
	}
}

void MdUi::MakeTextures() {
	auto* driver = mainGame->driver;
	auto make = [driver](const char* name, irr::u32 w, irr::u32 h, auto&& fn) -> irr::video::ITexture* {
		auto* img = driver->createImage(irr::video::ECF_A8R8G8B8, { w, h });
		if(!img)
			return nullptr;
		for(irr::u32 y = 0; y < h; y++)
			for(irr::u32 x = 0; x < w; x++)
				img->setPixel(x, y, fn(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f));
		auto* tex = driver->addTexture(name, img);
		img->drop();
		return tex;
	};
	tWhite = make("ui_white", 8, 8, [](float, float) { return SColor(255, 255, 255, 255); });

	// buttons: rounded, glossy
	struct Style { SColor top, bottom, border, glow; };
	const Style styles[2][4] = {
		{ // secondary
			{ SColor(255, 70, 78, 100), SColor(255, 28, 32, 46), SColor(255, 140, 150, 180), SColor(0, 0, 0, 0) },
			{ SColor(255, 95, 105, 135), SColor(255, 40, 46, 66), SColor(255, 200, 210, 240), SColor(120, 120, 160, 255) },
			{ SColor(255, 30, 34, 48), SColor(255, 60, 66, 90), SColor(255, 120, 130, 160), SColor(0, 0, 0, 0) },
			{ SColor(255, 50, 52, 58), SColor(255, 30, 31, 36), SColor(255, 80, 82, 90), SColor(0, 0, 0, 0) },
		},
		{ // primary
			{ SColor(255, 70, 160, 255), SColor(255, 18, 64, 180), SColor(255, 170, 220, 255), SColor(90, 60, 150, 255) },
			{ SColor(255, 110, 190, 255), SColor(255, 30, 90, 220), SColor(255, 230, 245, 255), SColor(170, 90, 180, 255) },
			{ SColor(255, 16, 56, 160), SColor(255, 50, 130, 235), SColor(255, 150, 200, 255), SColor(60, 60, 150, 255) },
			{ SColor(255, 60, 70, 90), SColor(255, 34, 38, 50), SColor(255, 90, 96, 110), SColor(0, 0, 0, 0) },
		},
	};
	// action circles
	{
		const irr::u32 CS = 128;
		const SColor fill[3][2] = { { SColor(235, 30, 70, 150), SColor(235, 8, 18, 50) },
									{ SColor(245, 60, 130, 230), SColor(245, 12, 40, 110) },
									{ SColor(245, 12, 40, 110), SColor(245, 50, 110, 210) } };
		for(int s = 0; s < 3; s++) {
			tCircle[s] = make(epro::format("ui_circle_{}", s).data(), CS, CS, [&](float x, float y) {
				const float dx = x - CS / 2.f, dy = y - CS / 2.f;
				const float r = std::sqrt(dx * dx + dy * dy);
				const float R = CS / 2.f - 8;
				if(r > R + 7)
					return SColor(0, 0, 0, 0);
				if(r > R + 2) {   // outer glow
					const float g = 1 - (r - R - 2) / 5.f;
					return SColor(A(g * (s == 1 ? 0.9f : 0.55f)), 120, 200, 255);
				}
				if(r > R - 4)     // bright ring
					return SColor(255, 170, 225, 255);
				if(r > R - 7)
					return SColor(255, 40, 90, 170);
				const float v = Clamp01((y - 8) / (CS - 16));
				SColor c = Mix(fill[s][0], fill[s][1], v);
				if(dy < -R * 0.25f && r < R - 10)   // gloss
					c = Mix(c, SColor(255, 255, 255, 255), 0.12f);
				return c;
			});
		}
	}
	const irr::u32 BW = 256, BH = 64;
	for(int p = 0; p < 2; p++) {
		for(int s = 0; s < 4; s++) {
			const Style st = styles[p][s];
			const auto name = epro::format("ui_btn_{}_{}", p, s);
			tBtn[p][s] = make(name.data(), BW, BH, [&](float x, float y) {
				const float px = x - BW / 2.f, py = y - BH / 2.f;
				const float d = SdRoundRect(px, py, BW / 2.f - 7, BH / 2.f - 7, 14);
				if(d > 6.5f) {
					return SColor(0, 0, 0, 0);
				}
				if(d > 0) { // outer glow
					const float g = std::pow(1 - d / 6.5f, 2.f) * (st.glow.getAlpha() / 255.f);
					SColor c = st.glow;
					c.setAlpha(A(g));
					return c;
				}
				const float v = Clamp01((y - 7) / (BH - 14));
				SColor c = Mix(st.top, st.bottom, v);
				if(v < 0.48f) // gloss
					c = Mix(c, SColor(255, 255, 255, 255), 0.22f * (1 - v / 0.48f));
				if(d > -2.2f)
					c = Mix(c, st.border, Clamp01((d + 2.2f) / 1.2f));
				c.setAlpha(A(Clamp01(0.5f - d)));
				return c;
			});
		}
	}

	// coin faces
	const irr::u32 CW = 256;
	for(int face = 0; face < 2; face++) {
		const SColor hi = face == 0 ? SColor(255, 255, 228, 130) : SColor(255, 240, 245, 255);
		const SColor lo = face == 0 ? SColor(255, 180, 120, 25) : SColor(255, 120, 130, 155);
		const SColor rim = face == 0 ? SColor(255, 140, 90, 15) : SColor(255, 90, 98, 120);
		const SColor mark = face == 0 ? SColor(255, 160, 100, 15) : SColor(255, 100, 110, 140);
		float sx[10], sy[10];
		for(int i = 0; i < 10; i++) {
			const float r = (i % 2 == 0) ? 0.48f : 0.2f;
			const float a = -PI / 2 + PI / 5 * i;
			sx[i] = std::cos(a) * r;
			sy[i] = std::sin(a) * r;
		}
		const auto name = epro::format("ui_coin_{}", face);
		tCoin[face] = make(name.data(), CW, CW, [&](float x, float y) {
			const float u = x / CW * 2 - 1, v = y / CW * 2 - 1;
			const float r = std::sqrt(u * u + v * v);
			if(r > 1)
				return SColor(0, 0, 0, 0);
			const float edge = Clamp01((1 - r) * CW / 2);
			SColor c = Mix(hi, lo, Clamp01((u + v + 1.2f) / 2.6f));
			if(r > 0.86f)
				c = Mix(rim, hi, std::pow(std::fabs(std::sin((r - 0.86f) / 0.14f * PI)), 2.f) * 0.6f);
			else if(std::fabs(r - 0.72f) < 0.025f)
				c = mark;
			else if(face == 0 && InPolygon(sx, sy, 10, u, v))
				c = Mix(mark, hi, Clamp01(-(u + v) * 0.8f + 0.2f));
			else if(face == 1 && (std::fabs(u) + std::fabs(v) * 0.7f < 0.42f) && (std::fabs(u) + std::fabs(v) * 0.7f > 0.30f))
				c = mark;
			c.setAlpha(A(edge));
			return c;
		});
	}

	tVignette = make("ui_vignette", 128, 128, [](float x, float y) {
		const float u = x / 128 * 2 - 1, v = y / 128 * 2 - 1;
		const float r = std::sqrt(u * u + v * v) / 1.41f;
		return SColor(A(std::pow(Clamp01((r - 0.35f) / 0.65f), 1.6f)), 0, 0, 0);
	});
}

void MdUi::StyleButton(irr::gui::IGUIButton* btn, bool primary) {
	if(!btn || !tBtn[1][0])
		return;
	const int p = primary ? 1 : 0;
	btn->setImage(irr::gui::EGBIS_IMAGE_UP, tBtn[p][0]);
	btn->setImage(irr::gui::EGBIS_IMAGE_UP_MOUSEOVER, tBtn[p][1]);
	btn->setImage(irr::gui::EGBIS_IMAGE_UP_FOCUSED, tBtn[p][0]);
	btn->setImage(irr::gui::EGBIS_IMAGE_UP_FOCUSED_MOUSEOVER, tBtn[p][1]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DOWN, tBtn[p][2]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DOWN_MOUSEOVER, tBtn[p][2]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DOWN_FOCUSED, tBtn[p][2]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DOWN_FOCUSED_MOUSEOVER, tBtn[p][2]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DISABLED, tBtn[p][3]);
	btn->setScaleImage(true);
	btn->setUseAlphaChannel(true);
	btn->setDrawBorder(false);
	btn->setOverrideColor(SColor(255, 255, 255, 255));
}

void MdUi::StyleCircleButton(irr::gui::IGUIButton* btn) {
	if(!btn || !tCircle[0])
		return;
	btn->setImage(irr::gui::EGBIS_IMAGE_UP, tCircle[0]);
	btn->setImage(irr::gui::EGBIS_IMAGE_UP_MOUSEOVER, tCircle[1]);
	btn->setImage(irr::gui::EGBIS_IMAGE_UP_FOCUSED, tCircle[0]);
	btn->setImage(irr::gui::EGBIS_IMAGE_UP_FOCUSED_MOUSEOVER, tCircle[1]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DOWN, tCircle[2]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DOWN_MOUSEOVER, tCircle[2]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DOWN_FOCUSED, tCircle[2]);
	btn->setImage(irr::gui::EGBIS_IMAGE_DOWN_FOCUSED_MOUSEOVER, tCircle[2]);
	btn->setScaleImage(true);
	btn->setUseAlphaChannel(true);
	btn->setDrawBorder(false);
	btn->setOverrideColor(SColor(255, 255, 255, 255));
}

void MdUi::LayoutCommandMenu(ClientCard* card) {
	auto* g = mainGame;
	irr::gui::IGUIButton* order[] = { g->btnActivate, g->btnSummon, g->btnSPSummon, g->btnMSet, g->btnSSet, g->btnRepos,
									  g->btnAttack, g->btnShowList, g->btnOperation, g->btnReset };
	std::vector<irr::gui::IGUIButton*> vis;
	for(auto* b : order)
		if(b->isVisible())
			vis.push_back(b);
	if(vis.empty())
		return;
	// short labels that fit in a circle
	if(g->btnActivate->isVisible()) g->btnActivate->setText(L"Activate");
	if(g->btnSummon->isVisible()) g->btnSummon->setText(L"Summon");
	if(g->btnSPSummon->isVisible()) g->btnSPSummon->setText(L"Special\nSummon");
	if(g->btnMSet->isVisible()) g->btnMSet->setText(L"Set");
	if(g->btnSSet->isVisible()) g->btnSSet->setText(L"Set");
	if(g->btnRepos->isVisible() && card)
		g->btnRepos->setText((card->position & POS_FACEDOWN) ? L"Flip" : L"Change\nPos.");
	if(g->btnAttack->isVisible()) g->btnAttack->setText(L"Attack");
	if(g->btnShowList->isVisible()) g->btnShowList->setText(L"View");
	if(g->btnOperation->isVisible() && std::wstring(g->btnOperation->getText()).find(L"Skill") == std::wstring::npos)
		g->btnOperation->setText(L"Effect");
	if(g->btnReset->isVisible()) g->btnReset->setText(L"Reset");
	const irr::s32 S = g->Scale(66), gap = g->Scale(10);
	const irr::s32 n = static_cast<irr::s32>(vis.size());
	const irr::s32 w = n * S + (n - 1) * gap;
	for(irr::s32 i = 0; i < n; i++) {
		StyleCircleButton(vis[static_cast<size_t>(i)]);
		vis[static_cast<size_t>(i)]->setRelativePosition(irr::core::recti(i * (S + gap), 0, i * (S + gap) + S, S));
	}
	// where is the card on screen? use its final position (hand cards rise when hovered)
	irr::core::recti rc;
	bool have = false;
	if(card) {
		irr::core::vector3df t, r;
		g->dField.GetCardDrawCoordinates(card, &t, &r);
		irr::core::matrix4 m;
		m.setTranslation(t);
		m.setRotationRadians(r);
		auto* coll = g->device->getSceneManager()->getSceneCollisionManager();
		const irr::core::vector3df corners[4] = { { -0.35f, -0.5f, 0 }, { 0.35f, -0.5f, 0 }, { -0.35f, 0.5f, 0 }, { 0.35f, 0.5f, 0 } };
		irr::s32 x1 = 1 << 30, y1 = 1 << 30, x2 = -(1 << 30), y2 = -(1 << 30);
		for(auto c : corners) {
			m.transformVect(c);
			const auto p = coll->getScreenCoordinatesFrom3DPosition(c, g->camera);
			x1 = std::min(x1, p.X); y1 = std::min(y1, p.Y); x2 = std::max(x2, p.X); y2 = std::max(y2, p.Y);
		}
		rc = irr::core::recti(x1, y1, x2, y2);
		if(card->location == LOCATION_HAND && card->hand_collision.getWidth() > 0) {
			// union with the hit box, the art can still be moving up
			rc.addInternalPoint(card->hand_collision.UpperLeftCorner);
			rc.addInternalPoint(card->hand_collision.LowerRightCorner);
		}
		have = true;
	}
	const auto ss = g->driver->getScreenSize();
	irr::s32 cx, top;
	if(have) {
		cx = rc.getCenter().X;
		// below the card, or above it when there is no room (hand)
		// never on top of the card and never over the hand:
		// hand -> above the card; field -> below it unless that lands on the hand (or off screen), then above
		const bool hand = card && card->location == LOCATION_HAND;
		irr::core::recti hand_area;
		bool have_hand = false;
		for(auto* hc : g->dField.hand[0]) {
			if(!hc || hc->hand_collision.getWidth() <= 0)
				continue;
			if(!have_hand)
				hand_area = hc->hand_collision;
			else {
				hand_area.addInternalPoint(hc->hand_collision.UpperLeftCorner);
				hand_area.addInternalPoint(hc->hand_collision.LowerRightCorner);
			}
			have_hand = true;
		}
		const irr::s32 below = rc.LowerRightCorner.Y + g->Scale(8);
		const irr::s32 above = rc.UpperLeftCorner.Y - S - g->Scale(10);
		const irr::core::recti below_rc(cx - w / 2, below, cx + w / 2, below + S);
		const bool below_hits_hand = have_hand && !hand && card->controler == 0 && below_rc.isRectCollided(hand_area);
		if(hand)   // on the upper part of the raised card: the cursor never has to leave the card
			top = rc.UpperLeftCorner.Y + g->Scale(4);
		else if(below_hits_hand || below + S > static_cast<irr::s32>(ss.Height) - g->Scale(4))
			top = above;
		else
			top = below;
		if(top < g->Scale(4))
			top = g->Scale(4);
	} else {
		const auto p = g->device->getCursorControl()->getPosition();
		cx = p.X;
		top = p.Y + g->Scale(20);
	}
	irr::s32 left = cx - w / 2;
	left = std::max<irr::s32>(g->Scale(4), std::min<irr::s32>(left, static_cast<irr::s32>(ss.Width) - w - g->Scale(4)));
	g->wCmdMenu->setRelativePosition(irr::core::recti(left, top, left + w, top + S));
	g->env->getRootGUIElement()->bringToFront(g->wCmdMenu);
	// corridor card <-> circles
	// only the circles + the empty gap between them and the card (not the card itself, not its neighbours)
	menu_keep = irr::core::recti(left - g->Scale(6), top - g->Scale(6), left + w + g->Scale(6), top + S + g->Scale(6));
	if(have) {
		const irr::s32 gx1 = std::min(left, rc.UpperLeftCorner.X), gx2 = std::max(left + w, rc.LowerRightCorner.X);
		if(card && card->location == LOCATION_HAND)   // circles on the raised hand card: a band under them too
			menu_keep.addInternalPoint(irr::core::vector2di(left - g->Scale(6), top + S + g->Scale(16)));
		else if(top + S <= rc.UpperLeftCorner.Y)        // circles above the card
			menu_keep.addInternalPoint(irr::core::vector2di(gx1, rc.UpperLeftCorner.Y)), menu_keep.addInternalPoint(irr::core::vector2di(gx2, top));
		else if(top >= rc.LowerRightCorner.Y)      // circles below the card
			menu_keep.addInternalPoint(irr::core::vector2di(gx1, rc.LowerRightCorner.Y)), menu_keep.addInternalPoint(irr::core::vector2di(gx2, top + S));
	}
}

bool MdUi::KeepMenuHover(const irr::core::vector2di& pos) const {
	return mainGame->wCmdMenu->isVisible() && menu_keep.isPointInside(pos);
}

void MdUi::Init() {
	LoadMap();
	MakeTextures();
	auto* g = mainGame;
	auto win = [this](irr::gui::IGUIElement* w) {
		if(!w)
			return;
		static_cast<irr::gui::IGUIWindow*>(w)->setDrawBackground(false);
		static_cast<irr::gui::IGUIWindow*>(w)->setDrawTitlebar(false);
		styled.push_back(w);
	};
	// duel prompts
	win(g->wQuery);
	win(g->wFTSelect);
	win(g->wOptions);
	win(g->wPosSelect);
	win(g->wMessage);
	win(g->wCardSelect);
	win(g->wCardDisplay);
	win(g->wANNumber);
	win(g->wANCard);
	win(g->wANAttribute);
	win(g->wANRace);
	// story / world
	win(gStory.wDialog);
	win(gStory.wStory);
	win(gWorld.wHub);
	win(gWorld.wStarter);
	win(gSandbox.wSandbox);
	win(gCampaign.wCampaign);
	win(g->wMainMenu);
	for(auto* child : g->wMainMenu->getChildren()) {
		if(child->getType() == irr::gui::EGUIET_BUTTON && child != g->wMainMenu->getCloseButton())
			StyleButton(static_cast<irr::gui::IGUIButton*>(child), false);
	}

	// query / message: bigger fixed windows with big buttons (not draggable)
	g->stQMessage->setRelativePosition(g->Scale(26, 38, 424, 116));
	g->btnYes->setRelativePosition(g->Scale(50, 124, 210, 168));
	g->btnNo->setRelativePosition(g->Scale(240, 124, 400, 168));
	g->stMessage->setRelativePosition(g->Scale(26, 38, 424, 116));
	g->btnMsgOK->setRelativePosition(g->Scale(145, 124, 305, 168));
	g->wCmdMenu->setDrawBackground(false);
	{
		// no window can be dragged around any more
		std::vector<irr::gui::IGUIElement*> stack{ g->env->getRootGUIElement() };
		while(!stack.empty()) {
			auto* e = stack.back();
			stack.pop_back();
			if(e->getType() == irr::gui::EGUIET_WINDOW)
				static_cast<irr::gui::IGUIWindow*>(e)->setDraggable(false);
			for(auto* c : e->getChildren())
				stack.push_back(c);
		}
	}
	StyleButton(g->btnYes, true);
	StyleButton(g->btnNo, false);
	// first / second
	g->wFTSelect->setRelativePosition(g->Scale(352, 230, 672, 385));
	g->wFTSelect->setText(L"เลือกลำดับการเล่น (Turn order)");
	g->btnFirst->setRelativePosition(g->Scale(20, 40, 300, 88));
	g->btnSecond->setRelativePosition(g->Scale(20, 98, 300, 146));
	StyleButton(g->btnFirst, true);
	StyleButton(g->btnSecond, false);
	// misc buttons
	StyleButton(g->btnMsgOK, true);
	StyleButton(g->btnOptionOK, true);
	StyleButton(g->btnOptionp, false);
	StyleButton(g->btnOptionn, false);
	for(auto* b : g->btnOption)
		StyleButton(b, false);
	StyleButton(g->btnSelectOK, true);
	StyleButton(g->btnDisplayOK, true);
	StyleButton(g->btnANNumberOK, true);
	StyleButton(g->btnANCardOK, true);
	// story + world buttons, by id
	auto by_id = [](irr::gui::IGUIElement* root, int id) -> irr::gui::IGUIButton* {
		if(!root)
			return nullptr;
		auto* e = root->getElementFromId(id, true);
		return (e && e->getType() == irr::gui::EGUIET_BUTTON) ? static_cast<irr::gui::IGUIButton*>(e) : nullptr;
	};
	StyleButton(by_id(gStory.wDialog, BUTTON_STORY_DIALOG_NEXT), true);
	StyleButton(by_id(gStory.wDialog, BUTTON_STORY_DIALOG_SKIP), false);
	StyleButton(by_id(gStory.wStory, BUTTON_STORY_PLAY), true);
	StyleButton(by_id(gStory.wStory, BUTTON_STORY_BACK), false);
	StyleButton(by_id(gStory.wStory, BUTTON_STORY_RESET), false);
	StyleButton(by_id(gStory.wStory, BUTTON_STORY_SKILL_INFO), false);
	for(int id : { BUTTON_WORLD_DUEL, BUTTON_WORLD_BUY1, BUTTON_WORLD_BUY10, BUTTON_WORLD_TRADER_BUY, BUTTON_WORLD_ITEM_USE, BUTTON_WORLD_FREE_PACK,
				   BUTTON_WORLD_STRUCT_BUY, BUTTON_WORLD_MISSION_CLAIM, BUTTON_WORLD_MISSION_CLAIM_ALL, BUTTON_WORLD_GATE_DUEL, BUTTON_WORLD_RANK_DUEL })
		StyleButton(by_id(gWorld.wHub, id), true);
	for(int id : { BUTTON_WORLD_REROLL, BUTTON_WORLD_TRADER_REFRESH, BUTTON_WORLD_BACK, BUTTON_WORLD_STORY, BUTTON_WORLD_DECKS, BUTTON_WORLD_ANIM,
				   BUTTON_WORLD_SKILL_INFO_GATE, BUTTON_WORLD_SKILL_INFO_RANK, BUTTON_WORLD_SKILL_INFO_ROAM })
		StyleButton(by_id(gWorld.wHub, id), false);
	for(int id : { BUTTON_WORLD_STARTER_0, BUTTON_WORLD_STARTER_1, BUTTON_WORLD_STARTER_2, BUTTON_WORLD_STARTER_3 })
		StyleButton(by_id(gWorld.wStarter, id), true);
	StyleButton(by_id(gWorld.wStarter, BUTTON_WORLD_STARTER_BACK), false);
	StyleButton(by_id(gWorld.wStarter, BUTTON_WORLD_STARTER_PREV), false);
	StyleButton(by_id(gWorld.wStarter, BUTTON_WORLD_STARTER_NEXT), false);
	StyleButton(by_id(gSandbox.wSandbox, BUTTON_SANDBOX_DUEL), true);
	StyleButton(by_id(gCampaign.wCampaign, BUTTON_CAMPAIGN_DUEL), true);
	StyleButton(by_id(gCampaign.wCampaign, BUTTON_CAMPAIGN_RESTART), false);
	StyleButton(by_id(gCampaign.wCampaign, BUTTON_CAMPAIGN_BACK), false);
	StyleButton(by_id(gWorld.wHub, BUTTON_WORLD_CAMPAIGN), true);
	if(gWorld.wCardSearch) {
		gWorld.wCardSearch->setDrawBackground(false);
		gWorld.wCardSearch->setDrawTitlebar(false);
		StyleButton(by_id(gWorld.wCardSearch, BUTTON_WORLD_SEARCH_POP), true);
		StyleButton(by_id(gWorld.wCardSearch, BUTTON_WORLD_SEARCH_CLOSE), false);
	}
	StyleButton(by_id(gWorld.wHub, BUTTON_WORLD_SEARCH), true);
	StyleButton(by_id(gWorld.wHub, BUTTON_WORLD_LOGIN), true);
	if(gWorld.wCardDetail) {
		gWorld.wCardDetail->setDrawBackground(false);
		gWorld.wCardDetail->setDrawTitlebar(false);
		StyleButton(by_id(gWorld.wCardDetail, BUTTON_WORLD_CD_CLOSE), false);
	}
	StyleButton(by_id(gWorld.wHub, BUTTON_WORLD_PACK_LIST), false);
	StyleButton(by_id(gWorld.wHub, BUTTON_WORLD_STRUCT_LIST), false);
	if(gWorld.wLogin) {
		gWorld.wLogin->setDrawBackground(false);
		gWorld.wLogin->setDrawTitlebar(false);
		StyleButton(by_id(gWorld.wLogin, BUTTON_WORLD_LOGIN_CLAIM), true);
		StyleButton(by_id(gWorld.wLogin, BUTTON_WORLD_LOGIN_CLOSE), false);
	}
	for(auto* b : gWorld.newDeckButtons)
		StyleButton(b, false);
	StyleButton(mainGame->btnNewDeck, true);
		if(gWorld.wSkillInfo) {
		gWorld.wSkillInfo->setDrawBackground(false);
		gWorld.wSkillInfo->setDrawTitlebar(false);
		StyleButton(by_id(gWorld.wSkillInfo, BUTTON_WORLD_SKILL_INFO_OK), true);
	}
	StyleButton(by_id(gWorld.wHub, BUTTON_WORLD_SKILL_EQUIP), true);
	StyleButton(by_id(gWorld.wHub, BUTTON_WORLD_SKILL_UNEQUIP), false);
	for(int id : { BUTTON_SANDBOX_DECKS, BUTTON_SANDBOX_LAN, BUTTON_SANDBOX_BACK })
		StyleButton(by_id(gSandbox.wSandbox, id), false);
	ready = true;
}

/* -------------------------------------------------------------- portraits */

irr::video::ITexture* MdUi::Portrait(const std::wstring& name) {
	if(name.empty())
		return nullptr;
	std::wstring file;
	if(name == L"คุณ" || name == L"__player__")
		file = player_file;
	else {
		auto it = name_files.find(name);
		if(it == name_files.end()) {
			if(default_file.empty())
				return nullptr;
			file = default_file;
		} else
			file = it->second;
	}
	return PortraitFile(file);
}

irr::video::ITexture* MdUi::PortraitFile(const std::wstring& file) {
	if(file.empty())
		return nullptr;
	auto cit = cache.find(file);
	if(cit != cache.end())
		return cit->second;
	const auto path = Utils::ToPathString(std::wstring(PORTRAIT_DIR) + file);
	irr::video::ITexture* tex = nullptr;
	if(Utils::FileExists(path))
		tex = mainGame->driver->getTexture(path.data());
	cache[file] = tex;
	return tex;
}

irr::video::ITexture* MdUi::OpponentPortrait() {
	const auto& ch = gStory.DuelChapter();
	if(!ch.portrait.empty())
		if(auto* tex = PortraitFile(ch.portrait))
			return tex;
	return Portrait(ch.opponent);
}

irr::video::ITexture* MdUi::PlayerPortrait() {
	return Portrait(L"__player__");
}

SColor MdUi::ArcColor(const std::wstring& arc) {
	if(arc.find(L"GX") != std::wstring::npos)
		return SColor(255, 230, 80, 40);
	if(arc.find(L"5D") != std::wstring::npos)
		return SColor(255, 220, 40, 70);
	if(arc.find(L"ZEXAL") != std::wstring::npos)
		return SColor(255, 240, 180, 40);
	if(arc.find(L"ARC") != std::wstring::npos)
		return SColor(255, 60, 200, 110);
	if(arc.find(L"VRAINS") != std::wstring::npos)
		return SColor(255, 40, 200, 200);
	return SColor(255, 160, 80, 230); // Battle City / default
}

/* ---------------------------------------------------------------- helpers */

void MdUi::Text(irr::gui::CGUITTFont* font, const std::wstring& text, int x, int y, SColor color, int align, bool shadow) {
	if(!font || text.empty())
		return;
	const irr::core::ustring t(text.data());
	const auto dim = font->getDimensionustring(t);
	int left = x;
	if(align == 0)
		left = x - static_cast<int>(dim.Width) / 2;
	else if(align > 0)
		left = x - static_cast<int>(dim.Width);
	const int top = y - static_cast<int>(dim.Height) / 2;
	if(shadow) {
		SColor sh(A(color.getAlpha() / 255.f * 0.85f), 0, 0, 0);
		font->drawustring(t, recti(left + 2, top + 2, left + dim.Width + 2, top + dim.Height + 2), sh);
	}
	font->drawustring(t, recti(left, top, left + dim.Width, top + dim.Height), color);
}

void MdUi::Rect(const recti& r, SColor top, SColor bottom) {
	mainGame->driver->draw2DRectangle(r, top, top, bottom, bottom);
}

void MdUi::HRect(const recti& r, SColor left, SColor right) {
	mainGame->driver->draw2DRectangle(r, left, right, left, right);
}

void MdUi::Quad(irr::video::ITexture* tex, float cx, float cy, float w, float h, float rot, SColor color, bool additive) {
	if(!tex || color.getAlpha() == 0)
		return;
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float sw = static_cast<float>(ss.Width), sh = static_cast<float>(ss.Height);
	const float rad = rot * PI / 180.f;
	const float c = std::cos(rad), s = std::sin(rad);
	const float lx[4] = { -0.5f, 0.5f, -0.5f, 0.5f }, ly[4] = { -0.5f, -0.5f, 0.5f, 0.5f };
	const float tu[4] = { 0, 1, 0, 1 }, tv[4] = { 0, 0, 1, 1 };
	irr::video::S3DVertex v[4];
	for(int i = 0; i < 4; i++) {
		const float px = cx + lx[i] * w * c - ly[i] * h * s;
		const float py = cy + lx[i] * w * s + ly[i] * h * c;
		v[i].Pos = { (px / sw - 0.5f) * 2.f, (py / sh - 0.5f) * -2.f, 0.5f };
		v[i].Normal = { 0, 0, -1 };
		v[i].Color = color;
		v[i].TCoords = irr::core::vector2df(tu[i], tv[i]);
	}
	const irr::u16 idx[6] = { 0, 1, 2, 3, 2, 1 };
	const auto oldProj = driver->getTransform(irr::video::ETS_PROJECTION);
	const auto oldView = driver->getTransform(irr::video::ETS_VIEW);
	const auto oldWorld = driver->getTransform(irr::video::ETS_WORLD);
	driver->setTransform(irr::video::ETS_PROJECTION, irr::core::IdentityMatrix);
	driver->setTransform(irr::video::ETS_VIEW, irr::core::IdentityMatrix);
	driver->setTransform(irr::video::ETS_WORLD, irr::core::IdentityMatrix);
	driver->setMaterial(UiMaterial(tex, additive));
	driver->drawIndexedTriangleList(v, 4, idx, 2);
	driver->setTransform(irr::video::ETS_PROJECTION, oldProj);
	driver->setTransform(irr::video::ETS_VIEW, oldView);
	driver->setTransform(irr::video::ETS_WORLD, oldWorld);
}

void MdUi::DrawPortrait(irr::video::ITexture* tex, float cx, float bottom, float height, SColor tint, bool flip) {
	if(!tex || tint.getAlpha() == 0)
		return;
	const auto ts = tex->getOriginalSize();
	const float w = height * ts.Width / static_cast<float>(ts.Height);
	const recti dst(static_cast<int>(cx - w / 2), static_cast<int>(bottom - height), static_cast<int>(cx + w / 2), static_cast<int>(bottom));
	recti src(0, 0, ts.Width, ts.Height);
	if(flip)
		src = recti(ts.Width, 0, 0, ts.Height);
	const SColor cols[4] = { tint, tint, tint, tint };
	mainGame->driver->draw2DImage(tex, dst, src, nullptr, cols, true);
}

void MdUi::DrawPanel(const recti& r, const std::wstring& title, SColor accent) {
	auto* driver = mainGame->driver;
	const float s = Scl();
	const int sh = static_cast<int>(7 * s);
	// drop shadow
	driver->draw2DRectangle(SColor(90, 0, 0, 0), recti(r.UpperLeftCorner.X - sh + 3, r.UpperLeftCorner.Y - sh + 6, r.LowerRightCorner.X + sh + 3, r.LowerRightCorner.Y + sh + 6));
	// body
	Rect(r, SColor(242, 24, 32, 56), SColor(244, 8, 10, 24));
	// subtle diagonal sheen
	HRect(recti(r.UpperLeftCorner.X, r.UpperLeftCorner.Y, r.LowerRightCorner.X, r.UpperLeftCorner.Y + r.getHeight() / 2),
		  SColor(26, 120, 170, 255), SColor(0, 120, 170, 255));
	// header
	if(!title.empty()) {
		const int hh = std::min(static_cast<int>(24 * s), r.getHeight() / 3);
		const recti hr(r.UpperLeftCorner.X, r.UpperLeftCorner.Y, r.LowerRightCorner.X, r.UpperLeftCorner.Y + hh);
		HRect(hr, WithA(accent, 0.85f), WithA(accent, 0.08f));
		HRect(recti(hr.UpperLeftCorner.X, hr.LowerRightCorner.Y - 1, hr.LowerRightCorner.X, hr.LowerRightCorner.Y + 1),
			  SColor(255, 240, 200, 100), SColor(0, 240, 200, 100));
		Text(mainGame->guiFont, title, hr.UpperLeftCorner.X + static_cast<int>(10 * s), hr.getCenter().Y, SColor(255, 255, 255, 255), -1);
	}
	// border + gold corners
	driver->draw2DRectangleOutline(r, SColor(200, 110, 160, 240));
	const int L = static_cast<int>(16 * s), T = std::max(2, static_cast<int>(2 * s));
	const SColor gold(255, 240, 200, 100);
	const int x0 = r.UpperLeftCorner.X, y0 = r.UpperLeftCorner.Y, x1 = r.LowerRightCorner.X, y1 = r.LowerRightCorner.Y;
	driver->draw2DRectangle(gold, recti(x0 - 1, y0 - 1, x0 + L, y0 - 1 + T));
	driver->draw2DRectangle(gold, recti(x0 - 1, y0 - 1, x0 - 1 + T, y0 + L));
	driver->draw2DRectangle(gold, recti(x1 - L, y0 - 1, x1 + 1, y0 - 1 + T));
	driver->draw2DRectangle(gold, recti(x1 + 1 - T, y0 - 1, x1 + 1, y0 + L));
	driver->draw2DRectangle(gold, recti(x0 - 1, y1 + 1 - T, x0 + L, y1 + 1));
	driver->draw2DRectangle(gold, recti(x0 - 1, y1 - L, x0 - 1 + T, y1 + 1));
	driver->draw2DRectangle(gold, recti(x1 - L, y1 + 1 - T, x1 + 1, y1 + 1));
	driver->draw2DRectangle(gold, recti(x1 + 1 - T, y1 - L, x1 + 1, y1 + 1));
}

std::vector<std::wstring> MdUi::Wrap(irr::gui::CGUITTFont* font, const std::wstring& text, int max_width) {
	std::vector<std::wstring> lines;
	std::wstring line;
	auto width = [font](const std::wstring& s) { return static_cast<int>(font->getDimensionustring(irr::core::ustring(s.data())).Width); };
	for(wchar_t c : text) {
		if(c == L'\n') {
			lines.push_back(line);
			line.clear();
			continue;
		}
		line.push_back(c);
		if(width(line) <= max_width)
			continue;
		size_t brk = line.rfind(L' ');
		std::wstring rest;
		if(brk != std::wstring::npos && brk > 0) {
			rest = line.substr(brk + 1);
			line.resize(brk);
		} else {
			// keep thai combining marks with their base character
			size_t cut = line.size() - 1;
			while(cut > 0 && ((line[cut] >= 0x0E31 && line[cut] <= 0x0E3A) || (line[cut] >= 0x0E47 && line[cut] <= 0x0E4E) || line[cut] == 0x0E34))
				cut--;
			rest = line.substr(cut);
			line.resize(cut);
		}
		lines.push_back(line);
		line = rest;
	}
	if(!line.empty())
		lines.push_back(line);
	return lines;
}

/* ------------------------------------------------------------------- draw */

void MdUi::DrawUnderGui() {
	if(!ready)
		return;
	DrawDialogScene();
	for(auto* w : styled) {
		if(!w->isVisible())
			continue;
		SColor accent(255, 50, 120, 230);
		if(w == gStory.wDialog)
			accent = ArcColor(gStory.DuelChapter().arc);
		DrawPanel(w->getAbsolutePosition(), w == gStory.wDialog ? std::wstring() : std::wstring(w->getText()), accent);
	}
	DrawQueryCard();
	// name plate of the dialogue
	if(gStory.wDialog && gStory.wDialog->isVisible()) {
		const auto speaker = gStory.DialogSpeaker();
		if(!speaker.empty()) {
			const auto r = gStory.wDialog->getAbsolutePosition();
			const float s = Scl();
			const auto dim = mainGame->guiFont->getDimensionustring(irr::core::ustring(speaker.data()));
			const recti plate(r.UpperLeftCorner.X + static_cast<int>(6 * s), r.UpperLeftCorner.Y + static_cast<int>(6 * s),
							  r.UpperLeftCorner.X + static_cast<int>(40 * s) + dim.Width, r.UpperLeftCorner.Y + static_cast<int>(40 * s));
			const SColor ac = speaker == L"คุณ" ? SColor(255, 40, 140, 220) : ArcColor(gStory.DuelChapter().arc);
			HRect(plate, WithA(ac, 0.9f), WithA(ac, 0.f));
			mainGame->driver->draw2DRectangle(SColor(255, 240, 200, 100), recti(plate.UpperLeftCorner.X, plate.LowerRightCorner.Y - 2, plate.LowerRightCorner.X, plate.LowerRightCorner.Y));
		}
	}
}

void MdUi::DrawDialogScene() {
	if(!gStory.wDialog || !gStory.wDialog->isVisible())
		return;
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float W = static_cast<float>(ss.Width), H = static_cast<float>(ss.Height);
	const auto& ch = gStory.DuelChapter();
	const SColor arc = ArcColor(ch.arc);
	const uint32_t now = Now();
	// backdrop
	Rect(recti(0, 0, ss.Width, ss.Height), Mix(SColor(255, 6, 8, 20), arc, 0.28f), SColor(255, 4, 4, 10));
	Quad(gFx.RaysTexture(), W * 0.5f, H * 0.38f, H * 1.9f, H * 1.9f, now * 0.004f, WithA(arc, 0.22f), true);
	Quad(gFx.GlowTexture(), W * 0.5f, H * 0.42f, W * 0.9f, H * 0.7f, 0, WithA(arc, 0.25f), true);
	// floor line
	HRect(recti(0, static_cast<int>(H * 0.64f), ss.Width / 2, static_cast<int>(H * 0.64f) + 2), WithA(arc, 0.f), WithA(arc, 0.8f));
	HRect(recti(ss.Width / 2, static_cast<int>(H * 0.64f), ss.Width, static_cast<int>(H * 0.64f) + 2), WithA(arc, 0.8f), WithA(arc, 0.f));

	// who is on screen
	const std::wstring speaker = gStory.DialogSpeaker();
	if(speaker != last_speaker) {
		last_speaker = speaker;
		speaker_change = now;
	}
	if(right_name.empty())
		right_name = ch.opponent;
	const bool player_talks = speaker == L"คุณ";
	if(!player_talks && Portrait(speaker))
		right_name = speaker;
	const float k = EaseOut((now - speaker_change) / 260.f);
	const float dialog_top = static_cast<float>(gStory.wDialog->getAbsolutePosition().UpperLeftCorner.Y);
	const float ph = std::min(H * 0.78f, dialog_top * 1.18f);
	const float bottom = dialog_top + ph * 0.16f;
	// left: player
	if(auto* tex = PlayerPortrait()) {
		const bool active = player_talks;
		const float off = active ? (1 - k) * -40.f : 0;
		const SColor tint = active ? SColor(255, 255, 255, 255) : SColor(255, 95, 100, 120);
		DrawPortrait(tex, W * 0.24f + off, bottom + (active ? 0 : ph * 0.03f), active ? ph : ph * 0.94f, tint);
	}
	// right: opponent / other speaker
	if(auto* tex = Portrait(right_name)) {
		const bool active = !player_talks && speaker == right_name;
		const float off = active ? (1 - k) * 40.f : 0;
		const SColor tint = active ? SColor(255, 255, 255, 255) : SColor(255, 95, 100, 120);
		DrawPortrait(tex, W * 0.76f + off, bottom + (active ? 0 : ph * 0.03f), active ? ph : ph * 0.94f, tint, true);
	}
	Quad(tVignette, W * 0.5f, H * 0.5f, W, H, 0, SColor(200, 255, 255, 255));
}

void MdUi::DrawQueryCard() {
	auto* g = mainGame;
	if(!g->wQuery->isVisible() || !g->dInfo.isInDuel || g->dInfo.curMsg != MSG_SELECT_EFFECTYN)
		return;
	auto* card = g->dField.highlighting_card;
	uint32_t code = card ? card->code : 0;
	if(!code)
		return;
	auto* tex = g->imageManager.GetTextureCard(code, imgType::ART);
	if(!tex)
		return;
	const auto r = g->wQuery->getAbsolutePosition();
	const float h = r.getHeight() * 1.55f;
	const float w = h * 177.f / 254.f;
	const float cx = r.UpperLeftCorner.X - w / 2 - 14 * Scl();
	const float cy = static_cast<float>(r.getCenter().Y);
	const float pulse = 0.75f + 0.25f * std::sin(Now() * 0.006f);
	Quad(gFx.GlowTexture(), cx, cy, w * 2.0f, h * 1.6f, 0, SColor(A(0.6f * pulse), 90, 170, 255), true);
	const auto ts = tex->getOriginalSize();
	g->driver->draw2DImage(tex, recti(static_cast<int>(cx - w / 2), static_cast<int>(cy - h / 2), static_cast<int>(cx + w / 2), static_cast<int>(cy + h / 2)),
						   recti(0, 0, ts.Width, ts.Height), nullptr, nullptr, true);
	g->driver->draw2DRectangleOutline(recti(static_cast<int>(cx - w / 2) - 1, static_cast<int>(cy - h / 2) - 1, static_cast<int>(cx + w / 2) + 1, static_cast<int>(cy + h / 2) + 1),
									  SColor(255, 240, 200, 100));
}

void MdUi::DrawMenuPortraits() {
	if(!ready || mainGame->wMessage->isVisible() || mainGame->wQuery->isVisible())
		return;
	// portrait of the selected opponent in the story / world menus
	auto info_portrait = [this](irr::gui::IGUIElement* box, const std::wstring& name, irr::gui::IGUIElement* win, const std::wstring& file = {}) {
		if(!box || !win || !win->isVisible() || !box->isTrulyVisible())
			return;
		irr::video::ITexture* tex = file.empty() ? nullptr : PortraitFile(file);
		if(!tex)
			tex = Portrait(name);
		if(!tex)
			return;
		const auto r = box->getAbsolutePosition();
		const float h = r.getHeight() * 0.98f;
		const auto ts = tex->getOriginalSize();
		const float w = h * ts.Width / static_cast<float>(ts.Height);
		const recti clip = r;
		const recti dst(static_cast<int>(r.LowerRightCorner.X - w), static_cast<int>(r.LowerRightCorner.Y - h), r.LowerRightCorner.X, r.LowerRightCorner.Y);
		const SColor c(255, 255, 255, 255);
		const SColor cols[4] = { c, c, c, c };
		mainGame->driver->draw2DImage(tex, dst, recti(0, 0, ts.Width, ts.Height), &clip, cols, true);
	};
	info_portrait(gWorld.DuelistInfoBox(), gWorld.SelectedOpponent(), gWorld.wHub);
	info_portrait(gCampaign.InfoBox(), gCampaign.CurrentOpponent(), gCampaign.wCampaign, gCampaign.CurrentPortrait());
}

void MdUi::DrawOverGui() {
	if(!ready)
		return;
	// skill description popup: drawn after the whole GUI so the menu window behind does not show through
	if(gWorld.wSkillInfo && gWorld.wSkillInfo->isVisible()) {
		mainGame->driver->draw2DRectangle(SColor(140, 0, 0, 0), recti(0, 0, mainGame->driver->getScreenSize().Width, mainGame->driver->getScreenSize().Height));
		DrawPanel(gWorld.wSkillInfo->getAbsolutePosition(), L"ข้อมูลสกิล  (Skill)", SColor(255, 200, 150, 40));
		gWorld.wSkillInfo->draw();
	}
	if(gWorld.wLogin && gWorld.wLogin->isVisible()) {
		mainGame->driver->draw2DRectangle(SColor(140, 0, 0, 0), recti(0, 0, mainGame->driver->getScreenSize().Width, mainGame->driver->getScreenSize().Height));
		DrawPanel(gWorld.wLogin->getAbsolutePosition(), L"รางวัลรายวัน  (Daily Login Bonus)", SColor(255, 230, 170, 40));
		gWorld.DrawLoginOverlay();
		gWorld.wLogin->draw();
	}
	if(gWorld.wCardSearch && gWorld.wCardSearch->isVisible()) {
		mainGame->driver->draw2DRectangle(SColor(140, 0, 0, 0), recti(0, 0, mainGame->driver->getScreenSize().Width, mainGame->driver->getScreenSize().Height));
		DrawPanel(gWorld.wCardSearch->getAbsolutePosition(), gWorld.search_title.data(), SColor(255, 60, 140, 220));
		gWorld.wCardSearch->draw();
		gWorld.DrawCardSearchArt();
	}
	if(gWorld.wCardDetail && gWorld.wCardDetail->isVisible()) {
		mainGame->driver->draw2DRectangle(SColor(150, 0, 0, 0), recti(0, 0, mainGame->driver->getScreenSize().Width, mainGame->driver->getScreenSize().Height));
		DrawPanel(gWorld.wCardDetail->getAbsolutePosition(), L"รายละเอียดการ์ด  (Card Detail)", SColor(255, 200, 150, 255));
		gWorld.DrawCardDetailArt();
		gWorld.wCardDetail->draw();
	}
		// story / sandbox duels: hide the host room while the bot joins (Master Duel "matching" screen)
	if(gStory.IsActive() && mainGame->dInfo.isInLobby && overlay == Overlay::NONE)
		DrawMatching();
	if(overlay != Overlay::NONE) {
		const float t = (Now() - overlay_start) / static_cast<float>(overlay_len);
		if(t >= 1.f) {
			overlay = Overlay::NONE;
		} else {
			switch(overlay) {
			case Overlay::VERSUS: DrawVersus(t); break;
			case Overlay::COIN: DrawCoin(t); break;
			case Overlay::START: DrawStart(t); break;
			case Overlay::RESULT: DrawResult(t); break;
			default: break;
			}
		}
	}
	DrawClock();
	DrawSpeech();
}

void MdUi::DrawClock() {
	// tournament clock: shown under the turn counter, at 0 the player with less LP loses (equal = draw)
	if(!clock_deadline || !mainGame->dInfo.isInDuel || !gStory.IsActive())
		return;
	const uint32_t now = Now();
	const int remain = clock_deadline > now ? static_cast<int>(clock_deadline - now) : 0;
	const int W = static_cast<int>(mainGame->driver->getScreenSize().Width);
	const int H = static_cast<int>(mainGame->driver->getScreenSize().Height);
	const int sec = (remain + 999) / 1000;
	const bool hurry = sec <= 60;
	const int cx = W / 2, cy = static_cast<int>(H * 0.135f);
	const int bw = static_cast<int>(H * 0.13f), bh = static_cast<int>(H * 0.04f);
	const bool blink = hurry && ((now / 400) % 2 == 0);
	Rect(recti(cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2), SColor(210, 20, 24, 40), SColor(210, 8, 10, 20));
	const SColor edge = hurry ? SColor(255, 255, 80, 80) : SColor(255, 120, 170, 255);
	mainGame->driver->draw2DRectangleOutline(recti(cx - bw / 2, cy - bh / 2, cx + bw / 2, cy + bh / 2), edge);
	Text(mainGame->guiFont, epro::format(L"TIME {:02}:{:02}", sec / 60, sec % 60), cx, cy,
		 blink ? SColor(255, 255, 255, 255) : (hurry ? SColor(255, 255, 110, 110) : SColor(255, 220, 235, 255)), 0, true);
	if(remain == 0 && !clock_fired) {
		clock_fired = true;
		const int me = mainGame->dInfo.lp[0], opp = mainGame->dInfo.lp[1];
		const uint8_t my_team = mainGame->dInfo.isTeam1 ? 0 : 1;
		const uint8_t winner = me == opp ? 2 : (me > opp ? my_team : static_cast<uint8_t>(1 - my_team));
		Speak(me == opp ? L"หมดเวลา! LP เท่ากัน - เสมอ" : (me > opp ? L"หมดเวลา! LP คุณมากกว่า - ชนะ!" : L"หมดเวลา! LP คุณน้อยกว่า - แพ้"));
		NetServer::ForceResult(winner, 3);   // 3 = time limit
	}
}

void MdUi::DrawVersus(float t) {
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float W = static_cast<float>(ss.Width), H = static_cast<float>(ss.Height);
	const auto& ch = gStory.DuelChapter();
	const SColor arc = ArcColor(ch.arc);
	const SColor mine(255, 30, 120, 230);
	const float fade = 1 - Seg(t, 0.88f, 1.f);
	driver->draw2DRectangle(SColor(A(0.92f * fade * (0.3f + 0.7f * Seg(t, 0, 0.08f))), 4, 5, 12), recti(0, 0, ss.Width, ss.Height));
	const float in = EaseOut(Seg(t, 0.02f, 0.24f));
	// diagonal halves
	Quad(tWhite, W * 0.22f - (1 - in) * W * 0.7f, H * 0.5f, W * 0.62f, H * 1.7f, 12, WithA(mine, 0.55f * fade));
	Quad(tWhite, W * 0.78f + (1 - in) * W * 0.7f, H * 0.5f, W * 0.62f, H * 1.7f, 12, WithA(arc, 0.55f * fade));
	Quad(tWhite, W * 0.5f, H * 0.5f, W * 0.012f, H * 1.7f, 12, SColor(A(fade * in), 255, 240, 200));
	// portraits
	const float ph = H * 0.82f;
	DrawPortrait(PlayerPortrait(), W * 0.25f - (1 - in) * W * 0.5f, H * 0.95f, ph, SColor(A(fade), 255, 255, 255));
	DrawPortrait(OpponentPortrait(), W * 0.75f + (1 - in) * W * 0.5f, H * 0.95f, ph, SColor(A(fade), 255, 255, 255), true);
	// names
	std::wstring me = mainGame->ebNickName ? std::wstring(mainGame->ebNickName->getText()) : L"";
	if(me.empty())
		me = L"คุณ";
	const float nk = EaseOut(Seg(t, 0.2f, 0.35f));
	auto plate = [&](float cx, const std::wstring& name, SColor c, bool right) {
		const int y = static_cast<int>(H * 0.84f);
		const int w2 = static_cast<int>(W * 0.18f * nk);
		const int hh = static_cast<int>(18 * Scl());
		if(right)
			HRect(recti(static_cast<int>(cx) - w2, y - hh, static_cast<int>(cx) + w2, y + hh), WithA(c, 0.f), WithA(c, 0.95f * fade));
		else
			HRect(recti(static_cast<int>(cx) - w2, y - hh, static_cast<int>(cx) + w2, y + hh), WithA(c, 0.95f * fade), WithA(c, 0.f));
		Text(mainGame->guiFont, name, static_cast<int>(cx), y, SColor(A(fade * nk), 255, 255, 255));
	};
	plate(W * 0.25f, me, mine, false);
	plate(W * 0.75f, ch.opponent, arc, true);
	// skills under the names (Duel Links style)
	if(const auto* sk = gWorld.FindSkill(ch.player_skill))
		Text(mainGame->guiFont, L"SKILL: " + sk->name, static_cast<int>(W * 0.25f), static_cast<int>(H * 0.91f), SColor(A(fade * nk), 140, 220, 255));
	if(!ch.skill_label.empty())
		Text(mainGame->guiFont, ch.skill_label, static_cast<int>(W * 0.72f), static_cast<int>(H * 0.91f), SColor(A(fade * nk), 255, 150, 130));
	// VS
	const float vk = Seg(t, 0.22f, 0.3f);
	if(vk > 0) {
		const float flash = 1 - Seg(t, 0.22f, 0.4f);
		driver->draw2DRectangle(SColor(A(flash * 0.7f * fade), 255, 255, 255), recti(0, 0, ss.Width, ss.Height));
		Quad(gFx.RaysTexture(), W * 0.5f, H * 0.45f, H * 1.2f, H * 1.2f, Now() * 0.02f, SColor(A(0.5f * fade), 255, 210, 120), true);
		Quad(gFx.GlowTexture(), W * 0.5f, H * 0.45f, H * 0.6f, H * 0.6f, 0, SColor(A(0.9f * fade), 255, 200, 90), true);
		const SColor vs(A(fade), 255, 236, 150);
		Text(mainGame->lpcFont, L"V S", static_cast<int>(W * 0.5f), static_cast<int>(H * 0.45f), vs);
	}
	if(!ch.arc.empty())
		Text(mainGame->guiFont, ch.arc, static_cast<int>(W * 0.5f), static_cast<int>(H * 0.08f), SColor(A(fade * nk), 255, 220, 140));
}

void MdUi::DrawMatching() {
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float W = static_cast<float>(ss.Width), H = static_cast<float>(ss.Height);
	const SColor arc = ArcColor(gStory.DuelChapter().arc);
	Rect(recti(0, 0, ss.Width, ss.Height), Mix(SColor(255, 6, 8, 20), arc, 0.22f), SColor(255, 3, 3, 8));
	const float now = static_cast<float>(Now());
	Quad(gFx.RaysTexture(), W * 0.5f, H * 0.45f, H * 1.6f, H * 1.6f, now * 0.006f, WithA(arc, 0.18f), true);
	Quad(gFx.RingTexture(), W * 0.5f, H * 0.45f, H * 0.28f, H * 0.28f, now * 0.25f, SColor(220, 140, 200, 255), true);
	Quad(gFx.RingTexture(), W * 0.5f, H * 0.45f, H * 0.2f, H * 0.2f, -now * 0.4f, WithA(arc, 0.9f), true);
	Quad(gFx.GlowTexture(), W * 0.5f, H * 0.45f, H * 0.35f, H * 0.35f, 0, SColor(A(0.4f + 0.2f * std::sin(now * 0.005f)), 120, 180, 255), true);
	DrawPortrait(OpponentPortrait(), W * 0.8f, H, H * 0.7f, SColor(140, 160, 170, 200), true);
	DrawPortrait(PlayerPortrait(), W * 0.2f, H, H * 0.7f, SColor(140, 160, 170, 200));
	const int dots = static_cast<int>(now / 400.f) % 4;
	Text(mainGame->lpcFont, L"MATCHING", static_cast<int>(W * 0.5f), static_cast<int>(H * 0.68f), SColor(255, 230, 240, 255));
	Text(mainGame->guiFont, std::wstring(L"กำลังเตรียมการดวลกับ ") + gStory.DuelChapter().opponent + std::wstring(static_cast<size_t>(dots), L'.'),
		 static_cast<int>(W * 0.5f), static_cast<int>(H * 0.76f), SColor(255, 190, 210, 240));
}

void MdUi::DrawCoin(float t) {
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float W = static_cast<float>(ss.Width), H = static_cast<float>(ss.Height);
	const float fade = 1 - Seg(t, 0.9f, 1.f);
	driver->draw2DRectangle(SColor(A(0.6f * fade * Seg(t, 0, 0.08f)), 0, 0, 10), recti(0, 0, ss.Width, ss.Height));
	const int outcome = overlay_arg;
	const float fly = Seg(t, 0.05f, 0.62f);
	const float cy = H * 0.5f - std::sin(fly * PI) * H * 0.2f;
	const float spins = 7.f;
	const int final_face = outcome == 0 ? 0 : 1;
	float angle = EaseOut(fly) * spins * 2 * PI + (final_face ? PI : 0);
	if(t > 0.62f)
		angle = final_face ? PI : 0;
	const float c = std::cos(angle);
	const int face = c >= 0 ? 0 : 1;
	const float size = H * 0.24f;
	if(t > 0.62f) {
		const float b = 1 - Seg(t, 0.62f, 0.85f);
		Quad(gFx.RaysTexture(), W * 0.5f, cy, size * 3.2f, size * 3.2f, Now() * 0.03f, SColor(A(0.5f * fade), 255, 210, 120), true);
		Quad(gFx.GlowTexture(), W * 0.5f, cy, size * 2.6f * (1.2f - b * 0.2f), size * 2.6f, 0, SColor(A((0.4f + 0.6f * b) * fade), 255, 220, 140), true);
	}
	Quad(tCoin[face], W * 0.5f, cy, size * std::max(0.06f, std::fabs(c)), size, 0, SColor(A(fade), 255, 255, 255));
	Text(mainGame->guiFont, L"ทอยเหรียญ (Coin toss)", static_cast<int>(W * 0.5f), static_cast<int>(H * 0.18f), SColor(A(fade * Seg(t, 0, 0.1f)), 220, 230, 255));
	if(t > 0.64f) {
		const float k = EaseOut(Seg(t, 0.64f, 0.74f));
		std::wstring msg = outcome == 0 ? L"คุณชนะการทอยเหรียญ!" : outcome == 1 ? L"คู่ต่อสู้ชนะการทอยเหรียญ" : L"เสมอ! ทอยใหม่";
		const SColor col = outcome == 0 ? SColor(A(fade * k), 255, 225, 120) : SColor(A(fade * k), 200, 215, 255);
		Text(mainGame->lpcFont, msg, static_cast<int>(W * 0.5f), static_cast<int>(H * 0.74f), col);
	}
}

void MdUi::DrawStart(float t) {
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float W = static_cast<float>(ss.Width), H = static_cast<float>(ss.Height);
	const float in = EaseOut(Seg(t, 0, 0.18f));
	const float out = EaseIn(Seg(t, 0.82f, 1.f));
	const float a = 1 - out;
	const int bh = static_cast<int>(H * 0.13f * in);
	const int cy = static_cast<int>(H * 0.45f);
	Rect(recti(0, cy - bh, ss.Width, cy), SColor(A(0.9f * a), 10, 30, 80), SColor(A(0.9f * a), 20, 60, 140));
	Rect(recti(0, cy, ss.Width, cy + bh), SColor(A(0.9f * a), 20, 60, 140), SColor(A(0.9f * a), 10, 30, 80));
	HRect(recti(0, cy - bh - 2, ss.Width, cy - bh), SColor(0, 240, 200, 100), SColor(A(a), 240, 200, 100));
	HRect(recti(0, cy + bh, ss.Width, cy + bh + 2), SColor(A(a), 240, 200, 100), SColor(0, 240, 200, 100));
	const float slide = (1 - in) * W * 0.4f - out * W * 0.4f;
	Text(mainGame->lpcFont, L"DUEL START", static_cast<int>(W * 0.5f + slide), cy - static_cast<int>(bh * 0.2f), SColor(A(a), 255, 240, 190));
	Text(mainGame->guiFont, overlay_arg ? L"คุณเริ่มเล่นก่อน (You go first)" : L"คุณเริ่มเล่นทีหลัง (You go second)",
		 static_cast<int>(W * 0.5f - slide), cy + static_cast<int>(bh * 0.55f), SColor(A(a * in), 200, 230, 255));
}

void MdUi::DrawResult(float t) {
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float W = static_cast<float>(ss.Width), H = static_cast<float>(ss.Height);
	const int outcome = overlay_arg;
	const float fade = 1 - Seg(t, 0.92f, 1.f);
	const SColor main = outcome == 0 ? SColor(255, 255, 210, 90) : outcome == 1 ? SColor(255, 120, 150, 230) : SColor(255, 200, 200, 210);
	driver->draw2DRectangle(SColor(A(0.75f * fade * Seg(t, 0, 0.1f)), 2, 3, 10), recti(0, 0, ss.Width, ss.Height));
	if(outcome == 0)
		Quad(gFx.RaysTexture(), W * 0.5f, H * 0.42f, H * 1.7f, H * 1.7f, Now() * 0.01f, WithA(main, 0.45f * fade), true);
	Quad(gFx.GlowTexture(), W * 0.5f, H * 0.42f, W * 0.9f, H * 0.35f, 0, WithA(main, 0.55f * fade), true);
	// opponent portrait on the side
	DrawPortrait(OpponentPortrait(), W * 0.82f, H * 1.0f, H * 0.75f,
				 outcome == 0 ? SColor(A(fade * 0.6f), 120, 120, 140) : SColor(A(fade), 255, 255, 255), true);
	DrawPortrait(PlayerPortrait(), W * 0.18f, H * 1.0f, H * 0.75f,
				 outcome == 0 ? SColor(A(fade), 255, 255, 255) : SColor(A(fade * 0.6f), 120, 120, 140));
	const float k = EaseOut(Seg(t, 0.05f, 0.25f));
	const int bh = static_cast<int>(H * 0.1f * k);
	const int cy = static_cast<int>(H * 0.42f);
	HRect(recti(0, cy - bh, ss.Width / 2, cy + bh), SColor(0, 0, 0, 0), SColor(A(0.75f * fade), 8, 10, 30));
	HRect(recti(ss.Width / 2, cy - bh, ss.Width, cy + bh), SColor(A(0.75f * fade), 8, 10, 30), SColor(0, 0, 0, 0));
	HRect(recti(0, cy - bh - 2, ss.Width / 2, cy - bh), SColor(0, 0, 0, 0), WithA(main, fade));
	HRect(recti(ss.Width / 2, cy - bh - 2, ss.Width, cy - bh), WithA(main, fade), SColor(0, 0, 0, 0));
	HRect(recti(0, cy + bh, ss.Width / 2, cy + bh + 2), SColor(0, 0, 0, 0), WithA(main, fade));
	HRect(recti(ss.Width / 2, cy + bh, ss.Width, cy + bh + 2), WithA(main, fade), SColor(0, 0, 0, 0));
	const wchar_t* big = outcome == 0 ? L"V I C T O R Y" : outcome == 1 ? L"L O S E" : L"D R A W";
	const wchar_t* sub = outcome == 0 ? L"คุณชนะการดวล!" : outcome == 1 ? L"คุณแพ้... ลองใหม่อีกครั้ง" : L"เสมอ";
	Text(mainGame->lpcFont, big, static_cast<int>(W * 0.5f), cy - static_cast<int>(bh * 0.15f), WithA(main, fade * k));
	Text(mainGame->guiFont, sub, static_cast<int>(W * 0.5f), cy + static_cast<int>(bh * 0.6f), SColor(A(fade * Seg(t, 0.2f, 0.35f)), 230, 235, 255));
}

void MdUi::DrawSpeech() {
	if(speech.empty())
		return;
	if(!gStory.IsActive() || !mainGame->dInfo.isInDuel) {
		speech.clear();
		return;
	}
	if(overlay == Overlay::VERSUS || overlay == Overlay::COIN)
		return;
	const uint32_t now = Now();
	auto& sp = speech.front();
	if(!sp.start)
		sp.start = now;
	const float t = (now - sp.start) / static_cast<float>(sp.duration);
	if(t >= 1.f) {
		speech.pop_front();
		return;
	}
	auto* driver = mainGame->driver;
	const auto ss = driver->getScreenSize();
	const float s = Scl();
	const float in = EaseOut(Seg(t * sp.duration, 0, 260));
	const float a = 1 - Seg(t * sp.duration, sp.duration - 300.f, static_cast<float>(sp.duration));
	const int slide = static_cast<int>((1 - in) * 120 * s);
	const auto& ch = gStory.DuelChapter();
	const SColor arc = ArcColor(ch.arc);
	// portrait bust in a frame (top right, under the LP bar)
	const int fw = static_cast<int>(92 * s), fh = static_cast<int>(92 * s);
	const int fx = static_cast<int>(ss.Width) - fw - static_cast<int>(16 * s) + slide;
	const int fy = static_cast<int>(64 * s);
	const recti frame(fx, fy, fx + fw, fy + fh);
	Rect(frame, WithA(Mix(SColor(255, 10, 12, 30), arc, 0.4f), a), WithA(SColor(255, 6, 6, 16), a));
	if(auto* tex = OpponentPortrait()) {
		const auto ts = tex->getOriginalSize();
		// head & shoulders: upper part of the picture
		const recti src(static_cast<int>(ts.Width * 0.12f), static_cast<int>(ts.Height * 0.12f), static_cast<int>(ts.Width * 0.88f),
						static_cast<int>(ts.Height * 0.12f + ts.Width * 0.76f));
		const SColor c(A(a), 255, 255, 255);
		const SColor cols[4] = { c, c, c, c };
		driver->draw2DImage(tex, frame, src, nullptr, cols, true);
	}
	driver->draw2DRectangleOutline(frame, WithA(SColor(255, 240, 200, 100), a));
	// bubble
	auto* font = mainGame->guiFont;
	const int maxw = static_cast<int>(300 * s);
	auto lines = Wrap(font, sp.text, maxw);
	if(lines.size() > 4)
		lines.resize(4);
	int tw = 0, lh = static_cast<int>(font->getDimensionustring(irr::core::ustring(L"ก")).Height + 2 * s);
	for(auto& l : lines)
		tw = std::max(tw, static_cast<int>(font->getDimensionustring(irr::core::ustring(l.c_str())).Width));
	const int pad = static_cast<int>(10 * s);
	const int bw = tw + pad * 2, bh = static_cast<int>(lines.size()) * lh + pad * 2;
	const int bx1 = fx - static_cast<int>(10 * s), bx0 = bx1 - bw;
	const int by0 = fy + static_cast<int>(6 * s);
	const recti bubble(bx0, by0, bx1, by0 + bh);
	Rect(bubble, SColor(A(0.94f * a), 250, 250, 255), SColor(A(0.94f * a), 220, 228, 245));
	driver->draw2DRectangleOutline(bubble, WithA(arc, a));
	// tail
	Quad(tWhite, static_cast<float>(bx1), static_cast<float>(by0 + static_cast<int>(18 * s)), 12 * s, 12 * s, 45, SColor(A(0.94f * a), 245, 245, 252));
	for(size_t i = 0; i < lines.size(); i++)
		Text(font, lines[i], bx0 + pad, by0 + pad + lh * static_cast<int>(i) + lh / 2, SColor(A(a), 20, 24, 40), -1, false);
	Text(font, ch.opponent, fx + fw / 2, fy + fh + static_cast<int>(10 * s), WithA(SColor(255, 255, 230, 160), a));
}

/* -------------------------------------------------------------- triggers */

void MdUi::ResetDuel() {
	clock_deadline = 0;
	clock_fired = false;
	versus_shown = false;
	overlay = Overlay::NONE;
	speech.clear();
	right_name.clear();
	last_speaker.clear();
}

int MdUi::Versus() {
	versus_shown = true;
	overlay = Overlay::VERSUS;
	overlay_start = Now();
	overlay_len = 2700;
	return 2700 * 60 / 1000;
}

int MdUi::CoinToss(int outcome) {
	overlay = Overlay::COIN;
	overlay_arg = outcome;
	overlay_start = Now();
	overlay_len = outcome == 2 ? 1700 : 2400;
	return static_cast<int>(overlay_len * 60 / 1000);
}

int MdUi::DuelStart(bool going_first) {
	clock_fired = false;
	clock_deadline = 0;
	if(gStory.IsActive() && gStory.DuelChapter().time_limit_ms > 0)
		clock_deadline = Now() + 1700 + gStory.DuelChapter().time_limit_ms;
	overlay = Overlay::START;
	overlay_arg = going_first ? 1 : 0;
	overlay_start = Now();
	overlay_len = 1700;
	return 1700 * 60 / 1000;
}

int MdUi::Result(int outcome) {
	overlay = Overlay::RESULT;
	overlay_arg = outcome;
	overlay_start = Now();
	overlay_len = 3200;
	return 3200 * 60 / 1000;
}

void MdUi::Speak(const std::wstring& text) {
	if(text.empty())
		return;
	Speech sp;
	sp.text = text;
	sp.duration = static_cast<uint32_t>(std::min<size_t>(5500, 2200 + text.size() * 45));
	if(speech.size() > 3)
		speech.pop_front();
	speech.push_back(sp);
}

}
