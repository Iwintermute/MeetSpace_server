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
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 14),
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

    draw->text_clipped(window->DrawList, font->get(inter_bold_data, font_sz),
        total.Min, total.Max, IM_COL32(255,255,255,255), icon, nullptr, nullptr, ImVec2(0.5f, 0.5f));
        
    return pressed;
}

// Custom Cycle Selector (Modern)
static bool cycle_selector(const char* label, const char* current_val) {
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
    
    button_state* state = gui->anim_container<button_state>(id);
    // OPTIMIZATION: Faster animation speed (30 -> 60)
    gui->easing(state->alpha, hovered ? 1.0f : 0.0f, gui->fixed_speed(60.0f), dynamic_easing);
    
    // Background
    ImU32 bg = draw->get_clr(clr->messages.background);
    ImU32 bg_hover = draw->get_clr(clr->main.outline);
    
    ImVec4 c1 = ImGui::ColorConvertU32ToFloat4(bg);
    ImVec4 c2 = ImGui::ColorConvertU32ToFloat4(bg_hover);
    ImVec4 c_lerp = ImLerp(c1, c2, state->alpha);
    ImU32 final_bg = draw->get_clr(c_lerp);
    
    draw->rect_filled(window->DrawList, rect.Min, rect.Max, final_bg, SCALE(12));
        
    // Label
    draw->text_clipped(window->DrawList, font->get(inter_medium_data, 13),
        rect.Min + SCALE(15, 0), rect.Max, draw->get_clr(clr->main.text_inactive), 
        label, nullptr, nullptr, ImVec2(0, 0.5f));
        
    // Value
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 15),
        rect.Min, rect.Max - SCALE(40, 0), draw->get_clr(clr->main.text), 
        current_val, nullptr, nullptr, ImVec2(1, 0.5f));
        
    // Arrow
    draw->text_clipped(window->DrawList, font->get(inter_bold_data, 12),
        rect.Max - SCALE(30, 0), rect.Max, draw->get_clr(clr->main.text_inactive), 
        ">", nullptr, nullptr, ImVec2(0, 0.5f));
    
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
        if (button_text("<", SCALE(40, 40), draw->get_clr(clr->window.background), draw->get_clr(clr->main.outline), true)) {
            app_state->current_screen = AppScreen::MainMessenger;
        }
    }

    // Title
    ImVec2 title_pos = content_pos + SCALE(85, 38);
    draw->text(window->DrawList, font->get(inter_bold_data, 28), 0, title_pos, 
        draw->get_clr(clr->main.text), "Conferences");
    draw->text(window->DrawList, font->get(inter_medium_data, 14), 0, title_pos + SCALE(0, 32), 
        draw->get_clr(clr->main.text_inactive), "Manage your meetings and video calls");

    // "Create" Button
    ImVec2 btn_size = SCALE(180, 44);
    ImVec2 btn_pos = content_pos + ImVec2(screen_size.x - btn_size.x - SCALE(40), SCALE(40));
    
    if (interactable) {
        gui->set_pos(btn_pos, pos_all);
        if (button_text("+ New Conference", btn_size, draw->get_clr(clr->main.accent), draw->get_clr(clr->conference.primary), false)) {
             ui_state = ConferenceUIState::CreationModal;
            show_create_modal = true;
            create_modal_alpha = 0.0f;
        }
    }

    // --- List Area ---
    if (interactable) {
        ImVec2 list_pos = content_pos + ImVec2(0, header_height);
        ImVec2 list_size = content_size - ImVec2(0, header_height);
        
        gui->begin_content("conf_list", list_size, SCALE(0, 0), SCALE(0, 20));
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
                ImVec2 cur_pos = list_window->DC.CursorPos;
                ImVec2 card_size = ImVec2(list_size.x - padding_x * 2, SCALE(100));
                
                cur_pos.x += padding_x; 
                ImRect card_rect(cur_pos, cur_pos + card_size);
                
                // Optimized Shadow (16 segments, reduced size)
                draw->shadow_rect(list_window->DrawList, card_rect.Min, card_rect.Max, 
                    draw->get_clr(ImGui::ColorConvertU32ToFloat4(IM_COL32(0,0,0,50))), SCALE(30), ImVec2(0, SCALE(5)), 0, 16);
                draw->rect_filled(list_window->DrawList, card_rect.Min, card_rect.Max, 
                    draw->get_clr(clr->messages.background), SCALE(16));
                    
                // Status
                ImU32 status_col = IM_COL32(100, 100, 100, 255);
                std::string status_txt = "Scheduled";
                if (conf.status == ConferenceStatus::Active) {
                    status_col = draw->get_clr(clr->conference.recording);
                    status_txt = "LIVE";
                } else if (conf.status == ConferenceStatus::Ended) {
                    status_txt = "Ended";
                }
                
                ImVec2 status_pos = card_rect.Min + SCALE(25, 25);
                ImVec2 ts = ImGui::CalcTextSize(status_txt.c_str());
                ImRect status_pill(status_pos, status_pos + ts + SCALE(16, 8));
                
                draw->rect_filled(list_window->DrawList, status_pill.Min, status_pill.Max, draw->get_clr(ImGui::ColorConvertU32ToFloat4(status_col), 0.15f), SCALE(20));
                draw->rect(list_window->DrawList, status_pill.Min, status_pill.Max, draw->get_clr(ImGui::ColorConvertU32ToFloat4(status_col), 0.5f), SCALE(20));
                draw->text_clipped(list_window->DrawList, font->get(inter_bold_data, 11),
                    status_pill.Min, status_pill.Max, status_col, status_txt.c_str(), nullptr, nullptr, ImVec2(0.5f, 0.5f));

                // Title
                draw->text(list_window->DrawList, font->get(inter_bold_data, 18), 0, 
                    card_rect.Min + SCALE(25, 60), draw->get_clr(clr->main.text), conf.settings.title.c_str());

                // Join Button
                if (conf.status != ConferenceStatus::Ended) {
                    ImVec2 j_size = SCALE(110, 36);
                    ImVec2 j_pos = card_rect.Max - j_size - SCALE(25, 0);
                    j_pos.y = card_rect.Min.y + (card_size.y - j_size.y) / 2;
                    
                    gui->set_screen_pos(j_pos, pos_all);
                    
                    gui->push_id(conf.id);
                    if (button_text("Join Call", j_size, draw->get_clr(clr->conference.primary), draw->get_clr(clr->conference.speaker_glow), false)) {
                        if (conference_manager->JoinConference(conf.id)) {
                             ui_state = ConferenceUIState::ActiveConference;
                        }
                    }
                    gui->pop_id();
                }

                // FIX: Reduced item spacing (120 -> 108)
                gui->dummy(ImVec2(list_size.x, SCALE(108))); 
            }
        }
        gui->end_content();
    }
}

void c_conference_widgets::render_conference_creation_modal() {
    ImGuiWindow* window = gui->get_window();
    ImVec2 screen_size = SCALE(var->window.size);

    gui->easing(create_modal_alpha, show_create_modal ? 1.0f : 0.0f, gui->fixed_speed(160.0f), static_easing);
    
    if (create_modal_alpha < 0.001f && !show_create_modal) {
        ui_state = ConferenceUIState::ListView;
        return;
    }

    // Modern Backdrop
    draw->rect_filled(window->DrawList, ImVec2(0,0), screen_size, 
        draw->get_clr(ImVec4(0,0,0,0.7f), create_modal_alpha), 0);
        
    ImVec2 modal_size = SCALE(400, 520);
    ImVec2 center = screen_size * 0.5f;
    float anim_scale = 0.95f + (0.05f * create_modal_alpha); 
    
    ImVec2 cur_size = modal_size * anim_scale;
    ImVec2 modal_pos = center - (cur_size * 0.5f);
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
    
    // Cancel Button
    ImVec2 cancel_pos = modal_pos + ImVec2(x_pad, btn_area_y);
    ImVec2 cancel_size = SCALE(100, 44);
    gui->set_pos(cancel_pos, pos_all);
    
    if (button_text("Cancel", cancel_size, IM_COL32(0,0,0,0), draw->get_clr(clr->main.text_inactive, 0.1f), false)) {
         show_create_modal = false;
    }
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
    static const char* icons[] = {"M", "C", "S", "R", "E", "T", "X"}; 
    
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
            case 0: active = !conf->participants[conference_manager->current_user_id].microphone_enabled; break; // M
            case 1: active = !conf->participants[conference_manager->current_user_id].camera_enabled; break; // C
            case 2: break; // S (Share, TODO)
            case 3: active = conf->is_recording; danger = active; break; // R
            case 4: active = show_reaction_popup; break; // E (Reaction)
            case 5: active = show_chat_panel; break; // T (Chat)
            case 6: danger = true; break; // X (Leave)
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
