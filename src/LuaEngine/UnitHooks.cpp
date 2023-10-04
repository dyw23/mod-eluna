/*
 * Copyright (C) 2010 - 2016 Eluna Lua Engine <http://emudevs.com/>
 * This program is free software licensed under GPL version 3
 * Please see the included DOCS/LICENSE.md for more information
 */

#include "Hooks.h"
#include "HookHelpers.h"
#include "LuaEngine.h"
#include "BindingMap.h"
#include "ElunaIncludes.h"
#include "ElunaTemplate.h"

using namespace Hooks;

#define START_HOOK(EVENT) \
    if (!IsEnabled())\
        return;\
    auto key = EventKey<UnitEvents>(EVENT);\
    if (!UnitEventBindings->HasBindingsFor(key))\
        return;\
    LOCK_ELUNA

#define START_HOOK_WITH_RETVAL(EVENT, RETVAL) \
    if (!IsEnabled())\
        return RETVAL;\
    auto key = EventKey<UnitEvents>(EVENT);\
    if (!UnitEventBindings->HasBindingsFor(key))\
        return RETVAL;\
    LOCK_ELUNA

void Eluna::OnAuraApply(Unit * unit, Aura * aura)
{
    START_HOOK(UNIT_EVENT_ON_AURA_APPLY);
    Push(unit);
    Push(aura);
    CallAllFunctions(UnitEventBindings, key);
}

void Eluna::OnAuraRemove(Unit* unit, Aura* aura, AuraRemoveMode removeMode)
{
    START_HOOK(UNIT_EVENT_ON_AURA_REMOVE);
    Push(unit);
    Push(aura);
    Push(removeMode);
    CallAllFunctions(UnitEventBindings, key);
}
