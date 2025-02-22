/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AllOf.h>
#include <AK/Array.h>
#include <AK/CharacterTypes.h>
#include <AK/HashMap.h>
#include <AK/Optional.h>
#include <AK/QuickSort.h>
#include <AK/SourceGenerator.h>
#include <AK/String.h>
#include <AK/StringUtils.h>
#include <AK/Types.h>
#include <AK/Vector.h>
#include <LibCore/ArgsParser.h>
#include <LibCore/File.h>

// Some code points are excluded from UnicodeData.txt, and instead are part of a "range" of code
// points, as indicated by the "name" field. For example:
//     3400;<CJK Ideograph Extension A, First>;Lo;0;L;;;;;N;;;;;
//     4DBF;<CJK Ideograph Extension A, Last>;Lo;0;L;;;;;N;;;;;
struct CodePointRange {
    u32 index;
    u32 first;
    u32 last;
};

// SpecialCasing source: https://www.unicode.org/Public/13.0.0/ucd/SpecialCasing.txt
// Field descriptions: https://www.unicode.org/reports/tr44/tr44-13.html#SpecialCasing.txt
struct SpecialCasing {
    u32 index { 0 };
    u32 code_point { 0 };
    Vector<u32> lowercase_mapping;
    Vector<u32> uppercase_mapping;
    Vector<u32> titlecase_mapping;
    String locale;
    String condition;
};

// PropList source: https://www.unicode.org/Public/13.0.0/ucd/PropList.txt
// Property descriptions: https://www.unicode.org/reports/tr44/tr44-13.html#PropList.txt
//                        https://www.unicode.org/reports/tr44/tr44-13.html#WordBreakProperty.txt
using PropList = HashMap<String, Vector<CodePointRange>>;

// UnicodeData source: https://www.unicode.org/Public/13.0.0/ucd/UnicodeData.txt
// Field descriptions: https://www.unicode.org/reports/tr44/tr44-13.html#UnicodeData.txt
//                     https://www.unicode.org/reports/tr44/#General_Category_Values
struct CodePointData {
    u32 index { 0 };
    u32 code_point { 0 };
    String name;
    String general_category;
    u8 canonical_combining_class { 0 };
    String bidi_class;
    String decomposition_type;
    Optional<i8> numeric_value_decimal;
    Optional<i8> numeric_value_digit;
    Optional<i8> numeric_value_numeric;
    bool bidi_mirrored { false };
    String unicode_1_name;
    String iso_comment;
    Optional<u32> simple_uppercase_mapping;
    Optional<u32> simple_lowercase_mapping;
    Optional<u32> simple_titlecase_mapping;
    Vector<u32> special_casing_indices;
    Vector<StringView> prop_list;
    StringView word_break_property;
};

struct UnicodeData {
    Vector<SpecialCasing> special_casing;
    u32 largest_casing_transform_size { 0 };
    u32 largest_special_casing_size { 0 };
    Vector<String> locales;
    Vector<String> conditions;

    Vector<CodePointData> code_point_data;
    Vector<CodePointRange> code_point_ranges;
    Vector<String> general_categories;
    u32 last_contiguous_code_point { 0 };

    PropList prop_list;
    u32 largest_prop_list_size { 0 };

    PropList word_break_prop_list;
};

static constexpr auto s_desired_fields = Array {
    "general_category"sv,
    "simple_uppercase_mapping"sv,
    "simple_lowercase_mapping"sv,
};

static void parse_special_casing(Core::File& file, UnicodeData& unicode_data)
{
    auto parse_code_point_list = [&](auto const& line) {
        Vector<u32> code_points;

        auto segments = line.split(' ');
        for (auto const& code_point : segments)
            code_points.append(AK::StringUtils::convert_to_uint_from_hex<u32>(code_point).value());

        return code_points;
    };

    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_empty() || line.starts_with('#'))
            continue;

        if (auto index = line.find('#'); index.has_value())
            line = line.substring(0, *index);

        auto segments = line.split(';', true);
        VERIFY(segments.size() == 5 || segments.size() == 6);

        SpecialCasing casing {};
        casing.index = static_cast<u32>(unicode_data.special_casing.size());
        casing.code_point = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[0]).value();
        casing.lowercase_mapping = parse_code_point_list(segments[1]);
        casing.titlecase_mapping = parse_code_point_list(segments[2]);
        casing.uppercase_mapping = parse_code_point_list(segments[3]);

        if (auto condition = segments[4].trim_whitespace(); !condition.is_empty()) {
            auto conditions = condition.split(' ', true);
            VERIFY(conditions.size() == 1 || conditions.size() == 2);

            if (conditions.size() == 2) {
                casing.locale = move(conditions[0]);
                casing.condition = move(conditions[1]);
            } else if (all_of(conditions[0], is_ascii_lower_alpha)) {
                casing.locale = move(conditions[0]);
            } else {
                casing.condition = move(conditions[0]);
            }

            casing.locale = casing.locale.to_uppercase();
            casing.condition.replace("_", "", true);

            if (!casing.locale.is_empty() && !unicode_data.locales.contains_slow(casing.locale))
                unicode_data.locales.append(casing.locale);
            if (!casing.condition.is_empty() && !unicode_data.conditions.contains_slow(casing.condition))
                unicode_data.conditions.append(casing.condition);
        }

        unicode_data.largest_casing_transform_size = max(unicode_data.largest_casing_transform_size, casing.lowercase_mapping.size());
        unicode_data.largest_casing_transform_size = max(unicode_data.largest_casing_transform_size, casing.titlecase_mapping.size());
        unicode_data.largest_casing_transform_size = max(unicode_data.largest_casing_transform_size, casing.uppercase_mapping.size());

        unicode_data.special_casing.append(move(casing));
    }
}

static void parse_prop_list(Core::File& file, PropList& prop_list)
{
    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_empty() || line.starts_with('#'))
            continue;

        if (auto index = line.find('#'); index.has_value())
            line = line.substring(0, *index);

        auto segments = line.split_view(';', true);
        VERIFY(segments.size() == 2);

        auto code_point_range = segments[0].trim_whitespace();
        auto property = segments[1].trim_whitespace().to_string();
        property.replace("_", "", true);

        auto& code_points = prop_list.ensure(property);

        if (code_point_range.contains(".."sv)) {
            segments = code_point_range.split_view(".."sv);
            VERIFY(segments.size() == 2);

            auto begin = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[0]).value();
            auto end = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[1]).value();
            code_points.append({ 0, begin, end });
        } else {
            auto code_point = AK::StringUtils::convert_to_uint_from_hex<u32>(code_point_range).value();
            code_points.append({ 0, code_point, code_point });
        }
    }
}

static void parse_unicode_data(Core::File& file, UnicodeData& unicode_data)
{
    Optional<u32> code_point_range_start;
    Optional<u32> code_point_range_index;

    Optional<u32> last_contiguous_code_point;
    u32 previous_code_point = 0;

    while (file.can_read_line()) {
        auto line = file.read_line();
        if (line.is_empty())
            continue;

        auto segments = line.split(';', true);
        VERIFY(segments.size() == 15);

        CodePointData data {};
        data.index = static_cast<u32>(unicode_data.code_point_data.size());
        data.code_point = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[0]).value();
        data.name = move(segments[1]);
        data.general_category = move(segments[2]);
        data.canonical_combining_class = AK::StringUtils::convert_to_uint<u8>(segments[3]).value();
        data.bidi_class = move(segments[4]);
        data.decomposition_type = move(segments[5]);
        data.numeric_value_decimal = AK::StringUtils::convert_to_int<i8>(segments[6]);
        data.numeric_value_digit = AK::StringUtils::convert_to_int<i8>(segments[7]);
        data.numeric_value_numeric = AK::StringUtils::convert_to_int<i8>(segments[8]);
        data.bidi_mirrored = segments[9] == "Y"sv;
        data.unicode_1_name = move(segments[10]);
        data.iso_comment = move(segments[11]);
        data.simple_uppercase_mapping = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[12]);
        data.simple_lowercase_mapping = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[13]);
        data.simple_titlecase_mapping = AK::StringUtils::convert_to_uint_from_hex<u32>(segments[14]);

        if (data.name.starts_with("<"sv) && data.name.ends_with(", First>")) {
            VERIFY(!code_point_range_start.has_value() && !code_point_range_index.has_value());

            code_point_range_start = data.code_point;
            code_point_range_index = data.index;

            data.name = data.name.substring(1, data.name.length() - 9);
        } else if (data.name.starts_with("<"sv) && data.name.ends_with(", Last>")) {
            VERIFY(code_point_range_start.has_value() && code_point_range_index.has_value());

            unicode_data.code_point_ranges.append({ *code_point_range_index, *code_point_range_start, data.code_point });
            data.name = data.name.substring(1, data.name.length() - 8);

            code_point_range_start.clear();
            code_point_range_index.clear();
        } else if ((data.code_point > 0) && (data.code_point - previous_code_point) != 1) {
            if (!last_contiguous_code_point.has_value())
                last_contiguous_code_point = previous_code_point;
        }

        for (auto const& casing : unicode_data.special_casing) {
            if (casing.code_point == data.code_point)
                data.special_casing_indices.append(casing.index);
        }

        for (auto const& property : unicode_data.prop_list) {
            for (auto const& range : property.value) {
                if ((range.first <= data.code_point) && (data.code_point <= range.last)) {
                    data.prop_list.append(property.key);
                    break;
                }
            }
        }

        for (auto const& property : unicode_data.word_break_prop_list) {
            for (auto const& range : property.value) {
                if ((range.first <= data.code_point) && (data.code_point <= range.last)) {
                    data.word_break_property = property.key;
                    break;
                }
            }
            if (!data.word_break_property.is_empty())
                break;
        }
        if (data.word_break_property.is_empty())
            data.word_break_property = "Other"sv;

        unicode_data.largest_special_casing_size = max(unicode_data.largest_special_casing_size, data.special_casing_indices.size());
        unicode_data.largest_prop_list_size = max(unicode_data.largest_prop_list_size, data.prop_list.size());

        if (!unicode_data.general_categories.contains_slow(data.general_category))
            unicode_data.general_categories.append(data.general_category);

        previous_code_point = data.code_point;
        unicode_data.code_point_data.append(move(data));
    }

    unicode_data.last_contiguous_code_point = *last_contiguous_code_point;
}

static void generate_unicode_data_header(UnicodeData& unicode_data)
{
    StringBuilder builder;
    SourceGenerator generator { builder };
    generator.set("casing_transform_size", String::number(unicode_data.largest_casing_transform_size));
    generator.set("special_casing_size", String::number(unicode_data.largest_special_casing_size));
    generator.set("prop_list_size", String::number(unicode_data.largest_prop_list_size));

    auto generate_enum = [&](StringView name, StringView default_, Vector<String> values) {
        quick_sort(values);

        generator.set("name", name);
        generator.append(R"~~~(
enum class @name@ {)~~~");

        if (!default_.is_empty())
            values.prepend(default_);

        for (auto const& value : values) {
            generator.set("value", value);
            generator.append(R"~~~(
    @value@,)~~~");
        }

        generator.append(R"~~~(
};
)~~~");
    };

    generator.append(R"~~~(
#pragma once

#include <AK/Optional.h>
#include <AK/Types.h>

namespace Unicode {
)~~~");

    generate_enum("Locale"sv, "None"sv, move(unicode_data.locales));
    generate_enum("Condition"sv, "None"sv, move(unicode_data.conditions));
    generate_enum("GeneralCategory"sv, {}, move(unicode_data.general_categories));
    generate_enum("Property"sv, {}, unicode_data.prop_list.keys());
    generate_enum("WordBreakProperty"sv, "Other"sv, unicode_data.word_break_prop_list.keys());

    generator.append(R"~~~(
struct SpecialCasing {
    u32 code_point { 0 };

    u32 lowercase_mapping[@casing_transform_size@];
    u32 lowercase_mapping_size { 0 };

    u32 uppercase_mapping[@casing_transform_size@];
    u32 uppercase_mapping_size { 0 };

    u32 titlecase_mapping[@casing_transform_size@];
    u32 titlecase_mapping_size { 0 };

    Locale locale { Locale::None };
    Condition condition { Condition::None };
};

struct UnicodeData {
    u32 code_point;)~~~");

    auto append_field = [&](StringView type, StringView name) {
        if (!s_desired_fields.span().contains_slow(name))
            return;

        generator.set("type", type);
        generator.set("name", name);
        generator.append(R"~~~(
    @type@ @name@;)~~~");
    };

    // Note: For compile-time performance, only primitive types are used.
    append_field("char const*"sv, "name"sv);
    append_field("GeneralCategory"sv, "general_category"sv);
    append_field("u8"sv, "canonical_combining_class"sv);
    append_field("char const*"sv, "bidi_class"sv);
    append_field("char const*"sv, "decomposition_type"sv);
    append_field("i8"sv, "numeric_value_decimal"sv);
    append_field("i8"sv, "numeric_value_digit"sv);
    append_field("i8"sv, "numeric_value_numeric"sv);
    append_field("bool"sv, "bidi_mirrored"sv);
    append_field("char const*"sv, "unicode_1_name"sv);
    append_field("char const*"sv, "iso_comment"sv);
    append_field("u32"sv, "simple_uppercase_mapping"sv);
    append_field("u32"sv, "simple_lowercase_mapping"sv);
    append_field("u32"sv, "simple_titlecase_mapping"sv);

    generator.append(R"~~~(

    SpecialCasing const* special_casing[@special_casing_size@] {};
    u32 special_casing_size { 0 };

    Property prop_list[@prop_list_size@] {};
    u32 prop_list_size { 0 };

    WordBreakProperty word_break_property { WordBreakProperty::Other };
};

Optional<UnicodeData> unicode_data_for_code_point(u32 code_point);

})~~~");

    outln("{}", generator.as_string_view());
}

static void generate_unicode_data_implementation(UnicodeData unicode_data)
{
    StringBuilder builder;
    SourceGenerator generator { builder };

    generator.set("special_casing_size", String::number(unicode_data.special_casing.size()));
    generator.set("code_point_data_size", String::number(unicode_data.code_point_data.size()));
    generator.set("last_contiguous_code_point", String::formatted("0x{:x}", unicode_data.last_contiguous_code_point));

    generator.append(R"~~~(
#include <AK/Array.h>
#include <AK/CharacterTypes.h>
#include <AK/Find.h>
#include <LibUnicode/UnicodeData.h>

namespace Unicode {
)~~~");

    auto append_list_and_size = [&](auto const& list, StringView format) {
        if (list.is_empty()) {
            generator.append(", {}, 0");
            return;
        }

        bool first = true;
        generator.append(", {");
        for (auto const& item : list) {
            generator.append(first ? " " : ", ");
            generator.append(String::formatted(format, item));
            first = false;
        }
        generator.append(String::formatted(" }}, {}", list.size()));
    };

    generator.append(R"~~~(
static constexpr Array<SpecialCasing, @special_casing_size@> s_special_casing { {)~~~");

    for (auto const& casing : unicode_data.special_casing) {
        generator.set("code_point", String::formatted("{:#x}", casing.code_point));
        generator.append(R"~~~(
    { @code_point@)~~~");

        constexpr auto format = "0x{:x}"sv;
        append_list_and_size(casing.lowercase_mapping, format);
        append_list_and_size(casing.uppercase_mapping, format);
        append_list_and_size(casing.titlecase_mapping, format);

        generator.set("locale", casing.locale.is_empty() ? "None" : casing.locale);
        generator.append(", Locale::@locale@");

        generator.set("condition", casing.condition.is_empty() ? "None" : casing.condition);
        generator.append(", Condition::@condition@");

        generator.append(" },");
    }

    generator.append(R"~~~(
} };

static constexpr Array<UnicodeData, @code_point_data_size@> s_unicode_data { {)~~~");

    auto append_field = [&](StringView name, String value) {
        if (!s_desired_fields.span().contains_slow(name))
            return;

        generator.set("value", move(value));
        generator.append(", @value@");
    };

    for (auto const& data : unicode_data.code_point_data) {
        generator.set("code_point", String::formatted("{:#x}", data.code_point));
        generator.append(R"~~~(
    { @code_point@)~~~");

        append_field("name", String::formatted("\"{}\"", data.name));
        append_field("general_category", String::formatted("GeneralCategory::{}", data.general_category));
        append_field("canonical_combining_class", String::number(data.canonical_combining_class));
        append_field("bidi_class", String::formatted("\"{}\"", data.bidi_class));
        append_field("decomposition_type", String::formatted("\"{}\"", data.decomposition_type));
        append_field("numeric_value_decimal", String::number(data.numeric_value_decimal.value_or(-1)));
        append_field("numeric_value_digit", String::number(data.numeric_value_digit.value_or(-1)));
        append_field("numeric_value_numeric", String::number(data.numeric_value_numeric.value_or(-1)));
        append_field("bidi_mirrored", String::formatted("{}", data.bidi_mirrored));
        append_field("unicode_1_name", String::formatted("\"{}\"", data.unicode_1_name));
        append_field("iso_comment", String::formatted("\"{}\"", data.iso_comment));
        append_field("simple_uppercase_mapping", String::formatted("{:#x}", data.simple_uppercase_mapping.value_or(data.code_point)));
        append_field("simple_lowercase_mapping", String::formatted("{:#x}", data.simple_lowercase_mapping.value_or(data.code_point)));
        append_field("simple_titlecase_mapping", String::formatted("{:#x}", data.simple_titlecase_mapping.value_or(data.code_point)));
        append_list_and_size(data.special_casing_indices, "&s_special_casing[{}]"sv);
        append_list_and_size(data.prop_list, "Property::{}"sv);
        generator.append(String::formatted(", WordBreakProperty::{}", data.word_break_property));
        generator.append(" },");
    }

    generator.append(R"~~~(
} };

static Optional<u32> index_of_code_point_in_range(u32 code_point)
{)~~~");

    for (auto const& range : unicode_data.code_point_ranges) {
        generator.set("index", String::formatted("{}", range.index));
        generator.set("first", String::formatted("{:#x}", range.first));
        generator.set("last", String::formatted("{:#x}", range.last));

        generator.append(R"~~~(
    if ((code_point > @first@) && (code_point < @last@))
        return @index@;)~~~");
    }

    generator.append(R"~~~(
    return {};
}

Optional<UnicodeData> unicode_data_for_code_point(u32 code_point)
{
    VERIFY(is_unicode(code_point));

    if (code_point <= @last_contiguous_code_point@)
        return s_unicode_data[code_point];

    if (auto index = index_of_code_point_in_range(code_point); index.has_value()) {
        auto data_for_range = s_unicode_data[*index];
        data_for_range.simple_uppercase_mapping = code_point;
        data_for_range.simple_lowercase_mapping = code_point;
        return data_for_range;
    }

    auto it = AK::find_if(s_unicode_data.begin(), s_unicode_data.end(), [code_point](auto const& data) { return data.code_point == code_point; });
    if (it != s_unicode_data.end())
        return *it;

    return {};
}

})~~~");

    outln("{}", generator.as_string_view());
}

int main(int argc, char** argv)
{
    bool generate_header = false;
    bool generate_implementation = false;
    char const* unicode_data_path = nullptr;
    char const* special_casing_path = nullptr;
    char const* prop_list_path = nullptr;
    char const* word_break_path = nullptr;

    Core::ArgsParser args_parser;
    args_parser.add_option(generate_header, "Generate the Unicode Data header file", "generate-header", 'h');
    args_parser.add_option(generate_implementation, "Generate the Unicode Data implementation file", "generate-implementation", 'c');
    args_parser.add_option(unicode_data_path, "Path to UnicodeData.txt file", "unicode-data-path", 'u', "unicode-data-path");
    args_parser.add_option(special_casing_path, "Path to SpecialCasing.txt file", "special-casing-path", 's', "special-casing-path");
    args_parser.add_option(prop_list_path, "Path to PropList.txt file", "prop-list-path", 'p', "prop-list-path");
    args_parser.add_option(word_break_path, "Path to WordBreakProperty.txt file", "word-break-path", 'w', "word-break-path");
    args_parser.parse(argc, argv);

    if (!generate_header && !generate_implementation) {
        warnln("At least one of -h/--generate-header or -c/--generate-implementation is required");
        args_parser.print_usage(stderr, argv[0]);
        return 1;
    }

    auto open_file = [&](StringView path, StringView flags) {
        if (path.is_empty()) {
            warnln("{} is required", flags);
            args_parser.print_usage(stderr, argv[0]);
            exit(1);
        }

        auto file_or_error = Core::File::open(path, Core::OpenMode::ReadOnly);
        if (file_or_error.is_error()) {
            warnln("Failed to open {}: {}", path, file_or_error.release_error());
            exit(1);
        }

        return file_or_error.release_value();
    };

    auto unicode_data_file = open_file(unicode_data_path, "-u/--unicode-data-path");
    auto special_casing_file = open_file(special_casing_path, "-s/--special-casing-path");
    auto prop_list_file = open_file(prop_list_path, "-p/--prop-list-path");
    auto word_break_file = open_file(word_break_path, "-w/--word-break-path");

    UnicodeData unicode_data {};
    parse_special_casing(special_casing_file, unicode_data);
    parse_prop_list(prop_list_file, unicode_data.prop_list);
    parse_prop_list(word_break_file, unicode_data.word_break_prop_list);
    parse_unicode_data(unicode_data_file, unicode_data);

    if (generate_header)
        generate_unicode_data_header(unicode_data);
    if (generate_implementation)
        generate_unicode_data_implementation(move(unicode_data));

    return 0;
}
