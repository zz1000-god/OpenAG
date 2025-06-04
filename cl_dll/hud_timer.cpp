#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "hud_timer.h"
#include "discord_integration.h"
#include <ctime>

DECLARE_MESSAGE(m_Timer, Timer);

static void unpack_seconds(int seconds_total, int& days, int& hours, int& minutes, int& seconds)
{
	constexpr int SECONDS_PER_MINUTE = 60;
	constexpr int SECONDS_PER_HOUR = SECONDS_PER_MINUTE * 60;
	constexpr int SECONDS_PER_DAY = SECONDS_PER_HOUR * 24;

	days = seconds_total / SECONDS_PER_DAY;
	seconds_total %= SECONDS_PER_DAY;

	hours = seconds_total / SECONDS_PER_HOUR;
	seconds_total %= SECONDS_PER_HOUR;

	minutes = seconds_total / SECONDS_PER_MINUTE;
	seconds_total %= SECONDS_PER_MINUTE;

	seconds = seconds_total;
}

int CHudTimer::Init()
{
	HOOK_MESSAGE(Timer);
	m_iFlags |= HUD_ACTIVE;
	
	hud_timer = CVAR_CREATE("hud_timer", "1", FCVAR_ARCHIVE);
	m_pCvarHudTimerSync = CVAR_CREATE("hud_timer_sync", "1", FCVAR_ARCHIVE);
	gHUD.AddHudElem(this);
	
	// Initialize member variables
	seconds_total = 0;
	seconds_passed = 0;
	draw_until = 0.0f;
	
	// Initialize sync variables
	m_flEndTime = 0.0f;
	m_flEffectiveTime = 0.0f;
	m_flNextSyncTime = 0.0f;
	m_flSynced = false;
	m_bDelayTimeleftReading = true;
	
	return 1;
}

int CHudTimer::VidInit()
{
	// Keep HUD active for timer display
	m_iFlags |= HUD_ACTIVE;
	
	// Get pointers to server cvars
	m_pCvarMpTimelimit = gEngfuncs.pfnGetCvarPointer("mp_timelimit");
	m_pCvarMpTimeleft = gEngfuncs.pfnGetCvarPointer("mp_timeleft");
	
	// Reset sync state
	m_flEndTime = 0.0f;
	m_flEffectiveTime = 0.0f;
	m_flNextSyncTime = 0.0f;
	m_flSynced = false;
	m_bDelayTimeleftReading = true;
	
	return 1;
}

void CHudTimer::Draw(float fTime)
{
    if (gHUD.m_iHideHUDDisplay & HIDEHUD_ALL)
        return;

    float currentTime = fTime;
    float timeleft = m_flSynced ? (m_flEndTime - currentTime) : (m_flEndTime - m_flEffectiveTime);
    int ypos = ScreenHeight * TIMER_Y;
    int hud_timer = (int)m_pCvarHudTimer->value;

    int r, g, b;
    float a = 255 * gHUD.GetHudTransparency();
    gHUD.GetHudColor(HudPart::Common, 0, r, g, b);
    ScaleColors(r, g, b, a);

    Con_Printf("HUD Timer Debug: Mode %d, Synced %d, EndTime %.2f, TimeLeft %.2f\n", hud_timer, (int)m_flSynced, m_flEndTime, timeleft);

    switch (hud_timer)
    {
    case 1: // Відображає залишковий час до кінця раунду
        if (currentTime > 0 && timeleft > 0)
        {
            div_t q;
            char text[64];

            if (timeleft >= 86400) // Дні
            {
                q = div((int)timeleft, 86400);
                int d = q.quot;
                q = div(q.rem, 3600);
                int h = q.quot;
                q = div(q.rem, 60);
                int m = q.quot;
                int s = q.rem;
                sprintf(text, "%dd %dh %02dm %02ds", d, h, m, s);
            }
            else if (timeleft >= 3600) // Години
            {
                q = div((int)timeleft, 3600);
                int h = q.quot;
                q = div(q.rem, 60);
                int m = q.quot;
                int s = q.rem;
                sprintf(text, "%dh %02dm %02ds", h, m, s);
            }
            else if (timeleft >= 60) // Хвилини
            {
                q = div((int)timeleft, 60);
                int m = q.quot;
                int s = q.rem;
                sprintf(text, "%d:%02d", m, s);
            }
            else // Секунди
            {
                sprintf(text, "%d", (int)timeleft);
            }

            int width = TextMessageDrawString(ScreenWidth + 1, ypos, text, 0, 0, 0);
            TextMessageDrawString((ScreenWidth - width) / 2, ypos, text, r, g, b);
        }
        break;

    case 2: // Відображає пройдений час
        if (currentTime > 0)
        {
            char text[64];
            sprintf(text, "%d", (int)currentTime);
            int width = TextMessageDrawString(ScreenWidth + 1, ypos, text, 0, 0, 0);
            TextMessageDrawString((ScreenWidth - width) / 2, ypos, text, r, g, b);
        }
        break;

    case 3: // Локальний комп’ютерний час
        time_t rawtime;
        struct tm *timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        char str[64];
        sprintf(str, "Clock %d:%02d:%02d", (int)timeinfo->tm_hour, (int)timeinfo->tm_min, (int)timeinfo->tm_sec);
        int width = TextMessageDrawString(ScreenWidth + 1, ypos, str, 0, 0, 0);
        TextMessageDrawString((ScreenWidth - width) / 2, ypos, str, r, g, b);
        break;
    }
}

int CHudTimer::MsgFunc_Timer(const char* name, int size, void* buf)
{
	BEGIN_READ(buf, size);
	int timelimit = READ_LONG();
	int effectiveTime = READ_LONG();

	// Update message-based timer data
	seconds_total = timelimit;
	seconds_passed = effectiveTime;
	draw_until = gHUD.m_flTime + 5.0f;
	m_iFlags |= HUD_ACTIVE;

	// Also update sync data if not already synced
	if (!m_flSynced)
	{
		m_flEndTime = timelimit;
		m_flEffectiveTime = effectiveTime;
	}

	discord_integration::set_time_data(seconds_total, seconds_passed);
	return 1;
}

void CHudTimer::Think()
{
	float flTime = gEngfuncs.GetClientTime();
	
	// Check for time reset
	if (m_flNextSyncTime - flTime > 60)
		m_flNextSyncTime = flTime;
		
	// Do sync if enabled
	if (m_pCvarHudTimerSync->value > 0 && m_flNextSyncTime <= flTime)
		SyncTimer(flTime);
}

void CHudTimer::SyncTimer(float fTime)
{
	// Note: Demo playback check removed due to API compatibility issues
	
	float prevEndtime = m_flEndTime;

	// Get timer settings directly from cvars
	if (m_pCvarMpTimelimit && m_pCvarMpTimeleft)
	{
		m_flEndTime = m_pCvarMpTimelimit->value * 60;
		
		if (!m_bDelayTimeleftReading)
		{
			float timeleft = m_pCvarMpTimeleft->value;
			if (timeleft > 0)
			{
				float endtime = timeleft + fTime;
				if (fabs(m_flEndTime - endtime) > 1.5)
					m_flEndTime = endtime;

				m_flSynced = true;
			}
		}
	}

	if (m_bDelayTimeleftReading)
	{
		m_bDelayTimeleftReading = false;
		// Update soon after initial delay
		m_flNextSyncTime = fTime + 1.5;
	}
	else
	{
		// Regular sync interval
		m_flNextSyncTime = fTime + 5.0;
	}
}

void CHudTimer::DoResync()
{
	m_bDelayTimeleftReading = true;
	m_flNextSyncTime = 0;
	m_flSynced = false;
}
