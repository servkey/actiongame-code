// weapon.cpp: all shooting and effects code

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"
#include "hudgun.h"

VARP(autoreload, 0, 1, 1);

vec sg[SGRAYS];

void updatelastaction(playerent *d)
{
    loopi(NUMGUNS) d->weapons[i]->updatetimers();
    d->lastaction = lastmillis;
}

void checkweaponswitch()
{
	if(!player1->weaponchanging) return;
    int timeprogress = lastmillis-player1->weaponchanging;
    if(timeprogress>weapon::weaponchangetime)
	{
        addmsg(SV_WEAPCHANGE, "ri", player1->weaponsel->type);
		player1->weaponchanging = 0;
	}
    else if(timeprogress>weapon::weaponchangetime/2)
    {
        player1->weaponsel = player1->nextweaponsel;
    }
}

void selectweapon(weapon *w)
{
    if(!w || !player1->weaponsel->deselectable()) return;
    if(w->selectable())
    {
        // substitute akimbo
        weapon *akimbo = player1->weapons[GUN_AKIMBO];
        if(w->type==GUN_PISTOL && akimbo->selectable()) w = akimbo;

        player1->weaponswitch(w);
    }
}

void selectweaponi(int w)
{
    if(player1->state == CS_ALIVE && w >= 0 && w < NUMGUNS)
    {
        selectweapon(player1->weapons[w]);
    }
}

void shiftweapon(int s)
{
    if(player1->state == CS_ALIVE)
    {
        if(!player1->weaponsel->deselectable()) return;

        weapon *curweapon = player1->weaponsel;
        weapon *akimbo = player1->weapons[GUN_AKIMBO];

        // collect available weapons
        vector<weapon *> availweapons;
        loopi(NUMGUNS)
        {
            weapon *w = player1->weapons[i];
            if(!w) continue;
            if(w->selectable() || w==curweapon || (w->type==GUN_PISTOL && player1->akimbo))
            {
                availweapons.add(w);
            }
        }

        // replace pistol by akimbo
        if(player1->akimbo)
        {
            availweapons.removeobj(akimbo); // and remove initial akimbo
            int pistolidx = availweapons.find(player1->weapons[GUN_PISTOL]);
            if(pistolidx>=0) availweapons[pistolidx] = akimbo; // insert at pistols position
            if(curweapon->type==GUN_PISTOL) curweapon = akimbo; // fix selection
        }

        // detect the next weapon
        int num = availweapons.length();
        int curidx = availweapons.find(curweapon);
        if(!num || curidx<0) return;
        int idx = (curidx+s) % num;
        if(idx<0) idx += num;
        weapon *next = availweapons[idx];
        if(next->type!=player1->weaponsel->type) // different weapon
        {
            selectweapon(next);
        }
    }
    else if(player1->isspectating()) updatefollowplayer(s);
}

int currentprimary() { return player1->primweap->type; }
int prevweapon() { return player1->prevweaponsel->type; }
int curweapon() { return player1->weaponsel->type; }

int magcontent(int w) { if(w >= 0 && w < NUMGUNS) return player1->weapons[w]->mag; else return -1;}
int magreserve(int w) { if(w >= 0 && w < NUMGUNS) return player1->weapons[w]->ammo; else return -1;}

COMMANDN(weapon, selectweaponi, ARG_1INT);
COMMAND(shiftweapon, ARG_1INT);
COMMAND(currentprimary, ARG_IVAL);
COMMAND(prevweapon, ARG_IVAL);
COMMAND(curweapon, ARG_IVAL);
COMMAND(magcontent, ARG_1EXP);
COMMAND(magreserve, ARG_1EXP);

void tryreload(playerent *p)
{
    if(!p || p->state!=CS_ALIVE || p->weaponsel->reloading || p->weaponchanging) return;
    p->weaponsel->reload();
}

void selfreload() { tryreload(player1); }
COMMANDN(reload, selfreload, ARG_NONE);

void createrays(vec &from, vec &to)             // create random spread of rays for the shotgun
{
    float f = to.dist(from)*SGSPREAD/1000;
    loopi(SGRAYS)
    {
        #define RNDD (rnd(101)-50)*f
        vec r(RNDD, RNDD, RNDD);
        sg[i] = to;
        sg[i].add(r);
        #undef RNDD
    }
}

static inline bool intersectbox(const vec &o, const vec &rad, const vec &from, const vec &to, vec *end) // if lineseg hits entity bounding box
{
    const vec *p;
    vec v = to, w = o;
    v.sub(from);
    w.sub(from);
    float c1 = w.dot(v);

    if(c1<=0) p = &from;
    else
    {
        float c2 = v.squaredlen();
        if(c2<=c1) p = &to;
        else
        {
            float f = c1/c2;
            v.mul(f).add(from);
            p = &v;
        }
    }

    if(p->x <= o.x+rad.x
       && p->x >= o.x-rad.x
       && p->y <= o.y+rad.y
       && p->y >= o.y-rad.y
       && p->z <= o.z+rad.z
       && p->z >= o.z-rad.z)
    {
        if(end) *end = *p;
        return true;
    }
    return false;
}

static inline bool intersectsphere(const vec &from, const vec &to, vec center, float radius, float &dist)
{
    vec ray(to);
    ray.sub(from);
    center.sub(from);
    float v = center.dot(ray),
          inside = radius*radius - center.squaredlen();
    if(inside < 0 && v < 0) return false;
    float raysq = ray.squaredlen(), d = inside*raysq + v*v;
    if(d < 0) return false;
    dist = (v - sqrtf(d)) / raysq;
    return dist >= 0 && dist <= 1;
}

static inline bool intersectcylinder(const vec &from, const vec &to, const vec &start, const vec &end, float radius, float &dist)
{
    vec d(end), m(from), n(to);
    d.sub(start);
    m.sub(start);
    n.sub(from);
    float md = m.dot(d),
          nd = n.dot(d),
          dd = d.squaredlen();
    if(md < 0 && md + nd < 0) return false;
    if(md > dd && md + nd > dd) return false;
    float nn = n.squaredlen(),
          mn = m.dot(n),
          a = dd*nn - nd*nd,
          k = m.squaredlen() - radius*radius,
          c = dd*k - md*md;
    if(fabs(a) < 1e-9f)
    {
        if(c > 0) return false;
        if(md < 0) dist = -mn / nn;
        else if(md > dd) dist = (nd - mn) / nn;
        else dist = 0;
        return true;
    }
    float b = dd*mn - nd*md,
          discrim = b*b - a*c;
    if(discrim < 0) return false;
    dist = (-b - sqrtf(discrim)) / a;
    if(dist < 0 || dist > 1) return false;
    float offset = md + dist*nd;
    if(offset < 0)
    {
        if(nd < 0) return false;
        dist = -md / nd;
        return k + 2*dist*(mn + dist*nn) <= 0;
    }
    else if(offset > dd)
    {
        if(nd >= 0) return 0;
        dist = (dd - md) / nd;
        return k + dd - 2*md + dist*(2*(mn-nd) + dist*nn) <= 0;
    }
    return true;
}

int intersect(playerent *d, const vec &from, const vec &to, vec *end)
{
    float dist;
    if(d->head.x >= 0)
    {
        if(intersectsphere(from, to, d->head, HEADSIZE, dist))
        {
            if(end) (*end = to).sub(from).mul(dist).add(from);
            return 2;
        }
    }
    float y = d->yaw*RAD, p = (d->pitch/4+90)*RAD, c = cosf(p);
    vec bottom(d->o), top(sinf(y)*c, -cosf(y)*c, sinf(p));
    bottom.z -= d->eyeheight;
    top.mul(d->eyeheight + d->aboveeye).add(bottom);
    if(intersectcylinder(from, to, bottom, top, d->radius, dist))
    {
        if(end) (*end = to).sub(from).mul(dist).add(from);
        return 1;
    }
    return 0;

#if 0
    const float eyeheight = d->eyeheight;
    vec o(d->o);
    o.z += (d->aboveeye - eyeheight)/2;
    return intersectbox(o, vec(d->radius, d->radius, (d->aboveeye + eyeheight)/2), from, to, end) ? 1 : 0;
#endif
}

bool intersect(entity *e, const vec &from, const vec &to, vec *end)
{
    mapmodelinfo &mmi = getmminfo(e->attr2);
    if(!&mmi || !mmi.h) return false;

    float lo = float(S(e->x, e->y)->floor+mmi.zoff+e->attr3);
    return intersectbox(vec(e->x, e->y, lo+mmi.h/2.0f), vec(mmi.rad, mmi.rad, mmi.h/2.0f), from, to, end);
}

playerent *intersectclosest(const vec &from, const vec &to, playerent *at, int &hitzone, bool aiming = true)
{
    playerent *best = NULL;
    float bestdist = 1e16f;
    int zone;
    if(at!=player1 && player1->state==CS_ALIVE && (zone = intersect(player1, from, to)))
    {
        best = player1;
        bestdist = at->o.dist(player1->o);
        hitzone = zone;
    }
    loopv(players)
    {
        playerent *o = players[i];
        if(!o || o==at || (o->state!=CS_ALIVE && (aiming || (o->state!=CS_EDITING && o->state!=CS_LAGGED)))) continue;
        float dist = at->o.dist(o->o);
        if(dist < bestdist && (zone = intersect(o, from, to)))
        {
            best = o;
            bestdist = dist;
            hitzone = zone;
        }
    }
    return best;
}

playerent *playerincrosshair()
{
    if(camera1->type == ENT_PLAYER || (camera1->type == ENT_CAMERA && player1->spectatemode == SM_DEATHCAM))
    {
        int hitzone;
        return intersectclosest(camera1->o, worldpos, (playerent *)camera1, hitzone, false);
    }
    else return NULL;
}

void damageeffect(int damage, playerent *d)
{
    particle_splash(3, damage/10, 1000, d->o);
}


vector<hitmsg> hits;

void hit(int damage, playerent *d, playerent *at, const vec &vel, int gun, bool gib, int info)
{
    if(d==player1 || d->type==ENT_BOT || !m_mp(gamemode)) d->hitpush(damage, vel, at, gun);

    if(!m_mp(gamemode)) dodamage(damage, d, at, gib);
    else
    {
        hitmsg &h = hits.add();
        h.target = d->clientnum;
        h.lifesequence = d->lifesequence;
        h.info = info;
        if(d==player1)
        {
            h.dir = ivec(0, 0, 0);
            d->damageroll(damage);
            updatedmgindicator(at->o);
            damageblend(damage);
            damageeffect(damage, d);
            audiomgr.playsound(S_PAIN6, SP_HIGH);
        }
        else
        {
            h.dir = ivec(int(vel.x*DNF), int(vel.y*DNF), int(vel.z*DNF));
            damageeffect(damage, d);
            audiomgr.playsound(S_PAIN1+rnd(5), d);
        }
    }
}

void hitpush(int damage, playerent *d, playerent *at, vec &from, vec &to, int gun, bool gib, int info)
{
    vec v(to);
    v.sub(from);
    v.normalize();
    hit(damage, d, at, v, gun, gib, info);
}

float expdist(playerent *o, vec &dir, const vec &v)
{
    vec middle = o->o;
    middle.z += (o->aboveeye-o->eyeheight)/2;
    float dist = middle.dist(v, dir);
    dir.div(dist);
    if(dist<0) dist = 0;
    return dist;
}

void radialeffect(playerent *o, vec &v, int qdam, playerent *at, int gun)
{
    if(o->state!=CS_ALIVE) return;
    vec dir;
    float dist = expdist(o, dir, v);
    if(dist<EXPDAMRAD)
    {
        int damage = (int)(qdam*(1-dist/EXPDAMRAD));
        hit(damage, o, at, dir, gun, true, int(dist*DMF));
    }
}

vector<bounceent *> bounceents;

void removebounceents(playerent *owner)
{
    loopv(bounceents) if(bounceents[i]->owner==owner) { delete bounceents[i]; bounceents.remove(i--); }
}

void movebounceents()
{
    loopv(bounceents) if(bounceents[i])
    {
        bounceent *p = bounceents[i];
        if((p->bouncetype==BT_NADE || p->bouncetype==BT_GIB) && p->applyphysics()) movebounceent(p, 1, false);
        if(!p->isalive(lastmillis))
        {
            p->destroy();
            delete p;
            bounceents.remove(i--);
        }
    }
}

void clearbounceents()
{
    if(gamespeed==100);
    else if(multiplayer(false)) bounceents.add((bounceent *)player1);
    loopv(bounceents) if(bounceents[i]) { delete bounceents[i]; bounceents.remove(i--); }
}

void renderbounceents()
{
    loopv(bounceents)
    {
        bounceent *p = bounceents[i];
        if(!p) continue;
        string model;
        vec o(p->o);

        int anim = ANIM_MAPMODEL, basetime = 0;
        switch(p->bouncetype)
        {
            case BT_NADE:
                s_strcpy(model, "weapons/grenade/static");
                break;
            case BT_GIB:
            default:
            {
                uint n = (((4*(uint)(size_t)p)+(uint)p->timetolive)%3)+1;
                s_sprintf(model)("misc/gib0%u", n);
                int t = lastmillis-p->millis;
                if(t>p->timetolive-2000)
                {
                    anim = ANIM_DECAY;
                    basetime = p->millis+p->timetolive-2000;
                    t -= p->timetolive-2000;
                    o.z -= t*t/4000000000.0f*t;
                }
                break;
            }
        }
        path(model);
        rendermodel(model, anim|ANIM_LOOP|ANIM_DYNALLOC, 0, 1.1f, o, p->yaw+90, p->pitch, 0, basetime);
    }
}

VARP(gib, 0, 1, 1);
VARP(gibnum, 0, 6, 1000);
VARP(gibttl, 0, 7000, 60000);
VARP(gibspeed, 1, 30, 100);

void addgib(playerent *d)
{
    if(!d || !gib || !gibttl) return;
    audiomgr.playsound(S_GIB, d);

    loopi(gibnum)
    {
        bounceent *p = bounceents.add(new bounceent);
        p->owner = d;
        p->millis = lastmillis;
        p->timetolive = gibttl+rnd(10)*100;
        p->bouncetype = BT_GIB;

        p->o = d->o;
        p->o.z -= d->aboveeye;
        p->inwater = hdr.waterlevel>p->o.z;

        p->yaw = (float)rnd(360);
        p->pitch = (float)rnd(360);

        p->maxspeed = 30.0f;
        p->rotspeed = 3.0f;

        const float angle = (float)rnd(360);
        const float speed = (float)gibspeed;

        p->vel.x = sinf(RAD*angle)*rnd(1000)/1000.0f;
        p->vel.y = cosf(RAD*angle)*rnd(1000)/1000.0f;
        p->vel.z = rnd(1000)/1000.0f;
        p->vel.mul(speed/100.0f);

        p->resetinterp();
    }
}

void shorten(vec &from, vec &to, vec &target)
{
    target.sub(from).normalize().mul(from.dist(to)).add(from);
}

void raydamage(vec &from, vec &to, playerent *d)
{
    int dam = d->weaponsel->info.damage;
    int hitzone = -1;
    playerent *o = NULL;

    if(d->weaponsel->type==GUN_SHOTGUN)
    {
        uint done = 0;
        playerent *cl = NULL;
        for(;;)
        {
            bool raysleft = false;
            int hitrays = 0;
            o = NULL;
            loop(r, SGRAYS) if((done&(1<<r))==0 && (cl = intersectclosest(from, sg[r], d, hitzone)))
            {
                if(!o || o==cl)
                {
                    hitrays++;
                    o = cl;
                    done |= 1<<r;
                    shorten(from, o->o, sg[r]);
                }
                else raysleft = true;
            }
            if(hitrays) hitpush(hitrays*dam, o, d, from, to, d->weaponsel->type, false, hitrays);
            if(!raysleft) break;
        }
    }
    else if((o = intersectclosest(from, to, d, hitzone)))
    {
        bool gib = false;
        if(d->weaponsel->type==GUN_KNIFE) gib = true;
    	else if(d==player1 && d->weaponsel->type==GUN_SNIPER && hitzone==2)
        {
            dam *= 3;
            gib = true;
        }

        hitpush(dam, o, d, from, to, d->weaponsel->type, gib, gib ? 1 : 0);
        shorten(from, o->o, to);
    }
}

// weapon

weapon::weapon(struct playerent *owner, int type) : type(type), owner(owner), info(guns[type]),
    ammo(owner->ammo[type]), mag(owner->mag[type]), gunwait(owner->gunwait[type]), reloading(0)
{
}

const int weapon::weaponchangetime = 400;
const float weapon::weaponbeloweye = 0.2f;

int weapon::flashtime() const { return max((int)info.attackdelay, 120)/4; }

void weapon::sendshoot(vec &from, vec &to)
{
    if(owner!=player1) return;
    addmsg(SV_SHOOT, "ri2i6iv", lastmillis, owner->weaponsel->type,
           (int)(from.x*DMF), (int)(from.y*DMF), (int)(from.z*DMF),
           (int)(to.x*DMF), (int)(to.y*DMF), (int)(to.z*DMF),
           hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
}

bool weapon::modelattacking()
{
    int animtime = min(owner->gunwait[owner->weaponsel->type], (int)owner->weaponsel->info.attackdelay);
    if(lastmillis - owner->lastaction < animtime) return true;
    else return false;
}

void weapon::attacksound()
{
    if(info.sound == S_NULL) return;
    bool local = (owner == player1);
    audiomgr.playsound(info.sound, owner, local ? SP_HIGH : SP_NORMAL);
}

bool weapon::reload()
{
    if(mag>=info.magsize || ammo<=0) return false;
    updatelastaction(owner);
    reloading = lastmillis;
    gunwait += info.reloadtime;

    int numbullets = min(info.magsize - mag, ammo);
	mag += numbullets;
	ammo -= numbullets;

    bool local = (player1 == owner);
	if(owner->type==ENT_BOT) audiomgr.playsound(info.reload, owner);
    else audiomgr.playsoundc(info.reload);
    if(local) addmsg(SV_RELOAD, "ri2", lastmillis, owner->weaponsel->type);
    return true;
}

void weapon::renderstats()
{
    char gunstats[64];
    sprintf(gunstats, "%i", mag); ///%i", mag, ammo);
    draw_text(gunstats, 690, 827);
    int offset = text_width(gunstats);
    glScalef(0.5f, 0.5f, 1.0f);
    sprintf(gunstats, "%i", ammo);
    draw_text(gunstats, (690 + offset)*2, 830*2);
    glLoadIdentity(); 
}

//VAR(recoiltest, 0, 0, 1); // FIXME ON RELEASE
int recoiltest = 0;

VAR(recoilincrease, 1, 2, 10);
VAR(recoilbase, 0, 40, 1000);
VAR(maxrecoil, 0, 1000, 1000);

void weapon::attackphysics(vec &from, vec &to) // physical fx to the owner
{
    const guninfo &g = info;
    vec unitv;
    float dist = to.dist(from, unitv);
    float f = dist/1000;
    int spread = dynspread();
    float recoil = dynrecoil()*-0.01f;

    // spread
    if(spread>1)
    {
        #define RNDD (rnd(spread)-spread/2)*f
        vec r(RNDD, RNDD, RNDD);
        to.add(r);
        #undef RNDD
    }
    // kickback & recoil
    if(recoiltest)
    {
        owner->vel.add(vec(unitv).mul(recoil/dist).mul(owner->crouching ? 0.75 : 1.0f));
        owner->pitchvel = min(powf(shots/(float)(recoilincrease), 2.0f)+(float)(recoilbase)/10.0f, (float)(maxrecoil)/10.0f);
    }
    else
    {
        owner->vel.add(vec(unitv).mul(recoil/dist).mul(owner->crouching ? 0.75 : 1.0f));
        owner->pitchvel = min(powf(shots/(float)(g.recoilincrease), 2.0f)+(float)(g.recoilbase)/10.0f, (float)(g.maxrecoil)/10.0f);
    }
}

void weapon::renderhudmodel(int lastaction, int index)
{
    vec unitv;
    float dist = worldpos.dist(owner->o, unitv);
    unitv.div(dist);

	weaponmove wm;
	if(!intermission) wm.calcmove(unitv, lastaction);
    s_sprintfd(path)("weapons/%s", info.modelname);
    bool emit = (wm.anim&ANIM_INDEX)==ANIM_GUN_SHOOT && (lastmillis - lastaction) < flashtime();
    rendermodel(path, wm.anim|ANIM_DYNALLOC|(index ? ANIM_MIRROR : 0)|(emit ? ANIM_PARTICLE : 0), 0, -1, wm.pos, player1->yaw+90, player1->pitch+wm.k_rot, 40.0f, wm.basetime, NULL, NULL, 1.28f);
}

void weapon::updatetimers()
{
    if(gunwait) gunwait = max(gunwait - (lastmillis-owner->lastaction), 0);
}

void weapon::onselecting()
{
    updatelastaction(owner);
    audiomgr.playsound(S_GUNCHANGE, owner == player1? SP_HIGH : SP_NORMAL);
}

void weapon::renderhudmodel() { renderhudmodel(owner->lastaction); }
void weapon::renderaimhelp(bool teamwarning) { drawcrosshair(owner, teamwarning ? CROSSHAIR_TEAMMATE : CROSSHAIR_DEFAULT); }
int weapon::dynspread() { return info.spread; }
float weapon::dynrecoil() { return info.recoil; }
bool weapon::selectable() { return this != owner->weaponsel && owner->state == CS_ALIVE && !owner->weaponchanging; }
bool weapon::deselectable() { return !reloading; }

void weapon::equipplayer(playerent *pl)
{
    if(!pl) return;
    pl->weapons[GUN_ASSAULT] = new assaultrifle(pl);
    pl->weapons[GUN_GRENADE] = new grenades(pl);
    pl->weapons[GUN_KNIFE] = new knife(pl);
    pl->weapons[GUN_PISTOL] = new pistol(pl);
    pl->weapons[GUN_SHOTGUN] = new shotgun(pl);
    pl->weapons[GUN_SNIPER] = new sniperrifle(pl);
    pl->weapons[GUN_SUBGUN] = new subgun(pl);
    pl->weapons[GUN_AKIMBO] = new akimbo(pl);
    pl->selectweapon(GUN_ASSAULT);
    pl->setprimary(GUN_ASSAULT);
    pl->setnextprimary(GUN_ASSAULT);
}

bool weapon::valid(int id) { return id>=0 && id<NUMGUNS; }

// grenadeent

enum { NS_NONE, NS_ACTIVATED = 0, NS_THROWED, NS_EXPLODED };

grenadeent::grenadeent (playerent *owner, int millis)
{
    ASSERT(owner);
    nadestate = NS_NONE;
    local = owner==player1;
    bounceent::owner = owner;
    bounceent::millis = lastmillis;
    timetolive = 2000-millis;
    bouncetype = BT_NADE;
    maxspeed = 30.0f;
    rotspeed = 6.0f;
    distsincebounce = 0.0f;
}

void grenadeent::explode()
{
    if(nadestate!=NS_ACTIVATED && nadestate!=NS_THROWED ) return;
    nadestate = NS_EXPLODED;
    static vec n(0,0,0);
    hits.setsizenodelete(0);
    splash();
    if(local)
        addmsg(SV_EXPLODE, "ri3iv", lastmillis, GUN_GRENADE, millis, // fixme
            hits.length(), hits.length()*sizeof(hitmsg)/sizeof(int), hits.getbuf());
    audiomgr.playsound(S_FEXPLODE, &o);
}

void grenadeent::splash()
{
    particle_splash(0, 50, 300, o);
    particle_fireball(5, o);
    addscorchmark(o);
    adddynlight(NULL, o, 16, 200, 100, 255, 255, 224);
    adddynlight(NULL, o, 16, 600, 600, 192, 160, 128);
    if(owner != player1) return;
    int damage = guns[GUN_GRENADE].damage;
    radialeffect(owner, o, damage, owner, GUN_GRENADE);
    loopv(players)
    {
        playerent *p = players[i];
        if(!p) continue;
        radialeffect(p, o, damage, owner, GUN_GRENADE);
    }
}

void grenadeent::activate(const vec &from, const vec &to)
{
    if(nadestate!=NS_NONE) return;
    nadestate = NS_ACTIVATED;

    if(local)
    {
        addmsg(SV_SHOOT, "ri2i6i", millis, owner->weaponsel->type,
               (int)(from.x*DMF), (int)(from.y*DMF), (int)(from.z*DMF),
               (int)(to.x*DMF), (int)(to.y*DMF), (int)(to.z*DMF),
               0);
        audiomgr.playsound(S_GRENADEPULL, SP_HIGH);
    }
}

void grenadeent::_throw(const vec &from, const vec &vel)
{
    if(nadestate!=NS_ACTIVATED) return;
    nadestate = NS_THROWED;
    this->vel = vel;
    this->o = from;
    this->resetinterp();
    inwater = hdr.waterlevel>o.z;

    if(local)
    {
        addmsg(SV_THROWNADE, "ri7", int(o.x*DMF), int(o.y*DMF), int(o.z*DMF), int(vel.x*DMF), int(vel.y*DMF), int(vel.z*DMF), lastmillis-millis);
        audiomgr.playsound(S_GRENADETHROW, SP_HIGH);
    }
    else audiomgr.playsound(S_GRENADETHROW, owner);
}

void grenadeent::moveoutsidebbox(const vec &direction, playerent *boundingbox)
{
    vel = direction;
    o = boundingbox->o;
    inwater = hdr.waterlevel>o.z;

    boundingbox->cancollide = false;
    loopi(10) moveplayer(this, 10, true, 10);
    boundingbox->cancollide = true;
}

void grenadeent::destroy() { explode(); }
bool grenadeent::applyphysics() { return nadestate==NS_THROWED; }

void grenadeent::oncollision()
{
    if(distsincebounce>=1.5f) audiomgr.playsound(S_GRENADEBOUNCE1+rnd(2), &o);
    distsincebounce = 0.0f;
}

void grenadeent::onmoved(const vec &dist)
{
    distsincebounce += dist.magnitude();
}

// grenades

grenades::grenades(playerent *owner) : weapon(owner, GUN_GRENADE), inhandnade(NULL), throwwait((13*1000)/40), throwmillis(0), state(GST_NONE) {}

int grenades::flashtime() const { return 0; }

bool grenades::busy() { return state!=GST_NONE; }

bool grenades::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction;
    vec &to = targ;

    bool waitdone = attackmillis>=gunwait;
    if(waitdone) gunwait = reloading = 0;

    switch(state)
    {
        case GST_NONE:
            if(waitdone && owner->attacking && this==owner->weaponsel) activatenade(to); // activate
            break;

        case GST_INHAND:
            if(waitdone)
            {
                if(!owner->attacking || this!=owner->weaponsel) thrownade(); // throw
                else if(!inhandnade->isalive(lastmillis)) dropnade(); // drop & have fun
            }
            break;

        case GST_THROWING:
            if(attackmillis >= throwwait) // throw done
            {
                reset();
                if(!mag && this==owner->weaponsel) // switch to primary immediately
                {
                    owner->weaponchanging = lastmillis-1-(weaponchangetime/2);
                    owner->nextweaponsel = owner->weaponsel = owner->primweap;
                }
                return false;
            }
            break;
    }
    return true;
}

void grenades::attackfx(const vec &from, const vec &to, int millis) // other player's grenades
{
    throwmillis = lastmillis-millis;
    if(millis == 0) audiomgr.playsound(S_GRENADEPULL, owner); // activate
    else if(millis > 0) // throw
    {
        grenadeent *g = new grenadeent(owner, millis);
        bounceents.add(g);
        g->_throw(from, to);
    }
}

int grenades::modelanim()
{
    if(state == GST_THROWING) return ANIM_GUN_THROW;
    else
    {
        int animtime = min(gunwait, (int)info.attackdelay);
        if(state == GST_INHAND || lastmillis - owner->lastaction < animtime) return ANIM_GUN_SHOOT;
    }
    return ANIM_GUN_IDLE;
}

void grenades::activatenade(const vec &to)
{
    if(!mag) return;
    throwmillis = 0;

    inhandnade = new grenadeent(owner);
    bounceents.add(inhandnade);

    updatelastaction(owner);
    mag--;
    gunwait = info.attackdelay;
    owner->lastattackweapon = this;
    state = GST_INHAND;
    inhandnade->activate(owner->o, to);
}

void grenades::thrownade()
{
    if(!inhandnade) return;
    const float speed = cosf(RAD*owner->pitch);
    vec vel(sinf(RAD*owner->yaw)*speed, -cosf(RAD*owner->yaw)*speed, sinf(RAD*owner->pitch));
    vel.mul(1.5f);
    thrownade(vel);
}

void grenades::thrownade(const vec &vel)
{
    inhandnade->moveoutsidebbox(vel, owner);
    inhandnade->_throw(inhandnade->o, vel);
    inhandnade = NULL;

    throwmillis = lastmillis;
    updatelastaction(owner);
    state = GST_THROWING;
    if(this==owner->weaponsel) owner->attacking = false;
}

void grenades::dropnade()
{
    vec n(0,0,0);
    thrownade(n);
}

void grenades::renderstats()
{
    char gunstats[64];
    sprintf(gunstats, "%i", mag);
    draw_text(gunstats, 690, 827);
}

bool grenades::selectable() { return weapon::selectable() && state != GST_INHAND && mag; }
void grenades::reset() { throwmillis = 0; state = GST_NONE; }

void grenades::onselecting() { reset(); audiomgr.playsound(S_GUNCHANGE); }
void grenades::onownerdies()
{
    reset();
    if(owner==player1 && inhandnade) dropnade();
}


// gun base class

gun::gun(playerent *owner, int type) : weapon(owner, type) {}

bool gun::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction;
	if(attackmillis<gunwait) return false;
    gunwait = reloading = 0;

    if(!owner->attacking)
    {
        shots = 0;
        checkautoreload();
        return false;
    }

    updatelastaction(owner);
    if(!mag)
    {
        audiomgr.playsoundc(S_NOAMMO);
	    gunwait += 250;
	    owner->lastattackweapon = NULL;
        shots = 0;
        checkautoreload();
        return false;
    }

    owner->lastattackweapon = this;
	shots++;

	if(!info.isauto) owner->attacking = false;

    vec from = owner->o;
    vec to = targ;
    from.z -= weaponbeloweye;

    attackphysics(from, to);

    hits.setsizenodelete(0);
    raydamage(from, to, owner);
    attackfx(from, to, 0);

    gunwait = info.attackdelay;
    mag--;

    sendshoot(from, to);
    return true;
}

void gun::attackfx(const vec &from, const vec &to, int millis)
{
    addbullethole(owner, from, to);
    addshotline(owner, from, to);
    particle_splash(0, 5, 250, to);
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
    attacksound();
}

int gun::modelanim() { return modelattacking() ? ANIM_GUN_SHOOT|ANIM_LOOP : ANIM_GUN_IDLE; }
void gun::checkautoreload() { if(autoreload && owner==player1 && !mag) reload(); }


// shotgun

shotgun::shotgun(playerent *owner) : gun(owner, GUN_SHOTGUN) {}

bool shotgun::attack(vec &targ)
{
    vec from = owner->o;
	from.z -= weaponbeloweye;
    createrays(from, targ);
    return gun::attack(targ);
}

void shotgun::attackfx(const vec &from, const vec &to, int millis)
{
    loopi(SGRAYS) particle_splash(0, 5, 200, sg[i]);
    if(addbullethole(owner, from, to))
    {
        int holes = 3+rnd(5);
        loopi(holes) addbullethole(owner, from, sg[i], 0, false);
    }
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
    attacksound();
}

bool shotgun::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }


// subgun

subgun::subgun(playerent *owner) : gun(owner, GUN_SUBGUN) {}
bool subgun::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }


// sniperrifle

sniperrifle::sniperrifle(playerent *owner) : gun(owner, GUN_SNIPER), scoped(false) {}

void sniperrifle::attackfx(const vec &from, const vec &to, int millis)
{
    addbullethole(owner, from, to);
    addshotline(owner, from, to);
    particle_splash(0, 50, 200, to);
    particle_trail(1, 500, from, to);
    adddynlight(owner, from, 4, 100, 50, 96, 80, 64);
    attacksound();
}

bool sniperrifle::reload()
{
    bool r = weapon::reload();
    if(owner==player1 && r) scoped = false;
    return r;
}

int sniperrifle::dynspread() { return scoped ? 1 : info.spread; }
float sniperrifle::dynrecoil() { return scoped ? info.recoil / 3 : info.recoil; }
bool sniperrifle::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }
void sniperrifle::onselecting() { weapon::onselecting(); scoped = false; }
void sniperrifle::ondeselecting() { scoped = false; }
void sniperrifle::onownerdies() { scoped = false; }
void sniperrifle::renderhudmodel() { if(!scoped) weapon::renderhudmodel(); }

void sniperrifle::renderaimhelp(bool teamwarning) 
{ 
    if(scoped) drawscope(); 
    if(scoped || teamwarning) drawcrosshair(owner, teamwarning ? CROSSHAIR_TEAMMATE : CROSSHAIR_SCOPE, NULL, 24.0f); 
}

void sniperrifle::setscope(bool enable) { if(this == owner->weaponsel && !reloading && owner->state == CS_ALIVE) scoped = enable; }


// assaultrifle

assaultrifle::assaultrifle(playerent *owner) : gun(owner, GUN_ASSAULT) {}

int assaultrifle::dynspread() { return shots > 3 ? 70 : info.spread; }
float assaultrifle::dynrecoil() { return info.recoil + (rnd(8)*-0.01f); }
bool assaultrifle::selectable() { return weapon::selectable() && !m_noprimary && this == owner->primweap; }


// pistol

pistol::pistol(playerent *owner) : gun(owner, GUN_PISTOL) {}
bool pistol::selectable() { return weapon::selectable() && !m_nopistol; }


// akimbo

akimbo::akimbo(playerent *owner) : gun(owner, GUN_AKIMBO), akimbomillis(0)
{
    akimbolastaction[0] = akimbolastaction[1] = 0;
}

bool akimbo::attack(vec &targ)
{
    if(gun::attack(targ))
    {
		akimbolastaction[akimboside?1:0] = lastmillis;
		akimboside = !akimboside;
        return true;
    }
    return false;
}

void akimbo::onammopicked()
{
    akimbomillis = lastmillis + 30000;
    if(owner==player1)
    {
        if(owner->weaponsel->type!=GUN_SNIPER && owner->weaponsel->type!=GUN_GRENADE) owner->weaponswitch(this);
        addmsg(SV_AKIMBO, "ri", lastmillis);
    }
}

void akimbo::onselecting()
{
    gun::onselecting();
    akimbolastaction[0] = akimbolastaction[1] = lastmillis;
}

bool akimbo::selectable() { return weapon::selectable() && !m_nopistol && owner->akimbo; }
void akimbo::updatetimers() { weapon::updatetimers(); /*loopi(2) akimbolastaction[i] = lastmillis;*/ }
void akimbo::reset() { akimbolastaction[0] = akimbolastaction[1] = akimbomillis = 0; akimboside = false; }

void akimbo::renderhudmodel()
{
    weapon::renderhudmodel(akimbolastaction[0], 0);
    weapon::renderhudmodel(akimbolastaction[1], 1);
}

bool akimbo::timerout() { return akimbomillis && akimbomillis <= lastmillis; }


// knife

knife::knife(playerent *owner) : weapon(owner, GUN_KNIFE) {}

int knife::flashtime() const { return 0; }

bool knife::attack(vec &targ)
{
    int attackmillis = lastmillis-owner->lastaction;
	if(attackmillis<gunwait) return false;
    gunwait = reloading = 0;

    if(!owner->attacking) return false;
    updatelastaction(owner);

    owner->lastattackweapon = this;
	owner->attacking = false;

    vec from = owner->o;
    vec to = targ;
    from.z -= weaponbeloweye;

    vec unitv;
    float dist = to.dist(from, unitv);
    unitv.div(dist);
    unitv.mul(3); // punch range
    to = from;
    to.add(unitv);

    hits.setsizenodelete(0);
    raydamage(from, to, owner);
    attackfx(from, to, 0);
    sendshoot(from, to);
    gunwait = info.attackdelay;
    return true;
}

int knife::modelanim() { return modelattacking() ? ANIM_GUN_SHOOT : ANIM_GUN_IDLE; }

void knife::drawstats() {}
void knife::attackfx(const vec &from, const vec &to, int millis) { attacksound(); }
void knife::renderstats() { }


void setscope(bool enable)
{
    if(player1->weaponsel->type != GUN_SNIPER) return;
    sniperrifle *sr = (sniperrifle *)player1->weaponsel;
    sr->setscope(enable);
}

COMMAND(setscope, ARG_1INT);


void shoot(playerent *p, vec &targ)
{
    if(p->state==CS_DEAD || p->weaponchanging) return;
    weapon *weap = p->weaponsel;
    if(weap)
    {
        weap->attack(targ);
        loopi(NUMGUNS)
        {
            weapon *bweap = player1->weapons[i];
            if(bweap != weap && bweap->busy()) bweap->attack(targ);
        }
    }
}

void checkakimbo()
{
    if(player1->akimbo)
    {
        akimbo &a = *((akimbo *)player1->weapons[GUN_AKIMBO]);
        if(a.timerout())
        {
            weapon &p = *player1->weapons[GUN_PISTOL];
            player1->akimbo = false;
            a.reset();
            // transfer ammo to pistol
            p.mag = min((int)p.info.magsize, max(a.mag, p.mag));
            p.ammo = max(p.ammo, p.ammo);
			// fix akimbo magcontent
			a.mag = 0;
			a.ammo = 0;
            if(player1->weaponsel->type==GUN_AKIMBO) player1->weaponswitch(&p);
	        audiomgr.playsoundc(S_AKIMBOOUT);
        }
    }
}
