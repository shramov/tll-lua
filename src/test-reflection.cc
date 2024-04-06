/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "tll/lua/luat.h"
#include "tll/lua/reflection.h"

#include "gtest/gtest.h"

#include <string_view>
#include <variant>

// TYPED_TEST_CASE is deprecated
#ifndef TYPED_TEST_SUITE
#define TYPED_TEST_SUITE TYPED_TEST_CASE
#endif

using namespace tll::lua;
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

#define ASSERT_LUA_VALUE(lua, msg, v, field) do { \
		tll_msg_t m = {}; \
		m.data = &msg; \
		m.size = sizeof(msg); \
		luaT_push(lua, reflection::Message { message, tll::make_view<const tll_msg_t>(m) }); \
		for (auto p : tll::split<'.'>(field)) { \
			std::string tmp(p); \
			lua_getfield(lua, -1, tmp.c_str()); \
		} \
		ASSERT_EQ(lua_toany(lua, -1, v), v); \
		lua_pop(lua, 2); \
	} while(0)

#define ASSERT_LUA(lua, msg, field) ASSERT_LUA_VALUE(lua, msg, s.field, #field)

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

int main(int argc, char *argv[])
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
