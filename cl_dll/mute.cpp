#include "hud.h"
#include "cl_util.h"
#include "voice_status.h"
#include "mute.h"
#include <cstring>

// Глобальна змінна
CVoiceMuteManager g_VoiceMuteManager;

// Функції-обробники команд
void MuteCommandHandler() 
{
    g_VoiceMuteManager.MuteCommand();
}

void UnmuteCommandHandler() 
{
    g_VoiceMuteManager.UnmuteCommand();
}

// Конструктор з реєстрацією команд
CVoiceMuteManager::CVoiceMuteManager()
{
    // Реєструємо команди
    gEngfuncs.pfnAddCommand("cl_mute", MuteCommandHandler);
    gEngfuncs.pfnAddCommand("cl_unmute", UnmuteCommandHandler);
}

int CVoiceMuteManager::FindPlayerByName(const char* name)
{
    for (int i = 1; i <= MAX_PLAYERS; i++)
    {
        hud_player_info_t* pl_info = &g_PlayerInfoList[i];
        if (pl_info && pl_info->name && pl_info->name[0])
        {
            if (_stricmp(pl_info->name, name) == 0)
                return i;
        }
    }
    return -1;
}

void CVoiceMuteManager::MutePlayer(int iPlayer)
{
    if (iPlayer <= 0 || iPlayer > MAX_PLAYERS)
        return;
        
    hud_player_info_t *pl_info = &g_PlayerInfoList[iPlayer];
    
    if (!pl_info || !pl_info->name || !pl_info->name[0])
        return;

    // Don't allow muting yourself
    if (pl_info->thisplayer && !gEngfuncs.IsSpectateOnly())
        return;

    char string[256];
    if (GetClientVoiceMgr()->IsPlayerBlocked(iPlayer))
    {
        // Remove mute
        GetClientVoiceMgr()->SetPlayerBlockedState(iPlayer, false);
        
        char string1[1024];
        snprintf(string1, sizeof(string1), CHudTextMessage::BufferedLocaliseTextString("#Unmuted"), pl_info->name);
        snprintf(string, sizeof(string), "%c** %s\n", HUD_PRINTTALK, string1);
    }
    else 
    {
        // Mute the player
        GetClientVoiceMgr()->SetPlayerBlockedState(iPlayer, true);
        
        char string1[1024];
        char string2[1024];
        snprintf(string1, sizeof(string1), CHudTextMessage::BufferedLocaliseTextString("#Muted"), pl_info->name);
        snprintf(string2, sizeof(string2), "%s", CHudTextMessage::BufferedLocaliseTextString("#No_longer_hear_that_player"));
        snprintf(string, sizeof(string), "%c** %s %s\n", HUD_PRINTTALK, string1, string2);
    }

    gHUD.m_TextMessage.MsgFunc_TextMsg(NULL, strlen(string)+1, string);
}

// Додаємо новий метод для розмуту
void CVoiceMuteManager::UnmutePlayer(int iPlayer)
{
    if (iPlayer <= 0 || iPlayer > MAX_PLAYERS)
        return;
        
    hud_player_info_t *pl_info = &g_PlayerInfoList[iPlayer];
    
    if (!pl_info || !pl_info->name || !pl_info->name[0])
        return;

    // Don't allow unmuting yourself
    if (pl_info->thisplayer && !gEngfuncs.IsSpectateOnly())
        return;

    // Remove mute
    GetClientVoiceMgr()->SetPlayerBlockedState(iPlayer, false);
    
    char string[256];
    char string1[1024];
    snprintf(string1, sizeof(string1), CHudTextMessage::BufferedLocaliseTextString("#Unmuted"), pl_info->name);
    snprintf(string, sizeof(string), "%c** %s\n", HUD_PRINTTALK, string1);

    gHUD.m_TextMessage.MsgFunc_TextMsg(NULL, strlen(string)+1, string);
}

void CVoiceMuteManager::MuteCommand(void)
{
    int argc = gEngfuncs.Cmd_Argc();
    
    if (argc != 2)
    {
        gEngfuncs.Con_Printf("Usage: cl_mute <playerid or playername>\n");
        return;
    }

    const char* arg = gEngfuncs.Cmd_Argv(1);
    
    // Спочатку перевіряємо чи це число (ID гравця)
    if (isdigit(arg[0]) || (arg[0] == '-' && isdigit(arg[1])))
    {
        int iPlayer = atoi(arg);
        MutePlayer(iPlayer);
    }
    else // Якщо не число - шукаємо за ніком
    {
        int iPlayer = FindPlayerByName(arg);
        if (iPlayer != -1)
        {
            MutePlayer(iPlayer);
        }
        else
        {
            gEngfuncs.Con_Printf("Player '%s' not found\n", arg);
        }
    }
}

void CVoiceMuteManager::UnmuteCommand(void)
{
    int argc = gEngfuncs.Cmd_Argc();
    
    if (argc != 2)
    {
        gEngfuncs.Con_Printf("Usage: cl_unmute <playerid or playername>\n");
        return;
    }

    const char* arg = gEngfuncs.Cmd_Argv(1);
    
    // Спочатку перевіряємо чи це число (ID гравця)
    if (isdigit(arg[0]) || (arg[0] == '-' && isdigit(arg[1])))
    {
        int iPlayer = atoi(arg);
        UnmutePlayer(iPlayer);
    }
    else // Якщо не число - шукаємо за ніком
    {
        int iPlayer = FindPlayerByName(arg);
        if (iPlayer != -1)
        {
            UnmutePlayer(iPlayer);
        }
        else
        {
            gEngfuncs.Con_Printf("Player '%s' not found\n", arg);
        }
    }
}