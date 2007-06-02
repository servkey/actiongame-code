// server.cpp: little more than enhanced multicaster
// runs dedicated or as client coroutine

#include "cube.h" 

#define valid_client(c) (clients.inrange(c) && clients[c]->type!=ST_EMPTY)
#define valid_flag(f) (f >= 0 && f < 2)

enum { ST_EMPTY, ST_LOCAL, ST_TCPIP };
enum { MM_OPEN, MM_PRIVATE, MM_NUM };

int mastermode = MM_OPEN;

struct clientscore
{
    int frags, flags, lifesequence;

    void reset()
    {
        frags = flags = lifesequence = 0;
    }
};

struct savedscore : clientscore
{
    string name;
    uint ip;
};

static vector<savedscore> scores;

struct client                   // server side version of "dynent" type
{
    int type;
    int clientnum;
    ENetPeer *peer;
    string hostname;
    string mapvote;
    string name, team;
    int modevote;
	uint pos[3];
    clientscore score;
    bool arenadeath;
    int role;
	bool isauthed; // for passworded servers
    vector<uchar> position, messages;

    void reset()
    {
        name[0] = 0;
        score.reset();
        arenadeath = true;
        position.setsizenodelete(0);
        messages.setsizenodelete(0);
        isauthed = false;
        role = CR_DEFAULT;
    }
};

vector<client *> clients;

struct worldstate
{
    enet_uint32 uses;
    vector<uchar> positions, messages;
};

vector<worldstate *> worldstates;

void cleanworldstate(ENetPacket *packet)
{
   loopv(worldstates)
   {
       worldstate *ws = worldstates[i];
       if(packet->data >= ws->positions.getbuf() && packet->data <= &ws->positions.last()) ws->uses--;
       else if(packet->data >= ws->messages.getbuf() && packet->data <= &ws->messages.last()) ws->uses--;
       else continue;
       if(!ws->uses)
       {
           delete ws;
           worldstates.remove(i);
       }
       break;
   }
}

int bsend = 0, brec = 0, laststatus = 0, lastsec = 0;

void sendpacket(int n, int chan, ENetPacket *packet)
{
    if(n<0)
    {
        loopv(clients) sendpacket(i, chan, packet);
        return;
    }
    switch(clients[n]->type)
    {
        case ST_TCPIP:
        {
            enet_peer_send(clients[n]->peer, chan, packet);
            bsend += (int)packet->dataLength;
            break;
        }

        case ST_LOCAL:
            localservertoclient(chan, packet->data, (int)packet->dataLength);
            break;
    }
}

static bool reliablemessages = false;

bool buildworldstate()
{
    static struct { int posoff, msgoff, msglen; } pkt[MAXCLIENTS];
    worldstate &ws = *new worldstate;
    loopv(clients) 
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP) continue;
        if(c.position.empty()) pkt[i].posoff = -1;
        else
        {
            pkt[i].posoff = ws.positions.length();
            loopvj(c.position) ws.positions.add(c.position[j]);
        }
        if(c.messages.empty()) pkt[i].msgoff = -1;
        else
        {
            pkt[i].msgoff = ws.messages.length();
            ucharbuf p = ws.messages.reserve(16);
            putint(p, SV_CLIENT);
            putint(p, c.clientnum);
            putuint(p, c.messages.length());
            ws.messages.addbuf(p);
            loopvj(c.messages) ws.messages.add(c.messages[j]);
            pkt[i].msglen = ws.messages.length()-pkt[i].msgoff;
        }
    }
    int psize = ws.positions.length(), msize = ws.messages.length();
    loopi(psize) { uchar c = ws.positions[i]; ws.positions.add(c); }
    loopi(msize) { uchar c = ws.messages[i]; ws.messages.add(c); }
    ws.uses = 0;
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP) continue;
        ENetPacket *packet;
        if(psize && (pkt[i].posoff<0 || psize-c.position.length()>0))
        {
            packet = enet_packet_create(&ws.positions[pkt[i].posoff<0 ? 0 : pkt[i].posoff+c.position.length()],
                                        pkt[i].posoff<0 ? psize : psize-c.position.length(),
                                        ENET_PACKET_FLAG_NO_ALLOCATE);
            sendpacket(c.clientnum, 0, packet);
            if(!packet->referenceCount) enet_packet_destroy(packet);
            else { ++ws.uses; packet->freeCallback = cleanworldstate; }
        }
        c.position.setsizenodelete(0);

        if(msize && (pkt[i].msgoff<0 || msize-pkt[i].msglen>0))
        {
            packet = enet_packet_create(&ws.messages[pkt[i].msgoff<0 ? 0 : pkt[i].msgoff+pkt[i].msglen],
                                        pkt[i].msgoff<0 ? msize : msize-pkt[i].msglen,
                                        (reliablemessages ? ENET_PACKET_FLAG_RELIABLE : 0) | ENET_PACKET_FLAG_NO_ALLOCATE);
            sendpacket(c.clientnum, 1, packet);
            if(!packet->referenceCount) enet_packet_destroy(packet);
            else { ++ws.uses; packet->freeCallback = cleanworldstate; }
        }
        c.messages.setsizenodelete(0);
    }
    reliablemessages = false;
    if(!ws.uses)
    {
        delete &ws;
        return false;
    }
    else
    {
        worldstates.add(&ws);
        return true;
    }
}

int maxclients = DEFAULTCLIENTS, scorethreshold = -5;
string smapname;

char *adminpasswd = NULL, *motd = NULL;

int numclients()
{
    int num = 0;
    loopv(clients) if(clients[i]->type!=ST_EMPTY) num++;
    return num;
}

void zapclient(int c)
{
	if(!clients.inrange(c)) return;
	clients[c]->type = ST_EMPTY;
    clients[c]->isauthed = false;
    clients[c]->role = CR_DEFAULT;
}

int freeteam()
{
	int teamsize[2] = {0, 0};
	loopv(clients) if(clients[i]->type!=ST_EMPTY) teamsize[team_int(clients[i]->team)]++;
	if(teamsize[0] == teamsize[1]) return rnd(2);
	return teamsize[0] < teamsize[1] ? 0 : 1;
}

clientscore *findscore(client &c, bool insert)
{
    if(c.type!=ST_TCPIP) return NULL;
    if(!insert) loopv(clients)
    {
        client &o = *clients[i];
        if(o.type!=ST_TCPIP) continue;
        if(o.clientnum!=c.clientnum && o.peer->address.host==c.peer->address.host && !strcmp(o.name, c.name)) return &o.score;
    }
    loopv(scores)
    {
        savedscore &sc = scores[i];
        if(!strcmp(sc.name, c.name) && sc.ip==c.peer->address.host) return &sc;
    }
    if(!insert) return NULL;
    savedscore &sc = scores.add();
    sc.reset();
    s_strcpy(sc.name, c.name);
    sc.ip = c.peer->address.host;
    return &sc;
}

void resetscores()
{
    loopv(clients) if(clients[i]->type==ST_TCPIP)
    {
        clients[i]->score.reset();
    }
    scores.setsize(0);
}

vector<server_entity> sents;

bool notgotitems = true;        // true when map has changed and waiting for clients to send item

// allows the gamemode macros to work with the server mode
#define gamemode smode
int smode = 0;

void restoreserverstate(vector<entity> &ents)   // hack: called from savegame code, only works in SP
{
    loopv(sents)
    {
        sents[i].spawned = ents[i].spawned;
        sents[i].spawnsecs = 0;
    } 
}

int interm = 0, minremain = 0, mapend = 0;
bool mapreload = false, autoteam = true;

string serverpassword = "";

bool isdedicated;
ENetHost *serverhost = NULL;

void process(ENetPacket *packet, int sender, int chan);

void sendf(int cn, int chan, const char *format, ...)
{
    bool reliable = false;
    if(*format=='r') { reliable = true; ++format; }
    ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
    ucharbuf p(packet->data, packet->dataLength);
    va_list args;
    va_start(args, format);
    while(*format) switch(*format++)
    {
        case 'i':
        {
            int n = isdigit(*format) ? *format++-'0' : 1;
            loopi(n) putint(p, va_arg(args, int));
            break;
        }
        case 's': sendstring(va_arg(args, const char *), p); break;
    }
    va_end(args);
    enet_packet_resize(packet, p.length());
    sendpacket(cn, chan, packet);
    if(packet->referenceCount==0) enet_packet_destroy(packet);
}

void sendservmsg(char *msg, int client=-1)
{
    sendf(client, 1, "ris", SV_SERVMSG, msg);
}

struct sflaginfo
{
    int state;
    int actor_cn;
    int pos[3];
    int lastupdate;
} sflaginfos[2];

void sendflaginfo(int flag, int action, int cn = -1)
{
    sflaginfo &f = sflaginfos[flag];
    ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    ucharbuf p(packet->data, packet->dataLength);
    putint(p, SV_FLAGINFO);
    putint(p, flag);
    putint(p, f.state);
    putint(p, action);
    if(f.state==CTFF_STOLEN || action==SV_FLAGRETURN) putint(p, f.actor_cn);
    else if(f.state==CTFF_DROPPED) loopi(3) putint(p, f.pos[i]);
    enet_packet_resize(packet, p.length());
    sendpacket(cn, 1, packet);
    if(packet->referenceCount==0) enet_packet_destroy(packet);
}

void flagaction(int flag, int action, int sender)
{
    if(!valid_flag(flag)) return;
	sflaginfo &f = sflaginfos[flag];

	switch(action)
	{
		case SV_FLAGPICKUP:
		{
			if(f.state == CTFF_STOLEN) return;
			f.state = CTFF_STOLEN;
			f.actor_cn = sender;
			break;
		}
		case SV_FLAGDROP:
		{
			if(f.state!=CTFF_STOLEN || (sender != -1 && f.actor_cn != sender)) return;
            f.state = CTFF_DROPPED;
            loopi(3) f.pos[i] = clients[sender]->pos[i];
            break;
		}
		case SV_FLAGRETURN:
		{
            if(f.state!=CTFF_DROPPED) return;
            f.state = CTFF_INBASE;
            f.actor_cn = sender;
			break;
		}
		case SV_FLAGRESET:
		{
			if(sender != -1 && f.actor_cn != sender) return;
			f.state = CTFF_INBASE;
			break;
		}
		case SV_FLAGSCORE:
		{
			if(f.state != CTFF_STOLEN) return;
			f.state = CTFF_INBASE;
			break;
		}
		default: return;
	}

	f.lastupdate = lastsec;
	sendflaginfo(flag, action);
}

void ctfreset()
{
    loopi(2) 
    {
        sflaginfos[i].actor_cn = 0;
        sflaginfos[i].state = CTFF_INBASE;
        sflaginfos[i].lastupdate = -1;
    }
}

int arenaround = 0;

void arenareset()
{
    if(!m_arena) return;

    arenaround = 0;
    loopv(clients) clients[i]->arenadeath = false;
    sendf(-1, 1, "ri", SV_ARENASPAWN);
}

void arenacheck(int secs)
{
    if(!m_arena || interm || secs<arenaround || clients.empty()) return;

    if(arenaround)
    {
        arenareset();
        return;
    }

#ifndef STANDALONE
    if(m_botmode && clients[0]->type==ST_LOCAL)
    {
        bool alive = false, dead = false;
        loopv(players) if(players[i])
        {
            if(players[i]->state==CS_DEAD) dead = true;
            else alive = true;
        }
        if((dead && !alive) || clients[0]->arenadeath)
        {
            sendf(-1, 1, "riis", SV_ARENAWIN, alive || player1->state!=CS_DEAD ? 1 : 0, player1->state==CS_DEAD ? "" : player1->name);
            arenaround = secs+5;
        }
        return;
    }
#endif
    client *alive = NULL;
    bool dead = false;
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type==ST_EMPTY) continue;
        if(c.arenadeath) dead = true;
        else if(!alive) alive = &c;
        else if(!m_teammode || strcmp(alive->team, c.team)) return;
    }
    if(!dead) return;
    sendf(-1, 1, "riis", SV_ARENAWIN, alive ? 1 : 0, !alive ? "" : (m_teammode ? alive->team : alive->name));
    arenaround = secs+5;
}

void sendteamtext(char *text, int sender)
{
    if(!clients[sender]->team[0]) return;
    ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    ucharbuf p(packet->data, packet->dataLength);
    putint(p, SV_TEAMTEXT);
    putint(p, sender);
    sendstring(text, p);
    enet_packet_resize(packet, p.length());
    loopv(clients) if(i!=sender && !strcmp(clients[i]->team, clients[sender]->team)) sendpacket(i, 1, packet);
    if(packet->referenceCount==0) enet_packet_destroy(packet);
}

const char *disc_reason(int reason)
{
    static char *disc_reasons[] = { "normal", "end of packet", "client num", "kicked by server operator", "banned by server operator", "tag type", "connection refused due to ban", "wrong password", "failed admin login", "server FULL - maxclients", "server mastermode is \"private\"", "auto kick - did your score drop below the threshold?" };
    return reason >= 0 && (size_t)reason < sizeof(disc_reasons)/sizeof(disc_reasons[0]) ? disc_reasons[reason] : "unknown";
}

void disconnect_client(int n, int reason = -1)
{
    if(!clients.inrange(n) || clients[n]->type!=ST_TCPIP) return;
    if(m_ctf) loopi(2) if(sflaginfos[i].state==CTFF_STOLEN && sflaginfos[i].actor_cn==n) flagaction(i, SV_FLAGDROP, n);
    client &c = *clients[n];
    clientscore *sc = findscore(c, true);
    if(sc) *sc = c.score;
	if(reason>=0) printf("disconnecting client (%s) [%s]\n", c.hostname, disc_reason(reason));
    else printf("disconnected client (%s)\n", c.hostname);
    c.peer->data = (void *)-1;
    if(reason>=0) enet_peer_disconnect(c.peer, reason);
	zapclient(n);
    sendf(-1, 1, "rii", SV_CDIS, n);
}

void resetitems() { sents.setsize(0); notgotitems = true; }

bool serverpickup(uint i, int sec, int sender)         // server side item pickup, acknowledge first client that gets it
{
    if(i>=(uint)sents.length()) return false;
    if(sents[i].spawned)
    {
        sents[i].spawned = false;
        sents[i].spawnsecs = sec;
        if(sender>=0) sendf(sender, 1, "rii", SV_ITEMACC, i);
		return true;
    }
	return false;
}

struct configset
{
    string mapname;
    int mode;
    int time;
    bool vote;
};

vector<configset> configsets;
int curcfgset = -1;

void readscfg(char *cfg)
{
    configsets.setsize(0);

    string s;
    s_strcpy(s, cfg);
    char *buf = loadfile(path(s), NULL);
    if(!buf) return;
    char *p, *l;
    
    p = buf;
    while((p = strstr(p, "//")) != NULL) // remove comments
        while(p[0] != '\n' && p[0] != '\0') p++[0] = ' ';
    
    l = buf;
    bool lastline = false;
    while((p = strstr(l, "\n")) != NULL || (l[0] && (lastline=true))) // remove empty/invalid lines
    {
        size_t len = lastline ? strlen(l) : p-l;
        string line;
        s_strncpy(line, l, len+1);
        char *d = line;
        int n = 0;
		while((p = strstr(d, ":")) != NULL) { d = p+1; n++; }
        if(n!=3) memset(l, ' ', len+1);
        if(lastline) { l[len+1] = 0; break; }
        l += len+1;
    }
         
    configset c;
    int argc = 0;
    string argv[4];

    p = strtok(buf, ":\n\0");
    while(p != NULL)
    {
        strcpy(argv[argc], p);
        if(++argc==4)
        {
            int numspaces;
            for(numspaces = 0; argv[0][numspaces]==' '; numspaces++){} // ingore space crap
            strcpy(c.mapname, argv[0]+numspaces);
            c.mode = atoi(argv[1]);
            c.time = atoi(argv[2]);
            c.vote = atoi(argv[3]) > 0;
            configsets.add(c);
            argc = 0;
        }
        p = strtok(NULL, ":\n\0");
    }
}

void resetvotes()
{
    loopv(clients) clients[i]->mapvote[0] = 0;
}

void forceteam(int client, int team)
{
    if(!valid_client(client) || team < 0 || team > 1) return;
    sendf(client, 1, "rii", SV_FORCETEAM, team);
}

void shuffleteams()
{
	int numplayers = numclients();
	int teamsize[2] = {0, 0};
	loopv(clients) if(clients[i]->type!=ST_EMPTY)
	{
		int team = rnd(2);
		if(teamsize[team] >= numplayers/2) team = team_opposite(team);
		forceteam(i, team);
		teamsize[team]++;
	}
}

void resetmap(const char *newname, int newmode, int newtime = -1, bool notify = true)
{
	bool lastteammode = m_teammode;
    smode = newmode;
    minremain = newtime >= 0 ? newtime : (m_teammode ? 15 : 10);
    s_strcpy(smapname, newname);

    mapend = lastsec+minremain*60;
    mapreload = false;
    interm = 0;
    laststatus = lastsec-61;
    resetvotes();
    resetitems();
    resetscores();
    ctfreset();
    if(notify) sendf(-1, 1, "risi", SV_MAPCHANGE, smapname, smode);
	if(m_teammode && !lastteammode) shuffleteams();
    arenareset();
}

void nextcfgset(bool notify = true) // load next maprotation set
{   
    curcfgset++;
    if(curcfgset>=configsets.length() || curcfgset<0) curcfgset=0;
    
    configset &c = configsets[curcfgset];
    resetmap(c.mapname, c.mode, c.time, notify);
}

bool vote(char *map, int reqmode, int sender)
{
	if(!valid_client(sender)) return false;

    if(configsets.length() && curcfgset < configsets.length() && !configsets[curcfgset].vote && clients[sender]->role == CR_DEFAULT)
    {
        s_sprintfd(msg)("%s voted, but voting is currently disabled", clients[sender]->name);
        sendservmsg(msg);
        return false;
    }

    s_strcpy(clients[sender]->mapvote, map);
    clients[sender]->modevote = reqmode;
    int yes = 0, no = 0; 
    loopv(clients) if(clients[i]->type!=ST_EMPTY)
    {
        if(clients[i]->mapvote[0]) { if(strcmp(clients[i]->mapvote, map)==0 && clients[i]->modevote==reqmode) yes++; else no++; }
        else no++;
    }
    if(yes==1 && no==0) return true;  // single player
    s_sprintfd(msg)("%s suggests mode \"%s\" on map %s (set map to vote)", clients[sender]->name, modestr(reqmode), map);
    sendservmsg(msg);
    if(yes/(float)(yes+no) <= 0.5f && clients[sender]->role == CR_DEFAULT) return false;
    sendservmsg("vote passed");
    resetvotes();
    return true;
}

struct ban
{
	ENetAddress address;
	int secs;
};

vector<ban> bans;

bool isbanned(int cn)
{
	if(!valid_client(cn)) return false;
	client &c = *clients[cn];
	loopv(bans)
	{
		ban &b = bans[i];
		if(b.secs < lastsec) { bans.remove(i--); }
		if(b.address.host == c.peer->address.host) { return true; }
	}
	return false;
}

int serveroperator()
{
	loopv(clients) if(clients[i]->type!=ST_EMPTY && clients[i]->role > CR_DEFAULT) return i;
	return -1;
}

void sendserveropinfo(int receiver)
{
    int op = serveroperator();
    sendf(receiver, 1, "riii", SV_SERVOPINFO, op, op >= 0 ? clients[op]->role : -1);
}

void changeclientrole(int client, int role, char *pwd = NULL, bool force=false)
{
    if(!isdedicated || !valid_client(client)) return;
    int serverop = serveroperator();
    if(force || role == CR_DEFAULT || (role == CR_MASTER && serverop < 0) || (role == CR_ADMIN && pwd && pwd[0] && adminpasswd && !strcmp(adminpasswd, pwd)))
    {
        if(role == clients[client]->role) return;
        if(role > CR_DEFAULT) loopv(clients) clients[i]->role = CR_DEFAULT;
        clients[client]->role = role;
        sendserveropinfo(-1);
    }
    else if(pwd && pwd[0]) disconnect_client(client, DISC_SOPLOGINFAIL); // avoid brute-force
}

void serveropcmddenied(int receiver, int requiredrole)
{
    sendf(receiver, 1, "rii", SV_SERVOPCMDDENIED, requiredrole);
}

void serveropcmd(int sender, int cmd, int a)
{
	if(!isdedicated || !valid_client(sender) || cmd < 0 || cmd >= SOPCMD_NUM) return;
    #define requirerole(r) if(clients[sender]->role < r) { sendf(sender, 1, "rii", SV_SERVOPCMDDENIED, r); return; }

	switch(cmd)
	{
		case SOPCMD_KICK:
		{
            requirerole(CR_MASTER);
            if(!valid_client(a)) return;
			disconnect_client(a, DISC_MKICK);
			break;
		}
		case SOPCMD_MASTERMODE:
		{
            requirerole(CR_MASTER);
			if(a < 0 || a >= MM_NUM) return;
			mastermode = a;
			break;
		}
		case SOPCMD_AUTOTEAM:
		{
            requirerole(CR_MASTER);
			if(a < 0 || a > 1) return;
			if((autoteam = a == 1) == true && m_teammode) shuffleteams();
			break;
		}
		case SOPCMD_BAN:
		{
            requirerole(CR_MASTER);
			if(!valid_client(a)) return;
			ban b = { clients[a]->peer->address, lastsec+20*60 };
			bans.add(b);
			disconnect_client(a, DISC_MBAN);
			break;
		}
		case SOPCMD_REMBANS:
		{
            requirerole(CR_MASTER);
			if(bans.length()) bans.setsize(0);
			break;
		}
        case SOPCMD_FORCETEAM:
        {
            requirerole(CR_MASTER);
            if(!valid_client(a)) return;
            forceteam(a, team_opposite(team_int(clients[a]->team)));
            break;
        }
        case SOPCMD_GIVEMASTER:
        {
            requirerole(CR_ADMIN);
            if(!valid_client(a)) return;
            changeclientrole(a, CR_MASTER, NULL, true);
            break;
        }
	}
	sendf(-1, 1, "riii", SV_SERVOPCMD, cmd, a);
}

int numnonlocalclients() 
{
    int nonlocalclients = 0;
    loopv(clients) if(clients[i]->type==ST_TCPIP) nonlocalclients++;
    return nonlocalclients;
}

// sending of maps between clients

string copyname; 
int copysize;
uchar *copydata = NULL;

void sendmapserv(int n, string mapname, int mapsize, uchar *mapdata)
{   
    if(mapsize <= 0 || mapsize > 256*256) return;
    s_strcpy(copyname, mapname);
    copysize = mapsize;
    DELETEA(copydata);
    copydata = new uchar[mapsize];
    memcpy(copydata, mapdata, mapsize);
}

ENetPacket *getmapserv(int n)
{
    if(!copydata) return NULL;
    ENetPacket *packet = enet_packet_create(NULL, MAXTRANS + copysize, ENET_PACKET_FLAG_RELIABLE);
    ucharbuf p(packet->data, packet->dataLength);
    putint(p, SV_RECVMAP);
    sendstring(copyname, p);
    putint(p, copysize);
    p.put(copydata, copysize);
    enet_packet_resize(packet, p.length());
    return packet;
}

int checktype(int type, client *cl)
{
    if(cl && cl->type==ST_LOCAL) return type;
    // only allow edit messages in coop-edit mode
    static int edittypes[] = { SV_EDITENT, SV_EDITH, SV_EDITT, SV_EDITS, SV_EDITD, SV_EDITE };
    if(cl && smode!=1) loopi(sizeof(edittypes)/sizeof(int)) if(type == edittypes[i]) return -1;
    // server only messages
    static int servtypes[] = { SV_INITS2C, SV_MAPRELOAD, SV_SERVMSG, SV_ITEMACC, SV_ITEMSPAWN, SV_TIMEUP, SV_CDIS, SV_PONG, SV_RESUME, SV_FLAGINFO, SV_ARENASPAWN, SV_ARENAWIN, SV_CLIENT };
    if(cl) loopi(sizeof(servtypes)/sizeof(int)) if(type == servtypes[i]) return -1;
    return type;
}

// server side processing of updates: does very little and most state is tracked client only
// could be extended to move more gameplay to server (at expense of lag)

void process(ENetPacket *packet, int sender, int chan)   // sender may be -1
{
    ucharbuf p(packet->data, packet->dataLength);
    char text[MAXTRANS];
    client *cl = sender>=0 ? clients[sender] : NULL;
    int type;

    if(cl && !cl->isauthed)
    {
        int nonlocalclients = numnonlocalclients();
        int primaryreason = serverpassword[0] ? DISC_WRONGPW : (mastermode!=MM_OPEN ? DISC_MASTERMODE : (nonlocalclients>maxclients ? DISC_MAXCLIENTS : (isbanned(sender) ? DISC_MBAN : DISC_NONE)));

        if(primaryreason == DISC_NONE) cl->isauthed = true;
        else if(chan==0) return;
        else if(chan!=1 || getint(p)!=SV_PWD) disconnect_client(sender, primaryreason);
        else
        {
            getstring(text, p);
            if(adminpasswd[0] && !strcmp(text, adminpasswd)) // pass admins always through
            { 
                cl->isauthed = true;
                changeclientrole(sender, CR_ADMIN, NULL, true);
                loopv(bans) if(bans[i].address.host == cl->peer->address.host) { bans.remove(i); break; } // remove admin bans
                if(nonlocalclients>maxclients) for(int i = 0; i < nonlocalclients; i++) if(i != sender && clients[i]->type==ST_TCPIP) 
                {
                    disconnect_client(i, DISC_MAXCLIENTS); // disconnect someone else to fit maxclients again
                    break;
                }
            }
            else if(serverpassword[0])
            {
                if(!strcmp(text, serverpassword)) cl->isauthed = true;
                else disconnect_client(sender, DISC_WRONGPW);
            }
            else if(mastermode==MM_PRIVATE) disconnect_client(sender, DISC_MASTERMODE);
            else if(nonlocalclients>maxclients) disconnect_client(sender, DISC_MAXCLIENTS);
            else disconnect_client(sender, DISC_MBAN);
        }
        if(!cl->isauthed) return;
    }

    if(packet->flags&ENET_PACKET_FLAG_RELIABLE) reliablemessages = true;

    #define QUEUE_MSG { if(cl->type==ST_TCPIP) while(curmsg<p.length()) cl->messages.add(p.buf[curmsg++]); }
    int curmsg;
    while((curmsg = p.length()) < p.maxlen) switch(type = checktype(getint(p), cl))
    {
        case SV_PWD:
            getstring(text, p);
            break;

        case SV_TEAMTEXT:
            getstring(text, p);
            sendteamtext(text, sender);
            break;

        case SV_TEXT:
            getstring(text, p);
            QUEUE_MSG;
            break;

        case SV_INITC2S:
        {
            bool newclient = false;
            if(!cl->name[0]) newclient = true;
            getstring(text, p);
            if(!text[0]) s_strcpy(text, "unarmed");
            s_strncpy(cl->name, text, MAXNAMELEN+1);
            if(newclient && cl->type==ST_TCPIP)
            {
                clientscore *sc = findscore(*cl, false);
                if(sc)
                {
                    cl->score = *sc; 
                    sendf(-1, 1, "ri5", SV_RESUME, sender, sc->frags, sc->flags, sc->lifesequence);
                }
            }
            getstring(text, p);
			s_strncpy(cl->team, text, MAXTEAMLEN+1);
            getint(p);
            getint(p);
            QUEUE_MSG;
            break;
        }

        case SV_MAPCHANGE:
        {
            getstring(text, p);
            int reqmode = getint(p);
            if(cl->type==ST_TCPIP && !m_mp(reqmode)) reqmode = 0;
            if(smapname[0] && !mapreload && !vote(text, reqmode, sender)) return;
            resetmap(text, reqmode);
            break;
        }
        
        case SV_ITEMLIST:
        {
            int n;
            while((n = getint(p))!=-1) if(notgotitems)
            {
                server_entity se = { false, 0 };
                while(sents.length()<=n) sents.add(se);
                sents[n].spawned = true;
            }
            notgotitems = false;
            QUEUE_MSG;
            break;
        }

        case SV_ITEMPICKUP:
        {
            int n = getint(p);
            serverpickup(n, getint(p), sender);
            QUEUE_MSG;
            break;
        }
        
        case SV_PING:
            sendf(sender, 1, "ii", SV_PONG, getint(p));
            break;

        case SV_POS:
        {
            int cn = getint(p);
            if(cn!=sender)
            {
                disconnect_client(sender, DISC_CN);
#ifndef STANDALONE
                conoutf("ERROR: invalid client (msg %i)", type);
#endif
                return;
            }
            loopi(3) clients[cn]->pos[i] = getuint(p);
            getuint(p);
            loopi(6) getint(p);
            if(cl->type==ST_TCPIP)
            {
                cl->position.setsizenodelete(0);
                while(curmsg<p.length()) cl->position.add(p.buf[curmsg++]);
            }
            break;
        }

        case SV_SENDMAP:
        {
            getstring(text, p);
            int mapsize = getint(p);
            if(p.remaining() < mapsize)
            {
                p.forceoverread();
                break;
            }
            sendmapserv(sender, text, mapsize, &p.buf[p.len]);
            p.len += mapsize;
            break;
        }

        case SV_RECVMAP:
        {
            ENetPacket *mappacket = getmapserv(sender);
            if(mappacket) sendpacket(sender, 2, mappacket);
            else sendservmsg("no map to get", sender);
            break;
        }
			
		case SV_FLAGPICKUP:
		case SV_FLAGDROP:
		case SV_FLAGRETURN:
		case SV_FLAGSCORE:
		case SV_FLAGRESET:
		{
			flagaction(getint(p), type, sender);
			break;
		}

		case SV_SETMASTER:
		{
            changeclientrole(sender, getint(p) != 0 ? CR_MASTER : CR_DEFAULT, NULL);
			break;
		}

        case SV_SETADMIN:
		{
			bool claim = getint(p) != 0;
			getstring(text, p);
            changeclientrole(sender, claim ? CR_ADMIN : CR_DEFAULT, text);
			break;
		}

		case SV_SERVOPCMD:
		{
			int cmd = getint(p);
			int arg = getint(p);
            serveropcmd(sender, cmd, arg);
			break;
		}

        case SV_FRAGS:
            cl->score.frags = getint(p);
            if(cl->score.frags < scorethreshold) disconnect_client(sender, DISC_AUTOKICK);
            QUEUE_MSG;
            break;

        case SV_FLAGS:
            cl->score.flags = getint(p);
            QUEUE_MSG;
            break;

        case SV_GIBDIED:
        case SV_DIED:
            getint(p);
            if(m_arena) cl->arenadeath = true;
            cl->score.lifesequence++;
            QUEUE_MSG;
            break;
        default:
        {
            int size = msgsizelookup(type);
            if(size==-1) { if(sender>=0) disconnect_client(sender, DISC_TAGT); return; }
            loopi(size-1) getint(p);
            QUEUE_MSG;
            break;
        }
    }

    if(p.overread() && sender>=0) disconnect_client(sender, DISC_EOP);
}

void send_welcome(int n)
{
    ENetPacket *packet = enet_packet_create(NULL, MAXTRANS, ENET_PACKET_FLAG_RELIABLE);
    ucharbuf p(packet->data, packet->dataLength);
    putint(p, SV_INITS2C);
    putint(p, n);
    putint(p, PROTOCOL_VERSION);
    if(!smapname[0] && configsets.length()) nextcfgset(false);
    int numcl = numclients();
    putint(p, smapname[0] ? numcl : -1);
	putint(p, serverpassword[0] ? 1 : 0);
    if(smapname[0])
    {
        putint(p, SV_MAPCHANGE);
        sendstring(smapname, p);
        putint(p, smode);
		if(!configsets.length() || numcl > 1)
		{
			putint(p, SV_ITEMLIST);
			loopv(sents) if(sents[i].spawned) putint(p, i);
			putint(p, -1);
		}
    }
	if(clients[n]->type == ST_TCPIP && serveroperator() != -1) sendserveropinfo(n);
    loopv(clients)
    {
        client &c = *clients[i];
        if(c.type!=ST_TCPIP || c.clientnum==n) continue;
        if(!c.score.frags && !c.score.flags) continue;
        putint(p, SV_RESUME);
        putint(p, c.clientnum);
        putint(p, c.score.frags);
        putint(p, c.score.flags);
        putint(p, c.score.lifesequence);
    }
	if(autoteam)
	{
		putint(p, SV_FORCETEAM);
		putint(p, freeteam());
	}
	putint(p, SV_SERVOPCMD);
	putint(p, SOPCMD_AUTOTEAM);
	putint(p, autoteam);
    if(motd)
    {
        putint(p, SV_TEXT);
        sendstring(motd, p);
    }
    enet_packet_resize(packet, p.length());
    sendpacket(n, 1, packet);
    if(smapname[0] && m_ctf) loopi(2) sendflaginfo(i, -1, n);
    if(m_arena && numcl<=2) clients[n]->arenadeath = false;
}

void localclienttoserver(int chan, ENetPacket *packet)
{
    process(packet, 0, chan);
    if(!packet->referenceCount) enet_packet_destroy(packet);
}

client &addclient()
{
    client *c = NULL; 
    loopv(clients) if(clients[i]->type==ST_EMPTY) { c = clients[i]; break; }
    if(!c) 
    { 
        c = new client; 
        c->clientnum = clients.length(); 
        clients.add(c);
    }
    c->reset();
    return *c;
}

void checkintermission()
{
    if(!minremain)
    {
        interm = lastsec+10;
        mapend = lastsec+1000;
    }
    sendf(-1, 1, "rii", SV_TIMEUP, minremain--);
}

/*void startintermission() { minremain = 0; checkintermission(); }*/

void resetserverifempty()
{
    loopv(clients) if(clients[i]->type!=ST_EMPTY) return;
    //clients.setsize(0);
    resetmap("", 0, 10, false);
	mastermode = MM_OPEN;
	autoteam = true;
}

void sendworldstate()
{
    static enet_uint32 lastsend = 0;
    if(clients.empty()) return;
    enet_uint32 curtime = enet_time_get()-lastsend;
    if(curtime<40) return;
    bool flush = buildworldstate();
    lastsend += curtime - (curtime%40);
    if(flush) enet_host_flush(serverhost);
}

void serverslice(int seconds, unsigned int timeout)   // main server update, called from cube main loop in sp, or dedicated server loop
{
    loopv(sents)        // spawn entities when timer reached
    {
        if(sents[i].spawnsecs && (sents[i].spawnsecs -= seconds-lastsec)<=0)
        {
            sents[i].spawnsecs = 0;
            sents[i].spawned = true;
            sendf(-1, 1, "rii", SV_ITEMSPAWN, i);
        }
    }
    
    if(m_ctf) loopi(2)
    {
        sflaginfo &f = sflaginfos[i];
		if(f.state==CTFF_DROPPED && seconds-f.lastupdate>30) flagaction(i, SV_FLAGRESET, -1);
    }
    if(m_arena) arenacheck(seconds);
 
    lastsec = seconds;
   
    int nonlocalclients = numnonlocalclients();

	if((smode>1 || (smode==0 && nonlocalclients)) && seconds>mapend-minremain*60) checkintermission();
    if(interm && seconds>interm)
    {
        interm = 0;

        if(configsets.length()) nextcfgset();
        else loopv(clients) if(clients[i]->type!=ST_EMPTY)
        {
            sendf(i, 1, "rii", SV_MAPRELOAD, 0);    // ask a client to trigger map reload
            mapreload = true;
            break;
        }
    }

    resetserverifempty();
    
    if(!isdedicated) return;     // below is network only

	serverms(smode, numclients(), minremain, smapname, seconds);

    if(seconds-laststatus>60)   // display bandwidth stats, useful for server ops
    {
        laststatus = seconds;     
		if(nonlocalclients || bsend || brec) 
		{ 
			printf("status: %d remote clients, %.1f send, %.1f rec (K/sec)\n", nonlocalclients, bsend/60.0f/1024, brec/60.0f/1024); 
#ifdef _DEBUG
			fflush(stdout);
#endif
		}
        bsend = brec = 0;
    }

    ENetEvent event;
    if(enet_host_service(serverhost, &event, timeout) > 0)
    {
        switch(event.type)
        {
            case ENET_EVENT_TYPE_CONNECT:
            {
                client &c = addclient();
                c.type = ST_TCPIP;
                c.peer = event.peer;
                c.peer->data = (void *)(size_t)c.clientnum;
				char hn[1024];
				s_strcpy(c.hostname, (enet_address_get_host_ip(&c.peer->address, hn, sizeof(hn))==0) ? hn : "unknown");
				printf("client connected (%s)\n", c.hostname);
                send_welcome(c.clientnum);
				break;
            }

            case ENET_EVENT_TYPE_RECEIVE:
			{
                brec += (int)event.packet->dataLength;
				int cn = (int)(size_t)event.peer->data;
				if(valid_client(cn)) process(event.packet, cn, event.channelID); 
                if(event.packet->referenceCount==0) enet_packet_destroy(event.packet);
                break;
			}

            case ENET_EVENT_TYPE_DISCONNECT: 
            {
				int cn = (int)(size_t)event.peer->data;
				if(!valid_client(cn)) break;
                disconnect_client(cn);
                break;
            }

            default:
                break;
        }
    }
    sendworldstate();
}

void cleanupserver()
{
    if(serverhost) enet_host_destroy(serverhost);
}

void localdisconnect()
{
    loopv(clients) if(clients[i]->type==ST_LOCAL) zapclient(i);
}

void localconnect()
{
    client &c = addclient();
    c.type = ST_LOCAL;
    s_strcpy(c.hostname, "local");
    send_welcome(c.clientnum);
}

void initserver(bool dedicated, int uprate, char *sdesc, char *ip, char *master, char *passwd, int maxcl, char *maprot, char *adminpwd, char *srvmsg, int scthreshold)
{
    if(passwd) s_strcpy(serverpassword, passwd);
    maxclients = maxcl > 0 ? min(maxcl, MAXCLIENTS) : DEFAULTCLIENTS;
	servermsinit(master ? master : "masterserver.cubers.net/cgi-bin/actioncube.pl/", ip, sdesc, dedicated);
    
    if(isdedicated = dedicated)
    {
        ENetAddress address = { ENET_HOST_ANY, CUBE_SERVER_PORT };
        if(*ip && enet_address_set_host(&address, ip)<0) printf("WARNING: server ip not resolved");
        serverhost = enet_host_create(&address, maxclients+1, 0, uprate);
        if(!serverhost) fatal("could not create server host\n");
        loopi(maxclients) serverhost->peers[i].data = (void *)-1;
		if(!maprot || !maprot[0]) maprot = newstring("config/maprot.cfg");
        readscfg(path(maprot));
        if(adminpwd && adminpwd[0]) adminpasswd = adminpwd;
        if(srvmsg && srvmsg[0]) motd = srvmsg;
        scorethreshold = min(-1, scthreshold);
    }

    resetserverifempty();

    if(isdedicated)       // do not return, this becomes main loop
    {
        #ifdef WIN32
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        #endif
        printf("dedicated server started, waiting for clients...\nCtrl-C to exit\n\n");
        atexit(enet_deinitialize);
        atexit(cleanupserver);
        for(;;) serverslice(/*enet_time_get_sec()*/time(NULL), 5);
    }
}

#ifdef STANDALONE

void localservertoclient(int chan, uchar *buf, int len) {}
void fatal(char *s, char *o) { cleanupserver(); printf("fatal: %s\n", s); exit(EXIT_FAILURE); }

int main(int argc, char **argv)
{   
    int uprate = 0, maxcl = DEFAULTCLIENTS, scthreshold = -5;
    char *sdesc = "", *ip = "", *master = NULL, *passwd = "", *maprot = "", *adminpasswd = NULL, *srvmsg = NULL;

    for(int i = 1; i<argc; i++)
    {
        char *a = &argv[i][2];
        if(argv[i][0]=='-') switch(argv[i][1])
        {
            case 'u': uprate = atoi(a); break;
            case 'n': sdesc  = a; break;
            case 'i': ip     = a; break;
            case 'm': master = a; break;
            case 'p': passwd = a; break;
            case 'c': maxcl  = atoi(a); break;
            case 'r': maprot = a; break; 
            case 'x' : adminpasswd = a; break;
            case 'o' : srvmsg = a; break;
            case 'k': scthreshold = atoi(a); break;
            default: printf("WARNING: unknown commandline option\n");
        }
    }

    if(enet_initialize()<0) fatal("Unable to initialise network module");
    initserver(true, uprate, sdesc, ip, master, passwd, maxcl, maprot, adminpasswd, srvmsg, scthreshold);
    return EXIT_SUCCESS;
}
#endif

