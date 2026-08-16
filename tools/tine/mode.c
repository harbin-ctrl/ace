#include <string.h>

#include "tine_console.h"

#include "structs.h"
#include "mode.h"

typedef struct MAP MAP;
struct MAP{
    KEYSTROKE k;
    callback f;
    ARG a;
};

#define MAP_MAX 100
struct MODE{
    callback defcb;
    MAP map[MAP_MAX];
};

#define CTRL(x) ((x) & 0x1f)
static bool
noaction(EDITOR *e, VIEW *v, const ARG *a)
{
    (void)e;
    (void)v;
    (void)a;
    return true;
}

static MODE cmdmodetab ={
    cmd_ty,
    {
        {{KEY_CODE_YES, KEY_BACKSPACE},    cmd_dl, {0}},
        {{KEY_CODE_YES, KEY_DC},           cmd_dc, {0}},
        {{KEY_CODE_YES, KEY_UP},           cmd_cu, {0}},
        {{KEY_CODE_YES, KEY_DOWN},         cmd_cd, {0}},
        {{KEY_CODE_YES, KEY_LEFT},         cmd_cl, {0}},
        {{KEY_CODE_YES, KEY_RIGHT},        cmd_cr, {0}},
        {{KEY_CODE_YES, KEY_SLEFT},        cmd_cs, {0}},
        {{KEY_CODE_YES, KEY_SRIGHT},       cmd_ce, {0}},
        {{KEY_CODE_YES, KEY_RESIZE},       cmd_vw, {0}},
        {{KEY_CODE_YES, KEY_ENTER},        cmd_ru, {0}},
        {{KEY_CODE_YES, KEY_BTAB},         cmd_bt, {0}},
        {{OK,           L'\n'},            cmd_rs, {0}},
        {{OK,           L'\r'},            cmd_ru, {0}},
        {{OK,           L'\t'},            cmd_tb, {0}},
        {{OK,           0x7f},             cmd_dl, {0}},
        {{OK,           CTRL(L'[')},       cmd_rs, {0}},
        {{OK,           CTRL(L']')},       cmd_cj, {0}},
        {{OK,           CTRL(L'H')},       cmd_dl, {0}},
        {{OK,           CTRL(L'F')},       cmd_fc, {0}},
        {{OK,           CTRL(L'O')},       cmd_dw, {0}},
        {{OK,           CTRL(L'R')},       cmd_wp, {0}},
        {{OK,           CTRL(L'T')},       cmd_wn, {0}},
        {{OK,           CTRL(L'V')},       cmd_vw, {0}},
        {{OK,           CTRL(L'Y')},       cmd_el, {0}},
        {{KEY_CODE_YES, KEY_HELP},         noaction, {0}},
        {{KEY_CODE_YES, 0},                noaction, {0}},
        {{OK,           0},                NULL,   {0}}
    }
};
MODE *cmdmode = &cmdmodetab;

static MODE docmodetab ={
    cmd_ty,
    {
        {{KEY_CODE_YES, KEY_BACKSPACE},    cmd_dl, {0}},
        {{KEY_CODE_YES, KEY_IC},           cmd_i,  {.t = ARG_STRING, .n1 = 0, .s1 = L""}},
        {{KEY_CODE_YES, KEY_HOME},         cmd_t,  {0}},
        {{KEY_CODE_YES, KEY_END},          cmd_b,  {0}},
        {{KEY_CODE_YES, KEY_DC},           cmd_dc, {0}},
        {{KEY_CODE_YES, KEY_UP},           cmd_cu, {0}},
        {{KEY_CODE_YES, KEY_DOWN},         cmd_cd, {0}},
        {{KEY_CODE_YES, KEY_LEFT},         cmd_cl, {0}},
        {{KEY_CODE_YES, KEY_RIGHT},        cmd_cr, {0}},
        {{KEY_CODE_YES, KEY_SLEFT},        cmd_cs, {0}},
        {{KEY_CODE_YES, KEY_SRIGHT},       cmd_ce, {0}},
        {{KEY_CODE_YES, KEY_RESIZE},       cmd_vw, {0}},
        {{KEY_CODE_YES, KEY_ENTER},        cmd_s,  {0}},
        {{KEY_CODE_YES, KEY_PPAGE},        cmd_pu, {0}},
        {{KEY_CODE_YES, KEY_NPAGE},        cmd_pd, {0}},
        {{KEY_CODE_YES, KEY_BTAB},         cmd_bt, {0}},
        {{KEY_CODE_YES, KEY_F(1)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(2)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(3)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(4)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(5)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(6)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(7)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(8)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(9)},         noaction, {0}},
        {{KEY_CODE_YES, KEY_F(10)},        noaction, {0}},
        {{KEY_CODE_YES, KEY_HELP},         noaction, {0}},
        {{OK,           L'\n'},            cmd_s,  {0}},
        {{OK,           L'\r'},            cmd_s,  {0}},
        {{OK,           L'\t'},            cmd_tb, {0}},
        {{OK,           0x7f},             cmd_dl, {0}},
        {{OK,           CTRL(L'[')},       cmd_cm, {0}},
        {{OK,           CTRL(L']')},       cmd_cj, {0}},
        {{OK,           CTRL(L'A')},       cmd_i,  {.t = ARG_STRING, .n1 = 0, .s1 = L""}},
        {{OK,           CTRL(L'B')},       cmd_d,  {0}},
        {{OK,           CTRL(L'C')},       noaction, {0}},
        {{OK,           CTRL(L'D')},       cmd_pd, {0}},
        {{OK,           CTRL(L'E')},       cmd_ep, {0}},
        {{OK,           CTRL(L'F')},       cmd_fc, {0}},
        {{OK,           CTRL(L'G')},       cmd_rp, {0}},
        {{OK,           CTRL(L'H')},       cmd_dl, {0}},
        {{OK,           CTRL(L'I')},       cmd_tb, {0}},
        {{OK,           CTRL(L'J')},       noaction, {0}},
        {{OK,           CTRL(L'K')},       noaction, {0}},
        {{OK,           CTRL(L'L')},       noaction, {0}},
        {{OK,           CTRL(L'N')},       noaction, {0}},
        {{OK,           CTRL(L'O')},       cmd_dw, {0}},
        {{OK,           CTRL(L'P')},       noaction, {0}},
        {{OK,           CTRL(L'Q')},       noaction, {0}},
        {{OK,           CTRL(L'R')},       cmd_wp, {0}},
        {{OK,           CTRL(L'T')},       cmd_wn, {0}},
        {{OK,           CTRL(L'U')},       cmd_pu, {0}},
        {{OK,           CTRL(L'V')},       cmd_vw, {0}},
        {{OK,           CTRL(L'W')},       noaction, {0}},
        {{OK,           CTRL(L'Y')},       cmd_el, {0}},
        {{OK,           CTRL(L'Z')},       noaction, {0}},
        {{OK,           L'['},             cmd_hb, {0}},
        {{OK,           L']'},             cmd_hb, {0}},
        {{OK,           L'{'},             cmd_hb, {0}},
        {{OK,           L'}'},             cmd_hb, {0}},
        {{OK,           L'('},             cmd_hb, {0}},
        {{OK,           L')'},             cmd_hb, {0}},
        {{KEY_CODE_YES, 0},                noaction, {0}},
        {{OK,           0},                NULL,   {0}}
    }
};
MODE *docmode = &docmodetab;

callback
lookupkeystroke(const MODE *m, KEYSTROKE k, ARG *a)
{
    for (const MAP *i = m->map; i->f; i++){
        if (i->k.o == k.o && (!i->k.c || i->k.c == k.c)){
            if (i->a.t != ARG_NONE)
                memcpy(a, &i->a, sizeof(ARG));
            return i->f;
        }
    }
    return m->defcb;
}
