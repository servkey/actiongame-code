// physics.cpp: no physics books were hurt nor consulted in the construction of this code.
// All physics computations and constants were invented on the fly and simply tweaked until
// they "felt right", and have no basis in reality. Collision detection is simplistic but
// very robust (uses discrete steps at fixed fps).

#include "cube.h"

/* Modified by Rick: Fix, so we can jump on players aswell
bool plcollide(dynent *d, dynent *o, float &headspace)          // collide with player or monster
{
    if(o->state!=CS_ALIVE) return true;
    const float r = o->radius+d->radius;
    if(fabs(o->o.x-d->o.x)<r && fabs(o->o.y-d->o.y)<r) 
    {
        if(fabs(o->o.z-d->o.z)<o->aboveeye+d->eyeheight) return false;
        if(d->monsterstate) return false; // hack
        headspace = d->o.z-o->o.z-o->aboveeye-d->eyeheight;
        if(headspace<0) headspace = 10;
    };
    return true;
};
*/

bool plcollide(dynent *d, dynent *o, float &headspace, float &hi, float &lo)          // collide with player or monster
{
    if(o->state!=CS_ALIVE) return true;
    const float r = o->radius+d->radius;
    if(fabs(o->o.x-d->o.x)<r && fabs(o->o.y-d->o.y)<r) 
    {
        if(d->o.z-d->eyeheight<o->o.z-o->eyeheight) { if(o->o.z-o->eyeheight<hi) hi = o->o.z-o->eyeheight-1; }
        else if(o->o.z+o->aboveeye>lo) lo = o->o.z+o->aboveeye+1;
    
        if(fabs(o->o.z-d->o.z)<o->aboveeye+d->eyeheight) return false;
        //if(d->monsterstate) return false; // hack
        headspace = d->o.z-o->o.z-o->aboveeye-d->eyeheight;
        if(headspace<0) headspace = 10;
    };
    return true;
};

bool cornertest(int mip, int x, int y, int dx, int dy, int &bx, int &by, int &bs)    // recursively collide with a mipmapped corner cube
{
    sqr *w = wmip[mip];
    int sz = ssize>>mip;
    bool stest = SOLID(SWS(w, x+dx, y, sz)) && SOLID(SWS(w, x, y+dy, sz));
    mip++;
    x /= 2;
    y /= 2;
    if(SWS(wmip[mip], x, y, ssize>>mip)->type==CORNER)
    {
        bx = x<<mip;
        by = y<<mip;
        bs = 1<<mip;
        return cornertest(mip, x, y, dx, dy, bx, by, bs);
    };
    return stest;
};

void mmcollide(dynent *d, float &hi, float &lo)           // collide with a mapmodel
{
    loopv(ents)
    {
        entity &e = ents[i];
        if(e.type!=MAPMODEL) continue;
        mapmodelinfo &mmi = getmminfo(e.attr2);
        if(!&mmi || !mmi.h) continue;
        const float r = mmi.rad+d->radius;
        if(fabs(e.x-d->o.x)<r && fabs(e.y-d->o.y)<r)
        { 
            float mmz = (float)(S(e.x, e.y)->floor+mmi.zoff+e.attr3);
            if(d->o.z-d->eyeheight<mmz) { if(mmz<hi) hi = mmz; }
            else if(mmz+mmi.h>lo) lo = mmz+mmi.h;
        };
    };
};

// all collision happens here
// spawn is a dirty side effect used in spawning
// drop & rise are supplied by the physics below to indicate gravity/push for current mini-timestep

bool collide(dynent *d, bool spawn, float drop, float rise)
{
    const float fx1 = d->o.x-d->radius;     // figure out integer cube rectangle this entity covers in map
    const float fy1 = d->o.y-d->radius;
    const float fx2 = d->o.x+d->radius;
    const float fy2 = d->o.y+d->radius;
    const int x1 = int(fx1);
    const int y1 = int(fy1);
    const int x2 = int(fx2);
    const int y2 = int(fy2);
    float hi = 127, lo = -128;
    float minfloor = (d->monsterstate && !spawn && d->health>100) ? d->o.z-d->eyeheight-4.5f : -1000.0f;  // big monsters are afraid of heights, unless angry :)

    for(int x = x1; x<=x2; x++) for(int y = y1; y<=y2; y++)     // collide with map
    {
        if(OUTBORD(x,y)) return false;
        sqr *s = S(x,y);
        float ceil = s->ceil;
        float floor = s->floor;
        switch(s->type)
        {
            case SOLID:
                return false;

            case CORNER:
            {
                int bx = x, by = y, bs = 1;
                if(x==x1 && y==y1 && cornertest(0, x, y, -1, -1, bx, by, bs) && fx1-bx+fy1-by<=bs
                || x==x2 && y==y1 && cornertest(0, x, y,  1, -1, bx, by, bs) && fx2-bx>=fy1-by
                || x==x1 && y==y2 && cornertest(0, x, y, -1,  1, bx, by, bs) && fx1-bx<=fy2-by
                || x==x2 && y==y2 && cornertest(0, x, y,  1,  1, bx, by, bs) && fx2-bx+fy2-by>=bs)
                   return false;
                break;
            };

            case FHF:       // FIXME: too simplistic collision with slopes, makes it feels like tiny stairs
                floor -= (s->vdelta+S(x+1,y)->vdelta+S(x,y+1)->vdelta+S(x+1,y+1)->vdelta)/16.0f;
                break;

            case CHF:
                ceil += (s->vdelta+S(x+1,y)->vdelta+S(x,y+1)->vdelta+S(x+1,y+1)->vdelta)/16.0f;

        };
        if(ceil<hi) hi = ceil;
        if(floor>lo) lo = floor;
        if(floor<minfloor) return false;   
    };

    if(hi-lo < d->eyeheight+d->aboveeye) return false;

    // Modified by Rick: plcollide now takes hi and lo in account aswell, that way we can jump/walk on players
    
    float headspace = 10;
    loopv(players)       // collide with other players
    {
        dynent *o = players[i]; 
        if(!o || o==d) continue;
        if(!plcollide(d, o, headspace, hi, lo)) return false;
    };
    loopv(bots)       // Added by Rick: collide with other bots
    {
        dynent *o = bots[i]; 
        if(!o || o==d) continue;
        if(!plcollide(d, o, headspace, hi, lo)) return false;
    };
    
    if(d!=player1 && d->mtype!=M_NADE) if(!plcollide(d, player1, headspace, hi, lo)) return false;
    //dvector &v = getmonsters();
    // this loop can be a performance bottleneck with many monster on a slow cpu,
    // should replace with a blockmap but seems mostly fast enough
    // Modified by Rick: Added v[i] pointer check
    //loopv(v) if(v[i] && !vreject(d->o, v[i]->o, 7.0f) && d!=v[i] && !plcollide(d, v[i], headspace, hi, lo)) return false; 
    headspace -= 0.01f;
    
    mmcollide(d, hi, lo);    // collide with map models

    if(spawn)
    {
        d->o.z = lo+d->eyeheight;       // just drop to floor (sideeffect)
        d->onfloor = true;
    }
    else
    {
        const float space = d->o.z-d->eyeheight-lo;
        if(space<0)
        {
            if(space>-0.01) 
            {
                d->o.z = lo+d->eyeheight;   // stick on step
            }
            else if(space>-1.26f && !d->isphysent) d->o.z += rise;       // rise thru stair
            else return false;
        }
        else
        {
            d->o.z -= min(min(drop, space), headspace);       // gravity
        };

        const float space2 = hi-(d->o.z+d->aboveeye);
        if(space2<0)
        {
            if(space2<-0.1) return false;     // hack alert!
            d->o.z = hi-d->aboveeye;          // glue to ceiling
            d->vel.z = 0;                     // cancel out jumping velocity
        };

        d->onfloor = d->o.z-d->eyeheight-lo<0.01f;
    };
    return true;
}

VAR(maxroll, 0, 3, 20);

void selfdamage(dynent *d, int dm)
{
    if(d==player1) selfdamage(dm, -1, d);
    else { addmsg(SV_DAMAGE, "ri3", d, dm, d->lifesequence); };
    //playsound(S_FALL1+rnd(5), &d->o); }; // fixme sound
    particle_splash(3, dm, 1000, d->o);  //edit out?
    demodamage(dm, d->o);
};


// main physics routine, moves a player/monster for a curtime step
// moveres indicated the physics precision (which is lower for monsters and multiplayer prediction)
// local is false for multiplayer prediction

void moveplayer(dynent *pl, int moveres, bool local, int curtime)
{
    const bool water = hdr.waterlevel>pl->o.z-0.5f;
    const bool floating = (editmode && local) || pl->state==CS_EDITING;

    vec d;      // vector of direction we ideally want to move in
    

    int move = pl->onladder && !pl->onfloor && pl->move == -1 ? 0 : pl->move; // fix movement on ladder
    
    d.x = (float)(move*cos(RAD*(pl->yaw-90)));
    d.y = (float)(move*sin(RAD*(pl->yaw-90)));
    d.z = (float) pl->isphysent ? pl->vel.z : 0;
    
    if(floating || water)
    {
        d.x *= (float)cos(RAD*(pl->pitch));
        d.y *= (float)cos(RAD*(pl->pitch));
        d.z = (float)(move*sin(RAD*(pl->pitch)));
    };

    d.x += (float)(pl->strafe*cos(RAD*(pl->yaw-180)));
    d.y += (float)(pl->strafe*sin(RAD*(pl->yaw-180)));

    const float speed = curtime/(water ? 2000.0f : 1000.0f)*pl->maxspeed;
    const float friction = water ? 20.0f : (pl->onfloor || floating ? 6.0f : (pl->onladder ? 1.5f : 30.0f));

    const float fpsfric = friction/curtime*20.0f;   
    
    pl->vel.mul(fpsfric-1);   // slowly apply friction and direction to velocity, gives a smooth movement
    pl->vel.add(d);
    pl->vel.div(fpsfric);
    d = pl->vel;
    d.mul(speed);             // d is now frametime based velocity vector
    
    if(pl->isphysent)
    {
        float dist = d.magnitude();
        pl->pitch += dist*pl->rotspeed*5.0f;
        if(pl->pitch>360.0f) pl->pitch = 0.0f;
        pl->yaw += dist*pl->rotspeed*5.0f;
        if(pl->yaw>360.0f) pl->yaw = 0.0f;
    }

    pl->blocked = false;
    pl->moving = true;

    if(floating)                // just apply velocity
    {
        pl->o.add(d);
        if(pl->jumpnext) { pl->jumpnext = false; pl->vel.z = 2; }
    }
    else                        // apply velocity with collision
    {   
        if(pl->onladder)
        {
            if(pl->k_up) pl->vel.z = 0.75;
            else if(pl->k_down) pl->vel.z = -0.75;
            pl->timeinair = 0;
        }
        else
        {
            if(pl->onfloor || water)
            {   
                if(pl->jumpnext)
                {
                    pl->jumpnext = false;
                    pl->vel.z = 2.0f; //1.7f;       // physics impulse upwards
                    if(water) { pl->vel.x /= 8; pl->vel.y /= 8; };      // dampen velocity change even harder, gives correct water feel
                    if(local) playsoundc(S_JUMP);
                    else if(pl->monsterstate) playsound(S_JUMP, &pl->o);
                    else if(pl->bIsBot) botplaysound(S_JUMP, pl); // Added by Rick
                }
                pl->timeinair = 0;
                if(pl->isphysent) pl->vel.z *= 0.7f;
            }
            else
            {
                pl->timeinair += curtime;
            };
        };

        const float gravity = pl->isphysent ? pl->gravity : 20;
        const float f = 1.0f/moveres;
        float dropf = pl->isphysent ? ((gravity-1)+pl->timeinair/14.0f) : ((gravity-1)+pl->timeinair/15.0f);        // incorrect, but works fine
        if(water) { dropf = 5; pl->timeinair = 0; };            // float slowly down in water
        if(pl->onladder) { dropf = 0; pl->timeinair = 0; };
        float drop = dropf*curtime/gravity/100/moveres;   // at high fps, gravity kicks in too fast
        const float rise = speed/moveres/1.2f;                  // extra smoothness when lifting up stairs

        loopi(moveres)                                          // discrete steps collision detection & sliding
        {
            // try move forward
            pl->o.x += f*d.x;
            pl->o.y += f*d.y;
            pl->o.z += f*d.z;
            if(collide(pl, false, drop, rise)) continue;                     
            // player stuck, try slide along y axis
            pl->blocked = true;
            pl->o.x -= f*d.x;
            if(collide(pl, false, drop, rise)) 
            { 
                d.x = 0; 
                if(pl->isphysent) pl->vel.x = -pl->vel.x;
                continue; 
            };   
            pl->o.x += f*d.x;
            // still stuck, try x axis
            pl->o.y -= f*d.y;
            if(collide(pl, false, drop, rise)) 
            { 
                d.y = 0; 
                if(pl->isphysent) pl->vel.y = -pl->vel.y;
                continue; 
            };       
            pl->o.y += f*d.y;
            // try just dropping down
            pl->moving = false;
            pl->o.x -= f*d.x;
            pl->o.y -= f*d.y;
            if(collide(pl, false, drop, rise)) 
            { 
                d.y = d.x = 0;
                continue; 
            }; 
            pl->o.z -= f*d.z;
            break;
        };
    };

    // detect wether player is outside map, used for skipping zbuffer clear mostly

    if(pl->o.x < 0 || pl->o.x >= ssize || pl->o.y <0 || pl->o.y > ssize)
    {
        pl->outsidemap = true;
    }
    else
    {
        sqr *s = S((int)pl->o.x, (int)pl->o.y);
        pl->outsidemap = SOLID(s)
           || pl->o.z < s->floor - (s->type==FHF ? s->vdelta/4 : 0)
           || pl->o.z > s->ceil  + (s->type==CHF ? s->vdelta/4 : 0);
    };
    
    // automatically apply smooth roll when strafing

    if(pl->strafe==0) 
    {
        pl->roll = pl->roll/(1+(float)sqrt((float)curtime)/25);
    }
    else
    {
        pl->roll += pl->strafe*curtime/-30.0f;
        if(pl->roll>maxroll) pl->roll = (float)maxroll;
        if(pl->roll<-maxroll) pl->roll = (float)-maxroll;
    };
    
    // play sounds on water transitions
    
    if(!pl->inwater && water) { playsound(S_SPLASH2, &pl->o); pl->vel.z = 0; }
    else if(pl->inwater && !water) playsound(S_SPLASH1, &pl->o);
    pl->inwater = water;
    // Added by Rick: Easy hack to store previous locations of all players/monsters/bots
    pl->PrevLocations.Update(pl->o);
    // End add
};

VAR(minframetime, 5, 10, 20);

int physicsfraction = 0, physicsrepeat = 0;

void physicsframe()          // optimally schedule physics frames inside the graphics frames
{
    if(curtime>=minframetime)
    {
        int faketime = curtime+physicsfraction;
        physicsrepeat = faketime/minframetime;
        physicsfraction = faketime%minframetime;
    }
    else
    {
        physicsrepeat = 1;
    };
};

void moveplayer(dynent *pl, int moveres, bool local)
{
    loopi(physicsrepeat) moveplayer(pl, moveres, local, min(curtime, minframetime));
};

vector <physent *>physents;

physent *new_physent()
{
    physent *p = new physent;
    
    p->yaw = 270;
    p->pitch = 0;
    p->roll = 0;
    p->isphysent = true;
	p->rotspeed = 1.0f;
    
    p->maxspeed = 40;
    p->outsidemap = false;
    p->inwater = false;
    p->radius = 0.2f;
    p->eyeheight = 0.3f;
    p->aboveeye = 0.0f;
    p->frags = 0;
    p->plag = 0;
    p->ping = 0;
    p->lastupdate = lastmillis;
    p->enemy = NULL;
    p->monsterstate = 0;
    p->name[0] = p->team[0] = 0;
    p->blocked = false;
    p->lifesequence = 0;
    p->dynent::state = CS_ALIVE;
    p->state = PHYSENT_NONE;
    p->shots = 0;
    p->reloading = false;
    p->nextprimary = 1;
    p->hasarmour = false;

    p->k_left = false;
    p->k_right = false;
    p->k_up = false;
    p->k_down = false;  
    p->jumpnext = false;
    p->onladder = false;
    p->strafe = 0;
    p->move = 0;
    
    physents.add(p);
    return p;
}

extern void explode_nade(physent *i);

void mphysents()
{
    loopv(physents) if(physents[i])
    {
        physent *p = physents[i];
        if(p->state == NADE_THROWED || p->state == GIB) moveplayer(p, 2, false);
        
        if(lastmillis - p->millis >= p->timetolife)
        {
            if(p->state==NADE_ACTIVATED || p->state==NADE_THROWED) explode_nade(physents[i]);
			delete p;
            physents.remove(i);
            i--;
        };
    };
};

void clearphysents()
{
	loopv(physents) if(physents[i]) { delete physents[i]; physents.remove(i); };
}

