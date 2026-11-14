#include "../app/app_internal.h"
#include "../app/ai_assistant_chat.h"
#include "../ui/ui_renderer.h"
#include <cstdio>
#include <filesystem>

using namespace pulse;
void Render(AppState&);

// Drives this app's normal window procedure in an isolated --shot instance.
// No global input, clipboard writes, or user preference persistence.
int RunClassicRibbonProbe(AppState& s, const wchar_t* output) {
    FILE* log = nullptr;
    if (!s.isolatedTest || _wfopen_s(&log, output, L"w") || !log) return 2;
    int failures = 0;
    const auto check = [&](bool ok, const char* what) {
        std::fprintf(log, "[%s] %s\n", ok ? "PASS" : "FAIL", what);
        std::fflush(log);
        failures += !ok;
    };
    const auto pump = [&]() {
        for (int i = 0; i < 20; ++i) {
            MSG msg{};
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
            // A hidden capture has no display-paced frame pump. Exercise the same
            // timer and render path each tick so filter and tray motion can settle.
            SendMessageW(s.hwnd, WM_TIMER, kTimerUi, 0);
            ProcessPendingResults(s);
            Render(s);
            Sleep(20);
        }
        Render(s);
    };
    const auto layout = [&]() {
        BuildVm(s, false);
        return s.renderer.ToolbarLayoutAt(static_cast<float>(s.compositor.Width()), s.renderer.NewButtonWidthPx(false),
                                          s.pane ? s.pane->filter_expand : 0.0f);
    };
    const auto click = [&](const D2D1_RECT_F& b, ui::HitTestResult::Region region, int index = -1) {
        auto vm = BuildVm(s, false);
        const int x = static_cast<int>((b.left + b.right) / 2), y = static_cast<int>((b.top + b.bottom) / 2);
        const auto hit = s.renderer.HitTest(vm, {0, 0, static_cast<float>(s.compositor.Width()), static_cast<float>(s.compositor.Height())},
                                           static_cast<float>(x), static_cast<float>(y));
        check(hit.region == region && (index < 0 || hit.index == index), "painted rectangle hits intended command");
        if (hit.region != region || (index >= 0 && hit.index != index)) return;
        SendMessageW(s.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
        SendMessageW(s.hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
        pump();
    };
    const auto shot = [&](const wchar_t* name) {
        Render(s); Render(s);
        check(s.compositor.SaveSnapshot((std::filesystem::path(output).parent_path() / name).c_str()), "rendered screenshot saved");
    };
    check(s.renderer.ClassicRibbon(), "classic ribbon active");
    {
        const float width = static_cast<float>(s.compositor.Width());
        const float height = static_cast<float>(s.compositor.Height());
        const auto content = s.renderer.ContentRect(width, height);
        check(content.left == s.renderer.EffectiveSidebarWidth(width) && content.right == width &&
              content.top == s.renderer.TitleBarHeight() + s.renderer.ToolbarHeight() &&
              content.bottom == s.renderer.SidebarRect(width, height).bottom,
              "file area meets shared separators without card gutters");
        const auto toolbar = layout();
        check(toolbar.overflow.right == toolbar.overflow.left &&
              std::abs(toolbar.filter.right - (width - 8 * s.scale)) < 0.01f,
              "redundant overflow removed and view commands align to right edge");
    }
    for (int tab : {1, 2, 0}) {
        click(layout().ribbon_tabs[static_cast<size_t>(tab + 1)], ui::HitTestResult::RibbonTab, tab);
        check(s.renderer.RibbonTab() == tab, "ribbon tab switches through normal input handler");
        check(ToolbarPinActions(s).size() == (tab == 1 ? 6u : 9u), "ribbon tab supplies correct command catalog");
        shot(tab == 1 ? L"share.png" : tab == 2 ? L"view.png" : L"home.png");
    }
    auto* active = ActiveTab(s);
    if (active && active->snapshot && active->EntryCount() > 0) {
        click(layout().pins[6], ui::HitTestResult::ToolbarPin, 6);
        check(active->SelectedCount() == active->EntryCount(), "Select all acts on current folder");
        shot(L"selection.png");
        click(layout().pins[7], ui::HitTestResult::ToolbarPin, 7);
        check(active->SelectedCount() == 0, "Invert selection clears fully selected folder");
    } else check(false, "test fixture folder contains entries");
    click(layout().ribbon_tabs[3], ui::HitTestResult::RibbonTab, 2);
    const bool hidden = s.appPrefs.show_hidden_files;
    click(layout().pins[3], ui::HitTestResult::ToolbarPin, 3);
    check(s.appPrefs.show_hidden_files != hidden, "View hidden files toggles the real setting");
    click(layout().pins[3], ui::HitTestResult::ToolbarPin, 3);
    check(s.appPrefs.show_hidden_files == hidden, "View hidden files toggle is reversible");
    click(layout().filter, ui::HitTestResult::FilterBox);
    check(s.filterEditing, "filter editor opens from ribbon");
    check(layout().filter.right - layout().filter.left >= 180 * s.scale, "expanded filter has usable editing width");
    shot(L"filter.png");
    HideFilterEditor(s, false);
    pump();
    click(layout().ribbon_tabs[1], ui::HitTestResult::RibbonTab, 0);
    {
        auto vm = BuildVm(s, false);
        if (!vm.pane_slots.empty()) {
            const auto bounds = vm.pane_slots.front().rect;
            click(s.renderer.PaneMediumIconsRect(bounds, vm.pane.filter_expand, vm.pane.size_control),
                  ui::HitTestResult::PaneMediumIcons);
            check(BuildVm(s, false).pane.view_mode != ui::ViewMode::Details, "grid view button changes view mode");
            shot(L"grid.png");
            vm = BuildVm(s, false);
            click(s.renderer.PaneDetailsRect(bounds, vm.pane.filter_expand, vm.pane.size_control),
                  ui::HitTestResult::PaneDetails);
            check(BuildVm(s, false).pane.view_mode == ui::ViewMode::Details, "details view button restores details");
        } else check(false, "view-button test has a visible pane");
    }
    {
        std::vector<std::wstring> paths;
        WIN32_FIND_DATAW file{};
        const std::wstring root = s.shot.path;
        HANDLE find = FindFirstFileW((root + L"\\*").c_str(), &file);
        if (find != INVALID_HANDLE_VALUE) {
            do {
                if (!(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && file.cFileName[0] != L'.')
                    paths.push_back(root + L"\\" + file.cFileName);
            } while (paths.size() < 2 && FindNextFileW(find, &file));
            FindClose(find);
        }
        s.tray.Clear();
        s.tray.Collect(paths, false);
        pump(); pump();
        auto vm = BuildVm(s, false);
        const auto panel = s.renderer.StagingTrayRect(vm, static_cast<float>(s.compositor.Width()),
                                                     static_cast<float>(s.compositor.Height()));
        const auto bounds = D2D1::RectF(0, 0, static_cast<float>(s.compositor.Width()),
                                            static_cast<float>(s.compositor.Height()));
        check(paths.size() == 2, "tray fixture contains two real files");
        for (auto command : {ui::TrayShelfHit::Release, ui::TrayShelfHit::ReleaseMenu,
                             ui::TrayShelfHit::Zip, ui::TrayShelfHit::Rename,
                             ui::TrayShelfHit::Pick, ui::TrayShelfHit::More}) {
            bool found = false;
            for (float y = panel.top + s.scale; y < panel.bottom && !found; y += 3 * s.scale)
                for (float x = panel.left + s.scale; x < panel.right && !found; x += 3 * s.scale) {
                    const auto hit = s.renderer.HitTest(vm, bounds, x, y);
                    found = hit.region == ui::HitTestResult::TrayShelf && hit.index == static_cast<int>(command);
                }
            check(found, "compact tray command has reachable hit region");
        }
        shot(L"tray.png");
        s.tray.Clear();
        pump();
    }
    // Local, synthetic content only: exercise both AI surfaces without sending a request.
    s.aiAssistant.prefs_loaded = true;
    s.aiAssistant.floating = false;
    AiAssistantOpen(s, false);
    pump();
    check(AiAssistantIsOpen(s), "assistant opens in isolated instance");
    shot(L"assistant-empty.png");
    auto& chat = Chat(s);
    ui::AiBlockView user;
    user.id = 1; user.kind = ui::AiBlockKind::User; user.text = L"Organize this folder";
    ui::AiBlockView answer;
    answer.id = 2; answer.kind = ui::AiBlockKind::Text; answer.text = L"This is a local preview of the classic assistant style.";
    chat.blocks = {user, answer};
    pump();
    shot(L"assistant-conversation.png");
    AiAssistantClose(s, false);
    s.aiAssistant.floating = true;
    AiAssistantOpen(s, false);
    pump();
    shot(L"assistant-floating.png");
    AiAssistantClose(s, false);
    std::fprintf(log, "Failures: %d\n", failures);
    std::fclose(log);
    return failures ? 1 : 0;
}
