/*
 * Copyright (c) 2021, Linus Groh <linusg@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Temporal/Calendar.h>
#include <LibJS/Runtime/Temporal/PlainDate.h>
#include <LibJS/Runtime/Temporal/PlainDateTime.h>
#include <LibJS/Runtime/Temporal/PlainTime.h>
#include <LibJS/Runtime/Temporal/PlainTimePrototype.h>

namespace JS::Temporal {

// 4.3 Properties of the Temporal.PlainTime Prototype Object, https://tc39.es/proposal-temporal/#sec-properties-of-the-temporal-plaintime-prototype-object
PlainTimePrototype::PlainTimePrototype(GlobalObject& global_object)
    : Object(*global_object.object_prototype())
{
}

void PlainTimePrototype::initialize(GlobalObject& global_object)
{
    Object::initialize(global_object);

    auto& vm = this->vm();

    // 4.3.2 Temporal.PlainTime.prototype[ @@toStringTag ], https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype-@@tostringtag
    define_direct_property(*vm.well_known_symbol_to_string_tag(), js_string(vm.heap(), "Temporal.PlainTime"), Attribute::Configurable);

    define_native_accessor(vm.names.calendar, calendar_getter, {}, Attribute::Configurable);
    define_native_accessor(vm.names.hour, hour_getter, {}, Attribute::Configurable);
    define_native_accessor(vm.names.minute, minute_getter, {}, Attribute::Configurable);
    define_native_accessor(vm.names.second, second_getter, {}, Attribute::Configurable);
    define_native_accessor(vm.names.millisecond, millisecond_getter, {}, Attribute::Configurable);
    define_native_accessor(vm.names.microsecond, microsecond_getter, {}, Attribute::Configurable);
    define_native_accessor(vm.names.nanosecond, nanosecond_getter, {}, Attribute::Configurable);

    u8 attr = Attribute::Writable | Attribute::Configurable;
    define_native_function(vm.names.toPlainDateTime, to_plain_date_time, 1, attr);
    define_native_function(vm.names.valueOf, value_of, 0, attr);
}

static PlainTime* typed_this(GlobalObject& global_object)
{
    auto& vm = global_object.vm();
    auto* this_object = vm.this_value(global_object).to_object(global_object);
    if (!this_object)
        return {};
    if (!is<PlainTime>(this_object)) {
        vm.throw_exception<TypeError>(global_object, ErrorType::NotA, "Temporal.PlainTime");
        return {};
    }
    return static_cast<PlainTime*>(this_object);
}

// 4.3.3 get Temporal.PlainTime.prototype.calendar, https://tc39.es/proposal-temporal/#sec-get-temporal.plaintime.prototype.calendar
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::calendar_getter)
{
    // 1. Let temporalTime be the this value.
    // 2. Perform ? RequireInternalSlot(temporalTime, [[InitializedTemporalTime]]).
    auto* temporal_time = typed_this(global_object);
    if (vm.exception())
        return {};

    // 3. Return temporalTime.[[Calendar]].
    return Value(&temporal_time->calendar());
}

// 4.3.4 get Temporal.PlainTime.prototype.hour, https://tc39.es/proposal-temporal/#sec-get-temporal.plaintime.prototype.hour
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::hour_getter)
{
    // 1. Let temporalTime be the this value.
    // 2. Perform ? RequireInternalSlot(temporalTime, [[InitializedTemporalTime]]).
    auto* temporal_time = typed_this(global_object);
    if (vm.exception())
        return {};

    // 3. Return 𝔽(temporalTime.[[ISOHour]]).
    return Value(temporal_time->iso_hour());
}

// 4.3.5 get Temporal.PlainTime.prototype.minute, https://tc39.es/proposal-temporal/#sec-get-temporal.plaintime.prototype.minute
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::minute_getter)
{
    // 1. Let temporalTime be the this value.
    // 2. Perform ? RequireInternalSlot(temporalTime, [[InitializedTemporalTime]]).
    auto* temporal_time = typed_this(global_object);
    if (vm.exception())
        return {};

    // 3. Return 𝔽(temporalTime.[[ISOMinute]]).
    return Value(temporal_time->iso_minute());
}

// 4.3.6 get Temporal.PlainTime.prototype.second, https://tc39.es/proposal-temporal/#sec-get-temporal.plaintime.prototype.second
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::second_getter)
{
    // 1. Let temporalTime be the this value.
    // 2. Perform ? RequireInternalSlot(temporalTime, [[InitializedTemporalTime]]).
    auto* temporal_time = typed_this(global_object);
    if (vm.exception())
        return {};

    // 3. Return 𝔽(temporalTime.[[ISOSecond]]).
    return Value(temporal_time->iso_second());
}

// 4.3.7 get Temporal.PlainTime.prototype.millisecond, https://tc39.es/proposal-temporal/#sec-get-temporal.plaintime.prototype.millisecond
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::millisecond_getter)
{
    // 1. Let temporalTime be the this value.
    // 2. Perform ? RequireInternalSlot(temporalTime, [[InitializedTemporalTime]]).
    auto* temporal_time = typed_this(global_object);
    if (vm.exception())
        return {};

    // 3. Return 𝔽(temporalTime.[[ISOMillisecond]]).
    return Value(temporal_time->iso_millisecond());
}

// 4.3.8 get Temporal.PlainTime.prototype.microsecond, https://tc39.es/proposal-temporal/#sec-get-temporal.plaintime.prototype.microsecond
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::microsecond_getter)
{
    // 1. Let temporalTime be the this value.
    // 2. Perform ? RequireInternalSlot(temporalTime, [[InitializedTemporalTime]]).
    auto* temporal_time = typed_this(global_object);
    if (vm.exception())
        return {};

    // 3. Return 𝔽(temporalTime.[[ISOMicrosecond]]).
    return Value(temporal_time->iso_microsecond());
}

// 4.3.9 get Temporal.PlainTime.prototype.nanosecond, https://tc39.es/proposal-temporal/#sec-get-temporal.plaintime.prototype.nanosecond
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::nanosecond_getter)
{
    // 1. Let temporalTime be the this value.
    // 2. Perform ? RequireInternalSlot(temporalTime, [[InitializedTemporalTime]]).
    auto* temporal_time = typed_this(global_object);
    if (vm.exception())
        return {};

    // 3. Return 𝔽(temporalTime.[[ISONanosecond]]).
    return Value(temporal_time->iso_nanosecond());
}

// 4.3.17 Temporal.PlainTime.prototype.toPlainDateTime ( temporalDate )
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::to_plain_date_time)
{
    // 1. Let temporalTime be the this value.
    // 2. Perform ? RequireInternalSlot(temporalTime, [[InitializedTemporalTime]]).
    auto* temporal_time = typed_this(global_object);
    if (vm.exception())
        return {};

    // 3. Set temporalDate to ? ToTemporalDate(temporalDate).
    auto* temporal_date = to_temporal_date(global_object, vm.argument(0));
    if (vm.exception())
        return {};

    // 4. Return ? CreateTemporalDateTime(temporalDate.[[ISOYear]], temporalDate.[[ISOMonth]], temporalDate.[[ISODay]], temporalTime.[[ISOHour]], temporalTime.[[ISOMinute]], temporalTime.[[ISOSecond]], temporalTime.[[ISOMillisecond]], temporalTime.[[ISOMicrosecond]], temporalTime.[[ISONanosecond]], temporalDate.[[Calendar]]).
    return create_temporal_date_time(global_object, temporal_date->iso_year(), temporal_date->iso_month(), temporal_date->iso_day(), temporal_time->iso_hour(), temporal_time->iso_minute(), temporal_time->iso_second(), temporal_time->iso_millisecond(), temporal_time->iso_microsecond(), temporal_time->iso_nanosecond(), temporal_date->calendar());
}

// 4.3.23 Temporal.PlainTime.prototype.valueOf ( ), https://tc39.es/proposal-temporal/#sec-temporal.plaintime.prototype.valueof
JS_DEFINE_NATIVE_FUNCTION(PlainTimePrototype::value_of)
{
    // 1. Throw a TypeError exception.
    vm.throw_exception<TypeError>(global_object, ErrorType::Convert, "Temporal.PlainTime", "a primitive value");
    return {};
}

}
