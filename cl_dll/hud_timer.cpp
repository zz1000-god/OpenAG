int CHudTimer::Draw(float time)
{
	if (gHUD.m_iHideHUDDisplay & HIDEHUD_ALL)
		return 0;

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
	
	// Handle local time display (hud_timer = 3)
	if (hud_timer->value == 3.0f) {
		time_t rawtime;
		struct tm* timeinfo;
		char str[64];
		
		time(&rawtime);
		timeinfo = localtime(&rawtime);
		
		// Format: HH:MM:SS
		sprintf(str, "%02d:%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
		
		int r, g, b;
		UnpackRGB(r, g, b, gHUD.m_iDefaultHUDColor);
		gHUD.DrawHudStringCentered(ScreenWidth / 2, gHUD.m_scrinfo.iCharHeight, str, r, g, b);
		return 1;
	}
	
	// Handle synced timer when no message timer is active
	if (hud_timer->value == 0.0f)
		return 0;

	// For values 1 and 2, we need either synced timer or message timer
	if (!m_flSynced && gHUD.m_flTime >= draw_until)
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

int CHudTimer::VidInit()
{
	// Don't disable HUD_ACTIVE here - let it stay active
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
