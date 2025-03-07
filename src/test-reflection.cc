/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "tll/lua/luat.h"
#include "tll/lua/reflection.h"
#include "tll/util/time.h"

#include "gtest/gtest.h"

#include <string_view>
#include <variant>

// TYPED_TEST_CASE is deprecated
#ifndef TYPED_TEST_SUITE
#define TYPED_TEST_SUITE TYPED_TEST_CASE
#endif

using namespace tll::lua;
using namespace std::literals::string_view_literals;
using nullptr_t = std::nullptr_t;

static const char * SCHEME = R"(yamls://
- name: simple
  msgid: 10
  fields:
    - {name: i8, type: int8}
    - {name: i16, type: int16}
    - {name: i32, type: int32}
    - {name: i64, type: int64}
    - {name: u8, type: uint8}
    - {name: u16, type: uint16}
    - {name: u32, type: uint32}
    - {name: u64, type: uint64}
    - {name: d, type: double}
    - {name: b8, type: byte8}
    - {name: s16, type: byte16, options.type: string}
    - {name: l16, type: 'int16[8]'}

- name: outer 
  msgid: 10
  fields:
    - {name: s, type: simple}
    - {name: l, type: 'simple[8]'}
    - {name: p, type: '*simple'}

- name: uniontest
  msgid: 20
  fields:
    - {name: u, type: union, union: [{name: i32, type: int32}, {name: b32, type: byte32}, {name: m, type: simple}]}

- name: bits
  msgid: 20
  fields:
    - {name: bits, type: uint32, options.type: bits, bits: [a, b, c]}

- name: d128
  msgid: 30
  fields:
    - {name: decimal, type: decimal128}

- name: enum
  msgid: 40
  fields:
    - {name: f0, type: uint16, options.type: enum, enum: {A: 10, B: 20}}

- name: Time
  msgid: 50
  fields:
    - {name: ns, type: uint64, options.type: time_point, options.resolution: ns}
    - {name: us, type: int64, options.type: time_point, options.resolution: us}
    - {name: ms, type: int64, options.type: time_point, options.resolution: ms}
    - {name: s, type: double, options.type: time_point, options.resolution: second}
    - {name: day, type: uint32, options.type: time_point, options.resolution: day}
)";

namespace generated {
struct __attribute__((__packed__)) simple
{
	int8_t i8;
	int16_t i16;
	int32_t i32;
	int64_t i64;
	uint8_t u8;
	uint16_t u16;
	uint32_t u32;
	uint64_t u64;
	double d;
	char b8[8];
	char s16[16];
	int8_t l16_size;
	int16_t l16[8];
};

struct __attribute__((__packed__)) outer
{
	simple s;
	int8_t l_size;
	simple l[8];
	tll_scheme_offset_ptr_t p;
};

struct __attribute__((__packed__)) uniontest
{
	int8_t _tll_type;
	union {
		int32_t i32;
		unsigned char b32[32];
		simple m;
	} u;
};

struct __attribute__((__packed__)) bits
{
	struct __attribute__((__packed__)) _bits_type : public tll::scheme::Bits<uint32_t>
	{
		bool a() const { return get(0); }; void a(bool v) { return set(0, v); };
		bool b() const { return get(1); }; void b(bool v) { return set(1, v); };
		bool c() const { return get(2); }; void c(bool v) { return set(2, v); };
	} bits;
};

}

template <typename T>
T lua_toany(lua_State * lua, int index, T v) { return luaL_checkinteger(lua, index); }

template <>
double lua_toany<double>(lua_State * lua, int index, double) { return luaL_checknumber(lua, index); }

template <>
bool lua_toany<bool>(lua_State * lua, int index, bool) { return lua_toboolean(lua, index); }

template <>
std::string_view lua_toany<std::string_view>(lua_State * lua, int index, std::string_view) { return luaT_checkstringview(lua, index); }

template <>
nullptr_t lua_toany<nullptr_t>(lua_State * lua, int index, nullptr_t) { if (!lua_isnil(lua, index)) luaL_error(lua, "Non NIL value: %d", lua_type(lua, index)); return nullptr; }

template <typename T>
void luaT_pushone(lua_State * lua, const T & value)
{
	if constexpr (std::is_floating_point_v<T>)
		lua_pushnumber(lua, value);
	else if constexpr (std::is_integral_v<T>)
		lua_pushinteger(lua, value);
	else if constexpr (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::string>)
		luaT_pushstringview(lua, value);
	else if constexpr (std::is_same_v<T, const char *>)
		lua_pushstring(lua, value);
	else // Check for CWG2518
		luaL_error(lua, "Unknown type"); // static_assert(false, "Unknown type");
}

unsigned luaT_pushmany(lua_State * lua) { return 0; }

template <typename Arg, typename... Args>
unsigned luaT_pushmany(lua_State * lua, const Arg & arg, const Args & ... args)
{
	luaT_pushone(lua, arg);
	return 1 + luaT_pushmany(lua, args...);
}

static Settings settings = { .enum_mode = Settings::Enum::Object, .decimal128_mode = Settings::Decimal128::Object };

#define ASSERT_LUA_VALUE(lua, msg, v, field) do { \
		tll_msg_t m = {}; \
		m.data = &msg; \
		m.size = sizeof(msg); \
		luaT_push(lua, reflection::Message { message, tll::make_view<const tll_msg_t>(m), settings }); \
		for (auto p : tll::split<'.'>(field)) { \
			std::string tmp(p); \
			lua_getfield(lua, -1, tmp.c_str()); \
		} \
		ASSERT_EQ(lua_toany(lua, -1, v), v); \
		lua_pop(lua, 2); \
	} while(0)

#define ASSERT_LUA(lua, msg, field) ASSERT_LUA_VALUE(lua, msg, s.field, #field)

#define ASSERT_LUA_PCALL(lua, v, function, ...) do { \
		lua_getglobal(lua, function); \
		auto i = luaT_pushmany(lua, __VA_ARGS__); \
		ASSERT_EQ(lua_pcall(lua, i, 1, 0), 0) << fmt::format("Lua function {} failed: {}", function, lua_tostring(lua, -1)); \
		ASSERT_EQ(lua_toany(lua, -1, v), v); \
		lua_pop(lua, 1); \
	} while(0)

unique_lua_ptr_t prepare_lua()
{
	unique_lua_ptr_t lua(luaL_newstate(), lua_close);

	luaL_openlibs(lua.get());
	LuaT<reflection::Array>::init(lua.get());
	LuaT<reflection::Message>::init(lua.get());
	LuaT<reflection::Union>::init(lua.get());
	LuaT<reflection::Bits>::init(lua.get());
	LuaT<reflection::Decimal128>::init(lua.get());
	LuaT<reflection::Enum>::init(lua.get());
	LuaT<tll::lua::TimePoint>::init(lua.get());

	lua_pushcfunction(lua.get(), tll::lua::TimePoint::create);
	lua_setglobal(lua.get(), "tll_time_point");

	return lua;
}

const tll::scheme::Message * lookup(const tll::Scheme *s, std::string_view name)
{
	for (auto m = s->messages; m; m = m->next) {
		if (m->name == name)
			return m;
	}
	return nullptr;
}

TEST(Lua, Reflection)
{
	tll::scheme::SchemePtr scheme(tll::Scheme::load(SCHEME));

	ASSERT_TRUE(scheme);

	auto message = lookup(scheme.get(), "simple");

	ASSERT_NE(message, nullptr);

	auto lua_ptr = prepare_lua();
	auto lua = lua_ptr.get();
	ASSERT_NE(lua, nullptr);

	generated::simple s = {};
	s.i8 = 0x8;
	s.i16 = 0x1616;
	s.i32 = 0x32323232;
	s.i64 = 0x6464646464646464;
	s.u8 = 0x80;
	s.u16 = 0x8080;
	s.u32 = 0x80808080;
	s.u64 = 0x8080808080808080;
	s.d = 123.456;
	memcpy(s.b8, "bytes\x01", 6);
	strcpy(s.s16, "string");
	s.l16_size = 3;
	s.l16[0] = 0x100;
	s.l16[1] = 0x101;
	s.l16[2] = 0x102;
	s.l16[3] = 0x103;

	ASSERT_LUA(lua, s, i8);
	ASSERT_LUA(lua, s, i16);
	ASSERT_LUA(lua, s, i32);
	ASSERT_LUA(lua, s, i64);
	ASSERT_LUA(lua, s, u8);
	ASSERT_LUA(lua, s, u16);
	ASSERT_LUA(lua, s, u32);
	ASSERT_LUA(lua, s, u64);
	ASSERT_LUA(lua, s, d);
	ASSERT_LUA_VALUE(lua, s, std::string_view("bytes\x01\x00\x00", 8), "b8");
	ASSERT_LUA_VALUE(lua, s, std::string_view("string"), "s16");
	ASSERT_LUA_VALUE(lua, s, s.l16[0], "l16.1");
	ASSERT_LUA_VALUE(lua, s, s.l16[1], "l16.2");
	ASSERT_LUA_VALUE(lua, s, s.l16[2], "l16.3");
	//ASSERT_LUA_VALUE(lua, s, s.l16[3], "l16.3");

	struct outer_ptr : public generated::outer
	{
		generated::simple ptr[4];
	} out = {};

	out.s = s;
	out.l_size = 2;
	out.l[0].d = 234.567;
	out.l[0].l16_size = 4;
	out.l[0].l16[0] = 0x200;
	out.l[0].l16[1] = 0x201;
	out.l[0].l16[2] = 0x202;
	out.l[0].l16[3] = 0x203;
	out.l[1].d = 345.678;
	out.l[1].l16_size = 2;
	out.l[1].l16[0] = 0x300;
	out.l[1].l16[1] = 0x301;
	out.l[1].l16[2] = 0x302;

	out.p.offset = (ptrdiff_t) &out.ptr - (ptrdiff_t) &out.p;
	out.p.size = 2;
	out.p.entity = sizeof(generated::simple);

	out.ptr[0].d = 456.78;
	out.ptr[1].d = 567.89;

	message = lookup(scheme.get(), "outer");
	ASSERT_NE(message, nullptr);

	ASSERT_LUA_VALUE(lua, out, std::string_view("bytes\x01\x00\x00", 8), "s.b8");
	ASSERT_LUA_VALUE(lua, out, std::string_view("string"), "s.s16");

	ASSERT_LUA_VALUE(lua, out, out.l[0].d, "l.1.d");
	ASSERT_LUA_VALUE(lua, out, out.l[0].l16[0], "l.1.l16.1");
	ASSERT_LUA_VALUE(lua, out, out.l[0].l16[1], "l.1.l16.2");
	ASSERT_LUA_VALUE(lua, out, out.l[0].l16[2], "l.1.l16.3");
	ASSERT_LUA_VALUE(lua, out, out.l[0].l16[3], "l.1.l16.4");

	ASSERT_LUA_VALUE(lua, out, out.l[1].d, "l.2.d");
	ASSERT_LUA_VALUE(lua, out, out.l[1].l16[0], "l.2.l16.1");
	ASSERT_LUA_VALUE(lua, out, out.l[1].l16[1], "l.2.l16.2");

	ASSERT_LUA_VALUE(lua, out, out.ptr[0].d, "p.1.d");
	ASSERT_LUA_VALUE(lua, out, out.ptr[1].d, "p.2.d");
	//ASSERT_LUA_VALUE(lua, out, out.ptr[2].d, "p.2.d");

	//out.p.size = 100;
	//ASSERT_LUA_VALUE(lua, out, out.ptr[0].d, "p.0.d");
}

TEST(Lua, ReflectionUnion)
{
	tll::scheme::SchemePtr scheme(tll::Scheme::load(SCHEME));

	ASSERT_TRUE(scheme);

	auto message = lookup(scheme.get(), "uniontest");

	ASSERT_NE(message, nullptr);

	auto lua_ptr = prepare_lua();
	auto lua = lua_ptr.get();
	ASSERT_NE(lua, nullptr);

	generated::uniontest u = {};
	u._tll_type = 0;
	u.u.i32 = 100;

	ASSERT_LUA_VALUE(lua, u, std::string_view {"i32"}, "u._tll_type");
	ASSERT_LUA_VALUE(lua, u, 100, "u.i32");
	ASSERT_LUA_VALUE(lua, u, nullptr, "u.b32");

	u._tll_type = 2;
	u.u.m.i8 = 10;
	u.u.m.i16 = 1000;
	u.u.m.i32 = 100000;

	ASSERT_LUA_VALUE(lua, u, std::string_view {"m"}, "u._tll_type");
	ASSERT_LUA_VALUE(lua, u, nullptr, "u.i32");
	ASSERT_LUA_VALUE(lua, u, 10, "u.m.i8");
	ASSERT_LUA_VALUE(lua, u, 1000, "u.m.i16");
	ASSERT_LUA_VALUE(lua, u, 100000, "u.m.i32");
}

TEST(Lua, ReflectionBits)
{
	tll::scheme::SchemePtr scheme(tll::Scheme::load(SCHEME));

	ASSERT_TRUE(scheme);

	auto message = lookup(scheme.get(), "bits");

	ASSERT_NE(message, nullptr);

	auto lua_ptr = prepare_lua();
	auto lua = lua_ptr.get();
	ASSERT_NE(lua, nullptr);

	generated::bits s = {};
	s.bits.a(true);
	s.bits.c(true);
	ASSERT_EQ((uint32_t ) s.bits, (1u << 0) | (1u << 2));
	//s.bits = (1 << 0 | 1 << 2); // a | b

	ASSERT_LUA_VALUE(lua, s, true, "bits.a");
	ASSERT_LUA_VALUE(lua, s, false, "bits.b");
	ASSERT_LUA_VALUE(lua, s, true, "bits.c");

	s.bits.clear();

	ASSERT_LUA_VALUE(lua, s, false, "bits.a");
}

TEST(Lua, ReflectionDecimal1228)
{
	tll::scheme::SchemePtr scheme(tll::Scheme::load(SCHEME));

	ASSERT_TRUE(scheme);

	auto message = lookup(scheme.get(), "d128");

	ASSERT_NE(message, nullptr);

	auto lua_ptr = prepare_lua();
	auto lua = lua_ptr.get();
	ASSERT_NE(lua, nullptr);

	tll::util::Decimal128 value = { 0, 123456, -3 };

	ASSERT_LUA_VALUE(lua, value, std::string_view { "123456.E-3" }, "decimal.string");
	ASSERT_LUA_VALUE(lua, value, 123.456, "decimal.float");
}

TEST(Lua, ReflectionEnum)
{
	tll::scheme::SchemePtr scheme(tll::Scheme::load(SCHEME));

	ASSERT_TRUE(scheme);

	auto message = lookup(scheme.get(), "enum");

	ASSERT_NE(message, nullptr);

	auto lua_ptr = prepare_lua();
	auto lua = lua_ptr.get();
	ASSERT_NE(lua, nullptr);

	uint16_t value = 10;

	ASSERT_LUA_VALUE(lua, value, 10, "f0.int");
	ASSERT_LUA_VALUE(lua, value, std::string_view { "A" }, "f0.string");

	value = 11;

	ASSERT_LUA_VALUE(lua, value, 11, "f0.int");
	ASSERT_LUA_VALUE(lua, value, nullptr, "f0.string");
}

TEST(Lua, TimePoint)
{
	using namespace std::chrono;
#if __cpp_lib_chrono < 201907L
	using days = duration<int, std::ratio<86400, 1>>;
#endif
#pragma pack(push, 1)
	struct Time
	{
		time_point<system_clock, duration<uint64_t, std::nano>> ns;
		time_point<system_clock, duration<int64_t, std::micro>> us;
		time_point<system_clock, duration<int64_t, std::milli>> ms;
		time_point<system_clock, duration<double, std::ratio<1, 1>>> s;
		time_point<system_clock, duration<uint32_t, std::ratio<86400, 1>>> day;
	};
#pragma pack(pop)

	tll::scheme::SchemePtr scheme(tll::Scheme::load(SCHEME));

	ASSERT_TRUE(scheme);

	auto message = lookup(scheme.get(), "Time");

	ASSERT_NE(message, nullptr);

	auto lua_ptr = prepare_lua();
	auto lua = lua_ptr.get();
	ASSERT_NE(lua, nullptr);

	Time value = {};
	auto ts = *tll::conv::to_any<time_point<system_clock, nanoseconds>>("2000-01-02T03:04:05.012345678");
	auto ns = ts.time_since_epoch().count();
	value.ns = ts;
	value.us = time_point_cast<microseconds>(ts);
	value.ms = time_point_cast<milliseconds>(ts);
	value.s = ts;
	value.day = time_point_cast<days>(ts);

	ASSERT_LUA_VALUE(lua, value, "2000-01-02T03:04:05.012345678"sv, "ns.string");
	ASSERT_LUA_VALUE(lua, value, "2000-01-02T03:04:05.012345"sv, "us.string");
	ASSERT_LUA_VALUE(lua, value, "2000-01-02T03:04:05.012"sv, "ms.string");
	ASSERT_LUA_VALUE(lua, value, "2000-01-02T03:04:05.012345671"sv, "s.string"); // Non exact
	ASSERT_LUA_VALUE(lua, value, "2000-01-02"sv, "day.string");

	ASSERT_LUA_VALUE(lua, value, ns / 1000000000., "ns.seconds");
	ASSERT_LUA_VALUE(lua, value, ns / 1000 / 1000000., "us.seconds");
	ASSERT_LUA_VALUE(lua, value, ns / 1000000 / 1000., "ms.seconds");
	ASSERT_LUA_VALUE(lua, value, ns / 1000000000., "s.seconds");
	ASSERT_LUA_VALUE(lua, value, ns / 1000000000 / 86400 * 86400., "day.seconds");

	ASSERT_LUA_VALUE(lua, value, 20000102, "ns.date");
	ASSERT_LUA_VALUE(lua, value, 20000102, "us.date");
	ASSERT_LUA_VALUE(lua, value, 20000102, "ms.date");
	ASSERT_LUA_VALUE(lua, value, 20000102, "s.date");
	ASSERT_LUA_VALUE(lua, value, 20000102, "day.date");

	constexpr std::string_view code = R"(
function ts(...)
	return tll_time_point(...).string
end
)";
	ASSERT_EQ(luaL_loadstring(lua, code.data()), 0);
	ASSERT_EQ(lua_pcall(lua, 0, LUA_MULTRET, 0), 0);

	ASSERT_LUA_PCALL(lua, "2000-01-02T03:04:05"sv, "ts", 2000, 01, 02, 03, 04, 05);
	ASSERT_LUA_PCALL(lua, "2000-01-02T00:00:00"sv, "ts", 2000, 01, 02);
	ASSERT_LUA_PCALL(lua, "2000-01-02T03:04:05.123456789"sv, "ts", 2000, 01, 02, 03, 04, 05, 123456789);
}

int main(int argc, char *argv[])
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
