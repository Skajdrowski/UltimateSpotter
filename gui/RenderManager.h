#pragma once
#include "../dxsdk/include/d3dx8.h"

namespace Render
{
	void Initialise(LPDIRECT3DDEVICE8 pDevice);

	void Draw(LPDIRECT3DDEVICE8 pDevice, int x, int y, int w, int h, D3DCOLOR color);
	void Outline(LPDIRECT3DDEVICE8 pDevice, int x, int y, int w, int h, D3DCOLOR color);
	void DrawSquare(LPDIRECT3DDEVICE8 pDevice, int x, int y, int size, D3DCOLOR color);

	namespace Fonts
	{
		extern LPD3DXFONT Menu;
		extern LPD3DXFONT MenuBold;
		extern LPD3DXFONT MenuText;
		extern LPD3DXFONT MenuTabs;
		extern LPD3DXFONT Tabs;
	};

	void Text(LPD3DXFONT pFont, int x, int y, D3DCOLOR color, const char* text, DWORD format = 0);
	void TextW(LPD3DXFONT pFont, int x, int y, D3DCOLOR color, const wchar_t* text, DWORD format = 0);
	void TextOutlined(LPD3DXFONT pFont, int x, int y, D3DCOLOR color, const char* text,
		D3DCOLOR outlineColor, int thickness = 1, DWORD format = 0);
	void TextWOutlined(LPD3DXFONT pFont, int x, int y, D3DCOLOR color, const wchar_t* text,
		D3DCOLOR outlineColor, int thickness = 1, DWORD format = 0);
	RECT GetTextSize(LPD3DXFONT pFont, const char* text);
	RECT GetViewport(LPDIRECT3DDEVICE8 pDevice);
	void Shutdown();
};
