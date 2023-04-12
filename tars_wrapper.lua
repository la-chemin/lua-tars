local tars = require "tars"

local type = type

-- 基础类型的枚举
local __basic_types = {
    ["bool"] = tars.BOOL,
    ["byte"] = tars.INT8,
    ["u_byte"] = tars.UINT8,
    ["short"] = tars.INT16,
    ["u_short"] = tars.UINT16,
    ["int"] = tars.INT32,
    ["u_int"] = tars.UINT32,
    ["long"] = tars.INT64,
    ["u_long"] = tars.INT64,
    ["float"] = tars.FLOAT,
    ["double"] = tars.DOUBLE,
    ["string"] = tars.STRING,
    ["map"] = tars.MAP,
    ["list"] = tars.LIST,
} 

-- 常用字典键的枚举
local __map_keys = {
    ["byte"] = tars.INT8,
    ["u_byte"] = tars.UINT8,
    ["short"] = tars.INT16,
    ["u_short"] = tars.UINT16,
    ["int"] = tars.INT32,
    ["u_int"] = tars.UINT32,
    ["long"] = tars.INT64,
    ["u_long"] = tars.INT64,
    ["string"] = tars.STRING,
}

-- 字符串模式匹配展开
local function str_expand(s, pattern)
    local b = {}
    for p in s:gmatch(pattern) do
        table.insert(b, p)
    end
    return b
end

-- sTars: tars结构体声明字符串
function tars.parse(sTars)
    -- 忽略命名空间声明(1级大括号)
    -- 忽略行注释，块注释排除不想写
    local structName
    local fields = {}
    local mt = {
        __index = tars,
        -- 正数字 - tars.TYPE_MAX => 结构体的标识符，供C层用
        -- 标识符 => 正数字，供lua使用
    }
    for line in sTars:gmatch("([^\r\n]+)") do
        line = line:gsub("//.+", "")
        if not structName then
            -- 结构体开始
            structName = line:match("struct%s+(%w[_%w]*)")
            if structName then
                if mt[structName] then
                    error(("duplicate struct definition '%s'"):format(structName))
                end
                local id = tars.TYPE_MAX + #(fields)
                -- 记录结构体的开始字段位置
                mt[structName] = id
                -- print("新增一个类型", structName, mt[structName])
            end
        else
            -- 字段声明部分解析
            local tag, forced, declare, default = line:match("(%d+)%s+(%w+)%s+([^=]+)=?(.*);")
            if not tag then
                if line:match("}") then
                    -- 结构体结束
                    structName = nil
                end
            else
                -- 字段是否强制写
                if forced == "required" then
                    forced = true
                elseif forced == "optional" then
                    forced = false
                else
                    error(("invalid forced:'%s', line:'%s'"):format(forced, line))
                end
                -- 字段类型和名称部分
                declare = declare:gsub("%s+", " "):gsub("unsigned ", "u_")
                local args = str_expand(declare, "[^<>, ]+")
                local name = table.remove(args)
                local type1, type2, type3 = table.unpack(args)
                if type3 and type3 ~= "" then
                    -- 这个是字典
                    -- 校验键值对类型
                    assert(type1 == "map" and __map_keys[type2])
                    type1, type2, type3 = tars.MAP, __map_keys[type2], __basic_types[type3] or mt[type3]

                    -- 校验键值对类型
                    if not (type(type2) == "number" and type(type3) == "number") then
                        error(("invalid map type '%s'"):format(declare))
                    end
                elseif type2 and type2 ~= "" then
                    -- 这个是数组
                    assert(type1 == "vector")
                    type1, type2 = tars.LIST, __basic_types[type2] or mt[type2]
                    type3 = nil

                    -- 校验数组元素类型
                    if not (type(type2) == "number") then
                        error(("invalid vector value type '%s'"):format(declare))
                    end
                else
                    -- 普通类型或者结构体
                    type1 = __basic_types[type1] or mt[type1]

                    -- 校验字段类型
                    if type(type1) ~= "number" then
                        error(("unknown type %s"):format(type2))
                    end

                    type2, type3 = nil, nil
                end
                if default and default ~= "" then
                    if default == "false" then
                        default = false
                    elseif default == "true" then
                        default = true
                    else
                        local s = default:match('"[^"]*"')
                        if s then
                            default = s
                        else
                            local n = tonumber(default)
                            if not n then
                                error(("invalid default value '%s'"):format(default))
                            end
                            default = n
                        end
                    end
                end
                -- print(tag, name, forced, type1, type2, type3, default)
                -- 记录结构体字段的名称
                mt[#fields] = name
                table.insert(fields, {
                    tag = tag,
                    name = name,
                    forced = forced,
                    type1 = type1,
                    type2 = type2,
                    type3 = type3,
                    default = default,
                })
            end
        end
    end
    -- 创建上下文
    return tars.createContext(fields, mt)
end

-- 编码结构体
local tars_encodeStruct = tars.encodeStruct
function tars:encodeStruct(name, obj)
    return tars_encodeStruct(self, getmetatable(self)[name], obj)
end

-- 编码数组
local tars_encodeList = tars.encodeList
function tars:encodeList(value_type, list)
    if type(value_type) == "number" then
        return tars_encodeList(self, value_type, list)
    else
        return tars_encodeList(self, getmetatable(self)[value_type], list)
    end
end

-- 编码字典
local tars_encodeMap = tars.encodeMap
function tars:encodeMap(key_type, value_type, map)
    if type(value_type) == "number" then
        return tars_encodeMap(self, key_type, value_type, map)
    else
        return tars_encodeMap(self, key_type, getmetatable(self)[value_type], map)
    end
end

-- 解码结构体
local tars_decodeStruct = tars.decodeStruct
function tars:decodeStruct(name, data)
    return tars_decodeStruct(self, getmetatable(self)[name], data)
end

-- 解码数组
local tars_decodeList = tars.decodeList
function tars:decodeList(value_type, data)
    if type(value_type) == "number" then
        return tars_decodeList(self, value_type, data)
    else
        return tars_decodeList(self, getmetatable(self)[value_type], data)
    end
end

-- 解码字典
local tars_decodeMap = tars.decodeMap
function tars:decodeMap(key_type, value_type, data)
    if type(value_type) == "number" then
        return tars_decodeMap(self, key_type, value_type, data)
    else
        return tars_decodeMap(self, key_type, getmetatable(self)[value_type], data)
    end
end

return tars
