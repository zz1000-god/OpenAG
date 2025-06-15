#ifndef MUTE_H
#define MUTE_H

class CVoiceMuteManager
{
public:
    CVoiceMuteManager();
    
    void MutePlayer(int iPlayer);
    void UnmutePlayer(int iPlayer); // Новий метод
    void MuteCommand(void);
    void UnmuteCommand(void); // Новий метод
    int FindPlayerByName(const char* name);
    
private:
    // Add any needed private members here
};

extern CVoiceMuteManager g_VoiceMuteManager;

#endif