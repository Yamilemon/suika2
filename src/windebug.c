/* -*- coding: utf-8; indent-tabs-mode: t; tab-width: 4; c-basic-offset: 4; -*- */

/*
 * Suika2
 * Copyright (C) 2001-2023, Keiichi Tabata. All rights reserved.
 */

/*
 * [Changes]
 *  2014-05-24 作成 (conskit)
 *  2016-05-29 作成 (suika)
 *  2017-11-07 フルスクリーンで解像度変更するように修正
 *  2023-09-20 Android/iOSエクスポート対応
 *  2023-10-25 エディタ対応
 */

/* Suika2 Base */
#include "suika.h"

/* Suika2 Pro */
#include "windebug.h"
#include "package.h"

/* Windows */
#include <windows.h>
#include <commctrl.h>	/* TOOLINFO */
#include <richedit.h>
#include "resource.h"

/* Standard C (msvcrt.dll) */
#include <stdlib.h>	/* exit() */

/* MSVC */
#define wcsdup(s)	_wcsdup(s)

/*
 * Constants
 */

/* 変数テキストボックスのテキストの最大長(形: "$00001=12345678901\r\n") */
#define VAR_TEXTBOX_MAX		(11000 * (1 + 5 + 1 + 11 + 2))

/* ウィンドウクラス名 */
static const wchar_t wszWindowClass[] = L"SuikaDebugPanel";

/*
 * Variables
 */

/* ウィンドウのハンドル (winmain.cで作成) */
static HWND hWndMain;		/* メインウィンドウ */
static HWND hWndGame;		/* ゲーム部分の描画領域のパネル */

/* デバッガのコントロール */
static HWND hWndDebug;				/* デバッガのパネル */
static HWND hWndBtnResume;			/* 「続ける」ボタン */
static HWND hWndBtnNext;				/* 「次へ」ボタン */
static HWND hWndBtnPause;			/* 「停止」ボタン */
static HWND hWndTextboxScript;		/* ファイル名のテキストボックス */
static HWND hWndBtnSelectScript;		/* ファイル選択のボタン */
static HWND hWndRichEdit;			/* スクリプトのリッチエディット */
static HWND hWndTextboxVar;			/* 変数一覧のテキストボックス */
static HWND hWndBtnVar;				/* 変数を反映するボタン */

/* メニュー */
static HMENU hMenu;

/* 英語モードか */
static BOOL bEnglish;

/* 実行中であるか */
static BOOL bRunning;

/* 発生したイベントの状態 */	
static BOOL bContinuePressed;		/* 「続ける」ボタンが押下された */
static BOOL bNextPressed;			/* 「次へ」ボタンが押下された */
static BOOL bStopPressed;			/* 「停止」ボタンが押下された */
static BOOL bScriptOpened;			/* スクリプトファイルが選択された */
static BOOL bExecLineChanged;		/* 実行行が変更された */
static int nLineChanged;			/* 実行行が変更された場合の行番号 */
static BOOL bRangedChanged;			/* 複数行の変更が加えられるか */

/* GetDpiForWindow() APIへのポインタ */
UINT (__stdcall *pGetDpiForWindow)(HWND);

/*
 * Forward Declaration
 */

/* initialization */
static VOID InitMenu(HWND hWnd);
static HWND CreateTooltip(HWND hWndBtn, const wchar_t *pszTextEnglish, const wchar_t *pszTextJapanese);

/* command handlers */
static VOID OnOpenScript(void);
static VOID OnSave(void);
static VOID OnNextError(void);
static VOID OnWriteVars(void);
static VOID OnExportPackage(void);
static VOID OnExportWin(void);
static VOID OnExportWinInst(void);
static VOID OnExportWinMac(void);
static VOID OnExportWeb(void);
static VOID OnExportAndroid(void);
static VOID OnExportIOS(void);
static VOID RecreateDirectory(const wchar_t *path);
static BOOL CopySourceFiles(const wchar_t *lpszSrcDir, const wchar_t *lpszDestDir);
static BOOL CopyMovFiles(const wchar_t *lpszSrcDir, const wchar_t *lpszDestDir);
static BOOL MovePackageFile(const wchar_t *lpszPkgFile, wchar_t *lpszDestDir);

/* Variable TextEdit */
static VOID Variable_UpdateText(void);

/* RichEdit handlers */
static VOID RichEdit_OnChange(void);
static VOID RichEdit_OnReturn(void);

/* RichEdit helpers */
static int RichEdit_GetCursorPosition(void);
static VOID RichEdit_SetCursorPosition(int nCursor);
static VOID RichEdit_GetSelectedRange(int *nStart, int *nEnd);
static VOID RichEdit_SetSelectedRange(int nLineStart, int nLineLen);
static int RichEdit_GetCursorLine(void);
static wchar_t *RichEdit_GetText(void);
static VOID RichEdit_SetTextFormatAll(void);
static VOID RichEdit_SetFontAll(void);
static VOID RichEdit_ClearBackgroundColorAll(void);
static VOID RichEdit_SetTextColorForAllLines(void);
static VOID RichEdit_SetTextColorForLine(const wchar_t *pText, int nLineStartCR, int nLineStartCRLF, int nLineLen);
static VOID RichEdit_SetTextColorForCursorLine(void);
static VOID RichEdit_SetBackgroundColorForExecuteLine(void);
static VOID RichEdit_GetLineStartAndLength(int nLine, int *nLineStart, int *nLineLen);
static VOID RichEdit_SetTextColorForSelectedRange(COLORREF cl);
static VOID RichEdit_SetBackgroundColorForSelectedRange(COLORREF cl);
static VOID RichEdit_AutoScroll(void);
static BOOL RichEdit_IsLineTop(void);
static BOOL RichEdit_IsLineEnd(void);
static VOID RichEdit_GetLineStartAndLength(int nLine, int *nLineStart, int *nLineLen);

/* Script Model */
static VOID RichEdit_UpdateTextFromScriptModel(void);
static VOID RichEdit_UpdateScriptModelFromText(void);
static VOID ValidateScriptModel(void);

/*
 * コマンドライン引数の処理
 */

/*
 * 引数が指定された場合はパッケージャとして機能する
 */
VOID DoPackagingIfArgExists(VOID)
{
	if (__argc == 2 && wcscmp(__wargv[1], L"--package") == 0)
	{
		if (!create_package(""))
		{
			log_error("Packaging error!");
			exit(1);
		}
		exit(0);
	}
}

/*
 * スタートアップファイル/ラインを取得する
 */
BOOL GetStartupPosition(void)
{
	int line;
	if (__argc >= 3) {
		/* 先に行番号を取得する(conv_*()がstatic変数が指すため) */
		line = atoi(conv_utf16_to_utf8(__wargv[2]));

		/* スタートアップファイル/ラインを指定する */
		if (!set_startup_file_and_line(conv_utf16_to_utf8(__wargv[1]), line))
			return FALSE;

		/* 実行開始ボタンが押されたことにする */
		bContinuePressed = TRUE;
	}

	return TRUE;
}

/*
 * 初期化
 */

/*
 * DPI拡張の初期化を行う
 */
VOID InitDpiExtension(void)
{
	HMODULE hModule;

	hModule = LoadLibrary(L"user32.dll");
	if (hModule == NULL)
		return;

	pGetDpiForWindow = (void *)GetProcAddress(hModule, "GetDpiForWindow");
}

/*
 * DPIを取得する
 */
int Win11_GetDpiForWindow(HWND hWnd)
{
	int nDpi;

	if (pGetDpiForWindow == NULL)
		return 96;

	nDpi = (int)pGetDpiForWindow(hWnd);
	if (nDpi == 0)
		return 96;

	return nDpi;
}

/*
 * デバッガパネルを作成する
 */
BOOL InitDebuggerPanel(HWND hMainWnd, HWND hGameWnd, void *pWndProc)
{
	WNDCLASSEX wcex;
	RECT rcClient;
	HFONT hFont;
	int nDpi;

	hWndMain = hMainWnd;
	hWndGame= hGameWnd;

	/* 領域の矩形を取得する */
	GetClientRect(hWndMain, &rcClient);

	/* ウィンドウクラスを登録する */
	ZeroMemory(&wcex, sizeof(wcex));
	wcex.cbSize			= sizeof(WNDCLASSEX);
	wcex.lpfnWndProc    = pWndProc;
	wcex.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
	wcex.lpszClassName  = wszWindowClass;
	if (!RegisterClassEx(&wcex))
		return FALSE;

	/* ウィンドウを作成する */
	hWndDebug = CreateWindowEx(0,
							   wszWindowClass,
							   NULL,
							   WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
							   rcClient.right - DEBUGGER_PANEL_WIDTH,
							   0,
							   DEBUGGER_PANEL_WIDTH,
							   rcClient.bottom,
							   hWndMain,
							   NULL,
							   GetModuleHandle(NULL),
							   NULL);
	if(!hWndDebug)
		return FALSE;

	/* DPIを取得する */
	nDpi = Win11_GetDpiForWindow(hWndMain);

	/* フォントを作成する */
	hFont = CreateFont(MulDiv(18, nDpi, 96),
					   0, 0, 0, FW_DONTCARE, FALSE, FALSE, FALSE,
					   ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
					   DEFAULT_QUALITY,
					   DEFAULT_PITCH | FF_DONTCARE, L"Yu Gothic UI");

	/* 続けるボタンを作成する */
	hWndBtnResume = CreateWindow(
		L"BUTTON",
		bEnglish ? L"Resume" : L"続ける",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		MulDiv(10, nDpi, 96),
		MulDiv(10, nDpi, 96),
		MulDiv(100, nDpi, 96),
		MulDiv(40, nDpi, 96),
		hWndDebug,
		(HMENU)ID_RESUME,
		(HINSTANCE)GetWindowLongPtr(hWndDebug, GWLP_HINSTANCE),
		NULL);
	SendMessage(hWndBtnResume, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);
	CreateTooltip(hWndBtnResume,
				  L"Start executing script and run continuosly.",
				  L"スクリプトの実行を開始し、継続して実行します。");

	/* 次へボタンを作成する */
	hWndBtnNext = CreateWindow(
		L"BUTTON",
		bEnglish ? L"Next" : L"次へ",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		MulDiv(120, nDpi, 96),
		MulDiv(10, nDpi, 96),
		MulDiv(100, nDpi, 96),
		MulDiv(40, nDpi, 96),
		hWndDebug,
		(HMENU)ID_NEXT,
		(HINSTANCE)GetWindowLongPtr(hWndDebug, GWLP_HINSTANCE),
		NULL);
	SendMessage(hWndBtnNext, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);
	CreateTooltip(hWndBtnNext,
				  L"Run only one command and stop after it.",
				  L"コマンドを1個だけ実行し、停止します。");

	/* 停止ボタンを作成する */
	hWndBtnPause = CreateWindow(
		L"BUTTON",
		bEnglish ? L"Paused" : L"停止",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		MulDiv(330, nDpi, 96),
		MulDiv(10, nDpi, 96),
		MulDiv(100, nDpi, 96),
		MulDiv(40, nDpi, 96),
		hWndDebug,
		(HMENU)ID_PAUSE,
		(HINSTANCE)GetWindowLongPtr(hWndDebug, GWLP_HINSTANCE),
		NULL);
	EnableWindow(hWndBtnPause, FALSE);
	SendMessage(hWndBtnPause, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);
	CreateTooltip(hWndBtnPause,
				  L"Stop script execution.",
				  L"コマンドの実行を停止します。");

	/* スクリプト名のテキストボックスを作成する */
	hWndTextboxScript = CreateWindow(
		L"EDIT",
		NULL,
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER | ES_READONLY| ES_AUTOHSCROLL,
		MulDiv(10, nDpi, 96),
		MulDiv(60, nDpi, 96),
		MulDiv(350, nDpi, 96),
		MulDiv(30, nDpi, 96),
		hWndDebug,
		0,
		(HINSTANCE)GetWindowLongPtr(hWndDebug, GWLP_HINSTANCE),
		NULL);
	SendMessage(hWndTextboxScript, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);
	CreateTooltip(hWndTextboxScript,
				  L"Write script file name to be jumped to.",
				  L"ジャンプしたいスクリプトファイル名を書きます。");

	/* スクリプトの選択ボタンを作成する */
	hWndBtnSelectScript = CreateWindow(
		L"BUTTON", L"...",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		MulDiv(370, nDpi, 96),
		MulDiv(60, nDpi, 96),
		MulDiv(60, nDpi, 96),
		MulDiv(30, nDpi, 96),
		hWndDebug,
		(HMENU)ID_OPEN,
		(HINSTANCE)GetWindowLongPtr(hWndDebug, GWLP_HINSTANCE),
		NULL);
	SendMessage(hWndBtnSelectScript, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);
	CreateTooltip(hWndBtnSelectScript,
				  L"Select a script file and jump to it.",
				  L"スクリプトファイルを選択してジャンプします。");

	/* スクリプトのリッチエディットを作成する */
	LoadLibrary(L"Msftedit.dll");
	hWndRichEdit = CreateWindowEx(
		0,
		MSFTEDIT_CLASS,
		L"Text",
		ES_MULTILINE | WS_VISIBLE | WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOVSCROLL,
		MulDiv(10, nDpi, 96),
		MulDiv(100, nDpi, 96),
		MulDiv(420, nDpi, 96),
		MulDiv(400, nDpi, 96),
		hWndDebug, 0,
		(HINSTANCE)GetWindowLongPtr(hWndDebug, GWLP_HINSTANCE),
		NULL);
	SendMessage(hWndRichEdit, EM_SHOWSCROLLBAR, (WPARAM)SB_VERT, (LPARAM)TRUE);
	SendMessage(hWndRichEdit, EM_SETEVENTMASK, 0, (LPARAM)ENM_CHANGE);

	/* 変数のテキストボックスを作成する */
	hWndTextboxVar = CreateWindow(
		L"EDIT",
		NULL,
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | WS_BORDER | WS_VSCROLL |
		ES_AUTOVSCROLL | ES_MULTILINE | ES_WANTRETURN,
		MulDiv(10, nDpi, 96),
		MulDiv(570, nDpi, 96),
		MulDiv(280, nDpi, 96),
		MulDiv(60, nDpi, 96),
		hWndDebug,
		0,
		(HINSTANCE)GetWindowLongPtr(hWndDebug, GWLP_HINSTANCE),
		NULL);
	SendMessage(hWndTextboxVar, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);
	CreateTooltip(hWndTextboxVar,
				  L"List of variables which have non-initial values.",
				  L"初期値から変更された変数の一覧です。");

	/* 値を書き込むボタンを作成する */
	hWndBtnVar = CreateWindow(
		L"BUTTON",
		bEnglish ? L"Write values" : L"値を書き込む",
		WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
		MulDiv(300, nDpi, 96),
		MulDiv(570, nDpi, 96),
		MulDiv(130, nDpi, 96),
		MulDiv(30, nDpi, 96),
		hWndDebug,
		(HMENU)ID_VARS,
		(HINSTANCE)GetWindowLongPtr(hWndDebug, GWLP_HINSTANCE),
		NULL);
	SendMessage(hWndBtnVar, WM_SETFONT, (WPARAM)hFont, (LPARAM)TRUE);
	CreateTooltip(hWndBtnVar,
				  L"Write to the variables.",
				  L"変数の内容を書き込みます。");

	/* メニューを作成する */
	InitMenu(hWndMain);

	return TRUE;
}

/*
  デバッガウィンドウの位置を修正する
*/
VOID RearrangeDebuggerPanel(int nGameWidth, int nGameHeight)
{
	int y, nDpi, nDebugWidth;

	/* DPIを求める */
	nDpi = Win11_GetDpiForWindow(hWndMain);

	/* デバッグパネルのサイズを求める*/
	nDebugWidth = MulDiv(DEBUGGER_PANEL_WIDTH, nDpi, 96);

	/* エディタのコントロールをサイズ変更する */
	MoveWindow(hWndRichEdit,
			   MulDiv(10, nDpi, 96),
			   MulDiv(100, nDpi, 96),
			   MulDiv(420, nDpi, 96),
			   nGameHeight - MulDiv(180, nDpi, 96),
			   TRUE);

	/* エディタより下のコントロールのY座標を計算する */
	y = nGameHeight - MulDiv(130, nDpi, 96);

	/* 変数のテキストボックスを移動する */
	MoveWindow(hWndTextboxVar,
			   MulDiv(10, nDpi, 96),
			   y + MulDiv(60, nDpi, 96),
			   MulDiv(280, nDpi, 96),
			   MulDiv(60, nDpi, 96),
			   TRUE);

	/* 変数書き込みのボタンを移動する */
	MoveWindow(hWndBtnVar,
			   MulDiv(300, nDpi, 96),
			   y + MulDiv(70, nDpi, 96),
			   MulDiv(130, nDpi, 96),
			   MulDiv(30, nDpi, 96),
			   TRUE);

	/* デバッグパネルの位置を変更する */
	MoveWindow(hWndDebug,
			   nGameWidth,
			   0,
			   nDebugWidth,
			   nGameHeight,
			   TRUE);
}

/* メニューを作成する */
static VOID InitMenu(HWND hWnd)
{
	HMENU hMenuFile = CreatePopupMenu();
	HMENU hMenuScript = CreatePopupMenu();
	HMENU hMenuExport = CreatePopupMenu();
	HMENU hMenuHelp = CreatePopupMenu();
    MENUITEMINFO mi;

	bEnglish = conf_locale == LOCALE_JA ? FALSE : TRUE;

	/* メニューを作成する */
	hMenu = CreateMenu();

	/* 1階層目を作成する準備を行う */
	ZeroMemory(&mi, sizeof(MENUITEMINFO));
	mi.cbSize = sizeof(MENUITEMINFO);
	mi.fMask = MIIM_TYPE | MIIM_SUBMENU;
	mi.fType = MFT_STRING;
	mi.fState = MFS_ENABLED;

	/* ファイル(F)を作成する */
	mi.hSubMenu = hMenuFile;
	mi.dwTypeData = bEnglish ? L"File(&F)": L"ファイル(&F)";
	InsertMenuItem(hMenu, 0, TRUE, &mi);

	/* スクリプト(S)を作成する */
	mi.hSubMenu = hMenuScript;
	mi.dwTypeData = bEnglish ? L"Script(&S)": L"スクリプト(&S)";
	InsertMenuItem(hMenu, 1, TRUE, &mi);

	/* エクスポート(E)を作成する */
	mi.hSubMenu = hMenuExport;
	mi.dwTypeData = bEnglish ? L"Export(&E)": L"エクスポート(&E)";
	InsertMenuItem(hMenu, 2, TRUE, &mi);

	/* ヘルプ(H)を作成する */
	mi.hSubMenu = hMenuHelp;
	mi.dwTypeData = bEnglish ? L"Help(&H)": L"ヘルプ(&H)";
	InsertMenuItem(hMenu, 3, TRUE, &mi);

	/* 2階層目を作成する準備を行う */
	mi.fMask = MIIM_TYPE | MIIM_ID;

	/* スクリプトを開く(Q)を作成する */
	mi.wID = ID_OPEN;
	mi.dwTypeData = bEnglish ?
		L"Open script(&Q)\tCtrl+O" :
		L"スクリプトを開く(&O)\tCtrl+O";
	InsertMenuItem(hMenuFile, 0, TRUE, &mi);

	/* スクリプトを上書き保存する(S)を作成する */
	mi.wID = ID_SAVE;
	mi.dwTypeData = bEnglish ?
		L"Overwrite script(&S)\tCtrl+S" :
		L"スクリプトを上書き保存する(&S)\tCtrl+S";
	InsertMenuItem(hMenuFile, 1, TRUE, &mi);

	/* 終了(Q)を作成する */
	mi.wID = ID_QUIT;
	mi.dwTypeData = bEnglish ? L"Quit(&Q)\tCtrl+Q" : L"終了(&Q)\tCtrl+Q";
	InsertMenuItem(hMenuFile, 2, TRUE, &mi);

	/* 続ける(C)を作成する */
	mi.wID = ID_RESUME;
	mi.dwTypeData = bEnglish ? L"Resume(&R)\tCtrl+R" : L"続ける(&R)\tCtrl+R";
	InsertMenuItem(hMenuScript, 0, TRUE, &mi);

	/* 次へ(N)を作成する */
	mi.wID = ID_NEXT;
	mi.dwTypeData = bEnglish ? L"Next(&N)\tCtrl+N" : L"次へ(&N)\tCtrl+N";
	InsertMenuItem(hMenuScript, 1, TRUE, &mi);

	/* 停止(P)を作成する */
	mi.wID = ID_PAUSE;
	mi.dwTypeData = bEnglish ? L"Pause(&P)\tCtrl+P" : L"停止(&P)\tCtrl+P";
	InsertMenuItem(hMenuScript, 2, TRUE, &mi);
	EnableMenuItem(hMenu, ID_PAUSE, MF_GRAYED);

	/* 次のエラー箇所へ移動(E)を作成する */
	mi.wID = ID_ERROR;
	mi.dwTypeData = bEnglish ?
		L"Go to next error(&E)\tCtrl+E" :
		L"次のエラー箇所へ移動(&E)\tCtrl+E";
	InsertMenuItem(hMenuScript, 3, TRUE, &mi);

	/* パッケージをエクスポートするを作成する */
	mi.wID = ID_EXPORT;
	mi.dwTypeData = bEnglish ?
		L"Export package(&X)" :
		L"パッケージをエクスポートする";
	InsertMenuItem(hMenuExport, 0, TRUE, &mi);

	/* Windows向けにエクスポートするを作成する */
	mi.wID = ID_EXPORT_WIN;
	mi.dwTypeData = bEnglish ?
		L"Export for Windows" :
		L"Windows向けにエクスポートする";
	InsertMenuItem(hMenuExport, 1, TRUE, &mi);

	/* Windows EXEインストーラを作成するを作成する */
	mi.wID = ID_EXPORT_WIN_INST;
	mi.dwTypeData = bEnglish ?
		L"Create EXE Installer for Windows" :
		L"Windows EXEインストーラを作成する";
	InsertMenuItem(hMenuExport, 2, TRUE, &mi);

	/* Windows/Mac向けにエクスポートするを作成する */
	mi.wID = ID_EXPORT_WIN_MAC;
	mi.dwTypeData = bEnglish ?
		L"Export for Windows/Mac" :
		L"Windows/Mac向けにエクスポートする";
	InsertMenuItem(hMenuExport, 3, TRUE, &mi);

	/* Web向けにエクスポートするを作成する */
	mi.wID = ID_EXPORT_WEB;
	mi.dwTypeData = bEnglish ?
		L"Export for Web" :
		L"Web向けにエクスポートする";
	InsertMenuItem(hMenuExport, 4, TRUE, &mi);

	/* Androidプロジェクトをエクスポートするを作成する */
	mi.wID = ID_EXPORT_ANDROID;
	mi.dwTypeData = bEnglish ?
		L"Export Android project" :
		L"Androidプロジェクトをエクスポートする";
	InsertMenuItem(hMenuExport, 5, TRUE, &mi);

	/* iOSプロジェクトをエクスポートするを作成する */
	mi.wID = ID_EXPORT_IOS;
	mi.dwTypeData = bEnglish ?
		L"Export iOS project" :
		L"iOSプロジェクトをエクスポートする";
	InsertMenuItem(hMenuExport, 6, TRUE, &mi);

	/* バージョン(V)を作成する */
	mi.wID = ID_VERSION;
	mi.dwTypeData = bEnglish ? L"Version(&V)" : L"バージョン(&V)\tCtrl+V";
	InsertMenuItem(hMenuHelp, 0, TRUE, &mi);

	/* メニューをセットする */
	SetMenu(hWnd, hMenu);
}

/* ツールチップを作成する */
static HWND CreateTooltip(HWND hWndBtn, const wchar_t *pszTextEnglish,
						  const wchar_t *pszTextJapanese)
{
	TOOLINFO ti;

	/* ツールチップを作成する */
	HWND hWndTip = CreateWindowEx(0, TOOLTIPS_CLASS, NULL, TTS_ALWAYSTIP,
								  CW_USEDEFAULT, CW_USEDEFAULT,
								  CW_USEDEFAULT, CW_USEDEFAULT,
								  hWndDebug, NULL, GetModuleHandle(NULL),
								  NULL);

	/* ツールチップをボタンに紐付ける */
	ZeroMemory(&ti, sizeof(ti));
	ti.cbSize = sizeof(ti);
	ti.uFlags = TTF_SUBCLASS;
	ti.hwnd = hWndBtn;
	ti.lpszText = (wchar_t *)(bEnglish ? pszTextEnglish : pszTextJapanese);
	GetClientRect(hWndBtn, &ti.rect);
	SendMessage(hWndTip, TTM_ADDTOOL, 0, (LPARAM)&ti);

	return hWndTip;
}

/*
 * メッセージ処理
 */

/*
 * メッセージのトランスレート前処理を行う
 *  - return: メッセージが消費されたか
 *    - TRUEなら呼出元はメッセージをウィンドウプロシージャに送ってはならない
 *    - FALSEなら呼出元はメッセージをウィンドウプロシージャに送らなくてはいけない
 */
BOOL PretranslateForDebugger(MSG* pMsg)
{
	static BOOL bShiftDown;
	static BOOL bControlDown;
	int nStart, nEnd;

	bRangedChanged = FALSE;

	/* シフト押下状態を保存する */
	if (pMsg->hwnd == hWndRichEdit && pMsg->wParam == VK_SHIFT)
	{
		if (pMsg->message == WM_KEYDOWN)
			bShiftDown = TRUE;
		if (pMsg->message == WM_KEYUP)
			bShiftDown = FALSE;
		return FALSE;
	}

	/* コントロール押下状態を保存する */
	if (pMsg->hwnd == hWndRichEdit && pMsg->wParam == VK_CONTROL)
	{
		if (pMsg->message == WM_KEYDOWN)
			bControlDown = TRUE;
		if (pMsg->message == WM_KEYUP)
			bControlDown = FALSE;
		return FALSE;
	}

	/* キー押下を処理する */
	if (pMsg->hwnd == hWndRichEdit && pMsg->message == WM_KEYDOWN)
	{
		switch (pMsg->wParam)
		{
		case VK_RETURN:
			if (bShiftDown)
			{
				/* Shift+Returnは通常の改行としてモデルに反映する */
				bRangedChanged = TRUE;
			}
			else
			{
				/* 実行位置/実行状態の制御を行う */
				RichEdit_OnReturn();

				/* このメッセージをリッチエディットにディスパッチしない */
				return TRUE;
			}
			break;
		case VK_DELETE:
			if (RichEdit_IsLineEnd())
				bRangedChanged = TRUE;
			break;
		case VK_BACK:
			if (RichEdit_IsLineTop())
				bRangedChanged = TRUE;
			break;
		case 'V':
			/* Ctrl+Vを処理する */
			if (bControlDown)
				bRangedChanged = TRUE;
			break;
		case 'S':
			/* Ctrl+Sを処理する */
			OnSave();
			break;
		default:
			/* 範囲選択に対する置き換えの場合 */
			RichEdit_GetSelectedRange(&nStart, &nEnd);
			if (nStart != nEnd)
				bRangedChanged = TRUE;
			break;
		}
		return FALSE;
	}

	/* このメッセージは引き続きリッチエディットで処理する */
	return FALSE;
}

/*
 * デバッガへのWM_COMMANDを処理する
 */
VOID OnCommandForDebugger(WPARAM wParam, UNUSED(LPARAM lParam))
{
	UINT nID;
	WORD wNotify;

	/* EN_CHANGEを確認する */
	wNotify = (WORD)(wParam >> 16) & 0xFFFF;
	if (wNotify == EN_CHANGE)
	{
		RichEdit_OnChange();
		return;
	}

	/* その他の通知を処理する */
	nID = LOWORD(wParam);
	switch(nID)
	{
	case ID_QUIT:
		DestroyWindow(hWndMain);
		break;
	case ID_VERSION:
		MessageBox(hWndMain, bEnglish ? VERSION_EN : VERSION_JP,
				   MSGBOX_TITLE, MB_OK | MB_ICONINFORMATION);
		break;
	case ID_RESUME:
		bContinuePressed = TRUE;
		break;
	case ID_NEXT:
		bNextPressed = TRUE;
		break;
	case ID_PAUSE:
		bStopPressed = TRUE;
		break;
	case ID_OPEN:
		OnOpenScript();
		break;
	case ID_ERROR:
		OnNextError();
		break;
	case ID_SAVE:
		OnSave();
		break;
	case ID_VARS:
		OnWriteVars();
		break;
	case ID_EXPORT:
		OnExportPackage();
		break;
	case ID_EXPORT_WIN:
		OnExportWin();
		break;
	case ID_EXPORT_WIN_INST:
		OnExportWinInst();
		break;
	case ID_EXPORT_WIN_MAC:
		OnExportWinMac();
		break;
	case ID_EXPORT_WEB:
		OnExportWeb();
		break;
	case ID_EXPORT_ANDROID:
		OnExportAndroid();
		break;
	case ID_EXPORT_IOS:
		OnExportIOS();
		break;
	default:
		break;
	}
}

/*
 * コマンド処理
 */

/* スクリプトオープン */
static VOID OnOpenScript(void)
{
	OPENFILENAMEW ofn;
	wchar_t szPath[1024];
	size_t i;

	memset(szPath, 0, sizeof(szPath));

	ZeroMemory(&szPath[0], sizeof(szPath));
	ZeroMemory(&ofn, sizeof(OPENFILENAMEW));
	ofn.lStructSize = sizeof(OPENFILENAMEW);
	ofn.hwndOwner = hWndMain;
	ofn.lpstrFilter = bEnglish ?
		L"Text Files\0*.txt;\0All Files(*.*)\0*.*\0\0" : 
		L"テキストファイル\0*.txt;\0すべてのファイル(*.*)\0*.*\0\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFile = szPath;
	ofn.nMaxFile = sizeof(szPath);
	ofn.lpstrInitialDir = conv_utf8_to_utf16(SCRIPT_DIR);
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
	ofn.lpstrDefExt = L"txt";
	GetOpenFileNameW(&ofn);
	if(ofn.lpstrFile[0])
	{
		for (i = wcslen(szPath) - 1; i != 0; i--) {
			if (*(szPath + i) == L'\\')
			{
				SetWindowText(hWndTextboxScript, szPath + i + 1);
				bScriptOpened = TRUE;
				break;
			}
		}
	}
}

/* 上書き保存 */
static VOID OnSave(void)
{
	if (!save_script())
	{
		MessageBox(hWndMain, bEnglish ?
				   L"Cannot write to file." :
				   L"ファイルに書き込めません。",
				   MSGBOX_TITLE, MB_OK | MB_ICONERROR);
	}
}

/* 次のエラー箇所へ移動ボタンが押下されたとき */
static VOID OnNextError(void)
{
	/* TODO */
}

/* 変数の書き込みボタンが押下された場合を処理する */
static VOID OnWriteVars(void)
{
	static wchar_t szTextboxVar[VAR_TEXTBOX_MAX];
	wchar_t *p, *next_line;
	int index, val;

	/* テキストボックスの内容を取得する */
	GetWindowText(hWndTextboxVar, szTextboxVar, sizeof(szTextboxVar) / sizeof(wchar_t) - 1);

	/* パースする */
	p = szTextboxVar;
	while(*p)
	{
		/* 空行を読み飛ばす */
		if(*p == '\n')
		{
			p++;
			continue;
		}

		/* 次の行の開始文字を探す */
		next_line = p;
		while(*next_line)
		{
			if(*next_line == '\r')
			{
				*next_line = '\0';
				next_line++;
				break;
			}
			next_line++;
		}

		/* パースする */
		if(swscanf(p, L"$%d=%d", &index, &val) != 2)
			index = -1, val = -1;
		if(index >= LOCAL_VAR_SIZE + GLOBAL_VAR_SIZE)
			index = -1;

		/* 変数を設定する */
		if(index != -1)
			set_variable(index, val);

		/* 次の行へポインタを進める */
		p = next_line;
	}

	Variable_UpdateText();
}

/* パッケージを作成メニューが押下されたときの処理を行う */
VOID OnExportPackage(void)
{
	if (MessageBox(hWndMain, bEnglish ?
				   L"Are you sure you want to export the package file?\n"
				   L"This may take a while." :
				   L"パッケージをエクスポートします。\n"
				   L"この処理には時間がかかります。\n"
				   L"よろしいですか？",
				   MSGBOX_TITLE, MB_ICONWARNING | MB_OKCANCEL) != IDOK)
		return;

	/* パッケージを作成する */
	if (create_package("")) {
		log_info(bEnglish ?
				 "Successfully exported data01.arc" :
				 "data01.arcのエクスポートに成功しました。");
	}
}

/* Windows向けにエクスポートのメニューが押下されたときの処理を行う */
VOID OnExportWin(void)
{
	if (MessageBox(hWndMain, bEnglish ?
				   L"Takes a while. Are you sure?\n" :
				   L"エクスポートには時間がかかります。よろしいですか？",
				   MSGBOX_TITLE, MB_ICONWARNING | MB_OKCANCEL) != IDOK)
		return;

	/* パッケージを作成する */
	if (!create_package(""))
	{
		log_info(bEnglish ?
				 "Failed to export data01.arc" :
				 "data01.arcのエクスポートに失敗しました。");
		return;
	}

	/* フォルダを再作成する */
	RecreateDirectory(L".\\windows-export");

	/* ファイルをコピーする */
	if (!CopySourceFiles(L".\\suika.exe", L".\\windows-export\\suika.exe"))
	{
		log_info(bEnglish ?
				 "Failed to copy exe file." :
				 "実行ファイルのコピーに失敗しました。");
		return;
	}

	/* movをコピーする */
	CopyMovFiles(L".\\mov", L".\\windows-export\\mov");

	/* パッケージを移動する */
	if (!MovePackageFile(L".\\data01.arc", L".\\windows-export\\data01.arc"))
	{
		log_info(bEnglish ?
				 "Failed to move data01.arc" :
				 "data01.arcの移動に失敗しました。");
		return;
	}

	MessageBox(hWndMain, bEnglish ?
			   L"Export succeeded. Will open the folder." :
			   L"エクスポートに成功しました。フォルダを開きます。",
			   MSGBOX_TITLE, MB_ICONINFORMATION | MB_OK);

	/* Explorerを開く */
	ShellExecuteW(NULL, L"explore", L".\\windows-export", NULL, NULL, SW_SHOW);
}

/* Windows向けインストーラをエクスポートするのメニューが押下されたときの処理を行う */
VOID OnExportWinInst(void)
{
	wchar_t cmdline[1024];
	STARTUPINFOW si;
	PROCESS_INFORMATION pi;

	if (MessageBox(hWndMain, bEnglish ?
				   L"Takes a while. Are you sure?\n" :
				   L"エクスポートには時間がかかります。よろしいですか？",
				   MSGBOX_TITLE, MB_ICONWARNING | MB_OKCANCEL) != IDOK)
		return;

	/* パッケージを作成する */
	if (!create_package(""))
	{
		log_info(bEnglish ?
				 "Failed to export data01.arc" :
				 "data01.arcのエクスポートに失敗しました。");
		return;
	}

	/* フォルダを再作成する */
	RecreateDirectory(L".\\windows-installer-export");
	CreateDirectory(L".\\windows-installer-export\\asset", 0);

	/* ファイルをコピーする */
	if (!CopySourceFiles(L".\\suika.exe", L".\\windows-installer-export\\asset\\suika.exe"))
	{
		log_info(bEnglish ?
				 "Failed to copy exe file." :
				 "実行ファイルのコピーに失敗しました。");
		return;
	}

	/* ファイルをコピーする */
	if (!CopySourceFiles(L".\\tools\\installer\\create-installer.bat", L".\\windows-installer-export\\asset\\create-installer.bat"))
	{
		log_info(bEnglish ?
				 "Failed to copy exe file." :
				 "実行ファイルのコピーに失敗しました。");
		return;
	}

	/* ファイルをコピーする */
	if (!CopySourceFiles(L".\\tools\\installer\\icon.ico", L".\\windows-installer-export\\asset\\icon.ico"))
	{
		log_info(bEnglish ?
				 "Failed to copy exe file." :
				 "実行ファイルのコピーに失敗しました。");
		return;
	}

	/* ファイルをコピーする */
	if (!CopySourceFiles(L".\\tools\\installer\\install-script.nsi", L".\\windows-installer-export\\asset\\install-script.nsi"))
	{
		log_info(bEnglish ?
				 "Failed to copy exe file." :
				 "実行ファイルのコピーに失敗しました。");
		return;
	}

	/* movをコピーする */
	CopyMovFiles(L".\\mov", L".\\windows-installer-export\\asset\\mov");

	/* パッケージを移動する */
	if (!MovePackageFile(L".\\data01.arc", L".\\windows-installer-export\\asset\\data01.arc"))
	{
		log_info(bEnglish ?
				 "Failed to move data01.arc" :
				 "data01.arcの移動に失敗しました。");
		return;
	}

	/* バッチファイルを呼び出す */
	wcscpy(cmdline, L"cmd.exe /k create-installer.bat");
	ZeroMemory(&si, sizeof(STARTUPINFOW));
	si.cb = sizeof(STARTUPINFOW);
	CreateProcessW(NULL,	/* lpApplication */
				   cmdline,
				   NULL,	/* lpProcessAttribute */
				   NULL,	/* lpThreadAttributes */
				   FALSE,	/* bInheritHandles */
				   NORMAL_PRIORITY_CLASS | CREATE_NEW_CONSOLE | CREATE_NEW_PROCESS_GROUP,
				   NULL,	/* lpEnvironment */
				   L".\\windows-installer-export\\asset",
				   &si,
				   &pi);
	if (pi.hProcess != NULL)
		CloseHandle(pi.hThread);
	if (pi.hProcess != NULL)
		CloseHandle(pi.hProcess);
}

/* Windows/Mac向けにエクスポートのメニューが押下されたときの処理を行う */
VOID OnExportWinMac(void)
{
	if (MessageBox(hWndMain, bEnglish ?
				   L"Takes a while. Are you sure?\n" :
				   L"エクスポートには時間がかかります。よろしいですか？",
				   MSGBOX_TITLE, MB_ICONWARNING | MB_OKCANCEL) != IDOK)
		return;

	/* パッケージを作成する */
	if (!create_package(""))
	{
		log_info(bEnglish ?
				 "Failed to export data01.arc" :
				 "data01.arcのエクスポートに失敗しました。");
		return;
	}

	/* フォルダを再作成する */
	RecreateDirectory(L".\\windows-mac-export");

	/* ファイルをコピーする */
	if (!CopySourceFiles(L".\\suika.exe", L".\\windows-mac-export\\suika.exe"))
	{
		log_info(bEnglish ?
				 "Failed to copy exe file." :
				 "実行ファイルのコピーに失敗しました。");
		return;
	}

	/* ファイルをコピーする */
	if (!CopySourceFiles(L".\\mac.dmg", L".\\windows-mac-export\\mac.dmg"))
	{
		log_info(bEnglish ?
				 "Failed to copy exe file." :
				 "Mac用の実行ファイルのコピーに失敗しました。");
		return;
	}

	/* movをコピーする */
	CopyMovFiles(L".\\mov", L".\\windows-mac-export\\mov");

	/* パッケージを移動する */
	if (!MovePackageFile(L".\\data01.arc", L".\\windows-mac-export\\data01.arc"))
	{
		log_info(bEnglish ?
				 "Failed to move data01.arc" :
				 "data01.arcの移動に失敗しました。");
		return;
	}

	MessageBox(hWndMain, bEnglish ?
			   L"Export succeeded. Will open the folder." :
			   L"エクスポートに成功しました。フォルダを開きます。",
			   MSGBOX_TITLE, MB_ICONINFORMATION | MB_OK);

	/* Explorerを開く */
	ShellExecuteW(NULL, L"explore", L".\\windows-mac-export", NULL, NULL, SW_SHOW);
}

/* Web向けにエクスポートのメニューが押下されたときの処理を行う */
VOID OnExportWeb(void)
{
	if (MessageBox(hWndMain, bEnglish ?
				   L"Takes a while. Are you sure?\n" :
				   L"エクスポートには時間がかかります。よろしいですか？",
				   MSGBOX_TITLE, MB_ICONWARNING | MB_OKCANCEL) != IDOK)
		return;

	/* パッケージを作成する */
	if (!create_package(""))
	{
		log_info(bEnglish ?
				 "Failed to export data01.arc" :
				 "data01.arcのエクスポートに失敗しました。");
		return;
	}

	/* フォルダを再作成する */
	RecreateDirectory(L".\\web-export");

	/* ソースをコピーする */
	if (!CopySourceFiles(L".\\tools\\web\\*", L".\\web-export"))
	{
		log_info(bEnglish ?
				 "Failed to copy source files for Web." :
				 "ソースコードのコピーに失敗しました。"
				 "最新のtools/webフォルダが存在するか確認してください。");
		return;
	}

	/* movをコピーする */
	CopyMovFiles(L".\\mov", L".\\web-export\\mov");

	/* パッケージを移動する */
	if (!MovePackageFile(L".\\data01.arc", L".\\web-export\\data01.arc"))
	{
		log_info(bEnglish ?
				 "Failed to move data01.arc" :
				 "data01.arcの移動に失敗しました。");
		return;
	}

	MessageBox(hWndMain, bEnglish ?
			   L"Export succeeded. Will open the folder." :
			   L"エクスポートに成功しました。フォルダを開きます。",
			   MSGBOX_TITLE, MB_ICONINFORMATION | MB_OK);

	/* Explorerを開く */
	ShellExecuteW(NULL, L"explore", L".\\web-export", NULL, NULL, SW_SHOW);
}

/* Androidプロジェクトをエクスポートのメニューが押下されたときの処理を行う */
VOID OnExportAndroid(void)
{
	if (MessageBox(hWndMain, bEnglish ?
				   L"Takes a while. Are you sure?\n" :
				   L"エクスポートには時間がかかります。よろしいですか？",
				   MSGBOX_TITLE, MB_ICONWARNING | MB_OKCANCEL) != IDOK)
		return;

	/* フォルダを再作成する */
	RecreateDirectory(L".\\android-export");

	/* ソースをコピーする */
	if (!CopySourceFiles(L".\\tools\\android-src", L".\\android-export"))
	{
		log_info(bEnglish ?
				 "Failed to copy source files for Android." :
				 "ソースコードのコピーに失敗しました。"
				 "最新のtools/android-srcフォルダが存在するか確認してください。");
		return;
	}

	/* アセットをコピーする */
	CopySourceFiles(L".\\anime", L".\\android-export\\app\\src\\main\\assets\\anime");
	CopySourceFiles(L".\\bg", L".\\android-export\\app\\src\\main\\assets\\bg");
	CopySourceFiles(L".\\bgm", L".\\android-export\\app\\src\\main\\assets\\bgm");
	CopySourceFiles(L".\\cg", L".\\android-export\\app\\src\\main\\assets\\cg");
	CopySourceFiles(L".\\ch", L".\\android-export\\app\\src\\main\\assets\\ch");
	CopySourceFiles(L".\\conf", L".\\android-export\\app\\src\\main\\assets\\conf");
	CopySourceFiles(L".\\cv", L".\\android-export\\app\\src\\main\\assets\\cv");
	CopySourceFiles(L".\\font", L".\\android-export\\app\\src\\main\\assets\\font");
	CopySourceFiles(L".\\gui", L".\\android-export\\app\\src\\main\\assets\\gui");
	CopySourceFiles(L".\\mov", L".\\android-export\\app\\src\\main\\assets\\mov");
	CopySourceFiles(L".\\rule", L".\\android-export\\app\\src\\main\\assets\\rule");
	CopySourceFiles(L".\\se", L".\\android-export\\app\\src\\main\\assets\\se");
	CopySourceFiles(L".\\txt", L".\\android-export\\app\\src\\main\\assets\\txt");
	CopySourceFiles(L".\\wms", L".\\android-export\\app\\src\\main\\assets\\wms");

	MessageBox(hWndMain, bEnglish ?
			   L"Will open the exported source code folder.\n"
			   L"Build with Android Studio." :
			   L"エクスポートしたソースコードフォルダを開きます。\n"
			   L"Android Studioでそのままビルドできます。",
			   MSGBOX_TITLE, MB_ICONINFORMATION | MB_OK);

	/* Explorerを開く */
	ShellExecuteW(NULL, L"explore", L".\\android-export", NULL, NULL, SW_SHOW);
}

/* iOSプロジェクトをエクスポートのメニューが押下されたときの処理を行う */
VOID OnExportIOS(void)
{
	if (MessageBox(hWndMain, bEnglish ?
				   L"Takes a while. Are you sure?\n" :
				   L"エクスポートには時間がかかります。よろしいですか？",
				   MSGBOX_TITLE, MB_ICONWARNING | MB_OKCANCEL) != IDOK)
		return;

	/* パッケージを作成する */
	if (!create_package(""))
	{
		log_info(bEnglish ?
				 "Failed to export data01.arc" :
				 "data01.arcのエクスポートに失敗しました。");
		return;
	}

	/* フォルダを再作成する */
	RecreateDirectory(L".\\ios-export");

	/* ソースをコピーする */
	if (!CopySourceFiles(L".\\tools\\ios-src", L".\\ios-export"))
	{
		log_info(bEnglish ?
				 "Failed to copy source files for Android." :
				 "ソースコードのコピーに失敗しました。"
				 "最新のtools/ios-srcフォルダが存在するか確認してください。");
		return;
	}

	/* movをコピーする */
	CopyMovFiles(L".\\mov", L".\\ios-export\\suika\\mov");

	/* パッケージを移動する */
	if (!MovePackageFile(L".\\data01.arc", L".\\ios-export\\suika\\data01.arc"))
	{
		log_info(bEnglish ?
				 "Failed to move data01.arc" :
				 "data01.arcの移動に失敗しました。");
		return;
	}

	MessageBox(hWndMain, bEnglish ?
			   L"Will open the exported source code folder.\n"
			   L"Build with Xcode." :
			   L"エクスポートしたソースコードフォルダを開きます。\n"
			   L"Xcodeでそのままビルドできます。\n",
			   MSGBOX_TITLE, MB_ICONINFORMATION | MB_OK);

	/* Explorerを開く */
	ShellExecuteW(NULL, L"explore", L".\\ios-export", NULL, NULL, SW_SHOW);
}

/* フォルダを再作成する */
static VOID RecreateDirectory(const wchar_t *path)
{
	wchar_t newpath[MAX_PATH];
	SHFILEOPSTRUCT fos;

	/* 二重のNUL終端を行う */
	wcscpy(newpath, path);
	newpath[wcslen(path) + 1] = L'\0';

	/* コピーする */
	ZeroMemory(&fos, sizeof(SHFILEOPSTRUCT));
	fos.wFunc = FO_DELETE;
	fos.pFrom = newpath;
	fos.fFlags = FOF_NOCONFIRMATION | FOF_NOERRORUI | FOF_SILENT;
	SHFileOperationW(&fos);
}

/* ソースツリーをコピーする */
static BOOL CopySourceFiles(const wchar_t *lpszSrcDir, const wchar_t *lpszDestDir)
{
	wchar_t from[MAX_PATH];
	wchar_t to[MAX_PATH];
	SHFILEOPSTRUCTW fos;
	int ret;

	/* 二重のNUL終端を行う */
	wcscpy(from, lpszSrcDir);
	from[wcslen(lpszSrcDir) + 1] = L'\0';
	wcscpy(to, lpszDestDir);
	to[wcslen(lpszDestDir) + 1] = L'\0';

	/* コピーする */
	ZeroMemory(&fos, sizeof(SHFILEOPSTRUCT));
	fos.wFunc = FO_COPY;
	fos.pFrom = from;
	fos.pTo = to;
	fos.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR;
	ret = SHFileOperationW(&fos);
	if (ret != 0)
	{
		log_info("error code = %d", ret);
		return FALSE;
	}

	return TRUE;
}

/* movをコピーする */
static BOOL CopyMovFiles(const wchar_t *lpszSrcDir, const wchar_t *lpszDestDir)
{
	wchar_t from[MAX_PATH];
	wchar_t to[MAX_PATH];
	SHFILEOPSTRUCTW fos;

	/* 二重のNUL終端を行う */
	wcscpy(from, lpszSrcDir);
	from[wcslen(lpszSrcDir) + 1] = L'\0';
	wcscpy(to, lpszDestDir);
	to[wcslen(lpszDestDir) + 1] = L'\0';

	/* コピーする */
	ZeroMemory(&fos, sizeof(SHFILEOPSTRUCT));
	fos.wFunc = FO_COPY;
	fos.pFrom = from;
	fos.pTo = to;
	fos.fFlags = FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI |
		FOF_SILENT;
	SHFileOperationW(&fos);

	return TRUE;
}

/* パッケージファイルを移動する */
static BOOL MovePackageFile(const wchar_t *lpszPkgFile, wchar_t *lpszDestDir)
{
	wchar_t from[MAX_PATH];
	wchar_t to[MAX_PATH];
	SHFILEOPSTRUCTW fos;
	int ret;

	/* 二重のNUL終端を行う */
	wcscpy(from, lpszPkgFile);
	from[wcslen(lpszPkgFile) + 1] = L'\0';
	wcscpy(to, lpszDestDir);
	to[wcslen(lpszDestDir) + 1] = L'\0';

	/* 移動する */
	ZeroMemory(&fos, sizeof(SHFILEOPSTRUCT));
	fos.hwnd = NULL;
	fos.wFunc = FO_MOVE;
	fos.pFrom = from;
	fos.pTo = to;
	fos.fFlags = FOF_NOCONFIRMATION;
	ret = SHFileOperationW(&fos);
	if (ret != 0)
	{
		log_info("error code = %d", ret);
		return FALSE;
	}

	return TRUE;
}

/*
 * platform.h
 */

/*
 * 続けるボタンが押されたか調べる
 */
bool is_continue_pushed(void)
{
	bool ret = bContinuePressed;
	bContinuePressed = FALSE;
	return ret;
}

/*
 * 次へボタンが押されたか調べる
 */
bool is_next_pushed(void)
{
	bool ret = bNextPressed;
	bNextPressed = FALSE;
	return ret;
}

/*
 * 停止ボタンが押されたか調べる
 */
bool is_stop_pushed(void)
{
	bool ret = bStopPressed;
	bStopPressed = FALSE;
	return ret;
}

/*
 * 実行するスクリプトファイルが変更されたか調べる
 */
bool is_script_opened(void)
{
	bool ret = bScriptOpened;
	bScriptOpened = FALSE;
	return ret;
}

/*
 * 変更された実行するスクリプトファイル名を取得する
 */
const char *get_opened_script(void)
{
	static wchar_t script[256];

	GetWindowText(hWndTextboxScript,
				  script,
				  sizeof(script) /sizeof(wchar_t) - 1);
	script[255] = L'\0';
	return conv_utf16_to_utf8(script);
}

/*
 * 実行する行番号が変更されたか調べる
 */
bool is_exec_line_changed(void)
{
	bool ret = bExecLineChanged;
	bExecLineChanged = FALSE;
	return ret;
}

/*
 * 変更された実行する行番号を取得する
 */
int get_changed_exec_line(void)
{
	return nLineChanged;
}

/*
 * 旧Suika2 Pro用: VLSでは使用しない TODO: 削除
 */
bool is_command_updated(void) { return false; }
const char* get_updated_command(void) { assert(0); return NULL; }
bool is_script_reloaded(void) { 	return false; }

/*
 * コマンドの実行中状態を設定する
 */
void on_change_running_state(bool running, bool request_stop)
{
	bRunning = running;

	if(request_stop)
	{
		/*
		 * 停止によりコマンドの完了を待機中のとき
		 *  - コントロールとメニューアイテムを無効にする
		 */
		EnableWindow(hWndBtnResume, FALSE);
		EnableWindow(hWndBtnNext, FALSE);
		EnableWindow(hWndBtnPause, FALSE);
		EnableWindow(hWndTextboxScript, FALSE);
		EnableWindow(hWndBtnSelectScript, FALSE);
		SendMessage(hWndRichEdit, EM_SETREADONLY, TRUE, 0);
		SendMessage(hWndTextboxVar, EM_SETREADONLY, TRUE, 0);
		EnableWindow(hWndBtnVar, FALSE);
		EnableMenuItem(hMenu, ID_OPEN, MF_GRAYED);
		EnableMenuItem(hMenu, ID_SAVE, MF_GRAYED);
		EnableMenuItem(hMenu, ID_EXPORT, MF_GRAYED);
		EnableMenuItem(hMenu, ID_RESUME, MF_GRAYED);
		EnableMenuItem(hMenu, ID_NEXT, MF_GRAYED);
		EnableMenuItem(hMenu, ID_PAUSE, MF_GRAYED);
		EnableMenuItem(hMenu, ID_ERROR, MF_GRAYED);
	}
	else if(running)
	{
		/*
		 * 実行中のとき
		 *  - 「停止」だけ有効、他は無効にする
		 */
		EnableWindow(hWndBtnResume, FALSE);
		EnableWindow(hWndBtnNext, FALSE);
		EnableWindow(hWndBtnPause, TRUE);					/* 有効 */
		EnableWindow(hWndTextboxScript, FALSE);
		EnableWindow(hWndBtnSelectScript, FALSE);
		SendMessage(hWndRichEdit, EM_SETREADONLY, TRUE, 0);
		SendMessage(hWndTextboxVar, EM_SETREADONLY, TRUE, 0);
		EnableWindow(hWndBtnVar, FALSE);
		EnableMenuItem(hMenu, ID_OPEN, MF_GRAYED);
		EnableMenuItem(hMenu, ID_SAVE, MF_GRAYED);
		EnableMenuItem(hMenu, ID_EXPORT, MF_GRAYED);
		EnableMenuItem(hMenu, ID_RESUME, MF_GRAYED);
		EnableMenuItem(hMenu, ID_NEXT, MF_GRAYED);
		EnableMenuItem(hMenu, ID_PAUSE, MF_ENABLED);		/* 有効 */
		EnableMenuItem(hMenu, ID_ERROR, MF_GRAYED);
	}
	else
	{
		/*
		 * 完全に停止中のとき
		 *  - 「停止」だけ無効、他は有効にする
		 */
		EnableWindow(hWndBtnResume, TRUE);
		EnableWindow(hWndBtnNext, TRUE);
		EnableWindow(hWndBtnPause, FALSE);					/* 無効 */
		EnableWindow(hWndTextboxScript, TRUE);
		EnableWindow(hWndBtnSelectScript, TRUE);
		SendMessage(hWndRichEdit, EM_SETREADONLY, FALSE, 0);
		SendMessage(hWndTextboxVar, EM_SETREADONLY, FALSE, 0);
		EnableWindow(hWndBtnVar, TRUE);
		EnableMenuItem(hMenu, ID_OPEN, MF_ENABLED);
		EnableMenuItem(hMenu, ID_SAVE, MF_ENABLED);
		EnableMenuItem(hMenu, ID_EXPORT, MF_ENABLED);
		EnableMenuItem(hMenu, ID_RESUME, MF_ENABLED);
		EnableMenuItem(hMenu, ID_NEXT, MF_ENABLED);
		EnableMenuItem(hMenu, ID_PAUSE, MF_GRAYED);		/* 無効 */
		EnableMenuItem(hMenu, ID_ERROR, MF_ENABLED);
	}
}

/*
 * 実行位置を更新する
 */
void on_change_exec_position(bool script_changed)
{
	const char *script_file;

	/* スクリプトファイル名を設定する */
	script_file = get_script_file_name();
	SetWindowText(hWndTextboxScript, conv_utf8_to_utf16(script_file));

	/* 実行中のスクリプトファイルが変更されたとき、リッチエディットにテキストを設定する */
	if (script_changed)
	{
		RichEdit_UpdateTextFromScriptModel();
		RichEdit_SetTextFormatAll();
	}

	RichEdit_SetBackgroundColorForExecuteLine();

	/* 変数の情報を更新する */
	if(check_variable_updated() || script_changed)
		Variable_UpdateText();
}

/*
 * 変数テキストボックス
 */

/* 変数の情報を更新する */
static VOID Variable_UpdateText(void)
{
	static wchar_t szTextboxVar[VAR_TEXTBOX_MAX];
	wchar_t line[1024];
	int index;
	int val;

	szTextboxVar[0] = L'\0';

	for(index = 0; index < LOCAL_VAR_SIZE + GLOBAL_VAR_SIZE; index++)
	{
		/* 変数が初期値の場合 */
		val = get_variable(index);
		if(val == 0 && !is_variable_changed(index))
			continue;

		/* 行を追加する */
		_snwprintf(line,
				   sizeof(line) / sizeof(wchar_t),
				   L"$%d=%d\r\n",
				   index,
				   val);
		line[1023] = L'\0';
		wcscat(szTextboxVar, line);
	}

	/* テキストボックスにセットする */
	SetWindowText(hWndTextboxVar, szTextboxVar);
}

/*
 * リッチエディットのハンドラ
 */

/* リッチエディットの内容の更新通知を処理する */
static VOID RichEdit_OnChange(void)
{
	int nCursor;

	/* カーソル位置を取得する */
	nCursor = RichEdit_GetCursorPosition();

	/* 複数行にまたがる可能性のある変更である場合 */
	if (bRangedChanged)
	{
		bRangedChanged = FALSE;

		/* スクリプトモデルを作り直す */
		RichEdit_UpdateScriptModelFromText();

		/* ハイライトを更新する */
		RichEdit_SetTextFormatAll();
	}
	else
	{
		/* 行の色付けを変更する */
		RichEdit_SetTextColorForCursorLine();
	}

	/*
	 * カーソル位置を設定する
	 *  - 色付けで選択が変更されたのを修正する
	 */
	RichEdit_SetCursorPosition(nCursor);
}

/* リッチエディットでのReturnキー押下を処理する */
static VOID RichEdit_OnReturn(void)
{
	int nCursorLine;

	/* リッチエディットのカーソル行番号を取得する */
	nCursorLine = RichEdit_GetCursorLine();
	if (nCursorLine == -1)
		return;

	/* 次フレームでの実行行の移動の問い合わせに真を返すようにしておく */
	if (nCursorLine != get_expanded_line_num())
	{
		nLineChanged = nCursorLine;
		bExecLineChanged = TRUE;
	}

	/* スクリプトモデルを更新する */
	RichEdit_UpdateScriptModelFromText();

	/* 次フレームでの一行実行の問い合わせに真を返すようにしておく */
	if (dbg_get_parse_error_count() == 0)
		bNextPressed = TRUE;
}

/*
 * リッチエディットを操作するヘルパー
 */

/* リッチエディットのカーソル位置を取得する */
static int RichEdit_GetCursorPosition(void)
{
	CHARRANGE cr;

	/* カーソル位置を取得する */
	SendMessage(hWndRichEdit, EM_EXGETSEL, 0, (LPARAM)&cr);

	return cr.cpMin;
}

/* リッチエディットのカーソル位置を設定する */
static VOID RichEdit_SetCursorPosition(int nCursor)
{
	CHARRANGE cr;

	/* カーソル位置を取得する */
	cr.cpMin = nCursor;
	cr.cpMax = nCursor;
	SendMessage(hWndRichEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
}

/* リッチエディットの範囲選択範囲を求める */
static VOID RichEdit_GetSelectedRange(int *nStart, int *nEnd)
{
	CHARRANGE cr;
	memset(&cr, 0, sizeof(cr));
	SendMessage(hWndRichEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
	*nStart = cr.cpMin;
	*nEnd = cr.cpMax;
}

/* リッチエディットの範囲を選択する */
static VOID RichEdit_SetSelectedRange(int nLineStart, int nLineLen)
{
	CHARRANGE cr;

	memset(&cr, 0, sizeof(cr));
	cr.cpMin = nLineStart;
	cr.cpMax = nLineStart + nLineLen;
	SendMessage(hWndRichEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
}

/* リッチエディットのカーソル行の行番号を取得する */
static int RichEdit_GetCursorLine(void)
{
	wchar_t *pWcs, *pCRLF;
	int nTotal, nCursor, nLineStartCharCR, nLineStartCharCRLF, nLine;

	pWcs = RichEdit_GetText();
	nTotal = (int)wcslen(pWcs);
	nCursor = RichEdit_GetCursorPosition();
	nLineStartCharCR = 0;
	nLineStartCharCRLF = 0;
	nLine = 0;
	while (nLineStartCharCRLF < nTotal)
	{
		pCRLF = wcswcs(pWcs + nLineStartCharCRLF, L"\r\n");
		int nLen = (pCRLF != NULL) ?
			(int)(pCRLF - (pWcs + nLineStartCharCRLF)) :
			(int)wcslen(pWcs + nLineStartCharCRLF);
		if (nCursor >= nLineStartCharCR && nCursor <= nLineStartCharCR + nLen)
			break;
		nLineStartCharCRLF += nLen + 2; /* +2 for CRLF */
		nLineStartCharCR += nLen + 1; /* +1 for CR */
		nLine++;
	}
	free(pWcs);

	return nLine;
}

/* リッチエディットのテキストを取得する */
static wchar_t *RichEdit_GetText(void)
{
	wchar_t *pText;
	int nTextLen;

	/* リッチエディットのテキストの長さを取得する */
	nTextLen = (int)SendMessage(hWndRichEdit, WM_GETTEXTLENGTH, 0, 0);
	if (nTextLen == 0)
	{
		pText = wcsdup(L"");
		if (pText == NULL)
		{
			log_memory();
			abort();
		}
	}

	/* テキスト全体を取得する */
	pText = malloc((size_t)(nTextLen + 1) * sizeof(wchar_t));
	if (pText == NULL)
	{
		log_memory();
		abort();
	}
	SendMessage(hWndRichEdit, WM_GETTEXT, (WPARAM)(nTextLen + 1), (LPARAM)pText);
	pText[nTextLen] = L'\0';

	return pText;
}

/* リッチエディットの書式をすべて更新する */
static VOID RichEdit_SetTextFormatAll(void)
{
	/* 全体の書式をクリアする */
	RichEdit_SetFontAll();
	RichEdit_ClearBackgroundColorAll();

	/* 行の内容により書式を設定する */
	RichEdit_SetTextColorForAllLines();

	/* 実行行の背景色を設定する */
	RichEdit_SetBackgroundColorForExecuteLine();
}

/* リッチエディットのフォントを設定する */
static VOID RichEdit_SetFontAll(void)
{
	CHARFORMAT2W cf;

	memset(&cf, 0, sizeof(cf));
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_FACE;
	wcscpy(&cf.szFaceName[0], L"BIZ UDゴシック");
	SendMessage(hWndRichEdit, EM_SETCHARFORMAT, (WPARAM)SCF_ALL, (LPARAM)&cf);
}

/* リッチエディットのテキスト全体の背景色をクリアする */
static VOID RichEdit_ClearBackgroundColorAll(void)
{
	CHARFORMAT2W cf;

	memset(&cf, 0, sizeof(cf));
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_BACKCOLOR;
	cf.crBackColor = 0x00ffffff;
	SendMessage(hWndRichEdit, EM_SETCHARFORMAT, (WPARAM)SCF_ALL, (LPARAM)&cf);
}

/* リッチエディットのテキストすべてについて、行の内容により色付けを行う */
static VOID RichEdit_SetTextColorForAllLines(void)
{
	wchar_t *pText, *pLineStop;
	int i, nLineStartCRLF, nLineStartCR, nLineLen;

	pText = RichEdit_GetText();
	nLineStartCRLF = 0;		/* WM_GETTEXTは改行をCRLFで返す */
	nLineStartCR = 0;		/* EM_EXSETSELでは改行はCRの1文字 */
	for (i = 0; i < get_line_count(); i++)
	{
		/* 行の終了位置を求める */
		pLineStop = wcswcs(pText + nLineStartCRLF, L"\r\n");
		nLineLen = pLineStop != NULL ?
			(int)(pLineStop - (pText + nLineStartCRLF)) :
			(int)wcslen(pText + nLineStartCRLF);

		/* 行の色付けを行う */
		RichEdit_SetTextColorForLine(pText, nLineStartCR, nLineStartCRLF, nLineLen);

		/* 次の行へ移動する */
		nLineStartCRLF += nLineLen + 2;	/* +2 for CRLF */
		nLineStartCR += nLineLen + 1;	/* +1 for CR */
	}
	free(pText);
}

/* 特定の行のテキスト色を設定する */
static VOID RichEdit_SetTextColorForLine(const wchar_t *pText, int nLineStartCR, int nLineStartCRLF, int nLineLen)
{
	const wchar_t *pCommandStop, *pParamStart, *pParamStop;

	/* 行を選択して選択範囲のテキスト色をデフォルトに変更する */
	RichEdit_SetSelectedRange(nLineStartCR, nLineLen);
	RichEdit_SetTextColorForSelectedRange(0x00000000);

	/* コメントを処理する */
	if (pText[nLineStartCRLF] == L'#')
	{
		/* 行全体を選択して、選択範囲のテキスト色を変更する */
		RichEdit_SetSelectedRange(nLineStartCR, nLineLen);
		RichEdit_SetTextColorForSelectedRange(0x00808080);
	}
	/* ラベルを処理する */
	else if (pText[nLineStartCRLF] == L':')
	{
		/* 行全体を選択して、選択範囲のテキスト色を変更する */
		RichEdit_SetSelectedRange(nLineStartCR, nLineLen);
		RichEdit_SetTextColorForSelectedRange(0x00ff0000);
	}
	/* エラー行を処理する */
	if (pText[nLineStartCRLF] == L'!')
	{
		/* 行全体を選択して、選択範囲のテキスト色を変更する */
		RichEdit_SetSelectedRange(nLineStartCR, nLineLen);
		RichEdit_SetTextColorForSelectedRange(0x000000ff);
	}
	/* コマンド行を処理する */
	else if (pText[nLineStartCRLF] == L'@')
	{
		int nParamLen, nNameOffset;

		/* コマンド名部分を選択してテキスト色を変更する */
		pCommandStop = wcswcs(pText + nLineStartCRLF, L" ");
		nParamLen = pCommandStop != NULL ?
			(int)(pCommandStop - (pText + nLineStartCRLF)) :
			(int)wcslen(pText + nLineStartCRLF);
		RichEdit_SetSelectedRange(nLineStartCR, nParamLen);
		RichEdit_SetTextColorForSelectedRange(0x00ff0000);

		/* 引数名を灰色にする */
		nNameOffset = 0;
		while ((pParamStart = wcswcs(pText + nLineStartCRLF + nNameOffset, L" ")) != NULL)
		{
			int nNameStart;
			int nNameLen;

			if (pParamStart >= pText + nLineStartCR + nLineLen)
				break;
			pParamStart++;
			pParamStop = wcswcs(pParamStart, L"=");
			if (pParamStop == NULL || pParamStop >= pText + nLineStartCR + nLineLen)
				break;

			/* 引数名部分を選択してテキスト色を変更する */
			nNameStart = nLineStartCR + (pParamStart - (pText + nLineStartCRLF));
			nNameLen = nLineStartCR + (pParamStop - (pText + nLineStartCRLF));
			RichEdit_SetSelectedRange(nNameStart, nNameLen);
			RichEdit_SetTextColorForSelectedRange(0x00808080);

			nNameOffset += (int)(pParamStop - (pText + nLineStartCRLF));
		}
	}
}

/* 現在のカーソル行のテキスト色を設定する */
static VOID RichEdit_SetTextColorForCursorLine(void)
{
	wchar_t *pText, *pLineStop;
	int i, nCursor, nLineStartCRLF, nLineStartCR, nLineLen;

	pText = RichEdit_GetText();
	nCursor = RichEdit_GetCursorPosition();
	nLineStartCRLF = 0;		/* WM_GETTEXTは改行をCRLFで返す */
	nLineStartCR = 0;		/* EM_EXSETSELでは改行はCRの1文字 */
	for (i = 0; i < get_line_count(); i++)
	{
		/* 行の終了位置を求める */
		pLineStop = wcswcs(pText + nLineStartCRLF, L"\r\n");
		nLineLen = pLineStop != NULL ?
			(int)(pLineStop - (pText + nLineStartCRLF)) :
			(int)wcslen(pText + nLineStartCRLF);

		if (nCursor >= nLineStartCR && nCursor <= nLineStartCR + nLineLen)
		{
			/* 行の色付けを行う */
			RichEdit_SetTextColorForLine(pText, nLineStartCR, nLineStartCRLF, nLineLen);
			break;
		}

		/* 次の行へ移動する */
		nLineStartCRLF += nLineLen + 2;	/* +2 for CRLF */
		nLineStartCR += nLineLen + 1;	/* +1 for CR */
	}
	free(pText);
}

/* 実行行の背景色を設定する */
static VOID RichEdit_SetBackgroundColorForExecuteLine(void)
{
	int nLine, nLineStart, nLineLen;

	/* 全体の背景色をクリアする */
	RichEdit_ClearBackgroundColorAll();

	/* 実行行を取得する */
	nLine = get_expanded_line_num();

	/* 実行行の開始文字と終了文字を求める */
	RichEdit_GetLineStartAndLength(nLine, &nLineStart, &nLineLen);

	/* 実行行を選択する */
	RichEdit_SetSelectedRange(nLineStart, nLineLen);

	/* 選択範囲の背景色を変更する */
	RichEdit_SetBackgroundColorForSelectedRange(0x00ffc0c0);

	/* カーソル位置を実行行の先頭に設定する */
	RichEdit_SetCursorPosition(nLineStart);

	/* リッチエディットをフォーカスする */
	SetFocus(hWndRichEdit);

	/* スクロールする */
	RichEdit_AutoScroll();
}

/* リッチエディットの選択範囲のテキスト色を変更する */
static VOID RichEdit_SetTextColorForSelectedRange(COLORREF cl)
{
	CHARFORMAT2W cf;

	memset(&cf, 0, sizeof(cf));
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_COLOR;
	cf.crTextColor = cl;
	SendMessage(hWndRichEdit, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)&cf);
}

/* リッチエディットの選択範囲の背景色を変更する */
static VOID RichEdit_SetBackgroundColorForSelectedRange(COLORREF cl)
{
	CHARFORMAT2W cf;

	memset(&cf, 0, sizeof(cf));
	cf.cbSize = sizeof(cf);
	cf.dwMask = CFM_BACKCOLOR;
	cf.crBackColor = cl;
	SendMessage(hWndRichEdit, EM_SETCHARFORMAT, (WPARAM)SCF_SELECTION, (LPARAM)&cf);
}

/* リッチエディットを自動スクロールする */
static VOID RichEdit_AutoScroll(void)
{
	SendMessage(hWndRichEdit, EM_SCROLLCARET, 0, 0);
	InvalidateRect(hWndRichEdit, NULL, TRUE);
}

/* カーソルが行頭にあるか調べる */
static BOOL RichEdit_IsLineTop(void)
{
	wchar_t *pWcs, *pCRLF;
	int i, nCursor, nLineStartCharCR, nLineStartCharCRLF;

	pWcs = RichEdit_GetText();
	nCursor = RichEdit_GetCursorPosition();
	nLineStartCharCR = 0;
	nLineStartCharCRLF = 0;
	for (i = 0; i < get_line_count(); i++)
	{
		pCRLF = wcswcs(pWcs + nLineStartCharCRLF, L"\r\n");
		int nLen = (pCRLF != NULL) ?
			(int)(pCRLF - (pWcs + nLineStartCharCRLF)) :
			(int)wcslen(pWcs + nLineStartCharCRLF);
		if (nCursor >= nLineStartCharCR && nCursor <= nLineStartCharCR + nLen)
		{
			free(pWcs);
			if (nCursor == nLineStartCharCR)
				return TRUE;
			return FALSE;
		}
		nLineStartCharCRLF += nLen + 2; /* +2 for CRLF */
		nLineStartCharCR += nLen + 1; /* +1 for CR */
	}
	free(pWcs);

	return FALSE;
}

/* カーソルが行末にあるか調べる */
static BOOL RichEdit_IsLineEnd(void)
{
	wchar_t *pWcs, *pCRLF;
	int i, nCursor, nLineStartCharCR, nLineStartCharCRLF;

	pWcs = RichEdit_GetText();
	nCursor = RichEdit_GetCursorPosition();
	nLineStartCharCR = 0;
	nLineStartCharCRLF = 0;
	for (i = 0; i < get_line_count(); i++)
	{
		pCRLF = wcswcs(pWcs + nLineStartCharCRLF, L"\r\n");
		int nLen = (pCRLF != NULL) ?
			(int)(pCRLF - (pWcs + nLineStartCharCRLF)) :
			(int)wcslen(pWcs + nLineStartCharCRLF);
		if (nCursor >= nLineStartCharCR && nCursor <= nLineStartCharCR + nLen)
		{
			free(pWcs);
			if (nCursor == nLineStartCharCR + nLen)
				return TRUE;
			return FALSE;
		}
		nLineStartCharCRLF += nLen + 2; /* +2 for CRLF */
		nLineStartCharCR += nLen + 1; /* +1 for CR */
	}
	free(pWcs);

	return FALSE;
}

/* 実行行の開始文字と終了文字を求める */
static VOID RichEdit_GetLineStartAndLength(int nLine, int *nLineStart, int *nLineLen)
{
	wchar_t *pText, *pCRLF;
	int i, nLineStartCharCRLF, nLineStartCharCR;

	pText = RichEdit_GetText();
	nLineStartCharCRLF = 0;		/* WM_GETTEXTは改行をCRLFで返す */
	nLineStartCharCR = 0;		/* EM_EXSETSELでは改行はCRの1文字 */
	for (i = 0; i < nLine; i++)
	{
		int nLen;
		pCRLF = wcswcs(pText + nLineStartCharCRLF, L"\r\n");
		nLen = pCRLF != NULL ?
			(int)(pCRLF - (pText + nLineStartCharCRLF)) :
			(int)wcslen(pText + nLineStartCharCRLF);
		nLineStartCharCRLF += nLen + 2;		/* +2 for CRLF */
		nLineStartCharCR += nLen + 1;		/* +1 for CR */
	}
	pCRLF = wcswcs(pText + nLineStartCharCRLF, L"\r\n");
	*nLineStart = nLineStartCharCR;
	*nLineLen = pCRLF != NULL ?
		(int)(pCRLF - (pText + nLineStartCharCRLF)) :
		(int)wcslen(pText + nLineStartCharCRLF);
	free(pText);
}

/*
 * リッチエディットとスクリプトモデルの対応付け
 */

/* リッチエディットのテキストをスクリプトモデルを元に設定する */
static VOID RichEdit_UpdateTextFromScriptModel(void)
{
	wchar_t *pWcs;
	int nScriptSize;
	int i;

	/* スクリプトのサイズを計算する */
	nScriptSize = 0;
	for (i = 0; i < get_line_count(); i++)
	{
		const char *pUtf8Line = get_line_string_at_line_num(i);
		nScriptSize += (int)strlen(pUtf8Line) + 1; /* +1 for CR */
	}

	/* スクリプトを格納するメモリを確保する */
	pWcs = malloc((size_t)(nScriptSize + 1) * sizeof(wchar_t));
	if (pWcs == NULL)
	{
		log_memory();
		abort();
	}

	/* 行を連列してスクリプト文字列を作成する */
	pWcs[0] = L'\0';
	for (i = 0; i < get_line_count(); i++)
	{
		const char *pUtf8Line = get_line_string_at_line_num(i);
		wcscat(pWcs, conv_utf8_to_utf16(pUtf8Line));
		wcscat(pWcs, L"\r");
	}

	/* リッチエディットにテキストを設定する */
	SetWindowText(hWndRichEdit, pWcs);

	/* メモリを解放する */
	free(pWcs);
}

/* リッチエディットの内容を元にスクリプトモデルを更新する */
static VOID RichEdit_UpdateScriptModelFromText(void)
{
	wchar_t *pWcs, *pCRLF;
	int nTotal, nLine, nLineStartCharCR, nLineStartCharCRLF;
	BOOL bExecLineChanged;

	/* パースエラーをリセットして、最初のパースエラーで通知を行う */
	dbg_reset_parse_error_count();

	/* リッチエディットのテキストの内容でスクリプトの各行をアップデートする */
	pWcs = RichEdit_GetText();
	nTotal = (int)wcslen(pWcs);
	nLine = 0;
	nLineStartCharCR = 0;
	nLineStartCharCRLF = 0;
	while (nLineStartCharCRLF < nTotal)
	{
		wchar_t *pLine;
		int nLen;

		/* 行を切り出す */
		pLine = pWcs + nLineStartCharCRLF;
		pCRLF = wcswcs(pWcs + nLineStartCharCRLF, L"\r\n");
		nLen = (pCRLF != NULL) ?
			(int)(pCRLF - (pWcs + nLineStartCharCRLF)) :
			(int)wcslen(pWcs + nLineStartCharCRLF);
		if (pCRLF != NULL)
			*pCRLF = L'\0';

		/* 行を更新する */
		update_script_line(nLine, conv_utf16_to_utf8(pLine));

		nLine++;
		nLineStartCharCRLF += nLen + 2; /* +2 for CRLF */
		nLineStartCharCR += nLen + 1; /* +1 for CR */
	}
	free(pWcs);

	/* 削除された末尾の行を処理する */
	bExecLineChanged = FALSE;
	for ( ; nLine < get_line_count(); nLine++)
	{
		if (delete_script_line(nLine))
			bExecLineChanged = TRUE;
	}
	if (bExecLineChanged)
		RichEdit_SetTextFormatAll();

	/* コマンドのパースに失敗した場合 */
	if (dbg_get_parse_error_count() > 0)
	{
		/* 行頭の'!'を反映するためにテキストを再設定する */
		RichEdit_UpdateTextFromScriptModel();
		RichEdit_SetTextFormatAll();
	}

	ValidateScriptModel();
}

static VOID ValidateScriptModel(void)
{
	wchar_t *pText, *pCRLF;
	int nTotal, nLineStartCharCRLF, nLineStartCharCR, nLine;

	pText = RichEdit_GetText();
	nTotal = (int)wcslen(pText);
	nLineStartCharCRLF = 0;		/* WM_GETTEXTは改行をCRLFで返す */
	nLineStartCharCR = 0;		/* EM_EXSETSELでは改行はCRの1文字 */
	nLine = 0;
	while (nLineStartCharCRLF < nTotal)
	{
		wchar_t *pLine = pText + nLineStartCharCRLF;
		pCRLF = wcswcs(pLine, L"\r\n");
		if (pCRLF != NULL)
			*pCRLF = L'\0';

		const char *pUtf8ScriptModelLine = get_line_string_at_line_num(nLine);
		const wchar_t *pWcsScriptModelLine = conv_utf8_to_utf16(pUtf8ScriptModelLine);
		assert(wcscmp(pLine, pWcsScriptModelLine) == 0);

		nLineStartCharCRLF += (int)wcslen(pLine) + 2;		/* +2 for CRLF */
		nLineStartCharCR += (int)wcslen(pLine) + 1;		/* +1 for CR */
		nLine++;
	}
	free(pText);
}
