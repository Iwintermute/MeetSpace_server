#pragma once
#include "includes.h"
#include "conference_state.h"
#include "conference_manager.h"
#include <memory>

class c_conference_widgets
{
public:
    // Main entry point
    void render_conference_ui();

    // Sub-screens
    void render_conference_list(bool interactable = true);
    void render_conference_creation_modal();
    void render_active_conference();
    void render_waiting_room();

    // Components of active conference
    void render_video_grid(Conference* conf, const ImVec2& pos, const ImVec2& size);
    void render_control_panel(Conference* conf, const ImVec2& pos, const ImVec2& size);
    void render_participants_panel(Conference* conf, const ImVec2& pos, const ImVec2& size);
    void render_conference_chat(Conference* conf, const ImVec2& pos, const ImVec2& size);
    
    // Small widgets
    void render_video_tile(const ConferenceParticipant& p, const ImVec2& pos, const ImVec2& size);
    void render_reaction_popup(Conference* conf);
    void render_emoji_floating(const ImVec2& pos, const std::string& emoji, float progress);
    
    // Helper state
    ConferenceUIState ui_state = ConferenceUIState::ListView;
    
    // UI state variables
    char create_title[64] = "";
    char create_password[64] = ""; // Password
    std::vector<int> selected_participants; // For creation
    char create_desc[128] = "";
    int create_duration_idx = 1; // 1 hour default
    bool show_create_modal = false;
    float create_modal_alpha = 0.0f;
    
    bool show_participants_panel = false;
    float participants_panel_width = 0.0f;
    
    bool show_chat_panel = false;
    float chat_panel_width = 0.0f;

    bool show_reaction_popup = false;
    char chat_input[256] = "";

private:
     ImVec2 get_grid_dimensions(int count, const ImVec2& available_size);
};

inline std::unique_ptr<c_conference_widgets> conference_widgets = std::make_unique<c_conference_widgets>();
