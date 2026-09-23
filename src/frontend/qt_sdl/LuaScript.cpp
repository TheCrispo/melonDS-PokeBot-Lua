#include "LuaScript.h"

#include <QFileInfo>
#include <QCryptographicHash>
#include <QDir>
#include <QTcpSocket>
#include <QHostAddress>
#include <QStringList>

#include <filesystem>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <memory>
#include <cstdint>
#include <vector>
#include <cctype>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "EmuInstance.h"
#include "EmuThread.h"
#include "NDS.h"
#include "NDSCart.h"
#include "NDS_Header.h"
#include "Platform.h"

namespace LuaScript
{
static EmuInstance* gInstance = nullptr;
static lua_State* gMain = nullptr;
static lua_State* gThread = nullptr;
static int gThreadRef = LUA_NOREF;
static QString gScriptDir;
static QString gPending;
static bool gStop = false;
static bool gRunning = false;
static bool gPaused = false;
static uint64_t gFrame = 0;
static bool gFocusModeActive = false;
static QString gScriptFile;
static uint32_t gLuaInputMask = 0xFFF;
static QTcpSocket* gSocket = nullptr;
static QByteArray gSocketBuffer;

static void message(EmuInstance* inst, const QString& text)
{
    fprintf(stderr, "[melonDS Lua] %s\n", text.toUtf8().constData());
    if (inst) inst->osdAddMessage(0, "Lua: %s", text.toUtf8().constData());
}

static uint32_t readMain(EmuInstance* inst, uint32_t addr, int size)
{
    if (!inst || !inst->getNDS()) return 0;
    auto* nds = inst->getNDS();
    addr &= nds->MainRAMMask;
    switch (size)
    {
    case 1: return nds->MainRAM[addr];
    case 2: return *(uint16_t*)(nds->MainRAM + addr);
    default: return *(uint32_t*)(nds->MainRAM + addr);
    }
}

static void writeMain(EmuInstance* inst, uint32_t addr, uint32_t value, int size)
{
    if (!inst || !inst->getNDS()) return;
    auto* nds = inst->getNDS();
    addr &= nds->MainRAMMask;
    switch (size)
    {
    case 1: nds->MainRAM[addr] = (uint8_t)value; break;
    case 2: *(uint16_t*)(nds->MainRAM + addr) = (uint16_t)value; break;
    default: *(uint32_t*)(nds->MainRAM + addr) = value; break;
    }
}

static int l_readbyte(lua_State* L) { lua_pushinteger(L, readMain(gInstance, (uint32_t)luaL_checkinteger(L,1),1)); return 1; }
static int l_readwordu(lua_State* L) { lua_pushinteger(L, readMain(gInstance, (uint32_t)luaL_checkinteger(L,1),2)); return 1; }
static int l_readdwordu(lua_State* L) { lua_pushinteger(L, readMain(gInstance, (uint32_t)luaL_checkinteger(L,1),4)); return 1; }
static int l_readwords(lua_State* L) { lua_pushinteger(L, (int16_t)readMain(gInstance, (uint32_t)luaL_checkinteger(L,1),2)); return 1; }
static int l_readdwords(lua_State* L) { lua_pushinteger(L, (int32_t)readMain(gInstance, (uint32_t)luaL_checkinteger(L,1),4)); return 1; }
static int l_writebyte(lua_State* L) { writeMain(gInstance,(uint32_t)luaL_checkinteger(L,1),(uint32_t)luaL_checkinteger(L,2),1); return 0; }
static int l_writeword(lua_State* L) { writeMain(gInstance,(uint32_t)luaL_checkinteger(L,1),(uint32_t)luaL_checkinteger(L,2),2); return 0; }
static int l_writedword(lua_State* L) { writeMain(gInstance,(uint32_t)luaL_checkinteger(L,1),(uint32_t)luaL_checkinteger(L,2),4); return 0; }

static int l_frameadvance(lua_State* L) { gFocusModeActive = false; return lua_yield(L, 0); }
static int l_emulateframeinvisible(lua_State* L) { gFocusModeActive = true; return lua_yield(L, 0); }
static int l_yield(lua_State* L) { return lua_yield(L, 0); }
static int l_framecount(lua_State* L) { lua_pushinteger(L, (lua_Integer)gFrame); return 1; }
static int l_emulating(lua_State* L) { lua_pushboolean(L, gInstance && gInstance->emuIsActive() && gInstance->getNDS() != nullptr); return 1; }
static int l_reset(lua_State* L) { if (gInstance) { gInstance->luaReset(); gFrame = 0; } return 0; }

static uint32_t buttonBit(const char* name)
{
    if (!name) return 0;
    std::string key(name);
    for (char& c : key) c = (char)std::tolower((unsigned char)c);
    static const char* names[] = {"a","b","select","start","right","left","up","down","r","l","x","y"};
    for (int i=0;i<12;i++) if (key == names[i]) return 1u<<i;
    return 0;
}

static int l_joyset(lua_State* L)
{
    luaL_checktype(L,1,LUA_TTABLE);
    uint32_t mask = 0xFFF;
    lua_pushnil(L);
    while (lua_next(L,1) != 0)
    {
        if (lua_type(L,-2)==LUA_TSTRING && lua_toboolean(L,-1))
        {
            const char* n = lua_tostring(L,-2);
            uint32_t b = buttonBit(n);
            if (b) mask &= ~b;
        }
        lua_pop(L,1);
    }
    gLuaInputMask = mask;
    if (gInstance) gInstance->setLuaInputMask(mask);
    return 0;
}

static int l_joyget(lua_State* L)
{
    lua_newtable(L);
    // PokéBot's DeSmuME compatibility layer expects these six controls in lowercase.
    // A/B/X/Y/L/R remain uppercase, matching the original script API.
    static const char* names[] = {"A","B","select","start","right","left","up","down","R","L","X","Y"};
    for (int i = 0; i < 12; i++)
    {
        lua_pushboolean(L, (gLuaInputMask & (1u << i)) == 0);
        lua_setfield(L, -2, names[i]);
    }
    return 1;
}

static int l_stylus_set(lua_State* L)
{
    luaL_checktype(L,1,LUA_TTABLE);
    bool touch = false;
    int x = 0, y = 0;
    lua_getfield(L,1,"x"); if (lua_isnumber(L,-1)) x=(int)lua_tointeger(L,-1); lua_pop(L,1);
    lua_getfield(L,1,"y"); if (lua_isnumber(L,-1)) y=(int)lua_tointeger(L,-1); lua_pop(L,1);
    lua_getfield(L,1,"touch"); touch=lua_toboolean(L,-1); lua_pop(L,1);
    if (gInstance)
    {
        if (touch) gInstance->touchScreen(x,y); else gInstance->releaseScreen();
    }
    return 0;
}

static int l_save(lua_State* L)
{
    const char* f=luaL_checkstring(L,1);
    bool ok=gInstance && gInstance->luaSaveState(QString::fromUtf8(f).toStdString());
    lua_pushboolean(L,ok); return 1;
}
static int l_load(lua_State* L)
{
    const char* f=luaL_checkstring(L,1);
    bool ok=gInstance && gInstance->luaLoadState(QString::fromUtf8(f).toStdString());
    lua_pushboolean(L,ok); return 1;
}

static int l_romhash(lua_State* L)
{
    std::string hash;
    if (gInstance && gInstance->getNDS() && gInstance->getNDS()->GetNDSCart())
    {
        auto* cart=gInstance->getNDS()->GetNDSCart();
        QByteArray rom(reinterpret_cast<const char*>(cart->GetROM()), (qsizetype)cart->GetROMLength());
        hash = QCryptographicHash::hash(rom, QCryptographicHash::Sha1).toHex().toStdString();
    }
    lua_pushstring(L,hash.c_str()); return 1;
}
static int l_romname(lua_State* L)
{
    if (gInstance && gInstance->getNDS() && gInstance->getNDS()->GetNDSCart())
        lua_pushlstring(L,gInstance->getNDS()->GetNDSCart()->GetHeader().GameTitle,12);
    else lua_pushliteral(L,"");
    return 1;
}
static int l_romserial(lua_State* L)
{
    if (gInstance && gInstance->getNDS() && gInstance->getNDS()->GetNDSCart())
        lua_pushlstring(L,gInstance->getNDS()->GetNDSCart()->GetHeader().GameCode,4);
    else lua_pushliteral(L,"");
    return 1;
}

class LuaSocketHandle
{
public:
    QTcpSocket socket;
    QByteArray pending;
};

static LuaSocketHandle* checkSocket(lua_State* L)
{
    return *(LuaSocketHandle**)luaL_checkudata(L,1,"melonDS.socket");
}
static int l_socket_connect(lua_State* L)
{
    const char* host=luaL_checkstring(L,1); quint16 port=(quint16)luaL_checkinteger(L,2);
    auto** ud=(LuaSocketHandle**)lua_newuserdatauv(L,sizeof(LuaSocketHandle*),0);
    *ud=new LuaSocketHandle();
    luaL_getmetatable(L,"melonDS.socket"); lua_setmetatable(L,-2);
    (*ud)->socket.connectToHost(QString::fromUtf8(host),port);
    if (!(*ud)->socket.waitForConnected(2000)) { delete *ud; *ud=nullptr; return luaL_error(L,"socket.connect failed"); }
    return 1;
}
static int l_socket_gc(lua_State* L) { auto** ud=(LuaSocketHandle**)luaL_checkudata(L,1,"melonDS.socket"); if(*ud){delete *ud;*ud=nullptr;} return 0; }
static int l_socket_settimeout(lua_State* L) { auto* s=checkSocket(L); double t=luaL_checknumber(L,2); if(t==0) s->socket.setReadBufferSize(1024*1024); return 0; }
static int l_socket_send(lua_State* L) { auto* s=checkSocket(L); size_t n=0; const char* d=luaL_checklstring(L,2,&n); qint64 w=s->socket.write(d,(qint64)n); s->socket.flush(); lua_pushinteger(L,(lua_Integer)w); return 1; }
static int l_socket_receive(lua_State* L)
{
    auto* s = checkSocket(L);
    s->socket.waitForReadyRead(0);
    s->pending += s->socket.readAll();

    // PokéBot's Node dashboard terminates emulator->dashboard messages with
    // NUL, but its dashboard->emulator messages are plain JSON without NUL.
    // Support both formats. For JSON, wait until a complete top-level object
    // has arrived (handling braces inside quoted strings).
    int idx = s->pending.indexOf('\0');
    int consumed = 1;

    if (idx < 0)
    {
        int depth = 0;
        bool inString = false;
        bool escaped = false;

        for (int i = 0; i < s->pending.size(); ++i)
        {
            const char c = s->pending.at(i);

            if (inString)
            {
                if (escaped)
                    escaped = false;
                else if (c == '\\')
                    escaped = true;
                else if (c == '"')
                    inString = false;
                continue;
            }

            if (c == '"')
                inString = true;
            else if (c == '{')
                ++depth;
            else if (c == '}')
            {
                --depth;
                if (depth == 0)
                {
                    // Include the closing brace in the JSON message.
                    // idx is the number of bytes to keep, so use i+1.
                    idx = i + 1;
                    consumed = 0;
                    break;
                }
            }
        }
    }

    if (idx >= 0)
    {
        QByteArray msg = s->pending.left(idx);
        s->pending.remove(0, idx + consumed);
        lua_pushnil(L);
        lua_pushnil(L);
        lua_pushlstring(L, msg.constData(), msg.size());
        return 3;
    }

    lua_pushnil(L);
    lua_pushliteral(L, "timeout");
    lua_pushliteral(L, "");
    return 3;
}

static int l_print(lua_State* L)
{
    QStringList vals;
    int n=lua_gettop(L); for(int i=1;i<=n;i++) vals << QString::fromUtf8(luaL_tolstring(L,i,nullptr)); lua_pop(L,1);
    message(gInstance,vals.join("\t")); return 0;
}

static void preload(lua_State* L, const char* name, lua_CFunction openf)
{
    lua_getglobal(L,"package"); lua_getfield(L,-1,"preload"); lua_pushcfunction(L,openf); lua_setfield(L,-2,name); lua_pop(L,2);
}

static int open_socket(lua_State* L)
{
    luaL_newmetatable(L,"melonDS.socket");
    lua_pushcfunction(L,l_socket_gc); lua_setfield(L,-2,"__gc");
    lua_newtable(L);
    lua_pushcfunction(L,l_socket_receive); lua_setfield(L,-2,"receive");
    lua_pushcfunction(L,l_socket_send); lua_setfield(L,-2,"send");
    lua_pushcfunction(L,l_socket_settimeout); lua_setfield(L,-2,"settimeout");
    lua_setfield(L,-2,"__index"); lua_pop(L,1);
    lua_newtable(L); lua_pushcfunction(L,l_socket_connect); lua_setfield(L,-2,"connect"); return 1;
}
static int open_bit(lua_State* L)
{
    lua_newtable(L);
    lua_pushcfunction(L,[](lua_State*L){lua_pushinteger(L,(lua_Integer)((uint32_t)luaL_checkinteger(L,1)&(uint32_t)luaL_checkinteger(L,2)));return 1;});lua_setfield(L,-2,"band");
    lua_pushcfunction(L,[](lua_State*L){lua_pushinteger(L,(lua_Integer)((uint32_t)luaL_checkinteger(L,1)|(uint32_t)luaL_checkinteger(L,2)));return 1;});lua_setfield(L,-2,"bor");
    lua_pushcfunction(L,[](lua_State*L){lua_pushinteger(L,(lua_Integer)((uint32_t)luaL_checkinteger(L,1)^(uint32_t)luaL_checkinteger(L,2)));return 1;});lua_setfield(L,-2,"bxor");
    lua_pushcfunction(L,[](lua_State*L){lua_pushinteger(L,(lua_Integer)~(uint32_t)luaL_checkinteger(L,1));return 1;});lua_setfield(L,-2,"bnot");
    lua_pushcfunction(L,[](lua_State*L){lua_pushinteger(L,(lua_Integer)((uint32_t)luaL_checkinteger(L,1) << luaL_checkinteger(L,2)));return 1;});lua_setfield(L,-2,"lshift");
    lua_pushcfunction(L,[](lua_State*L){lua_pushinteger(L,(lua_Integer)((uint32_t)luaL_checkinteger(L,1) >> luaL_checkinteger(L,2)));return 1;});lua_setfield(L,-2,"rshift");
    lua_pushcfunction(L,[](lua_State*L){lua_pushinteger(L,(lua_Integer)((int32_t)luaL_checkinteger(L,1) >> luaL_checkinteger(L,2)));return 1;});lua_setfield(L,-2,"arshift");
    return 1;
}

static void registerApis(lua_State* L)
{
    lua_newtable(L); lua_pushcfunction(L,l_readbyte); lua_setfield(L,-2,"readbyte"); lua_pushcfunction(L,l_readwordu); lua_setfield(L,-2,"readwordunsigned"); lua_pushcfunction(L,l_readdwordu); lua_setfield(L,-2,"readdwordunsigned"); lua_pushcfunction(L,l_readwords); lua_setfield(L,-2,"readword"); lua_pushcfunction(L,l_readdwords); lua_setfield(L,-2,"readdword"); lua_pushcfunction(L,l_writebyte); lua_setfield(L,-2,"writebyte"); lua_pushcfunction(L,l_writeword); lua_setfield(L,-2,"writeword"); lua_pushcfunction(L,l_writedword); lua_setfield(L,-2,"writedword"); lua_setglobal(L,"memory");
    lua_newtable(L); lua_pushcfunction(L,l_joyset); lua_setfield(L,-2,"set"); lua_pushcfunction(L,l_joyget); lua_setfield(L,-2,"get"); lua_setglobal(L,"joypad");
    lua_newtable(L); lua_pushcfunction(L,l_stylus_set); lua_setfield(L,-2,"set"); lua_setglobal(L,"stylus");
    lua_newtable(L); lua_pushcfunction(L,l_frameadvance); lua_setfield(L,-2,"frameadvance"); lua_pushcfunction(L,l_yield); lua_setfield(L,-2,"yield"); lua_pushcfunction(L,l_framecount); lua_setfield(L,-2,"framecount"); lua_pushcfunction(L,l_emulating); lua_setfield(L,-2,"emulating"); lua_pushcfunction(L,l_reset); lua_setfield(L,-2,"reset"); lua_pushcfunction(L,l_emulateframeinvisible); lua_setfield(L,-2,"emulateframeinvisible"); lua_setglobal(L,"emu");
    lua_newtable(L); lua_pushcfunction(L,l_romhash); lua_setfield(L,-2,"getromhash"); lua_pushcfunction(L,l_romname); lua_setfield(L,-2,"getromname"); lua_pushcfunction(L,l_romserial); lua_setfield(L,-2,"getromserial"); lua_setglobal(L,"gameinfo");
    lua_newtable(L); lua_pushcfunction(L,l_save); lua_setfield(L,-2,"save"); lua_pushcfunction(L,l_load); lua_setfield(L,-2,"load"); lua_setglobal(L,"savestate");
    lua_newtable(L); lua_pushcfunction(L,[](lua_State*L){ gFocusModeActive = true; return 0; }); lua_setfield(L,-2,"clear"); lua_setglobal(L,"sound");
    lua_newtable(L); lua_pushcfunction(L,[](lua_State*L){return 0;}); lua_setfield(L,-2,"clear"); lua_setglobal(L,"console");
    lua_pushcfunction(L,l_print); lua_setglobal(L,"print");
    preload(L,"lua\\modules\\socket",open_socket);
    luaL_requiref(L,"bit",open_bit,1);
    lua_pop(L,1);
}

void setInstance(EmuInstance* instance) { gInstance=instance; }

void queueScript(EmuInstance* instance, const QString& filename)
{
    gInstance=instance;
    if (gMain) stop();
    gPending=filename;
    gScriptFile=filename;
}

static bool loadPending()
{
    if (gPending.isEmpty() || !gInstance) return false;
    QString file=gPending; gPending.clear();
    stop();
    QFileInfo info(file); if(!info.exists()){message(gInstance,"Lua file not found: "+file);return false;}
    gScriptDir=info.absolutePath();
    std::error_code ec; std::filesystem::current_path(gScriptDir.toStdString(),ec);
    gMain=luaL_newstate(); luaL_openlibs(gMain); registerApis(gMain);
    lua_pushliteral(gMain,"DeSmuME"); lua_setglobal(gMain,"_EMU");
    if(luaL_loadfile(gMain,file.toLocal8Bit().constData())!=LUA_OK){message(gInstance,QString::fromUtf8(lua_tostring(gMain,-1)));stop();return false;}
    gThread=lua_newthread(gMain); gThreadRef=luaL_ref(gMain,LUA_REGISTRYINDEX); lua_xmove(gMain,gThread,1);
    gStop=false; gPaused=false; gRunning=true; gFrame=0;
    gScriptFile=file;
    return true;
}

void update(EmuInstance* instance)
{
    if(instance) gInstance=instance;
    if(!gMain) loadPending();
    if(!gThread || !gRunning || gPaused) return;
    int nres=0;
    int rc=lua_resume(gThread,nullptr,0,&nres);
    if(rc==LUA_YIELD) return;
    if(rc==LUA_OK){ gRunning=false; message(gInstance,"Lua script finished."); stop(); return; }
    const char* err=lua_tostring(gThread,-1); message(gInstance,err?QString::fromUtf8(err):QStringLiteral("Lua runtime error")); stop();
}

void onFrameComplete() { ++gFrame; }

bool isFocusModeActive()
{
    return gFocusModeActive;
}

void stop()
{
    if(gInstance){ gInstance->clearLuaInputOverride(); gInstance->releaseScreen(); }
    if(gThread && gMain && gThreadRef!=LUA_NOREF) luaL_unref(gMain,LUA_REGISTRYINDEX,gThreadRef);
    gThread=nullptr; gThreadRef=LUA_NOREF;
    if(gMain){ lua_close(gMain); gMain=nullptr; }
    if(gSocket){gSocket->disconnectFromHost();delete gSocket;gSocket=nullptr;}
    gSocketBuffer.clear(); gRunning=false; gStop=false; gFocusModeActive=false;
}

void pause()
{
    if (!gRunning) return;
    gPaused = true;
    if (gInstance) gInstance->clearLuaInputOverride();
    message(gInstance, "Lua bot paused.");
}

void resume()
{
    if (!gRunning) return;
    gPaused = false;
    message(gInstance, "Lua bot resumed.");
}

void restart()
{
    if (gScriptFile.isEmpty()) return;
    QString file = gScriptFile;
    stop();
    gPending = file;
    gScriptFile = file;
}

bool isRunning(){return gRunning;}
bool isPaused(){return gPaused;}
}
