/**
 * Copyright (c) 2006-2025 LOVE Development Team
 *
 * This software is provided 'as-is', without any express or implied
 * warranty.  In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 **/

#include "wrap_Event.h"

// LOVE
#include "common/runtime.h"
#include "common/Reference.h"
#include "sdl/Event.h"

#include <algorithm>

#include <lua.h>
// Shove the wrap_Event.lua code directly into a raw string literal.
static const char event_lua[] =
#include "wrap_Event.lua"
;

namespace love
{
namespace event
{

#define instance() (Module::getInstance<Event>(Module::M_EVENT))

static const char *extract_cstr(Variant v) {
	switch (v.getType()) {
		case Variant::Type::SMALLSTRING:
			return v.getData().smallstring.str;
		case Variant::Type::STRING:
			return v.getData().string->str;
		default:
			return NULL;
	}
}

// Adds the "restart" string to the beginning of arguments,
// if they don't already have it
// returns if the restart argument was added
static bool args_add_restart(std::vector<Variant> &args) {
	const char *restart = "restart";

	if (args.empty()) {
		args.emplace_back(restart, strlen(restart));
		return true;
	}

	std::vector<Variant>::const_iterator begin = args.begin();
	const Variant& first = *begin;
	const char* argstr = extract_cstr(first);
	if (argstr == NULL || strcmp(argstr, restart) != 0) {
		args.emplace(begin, restart, strlen(restart));
		return true;
	}

	return false;
}

// Changes the restart event argument
// so that Foxglove will restart the current game,
// instead of restarting into the launcher
static void modify_restart_arg(lua_State *L, std::vector<Variant> &args) {
	Variant prev_restartval;
	luax_catchexcept(L, [&]() {
			lua_getglobal(L, "Foxglove_restart");
			prev_restartval = luax_checkvariant(L, -1);
	});
	if (prev_restartval.getType() != Variant::Type::TABLE) {
		return;
	}

	bool has_restartarg = args.size() >= 2;
	Variant restartval = has_restartarg ? args[1] : Variant();
	std::vector<std::pair<Variant, Variant>> entries =
		prev_restartval.getData().table->pairs;
	bool restartcond_found = false;
	bool restartval_found = false;
	const char *restartcond_key = "foxglove_replace_restartval";
	const char *restartval_key = "foxglove_restartval";
	for (std::pair<Variant, Variant> &entry : entries) {
		const char* keystr = extract_cstr(entry.first);
		if (keystr == NULL) {
			continue;
		}

		if (strcmp(keystr, restartcond_key) == 0) {
			entry.second = Variant(true);
			restartcond_found = true;
		} else if (strcmp(keystr, restartval_key) == 0) {
			entry.second = restartval;
			restartval_found = true;
		}
	}
	if (!restartcond_found) {
		entries.emplace_back(Variant(restartcond_key), Variant(true));
	}
	if (!restartval_found) {
		entries.emplace_back(Variant(restartval_key), restartval);
	}

	if (has_restartarg) {
		args[1] = prev_restartval;
	} else {
		args.push_back(prev_restartval);
	}
}

static int luax_pushmessage(lua_State *L, const Message &m)
{
	luax_pushstring(L, m.name);

	for (const Variant &v : m.args)
		luax_pushvariant(L, v);

	return (int) m.args.size() + 1;
}

static int w_poll_i(lua_State *L)
{
	Message *m = nullptr;

	if (instance()->poll(m) && m != nullptr)
	{
		int args = luax_pushmessage(L, *m);
		m->release();
		return args;
	}

	// No pending events.
	return 0;
}

int w_pump(lua_State *L)
{
	float waitTimeout = (float)luaL_optnumber(L, 1, 0.0f);
	luax_catchexcept(L, [&]() { instance()->pump(waitTimeout); });
	return 0;
}

int w_wait(lua_State *L)
{
	luax_markdeprecated(L, 1, "love.event.wait", API_FUNCTION, DEPRECATED_REPLACED, "waitTimeout parameter in love.event.pump");

	Message *m = nullptr;
	luax_catchexcept(L, [&]() { m = instance()->wait(); });
	if (m != nullptr)
	{
		int args = luax_pushmessage(L, *m);
		m->release();
		return args;
	}

	return 0;
}

int w_push(lua_State *L)
{
	std::string name = luax_checkstring(L, 1);
	std::vector<Variant> vargs;


	int nargs = lua_gettop(L);
	for (int i = 2; i <= nargs; i++)
	{
		if (lua_isnoneornil(L, i))
			break;

		luax_catchexcept(L, [&]() { vargs.push_back(luax_checkvariant(L, i)); });

		if (vargs.back().getType() == Variant::UNKNOWN)
		{
			vargs.clear();
			return luaL_error(L, "Argument %d can't be stored safely\nExpected boolean, number, string or userdata.", i);
		}
	}

	// Pushed quit events should actually be restarts,
	// unless sent from launcher ("foxglove_quit")
	std::string quit("quit");
	if (name.compare("foxglove_quit") == 0) {
		name = quit;
	} else if (name.compare(quit) == 0 && !args_add_restart(vargs)) {
		modify_restart_arg(L, vargs);
	}

	StrongRef<Message> m(new Message(name, vargs), Acquire::NORETAIN);

	instance()->push(m);
	luax_pushboolean(L, true);
	return 1;
}

int w_clear(lua_State *L)
{
	luax_catchexcept(L, [&]() { instance()->clear(); });
	return 0;
}

int w_quit(lua_State *L)
{
	luax_catchexcept(L, [&]() {
		std::vector<Variant> args;
		for (int i = 1; i <= std::max(1, lua_gettop(L)); i++)
			args.push_back(luax_checkvariant(L, i));

		// Same situation as in w_push
		if (!args_add_restart(args)) {
			modify_restart_arg(L, args);
		}

		StrongRef<Message> m(new Message("quit", args), Acquire::NORETAIN);
		instance()->push(m);
	});

	luax_pushboolean(L, true);
	return 1;
}

int w_restart(lua_State *L)
{
	luax_catchexcept(L, [&]() {
		std::vector<Variant> args;
		args.emplace_back("restart", strlen("restart"));

		for (int i = 1; i <= lua_gettop(L); i++)
			args.push_back(luax_checkvariant(L, i));

		modify_restart_arg(L, args);

		StrongRef<Message> m(new Message("quit", args), Acquire::NORETAIN);
		instance()->push(m);
	});

	luax_pushboolean(L, true);
	return 1;
}

struct DrawCallbackData
{
	Variant returnValues[2];
	Reference *r;
};

static int drawCallbackInner(lua_State *L)
{
	auto data = (DrawCallbackData *)lua_touserdata(L, 1);

	data->r->push(L);

	lua_call(L, 0, 2);

	data->returnValues[0] = luax_checkvariant(L, -2, false);
	data->returnValues[1] = luax_checkvariant(L, -1, false);

	lua_pop(L, 2);
	return 0;
}

static void drawCallback(void *context, Variant *returnVal0, Variant *returnVal1)
{
	auto r = (Reference *)context;
	lua_State *L = r->getPinnedL();

	DrawCallbackData data = {};
	data.r = r;

	// pcall into C code to catch errors from checkvariant as well as the lua_call.
	int err = lua_cpcall(L, drawCallbackInner, &data);

	// Unfortunately, this eats the stack trace, too bad.
	if (err != 0)
		throw love::Exception("Error in modal draw callback: %s", lua_tostring(L, -1));

	*returnVal0 = data.returnValues[0];
	*returnVal1 = data.returnValues[1];
}

static void cleanupCallback(void *context)
{
	auto r = (Reference *)context;
	delete r;
}

int w_setModalDrawCallback(lua_State *L)
{
	Event::ModalDrawData data = {};

	if (!lua_isnoneornil(L, 1))
	{
		luaL_checktype(L, 1, LUA_TFUNCTION);

		// Save the callback function as a Reference.
		lua_pushvalue(L, 1);
		Reference *r = new Reference(L);
		lua_pop(L, 1);

		data.draw = drawCallback;
		data.cleanup = cleanupCallback;
		data.context = r;
	}

	luax_catchexcept(L, [&]() { instance()->setModalDrawData(data); });
	return 0;
}

int w__setDefaultModalDrawCallback(lua_State *L)
{
	Event::ModalDrawData data = {};

	if (!lua_isnoneornil(L, 1))
	{
		luaL_checktype(L, 1, LUA_TFUNCTION);

		// Save the callback function as a Reference.
		lua_pushvalue(L, 1);
		Reference *r = new Reference(L);
		lua_pop(L, 1);

		data.draw = drawCallback;
		data.cleanup = cleanupCallback;
		data.context = r;
	}

	luax_catchexcept(L, [&]() { instance()->setDefaultModalDrawData(data); });
	return 0;
}

// List of functions to wrap.
static const luaL_Reg functions[] =
{
	{ "pump", w_pump },
	{ "poll_i", w_poll_i },
	{ "wait", w_wait },
	{ "push", w_push },
	{ "clear", w_clear },
	{ "quit", w_quit },
	{ "restart", w_restart },
	{ "setModalDrawCallback", w_setModalDrawCallback },
	{ "_setDefaultModalDrawCallback", w__setDefaultModalDrawCallback },
	{ 0, 0 }
};

extern "C" int luaopen_love_event(lua_State *L)
{
	Event *instance = instance();
	if (instance == nullptr)
	{
		luax_catchexcept(L, [&](){ instance = new love::event::sdl::Event(); });
	}
	else
		instance->retain();

	WrappedModule w;
	w.module = instance;
	w.name = "event";
	w.type = &Module::type;
	w.functions = functions;
	w.types = nullptr;

	int ret = luax_register_module(L, w);

	if (luaL_loadbuffer(L, (const char *)event_lua, sizeof(event_lua), "=[love \"wrap_Event.lua\"]") == 0)
		lua_call(L, 0, 0);
	else
		lua_error(L);

	return ret;
}

} // event
} // love
