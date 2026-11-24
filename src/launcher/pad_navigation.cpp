// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <iterator>
#include <optional>
#include <vector>
#include <windows.h>
// windows.h first
#include <commctrl.h>

#include "launcher/pad_commands.h"
#include "launcher/pad_navigation.h"

namespace Gr2Port::Launcher::PadNavigation {

namespace {

using PadCommands::Command;

// Milliseconds between looks at the pads.
constexpr UINT PollMs = 16;
// The most rows a page scrolls by to show the focused control.
constexpr int MaxScrollRows = 64;
// The frame around the focused control: its width and its margin around the control, in pixels
// at 96 DPI.
constexpr const wchar_t* MarkClass = L"GR2portPadMark";
constexpr int MarkWidth = 2;
constexpr int MarkMargin = 3;

UINT_PTR g_timer = 0;
// The dialogs the pads have moved in. A dialog's first direction goes to its first control; one
// the pads come back to, as a dialog over it closes, keeps its focus.
std::vector<HWND> g_touched;
// The combo box whose list a pad opened, and its choice before, which going back restores.
HWND g_opened = nullptr;
LRESULT g_opened_choice = CB_ERR;
// A frame in the highlight colour around the control the pads have focused, as a game's menu
// marks its chosen row; the dialogs' own focus rectangles are too faint to follow from a
// distance. It belongs to the dialog in front, and follows its focus once the pads have moved in
// a dialog.
HWND g_mark = nullptr;
RECT g_marked{};
bool g_marking = false;

bool HasClass(HWND window, const wchar_t* name) {
    wchar_t found[32]{};
    return GetClassNameW(window, found, static_cast<int>(std::size(found))) > 0 &&
           lstrcmpiW(found, name) == 0;
}

bool IsRadioButton(HWND window) {
    const LONG type = GetWindowLongW(window, GWL_STYLE) & BS_TYPEMASK;
    return HasClass(window, WC_BUTTONW) && (type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON);
}

struct ClassSearch {
    const wchar_t* name;
    bool found;
};

BOOL CALLBACK FindClass(HWND child, LPARAM search) {
    auto* const wanted = reinterpret_cast<ClassSearch*>(search);
    if (HasClass(child, wanted->name)) {
        wanted->found = true;
        return FALSE;
    }
    return TRUE;
}

bool HasChildOfClass(HWND window, const wchar_t* name) {
    ClassSearch search{name, false};
    EnumChildWindows(window, &FindClass, reinterpret_cast<LPARAM>(&search));
    return search.found;
}

// Whether a dialog draws its controls with DirectUI, as the task dialogs and the folder picker
// do.
bool DirectUi(HWND dialog) {
    return HasChildOfClass(dialog, L"DirectUIHWND");
}

// The frame passes the mouse through and paints the highlight colour inside its region.
LRESULT CALLBACK MarkProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(window, &paint);
        RECT area{};
        GetClientRect(window, &area);
        FillRect(dc, &area, GetSysColorBrush(COLOR_HIGHLIGHT));
        EndPaint(window, &paint);
        return 0;
    }
    case WM_NCDESTROY:
        // A dialog destroys the frame it owns as it closes.
        if (window == g_mark) {
            g_mark = nullptr;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

void HideMark() {
    if (g_mark != nullptr) {
        ShowWindow(g_mark, SW_HIDE);
    }
}

// Places the frame around the dialog's focused control, as a popup the dialog owns, which stays
// above it.
void Mark(HWND dialog, HWND focus) {
    if (g_mark != nullptr && GetWindow(g_mark, GW_OWNER) != dialog) {
        DestroyWindow(g_mark);
    }
    if (g_mark == nullptr) {
        g_mark = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, MarkClass, L"",
            WS_POPUP, 0, 0, 0, 0, dialog, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (g_mark == nullptr) {
            return;
        }
        SetLayeredWindowAttributes(g_mark, 0, 255, LWA_ALPHA);
        g_marked = RECT{};
    }
    const UINT dpi = GetDpiForWindow(dialog);
    const int width = MulDiv(MarkWidth, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    const int margin = MulDiv(MarkMargin, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    RECT area{};
    GetWindowRect(focus, &area);
    InflateRect(&area, margin, margin);
    if (!EqualRect(&area, &g_marked)) {
        g_marked = area;
        const int area_width = area.right - area.left;
        const int area_height = area.bottom - area.top;
        SetWindowPos(g_mark, nullptr, area.left, area.top, area_width, area_height,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        const HRGN frame = CreateRectRgn(0, 0, area_width, area_height);
        const HRGN inside = CreateRectRgn(width, width, area_width - width, area_height - width);
        CombineRgn(frame, frame, inside, RGN_DIFF);
        DeleteObject(inside);
        // The window owns the region from here.
        SetWindowRgn(g_mark, frame, TRUE);
    }
    if (!IsWindowVisible(g_mark)) {
        ShowWindow(g_mark, SW_SHOWNOACTIVATE);
    }
}

BOOL CALLBACK FindTabs(HWND child, LPARAM found) {
    if (IsWindowVisible(child) && HasClass(child, WC_TABCONTROLW)) {
        *reinterpret_cast<HWND*>(found) = child;
        return FALSE;
    }
    return TRUE;
}

// A key press for the control itself, past the dialog's own handling of the keyboard.
void Press(HWND control, WPARAM key) {
    SendMessageW(control, WM_KEYDOWN, key, 1);
    SendMessageW(control, WM_KEYUP, key, static_cast<LPARAM>(0xC0000001));
}

// A key press typed into the window in front, as the keyboard gives it.
void Type(WORD key, bool shift = false) {
    INPUT inputs[4]{};
    UINT count = 0;
    const auto add = [&inputs, &count](WORD code, DWORD flags) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = code;
        inputs[count].ki.dwFlags = flags;
        ++count;
    };
    if (shift) {
        add(VK_SHIFT, 0);
    }
    add(key, 0);
    add(key, KEYEVENTF_KEYUP);
    if (shift) {
        add(VK_SHIFT, KEYEVENTF_KEYUP);
    }
    SendInput(count, inputs, sizeof(INPUT));
}

// Scrolls the pages around the focused control by rows until it is in sight.
void ShowFocused(HWND dialog) {
    const HWND focus = GetFocus();
    if (focus == nullptr) {
        return;
    }
    for (HWND page = GetParent(focus); page != nullptr && page != dialog; page = GetParent(page)) {
        SCROLLINFO scroll{sizeof(scroll), SIF_POS};
        if (!GetScrollInfo(page, SB_VERT, &scroll)) {
            continue;
        }
        for (int i = 0; i < MaxScrollRows; ++i) {
            RECT area{};
            RECT control{};
            GetClientRect(page, &area);
            GetWindowRect(focus, &control);
            MapWindowPoints(nullptr, page, reinterpret_cast<POINT*>(&control), 2);
            WPARAM step = 0;
            if (control.top < area.top) {
                step = SB_LINEUP;
            } else if (control.bottom > area.bottom && control.top > area.top) {
                step = SB_LINEDOWN;
            } else {
                break;
            }
            const int before = GetScrollPos(page, SB_VERT);
            SendMessageW(page, WM_VSCROLL, step, 0);
            if (GetScrollPos(page, SB_VERT) == before) {
                break;
            }
        }
    }
}

// The first control that Tab stops at inside parent, but not a tab control, whose tabs the
// shoulder buttons change.
HWND FirstTabStop(HWND parent) {
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindowVisible(child) || !IsWindowEnabled(child)) {
            continue;
        }
        if ((GetWindowLongW(child, GWL_STYLE) & WS_TABSTOP) != 0 &&
            !HasClass(child, WC_TABCONTROLW)) {
            return child;
        }
        if ((GetWindowLongW(child, GWL_EXSTYLE) & WS_EX_CONTROLPARENT) != 0) {
            if (const HWND inner = FirstTabStop(child)) {
                return inner;
            }
        }
    }
    return nullptr;
}

void Focus(HWND dialog, HWND control) {
    SendMessageW(dialog, WM_NEXTDLGCTL, reinterpret_cast<WPARAM>(control), TRUE);
    ShowFocused(dialog);
}

// Up and down pass over a tab control.
void MoveFocus(HWND dialog, bool back) {
    SendMessageW(dialog, WM_NEXTDLGCTL, back, FALSE);
    if (HasClass(GetFocus(), WC_TABCONTROLW)) {
        SendMessageW(dialog, WM_NEXTDLGCTL, back, FALSE);
    }
    ShowFocused(dialog);
}

// The dialog's tab control shows its previous or next tab, around the ends, and the dialog's
// first control takes the focus: the first on the page inside the tab control, or the list that
// the tab control fills.
void ChangeTab(HWND dialog, bool back) {
    HWND tabs = nullptr;
    EnumChildWindows(dialog, &FindTabs, reinterpret_cast<LPARAM>(&tabs));
    if (tabs == nullptr) {
        return;
    }
    const int count = TabCtrl_GetItemCount(tabs);
    if (count <= 0) {
        return;
    }
    const int shown = TabCtrl_GetCurSel(tabs);
    SendMessageW(tabs, TCM_SETCURFOCUS, (shown + (back ? count - 1 : 1)) % count, 0);
    if (const HWND first = FirstTabStop(dialog)) {
        Focus(dialog, first);
    }
}

void OpenList(HWND combo) {
    g_opened = combo;
    g_opened_choice = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
}

// Closes the list. Going back restores the choice the list opened with, and tells the dialog.
void CloseList(HWND combo, bool keep) {
    SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
    if (!keep && combo == g_opened && SendMessageW(combo, CB_GETCURSEL, 0, 0) != g_opened_choice) {
        SendMessageW(combo, CB_SETCURSEL, g_opened_choice, 0);
        SendMessageW(GetParent(combo), WM_COMMAND, MAKEWPARAM(GetDlgCtrlID(combo), CBN_SELCHANGE),
                     reinterpret_cast<LPARAM>(combo));
    }
    g_opened = nullptr;
}

// The list's current row takes a double click, as the mouse gives it.
void DoubleClick(HWND list, int row) {
    NMITEMACTIVATE activate{};
    activate.hdr.hwndFrom = list;
    activate.hdr.idFrom = static_cast<UINT_PTR>(GetDlgCtrlID(list));
    activate.hdr.code = NM_DBLCLK;
    activate.iItem = row;
    SendMessageW(GetParent(list), WM_NOTIFY, activate.hdr.idFrom,
                 reinterpret_cast<LPARAM>(&activate));
}

// The launcher's menu takes the arrows and Enter.
void InMenu(HWND window, Command command) {
    switch (command) {
    case Command::Up:
        PostMessageW(window, WM_KEYDOWN, VK_UP, 1);
        break;
    case Command::Down:
        PostMessageW(window, WM_KEYDOWN, VK_DOWN, 1);
        break;
    case Command::Accept:
        PostMessageW(window, WM_KEYDOWN, VK_RETURN, 1);
        break;
    default:
        break;
    }
}

// A dialog of controls takes a command by the control that has the focus. Up and down move
// between the controls, and left and right too, but change a combo box's choice, a slider, a
// tab control's tab and a group's radio button. A list moves its row until its first or last.
// The first direction in a dialog goes to its first control, whichever has the focus as it opens.
void InDialog(HWND dialog, HWND focus, Command command) {
    const bool horizontal = command == Command::Left || command == Command::Right;
    g_marking = true;
    std::erase_if(g_touched, [](HWND touched) { return !IsWindow(touched); });
    if (std::ranges::find(g_touched, dialog) == g_touched.end()) {
        g_touched.push_back(dialog);
        // The focus rectangles show from now on, as they do after a key press.
        SendMessageW(dialog, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS), 0);
        if (horizontal || command == Command::Up || command == Command::Down) {
            if (const HWND first = FirstTabStop(dialog)) {
                Focus(dialog, first);
            }
            return;
        }
    }
    if (HasClass(focus, WC_COMBOBOXW)) {
        if (SendMessageW(focus, CB_GETDROPPEDSTATE, 0, 0) != FALSE) {
            if (command == Command::Up || command == Command::Down) {
                Press(focus, command == Command::Up ? VK_UP : VK_DOWN);
            } else if (command == Command::Accept || command == Command::Back) {
                CloseList(focus, command == Command::Accept);
            }
            return;
        }
        if (horizontal) {
            Press(focus, command == Command::Left ? VK_UP : VK_DOWN);
            return;
        }
        if (command == Command::Accept) {
            OpenList(focus);
            return;
        }
    } else if (HasClass(focus, TRACKBAR_CLASSW) || HasClass(focus, WC_TABCONTROLW)) {
        if (horizontal) {
            Press(focus, command == Command::Left ? VK_LEFT : VK_RIGHT);
            return;
        }
    } else if (HasClass(focus, WC_LISTVIEWW)) {
        const int row = ListView_GetNextItem(focus, -1, LVNI_FOCUSED);
        if (command == Command::Up && row > 0) {
            Press(focus, VK_UP);
            return;
        }
        if (command == Command::Down && row + 1 < ListView_GetItemCount(focus)) {
            Press(focus, VK_DOWN);
            return;
        }
        if (command == Command::Accept) {
            if (row >= 0) {
                DoubleClick(focus, row);
            }
            return;
        }
    } else if (HasClass(focus, WC_BUTTONW)) {
        if (horizontal && IsRadioButton(focus)) {
            // The group's next radio button, past the labels between its buttons.
            HWND next = focus;
            do {
                next = GetNextDlgGroupItem(GetParent(focus), next, command == Command::Left);
            } while (next != nullptr && next != focus && !IsRadioButton(next));
            if (next != nullptr && next != focus) {
                Focus(dialog, next);
                PostMessageW(next, BM_CLICK, 0, 0);
            }
            return;
        }
        if (command == Command::Accept) {
            PostMessageW(focus, BM_CLICK, 0, 0);
            return;
        }
    } else if (HasClass(focus, WC_LINK)) {
        if (command == Command::Accept) {
            Press(focus, VK_RETURN);
            return;
        }
    }
    switch (command) {
    case Command::Up:
    case Command::Left:
        MoveFocus(dialog, true);
        break;
    case Command::Down:
    case Command::Right:
        MoveFocus(dialog, false);
        break;
    case Command::Back:
        PostMessageW(dialog, WM_COMMAND, IDCANCEL,
                     reinterpret_cast<LPARAM>(GetDlgItem(dialog, IDCANCEL)));
        break;
    case Command::PreviousTab:
    case Command::NextTab:
        ChangeTab(dialog, command == Command::PreviousTab);
        break;
    case Command::Accept:
        break;
    }
}

// Windows drawn with DirectUI, the task dialogs and the folder picker, take typed keys: the
// arrows, Escape, Tab and Shift+Tab for the shoulder buttons, and for accept Space in a task
// dialog, which presses its buttons and ticks its check box, and Enter in the folder picker.
void Typed(HWND window, Command command) {
    switch (command) {
    case Command::Up:
        Type(VK_UP);
        break;
    case Command::Down:
        Type(VK_DOWN);
        break;
    case Command::Left:
        Type(VK_LEFT);
        break;
    case Command::Right:
        Type(VK_RIGHT);
        break;
    case Command::Accept:
        Type(HasChildOfClass(window, L"DUIViewWndClassName") ? VK_RETURN : VK_SPACE);
        break;
    case Command::Back:
        Type(VK_ESCAPE);
        break;
    case Command::PreviousTab:
        Type(VK_TAB, true);
        break;
    case Command::NextTab:
        Type(VK_TAB);
        break;
    }
}

// The launcher's menu, a dialog of controls, or a DirectUI window in front takes the command.
void Dispatch(HWND front, Command command) {
    if (!HasClass(front, L"#32770")) {
        InMenu(front, command);
        return;
    }
    const HWND focus = GetFocus();
    if (DirectUi(front) || focus == nullptr || !IsChild(front, focus)) {
        Typed(front, command);
        return;
    }
    InDialog(front, focus, command);
}

// The frame follows the focused control of a dialog of controls in front, and hides elsewhere.
void FollowFocus() {
    // A list the pads opened and something else closed has no choice to restore.
    if (g_opened != nullptr && SendMessageW(g_opened, CB_GETDROPPEDSTATE, 0, 0) == FALSE) {
        g_opened = nullptr;
    }
    const HWND front = GetForegroundWindow();
    const HWND focus = GetFocus();
    if (!g_marking || front == nullptr ||
        GetWindowThreadProcessId(front, nullptr) != GetCurrentThreadId() ||
        !HasClass(front, L"#32770") || focus == nullptr || !IsChild(front, focus) ||
        DirectUi(front)) {
        HideMark();
        return;
    }
    Mark(front, focus);
}

// A command goes to this thread's window in front; with another program's window in front, the
// pads move nothing.
void CALLBACK OnTimer(HWND, UINT, UINT_PTR, DWORD) {
    if (const std::optional<Command> command = PadCommands::Next()) {
        const HWND front = GetForegroundWindow();
        if (front != nullptr && GetWindowThreadProcessId(front, nullptr) == GetCurrentThreadId()) {
            Dispatch(front, *command);
        }
    }
    FollowFocus();
}

} // namespace

void Start() {
    PadCommands::Start();
    WNDCLASSEXW mark_class{};
    mark_class.cbSize = sizeof(mark_class);
    mark_class.lpfnWndProc = &MarkProc;
    mark_class.hInstance = GetModuleHandleW(nullptr);
    mark_class.lpszClassName = MarkClass;
    RegisterClassExW(&mark_class);
    if (g_timer == 0) {
        g_timer = SetTimer(nullptr, 0, PollMs, &OnTimer);
    }
}

void Stop() {
    if (g_timer != 0) {
        KillTimer(nullptr, g_timer);
        g_timer = 0;
    }
    if (g_mark != nullptr) {
        DestroyWindow(g_mark);
    }
    UnregisterClassW(MarkClass, GetModuleHandleW(nullptr));
    g_marking = false;
    g_touched.clear();
    PadCommands::Stop();
}

} // namespace Gr2Port::Launcher::PadNavigation
