#pragma once
#include "net.h" // Додайте цей рядок для NetSocket

class CHudTimer : public CHudBase
{
public:
	virtual int Init();
	virtual int VidInit();
	virtual int Draw(float time);

	int MsgFunc_Timer(const char* name, int size, void* buf);
	void Think();
	// void SyncTimerLocal(float fTime); // Можна залишити, якщо є окрема реалізація, або інтегрувати в SyncTimer

private:
	void SyncTimer(float fTime);
	void DoResync();
	void SyncTimerRemote(unsigned int ip, unsigned short port, float fTime, double latency); // Оголошення нової функції

	// Message data
	int seconds_total;
	int seconds_passed;
	float draw_until;
	
	
	// Sync data
	float m_flEndTime;
	float m_flEffectiveTime; // Використовується, якщо m_flSynced == false
	float m_flNextSyncTime;
	bool m_flSynced;
	bool m_bDelayTimeleftReading;
	
	// CVars
	cvar_t* hud_timer;
	cvar_t* m_pCvarHudTimerSync;
	cvar_t* m_pCvarMpTimelimit;
	cvar_t* m_pCvarMpTimeleft;

	// Нові змінні для A2S_RULES синхронізації
	char m_szPacketBuffer[2048]; // Буфер для збереження відповіді (можливо, фрагментованої)
	int m_iReceivedSize;         // Загальний розмір отриманої відповіді
	int m_iResponceID;           // ID поточної відповіді (для фрагментованих пакетів)
	int m_iReceivedPackets;      // Бітова маска отриманих фрагментованих пакетів
	int m_iReceivedPacketsCount; // Кількість отриманих фрагментованих пакетів
};
