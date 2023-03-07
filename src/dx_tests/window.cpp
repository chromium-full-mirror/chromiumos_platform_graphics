// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "window.h"

#include <windows.h>

HWND getTestWindow()
{
  static HWND hwnd = []()
  {
    HWND ret = 0;
    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(WNDCLASSEXA));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &DefWindowProc;
    wc.hInstance = GetModuleHandle(0);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
    wc.lpszClassName = "Dx9SampleApp";

    RegisterClassExA(&wc);

    ret = CreateWindowExA(NULL,
                          "Dx9SampleApp",      // name of the window class
                          "Dx9SampleApp",      // title of the window
                          WS_OVERLAPPEDWINDOW, // window style
                          0,                   // x-position of the window
                          0,                   // y-position of the window
                          256,                 // width of the window
                          256,                 // height of the window
                          NULL,                // we have no parent window, NULL
                          NULL,                // we aren't using menus, NULL
                          GetModuleHandle(0),  // application handle
                          NULL);               // used with multiple windows, NULL
    return ret;
  }();
  return hwnd;
}
