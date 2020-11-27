/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "luat.h"
#include "reflection.h"

#include "gtest/gtest.h"

#include <string_view>
#include <variant>

// TYPED_TEST_CASE is deprecated
#ifndef TYPED_TEST_SUITE
#define TYPED_TEST_SUITE TYPED_TEST_CASE
#endif

static const char * SCHEME = R"(yamls://
- name: simple
  msgid: 10
  fields:
    - {name: i8, type: int8}
    - {name: i16, type: int16}
    - {name: i32, type: int32}
    - {name: i64, type: int64}
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
)";

namespace generated {
struct __attribute__((__packed__)) simple
{
	int8_t i8;
	int16_t i16;
	int32_t i32;
	int64_t i64;
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

}

template <typename T>
T lua_toany(lua_State * lua, int index, T v) { return luaL_checkinteger(lua, index); }

template <>
double lua_toany<double>(lua_State * lua, int index, double) { return luaL_checknumber(lua, index); }

template <>
std::string_view lua_toany<std::string_view>(lua_State * lua, int index, std::string_view) { return luaT_checkstringview(lua, index); }

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

	return lua;
}

TEST(Lua, Reflection)
{
	tll::scheme::SchemePtr scheme(tll::Scheme::load(SCHEME), tll_scheme_unref);

	ASSERT_TRUE(scheme);

	auto message = scheme->messages;

	ASSERT_EQ(std::string_view("simple"), message->name);

	auto lua_ptr = prepare_lua();
	auto lua = lua_ptr.get();
	ASSERT_NE(lua, nullptr);

	generated::simple s = {};
	s.i8 = 0x8;
	s.i16 = 0x1616;
	s.i32 = 0x32323232;
	s.i64 = 0x6464646464646464;
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
	ASSERT_LUA(lua, s, d);
	ASSERT_LUA_VALUE(lua, s, std::string_view("bytes\x01\x00\x00", 8), "b8");
	ASSERT_LUA_VALUE(lua, s, std::string_view("string"), "s16");
	ASSERT_LUA_VALUE(lua, s, s.l16[0], "l16.0");
	ASSERT_LUA_VALUE(lua, s, s.l16[1], "l16.1");
	ASSERT_LUA_VALUE(lua, s, s.l16[2], "l16.2");
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

	message = message->next;
	ASSERT_NE(message, nullptr);
	ASSERT_STREQ(message->name, "outer");

	ASSERT_LUA_VALUE(lua, out, std::string_view("bytes\x01\x00\x00", 8), "s.b8");
	ASSERT_LUA_VALUE(lua, out, std::string_view("string"), "s.s16");

	ASSERT_LUA_VALUE(lua, out, out.l[0].d, "l.0.d");
	ASSERT_LUA_VALUE(lua, out, out.l[0].l16[0], "l.0.l16.0");
	ASSERT_LUA_VALUE(lua, out, out.l[0].l16[1], "l.0.l16.1");
	ASSERT_LUA_VALUE(lua, out, out.l[0].l16[2], "l.0.l16.2");
	ASSERT_LUA_VALUE(lua, out, out.l[0].l16[3], "l.0.l16.3");

	ASSERT_LUA_VALUE(lua, out, out.l[1].d, "l.1.d");
	ASSERT_LUA_VALUE(lua, out, out.l[1].l16[0], "l.1.l16.0");
	ASSERT_LUA_VALUE(lua, out, out.l[1].l16[1], "l.1.l16.1");

	ASSERT_LUA_VALUE(lua, out, out.ptr[0].d, "p.0.d");
	ASSERT_LUA_VALUE(lua, out, out.ptr[1].d, "p.1.d");
	//ASSERT_LUA_VALUE(lua, out, out.ptr[2].d, "p.2.d");

	//out.p.size = 100;
	//ASSERT_LUA_VALUE(lua, out, out.ptr[0].d, "p.0.d");
}

int main(int argc, char *argv[])
{
	testing::InitGoogleTest(&argc, argv);
	return RUN_ALL_TESTS();
}
