// rendercubes.cpp: sits in between worldrender.cpp and rendergl.cpp and fills the vertex array for different cube surfaces.

#include "pch.h"
#include "cube.h"

vector<vertex> verts;

void finishstrips();

void setupstrips()
{
    finishstrips();

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    vertex *buf = verts.getbuf();
    glVertexPointer(3, GL_FLOAT, sizeof(vertex), &buf->x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(vertex), &buf->r);
    glTexCoordPointer(2, GL_FLOAT, sizeof(vertex), &buf->u);
}

struct strips { vector<GLint> first; vector<GLsizei> count; };

struct stripbatch
{
    int tex;
    strips tris, tristrips, quads;   
};

stripbatch skystrips = { DEFAULT_SKY };
stripbatch stripbatches[256];
uchar renderedtex[256];
int renderedtexs = 0;

extern int ati_mda_bug;

#define RENDERSTRIPS(strips, type) \
    if(strips.first.length()) \
    { \
        if(hasMDA && !ati_mda_bug) glMultiDrawArrays_(type, strips.first.getbuf(), strips.count.getbuf(), strips.first.length()); \
        else loopv(strips.first) glDrawArrays(type, strips.first[i], strips.count[i]); \
        strips.first.setsizenodelete(0); \
        strips.count.setsizenodelete(0); \
    }

void renderstripssky()
{
    if(skystrips.tris.first.empty() && skystrips.tristrips.first.empty() && skystrips.quads.first.empty()) return;
    glDisable(GL_TEXTURE_2D);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    RENDERSTRIPS(skystrips.tris, GL_TRIANGLES);
    RENDERSTRIPS(skystrips.tristrips, GL_TRIANGLE_STRIP);
    RENDERSTRIPS(skystrips.quads, GL_QUADS);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_TEXTURE_2D);
}

void renderstrips()
{
    loopj(renderedtexs)
    {
        stripbatch &sb = stripbatches[j];
        glBindTexture(GL_TEXTURE_2D, lookupworldtexture(sb.tex)->id);
        RENDERSTRIPS(sb.tris, GL_TRIANGLES);
        RENDERSTRIPS(sb.tristrips, GL_TRIANGLE_STRIP);
        RENDERSTRIPS(sb.quads, GL_QUADS);
    }
    renderedtexs = 0;

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

void addstrip(int type, int tex, int start, int n)
{
    stripbatch *sb = NULL;
    if(tex==DEFAULT_SKY)
    {
        if(minimap) return;
        sb = &skystrips;
        loopi(n) skyfloor = min(skyfloor, verts[start + i].z); 
    }
    else
    {
        sb = &stripbatches[renderedtex[tex]];
        if(sb->tex!=tex || sb>=&stripbatches[renderedtexs])
        {
            sb = &stripbatches[renderedtex[tex] = renderedtexs++];
            sb->tex = tex;
        }
    }
    strips &s = (type==GL_QUADS ? sb->quads : (type==GL_TRIANGLES ? sb->tris : sb->tristrips));
    if(type!=GL_TRIANGLE_STRIP && s.first.length() && s.first.last()+s.count.last() == start)
    {
        s.count.last() += n;
        return;
    }
    s.first.add(start);
    s.count.add(n);
}

// generating the actual vertices is done dynamically every frame and sits at the
// leaves of all these functions, and are part of the cpu bottleneck on really slow
// machines, hence the macros.

#define vert(v1, v2, v3, ls, t1, t2) { \
	vertex &v = verts.add(); \
    v.u = t1; v.v = t2; \
    v.x = (float)(v1); v.y = (float)(v2); v.z = (float)(v3); \
    v.r = ls->r; v.g = ls->g; v.b = ls->b; v.a = 255; \
}

int nquads;
const float TEXTURESCALE = 32.0f;
bool floorstrip = false, deltastrip = false;
int oh, oy, ox, striptex;                         // the o* vars are used by the stripification
int ol3r, ol3g, ol3b, ol4r, ol4g, ol4b;      
int firstindex;
bool showm = false;

void showmip() { showm = !showm; }
void mipstats(int a, int b, int c) { if(showm) conoutf("1x1/2x2/4x4: %d / %d / %d", a, b, c); }

COMMAND(showmip, ARG_NONE);

VAR(mergestrips, 0, 1, 1);

#define stripend(verts) \
    if(floorstrip || deltastrip) { \
        int type = GL_TRIANGLE_STRIP, len = verts.length()-firstindex; \
        if(mergestrips) switch(len) { \
            case 3: type = GL_TRIANGLES; break; \
            case 4: type = GL_QUADS; swap(verts.last(), verts[verts.length()-2]); break; \
         } \
         addstrip(type, striptex, firstindex, len); \
         floorstrip = deltastrip = false; \
    }

void finishstrips() { stripend(verts); }

sqr sbright, sdark;
VARP(lighterror, 1, 4, 100);

void render_flat(int wtex, int x, int y, int size, int h, sqr *l1, sqr *l2, sqr *l3, sqr *l4, bool isceil)  // floor/ceil quads
{
    if(showm) { l3 = l1 = &sbright; l4 = l2 = &sdark; }

    Texture *t = lookupworldtexture(wtex);
    float xf = TEXTURESCALE/t->xs;
    float yf = TEXTURESCALE/t->ys;
    float xs = size*xf;
    float ys = size*yf;
    float xo = xf*x;
    float yo = yf*y;

    bool first = !floorstrip || y!=oy+size || striptex!=wtex || h!=oh || x!=ox;

    if(first)       // start strip here
    {
        stripend(verts);
        firstindex = verts.length();
        striptex = wtex;
        oh = h;
        ox = x;
        floorstrip = true;
        if(isceil)
        {
            vert(x+size, y, h, l2, xo+xs, yo);
            vert(x,      y, h, l1, xo, yo);
        }
        else
        {
            vert(x,      y, h, l1, xo,    yo);
            vert(x+size, y, h, l2, xo+xs, yo);
        }
        ol3r = l1->r;
        ol3g = l1->g;
        ol3b = l1->b;
        ol4r = l2->r;
        ol4g = l2->g;
        ol4b = l2->b;
    }
    else        // continue strip
    {
        int lighterr = lighterror*2;
        if((abs(ol3r-l3->r)<lighterr && abs(ol4r-l4->r)<lighterr        // skip vertices if light values are close enough
        &&  abs(ol3g-l3->g)<lighterr && abs(ol4g-l4->g)<lighterr
        &&  abs(ol3b-l3->b)<lighterr && abs(ol4b-l4->b)<lighterr) || !wtex)   
        {
            verts.setsizenodelete(verts.length()-2);
            nquads--;
        }
        else
        {
            uchar *p3 = (uchar *)(&verts[verts.length()-1].r);
            ol3r = p3[0];  
            ol3g = p3[1];  
            ol3b = p3[2];
            uchar *p4 = (uchar *)(&verts[verts.length()-2].r);  
            ol4r = p4[0];
            ol4g = p4[1];
            ol4b = p4[2];
        }
    }

    if(isceil)
    {
        vert(x+size, y+size, h, l3, xo+xs, yo+ys);
        vert(x,      y+size, h, l4, xo,    yo+ys); 
    }
    else
    {
        vert(x,      y+size, h, l4, xo,    yo+ys);
        vert(x+size, y+size, h, l3, xo+xs, yo+ys); 
    }

    oy = y;
    nquads++;
}

void render_flatdelta(int wtex, int x, int y, int size, float h1, float h2, float h3, float h4, sqr *l1, sqr *l2, sqr *l3, sqr *l4, bool isceil)  // floor/ceil quads on a slope
{
    if(showm) { l3 = l1 = &sbright; l4 = l2 = &sdark; }

    Texture *t = lookupworldtexture(wtex);
    float xf = TEXTURESCALE/t->xs;
    float yf = TEXTURESCALE/t->ys;
    float xs = size*xf;
    float ys = size*yf;
    float xo = xf*x;
    float yo = yf*y;

    bool first = !deltastrip || y!=oy+size || striptex!=wtex || x!=ox; 

    if(first) 
    {
        stripend(verts);
        firstindex = verts.length();
        striptex = wtex;
        ox = x;
        deltastrip = true;
        if(isceil)
        {
            vert(x+size, y, h2, l2, xo+xs, yo);
            vert(x,      y, h1, l1, xo,    yo);
        }
        else
        {
            vert(x,      y, h1, l1, xo,    yo);
            vert(x+size, y, h2, l2, xo+xs, yo);
        }
        ol3r = l1->r;
        ol3g = l1->g;
        ol3b = l1->b;
        ol4r = l2->r;
        ol4g = l2->g;
        ol4b = l2->b;
    }

    if(isceil)
    {
        vert(x+size, y+size, h3, l3, xo+xs, yo+ys); 
        vert(x,      y+size, h4, l4, xo,    yo+ys);
    }
    else
    {
        vert(x,      y+size, h4, l4, xo,    yo+ys);
        vert(x+size, y+size, h3, l3, xo+xs, yo+ys); 
    }

    oy = y;
    nquads++;
}

void render_2tris(sqr *h, sqr *s, int x1, int y1, int x2, int y2, int x3, int y3, sqr *l1, sqr *l2, sqr *l3)   // floor/ceil tris on a corner cube
{
    stripend(verts);

    Texture *t = lookupworldtexture(h->ftex);
    float xf = TEXTURESCALE/t->xs;
    float yf = TEXTURESCALE/t->ys;
    vert(x1, y1, h->floor, l1, xf*x1, yf*y1);
    vert(x2, y2, h->floor, l2, xf*x2, yf*y2);
    vert(x3, y3, h->floor, l3, xf*x3, yf*y3);
    addstrip(mergestrips ? GL_TRIANGLES : GL_TRIANGLE_STRIP, h->ftex, verts.length()-3, 3);

    t = lookupworldtexture(h->ctex);
    xf = TEXTURESCALE/t->xs;
    yf = TEXTURESCALE/t->ys;

    vert(x3, y3, h->ceil, l3, xf*x3, yf*y3);
    vert(x2, y2, h->ceil, l2, xf*x2, yf*y2);
    vert(x1, y1, h->ceil, l1, xf*x1, yf*y1);
    addstrip(mergestrips ? GL_TRIANGLES : GL_TRIANGLE_STRIP, h->ctex, verts.length()-3, 3);
    nquads++;
}

void render_tris(int x, int y, int size, bool topleft,
                 sqr *h1, sqr *h2, sqr *s, sqr *t, sqr *u, sqr *v)
{
    if(topleft)
    {
        if(h1) render_2tris(h1, s, x+size, y+size, x, y+size, x, y, u, v, s);
        if(h2) render_2tris(h2, s, x, y, x+size, y, x+size, y+size, s, t, v);
    }
    else
    {
        if(h1) render_2tris(h1, s, x, y, x+size, y, x, y+size, s, t, u);
        if(h2) render_2tris(h2, s, x+size, y, x+size, y+size, x, y+size, t, u, v);
    }
}

void render_square(int wtex, float floor1, float floor2, float ceil1, float ceil2, int x1, int y1, int x2, int y2, int size, sqr *l1, sqr *l2, bool flip)   // wall quads
{
    stripend(verts);
    if(showm) { l1 = &sbright; l2 = &sdark; }

    Texture *t = lookupworldtexture(wtex);
    float xf = TEXTURESCALE/t->xs;
    float yf = TEXTURESCALE/t->ys;
    float xs = size*xf;
    float xo = xf*(x1==x2 ? min(y1,y2) : min(x1,x2));

    if(!flip)
    {
        vert(x2, y2, ceil2, l2, xo+xs, -yf*ceil2);
        vert(x1, y1, ceil1, l1, xo,    -yf*ceil1);
        if(mergestrips) vert(x1, y1, floor1, l1, xo, -floor1*yf);
        vert(x2, y2, floor2, l2, xo+xs, -floor2*yf);
        if(!mergestrips) vert(x1, y1, floor1, l1, xo, -floor1*yf);
    }
    else
    {
        vert(x1, y1, ceil1, l1, xo,    -yf*ceil1);
        vert(x2, y2, ceil2, l2, xo+xs, -yf*ceil2);
        if(mergestrips) vert(x2, y2, floor2, l2, xo+xs, -floor2*yf);
        vert(x1, y1, floor1, l1, xo,    -floor1*yf);
        if(!mergestrips) vert(x2, y2, floor2, l2, xo+xs, -floor2*yf);
    }
    addstrip(mergestrips ? GL_QUADS : GL_TRIANGLE_STRIP, wtex, verts.length()-4, 4);
    nquads++;
}

void resetcubes()
{
    verts.setsizenodelete(0);

    floorstrip = deltastrip = false;
    nquads = 0;

    sbright.r = sbright.g = sbright.b = 255;
    sdark.r = sdark.g = sdark.b = 0;

    resetwater();
}   

struct shadowvertex { float u, v, x, y, z; };
vector<shadowvertex> shadowverts;

static void resetshadowverts()
{
    shadowverts.setsizenodelete(0);

    floorstrip = deltastrip = false;
}

static void rendershadowstrips()
{
    stripend(shadowverts);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    shadowvertex *buf = shadowverts.getbuf();
    glVertexPointer(3, GL_FLOAT, sizeof(shadowvertex), &buf->x);
    glTexCoordPointer(2, GL_FLOAT, sizeof(shadowvertex), &buf->u);

    loopj(renderedtexs)
    {
        stripbatch &sb = stripbatches[j];
        RENDERSTRIPS(sb.tris, GL_TRIANGLES);
        RENDERSTRIPS(sb.tristrips, GL_TRIANGLE_STRIP);
        RENDERSTRIPS(sb.quads, GL_QUADS);
    }
    renderedtexs = 0;

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    xtraverts += shadowverts.length();
}

#define shadowvert(v1, v2, v3) { \
    shadowvertex &v = shadowverts.add(); \
    v.x = (float)(v1); v.y = (float)(v2); v.z = (float)(v3); \
    v.u = v.x*shadowtexgenS.x + v.y*shadowtexgenS.y + shadowtexgenS.z; \
    v.v = v.x*shadowtexgenT.x + v.y*shadowtexgenT.y + shadowtexgenT.z; \
}

vec shadowtexgenS, shadowtexgenT;

void rendershadow_tri(sqr *h, int x1, int y1, int x2, int y2, int x3, int y3)   // floor tris on a corner cube
{
    stripend(shadowverts);

    shadowvert(x1, y1, h->floor);
    shadowvert(x2, y2, h->floor);
    shadowvert(x3, y3, h->floor);
    addstrip(mergestrips ? GL_TRIANGLES : GL_TRIANGLE_STRIP, DEFAULT_FLOOR, shadowverts.length()-3, 3);
}

void rendershadow_tris(int x, int y, bool topleft, sqr *h1, sqr *h2)
{
    if(topleft)
    {
        if(h1) rendershadow_tri(h1, x+1, y+1, x, y+1, x, y);
        if(h2) rendershadow_tri(h2, x, y, x+1, y, x+1, y+1);
    }
    else
    {
        if(h1) rendershadow_tri(h1, x, y, x+1, y, x, y+1);
        if(h2) rendershadow_tri(h2, x+1, y, x+1, y+1, x, y+1);
    }
}

static void rendershadow_flat(int x, int y, int h) // floor quads
{
    bool first = !floorstrip || y!=oy+1 || h!=oh || x!=ox;

    if(first)       // start strip here
    {
        stripend(shadowverts);
        firstindex = shadowverts.length();
        striptex = DEFAULT_FLOOR;
        oh = h;
        ox = x;
        floorstrip = true;
        shadowvert(x,   y, h);
        shadowvert(x+1, y, h);
    }
    else        // continue strip
    {
        shadowverts.setsizenodelete(shadowverts.length()-2);
    }

    shadowvert(x,   y+1, h);
    shadowvert(x+1, y+1, h);

    oy = y;
}

static void rendershadow_flatdelta(int x, int y, float h1, float h2, float h3, float h4)  // floor quads on a slope
{
    bool first = !deltastrip || y!=oy+1 || x!=ox;

    if(first)
    {
        stripend(shadowverts);
        firstindex = shadowverts.length();
        striptex = DEFAULT_FLOOR;
        ox = x;
        deltastrip = true;
        shadowvert(x,   y, h1);
        shadowvert(x+1, y, h2);
    }

    shadowvert(x,   y+1, h4);
    shadowvert(x+1, y+1, h3);

    oy = y;
}

void rendershadow(int x, int y, int xs, int ys)
{
    x = max(x, 1);
    y = max(y, 1);
    xs = min(xs, ssize-1);
    ys = min(ys, ssize-1);

    resetshadowverts();

    #define df(x) s->floor-(x->vdelta/4.0f)

    sqr *w = wmip[0];
    for(int xx = x; xx<xs; xx++) for(int yy = y; yy<ys; yy++)
    {
        sqr *s = SW(w,xx,yy);
        if(s->type==SPACE || s->type==CHF)
        {
            rendershadow_flat(xx, yy, s->floor);
        }
        else if(s->type==FHF)
        {
            sqr *t = SW(s,1,0), *u = SW(s,1,1), *v = SW(s,0,1);
            rendershadow_flatdelta(xx, yy, df(s), df(t), df(u), df(v));
        }
        else if(s->type==CORNER)
        {
            sqr *t = SW(s,1,0), *v = SW(s,0,1), *w = SW(s,0,-1), *z = SW(s,-1,0);
            bool topleft = true;
            sqr *h1 = NULL, *h2 = NULL;
            if(SOLID(z))
            {
                if(SOLID(w))      { h2 = s; topleft = false; }
                else if(SOLID(v)) { h2 = s; }
            }
            else if(SOLID(t))
            {
                if(SOLID(w))      { h1 = s; }
                else if(SOLID(v)) { h1 = s; topleft = false; }
            }
            else
            {
                bool wv = w->ceil-w->floor < v->ceil-v->floor;
                if(z->ceil-z->floor < t->ceil-t->floor)
                {
                    if(wv) { h1 = s; h2 = v; topleft = false; }
                    else   { h1 = s; h2 = w; }
                }
                else
                {
                    if(wv) { h2 = s; h1 = v; }
                    else   { h2 = s; h1 = w; topleft = false; }
                }
            }
            rendershadow_tris(xx, yy, topleft, h1, h2);
        }
    }

    rendershadowstrips();
}

