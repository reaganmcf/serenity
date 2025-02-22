/*
 * Copyright (c) 2020, Hunter Salyer <thefalsehonesty@gmail.com>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "ConsoleWidget.h"
#include <AK/StringBuilder.h>
#include <LibGUI/BoxLayout.h>
#include <LibGUI/Button.h>
#include <LibGUI/TextBox.h>
#include <LibGfx/FontDatabase.h>
#include <LibJS/Interpreter.h>
#include <LibJS/MarkupGenerator.h>
#include <LibJS/Parser.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/SyntaxHighlighter.h>
#include <LibWeb/Bindings/DOMExceptionWrapper.h>
#include <LibWeb/DOM/DocumentType.h>
#include <LibWeb/DOM/ElementFactory.h>
#include <LibWeb/DOM/Text.h>
#include <LibWeb/DOMTreeModel.h>
#include <LibWeb/HTML/HTMLBodyElement.h>

namespace Browser {

ConsoleWidget::ConsoleWidget()
{
    set_layout<GUI::VerticalBoxLayout>();
    set_fill_with_background_color(true);

    auto base_document = Web::DOM::Document::create();
    base_document->append_child(adopt_ref(*new Web::DOM::DocumentType(base_document)));
    auto html_element = base_document->create_element("html");
    base_document->append_child(html_element);
    auto head_element = base_document->create_element("head");
    html_element->append_child(head_element);
    auto body_element = base_document->create_element("body");
    html_element->append_child(body_element);
    m_output_container = body_element;

    m_output_view = add<Web::InProcessWebView>();
    m_output_view->set_document(base_document);

    auto& bottom_container = add<GUI::Widget>();
    bottom_container.set_layout<GUI::HorizontalBoxLayout>();
    bottom_container.set_fixed_height(22);

    m_input = bottom_container.add<GUI::TextBox>();
    m_input->set_syntax_highlighter(make<JS::SyntaxHighlighter>());
    // FIXME: Syntax Highlighting breaks the cursor's position on non fixed-width fonts.
    m_input->set_font(Gfx::FontDatabase::default_fixed_width_font());
    m_input->set_history_enabled(true);

    m_input->on_return_pressed = [this] {
        auto js_source = m_input->text();

        // FIXME: An is_blank check to check if there is only whitespace would probably be preferable.
        if (js_source.is_empty())
            return;

        m_input->add_current_text_to_history();
        m_input->clear();

        print_source_line(js_source);

        if (on_js_input)
            on_js_input(js_source);

        // no interpreter being set means we are running in multi-process mode
        if (!m_interpreter)
            return;

        auto parser = JS::Parser(JS::Lexer(js_source));
        auto program = parser.parse_program();

        StringBuilder output_html;
        if (parser.has_errors()) {
            auto error = parser.errors()[0];
            auto hint = error.source_location_hint(js_source);
            if (!hint.is_empty())
                output_html.append(String::formatted("<pre>{}</pre>", escape_html_entities(hint)));
            m_interpreter->vm().throw_exception<JS::SyntaxError>(m_interpreter->global_object(), error.to_string());
        } else {
            m_interpreter->run(m_interpreter->global_object(), *program);
        }

        if (m_interpreter->exception()) {
            auto* exception = m_interpreter->exception();
            m_interpreter->vm().clear_exception();
            output_html.append("Uncaught exception: ");
            auto error = exception->value();
            if (error.is_object())
                output_html.append(JS::MarkupGenerator::html_from_error(error.as_object()));
            else
                output_html.append(JS::MarkupGenerator::html_from_value(error));
            print_html(output_html.string_view());
            return;
        }

        print_html(JS::MarkupGenerator::html_from_value(m_interpreter->vm().last_value()));
    };

    set_focus_proxy(m_input);

    auto& clear_button = bottom_container.add<GUI::Button>();
    clear_button.set_fixed_size(22, 22);
    clear_button.set_icon(Gfx::Bitmap::try_load_from_file("/res/icons/16x16/delete.png"));
    clear_button.set_tooltip("Clear the console output");
    clear_button.on_click = [this](auto) {
        clear_output();
    };
}

ConsoleWidget::~ConsoleWidget()
{
}

void ConsoleWidget::handle_js_console_output(const String& method, const String& line)
{
    if (method == "html") {
        print_html(line);
    } else if (method == "clear") {
        clear_output();
    }
}

void ConsoleWidget::set_interpreter(WeakPtr<JS::Interpreter> interpreter)
{
    if (m_interpreter.ptr() == interpreter.ptr())
        return;

    m_interpreter = interpreter;
    m_console_client = make<BrowserConsoleClient>(interpreter->global_object().console(), *this);
    interpreter->global_object().console().set_client(*m_console_client.ptr());

    clear_output();
}

void ConsoleWidget::print_source_line(const StringView& source)
{
    StringBuilder html;
    html.append("<span class=\"repl-indicator\">");
    html.append("&gt; ");
    html.append("</span>");

    html.append(JS::MarkupGenerator::html_from_source(source));

    print_html(html.string_view());
}

void ConsoleWidget::print_html(const StringView& line)
{
    auto paragraph = m_output_container->document().create_element("p");
    paragraph->set_inner_html(line);

    m_output_container->append_child(paragraph);
    m_output_container->document().invalidate_layout();
    m_output_container->document().update_layout();

    m_output_view->scroll_to_bottom();
}

void ConsoleWidget::clear_output()
{
    m_output_container->remove_all_children();
    m_output_view->update();
}

}
