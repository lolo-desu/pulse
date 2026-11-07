#include "ui_renderer_internal.h"
#include <shellapi.h>

namespace pulse::ui {
namespace {
ComPtr<ID2D1Bitmap> DecodeIcon(ID2D1DeviceContext* dc, HICON icon) {
    ComPtr<ID2D1Bitmap> bitmap;
    ComPtr<IWICImagingFactory> wic;
    ComPtr<IWICBitmap> source;
    ComPtr<IWICFormatConverter> converter;
    if (icon && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic))) &&
        SUCCEEDED(wic->CreateBitmapFromHICON(icon, &source)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
        SUCCEEDED(converter->Initialize(source.get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                       nullptr, 0, WICBitmapPaletteTypeMedianCut)))
        dc->CreateBitmapFromWicBitmap(converter.get(), nullptr, &bitmap);
    return bitmap;
}
}

bool MainRenderer::DrawClassicShellIcon(int stock_id, const D2D1_RECT_F& rect, float opacity) {
    auto* dc = compositor_ ? compositor_->Dc() : nullptr;
    if (!dc || rect.right <= rect.left || rect.bottom <= rect.top) return false;
    if (classic_icon_dc_ != dc) { classic_icon_bitmaps_.clear(); classic_icon_dc_ = dc; }
    const bool use_small_icon = rect.right - rect.left <= 20.0f;
    const int key = stock_id | (use_small_icon ? 0x10000 : 0);
    auto found = classic_icon_bitmaps_.find(key);
    if (found == classic_icon_bitmaps_.end()) {
        SHSTOCKICONINFO info{};
        info.cbSize = sizeof(info);
        if (FAILED(SHGetStockIconInfo(static_cast<SHSTOCKICONID>(stock_id),
                                     SHGSI_ICON | (use_small_icon ? SHGSI_SMALLICON : SHGSI_LARGEICON), &info))) return false;
        auto bitmap = DecodeIcon(dc, info.hIcon);
        DestroyIcon(info.hIcon);
        found = classic_icon_bitmaps_.emplace(key, std::move(bitmap)).first;
    }
    if (!found->second.get()) return false;
    dc->DrawBitmap(found->second.get(), rect, opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
    return true;
}

bool MainRenderer::DrawClassicCommandIcon(const std::wstring& glyph, const D2D1_RECT_F& rect, float opacity, D2D1_COLOR_F ink) {
    if (glyph.empty() || !compositor_) return false;
    const wchar_t code = glyph[0];
    if (code == 0xE8C8 || code == 0xE77F || code == 0xE946) {
        auto* dc = compositor_->Dc();
        if (!dc) return false;
        const float k = std::min(rect.right - rect.left, rect.bottom - rect.top) / 28.0f;
        const float x = rect.left, y = rect.top;
        ComPtr<ID2D1SolidColorBrush> brush;
        dc->CreateSolidColorBrush(D2D1::ColorF(0xFFFFFF, opacity), &brush);
        if (!brush.get()) return false;
        const auto fill = [&](float l, float t, float r, float b, UINT32 color) {
            brush->SetColor(D2D1::ColorF(color, opacity));
            dc->FillRectangle({x + l*k, y + t*k, x + r*k, y + b*k}, brush.get());
        };
        const auto paper = [&](float l, float t, float r, float b) {
            fill(l, t, r, b, 0x929292);
            fill(l+1, t+1, r-1, b-1, 0xE8E8E8);
            for (float row : {t+6, t+10, t+14}) if (row < b-2)
                fill(l+3, row, r-3, row+1, 0xA5A5A5);
        };
        if (code == 0xE8C8) {
            paper(3, 1, 20, 23);
            paper(9, 6, 26, 28);
        } else if (code == 0xE77F) {
            fill(3, 3, 23, 27, 0x8D8577);
            fill(4, 4, 22, 26, 0xC6B99E);
            fill(9, 1, 17, 6, 0x939393);
            fill(10, 2, 16, 5, 0xD4D4D4);
            paper(10, 10, 27, 28);
        } else {
            paper(6, 1, 24, 27);
            fill(9, 7, 13, 11, 0x6091B6);
            fill(10, 8, 12, 10, 0xE8E8E8);
            fill(9, 13, 13, 17, 0x6091B6);
            fill(10, 14, 12, 16, 0xE8E8E8);
            fill(9, 19, 13, 23, 0x6091B6);
            fill(10, 20, 12, 22, 0xE8E8E8);
        }
        return true;
    }
    int stock = -1;
    switch (code) {
    case 0xE8F4: case 0xE8B7: case 0xE8F1: stock = SIID_FOLDER; break;
    case 0xE8AC: stock = SIID_RENAME; break;
    case 0xE74D: stock = SIID_DELETE; break;
    case 0xE8CE: stock = SIID_DRIVENET; break;
    case 0xE8A5: case 0xE7C3: case 0xE943: stock = SIID_DOCNOASSOC; break;
    default: break;
    }
    if (stock >= 0) return DrawClassicShellIcon(stock, rect, opacity);
    auto* dc = compositor_->Dc();
    if (!dc) return false;
    if (classic_icon_dc_ != dc) { classic_icon_bitmaps_.clear(); classic_icon_dc_ = dc; }
    // Use Windows 10's native icon font for controls without a stock shell icon.
    // Pulse-private glyphs deliberately fall back to their existing renderer.
    if (code >= 0xF000) return false;
    ComPtr<IDWriteTextFormat> format;
    const float size = std::min(rect.right - rect.left, rect.bottom - rect.top) * 0.8f;
    if (FAILED(compositor_->DwriteFactory()->CreateTextFormat(L"Segoe MDL2 Assets", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-US", &format))) return false;
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    // Native icon glyphs retain neutral command ink in both themes.
    auto color = ink;
    color.a *= opacity;
    ComPtr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(color, &brush);
    if (!brush.get()) return false;
    dc->DrawTextW(glyph.c_str(), 1, format.get(), rect, brush.get());
    return true;
}
}
