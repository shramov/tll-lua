#include "measure.h"

using namespace measure;

int LuaMeasure::_init(const tll::Channel::Url &url, tll::Channel *master)
{
	auto reader = channel_props_reader(url);
	_code = reader.getT<std::string>("code");
	_manual_open = reader.getT("open-mode", false, {{"lua", true}, {"normal", false}});
	if (!reader)
		return _log.fail(EINVAL, "Invalid url: {}", reader.error());

	if (auto count = _channels.get<Output>().size(); count != 1)
		return _log.fail(EINVAL, "Need exactly one output, got {}", count);

	auto output = _channels.get<Output>().front().first;
	auto control = output->scheme(TLL_MESSAGE_CONTROL);
	if (!control)
		return _log.fail(EINVAL, "Output '{}' has not control scheme", output->name());

	auto time = control->lookup("Time");
	if (!time)
		return _log.fail(EINVAL, "Output has no 'Time' message");
	_output_time_msgid = time->msgid;

	if (auto count = _channels.get<Input>().size(); count != 1)
		return _log.fail(EINVAL, "Need exactly one input, got {}", count);

	return Base::_init(url, master);
}

int LuaMeasure::_open(const tll::ConstConfig &props)
{
	unique_lua_ptr_t lua_ptr(luaL_newstate(), lua_close);
	auto lua = lua_ptr.get();
	if (!lua)
		return _log.fail(EINVAL, "Failed to create lua state");

	luaL_openlibs(lua);
	LuaT<reflection::Array>::init(lua);
	LuaT<reflection::Message>::init(lua);
	LuaT<reflection::Union>::init(lua);
	LuaT<reflection::Bits>::init(lua);

	if (_code.substr(0, 7) == "file://") {
		if (luaL_loadfile(lua, _code.substr(7).c_str()))
			return this->_log.fail(EINVAL, "Failed to load file '{}': {}", _code, lua_tostring(lua, -1));
	} else {
		if (luaL_loadstring(lua, _code.c_str()))
			return this->_log.fail(EINVAL, "Failed to load source code '{}':\n{}", lua_tostring(lua, -1), _code);
	}

	if (lua_pcall(lua, 0, 0, 0))
		return this->_log.fail(EINVAL, "Failed to init globals: {}", lua_tostring(lua, -1));

	lua_getglobal(lua, "luatll_on_data");
	if (!lua_isfunction(lua, -1))
		return _log.fail(EINVAL, "Function luatll_on_data not defined");
	lua_pop(lua, 1);

	lua_getglobal(lua, "luatll_open");
	if (lua_isfunction(lua, -1)) {
		if (lua_pcall(lua, 0, 0, 0))
			return _log.fail(EINVAL, "Lua open (luatll_open) failed: {}", lua_tostring(lua, -1));
	}

	_ptr.reset(lua_ptr.release());
	_lua = _ptr.get();

	if (auto r = Base::_open(props); r)
		return r;
	
	if (!_manual_open)
		state(tll::state::Active);
	return 0;
}

int LuaMeasure::_close()
{
	if (_lua) {
		lua_getglobal(_lua, "luatll_close");
		if (lua_isfunction(_lua, -1)) {
			if (lua_pcall(_lua, 0, 0, 0))
				_log.warning("Lua close (luatll_close) failed: {}", lua_tostring(_lua, -1));
		}
	}

	_ptr.reset();
	_lua = nullptr;
	return Base::_close();
}

int LuaMeasure::callback_tag(TaggedChannel<Input> * c, const tll_msg_t *msg)
{
	if (msg->type != TLL_MESSAGE_DATA)
		return 0;

	tll::scheme::Message * message = nullptr;
	lua_getglobal(_lua, "luatll_on_data");
	lua_pushinteger(_lua, msg->seq);
	auto scheme = c->scheme();
	if (scheme) {
		auto message = scheme->lookup(msg->msgid);
		if (!message)
			return _log.fail(ENOENT, "Message {} not found", msg->msgid);
		lua_pushstring(_lua, message->name);
		luaT_push(_lua, reflection::Message { message, tll::make_view(*msg) });
	} else {
		lua_pushnil(_lua);
		lua_pushlstring(_lua, (const char *) msg->data, msg->size);
	}
	lua_pushinteger(_lua, msg->msgid);
	lua_pushinteger(_lua, msg->addr.i64);
	lua_pushinteger(_lua, msg->time);
	if (lua_pcall(_lua, 6, 1, 0)) {
		_log.warning("Lua filter failed for {}:{}: {}", message ? message->name : "", msg->seq, lua_tostring(_lua, -1));
		lua_pop(_lua, 1);
		return EINVAL;
	}
	
	if (!lua_isinteger(_lua, -1)) {
		if (!lua_isstring(_lua, -1)) {
			lua_pop(_lua, 1);
			return _log.fail(EINVAL, "Invalid return value from lua: not integer and not string");
		}
		auto r = luaT_tostringview(_lua, -1);
		if (r == "active") {
			if (state() == tll::state::Opening)
				state(tll::state::Active);
		} else if (r == "close") {
			if (state() != tll::state::Closing) {
				lua_pop(_lua, 1);
				close();
				return 0;
			}
		} else
			_log.info("Lua code reported message: {}", r);
		lua_pop(_lua, 1);
	}

	auto seq = lua_tointeger(_lua, -1);
	lua_pop(_lua, 1);

	_log.debug("Lua reported seq {}", seq);

	if (seq < 0)
		return 0;

	auto it = _request_time.find(seq);
	if (it == _request_time.end()) {
		_log.debug("Store response time for {}", seq);
		if (_response_time.size() == _map_size)
			_response_time.erase(_response_time.begin());
		_response_time[seq] = msg->time;
		return 0;
	}

	_report(seq, it->second, msg->time);
	_request_time.erase(it);

	return 0;
}

int LuaMeasure::callback_tag(TaggedChannel<Output> * c, const tll_msg_t *msg)
{
	if (msg->type != TLL_MESSAGE_CONTROL)
		return 0;
	if (msg->msgid != _output_time_msgid)
		return 0;

	auto time = * (const long long *) msg->data;
	_log.debug("Request with seq {}, time {}", msg->seq, time);

	auto it = _response_time.find(msg->seq);
	if (it == _response_time.end()) {
		_log.debug("Store request time for {}", msg->seq);
		if (_request_time.size() == _map_size)
			_request_time.erase(_request_time.begin());
		_request_time[msg->seq] = time;
		return 0;
	}

	_report(msg->seq, time, it->second);
	_response_time.erase(it);

	return 0;
}

int LuaMeasure::_report(long long seq, long long req, long long resp)
{
	auto dt = resp - req;
	_log.info("TIME: RTT {}: {}ns", seq, dt);
	auto stat = this->stat();
	if (stat) {
		auto page = stat->acquire();
		if (page) {
			page->rtt = dt;
			stat->release(page);
		}
	}
	return 0;
}
