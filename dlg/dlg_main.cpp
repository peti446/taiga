/*
** Taiga, a lightweight client for MyAnimeList
** Copyright (C) 2010-2012, Eren Okka
** 
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
** 
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
** 
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "../std.h"

#include "dlg_main.h"

#include "dlg_anime_info.h"
#include "dlg_search.h"
#include "dlg_season.h"
#include "dlg_settings.h"
#include "dlg_test_recognition.h"
#include "dlg_torrent.h"

#include "../anime_db.h"
#include "../anime_filter.h"
#include "../announce.h"
#include "../common.h"
#include "../debug.h"
#include "../event.h"
#include "../gfx.h"
#include "../http.h"
#include "../media.h"
#include "../monitor.h"
#include "../myanimelist.h"
#include "../process.h"
#include "../recognition.h"
#include "../resource.h"
#include "../settings.h"
#include "../string.h"
#include "../taiga.h"
#include "../theme.h"

#include "../win32/win_gdi.h"
#include "../win32/win_taskbar.h"
#include "../win32/win_taskdialog.h"

class MainDialog MainDialog;

// =============================================================================

MainDialog::MainDialog() {
  RegisterDlgClass(L"TaigaMainW");
}

BOOL MainDialog::OnInitDialog() {
  // Set global variables
  g_hMain = GetWindowHandle();
  
  // Initialize window position
  InitWindowPosition();

  // Set icons
  SetIconLarge(IDI_MAIN);
  SetIconSmall(IDI_MAIN);
  
  // Create controls
  CreateDialogControls();

  // Start process timer
  SetTimer(g_hMain, TIMER_MAIN, 1000, nullptr);
  
  // Add icon to taskbar
  Taskbar.Create(g_hMain, nullptr, APP_TITLE);
  UpdateTip();
  
  // Change status
  ChangeStatus();
  
  // Refresh list
  RefreshList(mal::MYSTATUS_WATCHING);
  
  // Set search bar mode
  //search_bar.Index = Settings.Program.General.SearchIndex;
  search_bar.SetMode(2, SEARCH_MODE_MAL, L"Search MyAnimeList");
  
  // Refresh menus
  UpdateAllMenus();

  // Apply start-up settings
  if (Settings.Account.MAL.auto_login) {
    ExecuteAction(L"Login");
  }
  if (Settings.Program.StartUp.check_new_episodes) {
    ExecuteAction(L"CheckEpisodes()", TRUE);
  }
  if (!Settings.Program.StartUp.minimize) {
    Show(Settings.Program.Exit.remember_pos_size && Settings.Program.Position.maximized ? 
      SW_MAXIMIZE : SW_SHOWNORMAL);
  }
  if (Settings.Account.MAL.user.empty()) {
    win32::TaskDialog dlg(APP_TITLE, TD_ICON_INFORMATION);
    dlg.SetMainInstruction(L"Welcome to Taiga!");
    dlg.SetContent(L"User name is not set. Would you like to open settings window to set it now?");
    dlg.AddButton(L"Yes", IDYES);
    dlg.AddButton(L"No", IDNO);
    dlg.Show(g_hMain);
    if (dlg.GetSelectedButtonID() == IDYES) {
      ExecuteAction(L"Settings", 0, PAGE_ACCOUNT);
    }
  }
  if (Settings.Folders.watch_enabled) {
    FolderMonitor.SetWindowHandle(GetWindowHandle());
    FolderMonitor.Enable();
  }

  // Success
  return TRUE;
}

void MainDialog::CreateDialogControls() {
  // Create rebar
  rebar.Attach(GetDlgItem(IDC_REBAR_MAIN));
  // Create menu toolbar
  toolbar_menu.Attach(GetDlgItem(IDC_TOOLBAR_MENU));
  toolbar_menu.SetImageList(nullptr, 0, 0);
  // Create main toolbar
  toolbar_main.Attach(GetDlgItem(IDC_TOOLBAR_MAIN));
  toolbar_main.SetImageList(UI.ImgList24.GetHandle(), 24, 24);
  toolbar_main.SendMessage(TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_MIXEDBUTTONS);
  // Create search toolbar
  toolbar_search.Attach(GetDlgItem(IDC_TOOLBAR_SEARCH));
  toolbar_search.SetImageList(UI.ImgList16.GetHandle(), 16, 16);
  toolbar_search.SendMessage(TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_MIXEDBUTTONS);
  // Create search text
  edit.Attach(GetDlgItem(IDC_EDIT_SEARCH));
  edit.SetCueBannerText(L"Search list");
  edit.SetParent(toolbar_search.GetWindowHandle());
  edit.SetPosition(nullptr, ScaleX(32), 1, 200, 20);
  edit.SetMargins(1, 16);
  win32::Rect rcEdit; edit.GetRect(&rcEdit);
  // Create cancel search button
  cancel_button.Attach(GetDlgItem(IDC_BUTTON_CANCELSEARCH));
  cancel_button.SetParent(edit.GetWindowHandle());
  cancel_button.SetPosition(nullptr, rcEdit.right + 1, 0, 16, 16);
  // Create treeview control
  treeview.Attach(GetDlgItem(IDC_TREE_MAIN));
  treeview.SetItemHeight(20);
  treeview.SetTheme();
  // Create tab control
  tab.Attach(GetDlgItem(IDC_TAB_MAIN));
  // Create main list
  listview.Attach(GetDlgItem(IDC_LIST_MAIN));
  listview.SetExtendedStyle(LVS_EX_AUTOSIZECOLUMNS | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_INFOTIP | LVS_EX_LABELTIP);
  listview.SetImageList(UI.ImgList16.GetHandle());
  listview.Sort(0, 1, 0, ListViewCompareProc);
  listview.SetTheme();
  // Create status bar
  statusbar.Attach(GetDlgItem(IDC_STATUSBAR_MAIN));
  statusbar.SetImageList(UI.ImgList16.GetHandle());
  statusbar.InsertPart(-1, 0, 0, 900, nullptr, nullptr);
  statusbar.InsertPart(ICON16_CLOCK, 0, 0,  32, nullptr, nullptr);

  // Insert treeview items
  treeview.RefreshItems();

  // Insert list columns
  listview.InsertColumn(0, GetSystemMetrics(SM_CXSCREEN), 340, LVCFMT_LEFT, L"Anime title");
  listview.InsertColumn(1, 160, 160, LVCFMT_CENTER, L"Progress");
  listview.InsertColumn(2,  62,  62, LVCFMT_CENTER, L"Score");
  listview.InsertColumn(3,  62,  62, LVCFMT_CENTER, L"Type");
  listview.InsertColumn(4, 105, 105, LVCFMT_RIGHT,  L"Season");

  // Insert menu toolbar buttons
  BYTE fsStyle0 = BTNS_AUTOSIZE | BTNS_DROPDOWN | BTNS_SHOWTEXT;
  toolbar_menu.InsertButton(0, I_IMAGENONE, 100, 1, fsStyle0, 0, L"  File", nullptr);
  toolbar_menu.InsertButton(1, I_IMAGENONE, 101, 1, fsStyle0, 0, L"  Account", nullptr);
  toolbar_menu.InsertButton(2, I_IMAGENONE, 102, 1, fsStyle0, 0, L"  List", nullptr);
  toolbar_menu.InsertButton(3, I_IMAGENONE, 103, 1, fsStyle0, 0, L"  Help", nullptr);
  // Insert main toolbar buttons
  BYTE fsStyle1 = BTNS_AUTOSIZE;
  BYTE fsStyle2 = BTNS_AUTOSIZE | BTNS_WHOLEDROPDOWN;
  toolbar_main.InsertButton(0,  ICON24_OFFLINE,  200, 1, fsStyle1,  0, nullptr, L"Log in");
  toolbar_main.InsertButton(1,  ICON24_SYNC,     201, 1, fsStyle1,  1, nullptr, L"Synchronize list");
  toolbar_main.InsertButton(2,  ICON24_MAL,      202, 1, fsStyle1,  2, nullptr, L"View your panel at MyAnimeList");
  toolbar_main.InsertButton(3,  0, 0, 0, BTNS_SEP, 0, nullptr, nullptr);
  toolbar_main.InsertButton(4,  ICON24_FOLDERS,  204, 1, fsStyle2,  4, nullptr, L"Anime folders");
  toolbar_main.InsertButton(5,  ICON24_CALENDAR, 205, 1, fsStyle1,  5, nullptr, L"Season browser");
  toolbar_main.InsertButton(6,  ICON24_TOOLS,    206, 1, fsStyle2,  6, nullptr, L"Tools");
  toolbar_main.InsertButton(7,  ICON24_RSS,      207, 1, fsStyle1,  7, nullptr, L"Torrents");
  toolbar_main.InsertButton(8,  0, 0, 0, BTNS_SEP, 0, nullptr, nullptr);
  toolbar_main.InsertButton(9,  ICON24_FILTER,   209, 1, fsStyle1,  9, nullptr, L"Filter list");
  toolbar_main.InsertButton(10, ICON24_SETTINGS, 210, 1, fsStyle1, 10, nullptr, L"Change program settings");
#ifdef _DEBUG
  toolbar_main.InsertButton(11, 0, 0, 0, BTNS_SEP, 0, nullptr, nullptr);
  toolbar_main.InsertButton(12, ICON24_ABOUT,    212, 1, fsStyle1, 12, nullptr, L"Debug");
#endif
  // Insert search toolbar button
  toolbar_search.InsertButton(0, ICON16_SEARCH, 300, 1, fsStyle2, 0, nullptr, L"Search");

  // Insert rebar bands
  UINT fMask = RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_HEADERSIZE | RBBIM_SIZE | RBBIM_STYLE;
  UINT fStyle = RBBS_NOGRIPPER;
  rebar.InsertBand(toolbar_menu.GetWindowHandle(), 
    GetSystemMetrics(SM_CXSCREEN), 
    0, 0, 0, 0, 0, 0,
    HIWORD(toolbar_menu.GetButtonSize()), 
    fMask, fStyle);
  rebar.InsertBand(toolbar_main.GetWindowHandle(), 
    GetSystemMetrics(SM_CXSCREEN), 
    WIN_CONTROL_MARGIN, 0, 0, 0, 0, 0, 
    HIWORD(toolbar_main.GetButtonSize()) + 2, 
    fMask, fStyle | RBBS_BREAK);
  rebar.InsertBand(toolbar_search.GetWindowHandle(), 
    0, WIN_CONTROL_MARGIN, 0, 240, 0, 0, 0, 
    HIWORD(toolbar_search.GetButtonSize()), 
    fMask, fStyle);

  // Insert tabs and list groups
  for (int i = mal::MYSTATUS_WATCHING; i <= mal::MYSTATUS_PLANTOWATCH; i++) {
    if (i != mal::MYSTATUS_UNKNOWN) {
      tab.InsertItem(i - 1, mal::TranslateMyStatus(i, true).c_str(), (LPARAM)i);
      listview.InsertGroup(i, mal::TranslateMyStatus(i, false).c_str());
    }
  }

  listview.parent = this;
  search_bar.parent = this;
}

void MainDialog::InitWindowPosition() {
  UINT flags = SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER;
  const LONG min_w = ScaleX(786);
  const LONG min_h = ScaleX(568);
  
  win32::Rect rcParent, rcWindow;
  ::GetWindowRect(GetParent(), &rcParent);
  rcWindow.Set(
    Settings.Program.Position.x, 
    Settings.Program.Position.y, 
    Settings.Program.Position.x + Settings.Program.Position.w,
    Settings.Program.Position.y + Settings.Program.Position.h);

  if (rcWindow.left < 0 || rcWindow.left >= rcParent.right || 
      rcWindow.top < 0 || rcWindow.top >= rcParent.bottom) {
        flags |= SWP_NOMOVE;
  }
  if (rcWindow.Width() < min_w) {
    rcWindow.right = rcWindow.left + min_w;
  }
  if (rcWindow.Height() < min_h) {
    rcWindow.bottom = rcWindow.top + min_h;
  }
  if (rcWindow.Width() > rcParent.Width()) {
    rcWindow.right = rcParent.left + rcParent.Width();
  }
  if (rcWindow.Height() > rcParent.Height()) {
    rcWindow.bottom = rcParent.top + rcParent.Height();
  }
  if (rcWindow.Width() > 0 && rcWindow.Height() > 0 && 
    Settings.Program.Position.maximized == FALSE &&
    Settings.Program.Exit.remember_pos_size == TRUE) {
      SetPosition(nullptr, rcWindow, flags);
      if (flags & SWP_NOMOVE) {
        CenterOwner();
      }
  }

  SetSizeMin(min_w, min_h);
  SetSnapGap(10);
}

// =============================================================================

INT_PTR MainDialog::DialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch (uMsg) {
    // Log off / Shutdown
    case WM_ENDSESSION: {
      OnDestroy();
      return FALSE;
    }

    // Drag list item
    case WM_MOUSEMOVE: {
      if (listview.dragging) {
        listview.drag_image.DragMove(LOWORD(lParam) + 16, HIWORD(lParam) + 24);
        SetCursor(LoadCursor(nullptr, tab.HitTest() > -1 ? IDC_ARROW : IDC_NO));
      }
      break;
    }
    case WM_LBUTTONUP: {
      if (listview.dragging) {
        listview.drag_image.DragLeave(g_hMain);
        listview.drag_image.EndDrag();
        listview.drag_image.Destroy();
        listview.dragging = false;
        ReleaseCapture();
        int tab_index = tab.HitTest();
        if (tab_index > -1) {
          int status = tab.GetItemParam(tab_index);
          ExecuteAction(L"EditStatus(" + ToWstr(status) + L")");
        }
      }
      break;
    }

    // Forward mouse wheel messages to the list
    case WM_MOUSEWHEEL: {
      return listview.SendMessage(uMsg, wParam, lParam);
    }

    // Back & forward buttons
    case WM_XBUTTONUP: {
      int index = tab.GetCurrentlySelected();
      int count = tab.GetItemCount();
      switch (HIWORD(wParam)) {
        case XBUTTON1: index--; break;
        case XBUTTON2: index++; break;
      }
      if (index < 0 || index > count - 1) return TRUE;
      tab.SetCurrentlySelected(index);
      index++; if (index == 5) index = 6;
      RefreshList(index);
      return TRUE;
    }

    // Monitor anime folders
    case WM_MONITORCALLBACK: {
      FolderMonitor.OnChange(reinterpret_cast<FolderInfo*>(lParam));
      return TRUE;
    }
    
    // External programs
    case WM_COPYDATA: {
      PCOPYDATASTRUCT pCDS = (PCOPYDATASTRUCT)lParam;
      // Skype
      if (reinterpret_cast<HWND>(wParam) == Skype.api_window_handle) {
        return TRUE; // pCDS->lpData is the response

      // JetAudio
      } else if (pCDS->dwData == 0x3000 /* JRC_COPYDATA_ID_TRACK_FILENAME */) {
        MediaPlayers.new_title = ToUTF8(reinterpret_cast<LPCSTR>(pCDS->lpData));
        return TRUE;

      // Media Portal
      } else if (pCDS->dwData == 0x1337) {
        MediaPlayers.new_title = ToUTF8(reinterpret_cast<LPCSTR>(pCDS->lpData));
        return TRUE;
      }
      break;
    }
    default: {
      // Skype
      if (uMsg == Skype.control_api_attach) {
        switch (lParam) {
          case 0: // ATTACH_SUCCESS
#ifdef _DEBUG
            ChangeStatus(L"Skype attach succeeded.");
#endif
            Skype.api_window_handle = reinterpret_cast<HWND>(wParam);
            Skype.ChangeMood();
            return TRUE;
          case 1: // ATTACH_PENDING_AUTHORIZATION
#ifdef _DEBUG
            ChangeStatus(L"Skype is pending authorization...");
#endif
            return TRUE;
        }
      }
    }
  }
  
  return DialogProcDefault(hwnd, uMsg, wParam, lParam);
}

BOOL MainDialog::PreTranslateMessage(MSG* pMsg) {
  switch (pMsg->message) {
    case WM_KEYDOWN: {
      if (::GetFocus() == edit.GetWindowHandle()) {
        switch (pMsg->wParam) {
          // Clear search text
          case VK_ESCAPE: {
            edit.SetText(L"");
            return TRUE;
          }
          // Search
          case VK_RETURN: {
            wstring text;
            edit.GetText(text);
            if (text.empty()) break;
            switch (search_bar.mode) {
              case SEARCH_MODE_MAL: {
                switch (Settings.Account.MAL.api) {
                  case MAL_API_OFFICIAL: {
                    ExecuteAction(L"SearchAnime(" + text + L")");
                    return TRUE;
                  }
                  case MAL_API_NONE: {
                    mal::ViewAnimeSearch(text); // TEMP
                    return TRUE;
                  }
                }
                break;
              }
              case SEARCH_MODE_TORRENT: {
                Feed* feed = Aggregator.Get(FEED_CATEGORY_LINK);
                if (feed) {
                  wstring search_url = search_bar.url;
                  Replace(search_url, L"%search%", text);
                  ExecuteAction(L"Torrents");
                  TorrentDialog.ChangeStatus(L"Searching torrents for \"" + text + L"\"...");
                  feed->Check(search_url);
                  return TRUE;
                }
                break;
              }
              case SEARCH_MODE_WEB: {
                wstring search_url = search_bar.url;
                Replace(search_url, L"%search%", text);
                ExecuteLink(search_url);
                return TRUE;
              }
            }
            break;
          }
        }
      }
      break;
    }
  }

  return FALSE;
}

// =============================================================================

BOOL MainDialog::OnClose() {
  if (Settings.Program.General.close) {
    Hide();
    return TRUE;
  }
  
  if (Settings.Program.Exit.ask) {
    win32::TaskDialog dlg(APP_TITLE, TD_ICON_INFORMATION);
    dlg.SetMainInstruction(L"Are you sure you want to exit?");
    dlg.AddButton(L"Yes", IDYES);
    dlg.AddButton(L"No", IDNO);
    dlg.Show(g_hMain);
    if (dlg.GetSelectedButtonID() != IDYES) return TRUE;
  }

  return FALSE;
}

BOOL MainDialog::OnDestroy() {
  // Announce
  if (Taiga.play_status == PLAYSTATUS_PLAYING) {
    Taiga.play_status = PLAYSTATUS_STOPPED;
    Announcer.Do(ANNOUNCE_TO_HTTP);
  }
  Announcer.Clear(ANNOUNCE_TO_MESSENGER | ANNOUNCE_TO_SKYPE);
  
  // Close other dialogs
  AnimeDialog.Destroy();
  RecognitionTest.Destroy();
  SearchDialog.Destroy();
  SeasonDialog.Destroy();
  TorrentDialog.Destroy();
  
  // Cleanup
  MainClient.Cleanup();
  Taskbar.Destroy();
  TaskbarList.Release();
  
  // Save settings
  if (Settings.Program.Exit.remember_pos_size) {
    Settings.Program.Position.maximized = (GetWindowLong() & WS_MAXIMIZE) ? TRUE : FALSE;
    if (Settings.Program.Position.maximized == FALSE) {
      bool invisible = !IsVisible();
      if (invisible) ActivateWindow(GetWindowHandle());
      win32::Rect rcWindow; GetWindowRect(&rcWindow);
      if (invisible ) Hide();
      Settings.Program.Position.x = rcWindow.left;
      Settings.Program.Position.y = rcWindow.top;
      Settings.Program.Position.w = rcWindow.Width();
      Settings.Program.Position.h = rcWindow.Height();
    }
  }
  Settings.Save();

  // Save anime database
  AnimeDatabase.SaveDatabase();
  
  // Exit
  Taiga.PostQuitMessage();
  return TRUE;
}

void MainDialog::OnDropFiles(HDROP hDropInfo) {
#ifdef _DEBUG
  WCHAR buffer[MAX_PATH];
  if (DragQueryFile(hDropInfo, 0, buffer, MAX_PATH) > 0) {
    anime::Episode episode;
    Meow.ExamineTitle(buffer, episode); 
    MessageBox(ReplaceVariables(Settings.Program.Balloon.format, episode).c_str(), APP_TITLE, MB_OK);
  }
#endif
}

LRESULT MainDialog::OnNotify(int idCtrl, LPNMHDR pnmh) {
  // ListView control
  if (idCtrl == IDC_LIST_MAIN || pnmh->hwndFrom == listview.GetHeader()) {
    return OnListNotify(reinterpret_cast<LPARAM>(pnmh));
  
  // Tab control
  } else if (idCtrl == IDC_TAB_MAIN) {
    return OnTabNotify(reinterpret_cast<LPARAM>(pnmh));

  // Toolbar controls
  } else if (idCtrl == IDC_TOOLBAR_MENU || idCtrl == IDC_TOOLBAR_MAIN || idCtrl == IDC_TOOLBAR_SEARCH) {
    return OnToolbarNotify(reinterpret_cast<LPARAM>(pnmh));

  // Tree control
  } else if (idCtrl == IDC_TREE_MAIN) {
    return OnTreeNotify(reinterpret_cast<LPARAM>(pnmh));

  // Button control
  } else if (idCtrl == IDC_BUTTON_CANCELSEARCH) {
    if (pnmh->code == NM_CUSTOMDRAW) {
      return OnButtonCustomDraw(reinterpret_cast<LPARAM>(pnmh));
    }
  }
  
  return 0;
}

void MainDialog::OnSize(UINT uMsg, UINT nType, SIZE size) {
  switch (uMsg) {
    case WM_ENTERSIZEMOVE: {
      if (::IsAppThemed() && win32::GetWinVersion() >= win32::VERSION_VISTA) {
        SetTransparency(200);
      }
      break;
    }
    case WM_EXITSIZEMOVE: {
      if (::IsAppThemed() && win32::GetWinVersion() >= win32::VERSION_VISTA) {
        SetTransparency(255);
      }
      break;
    }
    case WM_SIZE: {
      if (IsIconic()) {
        if (Settings.Program.General.minimize) Hide();
        return;
      }

      // Set client area
      win32::Rect rcWindow(0, 0, size.cx, size.cy);
      rcWindow.Inflate(-ScaleX(WIN_CONTROL_MARGIN), -ScaleY(WIN_CONTROL_MARGIN));
      // Resize rebar
      rebar.SendMessage(WM_SIZE, 0, 0);
      rcWindow.top += rebar.GetBarHeight() + 2;
      // Resize status bar
      win32::Rect rcStatus;
      statusbar.GetClientRect(&rcStatus);
      statusbar.SendMessage(WM_SIZE, 0, 0);
      UpdateStatusTimer();
      rcWindow.bottom -= rcStatus.Height();
      // Resize treeview
      if (treeview.IsVisible()) {
        treeview.SetPosition(nullptr, rcWindow.left, rcWindow.top, 200 /* TEMP */, rcWindow.Height());
        rcWindow.left += 200 + WIN_CONTROL_MARGIN;
      }
      // Resize tab
      tab.SetPosition(nullptr, rcWindow);
      // Resize list
      tab.AdjustRect(nullptr, FALSE, &rcWindow);
      rcWindow.left -= 3; rcWindow.top -= 1;
      listview.SetPosition(nullptr, rcWindow, 0);
    }
  }
}

// =============================================================================

/* Timer */

// This function is very delicate, even order of things are important.
// Please be careful with what you change.

void MainDialog::OnTimer(UINT_PTR nIDEvent) {
  // Check event queue
  Taiga.ticker_queue++;
  if (Taiga.ticker_queue >= 5 * 60) { // 5 minutes
    Taiga.ticker_queue = 0;
    if (EventQueue.updating == false) {
      EventQueue.Check();
    }
  }

  // ===========================================================================
  
  // Check new episodes (if folder monitor is disabled)
  if (!Settings.Folders.watch_enabled) {
    Taiga.ticker_new_episodes++;
    if (Taiga.ticker_new_episodes >= 30 * 60) { // 30 minutes
      Taiga.ticker_new_episodes = 0;
      ExecuteAction(L"CheckEpisodes()", TRUE);
    }
  }

  // ===========================================================================

  // Check feeds
  for (unsigned int i = 0; i < Aggregator.feeds.size(); i++) {
    switch (Aggregator.feeds[i].category) {
      case FEED_CATEGORY_LINK:
        if (Settings.RSS.Torrent.check_enabled) {
          Aggregator.feeds[i].ticker++;
        }
        if (Settings.RSS.Torrent.check_enabled && Settings.RSS.Torrent.check_interval) {
          if (TorrentDialog.IsWindow()) {
            TorrentDialog.SetTimerText(L"Check new torrents [" + 
              ToTimeString(Settings.RSS.Torrent.check_interval * 60 - Aggregator.feeds[i].ticker) + L"]");
          }
          if (Aggregator.feeds[i].ticker >= Settings.RSS.Torrent.check_interval * 60) {
            Aggregator.feeds[i].Check(Settings.RSS.Torrent.source);
          }
        } else {
          if (TorrentDialog.IsWindow()) {
            TorrentDialog.SetTimerText(L"Check new torrents");
          }
        }
        break;
    }
  }

  // ===========================================================================
  
  // Check process list for media players
  auto anime_item = AnimeDatabase.FindItem(CurrentEpisode.anime_id);
  int media_index = MediaPlayers.Check();

  // Media player is running
  if (media_index > -1) {
    // Started to watch?
    if (CurrentEpisode.anime_id == anime::ID_UNKNOWN) {
      // Recognized?
      if (Taiga.is_recognition_enabled) {
        if (Meow.ExamineTitle(MediaPlayers.current_title, CurrentEpisode)) {
          anime_item = Meow.MatchDatabase(CurrentEpisode, false, true);
          if (anime_item) {
            CurrentEpisode.Set(CurrentEpisode.anime_id);
            anime_item->StartWatching(CurrentEpisode);
            return;
          }
        }
        // Not recognized
#ifdef _DEBUG
        std::multimap<int, int> scores = Meow.GetScores();
        debug::Print(L"Not recognized: " + CurrentEpisode.title + L"\n");
        debug::Print(L"Could be:\n");
        for (auto it = scores.begin(); it != scores.end(); ++it) {
          debug::Print(L"* " + AnimeDatabase.items[it->second].GetTitle() + 
                       L" | Score: " + ToWstr(-it->first) + L"\n");
        }
#endif  
        CurrentEpisode.Set(anime::ID_NOTINLIST);
        if (CurrentEpisode.title.empty()) {
#ifdef _DEBUG
          ChangeStatus(MediaPlayers.items[MediaPlayers.index].name + L" is running.");
#endif
        } else if (Settings.Program.Balloon.enabled) {
          ChangeStatus(L"Watching: " + CurrentEpisode.title + 
            PushString(L" #", CurrentEpisode.number) + L" (Not recognized)");
          wstring tip_text = ReplaceVariables(Settings.Program.Balloon.format, CurrentEpisode);
          tip_text += L"\nClick here to search MyAnimeList for this anime.";
          Taiga.current_tip_type = TIPTYPE_SEARCH;
          Taskbar.Tip(L"", L"", 0);
          Taskbar.Tip(tip_text.c_str(), L"Media is not in your list", NIIF_WARNING);
        }
      }

    // Already watching or not recognized before
    } else {
      // Tick and compare with delay time
      if (Taiga.ticker_media > -1 && Taiga.ticker_media <= Settings.Account.Update.delay) {
        if (Taiga.ticker_media == Settings.Account.Update.delay) {
          // Disable ticker
          Taiga.ticker_media = -1;
          // Announce current episode
          Announcer.Do(ANNOUNCE_TO_HTTP | ANNOUNCE_TO_MESSENGER | ANNOUNCE_TO_MIRC | ANNOUNCE_TO_SKYPE);
          // Update
          if (Settings.Account.Update.time == UPDATE_MODE_AFTERDELAY && anime_item)
            anime_item->UpdateList(CurrentEpisode);
          return;
        }
        if (Settings.Account.Update.check_player == FALSE || 
            MediaPlayers.items[media_index].window_handle == GetForegroundWindow())
          Taiga.ticker_media++;
      }
      // Caption changed?
      if (MediaPlayers.TitleChanged() == true) {
        CurrentEpisode.Set(anime::ID_UNKNOWN);
        if (anime_item) {
          anime_item->EndWatching(CurrentEpisode);
          anime_item->UpdateList(CurrentEpisode);
        }
        Taiga.ticker_media = 0;
      }
    }
  
  // Media player is NOT running
  } else {
    // Was running, but not watching
    if (!anime_item) {
      if (MediaPlayers.index_old > 0){
        ChangeStatus();
        CurrentEpisode.Set(anime::ID_UNKNOWN);
        MediaPlayers.index_old = 0;
      }
    
    // Was running and watching
    } else {
      CurrentEpisode.Set(anime::ID_UNKNOWN);
      anime_item->EndWatching(CurrentEpisode);
      if (Settings.Account.Update.time == UPDATE_MODE_WAITPLAYER)
        anime_item->UpdateList(CurrentEpisode);
      Taiga.ticker_media = 0;
    }
  }

  // Update status timer
  UpdateStatusTimer();
}

// =============================================================================

/* Taskbar */

void MainDialog::OnTaskbarCallback(UINT uMsg, LPARAM lParam) {
  // Taskbar creation notification
  if (uMsg == WM_TASKBARCREATED) {
    Taskbar.Create(m_hWindow, nullptr, APP_TITLE);
  
  // Windows 7 taskbar interface
  } else if (uMsg == WM_TASKBARBUTTONCREATED) {
    TaskbarList.Initialize(m_hWindow);

  // Taskbar callback
  } else if (uMsg == WM_TASKBARCALLBACK) {
    switch (lParam) {
      case NIN_BALLOONSHOW: {
        debug::Print(L"Tip type: " + ToWstr(Taiga.current_tip_type) + L"\n");
        break;
      }
      case NIN_BALLOONTIMEOUT: {
        Taiga.current_tip_type = TIPTYPE_NORMAL;
        break;
      }
      case NIN_BALLOONUSERCLICK: {
        switch (Taiga.current_tip_type) {
          case TIPTYPE_SEARCH:
            ExecuteAction(L"SearchAnime(" + CurrentEpisode.title + L")");
            break;
          case TIPTYPE_TORRENT:
            ExecuteAction(L"Torrents");
            break;
          case TIPTYPE_UPDATEFAILED:
            EventQueue.Check();
            break;
        }
        Taiga.current_tip_type = TIPTYPE_NORMAL;
        break;
      }
      case WM_LBUTTONDBLCLK: {
        ActivateWindow(m_hWindow);
        break;
      }
      case WM_RBUTTONDOWN: {
        UpdateAllMenus(AnimeDatabase.GetCurrentItem());
        SetForegroundWindow();
        ExecuteAction(UI.Menus.Show(m_hWindow, 0, 0, L"Tray"));
        UpdateAllMenus(AnimeDatabase.GetCurrentItem());
        break;
      }
    }
  }
}

// =============================================================================

void MainDialog::ChangeStatus(wstring str) {
  // Change status text
  if (str.empty() && CurrentEpisode.anime_id > anime::ID_UNKNOWN) {
    auto anime_item = AnimeDatabase.FindItem(CurrentEpisode.anime_id);
    str = L"Watching: " + anime_item->GetTitle() + PushString(L" #", CurrentEpisode.number);
    if (Settings.Account.Update.out_of_range && 
      GetEpisodeLow(CurrentEpisode.number) > anime_item->GetMyLastWatchedEpisode() + 1) {
        str += L" (out of range)";
    }
  }
  if (!str.empty()) str = L"  " + str;
  statusbar.SetText(str.c_str());
}

void MainDialog::EnableInput(bool enable) {
  // Enable/disable toolbar buttons
  toolbar_main.EnableButton(0, enable);
  toolbar_main.EnableButton(1, enable);
  // Enable/disable list
  listview.Enable(enable);
}

int MainDialog::GetListIndex(int anime_id) {
  for (int i = 0; i < listview.GetItemCount(); i++)
    if (static_cast<int>(listview.GetItemParam(i)) == anime_id)
      return i;
  return -1;
}

void MainDialog::RefreshList(int index) {
  // Change window title
  wstring title = APP_TITLE;
  if (!Settings.Account.MAL.user.empty()) {
    title += L" - " + Settings.Account.MAL.user + L"'";
    title += EndsWith(Settings.Account.MAL.user, L"s") ? L"" : L"s";
    title += L" Anime List";
  }
  SetText(title.c_str());

  // Hide list to avoid visual defects and gain performance
  listview.Hide();
  listview.EnableGroupView(index == 0 && win32::GetWinVersion() > win32::VERSION_XP);
  listview.DeleteAllItems();

  // Remember last index
  static int last_index = 0;
  if (index == -1) index = last_index;
  if (index > 0) last_index = index;

  // Add items
  int group_index = -1, icon_index = 0, status = 0;
  vector<int> group_count(6);
  for (auto it = AnimeDatabase.items.begin(); it != AnimeDatabase.items.end(); ++it) {
    if (!it->second.IsInList()) continue;
    status = it->second.GetMyStatus();
    if (status == index || index == 0 || (index == mal::MYSTATUS_WATCHING && it->second.GetMyRewatching())) {
      if (AnimeFilters.CheckItem(it->second)) {
        group_index = win32::GetWinVersion() > win32::VERSION_XP ? status : -1;
        icon_index = it->second.GetPlaying() ? ICON16_PLAY : StatusToIcon(it->second.GetAiringStatus());
        group_count.at(status - 1)++;
        int i = listview.GetItemCount();
        listview.InsertItem(i, group_index, icon_index, 
          0, nullptr, LPSTR_TEXTCALLBACK, static_cast<LPARAM>(it->second.GetId()));
        listview.SetItem(i, 2, mal::TranslateNumber(it->second.GetMyScore()).c_str());
        listview.SetItem(i, 3, mal::TranslateType(it->second.GetType()).c_str());
        listview.SetItem(i, 4, mal::TranslateDateToSeason(it->second.GetDate(anime::DATE_START)).c_str());
      }
    }
  }

  // Set group headers
  for (int i = mal::MYSTATUS_WATCHING; i <= mal::MYSTATUS_PLANTOWATCH; i++) {
    if (index == 0 && i != mal::MYSTATUS_UNKNOWN) {
      wstring text = mal::TranslateMyStatus(i, false);
      text += group_count.at(i - 1) > 0 ? L" (" + ToWstr(group_count.at(i - 1)) + L")" : L"";
      listview.SetGroupText(i, text.c_str());
    }
  }

  // Sort items
  listview.Sort(listview.GetSortColumn(), listview.GetSortOrder(), 
    listview.GetSortType(listview.GetSortColumn()), ListViewCompareProc);

  // Show again
  listview.Show(SW_SHOW);
}

void MainDialog::RefreshTabs(int index, bool redraw) {
  // Remember last index
  static int last_index = 1;
  if (index == -1) index = last_index;
  if (index == 6) index--;
  last_index = index;
  if (!redraw) return;
  
  // Hide
  tab.Hide();

  // Refresh text
  for (int i = 1; i <= 6; i++) {
    if (i != 5) {
      tab.SetItemText(i == 6 ? 4 : i - 1, mal::TranslateMyStatus(i, true).c_str());
    }
  }

  // Select related tab
  tab.SetCurrentlySelected(--index);

  // Show again
  tab.Show(SW_SHOW);
}

void MainDialog::SearchBar::SetMode(UINT index, UINT mode, wstring cue_text, wstring url) {
  this->index = index;
  this->mode = mode;
  this->url = url;
  this->cue_text = cue_text;
  parent->edit.SetCueBannerText(cue_text.c_str());
  Settings.Program.General.search_index = index;
}

void MainDialog::UpdateStatusTimer() {
  win32::Rect rect;
  GetClientRect(&rect);
  
  int seconds = Settings.Account.Update.delay - Taiga.ticker_media;

  if (CurrentEpisode.anime_id > anime::ID_UNKNOWN && 
    seconds > 0 && seconds < Settings.Account.Update.delay) {
      wstring str = L"List update in " + ToTimeString(seconds);
      statusbar.SetPartText(1, str.c_str());
      statusbar.SetPartTipText(1, str.c_str());

      statusbar.SetPartWidth(0, rect.Width() - ScaleX(160));
      statusbar.SetPartWidth(1, ScaleX(160));
  
  } else {
    statusbar.SetPartWidth(0, rect.Width());
    statusbar.SetPartWidth(1, 0);
  }
}

void MainDialog::UpdateTip() {
  wstring tip = APP_TITLE;
  if (Taiga.logged_in) {
    tip += L"\n" + AnimeDatabase.user.GetName() + L" is logged in.";
  }
  if (CurrentEpisode.anime_id > 0) {
    auto anime_item = AnimeDatabase.FindItem(CurrentEpisode.anime_id);
    tip += L"\nWatching: " + anime_item->GetTitle() + 
      (!CurrentEpisode.number.empty() ? L" #" + CurrentEpisode.number : L"");
  }
  Taskbar.Modify(tip.c_str());
}