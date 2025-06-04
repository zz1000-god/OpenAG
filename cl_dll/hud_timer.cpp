#include "hud.h"
#include "cl_util.h"
#include "parsemsg.h"
#include "hud_timer.h"
#include "discord_integration.h"
#include <ctime>
#include "net_api.h" // Для gEngfuncs.pNetAPI та net_status_t
#include "net.h"     // Для NetSendUdp, NetReceiveUdp, NetGetRuleValueFromBuffer тощо

#define NET_API gEngfuncs.pNetAPI // Макрос для доступу до NetAPI

enum RulesRequestStatus
{
    SOCKET_NONE = 0,
    SOCKET_IDLE = 1,
    SOCKET_AWAITING_CODE = 2,
    SOCKET_AWAITING_ANSWER = 3,
};
RulesRequestStatus g_eRulesRequestStatus = SOCKET_NONE;
NetSocket g_timerSocket = 0; // Або NULL, якщо NetSocket це вказівник. 0 для uintptr_t є нормальним.

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
    m_iFlags |= HUD_ACTIVE;

    m_pCvarMpTimelimit = gEngfuncs.pfnGetCvarPointer("mp_timelimit");
    m_pCvarMpTimeleft = gEngfuncs.pfnGetCvarPointer("mp_timeleft");

    m_flEndTime = 0.0f;
    m_flEffectiveTime = 0.0f;
    m_flNextSyncTime = 0.0f;
    m_flSynced = false;
    m_bDelayTimeleftReading = true;

    // Ініціалізація нових змінних для A2S_RULES
    m_iReceivedSize = 0;
    m_iResponceID = 0;
    m_iReceivedPackets = 0;
    m_iReceivedPacketsCount = 0;
    memset(m_szPacketBuffer, 0, sizeof(m_szPacketBuffer));

    // Ініціалізація стану сокета та закриття існуючого, якщо є
    if (g_timerSocket != 0) // Припускаючи, що 0 - невалідний/закритий стан для NetSocket
    {
        NetCloseSocket(g_timerSocket);
        g_timerSocket = 0;
    }
    g_eRulesRequestStatus = SOCKET_NONE;

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
	if (hud_timer->value == 0.0f)
		return 0;
		
	// For synced timer (values 1 and 2), we need sync data
	if ((hud_timer->value == 1.0f || hud_timer->value == 2.0f) && !m_flSynced) {
		// If no sync data available, try to use basic time display
		char str[64];
		int seconds_to_draw = (int)time;
		
		if (hud_timer->value == 1.0f) {
			// Show remaining time based on mp_timelimit if available
			if (m_pCvarMpTimelimit && m_pCvarMpTimelimit->value > 0) {
				seconds_to_draw = (int)(m_pCvarMpTimelimit->value * 60 - time);
				if (seconds_to_draw < 0) seconds_to_draw = 0;
			} else {
				strcpy(str, "No mp_timelimit");
				int r, g, b;
				UnpackRGB(r, g, b, gHUD.m_iDefaultHUDColor);
				gHUD.DrawHudStringCentered(ScreenWidth / 2, gHUD.m_scrinfo.iCharHeight, str, r, g, b);
				return 1;
			}

		
		} else {
			// hud_timer = 2, show elapsed time
			seconds_to_draw = (int)time;
		}
		
		int days, hours, minutes, seconds;
		unpack_seconds(seconds_to_draw, days, hours, minutes, seconds);

		if (hours > 0)
			sprintf(str, "%dh %dm %ds", hours, minutes, seconds);
		else if (minutes > 0)
			sprintf(str, "%d:%02d", minutes, seconds);
		else
			sprintf(str, "%d", seconds_to_draw);

		int r, g, b;
		UnpackRGB(r, g, b, gHUD.m_iDefaultHUDColor);
		gHUD.DrawHudStringCentered(ScreenWidth / 2, gHUD.m_scrinfo.iCharHeight, str, r, g, b);
		return 1;
	}

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

    // Перевірка на скидання часу (як у timer.cpp)
    if (m_flNextSyncTime - flTime > 60.0f) // Якщо наступний час синхронізації надто далеко в майбутньому
        m_flNextSyncTime = flTime; // Скидаємо, щоб синхронізуватися зараз

    // Виконуємо синхронізацію, якщо увімкнено
    if (m_pCvarHudTimerSync != nullptr && m_pCvarHudTimerSync->value > 0.0f && m_flNextSyncTime <= flTime)
        SyncTimer(flTime);
}

void CHudTimer::SyncTimer(float fTime)
{
    // Демо-відтворення тут не обробляється, як у timer.cpp, оскільки CHudTimer може не мати цієї функціональності.

    if (m_pCvarHudTimerSync == nullptr || m_pCvarHudTimerSync->value == 0.0f) // Перевірка cvar hud_timer_sync
    {
        m_flSynced = false;
        return;
    }

    // Переконайтеся, що мережева підсистема ініціалізована.
    if (NET_API) // Перевіряємо, чи доступний NET_API
    {
        NET_API->InitNetworking(); // Може викликатися багато разів, це безпечно.

        net_status_t status;
        NET_API->Status(&status); // Отримуємо статус мережі

        if (status.connected)
        {
            if (status.remote_address.type == NA_IP) // Якщо підключено до сервера за IP
            {
                SyncTimerRemote(*(unsigned int *)status.remote_address.ip, status.remote_address.port, fTime, status.latency);
                // Якщо очікуємо відповідь від сервера, не перезаписуємо m_flNextSyncTime тут,
                // оскільки він встановлюється в SyncTimerRemote для таймаутів.
                if (g_eRulesRequestStatus == SOCKET_AWAITING_CODE || g_eRulesRequestStatus == SOCKET_AWAITING_ANSWER)
                {
                    return; 
                }
            }
            else if (status.remote_address.type == NA_LOOPBACK) // Якщо це loopback з'єднання
            {
                // Використовуємо локальну синхронізацію через CVars (адаптація оригінальної логіки SyncTimer)
                // float prevEndtime = m_flEndTime; // Якщо потрібно відстежувати зміни

                if (m_pCvarMpTimelimit && m_pCvarMpTimeleft)
                {
                    // Спочатку встановлюємо час кінця на основі mp_timelimit
                    m_flEndTime = m_pCvarMpTimelimit->value * 60.0f; // Хвилини в секунди

                    if (!m_bDelayTimeleftReading)
                    {
                        float timeleft_cvar = m_pCvarMpTimeleft->value;
                        if (timeleft_cvar > 0)
                        {
                            // Розраховуємо час кінця на основі mp_timeleft та поточного часу клієнта (fTime)
                            float endtime_calculated_from_timeleft = timeleft_cvar + fTime;

                            // Якщо розрахований час кінця суттєво відрізняється від часу з mp_timelimit,
                            // або якщо mp_timelimit = 0, то використовуємо розрахунок з mp_timeleft.
                            // Абсолютний час клієнта, коли карта закінчиться.
                            if (fabs(m_flEndTime - endtime_calculated_from_timeleft) > 1.5f || m_flEndTime == 0)
                            {
                                m_flEndTime = endtime_calculated_from_timeleft;
                            }
                            m_flSynced = true; // Синхронізовано через локальні CVars
                        }
                        // Якщо timeleft_cvar <= 0, m_flEndTime залишається тим, що встановлено з mp_timelimit
                        // (або 0, якщо mp_timelimit теж 0).
                        // m_flSynced може залишитися false, якщо тільки mp_timelimit не вказує на активний таймер.
                        // timer.cpp встановлює m_flSynced = true, якщо є mp_timelimit. Для узгодженості:
                        else if (m_flEndTime > 0) { // Якщо mp_timelimit встановлено
                            // m_flSynced = true; // Можна вважати синхронізованим за mp_timelimit, навіть якщо timeleft 0 (наприклад, початок раунду)
                            // Однак, це може бути неточним, краще покладатися на timeleft.
                            // Залишимо як є: m_flSynced = true тільки якщо timeleft > 0.
                        }
                    }
                }
                m_flNextSyncTime = fTime + 5.0f; // Інтервал синхронізації для loopback
            }
            else // Інші типи з'єднань (наприклад, NA_BROADCAST, NA_IPX)
            {
                m_flSynced = false; // Не можемо синхронізуватися
                m_flNextSyncTime = fTime + 1.0f; // Спробувати пізніше
            }

            if (m_bDelayTimeleftReading)
            {
                m_bDelayTimeleftReading = false;
                // Запланувати оновлення незабаром після початкової затримки
                m_flNextSyncTime = fTime + 1.5f;
            }
        }
        else // Не підключено до сервера
        {
            m_flSynced = false;
            // Закрити сокет, якщо він був відкритий і ми більше не підключені
            if (g_timerSocket != 0)
            {
                NetCloseSocket(g_timerSocket);
                g_timerSocket = 0;
                g_eRulesRequestStatus = SOCKET_NONE;
            }
            m_flNextSyncTime = fTime + 1.0f; // Спробувати синхронізуватися пізніше (раптом підключимося)
        }
    }
    else // NET_API недоступний
    {
        m_flSynced = false;
        m_flNextSyncTime = fTime + 5.0f; // Спробувати пізніше
    }
}

void CHudTimer::SyncTimerRemote(unsigned int ip, unsigned short port, float fTime, double latency)
{
    // float prevEndtime = m_flEndTime; // Якщо потрібно відстежувати зміни для інших цілей
    char buffer[2048]; // Тимчасовий буфер для отриманих даних
    int len = 0;

    // Перевірка на таймаут запиту та повторне надсилання
    if (fTime - m_flNextSyncTime > 3.0f && (g_eRulesRequestStatus == SOCKET_AWAITING_CODE || g_eRulesRequestStatus == SOCKET_AWAITING_ANSWER))
    {
        g_eRulesRequestStatus = SOCKET_IDLE; // Скидання стану для повторної спроби
        NetCloseSocket(g_timerSocket); // Закриваємо старий сокет перед перевідправкою
        g_timerSocket = 0;
    }

    // Отримання налаштувань з сервера
    switch (g_eRulesRequestStatus)
    {
    case SOCKET_NONE: // Якщо сокет не ініціалізований
         // Це не повинно траплятися тут, якщо SyncTimer правильно керує станами
    case SOCKET_IDLE: // Якщо сокет вільний або попередній запит завершено/скасовано
        m_iResponceID = 0;
        m_iReceivedSize = 0;
        m_iReceivedPackets = 0;
        m_iReceivedPacketsCount = 0;
        memset(m_szPacketBuffer, 0, sizeof(m_szPacketBuffer)); // Очищення буфера перед новим запитом

        if (g_timerSocket != 0) { // Переконайтеся, що старий сокет очищено
             NetClearSocket(g_timerSocket); // Функція з net.h, якщо є, або просто закрити/перевідкрити
        }
        // Надсилаємо запит A2S_RULES (старий метод з \xFF\xFF\xFF\xFFrules або \xFF\xFF\xFF\xFFV)
        // timer.cpp використовує "\xFF\xFF\xFF\xFFV\xFF\xFF\xFF\xFF"
        // Це запит на отримання challenge 'A'
        NetSendUdp(ip, port, "\xFF\xFF\xFF\xFFV\xFF\xFF\xFF\xFF", 9, &g_timerSocket);
        g_eRulesRequestStatus = SOCKET_AWAITING_CODE;
        m_flNextSyncTime = fTime; // Встановлюємо час для перевірки таймауту
        return;

    case SOCKET_AWAITING_CODE: // Очікування challenge 'A'
        len = NetReceiveUdp(ip, port, buffer, sizeof(buffer), g_timerSocket);
        if (len < 5) // Мінімальна довжина для відповіді
            return;

        // Перевірка, чи це відповідь з challenge ('A')
        if (*(int *)buffer == -1 /*0xFFFFFFFF*/ && buffer[4] == 'A' && len == 9)
        {
            // Відповідь - це challenge, надсилаємо запит знову з цим кодом
            // buffer вже містить challenge. Змінюємо тип запиту на 'V' (A2S_RULES)
            // timer.cpp надсилає той самий буфер, змінивши buffer[4] на 'V', але тут логіка інша:
            // потрібно надіслати A2S_RULES запит, передавши отриманий challenge.
            // Для простоти, адаптуємо логіку з timer.cpp: надсилаємо 'V' + challenge
            // char rulesQuery[9] = "\xFF\xFF\xFF\xFFV"; // A2S_RULES
            // memcpy(rulesQuery + 5, buffer + 5, 4); // Копіюємо challenge
            // NetSendUdp(ip, port, rulesQuery, 9, &g_timerSocket);
            // АБО, якщо сервер очікує V + challenge, як у timer.cpp:
            buffer[4] = 'V'; // Змінюємо 'A' на 'V'
            NetSendUdp(ip, port, buffer, 9, &g_timerSocket);


            g_eRulesRequestStatus = SOCKET_AWAITING_ANSWER;
            m_flNextSyncTime = fTime; // Встановлюємо час для перевірки таймауту
            return;
        }
        // Якщо це не 'A', можливо, це вже відповідь 'E' (A2S_RULES)
        // (деякі сервери можуть не надсилати challenge)
        g_eRulesRequestStatus = SOCKET_AWAITING_ANSWER;
        // Не робимо return, а переходимо до обробки як відповіді нижче
        break; // Перехід до обробки як SOCKET_AWAITING_ANSWER

    case SOCKET_AWAITING_ANSWER: // Очікування відповіді 'E' (A2S_RULES)
        len = NetReceiveUdp(ip, port, buffer, sizeof(buffer), g_timerSocket);
        if (len < 5) // Мінімальна довжина
            return;
        break; // Дані отримані, йдемо далі для обробки
    }

    // Обробка отриманих даних (можливо, фрагментованих)
    // Цей код значною мірою взятий з timer.cpp
    if (*(int *)buffer == -2 /*0xFEFFFFFF*/) // Фрагментований пакет
    {
        if (len < 9) return; // Недостатньо даних для заголовка фрагментованого пакета

        // Перевірка ID запиту та номера пакета
        int requestID = *(int *)(buffer + 4); // ID запиту
        unsigned char headerInfo = buffer[8];
        int currentPacket = headerInfo >> 4;  // Старші 4 біти - номер поточного пакета
        int totalPackets = headerInfo & 0x0F; // Молодші 4 біти - загальна кількість пакетів

        if (currentPacket >= totalPackets) return; // Пошкоджений пакет

        if (m_iReceivedPacketsCount == 0) // Перший фрагментований пакет
        {
            m_iResponceID = requestID;
        }
        else if (m_iResponceID != requestID)
        {
            return; // Пакет від іншої відповіді, ігноруємо
        }

        if (m_iReceivedPackets & (1 << currentPacket)) return; // Цей пакет вже отримано

        // Копіювання даних у буфер для збирання
        // Розмір даних у фрагментованому пакеті = len - 9 (заголовок)
        // Позиція для копіювання: стандартний розмір даних нефрагментованого пакета GoldSrc ~1400, але тут краще динамічно.
        // Однак, оскільки GoldSrc зазвичай має MTU близько 1400, і заголовок спліт-пакета - 9 байт,
        // то дані - це (len - 9). Позиція буде (packet_size - 9) * currentPacket.
        // timer.cpp використовує (1400 - 9) * currentPacket. Це припущення про максимальний розмір.
        int dataOffset = (1400 - 9) * currentPacket; // Припущення з timer.cpp
        if (dataOffset + (len - 9) > sizeof(m_szPacketBuffer)) return; // Переповнення буфера

        memcpy(m_szPacketBuffer + dataOffset, buffer + 9, len - 9);
        m_iReceivedSize += (len - 9);
        m_iReceivedPackets |= (1 << currentPacket);
        m_iReceivedPacketsCount++;

        if (m_iReceivedPacketsCount < totalPackets) return; // Ще не всі пакети отримано
        // Всі пакети отримано, m_szPacketBuffer містить повну відповідь (без заголовка 0xFFFFFFFFE)
    }
    else if (*(int *)buffer == -1 /*0xFFFFFFFF*/ && buffer[4] == 'E') // Нефрагментована відповідь A2S_RULES ('E')
    {
        // Копіюємо дані (включаючи заголовок 0xFFFFFFFFE)
        if (len > sizeof(m_szPacketBuffer)) return; // Переповнення
        memcpy(m_szPacketBuffer, buffer, len); // Копіюємо всю відповідь
        m_iReceivedSize = len; // Розмір всієї відповіді
    }
    else
    {
        // Невідомий тип пакета або помилка
        g_eRulesRequestStatus = SOCKET_IDLE; // Скидаємо стан
        NetCloseSocket(g_timerSocket);
        g_timerSocket = 0;
        return;
    }

    // Перевірка, чи це дійсно відповідь A2S_RULES ('E')
    // Для фрагментованих пакетів, перший заголовок був 0xFEFFFFFF,
    // а зібрані дані починаються безпосередньо з корисного навантаження (після 9 байт заголовка).
    // Для нефрагментованих, m_szPacketBuffer[4] має бути 'E'.
    // В timer.cpp, після збирання фрагментованих пакетів, він перевіряє
    // *(int *)m_szPacketBuffer != -1 /*0xFFFFFFFF*/ || m_szPacketBuffer[4] != 'E'.
    // Це означає, що після збирання фрагментів, перший int *має* бути 0xFFFFFFFF, а потім 'E'.
    // Це можливо, якщо перший фрагмент містив початковий заголовок, що не типово для стандартної реалізації A2S_RULES.
    // Простіше припустити, що якщо це був фрагментований пакет, то m_szPacketBuffer вже містить чисті дані правил.
    // Або, якщо це був нефрагментований, то він має заголовок.
    // Давайте слідувати логіці timer.cpp:
    if (*(int *)m_szPacketBuffer != -1 /*0xFFFFFFFF*/ || m_szPacketBuffer[4] != 'E')
    {
        // Це не валідна відповідь A2S_RULES
        g_eRulesRequestStatus = SOCKET_IDLE;
         NetCloseSocket(g_timerSocket); // Закриваємо, щоб уникнути проблем
         g_timerSocket = 0;
        return;
    }

    // Якщо дійшли сюди, відповідь отримана і (сподіваємось) валідна
    m_flSynced = true;
    g_eRulesRequestStatus = SOCKET_IDLE; // Готові до наступного запиту (після інтервалу)
    m_flNextSyncTime = fTime + 10.0f; // Інтервал синхронізації для віддаленого сервера (наприклад, 10 секунд)

    // Розбір правил
    char *value;

    // Отримання mp_timelimit
    value = NetGetRuleValueFromBuffer(m_szPacketBuffer, m_iReceivedSize, "mp_timelimit");
    if (value && value[0])
    {
        m_flEndTime = atof(value) * 60; // Конвертуємо хвилини в секунди
    }
    else
    {
        m_flEndTime = 0; // Якщо немає timelimit
    }

    // Отримання mp_timeleft та коригування m_flEndTime
    // gHUD.m_iIntermission тут недоступний напряму, але логіка важлива.
    // Можливо, потрібно передати стан інтермісії або перевірити його іншим способом.
    // Для простоти, поки що без перевірки інтермісії.
    if (!m_bDelayTimeleftReading) // Не читати timeleft, якщо є затримка (наприклад, після зміни карти)
    {
        value = NetGetRuleValueFromBuffer(m_szPacketBuffer, m_iReceivedSize, "mp_timeleft");
        if (value && value[0])
        {
            float timeleft_from_server = atof(value);
            if (timeleft_from_server > 0)
            {
                // Розрахунок часу кінця на основі timeleft сервера та поточного часу клієнта,
                // скоригованого на затримку (latency).
                // fTime - це gEngfuncs.GetClientTime()
                float calculated_endtime = timeleft_from_server + (fTime - latency);

                // Якщо розрахований час кінця значно відрізняється від того, що отримали з mp_timelimit,
                // або якщо mp_timelimit був 0, використовуємо розрахований.
                // Поріг 1.5 секунди, як у timer.cpp.
                if (fabs(m_flEndTime - calculated_endtime) > 1.5 || m_flEndTime == 0)
                {
                    m_flEndTime = calculated_endtime;
                }
                // Важливо: m_flEndTime тут - це абсолютний час клієнта, коли карта закінчиться.
            }
        }
    }
    // Якщо m_flEndTime змінився, можна встановити прапорець m_bNeedWriteTimer, якщо була б логіка запису в демо.
    // if (m_flEndTime != prevEndtime) m_bNeedWriteTimer = true;

    // Очищення буфера та скидання лічильників для наступного разу (хоча це вже робиться на початку SOCKET_IDLE)
    m_iReceivedSize = 0;
    m_iReceivedPackets = 0;
    m_iReceivedPacketsCount = 0;
    m_iResponceID = 0;
    // NetCloseSocket(g_timerSocket); // Закриваємо сокет після успішного отримання, він буде перевідкритий.
    // g_timerSocket = 0; // Це робиться в SOCKET_IDLE / SOCKET_NONE або при помилці.
}

void CHudTimer::DoResync()
{
	m_bDelayTimeleftReading = true;
	m_flNextSyncTime = 0;
	m_flSynced = false;
}
