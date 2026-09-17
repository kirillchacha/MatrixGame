// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#include "MatrixFormMenu.hpp"

#include "MatrixGame.h"
#include "MatrixSide.hpp"
#include "PlayableSides.hpp"
#include "MatrixInstantDraw.hpp"
#include "MatrixSampleStateManager.hpp"
#include "MatrixObjectBuilding.hpp"
#include "Common.hpp"
#include "Text/Render.hpp"

#include "3g.hpp"
#include "Cache.hpp"
#include "Texture.hpp"
#include "CFile.hpp"
#include "CStorage.hpp"
#include "CBitmap.hpp"

#include <algorithm>

using Base::CPoint;
using Base::CRect;

SMatchChoice g_MatchChoice{L"", PLAYER_SIDE_DEFAULT, false};

#define MENU_FONT       L"Font.2Normal"
#define MENU_FONT_SMALL L"Font.2Small"

#define MENU_COLOR_BACK      0xFF0F1419
#define MENU_COLOR_PANEL     0xFF182027
#define MENU_COLOR_SELECTED  0xFF2E3E46
#define MENU_COLOR_LINE      0xFF313F46
#define MENU_COLOR_TEXT      0xFFFFFFFF
#define MENU_COLOR_DIM       0xFF879AA5
#define MENU_COLOR_ACCENT    0xFFCAEE78
#define MENU_COLOR_DISABLED  0xFF4B5A62

#define MENU_ROW_HEIGHT 24
#define MENU_PAD        20

static const wchar *MENU_MAP_FOLDER = L"Matrix\\Map";

// Draws one line of text into dst. Text::Render() builds its own bitmap, we just blend it in.
static void DrawString(CBitmap &dst, int x, int y, int w, int h, const std::wstring &text, DWORD color,
                       const wchar *font = MENU_FONT, int alignx = 0) {
    if (text.empty() || w <= 0 || h <= 0)
        return;

    CBitmap bm;
    CRect clip(0, 0, w, h);
    Text::Render(text, font, color, w, h, alignx, 1, 0, 0, 0, clip, bm);

    if (bm.SizeX() <= 0 || bm.SizeY() <= 0)
        return;

    dst.MergeWithAlpha(CPoint(x, y), bm.Size(), bm, CPoint(0, 0));
}

static void FillRect(CBitmap &dst, const CRect &r, DWORD color) {
    if (r.IsEmpty())
        return;

    dst.Fill(CPoint(r.left, r.top), CPoint(r.right - r.left, r.bottom - r.top), color);
}

// Name and color of a side, taken from the same "Side" config block the map loader uses.
static bool GetSideInfo(int id, std::wstring &name, DWORD &color) {
    CBlockPar *bps = g_MatrixData->BlockGet(L"Side");
    int cnt = bps->ParCount();

    for (int i = 0; i < cnt; ++i) {
        if (bps->ParGetName(i).GetInt() != id)
            continue;

        const auto par = bps->ParGet(i);
        name = par.GetStrPar(0, L",");
        static const wchar *labels[] = {L"", L"Жёлтые", L"Красные", L"Синие", L"Зелёные"};
        if (id >= 1 && id <= 4)
            name = labels[id];
        color = 0xFF000000 | (DWORD(par.GetStrPar(1, L",").GetInt() & 255) << 16) |
                (DWORD(par.GetStrPar(2, L",").GetInt() & 255) << 8) | DWORD(par.GetStrPar(3, L",").GetInt() & 255);
        return true;
    }

    return false;
}

CFormMenu::CFormMenu(void)
  : CForm(), m_MapSel(0), m_MapTop(0), m_MapRows(0), m_SideSel(PLAYER_SIDE_DEFAULT), m_Texture(NULL), m_Dirty(true) {
    m_Name = L"FormMenu";
}

CFormMenu::~CFormMenu() {
    ReleaseTexture();
}

void CFormMenu::LoadMapList(void) {
    m_Maps.clear();

    std::vector<std::wstring> files;
    CFile::FindPackFiles(MENU_MAP_FOLDER, files);
    std::sort(files.begin(), files.end());

    for (const std::wstring &file : files) {
        std::wstring lower(file);
        utils::to_lower(lower);

        if (!lower.ends_with(L".cmap"))
            continue;

        // Maps named demo* put the engine into its automatic demo mode (MMFLAG_FULLAUTO), which is
        // not a playable match, so they are left out.
        if (lower.starts_with(L"demo"))
            continue;

        SMapItem item;
        item.m_Name = file.substr(0, file.length() - 5);
        item.m_Path = std::wstring(MENU_MAP_FOLDER) + L"\\" + file;
        item.m_SidesKnown = false;
        m_Maps.push_back(item);
    }
}

void CFormMenu::ReadMapSides(SMapItem &item) {
    if (item.m_SidesKnown)
        return;

    item.m_SidesKnown = true;

    try {
        CStorage stor;
        if (!stor.Load(item.m_Path.c_str()))
            return;

        CDataBuf *sides = stor.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_SIDE, ST_BYTE);
        CDataBuf *kinds = stor.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_KIND, ST_BYTE);
        if (sides == NULL || kinds == NULL || sides->GetArraysCount() == 0 || kinds->GetArraysCount() == 0)
            return;

        BYTE *side = sides->GetFirst<BYTE>(0);
        BYTE *kind = kinds->GetFirst<BYTE>(0);
        item.m_Sides = CollectPlayableSides({side, sides->GetArrayLength(0)},
                                           {kind, kinds->GetArrayLength(0)}, BUILDING_BASE);
    }
    catch (...) {
        // An unreadable map simply offers no sides; the player can pick another one.
        item.m_Sides.clear();
    }
}

void CFormMenu::SelectMap(int index) {
    if (m_Maps.empty())
        return;

    m_MapSel = std::clamp(index, 0, (int)m_Maps.size() - 1);

    if (m_MapSel < m_MapTop)
        m_MapTop = m_MapSel;
    if (m_MapRows > 0 && m_MapSel >= m_MapTop + m_MapRows)
        m_MapTop = m_MapSel - m_MapRows + 1;

    ReadMapSides(m_Maps[m_MapSel]);

    const std::vector<int> &sides = m_Maps[m_MapSel].m_Sides;
    if (sides.empty())
        m_SideSel = 0;
    else if (std::find(sides.begin(), sides.end(), m_SideSel) == sides.end())
        m_SideSel = sides.front();

    m_Dirty = true;
}

void CFormMenu::Enter(void) {
    DTRACE();

    // The engine keeps whatever cursor was set last (WM_SETCURSOR is swallowed in 3g.cpp), and the
    // in-game cursor hides the system one, so ask for the arrow back while the menu is up.
    SetCursor(LoadCursor(NULL, IDC_ARROW));
    while (ShowCursor(TRUE) < 0) {
    }

    if (m_Maps.empty())
        LoadMapList();

    m_MapRows = std::max(1, (g_ScreenY - MENU_PAD * 6) / MENU_ROW_HEIGHT);

    g_MatchChoice.m_Start = false;
    SelectMap(m_MapSel);
    m_Dirty = true;
}

void CFormMenu::Leave(void) {
    DTRACE();

    ReleaseTexture();
}

void CFormMenu::ReleaseTexture(void) {
    if (m_Texture) {
        g_Cache->Destroy(m_Texture);
        m_Texture = NULL;
    }
}

void CFormMenu::BuildTexture(void) {
    DTRACE();

    ReleaseTexture();

    const int w = g_ScreenX;
    const int h = g_ScreenY;

    CBitmap bmp;
    bmp.CreateRGBA(w, h);
    bmp.Fill(CPoint(0, 0), CPoint(w, h), MENU_COLOR_BACK);

    const int list_w = std::min(420, w / 2 - MENU_PAD * 2);
    const int list_x = MENU_PAD * 2;
    const int list_y = MENU_PAD * 4;
    const int right_x = list_x + list_w + MENU_PAD * 3;
    const int right_w = std::max(200, w - right_x - MENU_PAD * 2);

    DrawString(bmp, list_x, MENU_PAD, w - list_x, MENU_ROW_HEIGHT + 4, L"ПЛАНЕТАРНЫЕ БОИ", MENU_COLOR_ACCENT);

    m_MapRows = std::max(1, (h - list_y - MENU_PAD * 5) / MENU_ROW_HEIGHT);

    // Map list
    DrawString(bmp, list_x, list_y - MENU_ROW_HEIGHT, list_w, MENU_ROW_HEIGHT, L"КАРТА", MENU_COLOR_DIM,
               MENU_FONT_SMALL);
    FillRect(bmp, CRect(list_x, list_y, list_x + list_w, list_y + m_MapRows * MENU_ROW_HEIGHT), MENU_COLOR_PANEL);

    m_MapRects.clear();
    for (int row = 0; row < m_MapRows; ++row) {
        int index = m_MapTop + row;
        CRect r(list_x, list_y + row * MENU_ROW_HEIGHT, list_x + list_w, list_y + (row + 1) * MENU_ROW_HEIGHT);
        m_MapRects.push_back(r);

        if (index >= (int)m_Maps.size())
            continue;

        if (index == m_MapSel)
            FillRect(bmp, r, MENU_COLOR_SELECTED);

        DrawString(bmp, r.left + 10, r.top, list_w - 20, MENU_ROW_HEIGHT, m_Maps[index].m_Name,
                   index == m_MapSel ? MENU_COLOR_ACCENT : MENU_COLOR_TEXT);
    }

    if (!m_Maps.empty()) {
        DrawString(bmp, list_x, list_y + m_MapRows * MENU_ROW_HEIGHT + 4, list_w, MENU_ROW_HEIGHT,
                   utils::format(L"%d / %d", m_MapSel + 1, (int)m_Maps.size()), MENU_COLOR_DIM, MENU_FONT_SMALL);
    }
    else {
        DrawString(bmp, list_x + 10, list_y, list_w - 20, MENU_ROW_HEIGHT, L"Карты не найдены", MENU_COLOR_DISABLED);
    }

    // Sides
    DrawString(bmp, right_x, list_y - MENU_ROW_HEIGHT, right_w, MENU_ROW_HEIGHT, L"СТОРОНА", MENU_COLOR_DIM,
               MENU_FONT_SMALL);

    static const std::vector<int> no_sides;
    const std::vector<int> &sides = m_Maps.empty() ? no_sides : m_Maps[m_MapSel].m_Sides;

    for (int i = 0; i < 4; ++i) {
        int id = i + 1;
        int top = list_y + i * (MENU_ROW_HEIGHT + 8);
        m_SideRects[i] = CRect(right_x, top, right_x + right_w, top + MENU_ROW_HEIGHT);

        std::wstring name;
        DWORD color = MENU_COLOR_DISABLED;
        if (!GetSideInfo(id, name, color))
            continue;

        const bool available = std::find(sides.begin(), sides.end(), id) != sides.end();

        FillRect(bmp, m_SideRects[i], id == m_SideSel ? MENU_COLOR_SELECTED : MENU_COLOR_PANEL);
        FillRect(bmp, CRect(right_x + 6, top + 5, right_x + 6 + 14, top + MENU_ROW_HEIGHT - 5),
                 available ? color : MENU_COLOR_DISABLED);

        DrawString(bmp, right_x + 30, top, right_w - 40, MENU_ROW_HEIGHT,
                   available ? name : name + L" — нет базы на карте",
                   available ? (id == m_SideSel ? MENU_COLOR_ACCENT : MENU_COLOR_TEXT) : MENU_COLOR_DISABLED);
    }

    // Buttons
    const int btn_y = list_y + 4 * (MENU_ROW_HEIGHT + 8) + MENU_PAD * 2;
    const int btn_w = std::min(220, right_w);

    m_StartRect = CRect(right_x, btn_y, right_x + btn_w, btn_y + MENU_ROW_HEIGHT + 10);
    m_ExitRect = CRect(right_x, btn_y + MENU_ROW_HEIGHT + 20, right_x + btn_w, btn_y + 2 * MENU_ROW_HEIGHT + 30);

    const bool can_start = !m_Maps.empty() && m_SideSel != 0;

    FillRect(bmp, m_StartRect, can_start ? MENU_COLOR_ACCENT : MENU_COLOR_PANEL);
    DrawString(bmp, m_StartRect.left, m_StartRect.top, btn_w, MENU_ROW_HEIGHT + 10, L"НАЧАТЬ БОЙ",
               can_start ? MENU_COLOR_BACK : MENU_COLOR_DISABLED, MENU_FONT, 1);

    FillRect(bmp, m_ExitRect, MENU_COLOR_PANEL);
    DrawString(bmp, m_ExitRect.left, m_ExitRect.top, btn_w, MENU_ROW_HEIGHT + 10, L"ВЫХОД", MENU_COLOR_TEXT, MENU_FONT,
               1);

    FillRect(bmp, CRect(list_x, h - MENU_PAD * 2 - 1, w - MENU_PAD * 2, h - MENU_PAD * 2), MENU_COLOR_LINE);
    DrawString(bmp, list_x, h - MENU_PAD * 2, w - list_x, MENU_ROW_HEIGHT,
               L"Стрелки — выбор, Enter — начать, Esc — выход", MENU_COLOR_DIM, MENU_FONT_SMALL);

    CTextureManaged *tex = CACHE_CREATE_TEXTUREMANAGED();
    tex->MipmapOff();

    D3DLOCKED_RECT lr;
    tex->CreateLock(D3DFMT_A8R8G8B8, w, h, 1, lr);

    CBitmap dst;
    dst.CreateRGBA(w, h, lr.Pitch, lr.pBits);
    dst.Copy(CPoint(0, 0), CPoint(w, h), bmp, CPoint(0, 0));
    tex->UnlockRect();

    m_Texture = tex;
    m_Dirty = false;
}

void CFormMenu::Draw(void) {
    DTRACE();

    if (m_Dirty || m_Texture == NULL)
        BuildTexture();

    ASSERT_DX(g_D3DD->Clear(0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, MENU_COLOR_BACK, 1.0f, 0));
    ASSERT_DX(g_D3DD->BeginScene());

    CInstDraw::DrawFrameBegin();

    g_D3DD->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_D3DD->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_D3DD->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    g_D3DD->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_D3DD->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    g_D3DD->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    SetColorOpSelect(0, D3DTA_TEXTURE);
    SetAlphaOpSelect(0, D3DTA_TEXTURE);
    SetColorOpDisable(1);
    g_Sampler.SetState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    g_Sampler.SetState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    g_Sampler.SetState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    g_D3DD->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);

    SVert_V4_UV v[4];
    v[0].tu = 0.0f;
    v[0].tv = 1.0f;
    v[1].tu = 0.0f;
    v[1].tv = 0.0f;
    v[2].tu = 1.0f;
    v[2].tv = 1.0f;
    v[3].tu = 1.0f;
    v[3].tv = 0.0f;

    v[0].p = D3DXVECTOR4(-0.5f, float(g_ScreenY) - 0.5f, 0.5f, 1.0f);
    v[1].p = D3DXVECTOR4(-0.5f, -0.5f, 0.5f, 1.0f);
    v[2].p = D3DXVECTOR4(float(g_ScreenX) - 0.5f, float(g_ScreenY) - 0.5f, 0.5f, 1.0f);
    v[3].p = D3DXVECTOR4(float(g_ScreenX) - 0.5f, -0.5f, 0.5f, 1.0f);

    CInstDraw::BeginDraw(IDFVF_V4_UV);
    CInstDraw::AddVerts(v, m_Texture);
    CInstDraw::ActualDraw();

    ASSERT_DX(g_D3DD->EndScene());
    ASSERT_DX(g_D3DD->Present(NULL, NULL, NULL, NULL));
}

void CFormMenu::Takt([[maybe_unused]] int step) {}

void CFormMenu::MouseMove([[maybe_unused]] int x, [[maybe_unused]] int y) {}

void CFormMenu::MouseKey(ButtonStatus status, int key, int x, int y) {
    DTRACE();

    const CPoint pos(x, y);

    if (status == B_WHEEL) {
        SelectMap(m_MapSel - key);
        return;
    }

    if (status != B_DOWN && status != B_DOUBLE)
        return;
    if (key != VK_LBUTTON)
        return;

    for (int row = 0; row < (int)m_MapRects.size(); ++row) {
        if (!m_MapRects[row].IsInRect(pos))
            continue;

        int index = m_MapTop + row;
        if (index < (int)m_Maps.size())
            SelectMap(index);
        return;
    }

    for (int i = 0; i < 4; ++i) {
        if (!m_SideRects[i].IsInRect(pos))
            continue;

        int id = i + 1;
        if (m_Maps.empty())
            return;

        const std::vector<int> &sides = m_Maps[m_MapSel].m_Sides;
        if (std::find(sides.begin(), sides.end(), id) != sides.end()) {
            m_SideSel = id;
            m_Dirty = true;
        }
        return;
    }

    if (m_StartRect.IsInRect(pos)) {
        Start();
        return;
    }

    if (m_ExitRect.IsInRect(pos))
        Quit();
}

void CFormMenu::Keyboard(bool down, uint8_t vk) {
    DTRACE();

    if (!down)
        return;

    switch (vk) {
        case VK_UP:
            SelectMap(m_MapSel - 1);
            break;
        case VK_DOWN:
            SelectMap(m_MapSel + 1);
            break;
        case VK_PRIOR:
            SelectMap(m_MapSel - m_MapRows);
            break;
        case VK_NEXT:
            SelectMap(m_MapSel + m_MapRows);
            break;
        case VK_HOME:
            SelectMap(0);
            break;
        case VK_END:
            SelectMap((int)m_Maps.size() - 1);
            break;
        case VK_LEFT:
        case VK_RIGHT: {
            if (m_Maps.empty())
                break;

            const std::vector<int> &sides = m_Maps[m_MapSel].m_Sides;
            if (sides.empty())
                break;

            auto it = std::find(sides.begin(), sides.end(), m_SideSel);
            int at = (it == sides.end()) ? 0 : (int)(it - sides.begin());
            at += (vk == VK_LEFT) ? -1 : 1;
            at = (at + (int)sides.size()) % (int)sides.size();
            m_SideSel = sides[at];
            m_Dirty = true;
            break;
        }
        case VK_RETURN:
            Start();
            break;
        case VK_ESCAPE:
            Quit();
            break;
    }
}

void CFormMenu::SystemEvent([[maybe_unused]] ESysEvent se) {}

void CFormMenu::Start(void) {
    if (m_Maps.empty() || m_SideSel == 0)
        return;

    g_MatchChoice.m_Map = m_Maps[m_MapSel].m_Path;
    g_MatchChoice.m_SideId = m_SideSel;
    g_MatchChoice.m_Start = true;

    // Loading a map clears the cache, which owns our texture. Let go of it before that happens, or
    // Leave() would later destroy a texture that is already gone. Draw() rebuilds it when needed.
    ReleaseTexture();
    m_Dirty = true;

    SETFLAG(g_Flags, GFLAG_EXITLOOP);
}

void CFormMenu::Quit(void) {
    g_MatchChoice.m_Start = false;

    ReleaseTexture();
    m_Dirty = true;

    SETFLAG(g_Flags, GFLAG_EXITLOOP);
}
