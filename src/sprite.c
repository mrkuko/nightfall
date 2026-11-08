/* sprite.c — shape library and the .spr format parser (described in assets/FORMAT.md) */
#define _POSIX_C_SOURCE 200809L
#include "sprite.h"
#include "noise.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>

/* ------------------------------------------------------------------ */
/* sprite                                                              */
/* ------------------------------------------------------------------ */

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n);
    if (!q && n) { fprintf(stderr, "nightfall: out of memory\n"); exit(1); }
    return q;
}

static void copy_name(char *dst, const char *src, size_t n)
{
    size_t i = 0;
    if (src) for (; src[i] && i + 1 < n; i++) dst[i] = src[i];
    dst[i] = 0;
}

sprite_t *sprite_new(const char *name)
{
    sprite_t *s = calloc(1, sizeof *s);
    if (!s) exit(1);
    copy_name(s->name, name, sizeof s->name);
    return s;
}

void sprite_reset(sprite_t *s)
{
    for (int i = 0; i < s->nframes; i++) free(s->frames[i].px);
    for (int i = 0; i < s->nclips; i++) free(s->clips[i].seq);
    free(s->frames); free(s->clips); free(s->props);
    s->frames = NULL; s->clips = NULL; s->props = NULL;
    s->nframes = s->capf = s->nclips = s->capc = s->nprops = s->capp = 0;
}

void sprite_free(sprite_t *s)
{
    if (!s) return;
    sprite_reset(s);
    free(s);
}

frame_t *sprite_add_frame(sprite_t *s, const char *name, int w, int h, int ax, int ay)
{
    if (s->nframes == s->capf) {
        s->capf = s->capf ? s->capf * 2 : 8;
        s->frames = xrealloc(s->frames, (size_t)s->capf * sizeof *s->frames);
    }
    frame_t *f = &s->frames[s->nframes++];
    memset(f, 0, sizeof *f);
    copy_name(f->name, name, sizeof f->name);
    f->w = w > 0 ? w : 1;
    f->h = h > 0 ? h : 1;
    f->ax = ax; f->ay = ay;
    f->px = calloc((size_t)f->w * (size_t)f->h, sizeof *f->px);
    if (!f->px) exit(1);
    return f;
}

clip_t *sprite_add_clip(sprite_t *s, const char *name, int mode, float fps, float speed,
                        const int16_t *seq, int n)
{
    if (s->nclips == s->capc) {
        s->capc = s->capc ? s->capc * 2 : 8;
        s->clips = xrealloc(s->clips, (size_t)s->capc * sizeof *s->clips);
    }
    clip_t *c = &s->clips[s->nclips++];
    memset(c, 0, sizeof *c);
    copy_name(c->name, name, sizeof c->name);
    c->mode = mode;
    c->fps = fps > 0 ? fps : 8.0f;
    c->speed = speed;
    c->n = n > 0 ? n : 1;
    c->seq = calloc((size_t)c->n, sizeof *c->seq);
    if (!c->seq) exit(1);
    for (int i = 0; i < n; i++) c->seq[i] = seq[i];
    return c;
}

void sprite_set_prop(sprite_t *s, const char *key, const char *val)
{
    for (int i = 0; i < s->nprops; i++)
        if (strcmp(s->props[i].key, key) == 0) {
            copy_name(s->props[i].val, val, sizeof s->props[i].val);
            return;
        }
    if (s->nprops == s->capp) {
        s->capp = s->capp ? s->capp * 2 : 8;
        s->props = xrealloc(s->props, (size_t)s->capp * sizeof *s->props);
    }
    prop_t *p = &s->props[s->nprops++];
    copy_name(p->key, key, sizeof p->key);
    copy_name(p->val, val, sizeof p->val);
}

const char *sprite_prop(const sprite_t *s, const char *key)
{
    for (int i = 0; i < s->nprops; i++)
        if (strcmp(s->props[i].key, key) == 0) return s->props[i].val;
    return NULL;
}

int sprite_prop_sub(const sprite_t *s, const char *key, const char *sub, char *out, int outsz)
{
    const char *v = sprite_prop(s, key);
    size_t sl = strlen(sub);
    if (!v) return 0;
    while (*v) {
        while (*v == ' ' || *v == '\t') v++;
        const char *tok = v;
        while (*v && *v != ' ' && *v != '\t') v++;
        if ((size_t)(v - tok) > sl && strncmp(tok, sub, sl) == 0 && tok[sl] == '=') {
            int n = (int)(v - tok - (long)sl - 1);
            if (n >= outsz) n = outsz - 1;
            memcpy(out, tok + sl + 1, (size_t)n);
            out[n] = 0;
            return 1;
        }
    }
    return 0;
}

int sprite_has_tag(const sprite_t *s, const char *tag)
{
    size_t tl = strlen(tag);
    const char *p = s->tags;
    while ((p = strstr(p, tag)) != NULL) {
        int startok = (p == s->tags) || p[-1] == ' ';
        int endok = p[tl] == 0 || p[tl] == ' ';
        if (startok && endok) return 1;
        p += tl;
    }
    return 0;
}

int sprite_frame_index(const sprite_t *s, const char *name)
{
    for (int i = 0; i < s->nframes; i++)
        if (strcmp(s->frames[i].name, name) == 0) return i;
    return -1;
}

int sprite_clip_index(const sprite_t *s, const char *name)
{
    for (int i = 0; i < s->nclips; i++)
        if (strcmp(s->clips[i].name, name) == 0) return i;
    return -1;
}

void sprite_regen(sprite_t *s, uint64_t seed)
{
    if (!s->gen) return;
    sprite_reset(s);
    s->seed = seed;
    s->gen(s, seed);
}

int clip_frame_at(const clip_t *c, float t)
{
    if (c->n <= 1) return c->seq[0];
    int step = (int)(t * c->fps);
    if (step < 0) step = 0;
    switch (c->mode) {
    case CLIP_ONCE:
        return c->seq[step < c->n ? step : c->n - 1];
    case CLIP_PINGPONG: {
        int period = 2 * c->n - 2;
        int k = step % period;
        return c->seq[k < c->n ? k : period - k];
    }
    case CLIP_PARAM:
        return c->seq[c->n / 2];
    default:
        return c->seq[step % c->n];
    }
}

int clip_frame_param(const clip_t *c, float v)
{
    float t = (clampf(v, -1.0f, 1.0f) + 1.0f) * 0.5f;
    int k = (int)(t * (float)(c->n - 1) + 0.5f);
    return c->seq[clampi(k, 0, c->n - 1)];
}

float clip_duration(const clip_t *c)
{
    return (float)c->n / c->fps;
}

/* ------------------------------------------------------------------ */
/* library                                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    rgb_t   c;
    int8_t  z;
    uint8_t glow, jitter, alpha, def;
} palent_t;

struct palette {
    char     name[NF_NAME];
    palent_t e[128];
};

void lib_init(library_t *lib)
{
    memset(lib, 0, sizeof *lib);
}

void lib_free(library_t *lib)
{
    for (int i = 0; i < lib->n; i++) sprite_free(lib->items[i]);
    free(lib->items);
    free(lib->pals);
    memset(lib, 0, sizeof *lib);
}

void lib_add(library_t *lib, sprite_t *s)
{
    for (int i = 0; i < lib->n; i++)
        if (strcmp(lib->items[i]->name, s->name) == 0) {
            sprite_free(lib->items[i]);
            lib->items[i] = s;
            return;
        }
    if (lib->n == lib->cap) {
        lib->cap = lib->cap ? lib->cap * 2 : 32;
        lib->items = xrealloc(lib->items, (size_t)lib->cap * sizeof *lib->items);
    }
    lib->items[lib->n++] = s;
}

sprite_t *lib_find(const library_t *lib, const char *name)
{
    for (int i = 0; i < lib->n; i++)
        if (strcmp(lib->items[i]->name, name) == 0) return lib->items[i];
    return NULL;
}

static struct palette *lib_palette(library_t *lib, const char *name, int create)
{
    for (int i = 0; i < lib->npal; i++)
        if (strcmp(lib->pals[i].name, name) == 0) return &lib->pals[i];
    if (!create) return NULL;
    if (lib->npal == lib->cappal) {
        lib->cappal = lib->cappal ? lib->cappal * 2 : 8;
        lib->pals = xrealloc(lib->pals, (size_t)lib->cappal * sizeof *lib->pals);
    }
    struct palette *p = &lib->pals[lib->npal++];
    memset(p, 0, sizeof *p);
    copy_name(p->name, name, sizeof p->name);
    return p;
}

/* ------------------------------------------------------------------ */
/* parser                                                              */
/* ------------------------------------------------------------------ */

#define MAX_ROWS 256
#define MAX_TOK 64

enum { R_NONE, R_FRAME, R_DEPTH, R_PATCH };
enum { M_TOP, M_PAL, M_CREATURE };

typedef struct {
    library_t     *lib;
    const char    *src;
    int            line;
    sprite_t      *spr;
    int            mode;
    struct palette local;       /* local palette of the current shape */
    struct palette *pal;        /* palette we are currently writing to */
    int            def_ax, def_ay, has_anchor;
    int            frame;       /* index of the current frame */
    int            frame_fresh; /* the frame has no pixels yet */
    int            fr_ax, fr_ay, fr_has_anchor;
    int            rkind, rx, ry;
    int            nrows;
    char          *rows[MAX_ROWS];
    int            errors;
} parser_t;

static void perr(parser_t *p, const char *fmt, ...)
{
    char msg[128];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    library_t *lib = p->lib;
    int slot = lib->nerrors < 8 ? lib->nerrors : 7;
    if (lib->nerrors >= 8) memmove(lib->errors[0], lib->errors[1], sizeof lib->errors[0] * 7);
    snprintf(lib->errors[slot], sizeof lib->errors[slot], "%s:%d: %s", p->src, p->line, msg);
    if (lib->nerrors < 8) lib->nerrors++;
    fprintf(stderr, "%s:%d: %s\n", p->src, p->line, msg);
    p->errors++;
}

static int tokenize(char *s, char **tok, int max)
{
    int n = 0;
    while (*s && n < max) {
        while (*s && isspace((unsigned char)*s)) s++;
        if (!*s || *s == '#') break;
        tok[n++] = s;
        while (*s && !isspace((unsigned char)*s)) s++;
        if (*s) *s++ = 0;
    }
    return n;
}

static void apply_entry(const palent_t *e, int key, int x, int y, px_t *out)
{
    rgb_t c = e->c;
    if (e->jitter) {
        int j = e->jitter;
        int d = (int)(hash2i(x, y, (uint32_t)key * 7919u) % (uint32_t)(2 * j + 1)) - j;
        c = rgb_add(c, d);
    }
    out->r = c.r; out->g = c.g; out->b = c.b;
    out->a = e->alpha ? e->alpha : 255;
    out->z = e->z;
    out->f = e->glow ? PX_GLOW : 0;
}

static void flush_rows(parser_t *p)
{
    if (p->rkind == R_NONE || !p->spr || p->frame < 0) {
        p->rkind = R_NONE;
        p->nrows = 0;
        return;
    }
    frame_t *f = &p->spr->frames[p->frame];

    if (p->rkind == R_FRAME) {
        int w = 0, h = p->nrows;
        for (int i = 0; i < h; i++) {
            int l = (int)strlen(p->rows[i]);
            if (l > w) w = l;
        }
        if (w == 0 || h == 0) { perr(p, "empty frame '%s'", f->name); goto done; }
        free(f->px);
        f->w = w; f->h = h;
        f->px = calloc((size_t)w * (size_t)h, sizeof *f->px);
        if (!f->px) exit(1);
        f->ax = p->fr_has_anchor ? p->fr_ax : p->has_anchor ? p->def_ax : w / 2;
        f->ay = p->fr_has_anchor ? p->fr_ay : p->has_anchor ? p->def_ay : h - 1;
        for (int y = 0; y < h; y++) {
            const char *r = p->rows[y];
            for (int x = 0; r[x]; x++) {
                int k = (unsigned char)r[x];
                if (k == '.' || k == ' ' || k >= 128) continue;
                if (!p->local.e[k].def) {
                    perr(p, "unknown colour key '%c' in frame '%s'", k, f->name);
                    f->px[y * w + x] = (px_t){255, 0, 255, 255, 0, 0};
                    continue;
                }
                apply_entry(&p->local.e[k], k, x, y, &f->px[y * w + x]);
            }
        }
        p->frame_fresh = 0;
    } else if (p->rkind == R_PATCH) {
        for (int y = 0; y < p->nrows; y++) {
            const char *r = p->rows[y];
            int fy = p->ry + y;
            if (fy < 0 || fy >= f->h) continue;
            for (int x = 0; r[x]; x++) {
                int fx = p->rx + x, k = (unsigned char)r[x];
                if (fx < 0 || fx >= f->w || k == ' ' || k >= 128) continue;
                px_t *dst = &f->px[fy * f->w + fx];
                if (k == '.') { memset(dst, 0, sizeof *dst); continue; }
                if (!p->local.e[k].def) { perr(p, "unknown colour key '%c'", k); continue; }
                apply_entry(&p->local.e[k], k, fx, fy, dst);
            }
        }
    } else if (p->rkind == R_DEPTH) {
        for (int y = 0; y < p->nrows && y < f->h; y++) {
            const char *r = p->rows[y];
            for (int x = 0; r[x] && x < f->w; x++) {
                int k = r[x], z;
                if (k >= '0' && k <= '9') z = k - '0';
                else if (k >= 'a' && k <= 'i') z = -(k - 'a' + 1);
                else continue;
                f->px[y * f->w + x].z = (int8_t)z;
            }
        }
    }
done:
    for (int i = 0; i < p->nrows; i++) free(p->rows[i]);
    p->nrows = 0;
    p->rkind = R_NONE;
}

static void parse_palette_line(parser_t *p, char **tok, int n)
{
    if (n < 2) { perr(p, "palette line: KEY HEX [z+N] [~N] [glow] [a=N]"); return; }
    if (strlen(tok[0]) != 1 || (unsigned char)tok[0][0] >= 128 || tok[0][0] == '.') {
        perr(p, "palette key must be a single ASCII character (not '.'): '%s'", tok[0]);
        return;
    }
    palent_t e;
    memset(&e, 0, sizeof e);
    const char *hex = tok[1][0] == '#' ? tok[1] + 1 : tok[1];
    char *end;
    unsigned long v = strtoul(hex, &end, 16);
    if (*end || strlen(hex) != 6) { perr(p, "bad colour '%s'", tok[1]); return; }
    e.c = rgb_hex((uint32_t)v);
    e.def = 1;
    for (int i = 2; i < n; i++) {
        if (tok[i][0] == 'z') e.z = (int8_t)atoi(tok[i] + 1);
        else if (tok[i][0] == '~') e.jitter = (uint8_t)clampi(atoi(tok[i] + 1), 0, 60);
        else if (strcmp(tok[i], "glow") == 0) e.glow = 1;
        else if (strncmp(tok[i], "a=", 2) == 0) e.alpha = (uint8_t)clampi(atoi(tok[i] + 2), 1, 255);
        else perr(p, "unknown palette option '%s'", tok[i]);
    }
    p->pal->e[(unsigned char)tok[0][0]] = e;
}

static void end_sprite(parser_t *p)
{
    flush_rows(p);
    if (p->spr) {
        if (p->spr->nframes == 0) {
            perr(p, "shape '%s' has no frames", p->spr->name);
            sprite_free(p->spr);
        } else {
            /* every shape has at least the clip "idle" = the first frame */
            if (p->spr->nclips == 0) {
                int16_t z = 0;
                sprite_add_clip(p->spr, "idle", CLIP_LOOP, 1, 0, &z, 1);
            }
            lib_add(p->lib, p->spr);
        }
    }
    p->spr = NULL;
    p->frame = -1;
    p->mode = M_TOP;
}

static void parse_clip(parser_t *p, char **tok, int n)
{
    /* clip NAME [MODE] [fps=N] [speed=S] : f1 f2*3 ... */
    if (n < 2) { perr(p, "clip bez mena"); return; }
    int mode = CLIP_LOOP;
    float fps = 8.0f, speed = 0.0f;
    int i = 2;
    for (; i < n && strcmp(tok[i], ":") != 0; i++) {
        if (strcmp(tok[i], "loop") == 0) mode = CLIP_LOOP;
        else if (strcmp(tok[i], "once") == 0) mode = CLIP_ONCE;
        else if (strcmp(tok[i], "pingpong") == 0) mode = CLIP_PINGPONG;
        else if (strcmp(tok[i], "param") == 0) mode = CLIP_PARAM;
        else if (strncmp(tok[i], "fps=", 4) == 0) fps = (float)atof(tok[i] + 4);
        else if (strncmp(tok[i], "speed=", 6) == 0) speed = (float)atof(tok[i] + 6);
        else perr(p, "unknown clip option '%s'", tok[i]);
    }
    if (i >= n) { perr(p, "clip '%s': missing ':' and a frame list", tok[1]); return; }
    int16_t seq[512];
    int ns = 0;
    for (i++; i < n; i++) {
        char *star = strchr(tok[i], '*');
        int rep = 1;
        if (star) { *star = 0; rep = clampi(atoi(star + 1), 1, 200); }
        int fi = sprite_frame_index(p->spr, tok[i]);
        if (fi < 0) { perr(p, "clip '%s': unknown frame '%s'", tok[1], tok[i]); continue; }
        for (int r = 0; r < rep && ns < 512; r++) seq[ns++] = (int16_t)fi;
    }
    if (ns == 0) { perr(p, "clip '%s' is empty", tok[1]); return; }
    sprite_add_clip(p->spr, tok[1], mode, fps, speed, seq, ns);
}

static void join_tokens(char *out, size_t outsz, char **tok, int from, int n)
{
    out[0] = 0;
    for (int i = from; i < n; i++) {
        if (i > from) strncat(out, " ", outsz - strlen(out) - 1);
        strncat(out, tok[i], outsz - strlen(out) - 1);
    }
}

static void parse_line(parser_t *p, char *line)
{
    /* raster row */
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '|') {
        if (p->rkind == R_NONE) {
            if (!p->spr || p->frame < 0) { perr(p, "raster row outside a frame"); return; }
            p->rkind = p->frame_fresh ? R_FRAME : R_PATCH;
            p->rx = p->ry = 0;
        }
        if (p->nrows >= MAX_ROWS) { perr(p, "too many rows"); return; }
        char *r = s + 1;
        size_t l = strlen(r);
        while (l && (r[l - 1] == '\r' || r[l - 1] == '\n' || r[l - 1] == ' ' || r[l - 1] == '\t')) r[--l] = 0;
        p->rows[p->nrows++] = strdup(r);
        return;
    }
    flush_rows(p);

    char *tok[MAX_TOK];
    int n = tokenize(s, tok, MAX_TOK);
    if (n == 0) return;

    if (p->mode == M_PAL) {
        if (strcmp(tok[0], "end") == 0) { p->mode = M_TOP; return; }
        parse_palette_line(p, tok, n);
        return;
    }
    if (p->mode == M_CREATURE) {
        if (strcmp(tok[0], "end") == 0) { p->mode = M_TOP; return; }
        char key[NF_NAME], val[384];
        int from = 1;
        if (strcmp(tok[0], "point") == 0 && n >= 2) {
            snprintf(key, sizeof key, "point.%s", tok[1]);
            from = 2;
        } else {
            snprintf(key, sizeof key, "%s", tok[0]);
        }
        join_tokens(val, sizeof val, tok, from, n);
        sprite_set_prop(p->spr, key, val);
        return;
    }

    const char *cmd = tok[0];
    if (strcmp(cmd, "palette") == 0) {
        if (p->spr) {
            p->pal = &p->local;            /* local palette of the shape */
        } else {
            if (n < 2) { perr(p, "a named palette needs a name"); return; }
            p->pal = lib_palette(p->lib, tok[1], 1);
        }
        p->mode = M_PAL;
        return;
    }
    if (strcmp(cmd, "sprite") == 0) {
        end_sprite(p);
        if (n < 2) { perr(p, "sprite bez mena"); return; }
        p->spr = sprite_new(tok[1]);
        snprintf(p->spr->src, sizeof p->spr->src, "%s", p->src);
        memset(&p->local, 0, sizeof p->local);
        p->has_anchor = 0;
        p->frame = -1;
        return;
    }
    if (!p->spr) { perr(p, "command '%s' outside 'sprite'", cmd); return; }

    if (strcmp(cmd, "end") == 0) { end_sprite(p); return; }
    if (strcmp(cmd, "tags") == 0) { join_tokens(p->spr->tags, sizeof p->spr->tags, tok, 1, n); return; }
    if (strcmp(cmd, "uses") == 0) {
        for (int i = 1; i < n; i++) {
            struct palette *pl = lib_palette(p->lib, tok[i], 0);
            if (!pl) { perr(p, "unknown palette '%s'", tok[i]); continue; }
            for (int k = 0; k < 128; k++) if (pl->e[k].def) p->local.e[k] = pl->e[k];
        }
        return;
    }
    if (strcmp(cmd, "anchor") == 0 && n >= 3) {
        p->def_ax = atoi(tok[1]); p->def_ay = atoi(tok[2]); p->has_anchor = 1;
        return;
    }
    if (strcmp(cmd, "frame") == 0) {
        if (n < 2) { perr(p, "frame bez mena"); return; }
        if (sprite_frame_index(p->spr, tok[1]) >= 0) perr(p, "duplicate frame '%s'", tok[1]);
        int base = -1;
        p->fr_has_anchor = 0;
        for (int i = 2; i < n; i++) {
            if (strcmp(tok[i], "copy") == 0 && i + 1 < n) {
                base = sprite_frame_index(p->spr, tok[++i]);
                if (base < 0) perr(p, "frame '%s': unknown base frame '%s'", tok[1], tok[i]);
            } else if (strcmp(tok[i], "at") == 0 && i + 2 < n) {
                p->fr_ax = atoi(tok[i + 1]); p->fr_ay = atoi(tok[i + 2]);
                p->fr_has_anchor = 1;
                i += 2;
            } else {
                perr(p, "unknown frame option '%s'", tok[i]);
            }
        }
        if (base >= 0) {
            frame_t *b = &p->spr->frames[base];
            int ax = p->fr_has_anchor ? p->fr_ax : b->ax, ay = p->fr_has_anchor ? p->fr_ay : b->ay;
            frame_t *f = sprite_add_frame(p->spr, tok[1], b->w, b->h, ax, ay);
            b = &p->spr->frames[base]; /* the array may have moved */
            memcpy(f->px, b->px, (size_t)b->w * (size_t)b->h * sizeof *f->px);
            p->frame_fresh = 0;
        } else {
            sprite_add_frame(p->spr, tok[1], 1, 1, 0, 0);
            p->frame_fresh = 1;
        }
        p->frame = p->spr->nframes - 1;
        return;
    }
    if (strcmp(cmd, "depth") == 0) {
        if (p->frame < 0) { perr(p, "depth outside a frame"); return; }
        p->rkind = R_DEPTH;
        return;
    }
    if (strcmp(cmd, "patch") == 0) {
        if (p->frame < 0 || p->frame_fresh) { perr(p, "patch needs an existing frame"); return; }
        p->rkind = R_PATCH;
        p->rx = n >= 2 ? atoi(tok[1]) : 0;
        p->ry = n >= 3 ? atoi(tok[2]) : 0;
        return;
    }
    if (strcmp(cmd, "clip") == 0) { parse_clip(p, tok, n); return; }
    if (strcmp(cmd, "prop") == 0 && n >= 2) {
        char val[384];
        join_tokens(val, sizeof val, tok, 2, n);
        sprite_set_prop(p->spr, tok[1], val);
        return;
    }
    if (strcmp(cmd, "creature") == 0) { p->mode = M_CREATURE; return; }
    perr(p, "unknown command '%s'", cmd);
}

int lib_parse(library_t *lib, const char *text, const char *srcname)
{
    parser_t p;
    memset(&p, 0, sizeof p);
    p.lib = lib;
    p.src = srcname;
    p.frame = -1;
    p.pal = &p.local;

    const char *s = text;
    char buf[1024];
    while (*s) {
        const char *e = strchr(s, '\n');
        size_t l = e ? (size_t)(e - s) : strlen(s);
        if (l >= sizeof buf) l = sizeof buf - 1;
        memcpy(buf, s, l);
        buf[l] = 0;
        if (l && buf[l - 1] == '\r') buf[l - 1] = 0;
        p.line++;
        /* comment only at the start of a line ('#' is a valid key in raster rows) */
        const char *t = buf;
        while (*t == ' ' || *t == '\t') t++;
        if (*t != '#') parse_line(&p, buf);
        s = e ? e + 1 : s + l;
    }
    end_sprite(&p);
    return p.errors;
}

static char *read_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n < 0) { fclose(fp); return NULL; }
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(fp); return NULL; }
    size_t got = fread(b, 1, (size_t)n, fp);
    b[got] = 0;
    fclose(fp);
    return b;
}

static int cmpstr(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static int load_dir_rec(library_t *lib, const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) return 0;
    char *names[512];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 512) {
        if (de->d_name[0] == '.') continue;
        names[n++] = strdup(de->d_name);
    }
    closedir(d);
    qsort(names, (size_t)n, sizeof *names, cmpstr);

    /* palette files (palette*.spr) first, so the others can use them */
    int count = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < n; i++) {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", dir, names[i]);
            struct stat st;
            if (stat(path, &st) != 0) continue;
            int is_pal = strncmp(names[i], "palette", 7) == 0;
            if (S_ISDIR(st.st_mode)) {
                if (pass == 1 && depth < 2) count += load_dir_rec(lib, path, depth + 1);
                continue;
            }
            size_t l = strlen(names[i]);
            if (l < 4 || strcmp(names[i] + l - 4, ".spr") != 0) continue;
            if ((pass == 0) != is_pal) continue;
            char *txt = read_file(path);
            if (!txt) continue;
            lib_parse(lib, txt, names[i]);
            free(txt);
            count++;
        }
    }
    for (int i = 0; i < n; i++) free(names[i]);
    return count;
}

int lib_load_dir(library_t *lib, const char *dir)
{
    return load_dir_rec(lib, dir, 0);
}

extern const char *const nf_embedded_names[];
extern const char *const nf_embedded_assets[];

int lib_load_embedded(library_t *lib)
{
    int n = 0;
    /* the embed tool puts palettes first */
    for (int i = 0; nf_embedded_assets[i]; i++, n++)
        lib_parse(lib, nf_embedded_assets[i], nf_embedded_names[i]);
    return n;
}
