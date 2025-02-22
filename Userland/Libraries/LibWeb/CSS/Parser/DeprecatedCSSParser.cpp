/*
 * Copyright (c) 2018-2020, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2021, Tobias Christiansen <tobi@tobyase.de>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/GenericLexer.h>
#include <AK/HashMap.h>
#include <AK/NonnullOwnPtr.h>
#include <AK/SourceLocation.h>
#include <LibWeb/CSS/CSSImportRule.h>
#include <LibWeb/CSS/CSSRule.h>
#include <LibWeb/CSS/CSSStyleRule.h>
#include <LibWeb/CSS/Parser/DeprecatedCSSParser.h>
#include <LibWeb/CSS/PropertyID.h>
#include <LibWeb/CSS/Selector.h>
#include <LibWeb/DOM/Document.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define PARSE_VERIFY(x)                                     \
    if (!(x)) {                                             \
        dbgln("CSS PARSER ASSERTION FAILED: {}", #x);       \
        dbgln("At character# {} in CSS: _{}_", index, css); \
        VERIFY_NOT_REACHED();                               \
    }

static inline void log_parse_error(const SourceLocation& location = SourceLocation::current())
{
    dbgln("CSS Parse error! {}", location);
}

namespace Web {

namespace CSS {

DeprecatedParsingContext::DeprecatedParsingContext()
{
}

DeprecatedParsingContext::DeprecatedParsingContext(const DOM::Document& document)
    : m_document(&document)
{
}

DeprecatedParsingContext::DeprecatedParsingContext(const DOM::ParentNode& parent_node)
    : m_document(&parent_node.document())
{
}

bool DeprecatedParsingContext::in_quirks_mode() const
{
    return m_document ? m_document->in_quirks_mode() : false;
}

URL DeprecatedParsingContext::complete_url(const String& addr) const
{
    return m_document ? m_document->url().complete_url(addr) : URL::create_with_url_or_path(addr);
}

}

static Optional<Color> parse_css_color(const CSS::DeprecatedParsingContext&, const StringView& view)
{
    if (view.equals_ignoring_case("transparent"))
        return Color::from_rgba(0x00000000);

    auto color = Color::from_string(view.to_string().to_lowercase());
    if (color.has_value())
        return color;

    return {};
}

static Optional<float> try_parse_float(const StringView& string)
{
    const char* str = string.characters_without_null_termination();
    size_t len = string.length();
    size_t weight = 1;
    int exp_val = 0;
    float value = 0.0f;
    float fraction = 0.0f;
    bool has_sign = false;
    bool is_negative = false;
    bool is_fractional = false;
    bool is_scientific = false;

    if (str[0] == '-') {
        is_negative = true;
        has_sign = true;
    }
    if (str[0] == '+') {
        has_sign = true;
    }

    for (size_t i = has_sign; i < len; i++) {

        // Looks like we're about to start working on the fractional part
        if (str[i] == '.') {
            is_fractional = true;
            continue;
        }

        if (str[i] == 'e' || str[i] == 'E') {
            if (str[i + 1] == '-' || str[i + 1] == '+')
                exp_val = atoi(str + i + 2);
            else
                exp_val = atoi(str + i + 1);

            is_scientific = true;
            continue;
        }

        if (str[i] < '0' || str[i] > '9' || exp_val != 0) {
            return {};
            continue;
        }

        if (is_fractional) {
            fraction *= 10;
            fraction += str[i] - '0';
            weight *= 10;
        } else {
            value = value * 10;
            value += str[i] - '0';
        }
    }

    fraction /= weight;
    value += fraction;

    if (is_scientific) {
        bool divide = exp_val < 0;
        if (divide)
            exp_val *= -1;

        for (int i = 0; i < exp_val; i++) {
            if (divide)
                value /= 10;
            else
                value *= 10;
        }
    }

    return is_negative ? -value : value;
}

static CSS::Length::Type length_type_from_unit(const StringView& view)
{
    if (view.ends_with('%'))
        return CSS::Length::Type::Percentage;
    if (view.ends_with("px", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Px;
    if (view.ends_with("pt", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Pt;
    if (view.ends_with("pc", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Pc;
    if (view.ends_with("mm", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Mm;
    if (view.ends_with("rem", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Rem;
    if (view.ends_with("em", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Em;
    if (view.ends_with("ex", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Ex;
    if (view.ends_with("vw", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Vw;
    if (view.ends_with("vh", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Vh;
    if (view.ends_with("vmax", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Vmax;
    if (view.ends_with("vmin", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Vmin;
    if (view.ends_with("cm", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Cm;
    if (view.ends_with("in", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::In;
    if (view.ends_with("Q", CaseSensitivity::CaseInsensitive))
        return CSS::Length::Type::Q;
    if (view == "0")
        return CSS::Length::Type::Px;

    return CSS::Length::Type::Undefined;
}

static CSS::Length parse_length(const CSS::DeprecatedParsingContext& context, const StringView& view, bool& is_bad_length)
{
    CSS::Length::Type type = length_type_from_unit(view);
    Optional<float> value;

    switch (type) {
    case CSS::Length::Type::Percentage:
        value = try_parse_float(view.substring_view(0, view.length() - 1));
        break;
    case CSS::Length::Type::Px:
        if (view == "0")
            value = 0;
        else
            value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Pt:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Pc:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Mm:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Rem:
        value = try_parse_float(view.substring_view(0, view.length() - 3));
        break;
    case CSS::Length::Type::Em:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Ex:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Vw:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Vh:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Vmax:
        value = try_parse_float(view.substring_view(0, view.length() - 4));
        break;
    case CSS::Length::Type::Vmin:
        value = try_parse_float(view.substring_view(0, view.length() - 4));
        break;
    case CSS::Length::Type::Cm:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::In:
        value = try_parse_float(view.substring_view(0, view.length() - 2));
        break;
    case CSS::Length::Type::Q:
        value = try_parse_float(view.substring_view(0, view.length() - 1));
        break;
    default:
        if (context.in_quirks_mode()) {
            type = CSS::Length::Type::Px;
            value = try_parse_float(view);
        } else {
            value = try_parse_float(view);
            if (value.has_value())
                is_bad_length = true;
        }
    }

    if (!value.has_value())
        return {};

    return CSS::Length(value.value(), type);
}

static bool takes_integer_value(CSS::PropertyID property_id)
{
    return property_id == CSS::PropertyID::ZIndex || property_id == CSS::PropertyID::FontWeight || property_id == CSS::PropertyID::Custom;
}

static StringView parse_custom_property_name(const StringView& value)
{
    if (!value.starts_with("var(") || !value.ends_with(")"))
        return {};
    // FIXME: Allow for fallback
    auto first_comma_index = value.find(',');
    auto length = value.length();

    auto substring_length = first_comma_index.has_value() ? first_comma_index.value() - 4 - 1 : length - 4 - 1;
    return value.substring_view(4, substring_length);
}

static StringView isolate_calc_expression(const StringView& value)
{
    if (!value.starts_with("calc(") || !value.ends_with(")"))
        return {};
    auto substring_length = value.length() - 5 - 1;
    return value.substring_view(5, substring_length);
}

struct CalcToken {
    enum class Type {
        Undefined,
        Number,
        Unit,
        Whitespace,
        Plus,
        Minus,
        Asterisk,
        Slash,
        OpenBracket,
        CloseBracket,
    } type { Type::Undefined };
    String value {};
};

static void eat_white_space(Vector<CalcToken>&);
static Optional<CSS::CalculatedStyleValue::CalcValue> parse_calc_value(Vector<CalcToken>&);
static OwnPtr<CSS::CalculatedStyleValue::CalcProductPartWithOperator> parse_calc_product_part_with_operator(Vector<CalcToken>&);
static Optional<CSS::CalculatedStyleValue::CalcNumberValue> parse_calc_number_value(Vector<CalcToken>&);
static OwnPtr<CSS::CalculatedStyleValue::CalcProduct> parse_calc_product(Vector<CalcToken>&);
static OwnPtr<CSS::CalculatedStyleValue::CalcSumPartWithOperator> parse_calc_sum_part_with_operator(Vector<CalcToken>&);
static OwnPtr<CSS::CalculatedStyleValue::CalcSum> parse_calc_sum(Vector<CalcToken>&);
static OwnPtr<CSS::CalculatedStyleValue::CalcNumberSum> parse_calc_number_sum(Vector<CalcToken>& tokens);
static OwnPtr<CSS::CalculatedStyleValue::CalcNumberProductPartWithOperator> parse_calc_number_product_part_with_operator(Vector<CalcToken>& tokens);
static OwnPtr<CSS::CalculatedStyleValue::CalcNumberProduct> parse_calc_number_product(Vector<CalcToken>& tokens);
static OwnPtr<CSS::CalculatedStyleValue::CalcNumberSumPartWithOperator> parse_calc_number_sum_part_with_operator(Vector<CalcToken>& tokens);

static OwnPtr<CSS::CalculatedStyleValue::CalcSum> parse_calc_expression(const StringView& expression_string)
{
    // First, tokenize

    Vector<CalcToken> tokens;

    GenericLexer lexer(expression_string);
    while (!lexer.is_eof()) {
        // Number
        if (((lexer.next_is('+') || lexer.next_is('-')) && !isspace(lexer.peek(1)))
            || lexer.next_is('.')
            || lexer.next_is(isdigit)) {

            auto number = lexer.consume_while(is_any_of("+-.0123456789"));
            tokens.append(CalcToken { CalcToken::Type ::Number, number.to_string() });
            continue;
        }

        auto ch = lexer.consume();

        if (isspace(ch)) {
            tokens.append(CalcToken { CalcToken::Type::Whitespace });
            continue;
        }

        if (ch == '%') {
            tokens.append(CalcToken { CalcToken::Type::Unit, "%" });
            continue;
        }

        if (ch == '+') {
            tokens.append(CalcToken { CalcToken::Type::Plus });
            continue;
        }

        if (ch == '-') {
            tokens.append(CalcToken { CalcToken::Type::Minus });
            continue;
        }

        if (ch == '*') {
            tokens.append(CalcToken { CalcToken::Type::Asterisk });
            continue;
        }

        if (ch == '/') {
            tokens.append(CalcToken { CalcToken::Type::Slash });
            continue;
        }

        if (ch == '(') {
            tokens.append(CalcToken { CalcToken::Type::OpenBracket });
            continue;
        }

        if (ch == ')') {
            tokens.append(CalcToken { CalcToken::Type::CloseBracket });
            continue;
        }

        // Unit
        if (isalpha(ch)) {
            lexer.retreat();
            auto unit = lexer.consume_while(isalpha);
            tokens.append(CalcToken { CalcToken::Type::Unit, unit.to_string() });
            continue;
        }

        VERIFY_NOT_REACHED();
    }

    // Then, parse

    return parse_calc_sum(tokens);
}

static void eat_white_space(Vector<CalcToken>& tokens)
{
    while (tokens.size() > 0 && tokens.first().type == CalcToken::Type::Whitespace)
        tokens.take_first();
}

static Optional<CSS::CalculatedStyleValue::CalcValue> parse_calc_value(Vector<CalcToken>& tokens)
{
    auto current_token = tokens.take_first();

    if (current_token.type == CalcToken::Type::OpenBracket) {
        auto parsed_calc_sum = parse_calc_sum(tokens);
        if (!parsed_calc_sum)
            return {};
        return (CSS::CalculatedStyleValue::CalcValue) { parsed_calc_sum.release_nonnull() };
    }

    if (current_token.type != CalcToken::Type::Number)
        return {};

    auto try_the_number = try_parse_float(current_token.value);
    if (!try_the_number.has_value())
        return {};

    float the_number = try_the_number.value();

    if (tokens.first().type != CalcToken::Type::Unit)
        return (CSS::CalculatedStyleValue::CalcValue) { the_number };

    auto type = length_type_from_unit(tokens.take_first().value);

    if (type == CSS::Length::Type::Undefined)
        return {};

    return (CSS::CalculatedStyleValue::CalcValue) { CSS::Length(the_number, type) };
}

static OwnPtr<CSS::CalculatedStyleValue::CalcProductPartWithOperator> parse_calc_product_part_with_operator(Vector<CalcToken>& tokens)
{
    auto product_with_operator = make<CSS::CalculatedStyleValue::CalcProductPartWithOperator>();

    eat_white_space(tokens);

    auto op = tokens.first();
    if (op.type == CalcToken::Type::Asterisk) {
        tokens.take_first();
        eat_white_space(tokens);
        product_with_operator->op = CSS::CalculatedStyleValue::CalcProductPartWithOperator::Multiply;
        auto parsed_calc_value = parse_calc_value(tokens);
        if (!parsed_calc_value.has_value())
            return nullptr;
        product_with_operator->value = { parsed_calc_value.release_value() };

    } else if (op.type == CalcToken::Type::Slash) {
        tokens.take_first();
        eat_white_space(tokens);
        product_with_operator->op = CSS::CalculatedStyleValue::CalcProductPartWithOperator::Divide;
        auto parsed_calc_number_value = parse_calc_number_value(tokens);
        if (!parsed_calc_number_value.has_value())
            return nullptr;
        product_with_operator->value = { parsed_calc_number_value.release_value() };
    } else {
        return nullptr;
    }

    return product_with_operator;
}

static OwnPtr<CSS::CalculatedStyleValue::CalcNumberProductPartWithOperator> parse_calc_number_product_part_with_operator(Vector<CalcToken>& tokens)
{
    auto number_product_with_operator = make<CSS::CalculatedStyleValue::CalcNumberProductPartWithOperator>();

    eat_white_space(tokens);

    auto op = tokens.first();
    if (op.type == CalcToken::Type::Asterisk) {
        tokens.take_first();
        eat_white_space(tokens);
        number_product_with_operator->op = CSS::CalculatedStyleValue::CalcNumberProductPartWithOperator::Multiply;
    } else if (op.type == CalcToken::Type::Slash) {
        tokens.take_first();
        eat_white_space(tokens);
        number_product_with_operator->op = CSS::CalculatedStyleValue::CalcNumberProductPartWithOperator::Divide;
    } else {
        return nullptr;
    }
    auto parsed_calc_value = parse_calc_number_value(tokens);
    if (!parsed_calc_value.has_value())
        return nullptr;
    number_product_with_operator->value = parsed_calc_value.release_value();

    return number_product_with_operator;
}

static OwnPtr<CSS::CalculatedStyleValue::CalcNumberProduct> parse_calc_number_product(Vector<CalcToken>& tokens)
{
    auto calc_number_product = make<CSS::CalculatedStyleValue::CalcNumberProduct>();

    auto first_calc_number_value_or_error = parse_calc_number_value(tokens);
    if (!first_calc_number_value_or_error.has_value())
        return nullptr;
    calc_number_product->first_calc_number_value = first_calc_number_value_or_error.release_value();

    while (tokens.size() > 0) {
        auto number_product_with_operator = parse_calc_number_product_part_with_operator(tokens);
        if (!number_product_with_operator)
            break;
        calc_number_product->zero_or_more_additional_calc_number_values.append(number_product_with_operator.release_nonnull());
    }

    return calc_number_product;
}

static OwnPtr<CSS::CalculatedStyleValue::CalcNumberSumPartWithOperator> parse_calc_number_sum_part_with_operator(Vector<CalcToken>& tokens)
{
    if (tokens.size() < 3)
        return nullptr;
    if (!((tokens[0].type == CalcToken::Type::Plus
              || tokens[0].type == CalcToken::Type::Minus)
            && tokens[1].type == CalcToken::Type::Whitespace))
        return nullptr;

    auto op_token = tokens.take_first().type;
    tokens.take_first(); // Whitespace;

    CSS::CalculatedStyleValue::CalcNumberSumPartWithOperator::Operation op;
    if (op_token == CalcToken::Type::Plus)
        op = CSS::CalculatedStyleValue::CalcNumberSumPartWithOperator::Operation::Add;
    else if (op_token == CalcToken::Type::Minus)
        op = CSS::CalculatedStyleValue::CalcNumberSumPartWithOperator::Operation::Subtract;
    else
        return nullptr;

    auto calc_number_product = parse_calc_number_product(tokens);
    if (!calc_number_product)
        return nullptr;
    return make<CSS::CalculatedStyleValue::CalcNumberSumPartWithOperator>(op, calc_number_product.release_nonnull());
}

static OwnPtr<CSS::CalculatedStyleValue::CalcNumberSum> parse_calc_number_sum(Vector<CalcToken>& tokens)
{
    if (tokens.take_first().type != CalcToken::Type::OpenBracket)
        return nullptr;

    auto first_calc_number_product_or_error = parse_calc_number_product(tokens);
    if (!first_calc_number_product_or_error)
        return nullptr;

    NonnullOwnPtrVector<CSS::CalculatedStyleValue::CalcNumberSumPartWithOperator> additional {};
    while (tokens.size() > 0 && tokens.first().type != CalcToken::Type::CloseBracket) {
        auto calc_sum_part = parse_calc_number_sum_part_with_operator(tokens);
        if (!calc_sum_part)
            return nullptr;
        additional.append(calc_sum_part.release_nonnull());
    }

    eat_white_space(tokens);

    auto calc_number_sum = make<CSS::CalculatedStyleValue::CalcNumberSum>(first_calc_number_product_or_error.release_nonnull(), move(additional));
    return calc_number_sum;
}

static Optional<CSS::CalculatedStyleValue::CalcNumberValue> parse_calc_number_value(Vector<CalcToken>& tokens)
{
    if (tokens.first().type == CalcToken::Type::OpenBracket) {
        auto calc_number_sum = parse_calc_number_sum(tokens);
        if (calc_number_sum)
            return { calc_number_sum.release_nonnull() };
    }

    if (tokens.first().type != CalcToken::Type::Number)
        return {};

    auto the_number_string = tokens.take_first().value;
    auto try_the_number = try_parse_float(the_number_string);
    if (!try_the_number.has_value())
        return {};
    return try_the_number.value();
}

static OwnPtr<CSS::CalculatedStyleValue::CalcProduct> parse_calc_product(Vector<CalcToken>& tokens)
{
    auto calc_product = make<CSS::CalculatedStyleValue::CalcProduct>();

    auto first_calc_value_or_error = parse_calc_value(tokens);
    if (!first_calc_value_or_error.has_value())
        return nullptr;
    calc_product->first_calc_value = first_calc_value_or_error.release_value();

    while (tokens.size() > 0) {
        auto product_with_operator = parse_calc_product_part_with_operator(tokens);
        if (!product_with_operator)
            break;
        calc_product->zero_or_more_additional_calc_values.append(product_with_operator.release_nonnull());
    }

    return calc_product;
}

static OwnPtr<CSS::CalculatedStyleValue::CalcSumPartWithOperator> parse_calc_sum_part_with_operator(Vector<CalcToken>& tokens)
{
    // The following has to have the shape of <Whitespace><+ or -><Whitespace>
    // But the first whitespace gets eaten in parse_calc_product_part_with_operator().
    if (tokens.size() < 3)
        return {};
    if (!((tokens[0].type == CalcToken::Type::Plus
              || tokens[0].type == CalcToken::Type::Minus)
            && tokens[1].type == CalcToken::Type::Whitespace))
        return nullptr;

    auto op_token = tokens.take_first().type;
    tokens.take_first(); // Whitespace;

    CSS::CalculatedStyleValue::CalcSumPartWithOperator::Operation op;
    if (op_token == CalcToken::Type::Plus)
        op = CSS::CalculatedStyleValue::CalcSumPartWithOperator::Operation::Add;
    else if (op_token == CalcToken::Type::Minus)
        op = CSS::CalculatedStyleValue::CalcSumPartWithOperator::Operation::Subtract;
    else
        return nullptr;

    auto calc_product = parse_calc_product(tokens);
    if (!calc_product)
        return nullptr;
    return make<CSS::CalculatedStyleValue::CalcSumPartWithOperator>(op, calc_product.release_nonnull());
};

static OwnPtr<CSS::CalculatedStyleValue::CalcSum> parse_calc_sum(Vector<CalcToken>& tokens)
{
    auto parsed_calc_product = parse_calc_product(tokens);
    if (!parsed_calc_product)
        return nullptr;

    NonnullOwnPtrVector<CSS::CalculatedStyleValue::CalcSumPartWithOperator> additional {};
    while (tokens.size() > 0 && tokens.first().type != CalcToken::Type::CloseBracket) {
        auto calc_sum_part = parse_calc_sum_part_with_operator(tokens);
        if (!calc_sum_part)
            return nullptr;
        additional.append(calc_sum_part.release_nonnull());
    }

    eat_white_space(tokens);

    return make<CSS::CalculatedStyleValue::CalcSum>(parsed_calc_product.release_nonnull(), move(additional));
}

static RefPtr<CSS::BoxShadowStyleValue> parse_box_shadow(CSS::DeprecatedParsingContext const& context, StringView const& string)
{
    // FIXME: Also support inset, spread-radius and multiple comma-seperated box-shadows
    CSS::Length offset_x {};
    CSS::Length offset_y {};
    CSS::Length blur_radius {};
    Color color {};

    auto parts = string.split_view(' ');

    if (parts.size() < 3 || parts.size() > 4)
        return nullptr;

    bool bad_length = false;
    offset_x = parse_length(context, parts[0], bad_length);
    if (bad_length)
        return nullptr;

    bad_length = false;
    offset_y = parse_length(context, parts[1], bad_length);
    if (bad_length)
        return nullptr;

    if (parts.size() == 3) {
        auto parsed_color = parse_color(context, parts[2]);
        if (!parsed_color)
            return nullptr;
        color = parsed_color->color();
    } else if (parts.size() == 4) {
        bad_length = false;
        blur_radius = parse_length(context, parts[2], bad_length);
        if (bad_length)
            return nullptr;

        auto parsed_color = parse_color(context, parts[3]);
        if (!parsed_color)
            return nullptr;
        color = parsed_color->color();
    }
    return CSS::BoxShadowStyleValue::create(offset_x, offset_y, blur_radius, color);
}

RefPtr<CSS::StyleValue> parse_css_value(const CSS::DeprecatedParsingContext& context, const StringView& string, CSS::PropertyID property_id)
{
    bool is_bad_length = false;

    if (property_id == CSS::PropertyID::BoxShadow) {
        auto parsed_box_shadow = parse_box_shadow(context, string);
        if (parsed_box_shadow)
            return parsed_box_shadow;
    }

    if (takes_integer_value(property_id)) {
        auto integer = string.to_int();
        if (integer.has_value())
            return CSS::LengthStyleValue::create(CSS::Length::make_px(integer.value()));
    }

    auto length = parse_length(context, string, is_bad_length);
    if (is_bad_length) {
        auto float_number = try_parse_float(string);
        if (float_number.has_value())
            return CSS::NumericStyleValue::create(float_number.value());
        return nullptr;
    }
    if (!length.is_undefined())
        return CSS::LengthStyleValue::create(length);

    if (string.equals_ignoring_case("inherit"))
        return CSS::InheritStyleValue::create();
    if (string.equals_ignoring_case("initial"))
        return CSS::InitialStyleValue::create();
    if (string.equals_ignoring_case("auto"))
        return CSS::LengthStyleValue::create(CSS::Length::make_auto());
    if (string.starts_with("var("))
        return CSS::CustomStyleValue::create(parse_custom_property_name(string));
    if (string.starts_with("calc(")) {
        auto calc_expression_string = isolate_calc_expression(string);
        auto calc_expression = parse_calc_expression(calc_expression_string);
        if (calc_expression)
            return CSS::CalculatedStyleValue::create(calc_expression_string, calc_expression.release_nonnull());
    }

    auto value_id = CSS::value_id_from_string(string);
    if (value_id != CSS::ValueID::Invalid)
        return CSS::IdentifierStyleValue::create(value_id);

    auto color = parse_css_color(context, string);
    if (color.has_value())
        return CSS::ColorStyleValue::create(color.value());

    return CSS::StringStyleValue::create(string);
}

RefPtr<CSS::LengthStyleValue> parse_line_width(const CSS::DeprecatedParsingContext& context, const StringView& part)
{
    auto value = parse_css_value(context, part);
    if (value && value->is_length())
        return static_ptr_cast<CSS::LengthStyleValue>(value);
    return nullptr;
}

RefPtr<CSS::ColorStyleValue> parse_color(const CSS::DeprecatedParsingContext& context, const StringView& part)
{
    auto value = parse_css_value(context, part);
    if (value && value->is_color())
        return static_ptr_cast<CSS::ColorStyleValue>(value);
    return nullptr;
}

RefPtr<CSS::IdentifierStyleValue> parse_line_style(const CSS::DeprecatedParsingContext& context, const StringView& part)
{
    auto parsed_value = parse_css_value(context, part);
    if (!parsed_value || parsed_value->type() != CSS::StyleValue::Type::Identifier)
        return nullptr;
    auto value = static_ptr_cast<CSS::IdentifierStyleValue>(parsed_value);
    if (value->id() == CSS::ValueID::Dotted)
        return value;
    if (value->id() == CSS::ValueID::Dashed)
        return value;
    if (value->id() == CSS::ValueID::Solid)
        return value;
    if (value->id() == CSS::ValueID::Double)
        return value;
    if (value->id() == CSS::ValueID::Groove)
        return value;
    if (value->id() == CSS::ValueID::Ridge)
        return value;
    if (value->id() == CSS::ValueID::None)
        return value;
    if (value->id() == CSS::ValueID::Hidden)
        return value;
    if (value->id() == CSS::ValueID::Inset)
        return value;
    if (value->id() == CSS::ValueID::Outset)
        return value;
    return nullptr;
}

class CSSParser {
public:
    CSSParser(const CSS::DeprecatedParsingContext& context, const StringView& input)
        : m_context(context)
        , css(input)
    {
    }

    bool next_is(const char* str) const
    {
        size_t len = strlen(str);
        for (size_t i = 0; i < len; ++i) {
            if (peek(i) != str[i])
                return false;
        }
        return true;
    }

    char peek(size_t offset = 0) const
    {
        if ((index + offset) < css.length())
            return css[index + offset];
        return 0;
    }

    bool consume_specific(char ch)
    {
        if (peek() != ch) {
            dbgln("CSSParser: Peeked '{:c}' wanted specific '{:c}'", peek(), ch);
        }
        if (!peek()) {
            log_parse_error();
            return false;
        }
        if (peek() != ch) {
            log_parse_error();
            ++index;
            return false;
        }
        ++index;
        return true;
    }

    char consume_one()
    {
        PARSE_VERIFY(index < css.length());
        return css[index++];
    };

    bool consume_whitespace_or_comments()
    {
        size_t original_index = index;
        bool in_comment = false;
        for (; index < css.length(); ++index) {
            char ch = peek();
            if (isspace(ch))
                continue;
            if (!in_comment && ch == '/' && peek(1) == '*') {
                in_comment = true;
                ++index;
                continue;
            }
            if (in_comment && ch == '*' && peek(1) == '/') {
                in_comment = false;
                ++index;
                continue;
            }
            if (in_comment)
                continue;
            break;
        }
        return original_index != index;
    }

    static bool is_valid_selector_char(char ch)
    {
        return isalnum(ch) || ch == '-' || ch == '+' || ch == '_' || ch == '(' || ch == ')' || ch == '@';
    }

    static bool is_valid_selector_args_char(char ch)
    {
        return is_valid_selector_char(ch) || ch == ' ' || ch == '\t';
    }

    bool is_combinator(char ch) const
    {
        return ch == '~' || ch == '>' || ch == '+';
    }

    static StringView capture_selector_args(const String& pseudo_name)
    {
        if (const auto start_pos = pseudo_name.find('('); start_pos.has_value()) {
            const auto start = start_pos.value() + 1;
            if (const auto end_pos = pseudo_name.find(')', start); end_pos.has_value()) {
                return pseudo_name.substring_view(start, end_pos.value() - start).trim_whitespace();
            }
        }
        return {};
    }

    Optional<CSS::Selector::SimpleSelector> parse_simple_selector()
    {
        auto index_at_start = index;

        if (consume_whitespace_or_comments())
            return {};

        if (!peek() || peek() == '{' || peek() == ',' || is_combinator(peek()))
            return {};

        CSS::Selector::SimpleSelector simple_selector;

        if (peek() == '*') {
            simple_selector.type = CSS::Selector::SimpleSelector::Type::Universal;
            consume_one();
            return simple_selector;
        }

        if (peek() == '.') {
            simple_selector.type = CSS::Selector::SimpleSelector::Type::Class;
            consume_one();
        } else if (peek() == '#') {
            simple_selector.type = CSS::Selector::SimpleSelector::Type::Id;
            consume_one();
        } else if (isalpha(peek())) {
            simple_selector.type = CSS::Selector::SimpleSelector::Type::TagName;
        } else if (peek() == '[') {
            simple_selector.type = CSS::Selector::SimpleSelector::Type::Attribute;
        } else if (peek() == ':') {
            simple_selector.type = CSS::Selector::SimpleSelector::Type::PseudoClass;
        } else {
            simple_selector.type = CSS::Selector::SimpleSelector::Type::Universal;
        }

        if ((simple_selector.type != CSS::Selector::SimpleSelector::Type::Universal)
            && (simple_selector.type != CSS::Selector::SimpleSelector::Type::Attribute)
            && (simple_selector.type != CSS::Selector::SimpleSelector::Type::PseudoClass)) {

            while (is_valid_selector_char(peek()))
                buffer.append(consume_one());
            PARSE_VERIFY(!buffer.is_empty());
        }

        auto value = String::copy(buffer);

        if (simple_selector.type == CSS::Selector::SimpleSelector::Type::TagName) {
            // Some stylesheets use uppercase tag names, so here's a hack to just lowercase them internally.
            value = value.to_lowercase();
        }

        simple_selector.value = value;
        buffer.clear();

        if (simple_selector.type == CSS::Selector::SimpleSelector::Type::Attribute) {
            CSS::Selector::SimpleSelector::Attribute::MatchType attribute_match_type = CSS::Selector::SimpleSelector::Attribute::MatchType::HasAttribute;
            String attribute_name;
            String attribute_value;
            bool in_value = false;
            consume_specific('[');
            char expected_end_of_attribute_selector = ']';
            while (peek() != expected_end_of_attribute_selector) {
                char ch = consume_one();
                if (ch == '=' || (ch == '~' && peek() == '=')) {
                    if (ch == '=') {
                        attribute_match_type = CSS::Selector::SimpleSelector::Attribute::MatchType::ExactValueMatch;
                    } else if (ch == '~') {
                        consume_one();
                        attribute_match_type = CSS::Selector::SimpleSelector::Attribute::MatchType::ContainsWord;
                    }
                    attribute_name = String::copy(buffer);
                    buffer.clear();
                    in_value = true;
                    consume_whitespace_or_comments();
                    if (peek() == '\'') {
                        expected_end_of_attribute_selector = '\'';
                        consume_one();
                    } else if (peek() == '"') {
                        expected_end_of_attribute_selector = '"';
                        consume_one();
                    }
                    continue;
                }
                // FIXME: This is a hack that will go away when we replace this with a big boy CSS parser.
                if (ch == '\\')
                    ch = consume_one();
                buffer.append(ch);
            }
            if (in_value)
                attribute_value = String::copy(buffer);
            else
                attribute_name = String::copy(buffer);
            buffer.clear();
            simple_selector.attribute.match_type = attribute_match_type;
            simple_selector.attribute.name = attribute_name;
            simple_selector.attribute.value = attribute_value;
            if (expected_end_of_attribute_selector != ']') {
                if (!consume_specific(expected_end_of_attribute_selector))
                    return {};
            }
            consume_whitespace_or_comments();
            if (!consume_specific(']'))
                return {};
        }

        if (simple_selector.type == CSS::Selector::SimpleSelector::Type::PseudoClass) {
            // FIXME: Implement pseudo elements.
            [[maybe_unused]] bool is_pseudo_element = false;
            consume_one();
            if (peek() == ':') {
                is_pseudo_element = true;
                consume_one();
            }
            if (next_is("not")) {
                buffer.append(consume_one());
                buffer.append(consume_one());
                buffer.append(consume_one());
                if (!consume_specific('('))
                    return {};
                buffer.append('(');
                while (peek() != ')')
                    buffer.append(consume_one());
                if (!consume_specific(')'))
                    return {};
                buffer.append(')');
            } else {
                int nesting_level = 0;
                while (true) {
                    const auto ch = peek();
                    if (ch == '(')
                        ++nesting_level;
                    else if (ch == ')' && nesting_level > 0)
                        --nesting_level;

                    if (nesting_level > 0 ? is_valid_selector_args_char(ch) : is_valid_selector_char(ch))
                        buffer.append(consume_one());
                    else
                        break;
                };
            }

            auto pseudo_name = String::copy(buffer);
            buffer.clear();

            // Ignore for now, otherwise we produce a "false positive" selector
            // and apply styles to the element itself, not its pseudo element
            if (is_pseudo_element)
                return {};

            auto& pseudo_class = simple_selector.pseudo_class;

            if (pseudo_name.equals_ignoring_case("link")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Link;
            } else if (pseudo_name.equals_ignoring_case("visited")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Visited;
            } else if (pseudo_name.equals_ignoring_case("active")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Active;
            } else if (pseudo_name.equals_ignoring_case("hover")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Hover;
            } else if (pseudo_name.equals_ignoring_case("focus")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Focus;
            } else if (pseudo_name.equals_ignoring_case("first-child")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::FirstChild;
            } else if (pseudo_name.equals_ignoring_case("last-child")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::LastChild;
            } else if (pseudo_name.equals_ignoring_case("only-child")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::OnlyChild;
            } else if (pseudo_name.equals_ignoring_case("empty")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Empty;
            } else if (pseudo_name.equals_ignoring_case("root")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Root;
            } else if (pseudo_name.equals_ignoring_case("first-of-type")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::FirstOfType;
            } else if (pseudo_name.equals_ignoring_case("last-of-type")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::LastOfType;
            } else if (pseudo_name.starts_with("nth-child", CaseSensitivity::CaseInsensitive)) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::NthChild;
                pseudo_class.nth_child_pattern = CSS::Selector::SimpleSelector::NthChildPattern::parse(capture_selector_args(pseudo_name));
            } else if (pseudo_name.starts_with("nth-last-child", CaseSensitivity::CaseInsensitive)) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::NthLastChild;
                pseudo_class.nth_child_pattern = CSS::Selector::SimpleSelector::NthChildPattern::parse(capture_selector_args(pseudo_name));
            } else if (pseudo_name.equals_ignoring_case("before")) {
                simple_selector.pseudo_element = CSS::Selector::SimpleSelector::PseudoElement::Before;
            } else if (pseudo_name.equals_ignoring_case("after")) {
                simple_selector.pseudo_element = CSS::Selector::SimpleSelector::PseudoElement::After;
            } else if (pseudo_name.equals_ignoring_case("disabled")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Disabled;
            } else if (pseudo_name.equals_ignoring_case("enabled")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Enabled;
            } else if (pseudo_name.equals_ignoring_case("checked")) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Checked;
            } else if (pseudo_name.starts_with("not", CaseSensitivity::CaseInsensitive)) {
                pseudo_class.type = CSS::Selector::SimpleSelector::PseudoClass::Type::Not;
                auto not_selector = Web::parse_selector(m_context, capture_selector_args(pseudo_name));
                if (not_selector) {
                    pseudo_class.not_selector.clear();
                    pseudo_class.not_selector.append(not_selector.release_nonnull());
                }
            } else {
                dbgln("Unknown pseudo class: '{}'", pseudo_name);
                return {};
            }
        }

        if (index == index_at_start) {
            // We consumed nothing.
            return {};
        }

        return simple_selector;
    }

    Optional<CSS::Selector::ComplexSelector> parse_complex_selector()
    {
        auto relation = CSS::Selector::ComplexSelector::Relation::Descendant;

        if (peek() == '{' || peek() == ',')
            return {};

        if (is_combinator(peek())) {
            switch (peek()) {
            case '>':
                relation = CSS::Selector::ComplexSelector::Relation::ImmediateChild;
                break;
            case '+':
                relation = CSS::Selector::ComplexSelector::Relation::AdjacentSibling;
                break;
            case '~':
                relation = CSS::Selector::ComplexSelector::Relation::GeneralSibling;
                break;
            }
            consume_one();
            consume_whitespace_or_comments();
        }

        consume_whitespace_or_comments();

        Vector<CSS::Selector::SimpleSelector> simple_selectors;
        for (;;) {
            auto component = parse_simple_selector();
            if (!component.has_value())
                break;
            simple_selectors.append(component.value());
            // If this assert triggers, we're most likely up to no good.
            PARSE_VERIFY(simple_selectors.size() < 100);
        }

        if (simple_selectors.is_empty())
            return {};

        return CSS::Selector::ComplexSelector { relation, move(simple_selectors) };
    }

    void parse_selector()
    {
        Vector<CSS::Selector::ComplexSelector> complex_selectors;

        for (;;) {
            auto index_before = index;
            auto complex_selector = parse_complex_selector();
            if (complex_selector.has_value())
                complex_selectors.append(complex_selector.value());
            consume_whitespace_or_comments();
            if (!peek() || peek() == ',' || peek() == '{')
                break;
            // HACK: If we didn't move forward, just let go.
            if (index == index_before)
                break;
        }

        if (complex_selectors.is_empty())
            return;
        complex_selectors.first().relation = CSS::Selector::ComplexSelector::Relation::None;

        current_rule.selectors.append(CSS::Selector::create(move(complex_selectors)));
    }

    RefPtr<CSS::Selector> parse_individual_selector()
    {
        parse_selector();
        if (current_rule.selectors.is_empty())
            return {};
        return current_rule.selectors.last();
    }

    void parse_selector_list()
    {
        for (;;) {
            auto index_before = index;
            parse_selector();
            consume_whitespace_or_comments();
            if (peek() == ',') {
                consume_one();
                continue;
            }
            if (peek() == '{')
                break;
            // HACK: If we didn't move forward, just let go.
            if (index_before == index)
                break;
        }
    }

    bool is_valid_property_name_char(char ch) const
    {
        return ch && !isspace(ch) && ch != ':';
    }

    bool is_valid_property_value_char(char ch) const
    {
        return ch && ch != '!' && ch != ';' && ch != '}';
    }

    bool is_valid_string_quotes_char(char ch) const
    {
        return ch == '\'' || ch == '\"';
    }

    struct ValueAndImportant {
        String value;
        bool important { false };
    };

    ValueAndImportant consume_css_value()
    {
        buffer.clear();

        int paren_nesting_level = 0;
        bool important = false;

        for (;;) {
            char ch = peek();
            if (ch == '(') {
                ++paren_nesting_level;
                buffer.append(consume_one());
                continue;
            }
            if (ch == ')') {
                PARSE_VERIFY(paren_nesting_level > 0);
                --paren_nesting_level;
                buffer.append(consume_one());
                continue;
            }
            if (paren_nesting_level > 0) {
                buffer.append(consume_one());
                continue;
            }
            if (next_is("!important")) {
                consume_specific('!');
                consume_specific('i');
                consume_specific('m');
                consume_specific('p');
                consume_specific('o');
                consume_specific('r');
                consume_specific('t');
                consume_specific('a');
                consume_specific('n');
                consume_specific('t');
                important = true;
                continue;
            }
            if (next_is("/*")) {
                consume_whitespace_or_comments();
                continue;
            }
            if (!ch)
                break;
            if (ch == '\\') {
                consume_one();
                buffer.append(consume_one());
                continue;
            }
            if (ch == '}')
                break;
            if (ch == ';')
                break;
            buffer.append(consume_one());
        }

        // Remove trailing whitespace.
        while (!buffer.is_empty() && isspace(buffer.last()))
            buffer.take_last();

        auto string = String::copy(buffer);
        buffer.clear();

        return { string, important };
    }

    Optional<CSS::StyleProperty> parse_property()
    {
        consume_whitespace_or_comments();
        if (peek() == ';') {
            consume_one();
            return {};
        }
        if (peek() == '}')
            return {};
        buffer.clear();
        while (is_valid_property_name_char(peek()))
            buffer.append(consume_one());
        auto property_name = String::copy(buffer);
        buffer.clear();
        consume_whitespace_or_comments();
        if (!consume_specific(':'))
            return {};
        consume_whitespace_or_comments();

        auto [property_value, important] = consume_css_value();

        consume_whitespace_or_comments();

        if (peek() && peek() != '}') {
            if (!consume_specific(';'))
                return {};
        }

        auto property_id = CSS::property_id_from_string(property_name);

        if (property_id == CSS::PropertyID::Invalid && property_name.starts_with("--"))
            property_id = CSS::PropertyID::Custom;

        if (property_id == CSS::PropertyID::Invalid && !property_name.starts_with("-")) {
            dbgln("CSSParser: Unrecognized property '{}'", property_name);
        }
        auto value = parse_css_value(m_context, property_value, property_id);
        if (!value)
            return {};
        if (property_id == CSS::PropertyID::Custom) {
            return CSS::StyleProperty { property_id, value.release_nonnull(), property_name, important };
        }
        return CSS::StyleProperty { property_id, value.release_nonnull(), {}, important };
    }

    void parse_declaration()
    {
        for (;;) {
            auto property = parse_property();
            if (property.has_value()) {
                auto property_value = property.value();
                if (property_value.property_id == CSS::PropertyID::Custom)
                    current_rule.custom_properties.set(property_value.custom_name, property_value);
                else
                    current_rule.properties.append(property_value);
            }
            consume_whitespace_or_comments();
            if (!peek() || peek() == '}')
                break;
        }
    }

    void parse_style_rule()
    {
        parse_selector_list();
        if (!consume_specific('{')) {
            log_parse_error();
            return;
        }
        parse_declaration();
        if (!consume_specific('}')) {
            log_parse_error();
            return;
        }

        rules.append(CSS::CSSStyleRule::create(move(current_rule.selectors), CSS::CSSStyleDeclaration::create(move(current_rule.properties), move(current_rule.custom_properties))));
    }

    Optional<String> parse_string()
    {
        if (!is_valid_string_quotes_char(peek())) {
            log_parse_error();
            return {};
        }

        char end_char = consume_one();
        buffer.clear();
        while (peek() && peek() != end_char) {
            if (peek() == '\\') {
                consume_specific('\\');
                if (peek() == 0)
                    break;
            }
            buffer.append(consume_one());
        }

        String string_value(String::copy(buffer));
        buffer.clear();

        if (consume_specific(end_char)) {
            return { string_value };
        }
        return {};
    }

    Optional<String> parse_url()
    {
        if (is_valid_string_quotes_char(peek()))
            return parse_string();

        buffer.clear();
        while (peek() && peek() != ')')
            buffer.append(consume_one());

        String url_value(String::copy(buffer));
        buffer.clear();

        if (peek() == ')')
            return { url_value };
        return {};
    }

    void parse_at_import_rule()
    {
        consume_whitespace_or_comments();
        Optional<String> imported_address;
        if (is_valid_string_quotes_char(peek())) {
            imported_address = parse_string();
        } else if (next_is("url")) {
            consume_specific('u');
            consume_specific('r');
            consume_specific('l');

            consume_whitespace_or_comments();

            if (!consume_specific('('))
                return;
            imported_address = parse_url();
            if (!consume_specific(')'))
                return;
        } else {
            log_parse_error();
            return;
        }

        if (imported_address.has_value())
            rules.append(CSS::CSSImportRule::create(m_context.complete_url(imported_address.value())));

        // FIXME: We ignore possible media query list
        while (peek() && peek() != ';')
            consume_one();

        consume_specific(';');
    }

    void parse_at_rule()
    {
        HashMap<String, void (CSSParser::*)()> at_rules_parsers({ { "@import", &CSSParser::parse_at_import_rule } });

        for (const auto& rule_parser_pair : at_rules_parsers) {
            if (next_is(rule_parser_pair.key.characters())) {
                for (char c : rule_parser_pair.key) {
                    consume_specific(c);
                }
                (this->*(rule_parser_pair.value))();
                return;
            }
        }

        // FIXME: We ignore other @-rules completely for now.
        int level = 0;
        bool in_comment = false;

        while (peek() != 0) {
            auto ch = consume_one();

            if (!in_comment) {
                if (ch == '/' && peek() == '*') {
                    consume_one();
                    in_comment = true;
                } else if (ch == '{') {
                    ++level;
                } else if (ch == '}') {
                    --level;
                    if (level == 0)
                        break;
                }
            } else {
                if (ch == '*' && peek() == '/') {
                    consume_one();
                    in_comment = false;
                }
            }
        }
    }

    void parse_rule()
    {
        consume_whitespace_or_comments();
        if (!peek())
            return;

        if (peek() == '@') {
            parse_at_rule();
        } else {
            parse_style_rule();
        }

        consume_whitespace_or_comments();
    }

    RefPtr<CSS::CSSStyleSheet> parse_sheet()
    {
        if (peek(0) == (char)0xef && peek(1) == (char)0xbb && peek(2) == (char)0xbf) {
            // HACK: Skip UTF-8 BOM.
            index += 3;
        }

        while (peek()) {
            parse_rule();
        }

        return CSS::CSSStyleSheet::create(move(rules));
    }

    RefPtr<CSS::CSSStyleDeclaration> parse_standalone_declaration()
    {
        consume_whitespace_or_comments();
        for (;;) {
            auto property = parse_property();
            if (property.has_value()) {
                auto property_value = property.value();
                if (property_value.property_id == CSS::PropertyID::Custom)
                    current_rule.custom_properties.set(property_value.custom_name, property_value);
                else
                    current_rule.properties.append(property_value);
            }
            consume_whitespace_or_comments();
            if (!peek())
                break;
        }
        return CSS::CSSStyleDeclaration::create(move(current_rule.properties), move(current_rule.custom_properties));
    }

private:
    CSS::DeprecatedParsingContext m_context;

    NonnullRefPtrVector<CSS::CSSRule> rules;

    struct CurrentRule {
        NonnullRefPtrVector<CSS::Selector> selectors;
        Vector<CSS::StyleProperty> properties;
        HashMap<String, CSS::StyleProperty> custom_properties;
    };

    CurrentRule current_rule;
    Vector<char> buffer;

    size_t index = 0;

    StringView css;
};

RefPtr<CSS::Selector> parse_selector(const CSS::DeprecatedParsingContext& context, const StringView& selector_text)
{
    CSSParser parser(context, selector_text);
    return parser.parse_individual_selector();
}

RefPtr<CSS::CSSStyleSheet> parse_css(const CSS::DeprecatedParsingContext& context, const StringView& css)
{
    if (css.is_empty())
        return CSS::CSSStyleSheet::create({});
    CSSParser parser(context, css);
    return parser.parse_sheet();
}

RefPtr<CSS::CSSStyleDeclaration> parse_css_declaration(const CSS::DeprecatedParsingContext& context, const StringView& css)
{
    if (css.is_empty())
        return CSS::CSSStyleDeclaration::create({}, {});
    CSSParser parser(context, css);
    return parser.parse_standalone_declaration();
}

RefPtr<CSS::StyleValue> parse_html_length(const DOM::Document& document, const StringView& string)
{
    auto integer = string.to_int();
    if (integer.has_value())
        return CSS::LengthStyleValue::create(CSS::Length::make_px(integer.value()));
    return parse_css_value(CSS::DeprecatedParsingContext(document), string);
}
}
