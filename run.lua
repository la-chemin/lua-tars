local tars = require "tars_wrapper"

local text = [[
struct TBook {
    0 require int iId;
    1 optional string sName;
    2 optional long iWhen;
};

struct TBook1 {
    0 require int iId;
    1 optional string sName;
    2 optional long iWhen;
    3 optional string sComment;
};

struct TBook2 {
    0 require int iId;
    1 optional string sName;
    2 optional long iWhen;
    3 optional TBook stBook1;
    4 optional map<int, int> mExtra1;
    4 vector<string> vExtra2;
};


struct TStudent {
    0 optional long iBirth;
    1 optional string sId;
    2 optional int iGrade;
    3 optional map<int, TBook1> mBook;
    4 optional int iVersion;
};

struct TStudent1 {
    0 optional long iBirth;
    1 optional string sId;
    2 optional int iGrade;
    3 optional map<int, TBook> mBook;
    4 optional int iVersion;
};
]]

local context = tars.parse(text);

-- print(context:tars.toJson())

local s1 = context:encodeStruct("TBook", {
    iId = nil,
    sName = "hello, world",
    iWhen = nil})
print("测试普通结构体编码和解码", tars.toJson(context:decodeStruct("TBook", s1)))


local s2 = context:encodeList(tars.INT8, {1,0,0,0,0,1,2,3,4,5,6})
print("测试数字数组的编解码", tars.toJson(context:decodeList(tars.INT8, s2)))


local s3 = context:encodeMap(tars.INT8, tars.STRING, {"world", "hello", "say i love you"})
print("测试普通字典的编解码", tars.toJson(context:decodeMap(tars.INT8, tars.STRING, s3)))


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
print("测试复合结构体编解码", tars.toJson(res), tars.toJson(res.mBook[3]))


local s5  = context:encodeStruct("TBook1", {
    iId = 274, sName = "绿箭口香糖", iWhen = 26492, sComment = "绿色生活",
})

print("测试字段协议兼容", tars.toJson(context:decodeStruct("TBook", s5)))

local s6 = context:encodeStruct("TStudent", {
    iBirth = 298992,
    sId = "这是一行字符串",
    iGrade = 114121,
    mBook = {
        [292] = {iId = 18264, sName = "复合维生素B片说明书", iWhen = 2928929, sComment = "真实有效"}
    },
    iVersion = 9527,
})

print("测试嵌套结构体协议兼容", tars.toJson(context:decodeStruct("TStudent1", s6)))

local s7 = context:encodeStruct("TBook2", {
    iId = 9745, sName = "雀巢咖啡", iWhen = 2751, stBook1 = {
        iId = 415, sName = "小黄鸭", iWhen  = 7542, sComment = "醇香原味"
    },
    mExtra1 = {[232] = 732, [201] = 125},
    vExtra2 = {"维生素C片", "适应症", "用于预防坏血症"},
})
print("测试结构体协议兼容", tars.toJson(context:decodeStruct("TBook", s7)))
