// entities.cpp: map entity related functions (pickup etc.)

#include "pch.h"
#include "cube.h"

vector<entity> ents;

const char *entnames[] =
{
    "none?", "light", "playerstart",
    "pistol", "ammobox","grenades",
    "health", "armour", "akimbo",
    "mapmodel", "trigger",
    "ladder", "ctf-flag",
    "sound", "clip", "", ""
};

const char *entmdlnames[] =
{
	"pickups/pistolclips", "pickups/ammobox", "pickups/nades", "pickups/health", "pickups/kevlar", "pickups/akimbo",
};

void renderent(entity &e)
{
    const char *mdlname = entmdlnames[e.type-I_CLIPS];
    float z = (float)(1+sinf(lastmillis/100.0f+e.x+e.y)/20),
          yaw = lastmillis/10.0f;
	rendermodel(mdlname, ANIM_MAPMODEL|ANIM_LOOP|ANIM_DYNALLOC, 0, 0, vec(e.x, e.y, z+S(e.x, e.y)->floor+e.attr1), yaw, 0);
}

void renderclip(entity &e)
{
    float xradius = max(float(e.attr2), 0.1f), yradius = max(float(e.attr3), 0.1f);
    vec bbmin(e.x - xradius, e.y - yradius, float(S(e.x, e.y)->floor+e.attr1)),
        bbmax(e.x + xradius, e.y + yradius, bbmin.z + max(float(e.attr4), 0.1f));

    glDisable(GL_TEXTURE_2D);
    linestyle(1, 0xFF, 0xFF, 0);
    glBegin(GL_LINES);

    glVertex3f(bbmin.x, bbmin.y, bbmin.z);
    loopi(2) glVertex3f(bbmax.x, bbmin.y, bbmin.z);
    loopi(2) glVertex3f(bbmax.x, bbmax.y, bbmin.z);
    loopi(2) glVertex3f(bbmin.x, bbmax.y, bbmin.z);
    glVertex3f(bbmin.x, bbmin.y, bbmin.z);

    glVertex3f(bbmin.x, bbmin.y, bbmax.z);
    loopi(2) glVertex3f(bbmax.x, bbmin.y, bbmax.z);
    loopi(2) glVertex3f(bbmax.x, bbmax.y, bbmax.z);
    loopi(2) glVertex3f(bbmin.x, bbmax.y, bbmax.z);
    glVertex3f(bbmin.x, bbmin.y, bbmax.z);

    loopi(8) glVertex3f(i&2 ? bbmax.x : bbmin.x, i&4 ? bbmax.y : bbmin.y, i&1 ? bbmax.z : bbmin.z);
    
    glEnd();
    glEnable(GL_TEXTURE_2D);
}
    
void renderentities()
{
    if(editmode && !reflecting && !refracting && !stenciling)
    {
        static int lastsparkle = 0;
        if(lastmillis - lastsparkle >= 20)
        {
            lastsparkle = lastmillis - (lastmillis%20);
            int closest = closestent();
            loopv(ents)
            {
                entity &e = ents[i];
                if(e.type==NOTUSED) continue;
                vec v(e.x, e.y, e.z);
                if(vec(v).sub(camera1->o).dot(camdir) < 0) continue;
                particle_splash(i == closest ? 12 : 2, 2, 40, v);
            }
        }
    }
    loopv(ents)
    {
        entity &e = ents[i];
        if(e.type==MAPMODEL)
        {
            mapmodelinfo &mmi = getmminfo(e.attr2);
            if(!&mmi) continue;
			rendermodel(mmi.name, ANIM_MAPMODEL|ANIM_LOOP, e.attr4, 0, vec(e.x, e.y, (float)S(e.x, e.y)->floor+mmi.zoff+e.attr3), (float)((e.attr1+7)-(e.attr1+7)%15), 0, 10.0f);
        }
        else if(isitem(e.type))
        {
            if((!OUTBORD(e.x, e.y) && e.spawned) || editmode)
            {
                renderent(e);
            }
        }
        else if(editmode)
        {
            if(e.type==CTF_FLAG)
            {
                s_sprintfd(path)("pickups/flags/%s", team_string(e.attr2));
                rendermodel(path, ANIM_FLAG|ANIM_LOOP, 0, 0, vec(e.x, e.y, (float)S(e.x, e.y)->floor), (float)((e.attr1+7)-(e.attr1+7)%15), 0, 120.0f);
            }
            else if(e.type==CLIP) renderclip(e);
        }
    }
    if(m_flags) loopi(2)
    {
        flaginfo &f = flaginfos[i];
        switch(f.state)
        {
            case CTFF_STOLEN:
                if(f.actor && f.actor != player1)
                {
                    s_sprintfd(path)("pickups/flags/small_%s%s", m_ktf ? "" : team_string(i), m_htf ? "_htf" : m_ktf ? "ktf" : "");
                    rendermodel(path, ANIM_FLAG|ANIM_START|ANIM_DYNALLOC, 0, 0, vec(f.actor->o).add(vec(0, 0, 0.3f+(sinf(lastmillis/100.0f)+1)/10)), lastmillis/2.5f, 0, 120.0f);
                }
                break;
            case CTFF_INBASE:
                if(!numflagspawn[i]) break;
            case CTFF_DROPPED:
            {
                entity &e = *f.flagent;
                s_sprintfd(path)("pickups/flags/%s%s", m_ktf ? "" : team_string(i),  m_htf ? "_htf" : m_ktf ? "ktf" : "");
                rendermodel(path, ANIM_FLAG|ANIM_LOOP, 0, 0, vec(f.pos.x, f.pos.y, f.state==CTFF_INBASE ? (float)S(int(f.pos.x), int(f.pos.y))->floor : f.pos.z), (float)((e.attr1+7)-(e.attr1+7)%15), 0, 120.0f);
                break;
            }
            case CTFF_IDLE:
                break;
        }
    }
}

// these two functions are called when the server acknowledges that you really
// picked up the item (in multiplayer someone may grab it before you).

void pickupeffects(int n, playerent *d)
{
    if(!ents.inrange(n)) return;
    entity &e = ents[n];
    e.spawned = false;
    if(!d) return;
    d->pickup(e.type);
    itemstat &is = d->itemstats(e.type);
    if(d!=player1 && d->type!=ENT_BOT) return;
    if(&is)
    {
        if(d==player1) playsoundc(is.sound);
        else playsound(is.sound, d);
    }

    weapon *w = NULL;
    switch(e.type)
    {
        case I_AKIMBO: w = d->weapons[GUN_AKIMBO]; break;
        case I_CLIPS: w = d->weapons[GUN_PISTOL]; break;
        case I_AMMO: w = d->primweap; break;
        case I_GRENADE: w = d->weapons[GUN_GRENADE]; break;
    }
    if(w) w->onammopicked();
}

// these functions are called when the client touches the item

void trypickup(int n, playerent *d)
{
    entity &e = ents[n];
    switch(e.type)
    {
        default:
            if(d->canpickup(e.type))
            {
                if(d->type==ENT_PLAYER) addmsg(SV_ITEMPICKUP, "ri", n);
                else if(d->type==ENT_BOT && serverpickup(n, -1)) pickupeffects(n, d);
                e.spawned = false;
            }
            break;

        case LADDER:
            if(!d->crouching) d->onladder = true;
            break;
    }
}

void trypickupflag(int flag, playerent *d)
{
    if(d==player1)
    {
        flaginfo &f = flaginfos[flag];
        flaginfo &of = flaginfos[team_opposite(flag)];
        if(f.state == CTFF_STOLEN) return;
        bool own = flag == team_int(d->team);

        if(m_ctf)
        {
            if(own) // it's the own flag
            {
                if(f.state == CTFF_DROPPED) flagreturn(flag);
                else if(f.state == CTFF_INBASE && of.state == CTFF_STOLEN && of.actor == d && of.ack) flagscore(of.team);
            }
            else flagpickup(flag);
        }
        else if(m_htf)
        {
            if(own)
            {
                flagpickup(flag);
            }
            else
            {
                if(f.state == CTFF_DROPPED) flagscore(f.team); // may not count!
            }
        }
        else if(m_ktf)
        {
            if(f.state != CTFF_INBASE) return;
            flagpickup(flag);
        }
    }
}

void checkitems(playerent *d)
{
    if(editmode || d->state!=CS_ALIVE) return;
    d->onladder = false;
    float eyeheight = d->eyeheight;
    loopv(ents)
    {
        entity &e = ents[i];
        if(e.type==NOTUSED) continue;
        if(e.type==LADDER)
        {
            if(OUTBORD(e.x, e.y)) continue;
            vec v(e.x, e.y, d->o.z);
            float dist1 = d->o.dist(v);
            float dist2 = d->o.z - (S(e.x, e.y)->floor+eyeheight);
            if(dist1<1.5f && dist2<e.attr1) trypickup(i, d);
            continue;
        }

        if(!e.spawned) continue;
        if(OUTBORD(e.x, e.y)) continue;

        if(e.type==CTF_FLAG) continue;
        // simple 2d collision
        vec v(e.x, e.y, S(e.x, e.y)->floor+eyeheight);
        if(d->o.dist(v)<2.5f) trypickup(i, d);
    }
    if(m_flags) loopi(2)
    {
        flaginfo &f = flaginfos[i];
        entity &e = *f.flagent;
        if(!e.spawned || !f.ack || (f.state == CTFF_INBASE && !numflagspawn[i])) continue;
        if(OUTBORD(int(f.pos.x), int(f.pos.y))) continue;
        if(f.state==CTFF_DROPPED) // 3d collision for dropped ctf flags
        {
            if(objcollide(d, f.pos, 2.5f, 4.0f)) trypickupflag(i, d);
        }
        else // simple 2d collision
        {
            vec v = f.pos;
            v.z = S(int(v.x), int(v.y))->floor + eyeheight;
            if(d->o.dist(v)<2.5f) trypickupflag(i, d);
        }
    }
}

void putitems(ucharbuf &p)            // puts items in network stream and also spawns them locally
{
    loopv(ents) if(isitem(ents[i].type) || (multiplayer(false) && gamespeed!=100 && (i=-1)))
    {
		if(m_noitemsnade && ents[i].type!=I_GRENADE) continue;
		else if(m_pistol && ents[i].type==I_AMMO) continue;
        putint(p, i);
        putint(p, ents[i].type);
        ents[i].spawned = true;
    }
}

void resetspawns()
{
	loopv(ents) ents[i].spawned = false;
	if(m_noitemsnade || m_pistol)
    {
		loopv(ents)
		{
			entity &e = ents[i];
			if(m_noitemsnade && e.type == I_CLIPS) e.type = I_GRENADE;
			else if(m_pistol && e.type==I_AMMO) e.type = I_CLIPS;
		}
    }
}
void setspawn(int i, bool on) { if(ents.inrange(i)) ents[i].spawned = on; }

bool selectnextprimary(int num)
{
    switch(num)
    {
        case GUN_SHOTGUN:
        case GUN_SUBGUN:
        case GUN_SNIPER:
        case GUN_ASSAULT:
            player1->setnextprimary(num);
            addmsg(SV_PRIMARYWEAP, "ri", player1->nextprimweap->type);
            return true;

        default:
            conoutf("this is not a valid primary weapon");
            return false;
    }
}

VARFP(nextprimary, 0, GUN_ASSAULT, NUMGUNS,
{
    if(!selectnextprimary(nextprimary)) selectnextprimary((nextprimary = GUN_ASSAULT));
});

// flag ent actions done by the local player

int flagdropmillis = 0;

void flagpickup(int fln)
{
    if(flagdropmillis && flagdropmillis>lastmillis) return;
	flaginfo &f = flaginfos[fln];
	f.flagent->spawned = false;
	f.state = CTFF_STOLEN;
	f.actor = player1; // do this although we don't know if we picked the flag to avoid getting it after a possible respawn
	f.actor_cn = getclientnum();
	f.ack = false;
	addmsg(SV_FLAGACTION, "rii", FA_PICKUP, f.team);
}

void tryflagdrop(bool manual)
{
    loopi(2)
    {
        flaginfo &f = flaginfos[i];
        if(f.state==CTFF_STOLEN && f.actor==player1)
        {
            f.flagent->spawned = false;
            f.state = CTFF_DROPPED;
            f.ack = false;
            flagdropmillis = lastmillis+3000;
            addmsg(SV_FLAGACTION, "rii", manual ? FA_DROP : FA_LOST, f.team);
        }
    }
}

void flagreturn(int fln)
{
	flaginfo &f = flaginfos[fln];
	f.flagent->spawned = false;
	f.ack = false;
	addmsg(SV_FLAGACTION, "rii", FA_RETURN, f.team);
}

void flagscore(int fln)
{
	flaginfo &f = flaginfos[fln];
	f.ack = false;
	addmsg(SV_FLAGACTION, "rii", FA_SCORE, f.team);
}

// flag ent actions from the net

void flagstolen(int flag, int act)
{
	playerent *actor = act == getclientnum() ? player1 : getclient(act);
	flaginfo &f = flaginfos[flag];
	f.actor = actor; // could be NULL if we just connected
	f.actor_cn = act;
	f.flagent->spawned = false;
	f.ack = true;
}

void flagdropped(int flag, short x, short y, short z)
{
	flaginfo &f = flaginfos[flag];
    if(OUTBORD(x, y)) return; // valid pos
    bounceent p;
    p.rotspeed = 0.0f;
    p.o.x = x;
    p.o.y = y;
    p.o.z = z;
    p.vel.z = -0.8f;
    p.aboveeye = p.eyeheight = p.maxeyeheight = 0.4f;
    p.radius = 0.1f;

    bool oldcancollide = false;
    if(f.actor)
    {
        oldcancollide = f.actor->cancollide;
        f.actor->cancollide = false; // avoid collision with owner
    }
    loopi(100) // perform physics steps
    {
        moveplayer(&p, 10, true, 50);
        if(p.stuck) break;
    }
    if(f.actor) f.actor->cancollide = oldcancollide; // restore settings

    f.pos.x = round(p.o.x);
    f.pos.y = round(p.o.y);
    f.pos.z = round(p.o.z);
    if(f.pos.z < hdr.waterlevel) f.pos.z = (short) hdr.waterlevel;
	f.flagent->spawned = true;
	f.ack = true;
}

void flaginbase(int flag)
{
	flaginfo &f = flaginfos[flag];
	f.actor = NULL; f.actor_cn = -1;
    f.pos = vec(f.flagent->x, f.flagent->y, f.flagent->z);
	f.flagent->spawned = true;
	f.ack = true;
}

void flagidle(int flag)
{
    flaginbase(flag);
	flaginfos[flag].flagent->spawned = false;
}

