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
  switch(m){case WM_PAINT:self->paint(h);return 0;case WM_ERASEBKGND:return 1;case WM_DESTROY:PostQuitMessage(0);return 0;case WM_KEYDOWN:if(w==VK_ESCAPE)DestroyWindow(h);else if(w==VK_DOWN||w==VK_RIGHT){self->selected_nav_=(self->selected_nav_+1)%5;InvalidateRect(h,nullptr,FALSE);}else if(w==VK_UP||w==VK_LEFT){self->selected_nav_=(self->selected_nav_+4)%5;InvalidateRect(h,nullptr,FALSE);}return 0;case WM_LBUTTONDOWN:{int y=GET_Y_LPARAM(l);if(y>=105&&y<345){int index=(y-105)/48;if(index>=0&&index<5){self->selected_nav_=index;InvalidateRect(h,nullptr,FALSE);}}return 0;}}
  return DefWindowProcW(h,m,w,l);
 }
 void draw_text(HDC dc,int x,int y,const wchar_t* value,COLORREF color) {
  SetTextColor(dc,color);TextOutW(dc,x,y,value,lstrlenW(value));
 }
 void panel(HDC dc,int x,int y,int width,int height,COLORREF color) {
  RECT box{x,y,x+width,y+height};HBRUSH brush=CreateSolidBrush(color);FillRect(dc,&box,brush);DeleteObject(brush);
 }
 void paint(HWND h) {
  PAINTSTRUCT ps{};HDC dc=BeginPaint(h,&ps);RECT r{};GetClientRect(h,&r);
  panel(dc,0,0,r.right,r.bottom,RGB(15,22,35));
  panel(dc,0,0,210,r.bottom,RGB(10,16,27));
  SetBkMode(dc,TRANSPARENT);
  HFONT title=CreateFontW(30,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  HFONT normal=CreateFontW(18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  HGDIOBJ old=SelectObject(dc,title);draw_text(dc,28,28,L"WX360",RGB(235,244,255));SelectObject(dc,normal);
  const wchar_t* nav[]={L"INICIO",L"BIBLIOTECA",L"ARQUIVOS",L"SAVE STATES",L"CONFIGURACOES"};
  for(int i=0;i<5;++i){if(i==selected_nav_)panel(dc,12,105+i*48,186,40,RGB(0,116,153));draw_text(dc,28,112+i*48,nav[i],RGB(235,244,255));}
  const wchar_t* headings[]={L"INICIO",L"BIBLIOTECA DE JOGOS",L"ARQUIVOS DE JOGOS",L"SAVE STATES",L"CONFIGURACOES"};
  draw_text(dc,250,35,headings[selected_nav_],RGB(255,255,255));
  draw_text(dc,252,78,L"Dashboard inspirado no Aurora | WX360",RGB(130,205,225));
  panel(dc,250,125,r.right-280,135,RGB(23,43,62));
  if(selected_nav_==0){
   draw_text(dc,278,155,L"BEM-VINDO AO WX360",RGB(255,255,255));
   draw_text(dc,280,205,L"Emulador experimental Xbox 360 para Windows x64",RGB(174,197,215));
   const wchar_t* cards[]={L"BIBLIOTECA",L"FORMATOS",L"SAVE STATES"};
   const wchar_t* details[]={L"Gerencie sua colecao",L"ISO e XEX em desenvolvimento",L"10 slots CPU/RAM"};
   for(int i=0;i<3;++i){int x=250+i*250;panel(dc,x,300,225,170,RGB(27,39,56));panel(dc,x,300,225,7,RGB(0,166,204));draw_text(dc,x+18,340,cards[i],RGB(240,247,255));draw_text(dc,x+18,385,details[i],RGB(140,165,188));}
  } else if(selected_nav_==1){
   draw_text(dc,278,155,L"SUA BIBLIOTECA",RGB(255,255,255));
   draw_text(dc,280,205,L"Nenhum jogo foi indexado nesta versao.",RGB(174,197,215));
   draw_text(dc,252,300,L"Os formatos ISO/XEX ainda precisam ser ligados ao catalogo da interface.",RGB(190,205,220));
   draw_text(dc,252,340,L"As capas serao exibidas aqui quando a identificacao e o catalogo estiverem integrados.",RGB(140,165,188));
  } else if(selected_nav_==2){
   draw_text(dc,278,155,L"FONTES DE JOGOS",RGB(255,255,255));
   draw_text(dc,280,205,L"Formatos planejados: ISO, XEX e GOD.",RGB(174,197,215));
   draw_text(dc,252,300,L"Importacao e varredura de pastas ainda nao estao conectadas.",RGB(190,205,220));
   draw_text(dc,252,340,L"Esta tela e informativa; nenhum arquivo sera aberto por enquanto.",RGB(140,165,188));
  } else if(selected_nav_==3){
   draw_text(dc,278,155,L"10 SLOTS DISPONIVEIS NO GERENCIADOR",RGB(255,255,255));
   draw_text(dc,280,205,L"Save State atual: CPU Xenon e RAM.",RGB(174,197,215));
   for(int i=0;i<10;++i){int col=i%5,row=i/5,x=250+col*170,y=300+row*105;panel(dc,x,y,150,78,RGB(27,39,56));draw_text(dc,x+14,y+18,(std::wstring(L"SLOT ")+std::to_wstring(i+1)).c_str(),RGB(240,247,255));draw_text(dc,x+14,y+46,L"Sem dados conectados",RGB(140,165,188));}
  } else {
   draw_text(dc,278,155,L"COMPATIBILIDADE E DESEMPENHO",RGB(255,255,255));
   draw_text(dc,280,205,L"Baseline x86-64 / SSE2; AVX2 nao e obrigatorio.",RGB(174,197,215));
   draw_text(dc,252,300,L"Perfil de memoria, renderer grafico e configuracoes por jogo ainda pendentes.",RGB(190,205,220));
   draw_text(dc,252,340,L"Meta: PCs Windows 10/11 com ate 4 GB RAM (sem garantia de jogos completos).",RGB(140,165,188));
  }
  draw_text(dc,252,r.bottom-38,L"WX360 v0.3b | CPU Xenon em desenvolvimento | ESC para sair",RGB(110,135,158));
  SelectObject(dc,old);DeleteObject(title);DeleteObject(normal);EndPaint(h,&ps);
 }
 int selected_nav_=1;
 HINSTANCE instance_{};
};
}
#endif