local tars = require "tars"

local text = [[
    // 客户端请求包
    struct TClientPack {
        0 optional int shZoneId; //区服
        1 optional string sUid; //用户id
        2 optional string sSignature; //签名
        3 optional int iTimeStamp; //时间戳
        4 optional int iMsgSeq; //消息序列
        5 optional int iClientVersion; //游戏版本号
        6 optional int iPlatformId; // 平台渠道id[平台id * 100 + 渠道id]
        7 optional int iPatchVersion; //客户端通过补丁升级到的版本
        8 optional int shCmd; //命令字id
        9 optional string sCmdParam; //命令参数
        10 optional string sDeviceId; //设备id
    };

    // 服务器返回包
    struct TServerPack {
        0 optional int iMsgSeq; // 消息序列
        1 optional int shCmd; // 命令字
        2 optional int shResultId; // 结果
        3 optional string sCmdParam = "fuck you"; // 返回结果
        4 optional bool bCompress = false; // 是否压缩
    };

    //用户全局数据
    struct TGlobalProfileDb {
        0 optional vector<int> vLastZones; //用户最近登录的分区
    };

    // 热点数据
    struct THotDataInfo {
        0 require int iZoneId; // 区服ID
        1 require string sUin; // 角色ID
        2 optional map<int, string> mHotData; // 热点数据，参考 HOTDATA_LEVEL
    };

    // 技能
    struct TSkillDb {
        0 require int iId; // 技能id
        1 require int iLevel = 1; // 技能等级
        2 require int iStatus = 0; // 技能状态，见SkillStatus_Locked
    };

    // 推图记录
    struct TPveDb {
        0 require int iChapter = 1; // 当前章节
        1 require int iLevel = 0; // 当前关卡
        2 require int iLastIdleTime = 0; // 上次收取挂机奖励的时间
    };

    //快速挂机
    struct TQuickIdleDb {
        0 optional int iFreeTime = 0; //免费次数
        1 optional int iBuyTime = 0; //可购买次数
        2 optional int iAlreadyBuyTime = 0; //已购买次数
        3 optional int iRefreshTime = 0; //刷新时间
        4 optional int iLifeNums = 0; //生平次数
    };

    struct Test {
        0 optional TQuickIdleDb stQuickIdleDb;
        1 optional bool bTest;
    };
]]

-- 存放了所有字段的数组
local fields = {}
-- 结构体名称到上面数组的映射：name->(index + 100)
local host = {}

-- 常见类型
local types = {
    ["bool"] = 1,
    ["byte"] = 2,
    ["u_byte"] = 3,
    ["short"] = 4,
    ["u_short"] = 5,
    ["int"] = 6,
    ["u_int"] = 7,
    ["long"] = 8,
    ["u_long"] = 9,
    ["float"] = 10,
    ["double"] = 11,
    ["string"] = 12,
    ["map"] = 13,
    ["vector"] = 14,
}

-- map支持的键类型
-- 先限定只能用基础类型
local map_keys = {}
for __, key in ipairs{"int", "u_int", "long", "u_long", "string"} do
    map_keys[key] = assert(types[key])
end

-- 解析类型
local function parse_type(data_type)
    if types[data_type] then
        return types[data_type], 0, 0
    end
    local type1, type2, type3 = data_type:match("(%w+)<?(%w*),?([%w_]*)>?")
    
    if type3 and type3 ~= "" then
        -- 必须是map
        assert(type1 == "map")
        -- 必须是常见的key
        assert(map_keys[type2])
        return types[type1], map_keys[type2], types[type3] or host[type3]
    end

    if type2 and type2 ~= "" then
        -- 必须是vector
        assert(type1 == "vector")
        return types[type1], types[type2] or host[type2], 0
    end
    
    return types[type1] or host[type1], 0, 0
end

-- 解析默认值
local function parse_default(default)
    if not default then
        -- 没有默认值
        return nil
    end
    if default == "false" then
        return 0
    end
    if default == "true" then
        return 1
    end
    local num = tonumber(default)
    if num then
        return num
    end
    local s = default:match("(.+)")
    if s then
        return s
    end
end

for line in text:gmatch("[^\n]+") do
    local line_no_comment = line:gsub("//.*$", "") -- 尾部的注释
    :gsub("%s+", " ") -- 空白字符替换成空格
    :gsub("unsigned ", "u_") -- 合并unsigned
    :gsub(" ?([<,]) ?", tostring) -- 模板参数去除左右空格
    :gsub(" >", ">") -- 模板参数移除左空格

    local struct_name = line_no_comment:match("struct%s+(%w+)")
    if struct_name then
        -- 记录结构体的起始位置 + 偏移值100
        host[struct_name] = #(fields) + 100

        last = struct_name
    end

    local tag, forced, data_type, name, default = line_no_comment:match("(%d+) (%w+) (%a[%w<>_,]+) (%a[%w_]*) ?=? ?(.*);")
    if tag then
        local type1, type2, type3 = parse_type(data_type)
        table.insert(fields, {
            tag = tag, -- 下标
            name = name, -- 名称
            forced = forced, -- 必须写
            type1 = type1, -- 类型1
            type2 = type2, -- 类型2
            type3 = type3, -- 类型3
            default = parse_default(default) -- 默认值
        })
    end
end

local env = tars.create(fields, host)

-- for k, v in pairs(getmetatable(env)) do
--     print(k, v)
-- end

local s = tars.encode(env, host["TQuickIdleDb"], {
    iFreeTime = 999,
    iBuyTime = 888,
    iAlreadyBuyTime = 314,
    iRefreshTime = 2145,
    iLifeNums = 18274
})

print(s:byte(1, -1))

local t = io.popen("echo AQPnEQN4IQE6MQhhQUdi | base64 -d"):read("*a")
print(t:byte(1, -1))

