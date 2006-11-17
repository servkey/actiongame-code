/// one big bad include file for the whole engine... nasty!

#ifdef __GNUC__
#define gamma __gamma
#endif

#include <math.h>

#ifdef __GNUC__
#undef gamma
#endif

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#ifdef __GNUC__
#include <new>
#else
#include <new.h>
#endif

#ifdef WIN32
    #define WIN32_LEAN_AND_MEAN
    #include "windows.h"
    #define _WINDOWS
    #define ZLIB_DLL
#endif

#include <SDL.h>
#include <SDL_image.h>

#define GL_GLEXT_LEGACY
#define __glext_h__
#define NO_SDL_GLEXT
#include <SDL_opengl.h>
#undef __glext_h__

#ifdef __APPLE__
#include "OpenGL/glext.h"
#else
#include "GL/glext.h"
#endif

#include <enet/enet.h>

#include <zlib.h>

#include "tools.h"

enum                            // block types, order matters!
{
    SOLID = 0,                  // entirely solid cube [only specifies wtex]
    CORNER,                     // half full corner of a wall
    FHF,                        // floor heightfield using neighbour vdelta values
    CHF,                        // idem ceiling
    SPACE,                      // entirely empty cube
    SEMISOLID,                  // generated by mipmapping
    MAXTYPE
};
 
struct sqr
{
    uchar type;                 // one of the above
    char floor, ceil;           // height, in cubes
    uchar wtex, ftex, ctex;     // wall/floor/ceil texture ids
    uchar r, g, b;              // light value at upper left vertex
    uchar vdelta;               // vertex delta, used for heightfield cubes
    char defer;                 // used in mipmapping, when true this cube is not a perfect mip
    char occluded;              // true when occluded
    uchar utex;                 // upper wall tex id
    uchar tag;                  // used by triggers
};

enum                            // hardcoded texture numbers
{
    DEFAULT_SKY = 0,
    DEFAULT_LIQUID,
    DEFAULT_WALL,
    DEFAULT_FLOOR,
    DEFAULT_CEIL
};

enum                            // static entity types
{
    NOTUSED = 0,                // entity slot not in use in map
    LIGHT,                      // lightsource, attr1 = radius, attr2 = intensity
    PLAYERSTART,                // attr1 = angle
    I_CLIPS, I_AMMO,I_GRENADE, 
    I_HEALTH, I_ARMOUR, I_AKIMBO,
    MAPMODEL,                   // attr1 = angle, attr2 = idx
    CARROT,                     // attr1 = tag, attr2 = type
    LADDER,
    CTF_FLAG,                   // attr1 = angle, attr2 = red/blue
    MAXENTTYPES
};

struct persistent_entity        // map entity
{
    short x, y, z;              // cube aligned position
    short attr1;
    uchar type;                 // type is one of the above
    uchar attr2, attr3, attr4; 
};

struct entity : public persistent_entity    
{
    bool spawned;               // the only dynamic state of a map entity
};

#define MAPVERSION 6            // bump if map format changes, see worldio.cpp

struct header                   // map file format header
{
    char head[4];               // "CUBE"
    int version;                // any >8bit quantity is a little indian
    int headersize;             // sizeof(header)
    int sfactor;                // in bits
    int numents;
    char maptitle[128];
    uchar texlists[3][256];
    int waterlevel;
    int reserved[15];
};

#define SWS(w,x,y,s) (&(w)[(y)*(s)+(x)])
#define SW(w,x,y) SWS(w,x,y,ssize)
#define S(x,y) SW(world,x,y)            // convenient lookup of a lowest mip cube
#define SMALLEST_FACTOR 6               // determines number of mips there can be
#define DEFAULT_FACTOR 8
#define LARGEST_FACTOR 11               // 10 is already insane
#define SOLID(x) ((x)->type==SOLID)
#define MINBORD 2                       // 2 cubes from the edge of the world are always solid
#define OUTBORD(x,y) ((x)<MINBORD || (y)<MINBORD || (x)>=ssize-MINBORD || (y)>=ssize-MINBORD)

struct vec 
{ 
    union
    {
        struct { float x, y, z; };
        float v[3];
    };

    vec() {};
    vec(float a, float b, float c) : x(a), y(b), z(c) {};
    vec(float *v) : x(v[0]), y(v[1]), z(v[2]) {};

    float &operator[](int i)       { return v[i]; };
    float  operator[](int i) const { return v[i]; };

    bool operator==(const vec &o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const vec &o) const { return x != o.x || y != o.y || z != o.z; }

    vec &mul(float f) { x *= f; y *= f; z *= f; return *this; };
    vec &div(float f) { x /= f; y /= f; z /= f; return *this; };
    vec &add(float f) { x += f; y += f; z += f; return *this; };
    vec &sub(float f) { x -= f; y -= f; z -= f; return *this; };

    vec &add(const vec &o) { x += o.x; y += o.y; z += o.z; return *this; };
    vec &sub(const vec &o) { x -= o.x; y -= o.y; z -= o.z; return *this; };

    float squaredlen() const { return x*x + y*y + z*z; };
    float dot(const vec &o) const { return x*o.x + y*o.y + z*o.z; };

    float magnitude() const { return sqrtf(squaredlen()); };
    vec &normalize() { div(magnitude()); return *this; };

    float dist(const vec &e) const { vec t; return dist(e, t); };
    float dist(const vec &e, vec &t) const { t = *this; t.sub(e); return t.magnitude(); };

    bool reject(const vec &o, float max) { return x>o.x+max || x<o.x-max || y>o.y+max || y<o.y-max; };
};

struct block { int x, y, xs, ys; };

enum { GUN_KNIFE = 0, GUN_PISTOL, GUN_SHOTGUN, GUN_SUBGUN, GUN_SNIPER, GUN_ASSAULT, GUN_GRENADE, NUMGUNS };
#define reloadable_gun(g) (g != GUN_KNIFE && g != GUN_GRENADE)

enum { MCMD_KICK = 0, MCMD_BAN, MCMD_REMBANS };


/* Gamemodes
0	tdm
1	coop edit
2	dm
3	survivor
4	team survior
5	ctf
6	pistols
7	bot tdm
8	bot dm
9	last swiss standing
10	one shot, one kill
11  team one shot, one kill
*/

#define m_lms         (gamemode==3 || gamemode==4)
#define m_ctf	      (gamemode==5)
#define m_pistol      (gamemode==6)
#define m_lss		  (gamemode==9)
#define m_osok		  (gamemode==10 || gamemode==11)

#define m_noitems     (m_lms || m_osok)
#define m_noitemsnade (m_lss)
#define m_nopistol	  (m_osok || m_lss)
#define m_noprimary   (m_pistol || m_lss)
#define m_noguns	  (m_nopistol && m_noprimary)
#define m_arena       (m_lms || m_lss || m_osok)
#define m_teammode    (gamemode==0 || gamemode==4 || gamemode==5 || gamemode==7 || gamemode==11)
#define m_tarena      (m_arena && m_teammode)
#define m_botmode	  (gamemode==7 || gamemode == 8)

#define isteam(a,b)   (m_teammode && strcmp(a, b)==0)

#define TEAM_CLA 0 //
#define TEAM_RVSF 1 //
// rb means red/blue
#define rb_team_string(t) ((t) ? "RVSF" : "CLA")
#define rb_team_int(t) (strcmp((t), "CLA") == 0 ? TEAM_CLA : TEAM_RVSF)
#define rb_opposite(o) ((o) == TEAM_CLA ? TEAM_RVSF : TEAM_CLA)


// Added by Rick
class CBot;

#define MAX_STORED_LOCATIONS  7

extern int lastmillis;                  // last time (Moved up by Rick)

class CPrevLocation
{
     int nextupdate;
     
public:
     vec prevloc[MAX_STORED_LOCATIONS];
     
     CPrevLocation(void) : nextupdate(0) { Reset(); };
     void Reset(void) { loopi(MAX_STORED_LOCATIONS) prevloc[i].x=prevloc[i].y=prevloc[i].z=0.0f; };
     void Update(const vec &o)
     {
          extern float GetDistance(vec v1, vec v2);
          
          if (nextupdate > lastmillis) return;
          if (GetDistance(o, prevloc[0]) >= 4.0f)
          {
               for(int i=(MAX_STORED_LOCATIONS-1);i>=1;i--)
                    prevloc[i] = prevloc[i-1];
               prevloc[0] = o;
          }
          nextupdate = lastmillis + 100;
     };
};

struct itemstat { int add, start, max, sound; };
// End add


enum { ANIM_IDLE = 0, ANIM_RUN, ANIM_ATTACK, ANIM_PAIN, ANIM_JUMP, ANIM_LAND, ANIM_FLIPOFF, ANIM_SALUTE, ANIM_TAUNT, ANIM_WAVE, ANIM_POINT, ANIM_CROUCH_IDLE, ANIM_CROUCH_WALK, ANIM_CROUCH_ATTACK, ANIM_CROUCH_PAIN, ANIM_CROUCH_DEATH, ANIM_DEATH, ANIM_LYING_DEAD, ANIM_FLAG, ANIM_GUN_IDLE, ANIM_GUN_SHOOT, ANIM_GUN_RELOAD, ANIM_GUN_THROW, ANIM_MAPMODEL, ANIM_TRIGGER, ANIM_ALL, NUMANIMS };

#define ANIM_INDEX      0xFF
#define ANIM_LOOP       (1<<8)
#define ANIM_START      (1<<9)
#define ANIM_END        (1<<10)
#define ANIM_REVERSE    (1<<11)
#define ANIM_NOINTERP   (1<<12)
#define ANIM_MIRROR     (1<<13)

struct animstate                                // used for animation blending of animated characters
{
    int anim, frame, range, basetime;
    float speed;
    animstate() : anim(0), frame(0), range(0), basetime(0), speed(100.0f) { };

    bool operator==(const animstate &o) const { return frame==o.frame && range==o.range && basetime==o.basetime && speed==o.speed; };
    bool operator!=(const animstate &o) const { return frame!=o.frame || range!=o.range || basetime!=o.basetime || speed!=o.speed; };
};

enum { MDL_MD2 = 1, MDL_MD3 };

struct dynent;

struct model
{   
    float scale;
    vec translate;

    model() : scale(1), translate(0, 0, 0) {};
    virtual ~model() {};
    virtual void render(int anim, int varseed, float speed, int basetime, float x, float y, float z, float yaw, float pitch, dynent *d, model *vwepmdl = NULL, float scale = 1.0f) = 0;
    virtual void setskin(int tex = 0) = 0;
    virtual bool load() = 0;
    virtual char *name() = 0;
    virtual int type() = 0;
};

struct mapmodelinfo { int rad, h, zoff; string name; model *m; };

enum { ENT_PLAYER = 0, ENT_BOT, ENT_CAMERA, ENT_BOUNCE };

enum { CS_ALIVE = 0, CS_DEAD, CS_LAGGED, CS_EDITING };
 
struct physent
{
    vec o, vel;                         // origin, velocity
    float yaw, pitch, roll;             // used as vec in one place
    float maxspeed;                     // cubes per second, 24 for player
    int timeinair;                      // used for fake gravity
    int gravity;
    float radius, eyeheight, aboveeye;  // bounding box size
    bool inwater;
    bool onfloor, onladder, jumpnext;
    char move, strafe;
    uchar state, type;

    physent() : o(0, 0, 0), yaw(270), pitch(0), roll(0), maxspeed(16), gravity(20),
                radius(1.1f), eyeheight(4.5f), aboveeye(0.7f), 
                state(CS_ALIVE), type(ENT_PLAYER)
    { 
        reset(); 
    };

    void reset()
    {
        vel.x = vel.y = vel.z = 0;
        move = strafe = 0;
        timeinair = 0;
        onfloor = onladder = inwater = jumpnext = false;
    };
};

struct dynent : physent                 // animated ent
{
    bool k_left, k_right, k_up, k_down; // see input code  

    animstate prev[2], current[2];              // md2's need only [0], md3's need both for the lower&upper model
    int lastanimswitchtime[2];
    void *lastmodel[2];

    void stopmoving()
    {
        k_left = k_right = k_up = k_down = jumpnext = false;
        move = strafe = 0;
    };

    void reset()
    {
        physent::reset();
        stopmoving();
    };

    dynent() { reset(); loopi(2) { lastanimswitchtime[i] = -1; lastmodel[i] = NULL; }; };
};

#define MAXNAMELEN 15
#define MAXTEAMLEN 4

struct bounceent;

struct playerent : dynent
{
    float oldpitch;
    int lastupdate, plag, ping;
    int lifesequence;                   // sequence id for each respawn, used in damage test
    int frags, flagscore;
    int health, armour; //armourtype, quadmillis;
    int akimbomillis;
    int gunselect, gunwait;
    int lastaction, lastattackgun, lastmove, lastpain;
    bool attacking;
    int ammo[NUMGUNS];
    int mag[NUMGUNS];
    string name, team;
    //int startheight;
    int shots;                          //keeps track of shots from auto weapons
    bool reloading, hasarmour, weaponchanging;
    int nextweapon; // weapon we switch to
    int primary;                        //primary gun
    int nextprimary; // primary after respawning
    int skin, nextskin; // skin after respawning

    int thrownademillis;
    struct bounceent *inhandnade;
    int akimbo;
    int akimbolastaction[2];

    CPrevLocation PrevLocations; // Previous stored locations of this player

    playerent() : plag(0), ping(0), lifesequence(0), frags(0), flagscore(0), lastpain(0), 
                  shots(0), reloading(false), primary(GUN_ASSAULT), nextprimary(GUN_ASSAULT),
                  skin(0), nextskin(0), inhandnade(NULL)
    {
        name[0] = team[0] = 0;
        respawn();
    };

    void respawn()
    {
        reset();
        health = 100;
        armour = 0;
        hasarmour = false;
        lastaction = akimbolastaction[0] = akimbolastaction[1] = 0;
        akimbomillis = 0;
        gunselect = GUN_PISTOL;
        gunwait = 0;
        attacking = false;
        weaponchanging = false;
        akimbo = 0;
        loopi(NUMGUNS) ammo[i] = mag[i] = 0;
    };
};

struct botent : playerent
{
    // Added by Rick
    CBot *pBot; // Only used if this is a bot, points to the bot class if we are the host,
                // for other clients its NULL
    // End add by Rick      

    playerent *enemy;                      // monster wants to kill this entity
    // Added by Rick: targetpitch
    float targetpitch;                    // monster wants to look in this direction
    // End add   
    float targetyaw;                    // monster wants to look in this direction

    botent() : pBot(NULL), enemy(NULL) { type = ENT_BOT; };
};

// Moved from server.cpp by Rick
struct server_entity            // server side version of "entity" type
{
    bool spawned;
    int spawnsecs;
};
// End move

// EDIT: AH
enum { CTFF_INBASE = 0, CTFF_STOLEN, CTFF_DROPPED };

struct flaginfo
{
    entity *flag;
    playerent *actor;
    vec originalpos;
    int state; // one of the types above
    bool pick_ack;
    flaginfo() : flag(0), actor(0), state(CTFF_INBASE), pick_ack(false) {};
};

enum { NADE_ACTIVATED = 1, NADE_THROWED, GIB};

struct bounceent : physent // nades, gibs
{
    int millis, timetolife, bouncestate; // see enum above
    float rotspeed;
    playerent *owner;

    bounceent() : bouncestate(0), rotspeed(1.0f), owner(NULL)
    { 
        type = ENT_BOUNCE;
        maxspeed = 40;
        radius = 0.2f;
        eyeheight = 0.3f;
        aboveeye = 0.0f;
    };
};

#define NADE_IN_HAND (player1->gunselect==GUN_GRENADE && player1->inhandnade)
#define NADE_THROWING (player1->gunselect==GUN_GRENADE && player1->thrownademillis && !player1->inhandnade)

#define has_akimbo(d) ((d)->gunselect==GUN_PISTOL && (d)->akimbo)

#define SAVEGAMEVERSION 1006               // bump if dynent/netprotocol changes or any other savegame/demo data bumped from 5

//enum { A_BLUE, A_GREEN, A_YELLOW };     // armour types... take 20/40/60 % off

#define MAXCLIENTS 256                  // in a multiplayer game, can be arbitrarily changed
#define DEFAULTCLIENTS 6
#define MAXTRANS 5000                   // max amount of data to swallow in 1 go
#define CUBE_SERVER_PORT 28763
#define CUBE_SERVINFO_PORT 28764
#define PROTOCOL_VERSION 1123            // bump when protocol changes

#define WEAPONCHANGE_TIME 400

// network messages codes, c2s, c2c, s2c
enum
{
    SV_INITS2C = 0, SV_INITC2S, SV_POS, SV_TEXT, SV_SOUND, SV_CDIS,
    SV_GIBDIED, SV_DIED, SV_GIBDAMAGE, SV_DAMAGE, SV_SHOT, SV_FRAGS, SV_RESUME,
    SV_TIMEUP, SV_EDITENT, SV_MAPRELOAD, SV_ITEMACC,
    SV_MAPCHANGE, SV_ITEMSPAWN, SV_ITEMPICKUP, SV_DENIED,
    SV_PING, SV_PONG, SV_CLIENTPING, SV_GAMEMODE,
    SV_EDITH, SV_EDITT, SV_EDITS, SV_EDITD, SV_EDITE,
    SV_SENDMAP, SV_RECVMAP, SV_SERVMSG, SV_ITEMLIST, SV_WEAPCHANGE,
    SV_MODELSKIN,
    SV_FLAGPICKUP, SV_FLAGDROP, SV_FLAGRETURN, SV_FLAGSCORE, SV_FLAGINFO, SV_FLAGS, //EDIT: AH
	SV_GETMASTER, SV_MASTERCMD,
	SV_PWD,
    SV_EXT,
};     

// hardcoded sounds, defined in sounds.cfg
enum
{
    S_JUMP = 0, S_LAND,
    S_KNIFE,
    S_PISTOL, S_RPISTOL,
    S_SHOTGUN, S_RSHOTGUN,
    S_SUBGUN, S_RSUBGUN,
    S_SNIPER, S_RSNIPER, 
    S_ASSAULT, S_RASSAULT,
    S_ITEMAMMO, S_ITEMHEALTH,
    S_ITEMARMOUR, S_ITEMPUP, 
    S_NOAMMO, S_PUPOUT, 
    S_PAIN1, S_PAIN2, S_PAIN3, S_PAIN4, S_PAIN5, S_PAIN6, //24
    S_DIE1, S_DIE2, 
    S_FEXPLODE, 
    S_SPLASH1, S_SPLASH2,
    S_GRUNT1, S_GRUNT2, S_RUMBLE,    
    S_FLAGDROP, S_FLAGPICKUP, S_FLAGRETURN, S_FLAGSCORE,
    S_GRENADEPULL, S_GRENADETHROW,
    S_RAKIMBO, S_GUNCHANGE, S_GIB,
    S_NULL
};

// vertex array format

struct vertex { float u, v, x, y, z; uchar r, g, b, a; }; 

typedef vector<char *> cvector;
typedef vector<int> ivector;

// globals ooh naughty

extern sqr *world, *wmip[];             // map data, the mips are sequential 2D arrays in memory
extern header hdr;                      // current map header
extern int sfactor, ssize;              // ssize = 2^sfactor
extern int cubicsize, mipsize;          // cubicsize = ssize^2
extern physent *camera1;                // camera representing perspective of player, usually player1
extern playerent *player1;              // special client ent that receives input and acts as camera
extern vector<playerent *> players;     // all the other clients (in multiplayer)
extern vector<bounceent *> bounceents;
extern bool editmode;
extern vector<entity> ents;             // map entities
extern vec worldpos, camup, camright;   // current target of the crosshair in the world
extern int lastmillis;                  // last time
extern int curtime;                     // current frame time
extern int gamemode, nextmode;
extern int xtraverts;
extern bool demoplayback;


#define DMF 16.0f 
#define DAF 1.0f 
#define DVF 100.0f

#define VIRTW 2400                      // virtual screen size for text & HUD
#define VIRTH 1800
#define FONTH 64
#define PIXELTAB (VIRTW/12)

enum    // function signatures for script functions, see command.cpp
{
    ARG_1INT, ARG_2INT, ARG_3INT, ARG_4INT,
    ARG_NONE,
    ARG_1STR, ARG_2STR, ARG_3STR, ARG_4STR, ARG_5STR, ARG_6STR,
    ARG_DOWN,
    ARG_1EXP, ARG_2EXP,
    ARG_1EST, ARG_2EST,
    ARG_VARI
}; 

// nasty macros for registering script functions, abuses globals to avoid excessive infrastructure
#define COMMANDN(name, fun, nargs) static bool __dummy_##fun = addcommand(#name, (void (*)())fun, nargs)
#define COMMAND(name, nargs) COMMANDN(name, name, nargs)
#define VARP(name, min, cur, max) int name = variable(#name, min, cur, max, &name, NULL, true)
#define VAR(name, min, cur, max)  int name = variable(#name, min, cur, max, &name, NULL, false)
#define VARF(name, min, cur, max, body)  void var_##name(); static int name = variable(#name, min, cur, max, &name, var_##name, false); void var_##name() { body; }
#define VARFP(name, min, cur, max, body) void var_##name(); static int name = variable(#name, min, cur, max, &name, var_##name, true); void var_##name() { body; }

#define ATOI(s) strtol(s, NULL, 0)		// supports hexadecimal numbers

// Added by Rick
extern ENetHost *clienthost;
inline bool ishost(void) { return !clienthost; };

void splaysound(int n, vec *loc=0);
void addteamscore(playerent *d);
void renderscore(playerent *d);
extern void conoutf(const char *s, ...); // Moved from protos.h
extern void particle_trail(int type, int fade, vec &from, vec &to); // Moved from protos.h
extern bool listenserv;
extern bool intermission;
#include "bot/bot.h"
// End add by Rick

#include "protos.h"				// external function decls

