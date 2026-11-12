/*
 * A simple four-function calculator for Windows, using the native Win32 API.
 *
 * No external libraries: this links only against Windows' own built-in DLLs,
 * so the compiled .exe is a true standalone that runs on any Windows machine
 * with nothing to install.
 *
 * Mirrors the Python/Tkinter version: add, subtract, multiply, divide, a
 * running expression line above the main display, chained operations, and
 * graceful division-by-zero handling.
 *
 * Build (on Windows with MinGW):
 *     gcc calculator.c -o calculator.exe -mwindows -municode -lgdi32
 * Cross-build (on Linux):
 *     x86_64-w64-mingw32-gcc calculator.c -o calculator.exe \
 *         -mwindows -municode -lgdi32 -static
 */

#include <windows.h>
#include <commctrl.h>   /* SetWindowSubclass / DefSubclassProc */
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- control ids ---- */
#define ID_DISPLAY 100
#define ID_EXPR    101
#define ID_BTN_BASE 1000  /* button command ids start here */

/* ---- calculator state ---- */
static wchar_t g_entry[64] = L"0";   /* number being typed, as text */
static double  g_first = 0.0;        /* stored left-hand operand    */
static wchar_t g_op = 0;             /* pending operator, or 0      */
static BOOL    g_reset_next = TRUE;  /* next digit starts fresh     */
static BOOL    g_just_eval = FALSE;  /* next digit clears expr line */

static HWND g_display, g_expr;
static HFONT g_font_big, g_font_small, g_font_btn;

/* ---- button layout: label, column, row, width (in grid cells) ---- */
typedef struct { const wchar_t *label; int col, row, w; } BtnDef;
static const BtnDef g_buttons[] = {
    {L"C", 0, 0, 1}, {L"\x232B", 1, 0, 1}, {L"\x00F7", 2, 0, 1}, {L"\x00D7", 3, 0, 1},
    {L"7", 0, 1, 1}, {L"8", 1, 1, 1}, {L"9", 2, 1, 1}, {L"\x2212", 3, 1, 1},
    {L"4", 0, 2, 1}, {L"5", 1, 2, 1}, {L"6", 2, 2, 1}, {L"+", 3, 2, 1},
    {L"1", 0, 3, 1}, {L"2", 1, 3, 1}, {L"3", 2, 3, 1}, {L"=", 3, 3, 1},
    {L"0", 0, 4, 2}, {L".", 2, 4, 1},
};
#define NUM_BUTTONS (int)(sizeof(g_buttons) / sizeof(g_buttons[0]))

/* ---- formatting ---- */

/* Format a double like the Python version: whole numbers with no trailing
 * ".0", others trimmed to ~10 significant digits. */
static void format_number(double value, wchar_t *out, size_t n) {
    if (value == floor(value) && fabs(value) < 1e15) {
        swprintf(out, n, L"%lld", (long long)value);
    } else {
        swprintf(out, n, L"%.10g", value);
    }
}

static const wchar_t *op_symbol(wchar_t op) {
    switch (op) {
        case L'+': return L"+";
        case L'-': return L"\x2212";
        case L'*': return L"\x00D7";
        case L'/': return L"\x00F7";
        default:   return L"";
    }
}

/* ---- display helpers ---- */

static void show_display(const wchar_t *text) { SetWindowTextW(g_display, text); }
static void show_expr(const wchar_t *text)    { SetWindowTextW(g_expr, text); }
static double current_value(void)             { return _wtof(g_entry); }

/* ---- core logic ---- */

/* first <op> second -> *result. Returns FALSE (and shows an error) on /0. */
static BOOL compute(double first, wchar_t op, double second, double *result) {
    switch (op) {
        case L'+': *result = first + second; return TRUE;
        case L'-': *result = first - second; return TRUE;
        case L'*': *result = first * second; return TRUE;
        case L'/':
            if (second == 0.0) {
                show_display(L"Error: \x00F7 by 0");
                g_op = 0; g_reset_next = TRUE; g_just_eval = TRUE;
                return FALSE;
            }
            *result = first / second; return TRUE;
        default: return FALSE;
    }
}

static void start_fresh_if_needed(void) {
    if (g_just_eval) { show_expr(L""); g_just_eval = FALSE; }
}

static void input_digit(wchar_t digit) {
    start_fresh_if_needed();
    if (g_reset_next) {
        g_entry[0] = digit; g_entry[1] = 0;
        g_reset_next = FALSE;
    } else if (wcscmp(g_entry, L"0") == 0) {
        g_entry[0] = digit; g_entry[1] = 0;
    } else {
        size_t len = wcslen(g_entry);
        if (len < (sizeof(g_entry) / sizeof(wchar_t)) - 1) {
            g_entry[len] = digit; g_entry[len + 1] = 0;
        }
    }
    show_display(g_entry);
}

static void input_dot(void) {
    start_fresh_if_needed();
    if (g_reset_next) {
        wcscpy(g_entry, L"0.");
        g_reset_next = FALSE;
    } else if (wcschr(g_entry, L'.') == NULL) {
        size_t len = wcslen(g_entry);
        if (len < (sizeof(g_entry) / sizeof(wchar_t)) - 1) {
            g_entry[len] = L'.'; g_entry[len + 1] = 0;
        }
    }
    show_display(g_entry);
}

static void choose_operator(wchar_t op) {
    wchar_t buf[80], num[64];
    if (g_op && !g_reset_next) {
        double result;
        if (!compute(g_first, g_op, current_value(), &result)) return;
        g_first = result;
        format_number(result, num, 64);
        show_display(num);
    } else {
        g_first = current_value();
    }
    g_op = op;
    g_just_eval = FALSE;
    g_reset_next = TRUE;
    format_number(g_first, num, 64);
    swprintf(buf, 80, L"%ls %ls", num, op_symbol(op));
    show_expr(buf);
}

static void do_equals(void) {
    wchar_t buf[160], fnum[64], snum[64], rnum[64];
    double second, result;
    if (g_op == 0) return;
    second = current_value();
    format_number(g_first, fnum, 64);
    format_number(second, snum, 64);
    swprintf(buf, 160, L"%ls %ls %ls =", fnum, op_symbol(g_op), snum);
    show_expr(buf);
    if (!compute(g_first, g_op, second, &result)) return;
    format_number(result, rnum, 64);
    show_display(rnum);
    wcsncpy(g_entry, rnum, 63); g_entry[63] = 0;
    g_op = 0; g_reset_next = TRUE; g_just_eval = TRUE;
}

static void do_clear(void) {
    wcscpy(g_entry, L"0");
    show_display(L"0");
    show_expr(L"");
    g_first = 0.0; g_op = 0; g_reset_next = TRUE; g_just_eval = FALSE;
}

static void do_backspace(void) {
    size_t len;
    if (g_reset_next) return;
    len = wcslen(g_entry);
    if (len > 0) g_entry[len - 1] = 0;
    if (wcslen(g_entry) == 0 || wcscmp(g_entry, L"-") == 0) wcscpy(g_entry, L"0");
    show_display(g_entry);
}

/* Route a button label (or a keypress mapped to one) to the right action. */
static void handle_label(const wchar_t *label) {
    if (wcslen(label) == 1 && label[0] >= L'0' && label[0] <= L'9') {
        input_digit(label[0]);
    } else if (wcscmp(label, L".") == 0)      input_dot();
    else if (wcscmp(label, L"+") == 0)        choose_operator(L'+');
    else if (wcscmp(label, L"\x2212") == 0)   choose_operator(L'-');
    else if (wcscmp(label, L"\x00D7") == 0)   choose_operator(L'*');
    else if (wcscmp(label, L"\x00F7") == 0)   choose_operator(L'/');
    else if (wcscmp(label, L"=") == 0)        do_equals();
    else if (wcscmp(label, L"C") == 0)        do_clear();
    else if (wcscmp(label, L"\x232B") == 0)   do_backspace();
}

/* ---- keyboard ---- */

static void handle_key(WPARAM vk) {
    if (vk >= '0' && vk <= '9') { input_digit((wchar_t)vk); return; }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        input_digit((wchar_t)(L'0' + (vk - VK_NUMPAD0))); return;
    }
    switch (vk) {
        case VK_DECIMAL: case VK_OEM_PERIOD: input_dot(); break;
        case VK_ADD:      choose_operator(L'+'); break;
        case VK_SUBTRACT: choose_operator(L'-'); break;
        case VK_MULTIPLY: choose_operator(L'*'); break;
        case VK_DIVIDE:   choose_operator(L'/'); break;
        case VK_RETURN:   do_equals(); break;
        case VK_BACK:     do_backspace(); break;
        case VK_ESCAPE:   do_clear(); break;
    }
}

/* Catch the keys that don't arrive as clean WM_KEYDOWN virtual-keys
 * (the shifted +, *, and the = sign on the main row). */
static void handle_char(WPARAM ch) {
    switch (ch) {
        case L'+': choose_operator(L'+'); break;
        case L'-': choose_operator(L'-'); break;
        case L'*': choose_operator(L'*'); break;
        case L'/': choose_operator(L'/'); break;
        case L'=': do_equals(); break;
        case L'.': input_dot(); break;
    }
}

/* ---- painting the dark background ---- */

static LRESULT handle_ctlcolor(WPARAM wParam, HWND hCtl) {
    HDC hdc = (HDC)wParam;
    SetBkMode(hdc, TRANSPARENT);
    if (hCtl == g_display) SetTextColor(hdc, RGB(0xCD, 0xD6, 0xF4));
    else                   SetTextColor(hdc, RGB(0x6C, 0x70, 0x86));
    static HBRUSH bg = NULL;
    if (!bg) bg = CreateSolidBrush(RGB(0x1E, 0x1E, 0x2E));
    return (LRESULT)bg;
}

/* ---- window procedure ---- */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_COMMAND: {
            int id = LOWORD(wParam);
            if (id >= ID_BTN_BASE && id < ID_BTN_BASE + NUM_BUTTONS) {
                handle_label(g_buttons[id - ID_BTN_BASE].label);
                SetFocus(hwnd);  /* keep keyboard focus on the window */
            }
            return 0;
        }
        case WM_KEYDOWN:   handle_key(wParam); return 0;
        case WM_CHAR:      handle_char(wParam); return 0;
        case WM_CTLCOLORSTATIC:
            return handle_ctlcolor(wParam, (HWND)lParam);
        case WM_ERASEBKGND: {
            RECT rc; GetClientRect(hwnd, &rc);
            HBRUSH b = CreateSolidBrush(RGB(0x1E, 0x1E, 0x2E));
            FillRect((HDC)wParam, &rc, b);
            DeleteObject(b);
            return 1;
        }
        case WM_DESTROY:   PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ---- owner-drawn colored buttons ---- */

static LRESULT CALLBACK ButtonProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

/* Pick a button's colors from its label. */
static void button_colors(const wchar_t *label, COLORREF *bg, COLORREF *fg) {
    if (wcscmp(label, L"=") == 0) { *bg = RGB(0xA6,0xE3,0xA1); *fg = RGB(0x1E,0x1E,0x2E); return; }
    if (wcscmp(label, L"C") == 0 || wcscmp(label, L"\x232B") == 0) {
        *bg = RGB(0x45,0x47,0x5A); *fg = RGB(0xCD,0xD6,0xF4); return;
    }
    if (wcscmp(label, L"+")==0 || wcscmp(label, L"\x2212")==0 ||
        wcscmp(label, L"\x00D7")==0 || wcscmp(label, L"\x00F7")==0) {
        *bg = RGB(0xF9,0xE2,0xAF); *fg = RGB(0x1E,0x1E,0x2E); return;
    }
    *bg = RGB(0x31,0x32,0x44); *fg = RGB(0xCD,0xD6,0xF4);  /* numbers */
}

/* ---- create everything ---- */

#define CELL_W 70
#define CELL_H 60
#define GAP    6
#define MARGIN 8

static void create_controls(HWND hwnd) {
    int grid_top = 110;  /* leaves room for the two display lines */

    g_expr = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        MARGIN, 20, 4 * CELL_W + 3 * GAP, 28,
        hwnd, (HMENU)ID_EXPR, NULL, NULL);
    SendMessageW(g_expr, WM_SETFONT, (WPARAM)g_font_small, TRUE);

    g_display = CreateWindowExW(0, L"STATIC", L"0",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        MARGIN, 52, 4 * CELL_W + 3 * GAP, 48,
        hwnd, (HMENU)ID_DISPLAY, NULL, NULL);
    SendMessageW(g_display, WM_SETFONT, (WPARAM)g_font_big, TRUE);

    for (int i = 0; i < NUM_BUTTONS; i++) {
        const BtnDef *b = &g_buttons[i];
        int x = MARGIN + b->col * (CELL_W + GAP);
        int y = grid_top + b->row * (CELL_H + GAP);
        int w = b->w * CELL_W + (b->w - 1) * GAP;
        /* "=" spans two rows tall. */
        int h = (wcscmp(b->label, L"=") == 0) ? (2 * CELL_H + GAP) : CELL_H;

        HWND btn = CreateWindowExW(0, L"BUTTON", b->label,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            x, y, w, h, hwnd, (HMENU)(INT_PTR)(ID_BTN_BASE + i), NULL, NULL);
        SendMessageW(btn, WM_SETFONT, (WPARAM)g_font_btn, TRUE);
        SetWindowSubclass(btn, ButtonProc, 0, 0);
    }
}

/* Owner-draw handler so buttons get the Catppuccin colors. */
static LRESULT CALLBACK ButtonProc(HWND hwnd, UINT msg, WPARAM wParam,
                                   LPARAM lParam, UINT_PTR idSub, DWORD_PTR ref) {
    (void)idSub; (void)ref;
    if (msg == WM_PAINT) {
        wchar_t label[16];
        GetWindowTextW(hwnd, label, 16);
        COLORREF bg, fg;
        button_colors(label, &bg, &fg);

        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);

        HBRUSH b = CreateSolidBrush(bg);
        FillRect(hdc, &rc, b);
        DeleteObject(b);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, fg);
        HFONT old = (HFONT)SelectObject(hdc, g_font_btn);
        DrawTextW(hdc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, old);

        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev, PWSTR cmd, int show) {
    (void)hPrev; (void)cmd;

    g_font_big   = CreateFontW(34, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                   0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_small = CreateFontW(18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                   0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    g_font_btn   = CreateFontW(22, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                   0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"CalcWindow";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0x1E, 0x1E, 0x2E));
    RegisterClassW(&wc);

    int width  = 2 * MARGIN + 4 * CELL_W + 3 * GAP + 16;
    int height = 110 + 5 * CELL_H + 4 * GAP + 48;

    HWND hwnd = CreateWindowExW(0, L"CalcWindow", L"Calculator",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, hInst, NULL);

    create_controls(hwnd);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    SetFocus(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
