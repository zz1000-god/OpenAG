#pragma once

#include "hud.h"

class CHudTimer : public CHudBase
{
public:
	virtual int Init();
	virtual int VidInit();
	virtual int Draw(float time);

	int MsgFunc_Timer(const char* name, int size, void* buf);
	void Think();

private:
	void SyncTimer(float fTime);
	void DoResync();

	// Message data
	int seconds_total;
	int seconds_passed;
	float draw_until;
	
	// Sync data
	float m_flEndTime;
	float m_flEffectiveTime;
	float m_flNextSyncTime;
	bool m_flSynced;
	bool m_bDelayTimeleftReading;
	
	// CVars
	cvar_t* hud_timer;
	cvar_t* m_pCvarHudTimerSync;
	cvar_t* m_pCvarMpTimelimit;
	cvar_t* m_pCvarMpTimeleft;
};
