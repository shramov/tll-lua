#include "forward.h"

int Forward::_init(const tll::Channel::Url &url, tll::Channel *master)
{
	auto reader = channel_props_reader(url);
	_prefix_compat = reader.getT("prefix-compat", false);
	if (!reader)
		return _log.fail(EINVAL, "Invalid url: {}", reader.error());

	if (check_channels_size<Input>(1, 1))
		return EINVAL;
	if (check_channels_size<Output>(1, 1))
		return EINVAL;

	_output = _channels.get<Output>().front().first;
	_input = _channels.get<Input>().front().first;
	return Base::_init(url, master);
}

int Forward::_open(const tll::ConstConfig &cfg)
{
	if (auto s = _output->state(); s != tll::state::Active)
		return _log.fail(EINVAL, "Output is not Active: {}", tll_state_str(s));
	_output_scheme = _output->scheme();

	_input_scheme = nullptr;
	if (auto s = _input->state(); s == tll::state::Active)
		_input_scheme = _input->scheme();

	if (auto r = _lua_open(); r)
		return r;

	_on_data_name = "";
	lua_getglobal(_lua, "tll_on_data");
	if (lua_isfunction(_lua, -1))
		_on_data_name = "tll_on_data";
	lua_pop(_lua, 1);

	if (_on_data_name.empty())
		return _log.fail(EINVAL, "Can not find callback function");

	luaT_push<tll::lua::Channel>(_lua, { self(), &_encoder });
	lua_setglobal(_lua, "tll_self");

	luaT_push<tll::lua::Channel>(_lua, { _output, &_encoder });
	lua_setglobal(_lua, "tll_self_output");

	luaT_push<tll::lua::Channel>(_lua, { _input, &_encoder });
	lua_setglobal(_lua, "tll_self_input");

	if (_prefix_compat) {
		lua_pushlightuserdata(_lua, this->channelT());
		lua_pushcclosure(_lua, _lua_forward, 1);
		lua_setglobal(_lua, "tll_callback");
	}

	lua_pushlightuserdata(_lua, this->channelT());
	lua_pushcclosure(_lua, _lua_forward, 1);
	lua_setglobal(_lua, "tll_output_post");

	if (auto r = _lua_on_open(cfg); r)
		return r;

	return Base::_open(cfg);
}

int Forward::callback_tag(TaggedChannel<Input> * c, const tll_msg_t *msg)
{
	if (msg->type != TLL_MESSAGE_DATA) {
		if (msg->type == TLL_MESSAGE_STATE && msg->msgid == tll::state::Active)
			_input_scheme = c->scheme();
		return 0;
	}

	auto ref = _lua.copy();
	auto guard = tll::lua::StackGuard(ref);

	lua_getglobal(ref, _on_data_name.c_str());
	auto args = _lua_pushmsg(msg, _input_scheme, c, true);
	if (args < 0)
		return state_fail(EINVAL, "Failed to push message to Lua");

	if (lua_pcall(ref, args, 1, 0)) {
		auto text = fmt::format("Lua function {} failed: {}\n  on", _on_data_name, lua_tostring(ref, -1));
		tll_channel_log_msg(_input, _log.name(), tll::logger::Error, _dump_error, msg, text.data(), text.size());
		state(tll::state::Error);
		return EINVAL;
	}

	return 0;
}
