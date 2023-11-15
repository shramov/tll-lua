#include <tll/channel/module.h>

#include "measure.h"
#include "prefix.h"
#include "tcp.h"

TLL_DEFINE_IMPL(LuaTcp);
TLL_DEFINE_IMPL(LuaPrefix);
TLL_DEFINE_IMPL(tll::lua::LuaMeasure);

TLL_DEFINE_MODULE(LuaTcp, LuaPrefix, tll::lua::LuaMeasure);
