
/// WEB UI Components
/// This offers a small set of methods to create Web UI Components to be used for the Meshcom UI

#include "web_UIComponents.h"



/// #######################################################################################################################################
/// @brief Creates a default button with Confirm Dialog
/// @param target Pointer to an Instance of CommonWebClient (or anything else that inverits "Print")
/// @param onClickHandler An ECMAScript routine that is put into the "onclick"-handler.
/// @param buttonCaption The Caption that is put between <button> </button> ... can be anything HTML allows.
/// @param confirmationText A brief text hat is displayed for a confirmation.

void uic_button(CommonWebClient* target, char onClickHandler[], char buttonCaption[], char confirmationText[]){
    target->printf("<button onclick=\"if(confirm('%s'))", confirmationText);
    target->printf("%s\">", onClickHandler);
    target->printf("%s</button>\n", buttonCaption);
}

/// #######################################################################################################################################
/// @brief Creates a default button
/// @param target Pointer to an Instance of CommonWebClient (or anything else that inverits "Print")
/// @param onClickHandler An ECMAScript routine that is put into the "onclick"-handler.
/// @param buttonCaption The Caption that is put between <button> </button> ... can be anything HTML allows.
void uic_button(CommonWebClient* target, char onClickHandler[], char buttonCaption[]){
    target->printf("<button onclick=\"%s\">", onClickHandler);
    target->printf("%s</button>\n", buttonCaption);
}

/// #######################################################################################################################################
/// @brief Creates a simple textinput element
/// @param target Pointer to an Instance of CommonWebClient (or anything else that inverits "Print")
/// @param id The DOM ID so this element cam be adressed within the DOM
/// @param onChangeHandler An ECMAScript routine that is put into the "onclick"-handler.
/// @param value The predefined Value of this Input Element
void uic_input(CommonWebClient* target, char id[], char onChangeHandler[], char value[]) {
    target->printf("<input type=\"text\" id=\"%s\" onchange=\"%s\" value=\"%s\">\n" ,  id, onChangeHandler, value);
}
