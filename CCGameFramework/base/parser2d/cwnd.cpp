﻿//
// Project: CParser
// Created by bajdcc
//

#include "stdafx.h"
#include "cvm.h"
#include "cwnd.h"

namespace clib {

    vfs_node_stream_window::vfs_node_stream_window(const vfs_mod_query* mod, vfs_stream_t s, vfs_stream_call* call, int id) :
        vfs_node_dec(mod), call(call) {
        wnd = call->stream_getwnd(id);
    }

    vfs_node_stream_window::~vfs_node_stream_window()
    {
    }

    vfs_node_dec* vfs_node_stream_window::create(const vfs_mod_query* mod, vfs_stream_t s, vfs_stream_call* call, const string_t& path)
    {
        static string_t pat{ R"(/handle/([0-9]+)/message)" };
        static std::regex re(pat);
        std::smatch res;
        if (std::regex_match(path, res, re)) {
            auto id = atoi(res[1].str().c_str());
            return new vfs_node_stream_window(mod, s, call, id);
        }
        return nullptr;
    }

    bool vfs_node_stream_window::available() const {
        return wnd->get_state() != cwindow::W_CLOSING;
    }

    int vfs_node_stream_window::index() const {
        if (!available())return READ_EOF;
        auto d = wnd->get_msg_data();
        if (d == -1)
            return DELAY_CHAR;
        return (int)d;
    }

    void vfs_node_stream_window::advance() {

    }

    int vfs_node_stream_window::write(byte c) {
        return -1;
    }

    int vfs_node_stream_window::truncate() {
        return -1;
    }

    int sys_cursor(int cx, int cy) {
        using CT = Window::CursorType;
        static int curs[] = {
            CT::size_topleft, CT::size_left, CT::size_topright,
            CT::size_top, CT::arrow, CT::size_top,
            CT::size_topright, CT::size_left, CT::size_topleft,
        };
        return curs[(cx + 1) * 3 + (cy + 1)];
    }

    cwindow::cwindow(int handle, const string_t& caption, const CRect& location)
        : handle(handle), caption(caption), location(location), self_min_size(100, 40)
    {
        auto bg = SolidBackgroundElement::Create();
        bg->SetColor(CColor(Gdiplus::Color::White));
        root = bg;
        root->SetRenderRect(location);
        renderTarget = std::make_shared<Direct2DRenderTarget>(window->shared_from_this());
        renderTarget->Init();
        init();
        root->GetRenderer()->SetRenderTarget(renderTarget);
    }

    cwindow::~cwindow()
    {
        auto& focus = cvm::global_state.window_focus;
        auto& hover = cvm::global_state.window_hover;
        if (focus == handle)
            focus = -1;
        if (hover == handle)
            hover = -1;
    }

    void cwindow::init(cvm* vm)
    {
        const auto& s = location;
        hit(vm, 200, s.left + 1, s.top + 1);
        hit(vm, 211, s.left + 1, s.top + 1);
        hit(vm, 201, s.left + 1, s.top + 1);
    }

    void cwindow::paint(const CRect& bounds)
    {
        if (bounds1 != bounds || need_repaint) {
            need_repaint = false;
            CRect rt;
            root->SetRenderRect(location.OfRect(bounds));
            rt = bag.title->GetRenderRect();
            rt.right = root->GetRenderRect().Width();
            bag.title->SetRenderRect(rt);
            rt = bag.close_text->GetRenderRect();
            rt.left = root->GetRenderRect().Width() - 20;
            rt.right = root->GetRenderRect().Width();
            bag.close_text->SetRenderRect(rt);
            bounds1 = bounds;
            rt.left = rt.top = 0;
            rt.right = bounds.Width();
            rt.bottom = bounds.Height();
            bag.border->SetRenderRect(rt);
        }
        root->GetRenderer()->Render(root->GetRenderRect());
    }

#define WM_MOUSEENTER 0x2A5
    bool cwindow::hit(cvm* vm, int n, int x, int y)
    {
        static int mapnc[] = {
            WM_NCLBUTTONDOWN, WM_NCLBUTTONUP, WM_NCLBUTTONDBLCLK,
            WM_NCRBUTTONDOWN, WM_NCRBUTTONUP, WM_NCRBUTTONDBLCLK,
            WM_NCMBUTTONDOWN, WM_NCMBUTTONUP, WM_NCMBUTTONDBLCLK,
            WM_SETFOCUS, WM_KILLFOCUS,
            WM_NCMOUSEMOVE, 0x2A4, WM_NCMOUSELEAVE, WM_NCMOUSEHOVER
        };
        static int mapc[] = {
            WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK,
            WM_RBUTTONDOWN, WM_RBUTTONUP, WM_RBUTTONDBLCLK,
            WM_MBUTTONDOWN, WM_MBUTTONUP, WM_MBUTTONDBLCLK,
            WM_SETFOCUS, WM_KILLFOCUS,
            WM_MOUSEMOVE, WM_MOUSEENTER, WM_MOUSELEAVE, WM_MOUSEHOVER
        };
        auto pt = CPoint(x, y) + -root->GetRenderRect().TopLeft();
        if (self_drag && n == 211 && cvm::global_state.window_hover == handle) {
            post_data(WM_MOVING, x, y); // 发送本窗口MOVNG
            return true;
        }
        if (self_size && n == 211 && cvm::global_state.window_hover == handle) {
            post_data(WM_SIZING, x, y); // 发送本窗口MOVNG
            return true;
        }
        if (pt.x < 0 || pt.y < 0 || pt.x >= location.Width() || pt.y >= location.Height())
        {
            if (!(self_size || self_drag))
                return false;
        }
        if (n == 200) {
            if (bag.close_text->GetRenderRect().PtInRect(pt)) {
                post_data(WM_CLOSE);
                return true;
            }
            int cx, cy;
            self_size = is_border(pt, cx, cy);
            if (self_size) {
                post_data(WM_MOVE, x, y);
                post_data(WM_SIZE, cx, cy);
            }
        }
        int code = -1;
        if (n >= 200)
            n -= 200;
        else
            n += 3;
        if (n <= 11 && bag.title->GetRenderRect().PtInRect(pt)) {
            code = mapnc[n];
            if (n == 0) {
                post_data(WM_MOVE, x, y); // 发送本窗口MOVE
            }
        }
        else {
            pt.y -= bag.title->GetRenderRect().Height();
            code = mapc[n];
        }
        post_data(code, pt.x, pt.y);
        if (n == 11) {
            post_data(WM_SETCURSOR, pt.x, pt.y);
            auto& hover = cvm::global_state.window_hover;
            if (hover != -1) { // 当前有鼠标悬停
                if (hover != handle) { // 非当前窗口
                    vm->post_data(hover, WM_MOUSELEAVE); // 给它发送MOUSELEAVE
                    hover = handle; // 置为当前窗口
                    post_data(WM_MOUSEENTER); // 发送本窗口MOUSEENTER
                }
                else if (self_drag) {
                    post_data(WM_MOVING, x, y); // 发送本窗口MOVNG
                }
            }
            else { // 当前没有鼠标悬停
                hover = handle;
                post_data(WM_MOUSEENTER);
            }
        }
        else if (n == 0) {
            auto& focus = cvm::global_state.window_focus;
            if (focus != -1) { // 当前有焦点
                if (focus != handle) { // 非当前窗口
                    vm->post_data(focus, WM_KILLFOCUS); // 给它发送KILLFOCUS
                    focus = handle; // 置为当前窗口
                    vm->post_data(focus, WM_SETFOCUS); // 发送本窗口SETFOCUS
                }
            }
            else { // 当前没有焦点
                focus = handle;
                vm->post_data(focus, WM_SETFOCUS);
            }
        }
        return true;
    }

    cwindow::window_state_t cwindow::get_state() const
    {
        return state;
    }

    int cwindow::get_msg_data()
    {
        if (msg_data.empty())
            return - 1;
        auto d = msg_data.front();
        msg_data.pop();
        return d;
    }

    int cwindow::get_cursor() const
    {
        return cursor;
    }

    int cwindow::handle_msg(cvm* vm, const window_msg& msg)
    {
        switch (msg.code)
        {
        case WM_CLOSE:
            state = W_CLOSING;
            break;
        case WM_SETTEXT:
            caption = vm->vmm_getstr(msg.param1);
            bag.title_text->SetText(CString(CStringA(caption.c_str())));
            break;
        case WM_GETTEXT:
            vm->vmm_setstr(msg.param1, caption);
            break;
        case WM_MOVE:
            self_drag_pt.x = msg.param1;
            self_drag_pt.y = msg.param2;
            self_drag_rt = location;
            break;
        case WM_MOVING:
            location = self_drag_rt;
            location.OffsetRect((LONG)msg.param1 - self_drag_pt.x, (LONG)msg.param2 - self_drag_pt.y);
            need_repaint = true;
            break;
        case WM_SIZE:
            self_size_pt.x = (LONG)msg.param1;
            self_size_pt.y = (LONG)msg.param2;
            break;
        case WM_SIZING:
            location = self_drag_rt;
            if (self_size_pt.x == 1) {
                location.right = __max(location.left + self_min_size.cx, location.right + (LONG)msg.param1 - self_drag_pt.x);
            }
            if (self_size_pt.x == -1) {
                location.left = __min(location.right - self_min_size.cx, location.left + (LONG)msg.param1 - self_drag_pt.x);
            }
            if (self_size_pt.y == 1) {
                location.bottom = __max(location.top + self_min_size.cy, location.bottom + (LONG)msg.param2 - self_drag_pt.y);
            }
            if (self_size_pt.y == -1) {
                location.top = __min(location.bottom - self_min_size.cy, location.top + (LONG)msg.param2 - self_drag_pt.y);
            }
            need_repaint = true;
            break;
        case WM_SETCURSOR:
        {
        }
            break;
        default:
            break;
        }
        return 0;
    }

    void cwindow::init()
    {
        auto r = root->GetRenderRect();
        auto& list = root->GetChildren();
        auto title = SolidBackgroundElement::Create();
        title->SetColor(CColor(45, 120, 213));
        title->SetRenderRect(CRect(0, 0, r.Width(), 30));
        bag.title = title;
        list.push_back(title);
        root->GetRenderer()->SetRelativePosition(true);
        auto title_text = SolidLabelElement::Create();
        title_text->SetColor(CColor(Gdiplus::Color::White));
        title_text->SetRenderRect(CRect(5, 2, r.Width(), 30));
        title_text->SetText(CString(CStringA(caption.c_str())));
        bag.title_text = title_text;
        Font f;
        f.size = 20;
        f.fontFamily = "微软雅黑";
        title_text->SetFont(f);
        list.push_back(title_text);
        auto close_text = SolidLabelElement::Create();
        close_text->SetColor(CColor(Gdiplus::Color::White));
        close_text->SetRenderRect(CRect(r.Width() - 20, 2, r.Width(), 30));
        close_text->SetText(_T("×"));
        close_text->SetFont(f);
        bag.close_text = close_text;
        list.push_back(close_text);
        auto border = RoundBorderElement::Create();
        border->SetColor(CColor(36, 125, 234));
        border->SetFill(false);
        border->SetRadius(0.0f);
        border->SetRenderRect(r);
        bag.border = border;
        list.push_back(border);
    }

    bool cwindow::is_border(const CPoint& pt, int& cx, int& cy)
    {
        auto suc = false;
        cx = cy = 0;
        const int border_len = 5;
        if (abs(pt.x) <= border_len) {
            suc = true;
            cx = -1;
        }
        if (abs(pt.y) <= border_len) {
            suc = true;
            cy = -1;
        }
        if (abs(pt.x - location.Width()) <= border_len) {
            suc = true;
            cx = 1;
        }
        if (abs(pt.y - location.Height()) <= border_len) {
            suc = true;
            cy = 1;
        }
        return suc;
    }

    void cwindow::post_data(const int& code, int param1, int param2)
    {
        if (code == WM_MOUSEENTER)
        {
            bag.border->SetColor(CColor(36, 125, 234));
            self_hovered = true;
        }
        else if (code == WM_MOUSELEAVE)
        {
            bag.border->SetColor(CColor(149, 187, 234));
            self_hovered = false;
            self_drag = false;
            self_size = false;
        }
        else if (code == WM_SETFOCUS)
        {
            bag.title->SetColor(CColor(45, 120, 213));
            self_focused = true;
        }
        else if (code == WM_KILLFOCUS)
        {
            bag.title->SetColor(CColor(149, 187, 234));
            self_focused = false;
            self_drag = false;
            self_size = false;
        }
        else if (code == WM_NCLBUTTONDOWN)
        {
            int cx, cy;
            if (is_border(CPoint(param1, param2), cx, cy)) {
                self_size = true;
            }
            else {
                self_drag = true;
            }
        }
        else if (code == WM_LBUTTONDOWN)
        {
            int cx, cy;
            if (is_border(CPoint(param1, param2 + bag.title->GetRenderRect().Height()), cx, cy)) {
                self_size = true;
            }
        }
        else if (code == WM_MOUSEMOVE)
        {
            int cx, cy;
            if (is_border(CPoint(param1, param2 + bag.title->GetRenderRect().Height()), cx, cy)) {
                cursor = sys_cursor(cx, cy);
            }
            else {
                cursor = 1;
            }
        }
        else if (code == WM_NCMOUSEMOVE)
        {
            int cx, cy;
            if (is_border(CPoint(param1, param2), cx, cy)) {
                cursor = sys_cursor(cx, cy);
            }
            else {
                cursor = 1;
            }
        }
        else if (code == WM_NCLBUTTONUP || code == WM_LBUTTONUP)
        {
            self_drag = false;
            if (self_size) {
                self_size = false;
                cursor = 1;
            }
        }
        window_msg s{ code, (uint32)param1, (uint32)param2 };
        const auto p = (byte*)& s;
        for (auto i = 0; i < sizeof(window_msg); i++) {
            msg_data.push(p[i]);
        }
    }
}