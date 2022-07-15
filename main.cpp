#include <iostream>
#include <sstream>
#include <vector>

#include <initializer_list>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

// 编码中使用的字段类型
#define TarsHeadeChar 0
#define TarsHeadeShort 1
#define TarsHeadeInt32 2
#define TarsHeadeInt64 3
#define TarsHeadeFloat 4
#define TarsHeadeDouble 5
#define TarsHeadeString1 6
#define TarsHeadeString4 7
#define TarsHeadeMap 8
#define TarsHeadeList 9
#define TarsHeadeStructBegin 10
#define TarsHeadeStructEnd 11
#define TarsHeadeZeroTag 12
#define TarsHeadeSimpleList 13

// 定义中的字段类型
#define LTARS_BOOL 1
#define LTARS_INT 2
#define LTARS_UINT 3
#define LTARS_LONG 4
#define LTARS_ULONG 5
#define LTARS_FLOAT 6
#define LTARS_DOUBLE 7
#define LTARS_STRING 8
#define LTARS_MAP 9
#define LTARS_LIST 10
#define LTARS_TYPE_MAX 11

// 协议的字段
struct Field {
    uint8_t tag;       // 字段的序号
    bool forced;       // 是否必须写数据报
    int type1;         // 类型
    int type2, type3;  // 关联类型
    union {
        double f;
        const char* s;
        long long l;
    } def;
};

// 协议上下文
struct Context {
    int num;          // 类型数量
    Field fields[0];  // 成员数组
};

// 上下文的大小
#define GET_ENV_SIZE(n) ((n) * sizeof(Field) + sizeof(Context))

// clang-format off
#define LTARS_ENUM(Type) {#Type, LTARS_##Type}
// clang-format on

// 写入类型头部
#define WRITE_HEADER(buf, tag, type)         \
    if (tag < 15) {                          \
        buf.emplace_back(type + (tag << 4)); \
    }                                        \
    else {                                   \
        buf.emplace_back(type + 0xF0);       \
        buf.emplace_back(tag);               \
    }

// 整形数字写入
#define WRITE_INTEGER(buf, n, tag)                          \
    do {                                                    \
        if (0 == n) {                                       \
            WRITE_HEADER(buf, TarsHeadeZeroTag, tag);       \
            break;                                          \
        }                                                   \
        if (n >= (-2147483647 - 1) && n <= 2147483647) {    \
            if (n >= (-32768) && n <= 32767) {              \
                if (n >= (-128) && n <= 127) {              \
                    WRITE_HEADER(buf, tag, TarsHeadeChar);  \
                    buf.emplace_back(n);                    \
                }                                           \
                else {                                      \
                    WRITE_HEADER(buf, tag, TarsHeadeShort); \
                    auto sz = buf.size();                   \
                    buf.resize(sz + sizeof(int16_t));       \
                    *(int16_t*)&buf[sz] = (n);              \
                }                                           \
            }                                               \
            else {                                          \
                WRITE_HEADER(buf, tag, TarsHeadeInt32);     \
                auto sz = buf.size();                       \
                buf.resize(sz + sizeof(int32_t));           \
                *(int32_t*)&buf[sz] = (n);                  \
            }                                               \
        }                                                   \
        else {                                              \
            WRITE_HEADER(buf, tag, TarsHeadeInt64);         \
            auto sz = buf.size();                           \
            buf.resize(sz + sizeof(int64_t));               \
            *(int64_t*)&buf[sz] = (n);                      \
        }                                                   \
    } while (false);

// lua函数：创建tars上下文
static int context_create(lua_State* L)
{
    if (2 != lua_gettop(L)) {
        luaL_error(L, "只允许传递2两个参数，实际传递了%d个", lua_gettop(L));
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);

    int n = lua_rawlen(L, 1);
    size_t sz = GET_ENV_SIZE(n);
    Context* ctx = (Context*)lua_newuserdata(L, sz);  // 3
    ctx->num = n;
    Field* p = ctx->fields;
    lua_newtable(L);  // 4
    for (int i = 1; i <= n; ++i, ++p) {
        int t = lua_rawgeti(L, 1, i);
        if (LUA_TTABLE != t) {
            luaL_error(L, "错误的表元素[%d], 类型:%s", i, lua_typename(L, t));
        }
        lua_getfield(L, -1, "tag"), p->tag = lua_tointeger(L, -1), lua_pop(L, 1);
        lua_getfield(L, -1, "forced"), p->forced = lua_toboolean(L, -1), lua_pop(L, 1);
        lua_getfield(L, -1, "type1"), p->type1 = lua_tointeger(L, -1), lua_pop(L, 1);
        lua_getfield(L, -1, "type2"), p->type2 = lua_tointeger(L, -1), lua_pop(L, 1);
        lua_getfield(L, -1, "type3"), p->type3 = lua_tointeger(L, -1), lua_pop(L, 1);

        switch (p->type1) {
            case LTARS_BOOL:
            case LTARS_INT:
            case LTARS_UINT:
            case LTARS_LONG:
            case LTARS_ULONG: {
                lua_getfield(L, -1, "default"), p->def.l = lua_tointeger(L, -1), lua_pop(L, 1);
            } break;
            case LTARS_FLOAT:
            case LTARS_DOUBLE: {
                lua_getfield(L, -1, "default"), p->def.f = lua_tonumber(L, -1), lua_pop(L, 1);
            } break;
            case LTARS_STRING: {
                int tt = lua_getfield(L, -1, "default");
                if (LUA_TNONE == tt || LUA_TNIL == tt) {
                    p->def.s = nullptr;
                }
                else if (LUA_TSTRING == tt) {
                    size_t sz = 0;
                    p->def.s = lua_tolstring(L, -1, &sz);
                    if (sz < 1) {
                        // 空字符串不处理
                        p->def.s = nullptr;
                    }
                    else {
                        // 将字符串复制一份，避免指针失效
                        lua_pushvalue(L, -1);
                        lua_rawseti(L, 4, i);
                    }
                }
                lua_pop(L, 1);
            } break;
        }
    }

    return 1;
}

// lua函数：编码结构体
static int context_encode(lua_State* L)
{
    return 1;
}

// lua打开tars模块
static int luaopen_tars(lua_State* L)
{
    luaL_Reg reg[]{
        {"create", context_create},
        {"encode", context_encode},
        {nullptr, nullptr},
    };
    luaL_newlib(L, reg);
    return 1;
}

int main(int argc, const char* argv[])
{
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    luaL_requiref(L, "tars", luaopen_tars, 0);

    if (luaL_dofile(L, "run.lua")) {
        std::cerr << lua_tostring(L, -1) << std::endl;
    }

    lua_close(L);
    return 0;
}
