// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Pavel Shramov <shramov@mexmat.net>

#ifndef _FORWARD_H
#define _FORWARD_H

#include "tll/lua/base.h"

#include "tll/channel/tagged.h"

using tll::channel::Input;
using tll::channel::Output;
using tll::channel::TaggedChannel;

class Forward : public tll::lua::LuaBase<Forward, tll::channel::Tagged<Forward, Input, Output>>
{
	tll::Channel * _output = nullptr;
	const tll::Scheme * _output_scheme = nullptr;
	tll::Channel * _input = nullptr;
	const tll::Scheme * _input_scheme = nullptr;

	std::string _on_data_name;
	bool _prefix_compat = false;

 public:
	using Base = tll::lua::LuaBase<Forward, tll::channel::Tagged<Forward, Input, Output>>;

	static constexpr std::string_view param_prefix() { return "lua"; }
	static constexpr std::string_view channel_protocol() { return "lua-forward"; }

	int _init(const tll::Channel::Url &, tll::Channel *master);
	int _open(const tll::ConstConfig &cfg);

	int callback_tag(TaggedChannel<Input> * c, const tll_msg_t *msg);
	int callback_tag(TaggedChannel<Output> * c, const tll_msg_t *msg) { return 0; }

 private:
	static int _lua_forward(lua_State * lua)
	{
		if (auto self = _lua_self(lua, 1); self) {
			auto msg = self->_encoder.encode_stack(lua, self->_output_scheme, self->_output, 0);
			if (!msg) {
				self->_log.error("Failed to convert message: {}", self->_encoder.error);
				return luaL_error(lua, "Failed to convert message");
			}
			self->_output->post(msg);
			return 0;
		}
		return luaL_error(lua, "Non-userdata value in upvalue");
	}
};

#endif//_FORWARD_H
