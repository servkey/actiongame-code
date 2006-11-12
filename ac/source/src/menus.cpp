// menus.cpp: ingame menu system (also used for scores and serverlist)

#include "cube.h"

#define FIRSTMENU 120

struct mitem { char *text, *action, *hoveraction; };

struct gmenu
{
    char *name;
    vector<mitem> items;
    int mwidth;
    int menusel;
    char *mdl; // (optional) md2 mdl
    int anim, rotspeed, scale;
};

vector<gmenu> menus;

int vmenu = -1;

ivector menustack;

void menuset(int menu) // EDIT: AH
{
    if((vmenu = menu)>=1 && menu != 2) player1->stopmoving();
    if(vmenu==1) menus[1].menusel = 0;
};

void showmenu(char *name)
{
    loopv(menus) if(i>1 && strcmp(menus[i].name, name)==0)
    {
        menuset(i);
        return;
    };
};

int menucompare(mitem *a, mitem *b)
{
    int x = atoi(a->text);
    int y = atoi(b->text);
    if(x>y) return -1;
    if(x<y) return 1;
    return 0;
};

void sortmenu(int m, int start, int num)
{
    qsort(&menus[m].items[start], num, sizeof(mitem), (int (__cdecl *)(const void *,const void *))menucompare);
};

void refreshservers();

bool rendermenu()
{
    if(vmenu<0) { menustack.setsize(0); return false; };
    
    setscope(false);
    
    if(vmenu==1) refreshservers();
    gmenu &m = menus[vmenu];
    s_sprintfd(title)(vmenu>0 && vmenu!=2 ? "[ %s menu ]" : "%s", m.name); // EDIT: AH
    int mdisp = m.items.length();
    int w = 0;
    loopi(mdisp)
    {
        int x = text_width(m.items[i].text);
        if(x>w) w = x;
    };
    int tw = text_width(title);
    if(tw>w) w = tw;
    int step = FONTH/4*5;
    int h = (mdisp+2)*step;
    int y = (VIRTH-h)/2;
    int x = (VIRTW-w)/2;
    static Texture *menutex = NULL;
    if(!menutex) menutex = textureload("packages/textures/makke/menu.jpg", false);
    blendbox(x-FONTH/2*3, y-FONTH, x+w+FONTH/2*3, y+h+FONTH, true, menutex->id);
    draw_text(title, x, y);
    y += FONTH*2;
    if(vmenu && vmenu!=2)
    {
        int bh = y+m.menusel*step;
        blendbox(x-FONTH, bh-10, x+w+FONTH, bh+FONTH+10, false);
    };
    loopj(mdisp)
    {
        draw_text(m.items[j].text, x, y);
        y += step;
    };
    return true;
};

void rendermenumdl()
{
    if(vmenu<0) { menustack.setsize(0); return; };
    if(vmenu==1) refreshservers();
    gmenu &m = menus[vmenu];
    if(!m.mdl) return;
   
    glPushMatrix ();
   
    float t = (lastmillis-player1->lastaction)/1000.0f;
    if(t >= 1.6f) t = 1.6f;

    glTranslatef(camera1->o.x, camera1->o.z, camera1->o.y);
    glRotatef(camera1->yaw+90+180, 0, -1, 0);
	glRotatef(camera1->pitch, 0, 0, 1);
   
	bool isplayermodel = !strncmp(m.mdl, "playermodels", strlen("playermodels"));

    vec pos;
    if(isplayermodel) pos = vec(2.0f, 1.2f, -0.4f);
    else pos = vec(2.0f, 0, 1.7f);

    float yaw = 1.0f;
    if(m.rotspeed) yaw += lastmillis/5.0f/100.0f*m.rotspeed;

	rendermodel(m.mdl, m.anim, 0, 0, pos.x, pos.z, pos.y, yaw, 0, 100, 0, NULL, isplayermodel ? (char*)"weapons/subgun/world" : NULL, m.scale ? m.scale/25.0f : 1.0f);
	
    glPopMatrix();
}

void newmenu(char *name)
{
    gmenu &menu = menus.add();
    menu.name = newstring(name);
    menu.menusel = 0;
    menu.mdl = NULL;
};

void menumanual(int m, int n, char *text, char *action)
{
    if(!n) menus[m].items.setsize(0);
    mitem &mitem = menus[m].items.add();
    mitem.text = text;
	mitem.action = action;
	mitem.hoveraction = NULL;
}

void purgemenu(int m)
{
	menus[m].items.setsize(0);
}

void menuitem(char *text, char *action, char *hoveraction)
{
    gmenu &menu = menus.last();
    mitem &mi = menu.items.add();
    mi.text = newstring(text);
    mi.action = action[0] ? newstring(action) : mi.text;
	mi.hoveraction = hoveraction[0] ? newstring(hoveraction) : NULL;
};

void menumdl(char *mdl, char *anim, char *rotspeed, char *scale)
{
    if(!mdl || !anim) return;
    gmenu &menu = menus.last();
    menu.mdl = newstringbuf(mdl);
    menu.anim = findanim(anim)|ANIM_LOOP;
    menu.rotspeed = max(0, min(atoi(rotspeed), 100));
    menu.scale = max(0, min(atoi(scale), 100));
};

void chmenumdl(char *menu, char *mdl, char *anim, char *rotspeed, char *scale)
{
    if(!menu || !mdl) return;
    loopv(menus)
    {
        gmenu &m = menus[i];
        if(strcmp(m.name, menu) == 0)
        {
            if(m.mdl) s_strcpy(m.mdl, mdl);
            else m.mdl = newstringbuf(mdl);
            m.anim = findanim(anim)|ANIM_LOOP;
            m.rotspeed = max(0, min(atoi(rotspeed), 100));
            m.scale = max(0, min(atoi(scale), 100));
            return;
        };
    };
};
    

COMMAND(menuitem, ARG_3STR);
COMMAND(showmenu, ARG_1STR);
COMMAND(newmenu, ARG_1STR);
COMMAND(menumdl, ARG_5STR);
COMMAND(chmenumdl, ARG_6STR);

bool menukey(int code, bool isdown)
{   
    if(vmenu<=0 || vmenu == 2) return false; // EDIT: AH
    int menusel = menus[vmenu].menusel;
    
    if(isdown)
    {
		setscope(false);
        int oldmenusel = menusel;
        if(code==SDLK_ESCAPE)
        {
            menuset(-1);
            if(!menustack.empty()) menuset(menustack.pop());
            return true;
        }
        else if(code==SDLK_UP || code==-4) menusel--;
        else if(code==SDLK_DOWN || code==-5) menusel++;
        int n = menus[vmenu].items.length();
		if(menusel<0) menusel = n>0 ? n-1 : 0;
        else if(menusel>=n) menusel = 0;
        menus[vmenu].menusel = menusel;
		char *haction = menus[vmenu].items[menusel].hoveraction;
		if(menusel != oldmenusel && haction) execute(haction);
    }
    else
    {
        if(code==SDLK_RETURN || code==-2)
        {
			if(menusel<0 || menusel >= menus[vmenu].items.length()) { menuset(-1); return true; };
            char *action = menus[vmenu].items[menusel].action;
            if(vmenu==1) connects(getservername(menusel));
			else if(vmenu==3 || vmenu==4)
			{
				int cn = (int)(size_t)action;
				mastercommand(vmenu==3 ? MCMD_KICK : MCMD_BAN, cn);
				purgemenu(vmenu);
				menuset(-1);
				return true;
			}
            menustack.add(vmenu);
            menuset(-1);
            if(action) execute(action);
        };
    };
    return true;
};
