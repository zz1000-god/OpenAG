#pragma once

#include "hud.h"

class CHudTimer : public CHudBase
{
public:
	int Init();
	int VidInit();
	int Draw(float flTime);
	int MsgFunc_Timer(const char* name, int size, void* buf);

private:
	int seconds_total;
	int seconds_passed;
	float draw_until;
	cvar_t* hud_timer;
};
