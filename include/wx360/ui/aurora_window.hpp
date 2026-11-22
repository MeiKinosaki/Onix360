#pragma once
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <string>
#include <vector>
namespace wx360::ui {
struct Tile { std::wstring title; std::wstring detail; };
class AuroraWindow {
public:
 int run(HINSTANCE instance,int show) {
  WNDCLASSW wc{};wc.lpfnWndProc=window_proc;wc.hInstance=instance;wc.lpszClassName=L"WX360AuroraWindow";
  wc.hCursor=LoadCursorW(nullptr,MAKEINTRESOURCEW(32512));wc.hbrBackground=nullptr;
  RegisterClassW(&wc);instance_=instance;
  HWND window=CreateWindowExW(0,wc.lpszClassName,L"WX360 | Aurora-style Dashboard",WS_OVERLAPPEDWINDOW,
    CW_USEDEFAULT,CW_USEDEFAULT,1180,760,nullptr,nullptr,instance,this);
  if(!window)return 1;ShowWindow(window,show);UpdateWindow(window);
  MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return static_cast<int>(msg.wParam);
 }
private:
 static LRESULT CALLBACK window_proc(HWND h,UINT m,WPARAM w,LPARAM l) {
  AuroraWindow* self=reinterpret_cast<AuroraWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(m==WM_NCCREATE){auto* cs=reinterpret_cast<CREATESTRUCTW*>(l);self=static_cast<AuroraWindow*>(cs->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
  if(!self)return DefWindowProcW(h,m,w,l);
  switch(m){case WM_PAINT:self->paint(h);return 0;case WM_ERASEBKGND:return 1;case WM_DESTROY:PostQuitMessage(0);return 0;case WM_KEYDOWN:if(w==VK_ESCAPE)DestroyWindow(h);return 0;}
  return DefWindowProcW(h,m,w,l);
 }
 void paint(HWND h) {
  PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);
  HBRUSH bg=CreateSolidBrush(RGB(15,22,35));FillRect(dc,&r,bg);DeleteObject(bg);
  RECT sidebar{0,0,210,r.bottom};HBRUSH side=CreateSolidBrush(RGB(10,16,27));FillRect(dc,&sidebar,side);DeleteObject(side);
  SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(235,244,255));
  HFONT title=CreateFontW(30,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  HFONT normal=CreateFontW(18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  auto old=SelectObject(dc,title);TextOutW(dc,28,28,L"WX360",5);SelectObject(dc,normal);
  const wchar_t* nav[]={L"INICIO",L"BIBLIOTECA",L"ARQUIVOS",L"SAVE STATES",L"CONFIGURACOES"};
  for(int i=0;i<5;++i){if(i==1){RECT hi{12,105+i*48,198,145+i*48};HBRUSH accent=CreateSolidBrush(RGB(0,116,153));FillRect(dc,&hi,accent);DeleteObject(accent);}TextOutW(dc,28,112+i*48,nav[i],lstrlenW(nav[i]));}
  SelectObject(dc,title);TextOutW(dc,250,35,L"BIBLIOTECA DE JOGOS",19);
  SelectObject(dc,normal);SetTextColor(dc,RGB(130,205,225));TextOutW(dc,252,78,L"Dashboard inspirado no Aurora | WX360",39);
  RECT hero{250,125,r.right-30,275};HBRUSH panel=CreateSolidBrush(RGB(23,43,62));FillRect(dc,&hero,panel);DeleteObject(panel);
  SetTextColor(dc,RGB(255,255,255));SelectObject(dc,title);TextOutW(dc,278,155,L"SEUS JOGOS EM UM SO LUGAR",27);
  SelectObject(dc,normal);SetTextColor(dc,RGB(174,197,215));TextOutW(dc,280,205,L"Capas, favoritos, formatos ISO e XEX - interface inicial",57);
  const wchar_t* tiles[]={L"ADICIONAR JOGO",L"ULTIMO JOGO",L"SAVE STATE"};
  for(int i=0;i<3;++i){int x=250+i*250;RECT tile{x,315,x+225,510};HBRUSH t=CreateSolidBrush(RGB(27,39,56));FillRect(dc,&tile,t);DeleteObject(t);
   RECT stripe{x,315,x+225,322};HBRUSH a=CreateSolidBrush(RGB(0,166,204));FillRect(dc,&stripe,a);DeleteObject(a);
   SetTextColor(dc,RGB(240,247,255));TextOutW(dc,x+18,350,tiles[i],lstrlenW(tiles[i]));
   SetTextColor(dc,RGB(140,165,188));const wchar_t* desc=i==0?L"Adicionar ISO / XEX":(i==1?L"Nenhum jogo iniciado":L"10 slots planejados");
   TextOutW(dc,x+18,395,desc,lstrlenW(desc));
  }
  SetTextColor(dc,RGB(110,135,158));TextOutW(dc,252,r.bottom-38,L"WX360 v0.3b  |  CPU Xenon em desenvolvimento  |  ESC para sair",63);
  SelectObject(dc,old);DeleteObject(title);DeleteObject(normal);EndPaint(h,&ps);
 }
 HINSTANCE instance_{};
};
}
#endif