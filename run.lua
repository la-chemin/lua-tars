local tars = require "tars_wrapper"

local text = [[
struct TBook {
    0 required int iId;
    1 optional string sName;
    2 optional long iWhen;
    3 optional short i1;
    4 optional byte i2;
    5 optional unsigned byte i3;
};

struct TStudent {
    0 optional long iBirth;
    1 optional string sId;
    2 optional int iGrade;
    3 optional map<int, TBook> mBook;
};]]

local function escape(v)
    if type(v) == "string" then
        return ('"%s"'):format(v)
    else
        return v
    end
end

local function dump(v)
    local b = {}
    for key, val in pairs(v) do
        table.insert(b, ("[%s] = %s"):format(escape(key), escape(val)))
    end
    return table.concat(b, ", ")
end

local context = tars.parse(text);
-- print(context:dump())
local s1 = context:encodeStruct("TBook", {
    iId = nil,
    sName = "hello, world",
    iWhen = nil})
-- print("编码1", #s1, ("[%s]"):format(table.concat({s1:byte(0, -1)}, ", ")))
print("解码1", dump(context:decodeStruct("TBook", s1)))

local s2 = context:encodeList(tars.INT8, {1,0,0,0,0,1,2,3,4,5,6})
-- print("编码2", #s2, ("[%s]"):format(table.concat({s2:byte(0, -1)}, ", ")))
print("解码2", dump(context:decodeList(tars.INT8, s2)))

local s3 = context:encodeMap(tars.INT8, tars.STRING, {"world", "hello", "say i love you"})
-- print("编码3", #s3, ("[%s]"):format(table.concat({s3:byte(0, -1)}, ", ")))
print("解码3", dump(context:decodeMap(tars.INT8, tars.STRING, s3)))

local s4 = context:encodeStruct("TStudent", {
    iBirth = os.time(),
    sId = "to the violet sky",
    iGrade = 999123,
    mBook = {
        [3] = {sName = "金瓶梅",},
        [4] = {sName = "灯草和尚", iWhen = 2938},
    },
})

local res = context:decodeStruct("TStudent", s4)
print("解码", dump(res), dump(res.mBook[3]))
