#ifndef LUASCRIPT_H
#define LUASCRIPT_H

#include <QString>
#include <string>

struct lua_State;
class EmuInstance;

namespace LuaScript
{
void setInstance(EmuInstance* instance);
void queueScript(EmuInstance* instance, const QString& filename);
void update(EmuInstance* instance);
void onFrameComplete();
bool isFocusModeActive();
void stop();
void pause();
void resume();
void restart();
bool isRunning();
bool isPaused();
}

#endif
