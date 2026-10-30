// ==WindhawkMod==
// @id              custom-clock-line2
// @name            Custom Clock Line 2
// @description     Two-line Windows 11 taskbar clock. Line 1 is a date/time you format yourself, line 2 cycles through pages fed by feeder.py. Both lines support mixed styles with [tags]. Everything is configurable in Settings.
// @version         1.2
// @author          Shamilimanuel
// @include         explorer.exe
// @compilerOptions -lole32 -loleaut32 -lruntimeobject
// ==/WindhawkMod==

// ==WindhawkModSettings==
/*
- general:
  - line1Format: "%a, %b %e %Y | %H:%M:%S"
    $name: Line 1 format
    $description: >-
      Tokens - %a Fri, %A Friday, %b Oct, %B October, %d day 03, %e day 3, %m month 10, %Y 2026, %y 26, %H hour 14, %I hour 02 (12h), %M minutes, %S seconds, %p AM/PM, %% a percent sign.
  - cycleSeconds: 4
    $name: Seconds per page
    $description: How long each line 2 page stays on screen.
  - line2MaxLength: 39
    $name: Line 2 max length
    $description: Counts visible characters only (tags are not counted). Longer text is cut with an ellipsis. 0 means no limit.
  - staleSeconds: 10
    $name: Feeder timeout (seconds)
    $description: If feeder.py has not updated its data for this long, line 2 shows the warning text below. 0 turns the check off.
  - staleText: "[c=#ff6b6b]⚠ feeder stopped[/c]"
    $name: Feeder stopped text
    $description: Shown on line 2 when the feeder is not running. Tags work here too.
  $name: General
- line1:
  - color: "#8ff3ff"
    $name: Colour
    $description: Hex colour - #RRGGBB, or #AARRGGBB for transparency.
  - font: Segoe UI Semibold
    $name: Font
    $description: Font name, for example Segoe UI Light or Consolas. Empty keeps the default.
  - size: 0
    $name: Font size
    $description: 0 keeps the default size.
  - weight: normal
    $name: Weight
    $options:
    - light: Light
    - normal: Normal
    - semibold: Semi-bold
    - bold: Bold
  - italic: false
    $name: Italic
  - spacing: 0
    $name: Letter spacing
    $description: In 1/1000 of an em. 0 is normal, 40 is slightly wider.
  - align: right
    $name: Alignment
    $options:
    - left: Left
    - center: Center
    - right: Right
  - opacity: 100
    $name: Opacity
    $description: 20 to 100 percent.
  $name: Line 1 style
- line2:
  - color: "#4eb1b1"
    $name: Colour
    $description: Hex colour - #RRGGBB, or #AARRGGBB for transparency.
  - font: Consolas
    $name: Font
  - size: 0
    $name: Font size
  - weight: normal
    $name: Weight
    $options:
    - light: Light
    - normal: Normal
    - semibold: Semi-bold
    - bold: Bold
  - italic: false
    $name: Italic
  - spacing: 0
    $name: Letter spacing
  - align: right
    $name: Alignment
    $options:
    - left: Left
    - center: Center
    - right: Right
  - opacity: 100
    $name: Opacity
  $name: Line 2 style
- pages: ["[c=#8ff3ff]♪[/c] {media}", "[c=#8ff3ff]CPU[/c] {cpu}%  [c=#8ff3ff]GPU[/c] {gpu}%  [c=#8ff3ff]RAM[/c] {ram}%", "{resin} · [i]{resin_time}[/i]"]
  $name: Line 2 pages
  $description: >-
    Line 2 shows one page at a time and cycles through them. Data tokens - {cpu} {gpu} {ram} {media} {resin} {resin_time}. Mixed styles with tags - [c=#ff6b6b]colour[/c], [b]bold[/b], [i]italic[/i], [f=Consolas]font[/f], [s=14]size[/s]. Use #80e6fbfb for a half-transparent colour.
*/
// ==/WindhawkModSettings==

#include <windhawk_utils.h>
#include <windows.h>
#undef GetCurrentTime  // a Windows macro that clashes with the XAML headers

#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>  // defines Clear/Append for XAML lists (Inlines)
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Text.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Documents.h>
#include <winrt/Windows.UI.Xaml.Media.h>

using namespace winrt::Windows::UI::Xaml;

// ---------- settings ----------

struct LineStyle {
    std::wstring color, font, weight, align;
    int size = 0;
    int spacing = 0;
    int opacity = 100;
    bool italic = false;
};

struct Settings {
    std::wstring line1Format;
    std::vector<std::wstring> pages;
    int cycleSeconds = 4;
    int line2MaxLength = 39;
    int staleSeconds = 10;
    std::wstring staleText;
    LineStyle line1, line2;
};

std::mutex g_mutex;  // protects g_settings, g_blocks and g_dispatcher
Settings g_settings;

std::wstring GetString(PCWSTR name) {
    WindhawkUtils::StringSetting setting = WindhawkUtils::StringSetting::make(name);
    return setting.get() ? setting.get() : L"";
}

// prefix is "line1." or "line2."
LineStyle LoadStyle(const std::wstring& prefix) {
    auto key = [&](PCWSTR field) { return prefix + field; };
    LineStyle style;
    style.color = GetString(key(L"color").c_str());
    style.font = GetString(key(L"font").c_str());
    style.weight = GetString(key(L"weight").c_str());
    style.align = GetString(key(L"align").c_str());
    style.size = Wh_GetIntSetting(key(L"size").c_str());
    style.spacing = Wh_GetIntSetting(key(L"spacing").c_str());
    style.opacity = Wh_GetIntSetting(key(L"opacity").c_str());
    if (style.opacity < 20) style.opacity = 20;
    if (style.opacity > 100) style.opacity = 100;
    style.italic = Wh_GetIntSetting(key(L"italic").c_str()) != 0;
    return style;
}

void LoadSettings() {
    Settings s;
    s.line1Format = GetString(L"general.line1Format");
    for (int i = 0;; i++) {
        std::wstring page = GetString((L"pages[" + std::to_wstring(i) + L"]").c_str());
        if (page.empty()) {
            break;
        }
        s.pages.push_back(page);
    }
    s.cycleSeconds = Wh_GetIntSetting(L"general.cycleSeconds");
    if (s.cycleSeconds < 1) {
        s.cycleSeconds = 1;
    }
    s.line2MaxLength = Wh_GetIntSetting(L"general.line2MaxLength");
    s.staleSeconds = Wh_GetIntSetting(L"general.staleSeconds");
    s.staleText = GetString(L"general.staleText");
    s.line1 = LoadStyle(L"line1.");
    s.line2 = LoadStyle(L"line2.");

    std::lock_guard<std::mutex> lock(g_mutex);
    g_settings = s;
}

Settings GetSettings() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_settings;
}

// ---------- shared state ----------
std::vector<winrt::weak_ref<Controls::TextBlock>> g_blocks;  // the clock's text lines
winrt::Windows::UI::Core::CoreDispatcher g_dispatcher{nullptr};  // runs code on the UI thread

std::wstring g_dataPath;
HANDLE g_stopEvent = nullptr;
HANDLE g_thread = nullptr;
std::atomic<bool> g_stopping{false};
std::atomic<int> g_pending{0};  // UI-thread jobs not finished yet

// ---------- the text we show ----------

// Line 1: replaces the %-tokens of the format with the current local time.
std::wstring FormatTime(const std::wstring& format) {
    static const wchar_t* daysShort[] = {L"Sun", L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat"};
    static const wchar_t* daysLong[] = {L"Sunday", L"Monday", L"Tuesday", L"Wednesday",
                                        L"Thursday", L"Friday", L"Saturday"};
    static const wchar_t* monthsShort[] = {L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun",
                                           L"Jul", L"Aug", L"Sep", L"Oct", L"Nov", L"Dec"};
    static const wchar_t* monthsLong[] = {L"January", L"February", L"March", L"April", L"May",
                                          L"June", L"July", L"August", L"September", L"October",
                                          L"November", L"December"};
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::wstring out;
    wchar_t buf[16];
    for (size_t i = 0; i < format.size(); i++) {
        if (format[i] != L'%' || i + 1 >= format.size()) {
            out += format[i];
            continue;
        }
        wchar_t token = format[++i];
        switch (token) {
            case L'a': out += daysShort[t.wDayOfWeek]; break;
            case L'A': out += daysLong[t.wDayOfWeek]; break;
            case L'b': out += monthsShort[t.wMonth - 1]; break;
            case L'B': out += monthsLong[t.wMonth - 1]; break;
            case L'd': wsprintfW(buf, L"%02d", t.wDay); out += buf; break;
            case L'e': wsprintfW(buf, L"%d", t.wDay); out += buf; break;
            case L'm': wsprintfW(buf, L"%02d", t.wMonth); out += buf; break;
            case L'Y': wsprintfW(buf, L"%d", t.wYear); out += buf; break;
            case L'y': wsprintfW(buf, L"%02d", t.wYear % 100); out += buf; break;
            case L'H': wsprintfW(buf, L"%02d", t.wHour); out += buf; break;
            case L'I': wsprintfW(buf, L"%02d", t.wHour % 12 == 0 ? 12 : t.wHour % 12); out += buf; break;
            case L'M': wsprintfW(buf, L"%02d", t.wMinute); out += buf; break;
            case L'S': wsprintfW(buf, L"%02d", t.wSecond); out += buf; break;
            case L'p': out += t.wHour < 12 ? L"AM" : L"PM"; break;
            case L'%': out += L'%'; break;
            default: out += L'%'; out += token; break;
        }
    }
    return out;
}

// Reads data.txt (written by feeder.py): one "key=value" per line. Returns false if missing.
bool ReadData(std::map<std::wstring, std::wstring>& data) {
    HANDLE file = CreateFileW(g_dataPath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    char bytes[4096];
    DWORD count = 0;
    ReadFile(file, bytes, sizeof(bytes) - 1, &count, nullptr);
    CloseHandle(file);
    if (count == 0) {
        return true;
    }
    static wchar_t wide[4096];
    int length = MultiByteToWideChar(CP_UTF8, 0, bytes, (int)count, wide, 4096);
    std::wstring text(wide, length);

    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find(L'\n', start);
        if (end == std::wstring::npos) {
            end = text.size();
        }
        std::wstring line = text.substr(start, end - start);
        while (!line.empty() && line.back() == L'\r') {
            line.pop_back();
        }
        size_t equals = line.find(L'=');
        if (equals != std::wstring::npos) {
            data[line.substr(0, equals)] = line.substr(equals + 1);
        }
        start = end + 1;
    }
    return true;
}

void ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    for (size_t pos = text.find(from); pos != std::wstring::npos; pos = text.find(from, pos + to.size())) {
        text.replace(pos, from.size(), to);
    }
}

// How many seconds ago feeder.py last wrote data.txt (-1 if the file does not exist).
long long DataAgeSeconds() {
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(g_dataPath.c_str(), GetFileExInfoStandard, &info)) {
        return -1;
    }
    FILETIME nowFileTime;
    GetSystemTimeAsFileTime(&nowFileTime);
    ULARGE_INTEGER now, written;
    now.LowPart = nowFileTime.dwLowDateTime;
    now.HighPart = nowFileTime.dwHighDateTime;
    written.LowPart = info.ftLastWriteTime.dwLowDateTime;
    written.HighPart = info.ftLastWriteTime.dwHighDateTime;
    if (now.QuadPart < written.QuadPart) {
        return 0;
    }
    return (long long)((now.QuadPart - written.QuadPart) / 10000000ULL);  // 100 ns units -> seconds
}

// Line 2: picks the current page and fills in the {tokens}. Tags are kept for the renderer.
std::wstring BuildLine2(const Settings& s) {
    long long age = DataAgeSeconds();
    if (age < 0) {
        return L"(start feeder.py)";
    }
    if (s.staleSeconds > 0 && age > s.staleSeconds) {
        return s.staleText;  // the heartbeat: the feeder stopped updating
    }

    std::map<std::wstring, std::wstring> data;
    if (!ReadData(data)) {
        return L"(start feeder.py)";
    }
    if (s.pages.empty()) {
        return L"";
    }
    size_t index = (size_t)((GetTickCount64() / 1000 / s.cycleSeconds) % s.pages.size());
    std::wstring text = s.pages[index];
    for (auto& item : data) {
        ReplaceAll(text, L"{" + item.first + L"}", item.second);
    }
    return text;
}

// ---------- mixed styles: [c=#hex] [f=Font] [s=size] [b] [i] ----------

struct TagStyle {
    std::wstring color, font;
    int size = 0;
    bool bold = false;
    bool italic = false;
    bool IsDefault() const { return color.empty() && font.empty() && size == 0 && !bold && !italic; }
};

struct Segment {
    std::wstring text;
    TagStyle style;
};

// Splits text with [tags] into pieces that each have one style.
// Unknown [brackets] stay as normal text.
std::vector<Segment> ParseTagged(const std::wstring& input) {
    std::vector<Segment> out;
    std::vector<TagStyle> stack(1);
    std::wstring buffer;
    auto flush = [&]() {
        if (!buffer.empty()) {
            out.push_back({buffer, stack.back()});
            buffer.clear();
        }
    };

    size_t i = 0;
    while (i < input.size()) {
        if (input[i] == L'[') {
            size_t close = input.find(L']', i);
            if (close != std::wstring::npos) {
                std::wstring tag = input.substr(i + 1, close - i - 1);
                bool closing = !tag.empty() && tag[0] == L'/';
                std::wstring body = closing ? tag.substr(1) : tag;
                std::wstring name = body, value;
                size_t equals = body.find(L'=');
                if (equals != std::wstring::npos) {
                    name = body.substr(0, equals);
                    value = body.substr(equals + 1);
                }
                bool known = name == L"c" || name == L"f" || name == L"s" || name == L"b" || name == L"i";
                if (known) {
                    flush();
                    if (closing) {
                        if (stack.size() > 1) {
                            stack.pop_back();
                        }
                    } else {
                        TagStyle next = stack.back();
                        if (name == L"c") next.color = value;
                        else if (name == L"f") next.font = value;
                        else if (name == L"s") next.size = _wtoi(value.c_str());
                        else if (name == L"b") next.bold = true;
                        else next.italic = true;
                        stack.push_back(next);
                    }
                    i = close + 1;
                    continue;
                }
            }
        }
        buffer += input[i++];
    }
    flush();
    return out;
}

// Cuts the visible text to maxLength characters, ending with an ellipsis.
void LimitLength(std::vector<Segment>& segments, int maxLength) {
    if (maxLength <= 0) {
        return;
    }
    int total = 0;
    for (auto& segment : segments) {
        total += (int)segment.text.size();
    }
    if (total <= maxLength) {
        return;
    }
    int keep = maxLength - 1;  // room for the ellipsis
    for (size_t i = 0; i < segments.size(); i++) {
        int length = (int)segments[i].text.size();
        if (keep <= length) {
            segments[i].text = segments[i].text.substr(0, keep) + L"…";
            segments.resize(i + 1);
            return;
        }
        keep -= length;
    }
}

// ---------- changing the clock (UI thread only) ----------

winrt::Windows::UI::Color ParseColor(const std::wstring& hex, winrt::Windows::UI::Color fallback) {
    if ((hex.size() == 7 || hex.size() == 9) && hex[0] == L'#') {
        wchar_t* end = nullptr;
        unsigned long value = wcstoul(hex.c_str() + 1, &end, 16);
        if (end && *end == 0) {
            unsigned alpha = hex.size() == 9 ? (value >> 24) & 0xFF : 0xFF;
            return winrt::Windows::UI::ColorHelper::FromArgb(
                (uint8_t)alpha, (value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
        }
    }
    return fallback;
}

// Sets the whole line's look (colour, font, ...). Tagged pieces then override parts of it.
void ApplyStyle(Controls::TextBlock block, const LineStyle& style, winrt::Windows::UI::Color fallbackColor) {
    block.Foreground(Media::SolidColorBrush(ParseColor(style.color, fallbackColor)));

    if (!style.font.empty()) {
        block.FontFamily(Media::FontFamily(style.font));
    }
    if (style.size > 0) {
        block.FontSize(style.size);
    }

    using winrt::Windows::UI::Text::FontWeights;
    if (style.weight == L"light") block.FontWeight(FontWeights::Light());
    else if (style.weight == L"semibold") block.FontWeight(FontWeights::SemiBold());
    else if (style.weight == L"bold") block.FontWeight(FontWeights::Bold());
    else block.FontWeight(FontWeights::Normal());

    block.FontStyle(style.italic ? winrt::Windows::UI::Text::FontStyle::Italic
                                 : winrt::Windows::UI::Text::FontStyle::Normal);
    block.CharacterSpacing(style.spacing);
    block.Opacity(style.opacity / 100.0);

    if (style.align == L"left") block.TextAlignment(TextAlignment::Left);
    else if (style.align == L"center") block.TextAlignment(TextAlignment::Center);
    else block.TextAlignment(TextAlignment::Right);
}

// Puts text (with [tags]) into a TextBlock. Plain text uses Text; styled text uses Runs.
void SetTagged(Controls::TextBlock block, const std::wstring& text, int maxLength) {
    std::vector<Segment> segments = ParseTagged(text);
    LimitLength(segments, maxLength);

    bool plain = segments.empty() || (segments.size() == 1 && segments[0].style.IsDefault());
    if (plain) {
        block.Inlines().Clear();
        block.Text(segments.empty() ? L"" : segments[0].text);
        return;
    }

    block.Text(L"");  // clears the old content
    auto inlines = block.Inlines();
    inlines.Clear();
    auto white = winrt::Windows::UI::ColorHelper::FromArgb(255, 255, 255, 255);
    for (auto& segment : segments) {
        Documents::Run run;
        run.Text(segment.text);
        if (!segment.style.color.empty()) {
            run.Foreground(Media::SolidColorBrush(ParseColor(segment.style.color, white)));
        }
        if (!segment.style.font.empty()) {
            run.FontFamily(Media::FontFamily(segment.style.font));
        }
        if (segment.style.size > 0) {
            run.FontSize(segment.style.size);
        }
        if (segment.style.bold) {
            run.FontWeight(winrt::Windows::UI::Text::FontWeights::Bold());
        }
        if (segment.style.italic) {
            run.FontStyle(winrt::Windows::UI::Text::FontStyle::Italic);
        }
        inlines.Append(run);
    }
}

void ApplyText(const std::wstring& line1, const std::wstring& line2, const Settings& s) {
    std::vector<winrt::weak_ref<Controls::TextBlock>> saved;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        saved = g_blocks;
    }
    std::vector<Controls::TextBlock> blocks;
    for (auto& weak : saved) {
        if (auto block = weak.get()) {
            blocks.push_back(block);
        }
    }
    if (blocks.empty()) {
        return;
    }

    auto cyan = winrt::Windows::UI::ColorHelper::FromArgb(255, 0x8f, 0xf3, 0xff);
    auto teal = winrt::Windows::UI::ColorHelper::FromArgb(255, 0x4e, 0xb1, 0xb1);

    ApplyStyle(blocks[0], s.line1, cyan);
    if (blocks.size() >= 2) {
        SetTagged(blocks[0], line1, 0);
        ApplyStyle(blocks[1], s.line2, teal);
        SetTagged(blocks[1], line2, s.line2MaxLength);
    } else {
        SetTagged(blocks[0], line1 + L"\n" + line2, 0);  // only one text block: two lines in it
    }
}

// Walks the clock's XAML tree: logs every element (for debugging) and collects the TextBlocks.
void CollectTextBlocks(DependencyObject element, int depth, std::vector<Controls::TextBlock>& found) {
    std::wstring indent(depth * 2, L' ');
    winrt::hstring type = winrt::get_class_name(element);

    winrt::hstring text;
    if (auto block = element.try_as<Controls::TextBlock>()) {
        text = block.Text();
        found.push_back(block);
    }
    Wh_Log(L"%s%s text='%s'", indent.c_str(), type.c_str(), text.c_str());

    int childCount = Media::VisualTreeHelper::GetChildrenCount(element);
    for (int i = 0; i < childCount; i++) {
        CollectTextBlocks(Media::VisualTreeHelper::GetChild(element, i), depth + 1, found);
    }
}

// ---------- the worker thread: ticks once a second ----------

DWORD WINAPI WorkerThread(LPVOID) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    // Wakes up every second; stops as soon as g_stopEvent is set.
    while (WaitForSingleObject(g_stopEvent, 1000) == WAIT_TIMEOUT) {
        winrt::Windows::UI::Core::CoreDispatcher dispatcher{nullptr};
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            dispatcher = g_dispatcher;
        }
        if (!dispatcher) {
            continue;  // clock not found yet
        }

        Settings settings = GetSettings();
        std::wstring line1 = FormatTime(settings.line1Format);
        std::wstring line2 = BuildLine2(settings);

        g_pending++;
        try {
            // Touching XAML is only allowed on the UI thread, so hand the work over.
            dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,
                                [line1, line2, settings]() {
                                    try {
                                        if (!g_stopping) {
                                            ApplyText(line1, line2, settings);
                                        }
                                    } catch (...) {
                                    }
                                    g_pending--;
                                });
        } catch (...) {
            g_pending--;
        }
    }
    return 0;
}

// ---------- the clock hook ----------

using OnApplyTemplate_t = int(WINAPI*)(void* pThis);
OnApplyTemplate_t OnApplyTemplate_Original;

// Windows calls this when the clock's XAML layout is built. The real function
// runs first, so the layout exists when we look at it.
int WINAPI OnApplyTemplate_Hook(void* pThis) {
    int result = OnApplyTemplate_Original(pThis);
    Wh_Log(L"Clock OnApplyTemplate called, this=%p", pThis);

    try {
        winrt::Windows::Foundation::IInspectable object{nullptr};
        winrt::copy_from_abi(object, pThis);
        auto root = object.as<FrameworkElement>();

        std::vector<Controls::TextBlock> found;
        CollectTextBlocks(root, 0, found);
        Wh_Log(L"Found %d text blocks", (int)found.size());

        {
            std::lock_guard<std::mutex> lock(g_mutex);
            g_blocks.clear();
            for (auto& block : found) {
                g_blocks.push_back(winrt::make_weak(block));
            }
            g_dispatcher = root.Dispatcher();
        }
        Settings settings = GetSettings();
        ApplyText(FormatTime(settings.line1Format), BuildLine2(settings), settings);  // show it right away
    } catch (winrt::hresult_error const& error) {
        Wh_Log(L"Could not set up the clock: %08X", (unsigned)error.code().value);
    }
    return result;
}

bool g_clockHooked = false;  // so we only hook once

bool HookClock(HMODULE systemTray) {
    if (g_clockHooked) {
        return true;
    }
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {
            {LR"(public: virtual int __cdecl winrt::impl::produce<struct winrt::SystemTray::implementation::DateTimeIconContent,struct winrt::Windows::UI::Xaml::IFrameworkElementOverrides>::OnApplyTemplate(void))"},
            (void**)&OnApplyTemplate_Original,
            (void*)OnApplyTemplate_Hook,
        },
    };
    if (!WindhawkUtils::HookSymbols(systemTray, hooks, ARRAYSIZE(hooks))) {
        Wh_Log(L"Failed to hook OnApplyTemplate");
        return false;
    }
    g_clockHooked = true;
    Wh_Log(L"Hook installed");
    return true;
}

// ---------- waiting for SystemTray.dll ----------

using LoadLibraryExW_t = decltype(&LoadLibraryExW);
LoadLibraryExW_t LoadLibraryExW_Original;

HMODULE WINAPI LoadLibraryExW_Hook(LPCWSTR fileName, HANDLE file, DWORD flags) {
    HMODULE module = LoadLibraryExW_Original(fileName, file, flags);
    if (module && !g_clockHooked && fileName) {
        size_t length = wcslen(fileName);
        const wchar_t name[] = L"SystemTray.dll";
        size_t nameLength = ARRAYSIZE(name) - 1;
        if (length >= nameLength && _wcsicmp(fileName + length - nameLength, name) == 0) {
            Wh_Log(L"SystemTray.dll just loaded");
            if (HookClock(module)) {
                Wh_ApplyHookOperations();
            }
        }
    }
    return module;
}

// ---------- mod start / stop ----------

BOOL Wh_ModInit() {
    Wh_Log(L"Custom Clock Line 2 v1.2: init");
    LoadSettings();

    wchar_t appData[MAX_PATH];
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH)) {
        g_dataPath = std::wstring(appData) + L"\\CustomClock\\data.txt";
    }

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);

    HMODULE systemTray = GetModuleHandleW(L"SystemTray.dll");
    if (systemTray) {
        HookClock(systemTray);
    } else {
        Wh_Log(L"SystemTray.dll not loaded yet, waiting for it");
    }

    HMODULE kernelBase = GetModuleHandleW(L"kernelbase.dll");
    auto loadLibraryExW = (LoadLibraryExW_t)GetProcAddress(kernelBase, "LoadLibraryExW");
    Wh_SetFunctionHook((void*)loadLibraryExW, (void*)LoadLibraryExW_Hook,
                       (void**)&LoadLibraryExW_Original);
    return TRUE;
}

// Called by Windhawk when you change a setting: picks up the new values at the next tick.
void Wh_ModSettingsChanged() {
    Wh_Log(L"Settings changed");
    LoadSettings();
}

// Stop the worker and let any unfinished UI job end before our code is unloaded.
void Wh_ModUninit() {
    Wh_Log(L"Custom Clock Line 2: uninit");
    g_stopping = true;
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    if (g_thread) {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    for (int i = 0; i < 200 && g_pending > 0; i++) {
        Sleep(10);
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    g_blocks.clear();
    g_dispatcher = nullptr;
}
