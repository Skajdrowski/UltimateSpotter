#include "GUI.h"
#include "RenderTarget.h"
#include "RenderManager.h"
#include "../dllmain.h"
#include "../19in1.h"

#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <vector>

bool GUI::isVisible = false;
bool GUI::isGreeting = true;
bool GUI::isInitialized = false;

namespace
{
struct PromptState
{
    std::wstring selectedPlayer;
    std::wstring clipboardText;
};

enum class TextFieldFocus
{
    None,
    ClipboardText
};

struct FetchState
{
    bool hasResult = false;
    uint32_t uid = 0;
    uint32_t statusColor = 0xFFAAAAAA;
    std::wstring statusMessage;
    std::wstring displayName;
    std::string ip;
};

std::wstring Trim(const std::wstring& text);

int g_playerListScroll = 0;
bool g_playerListDraggingScroll = false;
int g_playerListDragMouseOffsetY = 0;

LPDIRECT3DDEVICE8 g_pd3dDevice = nullptr;

// State backup
DWORD g_dwOldVertexShader = 0;
DWORD g_dwOldStateBlock = 0;

bool g_isCursorVisible = false;
HCURSOR g_arrowCursor = nullptr;

DWORD g_lastGuiRedrawTick = 0;
DWORD g_lastGreetingRedrawTick = 0;
constexpr DWORD kRedrawIntervalMs = 34;

RenderTarget* g_guiRenderTarget = nullptr;
RenderTarget* g_greetingRenderTarget = nullptr;
bool g_guiDirty = true;
bool g_greetingDirty = true;

D3DVIEWPORT8 g_lastViewport = {};
bool g_lastViewportValid = false;

PromptState g_prompt;

constexpr size_t kPromptMaxChars = 48;
TextFieldFocus g_activeTextField = TextFieldFocus::None;

bool g_leftMouseWasDown = false;
bool g_mousePressPending = false;
POINT g_mousePressPosition = {};
std::array<bool, 256> g_keyLatch{};

FetchState g_fetch;

void ResetFetchState()
{
    g_fetch = {};
    g_fetch.statusColor = 0xFFAAAAAA;
    g_guiDirty = true;
}

void SetFetchStatus(D3DCOLOR color, const std::wstring& message)
{
    g_fetch.statusColor = color;
    g_fetch.statusMessage = message;
    g_guiDirty = true;
}

void SetMissingSelectionStatus()
{
    SetFetchStatus(0xFFAAAAAA,
        L"Firstly choose a player from 'Player listing' before issuing anything.");
}

void SetMissingIpStatus()
{
    SetFetchStatus(0xFFAAAAAA, L"Selected player has no known IP address.");
}

const PlayerFetchEntry* FindPlayerEntryByUid(uint32_t uid)
{
    for (const auto& [name, entry] : playerToName)
    {
        if (entry.uid == uid)
            return &entry;
    }

    return nullptr;
}

int g_currentPage = 0;

static bool g_loadoutPresetDropdownOpen = false;

bool IsGameLoading()
{
    const uint8_t* const loadPtr = reinterpret_cast<const uint8_t*>(LoadingFlagAddr);
    return *loadPtr == 0;
}

void DestroyRenderTargets()
{
    delete g_guiRenderTarget;
    g_guiRenderTarget = nullptr;
    delete g_greetingRenderTarget;
    g_greetingRenderTarget = nullptr;
    g_guiDirty = true;
    g_greetingDirty = true;
}

void CreateStateBlock()
{
    if (!g_pd3dDevice)
        return;

    if (g_dwOldStateBlock)
    {
        g_pd3dDevice->DeleteStateBlock(g_dwOldStateBlock);
        g_dwOldStateBlock = 0;
    }

    if (FAILED(g_pd3dDevice->CreateStateBlock(D3DSBT_ALL, &g_dwOldStateBlock)))
        g_dwOldStateBlock = 0;
}

void EnsureRenderTargets(const D3DVIEWPORT8& viewport)
{
    if (!g_pd3dDevice)
        return;

    const int width = static_cast<int>(viewport.Width);
    const int height = static_cast<int>(viewport.Height);
    if (width <= 0 || height <= 0)
        return;

    const bool viewportChanged = !g_lastViewportValid
        || g_lastViewport.Width != viewport.Width
        || g_lastViewport.Height != viewport.Height;

    if (!g_guiRenderTarget || viewportChanged)
    {
        delete g_guiRenderTarget;
        g_guiRenderTarget = nullptr;

        g_guiRenderTarget = new (std::nothrow) RenderTarget(g_pd3dDevice, width, height);
        if (g_guiRenderTarget && !g_guiRenderTarget->IsValid())
        {
            delete g_guiRenderTarget;
            g_guiRenderTarget = nullptr;
        }

        g_lastGuiRedrawTick = 0;
        g_guiDirty = true;
    }

    if (!g_greetingRenderTarget || viewportChanged)
    {
        delete g_greetingRenderTarget;
        g_greetingRenderTarget = nullptr;

        g_greetingRenderTarget = new (std::nothrow) RenderTarget(g_pd3dDevice, width, height);
        if (g_greetingRenderTarget && !g_greetingRenderTarget->IsValid())
        {
            delete g_greetingRenderTarget;
            g_greetingRenderTarget = nullptr;
        }

        g_lastGreetingRedrawTick = 0;
        g_greetingDirty = true;
    }

    g_lastViewport = viewport;
    g_lastViewportValid = true;
}

void UpdateCursorVisibility(bool shouldShow)
{
    if (g_isCursorVisible == shouldShow)
        return;

    if (shouldShow)
    {
        if (!g_arrowCursor)
            g_arrowCursor = LoadCursor(nullptr, IDC_ARROW);
        if (g_arrowCursor)
            SetCursor(g_arrowCursor);
        while (ShowCursor(TRUE) < 0) {}
    }
    else
    {
        while (ShowCursor(FALSE) >= 0) {}
    }

    g_isCursorVisible = shouldShow;
}

void SetTextFieldFocus(TextFieldFocus newFocus)
{
    if (g_activeTextField == newFocus)
        return;

    g_activeTextField = newFocus;
    g_keyLatch.fill(false);

    g_guiDirty = true;
}

bool IsPointInsideRect(const POINT& pt, const RECT& rect)
{
    return pt.x >= rect.left && pt.x <= rect.right && pt.y >= rect.top && pt.y <= rect.bottom;
}

bool DrawButton(const RECT& rect, const char* label, bool hasCursorPosition, const POINT& cursorPosition,
    bool mousePressedThisFrame, D3DCOLOR normalColor, D3DCOLOR hoverColor)
{
    const bool hovered = hasCursorPosition && IsPointInsideRect(cursorPosition, rect);
    Render::Draw(g_pd3dDevice,
        rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top,
        hovered ? hoverColor : normalColor);
    Render::Outline(g_pd3dDevice,
        rect.left, rect.top,
        rect.right - rect.left, rect.bottom - rect.top,
        0xFFFFFFFF);
    RECT textRect = rect;
    Render::Fonts::MenuBold->DrawTextA(label, -1, &textRect,
        DT_VCENTER | DT_CENTER | DT_NOCLIP, 0xFFFFFFFF);
    return mousePressedThisFrame && hovered;
}

struct CheckboxResult
{
    bool hovered = false;
    bool clicked = false;
    RECT labelRect = {};
};

void ShrinkRectToTextWidth(RECT& rect, LPD3DXFONT font, const char* text, int paddingRight)
{
    if (!text || !font)
        return;

    RECT textSize = Render::GetTextSize(font, text);
    const int textW = (std::max)(0, static_cast<int>(textSize.right - textSize.left));
    const int desiredRight = rect.left + textW + paddingRight;
    if (desiredRight < rect.right)
        rect.right = desiredRight;
}

CheckboxResult DrawCheckbox(const RECT& checkboxRect, RECT labelRect, const char* label, bool checked,
    bool enabled, bool hasCursorPosition, const POINT& cursorPosition, bool mousePressedThisFrame)
{
    ShrinkRectToTextWidth(labelRect, Render::Fonts::MenuText, label, 4);

    const bool hovered = hasCursorPosition
        && (IsPointInsideRect(cursorPosition, checkboxRect) || IsPointInsideRect(cursorPosition, labelRect));
    const bool clicked = enabled && mousePressedThisFrame && hovered;
    const bool displayChecked = clicked ? !checked : checked;

    Render::Draw(g_pd3dDevice,
        checkboxRect.left, checkboxRect.top,
        checkboxRect.right - checkboxRect.left, checkboxRect.bottom - checkboxRect.top,
        !enabled ? 0xFF141414 : (hovered ? 0xFF2F4F8F : 0xFF1E1E1E));
    Render::Outline(g_pd3dDevice,
        checkboxRect.left, checkboxRect.top,
        checkboxRect.right - checkboxRect.left, checkboxRect.bottom - checkboxRect.top,
        enabled ? 0xFFFFFFFF : 0xFF666666);

    if (displayChecked)
    {
        RECT checkRect = checkboxRect;
        Render::Fonts::MenuTabs->DrawTextA("X", -1, &checkRect,
            DT_VCENTER | DT_CENTER | DT_NOCLIP, enabled ? 0xFF7CFC00 : 0xFF6B6B6B);
    }

    Render::Text(Render::Fonts::MenuText,
        labelRect.left, labelRect.top + 2,
        enabled ? 0xFFFFFFFF : 0xFF8A8A8A,
        label);

    return { hovered, clicked, labelRect };
}

bool CopyTextToClipboard(const std::wstring& text)
{
    if (!OpenClipboard(nullptr))
        return false;

    if (!EmptyClipboard())
    {
        CloseClipboard();
        return false;
    }

    const size_t sizeInBytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, sizeInBytes);
    if (!memory)
    {
        CloseClipboard();
        return false;
    }

    void* destination = GlobalLock(memory);
    if (!destination)
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    memcpy(destination, text.c_str(), sizeInBytes);
    GlobalUnlock(memory);

    if (!SetClipboardData(CF_UNICODETEXT, memory))
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

bool ProcessPromptInput()
{
    if (g_activeTextField != TextFieldFocus::ClipboardText)
        return false;

    std::wstring* activeBuffer = &g_prompt.clipboardText;

    bool changed = false;
    const bool shiftHeld = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

    auto handleCharacterKey = [&](int vk, wchar_t baseChar, wchar_t shiftedChar = 0)
        {
            const bool isDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (isDown && !g_keyLatch[vk])
            {
                wchar_t ch = baseChar;
                if (shiftHeld && shiftedChar)
                    ch = shiftedChar;

                if (activeBuffer->size() < kPromptMaxChars && ch)
                {
                    activeBuffer->push_back(ch);
                    changed = true;
                }

                g_keyLatch[vk] = true;
            }
            else if (!isDown)
            {
                g_keyLatch[vk] = false;
            }
        };

    auto handleActionKey = [&](int vk, const auto& action)
        {
            const bool isDown = (GetAsyncKeyState(vk) & 0x8000) != 0;
            if (isDown && !g_keyLatch[vk])
            {
                action();
                g_keyLatch[vk] = true;
                changed = true;
            }
            else if (!isDown)
            {
                g_keyLatch[vk] = false;
            }
        };

    for (int vk = 'A'; vk <= 'Z'; ++vk)
    {
        const wchar_t lower = static_cast<wchar_t>(vk + ('a' - 'A'));
        const wchar_t upper = static_cast<wchar_t>(vk);
        handleCharacterKey(vk, lower, upper);
    }

    for (int vk = '0'; vk <= '9'; ++vk)
    {
        const wchar_t digit = static_cast<wchar_t>(vk);
        handleCharacterKey(vk, digit, digit);
    }

    handleCharacterKey(VK_SPACE, L' ', L' ');
    handleCharacterKey(VK_OEM_MINUS, L'-', L'_');
    handleCharacterKey(VK_OEM_PERIOD, L'.', L':');

    handleActionKey(VK_BACK, [&]()
        {
            if (!activeBuffer->empty())
            {
                activeBuffer->pop_back();
                changed = true;
            }
        });

    handleActionKey(VK_RETURN, [&]()
        {
            g_prompt.clipboardText = Trim(g_prompt.clipboardText);
        });

    return changed;
}

std::wstring Trim(const std::wstring& text)
{
    const size_t begin = text.find_first_not_of(L" \t\r\n");
    if (begin == std::wstring::npos)
        return L"";
    const size_t end = text.find_last_not_of(L" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

bool TryParseUID(const std::wstring& text, uint32_t& outUid)
{
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty())
        return false;

    wchar_t* endPtr = nullptr;
    const unsigned long long value = std::wcstoull(trimmed.c_str(), &endPtr, 0);
    if (endPtr == trimmed.c_str() || *endPtr != L'\0'
        || value > (std::numeric_limits<uint32_t>::max)())
        return false;

    outUid = static_cast<uint32_t>(value);
    return true;
}

void PerformInventoryFetch()
{
    ResetFetchState();

    const auto updateSameIpAliasStatus = [&]() -> void
        {
            if (g_fetch.uid == 0 || g_fetch.ip.empty())
                return;

            std::vector<std::wstring> aliases;
            aliases.reserve(4);

            for (const auto& [nameKey, entry] : playerToName)
            {
                if (entry.uid == g_fetch.uid)
                    continue;
                if (entry.ipAddress.empty())
                    continue;
                if (entry.ipAddress != g_fetch.ip)
                    continue;

                const std::wstring alias = !entry.displayName.empty() ? entry.displayName : nameKey;
                if (!alias.empty())
                    aliases.push_back(alias);
            }

            if (aliases.empty())
                return;

            std::sort(aliases.begin(), aliases.end());
            aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());

            std::wstring status = L"Player with same IP: ";
            for (size_t i = 0; i < aliases.size(); ++i)
            {
                if (i != 0)
                    status += L", ";
                status += aliases[i];
            }
            g_fetch.statusMessage = status;
        };

    const std::wstring trimmedInput = Trim(g_prompt.selectedPlayer);
    if (trimmedInput.empty())
    {
        g_fetch.statusMessage = L"Choose player's nickname before fetching.";
        g_guiDirty = true;
        return;
    }

    const auto resolveByName = [&](const std::wstring& name) -> bool
        {
            if (name.empty())
                return false;

            const std::unordered_map<std::wstring, PlayerFetchEntry>::iterator dirIt = playerToName.find(name);
            if (dirIt == playerToName.end())
                return false;

            const PlayerFetchEntry& entry = dirIt->second;
            if (entry.uid == 0)
                return false;

            g_fetch.uid = entry.uid;
            g_fetch.displayName = !entry.displayName.empty() ? entry.displayName : name;
            if (!entry.ipAddress.empty())
                g_fetch.ip = entry.ipAddress;

            g_fetch.hasResult = true;
            updateSameIpAliasStatus();
            g_guiDirty = true;
            return true;
        };

    if (resolveByName(trimmedInput))
        return;

    uint32_t parsedUID = 0;
    if (!TryParseUID(trimmedInput, parsedUID))
    {
        g_fetch.statusMessage = L"Player not found.";
        g_guiDirty = true;
        return;
    }

    g_fetch.uid = parsedUID;
    g_fetch.displayName = trimmedInput;

    for (const auto& [name, entry] : playerToName)
    {
        if (entry.uid == parsedUID)
        {
            g_fetch.displayName = !entry.displayName.empty() ? entry.displayName : name;
            if (!entry.ipAddress.empty())
                g_fetch.ip = entry.ipAddress;

            break;
        }
    }

    g_fetch.hasResult = true;
    updateSameIpAliasStatus();
    g_guiDirty = true;
}

void ApplyGuiRenderState()
{
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    g_pd3dDevice->SetTexture(0, nullptr);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    g_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    g_pd3dDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    g_pd3dDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}
} // namespace

void GUI::DrawGuiContent(const RECT& viewport, bool hasCursorPosition, const POINT& cursorPosition,
    bool leftMouseDown, bool mousePressedThisFrame)
{
    if (!isHost)
        return;

    bool promptVisibleThisFrame = false;

    if (isVisible)
    {
        const int screenWidth = viewport.right - viewport.left;
        const int screenHeight = viewport.bottom - viewport.top;

        if (screenWidth > 0 && screenHeight > 0)
        {
            const int minDimension = (screenWidth < screenHeight) ? screenWidth : screenHeight;
            int panelSize = minDimension > 0 ? minDimension : 16;
            if (panelSize < 16) panelSize = 16;

            const int panelX = viewport.left + (screenWidth - panelSize) / 2;
            const int panelY = viewport.top + (screenHeight - panelSize) / 2;

            Render::DrawSquare(g_pd3dDevice, panelX, panelY, panelSize, 0xC8000000);
            // =====================================================
            // TOOLBAR (pages)
            // =====================================================
            static constexpr std::array<const char*, 2> kPageLabels{
                "LOBBY MODERATION",
                "PLAYER INVENTORIES"
            };
            const int pageCount = static_cast<int>(kPageLabels.size());
            const int toolbarH = panelSize / 14;
            const int buttonW = panelSize / pageCount;

            Render::Draw(g_pd3dDevice, panelX, panelY, panelSize, toolbarH, 0xAA000000);

            for (int i = 0; i < pageCount; ++i)
            {
                RECT btn{
                    panelX + i * buttonW,
                    panelY,
                    panelX + (i + 1) * buttonW,
                    panelY + toolbarH
                };

                const bool hovered = hasCursorPosition && IsPointInsideRect(cursorPosition, btn);
                if (mousePressedThisFrame && hovered)
                {
                    g_currentPage = i;
                    g_guiDirty = true;
                }

                Render::Draw(
                    g_pd3dDevice,
                    btn.left, btn.top,
                    buttonW, toolbarH,
                    i == g_currentPage ? 0xFF2F4F8F : (hovered ? 0xFF1E1E1E : 0xFF101010)
                );

                Render::Outline(g_pd3dDevice, btn.left, btn.top, buttonW, toolbarH, 0xFFFF0000);

                Render::Fonts::Tabs->DrawTextA(kPageLabels[i], -1, &btn,
                    DT_VCENTER | DT_CENTER | DT_NOCLIP, 0xFFFFFFFF);
            }

            const int versionPad = 2;
            RECT versionRect{
                panelX + versionPad,
                panelY + versionPad,
                panelX + panelSize - versionPad,
                panelY + panelSize - versionPad
            };
            Render::Fonts::MenuBold->DrawTextA("Ultimate Spotter v1.3.1", -1, &versionRect, DT_RIGHT | DT_BOTTOM | DT_NOCLIP, 0xFF037d50);

            // =====================================================
            // CONTENT AREA
            // =====================================================
            const int padX = 20;
            int y = panelY + toolbarH + 20;

            // =====================================================
            // LOBBY MANAGEMENT PAGE
            // =====================================================
            if (g_currentPage == 0)
            {
                // Player list
                const int listHeight = 360;
                const int listHeaderH = 22;
                const int listRowH = 24;
                RECT listRect{
                    panelX + padX,
                    y,
                    panelX + panelSize - padX,
                    y + listHeight
                };

                Render::Draw(g_pd3dDevice,
                    listRect.left,
                    listRect.top,
                    listRect.right - listRect.left,
                    listRect.bottom - listRect.top,
                    0xAA0F0F0F);
                Render::Outline(g_pd3dDevice,
                    listRect.left,
                    listRect.top,
                    listRect.right - listRect.left,
                    listRect.bottom - listRect.top,
                    0xFF000000);

                Render::Text(Render::Fonts::MenuBold,
                    listRect.left + 6,
                    listRect.top + 2,
                    0xFFFFFFFF,
                    "Player listing");

                std::vector<std::pair<std::wstring, PlayerFetchEntry>> playerRows;
                playerRows.reserve(playerToName.size());
                for (const auto& [name, entry] : playerToName)
                    playerRows.emplace_back(name, entry);

                std::sort(playerRows.begin(), playerRows.end(),
                    [](const auto& a, const auto& b)
                    {
                        if (a.second.uid != b.second.uid)
                            return a.second.uid < b.second.uid;
                        return a.first < b.first;
                    });

                const int usableHeight = listHeight - listHeaderH - 6;
                const int maxRows = usableHeight / listRowH;
                const int totalRows = static_cast<int>(playerRows.size());
                const int maxScroll = (maxRows > 0) ? (std::max)(0, totalRows - maxRows) : 0;
                g_playerListScroll = (std::clamp)(g_playerListScroll, 0, maxScroll);

                const int scrollbarW = 10;
                const int scrollbarPad = 4;
                RECT scrollbarRect{
                    listRect.right - scrollbarPad - scrollbarW,
                    listRect.top + listHeaderH + 2,
                    listRect.right - scrollbarPad,
                    listRect.bottom - 4
                };

                const int rowWidth = (listRect.right - listRect.left) - 12 - (scrollbarW + scrollbarPad);

                const int trackH = (std::max)(1, static_cast<int>(scrollbarRect.bottom - scrollbarRect.top));
                const float visibleFrac = (totalRows > 0) ? (static_cast<float>(maxRows) / static_cast<float>(totalRows)) : 1.0f;
                int thumbH = static_cast<int>(trackH * (std::min)(1.0f, (std::max)(0.05f, visibleFrac)));
                if (thumbH < 12) thumbH = 12;
                if (thumbH > trackH) thumbH = trackH;

                const int thumbRange = (std::max)(0, trackH - thumbH);
                int thumbY = scrollbarRect.top;
                if (maxScroll > 0)
                    thumbY += static_cast<int>((static_cast<float>(g_playerListScroll) / static_cast<float>(maxScroll)) * thumbRange);

                RECT thumbRect{
                    scrollbarRect.left,
                    thumbY,
                    scrollbarRect.right,
                    thumbY + thumbH
                };

                const bool overThumb = hasCursorPosition && IsPointInsideRect(cursorPosition, thumbRect);
                const bool overScrollbar = hasCursorPosition && IsPointInsideRect(cursorPosition, scrollbarRect);

                if (mousePressedThisFrame && overThumb)
                {
                    g_playerListDraggingScroll = true;
                    g_playerListDragMouseOffsetY = cursorPosition.y - thumbRect.top;
                }

                if (!leftMouseDown)
                    g_playerListDraggingScroll = false;

                if (g_playerListDraggingScroll && hasCursorPosition)
                {
                    int desiredThumbTop = cursorPosition.y - g_playerListDragMouseOffsetY;
                    if (desiredThumbTop < scrollbarRect.top)
                        desiredThumbTop = scrollbarRect.top;
                    if (desiredThumbTop > scrollbarRect.bottom - thumbH)
                        desiredThumbTop = scrollbarRect.bottom - thumbH;

                    const int thumbPos = desiredThumbTop - scrollbarRect.top;
                    if (thumbRange > 0 && maxScroll > 0)
                    {
                        const float t = static_cast<float>(thumbPos) / static_cast<float>(thumbRange);
                        g_playerListScroll = static_cast<int>(t * maxScroll + 0.5f);
                    }
                    else
                    {
                        g_playerListScroll = 0;
                    }

                    g_playerListScroll = (std::clamp)(g_playerListScroll, 0, maxScroll);
                }
                else if (mousePressedThisFrame && overScrollbar && !overThumb)
                {
                    if (hasCursorPosition)
                    {
                        if (cursorPosition.y < thumbRect.top)
                            g_playerListScroll -= maxRows;
                        else if (cursorPosition.y > thumbRect.bottom)
                            g_playerListScroll += maxRows;

                        g_playerListScroll = (std::clamp)(g_playerListScroll, 0, maxScroll);
                    }
                }

                Render::Draw(g_pd3dDevice,
                    scrollbarRect.left,
                    scrollbarRect.top,
                    scrollbarRect.right - scrollbarRect.left,
                    scrollbarRect.bottom - scrollbarRect.top,
                    0xFF0E0E0E);
                Render::Outline(g_pd3dDevice,
                    scrollbarRect.left,
                    scrollbarRect.top,
                    scrollbarRect.right - scrollbarRect.left,
                    scrollbarRect.bottom - scrollbarRect.top,
                    0xFF000000);

                Render::Draw(g_pd3dDevice,
                    thumbRect.left,
                    thumbRect.top,
                    thumbRect.right - thumbRect.left,
                    thumbRect.bottom - thumbRect.top,
                    g_playerListDraggingScroll ? 0xFF2F4F8F : (overThumb ? 0xFF3A3A3A : 0xFF2A2A2A));
                Render::Outline(g_pd3dDevice,
                    thumbRect.left,
                    thumbRect.top,
                    thumbRect.right - thumbRect.left,
                    thumbRect.bottom - thumbRect.top,
                    0xFF000000);

                for (int localIdx = 0; localIdx < maxRows; ++localIdx)
                {
                    const int globalIdx = g_playerListScroll + localIdx;
                    if (globalIdx < 0 || globalIdx >= totalRows)
                        continue;

                    const int rowY = listRect.top + listHeaderH + localIdx * listRowH;
                    RECT rowRect{
                        listRect.left + 4,
                        rowY + 2,
                        listRect.left + 4 + rowWidth,
                        rowY + listRowH
                    };

                    const bool hovered = hasCursorPosition && IsPointInsideRect(cursorPosition, rowRect);
                    const PlayerFetchEntry* entryPtr = &playerRows[globalIdx].second;
                    const bool isOnline = entryPtr->isOnline;

                    const D3DCOLOR rowColor = hovered
                        ? 0xFF2F4F8F
                        : (isOnline ? 0xFF1A2A1A : 0xFF1E1E1E);

                    Render::Draw(g_pd3dDevice,
                        rowRect.left,
                        rowRect.top,
                        rowRect.right - rowRect.left,
                        rowRect.bottom - rowRect.top,
                        rowColor);

                    std::wstring rowText = playerRows[globalIdx].first;
                    if (entryPtr->uid == 1000)
                        rowText += L" [Host]";
                    else
                        rowText += (isOnline ? L" [Online]" : L" [Offline]");

                    if (isOnline)
                    {
                        const uint32_t pingMs = getPing(entryPtr->uid);
                        if (pingMs)
                            rowText += L" Ping: " + std::to_wstring(pingMs) + L"ms";
                    }

                    Render::TextW(Render::Fonts::MenuText,
                        rowRect.left + 6,
                        rowRect.top + 3,
                        0xFF90EE90,
                        rowText.c_str());

                    if (mousePressedThisFrame && hovered)
                    {
                        const std::wstring& selectedName = entryPtr->displayName.empty()
                            ? playerRows[globalIdx].first
                            : entryPtr->displayName;
                        g_prompt.selectedPlayer = selectedName;
                        g_prompt.clipboardText = selectedName;
                        SetTextFieldFocus(TextFieldFocus::None);
                        PerformInventoryFetch();
                    }
                }

                y += listHeight + 20;

                const int infoLabelX = panelX + padX;
                const int infoValueX = infoLabelX + 120;

                Render::Text(Render::Fonts::MenuText, infoLabelX, y, 0xFFFFFFFF, "Player name:");
                const std::wstring nameValue = g_fetch.hasResult && !g_fetch.displayName.empty() ? g_fetch.displayName : L"-";
                Render::TextW(Render::Fonts::MenuText, infoValueX, y, 0xFF90EE90, nameValue.c_str());
                y += 18;

                Render::Text(Render::Fonts::MenuText, infoLabelX, y, 0xFFFFFFFF, "Player address:");
                std::string addressValue = "-";
                if (g_fetch.hasResult && !g_fetch.ip.empty())
                {
                    addressValue = g_fetch.ip;
                    if (isIpAddressBanned(g_fetch.ip))
                        addressValue += " (Banned)";
                }
                else
                    addressValue = "-";
                Render::Text(Render::Fonts::MenuText, infoValueX, y, 0xFF90EE90, addressValue.c_str());
                y += 24;

                const wchar_t* const success = L" [Success]";
                const wchar_t* const fail = L" [Fail]";

                if (!g_fetch.statusMessage.empty())
                {
                    Render::TextW(Render::Fonts::MenuText, infoLabelX, y, g_fetch.statusColor, g_fetch.statusMessage.c_str());
                    y += 20;
                }

                // KICK
                const int dangerW = 64;
                const int dangerH = 32;

                RECT kickRect{
                    panelX + padX,
                    y,
                    panelX + padX + dangerW,
                    y + dangerH
                };

                const bool kickPressed = DrawButton(kickRect, "KICK", hasCursorPosition, cursorPosition,
                    mousePressedThisFrame, 0xFFFF8300, 0xFFFFA500);
                const bool canKick = g_fetch.hasResult && g_fetch.uid != 0;

                if (kickPressed)
                {
                    if (canKick)
                    {
                        const PlayerFetchEntry* entry = FindPlayerEntryByUid(g_fetch.uid);
                        const bool wasKnownOffline = entry && !entry->isOnline;
                        const bool result = kick(g_fetch.uid, 4);
                        std::wstring status = result ? success : fail;
                        if (!result && wasKnownOffline)
                            status += L" You tried to kick an offline player.";
                        else if (!result)
                            status += L" An internal error occurred.";

                        SetFetchStatus(result ? 0xFF90EE90 : 0xFFED4337, status);
                    }
                    else
                        SetMissingSelectionStatus();
                }

                // BAN
                RECT banRect{
                    kickRect.right + 12,
                    y,
                    kickRect.right + 12 + dangerW,
                    y + dangerH
                };

                const bool banPressed = DrawButton(banRect, "BAN", hasCursorPosition, cursorPosition,
                    mousePressedThisFrame, 0xFF8B0000, 0xFFB00000);
                const bool canBan = g_fetch.hasResult && g_fetch.uid != 0 && !g_fetch.ip.empty();

                if (banPressed)
                {
                    if (canBan)
                    {
                        const bool newlyBanned = banIpAddress(g_fetch.ip);
                        std::vector<uint32_t> playersToKick;

                        for (const auto& [nameKey, value] : playerToName)
                        {
                            if (!value.isOnline || value.ipAddress != g_fetch.ip)
                                continue;

                            playersToKick.push_back(value.uid);
                        }

                        for (const uint32_t uid : playersToKick)
                            kick(uid, 4);

                        std::wstring status = newlyBanned ? success : fail;
                        if (!newlyBanned)
                            status += L" This IP is already banned or an internal error occurred.";

                        SetFetchStatus(newlyBanned ? 0xFF90EE90 : 0xFFED4337, status);
                    }
                    else if (g_fetch.hasResult && g_fetch.uid != 0)
                        SetMissingIpStatus();
                    else
                        SetMissingSelectionStatus();
                }

                // UNBAN
                RECT unbanRect{
                    banRect.right + 12,
                    y,
                    banRect.right + 12 + dangerW,
                    y + dangerH
                };

                const bool unbanPressed = DrawButton(unbanRect, "UNBAN", hasCursorPosition, cursorPosition,
                    mousePressedThisFrame, 0xFF65358C, 0xFFA857EB);
                const bool canUnban = g_fetch.hasResult && g_fetch.uid != 0 && !g_fetch.ip.empty();

                if (unbanPressed)
                {
                    if (canUnban)
                    {
                        const bool removedBan = unBanIpAddress(g_fetch.ip);
                        std::wstring status = removedBan ? success : fail;
                        if (!removedBan)
                            status += L" The IP you're trying to unban wasn't banned or an internal error occurred.";

                        SetFetchStatus(removedBan ? 0xFF90EE90 : 0xFFED4337, status);
                    }
                    else if (g_fetch.hasResult && g_fetch.uid != 0)
                        SetMissingIpStatus();
                    else
                        SetMissingSelectionStatus();
                }
                y += dangerH + 12;

                // Autobalance
                const int checkboxSize = 18;

                RECT checkboxRect{
                    panelX + padX,
                    y,
                    panelX + padX + checkboxSize,
                    y + checkboxSize
                };

                const char* const autoBalanceLabel = "Disable TDM AutoBalance";
                RECT checkboxLabelRect{
                    checkboxRect.right + 8,
                    y,
                    panelX + panelSize - padX,
                    y + checkboxSize
                };

                const CheckboxResult autoBalanceCheckbox = DrawCheckbox(
                    checkboxRect, checkboxLabelRect, autoBalanceLabel, autoBalance, true,
                    hasCursorPosition, cursorPosition, mousePressedThisFrame);

                if (autoBalanceCheckbox.clicked)
                {
                    autoBalance = !autoBalance;
                    WritePrivateProfileStringW(L"LOBBY", L"AutoBalance", autoBalance ? L"1" : L"0", iniPath);
                    g_guiDirty = true;
                }

                // Enable 19in1
                RECT enableMapsCheckboxRect{
                    panelX + panelSize / 2,
                    y,
                    panelX + panelSize / 2 + checkboxSize,
                    y + checkboxSize
                };

                const char* const enableMapsLabel = "Enable 19in1 maps";
                RECT enableMapsCheckboxLabelRect{
                    enableMapsCheckboxRect.right + 8,
                    y,
                    panelX + panelSize - padX,
                    y + checkboxSize
                };

                const CheckboxResult enableMapsCheckbox = DrawCheckbox(
                    enableMapsCheckboxRect, enableMapsCheckboxLabelRect, enableMapsLabel, unlockMaps, true,
                    hasCursorPosition, cursorPosition, mousePressedThisFrame);

                if (enableMapsCheckbox.clicked)
                {
                    unlockMaps = !unlockMaps;
                    listMaps();
                    WritePrivateProfileStringW(L"LOBBY", L"19in1", unlockMaps ? L"1" : L"0", iniPath);
                    g_guiDirty = true;
                }

                if (enableMapsCheckbox.hovered)
                {
                    Render::Text(
                        Render::Fonts::MenuText,
                        enableMapsCheckbox.labelRect.left,
                        enableMapsCheckbox.labelRect.top - 2 - checkboxSize,
                        0xFFAAAAAA,
                        "Checks if you have 19in1 installed and enables them"
                    );
                }

                y += checkboxSize + 8;

                // Anti OOB
                RECT antiCheckboxRect{
                    panelX + padX,
                    y,
                    panelX + padX + checkboxSize,
                    y + checkboxSize
                };

                const char* const antiOobLabel = "Anti OOB";
                RECT antiCheckboxLabelRect{
                    antiCheckboxRect.right + 8,
                    y,
                    panelX + panelSize / 2 - 10,
                    y + checkboxSize
                };

                const CheckboxResult antiCheckbox = DrawCheckbox(
                    antiCheckboxRect, antiCheckboxLabelRect, antiOobLabel, antiOOB, true,
                    hasCursorPosition, cursorPosition, mousePressedThisFrame);

                if (antiCheckbox.clicked)
                {
                    antiOOB = !antiOOB;
                    WritePrivateProfileStringW(L"LOBBY", L"AntiOOB", antiOOB ? L"1" : L"0", iniPath);
                    g_guiDirty = true;
                }

                if (antiCheckbox.hovered)
                {
                    Render::Text(
                        Render::Fonts::MenuText,
                        antiCheckbox.labelRect.left,
                        antiCheckbox.labelRect.top + 2 + checkboxSize,
                        0xFFAAAAAA,
                        "Prevents players from entering inaccessible areas"
                    );
                }

                // Bottom value + Save
                const int bottomH = 40;
                const int saveW = 120;

                RECT valueRect{
                    panelX + padX,
                    panelY + panelSize - bottomH - 20,
                    panelX + panelSize - saveW - padX - 10,
                    panelY + panelSize - 20
                };

                RECT saveRect{
                    valueRect.right + 10,
                    valueRect.top,
                    valueRect.right + 10 + saveW,
                    valueRect.bottom
                };

                const bool overValue = hasCursorPosition && IsPointInsideRect(cursorPosition, valueRect);
                const bool overSave = hasCursorPosition && IsPointInsideRect(cursorPosition, saveRect);

                if (mousePressedThisFrame)
                {
                    if (overValue)
                    {
                        SetTextFieldFocus(TextFieldFocus::ClipboardText);
                    }
                    else if (overSave)
                    {
                        g_prompt.clipboardText = Trim(g_prompt.clipboardText);
                        const bool copied = CopyTextToClipboard(g_prompt.clipboardText);
                        SetFetchStatus(
                            copied ? 0xFF90EE90 : 0xFFED4337,
                            copied ? L"Copied to clipboard." : L"Failed to copy to clipboard.");

                        SetTextFieldFocus(TextFieldFocus::None);
                    }
                    else
                    {
                        SetTextFieldFocus(TextFieldFocus::None);
                    }
                }

                const bool savedValueFocused = g_activeTextField == TextFieldFocus::ClipboardText;

                Render::Draw(g_pd3dDevice,
                    valueRect.left, valueRect.top,
                    valueRect.right - valueRect.left,
                    bottomH,
                    savedValueFocused ? 0xFF1A1A2E : 0xFF111111);

                Render::Outline(g_pd3dDevice,
                    valueRect.left, valueRect.top,
                    valueRect.right - valueRect.left,
                    bottomH,
                    savedValueFocused ? 0xFF6AA4FF : 0xFF888888);

                const bool showValuePlaceholder = g_prompt.clipboardText.empty() && !savedValueFocused;
                const std::wstring valueText = showValuePlaceholder ? L"Type value to save..." : g_prompt.clipboardText;

                Render::TextW(Render::Fonts::MenuText,
                    valueRect.left + 6,
                    valueRect.top + 11,
                    showValuePlaceholder ? 0x80FFFFFF : 0xFFFFFFFF,
                    valueText.c_str());

                Render::Draw(g_pd3dDevice,
                    saveRect.left, saveRect.top,
                    saveW, bottomH,
                    overSave ? 0xFF3E6ACB : 0xFF2F4F8F);

                Render::Outline(g_pd3dDevice,
                    saveRect.left, saveRect.top,
                    saveW, bottomH,
                    0xFFFFFFFF);

                Render::Fonts::MenuBold->DrawTextA("Copy to Clipboard", -1, &saveRect,
                    DT_VCENTER | DT_CENTER | DT_NOCLIP, 0xFFFFFFFF);
            }

            // =====================================================
            // PLAYER INVENTORIES PAGE
            // =====================================================
            if (g_currentPage == 1)
            {
                const int controlW = panelSize - (padX * 2);
                const int btnH = 32;

                RECT toggleRect{
                    panelX + padX,
                    y,
                    panelX + padX + controlW,
                    y + btnH
                };

                const bool overToggle = hasCursorPosition && IsPointInsideRect(cursorPosition, toggleRect);
                if (mousePressedThisFrame && overToggle)
                {
                    g_loadoutPresetsEnabled = !g_loadoutPresetsEnabled;
                    if (!g_loadoutPresetsEnabled)
                        g_loadoutPresetDropdownOpen = false;
                    g_guiDirty = true;
                }

                Render::Draw(g_pd3dDevice,
                    toggleRect.left,
                    toggleRect.top,
                    toggleRect.right - toggleRect.left,
                    btnH,
                    g_loadoutPresetsEnabled
                    ? (overToggle ? 0xFF18E314 : 0xFF2CCC29)
                    : (overToggle ? 0xFF1E1E1E : 0xFF101010));

                Render::Outline(g_pd3dDevice,
                    toggleRect.left,
                    toggleRect.top,
                    toggleRect.right - toggleRect.left,
                    btnH,
                    0xFFFFFFFF);

                Render::Text(Render::Fonts::MenuBold,
                    toggleRect.left + 12,
                    toggleRect.top + 7,
                    0xFFFFFFFF,
                    g_loadoutPresetsEnabled ? "Custom Loadouts: ON" : "Custom Loadouts: OFF");

                y += btnH + 12;

                static const std::array<const char*, 5> kLoadoutPresets{
                    "Sniper only",
                    "Machine Gun only",
                    "Pistol only",
                    "Grenades only",
                    "No explosives"
                };

                if (g_selectedLoadoutPresetIndex >= kLoadoutPresets.size())
                    g_selectedLoadoutPresetIndex = kLoadoutPresets.size() - 1;

                RECT comboRect{
                    panelX + padX,
                    y,
                    panelX + padX + controlW,
                    y + btnH
                };

                const bool overCombo = hasCursorPosition && IsPointInsideRect(cursorPosition, comboRect);
                const bool comboPressed = mousePressedThisFrame && overCombo;

                if (comboPressed && g_loadoutPresetsEnabled)
                {
                    g_loadoutPresetDropdownOpen = !g_loadoutPresetDropdownOpen;
                    g_guiDirty = true;
                }

                Render::Draw(g_pd3dDevice,
                    comboRect.left,
                    comboRect.top,
                    comboRect.right - comboRect.left,
                    btnH,
                    g_loadoutPresetsEnabled
                    ? (overCombo ? 0xFF1A1A2E : 0xFF111111)
                    : 0xFF0C0C0C);

                Render::Outline(g_pd3dDevice,
                    comboRect.left,
                    comboRect.top,
                    comboRect.right - comboRect.left,
                    btnH,
                    g_loadoutPresetsEnabled ? 0xFF888888 : 0xFF444444);

                Render::Text(Render::Fonts::MenuText,
                    comboRect.left + 10,
                    comboRect.top + 8,
                    g_loadoutPresetsEnabled ? 0xFFFFFFFF : 0xFF888888,
                    kLoadoutPresets[g_selectedLoadoutPresetIndex]);

                Render::Text(Render::Fonts::MenuText,
                    comboRect.right - 18,
                    comboRect.top + 8,
                    g_loadoutPresetsEnabled ? 0xFFFFFFFF : 0xFF888888,
                    g_loadoutPresetDropdownOpen ? "^" : "v");

                y += btnH;

                if (g_loadoutPresetsEnabled && g_loadoutPresetDropdownOpen)
                {
                    const int itemH = 26;
                    const int listH = itemH * static_cast<int>(kLoadoutPresets.size());
                    RECT listRect{
                        comboRect.left,
                        comboRect.bottom,
                        comboRect.right,
                        comboRect.bottom + listH
                    };

                    Render::Draw(g_pd3dDevice,
                        listRect.left,
                        listRect.top,
                        listRect.right - listRect.left,
                        listRect.bottom - listRect.top,
                        0xFF0F0F0F);
                    Render::Outline(g_pd3dDevice,
                        listRect.left,
                        listRect.top,
                        listRect.right - listRect.left,
                        listRect.bottom - listRect.top,
                        0xFFFFFFFF);

                    bool clickedAnyItem = false;
                    for (int i = 0; i < static_cast<int>(kLoadoutPresets.size()); ++i)
                    {
                        RECT itemRect{
                            listRect.left + 2,
                            listRect.top + (i * itemH),
                            listRect.right - 2,
                            listRect.top + ((i + 1) * itemH)
                        };

                        const bool overItem = hasCursorPosition && IsPointInsideRect(cursorPosition, itemRect);
                        const bool itemPressed = mousePressedThisFrame && overItem;

                        const D3DCOLOR bg = (i == g_selectedLoadoutPresetIndex)
                            ? 0xFF2F4F8F
                            : (overItem ? 0xFF1E1E1E : 0xFF101010);

                        Render::Draw(g_pd3dDevice,
                            itemRect.left,
                            itemRect.top,
                            itemRect.right - itemRect.left,
                            itemRect.bottom - itemRect.top,
                            bg);

                        Render::Text(Render::Fonts::MenuText,
                            itemRect.left + 10,
                            itemRect.top + 5,
                            0xFFFFFFFF,
                            kLoadoutPresets[i]);

                        if (itemPressed)
                        {
                            g_selectedLoadoutPresetIndex = i;
                            WritePrivateProfileStringW(L"INVENTORIES", L"LoadoutPreset", std::to_wstring(g_selectedLoadoutPresetIndex).c_str(), iniPath);
                            g_loadoutPresetDropdownOpen = false;
                            g_guiDirty = true;
                            clickedAnyItem = true;
                        }
                    }

                    const bool overList = hasCursorPosition && IsPointInsideRect(cursorPosition, listRect);
                    if (mousePressedThisFrame && !overList && !overCombo && !overToggle && !clickedAnyItem)
                    {
                        g_loadoutPresetDropdownOpen = false;
                        g_guiDirty = true;
                    }

                    y = listRect.bottom + 12;
                }
                else
                {
                    if (mousePressedThisFrame && !overCombo && !overToggle)
                    {
                        if (g_loadoutPresetDropdownOpen)
                        {
                            g_loadoutPresetDropdownOpen = false;
                            g_guiDirty = true;
                        }
                    }

                    y += 12;
                }
                y += 4;

                // Knife
                const int checkboxSize = 18;

                RECT knifeCheckboxRect{
                    panelX + padX,
                    y,
                    panelX + padX + checkboxSize,
                    y + checkboxSize
                };

                const char* const knifeLabel = "Everyone has knife";
                RECT knifeCheckboxLabelRect{
                    knifeCheckboxRect.right + 8,
                    y,
                    panelX + panelSize - padX,
                    y + checkboxSize
                };

                const CheckboxResult knifeCheckbox = DrawCheckbox(
                    knifeCheckboxRect, knifeCheckboxLabelRect, knifeLabel, g_everyoneHasKnife, true,
                    hasCursorPosition, cursorPosition, mousePressedThisFrame);

                if (knifeCheckbox.clicked)
                {
                    g_everyoneHasKnife = !g_everyoneHasKnife;
                    WritePrivateProfileStringW(L"INVENTORIES", L"EveryOneKnife", g_everyoneHasKnife ? L"1" : L"0", iniPath);
                    g_guiDirty = true;
                }
            }

            promptVisibleThisFrame = true;
        }
    }
    else
    {
        SetTextFieldFocus(TextFieldFocus::None);
    }

    if (!promptVisibleThisFrame)
        SetTextFieldFocus(TextFieldFocus::None);
}

void GUI::DrawGreetingContent()
{
    if (!isHost)
        return;

    const D3DCOLOR greetColor = isPopulated ? 0xFFFFFF00 : 0xFFFFFFFF;
    Render::TextWOutlined(Render::Fonts::Menu, 30, 35, greetColor, greetBuffer, 0xFF000000);
    if (isPopulated)
        Render::TextOutlined(Render::Fonts::MenuText, 30, 60, 0xFFFFFFFF,
            "To enable menu, hit INSERT\nTo disable this fancy text, hit HOME", 0xFF000000);
}

bool GUI::ShouldRedrawGui()
{
    if (g_lastGuiRedrawTick == 0)
        return true;

    return GetTickCount() - g_lastGuiRedrawTick >= kRedrawIntervalMs;
}

bool GUI::ShouldRedrawGreeting()
{
    if (g_lastGreetingRedrawTick == 0)
        return true;

    return GetTickCount() - g_lastGreetingRedrawTick >= kRedrawIntervalMs;
}

bool GUI::GetCursorPosition(POINT& cursorViewport)
{
    if (!g_pd3dDevice)
        return false;

    POINT cursorScreen;
    if (!GetCursorPos(&cursorScreen))
        return false;

    D3DDEVICE_CREATION_PARAMETERS params = {};
    if (FAILED(g_pd3dDevice->GetCreationParameters(&params)) || !params.hFocusWindow)
        return false;

    if (!ScreenToClient(params.hFocusWindow, &cursorScreen))
        return false;

    const RECT viewport = Render::GetViewport(g_pd3dDevice);
    cursorViewport.x = cursorScreen.x - viewport.left;
    cursorViewport.y = cursorScreen.y - viewport.top;
    return true;
}

void GUI::Start(LPDIRECT3DDEVICE8 device)
{
    if (!device || isInitialized)
        return;

    g_pd3dDevice = device;

    CreateStateBlock();
    Render::Initialise(g_pd3dDevice);
    isInitialized = true;
    g_lastGuiRedrawTick = 0;
    g_lastGreetingRedrawTick = 0;
    g_guiDirty = true;
    g_greetingDirty = true;
}

void GUI::Render()
{
    if (!isHost && isVisible)
    {
        isVisible = false;
        ResetFetchState();
        SetTextFieldFocus(TextFieldFocus::None);
    }

    if (!isInitialized || !isVisible || !g_pd3dDevice || !isHost)
    {
        UpdateCursorVisibility(false);
        return;
    }

    if (IsGameLoading())
    {
        UpdateCursorVisibility(false);
        return;
    }

    if (!g_dwOldStateBlock)
        CreateStateBlock();

    if (!g_dwOldStateBlock)
    {
        UpdateCursorVisibility(false);
        return;
    }

    D3DVIEWPORT8 viewport = {};
    g_pd3dDevice->GetViewport(&viewport);
    if (viewport.Width == 0 || viewport.Height == 0)
        return;

    EnsureRenderTargets(viewport);
    if (!g_guiRenderTarget)
        return;

    POINT cursorPosition = {};
    const bool hasCursorPosition = GetCursorPosition(cursorPosition);
    const bool leftMouseDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    const bool mousePressedThisFrame = leftMouseDown && !g_leftMouseWasDown;
    if (mousePressedThisFrame && hasCursorPosition)
    {
        g_mousePressPending = true;
        g_mousePressPosition = cursorPosition;
    }

    if (isVisible && g_currentPage == 0 && g_activeTextField != TextFieldFocus::None)
        ProcessPromptInput();

    const bool shouldRedraw = ShouldRedrawGui();
    UpdateCursorVisibility(isVisible);

    g_pd3dDevice->CaptureStateBlock(g_dwOldStateBlock);
    g_pd3dDevice->GetVertexShader(&g_dwOldVertexShader);

    ApplyGuiRenderState();

    if (shouldRedraw)
    {
        D3DVIEWPORT8 oldViewport = viewport;
        const bool sceneStarted = g_guiRenderTarget->BeginScene();
        if (sceneStarted)
        {
            const POINT interactionCursor = g_mousePressPending ? g_mousePressPosition : cursorPosition;
            const bool hasInteractionCursor = g_mousePressPending || hasCursorPosition;
            DrawGuiContent(Render::GetViewport(g_pd3dDevice), hasInteractionCursor, interactionCursor,
                leftMouseDown, g_mousePressPending);
            g_guiRenderTarget->EndScene();
        }
        g_pd3dDevice->SetViewport(&oldViewport);

        if (sceneStarted)
        {
            g_lastGuiRedrawTick = GetTickCount();
            g_mousePressPending = false;
            g_guiDirty = false;
        }
        else
        {
            g_guiDirty = true;
        }
    }

    g_guiRenderTarget->Blit(static_cast<int>(viewport.X), static_cast<int>(viewport.Y));

    g_leftMouseWasDown = leftMouseDown;

    g_pd3dDevice->ApplyStateBlock(g_dwOldStateBlock);
    g_pd3dDevice->SetVertexShader(g_dwOldVertexShader);
}

void GUI::RenderGreeting()
{
    if (!isInitialized || !g_pd3dDevice || !isGreeting || !isHost || IsGameLoading())
        return;

    if (!g_dwOldStateBlock)
        CreateStateBlock();

    if (!g_dwOldStateBlock)
        return;

    D3DVIEWPORT8 viewport = {};
    g_pd3dDevice->GetViewport(&viewport);
    if (viewport.Width == 0 || viewport.Height == 0)
        return;

    EnsureRenderTargets(viewport);
    if (!g_greetingRenderTarget)
        return;

    const bool shouldRedraw = ShouldRedrawGreeting();

    g_pd3dDevice->CaptureStateBlock(g_dwOldStateBlock);
    g_pd3dDevice->GetVertexShader(&g_dwOldVertexShader);

    ApplyGuiRenderState();

    if (shouldRedraw)
    {
        D3DVIEWPORT8 oldViewport = viewport;
        const bool sceneStarted = g_greetingRenderTarget->BeginScene();
        if (sceneStarted)
        {
            DrawGreetingContent();
            g_greetingRenderTarget->EndScene();
        }
        g_pd3dDevice->SetViewport(&oldViewport);

        if (sceneStarted)
        {
            g_lastGreetingRedrawTick = GetTickCount();
            g_greetingDirty = false;
        }
        else
        {
            g_greetingDirty = true;
        }
    }

    g_greetingRenderTarget->Blit(static_cast<int>(viewport.X), static_cast<int>(viewport.Y));

    g_pd3dDevice->ApplyStateBlock(g_dwOldStateBlock);
    g_pd3dDevice->SetVertexShader(g_dwOldVertexShader);
}

void GUI::Shutdown()
{
    Render::Shutdown();

    UpdateCursorVisibility(false);

    g_arrowCursor = nullptr;

    if (g_dwOldStateBlock)
    {
        g_pd3dDevice->DeleteStateBlock(g_dwOldStateBlock);
        g_dwOldStateBlock = 0;
    }

    DestroyRenderTargets();
    g_pd3dDevice = nullptr;
    isInitialized = false;
}

void GUI::OnDeviceLost()
{
    if (!g_pd3dDevice)
        return;

    Render::Shutdown();

    if (g_dwOldStateBlock)
    {
        g_pd3dDevice->DeleteStateBlock(g_dwOldStateBlock);
        g_dwOldStateBlock = 0;
    }

    DestroyRenderTargets();
    isInitialized = false;
}

void GUI::OnDeviceReset(LPDIRECT3DDEVICE8 device)
{
    if (device)
        g_pd3dDevice = device;

    if (!g_pd3dDevice)
        return;

    CreateStateBlock();
    Render::Initialise(g_pd3dDevice);
    isInitialized = true;
    g_lastGuiRedrawTick = 0;
    g_lastGreetingRedrawTick = 0;
    g_guiDirty = true;
    g_greetingDirty = true;
}

void GUI::Toggle()
{
    if (!isHost)
        return;

    isVisible = !isVisible;
    g_guiDirty = true;

    if (!isVisible)
    {
        UpdateCursorVisibility(false);
        ResetFetchState();
    }
}

void GUI::ToggleGreeting()
{
    if (!isHost)
        return;

    if (isGreeting)
        isGreeting = false;
    g_greetingDirty = true;
}
