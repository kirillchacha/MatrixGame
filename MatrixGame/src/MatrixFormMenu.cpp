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

#define MENU_FONT       L"Font.MenuNormal"
#define MENU_FONT_SMALL L"Font.MenuSmall"

#define MENU_COLOR_BACK      0xFF0F1419
#define MENU_COLOR_PANEL     0xFF182027
#define MENU_COLOR_SELECTED  0xFF2E3E46
#define MENU_COLOR_LINE      0xFF313F46
#define MENU_COLOR_TEXT      0xFFFFFFFF
#define MENU_COLOR_DIM       0xFF879AA5
#define MENU_COLOR_ACCENT    0xFFCAEE78
#define MENU_COLOR_DISABLED  0xFF4B5A62

#define MENU_ROW_HEIGHT 32
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
  : CForm(), m_MapSel(0), m_MapTop(0), m_MapRows(0), m_SideSel(PLAYER_SIDE_DEFAULT), m_Texture(NULL),
    m_Preview(NULL), m_PreviewRect(0, 0, 0, 0), m_Dirty(true) {
    m_Name = L"FormMenu";
}

CFormMenu::~CFormMenu() {
    ReleaseTexture();
    ReleasePreview();
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

std::vector<int> ReadPlayableSides(Base::CStorage &stor) {
    CPlayableSides sides;

    CDataBuf *bside = stor.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_SIDE, ST_BYTE);
    CDataBuf *bkind = stor.GetBuf(DATA_BUILDINGS, DATA_BUILDINGS_KIND, ST_BYTE);
    if (bside != NULL && bkind != NULL && bside->GetArraysCount() > 0 && bkind->GetArraysCount() > 0)
        sides.AddBuildings({bside->GetFirst<BYTE>(0), bside->GetArrayLength(0)},
                           {bkind->GetFirst<BYTE>(0), bkind->GetArrayLength(0)}, BUILDING_BASE);

    // A side without a base is still playable while the map gives it robots: it cannot build,
    // but the engine keeps it in the match and it can capture buildings.
    CDataBuf *rside = stor.GetBuf(DATA_ROBOTS, DATA_ROBOTS_SIDE, ST_BYTE);
    if (rside != NULL && rside->GetArraysCount() > 0)
        sides.AddRobots({rside->GetFirst<BYTE>(0), rside->GetArrayLength(0)});

    return sides.Result();
}

void CFormMenu::ReadMapSides(SMapItem &item) {
    if (item.m_SidesKnown)
        return;

    item.m_SidesKnown = true;

    try {
        CStorage stor;
        if (!stor.Load(item.m_Path.c_str()))
            return;

        item.m_Sides = ReadPlayableSides(stor);
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
    LoadPreview();

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
    ReleasePreview();
}

void CFormMenu::ReleaseTexture(void) {
    if (m_Texture) {
        g_Cache->Destroy(m_Texture);
        m_Texture = NULL;
    }
}

// The original game ships a top down picture next to every map (Matrix\Map\<name>.jpg inside the
// package), which is what its own map picker shows. Ten of the 84 maps have none.
void CFormMenu::LoadPreview(void) {
    DTRACE();

    ReleasePreview();

    if (m_Maps.empty())
        return;

    // The texture cache looks the extension up itself, so it wants the name without one.
    const std::wstring name = std::wstring(MENU_MAP_FOLDER) + L"\\" + m_Maps[m_MapSel].m_Name;

    std::wstring found;
    if (!CFile::FileExist(found, name.c_str(), CacheExtsTex))
        return;

    CTextureManaged *tex = (CTextureManaged *)g_Cache->Get(CacheClass::TextureManaged, name.c_str());

    try {
        tex->Preload();
        CFile file(found);
        file.OpenRead();
        std::vector<BYTE> bytes(file.Size());
        file.Read(bytes.data(), (DWORD)bytes.size());
        D3DXIMAGE_INFO info{};
        if (FAILED(D3DXGetImageInfoFromFileInMemory(bytes.data(), (UINT)bytes.size(), &info)))
            throw std::runtime_error("Cannot read map preview dimensions.");
        m_PreviewSize = CPoint(std::min((int)info.Width, tex->GetSizeX()),
                               std::min((int)info.Height, tex->GetSizeY()));
    }
    catch (...) {
        // A broken picture is no reason to keep the player out of the map.
        g_Cache->Delete(tex);
        CCache::Destroy(tex);
        return;
    }

    if (tex->GetSizeX() <= 0 || tex->GetSizeY() <= 0) {
        g_Cache->Delete(tex);
        CCache::Destroy(tex);
        return;
    }

    m_Preview = tex;
}

void CFormMenu::ReleasePreview(void) {
    if (m_Preview) {
        // Get() put it into the cache index, so take it out before freeing it.
        g_Cache->Delete(m_Preview);
        CCache::Destroy(m_Preview);
        m_Preview = NULL;
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

    const int pad = w < 900 ? 24 : 40;
    const int list_x = pad;
    const int list_y = 112;
    const int list_w = std::min(360, w / 3);
    const int right_x = list_x + list_w + 28;
    const int right_w = w - right_x - pad;
    const int bottom = h - 76;

    // Restrained tactical display: slate panels, fine grid and a single lime accent.
    for (int y = 0; y < h; y += 48)
        FillRect(bmp, CRect(0, y, w, y + 1), 0xFF141C23);
    FillRect(bmp, CRect(0, 0, 6, h), MENU_COLOR_ACCENT);
    DrawString(bmp, list_x, 18, w - 2 * pad, 34, L"ПЛАНЕТАРНЫЕ БОИ", MENU_COLOR_ACCENT, L"Font.MenuTitle");
    DrawString(bmp, list_x, 54, w - 2 * pad, 22, L"ПОДГОТОВКА ОПЕРАЦИИ / ЛОКАЛЬНЫЙ БОЙ", MENU_COLOR_DIM, MENU_FONT_SMALL);
    FillRect(bmp, CRect(pad, 88, w - pad, 89), MENU_COLOR_LINE);

    DrawString(bmp, list_x, list_y - 24, list_w, 24,
               utils::format(L"01 / КАРТА   ·   %d", (int)m_Maps.size()), MENU_COLOR_DIM, MENU_FONT_SMALL);
    m_MapRows = std::max(1, (bottom - list_y - 30) / MENU_ROW_HEIGHT);
    m_MapTop = std::clamp(m_MapTop, 0, std::max(0, (int)m_Maps.size() - m_MapRows));
    const int list_bottom = list_y + m_MapRows * MENU_ROW_HEIGHT;
    FillRect(bmp, CRect(list_x, list_y, list_x + list_w, list_bottom), MENU_COLOR_PANEL);
    m_MapRects.clear();
    for (int row = 0; row < m_MapRows; ++row) {
        const int index = m_MapTop + row;
        CRect r(list_x, list_y + row * MENU_ROW_HEIGHT, list_x + list_w - 8,
                list_y + (row + 1) * MENU_ROW_HEIGHT);
        m_MapRects.push_back(r);
        if (index >= (int)m_Maps.size())
            continue;
        const bool selected = index == m_MapSel;
        if (selected || m_Hover == row)
            FillRect(bmp, r, selected ? MENU_COLOR_SELECTED : 0xFF202D36);
        if (selected)
            FillRect(bmp, CRect(r.left, r.top + 5, r.left + 3, r.bottom - 5), MENU_COLOR_ACCENT);
        DrawString(bmp, r.left + 14, r.top, list_w - 30, MENU_ROW_HEIGHT, m_Maps[index].m_Name,
                   selected ? MENU_COLOR_ACCENT : MENU_COLOR_TEXT);
    }
    if (!m_Maps.empty()) {
        const int track = list_bottom - list_y;
        const int thumb = std::max(16, track * std::min(m_MapRows, (int)m_Maps.size()) / (int)m_Maps.size());
        const int offset = (int)m_Maps.size() > m_MapRows
            ? (track - thumb) * m_MapTop / ((int)m_Maps.size() - m_MapRows) : 0;
        FillRect(bmp, CRect(list_x + list_w - 4, list_y + offset, list_x + list_w - 2,
                           list_y + offset + thumb), MENU_COLOR_ACCENT);
        DrawString(bmp, list_x, list_bottom + 6, list_w, 24,
                   utils::format(L"%02d / %02d    Колесо — листать", m_MapSel + 1, (int)m_Maps.size()),
                   MENU_COLOR_DIM, MENU_FONT_SMALL);
    }

    const int preview_bottom = std::max(list_y + 100, h - 414);
    DrawString(bmp, right_x, list_y - 24, right_w, 24,
               m_Maps.empty() ? L"Карты не найдены" : m_Maps[m_MapSel].m_Name,
               MENU_COLOR_TEXT);
    FillRect(bmp, CRect(right_x - 1, list_y - 1, right_x + right_w + 1, preview_bottom + 1), MENU_COLOR_LINE);
    m_PreviewRect = CRect(right_x, list_y, right_x + right_w, preview_bottom);
    FillRect(bmp, m_PreviewRect, MENU_COLOR_PANEL);
    if (!m_Preview)
        DrawString(bmp, right_x, (list_y + preview_bottom) / 2 - 12, right_w, 24,
                   L"Разведданные отсутствуют", MENU_COLOR_DIM, MENU_FONT, 1);

    const int mode_y = preview_bottom + 30;
    DrawString(bmp, right_x, mode_y - 24, right_w, 24, L"02 / ПРАВИЛА БОЯ", MENU_COLOR_DIM, MENU_FONT_SMALL);
    const int mode_w = (right_w - 8) / 2;
    for (int i = 0; i < 2; ++i) {
        m_ModeRects[i] = CRect(right_x + i * (mode_w + 8), mode_y,
                               right_x + i * (mode_w + 8) + mode_w, mode_y + 32);
        const bool selected = m_TeamMode == (i == 1);
        FillRect(bmp, m_ModeRects[i], selected ? MENU_COLOR_SELECTED : MENU_COLOR_PANEL);
        if (selected)
            FillRect(bmp, CRect(m_ModeRects[i].left, mode_y + 30, m_ModeRects[i].right, mode_y + 32), MENU_COLOR_ACCENT);
        DrawString(bmp, m_ModeRects[i].left, mode_y, mode_w, 30,
                   i == 0 ? L"Все против всех" : L"Командный бой",
                   selected ? MENU_COLOR_ACCENT : MENU_COLOR_DIM, MENU_FONT, 1);
    }
    const int sides_y = mode_y + 64;
    DrawString(bmp, right_x, sides_y - 26, right_w, 24, L"03 / ВАШ ЦВЕТ И СОСТАВ КОМАНД", MENU_COLOR_DIM, MENU_FONT_SMALL);
    static const std::vector<int> no_sides;
    const auto &sides = m_Maps.empty() ? no_sides : m_Maps[m_MapSel].m_Sides;
    const int team_w = std::min(150, right_w / 3);
    for (int i = 0; i < 4; ++i) {
        const int id = i + 1;
        const int y = sides_y + i * 40;
        const bool available = std::find(sides.begin(), sides.end(), id) != sides.end();
        const bool selected = id == m_SideSel;
        std::wstring name;
        DWORD color = MENU_COLOR_DISABLED;
        GetSideInfo(id, name, color);
        m_SideRects[i] = CRect(right_x, y, right_x + right_w - team_w - 8, y + 34);
        m_TeamRects[i] = CRect(right_x + right_w - team_w, y, right_x + right_w, y + 34);
        FillRect(bmp, m_SideRects[i], selected ? MENU_COLOR_SELECTED : MENU_COLOR_PANEL);
        FillRect(bmp, CRect(right_x + 10, y + 10, right_x + 24, y + 24), available ? color : MENU_COLOR_DISABLED);
        DrawString(bmp, right_x + 34, y, m_SideRects[i].right - right_x - 38, 34,
                   name + (available ? (selected ? L" / ВЫ" : L" / ИИ") : L" / нет войск"),
                   available ? MENU_COLOR_TEXT : MENU_COLOR_DISABLED, MENU_FONT_SMALL);
        FillRect(bmp, m_TeamRects[i], available && m_TeamMode ? MENU_COLOR_SELECTED : MENU_COLOR_PANEL);
        DrawString(bmp, m_TeamRects[i].left, y, team_w, 34,
                   !available ? L"—" : m_TeamMode ? utils::format(L"Команда %d  >", m_Teams.teams[id]) : L"Сам за себя",
                   available && m_TeamMode ? MENU_COLOR_ACCENT : MENU_COLOR_DIM, MENU_FONT_SMALL, 1);
    }
    const bool can_start = CanStart();
    DrawString(bmp, right_x, sides_y + 161, right_w, 24,
               m_TeamMode ? (can_start ? L"Одна команда — союзники. Нажмите номер для смены." : L"Назначьте хотя бы одну сторону в другую команду.")
                          : L"Выберите цвет. Остальными сторонами управляет ИИ.",
               can_start ? MENU_COLOR_DIM : MENU_COLOR_ACCENT, MENU_FONT_SMALL);
    const int exit_w = 100;
    m_ExitRect = CRect(pad, h - 56, pad + exit_w, h - 20);
    m_StartRect = CRect(right_x, h - 56, right_x + right_w, h - 20);
    FillRect(bmp, m_ExitRect, MENU_COLOR_PANEL);
    DrawString(bmp, pad, h - 56, exit_w, 36, L"ВЫХОД", MENU_COLOR_DIM, MENU_FONT_SMALL, 1);
    FillRect(bmp, m_StartRect, can_start ? MENU_COLOR_ACCENT : MENU_COLOR_PANEL);
    DrawString(bmp, right_x, h - 56, right_w, 36, L"НАЧАТЬ БОЙ  /  ENTER",
               can_start ? MENU_COLOR_BACK : MENU_COLOR_DISABLED, MENU_FONT, 1);

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

    if (m_Preview != NULL && !m_PreviewRect.IsEmpty()) {
        // Fit the picture into its panel without stretching it.
        const float box_w = float(m_PreviewRect.right - m_PreviewRect.left);
        const float box_h = float(m_PreviewRect.bottom - m_PreviewRect.top);
        const float scale = std::min(box_w / float(m_PreviewSize.x), box_h / float(m_PreviewSize.y));
        const float pic_w = float(m_PreviewSize.x) * scale;
        const float pic_h = float(m_PreviewSize.y) * scale;
        v[2].tu = v[3].tu = float(m_PreviewSize.x) / float(m_Preview->GetSizeX());
        v[0].tv = v[2].tv = float(m_PreviewSize.y) / float(m_Preview->GetSizeY());
        const float left = float(m_PreviewRect.left) + (box_w - pic_w) * 0.5f - 0.5f;
        const float top = float(m_PreviewRect.top) + (box_h - pic_h) * 0.5f - 0.5f;

        v[0].p = D3DXVECTOR4(left, top + pic_h, 0.5f, 1.0f);
        v[1].p = D3DXVECTOR4(left, top, 0.5f, 1.0f);
        v[2].p = D3DXVECTOR4(left + pic_w, top + pic_h, 0.5f, 1.0f);
        v[3].p = D3DXVECTOR4(left + pic_w, top, 0.5f, 1.0f);

        // The menu itself is drawn pixel to pixel, the picture is scaled down and needs filtering.
        g_Sampler.SetState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        g_Sampler.SetState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        g_Sampler.SetState(0, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);

        CInstDraw::BeginDraw(IDFVF_V4_UV);
        CInstDraw::AddVerts(v, m_Preview);
        CInstDraw::ActualDraw();
    }

    ASSERT_DX(g_D3DD->EndScene());
    ASSERT_DX(g_D3DD->Present(NULL, NULL, NULL, NULL));
}

void CFormMenu::Takt([[maybe_unused]] int step) {}

void CFormMenu::MouseMove(int x, int y) {
    int hover = -1;
    for (int i = 0; i < (int)m_MapRects.size(); ++i)
        if (m_MapRects[i].IsInRect(CPoint(x, y)))
            hover = i;
    if (hover != m_Hover) {
        m_Hover = hover;
        m_Dirty = true;
    }
}

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

    for (int i = 0; i < 2; ++i) {
        if (m_ModeRects[i].IsInRect(pos)) {
            if (m_TeamMode != (i == 1))
                ToggleMode();
            return;
        }
    }
    if (m_TeamMode && !m_Maps.empty()) {
        for (int id : m_Maps[m_MapSel].m_Sides) {
            if (m_TeamRects[id - 1].IsInRect(pos)) {
                m_Teams.teams[id] = m_Teams.teams[id] % 4 + 1;
                m_Dirty = true;
                return;
            }
        }
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
    if (!CanStart())
        return;

    g_MatchChoice.m_Map = m_Maps[m_MapSel].m_Path;
    g_MatchChoice.m_SideId = m_SideSel;
    g_MatchChoice.m_Teams = m_TeamMode ? m_Teams : CMatchTeams{};
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

bool CFormMenu::CanStart() const {
    if (m_Maps.empty() || m_SideSel == 0)
        return false;
    return !m_TeamMode || m_Teams.HasOpponent(m_Maps[m_MapSel].m_Sides, m_SideSel);
}

void CFormMenu::ToggleMode() {
    m_TeamMode = !m_TeamMode;
    if (m_TeamMode && !m_Maps.empty()) {
        // Split the actual participants, including maps with missing colours.
        int index = 0;
        for (int id : m_Maps[m_MapSel].m_Sides)
            m_Teams.teams[id] = index++ % 2 + 1;
    }
    m_Dirty = true;
}
