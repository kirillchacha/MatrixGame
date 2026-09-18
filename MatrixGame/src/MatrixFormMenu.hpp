// MatrixGame - SR2 Planetary battles engine
// Copyright (C) 2012, Elemental Games, Katauri Interactive, CHK-Games
// Licensed under GPLv2 or any later version
// Refer to the LICENSE file included

#pragma once

#include "Form.hpp"
#include "BaseDef.hpp"
#include "MatchTeams.hpp"

#include <string>
#include <vector>

class CTextureManaged;

namespace Base {
class CStorage;
}

// Side ids a human can take over on an already loaded map: the menu greys out the rest and
// CGame::StartMatch() refuses anything else. See PlayableSides.hpp for the rule itself.
std::vector<int> ReadPlayableSides(Base::CStorage &stor);

// What the player picked in the menu. Filled in by CFormMenu, read by WinMain.
struct SMatchChoice {
    std::wstring m_Map;  // map path inside the package, ready for CGame::StartMatch()
    int m_SideId;        // side id from the "Side" config block
    bool m_Start;        // false means the player chose to quit instead
    CMatchTeams m_Teams;
};

/**
 * @brief The screen shown before a match: pick a map and the side to play.
 *
 * Runs after CGame::InitEngine(), when Direct3D is up but no map exists, so it must not touch
 * g_MatrixMap or the in-game interface. Everything is drawn into one screen sized bitmap which is
 * uploaded as a single texture, the same way hints build theirs (Interface/MatrixHint.cpp).
 */
class CFormMenu : public CForm {
    struct SMapItem {
        std::wstring m_Name;   // file name without the extension, as shown
        std::wstring m_Path;   // full path inside the package
        std::vector<int> m_Sides;  // playable side ids, empty until read from the map
        bool m_SidesKnown;
    };

    std::vector<SMapItem> m_Maps;
    int m_MapSel;    // index in m_Maps
    int m_MapTop;    // first visible row
    int m_MapRows;   // visible rows, depends on the screen height
    int m_SideSel;   // side id, 0 when the current map offers none
    bool m_TeamMode = false;
    CMatchTeams m_Teams;
    Base::CRect m_ModeRects[2];
    Base::CRect m_TeamRects[4];
    int m_Hover = -1;

    bool CanStart() const;
    void ToggleMode();

    std::vector<Base::CRect> m_MapRects;   // hit areas, m_MapRects[i] belongs to row m_MapTop + i
    Base::CRect m_SideRects[4];
    Base::CRect m_StartRect;
    Base::CRect m_ExitRect;

    CTextureManaged *m_Texture;
    CTextureManaged *m_Preview;    // picture of the selected map, NULL when the package has none
    Base::CPoint m_PreviewSize{0, 0}; // image dimensions before D3D pads to a power of two
    Base::CRect m_PreviewRect;     // where Draw() puts the picture, laid out by BuildTexture()
    bool m_Dirty;  // selection changed, the screen sized texture has to be rebuilt

    void LoadMapList(void);
    void ReadMapSides(SMapItem &item);
    void SelectMap(int index);
    void BuildTexture(void);
    void ReleaseTexture(void);
    void LoadPreview(void);
    void ReleasePreview(void);
    void Start(void);
    void Quit(void);

public:
    CFormMenu(void);
    ~CFormMenu();

    virtual void Enter(void);
    virtual void Leave(void);
    virtual void Draw(void);
    virtual void Takt(int step);
    virtual void MouseMove(int x, int y);
    virtual void MouseKey(ButtonStatus status, int key, int x, int y);
    virtual void Keyboard(bool down, uint8_t vk);
    virtual void SystemEvent(ESysEvent se);
};

extern SMatchChoice g_MatchChoice;
