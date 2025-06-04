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
	// Don't disable HUD_ACTIVE here - let it stay active for local time display
	// m_iFlags &= ~HUD_ACTIVE;
	
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

int CHudTimer::Draw(float time)
{
	if (gHUD.m_iHideHUDDisplay & HIDEHUD_ALL)
		return 0;

	// Handle local time display (hud_timer = 3)
	if (hud_timer->value == 3.0f) {
		time_t rawtime;
		struct tm* timeinfo;
		char str[64];
		
		::time(&rawtime);
		timeinfo = ::localtime(&rawtime);
		
		// Format: HH:MM:SS
		sprintf(str, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
		
		int r, g, b;
		UnpackRGB(r, g, b, gHUD.m_iDefaultHUDColor);
		gHUD.DrawHudStringCentered(ScreenWidth / 2, gHUD.m_scrinfo.iCharHeight, str, r, g, b);
		return 1;
	}

	// Handle message-based timer
	if (gHUD.m_flTime < draw_until) {
		if (hud_timer->value == 0.0f)
			return 0;

		char str[64];
		int seconds_to_draw = (hud_timer->value == 2.0f || seconds_total == 0)
			? seconds_passed
			: seconds_total - seconds_passed;

		int days, hours, minutes, seconds;
		unpack_seconds(seconds_to_draw, days, hours, minutes, seconds);

		if (days > 0)
			sprintf(str, "%d day%s %dh %dm %ds", days, (days > 1 ? "s" : ""), hours, minutes, seconds);
		else if (hours > 0)
			sprintf(str, "%dh %dm %ds", hours, minutes, seconds);
		else if (minutes > 0)
			sprintf(str, "%d:%02d", minutes, seconds);
		else if (seconds_to_draw >= 0)
			sprintf(str, "%d", seconds);
		else
			sprintf(str, "%d", seconds_to_draw); // overtime

		int r, g, b;
		UnpackRGB(r, g, b, gHUD.m_iDefaultHUDColor);
		gHUD.DrawHudStringCentered(ScreenWidth / 2, gHUD.m_scrinfo.iCharHeight, str, r, g, b);
		return 1;
	}
	
	// Handle synced timer when no message timer is active
	if (hud_timer->value == 0.0f || !m_flSynced)
		return 0;

	float timeleft = m_flSynced ? (m_flEndTime - time) : (m_flEndTime - m_flEffectiveTime);
	
	if (timeleft <= 0 && hud_timer->value == 1.0f) // Don't show negative time left
		return 0;
		
	char str[64];
	int seconds_to_draw;
	
	if (hud_timer->value == 1.0f) // time left
		seconds_to_draw = (int)(timeleft + 0.5f);
	else if (hud_timer->value == 2.0f) // time passed
		seconds_to_draw = (int)(time + 0.5f);
	else
		return 0;

	int days, hours, minutes, seconds;
	unpack_seconds(abs(seconds_to_draw), days, hours, minutes, seconds);

	if (days > 0)
		sprintf(str, "%s%d day%s %dh %dm %ds", (seconds_to_draw < 0 ? "-" : ""), days, (days > 1 ? "s" : ""), hours, minutes, seconds);
	else if (hours > 0)
		sprintf(str, "%s%dh %dm %ds", (seconds_to_draw < 0 ? "-" : ""), hours, minutes, seconds);
	else if (minutes > 0)
		sprintf(str, "%s%d:%02d", (seconds_to_draw < 0 ? "-" : ""), minutes, seconds);
	else
		sprintf(str, "%d", seconds_to_draw);

	int r, g, b;
	// Red color for low time (less than 60 seconds)
	if (hud_timer->value == 1.0f && seconds_to_draw <= 60 && seconds_to_draw > 0)
	{
		r = 255; g = 16; b = 16;
	}
	else
	{
		UnpackRGB(r, g, b, gHUD.m_iDefaultHUDColor);
	}
	
	gHUD.DrawHudStringCentered(ScreenWidth / 2, gHUD.m_scrinfo.iCharHeight, str, r, g, b);

	return 1;
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
	if (gEngfuncs.pDemoAPI->IsPlayingback())
		return;

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
