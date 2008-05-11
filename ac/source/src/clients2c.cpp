// client processing of the incoming network stream

#include "pch.h"
#include "cube.h"
#include "bot/bot.h"

extern bool c2sinit, senditemstoserver;
extern string clientpassword;

void neterr(const char *s)
{
    conoutf("\f3illegal network message (%s)", s);
    disconnect();
}

VARP(autogetmap, 0, 1, 1);

void changemapserv(char *name, int mode, bool download)        // forced map change from the server
{
    gamemode = mode;
    if(m_demo) return;
    if(!load_world(name) && download)
    {
        if(securemapcheck(name, false)) return;
        if(autogetmap) getmap();
        else conoutf("\"getmap\" to download the current map from the server");
    }
}

// update the position of other clients in the game in our world
// don't care if he's in the scenery or other players,
// just don't overlap with our client

void updatepos(playerent *d)
{
    const float r = player1->radius+d->radius;
    const float dx = player1->o.x-d->o.x;
    const float dy = player1->o.y-d->o.y;
    const float dz = player1->o.z-d->o.z;
    const float rz = player1->aboveeye+d->dyneyeheight();
    const float fx = (float)fabs(dx), fy = (float)fabs(dy), fz = (float)fabs(dz);
    if(fx<r && fy<r && fz<rz && d->state!=CS_DEAD)
    {
        if(fx<fy) d->o.y += dy<0 ? r-fy : -(r-fy);  // push aside
        else      d->o.x += dx<0 ? r-fx : -(r-fx);
    }
}

void updatelagtime(playerent *d)
{
    int lagtime = lastmillis-d->lastupdate;
    if(lagtime)
    {
        if(d->state!=CS_SPAWNING && d->lastupdate) d->plag = (d->plag*5+lagtime)/6;
        d->lastupdate = lastmillis;
    }
}

extern void trydisconnect();

void parsepositions(ucharbuf &p)
{
    int type;
    while(p.remaining()) switch(type = getint(p))
    {
        case SV_POS:                        // position of another client
        {
            int cn = getint(p);
            vec o, vel;
            float yaw, pitch, roll;
            o.x   = getuint(p)/DMF;
            o.y   = getuint(p)/DMF;
            o.z   = getuint(p)/DMF;
            yaw   = (float)getuint(p);
            pitch = (float)getint(p);
            roll  = (float)getint(p);
            vel.x = getint(p)/DVELF;
            vel.y = getint(p)/DVELF;
            vel.z = getint(p)/DVELF;
            int f = getuint(p), seqcolor = (f>>6)&1;
            playerent *d = getclient(cn);
            if(!d || seqcolor!=(d->lifesequence&1)) continue;
            vec oldpos(d->o);
            float oldyaw = d->yaw, oldpitch = d->pitch;
            d->o = o;
            d->yaw = yaw;
            d->pitch = pitch;
            d->roll = roll;
            d->vel = vel;
            d->strafe = (f&3)==3 ? -1 : f&3;
            f >>= 2;  
            d->move = (f&3)==3 ? -1 : f&3;
            f >>= 2;
            d->onfloor = f&1;
            f >>= 1;
            d->onladder = f&1;
            f >>= 2;
            updatecrouch(d, f&1);
            updatepos(d);
            updatelagtime(d);
            extern int smoothmove, smoothdist;
            if(smoothmove && d->smoothmillis>=0 && oldpos.dist(d->o) < smoothdist)
            {
                d->newpos = d->o;
                d->newyaw = d->yaw;
                d->newpitch = d->pitch;
                d->o = oldpos;
                d->yaw = oldyaw;
                d->pitch = oldpitch;
                (d->deltapos = oldpos).sub(d->newpos);
                d->deltayaw = oldyaw - d->newyaw;
                if(d->deltayaw > 180) d->deltayaw -= 360;
                else if(d->deltayaw < -180) d->deltayaw += 360;
                d->deltapitch = oldpitch - d->newpitch;
                d->smoothmillis = lastmillis;
            }
            else d->smoothmillis = 0;
            if(d->state==CS_LAGGED || d->state==CS_SPAWNING) d->state = CS_ALIVE;
            break;
        }

        default:
            neterr("type");
            return;
    }
}
    
void parsemessages(int cn, playerent *d, ucharbuf &p)
{
    static char text[MAXTRANS];
    int type, joining = 0;
    bool mapchanged = false, demoplayback = false;

    while(p.remaining()) switch(type = getint(p))
    {
        case SV_INITS2C:                    // welcome messsage from the server
        {
            int mycn = getint(p), prot = getint(p);
            if(prot!=PROTOCOL_VERSION)
            {
                conoutf("you are using a different game protocol (you: %d, server: %d)", PROTOCOL_VERSION, prot);
                disconnect();
                return;
            }
            player1->clientnum = mycn;      // we are now fully connected
			joining = getint(p);
            if(getint(p) > 0) conoutf("INFO: this server is password protected");
			if(joining<0 && getclientmap()[0]) // we are the first client on this server, set map
            {
                nextmode = gamemode;
                changemap(getclientmap());
            }
            break;
        }

        case SV_CLIENT:
        {
            int cn = getint(p), len = getuint(p);
            ucharbuf q = p.subbuf(len);
            parsemessages(cn, getclient(cn), q);
            break;
        }

        case SV_SOUND:
            playsound(getint(p), !d ? NULL : d);
            break;

        case SV_VOICECOMTEAM:
        {
            playerent *d = getclient(getint(p));
            if(d) d->lastvoicecom = lastmillis;
            playsound(getint(p), SP_HIGH);
            break;
        }
        case SV_VOICECOM:
        {
            playsound(getint(p), SP_HIGH);
            if(d) d->lastvoicecom = lastmillis;
            break;
        }
        
        case SV_TEAMTEXT:
        {
            int cn = getint(p);
            getstring(text, p);
            filtertext(text, text);
            playerent *d = getclient(cn);
            if(!d) break;
            conoutf("%s:\f1 %s", colorname(d), text);
            break;
        }

        case SV_TEXT:
            if(cn == -1)
            {
                getstring(text, p);
                conoutf("MOTD:");
                conoutf("\f4%s", text);
            }
            else if(d)
            {
                getstring(text, p);
                filtertext(text, text);
                conoutf("%s:\f0 %s", colorname(d), text);
            }
            else return;
            break;

        case SV_MAPCHANGE:     
        {
            getstring(text, p);
            int mode = getint(p);
            bool downloadable = getint(p) > 0;
            changemapserv(text, mode, downloadable);
            if(m_arena && joining>2) deathstate(player1);
            mapchanged = true;
            break;
        }
        
        case SV_ITEMLIST:
        {
            int n;
            if(mapchanged) { senditemstoserver = false; resetspawns(); }
            while((n = getint(p))!=-1) if(mapchanged) setspawn(n, true);
            break;
        }

        case SV_MAPRELOAD:          // server requests next map
        {
            getint(p);
            s_sprintfd(nextmapalias)("nextmap_%s", getclientmap());
            const char *map = getalias(nextmapalias);     // look up map in the cycle
            changemap(map ? map : getclientmap());
            break;
        }

        case SV_INITC2S:            // another client either connected or changed name/team
        {
            d = newclient(cn);
            getstring(text, p);
            filtertext(text, text, false, MAXNAMELEN);
            if(!text[0]) s_strcpy(text, "unarmed");
            if(d->name[0])          // already connected
            {
                if(strcmp(d->name, text))
                    conoutf("%s is now known as %s", colorname(d, 0), colorname(d, 1, text));
            }
            else                    // new client
            {
                c2sinit = false;    // send new players my info again 
                conoutf("connected: %s", colorname(d, 0, text));
            } 
            s_strncpy(d->name, text, MAXNAMELEN+1);
            getstring(text, p);
            filtertext(d->team, text, false, MAXTEAMLEN);
			setskin(d, getint(p));
			if(m_ctf) loopi(2) 
			{
				flaginfo &f = flaginfos[i];
				if(!f.actor) f.actor = getclient(f.actor_cn);
			}
            break;
        }

        case SV_CDIS:
        {
            int cn = getint(p);
            playerent *d = getclient(cn);
            if(!d) break;
			if(d->name[0]) conoutf("player %s disconnected", colorname(d)); 
            zapplayer(players[cn]);
            break;
        }

        case SV_EDITMODE:
        {
            int val = getint(p);
            if(!d) break;
            if(val) d->state = CS_EDITING;
            else d->state = CS_ALIVE;
            break;
        }

        case SV_SPAWN:
        {
            int ls = getint(p), gunselect = getint(p);
            if(!d) break;
            d->lifesequence = ls;
            d->state = CS_SPAWNING;
            d->selectweapon(gunselect);
            break;
        }

        case SV_SPAWNSTATE:
        {
            if(editmode) toggleedit();
            showscores(false);
            setscope(false);
            player1->respawn();
            player1->lifesequence = getint(p);
            player1->health = getint(p);
            player1->armour = getint(p);
            player1->setprimary(getint(p));
            player1->selectweapon(getint(p));
            loopi(NUMGUNS) player1->ammo[i] = getint(p);
            loopi(NUMGUNS) player1->mag[i] = getint(p);
            player1->state = CS_ALIVE;
            findplayerstart(player1);
            if(player1->skin!=player1->nextskin) setskin(player1, player1->nextskin);
            if(m_arena) 
            {
                conoutf("new round starting... fight!");
                if(m_botmode) BotManager.RespawnBots();
            }
            addmsg(SV_SPAWN, "rii", player1->lifesequence, player1->weaponsel->type);
            break;
        }

        case SV_SHOTFX:
        {
            int scn = getint(p), gun = getint(p);
            vec from, to;
            loopk(3) from[k] = getint(p)/DMF;
            loopk(3) to[k] = getint(p)/DMF;
            playerent *s = getclient(scn);
            if(!s) break;
            if(gun==GUN_SHOTGUN) createrays(from, to);
            s->lastaction = lastmillis;
            //s->lastattackweapon = s->weaponsel;
            if(s->weapons[gun])
            {
                s->lastattackweapon = s->weapons[gun];
                s->weapons[gun]->attackfx(from, to, -1);
            }
            break;
        }

        case SV_THROWNADE:
        {
            vec from, to;
            loopk(3) from[k] = getint(p)/DMF;
            loopk(3) to[k] = getint(p)/DMF;
            int nademillis = getint(p);
            if(!d) break;
            d->lastaction = lastmillis;
            d->lastattackweapon = d->weapons[GUN_GRENADE];
            if(d->weapons[GUN_GRENADE]) d->weapons[GUN_GRENADE]->attackfx(from, to, nademillis);
            break;
        }

        case SV_GIBDAMAGE:
        case SV_DAMAGE:
        {
            int tcn = getint(p),
                acn = getint(p),
                damage = getint(p),
                armour = getint(p),
                health = getint(p);
            playerent *target = tcn==getclientnum() ? player1 : getclient(tcn),
                      *actor = acn==getclientnum() ? player1 : getclient(acn);
            if(!target || !actor) break;
            target->armour = armour;
            target->health = health;
            dodamage(damage, target, actor, type==SV_GIBDAMAGE, false);
            break;
        }

        case SV_HITPUSH:
        {
            int gun = getint(p), damage = getint(p);
            vec dir;
            loopk(3) dir[k] = getint(p)/DNF;
            player1->hitpush(damage, dir, NULL, gun);
            break;
        }

        case SV_GIBDIED:
        case SV_DIED:
        {
            int vcn = getint(p), acn = getint(p), frags = getint(p);
            playerent *victim = vcn==getclientnum() ? player1 : getclient(vcn),
                      *actor = acn==getclientnum() ? player1 : getclient(acn);
            if(!actor) break;
            actor->frags = frags;
            if(!victim) break;
            dokill(victim, actor, type==SV_GIBDIED);
            break;
        }

        case SV_RESUME:
        {
            loopi(MAXCLIENTS)
            {
                int cn = getint(p);
                if(cn<0) break;
                int state = getint(p), lifesequence = getint(p), gunselect = getint(p), flagscore = getint(p), frags = getint(p), deaths = getint(p);
                playerent *d = (cn == getclientnum() ? player1 : newclient(cn));
                if(!d) continue;
                if(d!=player1) d->state = state;
                d->lifesequence = lifesequence;
                d->flagscore = flagscore;
                d->frags = frags;
                d->deaths = deaths;
                d->selectweapon(gunselect);
            }
            break;
        }

        case SV_ITEMSPAWN:
        {
            int i = getint(p);
            setspawn(i, true);
            break;
        }

        case SV_ITEMACC:
        {
            int i = getint(p), cn = getint(p);
            playerent *d = cn==getclientnum() ? player1 : getclient(cn);
            pickupeffects(i, d);
            break;
        }

        case SV_EDITH:              // coop editing messages, should be extended to include all possible editing ops
        case SV_EDITT:
        case SV_EDITS:
        case SV_EDITD:
        case SV_EDITE:
        {
            int x  = getint(p);
            int y  = getint(p);
            int xs = getint(p);
            int ys = getint(p);
            int v  = getint(p);
            block b = { x, y, xs, ys };
            switch(type)
            {
                case SV_EDITH: editheightxy(v!=0, getint(p), b); break;
                case SV_EDITT: edittexxy(v, getint(p), b); break;
                case SV_EDITS: edittypexy(v, b); break;
                case SV_EDITD: setvdeltaxy(v, b); break;
                case SV_EDITE: editequalisexy(v!=0, b); break;
            }
            break;
        }

        case SV_EDITENT:            // coop edit of ent
        {
            uint i = getint(p);
            while((uint)ents.length()<=i) ents.add().type = NOTUSED;
            int to = ents[i].type;
            ents[i].type = getint(p);
            ents[i].x = getint(p);
            ents[i].y = getint(p);
            ents[i].z = getint(p);
            ents[i].attr1 = getint(p);
            ents[i].attr2 = getint(p);
            ents[i].attr3 = getint(p);
            ents[i].attr4 = getint(p);
            ents[i].spawned = false;
            if(ents[i].type==LIGHT || to==LIGHT) calclight();
            break;
        }

        case SV_PONG: 
        {
            int millis = getint(p);
            addmsg(SV_CLIENTPING, "i", player1->ping = (player1->ping*5+lastmillis-millis)/6);
            break;
        }

        case SV_CLIENTPING:
            if(!d) return;
            d->ping = getint(p);
            break;

        case SV_GAMEMODE:
            nextmode = getint(p);
            break;

        case SV_TIMEUP:
            timeupdate(getint(p));
            break;
        
        case SV_WEAPCHANGE:
        {
            if(!d) return;
            d->selectweapon(getint(p));
            break;
        }
        
        case SV_SERVMSG:
            getstring(text, p);
            conoutf("%s", text);
            break;

        case SV_FLAGINFO:
        {
            int flag = getint(p);
            if(flag<0 || flag>1) return;
            flaginfo &f = flaginfos[flag];
            f.state = getint(p);
            int action = getint(p);

			switch(f.state)
			{
				case CTFF_STOLEN:
				{ 
					flagstolen(flag, action, getint(p));
					break;
				}
				case CTFF_DROPPED:
				{
					short x = (ushort) (getint(p)/DMF);
					short y = (ushort) (getint(p)/DMF);
                    short z = ((ushort) (getint(p)/DMF))-((short)player1->eyeheight); // correct z offset, assumes all players do have the same eyeheight
					flagdropped(flag, action, x, y, z);
					break;
				}
				case CTFF_INBASE:
				{
					int actor = -1;
					if(action == SV_FLAGRETURN) actor = getint(p);
					flaginbase(flag, action, actor);
					break;
				}
			}
            break;
        }
        
        case SV_FLAGS:
        {
			if(!d) return;
            d->flagscore = getint(p);
            break;
        }

        case SV_ARENAWIN:
        {
            int acn = getint(p);
            playerent *alive = acn<0 ? NULL : (acn==getclientnum() ? player1 : getclient(acn));
            conoutf("arena round is over! next round in 5 seconds...");
            if(m_botmode && acn==-2) conoutf("the bots have won the round!");
            else if(!alive) conoutf("everyone died!");
            else if(m_teammode) conoutf("team %s has won the round!", alive->team);
            else if(alive==player1) conoutf("you are the survivor!");
            else conoutf("%s is the survivor!", colorname(alive));
            break;
        }

        case SV_FORCEDEATH:
        {
            int cn = getint(p);
            playerent *d = cn==getclientnum() ? player1 : newclient(cn);
            if(!d) break;
            deathstate(d);
            break;
        }

		case SV_SERVOPINFO:
		{
            loopv(players) { if(players[i]) players[i]->clientrole = CR_DEFAULT; }
			player1->clientrole = CR_DEFAULT;

			int cl = getint(p), r = getint(p);
			if(cl >= 0 && r >= 0)
			{	
				playerent *pl = (cl == getclientnum() ? player1 : newclient(cl));
				if(pl)
				{
					pl->clientrole = r;
                    if(pl->name[0]) conoutf("%s claimed %s status", pl == player1 ? "you" : colorname(pl), r == CR_ADMIN ? "admin" : "master");
				}
			}
			break;
		}

		case SV_FORCETEAM:
		{
            int team = getint(p);
            bool respawn = getint(p) == 1;
			changeteam(team, respawn);
			break;
		}

        case SV_AUTOTEAM:
            autoteambalance = 1 == getint(p);
            break;

        case SV_CALLVOTE:
        {
            int type = getint(p);
            if(type < 0 || type >= SA_NUM || !d) return;
            votedisplayinfo *v = NULL;
            string a;
            switch(type)
            {
                case SA_MAP:
                    getstring(text, p);
                    itoa(a, getint(p));
                    v = newvotedisplayinfo(d, type, text, a);
                    break;
                default:
                    itoa(a, getint(p));
                    v = newvotedisplayinfo(d, type, a, NULL);
                    break;
            }
            displayvote(v);
            break;
        }

        case SV_CALLVOTESUC:
            callvotesuc();
            break;
        
        case SV_CALLVOTEERR:
            callvoteerr(getint(p));
            break;

        case SV_VOTE:
            votecount(getint(p));
            break;

        case SV_VOTERESULT:
            voteresult(getint(p));
            break;

        case SV_WHOISINFO:
        {
            int cn = getint(p);
            playerent *pl = cn == getclientnum() ? player1 : getclient(cn);
            int ip = getint(p);
            if((ip>>16&0xFF) > 0 && (ip>>24&0xFF) > 0) conoutf("WHOIS client %d:\n\f5name\t%s\n\f5IP\t%d.%d.%d.%d", cn, pl ? colorname(pl) : "", ip&0xFF, ip>>8&0xFF, ip>>16&0xFF, ip>>24&0xFF); // full IP
            else conoutf("WHOIS client %d:\n\f5name\t%s\n\f5IP\t%d.%d.x.x", cn, pl ? colorname(pl) : "", ip&0xFF, ip>>8&0xFF); // censored IP
            break;
        }

        case SV_SENDDEMOLIST:
        {
            int demos = getint(p);
            if(!demos) conoutf("no demos available");
            else loopi(demos)
            {
                getstring(text, p);
                conoutf("%d. %s", i+1, text);
            }
            break;
        }

        case SV_DEMOPLAYBACK:
        {
            demoplayback = getint(p)!=0;
            if(demoplayback) 
            { 
                player1->state = CS_SPECTATE;
                player1->spectatemode = SM_NONE;
                togglespect();
            }
            else
            {
                // cleanups
                loopv(players) zapplayer(players[i]);
                clearvote();
            }
            break;
        }

        default:
            neterr("type");
            return;
    }
}

void receivefile(uchar *data, int len)
{
    ucharbuf p(data, len);
    int type = getint(p);
    data += p.length();
    len -= p.length();
    switch(type)
    {
        case SV_SENDDEMO:
        {
            s_sprintfd(fname)("demos/%d.dmo", lastmillis);
            path(fname);
            FILE *demo = openfile(fname, "wb");
            if(!demo) return;
            conoutf("received demo \"%s\"", fname);
            fwrite(data, 1, len, demo);
            fclose(demo);
            break;
        }

        case SV_RECVMAP:
        {
            static char text[MAXTRANS];
            getstring(text, p);
            conoutf("received map \"%s\" from server, reloading..", &text);
            int mapsize = getint(p);
            int cfgsize = getint(p);
            int size = mapsize + cfgsize;
            if(p.remaining() < size)
            {
                p.forceoverread();
                break;
            }
            if(securemapcheck(text))
            {
                p.len += size;
                break;
            }
            writemap(path(text), mapsize, &p.buf[p.len]);
            p.len += mapsize;
            writecfg(path(text), cfgsize, &p.buf[p.len]);
            p.len += cfgsize;
            changemapserv(text, gamemode);
            break;
        }
    }
}

void localservertoclient(int chan, uchar *buf, int len)   // processes any updates from the server
{
    ucharbuf p(buf, len);

    switch(chan)
    {
        case 0: parsepositions(p); break;
        case 1: parsemessages(-1, NULL, p); break;
        case 2: receivefile(p.buf, p.maxlen); break;
    }
}
