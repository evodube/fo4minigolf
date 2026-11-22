// The HUD, drawn through F4SE Menu Framework when it's installed, in Playable Pool's green terminal style: the hole
// (its number, name and par) and your strokes, the power meter while you charge a putt, the big banners (the hole,
// a birdie, the water) and the scorecard. Text uses the two pre-rendered fonts in Data\F4SE\Plugins\fo4minigolf\hud
// (HudFont.h, from Amusement Games); the panels are drawn shapes. Laid out on a 1280x720 design screen, scaled to the
// real one.

#include "Hud.h"

#include "Golf.h"
#include "F4SEMenuFramework.h"
#include "HudFont.h"

namespace mg::Hud
{
	namespace
	{
		using namespace ImGuiMCP;
		namespace DL = ImGuiMCP::ImDrawListManager;

		constexpr ImU32 Rgba(int r, int g, int b, float a = 1.0f)
		{
			return static_cast<ImU32>(r) | (static_cast<ImU32>(g) << 8) | (static_cast<ImU32>(b) << 16) |
			       (static_cast<ImU32>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
		}
		ImU32 Fade(ImU32 a_col, float a_alpha)
		{
			const float a = static_cast<float>(a_col >> 24) / 255.0f * std::clamp(a_alpha, 0.0f, 1.0f);
			return (a_col & 0x00FFFFFFu) | (static_cast<ImU32>(a * 255.0f + 0.5f) << 24);
		}

		constexpr ImU32 kBright = Rgba(166, 255, 166);
		constexpr ImU32 kText = Rgba(134, 255, 134);
		constexpr ImU32 kDim = Rgba(95, 174, 102);
		constexpr ImU32 kOff = Rgba(79, 143, 86);
		constexpr ImU32 kGlow = Rgba(80, 255, 110, 0.55f);
		constexpr ImU32 kRedText = Rgba(255, 122, 110);
		constexpr ImU32 kRedGlow = Rgba(255, 60, 40, 0.6f);
		constexpr ImU32 kPanel = Rgba(6, 20, 9, 0.78f);
		constexpr ImU32 kEdge = Rgba(80, 200, 100, 0.55f);

		// ------------------------------------------------------------ fonts

		struct Tex
		{
			ImTextureID id{ nullptr };
			bool        tried{ false };
		};
		std::unordered_map<std::string, Tex> g_textures;
		bool                                 g_artMissing{ false };

		ImTextureID Texture(const char* a_file)
		{
			auto& t = g_textures[a_file];
			if (!t.tried) {
				t.tried = true;
				for (const char* dir : { "Data\\F4SE\\Plugins\\fo4minigolf\\hud\\", "F4SE\\Plugins\\fo4minigolf\\hud\\" }) {
					t.id = F4SEMenuFramework::LoadTexture(std::string(dir) + a_file);
					if (t.id) {
						break;
					}
				}
				if (!t.id) {
					g_artMissing = true;
					log::warn("HUD: could not load {}; the plain HUD is used", a_file);
				}
			}
			return t.id;
		}

		struct View
		{
			ImDrawList* dl;
			ImVec2      origin;
			float       s;
			float       w, h;

			ImVec2 P(float a_x, float a_y) const { return { origin.x + a_x * s, origin.y + a_y * s }; }
		};

		const HudFont::Glyph* FindGlyph(const HudFont::Font& a_f, std::uint32_t a_code)
		{
			for (int i = 0; i < a_f.count; ++i) {
				if (a_f.glyphs[i].code == a_code) {
					return &a_f.glyphs[i];
				}
			}
			return a_code != '?' ? FindGlyph(a_f, '?') : nullptr;
		}

		std::vector<std::uint32_t> Decode(std::string_view a_text)
		{
			std::vector<std::uint32_t> out;
			for (std::size_t i = 0; i < a_text.size();) {
				const auto    c = static_cast<unsigned char>(a_text[i]);
				std::uint32_t cp = c;
				int           extra = 0;
				if (c >= 0xF0) {
					cp = c & 0x07;
					extra = 3;
				} else if (c >= 0xE0) {
					cp = c & 0x0F;
					extra = 2;
				} else if (c >= 0xC0) {
					cp = c & 0x1F;
					extra = 1;
				}
				++i;
				for (int k = 0; k < extra && i < a_text.size(); ++k, ++i) {
					cp = (cp << 6) | (static_cast<unsigned char>(a_text[i]) & 0x3F);
				}
				out.push_back(cp);
			}
			return out;
		}

		float TextWidth(const HudFont::Font& a_f, std::string_view a_text, float a_size, float a_spacing)
		{
			const float k = a_size / a_f.size;
			float       w = 0.0f;
			const auto  codes = Decode(a_text);
			for (std::size_t i = 0; i < codes.size(); ++i) {
				if (const auto* g = FindGlyph(a_f, codes[i])) {
					w += g->advance * k + (i + 1 < codes.size() ? a_spacing : 0.0f);
				}
			}
			return w;
		}

		enum class Align
		{
			kLeft,
			kCentre,
			kRight
		};

		void Print(const View& a_v, const HudFont::Font& a_f, std::string_view a_text, float a_x, float a_y, float a_size, ImU32 a_col,
			ImU32 a_glow = 0, float a_spacing = 0.0f, Align a_align = Align::kLeft)
		{
			auto sharp = Texture(a_f.file);
			auto glow = (a_glow >> 24) ? Texture(a_f.glowFile) : nullptr;
			if (!sharp) {
				return;
			}
			const float k = a_size / a_f.size;
			const float width = TextWidth(a_f, a_text, a_size, a_spacing);
			float       x = a_align == Align::kLeft ? a_x : a_align == Align::kCentre ? a_x - width * 0.5f : a_x - width;
			for (const auto code : Decode(a_text)) {
				const auto* g = FindGlyph(a_f, code);
				if (!g) {
					continue;
				}
				if (code != ' ') {
					const ImVec2 p0 = a_v.P(x + g->xoff * k, a_y + g->yoff * k);
					const ImVec2 p1 = a_v.P(x + (g->xoff + g->w) * k, a_y + (g->yoff + g->h) * k);
					const ImVec2 uv0{ g->x / a_f.atlasW, g->y / a_f.atlasH };
					const ImVec2 uv1{ (g->x + g->w) / a_f.atlasW, (g->y + g->h) / a_f.atlasH };
					if (glow) {
						DL::AddImage(a_v.dl, glow, p0, p1, uv0, uv1, a_glow);
					}
					DL::AddImage(a_v.dl, sharp, p0, p1, uv0, uv1, a_col);
				}
				x += g->advance * k + a_spacing;
			}
		}

		const HudFont::Font& Title() { return HudFont::k_title; }
		const HudFont::Font& Mono() { return HudFont::k_mono; }

		// ------------------------------------------------------------ shapes

		void Chamfer(const View& a_v, float a_x, float a_y, float a_w, float a_h, float a_c, ImU32 a_fill, ImU32 a_edge)
		{
			const std::array<ImVec2, 8> pts{ a_v.P(a_x + a_c, a_y), a_v.P(a_x + a_w - a_c, a_y), a_v.P(a_x + a_w, a_y + a_c),
				a_v.P(a_x + a_w, a_y + a_h - a_c), a_v.P(a_x + a_w - a_c, a_y + a_h), a_v.P(a_x + a_c, a_y + a_h), a_v.P(a_x, a_y + a_h - a_c),
				a_v.P(a_x, a_y + a_c) };
			DL::AddConvexPolyFilled(a_v.dl, pts.data(), static_cast<int>(pts.size()), a_fill);
			if (a_edge >> 24) {
				DL::AddPolyline(a_v.dl, pts.data(), static_cast<int>(pts.size()), a_edge, ImDrawFlags_Closed, std::max(1.0f, 1.2f * a_v.s));
			}
		}


		// ------------------------------------------------------------ the pieces

		std::string Signed(int a_n) { return a_n == 0 ? std::string("E") : std::format("{}{}", a_n > 0 ? "+" : "", a_n); }

		// top left: the hole, its name and par, your strokes on it
		void HolePanel(const View& a_v, const Golf::HudState& a_st)
		{
			const float x = 30.0f, y = 26.0f, w = 330.0f, h = 108.0f;
			Chamfer(a_v, x, y, w, h, 10.0f, kPanel, kEdge);
			Print(a_v, Mono(), std::format("HOLE {} OF 18", a_st.hole), x + 16, y + 10, 13, kDim, 0, 2.0f);
			if (!a_st.opponent.empty()) {
				Print(a_v, Mono(), a_st.oppTurn ? std::format("{} PUTTING", a_st.opponent) : std::string("YOUR PUTT"), x + w - 16, y + 10, 13,
					a_st.oppTurn ? kText : kBright, 0, 1.0f, Align::kRight);
			}
			Print(a_v, Title(), a_st.name, x + 16, y + 28, 28, kBright, kGlow, 1.0f);
			Print(a_v, Mono(), std::format("PAR {}", a_st.par), x + 16, y + 76, 16, kText, 0, 2.0f);
			Print(a_v, Mono(), std::format("STROKE {}", a_st.stroke), x + w - 16, y + 76, 16, kBright, kGlow, 2.0f, Align::kRight);
		}

		// top right: the round so far
		void TotalPanel(const View& a_v, const Golf::HudState& a_st)
		{
			if (!a_st.opponent.empty()) {
				// 1v1: you and them, each against par so far and in strokes
				const float w = 300.0f, h = 92.0f, x = a_v.w - w - 30.0f, y = 26.0f;
				Chamfer(a_v, x, y, w, h, 10.0f, kPanel, kEdge);
				Print(a_v, Mono(), "1V1", x + 16, y + 10, 13, kDim, 0, 2.0f);
				const auto row = [&](const std::string& a_name, int a_total, int a_par, float a_y, bool a_on) {
					const int diff = a_total - a_par;
					Print(a_v, Mono(), a_name, x + 16, a_y, 15, a_on ? kBright : kText, a_on ? kGlow : 0, 1.0f);
					Print(a_v, Mono(), std::format("{}", a_total), x + w - 90, a_y, 15, kText, 0, 1.0f, Align::kRight);
					Print(a_v, Mono(), Signed(diff), x + w - 16, a_y, 15, diff <= 0 ? kBright : kRedText, 0, 1.0f, Align::kRight);
				};
				std::string them = a_st.opponent;
				if (them.size() > 16) {
					them = them.substr(0, 16);
				}
				row("YOU", a_st.total, a_st.parSoFar, y + 34, !a_st.oppTurn);
				row(them, a_st.oppTotal, a_st.oppParSoFar, y + 60, a_st.oppTurn);
				return;
			}
			const float w = 190.0f, h = 78.0f, x = a_v.w - w - 30.0f, y = 26.0f;
			Chamfer(a_v, x, y, w, h, 10.0f, kPanel, kEdge);
			Print(a_v, Mono(), "ROUND", x + 16, y + 10, 13, kDim, 0, 2.0f);
			const int diff = a_st.total - a_st.parSoFar;
			Print(a_v, Title(), Signed(diff), x + 16, y + 28, 34, diff <= 0 ? kBright : kRedText, diff <= 0 ? kGlow : kRedGlow, 1.0f);
			Print(a_v, Mono(), std::format("{} STROKES", a_st.total), x + w - 16, y + 44, 14, kText, 0, 1.0f, Align::kRight);
		}

		// bottom middle: the putt's power while you pull back
		void Meter(const View& a_v, const Golf::HudState& a_st)
		{
			if (a_st.power < 0.0f) {
				return;
			}
			const float w = 360.0f, x = a_v.w * 0.5f - w * 0.5f, y = a_v.h - 120.0f;
			Chamfer(a_v, x - 18, y - 30, w + 36, 70.0f, 10.0f, kPanel, kEdge);
			Print(a_v, Mono(), "POWER", x, y - 22, 12, kDim, 0, 2.0f);
			Print(a_v, Mono(), std::format("{:.0f}%", a_st.power * 100.0f), x + w, y - 22, 12, kText, 0, 1.0f, Align::kRight);
			DL::AddRect(a_v.dl, a_v.P(x, y), a_v.P(x + w, y + 18), kEdge, 0.0f, 0, std::max(1.0f, 1.2f * a_v.s));
			const float p = std::clamp(a_st.power, 0.0f, 1.0f);
			const ImU32 col = p > 0.85f ? kRedText : kText;
			DL::AddRectFilled(a_v.dl, a_v.P(x + 2, y + 2), a_v.P(x + 2 + (w - 4) * p, y + 16), col, 0.0f, 0);
			for (int k = 1; k < 4; ++k) {
				const float tx = x + w * k / 4.0f;
				DL::AddLine(a_v.dl, a_v.P(tx, y + 12), a_v.P(tx, y + 18), kDim, std::max(1.0f, a_v.s));
			}
		}

		// the middle: the hole's title as you reach it, a birdie, the water
		void BannerPanel(const View& a_v, const Golf::HudState& a_st)
		{
			constexpr float kShow = 2.6f;
			if (a_st.banner.empty() || a_st.bannerAge > kShow) {
				return;
			}
			const float a = std::clamp((kShow - a_st.bannerAge) / 0.5f, 0.0f, 1.0f) * std::clamp(a_st.bannerAge / 0.15f, 0.0f, 1.0f);
			const ImU32 main = a_st.good ? kBright : kRedText;
			const ImU32 glow = a_st.good ? kGlow : kRedGlow;
			const float y = a_v.h * 0.28f;
			Print(a_v, Title(), a_st.banner, a_v.w * 0.5f, y, 54, Fade(main, a), Fade(glow, a), 2.0f, Align::kCentre);
			if (!a_st.bannerSub.empty()) {
				Print(a_v, Mono(), a_st.bannerSub, a_v.w * 0.5f, y + 66, 18, Fade(kText, a), 0, 3.0f, Align::kCentre);
			}
		}

		// the scorecard: every hole's par and your strokes on it (after a hole, and the whole of it at the end)
		void Card(const View& a_v, const Golf::HudState& a_st)
		{
			if (!a_st.showCard || a_st.card.empty()) {
				return;
			}
			const int   n = static_cast<int>(a_st.card.size());
			const float cell = 50.0f, label = 80.0f, half = static_cast<float>((n + 1) / 2);
			const bool  versus = !a_st.opponent.empty() && a_st.oppCard.size() == a_st.card.size();
			const int   rows = versus ? 4 : 3;
			const float w = label + 40.0f * versus + cell * (half + 1.0f) + 28.0f, rowH = 26.0f;
			const float h = 36.0f + rowH * rows * 2.0f, x = a_v.w * 0.5f - w * 0.5f, y = a_v.h - h - 150.0f;
			const float lw = label + 40.0f * versus;  // (the labels' column: wider for a name)
			std::string them = a_st.opponent;
			if (them.size() > 10) {
				them = them.substr(0, 10);
			}
			Chamfer(a_v, x, y, w, h, 12.0f, kPanel, kEdge);
			Print(a_v, Mono(), a_st.finished ? "FINAL SCORECARD" : "SCORECARD", x + 14, y + 10, 13, kDim, 0, 2.0f);
			for (int half_ = 0; half_ < 2; ++half_) {
				const int   first = half_ * static_cast<int>(half);
				const int   last = std::min(n, first + static_cast<int>(half));
				const float ry = y + 34.0f + half_ * rowH * static_cast<float>(rows);
				Print(a_v, Mono(), "HOLE", x + 14, ry, 13, kDim, 0, 1.0f);
				Print(a_v, Mono(), "PAR", x + 14, ry + rowH, 13, kDim, 0, 1.0f);
				Print(a_v, Mono(), "YOU", x + 14, ry + rowH * 2, 13, kText, 0, 1.0f);
				if (versus) {
					Print(a_v, Mono(), them, x + 14, ry + rowH * 3, 13, kText, 0, 1.0f);
				}
				int par = 0, you = 0, theirs = 0;
				for (int i = first; i < last; ++i) {
					const float cx = x + lw + cell * static_cast<float>(i - first) + cell * 0.5f;
					if (versus && a_st.oppCard[i] > 0) {
						const int   d = a_st.oppCard[i] - a_st.pars[i];
						const ImU32 col = d < 0 ? kBright : d == 0 ? kText : kRedText;
						Print(a_v, Mono(), std::format("{}", a_st.oppCard[i]), cx, ry + rowH * 3, 15, col, d < 0 ? kGlow : 0, 0.0f, Align::kCentre);
						theirs += a_st.oppCard[i];
					}
					const bool  current = i + 1 == a_st.hole;
					Print(a_v, Mono(), std::format("{}", i + 1), cx, ry, 13, current ? kBright : kDim, 0, 0.0f, Align::kCentre);
					Print(a_v, Mono(), std::format("{}", a_st.pars[i]), cx, ry + rowH, 14, kText, 0, 0.0f, Align::kCentre);
					par += a_st.pars[i];
					if (a_st.card[i] > 0) {
						const int   d = a_st.card[i] - a_st.pars[i];
						const ImU32 col = d < 0 ? kBright : d == 0 ? kText : kRedText;
						Print(a_v, Mono(), std::format("{}", a_st.card[i]), cx, ry + rowH * 2, 15, col, d < 0 ? kGlow : 0, 0.0f, Align::kCentre);
						you += a_st.card[i];
					}
				}
				const float tx = x + lw + cell * half + cell * 0.5f;
				Print(a_v, Mono(), half_ == 0 ? "OUT" : "IN", tx, ry, 13, kDim, 0, 0.0f, Align::kCentre);
				Print(a_v, Mono(), std::format("{}", par), tx, ry + rowH, 14, kText, 0, 0.0f, Align::kCentre);
				if (you > 0) {
					Print(a_v, Mono(), std::format("{}", you), tx, ry + rowH * 2, 15, kBright, kGlow, 0.0f, Align::kCentre);
				}
				if (theirs > 0) {
					Print(a_v, Mono(), std::format("{}", theirs), tx, ry + rowH * 3, 15, kBright, kGlow, 0.0f, Align::kCentre);
				}
			}
		}

		// the aim line, green, fading as it goes (Bowling's)
		void Guide(ImGuiViewport* a_viewport, const View& a_v, const Golf::HudState& a_st)
		{
			if (a_st.guide.size() < 2) {
				return;
			}
			const auto at = [&](const std::array<float, 2>& a_p) {
				return ImVec2{ a_viewport->Pos.x + a_p[0] * a_viewport->Size.x, a_viewport->Pos.y + a_p[1] * a_viewport->Size.y };
			};
			const std::size_t n = a_st.guide.size();
			for (std::size_t i = 1; i < n; ++i) {
				const float  fade = 1.0f - static_cast<float>(i) / static_cast<float>(n);
				const ImVec2 a = at(a_st.guide[i - 1]);
				const ImVec2 b = at(a_st.guide[i]);
				DL::AddLine(a_v.dl, a, b, Fade(kGlow, 0.45f * fade), 7.0f * a_v.s);  // (a soft glow under it)
				DL::AddLine(a_v.dl, a, b, Fade(kBright, 0.95f * fade + 0.05f), 2.5f * a_v.s);
			}
		}

		// bottom left: the keys
		void Keys(const View& a_v, const Golf::HudState& a_st)
		{
			if (a_st.keys.empty()) {
				return;
			}
			Print(a_v, Mono(), a_st.keys, 30.0f, a_v.h - 34.0f, 12, Fade(kDim, 0.9f), 0, 1.0f);
		}

		// hold activate on the course: who to play. A compact panel left of the crosshair, sized to what's in it (the
		// game's own prompt is to the right of the crosshair: kept clear of it)
		void PickList(ImGuiViewport* a_viewport, const Golf::HudState& a_st)
		{
			View v;
			v.dl = GetBackgroundDrawList();
			if (!v.dl) {
				return;
			}
			v.s = std::max(0.1f, a_viewport->Size.y / 720.0f);
			v.origin = a_viewport->Pos;
			v.w = a_viewport->Size.x / v.s;
			v.h = a_viewport->Size.y / v.s;
			constexpr float kTitle = 10.0f, kName = 14.0f, kHint = 9.0f, kRow = 18.0f, kPad = 12.0f, kArrow = 14.0f;
			// the hint: one line per part ("UP/DOWN: PICK", "F ON THE COURSE: PLAY")
			std::vector<std::string> hints;
			for (std::size_t at = 0; at <= a_st.pickHint.size();) {
				const std::size_t dot = a_st.pickHint.find(" · ", at);
				hints.push_back(a_st.pickHint.substr(at, dot == std::string::npos ? std::string::npos : dot - at));
				if (dot == std::string::npos) {
					break;
				}
				at = dot + std::string_view(" · ").size();
			}
			float inner = TextWidth(Mono(), "WHO PLAYS?", kTitle, 2.0f);
			for (const auto& n : a_st.pick) {
				inner = std::max(inner, kArrow + TextWidth(Title(), n, kName, 1.0f));
			}
			for (const auto& hnt : hints) {
				inner = std::max(inner, TextWidth(Mono(), hnt, kHint, 1.0f));
			}
			const float w = inner + kPad * 2.0f;
			const float h = kPad + 16.0f + kRow * static_cast<float>(a_st.pick.size()) + 6.0f + 13.0f * static_cast<float>(hints.size()) + kPad * 0.6f;
			const float x0 = v.w * 0.5f - 36.0f - w;  // (its right edge a little left of the crosshair)
			const float y0 = v.h * 0.5f - h * 0.5f;
			Chamfer(v, x0, y0, w, h, 6.0f, kPanel, kEdge);
			const float x = x0 + kPad;
			float       y = y0 + kPad;
			Print(v, Mono(), "WHO PLAYS?", x, y, kTitle, kDim, 0, 2.0f);
			y += 16.0f;
			for (std::size_t i = 0; i < a_st.pick.size(); ++i) {
				const bool on = static_cast<int>(i) == a_st.pickSel;
				if (on) {
					DL::AddTriangleFilled(v.dl, v.P(x, y + 4), v.P(x + 7, y + 8.5f), v.P(x, y + 13), kText);
				}
				Print(v, Title(), a_st.pick[i], x + kArrow, y, kName, on ? kBright : kOff, on ? kGlow : 0, 1.0f);
				y += kRow;
			}
			y += 6.0f;
			for (const auto& hnt : hints) {
				Print(v, Mono(), hnt, x, y, kHint, kDim, 0, 1.0f);
				y += 13.0f;
			}
		}

		void __stdcall Draw()
		{
			const auto st = Golf::Get().Hud();
			auto*      viewport = GetMainViewport();
			if (viewport && !st.playing && !st.pick.empty()) {
				Texture(Title().file);
				if (!g_artMissing) {
					PickList(viewport, st);
				}
				return;
			}
			if (!viewport || !st.playing) {
				return;
			}
			Texture(Title().file);
			if (g_artMissing) {
				return;
			}
			View v;
			v.dl = GetBackgroundDrawList();
			if (!v.dl) {
				return;
			}
			v.s = std::max(0.1f, viewport->Size.y / 720.0f);
			v.origin = viewport->Pos;
			v.w = viewport->Size.x / v.s;
			v.h = viewport->Size.y / v.s;
			Guide(viewport, v, st);
			HolePanel(v, st);
			TotalPanel(v, st);
			Card(v, st);
			BannerPanel(v, st);
			Meter(v, st);
			Keys(v, st);
		}

		F4SEMenuFramework::Model::HudElement* g_element{ nullptr };
	}

	bool Showing()
	{
		return g_element && !g_artMissing;
	}

	void Install()
	{
		if (g_element) {
			return;
		}
		if (!F4SEMenuFramework::IsInstalled()) {
			log::warn("F4SE Menu Framework not found: no HUD on screen (results come as notifications)");
			return;
		}
		g_element = F4SEMenuFramework::AddHudElement(Draw);
		log::info("HUD element registered");
	}
}
