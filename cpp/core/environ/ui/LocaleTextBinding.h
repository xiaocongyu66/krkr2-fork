#pragma once

#include <string>

namespace cocos2d::ui {
    class Text;
    class Button;
} // namespace cocos2d::ui

//---------------------------------------------------------------------------
// LocaleConfigManager 旧 initText 的 UI 胶水（B2-3）。
// LocaleConfigManager 已移入 core_environ_module，不能携带 Cocos UI 类型；
// ui::Text / ui::Button 的本地化赋值留在 core_environ_cocos 这一侧。
// 单参重载把控件当前文本当作 tid；命中翻译时把控件文本置为翻译并返回
// true，查不到 tid 时把 tid 原样显示、标红并返回 false，与旧行为一致。
//---------------------------------------------------------------------------
bool TVPInitLocaleText(cocos2d::ui::Text *ctrl);
bool TVPInitLocaleText(cocos2d::ui::Button *ctrl);
bool TVPInitLocaleText(cocos2d::ui::Text *ctrl, const std::string &tid);
bool TVPInitLocaleText(cocos2d::ui::Button *ctrl, const std::string &tid);
