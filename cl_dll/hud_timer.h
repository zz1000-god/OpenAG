#pragma once

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

class CHudTimer : public CHudBase // або CHudBase, якщо у вас так називається базовий клас
{
public:
    static CHudTimer* Get();

    void Init();
    void VidInit();
    int MsgFunc_Timer(const char* pszName, int iSize, void* pbuf);
    void Draw(float fTime);
    void CustomTimerCommand();
    void DrawTimerInternal(int time, float ypos, int r, int g, int b, bool redOnLow);
    void SetNextmap(const char* nextmap);
    const char* GetNextmap() const;

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
