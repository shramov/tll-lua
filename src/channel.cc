/*
 * Copyright (c) 2020 Pavel Shramov <shramov@mexmat.net>
 *
 * tll is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include "filter.h"
#include "measure.h"

#include <tll/channel/module.h>
#include <tll/channel/tcp.h>
#include <tll/channel/tcp.hpp>

#include "luat.h"
#include "msg.h"

using namespace tll;

class LuaTcp : public tll::channel::Base<LuaTcp>
{
 public:
	static constexpr std::string_view param_prefix() { return "tcp"; }
	static constexpr std::string_view channel_protocol() { return "tcp-lua"; }

	std::optional<const tll_channel_impl_t *> _init_replace(const tll::Channel::Url &url, tll::Channel *master);

	int _init(const tll::Channel::Url &url, tll::Channel * master) { return _log.fail(EINVAL, "Failed to choose proper tcp channel"); }
};

struct Common
{
	std::unique_ptr<lua_State, decltype(&lua_close)> lua = { nullptr, lua_close };
	size_t frame_size = 0;

	std::string code;
};

template <typename T>
class LuaCommon : public T
{
 public:
	static constexpr auto lua_hooks = false;

	std::shared_ptr<Common> _common;

	int _init_lua(const tll::Channel::Url &url, tll::Channel * master);
	int _open_lua();

	int _init(const tll::Channel::Url &url, tll::Channel *master)
	{
		this->_log.info("Common init {}", this->channelT()->lua_hooks);
		if (this->channelT()->lua_hooks) {
			if (_init_lua(url, master))
				return this->_log.fail(EINVAL, "Failed to init Lua parameters");
		}
		return T::_init(url, master);
	}

	int _open(const tll::ConstConfig &props)
	{
		this->_log.info("Common open {}", this->channelT()->lua_hooks);
		if (this->channelT()->lua_hooks) {
			if (_open_lua())
				return this->_log.fail(EINVAL, "Failed to open Lua parameters");
		}
		return T::_open(props);
	}

	int _close()
	{
		if (this->channelT()->lua_hooks) {
			if (this->_common)
				this->_common->lua.reset();
		} else
			this->_common.reset();
		return T::_close();
	}

	void _free()
	{
		this->_common.reset();
		return T::_free();
	}
};

template <typename T>
class LuaSocket : public LuaCommon<tll::channel::TcpSocket<T>>
{
 public:
	static constexpr std::string_view param_prefix() { return "tcp"; }

	int _post(const tll_msg_t *msg, int flags);
	int _process(long timeout, int flags);

	int _open(const tll::ConstConfig &props)
	{
		_pending_unpacked = false;
		return LuaCommon<tll::channel::TcpSocket<T>>::_open(props);
	}

 private:
	bool _pending_unpacked = false;
	tll_msg_t _pending_msg = {};

	int _pending();
};

class LuaTcpClient : public tll::channel::TcpClient<LuaTcpClient, LuaSocket<LuaTcpClient>>
{
 public:
	static constexpr std::string_view param_prefix() { return "tcp"; }
	static constexpr std::string_view impl_protocol() { return "tcp-client-lua"; } // Only visible in logs

	static constexpr auto lua_hooks = true;
};

class ChLuaSocket : public LuaSocket<ChLuaSocket>
{
 public:
	using Base = LuaSocket<ChLuaSocket>;

	static constexpr std::string_view param_prefix() { return "tcp"; }
	static constexpr std::string_view impl_protocol() { return "tcp-socket-lua"; } // Only visible in logs

	int _init(const tll::Channel::Url &url, tll::Channel *master);
};

class LuaTcpServer : public LuaCommon<tll::channel::TcpServer<LuaTcpServer, ChLuaSocket>>
{
 public:
	static constexpr std::string_view param_prefix() { return "tcp"; }
	static constexpr std::string_view impl_protocol() { return "tcp-server-lua"; } // Only visible in logs

	static constexpr auto socket_impl_policy() { return SocketImplPolicy::Fixed; }

	static constexpr auto lua_hooks = true;

	std::shared_ptr<Common> lua_common() { return _common; }
};

int ChLuaSocket::_init(const tll::Channel::Url &url, tll::Channel *master)
{
	auto server = tll::channel_cast<LuaTcpServer>(master);
	if (!server)
		return _log.fail(EINVAL, "Need tcp-lua server as master channel");
	_common = server->lua_common();

	return Base::_init(url, master);
}

std::optional<const tll_channel_impl_t *> LuaTcp::_init_replace(const tll::Channel::Url &url, tll::Channel *master)
{
	auto reader = channel_props_reader(url);
	auto client = reader.getT("mode", true, {{"client", true}, {"server", false}});
	if (!reader)
		return _log.fail(nullptr, "Invalid url: {}", reader.error());
	if (client)
		return &LuaTcpClient::impl;
	else
		return &LuaTcpServer::impl;
}

template <typename T>
int LuaCommon<T>::_init_lua(const tll::Channel::Url &url, tll::Channel *master)
{
	auto reader = this->channel_props_reader(url);
	auto code = reader.template getT<std::string>("code");
	if (!reader)
		return this->_log.fail(EINVAL, "Invalid url: {}", reader.error());
	_common.reset(new Common);
	_common->code = code;
	return 0;
}

template <typename T>
int LuaCommon<T>::_open_lua()
{
	std::unique_ptr<lua_State, decltype(&lua_close)> lua_ptr(luaL_newstate(), lua_close);
	auto lua = lua_ptr.get();
	if (!lua)
		return this->_log.fail(EINVAL, "Failed to create lua state");

	luaL_openlibs(lua);
	LuaT<tll_msg_t *>::init(lua);
	LuaT<const tll_msg_t *>::init(lua);

	std::string_view code = this->_common->code;
	if (code.substr(0, 7) == "file://") {
		if (luaL_loadfile(lua, code.substr(7).data()))
			return this->_log.fail(EINVAL, "Failed to load file '{}': {}", code, lua_tostring(lua, -1));
	} else {
		if (luaL_loadstring(lua, code.data()))
			return this->_log.fail(EINVAL, "Failed to load source code '{}':\n{}", lua_tostring(lua, -1), code);
	}

	if (lua_pcall(lua, 0, 0, 0))
		return this->_log.fail(EINVAL, "Failed to init globals: {}", lua_tostring(lua, -1));

	lua_getglobal(lua, "frame_size");
	auto size = lua_tointeger(lua, -1);
	if (size <= 0 || size > 64)
		return this->_log.fail(EINVAL, "Invalid frame size: {}", size);
	this->_log.info("Lua frame size: {}", size);
	this->_common->frame_size = size;

	this->_common->lua.reset(lua_ptr.release());
	return 0;
}

template <typename T>
int LuaSocket<T>::_post(const tll_msg_t *msg, int flags)
{
	if (msg->type != TLL_MESSAGE_DATA)
		return 0;

	auto lua = this->_common->lua.get();
	lua_getglobal(lua, "frame_pack");
	luaT_push(lua, msg);
	if (lua_pcall(lua, 1, 1, 0))
		return this->_log.fail(EINVAL, "Frame pack failed: {}", lua_tostring(lua, -1));
	auto frame = luaT_tostringview(lua, -1);

	this->_log.debug("Post {} + {} bytes of data", frame.size(), msg->size);
	int r = this->template _sendv(frame, *msg);

	lua_pop(lua, 1); // Pop result

	if (r < 0)
		return this->_log.fail(errno, "Failed to post data: {}", strerror(errno));
	else if ((size_t) r != frame.size() + msg->size)
		return this->_log.fail(errno, "Failed to post data (truncated): {}", strerror(errno));
	return 0;
}

template <typename T>
int LuaSocket<T>::_pending()
{
	const auto frame_size = this->_common->frame_size;
	if (!_pending_unpacked) {
		auto frame = this->template rdataT<char>(0, frame_size);
		if (!frame)
			return EAGAIN;

		_pending_msg = {};

		auto lua = this->_common->lua.get();
		lua_getglobal(lua, "frame_unpack");
		lua_pushlstring(lua, frame, frame_size);
		luaT_push(lua, &_pending_msg);
		if (lua_pcall(lua, 2, 1, 0))
			return this->_log.fail(EINVAL, "Failed to unpack frame: {}", lua_tostring(lua, -1));
		lua_pop(lua, 1);
		_pending_unpacked = true;
	}

	// Check for pending data
	auto data = this->template rdataT<char>(frame_size, _pending_msg.size);
	if (!data) {
		if (frame_size + _pending_msg.size > this->_rbuf.size())
			return this->_log.fail(EMSGSIZE, "Message size {} too large", _pending_msg.size);
		this->_dcaps_pending(false);
		return EAGAIN;
	}

	_pending_msg.data = (void *) data;
	_pending_msg.addr = this->msg_addr();
	this->rdone(frame_size + _pending_msg.size);
	this->_dcaps_pending(this->template rdataT<char>(0, frame_size));
	this->_callback_data(&_pending_msg);
	return 0;
}

template <typename T>
int LuaSocket<T>::_process(long timeout, int flags)
{
	auto r = this->_pending();
	if (r != EAGAIN)
		return r;

	this->_rbuf.shift();
	auto s = this->_recv(this->_rbuf.available());
	if (!s)
		return EINVAL;
	if (!*s)
		return EAGAIN;
	this->_log.debug("Got {} bytes of data", *s);
	return this->_pending();
}

TLL_DEFINE_IMPL(LuaTcpClient);
TLL_DEFINE_IMPL(LuaTcpServer);
TLL_DEFINE_IMPL(ChLuaSocket);
TLL_DEFINE_IMPL(tll::channel::TcpServerSocket<LuaTcpServer>);

TLL_DEFINE_IMPL(LuaTcp);
TLL_DEFINE_IMPL(LuaFilter);
TLL_DEFINE_IMPL(measure::LuaMeasure);

TLL_DEFINE_MODULE(LuaTcp, LuaFilter, measure::LuaMeasure);
