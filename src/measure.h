#ifndef _LUAMEASURE_H
#define _LUAMEASURE_H

#include <tll/channel/tagged.h>

#include "common.h"

namespace tll::lua {

using tll::channel::Input;
using tll::channel::Output;
using tll::channel::TaggedChannel;

class LuaMeasure : public LuaCommon<LuaMeasure, tll::channel::Tagged<LuaMeasure, Input, Output>>
{
	using Base = LuaCommon<LuaMeasure, tll::channel::Tagged<LuaMeasure, Input, Output>>;

	std::map<long long, long long> _response_time; // seq -> timestamp
	std::map<long long, long long> _request_time; // seq -> timestamp

	size_t _map_size = 10000; // Amount of stored entries

	int _output_time_msgid = -1;

	bool _manual_open = false;
 public:
	static constexpr std::string_view channel_protocol() { return "lua-measure"; }
	static constexpr auto open_policy() { return OpenPolicy::Manual; }
	static constexpr auto scheme_policy() { return SchemePolicy::Manual; }

	struct StatType : public Base::StatType
	{
		tll::stat::IntegerGroup<tll::stat::Ns, 'r', 't', 't'> rtt;
	};
	tll::stat::BlockT<StatType> * stat() { return static_cast<tll::stat::BlockT<StatType> *>(this->internal.stat); }

	int _init(const tll::Channel::Url &, tll::Channel *master);
	int _open(const tll::ConstConfig &props);
	int _close();

	int callback_tag(TaggedChannel<Input> * c, const tll_msg_t *msg);
	int callback_tag(TaggedChannel<Output> * c, const tll_msg_t *msg);

	int _report(long long seq, long long req, long long resp);
};

} // namespace tll::lua

#endif//_LUAMEASURE_H
