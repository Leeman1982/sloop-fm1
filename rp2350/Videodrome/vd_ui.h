/* SPDX-License-Identifier: GPL-3.0-only */
/* VIDEODROME on the RP2350: the OLED UI, xtro's panel (one encoder with push, PLAY SHIFT PAGE TRACK MUTE, OCT- OCT+,
 * the CUTOFF and RESONANCE pots) on a 128 x 64 SSD1309, in xtro's "84" vocabulary (vd_gfx.h). It is a new face on the
 * FM-1's own logic: every change goes through the function the FM-1's UI calls for it (edit_param, preset_go,
 * select_engine, track_select, rec_toggle, section_store, undo_swap ...), every value is shown with param_format, every
 * page is a page of params.c's PAGES. What the FM-1 says (ui_say) comes up as the footer's banner.
 *
 *   PAGE / SHIFT+PAGE     the next / previous screen: PATTERN SOUND PRESETS MIXER SONG SYSTEM HELP
 *   TRACK / SHIFT+TRACK   the next / previous track (1-3 synths, 4 the drums)
 *   PLAY                  start / stop;   SHIFT+PLAY  record (REC: arm, the next press records)
 *   MUTE                  the selected track muted;   SHIFT+MUTE  soloed
 *   OCT- / OCT+           the octave of a MIDI keyboard on MIDI IN (both: 0);   SHIFT+OCT- / +  undo / redo
 *   ENC                   turn: the cursor (a value while editing); push: the screen's action (below); long: PATTERN
 *   CUTOFF / RESONANCE    the selected track's filter (its engine's own CUT and RES, else the track filter)
 * PATTERN: push sets / clears the cursor's step (a synth: the last note played on MIDI IN; the drums: the sound picked
 * with SHIFT+turn); SHIFT+turn on a synth step moves its note. SOUND: every page of the track's sound, one list; push
 * edits the row (turn the value), SHIFT+turn jumps a page. PRESETS: turn loads, SHIFT+turn the engine, push the
 * favourite. MIXER: turn picks the track, push edits its level, SHIFT+turn its pan. SONG: the sections A-D (push loads,
 * SHIFT+push stores), the chain (push edits a step's section, SHIFT+turn its bars), its length, LOOP / SONG, SAVE. */
#include "vd_api.h"
#include "vd_gfx.h"
#include "vd_wordmark.h"

enum { VS_PATTERN, VS_SOUND, VS_PRESETS, VS_MIXER, VS_SONG, VS_SYSTEM, VS_HELP, VS_COUNT };
static const char *const VS_NAME[VS_COUNT] = {"PATTERN", "SOUND", "PRESETS", "MIXER", "SONG", "SYSTEM", "HELP"};
/* the buttons (Videodrome.ino reads them, debounced; vd_input gets their edges): VB_*, vd_ctl_t in vd_api.h */
#define VB_COUNT 7

static struct {
    u8g2_t *u;
    uint8_t scr, edit;
    int16_t sound_row, sound_first, preset_first, mix_sel, song_row, song_first, help_first, sys_page;
    uint8_t shown[1024];         /* the panel's last frame (vd_ui_push: only the tile rows that changed are sent) */
    uint8_t dirty_rows;          /* rows still to send */
    uint32_t t_timer;            /* the FM-1's 60 Hz frame timers (ui_timers) */
    char toast[24];
    uint32_t mhz;                /* the clock (Videodrome.ino), for SYSTEM */
} vu;

/* ------------------------------------------------------------ helpers --- */
static void vd_say(const char *a, const char *b) { ui_say(a, b); }
static const char *vd_eng_name(const track_t *t)
{
    return is_drum(t) ? "DRUMS" : ENGINES[t->eng_req % NENGINES]->name;
}
static void vd_sound_name(const track_t *t, char *b)       /* the track's sound: a kit, a user preset, a factory one */
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    b[0] = 0;
    if (is_drum(t))
        str_cpy(b, DRUM_KIT_NAMES[drum_kit()], 13);
    else if (user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else if (e->npresets)
        str_cpy(b, e->presets[t->preset % e->npresets].name, 13);
}
static void vd_upper(char *s) { for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32); }

/* the header: > the track, its sound, the tempo, the transport; the CPU as a framed bar on the right */
static void vd_header(void)
{
    u8g2_t *u = vu.u;
    char b[24], s[14];
    int x;
    u8g2_SetFont(u, u8g2_font_5x7_tr);
    u8g2_DrawGlyph(u, 0, 7, '>');
    b[0] = (char)('1' + song.sel);
    b[1] = '.';
    b[2] = ' ';
    b[3] = 0;
    vd_sound_name(TSEL, s);
    s[8] = 0;
    str_cpy(b + 3, s, 10);
    vd_upper(b);
    u8g2_DrawStr(u, 7, 7, b);
    fmt_int(b, song.g[G_BPM]);
    x = 66;
    u8g2_DrawStr(u, x, 7, b);
    x += u8g2_GetStrWidth(u, b) + 3;
    if (song.playing) {                                 /* boxes and lines only: xtro's rule for this panel */
        int i;
        for (i = 0; i < 4; i++)
            u8g2_DrawVLine(u, x + i, 1 + i, 7 - 2 * i);
        x += 7;
    }
    if (song.rec || rec_wait) {
        vg_frame(u, x, 1, 7, 7);
        if (song.rec || (fm1_ms / 250u) & 1u)
            u8g2_DrawBox(u, x + 2, 3, 3, 3);
    }
    vg_bar(u, VG_W - 24, 1, 24, vg_fill((int)(song.cpu_q8 > 256u ? 256u : song.cpu_q8), 256, 20));
    vg_header_rule(u, 60);
}
static void vd_footer(const char *chip, const char *hint)
{
    vg_footer(vu.u, chip, hint, vu.edit, ui.msg_t ? ui.msg : 0);
}

/* ----------------------------------------------------------- PATTERN --- */
static int vd_step_on(const track_t *t, uint32_t i)
{
    return is_drum(t) ? dstep_has(&t->dstep[i], drum_lane) : step_on(&t->step[i]);
}
static void vd_pattern_draw(void)
{
    u8g2_t *u = vu.u;
    track_t *t = TSEL;
    uint32_t len = trk_len(t), bank = ui.cursor / 16u, i;
    char b[32], n[6];
    for (i = 0; i < 16u; i++) {
        uint32_t idx = bank * 16u + i, row = i / 8u, col = i % 8u;
        int x = (int)col * 16, y = VG_BAND_Y + 1 + (int)row * 14, in = idx < len, h = 0, acc = 0;
        vg_rule(u, x, y + 10, 15, 1, in ? 2 : 4);
        if (in && col % 4u == 0u)
            u8g2_DrawVLine(u, x, y + 11, 2);
        if (in && vd_step_on(t, idx)) {
            if (is_drum(t)) {
                uint32_t lv = dstep_lvl(&t->dstep[idx], drum_lane);
                h = lv == LV_GHOST ? 3 : lv == LV_SOFT ? 5 : lv == LV_HARD ? 8 : 6;
                acc = lv == LV_HARD;
            } else {
                h = 2 + t->step[idx].vel * 6 / 127;
                acc = (t->step[idx].flags & SF_ACCENT) != 0;
            }
            u8g2_DrawBox(u, acc ? x + 1 : x + 3, y + 10 - h, acc ? 13 : 9, h);
        } else if (in && !is_drum(t) && t->step[idx].time == ST_TIE) {
            u8g2_DrawBox(u, x, y + 8, 16, 2);           /* a held note runs on through the step */
        } else if (in && is_drum(t) && dstep_mask(&t->dstep[idx])) {
            u8g2_DrawPixel(u, x + 7, y + 8);            /* another sound hits here */
        }
        if (song.playing && idx == t->seq_idx % len) {  /* the play head: a lid with caps */
            u8g2_DrawHLine(u, x, y, 15);
            u8g2_DrawVLine(u, x, y, 3);
            u8g2_DrawVLine(u, x + 14, y, 3);
        }
        if (idx == ui.cursor)
            u8g2_DrawBox(u, x, y + 11, 15, 2);
    }
    /* the readout: > the page of steps, the length, the cursor's step and what is on it */
    u8g2_SetFont(u, u8g2_font_5x7_tr);
    str_cpy(b, "PG", 4);
    fmt_int(b + 2, (int32_t)bank + 1);
    str_cpy(b + str_len(b), "/", 2);
    fmt_int(b + str_len(b), (int32_t)((len + 15u) / 16u));
    str_cpy(b + str_len(b), " L", 3);
    fmt_int(b + str_len(b), (int32_t)len);
    vg_prompt(u, 0, VG_FOOT_Y - 3, b);
    str_cpy(b, "S", 2);
    if (ui.cursor < 9u)
        str_cpy(b + 1, "0", 2);
    fmt_int(b + str_len(b), (int32_t)ui.cursor + 1);
    str_cpy(b + str_len(b), " ", 2);
    if (is_drum(t)) {
        str_cpy(b + str_len(b), LANE_NAME[drum_lane % DRUM_LANES], 10);
    } else if (step_on(&t->step[ui.cursor])) {
        note_name(n, t->step[ui.cursor].note[0]);
        str_cpy(b + str_len(b), n, 6);
        str_cpy(b + str_len(b), " v", 3);
        fmt_int(b + str_len(b), t->step[ui.cursor].vel);
    } else {
        str_cpy(b + str_len(b), "--", 3);
    }
    vd_upper(b);
    u8g2_DrawStr(u, VG_W - u8g2_GetStrWidth(u, b), VG_FOOT_Y - 3, b);
    {
        char chip[10] = "T1";
        chip[1] = (char)('1' + song.sel);
        vd_footer(is_drum(t) ? "DRUMS" : chip, is_drum(t) ? "PUSH=HIT SH+TURN=SND" : "PUSH=STEP SH+TURN=NOTE");
    }
}
static void vd_pattern_turn(int32_t s, int shift)
{
    track_t *t = TSEL;
    uint32_t len = trk_len(t);
    if (!shift) {
        ui.cursor = (uint8_t)clamp((int32_t)ui.cursor + s, 0, (int32_t)len - 1);
        ui.bank = (uint8_t)(ui.cursor / 16u);
        return;
    }
    if (is_drum(t)) {                                   /* the drum sound the grid writes (the GRID's own pick) */
        drum_lane = (uint8_t)(((int32_t)drum_lane + s % DRUM_LANES + DRUM_LANES) % DRUM_LANES);
        drum_hear(drum_lane);
        return;
    }
    if (step_on(&t->step[ui.cursor])) {                 /* the step's notes, a semitone a detent */
        step_t *st = &t->step[ui.cursor];
        uint32_t k;
        undo_mark(t, (undo_sess += 4u) | 3u);    /* (an action, an undo: seq.c undo_mark) */
        fm1_irq_off();
        for (k = 0; k < st->n && k < 4u; k++)
            st->note[k] = (uint8_t)clamp((int32_t)st->note[k] + s, 0, 127);
        fm1_irq_on();
        last_note = st->note[0];
        sync_reload = 1;
    }
}
static void vd_pattern_push(void)
{
    track_t *t = TSEL;
    uint32_t idx = ui.cursor;
    if (idx >= trk_len(t))
        return;
    if (song.playing && arrangement_enabled) {
        ui_message("STOP THE SONG FIRST");
        return;
    }
    undo_mark(t, (undo_sess += 4u) | 3u);    /* (an action, an undo: seq.c undo_mark) */
    fm1_irq_off();
    if (is_drum(t)) {                                   /* as the GRID page's key (ui_studio.c grid_key) */
        dstep_t *st = &t->dstep[idx];
        if (dstep_has(st, drum_lane)) {
            dstep_clr(st, drum_lane);
            if (!dstep_mask(st)) {
                lock_del(t, idx, P_COUNT);
                t->micro[idx] = 0;
                step_fill_set(t, idx, FC_NORM);
            }
        } else {
            dstep_set(st, drum_lane, LV_NORM, 0);
            t->seq_active = 1;
        }
    } else if (step_on(&t->step[idx])) {                /* as the SEQ layer's step key (ui_layers.c) */
        step_clear(&t->step[idx]);
    } else {
        step_t *s = &t->step[idx];
        memset(s, 0, sizeof *s);
        s->note[0] = last_note;
        s->n = 1;
        s->time = ST_NOTE;
        s->vel = 100;
        t->seq_active = 1;
    }
    fm1_irq_on();
    sync_reload = 1;
}

/* ------------------------------------------------------------- SOUND --- */
/* the selected track's pages, as one list of (page, slot): the sound (EDIT), its envelope, LFO, effects, arp, key and
 * chords, its pattern's settings, then the globals and the tools. A page the FM-1 would not show for this track (the
 * drums: only theirs) is left out, and so is an empty slot */
static const uint8_t VD_FAM_ORDER[] = {FAM_EDIT, FAM_ENV, FAM_LFO, FAM_FX, FAM_ARP, FAM_SCL, FAM_SEQ, FAM_GLO, FAM_SAVE};
static int vd_page_listed(uint32_t p)
{
    const page_t *pg = &PAGES[p];
    if (pg->scope == SC_STEP || pg->scope == SC_TRK || pg->scope == SC_SONG || pg->scope == SC_DRUM)
        return 0;
    if (pg->graph == GR_BROWSE || pg->graph == GR_USER || pg->graph == GR_SLOTS)
        return 0;                                       /* (PRESETS, USER and SECTION: their own screens) */
    if (!page_shown(pg))
        return 0;
    if (is_drum(TSEL) && !page_for_drum(pg))
        return 0;
    return 1;
}
#define VD_ROWS_MAX 160u
static uint16_t vd_rows[VD_ROWS_MAX];                  /* page << 2 | slot */
static uint32_t vd_rows_n;
static void vd_rows_build(void)
{
    uint32_t f, p, k;
    vd_rows_n = 0;
    for (f = 0; f < sizeof VD_FAM_ORDER; f++)
        for (p = 0; p < NPAGES; p++) {
            int16_t *vp;
            if (PAGES[p].fam != VD_FAM_ORDER[f] || !vd_page_listed(p))
                continue;
            for (k = 0; k < 4u && vd_rows_n < VD_ROWS_MAX; k++)
                if (page_desc(&PAGES[p], k, &vp) && vp)
                    vd_rows[vd_rows_n++] = (uint16_t)(p << 2 | k);
        }
}
static const char *vd_page_title(const page_t *pg)
{
    if (pg->scope == SC_ENGINE) {
        const char *t = ENGINES[TSEL->eng_req % NENGINES]->page_title[pg->id[0] != P_E0];
        if (t)
            return t;
    }
    return pg->title;
}
static void vd_sound_draw(void)
{
    u8g2_t *u = vu.u;
    uint32_t i, p;
    char chip[16];
    vd_rows_build();
    if (!vd_rows_n) {
        vd_footer("SOUND", "");
        return;
    }
    vu.sound_row = (int16_t)clamp(vu.sound_row, 0, (int32_t)vd_rows_n - 1);
    if (vu.sound_row < vu.sound_first)
        vu.sound_first = vu.sound_row;
    if (vu.sound_row >= vu.sound_first + 4)
        vu.sound_first = (int16_t)(vu.sound_row - 3);
    u8g2_SetFont(u, u8g2_font_6x10_tr);
    for (i = 0; i < 4u && vu.sound_first + (int32_t)i < (int32_t)vd_rows_n; i++) {
        uint32_t r = vd_rows[vu.sound_first + i], pg = r >> 2, k = r & 3u;
        int16_t *vp;
        const param_desc_t *d = page_desc(&PAGES[pg], k, &vp);
        char val[16], lab[12];
        const char *unit = "";
        if (!d || !vp)
            continue;
        param_format(d, *vp, val, &unit);
        if (unit && *unit && str_len(val) + str_len(unit) < 14u) {
            str_cpy(val + str_len(val), unit[0] == '%' ? "" : " ", 2);
            str_cpy(val + str_len(val), unit, 6);
        }
        str_cpy(lab, d->label, sizeof lab);
        vd_upper(val);
        vd_upper(lab);
        vg_list_row(u, VG_BAND_Y + (int)i * 10, vu.sound_first + (int32_t)i == vu.sound_row, lab, val, 1,
                    vu.edit && vu.sound_first + (int32_t)i == vu.sound_row);
    }
    vg_scrollbar(u, VG_BAND_Y, 40, vu.sound_first, 4, (int)vd_rows_n);
    p = vd_rows[vu.sound_row] >> 2;
    {   /* [TITLE n/m]: the page and its place in its family, as the FM-1's footer */
        uint32_t n = 0, at = 0, q;
        for (q = 0; q < NPAGES; q++)
            if (PAGES[q].fam == PAGES[p].fam && vd_page_listed(q)) {
                n++;
                if (q == p)
                    at = n;
            }
        str_cpy(chip, vd_page_title(&PAGES[p]), 10);
        if (n > 1) {
            str_cpy(chip + str_len(chip), " ", 2);
            fmt_int(chip + str_len(chip), (int32_t)at);
            str_cpy(chip + str_len(chip), "/", 2);
            fmt_int(chip + str_len(chip), (int32_t)n);
        }
        vd_upper(chip);
    }
    vd_footer(chip, vu.edit ? "TURN=VALUE" : "PUSH=EDIT");
}
static void vd_sound_turn(int32_t s, int shift)
{
    vd_rows_build();
    if (!vd_rows_n)
        return;
    if (vu.edit) {                                      /* the FM-1's knob: edit_param on its page and slot */
        uint32_t r = vd_rows[clamp(vu.sound_row, 0, (int32_t)vd_rows_n - 1)];
        ui.home = 0;
        ui.page = (uint8_t)(r >> 2);
        edit_param(r & 3u, s);
        return;
    }
    if (shift) {                                        /* a page at a time */
        uint32_t p = vd_rows[vu.sound_row] >> 2;
        int32_t r = vu.sound_row;
        while (r >= 0 && r < (int32_t)vd_rows_n && vd_rows[r] >> 2 == p)
            r += s > 0 ? 1 : -1;
        if (s < 0 && r >= 0) {                          /* (to the first row of the page before) */
            uint32_t q = vd_rows[r] >> 2;
            while (r > 0 && vd_rows[r - 1] >> 2 == q)
                r--;
        }
        vu.sound_row = (int16_t)clamp(r, 0, (int32_t)vd_rows_n - 1);
        return;
    }
    vu.sound_row = (int16_t)clamp(vu.sound_row + s, 0, (int32_t)vd_rows_n - 1);
}

/* ----------------------------------------------------------- PRESETS --- */
static void vd_presets_draw(void)
{
    u8g2_t *u = vu.u;
    uint32_t total, cur, i, k;
    int32_t first;
    u8g2_SetFont(u, u8g2_font_6x10_tr);
    if (is_drum(TSEL)) {                                /* the drums: their kits */
        cur = drum_kit();
        first = clamp((int32_t)cur - 1, 0, DRUM_KITS - 4);
        for (i = 0; i < 4u; i++) {
            char b[16];
            str_cpy(b, DRUM_KIT_NAMES[first + (int32_t)i], sizeof b);
            vd_upper(b);
            vg_list_row(u, VG_BAND_Y + (int)i * 10, (uint32_t)first + i == cur, b, 0, 1, 0);
        }
        vg_scrollbar(u, VG_BAND_Y, 40, first, 4, DRUM_KITS);
        vd_footer("KITS", "TURN=KIT");
        return;
    }
    cur = preset_pos(&total);
    if (!total) {
        vd_footer("PRESETS", "");
        return;
    }
    first = total <= 4u ? 0 : clamp((int32_t)cur - 1, 0, (int32_t)total - 4);
    for (i = 0; i < 4u && (uint32_t)first + i < total; i++) {
        uint32_t n = (uint32_t)first + i, e = preset_at(n, &k);
        char tag[6], nm[14], lab[22];
        if (e == NENGINES) {
            up_slot_label(tag, k);
            up_name(k, nm);
        } else {
            str_cpy(tag, preset_kind(n), sizeof tag);
            str_cpy(nm, ENGINES[e]->presets[k].name, sizeof nm);
        }
        str_cpy(lab, nm, sizeof lab);
        vd_upper(lab);
        vd_upper(tag);
        vg_list_row(u, VG_BAND_Y + (int)i * 10, n == cur, lab, tag, 1, 0);
    }
    vg_scrollbar(u, VG_BAND_Y, 40, first, 4, (int)total);
    {
        char chip[12];
        str_cpy(chip, vd_eng_name(TSEL), sizeof chip);
        if (fav_has(fav_key()))
            str_cpy(chip + str_len(chip), " *", 3);
        vd_footer(chip, "SH+TURN=ENGINE");
    }
}
static void vd_presets_turn(int32_t s, int shift)
{
    uint32_t total, cur;
    if (is_drum(TSEL)) {
        TDRUM->p[P_E0] = (int16_t)clamp(TDRUM->p[P_E0] + s, 0, DRUM_KITS - 1);
        return;
    }
    if (shift) {
        select_engine((TSEL->eng_req + (s > 0 ? 1u : NENGINES - 1u)) % NENGINES);
        return;
    }
    cur = preset_pos(&total);
    if (total)
        preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
}

/* ------------------------------------------------------------ MIXER --- */
/* the four tracks as xtro's MIXER bars: the level (the drums: GLO > DRUMS LEVEL), a muted track a hatched ghost, a
 * soloed one a pip, the cursor's number inverted; the readout its sound and level or pan */
static void vd_mixer_draw(void)
{
    u8g2_t *u = vu.u;
    uint32_t i;
    char b[24], v[8];
    const char *unit = "";
    {   /* MASTER: the fifth column (xtro's panel has no master pot) */
        int x = 116, h = (int)(song.master_q12 * 20u / 4096u), sel = vu.mix_sel == NTRK;
        u8g2_SetFont(u, u8g2_font_5x7_tr);
        if (sel) {
            u8g2_DrawBox(u, x - 1, VG_BAND_Y, 7, 8);
            u8g2_SetDrawColor(u, 0);
        }
        u8g2_DrawStr(u, x, VG_BAND_Y + 7, "M");
        u8g2_SetDrawColor(u, 1);
        u8g2_DrawBox(u, x, VG_BAND_Y + 30 - h, 5, h);
        u8g2_DrawHLine(u, x - 1, VG_BAND_Y + 31, 7);
        if (sel)
            u8g2_DrawBox(u, x - 1, VG_BAND_Y + 33, 7, 1);
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        int x = 4 + (int)i * 28, lv = (int)trk_level(i), h = lv * 20 / 127, sel = (int)i == vu.mix_sel;
        char n[2] = {(char)('1' + i), 0};
        u8g2_SetFont(u, u8g2_font_5x7_tr);
        if (sel) {
            u8g2_DrawBox(u, x - 1, VG_BAND_Y, 7, 8);
            u8g2_SetDrawColor(u, 0);
        }
        u8g2_DrawStr(u, x, VG_BAND_Y + 7, n);
        u8g2_SetDrawColor(u, 1);
        if ((song.solo >> i) & 1u)
            u8g2_DrawBox(u, x + 7, VG_BAND_Y + 1, 2, 2);
        vg_vrule(u, x + 14, VG_BAND_Y + 10, 21, 1, 3);  /* the rail */
        if (t->p[P_MUTE])
            vg_hatch(u, x + 2, VG_BAND_Y + 30 - h, 9, h);
        else
            u8g2_DrawBox(u, x + 2, VG_BAND_Y + 30 - h, 9, h);
        u8g2_DrawHLine(u, x + 1, VG_BAND_Y + 31, 11);
        if (sel)
            u8g2_DrawBox(u, x + 1, VG_BAND_Y + 33, 11, 1);
        {   /* the pan: a marker on a dotted line */
            int p = t->p[P_PAN];
            vg_rule(u, x + 16, VG_BAND_Y + 31, 9, 1, 2);
            u8g2_DrawVLine(u, x + 20 + p * 4 / 64, VG_BAND_Y + 29, 3);
        }
    }
    if (vu.mix_sel == NTRK) {
        u8g2_SetFont(u, u8g2_font_5x7_tr);
        vg_prompt(u, 0, VG_FOOT_Y - 2, "MASTER");
        fmt_int(v, (int32_t)(song.master_q12 * 100u / 4096u));
        str_cpy(v + str_len(v), "%", 2);
        u8g2_DrawStr(u, VG_W - u8g2_GetStrWidth(u, v), VG_FOOT_Y - 2, v);
    } else {
        track_t *t = &trk[clamp(vu.mix_sel, 0, NTRK - 1)];
        const param_desc_t *d = &TP[P_LEVEL];
        vd_sound_name(t, b);
        b[10] = 0;
        vd_upper(b);
        u8g2_SetFont(u, u8g2_font_5x7_tr);
        vg_prompt(u, 0, VG_FOOT_Y - 2, b);
        if (t->p[P_MUTE]) {
            str_cpy(v, "MUTE", sizeof v);
        } else {
            param_format(d, (int32_t)trk_level((uint32_t)vu.mix_sel), v, &unit);
            if (unit && *unit && str_len(v) < 5u) {
                str_cpy(v + str_len(v), unit, 4);
            }
        }
        vd_upper(v);
        u8g2_DrawStr(u, VG_W - u8g2_GetStrWidth(u, v), VG_FOOT_Y - 2, v);
    }
    vd_footer("MIXER", vu.edit ? "TURN=LEVEL" : "PUSH=LEVEL MUTE=MUTE");
}
static void vd_mixer_turn(int32_t s, int shift)
{
    track_t *t = &trk[clamp(vu.mix_sel, 0, NTRK - 1)];
    if (vu.edit && vu.mix_sel == NTRK) {
        song.master_q12 = (uint32_t)clamp((int32_t)song.master_q12 + s * 64, 0, 4096);
        return;
    }
    if (shift && vu.mix_sel < NTRK) {                                        /* the pan */
        t->p[P_PAN] = (int16_t)clamp(t->p[P_PAN] + s * 2, TP[P_PAN].min, TP[P_PAN].max);
        return;
    }
    if (!vu.edit) {
        vu.mix_sel = (int16_t)clamp(vu.mix_sel + s, 0, NTRK);
        return;
    }
    if (is_drum(t))
        song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + s, GP[G_DRLVL].min, GP[G_DRLVL].max);
    else
        t->p[P_LEVEL] = (int16_t)clamp(t->p[P_LEVEL] + s, TP[P_LEVEL].min, TP[P_LEVEL].max);
}

/* ------------------------------------------------------------- SONG --- */
/* rows: the sections A-D, the chain's steps, its LENGTH, MODE (loop / song), SAVE SONG */
static int vd_song_rows(void) { return 4 + arrangement.count + 3; }
static void vd_song_draw(void)
{
    u8g2_t *u = vu.u;
    int rows = vd_song_rows(), i;
    vu.song_row = (int16_t)clamp(vu.song_row, 0, rows - 1);
    if (vu.song_row < vu.song_first)
        vu.song_first = vu.song_row;
    if (vu.song_row >= vu.song_first + 4)
        vu.song_first = (int16_t)(vu.song_row - 3);
    u8g2_SetFont(u, u8g2_font_6x10_tr);
    for (i = 0; i < 4 && vu.song_first + i < rows; i++) {
        int r = vu.song_first + i, sel = r == vu.song_row;
        char lab[20], val[12];
        val[0] = 0;
        if (r < 4) {
            str_cpy(lab, "SECTION ", sizeof lab);
            lab[8] = (char)('A' + r);
            lab[9] = 0;
            str_cpy(val, live_sec == r ? "> LIVE" : project_used((uint32_t)r) ? "USED" : "EMPTY", sizeof val);
        } else if (r < 4 + arrangement.count) {
            const arr_entry_t *e = &arrangement.entry[r - 4];
            fmt_int(lab, r - 3);
            str_cpy(lab + str_len(lab), ". ", 3);
            lab[str_len(lab) + 1] = 0;
            lab[str_len(lab)] = (char)('A' + e->scene % 4u);
            fmt_int(val, e->bars);
            str_cpy(val + str_len(val), " BAR", 5);
            if (song.playing && arrangement_enabled && arrangement_clock.index == r - 4)
                str_cpy(lab + str_len(lab), " <", 3);
        } else if (r == 4 + arrangement.count) {
            str_cpy(lab, "LENGTH", sizeof lab);
            fmt_int(val, arrangement.count);
        } else if (r == 5 + arrangement.count) {
            str_cpy(lab, "MODE", sizeof lab);
            str_cpy(val, arrangement_enabled ? "SONG" : "LOOP", sizeof val);
        } else {
            str_cpy(lab, "SAVE SONG", sizeof lab);
        }
        vg_list_row(u, VG_BAND_Y + i * 10, sel, lab, val[0] ? val : 0, 1, sel && vu.edit);
    }
    vg_scrollbar(u, VG_BAND_Y, 40, vu.song_first, 4, rows);
    vd_footer("SONG", vu.song_row < 4 ? "PUSH=LOAD SH+PUSH=STORE" : vu.edit ? "TURN=SECT SH+TURN=BARS" : "PUSH=EDIT");
}
static int vd_song_stopped(void)
{
    if (song.playing || transport_req) {
        ui_message("STOP FIRST");
        return 0;
    }
    return 1;
}
static void vd_song_turn(int32_t s, int shift)
{
    int r = vu.song_row;
    if (!vu.edit) {
        vu.song_row = (int16_t)clamp(vu.song_row + s, 0, vd_song_rows() - 1);
        return;
    }
    if (!vd_song_stopped())
        return;
    if (r >= 4 && r < 4 + arrangement.count) {          /* as the FM-1's SONG screen: KNOB 2 the section, 3 the bars */
        arr_entry_t *e = &arrangement.entry[r - 4];
        if (shift)
            e->bars = (uint8_t)clamp(e->bars + s, 1, 64);
        else
            e->scene = (uint8_t)clamp(e->scene + s, 0, 3);
    } else if (r == 4 + arrangement.count) {            /* KNOB 4: the length */
        arrangement.count = (uint8_t)clamp(arrangement.count + s, 1, ARR_STEPS);
        vu.song_row = (int16_t)(4 + arrangement.count);
    }
}
static void vd_song_push(int shift)
{
    int r = vu.song_row;
    if (r < 4) {                                        /* a section: SHIFT stores the loop into it, else it loads */
        if (shift) {
            section_store((uint32_t)r);
            vd_say("STORED ", SEC_NAME[r]);
        } else if (vd_song_stopped()) {
            if (!project_used((uint32_t)r)) {
                ui_message("EMPTY SECTION");
                return;
            }
            section_load((uint32_t)r);
            vd_say("LOADED ", SEC_NAME[r]);
        }
        return;
    }
    if (r < 4 + arrangement.count + 1) {                /* a step or the length: edit */
        vu.edit = (uint8_t)!vu.edit;
        return;
    }
    if (!vd_song_stopped())
        return;
    if (r == 5 + arrangement.count) {
        arrangement_enabled ^= 1u;
        ui_message(arrangement_enabled ? "SONG MODE" : "LOOP MODE");
    } else {
        arrangement_save();
    }
}

/* ------------------------------------------------------------ SYSTEM --- */
static void vd_system_draw(void)
{
    u8g2_t *u = vu.u;
    char b[24];
    uint32_t p, i, busy = 0, cpu = song.cpu_q8 * 100u / 256u;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            busy += trk[p].v[i].active && trk[p].v[i].stage != 4u;
    u8g2_SetFont(u, u8g2_font_6x10_tr);
    u8g2_DrawGlyph(u, 0, VG_BAND_Y + 8, '>');
    u8g2_DrawStr(u, 8, VG_BAND_Y + 8, "CPU");
    vg_meter(u, 34, VG_BAND_Y + 2, 70, (int)(cpu > 100u ? 100u : cpu), 100, 1);
    u8g2_SetFont(u, u8g2_font_5x7_tr);
    fmt_int(b, (int32_t)cpu);
    str_cpy(b + str_len(b), "%", 2);
    u8g2_DrawStr(u, VG_W - 1 - u8g2_GetStrWidth(u, b), VG_BAND_Y + 8, b);
    u8g2_SetFont(u, u8g2_font_6x10_tr);
    u8g2_DrawGlyph(u, 0, VG_BAND_Y + 19, '>');
    u8g2_DrawStr(u, 8, VG_BAND_Y + 19, "VOX");
    for (i = 0; i < NVOICE; i++) {                      /* one cell a voice of the budget, solid = sounding */
        if (i < busy)
            u8g2_DrawBox(u, 34 + (int)i * 9, VG_BAND_Y + 12, 7, 7);
        else
            u8g2_DrawFrame(u, 34 + (int)i * 9, VG_BAND_Y + 12, 7, 7);
    }
    vg_rule(u, 0, VG_BAND_Y + 22, VG_W, 1, 2);
    u8g2_SetFont(u, u8g2_font_5x7_tr);
    str_cpy(b, cpu1_cores(), sizeof b);
    u8g2_DrawStr(u, 2, VG_BAND_Y + 31, b);
    str_cpy(b, "CLK ", sizeof b);
    fmt_int(b + 4, (int32_t)vu.mhz);
    str_cpy(b + str_len(b), "M", 2);
    u8g2_DrawStr(u, 2, VG_BAND_Y + 39, b);
    str_cpy(b, flash_ok ? "STORE OK" : "RAM ONLY", sizeof b);
    u8g2_DrawStr(u, 62, VG_BAND_Y + 31, b);
    {
        uint32_t s = fm1_ms / 1000u;
        str_cpy(b, "UP ", sizeof b);
        fmt_int(b + 3, (int32_t)(s / 3600u % 100u));
        str_cpy(b + str_len(b), ":", 2);
        if (s / 60u % 60u < 10u)
            str_cpy(b + str_len(b), "0", 2);
        fmt_int(b + str_len(b), (int32_t)(s / 60u % 60u));
        str_cpy(b + str_len(b), ":", 2);
        if (s % 60u < 10u)
            str_cpy(b + str_len(b), "0", 2);
        fmt_int(b + str_len(b), (int32_t)(s % 60u));
        u8g2_DrawStr(u, 62, VG_BAND_Y + 39, b);
    }
    vd_footer("SYSTEM", "CH 83 PUBLIC ACCESS");
}

/* -------------------------------------------------------------- HELP --- */
static const char *const VD_HELP[] = {
    "PAGE=NEXT SCREEN", "SH+PAGE=PREV SCREEN", "TRACK=NEXT TRACK", "PLAY=START/STOP", "SH+PLAY=RECORD",
    "MUTE=MUTE  SH=SOLO", "OCT-/+=MIDI OCTAVE", "SH+OCT-/+=UNDO/REDO", "ENC PUSH=EDIT/STEP", "ENC LONG=PATTERN",
    "POTS=CUTOFF/RESO", "MIDI IN=PLAY TRACK",
};
#define VD_HELP_N (sizeof VD_HELP / sizeof VD_HELP[0])
static void vd_help_draw(void)
{
    u8g2_t *u = vu.u;
    uint32_t i;
    vu.help_first = (int16_t)clamp(vu.help_first, 0, (int32_t)VD_HELP_N - 6);
    u8g2_SetFont(u, u8g2_font_5x7_tr);
    for (i = 0; i < 6u; i++)
        vg_prompt(u, 0, VG_BAND_Y + 6 + (int)i * 7, VD_HELP[vu.help_first + (int32_t)i]);
    vg_scrollbar(u, VG_BAND_Y, 40, vu.help_first, 6, (int)VD_HELP_N);
    vd_footer("HELP", "PAGE=BACK");
}

/* --------------------------------------------------------------- BOOT --- */
/* xtro's boot console (hx::splash): the stage that just finished closed out with its result, the one in progress
 * under a block cursor, the wordmark bent and sliced, the framed bar a step a stage; on the air: SIGNAL ACQUIRED */
#define VD_BOOT_STAGES 7
static uint8_t vd_boot_n;
static char vd_boot_prev[20], vd_boot_res[10];
static void vd_boot_draw(const char *next)
{
    u8g2_t *u = vu.u;
    int end;
    u8g2_ClearBuffer(u);
    vg_frame(u, 0, 0, VG_W, VG_H);
    u8g2_SetFont(u, u8g2_font_4x6_tr);
    if (vd_boot_prev[0]) {
        int tw, tx;
        end = vg_prompt(u, 5, 8, vd_boot_prev);
        tw = u8g2_GetStrWidth(u, vd_boot_res);
        tx = VG_W - 6 - tw;
        if (tx - 3 > end + 3)
            vg_rule(u, end + 3, 6, tx - 3 - (end + 3), 1, 2);
        u8g2_DrawStr(u, tx, 8, vd_boot_res);
    }
    if (next) {
        end = vg_prompt(u, 5, 15, next);
        vg_block_cursor(u, end < VG_W - 8 ? end : VG_W - 8, 15);
    } else {
        vg_prompt(u, 5, 15, FELUCCA_VERSION);
    }
    u8g2_DrawXBM(u, (VG_W - VD_WORD_W) / 2, 19, VD_WORD_W, VD_WORD_H, VD_WORD_BITS);
    u8g2_SetFont(u, u8g2_font_5x7_tr);
    u8g2_DrawStr(u, (VG_W - u8g2_GetStrWidth(u, "CIVIC-TV  CH 83")) / 2, 44, "CIVIC-TV  CH 83");
    vg_bar(u, 4, 48, VG_W - 8, vd_boot_n >= VD_BOOT_STAGES ? VG_W - 12 : vd_boot_n * (VG_W - 12) / VD_BOOT_STAGES);
    u8g2_SetFont(u, u8g2_font_4x6_tr);
    u8g2_DrawStr(u, 5, 61, next ? "PLEASE STAND BY" : "LONG LIVE THE NEW FLESH");
    u8g2_DrawStr(u, VG_W - 5 - u8g2_GetStrWidth(u, "PUBLIC ACCESS"), 61, next ? "PUBLIC ACCESS" : "");
    u8g2_SendBuffer(u);
}
/* a stage finished (done, its result), the next under way (0: on the air) */
static void vd_boot_log(const char *done, const char *res, const char *next)
{
    if (done) {
        vd_boot_n++;
        str_cpy(vd_boot_prev, done, sizeof vd_boot_prev);
        str_cpy(vd_boot_res, res, sizeof vd_boot_res);
    }
    if (vu.u)
        vd_boot_draw(next);
}

/* ------------------------------------------------------------- input --- */
static void vd_screen_go(int32_t d)
{
    vu.scr = (uint8_t)(((int32_t)vu.scr + d + VS_COUNT) % VS_COUNT);
    vu.edit = 0;
}
/* the pots: CUTOFF and RESONANCE of the selected track. Its engine's own (the first EDIT parameter labelled as a
 * cutoff / a resonance), the drums and an engine without one: the track filter (FX > FILTER) for CUTOFF */
static uint32_t vd_pot_param(const track_t *t, int res)
{
    static const char *const CUT[] = {"CUT", "CUTOFF", "FREQ", "FC", "VCF", "LPF"};
    static const char *const RES[] = {"RES", "RESO", "EMPH", "PEAK", "Q"};
    uint32_t id, k;
    if (!is_drum(t))
        for (id = P_E0; id < P_E0 + 8u; id++) {
            const param_desc_t *d = track_desc(t, id);
            if (!d || !d->label)
                continue;
            for (k = 0; k < (res ? sizeof RES / sizeof RES[0] : sizeof CUT / sizeof CUT[0]); k++)
                if (str_eq(d->label, res ? RES[k] : CUT[k]))
                    return id;
        }
    return res ? 0xFFu : P_TFLT;
}
static void vd_pot(uint32_t k, int32_t raw)
{
    track_t *t = TSEL;
    uint32_t id = vd_pot_param(t, (int)k);
    const param_desc_t *d;
    char val[16];
    const char *unit = "";
    if (id == 0xFFu)
        return;
    d = track_desc(t, id);
    if (!d)
        return;
    t->p[id] = (int16_t)(d->min + (raw * (d->max - d->min) + 2047) / 4095);   /* the knob always wins, as on xtro */
    param_format(d, t->p[id], val, &unit);
    if (unit && *unit)
        str_cpy(val + str_len(val), unit, 6);
    vd_say(k ? "RES " : "CUT ", val);
}
static void vd_input(const vd_ctl_t *c)
{
    int shift = (c->held >> VB_SHIFT) & 1u;
    uint32_t pr = c->pressed;
    if (c->enc || c->push || c->push_long || pr || c->held || c->pot[0] >= 0 || c->pot[1] >= 0)
        ui_input_ms = fm1_ms;                           /* (the FM-1's: the autosave waits for quiet hands) */
    if (pr & 1u << VB_PAGE)
        vd_screen_go(shift ? -1 : 1);
    if (pr & 1u << VB_TRACK)
        track_select((song.sel + (shift ? NTRK - 1u : 1u)) % NTRK);
    if (pr & 1u << VB_PLAY) {
        if (shift) {
            rec_toggle();
        } else if (!song.playing && arrangement_enabled && !arr_valid(&arrangement, arrangement_ready())) {
            ui_message("EMPTY SECTION: REC");
        } else if (!ft_owns_press()) {
            transport_req = song.playing ? 2 : 1;
        }
    }
    if (pr & 1u << VB_MUTE) {
        uint32_t i = vu.scr == VS_MIXER && vu.mix_sel < NTRK ? (uint32_t)vu.mix_sel : song.sel;
        if (shift) {
            song.solo ^= (uint8_t)(1u << i);
            ui_message((song.solo >> i) & 1u ? "SOLO" : "SOLO OFF");
        } else {
            trk[i].p[P_MUTE] = (int16_t)!trk[i].p[P_MUTE];
            ui_message(trk[i].p[P_MUTE] ? "MUTED" : "UNMUTED");
        }
    }
    if (pr & (1u << VB_OCTDN | 1u << VB_OCTUP)) {
        int both = (c->held & (1u << VB_OCTDN | 1u << VB_OCTUP)) == (1u << VB_OCTDN | 1u << VB_OCTUP);
        if (shift) {
            if (undo_swap((pr >> VB_OCTUP) & 1u))
                ui_message((pr >> VB_OCTUP) & 1u ? "REDO" : "UNDO");
            else
                ui_message("NOTHING TO UNDO");
        } else if (both) {
            song.octave = 0;
        } else {
            song.octave += (pr >> VB_OCTDN) & 1u ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            {
                char o[4];
                fmt_int(o, song.octave);
                vd_say("OCTAVE ", o);
            }
        }
    }
    if (c->push_long) {                                 /* back to the pattern, from anywhere */
        vu.scr = VS_PATTERN;
        vu.edit = 0;
    } else if (c->push) {
        switch (vu.scr) {
        case VS_PATTERN: vd_pattern_push(); break;
        case VS_SOUND: vu.edit = (uint8_t)!vu.edit; break;
        case VS_PRESETS:
            if (!is_drum(TSEL))
                fav_toggle(!fav_has(fav_key()));
            break;
        case VS_MIXER: vu.edit = (uint8_t)!vu.edit; break;
        case VS_SONG: vd_song_push(shift); break;
        default: break;
        }
    }
    if (c->enc) {
        switch (vu.scr) {
        case VS_PATTERN: vd_pattern_turn(c->enc, shift); break;
        case VS_SOUND: vd_sound_turn(c->enc, shift); break;
        case VS_PRESETS: vd_presets_turn(c->enc, shift); break;
        case VS_MIXER: vd_mixer_turn(c->enc, shift); break;
        case VS_SONG: vd_song_turn(c->enc, shift); break;
        case VS_HELP: vu.help_first = (int16_t)(vu.help_first + c->enc); break;
        default: break;
        }
    }
    if (c->pot[0] >= 0)
        vd_pot(0, c->pot[0]);
    if (c->pot[1] >= 0)
        vd_pot(1, c->pot[1]);
}

/* ------------------------------------------------------------- frame --- */
static void vd_draw(void)
{
    u8g2_t *u = vu.u;
    u8g2_ClearBuffer(u);
    vd_header();
    switch (vu.scr) {
    case VS_PATTERN: vd_pattern_draw(); break;
    case VS_SOUND: vd_sound_draw(); break;
    case VS_PRESETS: vd_presets_draw(); break;
    case VS_MIXER: vd_mixer_draw(); break;
    case VS_SONG: vd_song_draw(); break;
    case VS_SYSTEM: vd_system_draw(); break;
    default: vd_help_draw(); break;
    }
}
/* the tile rows that changed since they were last sent, at most max of them (the I2C bus: ~1 ms a row at 1 MHz):
 * xtro's sliced push, so a frame never holds the main loop for a whole 1 KB transfer. -> rows still to send */
static uint32_t vd_ui_push(uint32_t max)
{
    u8g2_t *u = vu.u;
    uint8_t *buf = u8g2_GetBufferPtr(u);
    uint32_t r, sent = 0, left = 0;
    for (r = 0; r < 8u; r++) {
        uint8_t *row = buf + r * 128u;
        if (!memcmp(row, vu.shown + r * 128u, 128u))
            continue;
        if (sent >= max) {
            left++;
            continue;
        }
        u8g2_UpdateDisplayArea(u, 0, (uint8_t)r, 16, 1);
        memcpy(vu.shown + r * 128u, row, 128u);
        sent++;
    }
    return left;
}
