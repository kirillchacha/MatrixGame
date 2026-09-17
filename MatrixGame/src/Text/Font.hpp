#pragma once

#include <d3dx9core.h>

#include <string>

namespace Text {

class Font
{
public:
    // no copy, no move
    Font(const Font&) = delete;
    Font(Font&&) = delete;
    Font& operator = (const Font&) = delete;
    Font& operator = (Font&&) = delete;

    Font(LPD3DXFONT font);

    ~Font();

    LPD3DXFONT operator -> () const;

    explicit operator bool () const;

    size_t GetHeight() const;

    size_t GetSpaceWidth() const;

    size_t CalcTextWidth(std::wstring_view text) const;

private:
    LPD3DXFONT m_font{nullptr};
};

Font& GetFont(IDirect3DDevice9* device, std::wstring_view font_name);

// ID3DXFont keeps device resources, so it has to be told about a device reset. Call these around
// every g_D3DD->Reset(), or the reset fails once any text has been drawn.
void OnLostDevice(void);
void OnResetDevice(void);

} // namespace Text
