// Copyright (c) NetXS Group.
// Licensed under the MIT license.

#ifndef NETXS_TERMINAL_HPP
#define NETXS_TERMINAL_HPP

#include "../ui/controls.hpp"
#include "../os/system.hpp"

namespace netxs::ui
{
    class rods // terminal: scrollback/altbuf internals v2 (New concept of the buffer)
        : public flow
    {
        //template<class T>
        //struct ring // rods: ring buffer
        //{
        //    using buff = std::vector<sptr<T>>;
        //    buff heap; // ring: Inner container.
        //    iota size; // ring: Limit of the ring buffer (-1: unlimited)
        //    iota head;
        //    iota tail;
        //    ring(iota size = -1)
        //        : size{ size }
        //    { }
        //};
        //
        //using heap = ring<para>;
        //heap batch;
    protected:
        using parx = sptr<para>;
        struct line
        {
            iota  master; // rod: Top visible text line index.
            iota  selfid; // rod: Text line index.
            bias  adjust;
            iota  length;
            bool  wrapln;
            parx  stanza;
            cell& marker;

            line(iota selfid,
                 cell const& brush = {},
                 bias adjust = bias::left,
                 bool wrapln = WRAPPING,
                 iota length = 0)
                : master { selfid },
                  selfid { selfid },
                  adjust { adjust },
                  length { length },
                  wrapln { wrapln },
                  stanza { std::make_shared<para>(brush) },
                  marker { stanza->brush }
            { }
            line(iota selfid,
                 line const& proto)
                : master { selfid },
                  selfid { selfid },
                  adjust { proto.adjust },
                  wrapln { proto.wrapln },
                  length { 0 },
                  stanza { std::make_shared<para>(proto.marker) },
                  marker { stanza->brush }
            { }

            void set(line&& l)
            {
                //master = l.selfid;
                //selfid = l.selfid;
                adjust = l.adjust;
                length = l.length;
                wrapln = l.wrapln;
                stanza = std::move(l.stanza);
                l.stanza = std::make_shared<para>();
            }
            auto line_height(iota width)
            {
                if (wrapln)
                {
                    auto len = length;
                    if (len && (len % width == 0)) len--;
                    return len / width + 1;
                }
                else return 1;
            }
            void cook(cell const& spare)
            {
                auto& item = *stanza;
                item.cook();
                item.trim(spare);
                auto size = item.size();
                length = size.x;
                //todo revise: height is always =1
                //height = size.y;

                //todo update wrapln and adjust
                //adjust = item...;
                //wrapln = item...;
            }
            // Make the line no longer than maxlen
            void trim_to(iota max_len)
            {
                auto& item = *stanza;
                item.trim_to(max_len);
                auto size = item.size();
                length = size.x;
                //todo revise: height is always =1
                //height = size.y;
            }
        };
        //todo implement as a ring buffer of size MAX_SCROLLBACK
        //using heap = std::list<line>;
        using heap = std::vector<line>;
        using iter = heap::iterator;
        iota        parid; // rods: Last used paragraph id.
        heap        batch; // rods: Rods inner container.
        iota const& width; // rods: Viewport width.
        iota const& viewport_height; // rods: Viewport height.
        iota        count; // rods: Scrollback height (& =batch.size()).
        side&       upset; // rods: Viewport oversize.
        twod        coord; // rods: Actual caret position.
        parx        caret; // rods: Active insertion point.
        cell&       spare; // rods: Shared current brush state (default brush).
        cell        brush; // rods: Last used brush.
        iota current_para; // rods: Active insertion point index (not id).
        //iter current_para_it; // rods: Active insertion point index (not id).
        iota        basis; // rods: Index of O(0, 0).

        iota scroll_top    = 0; // rods: Scrolling region top. 1-based, 0: use top of viewport
        iota scroll_bottom = 0; // rods: Scrolling region bottom. 1-based, 0: use bottom of viewport

    public:
        //todo unify
        bool        caret_visible = faux;

        rods(twod& anker, side& oversize, twod const& viewport_size, cell& spare)
            : flow { viewport_size.x, count   },
              parid{ 0                        },
              batch{ line(parid)              },
              width{ viewport_size.x          },
              count{ 1                        },
              upset{ oversize                 },
              caret{ batch.front().stanza     },
              basis{ 0                        },
              viewport_height{ viewport_size.y},
              spare{ spare                    },
              //current_para_it{batch.begin()},
              current_para{ 0 }
        { }
        auto get_scroll_limits()
        {
            auto top = scroll_top    ? scroll_top - 1
                                     : 0;
            auto end = scroll_bottom ? scroll_bottom - 1
                                     : viewport_height - 1;
            end = std::clamp(end, 0, viewport_height - 1);
            top = std::clamp(top, 0, end);
            return std::pair{ top, end };
        }
        bool inside_scroll_region()
        {
            auto[top, end] = get_scroll_limits();
            return coord.y >= top && coord.y <= end;
        }
        //todo revise
        void color(cell const& c)
        {
            caret->brush = c;
            brush = c;
        }
        void set_coord(twod new_coord)
        {
            // Checking bottom boundary
            auto min_y = -basis;
            new_coord.y = std::max(new_coord.y, min_y);

            coord = new_coord;
            new_coord.y += basis; // set coord inside batch

            if (new_coord.y > count - 1) // Add new lines
            {
                auto add_count = new_coord.y - (count - 1);
                auto brush = caret->brush;

                auto& item = batch[current_para];
                auto wrp = item.wrapln;
                auto jet = item.adjust;
                while(add_count-- > 0 ) //todo same as in finalize()
                {
                    auto& new_item = batch.emplace_back(++parid);
                    auto new_line = new_item.stanza;
                    new_line->brush = brush;
                    new_item.wrapln = wrp;
                    new_item.adjust = jet;

                    count++;
                    if (count - basis > viewport_height) basis = count - viewport_height;
                }
            }

            auto& line = batch[new_coord.y];
            auto line_len = line.length;

            auto index = line.selfid - batch.front().selfid; // current index inside batch
            brush = caret->brush;
            if (line.selfid == line.master) // no overlapped lines
            {
                caret = batch[index].stanza;
                caret->chx(new_coord.x);
            }
            else
            {
                auto delta = line.selfid - line.master; // master always less or eq selfid
                index -= delta;
                caret = batch[index].stanza;
                caret->chx(delta * width + new_coord.x);
                //todo implement checking hit by x
            }
            //todo implement the case when the coord is set to the outside viewport
            //     after the right side: disable wrapping (on overlapped line too)
            //     before the left side: disable wrapping + bias::right (on overlapped line too)

            current_para = index;
            caret->brush = brush;
        }
        void set_coord() { set_coord(coord); }
        auto get_line_index_by_id(iota id)
        {
                auto begin = batch.front().selfid;
                auto index = id - begin;
                return index;
        }
        void align_basis()
        {
            auto new_basis = count - viewport_height;
            if (new_basis > basis) basis = new_basis; // Move basis down if scrollback grows
        }
        template<class P>
        void for_each(iota from, iota upto, P proc)
        {
            auto head = batch.begin() + from;
            auto tail = batch.begin() + upto;
            if (from < upto) while(proc(*head) && head++ != tail);
            else             while(proc(*head) && head-- != tail);
        }
        void add_empty_lines(iota amount, line const& proto)
        {
            count += amount;
            while(amount-- > 0 ) batch.emplace_back(++parid, proto);
            align_basis();
        }
        void finalize()
        {
            auto point = batch.begin() + current_para;
            auto& cur_line = *point;

            auto old_height = cur_line.line_height(width);
            cur_line.cook(spare);
            auto new_height = cur_line.line_height(width);

            if (new_height > old_height)
            {
                auto overflow = current_para + new_height - count;
                if (overflow > 0)
                {
                    add_empty_lines(overflow, cur_line);
                    point = batch.begin() + current_para; // point is invalidated due to batch (std::vector) resize
                }
                // Update master id on overlapped below lines
                auto from = current_para + new_height - 1;
                auto upto = current_para + old_height;
                for_each(from, upto, [&](line& l)
                {
                    auto open = l.master - cur_line.selfid > 0;
                    if  (open)  l.master = cur_line.selfid;
                    return open;
                });
            }
            else if (new_height < old_height)
            {
                // Update master id on opened below lines
                auto from = current_para + old_height - 1;
                auto upto = current_para + new_height;
                for_each(from, upto, [&](line& l)
                {
                    if (l.master != cur_line.selfid) return faux; // this line, like all those lying above, is covered with a long super line above, don't touch it
                    // Looking for who else covers the current line below
                    auto from2 = current_para + 1;
                    auto upto2 = from--;
                    for_each(from2, upto2, [&](line& l2)
                    {
                        auto h = l2.line_height(width);
                        auto d = upto2 - from2++;
                        if (h > d)
                        {
                            l.master = l2.selfid;
                            return faux;
                        }
                        return true;
                    });
                    return true;
                });
            }

            auto pos = twod{ caret->chx(), current_para - basis };
            if (cur_line.wrapln)
            {
                if (pos.x && pos.x == cur_line.length && (pos.x % width == 0))
                {
                    pos.x--;
                    coord.x = width;
                }
                else
                {
                    coord.x = pos.x % width;
                }
                coord.y = pos.x / width + pos.y;
            }
            else
            {
                coord = pos;
            }

            //todo update flow::minmax and base::upset
        }
        auto& clear(bool preserve_brush = faux)
        {
            brush = preserve_brush ? caret->brush
                                   : cell{};
            batch.clear();
            parid = 0;
            count = 0;
            basis = 0;
            upset.set(0);
            caret = batch.emplace_back(parid, brush).stanza;
            current_para = count++;
            align_basis();
            return *this;
        }
        // rods: Add new line.
        void fork()
        {
            finalize();
            auto& item = batch[current_para];
            caret = batch.emplace_back(++parid, item).stanza;
            current_para = count++;
            align_basis();
        }
        auto cp()
        {
            auto pos = coord;
            pos.y += basis;
            if (pos.x == width
                && batch[current_para].wrapln)
            {
                pos.x = 0;
                pos.y++;
            }
            return pos;
        }
        auto reflow()
        {
            //todo recalc overlapping master id's over selfid's on resize

            flow::reset();
            //todo Determine page internal caret position
            //auto current_coord = dot_00;
            // Output lines in backward order from bottom to top
            auto tail = batch.rbegin();
            auto head = batch.rend();
            auto coor = twod{ 0, count };
            //todo optimize: print only visible (TIA canvas offsets)
            while(tail != head)
            {
                auto& line = *tail++;
                auto& rod = *line.stanza;
                --coor.y;
                flow::cursor = coor;
                flow::wrapln = line.wrapln;
                flow::adjust = line.adjust;
                flow::up(); // Append empty lines to flow::minmax
                flow::print<faux>(rod);
                brush = rod.mark(); // current mark of the last printed fragment
            }

            if (caret_visible) flow::minmax(cp()); // Register current caret position
            auto& cover = flow::minmax();
            upset.set(-std::min(0, cover.l),
                       std::max(0, cover.r - width + 1),
                      -std::min(0, cover.t),
                       0);
            auto height = cover.height() + 1;
            if (auto delta = height - count)
            {
                while(delta-- > 0 ) fork();
            }

            //auto h_test = cover.height() + 1;
            //if (h_test != count)
            //{
            //    log("h_test != count (", h_test, ", ", count, ")");
            //}

            // Register viewport
            auto viewport = rect{{ 0, basis }, { width, viewport_height }};
            flow::minmax(viewport);
            height = cover.height() + 1;

            align_basis();
            //todo merge with main loop here
            rebuild_upto_id(batch.front().selfid);
            //log("ok");

            return twod{ width, height };
        }
        void output(face& canvas)
        {
            //todo do the same that the reflow does but using canvas

            flow::reset();
            flow::corner = canvas.corner();

            // Output lines in backward order from bottom to top
            auto tail = batch.rbegin();
            auto head = batch.rend();
            auto coor = twod{ 0, count };
            //todo optimize: print only visible (TIA canvas offsets)
            while(tail != head)
            {
                auto& line = *tail++;
                auto& rod = *line.stanza;
                --coor.y;
                flow::cursor = coor;
                flow::wrapln = line.wrapln;
                flow::adjust = line.adjust;
                flow::print<faux>(rod, canvas);
                brush = rod.mark(); // current mark of the last printed fragment
            }
        }
        auto height() const
        {
            return count;
        }
        void height(iota limits)
        {
            //todo resize ring buffer
        }
        void remove_empties()
        {
            auto head = batch.begin();
            auto tail = std::prev(batch.end()); // Exclude the first line
            while(head != tail)
            {
                auto& line = *tail--;
                if (line.length == 0)
                {
                    batch.pop_back();
                    count--;
                    parid--;
                }
                else break;
            }
            if (current_para >= count)
            {
                current_para = count - 1;
                caret = batch[current_para].stanza;
            }
        }
        // rods: Cut all lines above and current line
        void cut_above()
        {
            auto cur_line_it = batch.begin() + current_para;
            auto master_id = cur_line_it->master;
            auto mas_index = get_line_index_by_id(master_id);
            auto head = batch.begin() + mas_index;
            auto tail = cur_line_it;
            do
            {
                auto required_len = (current_para - mas_index) * width + coord.x; // todo optimize
                auto& line = *head;
                line.trim_to(required_len);
                mas_index++;
            }
            while(head++ != tail);
        }
        // rods: Rebuild overlaps from bottom to line with selfid=top_id (inclusive)
        void rebuild_upto_id(iota top_id)
        {
            //log("top_id= ", top_id, " count= ", count, " batch.size=", batch.size());
            auto head = batch.rbegin();
            auto tail = head + count - get_line_index_by_id(top_id) - 1;
            do
            {
                auto& line =*head;
                auto below = head - (line.line_height(width) - 1);
                do  // Assign iff line isn't overlapped by somaething higher
                {   // Comparing the difference with zero In order to support id incrementing overflow
                    if (below->master - top_id > 0) below->master = line.selfid;
                    else                            break; // overlapped by a higher line
                }
                while(head != below++);
            }
            while(head++ != tail);
        }
        // for bug testing
        auto get_content()
        {
            text yield;
            auto i = 0;
            for(auto& l : batch)
            {
                yield += "\n =>" + std::to_string(i++) + "<= ";
                yield += l.stanza->get_utf8();
            }
            return yield;
        }
    };

    class term // terminal: Built-in terminal app
        : public base
    {
        using self = term;

        #ifndef DEMO
        FEATURE(pro::keybd, keybd); // term: Keyboard controller.
        #endif
        FEATURE(pro::caret, caret); // term: Caret controller.
        FEATURE(pro::mouse, mouse); // term: Mouse controller.

        struct commands
        {
            struct erase
            {
                struct line
                {
                    enum : int
                    {
                        right = 0,
                        left  = 1,
                        all   = 2,
                    };
                };
                struct display
                {
                    enum : int
                    {
                        below      = 0,
                        above      = 1,
                        viewport   = 2,
                        scrollback = 3,
                    };
                };
            };
        };

        struct wall // term: VT-behavior for the rods
            : public rods
        {
            template<class T>
            struct parser : public ansi::parser<T>
            {
                using vt = ansi::parser<T>;
                parser() : vt()
                {
                    using namespace netxs::console::ansi;
                    vt::csier.table[CSI_CUU] = VT_PROC{ p->up ( q(1)); };  // CSI n A
                    vt::csier.table[CSI_CUD] = VT_PROC{ p->dn ( q(1)); };  // CSI n B
                    vt::csier.table[CSI_CUF] = VT_PROC{ p->cuf( q(1)); };  // CSI n C
                    vt::csier.table[CSI_CUB] = VT_PROC{ p->cuf(-q(1)); };  // CSI n D

                    vt::csier.table[CSI_CNL] = vt::csier.table[CSI_CUD];   // CSI n E
                    vt::csier.table[CSI_CPL] = vt::csier.table[CSI_CUU];   // CSI n F
                    vt::csier.table[CSI_CHX] = VT_PROC{ p->chx( q(1)); };  // CSI n G  Move caret hz absolute
                    vt::csier.table[CSI_CHY] = VT_PROC{ p->chy( q(1)); };  // CSI n d  Move caret vt absolute
                    vt::csier.table[CSI_CUP] = VT_PROC{ p->cup( q   ); };  // CSI y ; x H (1-based)
                    vt::csier.table[CSI_HVP] = VT_PROC{ p->cup( q   ); };  // CSI y ; x f (1-based)

                    vt::csier.table[CSI_DCH] = VT_PROC{ p->dch( q(1)); };  // CSI n P  Delete n chars
                    vt::csier.table[CSI_ECH] = VT_PROC{ p->ech( q(1)); };  // CSI n X  Erase n chars
                    vt::csier.table[CSI_ICH] = VT_PROC{ p->ich( q(1)); };  // CSI n @  Insert n chars

                    vt::csier.table[CSI__ED] = VT_PROC{ p->ed ( q(0)); };  // CSI n J
                    vt::csier.table[CSI__EL] = VT_PROC{ p->el ( q(0)); };  // CSI n K
                    vt::csier.table[CSI__IL] = VT_PROC{ p->il ( q(1)); };  // CSI n L  Insert n lines
                    vt::csier.table[CSI__DL] = VT_PROC{ p->dl ( q(1)); };  // CSI n M  Delete n lines
                    vt::csier.table[CSI__SD] = VT_PROC{ p->scl( q(1)); };  // CSI n T  Scroll down by n lines, scrolled out lines are lost
                    vt::csier.table[CSI__SU] = VT_PROC{ p->scl(-q(1)); };  // CSI n S  Scroll   up by n lines, scrolled out lines are lost

                    vt::csier.table[DECSTBM] = VT_PROC{ p->scr( q   ); };  // CSI r; b r - Set scrolling region (t/b: top+bottom)

                    vt::intro[ctrl::ESC]['M']= VT_PROC{ p->ri(); }; // ESC M  Reverse index
                    vt::intro[ctrl::ESC]['H']= VT_PROC{ p->na("ESC H  Place tabstop at the current caret posistion"); }; // ESC H  Place tabstop at the current caret posistion
                    vt::intro[ctrl::ESC]['c']= VT_PROC{ p->boss.decstr(); }; // ESC c (same as CSI ! p) Full reset (RIS)

                    vt::intro[ctrl::BS ]     = VT_PROC{ p->cuf(-q.pop_all(ctrl::BS )); };
                    vt::intro[ctrl::DEL]     = VT_PROC{ p->del( q.pop_all(ctrl::DEL)); };
                    vt::intro[ctrl::TAB]     = VT_PROC{ p->tab( q.pop_all(ctrl::TAB)); };
                    vt::intro[ctrl::CR ]     = VT_PROC{ p->home(); };
                    vt::intro[ctrl::EOL]     = VT_PROC{ p->dn ( q.pop_all(ctrl::EOL)); };

                    vt::csier.table_quest[DECSET] = VT_PROC{ p->boss.decset(p, q); };
                    vt::csier.table_quest[DECRST] = VT_PROC{ p->boss.decrst(p, q); };
                    vt::csier.table_excl [DECSTR] = VT_PROC{ p->boss.decstr(); }; // CSI ! p = Soft terminal reset (DECSTR)

                    vt::csier.table[CSI_SGR][SGR_RST] = VT_PROC{ p->nil(); }; // fx_sgr_rst       ;
                    vt::csier.table[CSI_SGR][SGR_SAV] = VT_PROC{ p->sav(); }; // fx_sgr_sav       ;
                    vt::csier.table[CSI_SGR][SGR_FG ] = VT_PROC{ p->rfg(); }; // fx_sgr_fg_def    ;
                    vt::csier.table[CSI_SGR][SGR_BG ] = VT_PROC{ p->rbg(); }; // fx_sgr_bg_def    ;

                    vt::csier.table[CSI_CCC][CCC_JET] = VT_PROC{ p->jet(q(0)); }; // fx_ccc_jet  default=bias::left=0
                    vt::csier.table[CSI_CCC][CCC_WRP] = VT_PROC{ p->wrp(q(1)); }; // fx_ccc_wrp  default=true
                    vt::csier.table[CSI_CCC][CCC_RTL] = nullptr; // fx_ccc_rtl //todo implement
                    vt::csier.table[CSI_CCC][CCC_RLF] = nullptr; // fx_ccc_rlf

                    vt::oscer[OSC_0] = VT_PROC{ p->boss.prop(OSC_0, q); };
                    vt::oscer[OSC_1] = VT_PROC{ p->boss.prop(OSC_1, q); };
                    vt::oscer[OSC_2] = VT_PROC{ p->boss.prop(OSC_2, q); };
                    vt::oscer[OSC_3] = VT_PROC{ p->boss.prop(OSC_3, q); };

                    // Log all unimplemented CSI commands
                    for (auto i = 0; i < 0x100; ++i)
                    {
                        auto& proc = vt::csier.table[i];
                        if (!proc)
                        {
                           proc = [i](auto& q, auto& p) { p->not_implemented_CSI(i, q); };
                        }
                    }
                }
            };

            term& boss;

            wall(term& boss)
                : rods( boss.base::anchor,
                        boss.base::oversize,
                        boss.viewport.size,
                        boss.base::color()),
                  boss{ boss }
            { }

            // Implement base-CSI contract (see ansi::csi_t)
            void task(ansi::rule const& cmd)
            {
                if (!caret->empty()) fork();
                caret->locus.push(cmd);
            }
            void post(utf::frag const& cluster) { caret->post(cluster); }
            void cook()                         { finalize(); }
            void  nil()                         { caret->nil(spare); }
            void  sav()                         { caret->sav(spare); }
            void  rfg()                         { caret->rfg(spare); }
            void  rbg()                         { caret->rbg(spare); }
            void  fgc(rgba const& c)            { caret->fgc(c); }
            void  bgc(rgba const& c)            { caret->bgc(c); }
            void  bld(bool b)                   { caret->bld(b); }
            void  itc(bool b)                   { caret->itc(b); }
            void  inv(bool b)                   { caret->inv(b); }
            void  und(bool b)                   { caret->und(b); }
            void  wrp(bool b)                   { batch[current_para].wrapln = b; }
            void  jet(iota n)                   { batch[current_para].adjust = (bias)n; }

            // Implement text manipulation procs
            //
            template<class T>
            void na(T&& note)
            {
                log("not implemented: ", note);
            }
            void not_implemented_CSI(iota i, fifo& q)
            {
                text params;
                while(q)
                {
                    params += std::to_string(q(0));
                    if (q)
                    {
                        auto is_sub_arg = q.issub(q.front());
                        auto delim = is_sub_arg ? ':' : ';';
                        params.push_back(delim);
                    }
                }
                log("CSI ", params, " ", (unsigned char)i, " is not implemented.");
            }
            void tab(iota n) { caret->ins(n); }
            // wall: CSI n T/S  Scroll down/up, scrolled out lines are lost
            void scl(iota n)
            {
                auto[top, end] = get_scroll_limits();
                auto scroll_top_index = basis + top;
                auto scroll_end_index = basis + end;
                auto master = batch[scroll_top_index].master;
                auto count = end - top + 1;
                if (n > 0) // Scroll down
                {
                    n = std::min(n, count);
                    // Move down by n all below the current
                    // one by one from the bottom
                    auto dst = batch.begin() + scroll_end_index;
                    auto src = dst - n;
                    auto s = count - n;
                    while(s--)
                    {
                        //todo revise
                        (*dst--).set(std::move(*src--));
                    }
                    // Clear n first lines
                    auto head = batch.begin() + scroll_top_index;
                    auto tail = head + n;
                    while(head != tail)
                    {
                        (*head++).trim_to(0);
                    }
                }
                else if (n < 0) // Scroll up
                {
                    n = -n;
                    n = std::min(n, count);
                    // Move up by n=-n all below the current
                    // one by one from the top
                    auto dst = batch.begin() + scroll_top_index;
                    auto src = dst + n;
                    auto s = count - n;
                    while(s--)
                    {
                        //todo revise
                        (*dst++).set(std::move(*src++));
                    }
                    // Clear n last lines
                    //todo revise (possible bug)
                    auto head = batch.begin() + scroll_end_index;
                    auto tail = head - n;
                    while(head != tail)
                    {
                        (*head--).trim_to(0);
                    }
                }
                // Rebuild overlaps from bottom to id
                rods::rebuild_upto_id(master);
                set_coord();
            }
            // wall: CSI n L  Insert n lines. Place caret to the begining of the current.
            void il(iota n)
            {
               /* Works only if caret is in the scroll region.
                * Inserts n lines at the current row and removes n lines at the scroll bottom.
                */
                finalize();
                if (n > 0 && inside_scroll_region())
                {
                    auto old_top = scroll_top;
                    scroll_top = coord.y;
                    scl(n);
                    scroll_top = old_top;
                    coord.x = 0;
                    set_coord();
                }
            }
            // wall: CSI n M Delete n lines. Place caret to the begining of the current.
            void dl(iota n)
            {
               /* Works only if caret is in the scroll region.
                * Deletes n lines at the current row and add n lines at the scroll bottom.
                */
                finalize();
                if (n > 0 && inside_scroll_region())
                {
                    auto old_top = scroll_top;
                    scroll_top = coord.y;
                    scl(-n);
                    scroll_top = old_top;
                    coord.x = 0;
                    set_coord();
                }
            }
            // wall: ESC M  Reverse index
            void ri()
            {
                /*
                 * Reverse index
                 * - move caret one line up if it is outside of scrolling region or below the top line of scrolling region.
                 * - one line scroll down if caret is on the top line of scroll region.
                 */
                if (coord.y != scroll_top)
                {
                    coord.y--;
                    set_coord();
                }
                else scl(1);
            }
            // wall: CSI t;b r - Set scrolling region (t/b: top+bottom)
            void scr(fifo& queue)
            {
                scroll_top    = queue(0);
                scroll_bottom = queue(0);
            }
            // wall: CSI n @  Insert n blanks after cursor. Don't change cursor pos
            void ich(iota n)
            {
                /*
                *   Inserts n blanks.
                *   Don't change cursor pos.
                *   Existing chars after cursor shifts to the right.
                */
                if (n > 0)
                {
                    finalize();
                    auto size   = rods::caret->length();
                    auto pos    = rods::caret->chx();
                    auto brush  = rods::caret->brush;
                    brush.txt(whitespace);
                    //todo unify
                    if (pos < size)
                    {
                        // Move existing chars to right (backward decrement)
                        auto& lyric =*rods::caret->lyric;
                        lyric.crop(size + n);
                        auto dst = lyric.data() + size + n;
                        auto end = lyric.data() + pos + n;
                        auto src = lyric.data() + size;
                        while (dst != end)
                        {
                            *--dst = *--src;
                        }
                        // Fill blanks
                        dst = lyric.data() + pos;
                        end = dst + n;
                        while (dst != end)
                        {
                            *dst++ = brush;
                        }
                    }
                    else
                    {
                        auto& lyric =*rods::caret->lyric;
                        lyric.crop(pos + n);
                        // Fill blanks
                        auto dst = lyric.data() + size;
                        auto end = lyric.data() + pos + n;
                        while (dst != end)
                        {
                            *dst++ = brush;
                        }
                    }
                    caret->chx(pos);
                    finalize();
                }
            }
            // wall: CSI n X  Erase/put n chars after cursor. Don't change cursor pos
            void ech(iota n)
            {
                if (n > 0)
                {
                    finalize();
                    auto pos = caret->chx();
                    caret->ins(n);
                    finalize();
                    caret->chx(pos);
                }
            }
            // wall: CSI n P  Delete (not Erase) letters under the caret.
            void dch(iota n)
            {
                /* del:
                 *    As characters are deleted, the remaining characters
                 *    between the cursor and right margin move to the left.
                 *    Character attributes move with the characters.
                 *    The terminal adds blank characters at the right margin.
                 */
                finalize();
                auto& lyric =*rods::caret->lyric;
                auto size   = rods::caret->length();
                auto caret  = rods::caret->chx();
                auto brush  = rods::caret->brush;
                brush.txt(whitespace);

                //todo unify for size.y > 1
                if (n > 0)
                {
                    if (caret < size)
                    {
                        auto max_n = width - caret % width;
                        n = std::min(n, max_n);
                        auto right_margin = max_n + caret;

                        //todo unify all
                        if (n >= size - caret)
                        {
                            auto dst = lyric.data() + caret;
                            auto end = lyric.data() + size;
                            while (dst != end)
                            {
                                *dst++ = brush;
                            }
                        }
                        else
                        {
                            if (size < right_margin) lyric.crop(right_margin);

                            auto dst = lyric.data() + caret;
                            auto src = lyric.data() + caret + n;
                            auto end = dst + (max_n - n);
                            while (dst != end)
                            {
                                *dst++ = *src++;
                            }
                            end = lyric.data() + right_margin;
                            while (dst != end)
                            {
                                *dst++ = brush;
                            }
                        }
                    }
                }
                else
                {
                    //todo support negative n
                }
            }
            // wall: '\x7F'  Delete characters backwards.
            void del(iota n)
            {
                log("not implemented: '\\x7F' Delete characters backwards.");

                // auto& layer = rods::caret;
                // if (layer->chx() >= n)
                // {
                //     layer->chx(layer->chx() - n);
                //     layer->del(n);
                // }
                // else
                // {
                //     auto  here = layer->chx();
                //     auto there = n - here;
                //     layer->chx(0);
                //     if (here) layer->del(here);
                //     {
                //         if (layer != rods::batch.begin())
                //         {
                //             if (!layer->length())
                //             {
                //                 if (layer->locus.bare())
                //                 {
                //                     layer = std::prev(page::batch.erase(layer));
                //                 }
                //                 else
                //                 {
                //                     layer->locus.pop();
                //                 }
                //                 there--;
                //             }
                //             else
                //             {
                //                 auto prev = std::prev(layer);
                //                 *(*prev).lyric += *(*layer).lyric;
                //                 page::batch.erase(layer);
                //                 layer = prev;
                //                 layer->chx(layer->length());
                //             }
                //         }
                //     }
                // }
            }
            // wall: Move caret forward by n.
            void cuf(iota n)
            {
                finalize();
                auto posx = caret->chx();
                caret->chx(posx += n);
            }
            // wall: CSI n G  Absolute horizontal caret position (1-based)
            void chx(iota n)
            {
                finalize();
                coord.x = n - 1;
                set_coord();
            }
            // wall: CSI n d  Absolute vertical caret position (1-based)
            void chy(iota n)
            {
                finalize();
                coord.y = n - 1;
                set_coord();
            }
            // wall: CSI y; x H/F  Caret position (1-based)
            void cup(fifo& queue)
            {
                finalize();
                auto y = queue(1);
                auto x = queue(1);
                auto p = twod{ x, y };
                auto viewport = twod{ width, viewport_height };
                coord = std::clamp(p, dot_11, viewport);//todo unify
                coord-= dot_11;
                set_coord();
            }
            // wall: Line feed up
            template<bool PRESERVE_BRUSH = true>
            void up(iota n)
            {
                finalize();
                if (batch[current_para].wrapln)
                {
                    // Check the temp caret position (deffered wrap)
                    if (coord.x && (coord.x % width == 0))
                    {
                        coord.x -= width;
                        --n;
                    }
                }
                coord.y -= n;
                set_coord();
            }
            // wall: Line feed down
            void dn(iota n)
            {
                finalize();
                if (batch[current_para].wrapln
                 && coord.x == width)
                {
                    coord.x = 0;
                }

                // Scroll regions up if coord.y == scroll_bottom and scroll region are defined
                auto[top, end] = get_scroll_limits();
                if (n > 0 && (scroll_top || scroll_bottom) && coord.y == end)
                {
                    scl(-n);
                }
                else
                {
                    coord.y += n;
                    set_coord();
                }
            }
            // wall: '\r'  Go to home of visible line instead of home of para
            void home()
            {
                finalize();
                auto posx = caret->chx();
                if (batch[current_para].wrapln) posx -= posx % width;
                else                            posx = 0;
                caret->chx(posx);
                coord.x = posx % width;

                //if (posx && (posx % width == 0)) posx--;
                //caret->chx(posx -= posx % width);
            }
            // wall: '\n' || '\r\n'  Carriage return + Line feed
            void eol(iota n)
            {
                finalize();
                //todo Check the temp caret position (deffered wrap)
                coord.x = 0;
                coord.y += n;
                set_coord();
            }
            // wall: CSI n J  Erase display
            void ed(iota n)
            {
                finalize();
                auto current_brush = caret->brush;
                switch (n)
                {
                    case commands::erase::display::below: // n = 0(default)  Erase viewport after caret.
                    {
                        // Cut all lines above and current line
                        cut_above();
                        // Remove all lines below
                        //todo unify batch.resize(current_para); // no default ctor for line
                        auto erase_count = count - (current_para + 1);
                        parid -= erase_count;
                        count = current_para + 1;
                        while(erase_count--)
                        {
                            batch.pop_back();
                        }
                        break;
                    }
                    case commands::erase::display::above: // n = 1  Erase viewport before caret.
                    {
                        // Insert spaces on all lines above including the current line,
                        //   begining from master of viewport top line
                        //   ending the current line
                        auto master_id = batch[basis].master;
                        auto mas_index = get_line_index_by_id(master_id);
                        auto head = batch.begin() + mas_index;
                        auto tail = batch.begin() + current_para;
                        auto count = coord.y * width + coord.x;
                        auto start = (basis - mas_index) * width;
                        //todo unify
                        do
                        {
                            auto& lyric = *(*head).stanza;
                            lyric.ins(start, count, spare);
                            lyric.trim(spare);
                            start -= width;
                        }
                        while(head++ != tail);
                        break;
                    }
                    case commands::erase::display::viewport: // n = 2  Erase viewport.
                        set_coord(dot_00);
                        ed(commands::erase::display::below);
                    break;
                    case commands::erase::display::scrollback: // n = 3  Erase scrollback.
                        rods::clear(true);
                    break;
                    default:
                        break;
                }
                caret->brush = current_brush;
                //todo preserve other attributes: wrp, jet
            }
            // wall: CSI n K  Erase line (don't move caret)
            void el(iota n)
            {
                finalize();
                auto& lyric = *caret->lyric;

                switch (n)
                {
                    default:
                    case commands::erase::line::right: // Ps = 0  ⇒  Erase to Right (default).
                    {
                        // ConPTY doesn't move caret (is it ok?)
                        //todo optimize
                        auto brush = caret->brush;
                        brush.txt(' ');
                        auto coor = caret->chx();
                        //if (batch[current_para].wrapln)
                        {
                            auto start = coor;
                            auto end   = (coor + width) / width * width;
                            caret->ins(end - start);
                            caret->cook();
                            caret->chx(coor);
                            caret->trim(spare);
                            //finalize();
                        }
                        break;
                    }
                    case commands::erase::line::left: // Ps = 1  ⇒  Erase to Left.
                    {
                        auto brush = caret->brush;
                        brush.txt(' ');
                        auto coor = caret->chx();
                        auto width = boss.viewport.size.x;
                        if (coor < width)
                        {
                            lyric.each([&](cell& c) {if (coor > 0) { coor--; c.fuse(brush); } });
                        }
                        else
                        {
                            auto left_edge = coor - coor % width;
                            lyric.crop({ left_edge,1 }, brush);
                            lyric.crop({ left_edge + width,1 }, brush);
                        }
                        break;
                    }
                    case commands::erase::line::all: // Ps = 2  ⇒  Erase All.
                    {
                        //todo optimize
                        auto coor  = caret->chx();
                        auto brush = caret->brush;
                        brush.txt(' ');
                        auto width = boss.viewport.size.x;
                        auto left_edge = coor - coor % width;
                        lyric.crop({ left_edge,1 }, brush);
                        lyric.crop({ left_edge + width,1 }, brush);
                        break;
                    }
                }
            }
        };

        wall     scroll{ *this };
        wall     altbuf{ *this };
        wall*    target{ &scroll };
        os::cons ptycon;

        bool mode_DECCKM{ faux }; // todo unify
        std::map<iota, text> props;

        rect viewport = { {}, dot_11 }; // term: Viewport area

        subs tokens; // term: SGR mouse tracking subscription tokens set.

        ansi::esc   m_track; // term: Mouse tracking buffer.
        testy<twod> m_coord;

        // term: SGR mouse tracking switcher.
        void mouse_tracking(bool enable)
        {
            if (enable && !tokens.count())
            {
                tokens.clear();
                SUBMIT_T(e2::release, e2::hids::mouse::any, tokens, gear)
                {
                    using m = e2::hids::mouse;
                    constexpr static iota left     = 0;
                    constexpr static iota right    = 2;
                    constexpr static iota idle     = 32;
                    constexpr static iota wheel_up = 64;
                    constexpr static iota wheel_dn = 65;

                    auto capture = [&](){
                        if (!gear.captured(bell::id)) gear.capture(bell::id);
                        gear.dismiss();
                    };
                    auto release = [&](){
                        if (gear.captured(bell::id)) gear.release();
                        gear.dismiss();
                    };
                    auto proceed = [&](auto ctrl, bool ispressed){
                        if (gear.meta(hids::SHIFT))   ctrl |= 0x4;
                        if (gear.meta(hids::ANYCTRL)) ctrl |= 0x8;
                        if (gear.meta(hids::ALT))     ctrl |= 0x10;
                        m_track.mtrack(ctrl, gear.coord, ispressed);
                    };

                    iota bttn = 0;
                    switch (auto deal = bell::protos<e2::release>())
                    {
                    case m::move:
                        if (m_coord(gear.coord)) proceed(idle, faux);
                        break;
                    case m::button::drag::pull::leftright:
                    case m::button::drag::pull::win:
                    case m::button::drag::pull::right:
                    case m::button::drag::pull::middle:
                    case m::button::drag::pull::left:
                        if (m_coord(gear.coord)) proceed(idle, true);
                        break;
                    case m::button::down::leftright:
                        capture();
                        proceed(left,  true);
                        proceed(right, true);
                        break;
                    case m::button::down::win   : ++bttn;
                    case m::button::down::right : ++bttn;
                    case m::button::down::middle: ++bttn;
                    case m::button::down::left  :
                        capture();
                        proceed(bttn, true);
                        break;
                    case m::button::up::leftright:
                        release();
                        proceed(left,  faux);
                        proceed(right, faux);
                        break;
                    case m::button::up::win   : ++bttn;
                    case m::button::up::right : ++bttn;
                    case m::button::up::middle: ++bttn;
                    case m::button::up::left  :
                        release();
                        proceed(bttn, faux);
                        break;
                    case m::scroll::up:
                        proceed(wheel_up, faux);
                        break;
                    case m::scroll::down:
                        proceed(wheel_dn, faux);
                        break;
                    default:
                        break;
                    }

                    if (m_track.length())
                    {
                        ptycon.write(m_track);
                        m_track.clear();
                        gear.dismiss();
                    }
                };
            }
            else tokens.clear();
        }

        // term: Apply page props.
        void prop(iota cmd, view txt)
        {
            auto& utf8 = (props[cmd] = txt);
            switch (cmd)
            {
                case ansi::OSC_0: // prop_name::icon_title:
                case ansi::OSC_1: // prop_name::icon:
                case ansi::OSC_2: // prop_name::x_prop:
                case ansi::OSC_3: // prop_name::title:
                    base::riseup<e2::preview, e2::form::prop::header>(utf8);
                    break;
                default:
                    break;
            }
        }

        // term: Soft terminal reset (DECSTR)
        void decstr()
        {
            scroll.clear(true);
            altbuf.clear(true);
            target = &scroll;
        }
        void decset(wall*& p, fifo& queue)
        {
            while (auto q = queue(0))
            {
                switch (q)
                {
                    case 1:    // Cursor keys application mode.
                        mode_DECCKM = true;
                        break;
                    case 7:    // Enable auto-wrap
                        target->wrp(true);
                        break;
                    case 25:   // Caret on.
                        caret.show();
                        target->caret_visible = true; //todo unify
                        break;
                    case 1000: // Send mouse X & Y on button press and release.
                    case 1001: // Use Hilite mouse tracking.
                    case 1002: // Use cell motion mouse tracking.
                    case 1003: // Use all motion mouse tracking.
                        log("decset: CSI ? 1000-1003 h  old mouse modes are not supported");
                        break;
                    case 1006: // Enable SGR mouse tracking mode.
                        mouse_tracking(true);
                        break;

                    case 1004: // Enable sending FocusIn/FocusOut events.
                        log("decset: CSI ? 1004 h  is not implemented (focus)");
                        break;
                    case 1005: // Enable UTF-8 mouse mode.
                        log("decset: CSI ? 1005 h  UTF-8 mouse mode is not supported");
                        break;
                        
                    case 1048: // Save cursor
                        break;
                    case 1047: // Use alternate screen buffer
                    case 1049: // Save cursor and Use alternate screen buffer, clearing it first.  This control combines the effects of the 1047 and 1048  modes.
                        target->cook();
                        target = &altbuf;
                        break;
                    case 2004: // Set bracketed paste mode.
                        log("decset: CSI ? 2004 h  is not implemented (bracketed paste mode)");
                        break;

                    default:
                        break;
                }
            }
        }
        void decrst(wall*& p, fifo& queue)
        {
            while (auto q = queue(0))
            {
                switch (q)
                {
                    case 1:    // Cursor keys ANSI mode.
                        mode_DECCKM = faux;
                        break;
                    case 7:    // Disable auto-wrap
                        target->wrp(faux);
                        break;
                    case 25:   // Caret off.
                        caret.hide();
                        target->caret_visible = faux; //todo unify
                        break;
                    case 1000: // Don't send mouse X & Y on button press and release.
                    case 1001: // Don't use Hilite(c) mouse tracking.
                    case 1002: // Don't use cell motion mouse tracking.
                    case 1003: // Don't use all motion mouse tracking.
                        log("decset: CSI ? 1000-1003 l  old mouse modes are not supported");
                        break;
                    case 1006: // Disable SGR mouse tracking mode.
                        mouse_tracking(faux);
                        break;

                    case 1004: // Don't send FocusIn / FocusOut events.
                        log("decset: CSI ? 1004 l  is not implemented (focus)");
                        break;
                    case 1005: // Disable UTF-8 mouse mode.
                        log("decset: CSI ? 1005 l  UTF-8 mouse mode is not supported");
                        break;

                    case 1048: // Restore cursor
                        break;
                    case 1047: // Use normal screen nuffer
                    case 1049: // Use normal screen nuffer and restore cursor
                        target->cook();
                        target = &scroll;
                        break;
                    case 2004: // Set bracketed paste mode.
                        log("decset: CSI ? 2004 l  is not implemented (bracketed paste mode)");
                        break;

                    default:
                        break;
                }
            }
        }

    public:
        term(twod winsz, text cmdline)
        {
            caret.show();

            #ifdef DEMO
            //pane::scrollable = faux;
            #endif

            //todo unify
            //t.scroll.maxlen(2000);

            #ifndef DEMO
            keybd.accept(true); // Subscribe on keybd offers
            #endif

            SUBMIT(e2::release, e2::hids::keybd::any, gear)
            {
                //todo reset scroll position on keypress
                auto caret_xy = caret.coor();
                if (!viewport.hittest(caret_xy))
                {
                    //todo revise
                    auto old_caret_pos = viewport.coor + viewport.size - dot_11;
                    auto anchor_delta = caret_xy - old_caret_pos;
                    auto new_coor = base::coor.get() - anchor_delta;
                    SIGNAL(e2::release, base::move_event, new_coor);
                }

                //todo optimize/unify
                if (mode_DECCKM)
                {
                    auto trans = gear.keystrokes;
                    utf::change(trans, "\033[A", "\033OA");
                    utf::change(trans, "\033[B", "\033OB");
                    utf::change(trans, "\033[C", "\033OC");
                    utf::change(trans, "\033[D", "\033OD");

                    utf::change(trans, "\033[1A", "\033OA");
                    utf::change(trans, "\033[1B", "\033OB");
                    utf::change(trans, "\033[1C", "\033OC");
                    utf::change(trans, "\033[1D", "\033OD");

                    ptycon.write(trans);
                }
                else
                {
                    ptycon.write(gear.keystrokes);
                }

                #ifdef KEYLOG
                    std::stringstream d;
                    view v = gear.keystrokes;
                    log("key strokes raw: ", utf::debase(v));
                    while (v.size())
                    {
                        d << (int)v.front() << " ";
                        v.remove_prefix(1);
                    }
                    log("key strokes bin: ", d.str());
                #endif
            };

            SUBMIT(e2::preview, e2::form::layout::size, new_size)
            {
                //todo recalc overlapping master id's over selfid's on resize

                if (viewport.size != new_size)
                {
                    viewport.size = new_size;
                    if (target == &altbuf)
                    {
                        altbuf.ed(commands::erase::display::scrollback);
                    }
                    else
                    {
                        target->remove_empties();
                    }
                }

                auto new_pty_size = new_size;
                auto scrollback_size = target->reflow();
                new_size = std::max(new_size, scrollback_size); // Use the max size

                ptycon.resize(new_pty_size); // set viewport size
            };
            SUBMIT(e2::release, e2::form::layout::move, new_coor)
            {
                viewport.coor = -new_coor;
            };

            ptycon.start(cmdline, winsz, [&](auto d) { input_hndl(d); });
        }

        ~term()
        {
            ptycon.close();
        }

        void input_hndl(view shadow)
        {
            while (ptycon)
            {
                e2::try_sync guard;
                if (guard)
                {
                    //log(" 1. target content: ", target->get_content());


                    SIGNAL(e2::general, e2::debug::output, shadow);

                    auto old_caret_pos = caret.coor();
                    auto caret_is_visible = viewport.hittest(old_caret_pos);

                    ansi::parse(shadow, target); // Append using default insertion point

                    //if (target == &altbuf)
                    //{
                    //	altbuf.hz_trim(viewport.size.x);
                    //}

                    //todo remove rods::reflow(), take new_size only
                    //     all calcs are made already in rods::finalize()
                    auto new_size = target->reflow();
                    auto caret_xy = target->cp();

                    caret.coor(caret_xy);

                    if (caret_is_visible && !viewport.hittest(caret_xy))
                    {
                        auto anchor_delta = caret_xy - old_caret_pos;
                        auto new_coor = base::coor.get() - anchor_delta;
                        SIGNAL(e2::release, base::move_event, new_coor);
                    }

                    SIGNAL(e2::release, base::size_event, new_size);

                    base::deface();
                    //log(" 2. target content: ", target->get_content());

                    break;
                }
                else std::this_thread::yield();
            }
        }
        // term/base: Draw on canvas.
        virtual void renderproc (face& parent_canvas)
        {
            base::renderproc(parent_canvas);
            target->output(parent_canvas);

            // in order to show cursor/caret
            SIGNAL(e2::release, e2::form::upon::redrawn, parent_canvas);
        }
        virtual void color(rgba const& fg_color, rgba const& bg_color)
        {
            base::color(fg_color, bg_color);
            target->color(base::color());
        }
    };
}

#endif // NETXS_TERMINAL_HPP