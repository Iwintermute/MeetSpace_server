#include "headers/includes.h"
#include "headers/conference_widgets.h"

// ============================================================================
// ГЛАВНЫЙ РЕНДЕР КОНФЕРЕНЦИЙ
// ============================================================================
void c_conference_widgets::render_conference_ui() {
    
    // Switch between different conference states
    if (ui_state == ConferenceUIState::ListView) {
        render_conference_list();
    }
    else if (ui_state == ConferenceUIState::CreationModal) {
        // Render list in background
        render_conference_list(false); 
        
        // Render modal on top
        render_conference_creation_modal();
    }
    else if (ui_state == ConferenceUIState::ActiveConference) {
        render_active_conference();
    }
    else if (ui_state == ConferenceUIState::WaitingRoom) {
        render_waiting_room();
    }
    else {
        // Fallback
        ui_state = ConferenceUIState::ListView;
        render_conference_list();
    }

    // Process fake data updates for demonstration
    if (conference_manager->current_conference_id != -1) {
        conference_manager->UpdateFakeData(ImGui::GetIO().DeltaTime);
    }
}
