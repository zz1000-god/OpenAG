#include <time.h>
#include "hud_timer.h"
#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "net_api.h"
#include "demo_api.h"

#define TIMER_Y             0.04f
#define TIMER_Y_NEXT_OFFSET 0.04f
#define TIMER_RED_R         255
#define TIMER_RED_G         16
#define TIMER_RED_B         16
#define CUSTOM_TIMER_R      0
#define CUSTOM_TIMER_G      160
#define CUSTOM_TIMER_B      0
#define MAX_CUSTOM_TIMERS   4

void CustomTimerCommandCallback()
{
    CHudTimer::Get()->CustomTimerCommand();
}

class CHudTimer : public BaseHudClass
{
public:
    static CHudTimer* Get()
    {
        static CHudTimer s_Timer;
        return &s_Timer;
    }

	void CHudTimer::Init()
	{
		BaseHudClass::Init();

		HookMessage<&CHudTimer::MsgFunc_Timer>("Timer");

		m_iFlags |= HUD_ACTIVE;

		m_pCvarHudTimer = gEngfuncs.pfnRegisterVariable("hud_timer", "1", FCVAR_BHL_ARCHIVE);
		m_pCvarHudTimerSync = gEngfuncs.pfnRegisterVariable("hud_timer_sync", "1", FCVAR_BHL_ARCHIVE);
		m_pCvarHudNextmap = gEngfuncs.pfnRegisterVariable("hud_nextmap", "1", FCVAR_BHL_ARCHIVE);

		memset(m_flCustomTimerStart, 0, sizeof(m_flCustomTimerStart));
		memset(m_flCustomTimerEnd, 0, sizeof(m_flCustomTimerEnd));
		memset(m_bCustomTimerNeedSound, 0, sizeof(m_bCustomTimerNeedSound));
		m_szNextmap[0] = 0;

		// Register the command
		gEngfuncs.pfnAddCommand("customtimer", CustomTimerCommandCallback);
	}

    void VidInit()
    {
        m_iFlags |= HUD_ACTIVE;
        m_flEndTime = 0;
        m_flEffectiveTime = 0;
        m_flSynced = false;
        m_flDemoSyncTime = 0;
        m_bDemoSyncTimeValid = false;
        memset(m_flCustomTimerStart, 0, sizeof(m_flCustomTimerStart));
        memset(m_flCustomTimerEnd, 0, sizeof(m_flCustomTimerEnd));
        memset(m_bCustomTimerNeedSound, 0, sizeof(m_bCustomTimerNeedSound));
        m_szNextmap[0] = 0;
    }

    int MsgFunc_Timer(const char* pszName, int iSize, void* pbuf)
    {
        BEGIN_READ(pbuf, iSize);
        m_flEndTime = (float)READ_LONG();
        m_flEffectiveTime = (float)READ_LONG();
        m_flSynced = true;
        m_iFlags |= HUD_ACTIVE;
        return 1;
    }

    void Draw(float fTime)
    {
        char text[128];

        if (gHUD.m_iHideHUDDisplay & HIDEHUD_ALL)
            return;

        // Time source: demo or real time
        float currentTime;
        if (gEngfuncs.pDemoAPI->IsPlayingback())
        {
            if (m_bDemoSyncTimeValid)
                currentTime = m_flDemoSyncTime + fTime;
            else
                currentTime = 0;
        }
        else
        {
            currentTime = fTime;
        }

        // HUD color
        int r, g, b;
        float a = 255 * gHUD.GetHudTransparency();
        gHUD.GetHudColor(HudPart::Common, 0, r, g, b);
        ScaleColors(r, g, b, a);

        // Main timer
        float timeleft = m_flSynced ? (int)(m_flEndTime - currentTime) + 1 : (int)(m_flEndTime - m_flEffectiveTime);
        int hud_timer = (int)m_pCvarHudTimer->value;
        int ypos = ScreenHeight * TIMER_Y;
        switch (hud_timer)
        {
        case 1: // time left
            if (currentTime > 0 && timeleft > 0)
                DrawTimerInternal((int)timeleft, ypos, r, g, b, true);
            break;
        case 2: // time passed
            if (currentTime > 0)
                DrawTimerInternal((int)currentTime, ypos, r, g, b, false);
            break;
        case 3: // local PC time
        {
            time_t rawtime;
            struct tm* timeinfo;
            time(&rawtime);
            timeinfo = localtime(&rawtime);
            sprintf(text, "Clock %d:%02d:%02d", (int)timeinfo->tm_hour, (int)timeinfo->tm_min, (int)timeinfo->tm_sec);
            int width = TextMessageDrawString(ScreenWidth + 1, ypos, text, 0, 0, 0);
            TextMessageDrawString((ScreenWidth - width) / 2, ypos, text, r, g, b);
            break;
        }
        }

        // Nextmap
        int hud_nextmap = (int)m_pCvarHudNextmap->value;
        if (m_szNextmap[0] && timeleft < 60 && timeleft >= 0 && m_flEndTime > 0 && (hud_nextmap == 2 || (hud_nextmap == 1 && timeleft >= 37)))
        {
            snprintf(text, sizeof(text), "Nextmap is %s", m_szNextmap);
            ypos = ScreenHeight * (TIMER_Y + TIMER_Y_NEXT_OFFSET);
            int width = TextMessageDrawString(ScreenWidth + 1, ypos, text, 0, 0, 0);
            float a = (timeleft >= 40 || hud_nextmap > 1 ? 255.0f : 255.0f / 3 * ((m_flEndTime - currentTime) + 1 - 37)) * gHUD.GetHudTransparency();
            gHUD.GetHudColor(HudPart::Common, 0, r, g, b);
            ScaleColors(r, g, b, a);
            TextMessageDrawString((ScreenWidth - width) / 2, ypos, text, r, g, b);
        }

        // Custom timers
        for (int i = 0; i < MAX_CUSTOM_TIMERS; i++)
        {
            if (m_flCustomTimerStart[i] - currentTime > 0.5 || currentTime == 0)
            {
                m_flCustomTimerStart[i] = 0;
                m_flCustomTimerEnd[i] = 0;
                m_bCustomTimerNeedSound[i] = false;
            }
            else if (m_flCustomTimerEnd[i] > currentTime)
            {
                timeleft = (int)(m_flCustomTimerEnd[i] - currentTime) + 1;
                sprintf(text, "Timer %d", (int)timeleft);
                ypos = ScreenHeight * (TIMER_Y + TIMER_Y_NEXT_OFFSET * (i + 2));
                int width = TextMessageDrawString(ScreenWidth + 1, ypos, text, 0, 0, 0);
                float a = 255 * gHUD.GetHudTransparency();
                r = CUSTOM_TIMER_R, g = CUSTOM_TIMER_G, b = CUSTOM_TIMER_B;
                ScaleColors(r, g, b, a);
                TextMessageDrawString((ScreenWidth - width) / 2, ypos, text, r, g, b);
            }
            else if (m_bCustomTimerNeedSound[i])
            {
                if (currentTime - m_flCustomTimerEnd[i] < 1.5)
                    PlaySound("fvox/bell.wav", 1);
                m_flCustomTimerStart[i] = 0;
                m_flCustomTimerEnd[i] = 0;
                m_bCustomTimerNeedSound[i] = false;
            }
        }
    }

    void DrawTimerInternal(int time, float ypos, int r, int g, int b, bool redOnLow)
    {
        div_t q;
        char text[64];

        if (time >= 86400)
        {
            q = div(time, 86400);
            int d = q.quot;
            q = div(q.rem, 3600);
            int h = q.quot;
            q = div(q.rem, 60);
            int m = q.quot;
            int s = q.rem;
            sprintf(text, "%dd %dh %02dm %02ds", d, h, m, s);
        }
        else if (time >= 3600)
        {
            q = div(time, 3600);
            int h = q.quot;
            q = div(q.rem, 60);
            int m = q.quot;
            int s = q.rem;
            sprintf(text, "%dh %02dm %02ds", h, m, s);
        }
        else if (time >= 60)
        {
            q = div(time, 60);
            int m = q.quot;
            int s = q.rem;
            sprintf(text, "%d:%02d", m, s);
        }
        else
        {
            sprintf(text, "%d", (int)time);
            if (redOnLow)
            {
                float a = 255 * gHUD.GetHudTransparency();
                r = TIMER_RED_R, g = TIMER_RED_G, b = TIMER_RED_B;
                ScaleColors(r, g, b, a);
            }
        }

        int width = TextMessageDrawString(ScreenWidth + 1, ypos, text, 0, 0, 0);
        TextMessageDrawString((ScreenWidth - width) / 2, ypos, text, r, g, b);
    }

    void CustomTimerCommand(void)
    {
        if (gEngfuncs.pDemoAPI->IsPlayingback())
            return;

        if (gEngfuncs.Cmd_Argc() <= 1)
        {
            gEngfuncs.Con_Printf("usage:  customtimer <interval in seconds> [timer number 1|2]\n");
            return;
        }

        // Get interval value
        int interval;
        char* intervalString = gEngfuncs.Cmd_Argv(1);
        if (!intervalString || !intervalString[0])
            return;
        interval = atoi(intervalString);
        if (interval < 0)
            return;
        if (interval > 86400)
            interval = 86400;

        // Get timer number
        int number = 0;
        char* numberString = gEngfuncs.Cmd_Argv(2);
        if (numberString && numberString[0])
        {
            number = atoi(numberString) - 1;
            if (number < 0 || number >= MAX_CUSTOM_TIMERS)
                return;
        }

        // Set custom timer
        m_flCustomTimerStart[number] = gEngfuncs.GetClientTime();
        m_flCustomTimerEnd[number] = gEngfuncs.GetClientTime() + interval;
        m_bCustomTimerNeedSound[number] = true;
    }

    // Nextmap support
    void SetNextmap(const char* nextmap)
    {
        strncpy(m_szNextmap, nextmap, sizeof(m_szNextmap) - 1);
        m_szNextmap[sizeof(m_szNextmap) - 1] = 0;
    }

    const char* GetNextmap() const
    {
        return m_szNextmap;
    }

private:
    float m_flEndTime = 0;
    float m_flEffectiveTime = 0;
    bool m_flSynced = false;
    float m_flDemoSyncTime = 0;
    bool m_bDemoSyncTimeValid = false;

    cvar_t* m_pCvarHudTimer = nullptr;
    cvar_t* m_pCvarHudTimerSync = nullptr;
    cvar_t* m_pCvarHudNextmap = nullptr;

    char m_szNextmap[64]{};

    float m_flCustomTimerStart[MAX_CUSTOM_TIMERS]{};
    float m_flCustomTimerEnd[MAX_CUSTOM_TIMERS]{};
    bool m_bCustomTimerNeedSound[MAX_CUSTOM_TIMERS]{};
};

// HUD registration
DEFINE_HUD_ELEM(CHudTimer);
