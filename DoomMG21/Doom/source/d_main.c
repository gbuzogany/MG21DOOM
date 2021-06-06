/*  Doom Port on xMG21/Ikea Tradfri
 *  by Nicola Wrachien (next-hack in the comments)
 *
 *  This port is based on the excellent doomhack's GBA Doom Port.
 *  Several data structures and functions have been optimized to fit the
 *  96kB + 12kB memory of xMG21 devices. Z-Depth Light has been restored with almost
 *  no RAM consumption!
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *  Copyright (C) 2021 Nicola Wrachien (next-hack in the comments)
 *  on xMG21/Ikea Tradfri port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  DOOM main program (D_DoomMain) and game loop (D_DoomLoop),
 *  plus functions to determine game mode (shareware, registered),
 *  parse command line parameters, configure game parameters (turbo),
 *  and call the startup functions.
 *  next-hack: modified setup order to support flash caching.
 *             also removed if-less handling of input events.
 *
 *-----------------------------------------------------------------------------
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "doomdef.h"
#include "doomtype.h"
#include "doomstat.h"
#include "d_net.h"
#include "dstrings.h"
#include "sounds.h"
#include "z_zone.h"
#include "w_wad.h"
#include "s_sound.h"
#include "v_video.h"
#include "f_finale.h"
#include "f_wipe.h"
#include "m_misc.h"
#include "m_menu.h"
#include "i_main.h"
#include "i_system.h"
#include "i_sound.h"
#include "i_video.h"
#include "g_game.h"
#include "hu_stuff.h"
#include "wi_stuff.h"
#include "st_stuff.h"
#include "am_map.h"
#include "p_setup.h"
#include "r_draw.h"
#include "r_main.h"
#include "d_main.h"
#include "lprintf.h"  // jff 08/03/98 - declaration of lprintf
#include "am_map.h"
#include "m_cheat.h"

#include "doom_iwad.h"
#include "global_data.h"
#include "i_spi_support.h"
#include "pwm_audio.h"
#include "main.h"

extern byte displayBuffer[];

void GetFirstMap(int *ep, int *map); // Ty 08/29/98 - add "-warp x" functionality
static void D_PageDrawer(void);
static void D_UpdateFPS(void);

// CPhipps - removed wadfiles[] stuff

//jff 1/22/98 parms for disabling music and sound
const boolean nosfxparm = false;
const boolean nomusicparm = true;

const skill_t startskill = sk_medium;
const int startepisode = 1;
const int startmap = 1;

const boolean nodrawers = false;

static const char *timedemo = NULL; //"demo1";

/*
 * D_PostEvent - Event handling
 *
 * Called by I/O functions when an event is received.
 * Try event handlers for each code area in turn.
 * cph - in the true spirit of the Boom source, let the 
 *  short ciruit operator madness begin!
 *
 *  next-hack: nah, the original code was very unreadable, and threw
 *  a warning too, as the result of the statement was not read. Better to
 *  get closer to 2021 standards.
 *
 */

void D_PostEvent(event_t *ev)
{
    /* cph - suppress all input events at game start
     * FIXME: This is a lousy kludge */
    if (_g->gametic < 3)
        return;
    // next-hack: I understand you love short-circuits, but the original code
    // was too much over the line.
    if (!M_Responder(ev))
    {
        if (!(_g->gamestate == GS_LEVEL && (C_Responder(ev) || HU_Responder(ev) || ST_Responder(ev) || AM_Responder(ev))))
        {
            G_Responder(ev);
        }
    }
}

//
// D_Wipe
//
// CPhipps - moved the screen wipe code from D_Display to here
// The screens to wipe between are already stored, this just does the timing
// and screen updating

static void D_Wipe(void)
{
    boolean done;
    int wipestart = I_GetTime() - 1;

    do
    {
        int nowtime, tics;
        do
        {
            nowtime = I_GetTime();
            tics = nowtime - wipestart;
        } while (!tics);

        wipestart = nowtime;
        done = wipe_ScreenWipe(tics);

        M_Drawer();                   // menu is drawn even on top of wipes

    } while (!done);
}

//
// D_Display
//  draw current display, possibly wiping it from the previous
//

static void D_Display(void)
{

    boolean wipe;
    boolean viewactive = false;

    if (nodrawers)                    // for comparative timing / profiling
        return;

    if (!I_StartDisplay())
        return;

    // save the current screen if about to wipe
    if ((wipe = (_g->gamestate != _g->wipegamestate)))
        wipe_StartScreen();
    const int step = 96;
    for (int starty = 0; starty < 128; starty += step)
    {
        draw_starty = starty;
        _g->screens[0].data = displayBuffer - starty * SCREENWIDTH_PHYSICAL;
        drawvars.byte_topleft = _g->screens[0].data;
        draw_stopy = starty + step - 1;
        if (draw_stopy > 127)
            draw_stopy = 127;
        if (_g->gamestate != GS_LEVEL)
        { // Not a level
            if (_g->oldgamestate == GS_LEVEL)
            {
                V_SetPalette(0); // cph - use default (basic) palette
            }

            switch (_g->gamestate)
            {
                case GS_INTERMISSION:
                    WI_Drawer();
                    break;
                case GS_FINALE:
                    F_Drawer();
                    break;
                case GS_DEMOSCREEN:
                    D_PageDrawer();
                    break;
                default:
                    break;
            }
        }
        else if (_g->gametic != _g->basetic)
        { // In a level
            if (starty < 32)
                HU_Erase();

            // Work out if the player view is visible, and if there is a border
            // TODO: BUG if viewactive is false, then automap will lock the system.
            // It seems to me that R_RenderBSPNode is required before.
            viewactive = (!(_g->automapmode & am_active) || (_g->automapmode & am_overlay));
            // Now do the drawing
            if (viewactive && starty < 96)
                R_RenderPlayerView(&_g->player);

            if ((_g->automapmode & am_active) && starty < 96)
                AM_Drawer();

            if (draw_stopy >= 96)
                ST_Drawer(true, true);	// note: force refresh.

            if (starty < 32)
                HU_Drawer();
        }

        _g->oldgamestate = _g->wipegamestate = _g->gamestate;

        // draw pause pic
        if (_g->paused)
        {
            // Simplified the "logic" here and no need for x-coord caching - POPE
            V_DrawNamePatch((320 - V_NamePatchWidth("M_PAUSE"))/2, 4, 0, "M_PAUSE", CR_DEFAULT, VPT_STRETCH);
        }

        // menus go directly to the screen
        if (starty < 96)
            M_Drawer();          // menu is drawn even on top of everything
        // normal update
        I_FinishUpdateBlock();              // page flip or blit buffer
    }
    if (wipe)
    {
        // wipe update
        wipe_EndScreen();
        D_Wipe();
    }
    //
    D_BuildNewTiccmds();
    //I_EndDisplay(); // removed
}

//
//  D_DoomLoop()
//
// Not a globally visible function,
//  just included for source reference,
//  called by D_DoomMain, never exits.
// Manages timing and IO,
//  calls all ?_Responder, ?_Ticker, and ?_Drawer,
//  calls I_GetTime, I_StartFrame, and I_StartTic
//
int divisions;
extern int lumpByNameRequest;
static void D_DoomLoop(void)
{
    int oldMaxDivisions = 0;
    int oldLumpByNameRequests = 0;
#if DEBUG_SETUP
    // this to launch directly a new game without having to press keys...
    // useful if you are in a hurry and want to test if new code is faster or slower.
    bool startedGame = false;
#endif
    // set default behavior of ammo widget.
    _g->fps_show = SHOW_FPS;
    // allocate sound buffer.
    if (!nosfxparm)
    {
        // next-hack: added support for PWM audio. Buffer is created dynamically
        // TODO: if soundfx volume is 0, then free buffer (less chances of having
        // Z_Malloc failed on complex levels). If soundfx is set back to non-zero
        // value, then try to allocate it.
        audioBuffer = Z_Malloc(AUDIO_BUFFER_LENGTH * sizeof(*audioBuffer), PU_STATIC, NULL);
        //
        initPwmAudio();
    }
    //
    for (;;)
    {
        if (!nosfxparm)
            updateSound();
        // frame syncronous IO operations
        divisions = 0;
        lumpByNameRequest = 0;

        // process one or more tics
        if (_g->singletics)        // this is normally not executed
        {
            I_StartTic();
            G_BuildTiccmd(&_g->netcmd);

            if (_g->advancedemo)
                D_DoAdvanceDemo();

            M_Ticker();

            G_Ticker();

            _g->gametic++;
            _g->maketic++;
        }
        else
        {
            TryRunTics(); // will run at least one tic
        }
        // killough 3/16/98: change consoleplayer to displayplayer
        if (_g->player.mo) // cph 2002/08/10
            S_UpdateSounds(_g->player.mo); // move positional sounds

        // Update display, next frame, with current state.
        D_Display();
        if (oldMaxDivisions < divisions)
        {
            oldMaxDivisions = divisions;
            printf("Max div/frame %d\r\n", oldMaxDivisions);

        }
        if (oldLumpByNameRequests < lumpByNameRequest)
        {
            oldLumpByNameRequests = lumpByNameRequest;
            printf("Max lumpByNameRequest per frame increased to %d\r\n", lumpByNameRequest);

        }
        if (_g->fps_show)
        {
            D_UpdateFPS();
        }
#if DEBUG_SETUP
        if (!startedGame)
        {
            startedGame = true;
            G_DeferedInitNew(1, 1, 6);
        }
#endif
    }
}

static void D_UpdateFPS()
{
    _g->fps_frames++;

    unsigned int timenow = I_GetTimeDeciMicrosecs(); // this is running at 10MHz.
    unsigned int elapsed = timenow - _g->fps_timebefore;

    if (elapsed >= 10000000UL)
    {
        _g->fps_framerate = (int) (_g->fps_frames * 100000000 / ((float) elapsed));
        _g->fps_frames = 0;
        _g->fps_timebefore = timenow;
    }
}

//
//  DEMO LOOP
//

//
// D_PageTicker
// Handles timing for warped projection
//
void D_PageTicker(void)
{
    if (--_g->pagetic < 0)
        D_AdvanceDemo();
}

//
// D_PageDrawer
//
static void D_PageDrawer(void)
{
    // proff/nicolas 09/14/98 -- now stretchs bitmaps to fullscreen!
    // CPhipps - updated for new patch drawing
    // proff - added M_DrawCredits
    if (_g->pagelump)
    {

        V_DrawNumPatch(0, 0, 0, _g->pagelump, CR_DEFAULT, VPT_STRETCH);
    }

}

//
// D_AdvanceDemo
// Called after each demo or intro demosequence finishes
//
void D_AdvanceDemo(void)
{
    _g->advancedemo = true;
}

/* killough 11/98: functions to perform demo sequences
 * cphipps 10/99: constness fixes
 */

static void D_SetPageName(const char *name)
{
    _g->pagelump = W_GetNumForName(name);
}

static void D_DrawTitle1(const char *name)
{
    S_StartMusic(mus_intro);
    _g->pagetic = (TICRATE * 30);
    D_SetPageName(name);
}

static void D_DrawTitle2(const char *name)
{
    S_StartMusic(mus_dm2ttl);
    D_SetPageName(name);
}

/* killough 11/98: tabulate demo sequences
 */

static struct
{
    void (*func)(const char*);
    const char *name;
}

const demostates[][4] =
{
#if 1
    {
        { D_DrawTitle1, "TITLEPIC" },
        { D_DrawTitle1, "TITLEPIC" },
        { D_DrawTitle2, "TITLEPIC" },
        { D_DrawTitle1, "TITLEPIC" },
    },

#else
    {
        { D_DrawTitle1, "TITLEPIC" },
        { D_DrawTitle1, "TITLEPIC" },
        { D_DrawTitle2, "TITLEPIC" },
        { D_DrawTitle1, "TITLEPIC" },
    },

    {
        { G_DeferedPlayDemo, "demo1" },
        { G_DeferedPlayDemo, "demo1" },
        { G_DeferedPlayDemo, "demo1" },
        { G_DeferedPlayDemo, "demo1" },
    },
    {
        { D_SetPageName, "TITLEPIC" },
        { D_SetPageName, "TITLEPIC" },
        { D_SetPageName, "TITLEPIC" },
        { D_SetPageName, "TITLEPIC" },
    },

    {
        { G_DeferedPlayDemo, "demo2" },
        { G_DeferedPlayDemo, "demo2" },
        { G_DeferedPlayDemo, "demo2" },
        { G_DeferedPlayDemo, "demo2" },
    },

    {
        { D_SetPageName, "TITLEPIC" },
        { D_SetPageName, "TITLEPIC" },
        { D_SetPageName, "TITLEPIC" },
        { D_SetPageName, "TITLEPIC" },
    },

    {
        { G_DeferedPlayDemo, "demo3" },
        { G_DeferedPlayDemo, "demo3" },
        { G_DeferedPlayDemo, "demo3" },
        { G_DeferedPlayDemo, "demo3" },
    },
#endif
    {
        { NULL, NULL },
        { NULL, NULL },
        { NULL, NULL },
        { NULL, NULL },
    }
};

/*
 * This cycles through the demo sequences.
 * killough 11/98: made table-driven
 */

void D_DoAdvanceDemo(void)
{
    _g->player.playerstate = PST_LIVE; /* not reborn */
    _g->advancedemo = _g->usergame = _g->paused = false;
    _g->gameaction = ga_nothing;

    _g->pagetic = TICRATE * 11; /* killough 11/98: default behavior */
    _g->gamestate = GS_DEMOSCREEN;

    if (!demostates[++_g->demosequence][_g->gamemode].func)
        _g->demosequence = 0;

    demostates[_g->demosequence][_g->gamemode].func(demostates[_g->demosequence][_g->gamemode].name);
}

//
// D_StartTitle
//
void D_StartTitle(void)
{
    _g->gameaction = ga_nothing;
    _g->demosequence = -1;
    D_AdvanceDemo();
}

//
// D_AddFile
//
// Rewritten by Lee Killough
//
// Ty 08/29/98 - add source parm to indicate where this came from
// CPhipps - static, const char* parameter
//         - source is an enum
//         - modified to allocate & use new wadfiles array
void D_AddFile()
{

}

//
// CheckIWAD
//
// Verify a file is indeed tagged as an IWAD
// Scan its lumps for levelnames and return gamemode as indicated
// Detect missing wolf levels in DOOM II
//
// The filename to check is passed in iwadname, the gamemode detected is
// returned in gmode, hassec returns the presence of secret levels
//
// jff 4/19/98 Add routine to test IWAD for validity and determine
// the gamemode from it. Also note if DOOM II, whether secret levels exist
// CPhipps - const char* for iwadname, made static

static void CheckIWAD2(const unsigned char *iwad_data, const unsigned int iwad_len, GameMode_t *gmode, boolean *hassec)
{
    spiFlashSetAddress((uint32_t) iwad_data);
    wadinfo_t header_v;
    spiFlashGetData(&header_v, sizeof(header_v));
    const wadinfo_t *header = &header_v; //(const wadinfo_t*)iwad_data;

    int ud = 0, rg = 0, sw = 0, cm = 0, sc = 0;

    if (!strncmp(header->identification, "IWAD", 4))
    {
        size_t length = header->numlumps;

        filelump_t *fileinfo = Z_Malloc(length * sizeof(*fileinfo), PU_STATIC, NULL);
        spiFlashSetAddress((uint32_t) &iwad_data[header->infotableofs]);
        spiFlashGetData(fileinfo, length * sizeof(*fileinfo));
        while (length--)
        {
            if (fileinfo[length].name[0] == 'E' && fileinfo[length].name[2] == 'M' && fileinfo[length].name[4] == 0)
            {
                if (fileinfo[length].name[1] == '4')
                    ++ud;
                else if (fileinfo[length].name[1] == '3')
                    ++rg;
                else if (fileinfo[length].name[1] == '2')
                    ++rg;
                else if (fileinfo[length].name[1] == '1')
                    ++sw;
            }
            else if (fileinfo[length].name[0] == 'M' && fileinfo[length].name[1] == 'A' && fileinfo[length].name[2] == 'P' && fileinfo[length].name[5] == 0)
            {
                ++cm;
                if (fileinfo[length].name[3] == '3')
                {
                    if (fileinfo[length].name[4] == '1' || fileinfo[length].name[4] == '2')
                        ++sc;
                }
            }
            //Final Doom IWAD check hacks ~Kippykip
            //TNT - MURAL1
            else if (fileinfo[length].name[0] == 'M' && fileinfo[length].name[1] == 'U' && fileinfo[length].name[2] == 'R' && fileinfo[length].name[3] == 'A' && fileinfo[length].name[4] == 'L' && fileinfo[length].name[5] == '1' && fileinfo[length].name[6] == 0)
            {
                cm = 32;
                sc = 2;
                *gmode = commercial;
                _g->gamemission = pack_tnt;
                _g->gamemode = commercial;
                Z_Free(fileinfo);
                return;
            }
            //Plutonia - WFALL1
            else if (fileinfo[length].name[0] == 'W' && fileinfo[length].name[1] == 'F' && fileinfo[length].name[2] == 'A' && fileinfo[length].name[3] == 'L' && fileinfo[length].name[4] == 'L' && fileinfo[length].name[5] == '1' && fileinfo[length].name[6] == 0)
            {
                cm = 32;
                sc = 2;
                *gmode = commercial;
                _g->gamemission = pack_plut;
                _g->gamemode = commercial;
                Z_Free(fileinfo);
                return;
            }
        }
        Z_Free(fileinfo);

    }
    else
    {
        I_Error("CheckIWAD: IWAD tag not present");
    }
    // Determine game mode from levels present
    // Must be a full set for whichever mode is present
    // Lack of wolf-3d levels also detected here

    *gmode = indetermined;
    *hassec = false;
    if (cm >= 30)
    {
        *gmode = commercial;
        *hassec = sc >= 2;
    }
    else if (ud >= 9)
        *gmode = retail;
    else if (rg >= 18)
        *gmode = registered;
    else if (sw >= 9)
        *gmode = shareware;

}

//
// IdentifyVersion
//
// Set the location of the defaults file and the savegame root
// Locate and validate an IWAD file
// Determine gamemode from the IWAD
//
// supports IWADs with custom names. Also allows the -iwad parameter to
// specify which iwad is being searched for if several exist in one dir.
// The -iwad parm may specify:
//
// 1) a specific pathname, which must exist (.wad optional)
// 2) or a directory, which must contain a standard IWAD,
// 3) or a filename, which must be found in one of the standard places:
//   a) current dir,
//   b) exe dir
//   c) $DOOMWADDIR
//   d) or $HOME
//
// jff 4/19/98 rewritten to use a more advanced search algorithm

static void IdentifyVersion()
{
    if (doom_iwad && (p_doom_iwad_len > 0))
    {
        uint32_t doom_iwad_len;
        spiFlashSetAddress((uint32_t) p_doom_iwad_len);
        spiFlashGetData(&doom_iwad_len, sizeof(doom_iwad_len));
        CheckIWAD2(doom_iwad, doom_iwad_len, &_g->gamemode, &_g->haswolflevels);

        /* jff 8/23/98 set gamemission global appropriately in all cases
         * cphipps 12/1999 - no version output here, leave that to the caller
         */
        switch (_g->gamemode)
        {
            case retail:
            case registered:
            case shareware:
                _g->gamemission = doom;
                break;
            case commercial:
                _g->gamemission = doom2;
                break;

            default:
                _g->gamemission = none;
                break;
        }

        if (_g->gamemode == indetermined)
        {
            //jff 9/3/98 use logical output routine
            lprintf(LO_WARN, "Unknown Game Version, may not work\n");
        }

        D_AddFile();
    }
}

//
// D_DoomMainSetup
//
// CPhipps - the old contents of D_DoomMain, but moved out of the main
//  line of execution so its stack space can be freed

static void D_DoomMainSetup(void)
{
    IdentifyVersion();

    // jff 1/24/98 end of set to both working and command line value

    {
        // CPhipps - localise title variable
        // print title for every printed line
        // cph - code cleaned and made smaller
        const char *doomverstr;

        switch (_g->gamemode)
        {
            case retail:
                doomverstr = "The Ultimate DOOM";
                break;
            case shareware:
                doomverstr = "DOOM Shareware";
                break;
            case registered:
                doomverstr = "DOOM Registered";
                break;
            case commercial:  // Ty 08/27/98 - fixed gamemode vs gamemission
                switch (_g->gamemission)
                {
                    case pack_plut:
                        doomverstr = "DOOM 2: Plutonia Experiment";
                        break;
                    case pack_tnt:
                        doomverstr = "DOOM 2: TNT - Evilution";
                        break;
                    default:
                        doomverstr = "DOOM 2: Hell on Earth";
                        break;
                }
                break;
            default:
                doomverstr = "Public DOOM";
                break;
        }

        /* cphipps - the main display. This shows the build date, copyright, and game type */

        lprintf(LO_ALWAYS, "\r\nPrBoom (built %s)", version_date);
        lprintf(LO_ALWAYS, "\r\nPlaying: %s", doomverstr);
        lprintf(LO_ALWAYS, "\r\nPrBoom is released under the");
        lprintf(LO_ALWAYS, "\r\nGNU GPL v2.0.");

        lprintf(LO_ALWAYS, "\r\nYou are welcome to");
        lprintf(LO_ALWAYS, "\r\nredistribute it under");
        lprintf(LO_ALWAYS, "\r\ncertain conditions.");

        lprintf(LO_ALWAYS, "\r\nIt comes with ABSOLUTELY\r\nNO WARRANTY.\r\nSee the file COPYING for\r\ndetails.");

        lprintf(LO_ALWAYS, "\r\nPhew. Thats the nasty legal\r\nstuff out of the way.\r\nLet's play Doom!\n");

    }

    // init subsystems

    G_ReloadDefaults();    // killough 3/4/98: set defaults just loaded.
    // jff 3/24/98 this sets startskill if it was -1
    // CPhipps - move up netgame init
    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "D_InitNetGame.\r\n");
    D_InitNetGame();

    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "W_Init: Init WADfiles.\r\n");
    W_Init(); // CPhipps - handling of wadfiles init changed

    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "R_Init: DOOM refresh daemon.\r\n");
    R_Init();       // here initImmutableFlashRegion() called too

    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "M_Init: Init misc info.\r\n");
    M_Init();

    // 2021/05/19 next-hack: added WiInit to use lump nums instead of names
    lprintf(LO_INFO, "Wi_Init: Init misc info.\r\n");
    WI_Init();

    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "P_Init: Init Playloop state.\r\n");
    P_Init();

    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "I_Init: Set up machine state.\r\n");
    I_Init();

    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "S_Init: Setting up sound.\r\n");
    S_Init(_g->snd_SfxVolume /* *8 */, _g->snd_MusicVolume /* *8*/);

    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "HU_Init: Setting up HUD.\r\n");
    HU_Init();
    //jff 9/3/98 use logical output routine
    lprintf(LO_INFO, "ST_Init: Init status bar.\r\n");
    ST_Init();
    lprintf(LO_INFO, "G_LoadSettings: Loading settings.\r\n");
    G_LoadSettings();
    _g->idmusnum = -1; //jff 3/17/98 insure idmus number is blank

    _g->fps_show = false;

    if (timedemo)
    {
        _g->singletics = true;
        _g->timingdemo = true;            // show stats after quit
        G_DeferedPlayDemo(timedemo);
        _g->singledemo = true;            // quit after one demo
    }
    else
    {
        D_StartTitle();                 // start up intro loop
    }

    //
    p_wad_immutable_flash_data = storeFlashImmutableValues();

}

//
// D_DoomMain
//

void D_DoomMain(void)
{
    // debug;

    D_DoomMainSetup(); // CPhipps - setup out of main execution stack

    D_DoomLoop();  // never returns
}

//
// GetFirstMap
//
// Ty 08/29/98 - determine first available map from the loaded wads and run it
//

void GetFirstMap(int *ep, int *map)
{
    int i, j; // used to generate map name
    boolean done = false;  // Ty 09/13/98 - to exit inner loops
    char test[6];  // MAPxx or ExMx plus terminator for testing
    char name[6];  // MAPxx or ExMx plus terminator for display
    boolean newlevel = false;  // Ty 10/04/98 - to test for new level
    int ix;  // index for lookup

    strcpy(name, ""); // initialize
    if (*map == 0) // unknown so go search for first changed one
    {
        *ep = 1;
        *map = 1; // default E1M1 or MAP01
        if (_g->gamemode == commercial)
        {
            for (i = 1; !done && i < 33; i++)  // Ty 09/13/98 - add use of !done
            {
                sprintf(test, "MAP%02d", i);
                ix = W_CheckNumForName(test);
                if (ix != -1)  // Ty 10/04/98 avoid -1 subscript
                {
                    if (!*name)  // found one, not pwad.  First default.
                        strcpy(name, test);
                }
            }
        }
        else // one of the others
        {
            strcpy(name, "E1M1");  // Ty 10/04/98 - default for display
            for (i = 1; !done && i < 5; i++)  // Ty 09/13/98 - add use of !done
            {
                for (j = 1; !done && j < 10; j++) // Ty 09/13/98 - add use of !done
                {
                    sprintf(test, "E%dM%d", i, j);
                    ix = W_CheckNumForName(test);
                    if (ix != -1)  // Ty 10/04/98 avoid -1 subscript
                    {

                        if (!*name)  // found one, not pwad.  First default.
                            strcpy(name, test);
                    }
                }
            }
        }
        //jff 9/3/98 use logical output routine
        lprintf(LO_CONFIRM, "Auto-warping to first %slevel: %s\n",
                newlevel ? "new " : "", name);  // Ty 10/04/98 - new level test
    }
}
