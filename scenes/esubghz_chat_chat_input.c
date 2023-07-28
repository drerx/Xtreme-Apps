#include "../esubghz_chat_i.h"

/* If no message was entred this simply emits a MsgEntered event to the scene
 * manager to switch to the text box. If a message was entered it is appended
 * to the name string. The result is encrypted, if encryption is enabled, and
 * then copied into the TX buffer. The contents of the TX buffer are then
 * transmitted. The sent message is appended to the text box and a MsgEntered
 * event is sent to the scene manager to switch to the text box view. */
static void chat_input_cb(void *context)
{
	furi_assert(context);
	ESubGhzChatState* state = context;

	/* no message, just switch to the text box view */
#ifdef FW_ORIGIN_Official
	if (strcmp(state->text_input_store, " ") == 0) {
#else /* FW_ORIGIN_Official */
	if (strlen(state->text_input_store) == 0) {
#endif /* FW_ORIGIN_Official */
		scene_manager_handle_custom_event(state->scene_manager,
				ESubGhzChatEvent_MsgEntered);
		return;
	}

	/* concatenate the name prefix and the actual message */
	furi_string_set(state->msg_input, state->name_prefix);
	furi_string_cat_str(state->msg_input, ": ");
	furi_string_cat_str(state->msg_input, state->text_input_store);

	/* append the message to the chat box */
	furi_string_cat_printf(state->chat_box_store, "\n%s",
		furi_string_get_cstr(state->msg_input));

	/* encrypt and transmit message */
	tx_msg_input(state);

	/* clear message input buffer */
	furi_string_set_char(state->msg_input, 0, 0);

	/* switch to text box view */
	scene_manager_handle_custom_event(state->scene_manager,
			ESubGhzChatEvent_MsgEntered);
}

/* Prepares the message input scene. */
void scene_on_enter_chat_input(void* context)
{
	FURI_LOG_T(APPLICATION_NAME, "scene_on_enter_chat_input");

	furi_assert(context);
	ESubGhzChatState* state = context;

	state->text_input_store[0] = 0;
	text_input_reset(state->text_input);
	text_input_set_result_callback(
			state->text_input,
			chat_input_cb,
			state,
			state->text_input_store,
			sizeof(state->text_input_store),
			true);
	text_input_set_validator(
			state->text_input,
			NULL,
			NULL);
	text_input_set_header_text(
			state->text_input,
#ifdef FW_ORIGIN_Official
			"Message (space for none)");
#else /* FW_ORIGIN_Official */
			"Message");
	text_input_set_minimum_length(state->text_input, 0);
#endif /* FW_ORIGIN_Official */

	view_dispatcher_switch_to_view(state->view_dispatcher, ESubGhzChatView_Input);
}

/* Handles scene manager events for the message input scene. */
bool scene_on_event_chat_input(void* context, SceneManagerEvent event)
{
	FURI_LOG_T(APPLICATION_NAME, "scene_on_event_chat_input");

	furi_assert(context);
	ESubGhzChatState* state = context;

	bool consumed = false;

	switch(event.type) {
	case SceneManagerEventTypeCustom:
		switch(event.event) {
		/* switch to text box scene */
		case ESubGhzChatEvent_MsgEntered:
			scene_manager_next_scene(state->scene_manager,
					ESubGhzChatScene_ChatBox);
			consumed = true;
			break;
		}
		break;

	case SceneManagerEventTypeBack:
		/* stop the application and send a leave message if the user
		 * presses back here */

		/* concatenate the name prefix and leave message */
		furi_string_set(state->msg_input, state->name_prefix);
		furi_string_cat_str(state->msg_input, " left chat.");

		/* encrypt and transmit message */
		tx_msg_input(state);

		/* clear message input buffer */
		furi_string_set_char(state->msg_input, 0, 0);

		/* wait for leave message to be delivered */
                furi_delay_ms(CHAT_LEAVE_DELAY);

		view_dispatcher_stop(state->view_dispatcher);
		consumed = true;
		break;

	default:
		consumed = false;
		break;
	}

	return consumed;
}

/* Cleans up the password input scene. */
void scene_on_exit_chat_input(void* context)
{
	FURI_LOG_T(APPLICATION_NAME, "scene_on_exit_chat_input");

	furi_assert(context);
	ESubGhzChatState* state = context;

	text_input_reset(state->text_input);
}
