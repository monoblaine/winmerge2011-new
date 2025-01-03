// H2O2.cpp
//
// Copyright (c) 2005-2010  David Nash (as of Win32++ v7.0.2)
// Copyright (c) 2011-2018  Jochen Neubeck
//
// SPDX-License-Identifier: MIT
////////////////////////////////////////////////////////////////////////////////

#include "StdAfx.h"
#include "RegKey.h"
#include "SettingStore.h"

using namespace H2O;

/*
 * IsWindowsVersionOrGreater() originates from
 * https://github.com/brunophilipe/SDL2/.../SDL_windows.c
 * Copyright (C) 1997-2018 Sam Lantinga <slouken@libsdl.org>
 * SPDX-License-Identifier: Zlib
 */
static BOOL IsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
	OSVERSIONINFOEX osvi;
	DWORDLONG const dwlConditionMask = VerSetConditionMask(
		VerSetConditionMask(
		VerSetConditionMask(
		0, VER_MAJORVERSION, VER_GREATER_EQUAL ),
		VER_MINORVERSION, VER_GREATER_EQUAL ),
		VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL );

	ZeroMemory(&osvi, sizeof osvi);
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	osvi.dwMajorVersion = wMajorVersion;
	osvi.dwMinorVersion = wMinorVersion;
	osvi.wServicePackMajor = wServicePackMajor;

	return VerifyVersionInfo(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask);
}

BOOL const OWindow::m_vista_or_greater = IsWindowsVersionOrGreater(6, 0, 0);

ATOM OWindow::m_button;
HIGHCONTRAST OWindow::m_highcontrast;

#define DEFAULT_DPI 96

static int GetScalingDPI()
{
	int dpi = 0;
	if (HDC hdc = GetDC(NULL))
	{
		dpi = GetDeviceCaps(hdc, LOGPIXELSX); // assuming square pixels
		ReleaseDC(NULL, hdc);
	}
	return std::max(dpi, DEFAULT_DPI);
}

static LPDLGTEMPLATE ScaleDialogTemplate(LPVOID p, int dpi)
{
	union UDlgTemplate
	{
		struct
		{
			DWORD style;
			DWORD exStyle;
			WORD cDlgItems;
			short x;
			short y;
			short cx;
			short cy;
			WCHAR raw[1];
		} dlg;
		struct
		{
			WORD dlgVer;
			WORD signature;
			DWORD helpID;
			DWORD exStyle;
			DWORD style;
			WORD cDlgItems;
			short x;
			short y;
			short cx;
			short cy;
			WCHAR raw[1];
		} dlgEx;
	} *const pTemplate = static_cast<UDlgTemplate *>(p);
	LPWSTR raw = NULL;
	if (pTemplate->dlgEx.signature == 0xFFFF)
	{
		if (pTemplate->dlgEx.style & DS_SHELLFONT)
		{
			raw = pTemplate->dlgEx.raw;
		}
	}
	else
	{
		if (pTemplate->dlg.style & DS_SHELLFONT)
		{
			raw = pTemplate->dlg.raw;
		}
	}
	if (raw)
	{
		// Skip menu name string or ordinal
		raw += *raw == 0xFFFF ? 2 : wcslen(raw) + 1;
		// Skip class name string or ordinal
		raw += *raw == 0xFFFF ? 2 : wcslen(raw) + 1;
		// Skip caption string
		raw += wcslen(raw) + 1;
		*raw = MulDiv(*raw, dpi, DEFAULT_DPI);
	}
	return static_cast<LPDLGTEMPLATE>(p);
}

static LPDLGTEMPLATE ScaleDialogTemplate(HINSTANCE hInstance, LPCTSTR lpTemplateName)
{
	LPDLGTEMPLATE pTemplate = NULL;
	int const dpi = GetScalingDPI();
	// Celebrate the Arrow Antipattern ;)
	if (dpi != DEFAULT_DPI)
	{
		if (HRSRC hFindRes = FindResource(hInstance, lpTemplateName, RT_DIALOG))
		{
			if (HGLOBAL hLoadRes = LoadResource(hInstance, hFindRes))
			{
				if (DWORD dwSizeRes = SizeofResource(hInstance, hFindRes))
				{
					if (LPVOID q = LockResource(hLoadRes))
					{
						if (LPVOID p = GlobalAlloc(GPTR, dwSizeRes))
						{
							memcpy(p, q, dwSizeRes);
							pTemplate = ScaleDialogTemplate(p, dpi);
						}
					}
				}
			}
		}
	}
	return pTemplate;
}

LRESULT OWindow::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT lResult = ::CallWindowProc(m_pfnSuper, m_hWnd, message, wParam, lParam);
	switch (message)
	{
	case WM_NCDESTROY:
		m_hWnd = NULL;
		m_pfnSuper = NULL;
		if (m_bAutoDelete)
			delete this;
		break;
	}
	return lResult;
}

OWindow::~OWindow()
{
	if (m_pfnSuper)
		DestroyWindow();
}

unsigned int linesPerScroll = 0;

void OWindow::OnSettingChange()
{
	WNDCLASS wc;
	m_button = static_cast<ATOM>(GetClassInfo(NULL, WC_BUTTON, &wc));
	m_highcontrast.cbSize = sizeof m_highcontrast;
	SystemParametersInfo(SPI_GETHIGHCONTRAST, sizeof m_highcontrast, &m_highcontrast, FALSE);
	::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerScroll, 0);
}

unsigned int H2O::GetLinesPerScroll() {
	if (linesPerScroll == 0) {
		::SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &linesPerScroll, 0);
	}
	return linesPerScroll;
}

struct DrawItemStruct_WebLinkButton : DRAWITEMSTRUCT
{
	void DrawItem() const
	{
		// Neglect pressedness - no flicker-prone drawing while having capture
		if (hwndItem == ::GetCapture())
			return;
		TCHAR cText[INTERNET_MAX_PATH_LENGTH];
		int cchText = ::GetWindowText(hwndItem, cText, _countof(cText));
		COLORREF clrText = ::GetSysColor(COLOR_HOTLIGHT);
		if (::GetWindowLong(hwndItem, GWL_STYLE) & BS_LEFTTEXT)
		{
			clrText = ::GetSysColor(COLOR_GRAYTEXT);
			if (GetRValue(clrText) > 100 && GetBValue(clrText) > 100)
				clrText &= RGB(255,0,255);
		}
		RECT rcText = rcItem;
		::DrawText(hDC, cText, cchText, &rcText, DT_LEFT | DT_CALCRECT);
		::SetWindowPos(hwndItem, NULL, 0, 0, rcText.right, rcItem.bottom,
			SWP_NOMOVE | SWP_NOZORDER | SWP_NOREDRAW);
		switch (itemAction)
		{
		case ODA_DRAWENTIRE:
			::ExtTextOut(hDC, 0, 0, ETO_OPAQUE, &rcItem, 0, 0, 0);
			::SetBkMode(hDC, TRANSPARENT);
			::SetTextColor(hDC, clrText);
			if (!(itemState & ODS_NOACCEL))
			{
				if (LPWSTR p = StrChrW(cText, L'&'))
				{
					// Text is underlined already, so prefer a combining caron
					p[0] = p[1];
					p[1] = L'\x30C';
				}
			}
			::DrawText(hDC, cText, cchText, &rcText, DT_LEFT);
			rcText.top = rcText.bottom - 1;
			::SetBkColor(hDC, clrText);
			::ExtTextOut(hDC, 0, 0, ETO_OPAQUE, &rcText, 0, 0, 0);
			if (itemState & ODS_FOCUS)
			{
			case ODA_FOCUS:
				if (!(itemState & ODS_NOFOCUSRECT))
				{
					::SetTextColor(hDC, RGB(0,0,0));
					::SetBkColor(hDC, RGB(255,255,255));
					::SetBkMode(hDC, OPAQUE);
					rcText.top = rcText.bottom - 1;
					++rcText.bottom;
					::DrawFocusRect(hDC, &rcText);
				}
			}
			break;
		}
	}
};

template<>
LRESULT OWindow::MessageReflect_WebLinkButton<WM_DRAWITEM>(WPARAM, LPARAM lParam)
{
	reinterpret_cast<DrawItemStruct_WebLinkButton *>(lParam)->DrawItem();
	return TRUE;
}

template<>
LRESULT OWindow::MessageReflect_WebLinkButton<WM_SETCURSOR>(WPARAM, LPARAM lParam)
{
	HCURSOR hCursor = ::LoadCursor(NULL, MAKEINTRESOURCE(IDC_HAND));
	::SetCursor(hCursor);
	return TRUE;
}

template<>
LRESULT OWindow::MessageReflect_WebLinkButton<WM_COMMAND>(WPARAM, LPARAM lParam)
{
	HButton *button = reinterpret_cast<HButton *>(lParam);
	if (button->SetStyle(button->GetStyle() | BS_LEFTTEXT))
		button->Invalidate();
	return 0;
}

template<>
LRESULT OWindow::MessageReflect_ColorButton<WM_DRAWITEM>(WPARAM, LPARAM lParam)
{
	DRAWITEMSTRUCT *pdis = reinterpret_cast<DRAWITEMSTRUCT *>(lParam);
	DrawEdge(pdis->hDC, &pdis->rcItem, EDGE_SUNKEN,
		pdis->itemState & ODS_FOCUS ? BF_RECT|BF_ADJUST|BF_MONO : BF_RECT|BF_ADJUST);
	COLORREF cr = m_pWnd->GetDlgItemInt(pdis->CtlID);
	if (cr != CLR_NONE)
	{
		COLORREF crTmp = SetBkColor(pdis->hDC, cr);
		ExtTextOut(pdis->hDC, 0, 0, ETO_OPAQUE, &pdis->rcItem, 0, 0, 0);
		SetBkColor(pdis->hDC, crTmp);
	}
	else if (HBrush *pBrush = HBrush::CreateHatchBrush(HS_DIAGCROSS, 0))
	{
		FillRect(pdis->hDC, &pdis->rcItem, pBrush->m_hBrush);
		pBrush->DeleteObject();
	}
	return TRUE;
}

template<>
LRESULT OWindow::MessageReflect_Static<WM_CTLCOLORSTATIC>(WPARAM wParam, LPARAM lParam)
{
	HWindow *const control = reinterpret_cast<HWindow *>(lParam);
	HSurface *const surface = reinterpret_cast<HSurface *>(wParam);
	ATOM const ctlclass = control->GetClassAtom();
	if (ctlclass == m_button)
	{
		DWORD const ctlstyle = control->GetStyle();
		if (m_highcontrast.dwFlags & HCF_HIGHCONTRASTON)
		{
			// high-contrast themes play nice with disabled groupboxes, so no reason to mess with them
		}
		else if ((ctlstyle & (WS_DISABLED | BS_TYPEMASK)) == (WS_DISABLED | BS_GROUPBOX))
		{
			RECT rc;
			control->GetClientRect(&rc);
			rc.left += 9; // found through trial & error - doesn't seem to vary
			surface->SetTextColor(GetSysColor(COLOR_GRAYTEXT));
			TCHAR text[1024];
			int const len = control->GetWindowText(text, _countof(text));
			surface->SelectObject(control->GetFont());
			surface->DrawText(text, len, &rc, DT_SINGLELINE | DT_CALCRECT);
			surface->DrawText(text, len, &rc, DT_SINGLELINE);
			surface->ExcludeClipRect(rc.left, rc.top, rc.right, rc.bottom);
		}
	}
	return 0;
}

template<>
LRESULT OWindow::MessageReflect_TopLevelWindow<WM_ACTIVATE>(WPARAM wParam, LPARAM lParam)
{
	if (HWindow *pwndOther = reinterpret_cast<HWindow *>(lParam))
	{
		if (LOWORD(wParam) == WA_INACTIVE && pwndOther->GetParent()->m_hWnd == m_hWnd && !pwndOther->IsWindowVisible())
		{
			CenterWindow(pwndOther);
			HICON icon = pwndOther->GetIcon(ICON_BIG);
			if (icon == NULL)
			{
				icon = GetIcon(ICON_BIG);
				if (icon != NULL)
				{
					pwndOther->SetIcon(icon, ICON_BIG);
				}
			}
		}
	}
	return 0;
}

void OWindow::SwapPanes(UINT id_0, UINT id_1)
{
	struct
	{
		HWindow *pWindow;
		HWindow *pPrevWindow;
		LONG style;
		WINDOWPLACEMENT wp;
	} rg[2];
	rg[0].pWindow = m_pWnd->GetDlgItem(id_0);
	rg[0].pPrevWindow = rg[0].pWindow->GetWindow(GW_HWNDPREV);
	rg[0].style = rg[0].pWindow->GetStyle();
	rg[0].wp.length = sizeof(WINDOWPLACEMENT);
	rg[0].pWindow->GetWindowPlacement(&rg[0].wp);
	rg[1].pWindow = m_pWnd->GetDlgItem(id_1);
	rg[1].pPrevWindow = rg[1].pWindow->GetWindow(GW_HWNDPREV);
	rg[1].style = rg[1].pWindow->GetStyle();
	rg[1].wp.length = sizeof(WINDOWPLACEMENT);
	rg[1].pWindow->GetWindowPlacement(&rg[1].wp);
	rg[0].pWindow->SetDlgCtrlID(id_1);
	rg[1].pWindow->SetDlgCtrlID(id_0);
	rg[0].pWindow->SetStyle(rg[1].style);
	rg[1].pWindow->SetStyle(rg[0].style);
	rg[0].pWindow->SetWindowPlacement(&rg[1].wp);
	rg[1].pWindow->SetWindowPlacement(&rg[0].wp);
	const UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOCOPYBITS;
	if (rg[0].pWindow != rg[1].pPrevWindow)
		rg[0].pWindow->SetWindowPos(rg[1].pPrevWindow, 0, 0, 0, 0, flags);
	if (rg[1].pWindow != rg[0].pPrevWindow)
		rg[1].pWindow->SetWindowPos(rg[0].pPrevWindow, 0, 0, 0, 0, flags);
}

BOOL ODialog::OnInitDialog()
{
	return TRUE;
}

INT_PTR ODialog::DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_INITDIALOG)
	{
		lParam = reinterpret_cast<PROPSHEETPAGE *>(lParam)->lParam;
		::SetWindowLongPtr(hWnd, DWLP_USER, lParam);
		::SetWindowLongPtr(hWnd, DWLP_DLGPROC, NULL);
		::SetWindowLongPtr(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc));
		ODialog *pThis = reinterpret_cast<ODialog *>(lParam);
		const_cast<HWND &>(pThis->m_hWnd) = hWnd;
		return pThis->OnInitDialog();
	}
	return FALSE;
}

LRESULT ODialog::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	ODialog *const pThis = FromHandle(reinterpret_cast<HWindow *>(hWnd));
	LRESULT lResult = 0;
	try
	{
		lResult = pThis->WindowProc(uMsg, wParam, lParam);
		switch (uMsg)
		{
		case WM_ACTIVATE:
			pThis->MessageReflect_TopLevelWindow<WM_ACTIVATE>(wParam, lParam);
			break;
		case WM_CTLCOLORSTATIC:
			pThis->MessageReflect_Static<WM_CTLCOLORSTATIC>(wParam, lParam);
			break;
		}
	}
	catch (OException *e)
	{
		e->ReportError(hWnd);
		delete e;
	}
	return lResult;
}

LRESULT ODialog::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return ::DefDlgProc(m_hWnd, uMsg, wParam, lParam);
}

INT_PTR ODialog::DoModal(HINSTANCE hinst, HWND parent)
{
	PROPSHEETPAGE psp;
	psp.lParam = reinterpret_cast<LPARAM>(this);
	INT_PTR result;
	if (LPDLGTEMPLATE pTemplate = ScaleDialogTemplate(hinst, m_idd))
	{
		result = DialogBoxIndirectParam(hinst, pTemplate, parent, DlgProc, reinterpret_cast<LPARAM>(&psp));
		GlobalFree(pTemplate);
	}
	else
	{
		result = DialogBoxParam(hinst, m_idd, parent, DlgProc, reinterpret_cast<LPARAM>(&psp));
	}
	return result;
}

HWND ODialog::Create(HINSTANCE hinst, HWND parent)
{
	PROPSHEETPAGE psp;
	psp.lParam = reinterpret_cast<LPARAM>(this);
	HWND hwnd;
	if (LPDLGTEMPLATE pTemplate = ScaleDialogTemplate(hinst, m_idd))
	{
		hwnd = CreateDialogIndirectParam(hinst, pTemplate, parent, DlgProc, reinterpret_cast<LPARAM>(&psp));
		GlobalFree(pTemplate);
	}
	else
	{
		hwnd = CreateDialogParam(hinst, m_idd, parent, DlgProc, reinterpret_cast<LPARAM>(&psp));
	}
	return hwnd;
}

BOOL ODialog::IsUserInputCommand(WPARAM wParam)
{
	UINT id = LOWORD(wParam);
	UINT code = HIWORD(wParam);
	UINT dlgcode = (UINT)SendDlgItemMessage(id, WM_GETDLGCODE);
	if (dlgcode & DLGC_HASSETSEL)
		return code == EN_CHANGE && SendDlgItemMessage(id, EM_GETMODIFY) != 0;
	if (dlgcode & DLGC_BUTTON)
		return code == BN_CLICKED;
	if (dlgcode & DLGC_WANTARROWS)
	{
		switch (code)
		{
		case CBN_SELCHANGE:
			// force the selected item's text into the edit control
			SendDlgItemMessage(id, CB_SETCURSEL, SendDlgItemMessage(id, CB_GETCURSEL));
			// fall through
		case CBN_EDITCHANGE:
			return TRUE;
		}
	}
	return FALSE;
}

void ODialog::Update3StateCheckBoxLabel(UINT id)
{
	String text;
	HButton *const pButton = static_cast<HButton *>(GetDlgItem(id));
	// Remember the initial text of the label in an invisible child window.
	if (pButton->GetDlgItemText(1, text) == 0)
	{
		pButton->GetWindowText(text);
		HWindow::CreateEx(0, WC_STATIC, text.c_str(), WS_CHILD, 0, 0, 0, 0, pButton, 1);
	}
	// If there is a \t to split the text in twice, the first part shows up for
	// BST_(UN)CHECKED, and the second part shows up for BST_INDETERMINATE.
	String::size_type i = text.find(_T('\t'));
	LPCTSTR buf = text.c_str();
	if (i != String::npos)
	{
		if (pButton->GetCheck() == BST_INDETERMINATE)
			buf += i + 1;
		else
			text.resize(i);
		pButton->SetWindowText(buf);
	}
}

BOOL OResizableDialog::OnInitDialog()
{
	ODialog::OnInitDialog();
	CFloatState::Clear();
	TCHAR entry[8];
	GetAtomName(reinterpret_cast<ATOM>(m_idd), entry, _countof(entry));
	if (CRegKeyEx rk = SettingStore.GetSectionKey(_T("ScreenLayout")))
	{
		TCHAR value[1024];
		if (LPCTSTR pch = rk.ReadString(entry, NULL, CRegKeyEx::StringRef(value, _countof(value))))
		{
			int const cx = _tcstol(pch, const_cast<LPTSTR *>(&pch), 10);
			int const cy = _tcstol(&pch[*pch == 'x'], const_cast<LPTSTR *>(&pch), 10);
			SetWindowPos(NULL, 0, 0, cx, cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
			CSplitState::Scan(m_hWnd, pch);
		}
	}
	return TRUE;
}

LRESULT OResizableDialog::WindowProc(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	TCHAR entry[8];
	RECT rect;
	switch (uMsg)
	{
	case WM_DESTROY:
		GetAtomName(reinterpret_cast<ATOM>(m_idd), entry, _countof(entry));
		if (CRegKeyEx rk = SettingStore.GetSectionKey(_T("ScreenLayout")))
		{
			GetWindowRect(&rect);
			int const cx = rect.right - rect.left;
			int const cy = rect.bottom - rect.top;
			TCHAR value[1024];
			int cch = wsprintf(value, _T("%dx%d"), cx, cy);
			CSplitState::Dump(m_hWnd, value + cch);
			rk.WriteString(entry, value);
		}
		break;
	}
	return CFloatState::CallWindowProc(::DefDlgProc, m_hWnd, uMsg, wParam, lParam);
}

OPropertySheet::OPropertySheet()
{
	ZeroMemory(&m_psh, sizeof m_psh);
	m_psh.dwSize = sizeof m_psh;
	m_psh.dwFlags = PSH_PROPSHEETPAGE | PSH_USECALLBACK;
	m_psh.pfnCallback = PropSheetProc;
}

PROPSHEETPAGE *OPropertySheet::AddPage(ODialog &page)
{
	std::vector<PROPSHEETPAGE>::size_type index = m_pages.size();
	m_pages.resize(index + 1);
	PROPSHEETPAGE *psp = &m_pages.back();
	ZeroMemory(psp, sizeof *psp);
	psp->dwSize = sizeof *psp;
	psp->pszTemplate = page.m_idd;
	psp->pfnDlgProc = ODialog::DlgProc;
	psp->lParam = reinterpret_cast<LPARAM>(&page);
	return psp;
}

INT_PTR OPropertySheet::DoModal(HINSTANCE hinst, HWND parent)
{
	m_psh.ppsp = &m_pages.front();
	m_psh.nPages = m_pages.size();
	m_psh.hInstance = hinst;
	m_psh.hwndParent = parent;
	m_psh.pszCaption = m_caption.c_str();
	INT_PTR result;
	UINT i = 0;
	while (i < m_psh.nPages)
	{
		PROPSHEETPAGE *psp = const_cast<PROPSHEETPAGE *>(&m_psh.ppsp[i++]);
		if (LPDLGTEMPLATE pTemplate = ScaleDialogTemplate(m_psh.hInstance, psp->pszTemplate))
		{
			psp->dwFlags |= PSP_DLGINDIRECT;
			psp->pResource = pTemplate;
		}
	}
	result = PropertySheet(&m_psh);
	while (i > 0)
	{
		PROPSHEETPAGE *psp = const_cast<PROPSHEETPAGE *>(&m_psh.ppsp[--i]);
		if (psp->dwFlags & PSP_DLGINDIRECT)
		{
			GlobalFree(const_cast<LPDLGTEMPLATE>(psp->pResource));
			psp->pResource = NULL;
		}
	}
	return result;
}

int CALLBACK OPropertySheet::PropSheetProc(HWND hWnd, UINT uMsg, LPARAM lParam)
{
	switch (uMsg)
	{
	case PSCB_PRECREATE:
		ScaleDialogTemplate(reinterpret_cast<LPVOID>(lParam), GetScalingDPI());
		break;
	case PSCB_INITIALIZED:
		SetWindowLongPtr(hWnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProc));
		break;
	}
	return 0;
}

LRESULT OPropertySheet::WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_ACTIVATE:
		OWindow(hWnd).MessageReflect_TopLevelWindow<WM_ACTIVATE>(wParam, lParam);
		break;
	case WM_COMMAND:
		switch (wParam)
		{
		case MAKEWPARAM(IDOK, BN_CLICKED):
		case MAKEWPARAM(IDCANCEL, BN_CLICKED):
			// If the current property page has captured the mouse, don't close
			// the property sheet but rather just release the mouse capture.
			if (GetCapture() == PropSheet_GetCurrentPageHwnd(hWnd))
			{
				ReleaseCapture();
				return 0;
			}
			break;
		}
		break;
	}
	return ::DefDlgProc(hWnd, uMsg, wParam, lParam);
}

void H2O::ThrowJsonException(const char *msg)
{
	wchar_t buf[1024];
	buf[mbstowcs(buf, msg, 1023)] = L'\0';
	throw reinterpret_cast<OException*>(buf);
}

HWND H2O::GetTopLevelParent(HWND hWnd)
{
	while (HWND hWndParent = ::GetParent(hWnd))
		hWnd = hWndParent;
	return hWnd;
}

HIMAGELIST H2O::Create3StateImageList()
{
	HIMAGELIST himlState = ImageList_Create(16, 16, ILC_COLOR | ILC_MASK, 3, 0);

	RECT rc = { 0, 0, 48, 16 };

	HDC hdcScreen = GetDC(0);

	HDC hdc = CreateCompatibleDC(hdcScreen);
	HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, 48, 16);
	HGDIOBJ hbmOld = SelectObject(hdc, hbm);
	SetBkColor(hdc, RGB(255,255,255));
	ExtTextOut(hdc, 0, 0, ETO_OPAQUE, &rc, NULL, 0, NULL);

	SetRect(&rc, 1, 1, 14, 14);
	DrawFrameControl(hdc, &rc, DFC_BUTTON,
					 DFCS_FLAT|DFCS_BUTTONCHECK);

	OffsetRect(&rc, 16, 0);
	DrawFrameControl(hdc, &rc, DFC_BUTTON,
					 DFCS_FLAT|DFCS_BUTTONCHECK|DFCS_CHECKED);

	OffsetRect(&rc, 16, 0);
	DrawFrameControl(hdc, &rc, DFC_BUTTON,
					 DFCS_FLAT|DFCS_BUTTONCHECK);

	InflateRect(&rc, -4, -4);
	FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));

	SelectObject(hdc, hbmOld);
	ImageList_AddMasked(himlState, hbm, RGB(255,255,255));

	DeleteObject(hbm);
	DeleteDC(hdc);
	ReleaseDC(0, hdcScreen);

	return himlState;
}

void H2O::GetDesktopWorkArea(HWND hWnd, LPRECT prcDesktop)
{
	// Get screen dimensions excluding task bar
	::SystemParametersInfo(SPI_GETWORKAREA, 0, prcDesktop, 0);
#ifndef _WIN32_WCE
	// Import the GetMonitorInfo and MonitorFromWindow functions
	if (HMODULE hUser32 = GetModuleHandle(_T("USER32.DLL")))
	{
		typedef BOOL (WINAPI* LPGMI)(HMONITOR, LPMONITORINFO);
		typedef HMONITOR (WINAPI* LPMFW)(HWND, DWORD);
		LPMFW pfnMonitorFromWindow = (LPMFW)::GetProcAddress(hUser32, "MonitorFromWindow");
		LPGMI pfnGetMonitorInfo = (LPGMI)::GetProcAddress(hUser32, STRINGIZE(GetMonitorInfo));
		// Take multi-monitor systems into account
		if (pfnGetMonitorInfo && pfnMonitorFromWindow)
		{
			HMONITOR hActiveMonitor = pfnMonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi = { sizeof mi };

			if (pfnGetMonitorInfo(hActiveMonitor, &mi))
				*prcDesktop = mi.rcWork;
		}
	}
#endif
}

void H2O::CenterWindow(HWindow *pWnd, HWindow *pParent)
// Centers this window over its parent
{
	RECT rc;
	pWnd->GetWindowRect(&rc);
	RECT rcBounds;
	GetDesktopWorkArea(pWnd->m_hWnd, &rcBounds);
	// Get the parent window dimensions (parent could be the desktop)
	RECT rcParent = rcBounds;
	if ((pParent != NULL) || (pParent = pWnd->GetParent()) != NULL)
		pParent->GetWindowRect(&rcParent);
	// Calculate point to center the dialog over the portion of parent window on this monitor
	::IntersectRect(&rcParent, &rcParent, &rcBounds);
	rc.right -= rc.left;
	rc.bottom -= rc.top;
	rc.left = rcParent.left + (rcParent.right - rcParent.left - rc.right) / 2;
	rc.top = rcParent.top + (rcParent.bottom - rcParent.top - rc.bottom) / 2;
	rcBounds.right -= rc.right;
	rcBounds.bottom -= rc.bottom;
	// Keep the dialog within the work area
	if (rc.left < rcBounds.left)
		rc.left = rcBounds.left;
	if (rc.left > rcBounds.right)
		rc.left = rcBounds.right;
	if (rc.top < rcBounds.top)
		rc.top = rcBounds.top;
	if (rc.top > rcBounds.bottom)
		rc.top = rcBounds.bottom;
	pWnd->SetWindowPos(NULL, rc.left, rc.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static std::vector<String> rgDispinfoText(2); // used in function below

/**
 * @brief Allocate a text buffer to assign to NMLVDISPINFO::item::pszText
 * Quoting from SDK Docs:
 *	If the LVITEM structure is receiving item text, the pszText and cchTextMax
 *	members specify the address and size of a buffer. You can either copy text to
 *	the buffer or assign the address of a string to the pszText member. In the
 *	latter case, you must not change or delete the string until the corresponding
 *	item text is deleted or two additional LVN_GETDISPINFO messages have been sent.
 */
LPTSTR H2O::AllocDispinfoText(String &s)
{
	static int i = 0;
	rgDispinfoText[i].swap(s);
	LPCTSTR pszText = rgDispinfoText[i].c_str();
	i ^= 1;
	return const_cast<LPTSTR>(pszText);
}

int OException::ReportError(HWND hwnd, UINT type) const
{
	return msg ? ::MessageBox(hwnd, msg, NULL, type) : 0;
}

OException::OException(LPCTSTR str)
{
	lstrcpyn(msg, str, _countof(msg));
}

OException::OException(DWORD err, LPCTSTR fmt)
{
	static DWORD const WinInetFlags =
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE;
	static DWORD const DefaultFlags =
		FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
	switch (HIWORD(err))
	{
	case 0x800A: // VBScript or JScript error
		err = DISP_E_EXCEPTION;
		// fall through
	default:
		HMODULE const WinInet = ::GetModuleHandle(_T("WININET"));
		if (DWORD const len = ::FormatMessage(
			WinInet ? WinInetFlags : DefaultFlags, WinInet,
			err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			msg, _countof(msg), NULL))
		{
			if (err == DISP_E_EXCEPTION)
			{
				IErrorInfo *perrinfo;
				if (SUCCEEDED(GetErrorInfo(0, &perrinfo)) && perrinfo)
				{
					BSTR bstr;
					if (SUCCEEDED(perrinfo->GetSource(&bstr)) && bstr)
					{
						wnsprintf(msg + len, _countof(msg) - len, _T("\n%ls - "), bstr);
						SysFreeString(bstr);
					}
					if (SUCCEEDED(perrinfo->GetDescription(&bstr)) && bstr)
					{
						wnsprintf(msg + len, _countof(msg) - len, _T("%ls"), bstr);
						SysFreeString(bstr);
					}
					perrinfo->Release();
				}
			}
		}
		else
		{
			wnsprintf(msg, _countof(msg), fmt ? fmt : _T("Error 0x%08lX = %ld"), err, err);
		}
	}
}

void OException::Throw(LPCTSTR str)
{
	OException e(str);
	throw &e;
}

void OException::Throw(DWORD err, LPCTSTR fmt)
{
	OException e(err, fmt);
	throw &e;
}

void OException::ThrowSilent()
{
	throw static_cast<OException *>(0);
}

void OException::Check(HRESULT hr)
{
	if (FAILED(hr))
		Throw(hr);
}
