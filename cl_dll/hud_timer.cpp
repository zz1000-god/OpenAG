#include <time.h>

#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "discord_integration.h"

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

class CHudTimer : public CHudBase
{
public:
	int Init();
	int VidInit();
	int Draw(float flTime);
	int MsgFunc_Timer(const char* name, int size, void* buf);

private:
	int seconds_total = 0;
	int seconds_passed = 0;
	float draw_until = 0.0f;
	cvar_t* hud_timer = nullptr;
};

int CHudTimer::Init()
{
	HOOK_MESSAGE(Timer);
	hud_timer = CVAR_CREATE("hud_timer", "1", FCVAR_ARCHIVE);
	m_iFlags |= HUD_ACTIVE;
	gHUD.AddHudElem(this);
	return 1;
}

int CHudTimer::VidInit()
{
	m_iFlags |= HUD_ACTIVE;
	return 1;
}

int CHudTimer::Draw(float time)
{
	if (hud_timer->value == 0.0f)
		return 0;

	if (time >= draw_until) {
		m_iFlags &= ~HUD_ACTIVE;
		return 0;
	}

	char str[64];
	int seconds_to_draw = (hud_timer->value == 2.0f || seconds_total == 0)
		? seconds_passed
		: seconds_total - seconds_passed;

	int days, hours, minutes, seconds;
	unpack_seconds(seconds_to_draw, days, hours, minutes, seconds);

	if (hud_timer->value == 3.0f)
	{
		time_t rawtime;
		struct tm* timeinfo;
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		snprintf(str, sizeof(str), "Clock %02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
	}
	else if (days > 0)
		snprintf(str, sizeof(str), "%d day%s %dh %dm %ds", days, (days > 1 ? "s" : ""), hours, minutes, seconds);
	else if (hours > 0)
		snprintf(str, sizeof(str), "%dh %dm %ds", hours, minutes, seconds);
	else if (minutes > 0)
		snprintf(str, sizeof(str), "%d:%02d", minutes, seconds);
	else if (seconds_to_draw >= 0)
		snprintf(str, sizeof(str), "%d", seconds);
	else
		snprintf(str, sizeof(str), "%d", seconds_to_draw); // overtime

	int r, g, b;
	UnpackRGB(r, g, b, gHUD.m_iDefaultHUDColor);
	gHUD.DrawHudStringCentered(ScreenWidth / 2, gHUD.m_scrinfo.iCharHeight, str, r, g, b);

	return 1;
}

int CHudTimer::MsgFunc_Timer(const char* name, int size, void* buf)
{
	BEGIN_READ(buf, size);
	seconds_total = READ_LONG();
	seconds_passed = READ_LONG();
	draw_until = gHUD.m_flTime + seconds_total;
	m_iFlags |= HUD_ACTIVE;
	discord_integration::set_time_data(seconds_total, seconds_passed);
	return 1;
}
