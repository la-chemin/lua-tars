#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include <stdbool.h>
#include <string.h>

#include <arpa/inet.h>
#include <sys/types.h>

#include <inttypes.h>

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
#define LUATARS_BOOL 1
#define LUATARS_INT8 2
#define LUATARS_UINT8 3
#define LUATARS_INT16 4
#define LUATARS_UINT16 5
#define LUATARS_INT32 6
#define LUATARS_UINT32 7
#define LUATARS_INT64 8
#define LUATARS_FLOAT 9
#define LUATARS_DOUBLE 10
#define LUATARS_STRING 11
#define LUATARS_MAP 12
#define LUATARS_LIST 13
#define LUATARS_TYPE_MAX 14

// 最长字符串长度
#define _MAX_STR_LEN (100 * 1024 * 1024)

// 默认值联合体
static union default_value {
    lua_Integer integer;
    lua_Number number;
} def_zero = {0};

// tars字段的定义
struct tars_field {
    uint8_t tag;            // 序号字段
    bool forced;            // 是否强制写入
    uint32_t type1;         // 主类型
    uint32_t type2, type3;  // 补充类型2,3
                            // 名称字符串通过元表的附加子表查询
    union default_value def;
};

// 所有类型的上下文
struct tars_context {
    size_t n;
    struct tars_field fields[0];
};

#define _ENUM_CASE(Enum, Len)       \
    case (Enum):                    \
        if (Len) {                  \
            *(Len) = sizeof(#Enum); \
        }                           \
        return (#Enum);

// tars类型的名称
static const char* _tars_type_name(uint8_t type, size_t* len)
{
    switch (type) {
        _ENUM_CASE(TarsHeadeChar, len);
        _ENUM_CASE(TarsHeadeShort, len);
        _ENUM_CASE(TarsHeadeInt32, len);
        _ENUM_CASE(TarsHeadeInt64, len);
        _ENUM_CASE(TarsHeadeFloat, len);
        _ENUM_CASE(TarsHeadeDouble, len);
        _ENUM_CASE(TarsHeadeString1, len);
        _ENUM_CASE(TarsHeadeString4, len);
        _ENUM_CASE(TarsHeadeMap, len);
        _ENUM_CASE(TarsHeadeList, len);
        _ENUM_CASE(TarsHeadeStructBegin, len);
        _ENUM_CASE(TarsHeadeStructEnd, len);
        _ENUM_CASE(TarsHeadeZeroTag, len);
        _ENUM_CASE(TarsHeadeSimpleList, len);
    }
    return "InvalidHeade";
}

// 宏函数封装一下
#define tars_type_name(Type) _tars_type_name((Type), NULL)

// 列表、字典元表的id
static const void *list_mt = &list_mt, *map_mt = &map_mt;

static inline void write_header(  // 写入头部
    luaL_Buffer* B,
    uint8_t tag,
    uint8_t type)
{
    if (tag < 15u) {
        luaL_addchar(B, (tag << 4) | type);
    }
    else {
        luaL_addchar(B, 0xF0 | type);
        luaL_addchar(B, tag);
    }
}

static inline void write_int8(  // 写入一个字节
    luaL_Buffer* B,
    uint8_t tag,
    int8_t n)
{
    if (n == 0) {
        write_header(B, tag, TarsHeadeZeroTag);
    }
    else {
        write_header(B, tag, TarsHeadeChar);
        luaL_addchar(B, n);
    }
}

static inline void write_int16(  // 写入短整型
    luaL_Buffer* B,
    uint8_t tag,
    int16_t n)
{
    if (n >= (INT8_MIN) && n <= (INT8_MAX)) {
        write_int8(B, tag, n);
    }
    else {
        write_header(B, tag, TarsHeadeShort);
        n = htons(n);
        luaL_addlstring(B, (const char*)&n, sizeof n);
    }
}

static inline void write_int32(  // 写入整形
    luaL_Buffer* B,
    uint8_t tag,
    int32_t n)
{
    if (n >= (INT16_MIN) && n <= (INT16_MAX)) {
        write_int16(B, tag, n);
    }
    else {
        write_header(B, tag, TarsHeadeInt32);
        n = htonl(n);
        luaL_addlstring(B, (const char*)&n, sizeof n);
    }
}

static inline void write_int64(  // 写入长整形
    luaL_Buffer* B,
    uint8_t tag,
    int64_t n)
{
    if (n >= (INT32_MIN) && n <= (INT32_MAX)) {
        write_int32(B, tag, n);
    }
    else {
        write_header(B, tag, TarsHeadeInt64);
        n = htobe64(n);
        luaL_addlstring(B, (const char*)&n, sizeof n);
    }
}

static int write_basic(  // 写入基础类型
    lua_State* L,
    luaL_Buffer* B,
    uint8_t tag,
    uint32_t type,
    bool forced,
    union default_value def)
{
    int ltype = lua_type(L, -1);
    if (LUA_TNIL == ltype && !forced) {
        // printf("数据不存在，不要求强制写入，tag = %d\n", tag);
        return 0;  // 没有要求强制写
    }
    switch (type) {
        case LUATARS_BOOL: {
            if (LUA_TNIL == ltype) {  // 强制写入默认值
                write_int8(B, tag, def.integer ? 1 : 0);
            }
            else if (LUA_TBOOLEAN == ltype) {
                int b = lua_toboolean(L, -1) ? 1 : 0;
                if (b != def.integer || forced) {
                    write_int8(B, tag, b);
                }
            }
            else {
                luaL_error(L, "tag %d require a bool, got '%s'", tag, lua_typename(L, ltype));
            }
        } break;
        case LUATARS_INT8: {
            if (LUA_TNIL == ltype) {  // 强制写入默认值
                write_int8(B, tag, def.integer);
            }
            else {
                int isnum = 0;
                lua_Integer n = lua_tointegerx(L, -1, &isnum);
                if (!isnum) {
                    luaL_error(L, "tag %d requrie a number, got '%s'", tag, lua_typename(L, ltype));
                }
                if (n < INT8_MIN || n > INT8_MAX) {
                    luaL_error(L, "tag %d int8_t overflow, got '%d'", tag, n);
                }
                if (n != def.integer || forced) {
                    write_int8(B, tag, n);
                }
            }
        } break;
        case LUATARS_UINT8: {
            if (LUA_TNIL == ltype) {  // 强制写入默认值
                write_int16(B, tag, def.integer);
            }
            else {
                int isnum = 0;
                lua_Integer n = lua_tointegerx(L, -1, &isnum);
                if (!isnum) {
                    luaL_error(L, "tag %d requrie a number, got '%s'", tag, lua_typename(L, ltype));
                }
                if ((uint16_t)n > UINT8_MAX) {
                    luaL_error(L, "tag %d uint8_t overflow, got '%d'", tag, n);
                }
                if (n != def.integer || forced) {
                    write_int16(B, tag, n);
                }
            }
        } break;
        case LUATARS_INT16: {
            if (LUA_TNIL == ltype) {  // 强制写入默认值
                write_int16(B, tag, def.integer);
            }
            else {
                int isnum = 0;
                lua_Integer n = lua_tointegerx(L, -1, &isnum);
                if (!isnum) {
                    luaL_error(L, "tag %d requrie a number, got '%s'", tag, lua_typename(L, ltype));
                }
                if (n < INT16_MIN || n > INT16_MAX) {
                    luaL_error(L, "tag %d int16_t overflow, got '%d'", tag, n);
                }
                if (n != def.integer || forced) {
                    write_int16(B, tag, n);
                }
            }
        } break;
        case LUATARS_UINT16: {
            if (LUA_TNIL == ltype) {  // 强制写入默认值
                write_int32(B, tag, def.integer);
            }
            else {
                int isnum = 0;
                lua_Integer n = lua_tointegerx(L, -1, &isnum);
                if (!isnum) {
                    luaL_error(L, "tag %d requrie a number, got '%s'", tag, lua_typename(L, ltype));
                }
                if ((uint32_t)n > UINT16_MAX) {
                    luaL_error(L, "tag %d uint16_t overflow, got '%d'", tag, n);
                }
                if (n != def.integer || forced) {
                    write_int32(B, tag, n);
                }
            }
        } break;
        case LUATARS_INT32: {
            if (LUA_TNIL == ltype) {  // 强制写入默认值
                write_int32(B, tag, def.integer);
            }
            else {
                int isnum = 0;
                lua_Integer n = lua_tointegerx(L, -1, &isnum);
                if (!isnum) {
                    luaL_error(L, "tag %d requrie a number, got '%s'", tag, lua_typename(L, ltype));
                }
                if (n < INT32_MIN || n > INT32_MAX) {
                    luaL_error(L, "tag %d int32_t overflow, got '%d'", tag, n);
                }
                if (n != def.integer || forced) {
                    write_int32(B, tag, n);
                }
            }
        } break;
        case LUATARS_UINT32: {
            if (LUA_TNIL == ltype) {  // 强制写入默认值
                write_int64(B, tag, def.integer);
            }
            else {
                int isnum = 0;
                lua_Integer n = lua_tointegerx(L, -1, &isnum);
                if (!isnum) {
                    luaL_error(L, "tag %d requrie a number, got '%s'", tag, lua_typename(L, ltype));
                }
                if ((uint64_t)n > UINT32_MAX) {
                    luaL_error(L, "tag %d uint32_t overflow, got '%d'", tag, n);
                }
                if (n != def.integer || forced) {
                    write_int64(B, tag, n);
                }
            }
        } break;
        case LUATARS_INT64: {
            if (LUA_TNIL == ltype) {  // 强制写入默认值
                write_int64(B, tag, def.integer);
            }
            else {
                int isnum = 0;
                lua_Integer n = lua_tointegerx(L, -1, &isnum);
                if (!isnum) {
                    luaL_error(L, "tag %d requrie a number, got '%s'", tag, lua_typename(L, ltype));
                }
                if (n < INT64_MIN || n > INT64_MAX) {
                    luaL_error(L, "tag %d int64_t overflow, got '%d'", tag, n);
                }
                if (n != def.integer || forced) {
                    write_int64(B, tag, n);
                }
            }
        } break;
        case LUATARS_FLOAT: {
            luaL_error(L, "float not support yet");
        } break;
        case LUATARS_DOUBLE: {
            luaL_error(L, "double not support yet");
        } break;
        case LUATARS_STRING: {
            size_t sz = 0;
            const char* s = NULL;
            if (LUA_TNIL == ltype) {  // 强制写入默认字符串
                lua_pop(L, 1);
                if (0 == def.integer) {
                    lua_pushlstring(L, "", 0);
                }
                else {
                    lua_rawgeti(L, 4, def.integer);
                }
            }
            s = lua_tolstring(L, -1, &sz);
            if (NULL == s) {
                luaL_error(L, "invalid string, tag: %d, type:%s", tag, lua_typename(L, ltype));
            }
            // 先写入长度
            if (sz > 255) {
                if (sz > _MAX_STR_LEN) {
                    luaL_error(L, "string size too large, tag:%d, sz:%d", tag, sz);
                }
                // 写入整形长度
                write_header(B, tag, TarsHeadeString4);
                uint32_t sz1 = htonl(sz);
                luaL_addlstring(B, (const char*)&sz1, sizeof sz1);
            }
            else {
                // 写入字节长度
                write_header(B, tag, TarsHeadeString1);
                luaL_addchar(B, (uint8_t)sz);
            }
            // 再写入字符串
            luaL_addlstring(B, s, sz);
        } break;
        default: {
            luaL_error(L, "type not support: %d, tag: %d", type, tag);
        }
    }
    return 0;
}

// 创建上下文
static int luatars_createContext(lua_State* L);

static int encodeBasic(  // 编码基础类型
    struct tars_context* context,
    lua_State* L,
    luaL_Buffer* B,
    struct tars_field* field);

static int encodeStruct(  // 编码结构体
    struct tars_context* context,
    lua_State* L,
    luaL_Buffer* B,
    uint32_t id,
    uint8_t tag,
    bool forced,
    bool noWrap);

static int encodeMap(  // 编码字典
    struct tars_context* context,
    lua_State* L,
    luaL_Buffer* B,
    uint32_t key_type,
    uint32_t value_type,
    uint8_t tag,
    bool forced,
    bool noWrap);

static int encodeList(  // 编码数组
    struct tars_context* context,
    lua_State* L,
    luaL_Buffer* B,
    uint32_t value_type,
    uint8_t tag,
    bool forced,
    bool noWrap);

// 创建上下文
int luatars_createContext(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TTABLE);  // 字段描述数组
    luaL_checktype(L, 2, LUA_TTABLE);  // 元表，存字段的名称和默认值
    lua_settop(L, 2);

    size_t n = lua_rawlen(L, 1);
    size_t sz = sizeof(struct tars_context) + n * sizeof(struct tars_field);
    struct tars_context* context = (struct tars_context*)lua_newuserdata(L, sz);
    lua_pushvalue(L, 2), lua_setmetatable(L, 3);

    // 内存块置零处理
    memset(context, 0, sz);
    // 总共的字段数量
    context->n = n;
    for (size_t i = 0; i < context->n;) {
        struct tars_field* field = &context->fields[i];
        i += 1;
        int t = lua_rawgeti(L, 1, i);
        if (LUA_TTABLE != t) {
            luaL_error(L, "invalid field element at #[%d]", i);
        }
        // 序号
        lua_getfield(L, -1, "tag"), field->tag = lua_tointeger(L, -1), lua_pop(L, 1);
        // 是否必须写入
        lua_getfield(L, -1, "forced"), field->forced = lua_toboolean(L, -1), lua_pop(L, 1);
        // 主类型
        lua_getfield(L, -1, "type1"), field->type1 = lua_tointeger(L, -1), lua_pop(L, 1);
        // 补充类型2
        lua_getfield(L, -1, "type2"), field->type2 = lua_tointeger(L, -1), lua_pop(L, 1);
        // 补充类型3
        lua_getfield(L, -1, "type3"), field->type3 = lua_tointeger(L, -1), lua_pop(L, 1);

        field->def.integer = 0;

        // 参数校验在lua层做
        t = lua_getfield(L, -1, "default");
        if (field->type1 <= LUATARS_INT64) {
            field->def.integer = lua_tointeger(L, -1);
        }
        else if (LUATARS_FLOAT == field->type1 || LUATARS_DOUBLE == field->type1) {
            field->def.number = lua_tonumber(L, -1);
        }
        else if (LUATARS_STRING == field->type1) {
            if (LUA_TSTRING == t) {
                // 缓存字符串
                field->def.integer = LUATARS_TYPE_MAX + context->n + i;
                lua_pushvalue(L, -1);
                lua_rawseti(L, 2, field->def.integer);
            }
        }

        // printf("[%d]:%s %d %d %d\n", field->tag, field->forced ? "required" : "optional", field->type1, field->type2,
        //        field->type3);

        lua_pop(L, 2);
    }
    return 1;
}

int encodeBasic(  // 编码基础类型
    struct tars_context* context,
    lua_State* L,
    luaL_Buffer* B,
    struct tars_field* field)
{
    return write_basic(L, B, field->tag, field->type1, field->forced, field->def);
}

int encodeStruct(  // 编码结构体函数实现，使用栈顶的元素
    struct tars_context* context,
    lua_State* L,
    luaL_Buffer* B,
    uint32_t id,
    uint8_t tag,
    bool forced,
    bool noWrap)
{
    // printf("写入结构体， id = %d\n", id);
    int ltype = lua_type(L, -1);
    if (LUA_TNIL == ltype) {
        if (forced) {
            // 必须写入
            lua_pop(L, 1), lua_newtable(L);
        }
        else {
            // 对象不存在，直接退出编码
            return 0;
        }
    }
    else if (LUA_TTABLE != ltype) {
        luaL_error(L, "%s require a table, got '%s'", __FUNCTION__, lua_typename(L, ltype));
    }
    // 开始的字段
    struct tars_field* field = context->fields + (size_t)(id - LUATARS_TYPE_MAX);
    if (field > context->fields + context->n) {
        luaL_error(L, "invalid struct for %s, id = %d", __FUNCTION__, id);
    }
    // 字段需要从0号开始
    if (field->tag != 0) {
        luaL_error(L, "invalid start field for %s, require 0, got %d", __FUNCTION__, field->tag);
    }
    if (!noWrap) {
        // 写入结构体开始
        write_header(B, tag, TarsHeadeStructBegin);
    }
    // 编码每一个字段
    for (;;) {
        // 先从元表里面拿到字段的名称
        int t1 = lua_rawgeti(L, 4, (field - context->fields));
        if (LUA_TSTRING != t1) {
            luaL_error(L, "field name not found for index = %d", field - context->fields);
        }
        // 再用表名称从之前栈顶的表中查询成员
        // printf("写入字段tag = %d, name = %s, index = %d\n", field->tag, lua_tostring(L, -1), (int)(field - context->fields));
        lua_rawget(L, -2);
        if (LUATARS_MAP == field->type1) {
            // 写入字典
            encodeMap(context, L, B, field->type2, field->type3, field->tag, field->forced, false);
        }
        else if (LUATARS_LIST == field->type1) {
            // 写入数组
            encodeList(context, L, B, field->type2, field->tag, field->forced, false);
        }
        else if (LUATARS_TYPE_MAX > field->type1) {
            // 写入基础类型
            encodeBasic(context, L, B, field);
        }
        else {
            // 写入结构体
            encodeStruct(context, L, B, field->type1, field->tag, field->forced, false);
        }
        // printf("写入字段%d, %s, (%d/%d)\n", field->tag, lua_tostring(L, -1), B->n, B->size);
        lua_pop(L, 1);
        // 切换到下一个字段
        ++field;
        if ((size_t)(field - context->fields) >= context->n || field->tag == 0) {
            // 读取到字段结束，或者读取到了下一个结构体的开始
            break;
        }
    }
    // 写入结构体结束
    if (!noWrap) {
        write_header(B, 0, TarsHeadeStructEnd);
    }
    return 0;
}

int encodeMap(  // 编码字典
    struct tars_context* context,
    lua_State* L,
    luaL_Buffer* B,
    uint32_t key_type,
    uint32_t value_type,
    uint8_t tag,
    bool forced,
    bool noWrap)
{
    // 是否需要强制写入
    int ltype = lua_type(L, -1);
    if (LUA_TNIL == ltype) {
        if (forced) {
            lua_pop(L, 1), lua_newtable(L);
        }
        else {
            // printf("字典不存在\n");
            return 0;
        }
    }
    else if (LUA_TTABLE != ltype) {
        luaL_error(L, "%s require a table, got '%s'", __FUNCTION__, lua_typename(L, ltype));
    }
    // 检验键的类型
    if (key_type > LUATARS_STRING) {
        luaL_error(L, "support basic key type only, got '%d', tag: %d");
    }
    // 计算字典的大小
    size_t n = 0;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        lua_pop(L, 1);
        ++n;
    }
    if (n < 1 && !forced) {
        return 0;  // 不用强制写空字典
    }
    if (!noWrap) {
        // printf("写入字典头部 tag = %d，长度 = %d\n", tag, (int)n);
        write_header(B, tag, TarsHeadeMap);
    }
    // 写入字典的长度
    write_int32(B, 0, n);

    lua_pushnil(L);
    while (lua_next(L, -2)) {
        // 编码key
        lua_pushvalue(L, -2);
        // printf("编码字典的key %s\n", lua_tostring(L, -1));
        write_basic(L, B, 0, key_type, true, def_zero);
        lua_pop(L, 1);
        // printf("编码字典的value %s:%s\n", lua_tostring(L, -1), lua_typename(L, lua_type(L, -1)));
        if (value_type < LUATARS_TYPE_MAX) {
            // printf("编码字典的value写入基础值\n");
            write_basic(L, B, 1, value_type, true, def_zero);
        }
        else {
            // printf("编码字典的value写入结构体 %d\n", value_type);
            encodeStruct(context, L, B, value_type, 1, true, false);
        }
        lua_pop(L, 1);
    }
    return 1;
}

int encodeList(  // 编码数组
    struct tars_context* context,
    lua_State* L,
    luaL_Buffer* B,
    uint32_t value_type,
    uint8_t tag,
    bool forced,
    bool noWrap)
{
    // 是否需要强制写入
    int ltype = lua_type(L, -1);
    if (LUA_TNIL == ltype) {
        if (forced) {
            lua_pop(L, 1), lua_newtable(L);
        }
        else {
            return 0;
        }
    }
    else if (LUA_TTABLE != ltype) {
        luaL_error(L, "%s require a table, got '%s'", __FUNCTION__, lua_typename(L, ltype));
    }
    int32_t n = lua_rawlen(L, -1);
    if (n < 1 || !forced) {
        return 0;
    }
    if (!noWrap) {
        // TODO: vector<char> 写入SimpleList
        write_header(B, tag, TarsHeadeList);
    }
    write_int32(B, 0, n);  // 写入长度
    for (int i = 0; i < n;) {
        ++i;
        lua_rawgeti(L, -1, i);
        if (value_type < LUATARS_TYPE_MAX) {
            write_basic(L, B, 0, value_type, true, def_zero);
        }
        else {
            encodeStruct(context, L, B, value_type, 0, true, false);
        }
        lua_pop(L, 1);
    }
    return 1;
}

// 将lua的表当作对象，编码成二进制流
// 用法：context:encodeStruct("TDemoDb", {sName = "value", iId = 1234})
static int luatars_encodeStruct(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct tars_context* context = (struct tars_context*)lua_touserdata(L, 1);
    int id = luaL_checkinteger(L, 2);  // 结构体id
    luaL_checktype(L, 3, LUA_TTABLE);  // 对象本身
    lua_settop(L, 3);
    lua_getmetatable(L, 1);            // 拿到元表
    luaL_checktype(L, 4, LUA_TTABLE);  // 元表在4号位置

    lua_pushvalue(L, 3);  // 栈顶是要编码的对象

    luaL_Buffer B;
    luaL_buffinit(L, &B);
    encodeStruct(context, L, &B, id, 0, 0, true);
    luaL_pushresult(&B);

    return 1;
}

// 将lua表当作字典，编码成二进制流
// 用法：
//  1. context:encodeMap(tars.STRING, tars.STRING, {["hello"] = "world"}, 0)
//  2. 用法：context:encodeMap(tars.STRING, tars.STRING, {["hello"] = "world"})
static int luatars_encodeMap(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct tars_context* context = (struct tars_context*)lua_touserdata(L, 1);
    int key_type = luaL_checkinteger(L, 2);
    int value_type = luaL_checkinteger(L, 3);
    luaL_checktype(L, 4, LUA_TTABLE);  // 对象本身
    int tag = luaL_optinteger(L, 5, 0);
    lua_settop(L, 4);
    lua_pushvalue(L, 4);
    lua_getmetatable(L, 1);
    lua_replace(L, 4);  // 4号位置用来放元表

    luaL_Buffer B;
    luaL_buffinit(L, &B);
    encodeMap(context, L, &B, key_type, value_type, tag, true, true);
    luaL_pushresult(&B);

    return 1;
}

// 将lua表当作数组，编码成二进制流
// 用法：context:encodeList(tars.STRING, {"hello", "world", "!"}, 0)
static int luatars_encodeList(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct tars_context* context = (struct tars_context*)lua_touserdata(L, 1);
    int value_type = luaL_checkinteger(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    int tag = luaL_optinteger(L, 4, 0);
    lua_settop(L, 3);
    lua_getmetatable(L, 1);  // 4号位置用来放元表
    lua_pushvalue(L, 3);

    luaL_Buffer B;
    luaL_buffinit(L, &B);
    encodeList(context, L, &B, value_type, tag, true, true);
    luaL_pushresult(&B);
    return 1;
}

// 读缓存
struct read_buffer {
    size_t offset;
    size_t n;
    const char* data;
};

// 缓存是否足够
static inline bool has_size(struct read_buffer* buffer, size_t sz)
{
    return buffer->offset + sz <= buffer->n;
}

// 返回当前位置
static inline const char* read_buffer(struct read_buffer* buffer, int n)
{
    return buffer->data + buffer->offset + n;
}

// 跳过n个字节
static inline void skip_buffer(struct read_buffer* buffer, int n)
{
    buffer->offset += n;
}

// 头部
struct tars_header {
    uint8_t tag;
    uint8_t type;
};

// 读取头部
// 返回0: 没有读取到大小
// 返回1: 读取了一个字节
// 返回2: 读取了两个字节
// 返回-1: 字节流错误
static inline int read_header(struct read_buffer* buffer, struct tars_header* header)
{
    if (!has_size(buffer, 1)) {
        return 0;
    }
    uint8_t b = *read_buffer(buffer, 0);
    if (0xF0 == (0xF0 & b)) {
        // 2个字节的头部
        if (!has_size(buffer, 2)) {
            return -1;
        }
        header->type = (b & 0x0F);
        header->tag = *read_buffer(buffer, 1);
        return 2;
    }
    else {
        // 1个字节的头部
        header->type = (b & 0x0F);
        header->tag = (b >> 4);
        return 1;
    }
}

static int decodeStruct(  // 解码结构体
    struct tars_context* context,
    lua_State* L,
    struct read_buffer* buffer,
    uint32_t id,
    bool missing);

static int decodeList(  // 解码数组
    struct tars_context* context,
    lua_State* L,
    struct read_buffer* buffer,
    uint32_t value_type,
    bool missing);

static int decodeMap(  // 解码字典
    struct tars_context* context,
    lua_State* L,
    struct read_buffer* buffer,
    uint32_t key_type,
    uint32_t value_type,
    bool missing);

static bool readHeader(  // 读取字段头部，返回是否缺失字段
    lua_State* L,
    struct read_buffer* buffer,
    struct tars_header* header,
    int16_t tag)
{
    int n = read_header(buffer, header);
    if (n < 0) {
        luaL_error(L, "[C] %s %d: data truncated, require tag = %d", __FUNCTION__, __LINE__, tag);
    }
    if (0 == n) {
        return true;
    }
    // printf("read (tag = %d, type = %s), offset = (%ld/%ld), n = %d\n", header->tag, tars_type_name(header->type),
    // buffer->offset,
    //        buffer->n, n);
    if (TarsHeadeStructEnd == header->type) {
        if (-1 == tag) {
            // 在跳过模式下，读取到结构体结束标志位，会前移游标
            skip_buffer(buffer, n);
        }
        return true;  // 读取到结构体结束
    }
    if (-1 != tag) {
        if (header->tag > tag) {
            return true;  // 读取到了下一个字段
        }
        if (header->tag < tag) {
            // 读取到了上一个字段，这是不应该的，报错
            luaL_error(L, "[C] %s %d: discrete field, require tag = %d, got %d type = '%s'", __FUNCTION__, __LINE__, tag,
                       header->tag, tars_type_name(header->type));
        }
    }
    // 读取到了字段
    skip_buffer(buffer, n);
    return false;
}

static int64_t read_int64(  // 通用的读取整数值
    lua_State* L,
    struct read_buffer* buffer,
    union default_value def,
    struct tars_header header,
    bool field_missing)
{
    if (field_missing) {
        return def.integer;
    }
    switch (header.type) {
        case TarsHeadeZeroTag: {
            return 0;
        }
        case TarsHeadeChar: {
            if (!has_size(buffer, sizeof(int8_t))) {
                luaL_error(L, "no buffer int8_t, (%d/%d)", buffer->offset, buffer->n);
            }
            int8_t v = *(const int8_t*)read_buffer(buffer, 0);
            skip_buffer(buffer, sizeof(int8_t));
            return v;
        }
        case TarsHeadeShort: {
            if (!has_size(buffer, sizeof(int16_t))) {
                luaL_error(L, "no buffer int16_t");
            }
            int16_t v = ntohs(*(const int16_t*)read_buffer(buffer, 0));
            skip_buffer(buffer, sizeof(int16_t));
            return v;
        }
        case TarsHeadeInt32: {
            if (!has_size(buffer, sizeof(int32_t))) {
                luaL_error(L, "no buffer int32_t");
            }
            int32_t v = ntohl(*(const int32_t*)read_buffer(buffer, 0));
            skip_buffer(buffer, sizeof(int32_t));
            return v;
        }
        case TarsHeadeInt64: {
            if (!has_size(buffer, sizeof(int64_t))) {
                luaL_error(L, "no buffer int64_t");
            }
            int64_t v = be64toh(*(const int64_t*)read_buffer(buffer, 0));
            skip_buffer(buffer, sizeof(int64_t));
            return v;
        }
        default: {
            luaL_error(L, "invalid integer, got type = %d '%s', tag = %d", header.type, tars_type_name(header.type), header.tag);
            return 0;
        }
    }
}

static int read_basic(  // 读取基础类型
    lua_State* L,
    struct read_buffer* buffer,
    uint8_t type,
    union default_value def,
    struct tars_header header,
    bool field_missing)
{
    switch (type) {
        case LUATARS_BOOL: {
            int64_t n = read_int64(L, buffer, def, header, field_missing);
            if ((uint64_t)n > 1u) {
                luaL_error(L, "invalid bool value = %d, tag = %d", n, header.tag);
            }
            lua_pushboolean(L, n);
        } break;
        case LUATARS_INT8: {
            int64_t n = read_int64(L, buffer, def, header, field_missing);
            if (n < INT8_MIN || n > INT8_MAX) {
                luaL_error(L, "invalid int8_t value = %d, tag = %d", n, header.tag);
            }
            lua_pushinteger(L, n);
        } break;
        case LUATARS_UINT8: {
            int64_t n = read_int64(L, buffer, def, header, field_missing);
            if ((uint64_t)n > UINT8_MAX) {
                luaL_error(L, "invalid uint8_t value = %d, tag = %d", n, header.tag);
            }
            lua_pushinteger(L, n);
        } break;
        case LUATARS_INT16: {
            int64_t n = read_int64(L, buffer, def, header, field_missing);
            if (n < INT16_MIN || n > INT16_MAX) {
                luaL_error(L, "invalid int16_t value = %d, tag = %d", n, header.tag);
            }
            lua_pushinteger(L, n);
        } break;
        case LUATARS_UINT16: {
            int64_t n = read_int64(L, buffer, def, header, field_missing);
            if ((uint64_t)n > UINT16_MAX) {
                luaL_error(L, "invalid uint16_t value = %d, tag = %d", n, header.tag);
            }
            lua_pushinteger(L, n);
        } break;
        case LUATARS_INT32: {
            int64_t n = read_int64(L, buffer, def, header, field_missing);
            if (n < INT32_MIN || n > INT32_MAX) {
                luaL_error(L, "invalid int32_t value = %d, tag = %d", n, header.tag);
            }
            lua_pushinteger(L, n);
        } break;
        case LUATARS_UINT32: {
            int64_t n = read_int64(L, buffer, def, header, field_missing);
            if ((uint64_t)n > UINT32_MAX) {
                luaL_error(L, "invalid uint32_t value = %d, tag = %d", n, header.tag);
            }
            lua_pushinteger(L, n);
        } break;
        case LUATARS_INT64: {
            int64_t n = read_int64(L, buffer, def, header, field_missing);
            lua_pushinteger(L, n);
        } break;
        case LUATARS_FLOAT:
        case LUATARS_DOUBLE: {
            luaL_error(L, "float type not support yet");
        } break;
        case LUATARS_STRING: {
            if (field_missing) {
                if (def.integer == 0) {
                    lua_pushlstring(L, "", 0);
                }
                else {
                    lua_rawgeti(L, 4, def.integer);
                }
            }
            else if (TarsHeadeString4 == header.type) {
                if (!has_size(buffer, sizeof(uint32_t))) {
                    luaL_error(L, "[C] %s %d: truncated buffer", __FUNCTION__, __LINE__);
                }
                uint32_t sz = *(const uint32_t*)read_buffer(buffer, 0);
                sz = ntohl(sz);
                skip_buffer(buffer, sizeof(uint32_t));
                if (!has_size(buffer, sz)) {
                    luaL_error(L, "[C] %s %d: no buffer, need %d", __FUNCTION__, __LINE__, sz);
                }
                lua_pushlstring(L, read_buffer(buffer, 0), sz);
                skip_buffer(buffer, sz);
            }
            else if (TarsHeadeString1 == header.type) {
                if (!has_size(buffer, sizeof(uint8_t))) {
                    luaL_error(L, "[C] %s %d: no buffer", __FUNCTION__, __LINE__);
                }
                uint8_t sz = *(const uint8_t*)read_buffer(buffer, 0);
                skip_buffer(buffer, sizeof(uint8_t));
                if (!has_size(buffer, sz)) {
                    luaL_error(L, "truncated buffer, need %d", sz);
                }
                lua_pushlstring(L, read_buffer(buffer, 0), sz);
                skip_buffer(buffer, sz);
            }
            else {
                luaL_error(L, "invalid string type, got %d, tag = %d", header.type, header.tag);
            }
        } break;
    }
    return 0;
}

static int skipField(  // 跳过若干字段
    lua_State* L,
    struct read_buffer* buffer,
    uint16_t n);

int decodeStruct(  // 解码结构体
    struct tars_context* context,
    lua_State* L,
    struct read_buffer* buffer,
    uint32_t id,
    bool missing)
{
    // 开始的字段
    struct tars_field* field = context->fields + (size_t)(id - LUATARS_TYPE_MAX);
    if (field > context->fields + context->n) {
        luaL_error(L, "[C] %s %d: invalid struct, id = %d", __FUNCTION__, __LINE__, id);
    }
    // 字段需要从0号开始
    if (field->tag != 0) {
        luaL_error(L, "[C] %s %d: invalid start field, require 0, got %d", __FUNCTION__, __LINE__, field->tag);
    }
    // 此处头部已经读取，只要在读取字段的后读取到结构体结束，解码就结束
    lua_newtable(L);
    for (;;) {
        // 先查询名称
        int t = lua_rawgeti(L, 4, (field - context->fields));
        if (LUA_TSTRING != t) {
            luaL_error(L, "field name not found for id = %d", id);
        }
        // printf("解析字段 %d %s\n", (int)(field - context->fields), lua_tostring(L, -1));
        bool field_missing = missing;
        // 先读取字段头部
        struct tars_header header;
        if (!field_missing) {
            field_missing = readHeader(L, buffer, &header, field->tag);
            if (field_missing && TarsHeadeStructEnd == header.type) {
                missing = true;  // 读取到结构体结束了
            }
        }
        if (field->type1 <= LUATARS_STRING) {
            // 解析基础类型字段
            read_basic(L, buffer, field->type1, def_zero, header, field_missing);
        }
        else if (field->type1 == LUATARS_MAP) {
            // 解析字段字段
            if (!field_missing && TarsHeadeMap != header.type) {
                luaL_error(L, "[C] %s %d: invalid field, require 'map', got '%s', tag = %d", __FUNCTION__, __LINE__,
                           tars_type_name(header.type), field->tag);
            }
            decodeMap(context, L, buffer, field->type2, field->type3, field_missing);
        }
        else if (field->type1 == LUATARS_LIST) {
            // 解析数组字段
            if (!field_missing && TarsHeadeList != header.type) {
                luaL_error(L, "[C] %s %d: invalid field, require 'list', got '%s', tag = %d", __FUNCTION__, __LINE__,
                           tars_type_name(header.type), field->tag);
            }
            decodeList(context, L, buffer, field->type2, field_missing);
        }
        else {
            if (!field_missing && TarsHeadeStructBegin != header.type) {
                luaL_error(L, "[C] %s %d: invalid field, require 'struct', got '%s', tag = %d", __FUNCTION__, __LINE__,
                           tars_type_name(header.type), field->tag);
            }
            decodeStruct(context, L, buffer, field->type1, field_missing);
        }
        // printf("解析字段%d '%s' = %s %s\n", (int)(field - context->fields), lua_tostring(L, -2), lua_tostring(L, -1),
        //     lua_typename(L, lua_type(L, -1)));
        lua_rawset(L, -3);
        ++field;
        if ((size_t)(field - context->fields) >= context->n || field->tag == 0) {
            // 结构体结束了
            break;
        }
    }
    // 跳过结构体尾部多余的字段：使用旧协议解析新协议结构
    skipField(L, buffer, 255);

    return 1;
}

int decodeList(  // 解码数组
    struct tars_context* context,
    lua_State* L,
    struct read_buffer* buffer,
    uint32_t value_type,
    bool missing)
{
    if (value_type >= LUATARS_TYPE_MAX) {
        if ((value_type - LUATARS_TYPE_MAX) > context->n) {
            luaL_error(L, "[C] %s %d: invalid struct, id = %d", __FUNCTION__, __LINE__, value_type);
        }
    }
    int64_t len = 0;
    if (!missing) {
        // 读取长度
        struct tars_header header;
        if (readHeader(L, buffer, &header, 0)) {
            luaL_error(L, "[C] %s %d: list got no length, (%d/%d)", __FUNCTION__, __LINE__, buffer->offset, buffer->n);
        }
        len = read_int64(L, buffer, def_zero, header, false);
    }
    // printf("读取数组，长度为%d\n", len);
    lua_createtable(L, len, 0);
    lua_rawgetp(L, LUA_REGISTRYINDEX, list_mt), lua_setmetatable(L, -2);
    for (int i = 0; i < len;) {
        // 读取数组元素，先读取头部
        struct tars_header header;
        if (readHeader(L, buffer, &header, 0)) {
            luaL_error(L, "[C] %s %d: list element not found, index = %d, n = %d", __FUNCTION__, __LINE__, i, len);
        }
        if (value_type < LUATARS_TYPE_MAX) {
            read_basic(L, buffer, value_type, def_zero, header, false);
        }
        else {
            if (TarsHeadeStructBegin != header.type) {
                luaL_error(L, "[C] %s %d: invalid list element, require 'struct', got '%s', index = %d", __FUNCTION__, __LINE__,
                           tars_type_name(header.type), i);
            }
            decodeStruct(context, L, buffer, value_type, false);
        }
        i += 1;
        // printf("读取第%d个元素:%s, i = %d, n = %d\n", i, lua_tostring(L, -1), buffer->offset, buffer->n);
        lua_rawseti(L, -2, i);
    }
    return 0;
}

int decodeMap(  // 解码字典
    struct tars_context* context,
    lua_State* L,
    struct read_buffer* buffer,
    uint32_t key_type,
    uint32_t value_type,
    bool missing)
{
    if (value_type >= LUATARS_TYPE_MAX) {
        if ((size_t)(value_type - LUATARS_TYPE_MAX) > context->n) {
            luaL_error(L, "invalid struct, id = %d", value_type);
        }
    }
    int64_t len = 0;
    if (!missing) {
        struct tars_header header;
        if (readHeader(L, buffer, &header, 0)) {
            luaL_error(L, "[C] %s %d: map got no length", __FUNCTION__, __LINE__);
        }
        len = read_int64(L, buffer, def_zero, header, false);
    }
    lua_createtable(L, 0, len);
    lua_rawgetp(L, LUA_REGISTRYINDEX, map_mt), lua_setmetatable(L, -2);
    for (int i = 0; i < len; ++i) {
        // key只支持基础类型
        struct tars_header header;
        if (readHeader(L, buffer, &header, 0)) {
            luaL_error(L, "[C] %s %d: map got no key", __FUNCTION__, __LINE__);
        }
        read_basic(L, buffer, key_type, def_zero, header, false);
        // value只支持基础类型和复合类型，不支持嵌套类型
        if (readHeader(L, buffer, &header, 1)) {
            luaL_error(L, "[C] %s %d: map got no value, (%d/%d)", __FUNCTION__, __LINE__, i, len);
        }
        if (value_type < LUATARS_TYPE_MAX) {
            read_basic(L, buffer, value_type, def_zero, header, false);
        }
        else {
            if (TarsHeadeStructBegin != header.type) {
                luaL_error(L, "[C] %s %d: invalid map value, require 'struct', got '%s'", __FUNCTION__, __LINE__,
                           tars_type_name(header.type));
            }
            decodeStruct(context, L, buffer, value_type, false);
        }
        lua_rawset(L, -3);
    }
    return 0;
}

#define CHECK_SIZE(L, Buffer, N)                                                \
    if (!has_size(Buffer, N)) {                                                 \
        luaL_error(L, "[C] %s %d: malformaled stream", __FUNCTION__, __LINE__); \
    }

#define SKIP_SIZE(L, Buffer, N) \
    CHECK_SIZE(L, Buffer, N);   \
    skip_buffer(Buffer, N);

int skipField(  // 跳过若干字段
    lua_State* L,
    struct read_buffer* buffer,
    uint16_t n)
{
    for (struct tars_header header; n != 0 && !readHeader(L, buffer, &header, -1); --n) {
        switch (header.type) {
            case TarsHeadeZeroTag: {
                // skip nothing
            } break;
            case TarsHeadeChar: {
                SKIP_SIZE(L, buffer, sizeof(int8_t));
            } break;
            case TarsHeadeShort: {
                SKIP_SIZE(L, buffer, sizeof(int16_t));
            } break;
            case TarsHeadeInt32: {
                SKIP_SIZE(L, buffer, sizeof(int32_t));
            } break;
            case TarsHeadeInt64: {
                SKIP_SIZE(L, buffer, sizeof(int64_t));
            } break;
            case TarsHeadeFloat: {
                SKIP_SIZE(L, buffer, sizeof(float));
            } break;
            case TarsHeadeDouble: {
                SKIP_SIZE(L, buffer, sizeof(double));
            } break;
            case TarsHeadeString1: {
                CHECK_SIZE(L, buffer, sizeof(uint8_t));
                uint8_t sz = *(const uint8_t*)read_buffer(buffer, 0);
                SKIP_SIZE(L, buffer, sz + sizeof(uint8_t));
            } break;
            case TarsHeadeString4: {
                CHECK_SIZE(L, buffer, sizeof(uint32_t));
                uint32_t sz = *(const uint32_t*)read_buffer(buffer, 0);
                sz = ntohl(sz);
                SKIP_SIZE(L, buffer, sz + sizeof(uint32_t));
            } break;
            case TarsHeadeMap: {
                struct tars_header sz_header;
                if (readHeader(L, buffer, &sz_header, 0)) {
                    luaL_error(L, "[C] %s %d: map got no length", __FUNCTION__, __LINE__);
                }
                int64_t len = read_int64(L, buffer, def_zero, sz_header, false);
                for (; len > 0; --len) {
                    skipField(L, buffer, 1);
                    skipField(L, buffer, 1);
                }
            } break;
            case TarsHeadeList: {
                struct tars_header sz_header;
                if (readHeader(L, buffer, &sz_header, 0)) {
                    luaL_error(L, "[C] %s %d: list got no length", __FUNCTION__, __LINE__);
                }
                int64_t len = read_int64(L, buffer, def_zero, sz_header, false);
                for (; len > 0; --len) {
                    skipField(L, buffer, 1);
                }
            } break;
            case TarsHeadeStructBegin: {
                skipField(L, buffer, 256);  // 跳过一个完整的结构体
            } break;
            case TarsHeadeSimpleList: {
                luaL_error(L, "[C] %s %d: TODO 'TarsHeadeSimpleList' not support yet", __FUNCTION__, __LINE__);
            } break;
            default: {
                luaL_error(L, "[C] %s %d: can not skip type = %d '%s'", __FUNCTION__, __LINE__, header.type,
                           tars_type_name(header.type));
            }
        }
    }
    return 0;
}

// 从二进制流中解析出指定的结构体
static int luatars_decodeStruct(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct tars_context* context = (struct tars_context*)lua_touserdata(L, 1);
    uint32_t id = luaL_checkinteger(L, 2);
    size_t n = 0;
    const char* s = luaL_checklstring(L, 3, &n);
    lua_settop(L, 3);
    lua_getmetatable(L, 1);

    struct read_buffer buffer;
    buffer.n = n, buffer.offset = 0, buffer.data = s;

    decodeStruct(context, L, &buffer, id, false);

    return 1;
}

static int luatars_decodeMap(lua_State* L)
{
    // 从二进制流中解析出指定的字典
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct tars_context* context = (struct tars_context*)lua_touserdata(L, 1);
    uint32_t key_type = luaL_checkinteger(L, 2);
    uint32_t value_type = luaL_checkinteger(L, 3);
    lua_settop(L, 4), lua_replace(L, 3);
    size_t n = 0;
    const char* s = luaL_checklstring(L, 3, &n);
    lua_getmetatable(L, 1);  // 4号位置是元表

    struct read_buffer buffer;
    buffer.n = n, buffer.offset = 0, buffer.data = s;

    decodeMap(context, L, &buffer, key_type, value_type, false);

    return 1;
}

static int luatars_decodeList(lua_State* L)
{
    // 从二进制流中解析出指定的数组
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct tars_context* context = (struct tars_context*)lua_touserdata(L, 1);
    uint32_t value_type = luaL_checkinteger(L, 2);
    size_t n = 0;
    const char* s = luaL_checklstring(L, 3, &n);
    lua_settop(L, 3);
    lua_getmetatable(L, 1);  // 4号位置是元表

    struct read_buffer buffer;
    buffer.n = n, buffer.offset = 0, buffer.data = s;

    decodeList(context, L, &buffer, value_type, false);

    return 1;
}

// 打印环境的整体信息
static int luatars_dump(lua_State* L)
{
    luaL_checktype(L, 1, LUA_TUSERDATA);
    struct tars_context* context = (struct tars_context*)lua_touserdata(L, 1);
    lua_settop(L, 1);
    lua_getmetatable(L, 1);

    luaL_Buffer B;
    luaL_buffinit(L, &B);
    for (size_t i = 0; i < context->n; ++i) {
        char b[512];
        lua_rawgeti(L, 2, i);
        struct tars_field* field = &context->fields[i];
        int sz = snprintf(b, sizeof b, "[%d]:%8s\t%s\t%d\t%d\t%d\n", field->tag, lua_tostring(L, -1),
                          field->forced ? "require" : "optional", field->type1, field->type2, field->type3);
        lua_pop(L, 1);
        luaL_addlstring(&B, b, sz);
    }
    luaL_pushresult(&B);
    return 1;
}

// 设置
#define set_luatars_enum(L, Type) lua_pushinteger(L, LUATARS_##Type), lua_setfield(L, -2, #Type);

#ifdef __cplusplus
#define EXPORT extern "C"
#else
#define EXPORT
#endif

EXPORT int luaopen_tars(lua_State* L)
{
    // 注册所有的函数
    luaL_Reg funs[] = {
        {"createContext", luatars_createContext},
        {"encodeStruct", luatars_encodeStruct},
        {"encodeMap", luatars_encodeMap},
        {"encodeList", luatars_encodeList},
        {"decodeStruct", luatars_decodeStruct},
        {"decodeMap", luatars_decodeMap},
        {"decodeList", luatars_decodeList},
        {"dump", luatars_dump},
        {NULL, NULL},
    };
    luaL_newlib(L, funs);
    // 注册所有的类型定义
    set_luatars_enum(L, BOOL);
    set_luatars_enum(L, INT8);
    set_luatars_enum(L, UINT8);
    set_luatars_enum(L, INT16);
    set_luatars_enum(L, UINT16);
    set_luatars_enum(L, INT32);
    set_luatars_enum(L, UINT32);
    set_luatars_enum(L, INT64);
    set_luatars_enum(L, FLOAT);
    set_luatars_enum(L, DOUBLE);
    set_luatars_enum(L, STRING);
    set_luatars_enum(L, MAP);
    set_luatars_enum(L, LIST);
    set_luatars_enum(L, TYPE_MAX);

    lua_newtable(L), lua_rawsetp(L, LUA_REGISTRYINDEX, list_mt);
    lua_newtable(L), lua_rawsetp(L, LUA_REGISTRYINDEX, map_mt);

    lua_rawgetp(L, LUA_REGISTRYINDEX, list_mt), lua_setfield(L, -2, "list_mt");
    lua_rawgetp(L, LUA_REGISTRYINDEX, map_mt), lua_setfield(L, -2, "map_mt");

    return 1;
}

