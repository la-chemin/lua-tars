#include <iostream>

#include <initializer_list>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <cstring>

#include <arpa/inet.h>

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
#define LTARS_INT8 2
#define LTARS_UINT8 3
#define LTARS_INT16 4
#define LTARS_UINT16 5
#define LTARS_INT32 6
#define LTARS_UINT32 7
#define LTARS_INT64 8
#define LTARS_UINT64 9
#define LTARS_FLOAT 10
#define LTARS_DOUBLE 11
#define LTARS_STRING 12
#define LTARS_MAP 13
#define LTARS_LIST 14
#define LTARS_TYPE_MAX 15

// 最长字符串长度
#define _MAX_STR_LEN (100 * 1024 * 1024)

// 写缓存
struct WriteBuffer : public std::string {
    // 基础数据序列化
    template <typename T>
    typename std::enable_if<std::is_arithmetic<T>::value>::type push(const T& value)
    {
        append((const char*)&value, sizeof(T));
    }

    // 写入字段的头部信息
    inline void header(uint8_t type, uint8_t tag)
    {
        if (tag < 15) {
            push_back((tag << 4) | type);
        }
        else {
            push_back(0xF0 | type);
            push_back(tag);
        }
    }
    void write(bool b, uint8_t tag) { write(tag, (int8_t)b); }
    void write(int8_t n, uint8_t tag)
    {
        if (n == 0) {
            header(TarsHeadeZeroTag, tag);
        }
        else {
            header(TarsHeadeChar, tag);
            push_back(n);
        }
    }
    void write(uint8_t n, uint8_t tag) { write((int16_t)n, tag); }
    void write(int16_t n, uint8_t tag)
    {
        if (n >= -128 && n <= 127) {
            write((int8_t)n, tag);
        }
        else {
            header(TarsHeadeShort, tag);
            n = htons(n);
            push(n);
        }
    }
    void write(uint16_t n, uint8_t tag) { write((int32_t)n, tag); }
    void write(int32_t n, uint8_t tag)
    {
        if (n >= -32768 && n <= 32767) {
            write((int16_t)n, tag);
        }
        else {
            header(TarsHeadeInt32, tag);
            n = htonl(n);
            push(n);
        }
    }
    void write(uint32_t n, uint8_t tag) { write((int64_t)n, tag); }
    void write(int64_t n, uint8_t tag)
    {
        if (n >= (-2147483647 - 1) && n <= 2147483647) {
            write((int32_t)n, tag);
        }
        else {
            header(TarsHeadeInt64, tag);
            n = htobe64(n);
            push(n);
        }
    }

    // 写入字符串
    void writeString(const char* buf, size_t len, uint8_t tag)
    {
        if (len > 255) {
            header(TarsHeadeString4, tag);
            uint32_t n = htonl(len);
            push(n);
            append(buf, len);
        }
        else {
            header(TarsHeadeString1, tag);
            uint8_t n = len;
            push(n);
            append(buf, len);
        }
    }
    // 写入字符数组
    void writeCharArray(const char* buf, size_t len, uint8_t tag)
    {
        header(TarsHeadeSimpleList, tag);
        header(TarsHeadeChar, 0);
        write((int64_t)len, 0);
        append(buf, len);
    }
};

// 协议的字段
struct Field {
    uint8_t tag;            // 字段的序号
    bool forced;            // 是否必须写数据报
    uint16_t type1;         // 类型
    uint16_t type2, type3;  // 关联类型
    int def;                // 默认值
};

// 协议上下文
struct Context {
    int num;          // 类型数量
    Field fields[0];  // 成员数组

    int write(lua_State* L, WriteBuffer& buffer, int type1, int tag, int ltype, int def, bool forced) const;
    int write_field(lua_State* L, WriteBuffer& buffer, uint16_t row, uint8_t ltype) const;
    int encodeStruct(lua_State* L, WriteBuffer& buffer, uint16_t row, uint8_t ltype) const;
    int encodeList(lua_State* L, WriteBuffer& buffer, uint16_t tag, int type, uint8_t ltype) const;
    int encodeMap(lua_State* L, WriteBuffer& buffer, uint16_t tag, int type1, int type2, uint8_t ltype) const;
};

int Context::write(lua_State* L, WriteBuffer& buffer, int type1, int tag, int ltype, int def, bool forced) const
{
    switch (type1) {
        case LTARS_BOOL: {
            if (LUA_TBOOLEAN == ltype) {
                bool b = lua_toboolean(L, -1);
                if ((bool)def != b) {
                    buffer.write(b, tag);
                    // std::cout << "写入了一个布尔" << b << std::endl;
                }
            }
            else if (LUA_TNIL == ltype) {
                if (forced) {
                    bool b = def;
                    buffer.write(b, tag);
                    // std::cout << "写入了一个布尔" << b << std::endl;
                }
            }
            else {
                luaL_error(L, "tag %d 需要一个bool类型，实际却是 %s", tag, lua_typename(L, ltype));
            }
        } break;
        case LTARS_INT8:
        case LTARS_UINT8:
        case LTARS_INT16:
        case LTARS_UINT16:
        case LTARS_INT32:
        case LTARS_UINT32:
        case LTARS_INT64:
        case LTARS_UINT64: {
            if (LUA_TNUMBER == ltype) {
                int64_t n = lua_tointeger(L, -1);
                if (def != n) {
                    buffer.write(n, tag);
                    // std::cout << "写入了一个整数" << n << std::endl;
                }
            }
            else if (LUA_TNIL == ltype) {
                if (forced) {
                    int64_t n = def;
                    buffer.write(n, tag);
                    // std::cout << "写入了一个整数" << n << std::endl;
                }
            }
            else {
                luaL_error(L, "tag %d 需要一个number类型，实际却是 %s", tag, lua_typename(L, ltype));
            }
        } break;
        case LTARS_FLOAT:
        case LTARS_DOUBLE: {
            luaL_error(L, "暂时不支持浮点数");
        } break;
        case LTARS_STRING: {
            if (LUA_TSTRING == ltype) {
                if (def != 0) {
                    lua_rawgeti(L, 4, def);
                    bool eq = lua_rawequal(L, -1, -2);
                    lua_pop(L, 1);
                    if (eq) {
                        // 字符串和默认值相等不写处理
                        break;
                    }
                }
                size_t sz = 0;
                const char* s = lua_tolstring(L, -1, &sz);
                buffer.writeString(s, sz, tag);
            }
            else if (LUA_TNIL == ltype) {
                if (forced) {
                    if (0 == def) {
                        buffer.writeString((const char*)"", 0, tag);
                    }
                    else {
                        ltype = lua_rawgeti(L, 4, def);
                        if (LUA_TSTRING != ltype) {
                            luaL_error(L, "tag %d 字符串的默认值不是字符串，而是", tag, lua_typename(L, ltype));
                        }
                        size_t sz = 0;
                        const char* s = lua_tolstring(L, -1, &sz);
                        buffer.writeString(s, sz, tag);
                        lua_pop(L, 1);
                    }
                }
            }
            else {
                luaL_error(L, "tag %d 需要一个string类型，实际却是 %s", tag, lua_typename(L, ltype));
            }
        } break;
        default: {
            return type1;
        }
    }
    return 0;
}

int Context::write_field(lua_State* L, WriteBuffer& buffer, uint16_t row, uint8_t ltype) const
{
    auto& field = fields[row];
    return write(L, buffer, field.type1, field.tag, ltype, field.def, field.forced);
}

int Context::encodeStruct(lua_State* L, WriteBuffer& buffer, uint16_t row, uint8_t ltype) const
{
    // lua_rawgeti(L, 4, row);
    // std::cout << "编码:" << (int)row << ", " << lua_tostring(L, -1) << std::endl;
    // lua_pop(L, 1);

    if (LUA_TNIL == ltype) {
        // 空类型不处理
        return 0;
    }
    if (ltype != LUA_TTABLE) {
        // 只支持Table参数
        lua_rawgeti(L, 4, row);
        luaL_error(L, "%s 需要一个table，实际传递了%s", lua_tostring(L, -1), lua_typename(L, ltype));
    }
    // lua_rawgeti(L, 4, row);
    // std::cout << row << " = " << lua_tostring(L, -1) << std::endl;
    // lua_pop(L, 1);
    do {
        // std::cout<< "写入" << (int)row << ", 类型" << (int)fields[row].tag << std::endl;
        // 用名称从数据表中字段数据
        lua_rawgeti(L, 4, row);
        ltype = lua_rawget(L, -2);
        if (0 != write_field(L, buffer, row, ltype)) {
            if (LTARS_LIST == fields[row].type1) {
                encodeList(L, buffer, fields[row].tag, fields[row].type2, ltype);
            }
            else if (LTARS_MAP == fields[row].type1) {
                // std::cout << " 写map: " << lua_typename(L, ltype) << " row " << row << std::endl;
                encodeMap(L, buffer, fields[row].tag, fields[row].type2, fields[row].type3, ltype);
            }
            else {
                if (fields[row].type1 < 100) {
                    luaL_error(L, "未预料的类型 row = %d, type1 =  %d, type2 = %d", fields[row].type1, fields[row].type2);
                }
                // 编码结构体
                // std::cout << "编码结构体:" << row << ", 类型=" << (int)fields[row].type1 << std::endl;
                buffer.header(TarsHeadeStructBegin, fields[row].tag);
                encodeStruct(L, buffer, fields[row].type1 - 100, ltype);
                buffer.header(TarsHeadeStructEnd, 0);
            }
        }
        lua_pop(L, 1);
        row += 1;
    } while (row < num && fields[row].tag != 0);
    // std::cout << "encode struct, size = " << buffer.size() << std::endl;
    return 0;
}

int Context::encodeList(lua_State* L, WriteBuffer& buffer, uint16_t tag, int type, uint8_t ltype) const
{
    if (LTARS_INT8 == type) {
        // 需要写SimpleList
        if (LUA_TSTRING != ltype) {
            luaL_error(L, "tag %d 需要一个字符串，实际却传递了%s", tag, lua_typename(L, ltype));
        }
        size_t sz = 0;
        const char* s = lua_tolstring(L, -1, &sz);
        if (sz > 0) {
            buffer.writeCharArray(s, sz, tag);
        }
        return 0;
    }
    int32_t len = lua_rawlen(L, -1);
    if (len < 1) {
        // 空数组不写
        return 0;
    }
    // 写入数组头部和序号、写入数组长度
    buffer.header(LTARS_LIST, tag);
    buffer.write((int64_t)len, 0);
    for (int i = 1, tail = len + 1; i < tail; ++i) {
        ltype = lua_rawgeti(L, -1, i);
        if (0 != write(L, buffer, type, 0, ltype, 0, true)) {
            buffer.header(TarsHeadeStructBegin, 0);
            encodeStruct(L, buffer, type, ltype);
            buffer.header(TarsHeadeStructEnd, 0);
        }
        lua_pop(L, 1);
    }
    return 0;
}

int encodeMap(lua_State* L, WriteBuffer& buffer, uint16_t tag, int type1, int type2, uint8_t ltype) const
{
    if (ltype != LUA_TTABLE) {
        lua_rawgeti(L, 4, row);
        luaL_error(L, "%s 需要一个table，实际传递了%s", lua_tostring(L, -1), lua_typename(L, ltype));
    }
    // std::cout << "map: " << std::endl;
    // 先计算map的长度
    size_t len = 0;
    lua_pushnil(L);
    while (0 != lua_next(L, -2)) {
        len += 1;
        lua_pop(L, 1);
    }
    // std::cout << "map: " << len << std::endl;
    if (len > 0) {
        buffer.header(TarsHeadeMap, fields[row].tag);
        buffer.write((int64_t)len, 0);

        lua_pushnil(L);
        while (0 != lua_next(L, -2)) {
            // 先写key
            ltype = lua_type(L, -2);
            std::cout << "写key " << fields[row].type2 << std::endl;
            if (0 != write(L, buffer, fields[row].type2, 0, ltype, 0, true)) {
                luaL_error(L, "map只支持基础类型的key");
            }
            // 再写value
            ltype = lua_type(L, -1);
            std::cout << "写value " << fields[row].type3 << ", 类型 " << lua_typename(L, ltype) << std::endl;
            if (0 != write(L, buffer, fields[row].type3, 0, ltype, 0, true)) {
                buffer.header(TarsHeadeStructBegin, 0);
                encode(L, buffer, __MODE_STRUCT, fields[row].type3, ltype);
                buffer.header(TarsHeadeStructEnd, 0);
            }
            lua_pop(L, 1);
        }
    }
}

// 上下文的大小
#define GET_ENV_SIZE(n) ((n) * sizeof(Field) + sizeof(Context))

// clang-format off
#define LTARS_ENUM(Type) {#Type, LTARS_##Type}
// clang-format on

// lua函数：创建tars上下文
static int context_create(lua_State* L)
{
    if (2 != lua_gettop(L)) {
        luaL_error(L, "只允许传递2两个参数，实际传递了%d个", lua_gettop(L));
    }
    luaL_checktype(L, 1, LUA_TTABLE);
    luaL_checktype(L, 2, LUA_TTABLE);

    // 根据参数中表的大小分配空间
    int n = lua_rawlen(L, 1);
    size_t sz = GET_ENV_SIZE(n);
    Context* ctx = (Context*)lua_newuserdata(L, sz);  // 3 上下文
    ctx->num = n;
    Field* p = ctx->fields;
    lua_newtable(L);    // 4 上下文的元表
    int freei = n + 1;  // 没有被使用的数组下标
    for (int i = 1; i <= n; ++i, ++p) {
        int t = lua_rawgeti(L, 1, i);
        if (LUA_TTABLE != t) {
            luaL_error(L, "错误的表元素[%d], 类型:%s", i, lua_typename(L, t));
        }
        // 字段序号
        lua_getfield(L, -1, "tag"), p->tag = lua_tointeger(L, -1), lua_pop(L, 1);
        // 是否强制
        lua_getfield(L, -1, "forced"), p->forced = lua_toboolean(L, -1), lua_pop(L, 1);
        // 主类型
        lua_getfield(L, -1, "type1"), p->type1 = lua_tointeger(L, -1), lua_pop(L, 1);
        // 类型2
        lua_getfield(L, -1, "type2"), p->type2 = lua_tointeger(L, -1), lua_pop(L, 1);
        // 类型3
        lua_getfield(L, -1, "type3"), p->type3 = lua_tointeger(L, -1), lua_pop(L, 1);
        // 设置名称 上下文表[下标] = 名称
        lua_getfield(L, -1, "name"), lua_rawseti(L, 4, p - ctx->fields);

        switch (p->type1) {
            case LTARS_BOOL:
            case LTARS_INT8:
            case LTARS_UINT8:
            case LTARS_INT16:
            case LTARS_UINT16:
            case LTARS_INT32:
            case LTARS_UINT32:
            case LTARS_INT64:
            case LTARS_UINT64: {
                lua_getfield(L, -1, "default"), p->def = lua_tointeger(L, -1), lua_pop(L, 1);
            } break;
            case LTARS_FLOAT:
            case LTARS_DOUBLE: {
                // lua_getfield(L, -1, "default"), p->def.f = lua_tonumber(L, -1), lua_pop(L, 1);
            } break;
            case LTARS_STRING: {
                int tt = lua_getfield(L, -1, "default");
                if (LUA_TNONE == tt || LUA_TNIL == tt) {
                    // 没有给字符串的默认值
                    p->def = 0;
                }
                else if (LUA_TSTRING == tt) {
                    lua_pushvalue(L, -1);
                    lua_rawseti(L, 4, freei);
                    // 已经占用了的下标
                    p->def = freei, freei += 1;
                }
                lua_pop(L, 1);
            } break;
        }

        lua_pop(L, 1);
    }

    // 设置元表
    lua_setmetatable(L, 3);

    return 1;
}

// lua函数：编码结构体
// tars.encode(Context, StructId, Table)
static int context_encode(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TUSERDATA);
    Context* ctx = (Context*)lua_touserdata(L, 1);
    int id = luaL_checkinteger(L, 2) - 100;
    if ((size_t)id >= (size_t)ctx->num) {
        luaL_error(L, "无效的结构体 = %d, 最大值 = %d", id, ctx->num);
    }
    luaL_checktype(L, 3, LUA_TTABLE);
    lua_settop(L, 3);
    lua_getmetatable(L, 1);  // 4 = 元表
    lua_pushvalue(L, 3);     // -1 = 3

    WriteBuffer buff;
    ctx->encodeStruct(L, buff, id, LUA_TTABLE);

    lua_pushlstring(L, buff.data(), buff.size());
    return 1;
}

// lua函数：编码数组
// tars.encodeList(Context, Type, Table)
static int context_encodelist(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TUSERDATA);
    Context* ctx = (Context*)lua_touserdata(L, 1);
    int id = luaL_checkinteger(L, 2);
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
