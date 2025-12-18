#pragma once
#include "imgui.h"
#include <memory>

// ============================================================================
// MODERN DESIGN SYSTEM - Color Palette
// Inspired by Discord, Slack, Telegram Desktop
// ============================================================================

class c_colors
{
public:
	// ========================================================================
	// PRIMARY COLORS - Main application background
	// ========================================================================
	struct
	{
		ImColor background{ 30, 31, 34 };           // Main window background (Discord-like dark gray)
		ImColor background_secondary{ 25, 26, 29 }; // Secondary surfaces
		ImColor background_tertiary{ 20, 21, 24 };  // Tertiary surfaces
	} window;

	// ========================================================================
	// SEMANTIC COLORS - Primary UI elements
	// ========================================================================
	struct
	{
		// Primary accent - Modern blue-purple gradient
		ImColor accent{ 88, 101, 242 };            // Discord blurple - primary action color
		ImColor accent_hover{ 105, 116, 255 };     // Lighter on hover
		ImColor accent_active{ 71, 82, 196 };      // Darker when active
		
		// Gradient colors for buttons and highlights
		ImColor grad_1{ 88, 101, 242 };            // Start of gradient
		ImColor grad_2{ 114, 137, 218 };           // End of gradient
		
		// Text colors
		ImColor text{ 219, 222, 225 };             // Primary text (high contrast)
		ImColor text_secondary{ 181, 186, 193 };   // Secondary text
		ImColor text_inactive{ 114, 118, 125 };    // Inactive/disabled text
		ImColor text_muted{ 79, 84, 92 };         // Muted text (metadata)
		
		// Status colors
		ImColor success{ 67, 181, 129 };           // Success/online status
		ImColor warning{ 250, 166, 26 };           // Warning
		ImColor error{ 237, 66, 69 };              // Error/danger
		ImColor info{ 88, 101, 242 };              // Info (same as accent)
		
		// Borders and dividers
		ImColor outline{ 47, 49, 54 };             // Subtle borders
		ImColor outline_hover{ 66, 70, 78 };       // Borders on hover
		ImColor divider{ 40, 42, 46 };             // Dividers between sections
		
		// Interactive states
		ImColor hover{ 40, 42, 46 };               // Hover background
		ImColor active{ 47, 49, 54 };              // Active/pressed state
		ImColor selected{ 57, 60, 67 };            // Selected item background
		
		// Legacy colors for backward compatibility
		ImColor black{ 0, 0, 0 };                  // Black color
		ImColor red{ 237, 66, 69 };                 // Red color (error)
	} main;

	// ========================================================================
	// TOP BAR / HEADER COLORS
	// ========================================================================
	struct
	{
		ImColor background{ 32, 34, 37 };          // Header background
		ImColor background_hover{ 37, 39, 42 };    // Header on hover
		ImColor border{ 47, 49, 54 };              // Bottom border
		
		// Legacy colors for backward compatibility
		ImColor col{ 29, 30, 35 };                 // Top bar color
		ImColor col_start{ 29, 30, 35 };          // Top bar gradient start
		ImColor col_end{ 22, 23, 27 };            // Top bar gradient end
		ImColor product_line_col_1{ 56, 61, 78 };  // Product line color
	} top_bar;

	// ========================================================================
	// INPUT FIELD COLORS
	// ========================================================================
	struct
	{
		ImColor background{ 40, 42, 46 };         // Input background
		ImColor background_focus{ 48, 50, 55 };    // Input when focused
		ImColor border{ 47, 49, 54 };              // Input border
		ImColor border_focus{ 88, 101, 242 };      // Border when focused (accent)
		ImColor placeholder{ 114, 118, 125 };       // Placeholder text
		ImColor icon_background{ 47, 49, 54 };     // Icon container background
	} textfield;

	// ========================================================================
	// BUTTON COLORS
	// ========================================================================
	struct
	{
		ImColor primary{ 88, 101, 242 };           // Primary button
		ImColor primary_hover{ 105, 116, 255 };    // Primary hover
		ImColor primary_active{ 71, 82, 196 };     // Primary active
		
		ImColor secondary{ 47, 49, 54 };          // Secondary button
		ImColor secondary_hover{ 57, 60, 67 };     // Secondary hover
		ImColor secondary_active{ 40, 42, 46 };    // Secondary active
		
		ImColor outline{ 47, 49, 54 };             // Outline button border
		ImColor outline_hover{ 66, 70, 78 };      // Outline hover
		
		ImColor text{ 219, 222, 225 };            // Button text
		ImColor text_disabled{ 114, 118, 125 };    // Disabled button text
		
		// Legacy colors for backward compatibility
		ImColor log_outline{ 32, 34, 42 };        // Log outline color
		ImColor card_outline{ 51, 55, 67 };       // Card outline color
	} button;

	// ========================================================================
	// CARD / PANEL COLORS
	// ========================================================================
	struct
	{
		ImColor background{ 32, 34, 37 };          // Card background
		ImColor background_hover{ 37, 39, 42 };    // Card hover
		ImColor border{ 47, 49, 54 };              // Card border
		ImColor shadow{ 0, 0, 0, 100 };           // Card shadow (with alpha)
		ImColor highlight{ 88, 101, 242, 20 };     // Subtle highlight
		
		// Legacy colors for backward compatibility
		ImColor blur_bg{ 31, 33, 38 };            // Blur background
		ImColor on_update{ 159, 183, 255 };       // On update color
	} cards;

	// ========================================================================
	// MESSAGE / CHAT COLORS
	// ========================================================================
	struct
	{
		ImColor background{ 30, 31, 34 };          // Message area background
		ImColor bubble_own{ 88, 101, 242 };        // Own message bubble
		ImColor bubble_own_hover{ 105, 116, 255 };  // Own message hover
		ImColor bubble_other{ 40, 42, 46 };        // Other message bubble
		ImColor bubble_other_hover{ 47, 49, 54 };  // Other message hover
		ImColor bubble_system{ 47, 49, 54 };      // System message bubble
		ImColor bubble_text_own{ 255, 255, 255 }; // Text in own message
		ImColor bubble_text_other{ 219, 222, 225 }; // Text in other message
		ImColor bubble_meta{ 181, 186, 193 };      // Timestamp/metadata
		ImColor divider{ 40, 42, 46 };             // Message divider
		ImColor line_background{ 40, 42, 46 };    // Line between messages
	} messages;

	// ========================================================================
	// CONFERENCE / VOICE COLORS
	// ========================================================================
	struct
	{
		ImColor primary{ 88, 101, 242, 255 };       // Primary conference color
		ImColor background{ 25, 26, 29, 255 };     // Conference background
		ImColor speaker_glow{ 88, 101, 242, 100 }; // Speaking indicator glow
		ImColor recording{ 237, 66, 69, 255 };     // Recording indicator
		ImColor reaction_popup{ 40, 42, 46, 240 }; // Reaction popup background
		ImColor host_border{ 250, 166, 26, 255 };  // Host border (gold)
		ImColor cohost_border{ 88, 101, 242, 255 }; // Co-host border (accent)
		ImColor participant_border{ 114, 118, 125, 255 }; // Participant border
		ImColor muted{ 114, 118, 125, 255 };       // Muted participant
		ImColor speaking{ 67, 181, 129, 255 };     // Speaking participant
	} conference;

	// ========================================================================
	// SCROLLBAR COLORS
	// ========================================================================
	struct
	{
		ImColor track{ 30, 31, 34 };                // Scrollbar track
		ImColor thumb{ 47, 49, 54 };               // Scrollbar thumb
		ImColor thumb_hover{ 57, 60, 67 };         // Scrollbar thumb hover
		ImColor thumb_active{ 66, 70, 78 };        // Scrollbar thumb active
	} scrollbar;

	// ========================================================================
	// MODAL / DIALOG COLORS
	// ========================================================================
	struct
	{
		ImColor background{ 32, 34, 37, 250 };     // Modal background (slightly transparent)
		ImColor overlay{ 0, 0, 0, 180 };           // Overlay behind modal
		ImColor border{ 47, 49, 54 };              // Modal border
		ImColor header{ 37, 39, 42 };               // Modal header
	} modal;
};

inline std::unique_ptr<c_colors> clr = std::make_unique<c_colors>();
