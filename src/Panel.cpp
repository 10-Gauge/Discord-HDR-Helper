#include "Panel.h"
#include "App.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <wincodec.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

using namespace std::chrono;

// Segoe Fluent Icons (Windows 11) / Segoe MDL2 Assets (Windows 10) glyphs, as UTF-8.
#define ICON_DISPLAY    "\xEE\x9F\xB4"
#define ICON_WINDOW     "\xEE\xA2\xA7"
#define ICON_REFRESH    "\xEE\x9C\xAC"
#define ICON_CHECK      "\xEE\x9C\xBE"
#define ICON_STOP       "\xEE\x9C\x9A"
#define ICON_BRIGHTNESS "\xEE\x9C\x86"
#define ICON_HIGHLIGHT  "\xEE\x9F\xA6"
#define ICON_CURVE      "\xEE\xA7\xA9"
#define ICON_SETTINGS   "\xEE\x9C\x93"
#define ICON_INFO       "\xEE\xA5\x86"
#define ICON_VIDEO      "\xEE\x9C\x94"
#define ICON_VOLUME     "\xEE\x9D\xA7"
#define ICON_WARNING    "\xEE\x9E\xBA"

namespace
{
    constexpr wchar_t kClass[] = L"DiscordHDRHelperPanel";
    constexpr wchar_t kTitle[] = L"Discord HDR Helper";
    constexpr char kVersion[] = "v" APP_VERSION;
    constexpr wchar_t kRepoUrl[] = L"https://github.com/10-Gauge/Discord-HDR-Helper";

    // Logical (96 DPI) size of the panel.
    constexpr int kWidth = 1040;
    constexpr int kHeight = 740;
    constexpr float kRightColumn = 340.f;

    // Palette, close to Discord's own dark theme so the two feel related.
    constexpr ImU32 Rgb(uint32_t hex, uint8_t alpha = 255)
    {
        return IM_COL32((hex >> 16) & 0xFF, (hex >> 8) & 0xFF, hex & 0xFF, alpha);
    }
    ImVec4 Vec(uint32_t hex, float alpha = 1.f)
    {
        return ImVec4(((hex >> 16) & 0xFF) / 255.f, ((hex >> 8) & 0xFF) / 255.f, (hex & 0xFF) / 255.f, alpha);
    }
    constexpr uint32_t kBg = 0x1A1B1E;
    constexpr uint32_t kCard = 0x232428;
    constexpr uint32_t kCardRaised = 0x2B2D31;
    constexpr uint32_t kHover = 0x313338;
    constexpr uint32_t kBorder = 0x35373C;
    constexpr uint32_t kText = 0xF2F3F5;
    constexpr uint32_t kMuted = 0x949BA4;
    constexpr uint32_t kFaint = 0x6D6F78;
    constexpr uint32_t kAccent = 0x5865F2;
    constexpr uint32_t kAccentHover = 0x6D78F5;
    constexpr uint32_t kGreen = 0x23A55A;
    constexpr uint32_t kRed = 0xDA373C;

    std::string Utf8(const std::wstring& w)
    {
        if (w.empty())
            return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::wstring FontPath(const wchar_t* file)
    {
        wchar_t dir[MAX_PATH] = {};
        GetWindowsDirectoryW(dir, MAX_PATH);
        return std::wstring(dir) + L"\\Fonts\\" + file;
    }

    bool FileExists(const std::wstring& path)
    {
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    // ---- Small custom widgets ---------------------------------------------------

    float g_scale = 1.f;
    float S(float v) { return v * g_scale; }

    void SectionTitle(ImFont* font, const char* icon, const char* text)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Vec(kAccentHover));
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, S(8));
        ImGui::PushFont(font);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
    }

    void MutedText(const char* text, bool wrap = true)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Vec(kMuted));
        if (wrap)
            ImGui::TextWrapped("%s", text);
        else
            ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }

    // A label on the left and an animated switch on the right; the whole row is clickable.
    bool Toggle(const char* label, bool* value)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float rowW = ImGui::GetContentRegionAvail().x;
        float rowH = ImGui::GetFrameHeight();
        ImGui::PushID(label);
        bool clicked = ImGui::InvisibleButton("##toggle", ImVec2(rowW, rowH));
        bool hovered = ImGui::IsItemHovered();
        if (clicked)
            *value = !*value;

        // Ease the knob between positions.
        ImGuiStorage* storage = ImGui::GetStateStorage();
        ImGuiID animId = ImGui::GetID("anim");
        float t = storage->GetFloat(animId, *value ? 1.f : 0.f);
        float target = *value ? 1.f : 0.f;
        t += (target - t) * std::min(1.f, ImGui::GetIO().DeltaTime * 18.f);
        storage->SetFloat(animId, t);
        ImGui::PopID();

        float textY = pos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
        dl->AddText(ImVec2(pos.x, textY), Rgb(kText), label);

        float h = S(20), w = S(36);
        ImVec2 p0(pos.x + rowW - w, pos.y + (rowH - h) * 0.5f);
        ImVec2 p1(p0.x + w, p0.y + h);
        ImU32 off = hovered ? Rgb(0x4E5058) : Rgb(0x404249);
        ImU32 on = hovered ? Rgb(kAccentHover) : Rgb(kAccent);
        ImVec4 a = ImGui::ColorConvertU32ToFloat4(off), b = ImGui::ColorConvertU32ToFloat4(on);
        ImVec4 mix(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.f);
        dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(mix), h * 0.5f);
        float r = h * 0.5f - S(3);
        float cx = p0.x + h * 0.5f + (w - h) * t;
        dl->AddCircleFilled(ImVec2(cx, p0.y + h * 0.5f), r, Rgb(0xFFFFFF), 24);
        return clicked;
    }

    // Pill-shaped segmented control. Returns true when the selection changes.
    bool Segmented(const char* id, const char* const* items, int count, int* selected, float width)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float h = ImGui::GetFrameHeight();
        dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), Rgb(0x1E1F22), S(8));

        bool changed = false;
        float segW = width / count;
        ImGui::PushID(id);
        for (int i = 0; i < count; ++i)
        {
            ImGui::SetCursorScreenPos(ImVec2(pos.x + segW * i, pos.y));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##seg", ImVec2(segW, h)) && *selected != i)
            {
                *selected = i;
                changed = true;
            }
            bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            ImVec2 a(pos.x + segW * i + S(3), pos.y + S(3));
            ImVec2 b(pos.x + segW * (i + 1) - S(3), pos.y + h - S(3));
            if (*selected == i)
                dl->AddRectFilled(a, b, Rgb(kAccent), S(6));
            else if (hovered)
                dl->AddRectFilled(a, b, Rgb(kHover), S(6));

            ImVec2 ts = ImGui::CalcTextSize(items[i]);
            dl->AddText(ImVec2(pos.x + segW * i + (segW - ts.x) * 0.5f, pos.y + (h - ts.y) * 0.5f),
                        *selected == i ? Rgb(0xFFFFFF) : Rgb(kMuted), items[i]);
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h));
        ImGui::Dummy(ImVec2(width, 0));
        return changed;
    }

    // Two-line selectable row used in the source list.
    bool SourceRow(const char* id, ImFont* iconFont, ImFont* titleFont, ImFont* smallFont,
                   const char* icon, const std::string& title, const std::string& detail, bool active)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = S(52);
        bool pressed = ImGui::InvisibleButton(id, ImVec2(w, h));
        bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImVec2 end(pos.x + w, pos.y + h);
        if (active)
            dl->AddRectFilled(pos, end, Rgb(kAccent, 40), S(8));
        else if (hovered)
            dl->AddRectFilled(pos, end, Rgb(kHover), S(8));
        if (active)
            dl->AddRectFilled(ImVec2(pos.x, pos.y + S(12)), ImVec2(pos.x + S(3), end.y - S(12)), Rgb(kAccent), S(2));

        // Icon tile
        ImVec2 tile0(pos.x + S(12), pos.y + S(10)), tile1(pos.x + S(44), pos.y + S(42));
        dl->AddRectFilled(tile0, tile1, active ? Rgb(kAccent) : Rgb(kCardRaised), S(8));
        ImVec2 is = iconFont->CalcTextSizeA(iconFont->FontSize, FLT_MAX, 0.f, icon);
        dl->AddText(iconFont, iconFont->FontSize,
                    ImVec2((tile0.x + tile1.x - is.x) * 0.5f, (tile0.y + tile1.y - is.y) * 0.5f),
                    active ? Rgb(0xFFFFFF) : Rgb(kMuted), icon);

        float textX = pos.x + S(56);
        float textRight = end.x - S(active ? 40 : 12);
        dl->PushClipRect(ImVec2(textX, pos.y), ImVec2(textRight, end.y), true);
        dl->AddText(titleFont, titleFont->FontSize, ImVec2(textX, pos.y + S(8)), Rgb(kText), title.c_str());
        dl->AddText(smallFont, smallFont->FontSize, ImVec2(textX, pos.y + S(28)), Rgb(kMuted), detail.c_str());
        dl->PopClipRect();

        if (active)
        {
            ImVec2 cs = iconFont->CalcTextSizeA(iconFont->FontSize, FLT_MAX, 0.f, ICON_CHECK);
            dl->AddText(iconFont, iconFont->FontSize, ImVec2(end.x - S(14) - cs.x, pos.y + (h - cs.y) * 0.5f),
                        Rgb(kAccentHover), ICON_CHECK);
        }
        return pressed;
    }

    // Slim slider with a filled track and a round knob. Optionally logarithmic.
    bool NitsSlider(const char* id, int* value, int minValue, int maxValue, bool logarithmic, int step)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        float h = S(26);
        float knob = S(9);
        ImGui::InvisibleButton(id, ImVec2(w, h));
        bool hovered = ImGui::IsItemHovered(), active = ImGui::IsItemActive();
        if (hovered || active)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        auto toT = [&](float v) {
            if (logarithmic)
                return std::log(v / minValue) / std::log(float(maxValue) / minValue);
            return (v - minValue) / float(maxValue - minValue);
        };
        float x0 = pos.x + knob, x1 = pos.x + w - knob;
        bool changed = false;
        if (active)
        {
            float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / (x1 - x0), 0.f, 1.f);
            float v = logarithmic ? minValue * std::pow(float(maxValue) / minValue, t) : minValue + t * (maxValue - minValue);
            int snapped = std::clamp(int(std::lround(v / step)) * step, minValue, maxValue);
            if (snapped != *value)
            {
                *value = snapped;
                changed = true;
            }
        }

        float t = std::clamp(toT(float(*value)), 0.f, 1.f);
        float cy = pos.y + h * 0.5f, kx = x0 + (x1 - x0) * t, th = S(3);
        dl->AddRectFilled(ImVec2(x0, cy - th), ImVec2(x1, cy + th), Rgb(0x111214), th);
        dl->AddRectFilled(ImVec2(x0, cy - th), ImVec2(kx, cy + th), Rgb(kAccent), th);
        if (hovered || active)
            dl->AddCircleFilled(ImVec2(kx, cy), knob + S(5), Rgb(kAccent, 60), 32);
        dl->AddCircleFilled(ImVec2(kx, cy), knob, Rgb(0xFFFFFF), 32);
        dl->AddCircle(ImVec2(kx, cy), knob, Rgb(kAccent), 32, S(2));
        return changed;
    }

    // Small rounded label drawn over the preview.
    void Chip(ImDrawList* dl, ImFont* font, ImVec2 pos, const char* text, ImU32 dot = 0)
    {
        ImVec2 ts = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.f, text);
        float padX = S(10), padY = S(5), dotSpace = dot ? S(14) : 0.f;
        ImVec2 end(pos.x + ts.x + padX * 2 + dotSpace, pos.y + ts.y + padY * 2);
        dl->AddRectFilled(pos, end, IM_COL32(0, 0, 0, 170), (end.y - pos.y) * 0.5f);
        if (dot)
            dl->AddCircleFilled(ImVec2(pos.x + padX + S(4), (pos.y + end.y) * 0.5f), S(4), dot, 16);
        dl->AddText(font, font->FontSize, ImVec2(pos.x + padX + dotSpace, pos.y + padY), Rgb(kText), text);
    }

    bool PrimaryButton(const char* label, ImVec2 size, bool danger = false)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, Vec(danger ? kRed : kAccent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Vec(danger ? 0xE5484D : kAccentHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Vec(danger ? 0xB52F33 : 0x4752C4));
        ImGui::PushStyleColor(ImGuiCol_Text, Vec(0xFFFFFF));
        bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }

    bool BeginCard(const char* id, float width, float height = 0.f)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Vec(kCard));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, S(12));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(18), S(16)));
        ImGuiChildFlags flags = ImGuiChildFlags_AlwaysUseWindowPadding;
        if (height <= 0.f)
            flags |= ImGuiChildFlags_AutoResizeY;
        bool open = ImGui::BeginChild(id, ImVec2(width, height), flags, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        return open;
    }

    void EndCard()
    {
        ImGui::EndChild();
    }

    std::string ProcessName(HWND hwnd)
    {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        std::string name;
        if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            wchar_t path[MAX_PATH] = {};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(process, 0, path, &size))
            {
                std::wstring p(path, size);
                name = Utf8(p.substr(p.find_last_of(L'\\') + 1));
            }
            CloseHandle(process);
        }
        return name;
    }

    // Writes an 8-bit BGRA texture to a PNG file.
    void SaveTexturePng(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* texture, const std::wstring& path)
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        winrt::com_ptr<ID3D11Texture2D> staging;
        winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, staging.put()));
        ctx->CopyResource(staging.get(), texture);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        winrt::check_hresult(ctx->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
        const UINT stride = desc.Width * 4;
        std::vector<BYTE> pixels(size_t(stride) * desc.Height);
        for (UINT y = 0; y < desc.Height; ++y)
            memcpy(&pixels[size_t(y) * stride], static_cast<BYTE*>(mapped.pData) + size_t(y) * mapped.RowPitch, stride);
        ctx->Unmap(staging.get(), 0);
        for (size_t i = 3; i < pixels.size(); i += 4)
            pixels[i] = 255; // Opaque

        winrt::com_ptr<IWICImagingFactory> wic;
        winrt::check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.put())));
        winrt::com_ptr<IWICStream> stream;
        winrt::check_hresult(wic->CreateStream(stream.put()));
        winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
        winrt::com_ptr<IWICBitmapEncoder> encoder;
        winrt::check_hresult(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
        winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
        winrt::com_ptr<IWICBitmapFrameEncode> frame;
        winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
        winrt::check_hresult(frame->Initialize(nullptr));
        winrt::check_hresult(frame->SetSize(desc.Width, desc.Height));
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        winrt::check_hresult(frame->SetPixelFormat(&format));
        winrt::check_hresult(frame->WritePixels(desc.Height, stride, UINT(pixels.size()), pixels.data()));
        winrt::check_hresult(frame->Commit());
        winrt::check_hresult(encoder->Commit());
    }

    bool IsCapturableWindow(HWND hwnd)
    {
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) || IsIconic(hwnd))
            return false;
        if (GetWindowLongW(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW)
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId())
            return false;
        BOOL cloaked = FALSE;
        DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        if (cloaked)
            return false;
        return GetWindowTextLengthW(hwnd) > 0;
    }
}

Panel::Panel(App& app) : m_app(app) {}

Panel::~Panel()
{
    if (m_imguiReady)
    {
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
    if (m_hwnd)
        DestroyWindow(m_hwnd);
}

void Panel::Create(HINSTANCE instance, int showCmd)
{
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hbrBackground = CreateSolidBrush(RGB((kBg >> 16) & 0xFF, (kBg >> 8) & 0xFF, kBg & 0xFF));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    m_scale = GetDpiForSystem() / 96.f;
    RECT rc{ 0, 0, LONG(kWidth * m_scale), LONG(kHeight * m_scale) };
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRectExForDpi(&rc, style, FALSE, 0, GetDpiForSystem());
    m_hwnd = CreateWindowExW(0, kClass, kTitle, style, CW_USEDEFAULT, CW_USEDEFAULT,
                             rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, instance, this);
    winrt::check_bool(m_hwnd != nullptr);

    // The window may have landed on a monitor with a different DPI than the system's.
    m_scale = GetDpiForWindow(m_hwnd) / 96.f;

    // Dark title bar that blends into the panel (caption color is Windows 11 only).
    BOOL dark = TRUE;
    DwmSetWindowAttribute(m_hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    COLORREF caption = RGB((kBg >> 16) & 0xFF, (kBg >> 8) & 0xFF, kBg & 0xFF);
    DwmSetWindowAttribute(m_hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof(caption));

    ResizeTarget();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui_ImplWin32_Init(m_hwnd);
    ImGui_ImplDX11_Init(m_app.GetGpu().device.get(), m_app.GetGpu().context.get());
    m_imguiReady = true;

    ShowWindow(m_hwnd, showCmd);
    UpdateWindow(m_hwnd);
    RefreshSources(true);
}

void Panel::BringToFront()
{
    if (IsIconic(m_hwnd))
        ShowWindow(m_hwnd, SW_RESTORE);
    SetForegroundWindow(m_hwnd);
}

void Panel::SetExcludedFromCapture(bool excluded)
{
    // Keeps the panel out of every screen capture, including a full-display share.
    SetWindowDisplayAffinity(m_hwnd, excluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
}

void Panel::ResizeTarget()
{
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    UINT w = std::max<LONG>(rc.right - rc.left, 1), h = std::max<LONG>(rc.bottom - rc.top, 1);
    if (m_target && w == m_width && h == m_height)
        return;
    auto* device = m_app.GetGpu().device.get();
    auto* ctx = m_app.GetGpu().context.get();
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    m_rtv = nullptr;
    m_target = nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, m_target.put()));
    winrt::check_hresult(device->CreateRenderTargetView(m_target.get(), nullptr, m_rtv.put()));

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    for (int i = 0; i < 2; ++i)
    {
        m_staging[i] = nullptr;
        winrt::check_hresult(device->CreateTexture2D(&desc, nullptr, m_staging[i].put()));
        m_stagingFilled[i] = false;
    }
    m_width = w;
    m_height = h;
}

void Panel::PaintWindow(HDC dc)
{
    if (m_pixels.empty())
        return;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = LONG(m_pixelsWidth);
    bmi.bmiHeader.biHeight = -LONG(m_pixelsHeight); // Top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, m_pixelsWidth, m_pixelsHeight, 0, 0, 0, m_pixelsHeight,
                      m_pixels.data(), &bmi, DIB_RGB_COLORS);
}

void Panel::BuildFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    auto regular = FontPath(L"segoeui.ttf");
    auto semibold = FontPath(L"seguisb.ttf");
    auto icons = FontPath(L"SegoeIcons.ttf");
    if (!FileExists(icons))
        icons = FontPath(L"segmdl2.ttf");
    if (!FileExists(semibold))
        semibold = regular;

    static const ImWchar textRanges[] = { 0x0020, 0x024F, 0x2010, 0x205E, 0x2190, 0x21FF, 0x2212, 0x2212, 0 };
    static const ImWchar iconRanges[] = { 0xE700, 0xF8FF, 0 };

    auto load = [&](const std::wstring& path, float size, bool withIcons) -> ImFont* {
        if (!FileExists(path))
            return io.Fonts->AddFontDefault();
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        ImFont* font = io.Fonts->AddFontFromFileTTF(Utf8(path).c_str(), size, &cfg, textRanges);
        if (withIcons && FileExists(icons))
        {
            ImFontConfig iconCfg;
            iconCfg.MergeMode = true;
            iconCfg.GlyphOffset = ImVec2(0, size * 0.18f);
            iconCfg.GlyphMinAdvanceX = size;
            io.Fonts->AddFontFromFileTTF(Utf8(icons).c_str(), size * 0.95f, &iconCfg, iconRanges);
        }
        return font;
    };
    auto loadIcons = [&](float size) -> ImFont* {
        if (!FileExists(icons))
            return io.Fonts->AddFontDefault();
        return io.Fonts->AddFontFromFileTTF(Utf8(icons).c_str(), size, nullptr, iconRanges);
    };

    float s = m_scale;
    m_fontBody = load(regular, 15.f * s, true); // First font is the default.
    m_fontSemibold = load(semibold, 15.f * s, true);
    m_fontTitle = load(semibold, 22.f * s, false);
    m_fontSmall = load(regular, 13.f * s, true);
    m_fontIcon = loadIcons(16.f * s);
    m_fontHeroIcon = loadIcons(44.f * s);

    ImGui_ImplDX11_InvalidateDeviceObjects();
}

void Panel::ApplyStyle()
{
    ImGuiStyle style;
    ImGui::StyleColorsDark(&style);

    style.WindowPadding = ImVec2(24, 20);
    style.FramePadding = ImVec2(12, 7);
    style.ItemSpacing = ImVec2(10, 10);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.ScrollbarSize = 10;
    style.GrabMinSize = 18;
    style.WindowRounding = 0;
    style.ChildRounding = 12;
    style.FrameRounding = 8;
    style.PopupRounding = 8;
    style.ScrollbarRounding = 8;
    style.GrabRounding = 9;
    style.WindowBorderSize = 0;
    style.ChildBorderSize = 0;
    style.FrameBorderSize = 0;
    style.ScaleAllSizes(m_scale);

    ImVec4* c = style.Colors;
    c[ImGuiCol_Text] = Vec(kText);
    c[ImGuiCol_TextDisabled] = Vec(kFaint);
    c[ImGuiCol_WindowBg] = Vec(kBg);
    c[ImGuiCol_ChildBg] = Vec(kCard);
    c[ImGuiCol_PopupBg] = Vec(kCardRaised);
    c[ImGuiCol_Border] = Vec(kBorder);
    c[ImGuiCol_FrameBg] = Vec(0x1E1F22);
    c[ImGuiCol_FrameBgHovered] = Vec(0x1E1F22);
    c[ImGuiCol_FrameBgActive] = Vec(0x1E1F22);
    c[ImGuiCol_SliderGrab] = Vec(kAccent);
    c[ImGuiCol_SliderGrabActive] = Vec(kAccentHover);
    c[ImGuiCol_Button] = Vec(kCardRaised);
    c[ImGuiCol_ButtonHovered] = Vec(kHover);
    c[ImGuiCol_ButtonActive] = Vec(0x3A3C42);
    c[ImGuiCol_Header] = Vec(kAccent, 0.25f);
    c[ImGuiCol_HeaderHovered] = Vec(kHover);
    c[ImGuiCol_HeaderActive] = Vec(kAccent, 0.35f);
    c[ImGuiCol_ScrollbarBg] = Vec(0, 0.f);
    c[ImGuiCol_ScrollbarGrab] = Vec(0x404249);
    c[ImGuiCol_ScrollbarGrabHovered] = Vec(0x4E5058);
    c[ImGuiCol_ScrollbarGrabActive] = Vec(0x5C5E66);
    c[ImGuiCol_Separator] = Vec(kBorder);
    c[ImGuiCol_NavCursor] = Vec(kAccent);
    ImGui::GetStyle() = style;
}

void Panel::RefreshSources(bool force)
{
    auto now = steady_clock::now();
    if (!force && now - m_lastSourceRefresh < seconds(2))
        return;
    m_lastSourceRefresh = now;

    m_displays = EnumerateDisplays();
    m_audioDevices = AudioPassthrough::EnumerateOutputDevices();

    m_windows.clear();
    EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
        auto* list = reinterpret_cast<std::vector<WindowEntry>*>(lp);
        if (IsCapturableWindow(hwnd))
        {
            WindowEntry e;
            e.hwnd = hwnd;
            int len = GetWindowTextLengthW(hwnd);
            e.title.resize(len + 1);
            e.title.resize(GetWindowTextW(hwnd, e.title.data(), len + 1));
            e.titleUtf8 = Utf8(e.title);
            e.processUtf8 = ProcessName(hwnd);
            list->push_back(std::move(e));
        }
        return list->size() < 64;
    }, reinterpret_cast<LPARAM>(&m_windows));
}

bool Panel::Render()
{
    if (!m_imguiReady || IsIconic(m_hwnd) || !IsWindowVisible(m_hwnd))
        return false;

    if (m_rebuildFonts)
    {
        g_scale = m_scale;
        BuildFonts();
        ApplyStyle();
        m_rebuildFonts = false;
    }
    RefreshSources(false);

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::Begin("##panel", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                 ImGuiWindowFlags_NoBringToFrontOnFocus);

    DrawHeader();

    float gap = S(16);
    float avail = ImGui::GetContentRegionAvail().x;
    float rightW = S(kRightColumn);
    float leftW = std::max(S(320), avail - rightW - gap);
    float columnsH = ImGui::GetContentRegionAvail().y;

    ImGui::BeginGroup();
    float previewH = std::min(leftW * 9.f / 16.f, columnsH * 0.58f);
    DrawPreview(leftW, previewH);
    ImGui::Dummy(ImVec2(0, S(2)));
    float howToH = S(112);
    DrawSources(leftW, std::max(S(140), ImGui::GetContentRegionAvail().y - howToH - ImGui::GetStyle().ItemSpacing.y));
    DrawHowTo(leftW, howToH);
    ImGui::EndGroup();

    ImGui::SameLine(0, gap);

    // The right column scrolls if the window is made short.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Vec(0, 0.f));
    ImGui::BeginChild("##right", ImVec2(rightW, columnsH), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(10), S(8)));
    const char* tabs[] = { ICON_BRIGHTNESS "  Picture", ICON_VOLUME "  Audio", ICON_SETTINGS "  Options" };
    Segmented("##settingsTabs", tabs, 3, &m_settingsTab, rightW);
    ImGui::Dummy(ImVec2(0, S(4)));
    if (m_settingsTab == 0)
        DrawPicture(rightW);
    else if (m_settingsTab == 1)
        DrawAudio(rightW);
    else
        DrawOptions(rightW);
    ImGui::PopStyleVar();
    ImGui::EndChild();

    ImGui::End();
    ImGui::Render();

    auto* ctx = m_app.GetGpu().context.get();
    auto* rtv = m_rtv.get();
    ImVec4 bg = Vec(kBg);
    const float clear[4] = { bg.x, bg.y, bg.z, 1.f };
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->ClearRenderTargetView(rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    ctx->OMSetRenderTargets(0, nullptr, nullptr);

    if (!m_screenshotPath.empty())
    {
        SaveTexturePng(m_app.GetGpu().device.get(), ctx, m_target.get(), m_screenshotPath);
        m_screenshotPath.clear();
        PostQuitMessage(0);
    }

    // Copy this frame into one staging texture and read back the previous frame
    // from the other, so the CPU doesn't wait on the GPU.
    ctx->CopyResource(m_staging[m_stagingIndex].get(), m_target.get());
    m_stagingFilled[m_stagingIndex] = true;
    m_stagingIndex ^= 1;
    ID3D11Texture2D* ready = m_staging[m_stagingIndex].get();
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (m_stagingFilled[m_stagingIndex] && SUCCEEDED(ctx->Map(ready, 0, D3D11_MAP_READ, 0, &mapped)))
    {
        m_pixelsWidth = m_width;
        m_pixelsHeight = m_height;
        m_pixels.resize(size_t(m_width) * m_height);
        for (UINT y = 0; y < m_height; ++y)
            memcpy(m_pixels.data() + size_t(y) * m_width, static_cast<const uint8_t*>(mapped.pData) + size_t(y) * mapped.RowPitch,
                   size_t(m_width) * 4);
        ctx->Unmap(ready, 0);
        if (HDC dc = GetDC(m_hwnd))
        {
            PaintWindow(dc);
            ReleaseDC(m_hwnd, dc);
        }
    }

    // Pace the loop to the display's refresh, as a vsynced present would.
    if (FAILED(DwmFlush()))
        Sleep(8);
    return true;
}

void Panel::DrawHeader()
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float h = S(44);

    // Logo tile
    dl->AddRectFilled(pos, ImVec2(pos.x + h, pos.y + h), Rgb(kAccent), S(12));
    ImVec2 is = m_fontHeroIcon->CalcTextSizeA(S(24), FLT_MAX, 0.f, ICON_BRIGHTNESS);
    dl->AddText(m_fontHeroIcon, S(24), ImVec2(pos.x + (h - is.x) * 0.5f, pos.y + (h - is.y) * 0.5f), Rgb(0xFFFFFF), ICON_BRIGHTNESS);

    float textX = pos.x + h + S(14);
    dl->AddText(m_fontTitle, m_fontTitle->FontSize, ImVec2(textX, pos.y - S(1)), Rgb(kText), "Discord HDR Helper");
    dl->AddText(m_fontSmall, m_fontSmall->FontSize, ImVec2(textX, pos.y + S(26)), Rgb(kMuted),
                "Tonemaps HDR to SDR so your screen share looks right.");

    // Status pill on the right
    const char* label;
    char buf[64];
    ImU32 dot;
    if (m_app.Capturing())
    {
        snprintf(buf, sizeof(buf), "Live  \xC2\xB7  %.0f fps", m_app.FramesPerSecond());
        label = buf;
        dot = Rgb(kGreen);
    }
    else
    {
        label = "Idle";
        dot = Rgb(kFaint);
    }
    ImFont* f = m_fontSemibold;
    ImVec2 ts = f->CalcTextSizeA(f->FontSize, FLT_MAX, 0.f, label);
    float pillW = ts.x + S(40), pillH = S(30);
    float right = pos.x + ImGui::GetContentRegionAvail().x;
    ImVec2 p0(right - pillW, pos.y + (h - pillH) * 0.5f), p1(right, p0.y + pillH);
    dl->AddRectFilled(p0, p1, Rgb(kCard), pillH * 0.5f);
    float pulse = m_app.Capturing() ? 0.6f + 0.4f * std::sin(float(ImGui::GetTime()) * 3.f) : 1.f;
    ImVec4 dc = ImGui::ColorConvertU32ToFloat4(dot);
    dc.w = pulse;
    dl->AddCircleFilled(ImVec2(p0.x + S(16), (p0.y + p1.y) * 0.5f), S(4.5f), ImGui::ColorConvertFloat4ToU32(dc), 16);
    dl->AddText(f, f->FontSize, ImVec2(p0.x + S(28), p0.y + (pillH - ts.y) * 0.5f), Rgb(kText), label);

    // GitHub link to the left of the pill
    ImFont* sf = m_fontSmall;
    const char* link = "GitHub";
    ImVec2 ls = sf->CalcTextSizeA(sf->FontSize, FLT_MAX, 0.f, link);
    ImVec2 lp(p0.x - S(18) - ls.x, pos.y + (h - ls.y) * 0.5f);
    ImGui::SetCursorScreenPos(lp);
    if (ImGui::InvisibleButton("##github", ls))
        ShellExecuteW(nullptr, L"open", kRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
    bool hovered = ImGui::IsItemHovered();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    dl->AddText(sf, sf->FontSize, lp, hovered ? Rgb(kText) : Rgb(kMuted), link);
    if (hovered)
        dl->AddLine(ImVec2(lp.x, lp.y + ls.y), ImVec2(lp.x + ls.x, lp.y + ls.y), Rgb(kText));

    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + h + S(20)));
}

void Panel::DrawPreview(float width, float height)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 end(pos.x + width, pos.y + height);
    float rounding = S(12);
    dl->AddRectFilled(pos, end, Rgb(0x111214), rounding);

    const Tonemapper& tm = m_app.GetTonemapper();
    if (tm.HasOutput())
    {
        float scale = std::min(width / tm.Width(), height / tm.Height());
        float w = tm.Width() * scale, h = tm.Height() * scale;
        ImVec2 i0(pos.x + (width - w) * 0.5f, pos.y + (height - h) * 0.5f);
        ImVec2 i1(i0.x + w, i0.y + h);
        dl->AddImageRounded(reinterpret_cast<ImTextureID>(tm.OutputSrv()), i0, i1, ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32_WHITE, rounding);

        std::string name = Utf8(m_app.SourceName());
        Chip(dl, m_fontSmall, ImVec2(pos.x + S(12), pos.y + S(12)), name.empty() ? "Source" : name.c_str(), Rgb(kGreen));

        char info[96];
        snprintf(info, sizeof(info), "%u \xC3\x97 %u  \xC2\xB7  %s  \xE2\x86\x92  SDR", tm.Width(), tm.Height(),
                 m_app.SourceDisplay().hdr ? "HDR" : "SDR");
        ImVec2 ts = m_fontSmall->CalcTextSizeA(m_fontSmall->FontSize, FLT_MAX, 0.f, info);
        Chip(dl, m_fontSmall, ImVec2(end.x - ts.x - S(32), end.y - ts.y - S(22)), info);
    }
    else
    {
        // Empty state
        const char* icon = ICON_VIDEO;
        ImVec2 is = m_fontHeroIcon->CalcTextSizeA(m_fontHeroIcon->FontSize, FLT_MAX, 0.f, icon);
        float cy = pos.y + height * 0.5f - S(28);
        dl->AddText(m_fontHeroIcon, m_fontHeroIcon->FontSize, ImVec2(pos.x + (width - is.x) * 0.5f, cy - is.y * 0.5f),
                    Rgb(0x404249), icon);

        const char* title = "Nothing captured yet";
        ImVec2 ts = m_fontSemibold->CalcTextSizeA(m_fontSemibold->FontSize, FLT_MAX, 0.f, title);
        dl->AddText(m_fontSemibold, m_fontSemibold->FontSize, ImVec2(pos.x + (width - ts.x) * 0.5f, cy + S(34)), Rgb(kText), title);

        std::string sub = m_app.LastError().empty() ? "Choose a display or a window below to start the preview."
                                                    : Utf8(m_app.LastError());
        float wrap = width - S(80);
        ImVec2 ss = m_fontSmall->CalcTextSizeA(m_fontSmall->FontSize, FLT_MAX, wrap, sub.c_str());
        dl->AddText(m_fontSmall, m_fontSmall->FontSize, ImVec2(pos.x + (width - ss.x) * 0.5f, cy + S(58)),
                    m_app.LastError().empty() ? Rgb(kMuted) : Rgb(0xF0B232), sub.c_str(), nullptr, wrap);
    }

    ImGui::Dummy(ImVec2(width, height));
}

void Panel::DrawSources(float width, float height)
{
    if (!BeginCard("##sources", width, height))
    {
        EndCard();
        return;
    }

    float inner = ImGui::GetContentRegionAvail().x;
    ImVec2 rowStart = ImGui::GetCursorPos();
    ImGui::SetCursorPosY(rowStart.y + (ImGui::GetFrameHeight() - ImGui::GetTextLineHeight()) * 0.5f);
    SectionTitle(m_fontSemibold, ICON_DISPLAY, "Source");

    // Right side of the header: tabs, refresh, stop.
    float segW = S(200);
    float btnW = ImGui::GetFrameHeight();
    float stopW = m_app.Capturing() ? S(84) : 0.f;
    float x = rowStart.x + inner - segW - btnW - S(8) - (stopW > 0 ? stopW + S(8) : 0.f);
    ImGui::SetCursorPos(ImVec2(x, rowStart.y));
    const char* tabs[] = { ICON_DISPLAY "  Displays", ICON_WINDOW "  Windows" };
    if (Segmented("##srctab", tabs, 2, &m_sourceTab, segW))
        RefreshSources(true);
    ImGui::SetCursorPos(ImVec2(x + segW + S(8), rowStart.y));
    ImGui::PushFont(m_fontIcon);
    if (ImGui::Button(ICON_REFRESH "##refresh", ImVec2(btnW, btnW)))
        RefreshSources(true);
    ImGui::PopFont();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Refresh list");
    if (m_app.Capturing())
    {
        ImGui::SetCursorPos(ImVec2(x + segW + btnW + S(16), rowStart.y));
        if (PrimaryButton(ICON_STOP "  Stop", ImVec2(stopW, btnW), true))
            m_app.StopCapture();
    }

    ImGui::SetCursorPos(ImVec2(rowStart.x, rowStart.y + ImGui::GetFrameHeight() + S(12)));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, Vec(0, 0.f));
    ImGui::BeginChild("##list", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::PopStyleColor();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(4)));

    if (m_sourceTab == 0)
    {
        for (size_t i = 0; i < m_displays.size(); ++i)
        {
            const DisplayInfo& d = m_displays[i];
            std::string title = d.friendlyName.empty() ? "Display " + std::to_string(i + 1) : Utf8(d.friendlyName);
            if (d.primary)
                title += "  (primary)";
            char detail[160];
            snprintf(detail, sizeof(detail), "%ld \xC3\x97 %ld  \xC2\xB7  %s  \xC2\xB7  SDR white %.0f nits  \xC2\xB7  peak %.0f nits",
                     d.bounds.right - d.bounds.left, d.bounds.bottom - d.bounds.top,
                     d.hdr ? "HDR on" : "HDR off", d.sdrWhiteNits, d.peakNits);
            bool active = m_app.Capturing() && m_app.SourceMonitor() == d.monitor;
            ImGui::PushID(int(i));
            if (SourceRow("##display", m_fontIcon, m_fontSemibold, m_fontSmall, ICON_DISPLAY, title, detail, active) && !active)
                m_app.CaptureMonitor(d);
            ImGui::PopID();
        }
    }
    else
    {
        if (m_windows.empty())
            MutedText("No windows to capture. Minimized windows and games in exclusive fullscreen don't show up here.");
        for (size_t i = 0; i < m_windows.size(); ++i)
        {
            const WindowEntry& w = m_windows[i];
            bool active = m_app.Capturing() && m_app.SourceWindow() == w.hwnd;
            ImGui::PushID(int(i));
            if (SourceRow("##window", m_fontIcon, m_fontSemibold, m_fontSmall, ICON_WINDOW, w.titleUtf8,
                          w.processUtf8.empty() ? "Window" : w.processUtf8, active) && !active)
                m_app.CaptureWindow(w.hwnd, w.title);
            ImGui::PopID();
        }
    }

    ImGui::PopStyleVar();
    ImGui::EndChild();
    EndCard();
}

void Panel::DrawPicture(float width)
{
    BeginCard("##picture", width);
    Settings& s = m_app.settings;
    bool changed = false;

    SectionTitle(m_fontSemibold, ICON_BRIGHTNESS, "Picture");
    ImGui::Dummy(ImVec2(0, S(2)));

    auto valueRow = [&](const char* label, const char* value) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        ImVec2 vs = ImGui::CalcTextSize(value);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - vs.x);
        ImGui::PushStyleColor(ImGuiCol_Text, Vec(kMuted));
        ImGui::TextUnformatted(value);
        ImGui::PopStyleColor();
    };

    // Brightness: what nit level counts as SDR 100% white.
    char value[64];
    int white = int(std::lround(m_app.EffectiveWhite()));
    snprintf(value, sizeof(value), "%d nits%s", white, s.sdrWhiteNits == 0 ? "  \xC2\xB7  auto" : "");
    valueRow("Brightness", value);
    if (NitsSlider("##white", &white, 40, 600, false, 5))
    {
        s.sdrWhiteNits = white;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Raise it if the stream looks too bright, lower it if it looks dim.");
    bool autoWhite = s.sdrWhiteNits == 0;
    if (Toggle("Match Windows SDR brightness", &autoWhite))
    {
        s.sdrWhiteNits = autoWhite ? 0 : int(std::lround(m_app.SourceDisplay().sdrWhiteNits));
        changed = true;
    }
    ImGui::Dummy(ImVec2(0, S(2)));

    // Highlights: the brightest level in the source, which gets mapped to white.
    int peak = int(std::lround(m_app.EffectivePeak()));
    snprintf(value, sizeof(value), "%d nits%s", peak, s.peakNits == 0 ? "  \xC2\xB7  auto" : "");
    valueRow("Highlight peak", value);
    if (NitsSlider("##peak", &peak, 200, 4000, true, 10))
    {
        s.peakNits = peak;
        changed = true;
    }
    bool autoPeak = s.peakNits == 0;
    if (Toggle("Use the display's peak brightness", &autoPeak))
    {
        s.peakNits = autoPeak ? 0 : int(std::lround(m_app.SourceDisplay().peakNits));
        changed = true;
    }

    ImGui::Dummy(ImVec2(0, S(2)));
    ImGui::TextUnformatted("Tonemap curve");
    const char* curves[] = { "BT.2390", "Reinhard", "Clip" };
    int mode = int(s.mode);
    if (Segmented("##curve", curves, 3, &mode, ImGui::GetContentRegionAvail().x))
    {
        s.mode = TonemapMode(mode);
        changed = true;
    }
    static const char* descriptions[] = {
        "Keeps normal content as-is, rolls off highlights.",
        "Softer look, compresses the whole range.",
        "Brightness fix only, highlights are clipped.",
    };
    ImGui::PushFont(m_fontSmall);
    MutedText(descriptions[mode]);
    ImGui::PopFont();

    EndCard();
    ImGui::Dummy(ImVec2(0, S(4)));
    if (changed)
        m_app.ApplySettings();
}

void Panel::DrawAudio(float width)
{
    BeginCard("##audio", width);
    Settings& s = m_app.settings;
    bool changed = false;

    SectionTitle(m_fontSemibold, ICON_VOLUME, "Audio");
    ImGui::Dummy(ImVec2(0, S(2)));
    changed |= Toggle("Send PC audio with the stream", &s.audioEnabled);

    ImGui::BeginDisabled(!s.audioEnabled);

    // Output device the audio is replayed into.
    ImGui::TextUnformatted("Play through");
    const AudioPassthrough::Device* selected = nullptr;
    for (const auto& d : m_audioDevices)
        if (d.id == s.audioDevice)
            selected = &d;
    constexpr char kAutomatic[] = "Automatic (silent, no extra device)";
    std::string preview = selected ? Utf8(selected->name) : kAutomatic;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, Vec(0x1E1F22));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, Vec(kHover));
    if (ImGui::BeginCombo("##device", preview.c_str()))
    {
        if (ImGui::Selectable(kAutomatic, s.audioDevice.empty()) && !s.audioDevice.empty())
        {
            s.audioDevice.clear();
            changed = true;
        }
        ImGui::Separator();
        for (const auto& d : m_audioDevices)
        {
            std::string label = Utf8(d.name) + (d.isDefault ? "  (default)" : "");
            if (ImGui::Selectable(label.c_str(), d.id == s.audioDevice))
            {
                s.audioDevice = d.id;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor(2);

    ImGui::PushFont(m_fontSmall);
    if (selected && selected->isDefault)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Vec(0xF0B232));
        ImGui::TextWrapped(ICON_WARNING "  This is the device you listen on, so you'll hear the audio twice. "
                           "Pick one you don't use, like your monitor's speakers.");
        ImGui::PopStyleColor();
    }
    else if (!selected)
    {
        MutedText("Replays the audio on your speakers at zero volume, so you don't hear it twice. "
                  "If viewers get no sound, pick a device you don't listen to instead.");
    }
    else
    {
        MutedText("Discord picks the sound up from this device. You won't hear it unless you listen on it.");
    }
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, S(2)));
    ImGui::TextUnformatted("Audio from");
    const char* modes[] = { "Captured app", "All apps" };
    if (Segmented("##audioMode", modes, 2, &s.audioMode, ImGui::GetContentRegionAvail().x))
        changed = true;
    ImGui::PushFont(m_fontSmall);
    MutedText(s.audioMode == 0 ? "Only the app you're capturing. When capturing a display, all apps are used."
                               : "Every app except Discord, so viewers don't hear your voice chat.");
    ImGui::PopFont();
    ImGui::EndDisabled();

    // Live status and level meter.
    ImGui::Dummy(ImVec2(0, S(2)));
    auto status = m_app.AudioStatus();
    std::string line;
    ImU32 color = Rgb(kMuted);
    if (!s.audioEnabled)
        line = "Off";
    else if (!status.error.empty())
    {
        line = Utf8(status.error);
        color = Rgb(0xF0B232);
    }
    else if (!m_app.Capturing())
        line = "Starts when you capture a display or window.";
    else if (status.sources.empty())
        line = "Waiting for an app to play sound\xE2\x80\xA6";
    else
    {
        line = "Sending: ";
        for (size_t i = 0; i < status.sources.size(); ++i)
            line += (i ? ", " : "") + Utf8(status.sources[i]);
        color = Rgb(kText);
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x, h = S(6);
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Rgb(0x111214), h * 0.5f);
    float level = status.running ? std::sqrt(std::clamp(status.level, 0.f, 1.f)) : 0.f;
    if (level > 0.01f)
        dl->AddRectFilled(p, ImVec2(p.x + w * level, p.y + h), level > 0.95f ? Rgb(0xF0B232) : Rgb(kGreen), h * 0.5f);
    ImGui::Dummy(ImVec2(w, h));
    ImGui::PushFont(m_fontSmall);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::TextWrapped("%s", line.c_str());
    ImGui::PopStyleColor();
    MutedText("In Discord, make sure Sound is on in the stream settings.");
    ImGui::PopFont();

    EndCard();
    if (changed)
        m_app.ApplySettings();
}

void Panel::DrawOptions(float width)
{
    BeginCard("##options", width);
    Settings& s = m_app.settings;
    bool changed = false;

    SectionTitle(m_fontSemibold, ICON_SETTINGS, "Options");
    ImGui::Dummy(ImVec2(0, S(2)));
    changed |= Toggle("Show mouse cursor", &s.captureCursor);
    changed |= Toggle("Keep output window in background", &s.outputInBackground);
    changed |= Toggle("Hide this panel from the stream", &s.hidePanelFromCapture);

    ImGui::Dummy(ImVec2(0, S(2)));
    bool canFit = m_app.GetTonemapper().HasOutput();
    ImGui::BeginDisabled(!canFit);
    if (ImGui::Button("Resize output to source", ImVec2(-FLT_MIN, 0)))
        m_app.Output().SizeToSource(m_app.GetTonemapper().Width(), m_app.GetTonemapper().Height());
    ImGui::EndDisabled();

    EndCard();
    ImGui::Dummy(ImVec2(0, S(4)));
    if (changed)
        m_app.ApplySettings();
}

void Panel::DrawHowTo(float width, float height)
{
    BeginCard("##howto", width, height);
    float inner = ImGui::GetContentRegionAvail().x;
    ImVec2 titlePos = ImGui::GetCursorPos();
    SectionTitle(m_fontSemibold, ICON_INFO, "Sharing in Discord");

    // Version on the right of the title row.
    char version[48];
    snprintf(version, sizeof(version), "%s  \xC2\xB7  MIT License", kVersion);
    ImGui::PushFont(m_fontSmall);
    ImVec2 vs = ImGui::CalcTextSize(version);
    ImGui::SetCursorPos(ImVec2(titlePos.x + inner - vs.x, titlePos.y + S(2)));
    ImGui::PushStyleColor(ImGuiCol_Text, Vec(kFaint));
    ImGui::TextUnformatted(version);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    const char* steps[] = {
        "Pick a display or a window above.",
        "In Discord, share your screen and pick \"Discord HDR Helper Output\" under Applications.",
        "Fine-tune Brightness until the preview matches your screen.",
    };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    origin.y = std::max(origin.y, ImGui::GetWindowPos().y + titlePos.y + S(32));
    float colGap = S(16);
    float colW = (inner - colGap * 2) / 3.f;
    float r = S(11);
    ImFont* f = m_fontSmall;
    for (int i = 0; i < 3; ++i)
    {
        float x = origin.x + i * (colW + colGap);
        dl->AddCircleFilled(ImVec2(x + r, origin.y + r), r, Rgb(kAccent, 60), 24);
        char n[2] = { char('1' + i), 0 };
        ImVec2 ns = m_fontSemibold->CalcTextSizeA(m_fontSemibold->FontSize, FLT_MAX, 0.f, n);
        dl->AddText(m_fontSemibold, m_fontSemibold->FontSize, ImVec2(x + r - ns.x * 0.5f, origin.y + r - ns.y * 0.5f),
                    Rgb(kAccentHover), n);
        float textX = x + r * 2 + S(10);
        dl->AddText(f, f->FontSize, ImVec2(textX, origin.y + S(2)), Rgb(kMuted), steps[i], nullptr,
                    x + colW - textX);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(ImVec2(inner, S(40)));
    EndCard();
}

LRESULT CALLBACK Panel::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        auto* self = static_cast<Panel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->HandleMessage(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Panel::HandleMessage(UINT msg, WPARAM wp, LPARAM lp)
{
    if (m_imguiReady && ImGui_ImplWin32_WndProcHandler(m_hwnd, msg, wp, lp))
        return 1;

    switch (msg)
    {
    case WM_SIZE:
        if (m_target && wp != SIZE_MINIMIZED)
            ResizeTarget();
        return 0;
    case WM_GETMINMAXINFO:
    {
        auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
        mmi->ptMinTrackSize = { LONG(760 * m_scale), LONG(560 * m_scale) };
        // Allow the default size even on small screens (e.g. 1024x768 VMs).
        mmi->ptMaxTrackSize = { std::max<LONG>(mmi->ptMaxTrackSize.x, LONG(1600 * m_scale)),
                                std::max<LONG>(mmi->ptMaxTrackSize.y, LONG(1200 * m_scale)) };
        return 0;
    }
    case WM_DPICHANGED:
    {
        m_scale = HIWORD(wp) / 96.f;
        m_rebuildFonts = true;
        auto* rc = reinterpret_cast<RECT*>(lp);
        SetWindowPos(m_hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DISPLAYCHANGE:
        RefreshSources(true);
        m_app.RefreshDisplayInfo();
        return 0;
    case WM_APP_SOURCE_CLOSED:
        m_app.OnSourceClosed();
        return 0;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(m_hwnd, &ps);
        PaintWindow(dc);
        EndPaint(m_hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        DestroyWindow(m_hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(m_hwnd, msg, wp, lp);
}
