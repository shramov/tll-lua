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

	void _free()
	{
		config_info().remove("info.stream-open.mode");
		config_info().remove("info.stream-open.seq");
		Base::_free();
	}

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

	static char * _stream_mode(int * len, void * data)
	{
		auto self = static_cast<const Forward *>(data);
		auto seq = self->_output->config().get("info.seq");
		std::string_view r = "initial";
		if (seq && *seq != "-1")
			r = "seq-data";
		*len = r.size();
		return tll_config_value_dup(r.data(), r.size());
	}

	static char * _stream_seq(int * len, void * data)
	{
		auto self = static_cast<const Forward *>(data);
		auto seq = self->_output->config().get("info.seq");
		if (!seq)
			return nullptr;
		*len = seq->size();
		return (char *) seq.release();
	}
};

#endif//_FORWARD_H
