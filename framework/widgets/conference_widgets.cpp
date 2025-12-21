#include "../headers/includes.h"
#include "../headers/conference_widgets.h"
#include "../headers/widgets.h"
#include "../headers/app_state.h"
#include "../headers/draw.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <ctime>

// --- Helpers ---

// Helper: Get high contrast text color (white or black)
static ImU32 get_contrast_text_color(ImU32 bg_color) {
    ImVec4 col = ImGui::ColorConvertU32ToFloat4(bg_color);
    float brightness = (col.x * 0.299f + col.y * 0.587f + col.z * 0.114f);
    return brightness > 0.6f ? IM_COL32(20, 20, 20, 255) : IM_COL32(255, 255, 255, 255);
}

// Helper: Generating colors (Optimized: Simple calc)
static ImU32 get_user_color_u32(int user_id) {
    // Determine color deterministically without expensive HSV conversion every frame
    const ImU32 palette[] = {
        IM_COL32(100, 100, 160, 255), IM_COL32(100, 160, 100, 255),
        IM_COL32(160, 100, 100, 255), IM_COL32(160, 160, 100, 255),
        IM_COL32(100, 160, 160, 255), IM_COL32(160, 100, 160, 255)
    };
    return palette[user_id % 6];
}

static void draw_user_avatar_bg(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, int user_id) {
    // OPTIMIZATION: Use solid color instead of 4-point gradient for performance
    draw->rect_filled(draw_list, min, max, get_user_color_u32(user_id), SCALE(12));
}

// --- Custom Widget Implementations for Conference ---

// Generic Button State
struct button_state {
    float alpha{ 0.0f };
    float scale{ 0.0f };
    bool clicked{ false };
};

// Custom Text Button (Modern Pill Style)
static bool button_text(const char* label, const ImVec2& size_arg, ImU32 bg_col, ImU32 bg_hover, bool outline = false) {
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return false;

    ImGuiContext& g = *GImGui;
    const ImGuiID id = window->GetID(label);
    ImVec2 pos = window->DC.CursorPos;
    ImRect total(pos, pos + size_arg);

    gui->item_size(total);
    if (!gui->item_add(total, id)) return false;

    bool hovered, held;
    bool pressed = gui->button_behavior(total, id, &hovered, &held, 0);

    button_state* state = gui->anim_container<button_state>(id);
    if (pressed) state->clicked = true;
    if (state->alpha > 0.99f && !held) state->clicked = false;

    // OPTIMIZATION: Faster animation speed (20 -> 60)
    gui->easing(state->alpha, hovered ? 1.0f : 0.0f, gui->fixed_speed(60.0f), dynamic_easing);
    
    // OPTIMIZATION: Overlay blending instead of Lerp
    if (state->alpha > 0.01f) {
         draw->rect_filled(window->DrawList, total.Min, total.Max, bg_col, SCALE(10));
         draw->rect_filled(window->DrawList, total.Min, total.Max, draw->get_clr(ImGui::ColorConvertU32ToFloat4(bg_hover), state->alpha), SCALE(10));
    } else {
         draw->rect_filled(window->DrawList, total.Min, total.Max, bg_col, SCALE(10));
    }
    
    if (outline) {
         draw->rect(window->DrawList, total.Min, total.Max, draw->get_clr(clr->main.outline), SCALE(10), 0, 1.0f);
    }
    
    ImU32 text_col = get_contrast_text_color(bg_col);
    // Check if label is a single uppercase letter (A-Z) - use icons font, otherwise use text font
    bool is_icon = (strlen(label) == 1 && label[0] >= 'A' && label[0] <= 'Z');
    ImFont* font_to_use = is_icon ? font->get(icons_data, 14) : font->get(inter_bold_data, 14);
    draw->text_clipped(window->DrawList, font_to_use,
        total.Min, total.Max, text_col, label, nullptr, nullptr, ImVec2(0.5f, 0.5f));

    return pressed;
}

// Custom Icon Button (Circular Floating Action)
static bool button_icon(const char* icon, float radius, ImU32 bg_default, ImU32 bg_active, bool active, bool alert = false) {
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return false;
    
    ImGuiID id = window->GetID(icon);
    ImVec2 pos = window->DC.CursorPos;
    float diameter = radius * 2.0f;
    ImRect total(pos, pos + ImVec2(diameter, diameter));
    
    gui->item_size(total); 
    if (!gui->item_add(total, id)) return false; 
    
    bool hovered, held;
    bool pressed = gui->button_behavior(total, id, &hovered, &held, 0);
    
    button_state* state = gui->anim_container<button_state>(id);
    if (state->scale < 0.01f) state->scale = 1.0f; 

    // Fast animation
    gui->easing(state->scale, hovered ? 1.15f : 1.0f, gui->fixed_speed(60.f), dynamic_easing);
    
    ImU32 col_bg = bg_default;
    if (alert) col_bg = IM_COL32(220, 60, 60, 255);
    else if (active) col_bg = bg_active;
    else if (hovered) col_bg = draw->get_clr(clr->main.text_inactive, 0.5f); 
    
    float r = radius * state->scale;
    
    // Low cost hover glow
    if (hovered) {
        ImU32 glow_col = draw->get_clr(ImGui::ColorConvertU32ToFloat4(col_bg), 0.3f);
        draw->circle_filled(window->DrawList, total.GetCenter(), r + SCALE(5), glow_col, 24);
    }
    
    draw->circle_filled(window->DrawList, total.GetCenter(), r, col_bg, 32);
    
    // OPTIMIZATION: Do NOT scale font dynamically. 
    float font_sz = 16.0f;

    draw->text_clipped(window->DrawList, font->get(icons_data, font_sz),
        total.Min, total.Max, IM_COL32(255,255,255,255), icon, nullptr, nullptr, ImVec2(0.5f, 0.5f));
        
    return pressed;
}

// Custom Cycle Selector (Modern with improved animations)
static bool cycle_selector(const char* label, const char* current_val) {
    struct selector_state
    {
        float hover_alpha{ 0.0f };
        float click_animation{ 0.0f };
        float glow_animation{ 0.0f };
    };

    ImGuiWindow* window = gui->get_window();
    ImGuiContext& g = *GImGui;
    ImGuiID id = window->GetID(label);
    
    ImVec2 pos = window->DC.CursorPos;
    ImVec2 size(SCALE(340), SCALE(50));
    ImRect rect(pos, pos + size);
    
    gui->item_size(rect);
    if (!gui->item_add(rect, id)) return false;
    
    bool hovered, held;
    bool pressed = gui->button_behavior(rect, id, &hovered, &held, 0);
    
    selector_state* state = gui->anim_container<selector_state>(id);
    
    // Анимация наведения
    float target_hover = hovered ? 1.0f : 0.0f;
    gui->easing(state->hover_alpha, target_hover, 15.f, dynamic_easing);
    
    // Анимация клика
    if (pressed)
    {
        state->click_animation = 1.0f;
    }
    gui->easing(state->click_animation, 0.0f, 20.f, dynamic_easing);
    
    // Анимация подсветки
    float target_glow = hovered ? 1.0f : 0.0f;
    gui->easing(state->glow_animation, target_glow, 18.f, dynamic_easing);
    
    // Градиентные цвета
    ImVec4 grad_color_1 = clr->messages.background.Value;
    ImVec4 grad_color_2 = clr->main.outline.Value;
    
    if (hovered)
    {
        grad_color_1 = ImLerp(grad_color_1, clr->main.accent.Value, state->hover_alpha * 0.3f);
        grad_color_2 = ImLerp(grad_color_2, clr->main.grad_1.Value, state->hover_alpha * 0.3f);
    }
    
    ImU32 grad_col_1 = draw->get_clr(grad_color_1);
    ImU32 grad_col_2 = draw->get_clr(grad_color_2);
    
    // Эффект клика - небольшое сжатие
    float click_scale = 1.0f - (state->click_animation * 0.03f);
    ImVec2 center = rect.GetCenter();
    ImVec2 scaled_size = (rect.Max - rect.Min) * click_scale;
    ImRect scaled_rect(center - scaled_size * 0.5f, center + scaled_size * 0.5f);
    
    // Градиентный фон
    draw->rect_filled_multi_color(window->DrawList, scaled_rect.Min, scaled_rect.Max,
        grad_col_1, grad_col_2, grad_col_2, grad_col_1, SCALE(12));
    
    // Анимированная подсветка
    if (state->glow_animation > 0.01f)
    {
        float time = ImGui::GetTime();
        float glow_intensity = sinf(time * 2.5f) * 0.2f + 0.8f;
        ImVec4 glow_color = ImVec4(1.0f, 1.0f, 1.0f, 0.08f * state->glow_animation * glow_intensity);
        ImU32 glow_col = draw->get_clr(glow_color);
        draw->rect_filled_multi_color(window->DrawList, scaled_rect.Min, scaled_rect.Max,
            glow_col, IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0), glow_col, SCALE(12));
    }
        
    // Label
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
        scaled_rect.Min + SCALE(15, 0), scaled_rect.Max, draw->get_clr(clr->main.text_inactive), 
        label, nullptr, nullptr, ImVec2(0, 0.5f));
        
    // Value с анимацией при клике
    ImVec4 value_color = hovered ? clr->main.accent.Value : clr->main.text.Value;
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 15),
        scaled_rect.Min, scaled_rect.Max - SCALE(40, 0), draw->get_clr(value_color), 
        current_val, nullptr, nullptr, ImVec2(1, 0.5f));
        
    // Arrow с анимацией
    float arrow_offset = state->click_animation * SCALE(3.0f);
    ImVec2 arrow_pos = scaled_rect.Max - SCALE(30, 0) + ImVec2(arrow_offset, 0);
    draw->text_clipped(window->DrawList, font->get(icons_data, 12),
        arrow_pos - ImVec2(SCALE(20), 0), arrow_pos + ImVec2(SCALE(20), scaled_rect.GetHeight()),
        draw->get_clr(clr->main.text_inactive), "N", nullptr, nullptr, ImVec2(0, 0.5f)); // N=Next/Right
    
    return pressed;
}


// --- Main Render Methods ---

void c_conference_widgets::render_conference_list(bool interactable) {
    ImGuiWindow* window = gui->get_window();
    if (window->SkipItems) return;

    ImVec2 screen_size = SCALE(var->window.size);
    ImVec2 content_pos = SCALE(0, 0); 
    ImVec2 content_size = screen_size;

    // Main background
    draw->rect_filled(window->DrawList, content_pos, content_pos + content_size,
        draw->get_clr(clr->conference.background), 0);

    // --- Header ---
    float header_height = SCALE(120);
    
    // Back Button
    ImVec2 back_pos = content_pos + SCALE(30, 40);
    gui->set_pos(back_pos, pos_all);
    if (interactable) {
        if (button_text("L", SCALE(40, 40), draw->get_clr(clr->window.background), draw->get_clr(clr->main.outline), true)) { // L=Left/Back
            app_state->current_screen = AppScreen::MainMessenger;
        }
    }

    // Title
    ImVec2 title_pos = content_pos + SCALE(85, 38);
    draw->text(window->DrawList, font->get(inter_bold_data, 28), 0, title_pos, 
        draw->get_clr(clr->main.text), "Conferences");
    draw->text(window->DrawList, font->get(inter_medium_data, 14), 0, title_pos + SCALE(0, 32), 
        draw->get_clr(clr->main.text_inactive), "Manage your meetings and video calls");

    // "Create" Button с градиентом и анимацией
    struct create_btn_state
    {
        float glow_animation{ 0.0f };
    };
    
    const ImGuiID create_btn_id = window->GetID("new_conference_btn");
    create_btn_state* create_state = gui->anim_container<create_btn_state>(create_btn_id);
    
    ImVec2 btn_size = SCALE(180, 44);
    ImVec2 btn_pos = content_pos + ImVec2(screen_size.x - btn_size.x - SCALE(40), SCALE(40));
    ImRect create_btn_rect(btn_pos, btn_pos + btn_size);
    bool create_btn_hovered = create_btn_rect.Contains(gui->mouse_pos());
    bool create_btn_clicked = create_btn_hovered && gui->mouse_clicked(mouse_button_left);
    
    if (create_btn_clicked && interactable) {
             ui_state = ConferenceUIState::CreationModal;
            show_create_modal = true;
            create_modal_alpha = 0.0f;
        }
    
    // Анимация подсветки
    float create_target_glow = create_btn_hovered ? 1.0f : 0.0f;
    gui->easing(create_state->glow_animation, create_target_glow, 15.f, dynamic_easing);
    
    // Градиентные цвета
    ImVec4 create_grad_1 = clr->main.accent.Value;
    ImVec4 create_grad_2 = clr->conference.primary.Value;
    
    if (create_btn_hovered) {
        create_grad_1 = ImLerp(create_grad_1, ImVec4(create_grad_1.x * 1.2f, create_grad_1.y * 1.2f, create_grad_1.z * 1.2f, create_grad_1.w), create_state->glow_animation);
        create_grad_2 = ImLerp(create_grad_2, ImVec4(create_grad_2.x * 1.2f, create_grad_2.y * 1.2f, create_grad_2.z * 1.2f, create_grad_2.w), create_state->glow_animation);
    }
    
    ImU32 create_col_1 = draw->get_clr(create_grad_1);
    ImU32 create_col_2 = draw->get_clr(create_grad_2);
    
    if (create_btn_hovered) {
        draw->shadow_rect(window->DrawList, create_btn_rect.Min, create_btn_rect.Max,
            draw->get_clr(ImVec4(0, 0, 0, 0.2f)), SCALE(12), ImVec2(0, SCALE(3)),
            0, SCALE(10));
    }
    
    // Градиентная кнопка
    draw->rect_filled_multi_color(window->DrawList, create_btn_rect.Min, create_btn_rect.Max,
        create_col_1, create_col_2, create_col_2, create_col_1, SCALE(10));
    
    // Анимированная подсветка
    if (create_state->glow_animation > 0.01f) {
        float time = ImGui::GetTime();
        float glow_intensity = sinf(time * 2.0f) * 0.3f + 0.7f;
        ImVec4 glow_color_1 = ImVec4(1.0f, 1.0f, 1.0f, 0.15f * create_state->glow_animation * glow_intensity);
        ImVec4 glow_color_2 = ImVec4(1.0f, 1.0f, 1.0f, 0.05f * create_state->glow_animation * glow_intensity);
        ImU32 glow_col_1 = draw->get_clr(glow_color_1);
        ImU32 glow_col_2 = draw->get_clr(glow_color_2);
        draw->rect_filled_multi_color(window->DrawList, create_btn_rect.Min, create_btn_rect.Max,
            glow_col_1, glow_col_2, glow_col_2, glow_col_1, SCALE(10));
    }
    
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 14),
        create_btn_rect.Min, create_btn_rect.Max,
        draw->get_clr(clr->button.text), "+ New Conference", nullptr, nullptr, ImVec2(0.5f, 0.5f));
    
    gui->item_size(create_btn_rect);

    // --- List Area ---
    if (interactable) {
        ImVec2 list_pos = content_pos + ImVec2(0, header_height);
        ImVec2 list_size = content_size - ImVec2(0, header_height);
        
        gui->begin_content("conf_list", list_size, SCALE(0, 0), SCALE(0, 30));
        {
            ImGuiWindow* list_window = gui->get_window();
            auto conferences = conference_manager->GetUserConferences(conference_manager->current_user_id);
            
            if (conferences.empty()) {
                ImVec2 center = list_pos + list_size * 0.5f;
                draw->text_clipped(list_window->DrawList, font->get(inter_bold_data, 18),
                   list_pos, list_pos + list_size, draw->get_clr(clr->main.text_inactive), 
                   "No scheduled conferences.", nullptr, nullptr, ImVec2(0.5f, 0.45f));
            }

            float padding_x = SCALE(40);
            
            for (const auto& conf : conferences) {
                // Получаем текущую позицию курсора
                ImVec2 cur_pos = list_window->DC.CursorPos;
                // Высота карточки: 18 (status) + 8 (отступ) + 24 (title высота) + 36 (отступ) + 18 (info строка 1) + 24 (отступ) + 18 (info строка 2) + 24 (нижний отступ) = 170px
                // Увеличиваем до 180px для комфорта
                ImVec2 card_size = ImVec2(list_size.x - padding_x * 2, SCALE(180));
                
                // Позиция карточки с учетом отступа слева
                ImVec2 card_pos = ImVec2(cur_pos.x + padding_x, cur_pos.y);
                ImRect card_rect(card_pos, card_pos + card_size);
                
                bool card_hovered = card_rect.Contains(gui->mouse_pos());
                
                // Modern Shadow
                draw->shadow_rect(list_window->DrawList, card_rect.Min, card_rect.Max, 
                    draw->get_clr(ImVec4(0,0,0,0.4f)), SCALE(40), ImVec2(0, SCALE(8)), 0, SCALE(20));
                
                // Градиентный фон карточки
                ImVec4 card_bg_1 = clr->messages.background.Value;
                ImVec4 card_bg_2 = clr->window.background.Value;
                if (card_hovered) {
                    card_bg_1 = ImLerp(card_bg_1, clr->main.hover.Value, 0.3f);
                    card_bg_2 = ImLerp(card_bg_2, clr->main.hover.Value, 0.3f);
                }
                ImU32 card_col_1 = draw->get_clr(card_bg_1);
                ImU32 card_col_2 = draw->get_clr(card_bg_2);
                draw->rect_filled_multi_color(list_window->DrawList, card_rect.Min, card_rect.Max,
                    card_col_1, card_col_2, card_col_2, card_col_1, SCALE(20));
                
                // Border
                draw->rect(list_window->DrawList, card_rect.Min, card_rect.Max, 
                    draw->get_clr(clr->main.outline, 0.3f), SCALE(20), 0, SCALE(1));
                    
                // Status Badge (верхний левый угол)
                ImU32 status_col = IM_COL32(150, 150, 150, 255);
                std::string status_txt = "Scheduled";
                if (conf.status == ConferenceStatus::Active) {
                    status_col = draw->get_clr(clr->conference.recording);
                    status_txt = "LIVE";
                } else if (conf.status == ConferenceStatus::Ended) {
                    status_txt = "Ended";
                    status_col = IM_COL32(120, 120, 120, 255);
                }
                
                ImVec2 status_pos = card_rect.Min + SCALE(20, 18);
                ImVec2 status_text_size = gui->text_size(font->get(inter_bold_data, 11), status_txt.c_str());
                ImRect status_pill(status_pos, status_pos + status_text_size + SCALE(14, 8));
                
                draw->rect_filled(list_window->DrawList, status_pill.Min, status_pill.Max, 
                    draw->get_clr(ImGui::ColorConvertU32ToFloat4(status_col), 0.2f), SCALE(12));
                draw->rect(list_window->DrawList, status_pill.Min, status_pill.Max, 
                    draw->get_clr(ImGui::ColorConvertU32ToFloat4(status_col), 0.6f), SCALE(12), 0, SCALE(1));
                draw->text_clipped(list_window->DrawList, font->get(inter_bold_data, 11),
                    status_pill.Min, status_pill.Max, status_col, status_txt.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.5f));

                // Title (крупный, жирный) - если пустое, показываем понятное название
                std::string display_title = conf.settings.title;
                if (display_title.empty()) {
                    display_title = "Conference #" + std::to_string(conf.id);
                }
                ImVec2 title_pos = card_rect.Min + SCALE(20, 50);
                // Используем text_clipped чтобы текст не выходил за пределы карточки
                // Оставляем место для кнопки справа (120px кнопка + 20px отступ = 140px)
                float title_max_width = card_size.x - SCALE(160);
                draw->text_clipped(list_window->DrawList, font->get(inter_bold_data, 20),
                    title_pos, title_pos + ImVec2(title_max_width, SCALE(30)),
                    draw->get_clr(clr->main.text), display_title.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));

                // Информационная секция (дата, время, длительность, участники)
                // Отступ от названия: 36px
                // Убеждаемся, что информация помещается внутри карточки
                float title_height = SCALE(24); // Примерная высота текста title
                float info_start_y = title_pos.y + title_height + SCALE(12); // Отступ после title
                float info_x_offset = SCALE(20); // Отступ от левого края карточки
                float info_row_spacing = SCALE(22); // Расстояние между строками информации
                float info_col_spacing = SCALE(200); // Расстояние между колонками (время/длительность, участники/доступ)
                
                // Проверяем, что информация не выходит за пределы карточки
                float max_info_y = card_rect.Max.y - SCALE(24); // Минимальный отступ снизу
                float info_y = info_start_y;
                // Убеждаемся, что информация помещается
                if (info_y + info_row_spacing * 2 + SCALE(18) > max_info_y) {
                    info_y = max_info_y - info_row_spacing * 2 - SCALE(18);
                }
                
                // Правая граница для информации (оставляем место для кнопки)
                float info_right_bound = card_rect.Max.x - SCALE(140); // 120px кнопка + 20px отступ
                
                // Первая строка: Дата/время и Длительность
                // Дата и время начала
                std::string time_display = "No time set";
                if (conf.settings.start_time > 0) {
                    struct tm timeinfo;
                    localtime_s(&timeinfo, &conf.settings.start_time);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "%d %b, %H:%M", &timeinfo);
                    time_display = time_str;
                } else if (conf.created_at > 0) {
                    // Если start_time не установлен, показываем дату создания
                    struct tm timeinfo;
                    localtime_s(&timeinfo, &conf.created_at);
                    char time_str[64];
                    strftime(time_str, sizeof(time_str), "Created: %d %b", &timeinfo);
                    time_display = time_str;
                }
                
                // Дата/время (слева)
                ImVec2 time_pos = card_rect.Min + ImVec2(info_x_offset, info_y);
                ImVec2 time_max = ImVec2(ImMin(card_rect.Min.x + info_x_offset + info_col_spacing - SCALE(10), info_right_bound), info_y + SCALE(18));
                draw->text_clipped(list_window->DrawList, font->get(inter_medium_data, 13),
                    time_pos, time_max,
                    draw->get_clr(clr->main.text_secondary), time_display.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));
                
                // Длительность (справа от времени)
                std::string duration_str;
                if (conf.settings.duration_minutes == 0) {
                    duration_str = "Unlimited";
                } else if (conf.settings.duration_minutes < 60) {
                    duration_str = std::to_string(conf.settings.duration_minutes) + " min";
                } else {
                    int hours = conf.settings.duration_minutes / 60;
                    int mins = conf.settings.duration_minutes % 60;
                    duration_str = std::to_string(hours) + "h";
                    if (mins > 0) duration_str += " " + std::to_string(mins) + "m";
                }
                
                ImVec2 duration_pos = card_rect.Min + ImVec2(info_x_offset + info_col_spacing, info_y);
                ImVec2 duration_max = ImVec2(info_right_bound, info_y + SCALE(18));
                draw->text_clipped(list_window->DrawList, font->get(inter_medium_data, 13),
                    duration_pos, duration_max,
                    draw->get_clr(clr->main.text_secondary), duration_str.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));
                
                // Вторая строка: Участники и Тип доступа
                // Количество участников
                int participant_count = conf.participants.size();
                std::string participants_str = std::to_string(participant_count) + " participant" + (participant_count != 1 ? "s" : "");
                ImVec2 participants_pos = card_rect.Min + ImVec2(info_x_offset, info_y + info_row_spacing);
                ImVec2 participants_max = ImVec2(ImMin(card_rect.Min.x + info_x_offset + info_col_spacing - SCALE(10), info_right_bound), info_y + info_row_spacing + SCALE(16));
                draw->text_clipped(list_window->DrawList, font->get(inter_medium_data, 12),
                    participants_pos, participants_max,
                    draw->get_clr(clr->main.text_inactive), participants_str.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));
                
                // Тип доступа (справа от участников)
                std::string access_str;
                switch (conf.settings.access) {
                    case ConferenceAccess::Open:
                        access_str = "Public";
                        break;
                    case ConferenceAccess::InviteOnly:
                        access_str = "Invite Only";
                        break;
                    case ConferenceAccess::ApprovalRequired:
                        access_str = "Approval Required";
                        break;
                }
                ImVec2 access_pos = card_rect.Min + ImVec2(info_x_offset + info_col_spacing, info_y + info_row_spacing);
                ImVec2 access_max = ImVec2(info_right_bound, info_y + info_row_spacing + SCALE(16));
                draw->text_clipped(list_window->DrawList, font->get(inter_medium_data, 12),
                    access_pos, access_max,
                    draw->get_clr(clr->main.text_inactive), access_str.c_str(), nullptr, nullptr, ImVec2(0.0f, 0.0f));

                // Join Button (справа, по центру вертикально) с анимацией
                // Убеждаемся, что кнопка не перекрывает информацию
                if (conf.status != ConferenceStatus::Ended) {
                    struct join_btn_state
                    {
                        float glow_animation{ 0.0f };
                    };
                    
                    const ImGuiID join_btn_id = list_window->GetID(("join_btn_" + std::to_string(conf.id)).c_str());
                    join_btn_state* join_state = gui->anim_container<join_btn_state>(join_btn_id);
                    
                    ImVec2 j_size = SCALE(120, 42);
                    // Кнопка справа с отступом, по центру вертикально
                    // Учитываем, что информация заканчивается на info_y + info_row_spacing + 20 (примерно 82 + 24 + 20 = 126px)
                    // Кнопка должна быть ниже информации или справа от неё
                    float button_right_margin = SCALE(20);
                    float button_left_margin = SCALE(420); // Минимальное расстояние от левого края, чтобы не перекрывать информацию
                    float available_width = card_size.x - button_left_margin - button_right_margin;
                    
                    // Если места достаточно, размещаем справа, иначе под информацией
                    ImVec2 j_pos;
                    if (available_width >= j_size.x) {
                        // Справа от информации
                        j_pos = ImVec2(card_rect.Max.x - j_size.x - button_right_margin, 
                                     card_rect.Min.y + (card_size.y - j_size.y) / 2);
                    } else {
                        // Под информацией, справа
                        j_pos = ImVec2(card_rect.Max.x - j_size.x - button_right_margin,
                                     info_y + info_row_spacing + SCALE(8));
                    }
                    ImRect join_rect(j_pos, j_pos + j_size);
                    bool join_hovered = join_rect.Contains(gui->mouse_pos());
                    
                    // Анимация подсветки
                    float join_target_glow = join_hovered ? 1.0f : 0.0f;
                    gui->easing(join_state->glow_animation, join_target_glow, 15.f, dynamic_easing);
                    
                    // Градиентные цвета
                    ImVec4 join_grad_1 = clr->conference.primary.Value;
                    ImVec4 join_grad_2 = clr->main.accent.Value;
                    if (join_hovered) {
                        join_grad_1 = ImLerp(join_grad_1, ImVec4(join_grad_1.x * 1.2f, join_grad_1.y * 1.2f, join_grad_1.z * 1.2f, join_grad_1.w), join_state->glow_animation);
                        join_grad_2 = ImLerp(join_grad_2, ImVec4(join_grad_2.x * 1.2f, join_grad_2.y * 1.2f, join_grad_2.z * 1.2f, join_grad_2.w), join_state->glow_animation);
                    }
                    ImU32 join_col_1 = draw->get_clr(join_grad_1);
                    ImU32 join_col_2 = draw->get_clr(join_grad_2);
                    
                    if (join_hovered) {
                        draw->shadow_rect(list_window->DrawList, join_rect.Min, join_rect.Max,
                            draw->get_clr(ImVec4(0, 0, 0, 0.2f)), SCALE(12), ImVec2(0, SCALE(3)),
                            0, SCALE(10));
                    }
                    
                    // Градиентная кнопка
                    draw->rect_filled_multi_color(list_window->DrawList, join_rect.Min, join_rect.Max,
                        join_col_1, join_col_2, join_col_2, join_col_1, SCALE(10));
                    
                    // Анимированная подсветка
                    if (join_state->glow_animation > 0.01f) {
                        float time = ImGui::GetTime();
                        float glow_intensity = sinf(time * 2.0f) * 0.3f + 0.7f;
                        ImVec4 glow_color_1 = ImVec4(1.0f, 1.0f, 1.0f, 0.15f * join_state->glow_animation * glow_intensity);
                        ImVec4 glow_color_2 = ImVec4(1.0f, 1.0f, 1.0f, 0.05f * join_state->glow_animation * glow_intensity);
                        ImU32 glow_col_1 = draw->get_clr(glow_color_1);
                        ImU32 glow_col_2 = draw->get_clr(glow_color_2);
                        draw->rect_filled_multi_color(list_window->DrawList, join_rect.Min, join_rect.Max,
                            glow_col_1, glow_col_2, glow_col_2, glow_col_1, SCALE(10));
                    }
                    
                    draw->text_clipped(list_window->DrawList, font->get(inter_bold_data, 14),
                        join_rect.Min, join_rect.Max,
                        draw->get_clr(clr->button.text), "Join Call", nullptr, nullptr, ImVec2(0.5f, 0.5f));
                    
                    if (join_hovered && gui->mouse_clicked(mouse_button_left)) {
                        if (conference_manager->JoinConference(conf.id)) {
                             ui_state = ConferenceUIState::ActiveConference;
                        }
                    }
                    
                    gui->item_size(join_rect);
                }

                // ВАЖНО: Регистрируем размер всей карточки, чтобы курсор правильно обновился
                // Это гарантирует, что следующая карточка будет ниже этой
                gui->item_size(card_rect);
                
                // Обновляем курсор вручную, чтобы следующий элемент был после карточки
                list_window->DC.CursorPos = ImVec2(cur_pos.x, card_rect.Max.y + SCALE(32)); 
            }
        }
        gui->end_content();
    }
}

void c_conference_widgets::render_conference_creation_modal() {
    struct modal_anim_state
    {
        float scale_animation{ 0.0f };
        float slide_animation{ 0.0f };
        float cancel_glow{ 0.0f };
    };

    ImGuiWindow* window = gui->get_window();
    ImVec2 screen_size = SCALE(var->window.size);

    const ImGuiID modal_id = window->GetID("conference_creation_modal");
    modal_anim_state* anim_state = gui->anim_container<modal_anim_state>(modal_id);

    // Плавная анимация альфа-канала
    gui->easing(create_modal_alpha, show_create_modal ? 1.0f : 0.0f, 12.f, dynamic_easing);
    
    // Анимация масштаба с easing
    gui->easing(anim_state->scale_animation, show_create_modal ? 1.0f : 0.0f, 14.f, dynamic_easing);
    
    // Анимация слайда (появление сверху)
    gui->easing(anim_state->slide_animation, show_create_modal ? 1.0f : 0.0f, 16.f, dynamic_easing);
    
    if (create_modal_alpha < 0.001f && !show_create_modal) {
        ui_state = ConferenceUIState::ListView;
        return;
    }

    // Modern Backdrop с плавным fade
    draw->rect_filled(window->DrawList, ImVec2(0,0), screen_size, 
        draw->get_clr(ImVec4(0,0,0,0.75f), create_modal_alpha), 0);
        
    ImVec2 modal_size = SCALE(400, 520);
    ImVec2 center = screen_size * 0.5f;
    
    // Улучшенная анимация масштаба с easing
    float scale = 0.88f + (0.12f * anim_state->scale_animation);
    
    // Анимация слайда (появление сверху с небольшим отскоком)
    float slide_offset = (1.0f - anim_state->slide_animation) * SCALE(50.0f);
    float bounce = sinf(anim_state->slide_animation * 3.14159f) * SCALE(10.0f) * (1.0f - anim_state->slide_animation);
    
    ImVec2 cur_size = modal_size * scale;
    ImVec2 modal_pos = center - (cur_size * 0.5f) - ImVec2(0, slide_offset - bounce);
    ImVec2 modal_end = modal_pos + cur_size;
    
    // Modern Card Design
    draw->shadow_rect(window->DrawList, modal_pos, modal_end, 
        draw->get_clr(ImGui::ColorConvertU32ToFloat4(IM_COL32(0,0,0,100)), create_modal_alpha), 
        SCALE(60), ImVec2(0, 30), 0, 16);
        
    draw->rect_filled(window->DrawList, modal_pos, modal_end, 
        draw->get_clr(clr->window.background, create_modal_alpha), SCALE(24)); 
        
    draw->rect(window->DrawList, modal_pos, modal_end, 
        draw->get_clr(clr->main.outline, create_modal_alpha), SCALE(24), 0, 1.0f);

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) show_create_modal = false;

    // --- Header ---
    ImVec2 content_start = modal_pos + SCALE(30, 30);
    
    draw->text(window->DrawList, font->get(inter_bold_data, 24), 0,
        content_start, draw->get_clr(clr->main.text, create_modal_alpha), "New Conference");
        
    draw->text(window->DrawList, font->get(inter_medium_data, 14), 0,
        content_start + SCALE(0, 30), draw->get_clr(clr->main.text_inactive, create_modal_alpha), 
        "Setup your meeting details below");

    // --- Form ---
    float start_y = SCALE(90);
    float x_pad = SCALE(30);
    
    // Title Field
    gui->set_pos(modal_pos + ImVec2(x_pad, start_y), pos_all);
    widgets->text_field("Meeting Title", "e.g. Daily Standup", create_title, sizeof(create_title));
    start_y += SCALE(80);
    
    // Duration Selector
    gui->set_pos(modal_pos + ImVec2(x_pad, start_y), pos_all);
    static const char* durations[] = { "30 Minutes", "1 Hour", "2 Hours", "Unlimited" };
    static int dur_mins[] = { 30, 60, 120, 0 };
    if (cycle_selector("Duration", durations[create_duration_idx])) {
        create_duration_idx = (create_duration_idx + 1) % 4;
    }
    start_y += SCALE(70);
    
    // Access Selector
    gui->set_pos(modal_pos + ImVec2(x_pad, start_y), pos_all);
    static int access_idx = 0;
    static const char* access_types[] = { "Public", "Invite Only", "Password" };
    if (cycle_selector("Access", access_types[access_idx])) {
        access_idx = (access_idx + 1) % 3;
    }
    start_y += SCALE(70);
    
    if (access_idx == 2) {
         gui->set_pos(modal_pos + ImVec2(x_pad, start_y), pos_all);
         widgets->text_field("Set Password", "***", create_password, sizeof(create_password));
         start_y += SCALE(80);
    } else {
         draw->line(window->DrawList, 
            modal_pos + ImVec2(x_pad, start_y + SCALE(20)), 
            modal_pos + ImVec2(cur_size.x - x_pad, start_y + SCALE(20)),
            draw->get_clr(clr->main.outline, create_modal_alpha * 0.5f), 1.0f);
         start_y += SCALE(60); 
    }

    // --- Actions ---
    float btn_area_y = cur_size.y - SCALE(70);
    
    // Create Button
    ImVec2 create_size = SCALE(160, 44);
    ImVec2 create_pos = modal_pos + ImVec2(cur_size.x - create_size.x - x_pad, btn_area_y);
    
    gui->set_pos(create_pos, pos_all);
    if (button_text("Create Room", create_size, draw->get_clr(clr->main.accent), draw->get_clr(clr->conference.primary), false)) {
         if (std::string(create_title).empty()) {
            snprintf(create_title, sizeof(create_title), "My Meeting");
        }
        ConferenceSettings s;
        s.title = create_title;
        s.duration_minutes = dur_mins[create_duration_idx];
        s.access = (ConferenceAccess)access_idx;
        s.password = create_password;
        s.invited_user_ids = selected_participants;
        
        int id = conference_manager->CreateConference(s);
        conference_manager->JoinConference(id);
        
        show_create_modal = false;
        ui_state = ConferenceUIState::ActiveConference;
        
        memset(create_title, 0, sizeof(create_title));
        memset(create_password, 0, sizeof(create_password));
        selected_participants.clear();
        create_duration_idx = 1;
    }
    
    // Cancel Button с улучшенной анимацией
    ImVec2 cancel_pos = modal_pos + ImVec2(x_pad, btn_area_y);
    ImVec2 cancel_size = SCALE(100, 44);
    ImRect cancel_rect(cancel_pos, cancel_pos + cancel_size);
    bool cancel_hovered = cancel_rect.Contains(gui->mouse_pos());
    bool cancel_clicked = cancel_hovered && gui->mouse_clicked(mouse_button_left);
    
    if (cancel_clicked) {
         show_create_modal = false;
    }
    
    // Анимация подсветки для Cancel
    float cancel_target_glow = cancel_hovered ? 1.0f : 0.0f;
    gui->easing(anim_state->cancel_glow, cancel_target_glow, 15.f, dynamic_easing);
    
    // Градиентные цвета для Cancel
    ImVec4 cancel_grad_1 = ImVec4(0.15f, 0.15f, 0.15f, 1.0f);
    ImVec4 cancel_grad_2 = ImVec4(0.25f, 0.25f, 0.25f, 1.0f);
    
    if (cancel_hovered)
    {
        cancel_grad_1 = ImLerp(cancel_grad_1, ImVec4(0.25f, 0.25f, 0.25f, 1.0f), anim_state->cancel_glow);
        cancel_grad_2 = ImLerp(cancel_grad_2, ImVec4(0.35f, 0.35f, 0.35f, 1.0f), anim_state->cancel_glow);
    }
    
    ImU32 cancel_col_1 = draw->get_clr(cancel_grad_1, create_modal_alpha);
    ImU32 cancel_col_2 = draw->get_clr(cancel_grad_2, create_modal_alpha);
    
    // Градиентная кнопка Cancel
    draw->rect_filled_multi_color(window->DrawList, cancel_rect.Min, cancel_rect.Max,
        cancel_col_1, cancel_col_2, cancel_col_2, cancel_col_1, SCALE(8));
    
    // Анимированная подсветка
    if (anim_state->cancel_glow > 0.01f)
    {
        float time = ImGui::GetTime();
        float glow_intensity = sinf(time * 2.0f) * 0.2f + 0.8f;
        ImVec4 glow_color = ImVec4(1.0f, 1.0f, 1.0f, 0.06f * anim_state->cancel_glow * glow_intensity * create_modal_alpha);
        ImU32 glow_col = draw->get_clr(glow_color);
        draw->rect_filled_multi_color(window->DrawList, cancel_rect.Min, cancel_rect.Max,
            glow_col, IM_COL32(255, 255, 255, 0), IM_COL32(255, 255, 255, 0), glow_col, SCALE(8));
    }
    
    // Текст кнопки Cancel
    ImU32 cancel_text_color = draw->get_clr(cancel_hovered ? clr->main.text : clr->main.text_inactive, create_modal_alpha);
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 14),
        cancel_rect.Min, cancel_rect.Max,
        cancel_text_color, "Cancel", nullptr, nullptr, ImVec2(0.5f, 0.5f));
    
    gui->item_size(cancel_rect);
}

void c_conference_widgets::render_active_conference() {
    ImGuiWindow* window = gui->get_window();
    Conference* conf = conference_manager->GetConference(conference_manager->current_conference_id);
    if (!conf) {
        ui_state = ConferenceUIState::ListView;
        return;
    }
    
    ImVec2 screen_size = SCALE(var->window.size);

    float target_sidebar_w = (show_participants_panel || show_chat_panel) ? 320.0f : 0.0f;
    // OPTIMIZATION: Faster animation speed (60 -> 120)
    gui->easing(participants_panel_width, target_sidebar_w, gui->fixed_speed(120.0f), dynamic_easing);
    
    float sidebar_w = participants_panel_width;
    ImVec2 main_area_size = ImVec2(screen_size.x - SCALE(sidebar_w), screen_size.y);
    
    draw->rect_filled(window->DrawList, ImVec2(0,0), main_area_size, IM_COL32(10, 10, 12, 255), 0);
    
    render_video_grid(conf, ImVec2(0,0), main_area_size);
    
    float bar_h = SCALE(80);
    render_control_panel(conf, ImVec2(0, screen_size.y - bar_h - SCALE(10)), ImVec2(main_area_size.x, bar_h));

    if (sidebar_w > 1.0f) {
        ImVec2 side_pos = ImVec2(screen_size.x - SCALE(sidebar_w), 0);
        ImVec2 side_size = ImVec2(SCALE(sidebar_w), screen_size.y);
        
        draw->push_clip_rect(window->DrawList, side_pos, side_pos + side_size, true);
        
        draw->rect_filled(window->DrawList, side_pos, side_pos + side_size, draw->get_clr(clr->window.background), 0);
        draw->rect(window->DrawList, side_pos, side_pos + ImVec2(1, side_size.y), draw->get_clr(clr->main.outline), 0);
        
        if (show_participants_panel) render_participants_panel(conf, side_pos, side_size);
        else if (show_chat_panel) render_conference_chat(conf, side_pos, side_size);
            
        draw->pop_clip_rect(window->DrawList);
    }
    
    if (show_reaction_popup) render_reaction_popup(conf);
}

void c_conference_widgets::render_video_grid(Conference* conf, const ImVec2& pos, const ImVec2& size) {
    int count = conf->participants.size();
    if (count == 0) return;
    
    int cols = ceil(sqrt(count));
    int rows = ceil((float)count / cols);
    if (count <= 2 && size.x > size.y) { cols = count; rows = 1; }
    else if (count <= 4 && count > 2) { cols = 2; rows = 2; }
    
    ImVec2 cell_size = ImVec2(size.x / cols, size.y / rows);
    
    int idx = 0;
    for (const auto& pair : conf->participants) {
        int r = idx / cols;
        int c = idx % cols;
        
        ImVec2 p_pos = pos + ImVec2(c * cell_size.x, r * cell_size.y);
        float gap = SCALE(8);
        ImVec2 tile_pos = p_pos + ImVec2(gap, gap);
        ImVec2 tile_size = cell_size - ImVec2(gap*2, gap*2);
        
        if (tile_size.x < 1.0f) tile_size.x = 1.0f;
        if (tile_size.y < 1.0f) tile_size.y = 1.0f;
        
        render_video_tile(pair.second, tile_pos, tile_size);
        idx++;
    }
}

void c_conference_widgets::render_video_tile(const ConferenceParticipant& p, const ImVec2& pos, const ImVec2& size) {
    ImGuiWindow* window = gui->get_window();
    
    // OPTIMIZATION: Check clip rect overlaps before doing any work
    if (!window->ClipRect.Overlaps(ImRect(pos, pos + size))) return;

    draw->push_clip_rect(window->DrawList, pos, pos + size, true);

    FakeVideoStream fs = conference_manager->GetFakeStream(p.user_id);
    
    draw_user_avatar_bg(window->DrawList, pos, pos + size, p.user_id);
    
    ImVec2 center = pos + size * 0.5f;

    if (fs.has_camera && fs.is_active) {
         draw->rect_filled(window->DrawList, pos, pos + size, IM_COL32(0,0,0,100), 0);
         // OPTIMIZATION: Reduced segments (64->32)
         draw->circle_filled(window->DrawList, center - ImVec2(0, size.y * 0.1f), size.y * 0.18f, IM_COL32(255,220,190,255), 32);
         draw->circle_filled(window->DrawList, center + ImVec2(0, size.y * 0.25f), size.y * 0.28f, IM_COL32(100,100,120,255), 32);
    } else {
        float avatar_rad = (std::min)(size.x, size.y) * 0.3f;
        if (avatar_rad < 1.0f) avatar_rad = 1.0f;
        
        // OPTIMIZATION: Skip expensive shadow if not speaking or small
        if (p.is_speaking && avatar_rad > SCALE(20)) {
            draw->shadow_circle(window->DrawList, center, avatar_rad, draw->get_clr(ImGui::ColorConvertU32ToFloat4(IM_COL32(0,0,0,60))), SCALE(30), ImVec2(0, 5), 0, 12);
        }
        
        // OPTIMIZATION: Reduced segments (48/64 -> 32)
        draw->circle_filled(window->DrawList, center, avatar_rad, IM_COL32(255,255,255,30), 32); 
        
        draw->text_clipped(window->DrawList, font->get(inter_bold_data, avatar_rad),
            pos, pos + size, IM_COL32(255,255,255,230), fs.avatar_text.c_str(), 
            nullptr, nullptr, ImVec2(0.5f, 0.5f));
    }
    
    if (p.is_speaking) {
         float alpha = 0.5f + 0.5f * sin(ImGui::GetTime() * 8.0f);
         // OPTIMIZATION: Skip rounding calc as it's implied by draw function
         draw->rect(window->DrawList, pos, pos + size, draw->get_clr(clr->conference.speaker_glow, alpha), SCALE(12), 0, SCALE(4));
    }

    ImVec2 tag_pos = pos + ImVec2(SCALE(12), size.y - SCALE(40));
    if (tag_pos.y < pos.y) tag_pos.y = pos.y;

    // OPTIMIZATION: Use stack buffer instead of allocations
    char name_buf[64];
    if (p.user_id == conference_manager->current_user_id)
        snprintf(name_buf, 64, "User %d (You)", p.user_id);
    else
        snprintf(name_buf, 64, "User %d", p.user_id);

    ImVec2 name_sz = ImGui::CalcTextSize(name_buf);
    ImVec2 tag_end = tag_pos + name_sz + SCALE(20, 14);
    
    draw->rect_filled(window->DrawList, tag_pos, tag_end, IM_COL32(0,0,0,140), SCALE(16));
    draw->text(window->DrawList, font->get(inter_bold_data, 12), 0, tag_pos + SCALE(10, 2), IM_COL32(255,255,255,255), name_buf);
    
    if (!p.microphone_enabled) {
        ImVec2 icon_pos = pos + ImVec2(size.x - SCALE(30), SCALE(10));
        // Low fidelity circle
        draw->circle_filled(window->DrawList, icon_pos + SCALE(10,10), SCALE(14), IM_COL32(220, 50, 50, 200), 16);
        draw->line(window->DrawList, icon_pos + SCALE(4, 4), icon_pos + SCALE(16, 16), IM_COL32(255,255,255,255), 2.0f);
    }
    
    for (const auto& r : p.active_reactions) {
        ImVec2 r_pos = pos + size * 0.5f - ImVec2(0, size.y * 0.2f);
        draw->text_clipped(window->DrawList, font->get(inter_bold_data, 32),
            r_pos, r_pos + SCALE(40,40), IM_COL32(255,255,255,255), r.c_str(), nullptr, nullptr, ImVec2(0.5f,0.5f));
    }

    draw->pop_clip_rect(window->DrawList);
}

void c_conference_widgets::render_control_panel(Conference* conf, const ImVec2& pos, const ImVec2& size) {
    ImGuiWindow* window = gui->get_window();
    
    float dock_width = SCALE(550);
    ImVec2 center_pos = pos + ImVec2((size.x - dock_width) * 0.5f, 0);
    ImVec2 dock_size = ImVec2(dock_width, SCALE(70));
    
    // OPTIMIZATION: One solid rect instead of complex shadow for performance
    draw->rect_filled(window->DrawList, center_pos, center_pos + dock_size, IM_COL32(30, 30, 35, 240), SCALE(35));
    draw->rect(window->DrawList, center_pos, center_pos + dock_size, IM_COL32(255,255,255,30), SCALE(35), 0, 1.0f);
    
    // OPTIMIZATION: Static arrays to eliminate vector allocation
    static const char* icons[] = {"W", "K", "S", "R", "J", "T", "X"}; // W=Microphone, K=Camera, S=Share, R=Record, J=Reactions, T=Chat, X=Leave 
    
    const int btn_count = 7;
    float btn_dim = SCALE(46);
    float gap = SCALE(15);
    float start_x = center_pos.x + (dock_size.x - (btn_count * btn_dim + (btn_count - 1) * gap)) * 0.5f;
    float start_y = center_pos.y + (dock_size.y - btn_dim) * 0.5f;
    
    for (int i = 0; i < btn_count; i++) {
        ImVec2 b_pos(start_x + i * (btn_dim + gap), start_y);
        
        gui->set_screen_pos(b_pos, pos_all); 
        
        bool active = false;
        bool danger = false;
        
        // Fast switch instead of repeated string comparisons
        switch(i) {
            case 0: active = !conf->participants[conference_manager->current_user_id].microphone_enabled; break; // W=Microphone
            case 1: active = !conf->participants[conference_manager->current_user_id].camera_enabled; break; // K=Camera
            case 2: break; // S=Share (TODO)
            case 3: active = conf->is_recording; danger = active; break; // R=Record
            case 4: active = show_reaction_popup; break; // J=Reactions
            case 5: active = show_chat_panel; break; // T=Chat
            case 6: danger = true; break; // X=Leave
        }
        
        ImU32 col_active = draw->get_clr(clr->conference.primary);
        ImU32 col_inact = IM_COL32(60,60,65,255);
        
        bool clicked = false;
        gui->push_id(i);
        if (button_icon(icons[i], btn_dim * 0.5f, col_inact, col_active, active, danger)) {
            clicked = true;
        }
        gui->pop_id();
        
        if (clicked) {
            switch(i) {
                case 0: conference_manager->ToggleMicrophone(conf->id); break;
                case 1: conference_manager->ToggleCamera(conf->id); break;
                case 2: /* Share */ break;
                case 3: conference_manager->ToggleRecording(conf->id); break;
                case 4: show_reaction_popup = !show_reaction_popup; break;
                case 5: show_chat_panel = !show_chat_panel; if(show_chat_panel) show_participants_panel=false; break;
                case 6: conference_manager->LeaveConference(conf->id); ui_state = ConferenceUIState::ListView; return;
            }
        }
    }
}

void c_conference_widgets::render_participants_panel(Conference* conf, const ImVec2& pos, const ImVec2& size) {
    if (size.x < SCALE(150)) return; // Don't render content during small widths (animation start)

    ImGuiWindow* window = gui->get_window();
    
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 18),
        pos + SCALE(20, 20), pos + SCALE(size.x, 60),
        draw->get_clr(clr->main.text), "Participants", nullptr, nullptr, ImVec2(0, 0.5f));
        
    float y_start = SCALE(70);
    
    for (const auto& pair : conf->participants) {
        ImVec2 row_pos = pos + ImVec2(0, y_start);
        ImVec2 row_size(size.x, SCALE(50));
        ImRect row_rect(row_pos, row_pos + row_size);
        
        bool hovered = row_rect.Contains(gui->mouse_pos());
        if (hovered) {
            draw->rect_filled(window->DrawList, row_rect.Min, row_rect.Max, draw->get_clr(clr->main.outline, 0.3f), 0);
        }
        
        ImVec2 av_center = row_pos + SCALE(30, 25);
        draw->circle_filled(window->DrawList, av_center, SCALE(16), IM_COL32(100, 100, 150, 255), 32); 
        
        draw->text(window->DrawList, font->get(inter_medium_data, 14), 0,
            row_pos + SCALE(60, 15), draw->get_clr(clr->main.text), 
            (std::string("User ") + std::to_string(pair.first)).c_str());
            
        if (pair.second.role == UserRole::Host) {
             draw->text(window->DrawList, font->get(inter_medium_data, 10), 0,
                row_pos + SCALE(60, 32), draw->get_clr(clr->main.accent), "Host");
        }
        
        if (!pair.second.microphone_enabled) {
            draw->circle_filled(window->DrawList, row_rect.Max - SCALE(30, 25), SCALE(8), IM_COL32(220,50,50,255), 32);
        }
        
        y_start += SCALE(50);
    }
}

void c_conference_widgets::render_conference_chat(Conference* conf, const ImVec2& pos, const ImVec2& size) {
    if (size.x < SCALE(150)) return; // Safety guard: Don't render chat when panel is too narrow (animation)

    ImGuiWindow* window = gui->get_window();
    
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 18),
        pos + SCALE(20, 20), pos + SCALE(size.x, 60),
        draw->get_clr(clr->main.text), "Conference Chat", nullptr, nullptr, ImVec2(0, 0.5f));
        
    float input_h = SCALE(60);
    ImVec2 msg_size(size.x, size.y - SCALE(80) - input_h);
    ImVec2 msg_pos = pos + SCALE(0, 80);
    
    gui->set_pos(msg_pos, pos_all);
    gui->begin_content("conf_msg_list", msg_size, SCALE(0,0), SCALE(0, 10));
    {
        ImGuiWindow* cw = gui->get_window();
        
        for (const auto& msg : conf->messages) {
            ImVec2 m_cursor = cw->DC.CursorPos;
            bool is_me = (msg.sender_id == conference_manager->current_user_id);
            bool is_sys = msg.is_system;
            
            float wrap_w = size.x - SCALE(60);
            if (wrap_w < SCALE(50)) wrap_w = SCALE(50); // Ensure minimal wrap width

            gui->push_font(font->get(inter_medium_data, 13));
            ImVec2 txt_sz = ImGui::CalcTextSize(msg.text.c_str(), nullptr, false, wrap_w);
            gui->pop_font();
            
            float pad_x = SCALE(12);
            float pad_y = SCALE(8);
            ImVec2 bubble_sz = txt_sz + ImVec2(pad_x * 2, pad_y * 2);
            
            float x_pos = is_me ? (size.x - bubble_sz.x - SCALE(20)) : SCALE(20);
            if (is_sys) x_pos = (size.x - bubble_sz.x) * 0.5f;
            
            ImVec2 b_min = ImVec2(m_cursor.x + x_pos, m_cursor.y + SCALE(5));
            ImVec2 b_max = b_min + bubble_sz;
            
            ImU32 b_col = is_me ? draw->get_clr(clr->conference.primary, 0.8f) : draw->get_clr(clr->main.outline);
            if (is_sys) b_col = draw->get_clr(ImGui::ColorConvertU32ToFloat4(IM_COL32(100,100,100,50)));
            
            draw->rect_filled(cw->DrawList, b_min, b_max, b_col, SCALE(12), is_me ? ImDrawFlags_RoundCornersTopLeft : ImDrawFlags_RoundCornersTopRight);
            
            ImU32 t_col = is_sys ? draw->get_clr(clr->main.text_inactive) : IM_COL32(255,255,255,255);
            draw->text_clipped(cw->DrawList, font->get(inter_medium_data, 13),
                b_min, b_max, t_col, msg.text.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.5f));
            
            ImGui::SetCursorScreenPos(ImVec2(m_cursor.x, b_max.y + SCALE(5)));
        }
    }
    gui->end_content();
    
    ImVec2 inp_pos = pos + ImVec2(SCALE(20), size.y - input_h);
    gui->set_pos(inp_pos, pos_all); 
    
    bool enter = false;
    widgets->text_field("Type a message...", "M", chat_input, sizeof(chat_input), &enter);
    
    if (enter && strlen(chat_input) > 0) {
         conference_manager->SendChatMessage(conf->id, chat_input);
         memset(chat_input, 0, sizeof(chat_input));
    }
}

void c_conference_widgets::render_reaction_popup(Conference* conf) {
    ImGuiWindow* window = gui->get_window();
    ImVec2 screen_size = SCALE(var->window.size);
    ImVec2 pop_size = SCALE(320, 64);
    ImVec2 pop_pos = ImVec2((screen_size.x - pop_size.x) * 0.5f, screen_size.y - SCALE(160));
    
    // Optimized shadow
    draw->shadow_rect(window->DrawList, pop_pos, pop_pos + pop_size, IM_COL32(0,0,0,50), SCALE(30), ImVec2(0,5), 0, 16);
    draw->rect_filled(window->DrawList, pop_pos, pop_pos + pop_size, 
        draw->get_clr(clr->window.background), SCALE(32));
    draw->rect(window->DrawList, pop_pos, pop_pos + pop_size, draw->get_clr(clr->main.outline), SCALE(32), 0, 1.0f);
        
    static const char* emojis[] = {"👍", "👏", "😄", "🎉", "❤️", "😂"};
    const int count = 6;
    float x_start = pop_pos.x + SCALE(15);
    float y_start = pop_pos.y;
    
    float btn_dim = SCALE(48);
    
    for (int i = 0; i < count; i++) {
        ImVec2 e_pos(x_start + i * SCALE(48), y_start + SCALE(8)); 
        
        gui->set_screen_pos(e_pos, pos_all);
        gui->push_id(i + 100);
        
        ImRect bb(e_pos, e_pos + ImVec2(btn_dim, btn_dim));
        gui->item_size(bb);
        bool h, held, p;
        if (gui->item_add(bb, window->GetID(emojis[i]))) {
            p = gui->button_behavior(bb, window->GetID(emojis[i]), &h, &held, 0);
            
            button_state* st = gui->anim_container<button_state>(window->GetID(emojis[i]));
            if (st->scale < 0.01f) st->scale = 1.0f; 

            // Fast animation
            gui->easing(st->scale, h ? 1.5f : 1.0f, gui->fixed_speed(80.0f), dynamic_easing);
            
            // OPTIMIZATION: Static font size for emojis too
            float font_sz = 24.0f; 

            draw->text_clipped(window->DrawList, font->get(inter_bold_data, font_sz),
                bb.Min, bb.Max, IM_COL32(255,255,255,255), 
                emojis[i], nullptr, nullptr, ImVec2(0.5f, 0.5f));
            
            if (p) {
                 conference_manager->SendReaction(conf->id, emojis[i]);
                 show_reaction_popup = false;
            }
        }
        gui->pop_id();
    }
}

void c_conference_widgets::render_emoji_floating(const ImVec2& pos, const std::string& emoji, float progress) {
}

void c_conference_widgets::render_waiting_room() {
}
