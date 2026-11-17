#include "headers/includes.h"
#include "../Menu/Menu.h"
#include "../Menu/Config.h"
#include "headers/syndicate_logo.h"

// Menu/Hooks.cpp: one line on whether stream proof is working right now.
const char* StreamProofStatus();

#include <cfloat>
#include <cmath>
#include <d3d11.h>

void LoadSyndicateLogo(ID3D11Device* Device) {
	static ID3D11ShaderResourceView* Logo = nullptr;
	if (!Device || Logo)
		return;
	D3D11_TEXTURE2D_DESC Desc{};
	Desc.Width = (UINT)kSyndicateLogoW;
	Desc.Height = (UINT)kSyndicateLogoH;
	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA Upload{};
	Upload.pSysMem = kSyndicateLogoRgba;
	Upload.SysMemPitch = (UINT)kSyndicateLogoW * 4;
	ID3D11Texture2D* Texture = nullptr;
	if (FAILED(Device->CreateTexture2D(&Desc, &Upload, &Texture)) || !Texture)
		return;
	D3D11_SHADER_RESOURCE_VIEW_DESC View{};
	View.Format = Desc.Format;
	View.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	View.Texture2D.MipLevels = 1;
	if (SUCCEEDED(Device->CreateShaderResourceView(Texture, &View, &Logo)) && Logo)
		var->gui.texture = (ImTextureID)Logo;
	Texture->Release();
}

static ImColor ThemeRgb(int r, int g, int b, int a = 255)
{
	return ImColor(r, g, b, a);
}

static void ApplyMenuTheme()
{
	dropdown_t* theme = cfg->fill<dropdown_t>("Menu theme");
	const int id = theme ? theme->callback : 0;

	switch (id)
	{
	case 1:
		clr->accent = ThemeRgb(196, 62, 68);
		clr->window.background = ThemeRgb(17, 36, 28);
		clr->window.sub_background = ThemeRgb(12, 26, 21);
		clr->window.stroke = ThemeRgb(138, 108, 58);
		clr->window.section = ThemeRgb(26, 48, 38);
		clr->widgets.background = ThemeRgb(20, 40, 32);
		clr->widgets.sub_background = ThemeRgb(132, 42, 48);
		clr->widgets.text = ThemeRgb(236, 232, 220);
		clr->widgets.text_inactive = ThemeRgb(154, 138, 104);
		clr->widgets.stroke = ThemeRgb(92, 74, 44);
		clr->widgets.sub_stroke = ThemeRgb(168, 58, 64);
		break;
	case 2:
		clr->accent = ThemeRgb(255, 122, 24);
		clr->window.background = ThemeRgb(12, 6, 14);
		clr->window.sub_background = ThemeRgb(22, 10, 26);
		clr->window.stroke = ThemeRgb(64, 28, 12);
		clr->window.section = ThemeRgb(28, 12, 32);
		clr->widgets.background = ThemeRgb(26, 12, 18);
		clr->widgets.sub_background = ThemeRgb(92, 38, 8);
		clr->widgets.text = ThemeRgb(255, 236, 210);
		clr->widgets.text_inactive = ThemeRgb(168, 96, 48);
		clr->widgets.stroke = ThemeRgb(72, 32, 14);
		clr->widgets.sub_stroke = ThemeRgb(140, 58, 18);
		break;
	case 3:
		clr->accent = ThemeRgb(244, 143, 177);
		clr->window.background = ThemeRgb(18, 22, 28);
		clr->window.sub_background = ThemeRgb(24, 30, 38);
		clr->window.stroke = ThemeRgb(72, 118, 102);
		clr->window.section = ThemeRgb(32, 42, 52);
		clr->widgets.background = ThemeRgb(28, 38, 46);
		clr->widgets.sub_background = ThemeRgb(92, 156, 132);
		clr->widgets.text = ThemeRgb(255, 248, 252);
		clr->widgets.text_inactive = ThemeRgb(176, 168, 214);
		clr->widgets.stroke = ThemeRgb(86, 132, 118);
		clr->widgets.sub_stroke = ThemeRgb(186, 168, 232);
		break;
	case 4:
		clr->accent = ThemeRgb(232, 64, 109);
		clr->window.background = ThemeRgb(18, 6, 12);
		clr->window.sub_background = ThemeRgb(28, 10, 18);
		clr->window.stroke = ThemeRgb(92, 28, 46);
		clr->window.section = ThemeRgb(36, 12, 22);
		clr->widgets.background = ThemeRgb(34, 12, 20);
		clr->widgets.sub_background = ThemeRgb(118, 32, 56);
		clr->widgets.text = ThemeRgb(255, 232, 240);
		clr->widgets.text_inactive = ThemeRgb(196, 112, 138);
		clr->widgets.stroke = ThemeRgb(98, 32, 50);
		clr->widgets.sub_stroke = ThemeRgb(168, 64, 92);
		break;
	case 5: // Midnight
		clr->accent = ThemeRgb(88, 166, 255);
		clr->window.background = ThemeRgb(9, 12, 22);
		clr->window.sub_background = ThemeRgb(12, 16, 28);
		clr->window.stroke = ThemeRgb(26, 34, 56);
		clr->window.section = ThemeRgb(16, 22, 38);
		clr->widgets.background = ThemeRgb(14, 19, 33);
		clr->widgets.sub_background = ThemeRgb(30, 52, 92);
		clr->widgets.text = ThemeRgb(232, 240, 255);
		clr->widgets.text_inactive = ThemeRgb(86, 104, 140);
		clr->widgets.stroke = ThemeRgb(26, 34, 56);
		clr->widgets.sub_stroke = ThemeRgb(40, 58, 96);
		break;
	case 6: // Crimson
		clr->accent = ThemeRgb(232, 52, 64);
		clr->window.background = ThemeRgb(16, 9, 10);
		clr->window.sub_background = ThemeRgb(22, 12, 14);
		clr->window.stroke = ThemeRgb(52, 20, 24);
		clr->window.section = ThemeRgb(28, 14, 16);
		clr->widgets.background = ThemeRgb(26, 13, 15);
		clr->widgets.sub_background = ThemeRgb(96, 24, 32);
		clr->widgets.text = ThemeRgb(255, 236, 236);
		clr->widgets.text_inactive = ThemeRgb(138, 84, 88);
		clr->widgets.stroke = ThemeRgb(52, 20, 24);
		clr->widgets.sub_stroke = ThemeRgb(112, 32, 40);
		break;
	case 7: // Ocean
		clr->accent = ThemeRgb(38, 214, 196);
		clr->window.background = ThemeRgb(8, 18, 22);
		clr->window.sub_background = ThemeRgb(10, 24, 30);
		clr->window.stroke = ThemeRgb(20, 52, 60);
		clr->window.section = ThemeRgb(14, 32, 38);
		clr->widgets.background = ThemeRgb(12, 28, 34);
		clr->widgets.sub_background = ThemeRgb(18, 84, 90);
		clr->widgets.text = ThemeRgb(226, 252, 250);
		clr->widgets.text_inactive = ThemeRgb(80, 136, 140);
		clr->widgets.stroke = ThemeRgb(20, 52, 60);
		clr->widgets.sub_stroke = ThemeRgb(26, 104, 108);
		break;
	case 8: // Emerald
		clr->accent = ThemeRgb(46, 204, 113);
		clr->window.background = ThemeRgb(9, 17, 13);
		clr->window.sub_background = ThemeRgb(12, 22, 17);
		clr->window.stroke = ThemeRgb(22, 48, 34);
		clr->window.section = ThemeRgb(15, 30, 22);
		clr->widgets.background = ThemeRgb(13, 27, 20);
		clr->widgets.sub_background = ThemeRgb(22, 82, 52);
		clr->widgets.text = ThemeRgb(230, 255, 240);
		clr->widgets.text_inactive = ThemeRgb(88, 132, 106);
		clr->widgets.stroke = ThemeRgb(22, 48, 34);
		clr->widgets.sub_stroke = ThemeRgb(30, 102, 64);
		break;
	case 9: // Sunset
		clr->accent = ThemeRgb(255, 128, 72);
		clr->window.background = ThemeRgb(22, 10, 20);
		clr->window.sub_background = ThemeRgb(30, 13, 26);
		clr->window.stroke = ThemeRgb(70, 30, 50);
		clr->window.section = ThemeRgb(38, 16, 32);
		clr->widgets.background = ThemeRgb(34, 14, 28);
		clr->widgets.sub_background = ThemeRgb(120, 44, 58);
		clr->widgets.text = ThemeRgb(255, 240, 230);
		clr->widgets.text_inactive = ThemeRgb(170, 110, 120);
		clr->widgets.stroke = ThemeRgb(70, 30, 50);
		clr->widgets.sub_stroke = ThemeRgb(150, 62, 74);
		break;
	case 10: // Gold
		clr->accent = ThemeRgb(240, 190, 70);
		clr->window.background = ThemeRgb(14, 12, 8);
		clr->window.sub_background = ThemeRgb(20, 17, 11);
		clr->window.stroke = ThemeRgb(54, 44, 24);
		clr->window.section = ThemeRgb(26, 22, 14);
		clr->widgets.background = ThemeRgb(24, 20, 13);
		clr->widgets.sub_background = ThemeRgb(92, 72, 30);
		clr->widgets.text = ThemeRgb(255, 248, 228);
		clr->widgets.text_inactive = ThemeRgb(150, 132, 96);
		clr->widgets.stroke = ThemeRgb(54, 44, 24);
		clr->widgets.sub_stroke = ThemeRgb(120, 96, 40);
		break;
	case 11: // Mono
		clr->accent = ThemeRgb(220, 220, 226);
		clr->window.background = ThemeRgb(12, 12, 13);
		clr->window.sub_background = ThemeRgb(17, 17, 18);
		clr->window.stroke = ThemeRgb(36, 36, 40);
		clr->window.section = ThemeRgb(22, 22, 24);
		clr->widgets.background = ThemeRgb(20, 20, 22);
		clr->widgets.sub_background = ThemeRgb(64, 64, 70);
		clr->widgets.text = ThemeRgb(245, 245, 248);
		clr->widgets.text_inactive = ThemeRgb(110, 110, 118);
		clr->widgets.stroke = ThemeRgb(36, 36, 40);
		clr->widgets.sub_stroke = ThemeRgb(80, 80, 88);
		break;
	case 12: // Neon
		clr->accent = ThemeRgb(255, 46, 190);
		clr->window.background = ThemeRgb(8, 6, 18);
		clr->window.sub_background = ThemeRgb(12, 9, 26);
		clr->window.stroke = ThemeRgb(40, 22, 70);
		clr->window.section = ThemeRgb(18, 12, 36);
		clr->widgets.background = ThemeRgb(16, 11, 32);
		clr->widgets.sub_background = ThemeRgb(24, 96, 120);
		clr->widgets.text = ThemeRgb(240, 250, 255);
		clr->widgets.text_inactive = ThemeRgb(110, 96, 160);
		clr->widgets.stroke = ThemeRgb(40, 22, 70);
		clr->widgets.sub_stroke = ThemeRgb(0, 190, 220);
		break;
	default:
		clr->accent = ThemeRgb(111, 119, 255);
		clr->window.background = ThemeRgb(16, 16, 21);
		clr->window.sub_background = ThemeRgb(18, 18, 23);
		clr->window.stroke = ThemeRgb(25, 25, 34);
		clr->window.section = ThemeRgb(23, 23, 30);
		clr->widgets.background = ThemeRgb(21, 21, 28);
		clr->widgets.sub_background = ThemeRgb(37, 37, 57);
		clr->widgets.text = ThemeRgb(255, 255, 255);
		clr->widgets.text_inactive = ThemeRgb(79, 79, 98);
		clr->widgets.stroke = ThemeRgb(25, 25, 34);
		clr->widgets.sub_stroke = ThemeRgb(34, 34, 48);
		break;
	}

	checkbox_t* custom = cfg->fill<checkbox_t>("Custom color");
	color_edit_t* custom_color = cfg->fill<color_edit_t>("Custom color##color");
	if (custom && custom->callback && custom_color)
	{
		clr->accent.Value.x = custom_color->color[0];
		clr->accent.Value.y = custom_color->color[1];
		clr->accent.Value.z = custom_color->color[2];
	}
}

static int MenuThemeId()
{
	dropdown_t* theme = cfg->fill<dropdown_t>("Menu theme");
	return theme ? theme->callback : 0;
}

static void DrawBoneMark(ImDrawList* dl, ImVec2 c, float s, float ang, ImU32 col)
{
	const ImVec2 d(cosf(ang) * s, sinf(ang) * s);
	dl->AddLine(ImVec2(c.x - d.x, c.y - d.y), ImVec2(c.x + d.x, c.y + d.y), col, s * 0.28f);
	dl->AddCircleFilled(ImVec2(c.x - d.x, c.y - d.y), s * 0.34f, col, 10);
	dl->AddCircleFilled(ImVec2(c.x + d.x, c.y + d.y), s * 0.34f, col, 10);
}

static void DrawHeartMark(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
{
	dl->AddCircleFilled(ImVec2(c.x - s * 0.34f, c.y - s * 0.12f), s * 0.38f, col, 10);
	dl->AddCircleFilled(ImVec2(c.x + s * 0.34f, c.y - s * 0.12f), s * 0.38f, col, 10);
	dl->AddTriangleFilled(ImVec2(c.x - s * 0.70f, c.y + s * 0.02f), ImVec2(c.x + s * 0.70f, c.y + s * 0.02f), ImVec2(c.x, c.y + s * 0.86f), col);
}

static void DrawEggMark(ImDrawList* dl, ImVec2 c, float s, ImU32 col, ImU32 spot)
{
	dl->AddEllipseFilled(c, ImVec2(s * 0.62f, s), col, 0.f, 16);
	dl->AddCircleFilled(ImVec2(c.x - s * 0.14f, c.y - s * 0.18f), s * 0.13f, spot, 8);
}

static void DrawBunnyMark(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
{
	dl->AddCircleFilled(c, s * 0.52f, col, 12);
	dl->AddEllipseFilled(ImVec2(c.x - s * 0.26f, c.y - s * 0.92f), ImVec2(s * 0.16f, s * 0.46f), col, 0.12f, 10);
	dl->AddEllipseFilled(ImVec2(c.x + s * 0.26f, c.y - s * 0.92f), ImVec2(s * 0.16f, s * 0.46f), col, -0.12f, 10);
}

static void DrawFirstOpenIntro()
{
	const float t = MenuIntroT();
	ImGuiIO& io = ImGui::GetIO();
	const ImVec2 screen = io.DisplaySize;
	if (screen.x < 80.f || screen.y < 80.f)
		return;

	float fade = t < 0.28f ? (t / 0.28f) : 1.f;
	float slide = 0.f;
	if (t > 0.30f) {
		slide = (t - 0.30f) / 0.48f;
		if (slide > 1.f)
			slide = 1.f;
		slide = slide * slide * (3.f - 2.f * slide);
	}
	if (t > 0.82f) {
		float out = (t - 0.82f) / 0.18f;
		if (out > 1.f)
			out = 1.f;
		fade *= 1.f - out;
	}
	if (fade < 0.01f)
		return;

	const ImVec2 full = SCALE(var->window.size);
	const ImVec2 home((screen.x - full.x) * 0.5f, (screen.y - full.y) * 0.5f);
	const ImVec2 start(screen.x * 0.5f, screen.y * 0.5f);
	const ImVec2 dest(home.x + SCALE(var->gui.bar_width) * 0.5f, home.y + SCALE(var->gui.bar_width) * 0.5f);
	const ImVec2 pos(start.x + (dest.x - start.x) * slide, start.y + (dest.y - start.y) * slide);
	const float iconPx = SCALE(52.f - 24.f * slide);
	const int a = (int)(255.f * fade);
	ImDrawList* dl = ImGui::GetForegroundDrawList();

	if (var->gui.texture) {
		const float r = SCALE(40.f - 14.f * slide);
		dl->AddImageRounded(var->gui.texture, ImVec2(pos.x - r, pos.y - r), ImVec2(pos.x + r, pos.y + r), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, a), r * 0.28f);
	} else {
		ImFont* icon = font->get(icons_data, 48.f);
		if (icon)
			dl->AddText(icon, iconPx, ImVec2(pos.x - iconPx * 0.5f, pos.y - iconPx * 0.58f), draw->get_clr(clr->accent, fade), "A");
	}

	if (slide < 0.92f) {
		ImFont* title = font->get(onest_bold_data, 22.f);
		const char* name = var->gui.name.c_str();
		if (title && name && *name) {
			const ImVec2 ts = title->CalcTextSizeA(title->FontSize, FLT_MAX, 0.f, name);
			const float textA = fade * (1.f - slide);
			dl->AddText(title, title->FontSize, ImVec2(pos.x - ts.x * 0.5f, pos.y + iconPx * 0.55f), draw->get_clr(clr->widgets.text, textA), name);
		}
	}
}

enum { kThemeFxCap = 72 };

struct ThemeFx {
	float x, y;
	float vx, vy;
	float size;
	float rot, spin;
	float alpha;
	float age, life;
	float wobble;
	float length;
	unsigned seed;
	unsigned char kind;
	unsigned char tone;
	bool live;
};

static ThemeFx g_themeFx[kThemeFxCap];
static int g_themeFxId = -1;
static unsigned g_themeFxRng = 2166136261u;
static float g_themeFxSpawn = 0.f;
static float g_themeFxBurst = 0.f;
static float g_themeFxNextBurst = 2.8f;
static float g_themeFxWind = 0.f;
static float g_themeFxWindT = 0.f;

static float ThemeFxRand()
{
	g_themeFxRng = g_themeFxRng * 1664525u + 1013904223u;
	return (float)(g_themeFxRng >> 8) * (1.f / 16777215.f);
}

static int ThemeFxRandInt(int n)
{
	if (n <= 1)
		return 0;
	return (int)(ThemeFxRand() * (float)n) % n;
}

static ImU32 ThemeFxAlpha(ImU32 rgb, float a)
{
	if (a < 0.f)
		a = 0.f;
	if (a > 1.f)
		a = 1.f;
	a *= ImGui::GetStyle().Alpha;
	const int v = (int)(a * 255.f + 0.5f);
	return (rgb & 0x00FFFFFFu) | ((unsigned)(v < 0 ? 0 : (v > 255 ? 255 : v)) << 24);
}

static int ThemeFxLiveCount()
{
	int n = 0;
	for (int i = 0; i < kThemeFxCap; ++i)
		if (g_themeFx[i].live)
			++n;
	return n;
}

static int ThemeFxCapFor(int theme)
{
	if (theme == 1)
		return 68;
	if (theme == 2)
		return 42;
	if (theme == 3)
		return 50;
	if (theme == 4)
		return 54;
	return 16;
}

static float ThemeFxRateFor(int theme)
{
	if (theme == 1)
		return 14.f;
	if (theme == 2)
		return 5.5f;
	if (theme == 3)
		return 7.5f;
	if (theme == 4)
		return 8.5f;
	return 3.2f;
}

static ThemeFx* ThemeFxSlot()
{
	for (int i = 0; i < kThemeFxCap; ++i)
		if (!g_themeFx[i].live)
			return &g_themeFx[i];
	return nullptr;
}

static void ThemeFxInit(ThemeFx& p, int theme, float w, float h, bool midlife)
{
	p.live = true;
	p.age = 0.f;
	p.seed = g_themeFxRng;
	p.tone = (unsigned char)ThemeFxRandInt(8);
	p.wobble = 0.6f + ThemeFxRand() * 2.4f;
	p.length = 0.f;
	p.spin = 0.f;
	p.rot = ThemeFxRand() * 6.2831853f;

	if (theme == 1) {
		p.kind = 0;
		p.size = 1.1f + ThemeFxRand() * ThemeFxRand() * 3.8f;
		p.vx = g_themeFxWind + (ThemeFxRand() - 0.5f) * 28.f;
		p.vy = 22.f + ThemeFxRand() * 78.f;
		if (p.size > 3.2f)
			p.vy *= 0.55f + ThemeFxRand() * 0.25f;
		p.alpha = 0.16f + ThemeFxRand() * 0.28f;
		p.life = (h + 40.f) / (p.vy + 8.f) + ThemeFxRand() * 1.4f;
		p.spin = (ThemeFxRand() - 0.5f) * 2.2f;
		p.x = ThemeFxRand() * w;
		p.y = midlife ? (ThemeFxRand() * h) : (-8.f - ThemeFxRand() * 18.f);
	} else if (theme == 2) {
		p.kind = 1;
		p.size = 6.5f + ThemeFxRand() * 8.5f;
		const bool fromLeft = ThemeFxRand() < 0.5f;
		p.vx = (fromLeft ? 1.f : -1.f) * (18.f + ThemeFxRand() * 56.f);
		p.vy = (ThemeFxRand() - 0.5f) * 12.f;
		p.alpha = 0.14f + ThemeFxRand() * 0.20f;
		p.life = (w + 80.f) / (fabsf(p.vx) + 10.f) + ThemeFxRand() * 1.8f;
		p.spin = (ThemeFxRand() - 0.5f) * 1.8f;
		p.y = (0.06f + ThemeFxRand() * 0.88f) * h;
		p.x = midlife ? (ThemeFxRand() * w) : (fromLeft ? -16.f : (w + 16.f));
	} else if (theme == 3) {
		p.kind = (ThemeFxRand() < 0.42f) ? 3 : 2;
		p.size = 5.2f + ThemeFxRand() * 7.4f;
		p.vx = (ThemeFxRand() - 0.5f) * 36.f;
		p.vy = (p.kind == 3) ? (-8.f + ThemeFxRand() * 22.f) : (10.f + ThemeFxRand() * 28.f);
		p.alpha = 0.15f + ThemeFxRand() * 0.22f;
		p.life = 3.4f + ThemeFxRand() * 5.2f;
		p.spin = (ThemeFxRand() - 0.5f) * 1.4f;
		if (midlife) {
			p.x = ThemeFxRand() * w;
			p.y = ThemeFxRand() * h;
		} else if (ThemeFxRand() < 0.5f) {
			p.x = ThemeFxRand() * w;
			p.y = (p.vy >= 0.f) ? -14.f : (h + 14.f);
		} else {
			p.x = (p.vx >= 0.f) ? -14.f : (w + 14.f);
			p.y = ThemeFxRand() * h;
		}
	} else if (theme == 4) {
		p.kind = 4;
		p.size = 4.4f + ThemeFxRand() * ThemeFxRand() * 8.2f;
		p.vx = (ThemeFxRand() - 0.5f) * 22.f;
		p.vy = -(16.f + ThemeFxRand() * 42.f);
		if (p.size > 8.f)
			p.vy *= 0.62f;
		p.alpha = 0.14f + ThemeFxRand() * 0.22f;
		p.life = (h + 50.f) / (fabsf(p.vy) + 8.f) + ThemeFxRand() * 1.6f;
		p.spin = (ThemeFxRand() - 0.5f) * 0.9f;
		p.x = ThemeFxRand() * w;
		p.y = midlife ? (ThemeFxRand() * h) : (h + 12.f + ThemeFxRand() * 20.f);
	} else {
		p.kind = 5;
		const float ang = 0.62f + ThemeFxRand() * 0.42f;
		const float spd = 70.f + ThemeFxRand() * 160.f;
		p.vx = cosf(ang) * spd;
		p.vy = sinf(ang) * spd;
		p.length = 38.f + ThemeFxRand() * 92.f;
		p.size = 1.05f + ThemeFxRand() * 2.6f;
		p.alpha = 0.10f + ThemeFxRand() * 0.20f;
		p.life = 1.1f + ThemeFxRand() * 2.4f;
		if (midlife) {
			p.x = ThemeFxRand() * w;
			p.y = ThemeFxRand() * h;
		} else if (ThemeFxRand() < 0.55f) {
			p.x = ThemeFxRand() * (w * 0.85f) - 20.f;
			p.y = -p.length * 0.25f;
		} else {
			p.x = -p.length * 0.25f;
			p.y = ThemeFxRand() * (h * 0.85f);
		}
	}

	if (midlife)
		p.age = ThemeFxRand() * (p.life * 0.82f);
}

static void ThemeFxReset(int theme, float w, float h)
{
	for (int i = 0; i < kThemeFxCap; ++i)
		g_themeFx[i].live = false;
	g_themeFxId = theme;
	g_themeFxSpawn = 0.f;
	g_themeFxBurst = 0.35f;
	g_themeFxNextBurst = 2.2f + ThemeFxRand() * 3.4f;
	g_themeFxWind = (ThemeFxRand() - 0.5f) * 18.f;
	g_themeFxWindT = 0.f;
	const int seed = ThemeFxCapFor(theme) * 3 / 4;
	for (int i = 0; i < seed; ++i)
		if (ThemeFx* slot = ThemeFxSlot())
			ThemeFxInit(*slot, theme, w, h, true);
}

static void ThemeFxTick(int theme, float w, float h, float dt)
{
	if (theme != g_themeFxId)
		ThemeFxReset(theme, w, h);

	g_themeFxWindT += dt;
	g_themeFxWind += (sinf(g_themeFxWindT * 0.17f) * 22.f - g_themeFxWind) * dt * 0.35f;

	g_themeFxNextBurst -= dt;
	if (g_themeFxNextBurst <= 0.f) {
		g_themeFxBurst = 0.22f + ThemeFxRand() * 0.48f;
		g_themeFxNextBurst = 2.4f + ThemeFxRand() * 5.6f;
	}
	if (g_themeFxBurst > 0.f)
		g_themeFxBurst -= dt;

	const int cap = ThemeFxCapFor(theme);
	float rate = ThemeFxRateFor(theme);
	rate *= 0.82f + 0.28f * (0.5f + 0.5f * sinf(g_themeFxWindT * 0.41f + (float)theme));
	if (g_themeFxBurst > 0.f)
		rate *= 2.1f + ThemeFxRand() * 0.8f;

	g_themeFxSpawn += rate * dt;
	int born = 0;
	const int bornMax = (g_themeFxBurst > 0.f) ? 6 : 3;
	while (g_themeFxSpawn >= 1.f && born < bornMax && ThemeFxLiveCount() < cap) {
		g_themeFxSpawn -= 1.f;
		if (ThemeFx* slot = ThemeFxSlot()) {
			ThemeFxInit(*slot, theme, w, h, false);
			++born;
		} else {
			break;
		}
	}
	if (g_themeFxSpawn > 4.f)
		g_themeFxSpawn = 4.f;

	const float margin = 28.f;
	for (int i = 0; i < kThemeFxCap; ++i) {
		ThemeFx& p = g_themeFx[i];
		if (!p.live)
			continue;
		p.age += dt;
		if (p.age >= p.life) {
			p.live = false;
			continue;
		}

		float drift = sinf(p.age * p.wobble + (float)(p.seed & 255u) * 0.024f);
		if (p.kind == 0) {
			p.x += (p.vx + g_themeFxWind * 0.65f + drift * 16.f) * dt;
			p.y += p.vy * dt;
			p.rot += p.spin * dt;
		} else if (p.kind == 1) {
			p.x += p.vx * dt;
			p.y += (p.vy + drift * 10.f) * dt;
			p.rot += p.spin * dt;
		} else if (p.kind == 3) {
			const float hop = fabsf(sinf(p.age * (2.4f + p.wobble))) * 18.f;
			p.x += (p.vx + drift * 8.f) * dt;
			p.y += p.vy * dt - hop * dt * 6.f;
			p.rot += p.spin * dt;
		} else if (p.kind == 5) {
			p.x += p.vx * dt;
			p.y += p.vy * dt;
		} else {
			p.x += (p.vx + drift * 14.f) * dt;
			p.y += p.vy * dt;
			p.rot += p.spin * dt;
		}

		if (p.x < -margin || p.x > w + margin || p.y < -margin || p.y > h + margin)
			p.live = false;
	}
}

static float ThemeFxFade(const ThemeFx& p)
{
	float fade = 1.f;
	if (p.age < 0.28f)
		fade = p.age / 0.28f;
	const float left = p.life - p.age;
	if (left < 0.42f)
		fade *= left / 0.42f;
	return fade * p.alpha;
}

static void ThemeFxDrawOne(ImDrawList* dl, const ThemeFx& p, const ImVec2& origin)
{
	const float fade = ThemeFxFade(p);
	if (fade < 0.02f)
		return;
	const ImVec2 c(origin.x + p.x, origin.y + p.y);

	if (p.kind == 0) {
		const ImU32 col = ThemeFxAlpha(IM_COL32(236, 244, 255, 255), fade);
		if ((p.tone & 3) == 0) {
			const float r = p.size * 0.85f;
			dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), col, 1.1f);
			dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, 1.1f);
			dl->AddLine(ImVec2(c.x - r * 0.7f, c.y - r * 0.7f), ImVec2(c.x + r * 0.7f, c.y + r * 0.7f), col, 0.9f);
			dl->AddLine(ImVec2(c.x - r * 0.7f, c.y + r * 0.7f), ImVec2(c.x + r * 0.7f, c.y - r * 0.7f), col, 0.9f);
		} else {
			dl->AddCircleFilled(c, p.size, col, 8);
		}
	} else if (p.kind == 1) {
		DrawBoneMark(dl, c, p.size, p.rot, ThemeFxAlpha(IM_COL32(236, 228, 210, 255), fade));
	} else if (p.kind == 2) {
		const ImU32 eggs[] = {
			IM_COL32(255, 186, 210, 255),
			IM_COL32(186, 224, 255, 255),
			IM_COL32(198, 236, 186, 255),
			IM_COL32(255, 228, 168, 255)
		};
		DrawEggMark(dl, c, p.size, ThemeFxAlpha(eggs[p.tone & 3], fade), ThemeFxAlpha(IM_COL32(255, 255, 255, 255), fade * 0.7f));
	} else if (p.kind == 3) {
		DrawBunnyMark(dl, c, p.size, ThemeFxAlpha(IM_COL32(255, 248, 252, 255), fade));
	} else if (p.kind == 4) {
		const ImU32 hearts[] = {
			IM_COL32(232, 72, 118, 255),
			IM_COL32(214, 48, 96, 255),
			IM_COL32(255, 132, 164, 255),
			IM_COL32(176, 36, 78, 255)
		};
		DrawHeartMark(dl, c, p.size, ThemeFxAlpha(hearts[p.tone & 3], fade));
	} else {
		const float nx = p.vx;
		const float ny = p.vy;
		const float mag = sqrtf(nx * nx + ny * ny) + 0.001f;
		const ImVec2 b(c.x + nx / mag * p.length, c.y + ny / mag * p.length);
		const ImU32 col = draw->get_clr(clr->accent, fade);
		dl->AddLine(c, b, col, p.size);
		if ((p.tone & 1) != 0) {
			const ImVec2 o(ny / mag * 7.f, -nx / mag * 7.f);
			dl->AddLine(ImVec2(c.x + o.x, c.y + o.y), ImVec2(b.x + o.x, b.y + o.y), draw->get_clr(clr->accent, fade * 0.35f), p.size * 0.45f);
		}
	}
}

void DrawMenuThemeAmbient(ImDrawList* dl, const ImVec2& min, const ImVec2& max)
{
	if (!dl)
		return;
	const float w = max.x - min.x;
	const float h = max.y - min.y;
	if (w < 40.f || h < 40.f)
		return;

	float dt = ImGui::GetIO().DeltaTime;
	if (dt < 0.f)
		dt = 0.f;
	if (dt > 0.05f)
		dt = 0.05f;

	const int theme = MenuThemeId();
	ThemeFxTick(theme, w, h, dt);

	dl->PushClipRect(min, max, true);
	for (int i = 0; i < kThemeFxCap; ++i) {
		if (g_themeFx[i].live)
			ThemeFxDrawOne(dl, g_themeFx[i], min);
	}
	dl->PopClipRect();
}

void c_gui::render()
{
	const bool intro = MenuIntroActive();
	const float motion = MenuMotion();
	if (motion <= 0.001f && !intro)
		return;

	gui->initialize();
	ApplyMenuTheme();
	if (intro)
		DrawFirstOpenIntro();
	if (motion <= 0.001f)
		return;

	const bool moving = motion < 0.999f;
	const float scale = 0.94f + 0.06f * motion;
	const ImVec2 full = SCALE(var->window.size);
	const ImVec2 sized(full.x * scale, full.y * scale);
	float slide = (1.f - motion) * SCALE(18.f);
	float introShift = 0.f;
	if (intro) {
		slide = 0.f;
		introShift = (1.f - motion) * SCALE(72.f);
	}

	static ImVec2 home(0.f, 0.f);
	static bool placed = false;
	ImGuiIO& io = ImGui::GetIO();
	if (!placed && io.DisplaySize.x > 1.f && io.DisplaySize.y > 1.f) {
		home.x = (io.DisplaySize.x - full.x) * 0.5f;
		home.y = (io.DisplaySize.y - full.y) * 0.5f;
		placed = true;
	}
	if (moving) {
		ImVec2 pos(home.x + (full.x - sized.x) * 0.5f + introShift, home.y + (full.y - sized.y) * 0.5f - slide);
		gui->set_next_window_pos(pos);
		gui->set_next_window_size(sized);
	} else {
		gui->set_next_window_size(full);
	}

	gui->push_var(style_var_alpha, motion);
	gui->begin("menu", nullptr, var->window.flags);
	{
		cfg->process_keybinds();
		gui->set_style();
		gui->draw_decorations();

		{
			gui->set_pos(SCALE(var->gui.bar_width), pos_y);
			gui->begin_content("sections", SCALE(var->gui.bar_width, 0), SCALE(elements->section.padding), SCALE(elements->section.spacing), window_flags_no_scrollbar | window_flags_no_scroll_with_mouse | window_flags_no_move);
			{
				static float section_anim;
				static float saved_pos{ 0 };
				static bool initialized{ false };
				gui->easing(section_anim, var->gui.section_pos, 24.f, dynamic_easing);
				draw->rect_filled(gui->window_drawlist(), gui->window_pos() + ImVec2(SCALE(elements->section.padding.x), section_anim), gui->window_pos() + ImVec2(gui->window_width() - SCALE(elements->section.padding.x), gui->content_avail().x + section_anim), draw->get_clr(clr->window.section), SCALE(elements->section.rounding));
				
				for (int i = 0; i < var->gui.sections_data.second.size(); ++i)
				{
					if (widgets->section(var->gui.sections_data.second.at(i).icon, i, (int&)var->gui.sections_data.first).value_changed || !initialized)
					{
						var->gui.section_pos = GImGui->LastItemData.Rect.Min.y - gui->window_pos().y;
						saved_pos = (GImGui->LastItemData.Rect.Min.y - gui->window_pos().y) / var->gui.dpi;
						initialized = true;
					}

					if (var->gui.dpi_fix)
					{
						var->gui.section_pos = SCALE(saved_pos);
						var->gui.dpi_fix = false;
					}

					if (var->gui.sections_data.second.at(i).separator)
					{
						gui->dummy(SCALE(0, 1));
						draw->rect_filled(gui->window_drawlist(), ImVec2(gui->get_window()->Rect().GetCenter().x - (gui->content_avail().x / 4), GImGui->LastItemData.Rect.Min.y), ImVec2(gui->get_window()->Rect().GetCenter().x + (gui->content_avail().x / 4), GImGui->LastItemData.Rect.Max.y), draw->get_clr(clr->window.stroke));
					}
				}

				gui->easing(var->gui.section_alpha, var->gui.active_section == var->gui.sections_data.first ? 1.f : 0.f, 8.f, static_easing);

				if (var->gui.section_alpha == 0.f)
					var->gui.active_section = var->gui.sections_data.first;
			}
			gui->end_content();

			gui->sameline();
		}

		{
			gui->set_pos(0, pos_y);
			gui->begin_content("sub_sections", SCALE(var->gui.sidebar_width, 0), SCALE(0, 0), SCALE(0, 0));
			{
				gui->begin_content("search", SCALE(0, var->gui.bar_width), SCALE(elements->sub_section.padding), SCALE(elements->sub_section.spacing), window_flags_no_move | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
				{
					widgets->search_field("Search", search->search_buf, sizeof search->search_buf, SCALE(search->text_field_width));
					search->search_element = search->search_buf;
				}
				gui->end_content();

				float stored_pos_x;
				gui->push_var(style_var_alpha, var->gui.section_alpha);
				gui->begin_content("seb_sections_child", ImVec2(0, 0), SCALE(elements->sub_section.padding), SCALE(elements->sub_section.spacing), window_flags_no_move | window_flags_no_scrollbar | window_flags_no_scroll_with_mouse);
				{
					var->gui.drawlist = gui->window_drawlist();

					if (var->gui.sections_data.second.at((int)var->gui.active_section).subsections.size() > 0)
					{
						widgets->begin_section(var->gui.sections_data.second.at((int)var->gui.active_section).name);
						{
							if (var->gui.sections_data.second.size() > static_cast<int>(var->gui.active_section))
							{
								const auto& section = var->gui.sections_data.second[static_cast<int>(var->gui.active_section)];

								int& active_subsection = var->gui.count_subsections[static_cast<int>(var->gui.active_section)];

								int last_non_external_index = -1;
								for (int i = section.subsections.size() - 1; i >= 0; i--)
								{
									if (!section.subsections[i].is_external)
									{
										last_non_external_index = i;
										break;
									}
								}

								for (size_t i = 0; i < section.subsections.size(); i++)
								{
									const auto& subsection = section.subsections[i];
									if (subsection.is_external)
										continue;

									if (subsection.is_group)
									{
										if (widgets->begin_sub_section(subsection.name.c_str()))
										{
											for (const auto& item : subsection.items)
												widgets->sub_section_list(item.name.c_str(), item.index, active_subsection);

											widgets->end_sub_section();
										}

										if (i != last_non_external_index)
											gui->set_screen_pos(var->gui.sub_pos, pos_all);
									}
									else
									{
										widgets->sub_section(subsection.name.c_str(), subsection.index, active_subsection);

										if (i != last_non_external_index)
											gui->set_screen_pos(var->gui.sub_pos, pos_all);
									}
								}
								stored_pos_x = gui->get_screen_pos().x;
							}
						}
						widgets->end_section();

						gui->set_screen_pos(ImVec2(stored_pos_x, gui->get_screen_pos().y - SCALE(5)), pos_all);
						if (var->gui.sections_data.second.size() > static_cast<int>(var->gui.active_section))
						{
							const auto& section = var->gui.sections_data.second[static_cast<int>(var->gui.active_section)];

							int& active_subsection = var->gui.count_subsections[static_cast<int>(var->gui.active_section)];

							for (const auto& subsection : section.subsections)
							{
								if (subsection.is_external)
								{
									widgets->sub_section(subsection.name.c_str(), subsection.index, active_subsection, true);
									break;
								}
							}
						}
					}
				}
				gui->end_content();
				gui->pop_var();
			}
			gui->end_content();

			gui->easing(var->gui.sub_section_alpha, var->gui.active_subsections.at((int)var->gui.sections_data.first) == var->gui.count_subsections.at((int)var->gui.sections_data.first) ? 1.f : 0.f, 8.f, static_easing);

			if (var->gui.sub_section_alpha == 0.f)
				var->gui.active_subsections.at((int)var->gui.sections_data.first) = var->gui.count_subsections.at((int)var->gui.sections_data.first);

			gui->sameline();
		}

		{
			gui->push_var(style_var_alpha, var->gui.section_alpha * var->gui.sub_section_alpha);
			gui->set_pos(SCALE(var->gui.bar_width + var->gui.sidebar_width, var->gui.bar_width), pos_all);
			gui->begin_content("content", SCALE(0, 0), SCALE(elements->content.padding), SCALE(elements->content.spacing));
			{
				auto& current = var->gui.active_subsections.at((int)var->gui.active_section);

				if (var->gui.active_section == section_id::general && current == 0)
				{
					gui->begin_group();
					{
						widgets->begin_child("Aim");
						{
							widgets->checkbox("Enable aim assist");
							widgets->checkbox("Silent aim");
							widgets->checkbox("Magic Bullet");
							widgets->checkbox("Always headshots");
							widgets->checkbox("Sniper pump");
							widgets->checkbox("Sniper pump uses aim FOV");
							widgets->dropdown("Aim key");
							widgets->dropdown("Aim bone");
							widgets->checkbox("Draw target highlight");
						}
						widgets->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						widgets->begin_child("Aim extras");
						{
							widgets->checkbox("Draw FOV circle");
							widgets->slider_float("Aim FOV");
							widgets->slider_float("Smoothness");
							widgets->slider_float("FOV thickness");
							widgets->color_edit("FOV color");
						}
						widgets->end_child();
					}
					gui->end_group();
				}

				if (var->gui.active_section == section_id::wallhack && current == 0)
				{
					gui->begin_group();
					{
						widgets->begin_child("Box");
						{
							widgets->checkbox("Enable ESP");
							widgets->checkbox("Player box");
							widgets->checkbox("Corner box");
							widgets->checkbox("Box outline");
							widgets->checkbox("Box fill");
							widgets->checkbox("Player name");
							widgets->checkbox("Distance");
							widgets->checkbox("Player health bar");
							widgets->checkbox("Snapline");
							widgets->checkbox("No self ESP");
							widgets->checkbox("Team check");
							widgets->checkbox("Visible check");
						}
						widgets->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						widgets->begin_child("Skeleton");
						{
							widgets->checkbox("Player skeleton");
							widgets->checkbox("Curved skeleton");
							widgets->checkbox("Spaghetti skeleton");
							widgets->checkbox("Skeleton outline");
							widgets->checkbox("China hat");
							widgets->checkbox("Self china hat");
							widgets->checkbox("Chams");
							widgets->checkbox("Wireframe chams");
							widgets->checkbox("Show on enemies");
							widgets->checkbox("Show on teammates");
							widgets->checkbox("Only show on screen");
							widgets->slider_int("Render distance");
						}
						widgets->end_child();
					}
					gui->end_group();
				}

				if (var->gui.active_section == section_id::wallhack && current == 1)
				{
					gui->begin_group();
					{
						widgets->begin_child("Sizes");
						{
							widgets->slider_float("Box thickness");
							widgets->slider_float("Box width");
							widgets->slider_float("Skeleton thickness");
							widgets->slider_float("China thickness");
							widgets->dropdown("Snapline from");
							widgets->dropdown("Health bar side");
						}
						widgets->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						widgets->begin_child("Colors");
						{
							widgets->color_edit("Player box##color");
							widgets->color_edit("Box fill##color");
							widgets->color_edit("Player skeleton##color");
							widgets->color_edit("Name color");
							widgets->color_edit("Distance color");
							widgets->color_edit("Player health bar##color");
							widgets->color_edit("Snapline color");
							widgets->color_edit("China hat##color");
							widgets->color_edit("Chams color");
						}
						widgets->end_child();
					}
					gui->end_group();
				}

				if (var->gui.active_section == section_id::world && current == 0)
				{
					gui->begin_group();
					{
						widgets->begin_child("Loot");
						{
							widgets->checkbox("Floor loot");
							widgets->checkbox("Chests");
							widgets->checkbox("Loot distance");
							widgets->slider_float("Loot range");
							widgets->checkbox("Exclude ammo");
							widgets->checkbox("Exclude materials");
						}
						widgets->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						widgets->begin_child("Rarity");
						{
							widgets->checkbox("Common");
							widgets->checkbox("Uncommon");
							widgets->checkbox("Rare");
							widgets->checkbox("Epic");
							widgets->checkbox("Legendary");
							widgets->checkbox("Mythic");
						}
						widgets->end_child();
					}
					gui->end_group();
				}

				if (var->gui.active_section == section_id::movement && current == 0)
				{
					gui->begin_group();
					{
						widgets->begin_child("Ride");
						{
							widgets->checkbox("Vehicle fly");
							widgets->slider_float("Vehicle speed");
							widgets->checkbox("Spinbot");
							widgets->slider_float("Spin speed");
							widgets->checkbox("Zipline fly");
						}
						widgets->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						widgets->begin_child("Player");
						{
							widgets->checkbox("Fly");
							widgets->slider_float("Fly speed");
							widgets->dropdown("Fly key");
							widgets->checkbox("Speed boost");
							widgets->slider_float("Speed multiplier");
							widgets->checkbox("God mode");
							widgets->checkbox("Heal while moving");
							widgets->dropdown("Heal key");
							widgets->dropdown("Spawn item");
							if (widgets->button("Give item").value_changed)
								cheat.spawnPending = true;
							if (widgets->button("Teleport to bot").value_changed)
								cheat.tpPending = true;
							widgets->dropdown("Teleport key");
							widgets->checkbox("Time of day");
							widgets->slider_float("Hour of day");
							widgets->checkbox("FOV changer");
							widgets->slider_float("FOV value");
							widgets->checkbox("Auto revive");
							widgets->checkbox("Instant revive");
							widgets->checkbox("ADS in air");
						}
						widgets->end_child();
					}
					gui->end_group();
				}

				if (var->gui.active_section == section_id::weapon && current == 0)
				{
					gui->begin_group();
					{
						widgets->begin_child("Weapon");
						{
							widgets->checkbox("No bloom");
							widgets->checkbox("Projectile TP");
							widgets->checkbox("No spread");
							widgets->slider_float("Spread amount");
							widgets->checkbox("No recoil");
							widgets->slider_float("Recoil amount");
							widgets->checkbox("No reload");
							widgets->slider_float("Reload amount");
							widgets->checkbox("No charge");
							widgets->slider_float("Charge amount");
						}
						widgets->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						widgets->begin_child("Fire");
						{
							widgets->checkbox("Rapid fire");
							widgets->slider_float("Fire speed");
							widgets->checkbox("Damage multiplier");
							widgets->slider_float("Damage scale");
							widgets->checkbox("Fast pickaxe");
							widgets->slider_float("Pickaxe speed");
							widgets->checkbox("Automatic weapons");
							widgets->checkbox("Double pump");
							widgets->checkbox("Infinite ammo");
							widgets->checkbox("Infinite materials");
						}
						widgets->end_child();
					}
					gui->end_group();
				}

				if (var->gui.active_section == section_id::customize && current == 0)
				{
					widgets->begin_child("Preset themes", true);
					{
						widgets->dropdown("Menu theme");
						widgets->checkbox("Custom color");
						widgets->color_edit("Custom color##color");

						const int theme_id = cfg->get<dropdown_t>("Menu theme").callback;
						const char* blurb = "Clean Syndicate palette with a cool indigo accent.";
						if (theme_id == 1)
							blurb = "Pine greens, holly-berry red, and warm gold - a Christmas living-room look.";
						else if (theme_id == 2)
							blurb = "Midnight purple, pumpkin orange, and bone cream - a Halloween night look.";
						else if (theme_id == 3)
							blurb = "Mint grass, tulip pink, and lavender - a spring Easter look.";
						else if (theme_id == 4)
							blurb = "Wine, rose, and blush - a Valentine's look.";
						else if (theme_id == 5)
							blurb = "Deep navy with an electric blue accent - easy on the eyes at night.";
						else if (theme_id == 6)
							blurb = "Black and blood red - an aggressive, high-contrast look.";
						else if (theme_id == 7)
							blurb = "Deep sea blues with a bright teal accent.";
						else if (theme_id == 8)
							blurb = "Dark forest greens with a clean emerald accent.";
						else if (theme_id == 9)
							blurb = "Dusky purple fading into warm orange and pink.";
						else if (theme_id == 10)
							blurb = "Black and gold - a clean premium look.";
						else if (theme_id == 11)
							blurb = "Greyscale with a white accent - nothing to distract you.";
						else if (theme_id == 12)
							blurb = "Cyberpunk magenta and cyan on near-black.";

						widgets->text_box("Theme preview", blurb);
					}
					widgets->end_child();
				}

				if (var->gui.active_section == section_id::stream && current == 0)
				{
					gui->begin_group();
					{
						widgets->begin_child("Stream proof");
						{
							widgets->checkbox("Stream proof");
							widgets->text_box("Stream proof status", StreamProofStatus());
						}
						widgets->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						widgets->begin_child("How it works");
						{
							widgets->text_box("Stream proof help",
								"On: the menu, ESP boxes, lines, names and the debug overlay are drawn on their own window that "
								"OBS, Discord screen share, Game Bar, ShadowPlay and screenshots skip. You still see everything; "
								"your stream and recordings show only the game.\n\n"
								"Off: everything is drawn into the game again, so it shows up on stream (to showcase the menu).\n\n"
								"Chams recolor the characters inside the game, so they always show on stream. "
								"Needs Windows 10 2004 or newer and the game in windowed or borderless mode.");
						}
						widgets->end_child();
					}
					gui->end_group();
				}

				if (var->gui.active_section == section_id::misc && current == 0)
				{
					widgets->begin_child("Credits", true);
					{
						widgets->text_box("Credits text",
							"Big thank you to HobosUnite for helping getting this working, this couldn't have been possible without him. Also a huge thank you to Joey for trusting and allowing us to work on this portion of Syndicate.");
						gui->dummy(SCALE(0, 18));
						widgets->text_box("Credits names",
							"Made by: Sact, Hobosunite & trentlab\nCollaborator: Sact\nCheats: Hobosunite\nUI Design: trentlab");
					}
					widgets->end_child();
				}

				if (var->gui.active_section == section_id::misc && current == 1)
				{
					gui->begin_group();
					{
						widgets->begin_child("Session");
						{
							widgets->checkbox("Debug overlay");
							widgets->dropdown("Menu key");
							widgets->checkbox("Custom color");
							widgets->color_edit("Custom color##color");

							static bool changed;
							if (widgets->dropdown("Menu scale").value_changed)
								changed = true;

							if (changed)
							{
								if (cfg->get<dropdown_t>("Menu scale").callback == 0)
									var->gui.stored_dpi = 75;
								else if (cfg->get<dropdown_t>("Menu scale").callback == 1)
									var->gui.stored_dpi = 100;
								else if (cfg->get<dropdown_t>("Menu scale").callback == 2)
									var->gui.stored_dpi = 150;
								else if (cfg->get<dropdown_t>("Menu scale").callback == 3)
									var->gui.stored_dpi = 200;

								var->gui.dpi_fix = true;
								var->gui.dpi_changed = true;
								changed = false;
							}

							widgets->slider_float("Animation speed");

							if (var->gui.anim_speed != cfg->get<slider_float_t>("Animation speed").callback && IsMouseReleased(ImGuiMouseButton_Left))
								var->gui.anim_speed = cfg->get<slider_float_t>("Animation speed").callback;

							widgets->picker_field("Server name", cheat.serverName, sizeof(cheat.serverName), gui->content_avail().x);
							if (widgets->button("Server change name").value_changed && cheat.serverName[0])
								cheat.namePending = true;

							if (widgets->button("Unload DLL").value_changed)
								var->request_unload = true;
						}
						widgets->end_child();
					}
					gui->end_group();

					gui->sameline();

					gui->begin_group();
					{
						widgets->begin_child("Configs");
						{
							// Your settings are remembered by themselves (last session). A named config is a
							// .cfg file: Save makes one, changes then save to it, and Share puts the file on
							// the clipboard to paste in Discord; Import adds a .cfg file you copied.
							static char presetName[32] = {};
							widgets->picker_field("Config name", presetName, sizeof(presetName), gui->content_avail().x);
							if (widgets->button("Save config").value_changed && presetName[0]) {
								if (cfg->save_preset(presetName))
									notify->add_notify("Config saved: changes now save to it", success);
								else
									notify->add_notify("Use letters, numbers, spaces, - or _ in the name", error);
							}
							if (widgets->button("Import copied config file").value_changed) {
								if (!cfg->import_copied_file().empty())
									notify->add_notify("Config imported and loaded", success);
								else
									notify->add_notify("Copy a .cfg file first (Ctrl+C on it)", warning);
							}
							if (widgets->button("Open configs folder").value_changed)
								cfg->open_preset_folder();
							const std::vector<std::string> presets = cfg->preset_names();
							for (const std::string& preset : presets) {
								const bool active = preset == cfg->active_name;
								if (widgets->button((active ? "Loaded: " : "Load ") + preset).value_changed) {
									cfg->load_preset(preset);
									notify->add_notify("Config loaded", success);
								}
								if (widgets->button("Share " + preset + " file").value_changed) {
									if (cfg->share_preset_file(preset))
										notify->add_notify("Config file copied: paste it in Discord or a folder", success);
									else
										notify->add_notify("Could not copy the config file", error);
								}
								if (widgets->button("Delete " + preset).value_changed)
									cfg->delete_preset(preset);
							}
						}
						widgets->end_child();
					}
					gui->end_group();
				}

			}
			gui->end_content();
			gui->pop_var();
		}

		search->search();

		if (!moving && placed)
			home = gui->get_window()->Pos;
	}
	gui->end();
	gui->pop_var();
}
