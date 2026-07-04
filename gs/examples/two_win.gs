UIDF W1 = two_win.ui
CGSB
static void create_cgs_win(void) {
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = DefWindowProcA;
  wc.hInstance = GetModuleHandleA(NULL);
  wc.lpszClassName = "cgs_class";
  wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
  RegisterClassA(&wc);
  HWND h = CreateWindowExA(0, "cgs_class", "CGS Window",
    WS_OVERLAPPEDWINDOW|WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, 420, 260,
    NULL, NULL, GetModuleHandleA(NULL), NULL);
  CreateWindowExA(0, "STATIC", "This window is created from CGSB..CGSE block",
    WS_CHILD|WS_VISIBLE|SS_LEFT, 20, 20, 360, 24, h, (HMENU)1000, NULL, NULL);
  ShowWindow(h, SW_SHOW);
  UpdateWindow(h);
}
CGSE
CGSC create_cgs_win();
UILP W1