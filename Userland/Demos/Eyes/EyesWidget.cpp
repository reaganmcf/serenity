/*
 * Copyright (c) 2020, Sergey Bugaev <bugaevc@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include "EyesWidget.h"
#include <AK/Math.h>
#include <AK/StdLibExtraDetails.h>
#include <LibGUI/Painter.h>
#include <LibGUI/Window.h>
#include <LibGUI/WindowServerConnection.h>
#include <LibGfx/Palette.h>

EyesWidget::~EyesWidget()
{
}

void EyesWidget::track_cursor_globally()
{
    VERIFY(window());
    auto window_id = window()->window_id();
    VERIFY(window_id >= 0);

    set_global_cursor_tracking(true);
    GUI::WindowServerConnection::the().async_set_global_cursor_tracking(window_id, true);
}

void EyesWidget::mousemove_event(GUI::MouseEvent& event)
{
    m_mouse_position = event.position();
    update();
}

void EyesWidget::paint_event(GUI::PaintEvent& event)
{
    GUI::Painter painter(*this);

    painter.clear_rect(event.rect(), Gfx::Color());

    for (int i = 0; i < m_full_rows; i++) {
        for (int j = 0; j < m_eyes_in_row; j++)
            render_eyeball(i, j, painter);
    }
    for (int i = 0; i < m_extra_columns; ++i)
        render_eyeball(m_full_rows, i, painter);
}

void EyesWidget::render_eyeball(int row, int column, GUI::Painter& painter) const
{
    auto eye_width = width() / m_eyes_in_row;
    auto eye_height = height() / m_num_rows;
    Gfx::IntRect bounds { column * eye_width, row * eye_height, eye_width, eye_height };
    auto width_thickness = max(int(eye_width / 5.5), 1);
    auto height_thickness = max(int(eye_height / 5.5), 1);

    bounds.shrink(int(eye_width / 12.5), 0);
    painter.fill_ellipse(bounds, palette().base_text());
    bounds.shrink(width_thickness, height_thickness);
    painter.fill_ellipse(bounds, palette().base());

    Gfx::IntPoint pupil_center = this->pupil_center(bounds);
    Gfx::IntSize pupil_size {
        bounds.width() / 5,
        bounds.height() / 5
    };
    Gfx::IntRect pupil {
        pupil_center.x() - pupil_size.width() / 2,
        pupil_center.y() - pupil_size.height() / 2,
        pupil_size.width(),
        pupil_size.height()
    };

    painter.fill_ellipse(pupil, palette().base_text());
}

Gfx::IntPoint EyesWidget::pupil_center(Gfx::IntRect& eyeball_bounds) const
{
    auto mouse_vector = m_mouse_position - eyeball_bounds.center();
    double dx = mouse_vector.x();
    double dy = mouse_vector.y();
    double mouse_distance = AK::hypot(dx, dy);

    if (mouse_distance == 0.0)
        return eyeball_bounds.center();

    double width_squared = eyeball_bounds.width() * eyeball_bounds.width();
    double height_squared = eyeball_bounds.height() * eyeball_bounds.height();

    double max_distance_along_this_direction;

    // clang-format off
    if (dx != 0 && AK::abs(dx) >= AK::abs(dy)) {
        double slope = dy / dx;
        double slope_squared = slope * slope;
        max_distance_along_this_direction = 0.25 * AK::sqrt(
            (slope_squared + 1) /
            (1 / width_squared + slope_squared / height_squared)
        );
    } else if (dy != 0 && AK::abs(dy) >= AK::abs(dx)) {
        double slope = dx / dy;
        double slope_squared = slope * slope;
        max_distance_along_this_direction = 0.25 * AK::sqrt(
            (slope_squared + 1) /
            (slope_squared / width_squared + 1 / height_squared)
        );
    } else {
        VERIFY_NOT_REACHED();
    }
    // clang-format on

    double scale = min(1.0, max_distance_along_this_direction / mouse_distance);

    return {
        eyeball_bounds.center().x() + int(dx * scale),
        eyeball_bounds.center().y() + int(dy * scale)
    };
}
