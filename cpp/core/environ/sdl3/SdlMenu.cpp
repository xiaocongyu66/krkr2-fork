#include "MenuItemImpl.h"
#include <SDL3/SDL.h>

#include <string>
#include <vector>

void TVPShowPopMenu(tTJSNI_MenuItem *menu) {
    if(!menu)
        return;

    ttstr title;
    menu->GetCaption(title);
    std::vector<std::string> captions;
    std::vector<SDL_MessageBoxButtonData> buttons;
    std::vector<tTJSNI_MenuItem *> selectableItems;
    const auto &children = menu->GetChildren();
    for(auto *child : children) {
        auto *item = static_cast<tTJSNI_MenuItem *>(child);
        ttstr caption;
        item->GetCaption(caption);
        if(caption.IsEmpty() || caption == TJS_W("+") || !item->GetVisible() ||
           !item->GetEnabled() || !item->GetChildren().empty())
            continue;
        captions.emplace_back(caption.AsStdString());
        selectableItems.push_back(item);
        buttons.push_back({ SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,
                            static_cast<int>(captions.size() - 1),
                            captions.back().c_str() });
    }
    if(buttons.empty())
        return;

    // SDL owns no button labels, so keep caption storage alive through the
    // call.
    for(size_t i = 0; i < buttons.size(); ++i)
        buttons[i].text = captions[i].c_str();
    SDL_MessageBoxData data{};
    const std::string menuTitle =
        title.IsEmpty() ? "Menu" : title.AsStdString();
    data.flags = SDL_MESSAGEBOX_INFORMATION;
    data.title = menuTitle.c_str();
    data.message = "Select a menu item";
    data.numbuttons = static_cast<int>(buttons.size());
    data.buttons = buttons.data();
    int buttonId = -1;
    if(SDL_ShowMessageBox(&data, &buttonId) && buttonId >= 0 &&
       static_cast<size_t>(buttonId) < buttons.size()) {
        // Button IDs correspond to the child pointer saved alongside its label.
        selectableItems[static_cast<size_t>(buttonId)]->OnClick();
    }
}
